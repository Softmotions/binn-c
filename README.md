# binn-c

Lightweight C implementation of the [BINN](https://github.com/liteserver/binn) binary serialization format.

Single-file library (C11, no dependencies): public API in `binnc.h`, implementation in `binnc.c`,
format reference in `docs/spec.md`.

## Build

The build system is [Autark](https://autark.dev):

```sh
./build.sh                                # static library, release build.
./build.sh -DBUILD_TYPE=Debug             # debug build
./build.sh -DBINNC_BUILD_SHARED_LIBS=1    # also build the shared library
./build.sh -I                             # install
```

Tests, the benchmark and the fuzz harness are opt-in. Every `*_RUN_*` option enables the matching
`*_BUILD_*` option and executes the produced binaries as part of the build:

| Option              | Meaning                                                             |
| ------------------- | ------------------------------------------------------------------- |
| `BINNC_BUILD_TESTS` | Build the test suites.                                              |
| `BINNC_RUN_TESTS`   | Build and run the test suites.                                      |
| `BINNC_BUILD_PERF`  | Build the benchmark suite.                                          |
| `BINNC_RUN_PERF`    | Build and run the benchmark suite.                                  |
| `BINNC_BUILD_FUZZ`  | Build the fuzz harness (`fuzzdriver`, and `fuzzlib` when CC=clang). |
| `BINNC_RUN_FUZZ`    | Build and run the fuzz harness (`fuzzdriver` only).                 |

`ENABLE_ASAN=1` / `ENABLE_UBSAN=1` enable sanitizers, `ENABLE_DEBINFO=1` keeps debug info in release
builds, `ENABLE_FUZZ=1` instruments the library for libFuzzer coverage (implies
`BINNC_BUILD_FUZZ`).

```sh
./build.sh -DBINNC_RUN_TESTS=1                          # run the test suites
./build.sh -DBINNC_RUN_TESTS=1 -DENABLE_ASAN=1          # the same under ASAN
./build.sh -DBINNC_RUN_PERF=1                           # run the benchmark
./build.sh -DBINNC_RUN_FUZZ=1 -DENABLE_FUZZ=1           # fuzz driver, instrumented library
./build.sh -DBINNC_RUN_TESTS=1 -DBINNC_RUN_PERF=1 -DBINNC_RUN_FUZZ=1   # everything
```

`BINNC_RUN_FUZZ` executes only the portable `fuzzdriver` (100k rounds by default, see
`BINNC_FUZZ_ROUNDS`), the `libFuzzer` target `fuzzlib` is built but never started by the build since
it runs until it is stopped by hand.

## Usage

```c
#include "binnc.h"

// {"answer": 42}
uint32_t answer = 42;
struct binn_create_spec spec = { .type = BINN_TYPE_UINT32, .value = *binnv(&answer) };
struct binn *v = binn_create_value(0, &spec);

struct binn *obj = binn_create_object(0);
binn_add_keyed(obj, "answer", *binnv(v));
binn_destroy(v);

struct binn_val buf = binn_serialize_allocated(obj);
/* use buf.buf, buf.size */
free(buf.buf);
binn_destroy(obj);
```

## Layout

- `binnc.h` / `binnc.c` API and implementation
- `docs/spec.md` format specification
- `t/` unit tests, `perf/` benchmark, `fuzz/` fuzz harness (see `fuzz/README.md`)
- `iwtests.h` test harness, documented in `TESTS.md`

# License

```

MIT License

Copyright (c) 2012-2026 Softmotions Ltd <info@softmotions.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

```