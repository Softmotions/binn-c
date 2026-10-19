// Large containers, streaming serialization, round trips and nesting limits.
#define IW_TEST_HARNESS "binnc"
#include "tests.h"

static struct binn* make_u8(uint8_t v) {
  struct binn_create_spec spec;
  memset(&spec, 0, sizeof(spec));
  spec.type = BINN_TYPE_UINT8;
  spec.value = bv(&v, sizeof(v));
  return binn_create_value(0, &spec);
}

static struct binn* make_text(const char *s) {
  struct binn_create_spec spec;
  memset(&spec, 0, sizeof(spec));
  spec.type = BINN_TYPE_TEXT;
  spec.value = bv(s, strlen(s));
  return binn_create_value(0, &spec);
}

struct stream_acc {
  uint8_t data[4096];
  size_t  len;
  int     final_calls;
};

static bool stream_consumer(const uint8_t *buf, size_t len, void *op) {
  struct stream_acc *a = (struct stream_acc*) op;
  if (!buf && !len) {
    a->final_calls++;
    return true;
  }
  IW_TEST_ASSERT(a->len + len <= sizeof(a->data));
  memcpy(a->data + a->len, buf, len);
  a->len += len;
  return true;
}

struct abort_acc {
  int calls;
  int final_calls;
};

static bool abort_consumer(const uint8_t *buf, size_t len, void *op) {
  struct abort_acc *a = (struct abort_acc*) op;
  if (!buf && !len) {
    a->final_calls++;
    return true;
  }
  a->calls++;
  return false;
}

// A list with more than 127 elements uses a four byte count parameter.
static void test_list_wide_count(void) {
  struct binn *list = binn_create_list(0);
  struct binn_val buf;
  size_t i;
  IW_TEST_ASSERT_PTR_NOT_NULL(list);
  for (i = 0; i < 200; i++) {
    struct binn *v = make_u8(0);
    IW_TEST_ASSERT_TRUE(binn_add(list, *binnv(v), 0));
    binn_destroy(v);
  }
  IW_TEST_ASSERT_EQUAL(list->count, 200);
  IW_TEST_ASSERT_EQUAL(list->size, 409);
  buf = binn_serialize_allocated(list);
  IW_TEST_ASSERT_PTR_NOT_NULL(buf.buf);
  if (buf.buf) {
    static const uint8_t header[] = {
      0xE0, 0x80, 0x00, 0x01, 0x99, // total size 409
      0x80, 0x00, 0x00, 0xC8        // count 200
    };
    IW_TEST_ASSERT_EQUAL(buf.size, 409);
    IW_TEST_ASSERT_MEM_EQUAL(buf.buf, header, sizeof(header));
    IW_TEST_ASSERT(((uint8_t*) buf.buf)[9] == 0x20 && ((uint8_t*) buf.buf)[10] == 0x00);
    IW_TEST_ASSERT(((uint8_t*) buf.buf)[407] == 0x20 && ((uint8_t*) buf.buf)[408] == 0x00);
    free(buf.buf);
  }
  binn_destroy(list);
}

// A list with more than 127 elements survives a wrap round trip.
static void test_list_wide_roundtrip(void) {
  struct binn *list = binn_create_list(0);
  struct binn holder;
  struct binn *w;
  struct binn_val buf;
  bool casted = true;
  size_t i;
  for (i = 0; i < 200; i++) {
    struct binn *v = make_u8((uint8_t) i);
    IW_TEST_ASSERT_TRUE(binn_add(list, *binnv(v), 0));
    binn_destroy(v);
  }
  buf = binn_serialize_allocated(list);
  IW_TEST_ASSERT_PTR_NOT_NULL(buf.buf);
  w = binn_from(&holder, buf, &casted);
  IW_TEST_ASSERT(w != 0 && casted == false);
  IW_TEST_ASSERT_EQUAL(w->count, 200);
  {
    struct binn found;
    IW_TEST_ASSERT_TRUE(binn_indexed_get(w, 127, &found));
    IW_TEST_ASSERT_EQUAL(found.val.nval, 127);
    IW_TEST_ASSERT_TRUE(binn_indexed_get(w, 199, &found));
    IW_TEST_ASSERT_EQUAL(found.val.nval, 199);
  }
  IW_TEST_ASSERT_FALSE(binn_destroy(w));
  free(buf.buf);
  binn_destroy(list);
}

