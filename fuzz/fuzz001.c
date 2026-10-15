// Fuzzy testing harness for the binnc library.
//
// Two entry points share the same checks:
//   * libFuzzer target - compile without BINNC_FUZZ_STANDALONE and link with `-fsanitize=fuzzer`.
//     The library should be instrumented with `-fsanitize=fuzzer-no-link` (ENABLE_FUZZ=1).
//   * Standalone randomized driver - compile with `-DBINNC_FUZZ_STANDALONE`. It runs mutated
//     corpus entries and pure random inputs for a configurable number of rounds.
//
// The checks are:
//   * every read path is exercised (checked/unchecked wrapping, iteration, traversal, lookups, copy);
//   * a container parsed from a buffer must re-serialize to exactly the same bytes;
//   * values produced by the library must round trip: serialize -> parse -> serialize and
//     copy -> serialize must be byte identical;
//   * the streaming serializer must produce the same bytes as `binn_serialize_allocated()`.
#include "../binnc.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef BINNC_FUZZ_STANDALONE
#include <inttypes.h>
#include <stdio.h>
#endif

#define FUZZ_ST_STRING    5
#define FUZZ_ST_BLOB      6
#define FUZZ_ST_CONTAINER 7

#define FUZZ_MAX_INPUT 4096

static _Noreturn void fuzz_bug(const char *what) {
  (void) what;
  abort();
}

/// Storage class of a type identifier (mirrors the format definition, without touching internals).
static int fuzz_storage(binn_type_t type) {
  return (type > 0xFF) ? (int) ((type >> 13) & 0x7) : (int) ((type >> 5) & 0x7);
}

static struct binn_val fuzz_struct_val(const struct binn *b) {
  struct binn_val v;
  v.buf = (void*) (uintptr_t) b;
  v.size = sizeof(*b);
  return v;
}

static struct binn* fuzz_mk(binn_type_t type, const void *p, size_t n) {
  struct binn_create_spec spec;
  memset(&spec, 0, sizeof(spec));
  spec.type = type;
  spec.value.buf = (void*) (uintptr_t) p;
  spec.value.size = n;
  return binn_create_value(0, &spec);
}

/// Reads every byte of a leaf payload so that out-of-bounds wrappers are caught by ASAN.
static void fuzz_touch(const uint8_t *p, size_t n) {
  volatile uint32_t h = 0;
  size_t i;
  if (!p) {
    return;
  }
  for (i = 0; i < n; i++) {
    h = h * 131u + p[i];
  }
  (void) h;
}

static int fuzz_visit_cb(struct binn_iter *it, void *op) {
  (void) it;
  (*(unsigned long*) op)++;
  return 0;
}

/// Recursively reads a wrapped value through the public API.
static void fuzz_exercise(const struct binn *b, int depth) {
  int storage;
  if (!b) {
    return;
  }
  storage = fuzz_storage(b->type);
  if (storage == FUZZ_ST_STRING || storage == FUZZ_ST_BLOB) {
    fuzz_touch(b->val.bval, b->used);
    return;
  }
  if (storage != FUZZ_ST_CONTAINER) {
    volatile uint64_t nv = b->val.nval;
    (void) nv;
    return;
  }
  if (depth >= BINN_MAX_NESTING_LEVELS) {
    return;
  }
  {
    struct binn_iter it;
    struct binn r;
    unsigned long visited = 0;
    int n = 0;
    binn_visit(b, fuzz_visit_cb, &visited);
    if (!binn_iter_init(fuzz_struct_val(b), &it)) {
      return;
    }
    while (binn_iter_next(&it)) {
      fuzz_exercise(&it.binn, depth + 1);
      if (++n > 100000) {
        break;
      }
    }
    binn_iter_close(&it);
    binn_keyed_get(b, "", &r);
    binn_keyed_get(b, "k", &r);
    binn_indexed_get(b, 0, &r);
    binn_indexed_get(b, 1, &r);
    binn_indexed_get(b, UINT32_MAX, &r);
  }
}

