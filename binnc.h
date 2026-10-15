/// @file binnc.h
/// @brief Lightweight C implementation of the BINN data serialization format.
///
/// The BINN format specification is available in `docs/spec.md`.

#pragma once
#ifndef BINNC_H
#define BINNC_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <stdbool.h>

/// Binn value type identifier. See the `BINN_TYPE_*` constants.
typedef uint16_t binn_type_t;

/// Control flags affecting the behavior of binn struct operations.
/// See the `BINN_FLG_*` constants.
typedef uint16_t binn_flags_t;

/// Magic number used to distinguish a binn struct from a serialized binn buffer.
/// Expected to be stored in `binn::magic`.
#define BINN_MAJIC_NUMBER 0x1F22B11F

/// Max length of binn object key.
#define BINN_KEY_MAXLEN 255

/// Max nesting levels of container binn objects.
#define BINN_MAX_NESTING_LEVELS 255

/// Minimum size in bytes of a serialized binn container (`[type][size][count]`).
#define MIN_BINN_SIZE 3

/// Binn value type identifiers.
///
/// The three most significant bits encode the storage class and the remaining bits the sub-type,
/// as defined by the BINN format specification.
/// @{
#define BINN_TYPE_NULL     0x00 ///< Null value, no payload.
#define BINN_TYPE_TRUE     0x01 ///< Boolean `true`, no payload.
#define BINN_TYPE_FALSE    0x02 ///< Boolean `false`, no payload.
#define BINN_TYPE_UINT8    0x20 ///< Unsigned 8-bit integer.
#define BINN_TYPE_INT8     0x21 ///< Signed 8-bit integer.
#define BINN_TYPE_UINT16   0x40 ///< Unsigned 16-bit integer.
#define BINN_TYPE_INT16    0x41 ///< Signed 16-bit integer.
#define BINN_TYPE_UIN32    0x60 ///< Unsigned 32-bit integer.
#define BINN_TYPE_INT32    0x61 ///< Signed 32-bit integer.
#define BINN_TYPE_FLOAT    0x62 ///< Single precision (32-bit) floating point number.
#define BINN_TYPE_UINT64   0x80 ///< Unsigned 64-bit integer.
#define BINN_TYPE_IN64     0x81 ///< Signed 64-bit integer.
#define BINN_TYPE_DOUBLE   0x82 ///< Double precision (64-bit) floating point number.
#define BINN_TYPE_TEXT     0xA0 ///< UTF-8, null-terminated text string.
#define BINN_TYPE_DATETIME 0xA1 ///< Date and time string.
#define BINN_TYPE_DATE     0xA2 ///< Date string.
#define BINN_TYPE_TIME     0xA3 ///< Time string.
#define BINN_TYPE_DECIMAL  0xA4 ///< Arbitrary precision decimal number string.
#define BINN_TYPE_BLOB     0xC0 ///< Binary data (blob).
#define BINN_TYPE_LIST     0xE0 ///< Container holding an ordered list of values.
#define BINN_TYPE_MAP      0xE1 ///< Container holding a set of key/value pairs.
#define BINN_TYPE_OBJECT   0xE2 ///< Container holding an object with named fields.
/// @}

/// The binn object is in `wrapped mode` and uses a serialized binn buffer referenced by
/// `binn::wrapped` (`struct binn_val`). On any modification this flag is unset, the data is copied to
/// the heap and the binn moves into `construction mode`.
#define BINN_FLG_WRAPPED_BINN 0x04

/// The binn object is in `wrapped mode` but wraps only binn payload data (for example a string or blob
/// data buffer). Used by `binn_create_value()`. The caller owns the provided data.
/// Useful only for non-numeric data, such as strings and blobs.
/// On any modification this flag is unset, the data is copied to the heap and the binn moves into
/// `construction mode`.
#define BINN_FLG_WRAPPED_PAYLOAD 0x01

/// The binn struct is allocated on the heap.
#define BINN_FLG_STRUCT_ON_HEAP 0x02

/// Bitmask of all wrapped modes.
#define BINN_FLAGS_WRAPPED (BINN_FLG_WRAPPED_BINN | BINN_FLG_WRAPPED_PAYLOAD)

/// Builds a `struct binn_val` for a typed pointer. `binnv(ptr)` yields a `struct binn_val*`, so it is
/// normally dereferenced when passed by value: `binn_add(list, *binnv(value), 0)`.
#define binnv(binnptr__) & (struct binn_val) { .buf = (binnptr__), .size = sizeof(*(binnptr__)) }