// Streaming serialization matches the allocated buffer.
static void test_stream_serialize(void) {
  struct binn *obj = binn_create_object(0);
  struct binn *inner = binn_create_list(0);
  struct binn *v;
  struct binn_val buf;
  struct stream_acc acc;
  memset(&acc, 0, sizeof(acc));

  v = make_u8(1);
  IW_TEST_ASSERT_TRUE(binn_add(inner, *binnv(v), 0));
  binn_destroy(v);
  v = make_text("value");
  IW_TEST_ASSERT_TRUE(binn_add(inner, *binnv(v), 0));
  binn_destroy(v);
  IW_TEST_ASSERT_TRUE(binn_add_keyed(obj, "list", *binnv(inner)));
  binn_destroy(inner);
  v = make_text("stream");
  IW_TEST_ASSERT_TRUE(binn_add_keyed(obj, "name", *binnv(v)));
  binn_destroy(v);

  buf = binn_serialize_allocated(obj);
  IW_TEST_ASSERT_PTR_NOT_NULL(buf.buf);
  IW_TEST_ASSERT_TRUE(binn_serialize(obj, stream_consumer, &acc));
  IW_TEST_ASSERT_EQUAL(acc.final_calls, 1);
  IW_TEST_ASSERT_EQUAL(acc.len, obj->size);
  if (buf.buf) {
    IW_TEST_ASSERT_MEM_EQUAL(acc.data, buf.buf, obj->size);
    free(buf.buf);
  }
  binn_destroy(obj);
}

// A consumer returning false aborts serialization before the end-of-stream call.
static void test_stream_abort(void) {
  struct abort_acc acc;
  struct binn *obj = binn_create_object(0);
  struct binn *v = make_text("value");
  memset(&acc, 0, sizeof(acc));
  IW_TEST_ASSERT_TRUE(binn_add_keyed(obj, "k", *binnv(v)));
  binn_destroy(v);
  IW_TEST_ASSERT_FALSE(binn_serialize(obj, abort_consumer, &acc));
  IW_TEST_ASSERT_EQUAL(acc.calls, 1);
  IW_TEST_ASSERT_EQUAL(acc.final_calls, 0);
  binn_destroy(obj);
}

// Serialize -> wrap -> serialize produces identical bytes.
static void test_wrap_roundtrip(void) {
  struct binn *obj = binn_create_object(0);
  struct binn *inner = binn_create_list(0);
  struct binn *v;
  struct binn_val buf1;
  struct binn_val buf2;
  struct binn *w;
  struct binn holder;
  bool casted = true;
  int32_t idx;

  v = make_u8(7);
  IW_TEST_ASSERT_TRUE(binn_add(inner, *binnv(v), &idx));
  binn_destroy(v);
  v = make_text("round");
  IW_TEST_ASSERT_TRUE(binn_add(inner, *binnv(v), 0));
  binn_destroy(v);
  IW_TEST_ASSERT_TRUE(binn_add_keyed(obj, "items", *binnv(inner)));
  binn_destroy(inner);

  buf1 = binn_serialize_allocated(obj);
  IW_TEST_ASSERT_PTR_NOT_NULL(buf1.buf);
  w = binn_from(&holder, buf1, &casted);
  IW_TEST_ASSERT_PTR_NOT_NULL(w);
  buf2 = binn_serialize_allocated(w);
  IW_TEST_ASSERT_PTR_NOT_NULL(buf2.buf);
  if (buf1.buf && buf2.buf) {
    IW_TEST_ASSERT_EQUAL(obj->size, w->size);
    IW_TEST_ASSERT_MEM_EQUAL(buf1.buf, buf2.buf, obj->size);
  }
  free(buf2.buf);
  binn_destroy(w);
  free(buf1.buf);
  binn_destroy(obj);
}

