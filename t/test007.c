// Scalar accessors: binn_blob_get(), binn_text_get(), binn_u64_get(), binn_i64_get(),
// binn_f64_get(), binn_bool_get().
#define IW_TEST_HARNESS "binnc"
#include <math.h>
#include "tests.h"

static struct binn* mk(binn_type_t type, const void *p, size_t n) {
  struct binn_create_spec spec;
  memset(&spec, 0, sizeof(spec));
  spec.type = type;
  spec.value = bv(p, n);
  return binn_create_value(0, &spec);
}

// binn_blob_get(): string storage and blobs.
static void test_blob_get(void) {
  struct binn *b = binn_create_text(0, "hello", -1);
  struct binn_val v = binn_blob_get(b);
  IW_TEST_ASSERT(v.buf != 0 && v.size == 5 && memcmp(v.buf, "hello", 5) == 0);
  binn_destroy(b);

  {
    uint8_t bytes[3] = { 0x00, 0x01, 0x02 };
    b = binn_create_blob(0, bytes, sizeof(bytes));
    v = binn_blob_get(b);
    IW_TEST_ASSERT(v.buf != 0 && v.size == 3 && memcmp(v.buf, bytes, 3) == 0);
    binn_destroy(b);
  }

  b = binn_create_text(0, 0, 0);
  v = binn_blob_get(b);
  IW_TEST_ASSERT(v.buf == 0 && v.size == 0);
  binn_destroy(b);

  b = binn_create_u64c(0, 5);
  v = binn_blob_get(b);
  IW_TEST_ASSERT(v.buf == 0 && v.size == 0);
  binn_destroy(b);

  b = binn_create_list(0);
  v = binn_blob_get(b);
  IW_TEST_ASSERT(v.buf == 0 && v.size == 0);
  binn_destroy(b);

  v = binn_blob_get(0);
  IW_TEST_ASSERT(v.buf == 0 && v.size == 0);

  // Any string sub-type, not only BINN_TYPE_TEXT.
  b = mk(BINN_TYPE_DATETIME, "2026-10-15 00:00:00", 19);
  v = binn_blob_get(b);
  IW_TEST_ASSERT(v.size == 19 && memcmp(v.buf, "2026-10-15 00:00:00", 19) == 0);
  binn_destroy(b);

  // Wrapped buffer: the payload points into the serialized binn.
  {
    struct binn holder;
    struct binn *w;
    b = binn_create_text(0, "wrapped", -1);
    {
      struct binn_val ser = binn_serialize_allocated(b);
      IW_TEST_ASSERT_PTR_NOT_NULL(ser.buf);
      binn_destroy(b);
      w = binn_from(&holder, ser, 0);
      IW_TEST_ASSERT_PTR_NOT_NULL(w);
      v = binn_blob_get(w);
      IW_TEST_ASSERT(v.buf != 0 && v.size == 7 && memcmp(v.buf, "wrapped", 7) == 0);
      binn_destroy(w);
      free(ser.buf);
    }
  }
}

