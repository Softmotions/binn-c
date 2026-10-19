#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "../binnc.h"
#include "../iwtests.h"

/// Builds a bounded buffer reference for tests.
static struct binn_val bv(const void *buf, size_t size) {
  struct binn_val v;
  v.buf = (void*) buf;
  v.size = size;
  return v;
}

/// Serializes `b` and compares the result with the expected byte sequence.
static void binn_test_check_bytes(const struct binn *b, const uint8_t *expected, size_t len) {
  struct binn_val buf = binn_serialize_allocated(b);
  IW_TEST_ASSERT_PTR_NOT_NULL(buf.buf);
  if (!buf.buf) {
    return;
  }
  IW_TEST_ASSERT_EQUAL(b->size, len);
  IW_TEST_ASSERT_EQUAL(buf.size, len);
  IW_TEST_ASSERT_MEM_EQUAL(buf.buf, expected, len);
  free(buf.buf);
}

/// Serializes `b` and checks that the result is `len` bytes long.
static void binn_test_check_size(const struct binn *b, size_t len) {
  struct binn_val buf = binn_serialize_allocated(b);
  IW_TEST_ASSERT_PTR_NOT_NULL(buf.buf);
  if (!buf.buf) {
    return;
  }
  IW_TEST_ASSERT_EQUAL(b->size, len);
  IW_TEST_ASSERT_EQUAL(buf.size, len);
  free(buf.buf);
}
