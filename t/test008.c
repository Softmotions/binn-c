// View mode: binn_create_view_from() and BINN_FLG_VIEW.
#define IW_TEST_HARNESS "binnc"
#include "tests.h"

static struct binn* make_u8(uint8_t v) {
  struct binn_create_spec spec;
  memset(&spec, 0, sizeof(spec));
  spec.type = BINN_TYPE_UINT8;
  spec.value = bv(&v, sizeof(v));
  return binn_create_value(0, &spec);
}

// View of a construction-mode container: borrows the source children buffer.
static void test_view_of_construction_container(void) {
  struct binn *src = binn_create_list(0);
  struct binn view;
  struct binn *v;
  struct binn_val ss, vs;
  int32_t idx;

  v = make_u8(1);
  IW_TEST_ASSERT_TRUE(binn_add(src, *binnv(v), &idx));
  binn_destroy(v);
  v = make_u8(2);
  IW_TEST_ASSERT_TRUE(binn_add(src, *binnv(v), 0));
  binn_destroy(v);

  IW_TEST_ASSERT_TRUE(binn_create_view_from(&view, src));
  IW_TEST_ASSERT_EQUAL(view.magic, BINN_MAJIC_NUMBER);
  IW_TEST_ASSERT_TRUE(view.flags & BINN_FLG_VIEW);
  IW_TEST_ASSERT_FALSE(view.flags & BINN_FLAGS_WRAPPED);
  IW_TEST_ASSERT_EQUAL(view.type, BINN_TYPE_LIST);
  IW_TEST_ASSERT_EQUAL(view.count, 2);
  IW_TEST_ASSERT_EQUAL(view.used, src->used);
  IW_TEST_ASSERT_TRUE(view.val.bval == src->val.bval);
  IW_TEST_ASSERT_EQUAL(view.allocated, 0);
  IW_TEST_ASSERT_TRUE(binn_is_container(&view));
  IW_TEST_ASSERT_TRUE(binn_is_struct_instance(*binnv(&view)));

  // Serializes exactly like the source.
  ss = binn_serialize_allocated(src);
  vs = binn_serialize_allocated(&view);
  IW_TEST_ASSERT(ss.buf != 0 && vs.buf != 0 && ss.size == vs.size);
  IW_TEST_ASSERT_MEM_EQUAL(ss.buf, vs.buf, ss.size);
  free(ss.buf);
  free(vs.buf);

  // Iteration sees the source children.
  {
    struct binn_iter it;
    int n = 0;
    IW_TEST_ASSERT_TRUE(binn_iter_init(*binnv(&view), &it));
    while (binn_iter_next(&it)) {
      n++;
    }
    IW_TEST_ASSERT_EQUAL(n, 2);
    binn_iter_close(&it);
  }

  // Modifying the view copies the data; the source is untouched.
  v = make_u8(3);
  IW_TEST_ASSERT_TRUE(binn_add(&view, *binnv(v), 0));
  binn_destroy(v);
  IW_TEST_ASSERT_FALSE(view.flags & (BINN_FLG_VIEW | BINN_FLAGS_WRAPPED));
  IW_TEST_ASSERT_EQUAL(view.count, 3);
  IW_TEST_ASSERT_TRUE(view.val.bval != src->val.bval);
  IW_TEST_ASSERT_EQUAL(src->count, 2);

  // Destroying the view does not free the source data.
  IW_TEST_ASSERT_FALSE(binn_destroy(&view));
  IW_TEST_ASSERT_EQUAL(src->count, 2);
  ss = binn_serialize_allocated(src);
  IW_TEST_ASSERT_PTR_NOT_NULL(ss.buf);
  free(ss.buf);
  binn_destroy(src);
}

