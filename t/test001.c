// Basic value types and their serialized representation.
#define IW_TEST_HARNESS "binnc"
#include "tests.h"

static struct binn* make_value(binn_type_t type, const void *value, size_t valuesz) {
  struct binn_create_spec spec;
  memset(&spec, 0, sizeof(spec));
  spec.type = type;
  spec.value = bv(value, valuesz);
  return binn_create_value(0, &spec);
}

static void check_scalar(binn_type_t type, const void *value, size_t valuesz, const uint8_t *expected, size_t len) {
  struct binn *b = make_value(type, value, valuesz);
  IW_TEST_ASSERT_PTR_NOT_NULL(b);
  if (!b) {
    return;
  }
  IW_TEST_ASSERT_EQUAL(b->type, type);
  binn_test_check_bytes(b, expected, len);
  binn_destroy(b);
}

static void test_no_bytes_values(void) {
  static const uint8_t v_null[] = { 0x00 };
  static const uint8_t v_true[] = { 0x01 };
  static const uint8_t v_false[] = { 0x02 };
  struct binn *b;
  b = make_value(BINN_TYPE_NULL, 0, 0);
  IW_TEST_ASSERT_PTR_NOT_NULL(b);
  binn_test_check_bytes(b, v_null, sizeof(v_null));
  binn_destroy(b);
  b = make_value(BINN_TYPE_TRUE, 0, 0);
  binn_test_check_bytes(b, v_true, sizeof(v_true));
  binn_destroy(b);
  b = make_value(BINN_TYPE_FALSE, 0, 0);
  binn_test_check_bytes(b, v_false, sizeof(v_false));
  binn_destroy(b);
}

static void test_integers(void) {
  uint8_t u8 = 0x7B;
  int8_t i8 = -1;
  uint16_t u16 = 0x0315;
  int16_t i16 = -456;
  uint32_t u32 = 0xDEADBEEFu;
  int32_t i32 = -12345;
  uint64_t u64 = 0x0123456789ABCDEFull;
  int64_t i64 = -2;
  static const uint8_t e_u8[] = { 0x20, 0x7B };
  static const uint8_t e_i8[] = { 0x21, 0xFF };
  static const uint8_t e_u16[] = { 0x40, 0x03, 0x15 };
  static const uint8_t e_i16[] = { 0x41, 0xFE, 0x38 };
  static const uint8_t e_u32[] = { 0x60, 0xDE, 0xAD, 0xBE, 0xEF };
  static const uint8_t e_i32[] = { 0x61, 0xFF, 0xFF, 0xCF, 0xC7 };
  static const uint8_t e_u64[] = { 0x80, 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF };
  static const uint8_t e_i64[] = { 0x81, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE };
  check_scalar(BINN_TYPE_UINT8, &u8, sizeof(u8), e_u8, sizeof(e_u8));
  check_scalar(BINN_TYPE_INT8, &i8, sizeof(i8), e_i8, sizeof(e_i8));
  check_scalar(BINN_TYPE_UINT16, &u16, sizeof(u16), e_u16, sizeof(e_u16));
  check_scalar(BINN_TYPE_INT16, &i16, sizeof(i16), e_i16, sizeof(e_i16));
  check_scalar(BINN_TYPE_UINT32, &u32, sizeof(u32), e_u32, sizeof(e_u32));
  check_scalar(BINN_TYPE_INT32, &i32, sizeof(i32), e_i32, sizeof(e_i32));
  check_scalar(BINN_TYPE_UINT64, &u64, sizeof(u64), e_u64, sizeof(e_u64));
  check_scalar(BINN_TYPE_INT64, &i64, sizeof(i64), e_i64, sizeof(e_i64));
}

static void test_inline_values(void) {
  int8_t i8 = -1;
  uint8_t u8 = 255;
  struct binn *b = make_value(BINN_TYPE_INT8, &i8, sizeof(i8));
  IW_TEST_ASSERT_EQUAL(b->used, 1);
  IW_TEST_ASSERT_EQUAL((int64_t) b->val.nval, -1);
  binn_destroy(b);
  b = make_value(BINN_TYPE_UINT8, &u8, sizeof(u8));
  IW_TEST_ASSERT_EQUAL(b->val.nval, 255);
  binn_destroy(b);
}

