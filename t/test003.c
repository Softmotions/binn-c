// Iteration, lookups, wrapping, copy-on-write and traversal.
#include "../binnc.h"
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

static struct binn* make_bool(bool v) {
  struct binn_create_spec spec;
  memset(&spec, 0, sizeof(spec));
  spec.type = v ? BINN_TYPE_TRUE : BINN_TYPE_FALSE;
  return binn_create_value(0, &spec);
}

static struct binn* make_object_fixture(void) {
  struct binn *obj = binn_create_object(0);
  struct binn *v;
  double points = 30.5;
  v = make_u8(1);
  BINN_ASSERT(binn_add_keyed(obj, "id", *binnv(v)));
  binn_destroy(v);
  v = make_text("John");
  BINN_ASSERT(binn_add_keyed(obj, "name", *binnv(v)));
  binn_destroy(v);
  v = make_scalar(BINN_TYPE_DOUBLE, &points, sizeof(points));
  BINN_ASSERT(binn_add_keyed(obj, "points", *binnv(v)));
  binn_destroy(v);
  v = make_bool(true);
  BINN_ASSERT(binn_add_keyed(obj, "active", *binnv(v)));
  binn_destroy(v);
  return obj;
}

static int count_visitor(struct binn_iter *it, void *op) {
  (void) it;
  (*(int*) op)++;
  return 0;
}

static int stop_visitor(struct binn_iter *it, void *op) {
  (void) it;
  (*(int*) op)++;
  return -1;
}