/// Bounded reference to a buffer holding either a serialized binn value or a `struct binn`.
///
/// Carrying the buffer size alongside the pointer lets the library validate every access and reject
/// malformed or truncated input instead of reading past the end of the buffer.
struct binn_val {
  void  *buf;  ///< Buffer pointer. Zero means `no value`.
  size_t size; ///< Size of the buffer in bytes.
};

/// @brief Binn value.
///
/// A binn value struct operates in two modes:
///
/// 1. Construction mode. In this mode the binn is iteratively constructed from scratch by the
///    `binn_add_xxx()` functions. It does not hold a reference to a wrapped serialized binn buffer or to
///    wrapped payload data; instead it holds the data in `binn::val`.
///    The binn is in construction mode if neither `BINN_FLG_WRAPPED_BINN` nor `BINN_FLG_WRAPPED_PAYLOAD` is set.
///
/// 2. Wrapper mode (when `BINN_FLG_WRAPPED_BINN` or `BINN_FLG_WRAPPED_PAYLOAD` is set).
///    In this mode `binn::wrapped` holds a `struct binn_val` referencing a serialized binn buffer
///    (`BINN_FLG_WRAPPED_BINN`) or the unmodifiable payload data (`BINN_FLG_WRAPPED_PAYLOAD`).
///    The state of the binn structure fields is based on the given buffer. On any modification the binn is
///    moved to construction mode (copy-on-write), leaving the wrapped buffer untouched, and the data is
///    copied to the heap.
///
/// @see BINN_FLG_WRAPPED_BINN, BINN_FLG_WRAPPED_PAYLOAD
struct binn {
  uint32_t magic;     ///< Magic number (`BINN_MAJIC_NUMBER`) used to distinguish a binn structure from a
                      ///  serialized binn buffer.
  uint16_t    flags;  ///< Bitmask of `BINN_FLG_*` flags.
  binn_type_t type;   ///< Binn object type, one of the `BINN_TYPE_*` constants.
  uint32_t    count;  ///< Number of elements in the binn container.
                      ///  This value is iteratively updated in construction mode.
  size_t size;        ///< Size of the serialized binn object in bytes.
                      ///  This value is iteratively updated in construction mode.
                      ///  Note: the binn object may not actually be in serialized (finalized) form.
  uint32_t used;      ///< Number of bytes actually used in the buffer owned by `binn::val::bval`.
  uint32_t allocated; ///< Number of bytes allocated for `binn::val::bval`.

  /// Binn data value reference. Holds either a wrapped buffer or the constructed value.
  union {
    /// References the serialized binn buffer (`BINN_FLG_WRAPPED_BINN`) or the payload data
    /// (`BINN_FLG_WRAPPED_PAYLOAD`) in wrapper mode.
    struct binn_val wrapped;
    union {
      uint64_t nval; ///< Data value for numeric types and for any types up to eight bytes in size.
      uint8_t *bval; ///< Pointer to a non-numeric binn data buffer.
    } val;           ///< Binn data value in construction mode.
  };

  void *user_data;                   ///< Optional user data associated with the binn object.
  void  (*on_destroy)(struct binn*); ///< Optional callback invoked when the binn object is destroyed.
};

/// Binn value creation specification.
/// Used as the specification for `binn_create_value()`.
struct binn_create_spec {
  binn_type_t type;  ///< Type of the binn value to create. Container types are also supported.
  /// Control flags affecting binn creation. Currently in use:
  /// - BINN_FLG_WRAPPED_PAYLOAD The value will not be copied into the binn object.
  ///   The caller controls the provided value. A convenient way to release it
  ///   is to use an `on_destroy()` callback.
  binn_flags_t flags;
  /// Payload value. Optional when `value.buf` is zero. For strings/blobs `size` is the payload
  /// length in bytes; for numeric values `size` must cover the value storage.
  struct binn_val value;
  void (*on_destroy)(struct binn*); ///< Optional callback invoked when the binn object is destroyed.
};

/// @brief Binn library version information.
struct binnc_version {
  int major;
  int minor;
  int patch;
  const char *flavor;
  const char *revision;
};

/// @brief Get a binn library version.
void binnc_version_get(struct binnc_version *out_version);

/// @brief Creates a binn value using an optional `holder` (usually allocated on the stack).
///
/// `spec` specifies the value `type` and the value data. If `holder` is zero, a binn structure is
/// allocated on the heap. Container objects can also be created; in that case `value` is not used.
///
/// In any case resulting binn or holder should be released by `binn_destroy()`.
///
/// @param holder Optional storage for the created binn structure, or zero to allocate it on the heap.
/// @param spec Value creation specification.
/// @return Pointer to the created binn value, or zero on error with `errno` set accordingly.
struct binn* binn_create_value(struct binn *holder, const struct binn_create_spec *spec);

