#include "../binnc.h"
#include "tests.h"

int main(int argc, char* argv[]) {
  BINN_ASSERT(1);
  return binn_assertions_failed > 0 ? 1 : 0;
}