struct fuzz_stream {
  uint8_t buf[FUZZ_MAX_INPUT];
  size_t  len;
  int     terminated;
};

static void fuzz_stream_cb(struct binn_val val, void *op) {
  struct fuzz_stream *s = (struct fuzz_stream*) op;
  if (!val.buf && !val.size) {
    s->terminated++;
    return;
  }
  if (s->len + val.size <= sizeof(s->buf)) {
    memcpy(s->buf + s->len, val.buf, val.size);
    s->len += val.size;
  }
}

/// Checks that streaming and allocated serialization agree.
static void fuzz_check_serialize(const struct binn *b) {
  struct fuzz_stream st;
  struct binn_val sa;
  memset(&st, 0, sizeof(st));
  if (!binn_serialize(b, fuzz_stream_cb, &st)) {
    return;
  }
  if (st.terminated != 1) {
    fuzz_bug("stream not terminated");
  }
  sa = binn_serialize_allocated(b);
  if (!sa.buf) {
    fuzz_bug("allocated serialization failed");
  }
  if (st.len != sa.size || (sa.size && memcmp(st.buf, sa.buf, sa.size) != 0)) {
    fuzz_bug("stream mismatch");
  }
  free(sa.buf);
}

static void fuzz_run(const uint8_t *data, size_t size) {
  struct binn_val v;
  struct binn holder;
  struct binn *b;
  bool casted;

  if (!data || size == 0) {
    return;
  }
  v.buf = (void*) (uintptr_t) data;
  v.size = size;

  (void) binn_is_any(v);
  (void) binn_is_struct_instance(v);
  (void) binn_is_serialized_buffer(v);

  // Checked wrapping.
  casted = false;
  b = binn_from(&holder, v, &casted);
  if (binn_is_serialized_buffer(v) && !b) {
    fuzz_bug("valid buffer rejected by binn_from");
  }
  if (b && !casted) {
    fuzz_exercise(b, 0);
    fuzz_check_serialize(b);
    if (b->flags & BINN_FLG_WRAPPED_BINN) {
      struct binn_val s = binn_serialize_allocated(b);
      if (!s.buf) {
        fuzz_bug("wrapped container serialization failed");
      }
      if (s.size != b->size || s.size > size || memcmp(s.buf, data, s.size) != 0) {
        fuzz_bug("wrapped container bytes changed");
      }
      free(s.buf);
    }
    binn_destroy(b);
  }
  // `casted == true` means `b` aliases the caller memory (here the fuzzer input); never destroy it.

  // Unchecked wrapping: parsing is lazy, must never read out of bounds.
  casted = false;
  b = binn_from_unchecked(&holder, v, &casted);
  if (b && !casted) {
    fuzz_exercise(b, 0);
    binn_destroy(b);
  }

  // Copying a valid serialized value must produce a value that round trips through the format.
  if (!binn_is_struct_instance(v)) {
    struct binn *cp = binn_copy_from(0, v);
    if (cp) {
      struct binn_val cs = binn_serialize_allocated(cp);
      if (!cs.buf) {
        fuzz_bug("copy serialization failed");
      } else {
        struct binn holder2;
        struct binn *w2;
        bool casted2 = false;
        w2 = binn_from(&holder2, cs, &casted2);
        if (!w2) {
          fuzz_bug("copy output is not a valid binn");
        }
        if (!casted2 && (w2->type != cp->type || w2->count != cp->count)) {
          fuzz_bug("copy round trip mismatch");
        }
        if (!casted2) {
          binn_destroy(w2);
        }
        free(cs.buf);
      }
      binn_destroy(cp);
    }
  }
}

static uint8_t fuzz_byte(const uint8_t *data, size_t size, size_t *pos) {
  if (*pos >= size) {
    return 0;
  }
  return data[(*pos)++];
}

