// Performance benchmark for the binnc library.
//
// Usage:
//   perf001 [phase] [target_ms]
//
//   phase      optional, one of: build_object build_list serialize wrap_iterate
//              keyed_get copy_from (default: all phases)
//   target_ms  optional per-phase calibration target in milliseconds (default 300)
//
// Each phase is calibrated so that it runs for roughly `target_ms`, then measured three
// times and the fastest run reported.
#include "../binnc.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define OBJ_KEYS   64
#define LIST_ITEMS 1000

static char g_keys[OBJ_KEYS][16];
static struct binn *g_obj;
static struct binn_val g_ser;
static double g_target = 0.3;

static double now_sec(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double) ts.tv_sec + (double) ts.tv_nsec / 1e9;
}

static struct binn* mk(binn_type_t type, const void *p, size_t n) {
  struct binn_create_spec spec;
  memset(&spec, 0, sizeof(spec));
  spec.type = type;
  spec.value.buf = (void*) (uintptr_t) p;
  spec.value.size = n;
  return binn_create_value(0, &spec);
}

static struct binn* make_obj(void) {
  struct binn *o = binn_create_object(0);
  int i;
  for (i = 0; i < OBJ_KEYS; i++) {
    struct binn *v;
    switch (i % 4) {
      case 0: {
        uint32_t x = (uint32_t) i;
        v = mk(BINN_TYPE_UINT32, &x, sizeof(x));
        break;
      }
      case 1: {
        char s[32];
        int n = snprintf(s, sizeof(s), "value-%d", i);
        v = mk(BINN_TYPE_TEXT, s, (size_t) n);
        break;
      }
      case 2: {
        double d = i * 1.5;
        v = mk(BINN_TYPE_DOUBLE, &d, sizeof(d));
        break;
      }
      default: {
        int64_t x = -(int64_t) i;
        v = mk(BINN_TYPE_INT64, &x, sizeof(x));
        break;
      }
    }
    binn_add_keyed(o, g_keys[i], *binnv(v));
    binn_destroy(v);
  }
  return o;
}

static struct binn* make_list(void) {
  struct binn *l = binn_create_list(0);
  int i;
  for (i = 0; i < LIST_ITEMS; i++) {
    uint32_t x = (uint32_t) i;
    struct binn *v = mk(BINN_TYPE_UINT32, &x, sizeof(x));
    binn_add(l, *binnv(v), 0);
    binn_destroy(v);
  }
  return l;
}

static void ph_build_object(long n) {
  long i;
  for (i = 0; i < n; i++) {
    struct binn *o = make_obj();
    binn_destroy(o);
  }
}

static void ph_build_list(long n) {
  long i;
  for (i = 0; i < n; i++) {
    struct binn *l = make_list();
    binn_destroy(l);
  }
}

static void ph_serialize(long n) {
  long i;
  for (i = 0; i < n; i++) {
    struct binn_val b = binn_serialize_allocated(g_obj);
    free(b.buf);
  }
}

static void ph_wrap_iterate(long n) {
  long i;
  for (i = 0; i < n; i++) {
    struct binn holder;
    struct binn *w;
    struct binn_iter it;
    bool casted = false;
    w = binn_from(&holder, g_ser, &casted);
    if (w) {
      binn_iter_init(*binnv(w), &it);
      while (binn_iter_next(&it)) {
      }
      binn_iter_close(&it);
      binn_destroy(w);
    }
  }
}

static void ph_keyed_get(long n) {
  struct binn r;
  long i;
  for (i = 0; i < n; i++) {
    int k;
    for (k = 0; k < OBJ_KEYS; k++) {
      binn_keyed_get(g_obj, g_keys[k], &r);
    }
  }
}

static void ph_copy_from(long n) {
  long i;
  for (i = 0; i < n; i++) {
    struct binn *c = binn_copy_from(0, g_ser);
    binn_destroy(c);
  }
}

static void ph_copy_struct(long n) {
  long i;
  for (i = 0; i < n; i++) {
    struct binn *c = binn_copy_from(0, *binnv(g_obj));
    binn_destroy(c);
  }
}

static int perf_visitor(struct binn_iter *it, void *op) {
  (void) it;
  (*(long*) op)++;
  return 0;
}

static void ph_visit(long n) {
  long i;
  for (i = 0; i < n; i++) {
    long count = 0;
    binn_visit(g_obj, perf_visitor, &count);
  }
}

struct phase {
  const char *name;
  void (*fn)(long);
  long unit; // number of logical operations per iteration
};

static const struct phase g_phases[] = {
  { "build_object", ph_build_object, 1 },
  { "build_list", ph_build_list, 1 },
  { "serialize", ph_serialize, 1 },
  { "wrap_iterate", ph_wrap_iterate, 1 },
  { "keyed_get", ph_keyed_get, OBJ_KEYS },
  { "copy_from", ph_copy_from, 1 },
  { "copy_struct", ph_copy_struct, 1 },
  { "visit", ph_visit, 1 },
};

static void run_phase(const struct phase *ph) {
  long n = 1;
  double dt = 0;
  double best;
  int r;
  for ( ; ; ) {
    double t0 = now_sec();
    ph->fn(n);
    dt = now_sec() - t0;
    if (dt >= g_target || n >= (1L << 26)) {
      break;
    }
    n = (n < 16) ? n * 4 : n * 2;
  }
  best = dt;
  for (r = 0; r < 2; r++) {
    double t0 = now_sec();
    ph->fn(n);
    dt = now_sec() - t0;
    if (dt < best) {
      best = dt;
    }
  }
  printf("%-14s %10ld iters %9.3f ms %10.1f ns/op\n",
         ph->name, n, best * 1e3, best * 1e9 / ((double) n * (double) ph->unit));
  fflush(stdout);
}

int main(int argc, char *argv[]) {
  const char *only = 0;
  size_t i;
  int a;

  for (a = 1; a < argc; a++) {
    char *end = 0;
    double v = strtod(argv[a], &end);
    if (end && *end == 0 && v > 0) {
      g_target = v / 1000.0;
    } else {
      only = argv[a];
    }
  }

  for (i = 0; i < OBJ_KEYS; i++) {
    snprintf(g_keys[i], sizeof(g_keys[i]), "key-%d", (int) i);
  }
  g_obj = make_obj();
  g_ser = binn_serialize_allocated(g_obj);

  printf("binnc perf: object=%d keys, list=%d items, target=%.0f ms\n",
         OBJ_KEYS, LIST_ITEMS, g_target * 1e3);

  for (i = 0; i < sizeof(g_phases) / sizeof(g_phases[0]); i++) {
    if (!only || strcmp(only, g_phases[i].name) == 0) {
      run_phase(&g_phases[i]);
    }
  }

  free(g_ser.buf);
  binn_destroy(g_obj);
  return 0;
}
