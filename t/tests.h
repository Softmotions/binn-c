#pragma once

#include <stdlib.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "../binnc.h"

#define __SS(x) #x
#define __S(x)  __SS(x)

static atomic_int binn_assertions_failed;

#define BINN_ASSERT_FATAL(v__)                                                      \
        if (!(v__)) {                                                               \
          fputs(__FILE__ ":" __S(__LINE__) " Assertion failed: "#v__ "\n", stderr); \
          exit(1);                                                                  \
        }

#define BINN_ASSERT(v__)                                                            \
        if (!(v__)) {                                                               \
          ++binn_assertions_failed;                                                 \
          fputs(__FILE__ ":" __S(__LINE__) " Assertion failed: "#v__ "\n", stderr); \
        }

#define BINN_ASSERT_MEM(actual__, expected__, len__)                      \
        if (memcmp((actual__), (expected__), (len__)) != 0) {             \
          ++binn_assertions_failed;                                       \
          fputs(__FILE__ ":" __S(__LINE__) " Memory mismatch\n", stderr); \
        }

/// Serializes `b` and compares the result with the expected byte sequence.
static void binn_test_check_bytes(const struct binn *b, const uint8_t *expected, size_t len) {
  void *buf = binn_serialize_allocated(b);
  BINN_ASSERT(buf != 0);
  if (!buf) {
    return;
  }
  BINN_ASSERT(b->size == len);
  BINN_ASSERT_MEM(buf, expected, len);
  free(buf);
}

/// Serializes `b` and checks that the result is `len` bytes long.
static void binn_test_check_size(const struct binn *b, size_t len) {
  void *buf = binn_serialize_allocated(b);
  BINN_ASSERT(buf != 0);
  if (!buf) {
    return;
  }
  BINN_ASSERT(b->size == len);
  free(buf);
}