/// Builds a random value tree from `data`. Returns zero when the input is exhausted.
static struct binn* fuzz_build(int depth, const uint8_t *data, size_t size, size_t *pos) {
  uint8_t sel;
  if (depth > 6 || *pos >= size) {
    return 0;
  }
  sel = fuzz_byte(data, size, pos);
  switch (sel % 10) {
    case 0: {
      uint8_t x = fuzz_byte(data, size, pos);
      return fuzz_mk(BINN_TYPE_UINT8, &x, sizeof(x));
    }
    case 1: {
      int16_t x = (int16_t) fuzz_byte(data, size, pos);
      return fuzz_mk(BINN_TYPE_INT16, &x, sizeof(x));
    }
    case 2: {
      uint64_t x = ((uint64_t) fuzz_byte(data, size, pos) << 8) | fuzz_byte(data, size, pos);
      return binn_create_u64c(0, x);
    }
    case 3: {
      int64_t x = -(int64_t) fuzz_byte(data, size, pos);
      return binn_create_i64c(0, x);
    }
    case 4: {
      double x = (double) fuzz_byte(data, size, pos) / 3.0;
      if (sel & 0x10) {
        return binn_create_f32c(0, (float) x);
      }
      return binn_create_f64c(0, x);
    }
    case 5: {
      size_t n = fuzz_byte(data, size, pos) % 24;
      const uint8_t *p = data + (*pos < size ? *pos : size);
      size_t avail = size - (*pos < size ? *pos : size);
      if (n > avail) {
        n = avail;
      }
      *pos += n;
      return binn_create_text(0, (const char*) p, n ? (ssize_t) n : 0);
    }
    case 6: {
      size_t n = fuzz_byte(data, size, pos) % 24;
      const uint8_t *p = data + (*pos < size ? *pos : size);
      size_t avail = size - (*pos < size ? *pos : size);
      if (n > avail) {
        n = avail;
      }
      *pos += n;
      return n ? fuzz_mk(BINN_TYPE_BLOB, p, n) : fuzz_mk(BINN_TYPE_BLOB, 0, 0);
    }
    case 7:
      return fuzz_mk(BINN_TYPE_TRUE, 0, 0);
    case 8: {
      // list
      struct binn *l = binn_create_list(0);
      int n = fuzz_byte(data, size, pos) % 6;
      int i;
      for (i = 0; i < n; i++) {
        struct binn *child = fuzz_build(depth + 1, data, size, pos);
        if (!child || !binn_add(l, *binnv(child), 0)) {
          binn_destroy(child);
          break;
        }
        binn_destroy(child);
      }
      return l;
    }
    default: {
      // object or map
      struct binn *c = (sel & 1) ? binn_create_map(0) : binn_create_object(0);
      int is_map = (sel & 1) != 0;
      int n = fuzz_byte(data, size, pos) % 6;
      int i;
      for (i = 0; i < n; i++) {
        struct binn *child = fuzz_build(depth + 1, data, size, pos);
        int ok = 0;
        if (!child) {
          break;
        }
        if (is_map) {
          ok = binn_add_mapped(c, (int32_t) fuzz_byte(data, size, pos), *binnv(child));
        } else {
          char key[16];
          int kl = 1 + (fuzz_byte(data, size, pos) % 8);
          int k;
          for (k = 0; k < kl; k++) {
            key[k] = (char) ('a' + (fuzz_byte(data, size, pos) % 26));
          }
          key[kl] = 0;
          ok = binn_add_keyed(c, key, *binnv(child));
        }
        binn_destroy(child);
        if (!ok) {
          break;
        }
      }
      return c;
    }
  }
}

