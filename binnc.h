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

/// Control flags affecting the behavior of binn operations. See the `BINN_FLG_*` constants.
typedef uint16_t binn_flags_t;

/// Magic number used to distinguish a binn structure from a serialized binn buffer.
/// Expected to be stored in `struct binn_create_spec::magic`.
#define BINN_MAJIC_NUMBER 0x1F22B11F

/// Max length of binn object key.
#define BINN_KEY_MAXLEN 255

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

/// Do not copy the provided data into the binn object.
/// Used by `binn_create_value()`. The caller owns the provided data.
/// Useful only for non-numeric data, such as strings and blobs.
#define BINN_FLG_DATA_NO_COPY   0x01
#define BINN_FLG_STRUCT_ON_HEAP 0x02 ///< The binn struct is allocated on the heap.

/// Binn value representation.
struct binn {
  uint16_t    flags;                    ///< Object mode and flags.
  binn_type_t type;                     ///< Binn object type.
  uint32_t    count;                    ///< Number of elements in the binn container.
  uint32_t    allocated;                ///< Number of bytes allocated for `ptr`.
  uint32_t    used;                     ///< Number of bytes actually used in the buffer owned by `ptr`.
  size_t      size;                     ///< Size of the serialized binn object in bytes.
                                        ///  Note: the binn object may not actually be in serialized (finalized) form.
  uint64_t nval;                        ///< Data value for numeric types. Up to eight bytes.
  uint8_t *ptr;                         ///< Pointer to the binn buffer.
  void    *user_data;                   ///< Optional user data associated with the binn object.
  void     (*on_destroy)(struct binn*); ///< Optional callback invoked when the binn object is destroyed.
};

/// Binn value creation specification.
/// Used as the specification for `binn_create_value()`.
struct binn_create_spec {
  uint32_t    magic; ///< Magic number used to distinguish a binn structure from a serialized binn buffer.
  binn_type_t type;  ///< Type of the binn value to create. Container types are also supported.
  /// Control flags affecting binn creation. Currently in use:
  /// - BINN_FLG_DATA_NO_COPY The value will not be copied into the binn object.
  ///   The caller controls the provided value. A convenient way to release it
  ///   is to use an `on_destroy()` callback.
  binn_flags_t flags;
  void  *value;                       ///< Optional binn payload value.
  size_t valuesz;                     ///< Size of the `value` payload in bytes.
  void   (*on_destroy)(struct binn*); ///< Optional callback invoked when the binn object is destroyed.
};

/// @brief Creates a binn value using an optional `holder` (usually allocated on the stack).
///
/// `spec` specifies the value `type` and the value data. If `holder` is zero, a binn structure is
/// allocated on the heap. Container objects can also be created; in that case `value` is not used.
///
/// @param holder Optional storage for the created binn structure, or zero to allocate it on the heap.
/// @param spec Value creation specification.
/// @return Pointer to the created binn value, or zero on error with `errno` set accordingly.
struct binn* binn_create_value(struct binn *holder, const struct binn_create_spec *spec);

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
/// @param value Serialized binn buffer, or a pointer to a `struct binn`.
/// @param out_casted Set to `true` if `value` was cast to `struct binn*` rather than wrapped.
/// @return Pointer to the resulting binn value, or zero on error.
///
/// @see binn::on_destroy
struct binn* binn_from(struct binn *holder, void *value, bool *out_casted);

/// @brief Creates a new binn value by copying the given serialized binn `value` or binn structure.
///
/// @param holder Optional storage for the created binn structure, or zero to allocate it on the heap.
/// @param value Serialized binn buffer, or a pointer to a `struct binn` to copy.
/// @return Pointer to the copied binn value, or zero on error.
struct binn* binn_copy_from(struct binn *holder, void *value);

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
/// @param binn Binn value to destroy.
void binn_destroy(struct binn *binn);

/// @brief Returns `true` if a given `value` represents a `struct binn` object.
///
/// @param value Value to inspect.
/// @return `true` if `value` represents a `struct binn` object.
bool binn_is_struct_instance(void *value);

/// @brief Returns `true` if a given `value` represents a serialized binn buffer.
///
/// @param value Value to inspect.
/// @return `true` if `value` represents a serialized binn buffer.
bool binn_is_serialized_buffer(void *value);

/// @brief Returns `true` if a given `value` is either a binn structure or a serialized binn value.
///
/// @param value Value to inspect.
/// @return `true` if `value` is a binn structure or a serialized binn value.
bool binn_is_any(void *value);

/// @brief Serializes a given binn `value` and feeds the result to the `consumer` callback.
///
/// The `consumer` may be called many times as the serialized data is produced. The final call signals
/// the end of the stream and is made with zero `data` and zero `len`.
///
/// @param value A pointer to a binn struct or a serialized binn value.
/// @param consumer Callback invoked with chunks of the serialized binn buffer.
/// @param op Optional user data passed to the `consumer` callback.
/// @return `false` on error.
bool binn_serialize(void *value, void (*consumer)(uint8_t *data, size_t len, void *op), void *op);

/// @brief Adds a `value` associated with the given string `key` to a `BINN_TYPE_OBJECT` binn object.
///
/// @param binn Target binn object.
/// @param key Key to associate the value with. Its length must not exceed `BINN_KEY_MAXLEN`.
/// @param value A pointer to a binn struct or a serialized binn value.
/// @return `false` on error.
bool binn_add_keyed(struct binn *binn, const char *key, void *value);

/// @brief Adds a `value` associated with the given numeric `idx` key to a `BINN_TYPE_MAP` binn object.
///
/// @param binn Target binn object.
/// @param idx Numeric key to associate the value with.
/// @param value A pointer to a binn struct or a serialized binn value.
/// @return `false` on error.
bool binn_add_indexed(struct binn *binn, int32_t idx, void *value);

/// @brief Adds a `value` to a `BINN_TYPE_LIST` binn object.
///
/// @param binn Target binn object.
/// @param value A pointer to a binn struct or a serialized binn value.
/// @return `false` on error.
bool binn_add(struct binn *binn, void *value);

/// Binn iterator over the children of a container binn value.
struct binn_iter {
  struct binn binn;                  ///< Current binn object holder. Use only when `exists` is `true`.
  char     key[BINN_KEY_MAXLEN + 1]; ///< String key of the current binn object.
  uint32_t idx;                      ///< Indexed key of the current binn object.
  void    *next;                     ///< Pointer to the next child binn object.
  bool     exists;                   ///< `true` if the iterator points to a real binn object.
};

/// @brief Initializes an iterator over the children of the given binn object.
///
/// At this stage the iterator does not point to any of the binn's children and requires a
/// `binn_iter_next()` call.
///
/// @param binn Binn object to iterate over: a pointer to a binn struct or a serialized binn value.
/// @param iter Iterator to initialize.
/// @return `false` on error.
bool binn_iter_init(void *binn, struct binn_iter *iter);

/// @brief Advances a given iterator to the next child.
///
/// @param next Iterator to advance.
/// @return `true` if the iterator was moved to the next child, `false` otherwise.
bool binn_iter_next(struct binn_iter *next);

/// @brief Releases resources associated with a given iterator.
///
/// @param iter Iterator to close.
void binn_iter_close(struct binn_iter *iter);

#endif