/// @brief Creates an unsigned integer scalar holding `val`, using automatic integer compression.
///
/// The narrowest unsigned integer type able to represent `val` is selected, one of
/// `BINN_TYPE_UINT8`, `BINN_TYPE_UINT16`, `BINN_TYPE_UIN32` or `BINN_TYPE_UINT64`.
///
/// @param holder Optional storage for the created binn structure, or zero to allocate it on the heap.
/// @param val Value to store.
/// @return Pointer to the created binn value, or zero on error with `errno` set accordingly.
///
/// @see binn_create_value()
struct binn* binn_create_u64c(struct binn *holder, uint64_t val);

/// @brief Creates a signed integer scalar holding `val`, using automatic integer compression.
///
/// The narrowest signed integer type able to represent `val` is selected, one of
/// `BINN_TYPE_INT8`, `BINN_TYPE_INT16`, `BINN_TYPE_INT32` or `BINN_TYPE_IN64`.
///
/// @param holder Optional storage for the created binn structure, or zero to allocate it on the heap.
/// @param val Value to store.
/// @return Pointer to the created binn value, or zero on error with `errno` set accordingly.
///
/// @see binn_create_value()
struct binn* binn_create_i64c(struct binn *holder, int64_t val);

/// @brief Creates a single precision floating point scalar (`BINN_TYPE_FLOAT`) holding `val`.
///
/// @param holder Optional storage for the created binn structure, or zero to allocate it on the heap.
/// @param val Value to store.
/// @return Pointer to the created binn value, or zero on error with `errno` set accordingly.
///
/// @see binn_create_value()
struct binn* binn_create_f32c(struct binn *holder, float val);

/// @brief Creates a floating point scalar holding `val`, using automatic floating point compression.
///
/// `BINN_TYPE_FLOAT` is used when `val` can be represented as a 32-bit float without loss of
/// precision, otherwise `BINN_TYPE_DOUBLE` is used.
///
/// @param holder Optional storage for the created binn structure, or zero to allocate it on the heap.
/// @param val Value to store.
/// @return Pointer to the created binn value, or zero on error with `errno` set accordingly.
///
/// @see binn_create_value()
struct binn* binn_create_f64c(struct binn *holder, double val);

/// @brief Creates a UTF-8 text scalar (`BINN_TYPE_TEXT`) holding `val`.
///
/// `vallen` is the byte length of `val`, excluding the terminating null byte; the input does not have
/// to be null terminated. A negative `vallen` makes the length be derived with `strlen(val)`.
/// A zero `val` creates an empty text value.
///
/// The text is copied into the created value.
///
/// @param holder Optional storage for the created binn structure, or zero to allocate it on the heap.
/// @param val Text to store, or zero to create an empty text value.
/// @param vallen Byte length of `val`, or a negative value to derive it with `strlen(val)`.
/// @return Pointer to the created binn value, or zero on error with `errno` set accordingly.
///
/// @see binn_create_value()
struct binn* binn_create_text(struct binn *holder, const char *val, ssize_t vallen);

/// @brief Creates a new binn value that wraps the given serialized binn `value`.
///
/// If `value` points to a `struct binn` already, it is cast to `struct binn*` instead of being wrapped,
/// and `out_casted` is set accordingly.
///
/// @note If `out_casted` is set to `false`, the caller must dispose of the result with `binn_destroy()`.
/// @note A provided serialized binn `value` is not copied. The caller is responsible for releasing the
/// resources allocated for `value` when the binn object is destroyed.
///
/// @param holder Optional storage for the created binn structure, or zero to allocate it on the heap.
/// @param value Serialized binn value or a `struct binn` wrapper (see `binnv()`).
/// @param out_casted Set to `true` if `value` was cast to `struct binn*` rather than wrapped.
/// @return Pointer to the resulting binn value, or zero on error.
///
/// @see binn::on_destroy
struct binn* binn_from(struct binn *holder, struct binn_val value, bool *out_casted);

