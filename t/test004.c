// Error handling, edge cases and lifecycle semantics.
#define IW_TEST_HARNESS "binnc"
#include "tests.h"

static int destroyed_count;

static void on_destroy_cb(struct binn *b) {
  destroyed_count++;
  free(b->user_data);
}

static struct binn* make_u8(uint8_t v) {
  struct binn_create_spec spec;
  memset(&spec, 0, sizeof(spec));
  spec.type = BINN_TYPE_UINT8;
  spec.value = bv(&v, sizeof(v));
  return binn_create_value(0, &spec);
}

static void test_invalid_arguments(void) {
  struct binn holder;
  bool casted = true;
  IW_TEST_ASSERT_PTR_NULL(binn_create_value(0, 0));
  IW_TEST_ASSERT_FALSE(binn_is_struct_instance(bv(0, 0)));
  IW_TEST_ASSERT_FALSE(binn_is_serialized_buffer(bv(0, 0)));
  IW_TEST_ASSERT_FALSE(binn_is_any(bv(0, 0)));
  IW_TEST_ASSERT_TRUE(binn_destroy(0));
  IW_TEST_ASSERT_PTR_NULL(binn_from(&holder, bv(0, 0), &casted));
  IW_TEST_ASSERT_FALSE(casted);
  IW_TEST_ASSERT_FALSE(binn_serialize(0, 0, 0));
  IW_TEST_ASSERT_PTR_NULL(binn_serialize_allocated(0).buf);
  IW_TEST_ASSERT_PTR_NULL(binn_serialize_into(0, (uint8_t*) &holder, sizeof(holder)).buf);
  IW_TEST_ASSERT_PTR_NULL(binn_copy_from(0, bv(0, 0)));
  IW_TEST_ASSERT_FALSE(binn_iter_init(bv(0, 0), 0));
  IW_TEST_ASSERT_FALSE(binn_iter_next(0));
  binn_iter_close(0);
  {
    struct binn *l = binn_create_list(0);
    IW_TEST_ASSERT_TRUE(binn_destroy(l)); // heap allocated struct
  }
}

// A short buffer must never be mistaken for a `struct binn`.
static void test_short_buffer(void) {
  uint8_t tiny[1] = { 0x1F };
  IW_TEST_ASSERT_FALSE(binn_is_struct_instance(bv(tiny, sizeof(tiny))));
}

static void test_container_type_mismatch(void) {
  struct binn *list = binn_create_list(0);
  struct binn *map = binn_create_map(0);
  struct binn *obj = binn_create_object(0);
  struct binn *v = make_u8(1);
  IW_TEST_ASSERT_FALSE(binn_add(map, *binnv(v), 0));
  IW_TEST_ASSERT_FALSE(binn_add(obj, *binnv(v), 0));
  IW_TEST_ASSERT_FALSE(binn_add_mapped(list, 1, *binnv(v)));
  IW_TEST_ASSERT_FALSE(binn_add_mapped(obj, 1, *binnv(v)));
  IW_TEST_ASSERT_FALSE(binn_add_keyed(list, "k", *binnv(v)));
  IW_TEST_ASSERT_FALSE(binn_add_keyed(map, "k", *binnv(v)));
  IW_TEST_ASSERT_FALSE(binn_add(list, bv(0, 0), 0));
  IW_TEST_ASSERT_FALSE(binn_add_keyed(obj, 0, *binnv(v)));
  IW_TEST_ASSERT_FALSE(binn_add_keyed(0, "k", *binnv(v)));
  binn_destroy(v);
  binn_destroy(list);
  binn_destroy(map);
  binn_destroy(obj);
}

