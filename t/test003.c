// Iteration, lookups, wrapping, copy-on-write and traversal.
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
  IW_TEST_ASSERT_TRUE(binn_add_keyed(obj, "id", *binnv(v)));
  binn_destroy(v);
  v = make_text("John");
  IW_TEST_ASSERT_TRUE(binn_add_keyed(obj, "name", *binnv(v)));
  binn_destroy(v);
  v = make_scalar(BINN_TYPE_DOUBLE, &points, sizeof(points));
  IW_TEST_ASSERT_TRUE(binn_add_keyed(obj, "points", *binnv(v)));
  binn_destroy(v);
  v = make_bool(true);
  IW_TEST_ASSERT_TRUE(binn_add_keyed(obj, "active", *binnv(v)));
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

static void test_keyed_get(void) {
  struct binn *obj = make_object_fixture();
  double points = 30.5;
  struct binn found;
  IW_TEST_ASSERT_TRUE(binn_keyed_get(obj, "id", &found));
  IW_TEST_ASSERT_EQUAL(found.type, BINN_TYPE_UINT8);
  IW_TEST_ASSERT_EQUAL(found.val.nval, 1);
  IW_TEST_ASSERT_TRUE(binn_keyed_get(obj, "name", &found));
  IW_TEST_ASSERT_EQUAL(found.type, BINN_TYPE_TEXT);
  IW_TEST_ASSERT_EQUAL(found.used, 4);
  IW_TEST_ASSERT_MEM_EQUAL(found.val.bval, "John", 4);
  IW_TEST_ASSERT_TRUE(binn_keyed_get(obj, "points", &found));
  IW_TEST_ASSERT_EQUAL(found.type, BINN_TYPE_DOUBLE);
  {
    uint64_t points_bits;
    memcpy(&points_bits, &points, sizeof(points_bits));
    IW_TEST_ASSERT_EQUAL(found.val.nval, points_bits);
  }
  IW_TEST_ASSERT_TRUE(binn_keyed_get(obj, "active", &found));
  IW_TEST_ASSERT_EQUAL(found.type, BINN_TYPE_TRUE);
  IW_TEST_ASSERT_FALSE(binn_keyed_get(obj, "missing", &found));
  binn_destroy(obj);
}

// Object iteration yields keys and values.
static void test_object_iteration(void) {
  struct binn *obj = make_object_fixture();
  struct binn_iter it;
  int n = 0;
  IW_TEST_ASSERT_TRUE(binn_iter_init(*binnv(obj), &it));
  while (binn_iter_next(&it)) {
    IW_TEST_ASSERT_TRUE(it.exists);
    IW_TEST_ASSERT_PTR_NOT_NULL(it.key);
    IW_TEST_ASSERT_EQUAL(it.idx, (int32_t) strlen(it.key)); // string key length
    switch (n) {
      case 0:
        IW_TEST_ASSERT_STR_EQUAL(it.key, "id");
        IW_TEST_ASSERT_EQUAL(it.binn.val.nval, 1);
        break;
      case 1:
        IW_TEST_ASSERT_STR_EQUAL(it.key, "name");
        break;
      case 2:
        IW_TEST_ASSERT_STR_EQUAL(it.key, "points");
        break;
      case 3:
        IW_TEST_ASSERT_STR_EQUAL(it.key, "active");
        break;
      default:
        IW_TEST_FAIL("unexpected object element");
        break;
    }
    n++;
  }
  IW_TEST_ASSERT_EQUAL(n, 4);
  IW_TEST_ASSERT_FALSE(it.exists);
  binn_iter_close(&it);
  binn_destroy(obj);
}

