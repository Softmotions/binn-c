// Large containers, streaming serialization, round trips and nesting limits.
#include "../binnc.h"
#include "tests.h"

static struct binn* make_u8(uint8_t v) {
  struct binn_create_spec spec;
  memset(&spec, 0, sizeof(spec));
  spec.type = BINN_TYPE_UINT8;
  spec.value = &v;
  spec.valuesz = sizeof(v);
  return binn_create_value(0, &spec);
}

static struct binn* make_text(const char *s) {
  struct binn_create_spec spec;
  memset(&spec, 0, sizeof(spec));
  spec.type = BINN_TYPE_TEXT;
  spec.value = (void*) s;
  spec.valuesz = strlen(s);
  return binn_create_value(0, &spec);
}

struct stream_acc {
  uint8_t data[4096];
  size_t  len;
  int     final_calls;
};

static void stream_consumer(uint8_t *data, size_t len, void *op) {
  struct stream_acc *a = (struct stream_acc*) op;
  if (!data && !len) {
    a->final_calls++;
    return;
  }
  BINN_ASSERT(a->len + len <= sizeof(a->data));
  memcpy(a->data + a->len, data, len);
  a->len += len;
}

int main(int argc, char *argv[]) {
  // A list with more than 127 elements uses a four byte count parameter.
  {
    struct binn *list = binn_create_list(0);
    uint8_t *buf;
    size_t i;
    BINN_ASSERT(list != 0);
    for (i = 0; i < 200; i++) {
      struct binn *v = make_u8(0);
      BINN_ASSERT(binn_add(list, v, 0));
      binn_destroy(v);
    }
    BINN_ASSERT(list->count == 200);
    BINN_ASSERT(list->size == 409);
    buf = (uint8_t*) binn_serialize_allocated(list);
    BINN_ASSERT(buf != 0);
    if (buf) {
      static const uint8_t header[] = {
        0xE0, 0x80, 0x00, 0x01, 0x99, // total size 409
        0x80, 0x00, 0x00, 0xC8        // count 200
      };
      BINN_ASSERT_MEM(buf, header, sizeof(header));
      BINN_ASSERT(buf[9] == 0x20 && buf[10] == 0x00);
      BINN_ASSERT(buf[407] == 0x20 && buf[408] == 0x00);
      free(buf);
    }
    binn_destroy(list);
  }

  // A list with more than 127 elements survives a wrap round trip.
  {
    struct binn *list = binn_create_list(0);
    struct binn holder;
    struct binn *w;
    void *buf;
    bool casted = true;
    size_t i;
    for (i = 0; i < 200; i++) {
      struct binn *v = make_u8((uint8_t) i);
      BINN_ASSERT(binn_add(list, v, 0));
      binn_destroy(v);
    }
    buf = binn_serialize_allocated(list);
    BINN_ASSERT(buf != 0);
    w = binn_from(&holder, buf, &casted);
    BINN_ASSERT(w != 0 && casted == false);
    BINN_ASSERT(w->count == 200);
    {
      struct binn found;
      BINN_ASSERT(binn_indexed_get(w, 127, &found));
      BINN_ASSERT(found.val.nval == 127);
      BINN_ASSERT(binn_indexed_get(w, 199, &found));
      BINN_ASSERT(found.val.nval == 199);
    }
    BINN_ASSERT(binn_destroy(w) == false);
    free(buf);
    binn_destroy(list);
  }

  // Streaming serialization matches the allocated buffer.
  {
    struct binn *obj = binn_create_object(0);
    struct binn *inner = binn_create_list(0);
    struct binn *v;
    void *buf;
    struct stream_acc acc;
    memset(&acc, 0, sizeof(acc));

    v = make_u8(1);
    BINN_ASSERT(binn_add(inner, v, 0));
    binn_destroy(v);
    v = make_text("value");
    BINN_ASSERT(binn_add(inner, v, 0));
    binn_destroy(v);
    BINN_ASSERT(binn_add_keyed(obj, "list", inner));
    binn_destroy(inner);
    v = make_text("stream");
    BINN_ASSERT(binn_add_keyed(obj, "name", v));
    binn_destroy(v);

    buf = binn_serialize_allocated(obj);
    BINN_ASSERT(buf != 0);
    BINN_ASSERT(binn_serialize(obj, stream_consumer, &acc));
    BINN_ASSERT(acc.final_calls == 1);
    BINN_ASSERT(acc.len == obj->size);
    if (buf) {
      BINN_ASSERT_MEM(acc.data, buf, obj->size);
      free(buf);
    }
    binn_destroy(obj);
  }

  // Serialize -> wrap -> serialize produces identical bytes.
  {
    struct binn *obj = binn_create_object(0);
    struct binn *inner = binn_create_list(0);
    struct binn *v;
    void *buf1;
    void *buf2;
    struct binn *w;
    struct binn holder;
    bool casted = true;
    int32_t idx;

    v = make_u8(7);
    BINN_ASSERT(binn_add(inner, v, &idx));
    binn_destroy(v);
    v = make_text("round");
    BINN_ASSERT(binn_add(inner, v, 0));
    binn_destroy(v);
    BINN_ASSERT(binn_add_keyed(obj, "items", inner));
    binn_destroy(inner);

    buf1 = binn_serialize_allocated(obj);
    BINN_ASSERT(buf1 != 0);
    w = binn_from(&holder, buf1, &casted);
    BINN_ASSERT(w != 0);
    buf2 = binn_serialize_allocated(w);
    BINN_ASSERT(buf2 != 0);
    if (buf1 && buf2) {
      BINN_ASSERT(obj->size == w->size);
      BINN_ASSERT_MEM(buf1, buf2, obj->size);
    }
    free(buf2);
    binn_destroy(w);
    free(buf1);
    binn_destroy(obj);
  }

  // The reader must accept non minimal (four byte) size/count encodings.
  {
    // A short string stored with a four byte size parameter.
    static uint8_t wide_string[] = {
      0xA0, 0x80, 0x00, 0x00, 0x02, 'h', 'i', 0x00
    };
    // A one element list stored with four byte size and count parameters.
    static uint8_t wide_list[] = {
      0xE0, 0x80, 0x00, 0x00, 0x0B, // size 11
      0x80, 0x00, 0x00, 0x01,       // count 1
      0x20, 0x2A                    // uint8 42
    };
    struct binn holder;
    struct binn *w;
    struct binn found;
    struct binn_iter it;
    bool casted = true;

    w = binn_from(&holder, wide_string, &casted);
    BINN_ASSERT(w != 0 && casted == false);
    BINN_ASSERT(w->type == BINN_TYPE_TEXT);
    BINN_ASSERT(w->used == 2 && memcmp(w->val.bval, "hi", 2) == 0);
    BINN_ASSERT(binn_destroy(w) == false);

    w = binn_from(&holder, wide_list, &casted);
    BINN_ASSERT(w != 0 && casted == false);
    BINN_ASSERT(w->count == 1);
    BINN_ASSERT(binn_iter_init(w, &it));
    BINN_ASSERT(binn_iter_next(&it));
    BINN_ASSERT(it.binn.val.nval == 42);
    BINN_ASSERT(!binn_iter_next(&it));
    binn_iter_close(&it);
    BINN_ASSERT(binn_indexed_get(w, 0, &found));
    BINN_ASSERT(found.val.nval == 42);
    BINN_ASSERT(binn_destroy(w) == false);
  }

  // Nesting is limited by BINN_MAX_NESTING_LEVELS.
  {
    struct binn *inner = binn_create_list(0);
    int built = 0;
    int i;
    for (i = 0; i < 300; i++) {
      struct binn *outer = binn_create_list(0);
      if (!binn_add(outer, inner, 0)) {
        binn_destroy(outer);
        break;
      }
      binn_destroy(inner);
      inner = outer;
      built++;
    }
    BINN_ASSERT(built < 300);
    BINN_ASSERT(built > 250);
    binn_destroy(inner);
  }

  return binn_assertions_failed > 0 ? 1 : 0;
}