// binn_text_get(): text only, always null terminated.
static void test_text_get(void) {
  struct binn *b = binn_create_text(0, "hello", -1);
  struct binn_val v = binn_text_get(b);
  IW_TEST_ASSERT(v.buf != 0 && v.size == 5);
  IW_TEST_ASSERT(memcmp(v.buf, "hello", 5) == 0);
  IW_TEST_ASSERT(((const char*) v.buf)[5] == 0);
  binn_destroy(b);

  // Other null terminated string variants are text.
  b = mk(BINN_TYPE_DECIMAL, "123.456", 7);
  v = binn_text_get(b);
  IW_TEST_ASSERT(v.size == 7 && memcmp(v.buf, "123.456", 7) == 0);
  IW_TEST_ASSERT(((const char*) v.buf)[7] == 0);
  binn_destroy(b);

  // Blobs are not text.
  {
    uint8_t bytes[3] = { 0x01, 0x00, 0x02 };
    b = binn_create_blob(0, bytes, sizeof(bytes));
    v = binn_text_get(b);
    IW_TEST_ASSERT(v.buf == 0 && v.size == 0);
    binn_destroy(b);
  }

  // Numeric, container, empty and null inputs.
  b = binn_create_u64c(0, 1);
  v = binn_text_get(b);
  IW_TEST_ASSERT(v.buf == 0 && v.size == 0);
  binn_destroy(b);
  b = binn_create_list(0);
  v = binn_text_get(b);
  IW_TEST_ASSERT(v.buf == 0 && v.size == 0);
  binn_destroy(b);
  b = binn_create_text(0, 0, 0);
  v = binn_text_get(b);
  IW_TEST_ASSERT(v.buf == 0 && v.size == 0);
  binn_destroy(b);
  v = binn_text_get(0);
  IW_TEST_ASSERT(v.buf == 0 && v.size == 0);

  // Wrapped text from a serialized buffer is null terminated too.
  {
    struct binn holder;
    struct binn *w;
    b = binn_create_text(0, "wrapped", -1);
    {
      struct binn_val ser = binn_serialize_allocated(b);
      binn_destroy(b);
      w = binn_from(&holder, ser, 0);
      IW_TEST_ASSERT_PTR_NOT_NULL(w);
      v = binn_text_get(w);
      IW_TEST_ASSERT(v.size == 7 && memcmp(v.buf, "wrapped", 7) == 0);
      IW_TEST_ASSERT(((const char*) v.buf)[7] == 0);
      binn_destroy(w);
      free(ser.buf);
    }
  }

  // Copies keep the text null terminated.
  {
    struct binn *src = binn_create_text(0, "copy", -1);
    struct binn *cp = binn_copy_from(0, *binnv(src));
    IW_TEST_ASSERT_PTR_NOT_NULL(cp);
    v = binn_text_get(cp);
    IW_TEST_ASSERT(v.size == 4 && memcmp(v.buf, "copy", 4) == 0);
    IW_TEST_ASSERT(((const char*) v.buf)[4] == 0);
    binn_destroy(cp);
    {
      struct binn_val ser = binn_serialize_allocated(src);
      struct binn *cp2 = binn_copy_from(0, ser);
      IW_TEST_ASSERT_PTR_NOT_NULL(cp2);
      v = binn_text_get(cp2);
      IW_TEST_ASSERT(v.size == 4 && memcmp(v.buf, "copy", 4) == 0);
      IW_TEST_ASSERT(((const char*) v.buf)[4] == 0);
      binn_destroy(cp2);
      free(ser.buf);
    }
    binn_destroy(src);
  }

  // A serialized string with a non NUL terminator is rejected.
  {
    static const uint8_t bad[] = { 0xA0, 0x03, 'a', 'b', 'c', 0xFF };
    IW_TEST_ASSERT_FALSE(binn_is_serialized_buffer(bv(bad, sizeof(bad))));
    IW_TEST_ASSERT_PTR_NULL(binn_from(0, bv(bad, sizeof(bad)), 0));
  }
}

static void test_u64_get(void) {
  struct binn *b = binn_create_u64c(0, 0x1226652FEull);
  IW_TEST_ASSERT_EQUAL(binn_u64_get(b), 0x1226652FEull);
  binn_destroy(b);
  b = binn_create_u64c(0, UINT64_MAX);
  IW_TEST_ASSERT_EQUAL(binn_u64_get(b), UINT64_MAX);
  binn_destroy(b);
  b = binn_create_u64c(0, 255);
  IW_TEST_ASSERT_EQUAL(binn_u64_get(b), 255);
  binn_destroy(b);
  b = binn_create_i64c(0, -1); // negative integers wrap to two's complement
  IW_TEST_ASSERT_EQUAL(binn_u64_get(b), UINT64_MAX);
  binn_destroy(b);
  b = binn_create_i64c(0, INT64_MIN);
  IW_TEST_ASSERT_EQUAL(binn_u64_get(b), (uint64_t) INT64_MIN);
  binn_destroy(b);
  b = binn_create_f32c(0, 2.75f);
  IW_TEST_ASSERT_EQUAL(binn_u64_get(b), 2);
  binn_destroy(b);
  b = binn_create_f64c(0, 3.9);
  IW_TEST_ASSERT_EQUAL(binn_u64_get(b), 3);
  binn_destroy(b);
  b = binn_create_f64c(0, -1.5);
  IW_TEST_ASSERT_EQUAL(binn_u64_get(b), 0);
  binn_destroy(b);
  b = binn_create_f64c(0, 1e300);
  IW_TEST_ASSERT_EQUAL(binn_u64_get(b), 0);
  binn_destroy(b);
  b = binn_create_text(0, "x", -1);
  IW_TEST_ASSERT_EQUAL(binn_u64_get(b), 0);
  binn_destroy(b);
  b = binn_create_bool(0, true);
  IW_TEST_ASSERT_EQUAL(binn_u64_get(b), 0);
  binn_destroy(b);
  IW_TEST_ASSERT_EQUAL(binn_u64_get(0), 0);
}

