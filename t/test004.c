// Error handling, edge cases and lifecycle semantics.
#include "../binnc.h"
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
  spec.value = &v;
  spec.valuesz = sizeof(v);
  return binn_create_value(0, &spec);
}

int main(int argc, char *argv[]) {
  // Invalid arguments.
  {
    struct binn holder;
    bool casted = true;
    BINN_ASSERT(binn_create_value(0, 0) == 0);
    BINN_ASSERT(!binn_is_struct_instance(0));
    BINN_ASSERT(!binn_is_serialized_buffer(0));
    BINN_ASSERT(!binn_is_any(0));
    BINN_ASSERT(binn_destroy(0) == true);
    BINN_ASSERT(binn_from(&holder, 0, &casted) == 0);
    BINN_ASSERT(casted == false);
    BINN_ASSERT(binn_serialize(0, 0, 0) == false);
    BINN_ASSERT(binn_serialize_allocated(0) == 0);
    BINN_ASSERT(binn_serialize_into(0, (uint8_t*) &holder, sizeof(holder)) == 0);
    BINN_ASSERT(binn_copy_from(0, 0) == 0);
    BINN_ASSERT(!binn_iter_init(0, 0));
    BINN_ASSERT(!binn_iter_next(0));
    binn_iter_close(0);
    {
      struct binn *l = binn_create_list(0);
      BINN_ASSERT(binn_destroy(l) == true); // heap allocated struct
    }
  }

  // Container type mismatches.
  {
    struct binn *list = binn_create_list(0);
    struct binn *map = binn_create_map(0);
    struct binn *obj = binn_create_object(0);
    struct binn *v = make_u8(1);
    BINN_ASSERT(!binn_add(map, v, 0));
    BINN_ASSERT(!binn_add(obj, v, 0));
    BINN_ASSERT(!binn_add_mapped(list, 1, v));
    BINN_ASSERT(!binn_add_mapped(obj, 1, v));
    BINN_ASSERT(!binn_add_keyed(list, "k", v));
    BINN_ASSERT(!binn_add_keyed(map, "k", v));
    BINN_ASSERT(!binn_add(list, 0, 0));
    BINN_ASSERT(!binn_add_keyed(obj, 0, v));
    BINN_ASSERT(!binn_add_keyed(0, "k", v));
    binn_destroy(v);
    binn_destroy(list);
    binn_destroy(map);
    binn_destroy(obj);
  }

  // Duplicate keys and indexes, oversized keys.
  {
    struct binn *map = binn_create_map(0);
    struct binn *obj = binn_create_object(0);
    struct binn *v = make_u8(1);
    char bigkey[BINN_KEY_MAXLEN + 40];
    memset(bigkey, 'k', sizeof(bigkey) - 1);
    bigkey[sizeof(bigkey) - 1] = 0;

    BINN_ASSERT(binn_add_mapped(map, 10, v));
    BINN_ASSERT(!binn_add_mapped(map, 10, v));
    BINN_ASSERT(binn_add_keyed(obj, "k", v));
    BINN_ASSERT(!binn_add_keyed(obj, "k", v));
    BINN_ASSERT(!binn_add_keyed(obj, bigkey, v));
    binn_destroy(v);
    binn_destroy(map);
    binn_destroy(obj);
  }

  // Empty strings and blobs.
  {
    static const uint8_t e_text[] = { 0xA0, 0x00, 0x00 };
    static const uint8_t e_blob[] = { 0xC0, 0x00 };
    struct binn *b = binn_create_value(0, &(struct binn_create_spec) { .type = BINN_TYPE_TEXT });
    BINN_ASSERT(b != 0);
    binn_test_check_bytes(b, e_text, sizeof(e_text));
    binn_destroy(b);
    b = binn_create_value(0, &(struct binn_create_spec) { .type = BINN_TYPE_BLOB });
    binn_test_check_bytes(b, e_blob, sizeof(e_blob));
    binn_destroy(b);
  }

  // Invalid create specs.
  {
    uint32_t v = 1;
    struct binn_create_spec spec;
    memset(&spec, 0, sizeof(spec));
    spec.type = BINN_TYPE_UIN32;
    spec.value = &v;
    spec.valuesz = 2; // smaller than the DWORD storage
    BINN_ASSERT(binn_create_value(0, &spec) == 0);

    memset(&spec, 0, sizeof(spec));
    spec.type = BINN_TYPE_TEXT;
    spec.value = 0;
    spec.valuesz = 5; // no data pointer
    BINN_ASSERT(binn_create_value(0, &spec) == 0);
  }

  // Malformed serialized buffers.
  {
    uint8_t bad_container[8] = { 0xE0, 0x03, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00 };
    uint8_t bad_type[8] = { 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    uint8_t small_container[8] = { 0xE0, MIN_BINN_SIZE - 1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    bool casted = true;
    BINN_ASSERT(!binn_is_serialized_buffer(bad_container));
    BINN_ASSERT(binn_from(0, bad_container, &casted) == 0);
    BINN_ASSERT(casted == false);
    BINN_ASSERT(!binn_is_serialized_buffer(bad_type));
    BINN_ASSERT(!binn_is_serialized_buffer(small_container));
    BINN_ASSERT(binn_from(0, small_container, &casted) == 0);
  }

  // binn_serialize_into() size checks.
  {
    uint8_t buf[2] = { 0, 0 };
    struct binn *v = make_u8(1);
    BINN_ASSERT(binn_serialize_into(v, buf, 1) == 0);
    BINN_ASSERT(binn_serialize_into(v, buf, 0) == 0);
    BINN_ASSERT(binn_serialize_into(v, buf, sizeof(buf)) == buf);
    binn_destroy(v);
  }

  // Wrapped payload values are not copied and the destroy hook is called.
  {
    uint8_t *ext = (uint8_t*) malloc(5);
    struct binn_create_spec spec;
    struct binn *b;
    static const uint8_t expected[] = { 0xA0, 0x05, 'h', 'e', 'l', 'l', 'o', 0x00 };
    memcpy(ext, "hello", 5);
    memset(&spec, 0, sizeof(spec));
    spec.type = BINN_TYPE_TEXT;
    spec.flags = BINN_FLG_WRAPPED_PAYLOAD;
    spec.value = ext;
    spec.valuesz = 5;
    spec.on_destroy = on_destroy_cb;
    b = binn_create_value(0, &spec);
    BINN_ASSERT(b != 0);
    BINN_ASSERT(b->flags & BINN_FLG_WRAPPED_PAYLOAD);
    BINN_ASSERT(b->val.bval == ext); // no copy
    b->user_data = ext;
    binn_test_check_bytes(b, expected, sizeof(expected));
    BINN_ASSERT(binn_destroy(b) == true);
    BINN_ASSERT(destroyed_count == 1);
  }

  // binn_destroy() on a zeroed holder is harmless.
  {
    struct binn holder;
    memset(&holder, 0, sizeof(holder));
    BINN_ASSERT(binn_destroy(&holder) == false);
    BINN_ASSERT(binn_destroy(&holder) == false);
    holder.magic = BINN_MAJIC_NUMBER;
    BINN_ASSERT(binn_destroy(&holder) == false);
  }

  return binn_assertions_failed > 0 ? 1 : 0;
}