/// Serialize -> parse -> serialize and copy round trip oracle for library built values.
static void fuzz_build_roundtrip(const uint8_t *data, size_t size) {
  size_t pos = 0;
  struct binn *root = fuzz_build(0, data, size, &pos);
  if (!root) {
    return;
  }
  {
    struct binn_val s1 = binn_serialize_allocated(root);
    if (s1.buf) {
      struct binn holder;
      struct binn *w;
      struct binn *cp;
      bool casted = false;
      w = binn_from(&holder, s1, &casted);
      if (!w) {
        fuzz_bug("library output rejected by binn_from");
      }
      if (casted) {
        fuzz_bug("library output mistaken for a struct");
      }
      fuzz_check_serialize(w);
      {
        struct binn_val s2 = binn_serialize_allocated(w);
        if (!s2.buf || s2.size != s1.size || memcmp(s1.buf, s2.buf, s1.size) != 0) {
          fuzz_bug("wrap round trip mismatch");
        }
        free(s2.buf);
      }
      binn_destroy(w);
      cp = binn_copy_from(0, *binnv(root));
      if (!cp) {
        fuzz_bug("struct copy failed");
      }
      {
        struct binn_val s3 = binn_serialize_allocated(cp);
        if (!s3.buf || s3.size != s1.size || memcmp(s1.buf, s3.buf, s1.size) != 0) {
          fuzz_bug("struct copy round trip mismatch");
        }
        free(s3.buf);
      }
      binn_destroy(cp);
      cp = binn_copy_from(0, s1);
      if (!cp) {
        fuzz_bug("serialized copy failed");
      }
      {
        struct binn_val s4 = binn_serialize_allocated(cp);
        if (!s4.buf || s4.size != s1.size || memcmp(s1.buf, s4.buf, s1.size) != 0) {
          fuzz_bug("serialized copy round trip mismatch");
        }
        free(s4.buf);
      }
      binn_destroy(cp);
      free(s1.buf);
    }
  }
  binn_destroy(root);
}

#ifndef BINNC_FUZZ_STANDALONE

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size > FUZZ_MAX_INPUT) {
    return 0;
  }
  fuzz_run(data, size);
  fuzz_build_roundtrip(data, size);
  return 0;
}

#else // BINNC_FUZZ_STANDALONE

static uint64_t g_rng;

static uint32_t fuzz_rand(void) {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 7;
  g_rng ^= g_rng << 17;
  return (uint32_t) (g_rng >> 32);
}

#define FUZZ_MAX_CORPUS 16

static uint8_t *g_corpus[FUZZ_MAX_CORPUS];
static size_t g_corpus_len[FUZZ_MAX_CORPUS];
static int g_corpus_n;

static void corpus_add(struct binn *b) {
  struct binn_val s;
  if (!b) {
    return;
  }
  s = binn_serialize_allocated(b);
  if (s.buf && g_corpus_n < FUZZ_MAX_CORPUS) {
    g_corpus[g_corpus_n] = s.buf;
    g_corpus_len[g_corpus_n] = s.size;
    g_corpus_n++;
  } else {
    free(s.buf);
  }
  binn_destroy(b);
}