static void test_i64_get(void) {
  struct binn *b = binn_create_i64c(0, -1);
  IW_TEST_ASSERT_EQUAL(binn_i64_get(b), -1);
  binn_destroy(b);
  b = binn_create_i64c(0, INT64_MIN);
  IW_TEST_ASSERT_EQUAL(binn_i64_get(b), INT64_MIN);
  binn_destroy(b);
  b = binn_create_u64c(0, 12345);
  IW_TEST_ASSERT_EQUAL(binn_i64_get(b), 12345);
  binn_destroy(b);
  b = binn_create_f32c(0, -2.75f);
  IW_TEST_ASSERT_EQUAL(binn_i64_get(b), -2);
  binn_destroy(b);
  b = binn_create_f64c(0, 3.9);
  IW_TEST_ASSERT_EQUAL(binn_i64_get(b), 3);
  binn_destroy(b);
  b = binn_create_f64c(0, 1e300);
  IW_TEST_ASSERT_EQUAL(binn_i64_get(b), 0);
  binn_destroy(b);
  b = binn_create_f64c(0, -1e300);
  IW_TEST_ASSERT_EQUAL(binn_i64_get(b), 0);
  binn_destroy(b);
  IW_TEST_ASSERT_EQUAL(binn_i64_get(0), 0);
}

static void test_f64_get(void) {
  struct binn *b = binn_create_i64c(0, -5);
  IW_TEST_ASSERT_DOUBLE_EQUAL(binn_f64_get(b), -5.0, 0);
  binn_destroy(b);
  b = binn_create_u64c(0, 123456789);
  IW_TEST_ASSERT_DOUBLE_EQUAL(binn_f64_get(b), 123456789.0, 0);
  binn_destroy(b);
  b = binn_create_f32c(0, 1.5f);
  IW_TEST_ASSERT_DOUBLE_EQUAL(binn_f64_get(b), 1.5, 0);
  binn_destroy(b);
  b = binn_create_f64c(0, 0.1);
  IW_TEST_ASSERT_DOUBLE_EQUAL(binn_f64_get(b), 0.1, 0);
  binn_destroy(b);
  b = binn_create_text(0, "x", -1);
  IW_TEST_ASSERT_DOUBLE_EQUAL(binn_f64_get(b), 0.0, 0);
  binn_destroy(b);
  IW_TEST_ASSERT_DOUBLE_EQUAL(binn_f64_get(0), 0.0, 0);
}

static void test_bool_get(void) {
  struct binn *b = binn_create_bool(0, true);
  IW_TEST_ASSERT_TRUE(binn_bool_get(b) == true);
  binn_destroy(b);
  b = binn_create_bool(0, false);
  IW_TEST_ASSERT_TRUE(binn_bool_get(b) == false);
  binn_destroy(b);
  b = binn_create_null(0);
  IW_TEST_ASSERT_TRUE(binn_bool_get(b) == false);
  binn_destroy(b);
  b = binn_create_u64c(0, 1);
  IW_TEST_ASSERT_TRUE(binn_bool_get(b) == false);
  binn_destroy(b);
  b = binn_create_text(0, "x", -1);
  IW_TEST_ASSERT_TRUE(binn_bool_get(b) == false);
  binn_destroy(b);
  IW_TEST_ASSERT_TRUE(binn_bool_get(0) == false);
}

// Getters work on values obtained from lookups (wrapped mode).
static void test_getters_from_lookup(void) {
  struct binn *obj = binn_create_object(0);
  struct binn *u = binn_create_u64c(0, 42);
  struct binn *t = binn_create_text(0, "hi", -1);
  struct binn found;
  IW_TEST_ASSERT_TRUE(binn_add_keyed(obj, "n", *binnv(u)));
  IW_TEST_ASSERT_TRUE(binn_add_keyed(obj, "s", *binnv(t)));
  binn_destroy(u);
  binn_destroy(t);
  IW_TEST_ASSERT_TRUE(binn_keyed_get(obj, "n", &found));
  IW_TEST_ASSERT_EQUAL(binn_u64_get(&found), 42);
  IW_TEST_ASSERT_EQUAL(binn_i64_get(&found), 42);
  IW_TEST_ASSERT_DOUBLE_EQUAL(binn_f64_get(&found), 42.0, 0);
  IW_TEST_ASSERT_TRUE(binn_keyed_get(obj, "s", &found));
  {
    struct binn_val sv = binn_blob_get(&found);
    IW_TEST_ASSERT(sv.size == 2 && memcmp(sv.buf, "hi", 2) == 0);
  }
  binn_destroy(obj);
}