static void test_map_lookup_iteration(void) {
  struct binn *map = binn_create_map(0);
  struct binn *v;
  struct binn found;
  struct binn_iter it;
  int ids[3];
  int n = 0;
  v = make_u8(10);
  IW_TEST_ASSERT_TRUE(binn_add_mapped(map, 1, *binnv(v)));
  binn_destroy(v);
  v = make_text("the value");
  IW_TEST_ASSERT_TRUE(binn_add_mapped(map, 5, *binnv(v)));
  binn_destroy(v);
  v = make_bool(true);
  IW_TEST_ASSERT_TRUE(binn_add_mapped(map, 7, *binnv(v)));
  binn_destroy(v);

  IW_TEST_ASSERT_TRUE(binn_indexed_get(map, 1, &found));
  IW_TEST_ASSERT_EQUAL(found.val.nval, 10);
  IW_TEST_ASSERT_TRUE(binn_indexed_get(map, 5, &found));
  IW_TEST_ASSERT(found.type == BINN_TYPE_TEXT && found.used == 9);
  IW_TEST_ASSERT_TRUE(binn_indexed_get(map, 7, &found));
  IW_TEST_ASSERT_EQUAL(found.type, BINN_TYPE_TRUE);
  IW_TEST_ASSERT_FALSE(binn_indexed_get(map, 3, &found));

  IW_TEST_ASSERT_TRUE(binn_iter_init(*binnv(map), &it));
  while (binn_iter_next(&it)) {
    IW_TEST_ASSERT_PTR_NULL(it.key);
    IW_TEST_ASSERT_TRUE(n < 3);
    ids[n++] = it.idx;
  }
  IW_TEST_ASSERT_EQUAL(n, 3);
  IW_TEST_ASSERT(ids[0] == 1 && ids[1] == 5 && ids[2] == 7);
  binn_iter_close(&it);
  binn_destroy(map);
}

static void test_list_iteration(void) {
  struct binn *list = binn_create_list(0);
  struct binn *v;
  struct binn_iter it;
  struct binn found;
  int32_t expect[3] = { 123, -456, 789 };
  int n = 0;
  int16_t i16 = -456;
  uint16_t u16 = 789;
  v = make_u8(123);
  IW_TEST_ASSERT_TRUE(binn_add(list, *binnv(v), 0));
  binn_destroy(v);
  v = make_scalar(BINN_TYPE_INT16, &i16, sizeof(i16));
  IW_TEST_ASSERT_TRUE(binn_add(list, *binnv(v), 0));
  binn_destroy(v);
  v = make_scalar(BINN_TYPE_UINT16, &u16, sizeof(u16));
  IW_TEST_ASSERT_TRUE(binn_add(list, *binnv(v), 0));
  binn_destroy(v);

  IW_TEST_ASSERT_TRUE(binn_iter_init(*binnv(list), &it));
  while (binn_iter_next(&it)) {
    int64_t got = (int64_t) it.binn.val.nval;
    IW_TEST_ASSERT_PTR_NULL(it.key);
    IW_TEST_ASSERT_EQUAL(it.idx, -1); // no numeric key for list elements
    IW_TEST_ASSERT_EQUAL(got, expect[n]);
    n++;
  }
  IW_TEST_ASSERT_EQUAL(n, 3);
  binn_iter_close(&it);

  IW_TEST_ASSERT_TRUE(binn_indexed_get(list, 1, &found));
  IW_TEST_ASSERT_EQUAL((int64_t) found.val.nval, -456);
  IW_TEST_ASSERT_FALSE(binn_indexed_get(list, 3, &found));
  binn_destroy(list);
}

// Depth first traversal.
static void test_visit(void) {
  struct binn *root = binn_create_object(0);
  struct binn *inner_list = binn_create_list(0);
  struct binn *inner_obj = binn_create_object(0);
  struct binn *v;
  int count = 0;
  int stopped = 0;

  v = make_u8(1);
  IW_TEST_ASSERT_TRUE(binn_add(inner_list, *binnv(v), 0));
  binn_destroy(v);
  v = make_u8(2);
  IW_TEST_ASSERT_TRUE(binn_add(inner_list, *binnv(v), 0));
  binn_destroy(v);
  v = make_u8(3);
  IW_TEST_ASSERT_TRUE(binn_add_keyed(inner_obj, "c", *binnv(v)));
  binn_destroy(v);

  IW_TEST_ASSERT_TRUE(binn_add_keyed(root, "a", *binnv(inner_list)));
  binn_destroy(inner_list);
  IW_TEST_ASSERT_TRUE(binn_add_keyed(root, "b", *binnv(inner_obj)));
  binn_destroy(inner_obj);

  binn_visit(root, count_visitor, &count);
  IW_TEST_ASSERT_EQUAL(count, 5); // a, a[0], a[1], b, b.c

  binn_visit(root, stop_visitor, &stopped);
  IW_TEST_ASSERT_EQUAL(stopped, 1);

  binn_destroy(root);
}