// View of a wrapped container: keeps WRAPPED_BINN and COW copies the raw children.
static void test_view_of_wrapped_container(void) {
  struct binn *src = binn_create_list(0);
  struct binn *v = make_u8(7);
  struct binn_val ser;
  struct binn holder;
  struct binn *w;
  struct binn view;
  bool casted = true;

  IW_TEST_ASSERT(src != 0 && v != 0);
  IW_TEST_ASSERT_TRUE(binn_add(src, *binnv(v), 0));
  binn_destroy(v);
  ser = binn_serialize_allocated(src);
  IW_TEST_ASSERT_PTR_NOT_NULL(ser.buf);
  w = binn_from(&holder, ser, &casted);
  IW_TEST_ASSERT(w != 0 && casted == false && (w->flags & BINN_FLG_WRAPPED_BINN));

  IW_TEST_ASSERT_TRUE(binn_create_view_from(&view, w));
  IW_TEST_ASSERT((view.flags & BINN_FLG_VIEW) && (view.flags & BINN_FLG_WRAPPED_BINN));
  IW_TEST_ASSERT_EQUAL(view.size, w->size);
  IW_TEST_ASSERT_TRUE(view.wrapped.buf == w->wrapped.buf);
  IW_TEST_ASSERT_EQUAL(view.wrapped.size, w->wrapped.size);

  // Re-serializes byte-identically (raw wrapped buffer).
  {
    struct binn_val vs = binn_serialize_allocated(&view);
    IW_TEST_ASSERT(vs.buf != 0 && vs.size == ser.size);
    IW_TEST_ASSERT_MEM_EQUAL(vs.buf, ser.buf, vs.size);
    free(vs.buf);
  }

  // Modifying the view copies the wrapped children; the wrapped buffer is untouched.
  {
    uint8_t *saved = (uint8_t*) malloc(ser.size);
    memcpy(saved, ser.buf, ser.size);
    v = make_u8(9);
    IW_TEST_ASSERT_TRUE(binn_add(&view, *binnv(v), 0));
    binn_destroy(v);
    IW_TEST_ASSERT_FALSE(view.flags & (BINN_FLG_VIEW | BINN_FLAGS_WRAPPED));
    IW_TEST_ASSERT_EQUAL(view.count, 2);
    IW_TEST_ASSERT_EQUAL(w->count, 1);
    IW_TEST_ASSERT_MEM_EQUAL(ser.buf, saved, ser.size);
    free(saved);
  }
  IW_TEST_ASSERT_FALSE(binn_destroy(&view));
  IW_TEST_ASSERT_FALSE(binn_destroy(w));
  free(ser.buf);
  binn_destroy(src);
}

// View of a wrapped payload: keeps WRAPPED_PAYLOAD, text access and serialization work.
static void test_view_of_wrapped_payload(void) {
  struct binn *src = binn_create_text(0, "hello", -1);
  struct binn_val ser = binn_serialize_allocated(src);
  struct binn holder;
  struct binn *w;
  struct binn view;
  struct binn_val tv;
  bool casted = true;

  IW_TEST_ASSERT_PTR_NOT_NULL(ser.buf);
  binn_destroy(src);
  w = binn_from(&holder, ser, &casted);
  IW_TEST_ASSERT(w != 0 && casted == false && (w->flags & BINN_FLG_WRAPPED_PAYLOAD));

  IW_TEST_ASSERT_TRUE(binn_create_view_from(&view, w));
  IW_TEST_ASSERT((view.flags & BINN_FLG_VIEW) && (view.flags & BINN_FLG_WRAPPED_PAYLOAD));
  tv = binn_text_get(&view);
  IW_TEST_ASSERT(tv.size == 5 && memcmp(tv.buf, "hello", 5) == 0);
  IW_TEST_ASSERT(((const char*) tv.buf)[5] == 0);

  {
    struct binn_val vs = binn_serialize_allocated(&view);
    IW_TEST_ASSERT(vs.buf != 0 && vs.size == ser.size);
    IW_TEST_ASSERT_MEM_EQUAL(vs.buf, ser.buf, vs.size);
    free(vs.buf);
  }
  IW_TEST_ASSERT_FALSE(binn_destroy(&view));
  IW_TEST_ASSERT_FALSE(binn_destroy(w));
  free(ser.buf);
}

static void test_view_of_scalar(void) {
  struct binn *src = binn_create_u64c(0, 0x1226652FEull);
  struct binn view;
  IW_TEST_ASSERT_TRUE(binn_create_view_from(&view, src));
  IW_TEST_ASSERT_TRUE(view.flags & BINN_FLG_VIEW);
  IW_TEST_ASSERT_EQUAL(binn_u64_get(&view), 0x1226652FEull);
  IW_TEST_ASSERT_TRUE(binn_is_int_number(&view));
  IW_TEST_ASSERT_FALSE(binn_destroy(&view));
  binn_destroy(src);
}

