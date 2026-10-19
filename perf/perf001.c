// Performance benchmark for the binnc library.
//
// Every benchmark phase is a test case of the `perf` suite, so a subset of the phases can be run
// with the usual `iwtests.h` case selection:
//
//   perf001                        - run every phase
//   perf001 serialize,keyed_get    - run the matching phases only
//   perf001 --list                 - show the available phases
//   perf001 --no-log               - keep the timing table on the console
//
// Environment:
//   BINNC_PERF_TARGET_MS  per-phase calibration target in milliseconds (default 300)
//
// Each phase is calibrated so that it runs for roughly `BINNC_PERF_TARGET_MS`, then measured three
// times and the fastest run reported. The report goes into the test output log named in the report
// header, use `--no-log` to see the timings directly.
#define IW_TEST_HARNESS "binnc"

#include "../binnc.h"
#include "../iwtests.h"

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

/// Calibrates and measures a single benchmark phase.
/// @param name Phase name shown in the report row.
/// @param fn Benchmarked function, it is called `n` times.
/// @param unit Number of logical operations performed by one call of `fn`.
static void run_phase(const char *name, void (*fn)(long), long unit) {
  long n = 1;
  double dt = 0;
  double best;
  int r;
  for ( ; ; ) {
    double t0 = now_sec();
    fn(n);
    dt = now_sec() - t0;
    if (dt >= g_target || n >= (1L << 26)) {
      break;
    }
    n = (n < 16) ? n * 4 : n * 2;
  }
  best = dt;
  for (r = 0; r < 2; r++) {
    double t0 = now_sec();
    fn(n);
    dt = now_sec() - t0;
    if (dt < best) {
      best = dt;
    }
  }
  printf("%-14s %10ld iters %9.3f ms %10.1f ns/op\n",
         name, n, best * 1e3, best * 1e9 / ((double) n * (double) unit));
  fflush(stdout);
  IW_TEST_ASSERT_TRUE(n > 0);
}

static void test_build_object(void) {
  run_phase("build_object", ph_build_object, 1);
}

static void test_build_list(void) {
  run_phase("build_list", ph_build_list, 1);
}

static void test_serialize(void) {
  run_phase("serialize", ph_serialize, 1);
}

static void test_wrap_iterate(void) {
  run_phase("wrap_iterate", ph_wrap_iterate, 1);
}

static void test_keyed_get(void) {
  run_phase("keyed_get", ph_keyed_get, OBJ_KEYS);
}

static void test_copy_from(void) {
  run_phase("copy_from", ph_copy_from, 1);
}

static void test_copy_struct(void) {
  run_phase("copy_struct", ph_copy_struct, 1);
}

static void test_visit(void) {
  run_phase("visit", ph_visit, 1);
}

static int suite_init(void) {
  const char *target = getenv("BINNC_PERF_TARGET_MS");
  size_t i;
  if (target && *target) {
    double ms = strtod(target, 0);
    if (ms > 0) {
      g_target = ms / 1000.0;
    }
  }
  for (i = 0; i < OBJ_KEYS; i++) {
    snprintf(g_keys[i], sizeof(g_keys[i]), "key-%d", (int) i);
  }
  g_obj = make_obj();
  IW_TEST_ASSERT_PTR_NOT_NULL(g_obj);
  if (!g_obj) {
    return -1;
  }
  g_ser = binn_serialize_allocated(g_obj);
  IW_TEST_ASSERT_PTR_NOT_NULL(g_ser.buf);
  if (!g_ser.buf) {
    binn_destroy(g_obj);
    g_obj = 0;
    return -1;
  }
  printf("binnc perf: object=%d keys, list=%d items, target=%.0f ms\n",
         OBJ_KEYS, LIST_ITEMS, g_target * 1e3);
  return 0;
}

static void suite_cleanup(void) {
  free(g_ser.buf);
  binn_destroy(g_obj);
  g_ser.buf = 0;
  g_obj = 0;
}

int main(int argc, char *argv[]) {
  static const struct iw_test_case cases[] = {
    IW_TEST_CASE_AS("build_object", test_build_object),
    IW_TEST_CASE_AS("build_list", test_build_list),
    IW_TEST_CASE_AS("serialize", test_serialize),
    IW_TEST_CASE_AS("wrap_iterate", test_wrap_iterate),
    IW_TEST_CASE_AS("keyed_get", test_keyed_get),
    IW_TEST_CASE_AS("copy_from", test_copy_from),
    IW_TEST_CASE_AS("copy_struct", test_copy_struct),
    IW_TEST_CASE_AS("visit", test_visit),
  };
  return iw_test_run(argc, argv, IW_TEST_SUITE("perf", cases, suite_init, suite_cleanup));
}
