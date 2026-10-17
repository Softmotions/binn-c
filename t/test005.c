// Large containers, streaming serialization, round trips and nesting limits.
#include "../binnc.h"
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

static bool stream_consumer(struct binn_val val, void *op) {
  struct stream_acc *a = (struct stream_acc*) op;
  if (!val.buf && !val.size) {
    a->final_calls++;
    return true;
  }
  BINN_ASSERT(a->len + val.size <= sizeof(a->data));
  memcpy(a->data + a->len, val.buf, val.size);
  a->len += val.size;
  return true;
}

struct abort_acc {
  int calls;
  int final_calls;
};

static bool abort_consumer(struct binn_val val, void *op) {
  struct abort_acc *a = (struct abort_acc*) op;
  if (!val.buf && !val.size) {
    a->final_calls++;
    return true;
  }
  a->calls++;
  return false;
}

int main(int argc, char *argv[]) {
  // A list with more than 127 elements uses a four byte count parameter.
  {
    struct binn *list = binn_create_list(0);
    struct binn_val buf;
    size_t i;
    BINN_ASSERT(list != 0);
    for (i = 0; i < 200; i++) {
      struct binn *v = make_u8(0);
      BINN_ASSERT(binn_add(list, *binnv(v), 0));
      binn_destroy(v);
    }
    BINN_ASSERT(list->count == 200);
    BINN_ASSERT(list->size == 409);
    buf = binn_serialize_allocated(list);
    BINN_ASSERT(buf.buf != 0);
    if (buf.buf) {
      static const uint8_t header[] = {
        0xE0, 0x80, 0x00, 0x01, 0x99, // total size 409
        0x80, 0x00, 0x00, 0xC8        // count 200
      };
      BINN_ASSERT(buf.size == 409);
      BINN_ASSERT_MEM(buf.buf, header, sizeof(header));
      BINN_ASSERT(((uint8_t*) buf.buf)[9] == 0x20 && ((uint8_t*) buf.buf)[10] == 0x00);
      BINN_ASSERT(((uint8_t*) buf.buf)[407] == 0x20 && ((uint8_t*) buf.buf)[408] == 0x00);
      free(buf.buf);
    }
    binn_destroy(list);
  }

  // A list with more than 127 elements survives a wrap round trip.
  {
    struct binn *list = binn_create_list(0);
    struct binn holder;
    struct binn *w;
    struct binn_val buf;
    bool casted = true;
    size_t i;
    for (i = 0; i < 200; i++) {
      struct binn *v = make_u8((uint8_t) i);
      BINN_ASSERT(binn_add(list, *binnv(v), 0));
      binn_destroy(v);
    }
    buf = binn_serialize_allocated(list);
    BINN_ASSERT(buf.buf != 0);
    w = binn_from(&holder, buf, &casted);
    BINN_ASSERT(w != 0 && casted == false);
    BINN_ASSERT(w->count == 200);
    {
      struct binn found;
      BINN_ASSERT(binn_indexed_get(w, 127, &found));
      BINN_ASSERT(found.val.nval == 127);
      BINN_ASSERT(binn_indexed_get(w, 199, &found));
      BINN_ASSERT(found.val.nval == 199);
    }
    BINN_ASSERT(binn_destroy(w) == false);
    free(buf.buf);
    binn_destroy(list);
  }

  // Streaming serialization matches the allocated buffer.
  {
    struct binn *obj = binn_create_object(0);
    struct binn *inner = binn_create_list(0);
    struct binn *v;
    struct binn_val buf;
    struct stream_acc acc;
    memset(&acc, 0, sizeof(acc));

    v = make_u8(1);
    BINN_ASSERT(binn_add(inner, *binnv(v), 0));
    binn_destroy(v);
    v = make_text("value");
    BINN_ASSERT(binn_add(inner, *binnv(v), 0));
    binn_destroy(v);
    BINN_ASSERT(binn_add_keyed(obj, "list", *binnv(inner)));
    binn_destroy(inner);
    v = make_text("stream");
    BINN_ASSERT(binn_add_keyed(obj, "name", *binnv(v)));
    binn_destroy(v);

    buf = binn_serialize_allocated(obj);
    BINN_ASSERT(buf.buf != 0);
    BINN_ASSERT(binn_serialize(obj, stream_consumer, &acc));
    BINN_ASSERT(acc.final_calls == 1);
    BINN_ASSERT(acc.len == obj->size);
    if (buf.buf) {
      BINN_ASSERT_MEM(acc.data, buf.buf, obj->size);
      free(buf.buf);
    }
    binn_destroy(obj);
  }

  // A consumer returning false aborts serialization before the end-of-stream call.
  {
    struct abort_acc acc;
    struct binn *obj = binn_create_object(0);
    struct binn *v = make_text("value");
    memset(&acc, 0, sizeof(acc));
    BINN_ASSERT(binn_add_keyed(obj, "k", *binnv(v)));
    binn_destroy(v);
    BINN_ASSERT(binn_serialize(obj, abort_consumer, &acc) == false);
    BINN_ASSERT(acc.calls == 1);
    BINN_ASSERT(acc.final_calls == 0);
    binn_destroy(obj);
  }

  // Serialize -> wrap -> serialize produces identical bytes.
  {
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
    BINN_ASSERT(binn_add(inner, *binnv(v), &idx));
    binn_destroy(v);
    v = make_text("round");
    BINN_ASSERT(binn_add(inner, *binnv(v), 0));
    binn_destroy(v);
    BINN_ASSERT(binn_add_keyed(obj, "items", *binnv(inner)));
    binn_destroy(inner);

    buf1 = binn_serialize_allocated(obj);
    BINN_ASSERT(buf1.buf != 0);
    w = binn_from(&holder, buf1, &casted);
    BINN_ASSERT(w != 0);
    buf2 = binn_serialize_allocated(w);
    BINN_ASSERT(buf2.buf != 0);
    if (buf1.buf && buf2.buf) {
      BINN_ASSERT(obj->size == w->size);
      BINN_ASSERT_MEM(buf1.buf, buf2.buf, obj->size);
    }
    free(buf2.buf);
    binn_destroy(w);
    free(buf1.buf);
    binn_destroy(obj);
  }

  // The reader must accept non minimal (four byte) size/count encodings.
  {
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
    BINN_ASSERT(w != 0 && casted == false);
    BINN_ASSERT(w->type == BINN_TYPE_TEXT);
    BINN_ASSERT(w->used == 2 && memcmp(w->val.bval, "hi", 2) == 0);
    // `size` is canonical even if the input used a four byte size parameter.
    BINN_ASSERT(w->size == 5 && w->size == binn_serialized_size(w));
    BINN_ASSERT(binn_destroy(w) == false);

    w = binn_from(&holder, bv(wide_list, sizeof(wide_list)), &casted);
    BINN_ASSERT(w != 0 && casted == false);
    BINN_ASSERT(w->count == 1);
    BINN_ASSERT(w->size == sizeof(wide_list)); // wrapped containers keep the exact input size
    BINN_ASSERT(binn_iter_init(*binnv(w), &it));
    BINN_ASSERT(binn_iter_next(&it));
    BINN_ASSERT(it.binn.val.nval == 42);
    BINN_ASSERT(!binn_iter_next(&it));
    binn_iter_close(&it);
    BINN_ASSERT(binn_indexed_get(w, 0, &found));
    BINN_ASSERT(found.val.nval == 42);
    BINN_ASSERT(binn_destroy(w) == false);

    // Copies of non minimal inputs are reserialized canonically, so `size` shrinks accordingly.
    {
      struct binn *cs = binn_copy_from(0, bv(wide_string, sizeof(wide_string)));
      struct binn *cl = binn_copy_from(0, bv(wide_list, sizeof(wide_list)));
      BINN_ASSERT(cs != 0 && cs->size == 5 && cs->size == binn_serialized_size(cs));
      BINN_ASSERT(cl != 0 && cl->size == 5 && cl->size == binn_serialized_size(cl));
      BINN_ASSERT(cs->size == 5);
      binn_destroy(cs);
      binn_destroy(cl);
    }
  }

  // binn_serialized_size() matches the allocated buffer.
  {
    struct binn *b;
    struct binn_val ser;
    BINN_ASSERT(binn_serialized_size(0) == 0);

    b = binn_create_u64c(0, 0x1226652FEull);
    BINN_ASSERT(binn_serialized_size(b) == b->size && binn_serialized_size(b) == 9);
    ser = binn_serialize_allocated(b);
    BINN_ASSERT(ser.size == binn_serialized_size(b));
    free(ser.buf);
    binn_destroy(b);

    b = binn_create_text(0, "hello", -1);
    BINN_ASSERT(binn_serialized_size(b) == 8);
    binn_destroy(b);

    b = binn_create_list(0);
    BINN_ASSERT(binn_serialized_size(b) == 3);
    {
      struct binn *v = binn_create_u64c(0, 1);
      BINN_ASSERT(binn_add(b, *binnv(v), 0));
      binn_destroy(v);
    }
    BINN_ASSERT(binn_serialized_size(b) == 5); // header + uint8
    ser = binn_serialize_allocated(b);
    BINN_ASSERT(ser.size == binn_serialized_size(b));
    {
      struct binn holder;
      struct binn *w = binn_from(&holder, ser, 0);
      BINN_ASSERT(w != 0);
      BINN_ASSERT(binn_serialized_size(w) == ser.size);
      binn_destroy(w);
    }
    free(ser.buf);
    binn_destroy(b);
  }

  // Nesting is limited by BINN_MAX_NESTING_LEVELS.
  {
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
    BINN_ASSERT(built < 300);
    BINN_ASSERT(built > 250);
    binn_destroy(inner);
  }

  return binn_assertions_failed > 0 ? 1 : 0;
}