static void build_corpus(void) {
  uint8_t small = 0x7F;
  int64_t neg = -1;
  double dbl = 3.5;
  binn_type_t html = (binn_type_t) ((5u << 13) | 0x1000u | 21u);
  corpus_add(binn_create_list(0));
  corpus_add(binn_create_map(0));
  corpus_add(binn_create_object(0));
  corpus_add(fuzz_mk(BINN_TYPE_NULL, 0, 0));
  corpus_add(fuzz_mk(BINN_TYPE_UINT8, &small, sizeof(small)));
  corpus_add(fuzz_mk(BINN_TYPE_IN64, &neg, sizeof(neg)));
  corpus_add(fuzz_mk(BINN_TYPE_DOUBLE, &dbl, sizeof(dbl)));
  corpus_add(fuzz_mk(BINN_TYPE_TEXT, "hello world", 11));
  corpus_add(fuzz_mk(BINN_TYPE_BLOB, "\x00\x01\x02\x03", 4));
  corpus_add(fuzz_mk(html, "html", 4));
  {
    uint8_t big[300];
    memset(big, 'x', sizeof(big));
    corpus_add(fuzz_mk(BINN_TYPE_TEXT, big, sizeof(big)));
  }
  {
    struct binn *l = binn_create_list(0);
    int i;
    for (i = 0; i < 200; i++) {
      uint8_t x = (uint8_t) i;
      struct binn *v = fuzz_mk(BINN_TYPE_UINT8, &x, sizeof(x));
      binn_add(l, *binnv(v), 0);
      binn_destroy(v);
    }
    corpus_add(l);
  }
  {
    struct binn *o = binn_create_object(0);
    struct binn *v;
    struct binn *inner = binn_create_list(0);
    uint8_t x = 1;
    v = fuzz_mk(BINN_TYPE_UINT8, &x, sizeof(x));
    binn_add(inner, *binnv(v), 0);
    binn_destroy(v);
    binn_add_keyed(o, "list", *binnv(inner));
    binn_destroy(inner);
    v = fuzz_mk(BINN_TYPE_TEXT, "value", 5);
    binn_add_keyed(o, "name", *binnv(v));
    binn_destroy(v);
    corpus_add(o);
  }
  {
    struct binn *m = binn_create_map(0);
    uint8_t x = 9;
    struct binn *v = fuzz_mk(BINN_TYPE_UINT8, &x, sizeof(x));
    binn_add_mapped(m, -5, *binnv(v));
    binn_add_mapped(m, INT32_MAX, *binnv(v));
    binn_destroy(v);
    corpus_add(m);
  }
}

int main(int argc, char *argv[]) {
  unsigned long rounds = 100000;
  uint64_t seed = 0x9E3779B97F4A7C15ull;
  uint8_t buf[FUZZ_MAX_INPUT];
  unsigned long r;

  if (argc > 1) {
    rounds = strtoul(argv[1], 0, 0);
  }
  if (argc > 2) {
    seed = strtoull(argv[2], 0, 0);
  }
  g_rng = seed ? seed : 1;
  build_corpus();

  // Feed every corpus entry unmutated first.
  for (r = 0; r < (unsigned long) g_corpus_n; r++) {
    fuzz_run(g_corpus[r], g_corpus_len[r]);
    fuzz_build_roundtrip(g_corpus[r], g_corpus_len[r]);
  }

  for (r = 0; r < rounds; r++) {
    size_t len;
    int mode = (int) (fuzz_rand() % 4);
    if (mode == 0 || g_corpus_n == 0) {
      size_t i;
      len = fuzz_rand() % sizeof(buf);
      for (i = 0; i < len; i++) {
        buf[i] = (uint8_t) fuzz_rand();
      }
    } else {
      int ci = (int) (fuzz_rand() % (uint32_t) g_corpus_n);
      int muts = 1 + (int) (fuzz_rand() % 8);
      int m;
      len = g_corpus_len[ci];
      if (len > sizeof(buf)) {
        len = sizeof(buf);
      }
      memcpy(buf, g_corpus[ci], len);
      for (m = 0; m < muts; m++) {
        int op = (int) (fuzz_rand() % 4);
        if (len == 0) {
          buf[0] = (uint8_t) fuzz_rand();
          len = 1;
          continue;
        }
        if (op == 0) {
          buf[fuzz_rand() % len] ^= (uint8_t) (1u << (fuzz_rand() % 8));
        } else if (op == 1) {
          buf[fuzz_rand() % len] = (uint8_t) fuzz_rand();
        } else if (op == 2) {
          len = fuzz_rand() % len;
        } else if (len < sizeof(buf)) {
          buf[len++] = (uint8_t) fuzz_rand();
        }
      }
    }
    fuzz_run(buf, len);
    if (fuzz_rand() & 1) {
      fuzz_build_roundtrip(buf, len);
    }
  }

  fprintf(stderr, "fuzz001: %lu rounds ok (seed=%" PRIu64 ", corpus=%d)\n", rounds, seed, g_corpus_n);
  for (r = 0; r < (unsigned long) g_corpus_n; r++) {
    free(g_corpus[r]);
  }
  return 0;
}

#endif // BINNC_FUZZ_STANDALONE