int main(int argc, char *argv[]) {
  struct binn *obj = make_object_fixture();
  double points = 30.5;

  // Keyed lookups.
  {
    struct binn found;
    BINN_ASSERT(binn_keyed_get(obj, "id", &found));
    BINN_ASSERT(found.type == BINN_TYPE_UINT8);
    BINN_ASSERT(found.val.nval == 1);
    BINN_ASSERT(binn_keyed_get(obj, "name", &found));
    BINN_ASSERT(found.type == BINN_TYPE_TEXT);
    BINN_ASSERT(found.used == 4);
    BINN_ASSERT(memcmp(found.val.bval, "John", 4) == 0);
    BINN_ASSERT(binn_keyed_get(obj, "points", &found));
    BINN_ASSERT(found.type == BINN_TYPE_DOUBLE);
    {
      uint64_t points_bits;
      memcpy(&points_bits, &points, sizeof(points_bits));
      BINN_ASSERT(found.val.nval == points_bits);
    }
    BINN_ASSERT(binn_keyed_get(obj, "active", &found));
    BINN_ASSERT(found.type == BINN_TYPE_TRUE);
    BINN_ASSERT(!binn_keyed_get(obj, "missing", &found));
  }

  // Object iteration yields keys and values.
  {
    struct binn_iter it;
    int n = 0;
    BINN_ASSERT(binn_iter_init(*binnv(obj), &it));
    while (binn_iter_next(&it)) {
      BINN_ASSERT(it.exists);
      BINN_ASSERT(it.key != 0);
      BINN_ASSERT(it.idx == -1);
      switch (n) {
        case 0:
          BINN_ASSERT(strcmp(it.key, "id") == 0);
          BINN_ASSERT(it.binn.val.nval == 1);
          break;
        case 1:
          BINN_ASSERT(strcmp(it.key, "name") == 0);
          break;
        case 2:
          BINN_ASSERT(strcmp(it.key, "points") == 0);
          break;
        case 3:
          BINN_ASSERT(strcmp(it.key, "active") == 0);
          break;
        default:
          BINN_ASSERT(0);
          break;
      }
      n++;
    }
    BINN_ASSERT(n == 4);
    BINN_ASSERT(!it.exists);
    binn_iter_close(&it);
  }

  // Map lookups and iteration.
  {
    struct binn *map = binn_create_map(0);
    struct binn *v;
    struct binn found;
    struct binn_iter it;
    int ids[3];
    int n = 0;
    v = make_u8(10);
    BINN_ASSERT(binn_add_mapped(map, 1, *binnv(v)));
    binn_destroy(v);
    v = make_text("the value");
    BINN_ASSERT(binn_add_mapped(map, 5, *binnv(v)));
    binn_destroy(v);
    v = make_bool(true);
    BINN_ASSERT(binn_add_mapped(map, 7, *binnv(v)));
    binn_destroy(v);

    BINN_ASSERT(binn_indexed_get(map, 1, &found));
    BINN_ASSERT(found.val.nval == 10);
    BINN_ASSERT(binn_indexed_get(map, 5, &found));
    BINN_ASSERT(found.type == BINN_TYPE_TEXT && found.used == 9);
    BINN_ASSERT(binn_indexed_get(map, 7, &found));
    BINN_ASSERT(found.type == BINN_TYPE_TRUE);
    BINN_ASSERT(!binn_indexed_get(map, 3, &found));

    BINN_ASSERT(binn_iter_init(*binnv(map), &it));
    while (binn_iter_next(&it)) {
      BINN_ASSERT(it.key == 0);
      BINN_ASSERT(n < 3);
      ids[n++] = it.idx;
    }
    BINN_ASSERT(n == 3);
    BINN_ASSERT(ids[0] == 1 && ids[1] == 5 && ids[2] == 7);
    binn_iter_close(&it);
    binn_destroy(map);
  }

  // List iteration and indexed lookup.
  {
    struct binn *list = binn_create_list(0);
    struct binn *v;
    struct binn_iter it;
    struct binn found;
    int32_t expect[3] = { 123, -456, 789 };
    int n = 0;
    int16_t i16 = -456;
    uint16_t u16 = 789;
    v = make_u8(123);
    BINN_ASSERT(binn_add(list, *binnv(v), 0));
    binn_destroy(v);
    v = make_scalar(BINN_TYPE_INT16, &i16, sizeof(i16));
    BINN_ASSERT(binn_add(list, *binnv(v), 0));
    binn_destroy(v);
    v = make_scalar(BINN_TYPE_UINT16, &u16, sizeof(u16));
    BINN_ASSERT(binn_add(list, *binnv(v), 0));
    binn_destroy(v);

    BINN_ASSERT(binn_iter_init(*binnv(list), &it));
    while (binn_iter_next(&it)) {
      int64_t got = (int64_t) it.binn.val.nval;
      BINN_ASSERT(it.key == 0);
      BINN_ASSERT(it.idx == -1); // no numeric key for list elements
      BINN_ASSERT(got == expect[n]);
      n++;
    }
    BINN_ASSERT(n == 3);
    binn_iter_close(&it);

    BINN_ASSERT(binn_indexed_get(list, 1, &found));
    BINN_ASSERT((int64_t) found.val.nval == -456);
    BINN_ASSERT(!binn_indexed_get(list, 3, &found));
    binn_destroy(list);
  }

  // Depth first traversal.
  {
    struct binn *root = binn_create_object(0);
    struct binn *inner_list = binn_create_list(0);
    struct binn *inner_obj = binn_create_object(0);
    struct binn *v;
    int count = 0;
    int stopped = 0;

    v = make_u8(1);
    BINN_ASSERT(binn_add(inner_list, *binnv(v), 0));
    binn_destroy(v);
    v = make_u8(2);
    BINN_ASSERT(binn_add(inner_list, *binnv(v), 0));
    binn_destroy(v);
    v = make_u8(3);
    BINN_ASSERT(binn_add_keyed(inner_obj, "c", *binnv(v)));
    binn_destroy(v);

    BINN_ASSERT(binn_add_keyed(root, "a", *binnv(inner_list)));
    binn_destroy(inner_list);
    BINN_ASSERT(binn_add_keyed(root, "b", *binnv(inner_obj)));
    binn_destroy(inner_obj);

    binn_visit(root, count_visitor, &count);
    BINN_ASSERT(count == 5); // a, a[0], a[1], b, b.c

    binn_visit(root, stop_visitor, &stopped);
    BINN_ASSERT(stopped == 1);

    binn_destroy(root);
  }

  // Wrapping a serialized buffer, copy-on-write, and binn_copy_from().
  {
    struct binn_val buf = binn_serialize_allocated(obj);
    size_t buflen = buf.size;
    uint8_t *saved;
    struct binn holder;
    struct binn *w;
    struct binn *v;
    bool casted = true;
    struct binn found;
    struct binn_iter it;

    BINN_ASSERT(buf.buf != 0);
    saved = (uint8_t*) malloc(buflen);
    memcpy(saved, buf.buf, buflen);

    BINN_ASSERT(binn_is_serialized_buffer(buf));
    BINN_ASSERT(!binn_is_struct_instance(buf));
    BINN_ASSERT(binn_is_any(buf));
    BINN_ASSERT(binn_is_any(*binnv(obj)));

    w = binn_from(&holder, buf, &casted);
    BINN_ASSERT(w == &holder);
    BINN_ASSERT(casted == false);
    BINN_ASSERT(w->flags & BINN_FLG_WRAPPED_BINN);
    BINN_ASSERT(w->count == obj->count);
    BINN_ASSERT(w->size == buflen);

    // Reading from the wrapped buffer.
    BINN_ASSERT(binn_keyed_get(w, "name", &found));
    BINN_ASSERT(found.type == BINN_TYPE_TEXT);
    BINN_ASSERT(found.used == 4 && memcmp(found.val.bval, "John", 4) == 0);

    BINN_ASSERT(binn_iter_init(*binnv(w), &it));
    BINN_ASSERT(binn_iter_next(&it));
    BINN_ASSERT(strcmp(it.key, "id") == 0);
    binn_iter_close(&it);

    // Casting an existing struct.
    {
      bool c2 = false;
      struct binn *c = binn_from(0, *binnv(obj), &c2);
      BINN_ASSERT(c == obj && c2 == true);
    }

    // Modification triggers copy-on-write.
    v = make_u8(42);
    BINN_ASSERT(binn_add_keyed(w, "extra", *binnv(v)));
    binn_destroy(v);
    BINN_ASSERT(!(w->flags & BINN_FLAGS_WRAPPED));
    BINN_ASSERT_MEM(buf.buf, saved, buflen); // the wrapped buffer is untouched

    // binn_copy_from() produces an independent construction mode value.
    {
      struct binn *cp = binn_copy_from(0, *binnv(obj));
      struct binn *cp2 = binn_copy_from(0, buf);
      struct binn *cv;
      BINN_ASSERT(cp != 0);
      BINN_ASSERT(cp2 != 0);
      BINN_ASSERT(!(cp->flags & BINN_FLAGS_WRAPPED));
      BINN_ASSERT(cp->count == obj->count);
      BINN_ASSERT(cp2->count == obj->count);
      // The struct copy serializes identically to the source.
      {
        struct binn_val s1 = binn_serialize_allocated(obj);
        struct binn_val s2 = binn_serialize_allocated(cp);
        BINN_ASSERT(s1.buf != 0 && s2.buf != 0);
        BINN_ASSERT(s1.size == s2.size);
        BINN_ASSERT_MEM(s1.buf, s2.buf, s1.size);
        free(s1.buf);
        free(s2.buf);
      }
      cv = make_u8(7);
      BINN_ASSERT(binn_add_keyed(cp, "added", *binnv(cv)));
      binn_destroy(cv);
      BINN_ASSERT(binn_keyed_get(obj, "added", &found) == false);
      binn_destroy(cp);
      binn_destroy(cp2);
    }

    BINN_ASSERT(binn_destroy(w) == false); // stack holder: not freed
    free(saved);
    free(buf.buf);
  }

  binn_destroy(obj);
  return binn_assertions_failed > 0 ? 1 : 0;
}
