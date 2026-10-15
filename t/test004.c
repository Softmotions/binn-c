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
  spec.value = bv(&v, sizeof(v));
  return binn_create_value(0, &spec);
}

int main(int argc, char *argv[]) {
  // Invalid arguments.
  {
    struct binn holder;
    bool casted = true;
    BINN_ASSERT(binn_create_value(0, 0) == 0);
    BINN_ASSERT(!binn_is_struct_instance(bv(0, 0)));
    BINN_ASSERT(!binn_is_serialized_buffer(bv(0, 0)));
    BINN_ASSERT(!binn_is_any(bv(0, 0)));
    BINN_ASSERT(binn_destroy(0) == true);
    BINN_ASSERT(binn_from(&holder, bv(0, 0), &casted) == 0);
    BINN_ASSERT(casted == false);
    BINN_ASSERT(binn_serialize(0, 0, 0) == false);
    BINN_ASSERT(binn_serialize_allocated(0).buf == 0);
    BINN_ASSERT(binn_serialize_into(0, (uint8_t*) &holder, sizeof(holder)).buf == 0);
    BINN_ASSERT(binn_copy_from(0, bv(0, 0)) == 0);
    BINN_ASSERT(!binn_iter_init(bv(0, 0), 0));
    BINN_ASSERT(!binn_iter_next(0));
    binn_iter_close(0);
    {
      struct binn *l = binn_create_list(0);
      BINN_ASSERT(binn_destroy(l) == true); // heap allocated struct
    }
  }

  // A short buffer must never be mistaken for a `struct binn`.
  {
    uint8_t tiny[1] = { 0x1F };
    BINN_ASSERT(!binn_is_struct_instance(bv(tiny, sizeof(tiny))));
  }

  // Container type mismatches.
  {
    struct binn *list = binn_create_list(0);
    struct binn *map = binn_create_map(0);
    struct binn *obj = binn_create_object(0);
    struct binn *v = make_u8(1);
    BINN_ASSERT(!binn_add(map, *binnv(v), 0));
    BINN_ASSERT(!binn_add(obj, *binnv(v), 0));
    BINN_ASSERT(!binn_add_mapped(list, 1, *binnv(v)));
    BINN_ASSERT(!binn_add_mapped(obj, 1, *binnv(v)));
    BINN_ASSERT(!binn_add_keyed(list, "k", *binnv(v)));
    BINN_ASSERT(!binn_add_keyed(map, "k", *binnv(v)));
    BINN_ASSERT(!binn_add(list, bv(0, 0), 0));
    BINN_ASSERT(!binn_add_keyed(obj, 0, *binnv(v)));
    BINN_ASSERT(!binn_add_keyed(0, "k", *binnv(v)));
    binn_destroy(v);
    binn_destroy(list);
    binn_destroy(map);
    binn_destroy(obj);
  }

  // Duplicate keys/indexes are appended, oversized keys are rejected.
  {
    struct binn *map = binn_create_map(0);
    struct binn *obj = binn_create_object(0);
    struct binn *v1 = make_u8(1);
    struct binn *v2 = make_u8(2);
    struct binn found;
    char bigkey[BINN_KEY_MAXLEN + 40];
    memset(bigkey, 'k', sizeof(bigkey) - 1);
    bigkey[sizeof(bigkey) - 1] = 0;

    BINN_ASSERT(binn_add_mapped(map, 10, *binnv(v1)));
    BINN_ASSERT(binn_add_mapped(map, 10, *binnv(v2)));
    BINN_ASSERT(map->count == 2);
    BINN_ASSERT(binn_indexed_get(map, 10, &found));
    BINN_ASSERT(found.val.nval == 1); // the first occurrence wins

    BINN_ASSERT(binn_add_keyed(obj, "k", *binnv(v1)));
    BINN_ASSERT(binn_add_keyed(obj, "k", *binnv(v2)));
    BINN_ASSERT(obj->count == 2);
    BINN_ASSERT(binn_keyed_get(obj, "k", &found));
    BINN_ASSERT(found.val.nval == 1); // the first occurrence wins

    BINN_ASSERT(!binn_add_keyed(obj, bigkey, *binnv(v1)));
    BINN_ASSERT(obj->count == 2);

    // Duplicates survive a serialization round trip.
    {
      struct binn_val sb = binn_serialize_allocated(map);
      struct binn holder;
      struct binn *w;
      struct binn_iter it;
      bool casted = true;
      int n = 0;
      BINN_ASSERT(sb.buf != 0);
      w = binn_from(&holder, sb, &casted);
      BINN_ASSERT(w != 0 && casted == false);
      BINN_ASSERT(w->count == 2);
      BINN_ASSERT(binn_iter_init(*binnv(w), &it));
      while (binn_iter_next(&it)) {
        n++;
      }
      BINN_ASSERT(n == 2);
      binn_iter_close(&it);
      BINN_ASSERT(binn_destroy(w) == false);
      free(sb.buf);
    }
    binn_destroy(v1);
    binn_destroy(v2);
    binn_destroy(map);
    binn_destroy(obj);
  }

  // Empty strings and blobs.
  {
    static const uint8_t e_text[] = { 0xA0, 0x00, 0x00 };
    static const uint8_t e_blob[] = { 0xC0, 0x00 };
    struct binn_create_spec spec;
    struct binn *b;
    memset(&spec, 0, sizeof(spec));
    spec.type = BINN_TYPE_TEXT;
    b = binn_create_value(0, &spec);
    BINN_ASSERT(b != 0);
    binn_test_check_bytes(b, e_text, sizeof(e_text));
    binn_destroy(b);
    memset(&spec, 0, sizeof(spec));
    spec.type = BINN_TYPE_BLOB;
    b = binn_create_value(0, &spec);
    binn_test_check_bytes(b, e_blob, sizeof(e_blob));
    binn_destroy(b);
  }

  // Invalid create specs.
  {
    uint32_t v = 1;
    struct binn_create_spec spec;
    memset(&spec, 0, sizeof(spec));
    spec.type = BINN_TYPE_UINT32;
    spec.value = bv(&v, 2); // smaller than the DWORD storage
    BINN_ASSERT(binn_create_value(0, &spec) == 0);

    memset(&spec, 0, sizeof(spec));
    spec.type = BINN_TYPE_TEXT;
    spec.value = bv(0, 5); // no data pointer
    BINN_ASSERT(binn_create_value(0, &spec) == 0);
  }

  // Malformed and truncated serialized buffers.
  {
    uint8_t bad_container[8] = { 0xE0, 0x03, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00 };
    uint8_t bad_type[8] = { 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    uint8_t small_container[8] = { 0xE0, MIN_BINN_SIZE - 1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static const uint8_t valid_obj[] = {
      0xE2, 0x11, 0x01,
      0x05, 'h', 'e', 'l', 'l', 'o',
      0xA0, 0x05, 'w', 'o', 'r', 'l', 'd', 0x00
    };
    bool casted = true;
    BINN_ASSERT(!binn_is_serialized_buffer(bv(bad_container, sizeof(bad_container))));
    BINN_ASSERT(binn_from(0, bv(bad_container, sizeof(bad_container)), &casted) == 0);
    BINN_ASSERT(casted == false);
    BINN_ASSERT(!binn_is_serialized_buffer(bv(bad_type, sizeof(bad_type))));
    BINN_ASSERT(!binn_is_serialized_buffer(bv(small_container, sizeof(small_container))));
    BINN_ASSERT(binn_from(0, bv(small_container, sizeof(small_container)), &casted) == 0);
    // A fully valid value, and the same value truncated by one byte.
    BINN_ASSERT(binn_is_serialized_buffer(bv(valid_obj, sizeof(valid_obj))));
    BINN_ASSERT(!binn_is_serialized_buffer(bv(valid_obj, sizeof(valid_obj) - 1)));
    BINN_ASSERT(binn_from(0, bv(valid_obj, sizeof(valid_obj) - 1), &casted) == 0);
  }

  // binn_from_unchecked() skips full structural validation but stays memory safe.
  {
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
    BINN_ASSERT(binn_from(&holder, bv(bad_container, sizeof(bad_container)), &casted) == 0);

    // The unchecked variant wraps it, but iteration terminates safely.
    w = binn_from_unchecked(&holder, bv(bad_container, sizeof(bad_container)), &casted);
    BINN_ASSERT(w == &holder && casted == false);
    BINN_ASSERT(w->count == 5);
    BINN_ASSERT(binn_iter_init(*binnv(w), &it));
    BINN_ASSERT(!binn_iter_next(&it));
    BINN_ASSERT(!it.exists);
    binn_iter_close(&it);
    BINN_ASSERT(binn_destroy(w) == false);

    // A truncated buffer is still rejected by the root parse in both variants.
    BINN_ASSERT(binn_from_unchecked(&holder, bv(valid_obj, sizeof(valid_obj) - 1), &casted) == 0);

    // A valid buffer wraps and reads normally.
    w = binn_from_unchecked(&holder, bv(valid_obj, sizeof(valid_obj)), &casted);
    BINN_ASSERT(w == &holder && casted == false);
    BINN_ASSERT(w->count == 1);
    BINN_ASSERT(binn_iter_init(*binnv(w), &it));
    BINN_ASSERT(binn_iter_next(&it));
    BINN_ASSERT(strcmp(it.key, "hello") == 0);
    binn_iter_close(&it);
    BINN_ASSERT(binn_destroy(w) == false);
  }

  // A serialized buffer that merely starts with the magic number must not be treated as a struct.
  {
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
    BINN_ASSERT(!binn_is_struct_instance(bv(spoof, sizeof(spoof))));
    BINN_ASSERT(binn_is_serialized_buffer(bv(spoof, sizeof(spoof))));
    b = binn_from(&holder, bv(spoof, sizeof(spoof)), &casted);
    BINN_ASSERT(b == &holder && casted == false);
    BINN_ASSERT(binn_destroy(b) == false);
  }

  // Containers using the extended (two byte) type encoding.
  {
    binn_type_t et = (binn_type_t) ((7u << 13) | 0x1000u | 21u); // 0xF015
    static const uint8_t e_empty[] = { 0xF0, 0x15, 0x04, 0x00 };
    static const uint8_t e_one[] = { 0xF0, 0x15, 0x06, 0x01, 0x20, 0x2A };
    struct binn holder;
    struct binn *w;
    struct binn *cp;
    bool casted = true;

    BINN_ASSERT(et == 0xF015);

    // Empty construction mode container with an extended type.
    {
      struct binn_create_spec spec;
      struct binn *b;
      memset(&spec, 0, sizeof(spec));
      spec.type = et;
      b = binn_create_value(0, &spec);
      BINN_ASSERT(b != 0);
      binn_test_check_bytes(b, e_empty, sizeof(e_empty));
      binn_destroy(b);
    }

    // Wrapping and copying an extended type container holding one child.
    BINN_ASSERT(binn_is_serialized_buffer(bv(e_one, sizeof(e_one))));
    w = binn_from(&holder, bv(e_one, sizeof(e_one)), &casted);
    BINN_ASSERT(w != 0 && casted == false);
    binn_test_check_bytes(w, e_one, sizeof(e_one));
    BINN_ASSERT(binn_destroy(w) == false);

    cp = binn_copy_from(0, bv(e_one, sizeof(e_one)));
    BINN_ASSERT(cp != 0);
    binn_test_check_bytes(cp, e_one, sizeof(e_one));
    binn_destroy(cp);
  }

  // binn_serialize_into() size checks.
  {
    uint8_t buf[2] = { 0, 0 };
    struct binn *v = make_u8(1);
    BINN_ASSERT(binn_serialize_into(v, buf, 1).buf == 0);
    BINN_ASSERT(binn_serialize_into(v, buf, 0).buf == 0);
    BINN_ASSERT(binn_serialize_into(v, buf, sizeof(buf)).buf == buf);
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
    spec.value = bv(ext, 5);
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
