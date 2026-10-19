# binn-c

Lightweight C implementation of the [BINN](https://github.com/liteserver/binn) binary serialization format.

Single-file library (C11, no dependencies): public API in `binnc.h`, implementation in `binnc.c`,
format reference in `docs/spec.md`.

## Build

The build system is [Autark](https://autark.dev):

```sh
./build.sh                                # static library, release build.
./build.sh -DBUILD_TYPE=Debug             # debug build
./build.sh -DBINNC_RUN_TESTS=1            # build and run the test suite
./build.sh -DBINNC_BUILD_SHARED_LIBS=1    # also build the shared library
./build.sh -I                             # install
```

`ENABLE_ASAN=1` / `ENABLE_UBSAN=1` enable sanitizers, `ENABLE_DEBINFO=1` keeps debug info in release builds.

## Tests

The test cases use the dependency free [`iwtests.h`](TESTS.md) harness: one executable per suite,
and each suite is a list of cases selectable from the command line.

```sh
./build.sh -DBINNC_RUN_TESTS=1              # build and run every suite

autark-cache/t/test003                      # run the whole suite
autark-cache/t/test003 iter                 # only the cases matching "iter"
autark-cache/t/test003 --list               # show the selectable cases
autark-cache/t/test003 --filter iter,visit -v
IW_TEST_FILTER=view,nesting ./build.sh -DBINNC_RUN_TESTS=1
```

Suites live in `t/`, the benchmark suite in `perf/`, the fuzzing suites in `fuzz/` (see
[`fuzz/README.md`](fuzz/README.md)). The `stdout`/`stderr` of the test code is redirected into
`<suite>.log` next to the binary; `--no-log` keeps it on the console, which is what the benchmark
wants:

```sh
autark-cache/perf/perf001 --no-log                   # every benchmark phase
autark-cache/perf/perf001 --no-log serialize,visit   # the selected phases only
BINNC_PERF_TARGET_MS=500 autark-cache/perf/perf001 --no-log
```

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