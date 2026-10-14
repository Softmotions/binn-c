// Container encoding following the examples from docs/spec.md.
#include "../binnc.h"
#include "tests.h"

static struct binn* make_scalar(binn_type_t type, const void *value, size_t sz) {
  struct binn_create_spec spec;
  memset(&spec, 0, sizeof(spec));
  spec.type = type;
  spec.value = (void*) value;
  spec.valuesz = sz;
  return binn_create_value(0, &spec);
}

static struct binn* make_text(const char *s) {
  return make_scalar(BINN_TYPE_TEXT, s, strlen(s));
}

static struct binn* make_u8(uint8_t v) {
  return make_scalar(BINN_TYPE_UINT8, &v, sizeof(v));
}

static struct binn* make_i16(int16_t v) {
  return make_scalar(BINN_TYPE_INT16, &v, sizeof(v));
}

static struct binn* make_u16(uint16_t v) {
  return make_scalar(BINN_TYPE_UINT16, &v, sizeof(v));
}

int main(int argc, char *argv[]) {
  // Empty containers: [type][size=3][count=0].
  {
    static const uint8_t e_list[] = { 0xE0, 0x03, 0x00 };
    static const uint8_t e_map[] = { 0xE1, 0x03, 0x00 };
    static const uint8_t e_object[] = { 0xE2, 0x03, 0x00 };
    struct binn *b;
    b = binn_create_list(0);
    binn_test_check_bytes(b, e_list, sizeof(e_list));
    binn_destroy(b);
    b = binn_create_map(0);
    binn_test_check_bytes(b, e_map, sizeof(e_map));
    binn_destroy(b);
    b = binn_create_object(0);
    binn_test_check_bytes(b, e_object, sizeof(e_object));
    binn_destroy(b);
  }

  // {"hello":"world"} - 17 bytes.
  {
    static const uint8_t expected[] = {
      0xE2, 0x11, 0x01,
      0x05, 'h', 'e', 'l', 'l', 'o',
      0xA0, 0x05, 'w', 'o', 'r', 'l', 'd', 0x00
    };
    struct binn *obj = binn_create_object(0);
    struct binn *str = make_text("world");
    BINN_ASSERT(binn_add_keyed(obj, "hello", str));
    binn_destroy(str);
    BINN_ASSERT(obj->count == 1);
    binn_test_check_bytes(obj, expected, sizeof(expected));
    binn_destroy(obj);
  }

  // [123, -456, 789] - 11 bytes.
  {
    static const uint8_t expected[] = {
      0xE0, 0x0B, 0x03,
      0x20, 0x7B,
      0x41, 0xFE, 0x38,
      0x40, 0x03, 0x15
    };
    struct binn *list = binn_create_list(0);
    struct binn *v;
    int32_t idx = -1;
    v = make_u8(123);
    BINN_ASSERT(binn_add(list, v, &idx));
    BINN_ASSERT(idx == 0);
    binn_destroy(v);
    v = make_i16(-456);
    BINN_ASSERT(binn_add(list, v, &idx));
    BINN_ASSERT(idx == 1);
    binn_destroy(v);
    v = make_u16(789);
    BINN_ASSERT(binn_add(list, v, &idx));
    BINN_ASSERT(idx == 2);
    binn_destroy(v);
    BINN_ASSERT(list->count == 3);
    binn_test_check_bytes(list, expected, sizeof(expected));
    binn_destroy(list);
  }

  // {1: "add", 2: [-12345, 6789]} - 26 bytes.
  {
    static const uint8_t expected[] = {
      0xE1, 0x1A, 0x02,
      0x00, 0x00, 0x00, 0x01,
      0xA0, 0x03, 'a', 'd', 'd', 0x00,
      0x00, 0x00, 0x00, 0x02,
      0xE0, 0x09, 0x02,
      0x41, 0xCF, 0xC7,
      0x40, 0x1A, 0x85
    };
    struct binn *map = binn_create_map(0);
    struct binn *inner = binn_create_list(0);
    struct binn *v;
    v = make_i16(-12345);
    BINN_ASSERT(binn_add(inner, v, 0));
    binn_destroy(v);
    v = make_u16(6789);
    BINN_ASSERT(binn_add(inner, v, 0));
    binn_destroy(v);
    v = make_text("add");
    BINN_ASSERT(binn_add_mapped(map, 1, v));
    binn_destroy(v);
    BINN_ASSERT(binn_add_mapped(map, 2, inner));
    binn_destroy(inner);
    BINN_ASSERT(map->count == 2);
    binn_test_check_bytes(map, expected, sizeof(expected));
    binn_destroy(map);
  }

  // [{"id":1,"name":"John"},{"id":2,"name":"Eric"}] - 43 bytes.
  {
    static const uint8_t expected[] = {
      0xE0, 0x2B, 0x02,
      0xE2, 0x14, 0x02,
      0x02, 'i', 'd', 0x20, 0x01,
      0x04, 'n', 'a', 'm', 'e', 0xA0, 0x04, 'J', 'o', 'h', 'n', 0x00,
      0xE2, 0x14, 0x02,
      0x02, 'i', 'd', 0x20, 0x02,
      0x04, 'n', 'a', 'm', 'e', 0xA0, 0x04, 'E', 'r', 'i', 'c', 0x00
    };
    struct binn *list = binn_create_list(0);
    struct binn *obj;
    struct binn *v;
    int i;
    for (i = 0; i < 2; i++) {
      obj = binn_create_object(0);
      v = make_u8((uint8_t) (i + 1));
      BINN_ASSERT(binn_add_keyed(obj, "id", v));
      binn_destroy(v);
      v = make_text(i == 0 ? "John" : "Eric");
      BINN_ASSERT(binn_add_keyed(obj, "name", v));
      binn_destroy(v);
      BINN_ASSERT(binn_add(list, obj, 0));
      binn_destroy(obj);
    }
    BINN_ASSERT(list->count == 2);
    binn_test_check_bytes(list, expected, sizeof(expected));
    binn_destroy(list);
  }

  return binn_assertions_failed > 0 ? 1 : 0;
}
