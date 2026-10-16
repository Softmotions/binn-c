// Scalar accessors: binn_blob_get(), binn_text_get(), binn_u64_get(), binn_i64_get(),
// binn_f64_get(), binn_bool_get().
#include "../binnc.h"
#include "tests.h"

static struct binn* mk(binn_type_t type, const void *p, size_t n) {
  struct binn_create_spec spec;
  memset(&spec, 0, sizeof(spec));
  spec.type = type;
  spec.value = bv(p, n);
  return binn_create_value(0, &spec);
}

int main(int argc, char *argv[]) {
  // binn_blob_get(): string storage and blobs.
  {
    struct binn *b = binn_create_text(0, "hello", -1);
    struct binn_val v = binn_blob_get(b);
    BINN_ASSERT(v.buf != 0 && v.size == 5 && memcmp(v.buf, "hello", 5) == 0);
    binn_destroy(b);

    {
      uint8_t bytes[3] = { 0x00, 0x01, 0x02 };
      b = binn_create_blob(0, bytes, sizeof(bytes));
      v = binn_blob_get(b);
      BINN_ASSERT(v.buf != 0 && v.size == 3 && memcmp(v.buf, bytes, 3) == 0);
      binn_destroy(b);
    }

    b = binn_create_text(0, 0, 0);
    v = binn_blob_get(b);
    BINN_ASSERT(v.buf == 0 && v.size == 0);
    binn_destroy(b);

    b = binn_create_u64c(0, 5);
    v = binn_blob_get(b);
    BINN_ASSERT(v.buf == 0 && v.size == 0);
    binn_destroy(b);

    b = binn_create_list(0);
    v = binn_blob_get(b);
    BINN_ASSERT(v.buf == 0 && v.size == 0);
    binn_destroy(b);

    v = binn_blob_get(0);
    BINN_ASSERT(v.buf == 0 && v.size == 0);

    // Any string sub-type, not only BINN_TYPE_TEXT.
    b = mk(BINN_TYPE_DATETIME, "2026-10-15 00:00:00", 19);
    v = binn_blob_get(b);
    BINN_ASSERT(v.size == 19 && memcmp(v.buf, "2026-10-15 00:00:00", 19) == 0);
    binn_destroy(b);

    // Wrapped buffer: the payload points into the serialized binn.
    {
      struct binn holder;
      struct binn *w;
      b = binn_create_text(0, "wrapped", -1);
      {
        struct binn_val ser = binn_serialize_allocated(b);
        BINN_ASSERT(ser.buf != 0);
        binn_destroy(b);
        w = binn_from(&holder, ser, 0);
        BINN_ASSERT(w != 0);
        v = binn_blob_get(w);
        BINN_ASSERT(v.buf != 0 && v.size == 7 && memcmp(v.buf, "wrapped", 7) == 0);
        binn_destroy(w);
        free(ser.buf);
      }
    }
  }

  // binn_text_get(): text only, always null terminated.
  {
    struct binn *b = binn_create_text(0, "hello", -1);
    struct binn_val v = binn_text_get(b);
    BINN_ASSERT(v.buf != 0 && v.size == 5);
    BINN_ASSERT(memcmp(v.buf, "hello", 5) == 0);
    BINN_ASSERT(((const char*) v.buf)[5] == 0);
    binn_destroy(b);

    // Other null terminated string variants are text.
    b = mk(BINN_TYPE_DECIMAL, "123.456", 7);
    v = binn_text_get(b);
    BINN_ASSERT(v.size == 7 && memcmp(v.buf, "123.456", 7) == 0);
    BINN_ASSERT(((const char*) v.buf)[7] == 0);
    binn_destroy(b);

    // Blobs are not text.
    {
      uint8_t bytes[3] = { 0x01, 0x00, 0x02 };
      b = binn_create_blob(0, bytes, sizeof(bytes));
      v = binn_text_get(b);
      BINN_ASSERT(v.buf == 0 && v.size == 0);
      binn_destroy(b);
    }

    // Numeric, container, empty and null inputs.
    b = binn_create_u64c(0, 1);
    v = binn_text_get(b);
    BINN_ASSERT(v.buf == 0 && v.size == 0);
    binn_destroy(b);
    b = binn_create_list(0);
    v = binn_text_get(b);
    BINN_ASSERT(v.buf == 0 && v.size == 0);
    binn_destroy(b);
    b = binn_create_text(0, 0, 0);
    v = binn_text_get(b);
    BINN_ASSERT(v.buf == 0 && v.size == 0);
    binn_destroy(b);
    v = binn_text_get(0);
    BINN_ASSERT(v.buf == 0 && v.size == 0);

    // Wrapped text from a serialized buffer is null terminated too.
    {
      struct binn holder;
      struct binn *w;
      b = binn_create_text(0, "wrapped", -1);
      {
        struct binn_val ser = binn_serialize_allocated(b);
        binn_destroy(b);
        w = binn_from(&holder, ser, 0);
        BINN_ASSERT(w != 0);
        v = binn_text_get(w);
        BINN_ASSERT(v.size == 7 && memcmp(v.buf, "wrapped", 7) == 0);
        BINN_ASSERT(((const char*) v.buf)[7] == 0);
        binn_destroy(w);
        free(ser.buf);
      }
    }

    // Copies keep the text null terminated.
    {
      struct binn *src = binn_create_text(0, "copy", -1);
      struct binn *cp = binn_copy_from(0, *binnv(src));
      BINN_ASSERT(cp != 0);
      v = binn_text_get(cp);
      BINN_ASSERT(v.size == 4 && memcmp(v.buf, "copy", 4) == 0);
      BINN_ASSERT(((const char*) v.buf)[4] == 0);
      binn_destroy(cp);
      {
        struct binn_val ser = binn_serialize_allocated(src);
        struct binn *cp2 = binn_copy_from(0, ser);
        BINN_ASSERT(cp2 != 0);
        v = binn_text_get(cp2);
        BINN_ASSERT(v.size == 4 && memcmp(v.buf, "copy", 4) == 0);
        BINN_ASSERT(((const char*) v.buf)[4] == 0);
        binn_destroy(cp2);
        free(ser.buf);
      }
      binn_destroy(src);
    }

    // A serialized string with a non NUL terminator is rejected.
    {
      static const uint8_t bad[] = { 0xA0, 0x03, 'a', 'b', 'c', 0xFF };
      BINN_ASSERT(!binn_is_serialized_buffer(bv(bad, sizeof(bad))));
      BINN_ASSERT(binn_from(0, bv(bad, sizeof(bad)), 0) == 0);
    }
  }

  // binn_u64_get()
  {
    struct binn *b = binn_create_u64c(0, 0x1226652FEull);
    BINN_ASSERT(binn_u64_get(b) == 0x1226652FEull);
    binn_destroy(b);
    b = binn_create_u64c(0, UINT64_MAX);
    BINN_ASSERT(binn_u64_get(b) == UINT64_MAX);
    binn_destroy(b);
    b = binn_create_u64c(0, 255);
    BINN_ASSERT(binn_u64_get(b) == 255);
    binn_destroy(b);
    b = binn_create_i64c(0, -1); // negative integers wrap to two's complement
    BINN_ASSERT(binn_u64_get(b) == UINT64_MAX);
    binn_destroy(b);
    b = binn_create_i64c(0, INT64_MIN);
    BINN_ASSERT(binn_u64_get(b) == (uint64_t) INT64_MIN);
    binn_destroy(b);
    b = binn_create_f32c(0, 2.75f);
    BINN_ASSERT(binn_u64_get(b) == 2);
    binn_destroy(b);
    b = binn_create_f64c(0, 3.9);
    BINN_ASSERT(binn_u64_get(b) == 3);
    binn_destroy(b);
    b = binn_create_f64c(0, -1.5);
    BINN_ASSERT(binn_u64_get(b) == 0);
    binn_destroy(b);
    b = binn_create_f64c(0, 1e300);
    BINN_ASSERT(binn_u64_get(b) == 0);
    binn_destroy(b);
    b = binn_create_text(0, "x", -1);
    BINN_ASSERT(binn_u64_get(b) == 0);
    binn_destroy(b);
    b = binn_create_bool(0, true);
    BINN_ASSERT(binn_u64_get(b) == 0);
    binn_destroy(b);
    BINN_ASSERT(binn_u64_get(0) == 0);
  }

  // binn_i64_get()
  {
    struct binn *b = binn_create_i64c(0, -1);
    BINN_ASSERT(binn_i64_get(b) == -1);
    binn_destroy(b);
    b = binn_create_i64c(0, INT64_MIN);
    BINN_ASSERT(binn_i64_get(b) == INT64_MIN);
    binn_destroy(b);
    b = binn_create_u64c(0, 12345);
    BINN_ASSERT(binn_i64_get(b) == 12345);
    binn_destroy(b);
    b = binn_create_f32c(0, -2.75f);
    BINN_ASSERT(binn_i64_get(b) == -2);
    binn_destroy(b);
    b = binn_create_f64c(0, 3.9);
    BINN_ASSERT(binn_i64_get(b) == 3);
    binn_destroy(b);
    b = binn_create_f64c(0, 1e300);
    BINN_ASSERT(binn_i64_get(b) == 0);
    binn_destroy(b);
    b = binn_create_f64c(0, -1e300);
    BINN_ASSERT(binn_i64_get(b) == 0);
    binn_destroy(b);
    BINN_ASSERT(binn_i64_get(0) == 0);
  }

  // binn_f64_get()
  {
    struct binn *b = binn_create_i64c(0, -5);
    BINN_ASSERT(binn_f64_get(b) == -5.0);
    binn_destroy(b);
    b = binn_create_u64c(0, 123456789);
    BINN_ASSERT(binn_f64_get(b) == 123456789.0);
    binn_destroy(b);
    b = binn_create_f32c(0, 1.5f);
    BINN_ASSERT(binn_f64_get(b) == 1.5);
    binn_destroy(b);
    b = binn_create_f64c(0, 0.1);
    BINN_ASSERT(binn_f64_get(b) == 0.1);
    binn_destroy(b);
    b = binn_create_text(0, "x", -1);
    BINN_ASSERT(binn_f64_get(b) == 0.0);
    binn_destroy(b);
    BINN_ASSERT(binn_f64_get(0) == 0.0);
  }

  // binn_bool_get()
  {
    struct binn *b = binn_create_bool(0, true);
    BINN_ASSERT(binn_bool_get(b) == true);
    binn_destroy(b);
    b = binn_create_bool(0, false);
    BINN_ASSERT(binn_bool_get(b) == false);
    binn_destroy(b);
    b = binn_create_null(0);
    BINN_ASSERT(binn_bool_get(b) == false);
    binn_destroy(b);
    b = binn_create_u64c(0, 1);
    BINN_ASSERT(binn_bool_get(b) == false);
    binn_destroy(b);
    b = binn_create_text(0, "x", -1);
    BINN_ASSERT(binn_bool_get(b) == false);
    binn_destroy(b);
    BINN_ASSERT(binn_bool_get(0) == false);
  }

  // Getters work on values obtained from lookups (wrapped mode).
  {
    struct binn *obj = binn_create_object(0);
    struct binn *u = binn_create_u64c(0, 42);
    struct binn *t = binn_create_text(0, "hi", -1);
    struct binn found;
    BINN_ASSERT(binn_add_keyed(obj, "n", *binnv(u)));
    BINN_ASSERT(binn_add_keyed(obj, "s", *binnv(t)));
    binn_destroy(u);
    binn_destroy(t);
    BINN_ASSERT(binn_keyed_get(obj, "n", &found));
    BINN_ASSERT(binn_u64_get(&found) == 42);
    BINN_ASSERT(binn_i64_get(&found) == 42);
    BINN_ASSERT(binn_f64_get(&found) == 42.0);
    BINN_ASSERT(binn_keyed_get(obj, "s", &found));
    {
      struct binn_val sv = binn_blob_get(&found);
      BINN_ASSERT(sv.size == 2 && memcmp(sv.buf, "hi", 2) == 0);
    }
    binn_destroy(obj);
  }

  return binn_assertions_failed > 0 ? 1 : 0;
}
