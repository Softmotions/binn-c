#pragma once

#include <stdlib.h>
#include <stdatomic.h>
#include <stdio.h>

#define __SS(x) #x
#define __S(x)  __SS(x)

static atomic_int binn_assertions_failed;

#define BINN_ASSERT_FATAL(v__)                                                       \
        if (!(v__)) {                                                                \
          fputs(__FILE__ ":" __S(__LINE__) " Assertion failed: "#v__ "\n", stderr); \
          exit(1);                                                                   \
        }

#define BINN_ASSERT(v__)                                                             \
        if (!(v__)) {                                                                \
          ++binn_assertions_failed;                                                  \
          fputs(__FILE__ ":" __S(__LINE__) " Assertion failed: "#v__ "\n", stderr); \
        }