// A copy of a view owns its own data.
static void test_copy_of_view(void) {
  struct binn *src = binn_create_list(0);
  struct binn view;
  struct binn *cp;
  struct binn *v = make_u8(5);
  IW_TEST_ASSERT_TRUE(binn_add(src, *binnv(v), 0));
  binn_destroy(v);
  IW_TEST_ASSERT_TRUE(binn_create_view_from(&view, src));
  cp = binn_copy_from(0, *binnv(&view));
  IW_TEST_ASSERT_PTR_NOT_NULL(cp);
  IW_TEST_ASSERT_FALSE(cp->flags & (BINN_FLG_VIEW | BINN_FLAGS_WRAPPED));
  IW_TEST_ASSERT_EQUAL(cp->count, 1);
  IW_TEST_ASSERT_FALSE(binn_destroy(&view));
  binn_destroy(src);
  {
    struct binn_val s = binn_serialize_allocated(cp);
    IW_TEST_ASSERT_PTR_NOT_NULL(s.buf);
    free(s.buf);
  }
  binn_destroy(cp);
}

// binn_view_materialize(): copy borrowed data and become an owner.
static void test_materialize_container_view(void) {
  struct binn *src = binn_create_list(0);
  struct binn view;
  struct binn *v = make_u8(1);
  struct binn_val s1, s2;
  IW_TEST_ASSERT_TRUE(binn_add(src, *binnv(v), 0));
  binn_destroy(v);
  IW_TEST_ASSERT_TRUE(binn_create_view_from(&view, src));
  IW_TEST_ASSERT_TRUE(binn_view_materialize(&view));
  IW_TEST_ASSERT_FALSE(view.flags & (BINN_FLG_VIEW | BINN_FLAGS_WRAPPED));
  IW_TEST_ASSERT_TRUE(view.val.bval != src->val.bval);
  IW_TEST_ASSERT_EQUAL(view.count, src->count);
  IW_TEST_ASSERT_TRUE(binn_view_materialize(&view)); // idempotent
  s1 = binn_serialize_allocated(src);
  s2 = binn_serialize_allocated(&view);
  IW_TEST_ASSERT(s1.buf != 0 && s2.buf != 0 && s1.size == s2.size);
  IW_TEST_ASSERT_MEM_EQUAL(s1.buf, s2.buf, s1.size);
  free(s1.buf);
  free(s2.buf);
  binn_destroy(src); // source gone; view owns its own data
  s2 = binn_serialize_allocated(&view);
  IW_TEST_ASSERT_PTR_NOT_NULL(s2.buf);
  free(s2.buf);
  IW_TEST_ASSERT_FALSE(binn_destroy(&view));
}

// Materializing a wrapped container view: the raw buffer can be freed afterwards.
static void test_materialize_wrapped_container(void) {
  struct binn *src = binn_create_list(0);
  struct binn *v = make_u8(3);
  struct binn_val ser;
  struct binn holder;
  struct binn *w;
  struct binn view;
  bool casted = true;
  IW_TEST_ASSERT_TRUE(binn_add(src, *binnv(v), 0));
  binn_destroy(v);
  ser = binn_serialize_allocated(src);
  IW_TEST_ASSERT_PTR_NOT_NULL(ser.buf);
  binn_destroy(src);
  w = binn_from(&holder, ser, &casted);
  IW_TEST_ASSERT(w != 0 && casted == false && (w->flags & BINN_FLG_WRAPPED_BINN));
  IW_TEST_ASSERT_TRUE(binn_create_view_from(&view, w));
  IW_TEST_ASSERT_TRUE(binn_view_materialize(&view));
  IW_TEST_ASSERT_FALSE(view.flags & (BINN_FLG_VIEW | BINN_FLAGS_WRAPPED));
  IW_TEST_ASSERT_EQUAL(view.count, 1);
  binn_destroy(w);
  free(ser.buf); // view owns its own copy now
  {
    struct binn_val s = binn_serialize_allocated(&view);
    IW_TEST_ASSERT_PTR_NOT_NULL(s.buf);
    free(s.buf);
  }
  binn_destroy(&view);
}