// The reader must accept non minimal (four byte) size/count encodings.
static void test_non_minimal_encodings(void) {
  // A short string stored with a four byte size parameter.
  static uint8_t wide_string[] = {
    0xA0, 0x80, 0x00, 0x00, 0x02, 'h', 'i', 0x00
  };
  // A one element list stored with four byte size and count parameters.
  static uint8_t wide_list[] = {
    0xE0, 0x80, 0x00, 0x00, 0x0B, // size 11
    0x80, 0x00, 0x00, 0x01,       // count 1
    0x20, 0x2A                    // uint8 42
  };
  struct binn holder;
  struct binn *w;
  struct binn found;
  struct binn_iter it;
  bool casted = true;

  w = binn_from(&holder, bv(wide_string, sizeof(wide_string)), &casted);
  IW_TEST_ASSERT(w != 0 && casted == false);
  IW_TEST_ASSERT_EQUAL(w->type, BINN_TYPE_TEXT);
  IW_TEST_ASSERT(w->used == 2 && memcmp(w->val.bval, "hi", 2) == 0);
  // `size` is canonical even if the input used a four byte size parameter.
  IW_TEST_ASSERT(w->size == 5 && w->size == binn_serialized_size(w));
  IW_TEST_ASSERT_FALSE(binn_destroy(w));

  w = binn_from(&holder, bv(wide_list, sizeof(wide_list)), &casted);
  IW_TEST_ASSERT(w != 0 && casted == false);
  IW_TEST_ASSERT_EQUAL(w->count, 1);
  IW_TEST_ASSERT_EQUAL(w->size, sizeof(wide_list)); // wrapped containers keep the exact input size
  IW_TEST_ASSERT_TRUE(binn_iter_init(*binnv(w), &it));
  IW_TEST_ASSERT_TRUE(binn_iter_next(&it));
  IW_TEST_ASSERT_EQUAL(it.binn.val.nval, 42);
  IW_TEST_ASSERT_FALSE(binn_iter_next(&it));
  binn_iter_close(&it);
  IW_TEST_ASSERT_TRUE(binn_indexed_get(w, 0, &found));
  IW_TEST_ASSERT_EQUAL(found.val.nval, 42);
  IW_TEST_ASSERT_FALSE(binn_destroy(w));

  // Copies of non minimal inputs are reserialized canonically, so `size` shrinks accordingly.
  {
    struct binn *cs = binn_copy_from(0, bv(wide_string, sizeof(wide_string)));
    struct binn *cl = binn_copy_from(0, bv(wide_list, sizeof(wide_list)));
    IW_TEST_ASSERT(cs != 0 && cs->size == 5 && cs->size == binn_serialized_size(cs));
    IW_TEST_ASSERT(cl != 0 && cl->size == 5 && cl->size == binn_serialized_size(cl));
    IW_TEST_ASSERT_EQUAL(cs->size, 5);
    binn_destroy(cs);
    binn_destroy(cl);
  }
}

static void test_serialized_size(void) {
  struct binn *b;
  struct binn_val ser;
  IW_TEST_ASSERT_EQUAL(binn_serialized_size(0), 0);

  b = binn_create_u64c(0, 0x1226652FEull);
  IW_TEST_ASSERT(binn_serialized_size(b) == b->size && binn_serialized_size(b) == 9);
  ser = binn_serialize_allocated(b);
  IW_TEST_ASSERT_EQUAL(ser.size, binn_serialized_size(b));
  free(ser.buf);
  binn_destroy(b);

  b = binn_create_text(0, "hello", -1);
  IW_TEST_ASSERT_EQUAL(binn_serialized_size(b), 8);
  binn_destroy(b);

  b = binn_create_list(0);
  IW_TEST_ASSERT_EQUAL(binn_serialized_size(b), 3);
  {
    struct binn *v = binn_create_u64c(0, 1);
    IW_TEST_ASSERT_TRUE(binn_add(b, *binnv(v), 0));
    binn_destroy(v);
  }
  IW_TEST_ASSERT_EQUAL(binn_serialized_size(b), 5); // header + uint8
  ser = binn_serialize_allocated(b);
  IW_TEST_ASSERT_EQUAL(ser.size, binn_serialized_size(b));
  {
    struct binn holder;
    struct binn *w = binn_from(&holder, ser, 0);
    IW_TEST_ASSERT_PTR_NOT_NULL(w);
    IW_TEST_ASSERT_EQUAL(binn_serialized_size(w), ser.size);
    binn_destroy(w);
  }
  free(ser.buf);
  binn_destroy(b);
}

// Nesting is limited by BINN_MAX_NESTING_LEVELS.
static void test_nesting_limit(void) {
  struct binn *inner = binn_create_list(0);
  int built = 0;
  int i;
  for (i = 0; i < 300; i++) {
    struct binn *outer = binn_create_list(0);
    if (!binn_add(outer, *binnv(inner), 0)) {
      binn_destroy(outer);
      break;
    }
    binn_destroy(inner);
    inner = outer;
    built++;
  }
  IW_TEST_ASSERT(built < 300);
  IW_TEST_ASSERT(built > 250);
  binn_destroy(inner);
}

int main(int argc, char *argv[]) {
  static const struct iw_test_case cases[] = {
    IW_TEST_CASE(test_list_wide_count),
    IW_TEST_CASE(test_list_wide_roundtrip),
    IW_TEST_CASE(test_stream_serialize),
    IW_TEST_CASE(test_stream_abort),
    IW_TEST_CASE(test_wrap_roundtrip),
    IW_TEST_CASE(test_non_minimal_encodings),
    IW_TEST_CASE(test_serialized_size),
    IW_TEST_CASE(test_nesting_limit),
  };
  return iw_test_run(argc, argv, IW_TEST_SUITE("large_and_streaming", cases, 0, 0));
}