// Wrapping a serialized buffer, copy-on-write, and binn_copy_from().
static void test_wrap_and_copy(void) {
  struct binn *obj = make_object_fixture();
  struct binn_val buf = binn_serialize_allocated(obj);
  size_t buflen = buf.size;
  uint8_t *saved;
  struct binn holder;
  struct binn *w;
  struct binn *v;
  bool casted = true;
  struct binn found;
  struct binn_iter it;

  IW_TEST_ASSERT_PTR_NOT_NULL_FATAL(buf.buf);
  saved = (uint8_t*) malloc(buflen);
  memcpy(saved, buf.buf, buflen);

  IW_TEST_ASSERT_TRUE(binn_is_serialized_buffer(buf));
  IW_TEST_ASSERT_FALSE(binn_is_struct_instance(buf));
  IW_TEST_ASSERT_TRUE(binn_is_any(buf));
  IW_TEST_ASSERT_TRUE(binn_is_any(*binnv(obj)));

  w = binn_from(&holder, buf, &casted);
  IW_TEST_ASSERT(w == &holder);
  IW_TEST_ASSERT_FALSE(casted);
  IW_TEST_ASSERT(w->flags & BINN_FLG_WRAPPED_BINN);
  IW_TEST_ASSERT_EQUAL(w->count, obj->count);
  IW_TEST_ASSERT_EQUAL(w->size, buflen);

  // Reading from the wrapped buffer.
  IW_TEST_ASSERT_TRUE(binn_keyed_get(w, "name", &found));
  IW_TEST_ASSERT_EQUAL(found.type, BINN_TYPE_TEXT);
  IW_TEST_ASSERT(found.used == 4 && memcmp(found.val.bval, "John", 4) == 0);

  IW_TEST_ASSERT_TRUE(binn_iter_init(*binnv(w), &it));
  IW_TEST_ASSERT_TRUE(binn_iter_next(&it));
  IW_TEST_ASSERT_STR_EQUAL(it.key, "id");
  binn_iter_close(&it);

  // Casting an existing struct.
  {
    bool c2 = false;
    struct binn *c = binn_from(0, *binnv(obj), &c2);
    IW_TEST_ASSERT(c == obj && c2 == true);
  }

  // Modification triggers copy-on-write.
  v = make_u8(42);
  IW_TEST_ASSERT_TRUE(binn_add_keyed(w, "extra", *binnv(v)));
  binn_destroy(v);
  IW_TEST_ASSERT_FALSE(w->flags & BINN_FLAGS_WRAPPED);
  IW_TEST_ASSERT_MEM_EQUAL(buf.buf, saved, buflen); // the wrapped buffer is untouched

  // binn_copy_from() produces an independent construction mode value.
  {
    struct binn *cp = binn_copy_from(0, *binnv(obj));
    struct binn *cp2 = binn_copy_from(0, buf);
    struct binn *cv;
    IW_TEST_ASSERT_PTR_NOT_NULL(cp);
    IW_TEST_ASSERT_PTR_NOT_NULL(cp2);
    IW_TEST_ASSERT_FALSE(cp->flags & BINN_FLAGS_WRAPPED);
    IW_TEST_ASSERT_EQUAL(cp->count, obj->count);
    IW_TEST_ASSERT_EQUAL(cp2->count, obj->count);
    // The struct copy serializes identically to the source.
    {
      struct binn_val s1 = binn_serialize_allocated(obj);
      struct binn_val s2 = binn_serialize_allocated(cp);
      IW_TEST_ASSERT(s1.buf != 0 && s2.buf != 0);
      IW_TEST_ASSERT_EQUAL(s1.size, s2.size);
      IW_TEST_ASSERT_MEM_EQUAL(s1.buf, s2.buf, s1.size);
      free(s1.buf);
      free(s2.buf);
    }
    cv = make_u8(7);
    IW_TEST_ASSERT_TRUE(binn_add_keyed(cp, "added", *binnv(cv)));
    binn_destroy(cv);
    IW_TEST_ASSERT_FALSE(binn_keyed_get(obj, "added", &found));
    binn_destroy(cp);
    binn_destroy(cp2);
  }

  IW_TEST_ASSERT_FALSE(binn_destroy(w)); // stack holder: not freed
  free(saved);
  free(buf.buf);
  binn_destroy(obj);
}

int main(int argc, char *argv[]) {
  static const struct iw_test_case cases[] = {
    IW_TEST_CASE(test_keyed_get),
    IW_TEST_CASE(test_object_iteration),
    IW_TEST_CASE(test_map_lookup_iteration),
    IW_TEST_CASE(test_list_iteration),
    IW_TEST_CASE(test_visit),
    IW_TEST_CASE(test_wrap_and_copy),
  };
  return iw_test_run(argc, argv, IW_TEST_SUITE("iterate_lookup_copy", cases, 0, 0));
}