// Duplicate keys/indexes are appended, oversized keys are rejected.
static void test_duplicate_keys(void) {
  struct binn *map = binn_create_map(0);
  struct binn *obj = binn_create_object(0);
  struct binn *v1 = make_u8(1);
  struct binn *v2 = make_u8(2);
  struct binn found;
  char bigkey[BINN_KEY_MAXLEN + 40];
  memset(bigkey, 'k', sizeof(bigkey) - 1);
  bigkey[sizeof(bigkey) - 1] = 0;

  IW_TEST_ASSERT_TRUE(binn_add_mapped(map, 10, *binnv(v1)));
  IW_TEST_ASSERT_TRUE(binn_add_mapped(map, 10, *binnv(v2)));
  IW_TEST_ASSERT_EQUAL(map->count, 2);
  IW_TEST_ASSERT_TRUE(binn_indexed_get(map, 10, &found));
  IW_TEST_ASSERT_EQUAL(found.val.nval, 1); // the first occurrence wins

  IW_TEST_ASSERT_TRUE(binn_add_keyed(obj, "k", *binnv(v1)));
  IW_TEST_ASSERT_TRUE(binn_add_keyed(obj, "k", *binnv(v2)));
  IW_TEST_ASSERT_EQUAL(obj->count, 2);
  IW_TEST_ASSERT_TRUE(binn_keyed_get(obj, "k", &found));
  IW_TEST_ASSERT_EQUAL(found.val.nval, 1); // the first occurrence wins

  IW_TEST_ASSERT_FALSE(binn_add_keyed(obj, bigkey, *binnv(v1)));
  IW_TEST_ASSERT_EQUAL(obj->count, 2);

  // Duplicates survive a serialization round trip.
  {
    struct binn_val sb = binn_serialize_allocated(map);
    struct binn holder;
    struct binn *w;
    struct binn_iter it;
    bool casted = true;
    int n = 0;
    IW_TEST_ASSERT_PTR_NOT_NULL(sb.buf);
    w = binn_from(&holder, sb, &casted);
    IW_TEST_ASSERT(w != 0 && casted == false);
    IW_TEST_ASSERT_EQUAL(w->count, 2);
    IW_TEST_ASSERT_TRUE(binn_iter_init(*binnv(w), &it));
    while (binn_iter_next(&it)) {
      n++;
    }
    IW_TEST_ASSERT_EQUAL(n, 2);
    binn_iter_close(&it);
    IW_TEST_ASSERT_FALSE(binn_destroy(w));
    free(sb.buf);
  }
  binn_destroy(v1);
  binn_destroy(v2);
  binn_destroy(map);
  binn_destroy(obj);
}

static void test_empty_strings_and_blobs(void) {
  static const uint8_t e_text[] = { 0xA0, 0x00, 0x00 };
  static const uint8_t e_blob[] = { 0xC0, 0x00 };
  struct binn_create_spec spec;
  struct binn *b;
  memset(&spec, 0, sizeof(spec));
  spec.type = BINN_TYPE_TEXT;
  b = binn_create_value(0, &spec);
  IW_TEST_ASSERT_PTR_NOT_NULL(b);
  binn_test_check_bytes(b, e_text, sizeof(e_text));
  binn_destroy(b);
  memset(&spec, 0, sizeof(spec));
  spec.type = BINN_TYPE_BLOB;
  b = binn_create_value(0, &spec);
  binn_test_check_bytes(b, e_blob, sizeof(e_blob));
  binn_destroy(b);
}

static void test_invalid_create_specs(void) {
  uint32_t v = 1;
  struct binn_create_spec spec;
  memset(&spec, 0, sizeof(spec));
  spec.type = BINN_TYPE_UINT32;
  spec.value = bv(&v, 2); // smaller than the DWORD storage
  IW_TEST_ASSERT_PTR_NULL(binn_create_value(0, &spec));

  memset(&spec, 0, sizeof(spec));
  spec.type = BINN_TYPE_TEXT;
  spec.value = bv(0, 5); // no data pointer
  IW_TEST_ASSERT_PTR_NULL(binn_create_value(0, &spec));
}