/// @brief Creates a new binn value that wraps the given serialized binn `value` without validating it.
///
/// Behaves like `binn_from()` but skips the structural validation of the wrapped buffer. Only the root
/// header is parsed; malformed children are detected lazily when the value is read. Use it only for
/// buffers that are already known to be valid (for example, produced by `binn_serialize_*()`).
///
/// @param holder Optional storage for the created binn structure, or zero to allocate it on the heap.
/// @param value Serialized binn value or a `struct binn` wrapper (see `binnv()`).
/// @param out_casted Set to `true` if `value` was cast to `struct binn*` rather than wrapped.
/// @return Pointer to the resulting binn value, or zero on error.
///
/// @see binn_from()
struct binn* binn_from_unchecked(struct binn *holder, struct binn_val value, bool *out_casted);

/// @brief Creates a new binn value by copying the given serialized binn `value` or binn structure.
///
/// @param holder Optional storage for the created binn structure, or zero to allocate it on the heap.
/// @param value Serialized binn value or a `struct binn` wrapper to copy (see `binnv()`).
/// @return Pointer to the copied binn value, or zero on error.
struct binn* binn_copy_from(struct binn *holder, struct binn_val value);

/// @brief Short form for creating a binn list (`BINN_TYPE_LIST`) value.
///
/// @param holder Optional storage for the created binn structure, or zero to allocate it on the heap.
/// @return Pointer to the created binn list, or zero on error.
struct binn* binn_create_list(struct binn *holder);

/// @brief Short form for creating a binn map (`BINN_TYPE_MAP`) value.
///
/// @param holder Optional storage for the created binn structure, or zero to allocate it on the heap.
/// @return Pointer to the created binn map, or zero on error.
struct binn* binn_create_map(struct binn *holder);

/// @brief Short form for creating a binn object (`BINN_TYPE_OBJECT`) value.
///
/// @param holder Optional storage for the created binn structure, or zero to allocate it on the heap.
/// @return Pointer to the created binn object, or zero on error.
struct binn* binn_create_object(struct binn *holder);

/// @brief Destroys a binn value. Does nothing if `binn` is zero.
///
/// Deallocates the `binn` struct itself only if `BINN_FLG_STRUCT_ON_HEAP` is set.
/// Frees the `binn::val::bval` buffer if the binn is not in `wrapped mode` and its type stores the value
/// as `bval`.
///
/// @param binn Binn value to destroy. May be zero.
/// @return `true` if the `binn` struct itself was freed or was a zero, in which case the pointer is no longer valid.
bool binn_destroy(struct binn *binn);

/// @brief Returns `true` if a given `value` represents a `struct binn` object.
///
/// @param value Value to inspect.
/// @return `true` if `value` represents a `struct binn` object.
bool binn_is_struct_instance(struct binn_val value);

/// @brief Returns `true` if a given `value` represents a serialized binn buffer.
///
/// @param value Value to inspect.
/// @return `true` if `value` represents a serialized binn buffer.
bool binn_is_serialized_buffer(struct binn_val value);

/// @brief Returns `true` if a given `value` is either a binn structure or a serialized binn value.
///
/// @param value Value to inspect.
/// @return `true` if `value` is a binn structure or a serialized binn value.
bool binn_is_any(struct binn_val value);

/// @brief Serializes a given binn object and feeds the result to the `consumer` callback.
///
/// The `consumer` may be called many times as the serialized data is produced. The final call signals
/// the end of the stream and is made with a zero `val.buf` and a zero `val.size`.
///
/// @param binn A pointer to a binn struct to serialize.
/// @param consumer Callback invoked with chunks of the serialized binn buffer.
/// @param op Optional user data passed to the `consumer` callback.
/// @return `false` on error.
bool binn_serialize(const struct binn *binn, void (*consumer)(struct binn_val val, void *op), void *op);

/// @brief Serializes a given binn object into a newly allocated heap buffer.
///
/// The returned buffer must be freed by the caller after use.
///
/// @param binn A pointer to a binn struct to serialize.
/// @return Allocated serialized value (release `binn_val::buf`), or a zero `binn_val` on error.
struct binn_val binn_serialize_allocated(const struct binn *binn);

/// @brief Writes a given binn object in serialized form into `buffer` of `buflen` bytes.
///
/// Zero is returned if the data cannot fit into the provided buffer, or on any other error.
///
/// @param binn A pointer to a binn struct to serialize.
/// @param buffer Destination buffer for the serialized data.
/// @param buflen Size of the `buffer` in bytes.
/// @return Serialized value referencing `buffer`, or a zero `binn_val` on error.
struct binn_val binn_serialize_into(const struct binn *binn, uint8_t *buffer, size_t buflen);

