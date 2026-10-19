# Fuzzy testing

Two fuzzing front-ends share the same checks in `fuzz001.c`:

* **`fuzzdriver`** - portable randomized driver. No extra dependencies; builds with any compiler.
  It is an [`iwtests.h`](../TESTS.md) suite, so single campaigns can be selected by case name.
* **`fuzzlib`** - coverage guided [libFuzzer](https://llvm.org/docs/LibFuzzer.html) target (clang only).

Fuzzing is disabled by default and enabled with `BINNC_BUILD_FUZZ=1` (`BINNC_RUN_FUZZ=1` builds it
and runs the portable driver as part of the build).

## What is checked

Every input is used both as a serialized buffer and as a program that builds a random value tree.

* `binn_is_struct_instance()` / `binn_is_serialized_buffer()` / `binn_is_any()`
* `binn_from()` (checked) and `binn_from_unchecked()` (lazy) wrapping, iteration, `binn_visit()`,
  `binn_keyed_get()` / `binn_indexed_get()`
* `binn_copy_from()` in both struct and serialized-buffer form
* `binn_serialize()` (streaming) vs `binn_serialize_allocated()`

The harness reports an oracle violation as a failed test case (`iwtests.h` assertion) and aborts
the case, so ASAN/UBSAN reports are meaningful:

* a container parsed from a buffer must re-serialize to exactly the same bytes;
* values produced by the library must round trip byte-for-byte through
  `serialize -> wrap -> serialize` and `copy -> serialize`;
* the streaming serializer must match `binn_serialize_allocated()`;
* wrapping/copy output must always be a valid binn value.

The libFuzzer target has no test case to abort, there an oracle violation aborts the process, which
is exactly what libFuzzer expects from a crash.

## Build

```sh
# Random driver + libFuzzer target, instrumented library.
./build.sh -c -DBINNC_BUILD_FUZZ=1 -DENABLE_FUZZ=1 \
           -DENABLE_ASAN=1 -DENABLE_UBSAN=1 -DBUILD_TYPE=Debug
```

`ENABLE_FUZZ` adds `-fsanitize=fuzzer-no-link` to the library so libFuzzer sees coverage inside it.
Use `BINNC_RUN_FUZZ=1` instead of `BINNC_BUILD_FUZZ=1` to also execute the portable driver as part of
the build (the libFuzzer target is built but never started by the build).
The targets are produced in `autark-cache/fuzz/`:

```sh
autark-cache/fuzz/fuzzdriver [iwtests.h options] [case ...]
autark-cache/fuzz/fuzzlib -max_total_time=300 -dict=fuzz/binn.dict corpus_dir
```

## Running campaigns

The portable driver runs two cases: `corpus` (every seed corpus entry, unmutated) and
`random_rounds` (mutation/random loop). The campaign parameters come from the environment:

| Variable            | Meaning                                    |
| ------------------- | ------------------------------------------ |
| `BINNC_FUZZ_ROUNDS` | Number of random rounds, default `100000`. |
| `BINNC_FUZZ_SEED`   | PRNG seed, default `0x9E3779B97F4A7C15`.   |

```sh
# Portable driver, e.g. 20M rounds with a fixed seed (~60k rounds/s in a Debug+ASAN build).
BINNC_FUZZ_ROUNDS=20000000 BINNC_FUZZ_SEED=1 autark-cache/fuzz/fuzzdriver

# Only the corpus replay, without the random rounds.
autark-cache/fuzz/fuzzdriver corpus

# The driver writes its output into fuzzdriver.log, --no-log keeps it on the console.
BINNC_FUZZ_ROUNDS=100000 autark-cache/fuzz/fuzzdriver --no-log

# Coverage guided campaign, seeds accumulate in the corpus directory.
mkdir -p /tmp/binn-corpus
autark-cache/fuzz/fuzzlib -max_total_time=300 -max_len=1024 -use_value_profile=1 \
    -dict=fuzz/binn.dict /tmp/binn-corpus
```

Crashes are written to `autark-cache/fuzz/crash-*`.