static void test_type_predicates(void) {
  struct binn *b;
  b = binn_create_u64c(0, 1);
  IW_TEST_ASSERT(  binn_is_int_number(b) && !binn_is_fp_number(b) && !binn_is_text(b) && !binn_is_blob(b)
                && !binn_is_container(b));
  binn_destroy(b);
  b = binn_create_i64c(0, -1);
  IW_TEST_ASSERT(binn_is_int_number(b) && !binn_is_fp_number(b));
  binn_destroy(b);
  b = binn_create_f32c(0, 1.0f);
  IW_TEST_ASSERT(!binn_is_int_number(b) && binn_is_fp_number(b));
  binn_destroy(b);
  b = binn_create_f64c(0, 1.0);
  IW_TEST_ASSERT(!binn_is_int_number(b) && binn_is_fp_number(b));
  binn_destroy(b);

  b = binn_create_text(0, "t", -1);
  IW_TEST_ASSERT(binn_is_text(b) && !binn_is_blob(b) && !binn_is_container(b));
  binn_destroy(b);
  b = mk(BINN_TYPE_DATETIME, "2026-10-15 00:00:00", 19);
  IW_TEST_ASSERT(binn_is_text(b) && binn_is_datetime(b));
  IW_TEST_ASSERT(!binn_is_date(b) && !binn_is_time(b) && !binn_is_decimal(b));
  binn_destroy(b);
  b = mk(BINN_TYPE_DATE, "2026-10-15", 10);
  IW_TEST_ASSERT(binn_is_text(b) && binn_is_date(b) && !binn_is_datetime(b));
  binn_destroy(b);
  b = mk(BINN_TYPE_TIME, "00:00:00", 8);
  IW_TEST_ASSERT(binn_is_text(b) && binn_is_time(b) && !binn_is_datetime(b));
  binn_destroy(b);
  b = mk(BINN_TYPE_DECIMAL, "1.5", 3);
  IW_TEST_ASSERT(binn_is_text(b) && binn_is_decimal(b) && !binn_is_datetime(b));
  binn_destroy(b);

  {
    uint8_t bytes[2] = { 0, 1 };
    b = binn_create_blob(0, bytes, sizeof(bytes));
    IW_TEST_ASSERT(binn_is_blob(b) && !binn_is_text(b) && !binn_is_container(b));
    binn_destroy(b);
  }

  b = binn_create_bool(0, true);
  IW_TEST_ASSERT(binn_is_boolean(b) && !binn_is_null(b));
  binn_destroy(b);
  b = binn_create_bool(0, false);
  IW_TEST_ASSERT(binn_is_boolean(b));
  binn_destroy(b);
  b = binn_create_null(0);
  IW_TEST_ASSERT(binn_is_null(b) && !binn_is_boolean(b) && !binn_is_container(b));
  binn_destroy(b);

  b = binn_create_list(0);
  IW_TEST_ASSERT(binn_is_list(b) && !binn_is_map(b) && !binn_is_object(b) && binn_is_container(b));
  binn_destroy(b);
  b = binn_create_map(0);
  IW_TEST_ASSERT(binn_is_map(b) && !binn_is_list(b) && !binn_is_object(b) && binn_is_container(b));
  binn_destroy(b);
  b = binn_create_object(0);
  IW_TEST_ASSERT(binn_is_object(b) && !binn_is_list(b) && !binn_is_map(b) && binn_is_container(b));
  binn_destroy(b);

  IW_TEST_ASSERT(!binn_is_int_number(0) && !binn_is_fp_number(0) && !binn_is_text(0) && !binn_is_blob(0));
  IW_TEST_ASSERT(!binn_is_boolean(0) && !binn_is_null(0));
  IW_TEST_ASSERT(!binn_is_list(0) && !binn_is_map(0) && !binn_is_object(0) && !binn_is_container(0));
  IW_TEST_ASSERT(!binn_is_datetime(0) && !binn_is_date(0) && !binn_is_time(0) && !binn_is_decimal(0));
}

int main(int argc, char *argv[]) {
  static const struct iw_test_case cases[] = {
    IW_TEST_CASE(test_blob_get),
    IW_TEST_CASE(test_text_get),
    IW_TEST_CASE(test_u64_get),
    IW_TEST_CASE(test_i64_get),
    IW_TEST_CASE(test_f64_get),
    IW_TEST_CASE(test_bool_get),
    IW_TEST_CASE(test_getters_from_lookup),
    IW_TEST_CASE(test_type_predicates),
  };
  return iw_test_run(argc, argv, IW_TEST_SUITE("getters", cases, 0, 0));
}
