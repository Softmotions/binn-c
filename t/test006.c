// Convenience numeric/text creators with automatic number compression.
#define IW_TEST_HARNESS "binnc"
#include "tests.h"

static void check_u64(uint64_t v, binn_type_t type, const uint8_t *exp, size_t len) {
  struct binn *b = binn_create_u64c(0, v);
  IW_TEST_ASSERT_PTR_NOT_NULL(b);
  if (!b) {
    return;
  }
  IW_TEST_ASSERT_EQUAL(b->type, type);
  IW_TEST_ASSERT_EQUAL(b->val.nval, v);
  binn_test_check_bytes(b, exp, len);
  binn_destroy(b);
}

static void check_i64(int64_t v, binn_type_t type, const uint8_t *exp, size_t len) {
  struct binn *b = binn_create_i64c(0, v);
  IW_TEST_ASSERT_PTR_NOT_NULL(b);
  if (!b) {
    return;
  }
  IW_TEST_ASSERT_EQUAL(b->type, type);
  IW_TEST_ASSERT_EQUAL((int64_t) b->val.nval, v);
  binn_test_check_bytes(b, exp, len);
  binn_destroy(b);
}

static void check_f32(float v, const uint8_t *exp, size_t len) {
  struct binn *b = binn_create_f32c(0, v);
  uint32_t bits;
  memcpy(&bits, &v, sizeof(bits));
  IW_TEST_ASSERT_PTR_NOT_NULL(b);
  if (!b) {
    return;
  }
  IW_TEST_ASSERT_EQUAL(b->type, BINN_TYPE_FLOAT);
  IW_TEST_ASSERT_EQUAL(b->val.nval, bits);
  binn_test_check_bytes(b, exp, len);
  binn_destroy(b);
}

// `exp` may be zero to only check the chosen type and value bits.
static void check_f64(double v, binn_type_t type, const uint8_t *exp, size_t len) {
  struct binn *b = binn_create_f64c(0, v);
  uint64_t bits;
  if (type == BINN_TYPE_FLOAT) {
    float f = (float) v;
    uint32_t t;
    memcpy(&t, &f, sizeof(t));
    bits = t;
  } else {
    memcpy(&bits, &v, sizeof(bits));
  }
  IW_TEST_ASSERT_PTR_NOT_NULL(b);
  if (!b) {
    return;
  }
  IW_TEST_ASSERT_EQUAL(b->type, type);
  IW_TEST_ASSERT_EQUAL(b->val.nval, bits);
  if (exp) {
    binn_test_check_bytes(b, exp, len);
  } else {
    binn_test_check_size(b, 1 + 8);
  }
  binn_destroy(b);
}

static void check_text(const char *val, ssize_t vallen, size_t exp_used, const uint8_t *exp, size_t len) {
  struct binn *b = binn_create_text(0, val, vallen);
  IW_TEST_ASSERT_PTR_NOT_NULL(b);
  if (!b) {
    return;
  }
  IW_TEST_ASSERT_EQUAL(b->type, BINN_TYPE_TEXT);
  IW_TEST_ASSERT_EQUAL(b->used, exp_used);
  binn_test_check_bytes(b, exp, len);
  binn_destroy(b);
}

static void check_blob(const void *val, ssize_t vallen, size_t exp_used, const uint8_t *exp, size_t len) {
  struct binn *b = binn_create_blob(0, val, vallen);
  IW_TEST_ASSERT_PTR_NOT_NULL(b);
  if (!b) {
    return;
  }
  IW_TEST_ASSERT_EQUAL(b->type, BINN_TYPE_BLOB);
  IW_TEST_ASSERT_EQUAL(b->used, exp_used);
  binn_test_check_bytes(b, exp, len);
  binn_destroy(b);
}