static void test_malformed_buffers(void) {
  uint8_t bad_container[8] = { 0xE0, 0x03, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00 };
  uint8_t bad_type[8] = { 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
  uint8_t small_container[8] = { 0xE0, MIN_BINN_SIZE - 1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
  static const uint8_t valid_obj[] = {
    0xE2, 0x11, 0x01,
    0x05, 'h', 'e', 'l', 'l', 'o',
    0xA0, 0x05, 'w', 'o', 'r', 'l', 'd', 0x00
  };
  bool casted = true;
  IW_TEST_ASSERT_FALSE(binn_is_serialized_buffer(bv(bad_container, sizeof(bad_container))));
  IW_TEST_ASSERT_PTR_NULL(binn_from(0, bv(bad_container, sizeof(bad_container)), &casted));
  IW_TEST_ASSERT_FALSE(casted);
  IW_TEST_ASSERT_FALSE(binn_is_serialized_buffer(bv(bad_type, sizeof(bad_type))));
  IW_TEST_ASSERT_FALSE(binn_is_serialized_buffer(bv(small_container, sizeof(small_container))));
  IW_TEST_ASSERT_PTR_NULL(binn_from(0, bv(small_container, sizeof(small_container)), &casted));
  // A fully valid value, and the same value truncated by one byte.
  IW_TEST_ASSERT_TRUE(binn_is_serialized_buffer(bv(valid_obj, sizeof(valid_obj))));
  IW_TEST_ASSERT_FALSE(binn_is_serialized_buffer(bv(valid_obj, sizeof(valid_obj) - 1)));
  IW_TEST_ASSERT_PTR_NULL(binn_from(0, bv(valid_obj, sizeof(valid_obj) - 1), &casted));
}

// binn_from_unchecked() skips full structural validation but stays memory safe.
static void test_from_unchecked(void) {
  uint8_t bad_container[8] = { 0xE0, 0x03, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00 };
  static const uint8_t valid_obj[] = {
    0xE2, 0x11, 0x01,
    0x05, 'h', 'e', 'l', 'l', 'o',
    0xA0, 0x05, 'w', 'o', 'r', 'l', 'd', 0x00
  };
  struct binn holder;
  struct binn *w;
  struct binn_iter it;
  bool casted = true;

  // The checked variant rejects the inconsistent child count.
  IW_TEST_ASSERT_PTR_NULL(binn_from(&holder, bv(bad_container, sizeof(bad_container)), &casted));

  // The unchecked variant wraps it, but iteration terminates safely.
  w = binn_from_unchecked(&holder, bv(bad_container, sizeof(bad_container)), &casted);
  IW_TEST_ASSERT(w == &holder && casted == false);
  IW_TEST_ASSERT_EQUAL(w->count, 5);
  IW_TEST_ASSERT_TRUE(binn_iter_init(*binnv(w), &it));
  IW_TEST_ASSERT_FALSE(binn_iter_next(&it));
  IW_TEST_ASSERT_FALSE(it.exists);
  binn_iter_close(&it);
  IW_TEST_ASSERT_FALSE(binn_destroy(w));

  // A truncated buffer is still rejected by the root parse in both variants.
  IW_TEST_ASSERT_PTR_NULL(binn_from_unchecked(&holder, bv(valid_obj, sizeof(valid_obj) - 1), &casted));

  // A valid buffer wraps and reads normally.
  w = binn_from_unchecked(&holder, bv(valid_obj, sizeof(valid_obj)), &casted);
  IW_TEST_ASSERT(w == &holder && casted == false);
  IW_TEST_ASSERT_EQUAL(w->count, 1);
  IW_TEST_ASSERT_TRUE(binn_iter_init(*binnv(w), &it));
  IW_TEST_ASSERT_TRUE(binn_iter_next(&it));
  IW_TEST_ASSERT_STR_EQUAL(it.key, "hello");
  binn_iter_close(&it);
  IW_TEST_ASSERT_FALSE(binn_destroy(w));
}

// A serialized buffer that merely starts with the magic number must not be treated as a struct.
static void test_magic_spoof(void) {
  uint8_t spoof[128];
  struct binn holder;
  struct binn *b;
  bool casted = true;
  memset(spoof, 0, sizeof(spoof));
  spoof[0] = 0x1F;
  spoof[1] = 0xB1;
  spoof[2] = 0x22;
  spoof[3] = 0x1F;
  spoof[4] = 0xFF;
  spoof[5] = 0x01; // invalid flag bits
  IW_TEST_ASSERT_FALSE(binn_is_struct_instance(bv(spoof, sizeof(spoof))));
  IW_TEST_ASSERT_TRUE(binn_is_serialized_buffer(bv(spoof, sizeof(spoof))));
  b = binn_from(&holder, bv(spoof, sizeof(spoof)), &casted);
  IW_TEST_ASSERT(b == &holder && casted == false);
  IW_TEST_ASSERT_FALSE(binn_destroy(b));
}

// Containers using the extended (two byte) type encoding.
static void test_extended_container_types(void) {
  binn_type_t et = (binn_type_t) ((7u << 13) | 0x1000u | 21u); // 0xF015
  static const uint8_t e_empty[] = { 0xF0, 0x15, 0x04, 0x00 };
  static const uint8_t e_one[] = { 0xF0, 0x15, 0x06, 0x01, 0x20, 0x2A };
  struct binn holder;
  struct binn *w;
  struct binn *cp;
  bool casted = true;

  IW_TEST_ASSERT_EQUAL(et, 0xF015);

  // Empty construction mode container with an extended type.
  {
    struct binn_create_spec spec;
    struct binn *b;
    memset(&spec, 0, sizeof(spec));
    spec.type = et;
    b = binn_create_value(0, &spec);
    IW_TEST_ASSERT_PTR_NOT_NULL(b);
    binn_test_check_bytes(b, e_empty, sizeof(e_empty));
    binn_destroy(b);
  }

  // Wrapping and copying an extended type container holding one child.
  IW_TEST_ASSERT_TRUE(binn_is_serialized_buffer(bv(e_one, sizeof(e_one))));
  w = binn_from(&holder, bv(e_one, sizeof(e_one)), &casted);
  IW_TEST_ASSERT(w != 0 && casted == false);
  binn_test_check_bytes(w, e_one, sizeof(e_one));
  IW_TEST_ASSERT_FALSE(binn_destroy(w));

  cp = binn_copy_from(0, bv(e_one, sizeof(e_one)));
  IW_TEST_ASSERT_PTR_NOT_NULL(cp);
  binn_test_check_bytes(cp, e_one, sizeof(e_one));
  binn_destroy(cp);
}

static void test_serialize_into_sizes(void) {
  uint8_t buf[2] = { 0, 0 };
  struct binn *v = make_u8(1);
  IW_TEST_ASSERT_PTR_NULL(binn_serialize_into(v, buf, 1).buf);
  IW_TEST_ASSERT_PTR_NULL(binn_serialize_into(v, buf, 0).buf);
  IW_TEST_ASSERT(binn_serialize_into(v, buf, sizeof(buf)).buf == buf);
  binn_destroy(v);
}

// Wrapped payload values are not copied and the destroy hook is called.
static void test_wrapped_payload_destroy(void) {
  uint8_t *ext = (uint8_t*) malloc(5);
  struct binn_create_spec spec;
  struct binn *b;
  static const uint8_t expected[] = { 0xA0, 0x05, 'h', 'e', 'l', 'l', 'o', 0x00 };
  memcpy(ext, "hello", 5);
  memset(&spec, 0, sizeof(spec));
  spec.type = BINN_TYPE_TEXT;
  spec.flags = BINN_FLG_WRAPPED_PAYLOAD;
  spec.value = bv(ext, 5);
  spec.on_destroy = on_destroy_cb;
  destroyed_count = 0;
  b = binn_create_value(0, &spec);
  IW_TEST_ASSERT_PTR_NOT_NULL(b);
  IW_TEST_ASSERT_TRUE(b->flags & BINN_FLG_WRAPPED_PAYLOAD);
  IW_TEST_ASSERT_TRUE(b->val.bval == ext); // no copy
  b->user_data = ext;
  binn_test_check_bytes(b, expected, sizeof(expected));
  IW_TEST_ASSERT_TRUE(binn_destroy(b));
  IW_TEST_ASSERT_EQUAL(destroyed_count, 1);
}

// binn_destroy() on a zeroed holder is harmless.
static void test_destroy_zeroed_holder(void) {
  struct binn holder;
  memset(&holder, 0, sizeof(holder));
  IW_TEST_ASSERT_FALSE(binn_destroy(&holder));
  IW_TEST_ASSERT_FALSE(binn_destroy(&holder));
  holder.magic = BINN_MAJIC_NUMBER;
  IW_TEST_ASSERT_FALSE(binn_destroy(&holder));
}

int main(int argc, char *argv[]) {
  static const struct iw_test_case cases[] = {
    IW_TEST_CASE(test_invalid_arguments),
    IW_TEST_CASE(test_short_buffer),
    IW_TEST_CASE(test_container_type_mismatch),
    IW_TEST_CASE(test_duplicate_keys),
    IW_TEST_CASE(test_empty_strings_and_blobs),
    IW_TEST_CASE(test_invalid_create_specs),
    IW_TEST_CASE(test_malformed_buffers),
    IW_TEST_CASE(test_from_unchecked),
    IW_TEST_CASE(test_magic_spoof),
    IW_TEST_CASE(test_extended_container_types),
    IW_TEST_CASE(test_serialize_into_sizes),
    IW_TEST_CASE(test_wrapped_payload_destroy),
    IW_TEST_CASE(test_destroy_zeroed_holder),
  };
  return iw_test_run(argc, argv, IW_TEST_SUITE("errors_lifecycle", cases, 0, 0));
}
