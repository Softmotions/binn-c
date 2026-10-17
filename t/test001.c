// Basic value types and their serialized representation.
#include "../binnc.h"
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
  BINN_ASSERT(b != 0);
  if (!b) {
    return;
  }
  BINN_ASSERT(b->type == type);
  binn_test_check_bytes(b, expected, len);
  binn_destroy(b);
}

int main(int argc, char *argv[]) {
  // No bytes values.
  {
    static const uint8_t v_null[] = { 0x00 };
    static const uint8_t v_true[] = { 0x01 };
    static const uint8_t v_false[] = { 0x02 };
    struct binn *b;
    b = make_value(BINN_TYPE_NULL, 0, 0);
    BINN_ASSERT(b != 0);
    binn_test_check_bytes(b, v_null, sizeof(v_null));
    binn_destroy(b);
    b = make_value(BINN_TYPE_TRUE, 0, 0);
    binn_test_check_bytes(b, v_true, sizeof(v_true));
    binn_destroy(b);
    b = make_value(BINN_TYPE_FALSE, 0, 0);
    binn_test_check_bytes(b, v_false, sizeof(v_false));
    binn_destroy(b);
  }

  // Signed/unsigned integers.
  {
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

  // Inline value representation.
  {
    int8_t i8 = -1;
    uint8_t u8 = 255;
    struct binn *b = make_value(BINN_TYPE_INT8, &i8, sizeof(i8));
    BINN_ASSERT(b->used == 1);
    BINN_ASSERT((int64_t) b->val.nval == -1);
    binn_destroy(b);
    b = make_value(BINN_TYPE_UINT8, &u8, sizeof(u8));
    BINN_ASSERT(b->val.nval == 255);
    binn_destroy(b);
  }

  // Floating point.
  {
    float f = 1.5f;
    double d = 1.5;
    static const uint8_t e_f[] = { 0x62, 0x3F, 0xC0, 0x00, 0x00 };
    static const uint8_t e_d[] = { 0x82, 0x3F, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    check_scalar(BINN_TYPE_FLOAT, &f, sizeof(f), e_f, sizeof(e_f));
    check_scalar(BINN_TYPE_DOUBLE, &d, sizeof(d), e_d, sizeof(e_d));
  }

  // Strings, blobs and terminator handling.
  {
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
  {
    binn_type_t html = (binn_type_t) ((5u << 13) | 0x1000u | 21u); // 0xB015
    static const uint8_t e_html[] = { 0xB0, 0x15, 0x04, 'h', 't', 'm', 'l', 0x00 };
    struct binn *b;
    BINN_ASSERT(html == 0xB015);
    b = make_value(html, "html", 4);
    BINN_ASSERT(b->type == html);
    binn_test_check_bytes(b, e_html, sizeof(e_html));
    binn_destroy(b);
  }

  // A long string uses a four byte size parameter.
  {
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

  // binn_serialize_into() and its error path.
  {
    uint8_t small[2];
    uint8_t big[8];
    uint16_t v = 0x15;
    struct binn *b = make_value(BINN_TYPE_UINT16, &v, sizeof(v));
    BINN_ASSERT(binn_serialize_into(b, small, 1).buf == 0);
    BINN_ASSERT(binn_serialize_into(b, big, sizeof(big)).buf == big);
    BINN_ASSERT(big[0] == 0x40 && big[1] == 0x00 && big[2] == 0x15);
    binn_destroy(b);
  }

  // BINN_INITIALIZER creates a valid empty binn struct.
  {
    struct binn b = BINN_INITIALIZER;
    struct binn_val s;
    BINN_ASSERT(b.magic == BINN_MAJIC_NUMBER);
    BINN_ASSERT(b.flags == 0);
    BINN_ASSERT(b.type == BINN_TYPE_NULL);
    BINN_ASSERT(binn_is_null(&b));
    BINN_ASSERT(binn_serialized_size(&b) == 1);
    s = binn_serialize_allocated(&b);
    BINN_ASSERT(s.buf != 0 && s.size == 1 && ((const uint8_t*) s.buf)[0] == 0x00);
    free(s.buf);
    BINN_ASSERT(binn_destroy(&b) == false);

    // The holder can be reused by a creator.
    {
      struct binn *list = binn_create_list(&b);
      BINN_ASSERT(list == &b && b.type == BINN_TYPE_LIST);
      BINN_ASSERT(binn_destroy(list) == false);
    }

    // Compound literal form.
    {
      struct binn *p = &(struct binn) BINN_INITIALIZER;
      BINN_ASSERT(p->magic == BINN_MAJIC_NUMBER && binn_is_null(p));
    }
  }

  return binn_assertions_failed > 0 ? 1 : 0;
}