// Materializing a wrapped payload text view: the payload can be freed afterwards.
static void test_materialize_wrapped_payload(void) {
  struct binn *src = binn_create_text(0, "hello", -1);
  struct binn_val ser = binn_serialize_allocated(src);
  struct binn holder;
  struct binn *w;
  struct binn view;
  struct binn_val tv;
  bool casted = true;
  IW_TEST_ASSERT_PTR_NOT_NULL(ser.buf);
  binn_destroy(src);
  w = binn_from(&holder, ser, &casted);
  IW_TEST_ASSERT(w != 0 && casted == false && (w->flags & BINN_FLG_WRAPPED_PAYLOAD));
  IW_TEST_ASSERT_TRUE(binn_create_view_from(&view, w));
  IW_TEST_ASSERT_TRUE(binn_view_materialize(&view));
  IW_TEST_ASSERT_FALSE(view.flags & (BINN_FLG_VIEW | BINN_FLAGS_WRAPPED));
  binn_destroy(w);
  free(ser.buf);
  tv = binn_text_get(&view);
  IW_TEST_ASSERT(tv.size == 5 && memcmp(tv.buf, "hello", 5) == 0);
  IW_TEST_ASSERT(((const char*) tv.buf)[5] == 0);
  binn_destroy(&view);
}

// Numeric view, non-view no-op and NULL.
static void test_materialize_scalar_and_noop(void) {
  struct binn *src = binn_create_u64c(0, 77);
  struct binn view;
  IW_TEST_ASSERT_TRUE(binn_create_view_from(&view, src));
  IW_TEST_ASSERT_TRUE(binn_view_materialize(&view));
  IW_TEST_ASSERT_FALSE(view.flags & (BINN_FLG_VIEW | BINN_FLAGS_WRAPPED));
  IW_TEST_ASSERT_EQUAL(binn_u64_get(&view), 77);
  binn_destroy(&view);
  IW_TEST_ASSERT_TRUE(binn_view_materialize(src)); // not a view: no-op
  IW_TEST_ASSERT_EQUAL(binn_u64_get(src), 77);
  binn_destroy(src);
  IW_TEST_ASSERT_FALSE(binn_view_materialize(0));
}

static void test_is_view(void) {
  struct binn *src = binn_create_list(0);
  struct binn view;
  IW_TEST_ASSERT_FALSE(binn_is_view(src));
  IW_TEST_ASSERT_FALSE(binn_is_view(0));
  IW_TEST_ASSERT_TRUE(binn_create_view_from(&view, src));
  IW_TEST_ASSERT_TRUE(binn_is_view(&view));
  IW_TEST_ASSERT_TRUE(binn_view_materialize(&view));
  IW_TEST_ASSERT_FALSE(binn_is_view(&view));
  binn_destroy(&view);
  binn_destroy(src);
}

static void test_invalid_view_arguments(void) {
  struct binn *src = binn_create_list(0);
  struct binn view;
  uint8_t not_a_struct[sizeof(struct binn)];
  memset(not_a_struct, 0, sizeof(not_a_struct));
  IW_TEST_ASSERT_FALSE(binn_create_view_from(0, src));
  IW_TEST_ASSERT_FALSE(binn_create_view_from(&view, 0));
  IW_TEST_ASSERT_FALSE(binn_create_view_from(&view, &view));
  IW_TEST_ASSERT_FALSE(binn_create_view_from(&view, (struct binn*) not_a_struct));
  binn_destroy(src);
}

int main(int argc, char *argv[]) {
  static const struct iw_test_case cases[] = {
    IW_TEST_CASE(test_view_of_construction_container),
    IW_TEST_CASE(test_view_of_wrapped_container),
    IW_TEST_CASE(test_view_of_wrapped_payload),
    IW_TEST_CASE(test_view_of_scalar),
    IW_TEST_CASE(test_copy_of_view),
    IW_TEST_CASE(test_materialize_container_view),
    IW_TEST_CASE(test_materialize_wrapped_container),
    IW_TEST_CASE(test_materialize_wrapped_payload),
    IW_TEST_CASE(test_materialize_scalar_and_noop),
    IW_TEST_CASE(test_is_view),
    IW_TEST_CASE(test_invalid_view_arguments),
  };
  return iw_test_run(argc, argv, IW_TEST_SUITE("views", cases, 0, 0));
}
