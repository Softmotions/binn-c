// Container encoding following the examples from docs/spec.md.
#define IW_TEST_HARNESS "binnc"
#include "tests.h"

static struct binn* make_scalar(binn_type_t type, const void *value, size_t sz) {
  struct binn_create_spec spec;
  memset(&spec, 0, sizeof(spec));
  spec.type = type;
  spec.value = bv(value, sz);
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

// Empty containers: [type][size=3][count=0].
static void test_empty_containers(void) {
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
static void test_object(void) {
  static const uint8_t expected[] = {
    0xE2, 0x11, 0x01,
    0x05, 'h', 'e', 'l', 'l', 'o',
    0xA0, 0x05, 'w', 'o', 'r', 'l', 'd', 0x00
  };
  struct binn *obj = binn_create_object(0);
  struct binn *str = make_text("world");
  IW_TEST_ASSERT_TRUE(binn_add_keyed(obj, "hello", *binnv(str)));
  binn_destroy(str);
  IW_TEST_ASSERT_EQUAL(obj->count, 1);
  binn_test_check_bytes(obj, expected, sizeof(expected));
  binn_destroy(obj);
}

// [123, -456, 789] - 11 bytes.
static void test_list(void) {
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
  IW_TEST_ASSERT_TRUE(binn_add(list, *binnv(v), &idx));
  IW_TEST_ASSERT_EQUAL(idx, 0);
  binn_destroy(v);
  v = make_i16(-456);
  IW_TEST_ASSERT_TRUE(binn_add(list, *binnv(v), &idx));
  IW_TEST_ASSERT_EQUAL(idx, 1);
  binn_destroy(v);
  v = make_u16(789);
  IW_TEST_ASSERT_TRUE(binn_add(list, *binnv(v), &idx));
  IW_TEST_ASSERT_EQUAL(idx, 2);
  binn_destroy(v);
  IW_TEST_ASSERT_EQUAL(list->count, 3);
  binn_test_check_bytes(list, expected, sizeof(expected));
  binn_destroy(list);
}

// {1: "add", 2: [-12345, 6789]} - 26 bytes.
static void test_nested_map(void) {
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
  IW_TEST_ASSERT_TRUE(binn_add(inner, *binnv(v), 0));
  binn_destroy(v);
  v = make_u16(6789);
  IW_TEST_ASSERT_TRUE(binn_add(inner, *binnv(v), 0));
  binn_destroy(v);
  v = make_text("add");
  IW_TEST_ASSERT_TRUE(binn_add_mapped(map, 1, *binnv(v)));
  binn_destroy(v);
  IW_TEST_ASSERT_TRUE(binn_add_mapped(map, 2, *binnv(inner)));
  binn_destroy(inner);
  IW_TEST_ASSERT_EQUAL(map->count, 2);
  binn_test_check_bytes(map, expected, sizeof(expected));
  binn_destroy(map);
}

// [{"id":1,"name":"John"},{"id":2,"name":"Eric"}] - 43 bytes.
static void test_list_of_objects(void) {
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
    IW_TEST_ASSERT_TRUE(binn_add_keyed(obj, "id", *binnv(v)));
    binn_destroy(v);
    v = make_text(i == 0 ? "John" : "Eric");
    IW_TEST_ASSERT_TRUE(binn_add_keyed(obj, "name", *binnv(v)));
    binn_destroy(v);
    IW_TEST_ASSERT_TRUE(binn_add(list, *binnv(obj), 0));
    binn_destroy(obj);
  }
  IW_TEST_ASSERT_EQUAL(list->count, 2);
  binn_test_check_bytes(list, expected, sizeof(expected));
  binn_destroy(list);
}

// binn_add_keyed2(): explicit key length, no null termination required.
static void test_add_keyed2(void) {
  struct binn *obj = binn_create_object(0);
  struct binn *v = make_u8(7);
  struct binn found;
  char key[] = { 'a', 'b', 'c', 'd', 'e' }; // not null terminated
  static const uint8_t expected[] = {
    0xE2, 0x09, 0x01,
    0x03, 'a', 'b', 'c',
    0x20, 0x07
  };
  IW_TEST_ASSERT_TRUE(binn_add_keyed2(obj, key, 3, *binnv(v)));
  IW_TEST_ASSERT_EQUAL(obj->count, 1);
  binn_test_check_bytes(obj, expected, sizeof(expected));
  IW_TEST_ASSERT_TRUE(binn_keyed_get(obj, "abc", &found));
  IW_TEST_ASSERT_EQUAL(found.val.nval, 7);
  binn_destroy(obj);
  binn_destroy(v);
}

// Empty key: a zero key pointer is allowed with a zero length.
static void test_add_keyed2_empty_key(void) {
  struct binn *obj = binn_create_object(0);
  struct binn *v = make_u8(1);
  struct binn found;
  IW_TEST_ASSERT_TRUE(binn_add_keyed2(obj, 0, 0, *binnv(v)));
  IW_TEST_ASSERT_TRUE(binn_keyed_get(obj, "", &found));
  IW_TEST_ASSERT_EQUAL(found.val.nval, 1);
  binn_destroy(obj);
  binn_destroy(v);
}

static void test_add_keyed2_limits(void) {
  struct binn *obj = binn_create_object(0);
  struct binn *v = make_u8(1);
  struct binn found;
  char big[BINN_KEY_MAXLEN + 2];
  memset(big, 'k', sizeof(big));
  big[BINN_KEY_MAXLEN] = 0;
  IW_TEST_ASSERT_FALSE(binn_add_keyed2(obj, big, BINN_KEY_MAXLEN + 1, *binnv(v))); // too long
  IW_TEST_ASSERT_FALSE(binn_add_keyed2(obj, 0, 3, *binnv(v)));                     // NULL key with a length
  IW_TEST_ASSERT_FALSE(binn_add_keyed2(obj, "k", 1, bv(0, 0)));                    // no value
  IW_TEST_ASSERT_EQUAL(obj->count, 0);
  IW_TEST_ASSERT_TRUE(binn_add_keyed2(obj, big, BINN_KEY_MAXLEN, *binnv(v)));      // max length
  IW_TEST_ASSERT_TRUE(binn_keyed_get(obj, big, &found));
  IW_TEST_ASSERT_EQUAL(found.val.nval, 1);
  binn_destroy(obj);
  binn_destroy(v);
}

int main(int argc, char *argv[]) {
  static const struct iw_test_case cases[] = {
    IW_TEST_CASE(test_empty_containers),
    IW_TEST_CASE(test_object),
    IW_TEST_CASE(test_list),
    IW_TEST_CASE(test_nested_map),
    IW_TEST_CASE(test_list_of_objects),
    IW_TEST_CASE(test_add_keyed2),
    IW_TEST_CASE(test_add_keyed2_empty_key),
    IW_TEST_CASE(test_add_keyed2_limits),
  };
  return iw_test_run(argc, argv, IW_TEST_SUITE("containers", cases, 0, 0));
}
