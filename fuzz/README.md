# Fuzzy testing

Two fuzzing front-ends share the same checks in `fuzz001.c`:

* **`fuzzdriver`** - portable randomized driver. No extra dependencies; builds with any compiler.
* **`fuzzlib`** - coverage guided [libFuzzer](https://llvm.org/docs/LibFuzzer.html) target (clang only).

Fuzzing is disabled by default and enabled with `BINNC_BUILD_FUZZ=1`.

## What is checked

Every input is used both as a serialized buffer and as a program that builds a random value tree.

* `binn_is_struct_instance()` / `binn_is_serialized_buffer()` / `binn_is_any()`
* `binn_from()` (checked) and `binn_from_unchecked()` (lazy) wrapping, iteration, `binn_visit()`,
  `binn_keyed_get()` / `binn_indexed_get()`
* `binn_copy_from()` in both struct and serialized-buffer form
* `binn_serialize()` (streaming) vs `binn_serialize_allocated()`

The harness aborts on an oracle violation, so ASAN/UBSAN reports are meaningful:

* a container parsed from a buffer must re-serialize to exactly the same bytes;
* values produced by the library must round trip byte-for-byte through
  `serialize -> wrap -> serialize` and `copy -> serialize`;
* the streaming serializer must match `binn_serialize_allocated()`;
* wrapping/copy output must always be a valid binn value.

## Build

```sh
# Random driver + libFuzzer target, instrumented library.
./build.sh -c -DBINNC_BUILD_FUZZ=1 -DENABLE_FUZZ=1 \
           -DENABLE_ASAN=1 -DENABLE_UBSAN=1 -DBUILD_TYPE=Debug
```

`ENABLE_FUZZ` adds `-fsanitize=fuzzer-no-link` to the library so libFuzzer sees coverage inside it.
The targets are produced in `autark-cache/fuzz/`:

```sh
autark-cache/fuzz/fuzzdriver [rounds] [seed]
autark-cache/fuzz/fuzzlib -max_total_time=300 -dict=fuzz/binn.dict corpus_dir
```

## Running campaigns

```sh
# Portable driver, e.g. 20M rounds with a fixed seed (~60k rounds/s in a Debug+ASAN build).
autark-cache/fuzz/fuzzdriver 20000000 1

# Coverage guided campaign, seeds accumulate in the corpus directory.
mkdir -p /tmp/binn-corpus
autark-cache/fuzz/fuzzlib -max_total_time=300 -max_len=1024 -use_value_profile=1 \
    -dict=fuzz/binn.dict /tmp/binn-corpus
```

Crashes are written to `autark-cache/fuzz/crash-*`.