static void test_floats(void) {
  float f = 1.5f;
  double d = 1.5;
  static const uint8_t e_f[] = { 0x62, 0x3F, 0xC0, 0x00, 0x00 };
  static const uint8_t e_d[] = { 0x82, 0x3F, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
  check_scalar(BINN_TYPE_FLOAT, &f, sizeof(f), e_f, sizeof(e_f));
  check_scalar(BINN_TYPE_DOUBLE, &d, sizeof(d), e_d, sizeof(e_d));
}

static void test_text_and_blob(void) {
  static const uint8_t e_text[] = { 0xA0, 0x05, 'w', 'o', 'r', 'l', 'd', 0x00 };
  static const uint8_t e_empty[] = { 0xA0, 0x00, 0x00 };
  static const uint8_t e_blob[] = { 0xC0, 0x03, 0x01, 0x02, 0x03 };
  check_scalar(BINN_TYPE_TEXT, "world", 5, e_text, sizeof(e_text));
  check_scalar(BINN_TYPE_TEXT, "", 0, e_empty, sizeof(e_empty));
  check_scalar(BINN_TYPE_BLOB, "\x01\x02\x03", 3, e_blob, sizeof(e_blob));
  // strlen() is used when the value size is zero.
  check_scalar(BINN_TYPE_TEXT, "world", 0, e_text, sizeof(e_text));
}

// Two byte (extended) types: STRING storage, sub-type 21.
static void test_extended_type(void) {
  binn_type_t html = (binn_type_t) ((5u << 13) | 0x1000u | 21u); // 0xB015
  static const uint8_t e_html[] = { 0xB0, 0x15, 0x04, 'h', 't', 'm', 'l', 0x00 };
  struct binn *b;
  IW_TEST_ASSERT_EQUAL(html, 0xB015);
  b = make_value(html, "html", 4);
  IW_TEST_ASSERT_EQUAL(b->type, html);
  binn_test_check_bytes(b, e_html, sizeof(e_html));
  binn_destroy(b);
}

// A long string uses a four byte size parameter.
static void test_long_text(void) {
  uint8_t value[200];
  uint8_t expected[200 + 7];
  struct binn *b;
  size_t i;
  memset(value, 'a', sizeof(value));
  expected[0] = 0xA0;
  expected[1] = 0x80;
  expected[2] = 0x00;
  expected[3] = 0x00;
  expected[4] = 200;
  for (i = 0; i < sizeof(value); i++) {
    expected[5 + i] = 'a';
  }
  expected[5 + sizeof(value)] = 0x00;
  b = make_value(BINN_TYPE_TEXT, value, sizeof(value));
  binn_test_check_bytes(b, expected, 5 + sizeof(value) + 1);
  binn_destroy(b);
}

static void test_serialize_into(void) {
  uint8_t small[2];
  uint8_t big[8];
  uint16_t v = 0x15;
  struct binn *b = make_value(BINN_TYPE_UINT16, &v, sizeof(v));
  IW_TEST_ASSERT_PTR_NULL(binn_serialize_into(b, small, 1).buf);
  IW_TEST_ASSERT(binn_serialize_into(b, big, sizeof(big)).buf == big);
  IW_TEST_ASSERT(big[0] == 0x40 && big[1] == 0x00 && big[2] == 0x15);
  binn_destroy(b);
}

// BINN_INITIALIZER creates a valid empty binn struct.
static void test_initializer(void) {
  struct binn b = BINN_INITIALIZER;
  struct binn_val s;
  IW_TEST_ASSERT_EQUAL(b.magic, BINN_MAJIC_NUMBER);
  IW_TEST_ASSERT_EQUAL(b.flags, 0);
  IW_TEST_ASSERT_EQUAL(b.type, BINN_TYPE_NULL);
  IW_TEST_ASSERT_TRUE(binn_is_null(&b));
  IW_TEST_ASSERT_EQUAL(binn_serialized_size(&b), 1);
  s = binn_serialize_allocated(&b);
  IW_TEST_ASSERT(s.buf != 0 && s.size == 1 && ((const uint8_t*) s.buf)[0] == 0x00);
  free(s.buf);
  IW_TEST_ASSERT_FALSE(binn_destroy(&b));

  // The holder can be reused by a creator.
  {
    struct binn *list = binn_create_list(&b);
    IW_TEST_ASSERT(list == &b && b.type == BINN_TYPE_LIST);
    IW_TEST_ASSERT_FALSE(binn_destroy(list));
  }

  // Compound literal form.
  {
    struct binn *p = &(struct binn) BINN_INITIALIZER;
    IW_TEST_ASSERT(p->magic == BINN_MAJIC_NUMBER && binn_is_null(p));
  }
}

int main(int argc, char *argv[]) {
  static const struct iw_test_case cases[] = {
    IW_TEST_CASE(test_no_bytes_values),
    IW_TEST_CASE(test_integers),
    IW_TEST_CASE(test_inline_values),
    IW_TEST_CASE(test_floats),
    IW_TEST_CASE(test_text_and_blob),
    IW_TEST_CASE(test_extended_type),
    IW_TEST_CASE(test_long_text),
    IW_TEST_CASE(test_serialize_into),
    IW_TEST_CASE(test_initializer),
  };
  return iw_test_run(argc, argv, IW_TEST_SUITE("basic_types", cases, 0, 0));
}