// Unsigned integer compression: UINT8 / UINT16 / UIN32 / UINT64.
static void test_unsigned_compression(void) {
  static const uint8_t e0[] = { 0x20, 0x00 };
  static const uint8_t e_u8max[] = { 0x20, 0xFF };
  static const uint8_t e_u16max[] = { 0x40, 0xFF, 0xFF };
  static const uint8_t e_u16_1[] = { 0x40, 0x01, 0x00 };
  static const uint8_t e_u32max[] = { 0x60, 0xFF, 0xFF, 0xFF, 0xFF };
  static const uint8_t e_u32_1[] = { 0x60, 0x00, 0x01, 0x00, 0x00 };
  static const uint8_t e_u64_1[] = { 0x80, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00 };
  static const uint8_t e_u64_yours[] = { 0x80, 0x00, 0x00, 0x00, 0x01, 0x22, 0x66, 0x52, 0xFE };
  static const uint8_t e_u64max[] = { 0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  check_u64(0, BINN_TYPE_UINT8, e0, sizeof(e0));
  check_u64(0xFF, BINN_TYPE_UINT8, e_u8max, sizeof(e_u8max));
  check_u64(0x100, BINN_TYPE_UINT16, e_u16_1, sizeof(e_u16_1));
  check_u64(0xFFFF, BINN_TYPE_UINT16, e_u16max, sizeof(e_u16max));
  check_u64(0x10000, BINN_TYPE_UINT32, e_u32_1, sizeof(e_u32_1));
  check_u64(0xFFFFFFFFull, BINN_TYPE_UINT32, e_u32max, sizeof(e_u32max));
  check_u64(0x100000000ull, BINN_TYPE_UINT64, e_u64_1, sizeof(e_u64_1));
  check_u64(0x1226652FEull, BINN_TYPE_UINT64, e_u64_yours, sizeof(e_u64_yours));
  check_u64(UINT64_MAX, BINN_TYPE_UINT64, e_u64max, sizeof(e_u64max));
}

// Signed integer compression: INT8 / INT16 / INT32 / IN64.
static void test_signed_compression(void) {
  static const uint8_t e0[] = { 0x21, 0x00 };
  static const uint8_t e_i8min[] = { 0x21, 0x80 };
  static const uint8_t e_i8max[] = { 0x21, 0x7F };
  static const uint8_t e_im1[] = { 0x21, 0xFF };
  static const uint8_t e_i16min[] = { 0x41, 0x80, 0x00 };
  static const uint8_t e_i16max[] = { 0x41, 0x7F, 0xFF };
  static const uint8_t e_i16_128[] = { 0x41, 0x00, 0x80 };
  static const uint8_t e_i16_m129[] = { 0x41, 0xFF, 0x7F };
  static const uint8_t e_i32min[] = { 0x61, 0x80, 0x00, 0x00, 0x00 };
  static const uint8_t e_i32max[] = { 0x61, 0x7F, 0xFF, 0xFF, 0xFF };
  static const uint8_t e_i32_32768[] = { 0x61, 0x00, 0x00, 0x80, 0x00 };
  static const uint8_t e_i32_m32769[] = { 0x61, 0xFF, 0xFF, 0x7F, 0xFF };
  static const uint8_t e_i64_2G[] = { 0x81, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00 };
  static const uint8_t e_i64min[] = { 0x81, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
  static const uint8_t e_i64max[] = { 0x81, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  check_i64(0, BINN_TYPE_INT8, e0, sizeof(e0));
  check_i64(-128, BINN_TYPE_INT8, e_i8min, sizeof(e_i8min));
  check_i64(127, BINN_TYPE_INT8, e_i8max, sizeof(e_i8max));
  check_i64(-1, BINN_TYPE_INT8, e_im1, sizeof(e_im1));
  check_i64(-32768, BINN_TYPE_INT16, e_i16min, sizeof(e_i16min));
  check_i64(32767, BINN_TYPE_INT16, e_i16max, sizeof(e_i16max));
  check_i64(128, BINN_TYPE_INT16, e_i16_128, sizeof(e_i16_128));
  check_i64(-129, BINN_TYPE_INT16, e_i16_m129, sizeof(e_i16_m129));
  check_i64(INT32_MIN, BINN_TYPE_INT32, e_i32min, sizeof(e_i32min));
  check_i64(INT32_MAX, BINN_TYPE_INT32, e_i32max, sizeof(e_i32max));
  check_i64(32768, BINN_TYPE_INT32, e_i32_32768, sizeof(e_i32_32768));
  check_i64(-32769, BINN_TYPE_INT32, e_i32_m32769, sizeof(e_i32_m32769));
  check_i64(2147483648ll, BINN_TYPE_INT64, e_i64_2G, sizeof(e_i64_2G));
  check_i64(INT64_MIN, BINN_TYPE_INT64, e_i64min, sizeof(e_i64min));
  check_i64(INT64_MAX, BINN_TYPE_INT64, e_i64max, sizeof(e_i64max));
}

static void test_float32(void) {
  static const uint8_t e0[] = { 0x62, 0x00, 0x00, 0x00, 0x00 };
  static const uint8_t e15[] = { 0x62, 0x3F, 0xC0, 0x00, 0x00 };
  static const uint8_t em15[] = { 0x62, 0xBF, 0xC0, 0x00, 0x00 };
  check_f32(0.0f, e0, sizeof(e0));
  check_f32(1.5f, e15, sizeof(e15));
  check_f32(-1.5f, em15, sizeof(em15));
}

// Double precision with compression to FLOAT when exactly representable.
static void test_float64_compression(void) {
  static const uint8_t e_double_01[] = { 0x82, 0x3F, 0xB9, 0x99, 0x99, 0x99, 0x99, 0x99, 0x9A };
  static const uint8_t e_float_15[] = { 0x62, 0x3F, 0xC0, 0x00, 0x00 };
  static const uint8_t e_float_1[] = { 0x62, 0x3F, 0x80, 0x00, 0x00 };
  static const uint8_t e_float_35[] = { 0x62, 0x40, 0x60, 0x00, 0x00 };
  check_f64(1.5, BINN_TYPE_FLOAT, e_float_15, sizeof(e_float_15));
  check_f64(1.0, BINN_TYPE_FLOAT, e_float_1, sizeof(e_float_1));
  check_f64(3.5, BINN_TYPE_FLOAT, e_float_35, sizeof(e_float_35));
  check_f64(0.1, BINN_TYPE_DOUBLE, e_double_01, sizeof(e_double_01));
  check_f64(1e300, BINN_TYPE_DOUBLE, 0, 0); // only type/bits are checked
  check_f64(-0.1, BINN_TYPE_DOUBLE, 0, 0);
}

static void test_bool(void) {
  static const uint8_t e_true[] = { 0x01 };
  static const uint8_t e_false[] = { 0x02 };
  struct binn holder;
  struct binn *b = binn_create_bool(0, true);
  IW_TEST_ASSERT_PTR_NOT_NULL(b);
  IW_TEST_ASSERT_EQUAL(b->type, BINN_TYPE_TRUE);
  IW_TEST_ASSERT_EQUAL(b->used, 0);
  binn_test_check_bytes(b, e_true, sizeof(e_true));
  binn_destroy(b);
  b = binn_create_bool(0, false);
  IW_TEST_ASSERT_PTR_NOT_NULL(b);
  IW_TEST_ASSERT_EQUAL(b->type, BINN_TYPE_FALSE);
  IW_TEST_ASSERT_EQUAL(b->used, 0);
  binn_test_check_bytes(b, e_false, sizeof(e_false));
  binn_destroy(b);
  b = binn_create_bool(&holder, true);
  IW_TEST_ASSERT(b == &holder && b->type == BINN_TYPE_TRUE);
  IW_TEST_ASSERT_FALSE(binn_destroy(b));
}

static void test_null(void) {
  static const uint8_t e_null[] = { 0x00 };
  struct binn holder;
  struct binn *b = binn_create_null(0);
  IW_TEST_ASSERT_PTR_NOT_NULL(b);
  IW_TEST_ASSERT_EQUAL(b->type, BINN_TYPE_NULL);
  IW_TEST_ASSERT_EQUAL(b->used, 0);
  binn_test_check_bytes(b, e_null, sizeof(e_null));
  binn_destroy(b);
  b = binn_create_null(&holder);
  IW_TEST_ASSERT(b == &holder && b->type == BINN_TYPE_NULL);
  IW_TEST_ASSERT_FALSE(binn_destroy(b));
}

static void test_text(void) {
  static const uint8_t e_hello[] = { 0xA0, 0x05, 'h', 'e', 'l', 'l', 'o', 0x00 };
  static const uint8_t e_hel[] = { 0xA0, 0x03, 'h', 'e', 'l', 0x00 };
  static const uint8_t e_abc[] = { 0xA0, 0x03, 'a', 'b', 'c', 0x00 };
  static const uint8_t e_empty[] = { 0xA0, 0x00, 0x00 };
  char nc[3] = { 'a', 'b', 'c' }; // not null terminated
  check_text("hello", -1, 5, e_hello, sizeof(e_hello));
  check_text("hello", 5, 5, e_hello, sizeof(e_hello));
  check_text("hello", 3, 3, e_hel, sizeof(e_hel));
  check_text("hello", 0, 0, e_empty, sizeof(e_empty));
  check_text("", -1, 0, e_empty, sizeof(e_empty));
  check_text(0, 0, 0, e_empty, sizeof(e_empty));
  check_text(0, -1, 0, e_empty, sizeof(e_empty));
  check_text(nc, 3, 3, e_abc, sizeof(e_abc));
  IW_TEST_ASSERT_PTR_NULL(binn_create_text(0, 0, 5)); // no data pointer
}

// A long text uses a four byte size parameter.
static void test_long_text(void) {
  char big[200];
  struct binn *b;
  memset(big, 'x', sizeof(big));
  b = binn_create_text(0, big, (ssize_t) sizeof(big));
  IW_TEST_ASSERT_PTR_NOT_NULL(b);
  IW_TEST_ASSERT_EQUAL(b->type, BINN_TYPE_TEXT);
  IW_TEST_ASSERT_EQUAL(b->used, sizeof(big));
  IW_TEST_ASSERT_EQUAL(b->size, 1 + 4 + sizeof(big) + 1);
  binn_destroy(b);
}

static void test_blob(void) {
  static const uint8_t e_bin[] = { 0xC0, 0x04, 0x00, 0x01, 0xFF, 0x00 };
  static const uint8_t e_empty[] = { 0xC0, 0x00 };
  static const uint8_t blob[] = { 0x00, 0x01, 0xFF, 0x00 }; // embedded null bytes
  char pad[3] = { 'a', 'b', 'c' };
  struct binn holder;
  struct binn *b;
  check_blob(blob, sizeof(blob), sizeof(blob), e_bin, sizeof(e_bin));
  check_blob(0, 0, 0, e_empty, sizeof(e_empty));
  check_blob(pad, 0, 0, e_empty, sizeof(e_empty));
  IW_TEST_ASSERT_PTR_NULL(binn_create_blob(0, pad, -1)); // negative length
  IW_TEST_ASSERT_PTR_NULL(binn_create_blob(0, 0, 3));    // no data pointer

  b = binn_create_blob(&holder, blob, sizeof(blob));
  IW_TEST_ASSERT(b == &holder && b->type == BINN_TYPE_BLOB);
  IW_TEST_ASSERT_FALSE(binn_destroy(b));
}

// A long blob uses a four byte size parameter.
static void test_long_blob(void) {
  uint8_t big[300];
  struct binn *b;
  size_t i;
  for (i = 0; i < sizeof(big); i++) {
    big[i] = (uint8_t) i;
  }
  b = binn_create_blob(0, big, (ssize_t) sizeof(big));
  IW_TEST_ASSERT_PTR_NOT_NULL(b);
  IW_TEST_ASSERT_EQUAL(b->type, BINN_TYPE_BLOB);
  IW_TEST_ASSERT_EQUAL(b->used, sizeof(big));
  IW_TEST_ASSERT_EQUAL(b->size, 1 + 4 + sizeof(big));
  binn_destroy(b);
}

// Stack holders are supported and are not freed.
static void test_stack_holders(void) {
  struct binn holder;
  struct binn *b = binn_create_u64c(&holder, 7);
  IW_TEST_ASSERT(b == &holder);
  IW_TEST_ASSERT_EQUAL(b->type, BINN_TYPE_UINT8);
  IW_TEST_ASSERT_FALSE(binn_destroy(b));
  b = binn_create_text(&holder, "x", -1);
  IW_TEST_ASSERT(b == &holder);
  IW_TEST_ASSERT_FALSE(binn_destroy(b));
}

// Created scalars can be stored in containers and survive a round trip.
static void test_container_roundtrip(void) {
  struct binn *obj = binn_create_object(0);
  struct binn *u = binn_create_u64c(0, 0x1226652FEull);
  struct binn *t = binn_create_text(0, "hello", -1);
  struct binn_val ser;
  struct binn holder;
  struct binn *w;
  struct binn found;
  IW_TEST_ASSERT_TRUE(binn_add_keyed(obj, "num", *binnv(u)));
  IW_TEST_ASSERT_TRUE(binn_add_keyed(obj, "text", *binnv(t)));
  binn_destroy(u);
  binn_destroy(t);
  ser = binn_serialize_allocated(obj);
  IW_TEST_ASSERT_PTR_NOT_NULL(ser.buf);
  w = binn_from(&holder, ser, 0);
  IW_TEST_ASSERT_PTR_NOT_NULL(w);
  IW_TEST_ASSERT_TRUE(binn_keyed_get(w, "num", &found));
  IW_TEST_ASSERT_EQUAL(found.type, BINN_TYPE_UINT64);
  IW_TEST_ASSERT_EQUAL(found.val.nval, 0x1226652FEull);
  IW_TEST_ASSERT_TRUE(binn_keyed_get(w, "text", &found));
  IW_TEST_ASSERT_EQUAL(found.type, BINN_TYPE_TEXT);
  IW_TEST_ASSERT(found.used == 5 && memcmp(found.val.bval, "hello", 5) == 0);
  binn_destroy(w);
  free(ser.buf);
  binn_destroy(obj);
}

int main(int argc, char *argv[]) {
  static const struct iw_test_case cases[] = {
    IW_TEST_CASE(test_unsigned_compression),
    IW_TEST_CASE(test_signed_compression),
    IW_TEST_CASE(test_float32),
    IW_TEST_CASE(test_float64_compression),
    IW_TEST_CASE(test_bool),
    IW_TEST_CASE(test_null),
    IW_TEST_CASE(test_text),
    IW_TEST_CASE(test_long_text),
    IW_TEST_CASE(test_blob),
    IW_TEST_CASE(test_long_blob),
    IW_TEST_CASE(test_stack_holders),
    IW_TEST_CASE(test_container_roundtrip),
  };
  return iw_test_run(argc, argv, IW_TEST_SUITE("creators", cases, 0, 0));
}
