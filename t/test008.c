// View mode: binn_create_view_from() and BINN_FLG_VIEW.
#include "../binnc.h"
#include "tests.h"

static struct binn* make_u8(uint8_t v) {
  struct binn_create_spec spec;
  memset(&spec, 0, sizeof(spec));
  spec.type = BINN_TYPE_UINT8;
  spec.value = bv(&v, sizeof(v));
  return binn_create_value(0, &spec);
}

int main(int argc, char *argv[]) {
  // View of a construction-mode container: borrows the source children buffer.
  {
    struct binn *src = binn_create_list(0);
    struct binn view;
    struct binn *v;
    struct binn_val ss, vs;
    int32_t idx;

    v = make_u8(1);
    BINN_ASSERT(binn_add(src, *binnv(v), &idx));
    binn_destroy(v);
    v = make_u8(2);
    BINN_ASSERT(binn_add(src, *binnv(v), 0));
    binn_destroy(v);

    BINN_ASSERT(binn_create_view_from(&view, src));
    BINN_ASSERT(view.magic == BINN_MAJIC_NUMBER);
    BINN_ASSERT(view.flags & BINN_FLG_VIEW);
    BINN_ASSERT(!(view.flags & BINN_FLAGS_WRAPPED));
    BINN_ASSERT(view.type == BINN_TYPE_LIST);
    BINN_ASSERT(view.count == 2);
    BINN_ASSERT(view.used == src->used);
    BINN_ASSERT(view.val.bval == src->val.bval);
    BINN_ASSERT(view.allocated == 0);
    BINN_ASSERT(binn_is_container(&view));
    BINN_ASSERT(binn_is_struct_instance(*binnv(&view)));

    // Serializes exactly like the source.
    ss = binn_serialize_allocated(src);
    vs = binn_serialize_allocated(&view);
    BINN_ASSERT(ss.buf != 0 && vs.buf != 0 && ss.size == vs.size);
    BINN_ASSERT_MEM(ss.buf, vs.buf, ss.size);
    free(ss.buf);
    free(vs.buf);

    // Iteration sees the source children.
    {
      struct binn_iter it;
      int n = 0;
      BINN_ASSERT(binn_iter_init(*binnv(&view), &it));
      while (binn_iter_next(&it)) {
        n++;
      }
      BINN_ASSERT(n == 2);
      binn_iter_close(&it);
    }

    // Modifying the view copies the data; the source is untouched.
    v = make_u8(3);
    BINN_ASSERT(binn_add(&view, *binnv(v), 0));
    binn_destroy(v);
    BINN_ASSERT(!(view.flags & (BINN_FLG_VIEW | BINN_FLAGS_WRAPPED)));
    BINN_ASSERT(view.count == 3);
    BINN_ASSERT(view.val.bval != src->val.bval);
    BINN_ASSERT(src->count == 2);

    // Destroying the view does not free the source data.
    BINN_ASSERT(binn_destroy(&view) == false);
    BINN_ASSERT(src->count == 2);
    ss = binn_serialize_allocated(src);
    BINN_ASSERT(ss.buf != 0);
    free(ss.buf);
    binn_destroy(src);
  }

  // View of a wrapped container: keeps WRAPPED_BINN and COW copies the raw children.
  {
    struct binn *src = binn_create_list(0);
    struct binn *v = make_u8(7);
    struct binn_val ser;
    struct binn holder;
    struct binn *w;
    struct binn view;
    bool casted = true;

    BINN_ASSERT(src != 0 && v != 0);
    BINN_ASSERT(binn_add(src, *binnv(v), 0));
    binn_destroy(v);
    ser = binn_serialize_allocated(src);
    BINN_ASSERT(ser.buf != 0);
    w = binn_from(&holder, ser, &casted);
    BINN_ASSERT(w != 0 && casted == false && (w->flags & BINN_FLG_WRAPPED_BINN));

    BINN_ASSERT(binn_create_view_from(&view, w));
    BINN_ASSERT((view.flags & BINN_FLG_VIEW) && (view.flags & BINN_FLG_WRAPPED_BINN));
    BINN_ASSERT(view.size == w->size);
    BINN_ASSERT(view.wrapped.buf == w->wrapped.buf);
    BINN_ASSERT(view.wrapped.size == w->wrapped.size);

    // Re-serializes byte-identically (raw wrapped buffer).
    {
      struct binn_val vs = binn_serialize_allocated(&view);
      BINN_ASSERT(vs.buf != 0 && vs.size == ser.size);
      BINN_ASSERT_MEM(vs.buf, ser.buf, vs.size);
      free(vs.buf);
    }

    // Modifying the view copies the wrapped children; the wrapped buffer is untouched.
    {
      uint8_t *saved = (uint8_t*) malloc(ser.size);
      memcpy(saved, ser.buf, ser.size);
      v = make_u8(9);
      BINN_ASSERT(binn_add(&view, *binnv(v), 0));
      binn_destroy(v);
      BINN_ASSERT(!(view.flags & (BINN_FLG_VIEW | BINN_FLAGS_WRAPPED)));
      BINN_ASSERT(view.count == 2);
      BINN_ASSERT(w->count == 1);
      BINN_ASSERT_MEM(ser.buf, saved, ser.size);
      free(saved);
    }
    BINN_ASSERT(binn_destroy(&view) == false);
    BINN_ASSERT(binn_destroy(w) == false);
    free(ser.buf);
    binn_destroy(src);
  }

  // View of a wrapped payload: keeps WRAPPED_PAYLOAD, text access and serialization work.
  {
    struct binn *src = binn_create_text(0, "hello", -1);
    struct binn_val ser = binn_serialize_allocated(src);
    struct binn holder;
    struct binn *w;
    struct binn view;
    struct binn_val tv;
    bool casted = true;

    BINN_ASSERT(ser.buf != 0);
    binn_destroy(src);
    w = binn_from(&holder, ser, &casted);
    BINN_ASSERT(w != 0 && casted == false && (w->flags & BINN_FLG_WRAPPED_PAYLOAD));

    BINN_ASSERT(binn_create_view_from(&view, w));
    BINN_ASSERT((view.flags & BINN_FLG_VIEW) && (view.flags & BINN_FLG_WRAPPED_PAYLOAD));
    tv = binn_text_get(&view);
    BINN_ASSERT(tv.size == 5 && memcmp(tv.buf, "hello", 5) == 0);
    BINN_ASSERT(((const char*) tv.buf)[5] == 0);

    {
      struct binn_val vs = binn_serialize_allocated(&view);
      BINN_ASSERT(vs.buf != 0 && vs.size == ser.size);
      BINN_ASSERT_MEM(vs.buf, ser.buf, vs.size);
      free(vs.buf);
    }
    BINN_ASSERT(binn_destroy(&view) == false);
    BINN_ASSERT(binn_destroy(w) == false);
    free(ser.buf);
  }

  // View of a numeric scalar.
  {
    struct binn *src = binn_create_u64c(0, 0x1226652FEull);
    struct binn view;
    BINN_ASSERT(binn_create_view_from(&view, src));
    BINN_ASSERT(view.flags & BINN_FLG_VIEW);
    BINN_ASSERT(binn_u64_get(&view) == 0x1226652FEull);
    BINN_ASSERT(binn_is_int_number(&view));
    BINN_ASSERT(binn_destroy(&view) == false);
    binn_destroy(src);
  }

  // A copy of a view owns its own data.
  {
    struct binn *src = binn_create_list(0);
    struct binn view;
    struct binn *cp;
    struct binn *v = make_u8(5);
    BINN_ASSERT(binn_add(src, *binnv(v), 0));
    binn_destroy(v);
    BINN_ASSERT(binn_create_view_from(&view, src));
    cp = binn_copy_from(0, *binnv(&view));
    BINN_ASSERT(cp != 0);
    BINN_ASSERT(!(cp->flags & (BINN_FLG_VIEW | BINN_FLAGS_WRAPPED)));
    BINN_ASSERT(cp->count == 1);
    BINN_ASSERT(binn_destroy(&view) == false);
    binn_destroy(src);
    {
      struct binn_val s = binn_serialize_allocated(cp);
      BINN_ASSERT(s.buf != 0);
      free(s.buf);
    }
    binn_destroy(cp);
  }

  // binn_view_materialize(): copy borrowed data and become an owner.
  {
    // Construction-mode container view.
    struct binn *src = binn_create_list(0);
    struct binn view;
    struct binn *v = make_u8(1);
    struct binn_val s1, s2;
    BINN_ASSERT(binn_add(src, *binnv(v), 0));
    binn_destroy(v);
    BINN_ASSERT(binn_create_view_from(&view, src));
    BINN_ASSERT(binn_view_materialize(&view));
    BINN_ASSERT(!(view.flags & (BINN_FLG_VIEW | BINN_FLAGS_WRAPPED)));
    BINN_ASSERT(view.val.bval != src->val.bval);
    BINN_ASSERT(view.count == src->count);
    BINN_ASSERT(binn_view_materialize(&view)); // idempotent
    s1 = binn_serialize_allocated(src);
    s2 = binn_serialize_allocated(&view);
    BINN_ASSERT(s1.buf != 0 && s2.buf != 0 && s1.size == s2.size);
    BINN_ASSERT_MEM(s1.buf, s2.buf, s1.size);
    free(s1.buf);
    free(s2.buf);
    binn_destroy(src); // source gone; view owns its own data
    s2 = binn_serialize_allocated(&view);
    BINN_ASSERT(s2.buf != 0);
    free(s2.buf);
    BINN_ASSERT(binn_destroy(&view) == false);
  }

  // Materializing a wrapped container view: the raw buffer can be freed afterwards.
  {
    struct binn *src = binn_create_list(0);
    struct binn *v = make_u8(3);
    struct binn_val ser;
    struct binn holder;
    struct binn *w;
    struct binn view;
    bool casted = true;
    BINN_ASSERT(binn_add(src, *binnv(v), 0));
    binn_destroy(v);
    ser = binn_serialize_allocated(src);
    BINN_ASSERT(ser.buf != 0);
    binn_destroy(src);
    w = binn_from(&holder, ser, &casted);
    BINN_ASSERT(w != 0 && casted == false && (w->flags & BINN_FLG_WRAPPED_BINN));
    BINN_ASSERT(binn_create_view_from(&view, w));
    BINN_ASSERT(binn_view_materialize(&view));
    BINN_ASSERT(!(view.flags & (BINN_FLG_VIEW | BINN_FLAGS_WRAPPED)));
    BINN_ASSERT(view.count == 1);
    binn_destroy(w);
    free(ser.buf); // view owns its own copy now
    {
      struct binn_val s = binn_serialize_allocated(&view);
      BINN_ASSERT(s.buf != 0);
      free(s.buf);
    }
    binn_destroy(&view);
  }

  // Materializing a wrapped payload text view: the payload can be freed afterwards.
  {
    struct binn *src = binn_create_text(0, "hello", -1);
    struct binn_val ser = binn_serialize_allocated(src);
    struct binn holder;
    struct binn *w;
    struct binn view;
    struct binn_val tv;
    bool casted = true;
    BINN_ASSERT(ser.buf != 0);
    binn_destroy(src);
    w = binn_from(&holder, ser, &casted);
    BINN_ASSERT(w != 0 && casted == false && (w->flags & BINN_FLG_WRAPPED_PAYLOAD));
    BINN_ASSERT(binn_create_view_from(&view, w));
    BINN_ASSERT(binn_view_materialize(&view));
    BINN_ASSERT(!(view.flags & (BINN_FLG_VIEW | BINN_FLAGS_WRAPPED)));
    binn_destroy(w);
    free(ser.buf);
    tv = binn_text_get(&view);
    BINN_ASSERT(tv.size == 5 && memcmp(tv.buf, "hello", 5) == 0);
    BINN_ASSERT(((const char*) tv.buf)[5] == 0);
    binn_destroy(&view);
  }

  // Numeric view, non-view no-op and NULL.
  {
    struct binn *src = binn_create_u64c(0, 77);
    struct binn view;
    BINN_ASSERT(binn_create_view_from(&view, src));
    BINN_ASSERT(binn_view_materialize(&view));
    BINN_ASSERT(!(view.flags & (BINN_FLG_VIEW | BINN_FLAGS_WRAPPED)));
    BINN_ASSERT(binn_u64_get(&view) == 77);
    binn_destroy(&view);
    BINN_ASSERT(binn_view_materialize(src)); // not a view: no-op
    BINN_ASSERT(binn_u64_get(src) == 77);
    binn_destroy(src);
    BINN_ASSERT(!binn_view_materialize(0));
  }

  // binn_is_view()
  {
    struct binn *src = binn_create_list(0);
    struct binn view;
    BINN_ASSERT(!binn_is_view(src));
    BINN_ASSERT(!binn_is_view(0));
    BINN_ASSERT(binn_create_view_from(&view, src));
    BINN_ASSERT(binn_is_view(&view));
    BINN_ASSERT(binn_view_materialize(&view));
    BINN_ASSERT(!binn_is_view(&view));
    binn_destroy(&view);
    binn_destroy(src);
  }

  // Invalid arguments.
  {
    struct binn *src = binn_create_list(0);
    struct binn view;
    uint8_t not_a_struct[sizeof(struct binn)];
    memset(not_a_struct, 0, sizeof(not_a_struct));
    BINN_ASSERT(!binn_create_view_from(0, src));
    BINN_ASSERT(!binn_create_view_from(&view, 0));
    BINN_ASSERT(!binn_create_view_from(&view, &view));
    BINN_ASSERT(!binn_create_view_from(&view, (struct binn*) not_a_struct));
    binn_destroy(src);
  }

  return binn_assertions_failed > 0 ? 1 : 0;
}
