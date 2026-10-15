// Convenience numeric/text creators with automatic number compression.
#include "../binnc.h"
#include "tests.h"

static void check_u64(uint64_t v, binn_type_t type, const uint8_t *exp, size_t len) {
  struct binn *b = binn_create_u64c(0, v);
  BINN_ASSERT(b != 0);
  if (!b) {
    return;
  }
  BINN_ASSERT(b->type == type);
  BINN_ASSERT(b->val.nval == v);
  binn_test_check_bytes(b, exp, len);
  binn_destroy(b);
}

static void check_i64(int64_t v, binn_type_t type, const uint8_t *exp, size_t len) {
  struct binn *b = binn_create_i64c(0, v);
  BINN_ASSERT(b != 0);
  if (!b) {
    return;
  }
  BINN_ASSERT(b->type == type);
  BINN_ASSERT((int64_t) b->val.nval == v);
  binn_test_check_bytes(b, exp, len);
  binn_destroy(b);
}

static void check_f32(float v, const uint8_t *exp, size_t len) {
  struct binn *b = binn_create_f32c(0, v);
  uint32_t bits;
  memcpy(&bits, &v, sizeof(bits));
  BINN_ASSERT(b != 0);
  if (!b) {
    return;
  }
  BINN_ASSERT(b->type == BINN_TYPE_FLOAT);
  BINN_ASSERT(b->val.nval == bits);
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
  BINN_ASSERT(b != 0);
  if (!b) {
    return;
  }
  BINN_ASSERT(b->type == type);
  BINN_ASSERT(b->val.nval == bits);
  if (exp) {
    binn_test_check_bytes(b, exp, len);
  } else {
    binn_test_check_size(b, 1 + 8);
  }
  binn_destroy(b);
}

static void check_text(const char *val, ssize_t vallen, size_t exp_used, const uint8_t *exp, size_t len) {
  struct binn *b = binn_create_text(0, val, vallen);
  BINN_ASSERT(b != 0);
  if (!b) {
    return;
  }
  BINN_ASSERT(b->type == BINN_TYPE_TEXT);
  BINN_ASSERT(b->used == exp_used);
  binn_test_check_bytes(b, exp, len);
  binn_destroy(b);
}

int main(int argc, char *argv[]) {
  // Unsigned integer compression: UINT8 / UINT16 / UIN32 / UINT64.
  {
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
    check_u64(0x10000, BINN_TYPE_UIN32, e_u32_1, sizeof(e_u32_1));
    check_u64(0xFFFFFFFFull, BINN_TYPE_UIN32, e_u32max, sizeof(e_u32max));
    check_u64(0x100000000ull, BINN_TYPE_UINT64, e_u64_1, sizeof(e_u64_1));
    check_u64(0x1226652FEull, BINN_TYPE_UINT64, e_u64_yours, sizeof(e_u64_yours));
    check_u64(UINT64_MAX, BINN_TYPE_UINT64, e_u64max, sizeof(e_u64max));
  }

  // Signed integer compression: INT8 / INT16 / INT32 / IN64.
  {
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

  // Single precision.
  {
    static const uint8_t e0[] = { 0x62, 0x00, 0x00, 0x00, 0x00 };
    static const uint8_t e15[] = { 0x62, 0x3F, 0xC0, 0x00, 0x00 };
    static const uint8_t em15[] = { 0x62, 0xBF, 0xC0, 0x00, 0x00 };
    check_f32(0.0f, e0, sizeof(e0));
    check_f32(1.5f, e15, sizeof(e15));
    check_f32(-1.5f, em15, sizeof(em15));
  }

  // Double precision with compression to FLOAT when exactly representable.
  {
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

  // Text.
  {
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
    BINN_ASSERT(binn_create_text(0, 0, 5) == 0); // no data pointer
  }

  // A long text uses a four byte size parameter.
  {
    char big[200];
    struct binn *b;
    memset(big, 'x', sizeof(big));
    b = binn_create_text(0, big, (ssize_t) sizeof(big));
    BINN_ASSERT(b != 0);
    BINN_ASSERT(b->type == BINN_TYPE_TEXT);
    BINN_ASSERT(b->used == sizeof(big));
    BINN_ASSERT(b->size == 1 + 4 + sizeof(big) + 1);
    binn_destroy(b);
  }

  // Stack holders are supported and are not freed.
  {
    struct binn holder;
    struct binn *b = binn_create_u64c(&holder, 7);
    BINN_ASSERT(b == &holder);
    BINN_ASSERT(b->type == BINN_TYPE_UINT8);
    BINN_ASSERT(binn_destroy(b) == false);
    b = binn_create_text(&holder, "x", -1);
    BINN_ASSERT(b == &holder);
    BINN_ASSERT(binn_destroy(b) == false);
  }

  // Created scalars can be stored in containers and survive a round trip.
  {
    struct binn *obj = binn_create_object(0);
    struct binn *u = binn_create_u64c(0, 0x1226652FEull);
    struct binn *t = binn_create_text(0, "hello", -1);
    struct binn_val ser;
    struct binn holder;
    struct binn *w;
    struct binn found;
    BINN_ASSERT(binn_add_keyed(obj, "num", *binnv(u)));
    BINN_ASSERT(binn_add_keyed(obj, "text", *binnv(t)));
    binn_destroy(u);
    binn_destroy(t);
    ser = binn_serialize_allocated(obj);
    BINN_ASSERT(ser.buf != 0);
    w = binn_from(&holder, ser, 0);
    BINN_ASSERT(w != 0);
    BINN_ASSERT(binn_keyed_get(w, "num", &found));
    BINN_ASSERT(found.type == BINN_TYPE_UINT64);
    BINN_ASSERT(found.val.nval == 0x1226652FEull);
    BINN_ASSERT(binn_keyed_get(w, "text", &found));
    BINN_ASSERT(found.type == BINN_TYPE_TEXT);
    BINN_ASSERT(found.used == 5 && memcmp(found.val.bval, "hello", 5) == 0);
    binn_destroy(w);
    free(ser.buf);
    binn_destroy(obj);
  }

  return binn_assertions_failed > 0 ? 1 : 0;
}
