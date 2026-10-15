// Standalone fuzz driver wrapper.
//
// Compiles the shared harness in randomized-driver mode so that it can be built next to the
// libFuzzer object without clashing object names.
#define BINNC_FUZZ_STANDALONE 1
#include "fuzz001.c"