/// @brief Adds a `value` associated with the given string `key` to a `BINN_TYPE_OBJECT` binn object.
///
/// @param binn Target binn object.
/// @param key Key to associate the value with. Its length must not exceed `BINN_KEY_MAXLEN`.
/// @param value A binn struct or a serialized binn value (see `binnv()`).
/// @return `false` on error.
bool binn_add_keyed(struct binn *binn, const char *key, struct binn_val value);

/// @brief Adds a `value` associated with the given numeric `idx` key to a `BINN_TYPE_MAP` binn object.
///
/// @param binn Target binn object.
/// @param idx Numeric key to associate the value with.
/// @param value A binn struct or a serialized binn value (see `binnv()`).
/// @return `false` on error.
bool binn_add_mapped(struct binn *binn, int32_t idx, struct binn_val value);

/// @brief Adds a `value` to a `BINN_TYPE_LIST` binn object.
///
/// @param binn Target binn object.
/// @param value A binn struct or a serialized binn value (see `binnv()`).
/// @param out_idx Optional. The zero-based index of the added element in the list is stored here.
/// @return `false` on error.
bool binn_add(struct binn *binn, struct binn_val value, int32_t *out_idx);

/// Iterator internal state. Not intended for direct use.
struct binn_iter_state {
  const uint8_t *next;      ///< Pointer to the next child serialized element.
  const uint8_t *end;       ///< End of the container children region.
  uint32_t       remaining; ///< Number of children left to visit.
  binn_type_t    ctype;     ///< Type of the container being iterated.
};

/// Binn iterator over all the children of a given container binn value.
struct binn_iter {
  struct binn binn;                 ///< Current binn object holder. Use only when `exists` is `true`.
                                    ///  It is a shallow copy of a binn or serialized data element in
                                    ///  `wrapper mode`.
  const char *key;                  ///< Zero-terminated string key of the current object in the binn
                                    ///  container, or zero if not applicable. A non-zero value normally
                                    ///  points to `binn_iter::keybuf`.
  int32_t idx;                      ///< Numeric key of the current binn object in a binn map, or `-1` if
                                    ///  not applicable.
  struct binn_iter_state state;     ///< Internal iteration state. Not intended for direct use.
  bool exists;                      ///< `true` if the iterator points to a real binn object.
  char keybuf[BINN_KEY_MAXLEN + 1]; ///< Buffer holding the string key of the current binn object.
};

/// @brief Initializes an iterator over all of the children of given binn object.
///
/// At this stage the iterator does not point to any of the binn's children and requires a
/// `binn_iter_next()` call.
///
/// @param binn Binn object to iterate over: a `struct binn` or a serialized binn value (see `binnv()`).
/// @param iter Iterator to initialize.
/// @return `false` on error.
bool binn_iter_init(struct binn_val binn, struct binn_iter *iter);

/// @brief Advances a given iterator to the next child.
///
/// @param iter Iterator to advance.
/// @return `true` if the iterator was moved to the next child, `false` otherwise.
bool binn_iter_next(struct binn_iter *iter);

/// @brief Releases resources associated with a given iterator.
///
/// The iterator keeps all of its state inline, so this only resets the iterator.
///
/// @param iter Iterator to close.
void binn_iter_close(struct binn_iter *iter);

/// @brief Performs a depth-first traversal of a binn object, calling the `visitor` for every child node.
///
/// The traversal stops immediately if the `visitor` returns `-1`.
///
/// @param binn A pointer to a binn struct to traverse.
/// @param visitor Callback invoked for every child node.
/// @param op Optional user data passed to the `visitor` callback.
void binn_visit(const struct binn *binn, int (*visitor)(struct binn_iter *it, void *op), void *op);

/// @brief Looks up a value associated with the given string `key` in a `BINN_TYPE_OBJECT` binn object.
///
/// @param binn Source binn object.
/// @param key Key to look up.
/// @param result Receives the found value.
/// @return `true` if the key was found, `false` otherwise.
///
/// @see binn_add_keyed()
bool binn_keyed_get(const struct binn *binn, const char *key, struct binn *result);

/// @brief Looks up a `value` associated with the given numeric `idx` key in a `BINN_TYPE_MAP`
///     or in `BINN_TYPE_LIST` binn object.
///
/// @param binn Source binn object.
/// @param nkey Number key to look up in binn map or zero based index if container is a binn list.
/// @param result Receives the found value.
/// @return `true` if the value was found, `false` otherwise.
///
/// @see binn_add_mapped()
bool binn_indexed_get(const struct binn *binn, uint32_t nkey, struct binn *result);


#endif
