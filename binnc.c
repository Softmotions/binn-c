/// BINNC Library implementation.
///
/// Format reference: `docs/spec.md`.
///
/// Internal overview
/// =================
/// A `struct binn` operates in one of three modes (see `binnc.h`):
///
/// * construction mode  - the value is owned by the struct. Numeric values are kept inline in
///                        `binn::val::nval`. Strings/blobs keep their payload in a heap buffer
///                        (`binn::val::bval`, `binn::used` bytes). Containers keep the concatenated
///                        serialized children in a heap buffer (`binn::val::bval`, `binn::used` bytes),
///                        without the container header. The header is (re)computed on serialization
///                        from `binn::count` and `binn::used`.
/// * `BINN_FLG_WRAPPED_BINN` - the struct is a read only view over a serialized container buffer
///                        (`binn::wrapped`, `binn::wrapped.size` bytes). The first modification
///                        triggers a copy-on-write to construction mode.
/// * `BINN_FLG_WRAPPED_PAYLOAD` - the struct is a read only view over an external payload buffer
///                        (string/blob data) or holds a numeric value read from a serialized buffer.
///
/// In all modes `binn::size` holds the size in bytes of the serialized value.
///
/// Every externally supplied buffer is passed as a `struct binn_val` carrying both the pointer and
/// its size. All parsing and validation is bounded by that size, so malformed or truncated input is
/// rejected instead of being read out of bounds.

#include "binnc.h"
#include "binnc_cfg.h" // IWYU pragma: keep

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

/* ------------------------------------------------------------------ */
/* Internal constants                                                 */
/* ------------------------------------------------------------------ */

/// Storage classes (the 3 most significant bits of a binn type) and the
/// `has more` marker of the type encoding.
enum {
  BINN_ST_NOBYTES   = 0,
  BINN_ST_BYTE      = 1,
  BINN_ST_WORD      = 2,
  BINN_ST_DWORD     = 3,
  BINN_ST_QWORD     = 4,
  BINN_ST_STRING    = 5,
  BINN_ST_BLOB      = 6,
  BINN_ST_CONTAINER = 7,
  BINN_FLG_HAS_MORE = 0x10, ///< The type is encoded using two bytes.
};

enum {
  BINN_SUBTYPE_LIST   = 0,
  BINN_SUBTYPE_MAP    = 1,
  BINN_SUBTYPE_OBJECT = 2,
};

/* ------------------------------------------------------------------ */
/* Bounded buffer reference helpers                                   */
/* ------------------------------------------------------------------ */

/// Builds a `struct binn_val` referencing `size` bytes at `buf`.
static struct binn_val binn_val_make(const void *buf, size_t size) {
  struct binn_val v;
  v.buf = (void*) (uintptr_t) buf;
  v.size = size;
  return v;
}

/// A zero length `struct binn_val` referencing `buf`.
static struct binn_val binn_val_of_struct(const void *b) {
  return binn_val_make(b, sizeof(struct binn));
}

/* ------------------------------------------------------------------ */
/* Big endian scalar helpers                                          */
/* ------------------------------------------------------------------ */

// Host byte order. Prefer the compiler built-in, fall back to the build system probe.
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__)
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define BINNC_BIG_ENDIAN 1
#else
#define BINNC_BIG_ENDIAN 0
#endif
#elif defined(IW_BIGENDIAN)
#define BINNC_BIG_ENDIAN 1
#else
#define BINNC_BIG_ENDIAN 0
#endif

// 16/32/64 bit byte swap. The fallback evaluates its argument more than once, so only pass values
// without side effects.
#if defined(__clang__) || (defined(__GNUC__) && ((__GNUC__ > 4) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8)))
#define BINNC_BSWAP16(v__) ((uint16_t) __builtin_bswap16((uint16_t) (v__)))
#define BINNC_BSWAP32(v__) ((uint32_t) __builtin_bswap32((uint32_t) (v__)))
#define BINNC_BSWAP64(v__) ((uint64_t) __builtin_bswap64((uint64_t) (v__)))
#elif defined(_MSC_VER)
#define BINNC_BSWAP16(v__) ((uint16_t) _byteswap_ushort((unsigned short) (v__)))
#define BINNC_BSWAP32(v__) ((uint32_t) _byteswap_ulong((unsigned long) (v__)))
#define BINNC_BSWAP64(v__) ((uint64_t) _byteswap_uint64((unsigned __int64) (v__)))
#else
#define BINNC_BSWAP16(v__) ((uint16_t) ((((uint16_t) (v__) & 0x00FFu) << 8) | (((uint16_t) (v__) & 0xFF00u) >> 8)))
#define BINNC_BSWAP32(v__) ((uint32_t) ((((uint32_t) (v__) & 0x000000FFu) << 24) | \
                                        (((uint32_t) (v__) & 0x0000FF00u) << 8) |  \
                                        (((uint32_t) (v__) & 0x00FF0000u) >> 8) |  \
                                        (((uint32_t) (v__) & 0xFF000000u) >> 24)))
#define BINNC_BSWAP64(v__) ((uint64_t) ((((uint64_t) BINNC_BSWAP32((uint32_t) (v__))) << 32) | \
                                        (uint64_t) BINNC_BSWAP32((uint32_t) ((uint64_t) (v__) >> 32))))
#endif

// Converts a scalar between host and big endian order (the operation is its own inverse).
#if BINNC_BIG_ENDIAN
#define BINNC_HOST_TO_BE16(v__) ((uint16_t) (v__))
#define BINNC_HOST_TO_BE32(v__) ((uint32_t) (v__))
#define BINNC_HOST_TO_BE64(v__) ((uint64_t) (v__))
#else
#define BINNC_HOST_TO_BE16(v__) BINNC_BSWAP16(v__)
#define BINNC_HOST_TO_BE32(v__) BINNC_BSWAP32(v__)
#define BINNC_HOST_TO_BE64(v__) BINNC_BSWAP64(v__)
#endif

static inline uint32_t get_be32(const uint8_t *p) {
  uint32_t v;
  memcpy(&v, p, sizeof(v));
  return BINNC_HOST_TO_BE32(v);
}

static inline void put_be32(uint8_t *p, uint32_t v) {
  v = BINNC_HOST_TO_BE32(v);
  memcpy(p, &v, sizeof(v));
}

static inline uint64_t get_scalar_be(const uint8_t *p, size_t n) {
  switch (n) {
    case 1:
      return p[0];
    case 2: {
      uint16_t v;
      memcpy(&v, p, sizeof(v));
      return BINNC_HOST_TO_BE16(v);
    }
    case 4: {
      uint32_t v;
      memcpy(&v, p, sizeof(v));
      return BINNC_HOST_TO_BE32(v);
    }
    case 8: {
      uint64_t v;
      memcpy(&v, p, sizeof(v));
      return BINNC_HOST_TO_BE64(v);
    }
    default:
      return 0;
  }
}

static inline void put_scalar_be(uint8_t *p, uint64_t v, size_t n) {
  switch (n) {
    case 1:
      p[0] = (uint8_t) v;
      return;
    case 2: {
      uint16_t x = BINNC_HOST_TO_BE16((uint16_t) v);
      memcpy(p, &x, sizeof(x));
      return;
    }
    case 4: {
      uint32_t x = BINNC_HOST_TO_BE32((uint32_t) v);
      memcpy(p, &x, sizeof(x));
      return;
    }
    case 8: {
      uint64_t x = BINNC_HOST_TO_BE64(v);
      memcpy(p, &x, sizeof(x));
      return;
    }
    default:
      return;
  }
}

/* ------------------------------------------------------------------ */
/* Type helpers                                                       */
/* ------------------------------------------------------------------ */

/// Brings a user supplied type into the canonical 16 bit representation.
static binn_type_t type_canonical(binn_type_t type) {
  if (type > 0xFF) {
    return type;
  }
  if (type & BINN_FLG_HAS_MORE) {
    // A one byte type with the `has more` bit set is not valid: re-encode it using two bytes.
    binn_type_t storage = (binn_type_t) ((type >> 5) & 0x07);
    binn_type_t subtype = (binn_type_t) (type & 0x0F);
    return (binn_type_t) ((storage << 13) | 0x1000 | subtype);
  }
  return type;
}

static inline int type_storage(binn_type_t type) {
  if (type > 0xFF) {
    return (type >> 13) & 0x07;
  }
  return (type >> 5) & 0x07;
}

static inline int type_subtype(binn_type_t type) {
  if (type > 0xFF) {
    return type & 0x0FFF;
  }
  return type & 0x0F;
}

static inline bool type_is_container(binn_type_t type) {
  return type_storage(type) == BINN_ST_CONTAINER;
}

/// Size in bytes of the encoded type parameter.
static inline size_t type_encoded_size(binn_type_t type) {
  return (type > 0xFF) ? 2u : 1u;
}

/// Payload size in bytes of the given storage class (0 for NOBYTES/STRING/BLOB/CONTAINER).
static inline size_t storage_bytes(int storage) {
  switch (storage) {
    case BINN_ST_BYTE:
      return 1;
    case BINN_ST_WORD:
      return 2;
    case BINN_ST_DWORD:
      return 4;
    case BINN_ST_QWORD:
      return 8;
    default:
      return 0;
  }
}

/// `true` if the type denotes a signed integer. All signed integers use the sub-type 1.
static inline bool type_is_signed_int(binn_type_t type) {
  int storage = type_storage(type);
  if (storage < BINN_ST_BYTE || storage > BINN_ST_QWORD) {
    return false;
  }
  return type_subtype(type) == 1;
}

/// Reads a big endian scalar value of the given storage class from `p`.
static uint64_t load_scalar(const void *p, int storage, bool is_signed) {
  size_t n = storage_bytes(storage);
  uint64_t v;
  if (n == 0) {
    return 0;
  }
  v = get_scalar_be((const uint8_t*) p, n);
  if (is_signed && n < 8) {
    uint64_t sign = (uint64_t) 1 << (n * 8 - 1);
    if (v & sign) {
      v |= ~(((uint64_t) 1 << (n * 8)) - 1);
    }
  }
  return v;
}

/// Reads a native (host byte order) scalar value of the given storage class from `p`.
static uint64_t load_native_scalar(const void *p, int storage, bool is_signed) {
  uint64_t v = 0;
  switch (storage) {
    case BINN_ST_BYTE: {
      uint8_t t;
      memcpy(&t, p, 1);
      v = is_signed ? (uint64_t) (int64_t) (int8_t) t : t;
      break;
    }
    case BINN_ST_WORD: {
      uint16_t t;
      memcpy(&t, p, 2);
      v = is_signed ? (uint64_t) (int64_t) (int16_t) t : t;
      break;
    }
    case BINN_ST_DWORD: {
      uint32_t t;
      memcpy(&t, p, 4);
      v = is_signed ? (uint64_t) (int64_t) (int32_t) t : t;
      break;
    }
    case BINN_ST_QWORD: {
      uint64_t t;
      memcpy(&t, p, 8);
      v = t;
      break;
    }
    default:
      break;
  }
  return v;
}

/// Writes the given scalar into a 16 bit buffer using the binn type encoding.
static size_t put_type(uint8_t *out, binn_type_t type) {
  if (type > 0xFF) {
    out[0] = (uint8_t) (type >> 8);
    out[1] = (uint8_t) type;
    return 2;
  }
  out[0] = (uint8_t) type;
  return 1;
}

static size_t size_param_size(size_t n) {
  return (n <= 127) ? 1u : 4u;
}

static size_t put_size_param(uint8_t *out, size_t n) {
  if (n <= 127) {
    out[0] = (uint8_t) n;
    return 1;
  }
  put_be32(out, (uint32_t) n | 0x80000000u);
  return 4;
}

/// Size in bytes of a serialized scalar (numeric/nobytes) value.
static inline size_t leaf_serialized_size(binn_type_t type, size_t payload_len) {
  int storage = type_storage(type);
  size_t n = type_encoded_size(type);
  if (storage == BINN_ST_STRING || storage == BINN_ST_BLOB) {
    n += size_param_size(payload_len) + payload_len;
    if (storage == BINN_ST_STRING) {
      n += 1; // null terminator
    }
  } else {
    n += storage_bytes(storage);
  }
  return n;
}

/* ------------------------------------------------------------------ */
/* Serialized value parsing                                           */
/* ------------------------------------------------------------------ */

/// Description of a parsed serialized binn value.
struct value_meta {
  binn_type_t type;       ///< Parsed type (canonical form).
  int      storage;       ///< Storage class index.
  int      subtype;       ///< Sub-type.
  uint32_t count;         ///< Element count for containers.
  size_t   total;         ///< Total serialized size in bytes.
  const uint8_t *payload; ///< Payload/children pointer (may be zero).
  size_t payload_len;     ///< Payload/children length.
};

/// Reads a `[size]`/`[count]` parameter (1 or 4 bytes, big endian).
/// `end` may be zero, in which case only self consistency of the parameter is checked.
static bool read_param(const uint8_t **pp, const uint8_t *end, size_t *out) {
  const uint8_t *p = *pp;
  if (end && p >= end) {
    return false;
  }
  if (*p & 0x80) {
    if (end && (size_t) (end - p) < 4) {
      return false;
    }
    *out = (size_t) (get_be32(p) & 0x7FFFFFFFu);
    p += 4;
  } else {
    *out = (size_t) *p;
    p += 1;
  }
  *pp = p;
  return true;
}

/// Parses a serialized value starting at `p`. `end` limits the readable range and may be zero.
/// Returns the pointer just past the value, or zero on error.
static const uint8_t* parse_value(const uint8_t *p, const uint8_t *end, struct value_meta *m) {
  const uint8_t *start;
  binn_type_t type;
  uint8_t b;
  size_t sz;
  size_t hdr;

  if (!p || !m) {
    return 0;
  }
  if (end && p >= end) {
    return 0;
  }
  start = p;
  b = *p;
  if (b & BINN_FLG_HAS_MORE) {
    if (end && (size_t) (end - p) < 2) {
      return 0;
    }
    type = (binn_type_t) (((uint16_t) b << 8) | p[1]);
    p += 2;
  } else {
    type = b;
    p += 1;
  }

  memset(m, 0, sizeof(*m));
  m->type = type;
  m->storage = type_storage(type);
  m->subtype = type_subtype(type);

  switch (m->storage) {
    case BINN_ST_NOBYTES:
      m->total = (size_t) (p - start);
      return p;
    case BINN_ST_BYTE:
    case BINN_ST_WORD:
    case BINN_ST_DWORD:
    case BINN_ST_QWORD: {
      size_t n = storage_bytes(m->storage);
      if (end && (size_t) (end - p) < n) {
        return 0;
      }
      m->payload = p;
      m->payload_len = n;
      m->total = (size_t) (p - start) + n;
      return p + n;
    }
    case BINN_ST_STRING:
    case BINN_ST_BLOB:
      if (!read_param(&p, end, &sz)) {
        return 0;
      }
      if (end && (size_t) (end - p) < sz) {
        return 0;
      }
      m->payload = p;
      m->payload_len = sz;
      p += sz;
      if (m->storage == BINN_ST_STRING) {
        if (end && p >= end) {
          return 0;
        }
        if (*p != 0) { // strings are null terminated
          return 0;
        }
        p += 1; // null terminator
      }
      m->total = (size_t) (p - start);
      return p;
    case BINN_ST_CONTAINER:
      if (!read_param(&p, end, &sz)) {
        return 0;
      }
      {
        size_t cnt;
        if (!read_param(&p, end, &cnt)) {
          return 0;
        }
        m->count = (uint32_t) cnt;
      }
      hdr = (size_t) (p - start);
      if (sz < hdr || sz < MIN_BINN_SIZE) {
        return 0;
      }
      if (end && (size_t) (end - start) < sz) {
        return 0;
      }
      m->payload = p;
      m->payload_len = sz - hdr;
      m->total = sz;
      return start + sz;
    default:
      return 0;
  }
}

/// Validates a serialized value and computes its container nesting depth.
/// A leaf has depth 0, a container has depth `1 + max(child depth)`.
/// Returns -1 if the value is malformed, has an inconsistent element count, or is deeper than `limit`.
/// The recursion is bounded by `limit`.
static int validate_depth(const uint8_t *p, const uint8_t *end, int limit) { // NOLINT(misc-no-recursion)
  struct value_meta m;
  int subtype;
  const uint8_t *cp;
  const uint8_t *cend;
  int maxd = 0;
  uint32_t cnt = 0;

  if (limit < 0) {
    return -1;
  }
  if (!parse_value(p, end, &m)) {
    return -1;
  }
  if (m.storage != BINN_ST_CONTAINER) {
    return 0;
  }
  if (limit < 1) {
    return -1;
  }
  subtype = type_subtype(m.type);
  cp = m.payload;
  cend = m.payload + m.payload_len;
  while (cp < cend) {
    struct value_meta cm;
    const uint8_t *np;
    if (subtype == BINN_SUBTYPE_MAP) { // map key: 4 bytes big endian
      if ((size_t) (cend - cp) < 4) {
        return -1;
      }
      cp += 4;
    } else if (subtype == BINN_SUBTYPE_OBJECT) { // object key: 1 byte length + bytes
      uint8_t klen;
      if (cp >= cend) {
        return -1;
      }
      klen = *cp++;
      if ((size_t) (cend - cp) < klen) {
        return -1;
      }
      cp += klen;
    }
    np = parse_value(cp, cend, &cm);
    if (!np) {
      return -1;
    }
    if (cm.storage == BINN_ST_CONTAINER) {
      int d = validate_depth(cp, cend, limit - 1);
      if (d < 0) {
        return -1;
      }
      if (d > maxd) {
        maxd = d;
      }
    }
    cp = np;
    if (++cnt > m.count) {
      return -1;
    }
  }
  if (cp != cend || cnt != m.count) {
    return -1;
  }
  if (maxd + 1 > limit) {
    return -1;
  }
  return maxd + 1;
}

/// Validates that `p` holds a structurally sound serialized binn value.
static inline bool validate_value(const uint8_t *p, const uint8_t *end) {
  return validate_depth(p, end, BINN_MAX_NESTING_LEVELS) >= 0;
}

/* ------------------------------------------------------------------ */
/* Serialization sinks                                                */
/* ------------------------------------------------------------------ */

struct binn_sink {
  bool  (*write)(void *ctx, const uint8_t *data, size_t len);
  void *ctx;
};

struct mem_sink {
  uint8_t *dst;
  size_t   cap;
  size_t   pos;
};

static bool mem_sink_write(void *ctx, const uint8_t *data, size_t len) {
  struct mem_sink *m = (struct mem_sink*) ctx;
  if (m->pos + len > m->cap) {
    return false;
  }
  if (len) {
    memcpy(m->dst + m->pos, data, len);
  }
  m->pos += len;
  return true;
}

struct stream_sink {
  bool  (*consumer)(const uint8_t *buf, size_t len, void *op);
  void *op;
};

static bool stream_sink_write(void *ctx, const uint8_t *data, size_t len) {
  struct stream_sink *s = (struct stream_sink*) ctx;
  return s->consumer(data, len, s->op);
}

/* ------------------------------------------------------------------ */
/* Sizes and emitting                                                 */
/* ------------------------------------------------------------------ */

/// Total serialized size of a construction mode container (header + children).
static inline size_t container_serialized_size(const struct binn *b) {
  size_t content = b->used;
  size_t type_sz = type_encoded_size(b->type);
  size_t count_sz = size_param_size(b->count);
  size_t total = type_sz + 1 + count_sz + content;
  if (total > 127) {
    total = type_sz + 4 + count_sz + content;
  }
  return total;
}

/// Size in bytes of the serialized form of a binn value.
static size_t value_serialized_size(const struct binn *b) {
  if (b->flags & BINN_FLG_WRAPPED_BINN) {
    return b->wrapped.size;
  }
  if (type_is_container(b->type)) {
    return container_serialized_size(b);
  }
  return leaf_serialized_size(b->type, b->used);
}

/// Emits the serialized form of `b` into the sink.
static bool emit_value(struct binn_sink *sink, const struct binn *b, int depth) {
  int storage;
  uint8_t hdr[16];
  size_t n = 0;

  if (depth > BINN_MAX_NESTING_LEVELS) {
    return false;
  }
  if (b->flags & BINN_FLG_WRAPPED_BINN) {
    if (!b->wrapped.size) {
      return true;
    }
    return sink->write(sink->ctx, b->wrapped.buf, b->wrapped.size);
  }

  storage = type_storage(b->type);
  n += put_type(hdr + n, b->type);

  if (storage == BINN_ST_CONTAINER) {
    size_t total = container_serialized_size(b);
    if (b->flags & BINN_FLG_WRAPPED_PAYLOAD) {
      return false; // invalid state: a wrapped payload cannot be a container
    }
    n += put_size_param(hdr + n, total);
    n += put_size_param(hdr + n, b->count);
    if (!sink->write(sink->ctx, hdr, n)) {
      return false;
    }
    if (b->used && !sink->write(sink->ctx, b->val.bval, b->used)) {
      return false;
    }
    return true;
  }

  if (storage == BINN_ST_STRING || storage == BINN_ST_BLOB) {
    n += put_size_param(hdr + n, b->used);
    if (!sink->write(sink->ctx, hdr, n)) {
      return false;
    }
    if (b->used && !sink->write(sink->ctx, b->val.bval, b->used)) {
      return false;
    }
    if (storage == BINN_ST_STRING) {
      static const uint8_t zero = 0;
      if (!sink->write(sink->ctx, &zero, 1)) {
        return false;
      }
    }
    return true;
  }

  // Numeric / no bytes.
  if (storage_bytes(storage)) {
    put_scalar_be(hdr + n, b->val.nval, storage_bytes(storage));
    n += storage_bytes(storage);
  }
  return sink->write(sink->ctx, hdr, n);
}

/* ------------------------------------------------------------------ */
/* Struct lifecycle helpers                                           */
/* ------------------------------------------------------------------ */

static struct binn* alloc_holder(struct binn *holder) {
  struct binn *b = holder;
  if (!b) {
    b = (struct binn*) malloc(sizeof(struct binn));
    if (!b) {
      errno = ENOMEM;
      return 0;
    }
  }
  memset(b, 0, sizeof(*b));
  b->magic = BINN_MAJIC_NUMBER;
  if (b != holder) {
    b->flags |= BINN_FLG_STRUCT_ON_HEAP;
  }
  return b;
}

static void free_holder(struct binn *b, struct binn *holder) {
  if (b && b != holder) {
    free(b);
  }
}

/// Fills a wrapper mode binn pointing at a serialized value found at `start`.
static void fill_wrapper(struct binn *out, const uint8_t *start, const struct value_meta *m) {
  out->magic = BINN_MAJIC_NUMBER;
  out->type = m->type;
  out->count = m->count;
  out->used = (uint32_t) m->payload_len;
  out->allocated = 0;
  out->user_data = 0;
  out->on_destroy = 0;
  if (m->storage == BINN_ST_CONTAINER) {
    out->flags = BINN_FLG_WRAPPED_BINN;
    out->wrapped = binn_val_make(start, m->total);
  } else {
    out->flags = BINN_FLG_WRAPPED_PAYLOAD;
    if (m->storage == BINN_ST_STRING || m->storage == BINN_ST_BLOB) {
      out->wrapped = binn_val_make(m->payload, m->payload_len);
    } else {
      out->val.nval = load_scalar(m->payload, m->storage, type_is_signed_int(m->type));
      out->wrapped.size = m->payload_len;
    }
  }
  out->size = value_serialized_size(out);
}

/* ------------------------------------------------------------------ */
/* Container internals                                                */
/* ------------------------------------------------------------------ */

/// Returns the container children buffer, its length and the element count.
static const uint8_t* container_children(const struct binn *b, size_t *len, uint32_t *count) {
  if (b->flags & BINN_FLG_WRAPPED_BINN) {
    *len = b->used;
    *count = b->count;
    return (const uint8_t*) b->wrapped.buf + (b->wrapped.size - b->used);
  }
  if (b->flags & BINN_FLG_WRAPPED_PAYLOAD) {
    *len = 0;
    *count = 0;
    return 0;
  }
  *len = b->used;
  *count = b->count;
  return b->val.bval;
}

/// Copies a wrapped container payload to the heap, leaving the struct in construction mode.
static bool binn_cow_container(struct binn *b) {
  uint8_t *buf = 0;
  size_t children_len;

  if (!(b->flags & BINN_FLAGS_WRAPPED)) {
    return true;
  }
  if (!(b->flags & BINN_FLG_WRAPPED_BINN)) {
    errno = EINVAL;
    return false;
  }
  children_len = b->used;
  if (children_len) {
    buf = (uint8_t*) malloc(children_len);
    if (!buf) {
      errno = ENOMEM;
      return false;
    }
    memcpy(buf, (const uint8_t*) b->wrapped.buf + (b->wrapped.size - children_len), children_len);
  }
  b->flags &= (binn_flags_t) ~BINN_FLAGS_WRAPPED;
  b->val.bval = buf;
  b->allocated = (uint32_t) children_len;
  b->size = container_serialized_size(b);
  return true;
}

/// Ensures at least `extra` free bytes are available in the container buffer.
static bool container_reserve(struct binn *b, size_t extra) {
  size_t need = (size_t) b->used + extra;
  size_t cap;
  uint8_t *p;

  if (need <= b->allocated) {
    return true;
  }
  if (need > 0x7FFFFFFFu) {
    errno = EOVERFLOW;
    return false;
  }
  cap = b->allocated ? b->allocated : 64;
  while (cap < need) {
    if (cap > (0x7FFFFFFFu >> 1)) {
      cap = need;
      break;
    }
    cap <<= 1;
  }
  p = (uint8_t*) realloc(b->val.bval, cap);
  if (!p) {
    errno = ENOMEM;
    return false;
  }
  b->val.bval = p;
  b->allocated = (uint32_t) cap;
  return true;
}

/// Appends a serialized value (optionally prefixed by a key) to the container `b`.
static bool container_add_value(struct binn *b, const uint8_t *prefix, size_t prefix_len, struct binn_val value) {
  const struct binn *v = 0;
  const uint8_t *raw = 0;
  size_t value_len = 0;
  bool is_container = false;
  uint8_t *dst;

  if (!value.buf) {
    errno = EINVAL;
    return false;
  }
  if (!binn_cow_container(b)) {
    return false;
  }
  if (binn_is_struct_instance(value)) {
    v = (const struct binn*) value.buf;
    if (v == b) {
      errno = EINVAL; // self reference
      return false;
    }
    is_container = type_is_container(v->type);
    value_len = value_serialized_size(v);
  } else {
    const uint8_t *end;
    struct value_meta m;
    if (!value.size) {
      errno = EINVAL;
      return false;
    }
    end = (const uint8_t*) value.buf + value.size;
    if (!parse_value(value.buf, end, &m)) {
      errno = EINVAL;
      return false;
    }
    raw = value.buf;
    value_len = m.total;
    is_container = (m.storage == BINN_ST_CONTAINER);
  }

  if (!container_reserve(b, prefix_len + value_len)) {
    return false;
  }
  dst = b->val.bval + b->used;
  if (prefix_len) {
    memcpy(dst, prefix, prefix_len);
    dst += prefix_len;
  }
  if (v) {
    struct mem_sink mc;
    struct binn_sink sink;
    mc.dst = dst;
    mc.cap = value_len;
    mc.pos = 0;
    sink.write = mem_sink_write;
    sink.ctx = &mc;
    if (!emit_value(&sink, v, 0) || mc.pos != value_len) {
      errno = EINVAL;
      return false;
    }
  } else if (value_len) {
    memcpy(dst, raw, value_len);
  }
  // Validate the container structure and enforce the nesting limit in a single pass. Leaf values are
  // fully bounds checked by `parse_value()` above and have no nesting to account for.
  if (is_container && validate_depth(dst, dst + value_len, BINN_MAX_NESTING_LEVELS - 1) < 0) {
    errno = EINVAL;
    return false;
  }
  b->used += (uint32_t) (prefix_len + value_len);
  b->count++;
  b->size = container_serialized_size(b);
  return true;
}

/// Searches for an object key. Returns the value pointer or zero.
static const uint8_t* object_find(const uint8_t *cp, const uint8_t *cend, const char *key, size_t keylen) {
  while (cp < cend) {
    uint8_t klen;
    struct value_meta m;
    const uint8_t *np;
    if (cp >= cend) {
      return 0;
    }
    klen = *cp++;
    if ((size_t) (cend - cp) < klen) {
      return 0;
    }
    np = parse_value(cp + klen, cend, &m);
    if (!np) {
      return 0;
    }
    if (klen == keylen && (klen == 0 || memcmp(cp, key, klen) == 0)) {
      return cp + klen;
    }
    cp = np;
  }
  return 0;
}

/// Searches for a map id. Returns the value pointer or zero.
static const uint8_t* map_find(const uint8_t *cp, const uint8_t *cend, int32_t id) {
  while (cp < cend) {
    struct value_meta m;
    const uint8_t *np;
    int32_t cid;
    if ((size_t) (cend - cp) < 4) {
      return 0;
    }
    cid = (int32_t) get_be32(cp);
    cp += 4;
    np = parse_value(cp, cend, &m);
    if (!np) {
      return 0;
    }
    if (cid == id) {
      return cp;
    }
    cp = np;
  }
  return 0;
}

void binnc_version_get(struct binnc_version *out_version) {
  if (out_version) {
    out_version->major = BINNC_VERSION_MAJOR;
    out_version->minor = BINNC_VERSION_MINOR;
    out_version->patch = BINNC_VERSION_PATCH;
    out_version->flavor = BINNC_VERSION_FLAVOR;
    out_version->revision = BINNC_REVISION;
  }
}

/* ------------------------------------------------------------------ */
/* Public API: creation                                               */
/* ------------------------------------------------------------------ */

struct binn* binn_create_value(struct binn *holder, const struct binn_create_spec *spec) {
  binn_type_t type;
  int storage;
  struct binn *b;
  struct binn_val sval;

  if (!spec) {
    errno = EINVAL;
    return 0;
  }
  type = type_canonical(spec->type);
  storage = type_storage(type);
  b = alloc_holder(holder);
  if (!b) {
    return 0;
  }
  b->type = type;
  b->on_destroy = spec->on_destroy;
  sval = spec->value;

  if (storage == BINN_ST_CONTAINER) {
    b->count = 0;
    b->used = 0;
    b->allocated = 0;
    b->val.bval = 0;
    b->size = container_serialized_size(b);
    return b;
  }

  if (storage == BINN_ST_STRING || storage == BINN_ST_BLOB) {
    size_t len = sval.size;
    if (sval.buf && len == 0 && storage == BINN_ST_STRING) {
      len = strlen((const char*) sval.buf);
    }
    if (!sval.buf && len != 0) {
      errno = EINVAL;
      free_holder(b, holder);
      return 0;
    }
    if (len > 0x7FFFFFFFu) {
      errno = EOVERFLOW;
      free_holder(b, holder);
      return 0;
    }
    b->used = (uint32_t) len;
    if (spec->flags & BINN_FLG_WRAPPED_PAYLOAD) {
      b->flags |= BINN_FLG_WRAPPED_PAYLOAD;
      b->wrapped = binn_val_make(sval.buf, len);
    } else if (len) {
      size_t alloc = len + ((storage == BINN_ST_STRING) ? 1 : 0);
      b->val.bval = (uint8_t*) malloc(alloc);
      if (!b->val.bval) {
        errno = ENOMEM;
        free_holder(b, holder);
        return 0;
      }
      memcpy(b->val.bval, sval.buf, len);
      if (storage == BINN_ST_STRING) {
        b->val.bval[len] = 0; // keep string payloads null terminated
      }
      b->allocated = (uint32_t) alloc;
    }
    b->size = leaf_serialized_size(type, len);
    return b;
  }

  // Numeric or no bytes value. The value size must cover the storage class.
  {
    size_t n = storage_bytes(storage);
    if (sval.buf) {
      if (sval.size < n) {
        errno = EINVAL;
        free_holder(b, holder);
        return 0;
      }
      b->val.nval = load_native_scalar(sval.buf, storage, type_is_signed_int(type));
    } else {
      b->val.nval = 0;
    }
    b->used = (uint32_t) n;
    b->size = leaf_serialized_size(type, n);
  }
  return b;
}

struct binn* binn_create_list(struct binn *holder) {
  struct binn_create_spec spec;
  memset(&spec, 0, sizeof(spec));
  spec.type = BINN_TYPE_LIST;
  return binn_create_value(holder, &spec);
}

struct binn* binn_create_map(struct binn *holder) {
  struct binn_create_spec spec;
  memset(&spec, 0, sizeof(spec));
  spec.type = BINN_TYPE_MAP;
  return binn_create_value(holder, &spec);
}

struct binn* binn_create_object(struct binn *holder) {
  struct binn_create_spec spec;
  memset(&spec, 0, sizeof(spec));
  spec.type = BINN_TYPE_OBJECT;
  return binn_create_value(holder, &spec);
}

/// Creates a numeric scalar of the given type directly from a host order value.
static struct binn* create_numeric_value(struct binn *holder, binn_type_t type, uint64_t nval) {
  struct binn *b;
  int storage;
  type = type_canonical(type);
  storage = type_storage(type);
  b = alloc_holder(holder);
  if (!b) {
    return 0;
  }
  b->type = type;
  b->val.nval = nval;
  b->used = (uint32_t) storage_bytes(storage);
  b->size = leaf_serialized_size(type, b->used);
  return b;
}

struct binn* binn_create_u64c(struct binn *holder, uint64_t val) {
  binn_type_t type;
  if (val <= UINT8_MAX) {
    type = BINN_TYPE_UINT8;
  } else if (val <= UINT16_MAX) {
    type = BINN_TYPE_UINT16;
  } else if (val <= UINT32_MAX) {
    type = BINN_TYPE_UINT32;
  } else {
    type = BINN_TYPE_UINT64;
  }
  return create_numeric_value(holder, type, val);
}

struct binn* binn_create_i64c(struct binn *holder, int64_t val) {
  binn_type_t type;
  if (val >= INT8_MIN && val <= INT8_MAX) {
    type = BINN_TYPE_INT8;
  } else if (val >= INT16_MIN && val <= INT16_MAX) {
    type = BINN_TYPE_INT16;
  } else if (val >= INT32_MIN && val <= INT32_MAX) {
    type = BINN_TYPE_INT32;
  } else {
    type = BINN_TYPE_INT64;
  }
  return create_numeric_value(holder, type, (uint64_t) val);
}

struct binn* binn_create_f32c(struct binn *holder, float val) {
  uint32_t bits;
  memcpy(&bits, &val, sizeof(bits));
  return create_numeric_value(holder, BINN_TYPE_FLOAT, bits);
}

struct binn* binn_create_f64c(struct binn *holder, double val) {
  float f32 = (float) val;
  if ((double) f32 == val) {
    uint32_t bits;
    memcpy(&bits, &f32, sizeof(bits));
    return create_numeric_value(holder, BINN_TYPE_FLOAT, bits);
  } else {
    uint64_t bits;
    memcpy(&bits, &val, sizeof(bits));
    return create_numeric_value(holder, BINN_TYPE_DOUBLE, bits);
  }
}

struct binn* binn_create_bool(struct binn *holder, bool val) {
  return create_numeric_value(holder, val ? BINN_TYPE_TRUE : BINN_TYPE_FALSE, 0);
}

struct binn* binn_create_null(struct binn *holder) {
  return create_numeric_value(holder, BINN_TYPE_NULL, 0);
}

struct binn* binn_create_text(struct binn *holder, const char *val, ssize_t vallen) {
  struct binn_create_spec spec;
  memset(&spec, 0, sizeof(spec));
  spec.type = BINN_TYPE_TEXT;
  if (vallen < 0) {
    if (val) {
      spec.value.buf = (void*) (uintptr_t) val;
      spec.value.size = strlen(val);
    }
  } else if (vallen > 0) {
    if (!val) {
      errno = EINVAL;
      return 0;
    }
    spec.value.buf = (void*) (uintptr_t) val;
    spec.value.size = (size_t) vallen;
  }
  // A non positive `vallen` yields an empty text value.
  return binn_create_value(holder, &spec);
}

struct binn* binn_create_blob(struct binn *holder, const void *val, ssize_t vallen) {
  struct binn_create_spec spec;
  if (vallen < 0) {
    errno = EINVAL;
    return 0;
  }
  memset(&spec, 0, sizeof(spec));
  spec.type = BINN_TYPE_BLOB;
  if (vallen > 0) {
    if (!val) {
      errno = EINVAL;
      return 0;
    }
    spec.value.buf = (void*) (uintptr_t) val;
    spec.value.size = (size_t) vallen;
  }
  // A zero `vallen` yields an empty blob.
  return binn_create_value(holder, &spec);
}

static struct binn* binn_from_impl(struct binn *holder, struct binn_val value, bool *out_casted, bool validate) {
  const uint8_t *buf;
  const uint8_t *end;
  struct value_meta m;
  struct binn *b;

  if (out_casted) {
    *out_casted = false;
  }
  if (!value.buf || !value.size) {
    errno = EINVAL;
    return 0;
  }
  if (binn_is_struct_instance(value)) {
    if (out_casted) {
      *out_casted = true;
    }
    return (struct binn*) value.buf;
  }
  buf = (const uint8_t*) value.buf;
  end = buf + value.size;
  if (!parse_value(buf, end, &m)) {
    errno = EINVAL;
    return 0;
  }
  if (validate && m.storage == BINN_ST_CONTAINER && !validate_value(buf, end)) {
    errno = EINVAL;
    return 0;
  }
  b = alloc_holder(holder);
  if (!b) {
    return 0;
  }
  b->type = m.type;
  b->count = m.count;
  b->used = (uint32_t) m.payload_len;
  if (m.storage == BINN_ST_CONTAINER) {
    b->flags |= BINN_FLG_WRAPPED_BINN;
    b->wrapped = binn_val_make(value.buf, m.total);
  } else {
    b->flags |= BINN_FLG_WRAPPED_PAYLOAD;
    if (m.storage == BINN_ST_STRING || m.storage == BINN_ST_BLOB) {
      b->wrapped = binn_val_make(m.payload, m.payload_len);
    } else {
      b->val.nval = load_scalar(m.payload, m.storage, type_is_signed_int(m.type));
    }
  }
  b->size = value_serialized_size(b);
  return b;
}

struct binn* binn_from(struct binn *holder, struct binn_val value, bool *out_casted) {
  return binn_from_impl(holder, value, out_casted, true);
}

struct binn* binn_from_unchecked(struct binn *holder, struct binn_val value, bool *out_casted) {
  return binn_from_impl(holder, value, out_casted, false);
}

/// Creates an owning, construction mode copy of a serialized value.
static struct binn* copy_from_serialized(struct binn *holder, const uint8_t *buf, size_t size) {
  const uint8_t *end = buf + size;
  struct value_meta m;
  struct binn *dst;

  if (!parse_value(buf, end, &m)) {
    errno = EINVAL;
    return 0;
  }
  dst = alloc_holder(holder);
  if (!dst) {
    return 0;
  }
  dst->type = m.type;
  dst->count = m.count;
  dst->used = (uint32_t) m.payload_len;
  if (m.storage == BINN_ST_CONTAINER || m.storage == BINN_ST_STRING || m.storage == BINN_ST_BLOB) {
    if (m.payload_len) {
      size_t alloc = m.payload_len + ((m.storage == BINN_ST_STRING) ? 1 : 0);
      dst->val.bval = (uint8_t*) malloc(alloc);
      if (!dst->val.bval) {
        errno = ENOMEM;
        free_holder(dst, holder);
        return 0;
      }
      memcpy(dst->val.bval, m.payload, m.payload_len);
      if (m.storage == BINN_ST_STRING) {
        dst->val.bval[m.payload_len] = 0;
      }
      dst->allocated = (uint32_t) alloc;
    }
  } else {
    dst->val.nval = load_scalar(m.payload, m.storage, type_is_signed_int(m.type));
  }
  dst->size = value_serialized_size(dst);
  return dst;
}

/// Creates an owning, construction mode copy of a `struct binn` without a serialize/parse round trip.
static struct binn* copy_from_struct(struct binn *holder, const struct binn *v) {
  struct binn *dst = alloc_holder(holder);
  int storage = type_storage(v->type);
  if (!dst) {
    return 0;
  }
  dst->type = v->type;
  dst->count = v->count;
  dst->used = v->used;
  if (type_is_container(v->type)) {
    const uint8_t *src = (v->flags & BINN_FLG_WRAPPED_BINN)
                         ? (const uint8_t*) v->wrapped.buf + (v->wrapped.size - v->used)
                         : v->val.bval;
    if (v->used) {
      dst->val.bval = (uint8_t*) malloc(v->used);
      if (!dst->val.bval) {
        errno = ENOMEM;
        free_holder(dst, holder);
        return 0;
      }
      memcpy(dst->val.bval, src, v->used);
      dst->allocated = v->used;
    }
    dst->size = container_serialized_size(dst);
  } else if (storage == BINN_ST_STRING || storage == BINN_ST_BLOB) {
    if (v->used) {
      size_t alloc = v->used + ((storage == BINN_ST_STRING) ? 1 : 0);
      dst->val.bval = (uint8_t*) malloc(alloc);
      if (!dst->val.bval) {
        errno = ENOMEM;
        free_holder(dst, holder);
        return 0;
      }
      memcpy(dst->val.bval, v->val.bval, v->used);
      if (storage == BINN_ST_STRING) {
        dst->val.bval[v->used] = 0;
      }
      dst->allocated = (uint32_t) alloc;
    }
    dst->size = leaf_serialized_size(v->type, v->used);
  } else {
    dst->val.nval = v->val.nval;
    dst->size = leaf_serialized_size(v->type, v->used);
  }
  return dst;
}

struct binn* binn_copy_from(struct binn *holder, struct binn_val value) {
  if (!value.buf || !value.size) {
    errno = EINVAL;
    return 0;
  }
  if (binn_is_struct_instance(value)) {
    return copy_from_struct(holder, (const struct binn*) value.buf);
  }
  if (!validate_value((const uint8_t*) value.buf, (const uint8_t*) value.buf + value.size)) {
    errno = EINVAL;
    return 0;
  }
  return copy_from_serialized(holder, (const uint8_t*) value.buf, value.size);
}

bool binn_destroy(struct binn *binn) {
  bool on_heap;

  if (!binn) {
    return true;
  }
  if (binn->magic != BINN_MAJIC_NUMBER) {
    return false;
  }
  on_heap = (binn->flags & BINN_FLG_STRUCT_ON_HEAP) != 0;
  if (binn->on_destroy) {
    void (*cb)(struct binn*) = binn->on_destroy;
    binn->on_destroy = 0;
    cb(binn);
  }
  if (!(binn->flags & BINN_FLAGS_WRAPPED)) {
    int storage = type_storage(binn->type);
    if (  (storage == BINN_ST_STRING || storage == BINN_ST_BLOB || storage == BINN_ST_CONTAINER)
       && binn->val.bval) {
      free(binn->val.bval);
      binn->val.bval = 0;
    }
  }
  if (on_heap) {
    free(binn);
    return true;
  }
  // Reset the stack holder so that repeated destroy calls are harmless.
  memset(binn, 0, sizeof(*binn));
  binn->magic = BINN_MAJIC_NUMBER;
  return false;
}

/* ------------------------------------------------------------------ */
/* Public API: type checks                                            */
/* ------------------------------------------------------------------ */

bool binn_is_struct_instance(struct binn_val value) {
  uint32_t magic;
  binn_flags_t flags;
  if (!value.buf || value.size < sizeof(struct binn)) {
    return false;
  }
  // The buffer may be unaligned, so read the fields with memcpy().
  memcpy(&magic, value.buf, sizeof(magic));
  if (magic != BINN_MAJIC_NUMBER) {
    return false;
  }
  // Reject flag combinations that cannot describe a live binn structure. This keeps a crafted
  // serialized buffer that merely starts with the magic number from being mistaken for a struct.
  memcpy(&flags, (const uint8_t*) value.buf + offsetof(struct binn, flags), sizeof(flags));
  if ((flags & (binn_flags_t) ~(BINN_FLAGS_WRAPPED | BINN_FLG_STRUCT_ON_HEAP)) != 0) {
    return false;
  }
  return true;
}

bool binn_is_serialized_buffer(struct binn_val value) {
  if (!value.buf || value.size == 0 || binn_is_struct_instance(value)) {
    return false;
  }
  return validate_value((const uint8_t*) value.buf, (const uint8_t*) value.buf + value.size);
}

bool binn_is_any(struct binn_val value) {
  return binn_is_struct_instance(value) || binn_is_serialized_buffer(value);
}

/* ------------------------------------------------------------------ */
/* Public API: serialization                                          */
/* ------------------------------------------------------------------ */

size_t binn_serialized_size(const struct binn *binn) {
  if (!binn) {
    return 0;
  }
  return value_serialized_size(binn);
}

bool binn_serialize(const struct binn *binn, bool (*consumer)(const uint8_t *buf, size_t len, void *op), void *op) {
  struct stream_sink sc;
  struct binn_sink sink;
  if (!binn || !consumer) {
    errno = EINVAL;
    return false;
  }
  sc.consumer = consumer;
  sc.op = op;
  sink.write = stream_sink_write;
  sink.ctx = &sc;
  if (!emit_value(&sink, binn, 0)) {
    return false;
  }
  // Signal end of stream; its result is the overall result.
  return consumer(0, 0, op);
}

struct binn_val binn_serialize_allocated(const struct binn *binn) {
  struct mem_sink mc;
  struct binn_sink sink;
  size_t total;
  uint8_t *buf;

  if (!binn) {
    errno = EINVAL;
    return binn_val_make(0, 0);
  }
  total = value_serialized_size(binn);
  buf = (uint8_t*) malloc(total ? total : 1);
  if (!buf) {
    errno = ENOMEM;
    return binn_val_make(0, 0);
  }
  mc.dst = buf;
  mc.cap = total;
  mc.pos = 0;
  sink.write = mem_sink_write;
  sink.ctx = &mc;
  if (!emit_value(&sink, binn, 0) || mc.pos != total) {
    free(buf);
    errno = EINVAL;
    return binn_val_make(0, 0);
  }
  return binn_val_make(buf, total);
}

struct binn_val binn_serialize_into(const struct binn *binn, uint8_t *buffer, size_t buflen) {
  struct mem_sink mc;
  struct binn_sink sink;
  size_t total;

  if (!binn || !buffer) {
    errno = EINVAL;
    return binn_val_make(0, 0);
  }
  total = value_serialized_size(binn);
  if (buflen < total) {
    errno = ENOSPC;
    return binn_val_make(0, 0);
  }
  mc.dst = buffer;
  mc.cap = buflen;
  mc.pos = 0;
  sink.write = mem_sink_write;
  sink.ctx = &mc;
  if (!emit_value(&sink, binn, 0) || mc.pos != total) {
    errno = EINVAL;
    return binn_val_make(0, 0);
  }
  return binn_val_make(buffer, total);
}

/* ------------------------------------------------------------------ */
/* Public API: containers                                             */
/* ------------------------------------------------------------------ */

bool binn_add_keyed(struct binn *binn, const char *key, struct binn_val value) {
  uint8_t prefix[BINN_KEY_MAXLEN + 1];
  size_t keylen;

  if (!binn || !key || !value.buf) {
    errno = EINVAL;
    return false;
  }
  if (!type_is_container(binn->type) || type_subtype(binn->type) != BINN_SUBTYPE_OBJECT) {
    errno = EINVAL;
    return false;
  }
  keylen = strlen(key);
  if (keylen > BINN_KEY_MAXLEN) {
    errno = EINVAL;
    return false;
  }
  prefix[0] = (uint8_t) keylen;
  if (keylen) {
    memcpy(prefix + 1, key, keylen);
  }
  return container_add_value(binn, prefix, keylen + 1, value);
}

bool binn_add_mapped(struct binn *binn, int32_t idx, struct binn_val value) {
  uint8_t prefix[4];

  if (!binn || !value.buf) {
    errno = EINVAL;
    return false;
  }
  if (!type_is_container(binn->type) || type_subtype(binn->type) != BINN_SUBTYPE_MAP) {
    errno = EINVAL;
    return false;
  }
  put_be32(prefix, (uint32_t) idx);
  return container_add_value(binn, prefix, 4, value);
}

bool binn_add(struct binn *binn, struct binn_val value, int32_t *out_idx) {
  int32_t idx;
  if (!binn || !value.buf) {
    errno = EINVAL;
    return false;
  }
  if (!type_is_container(binn->type) || type_subtype(binn->type) != BINN_SUBTYPE_LIST) {
    errno = EINVAL;
    return false;
  }
  idx = (int32_t) binn->count;
  if (!container_add_value(binn, 0, 0, value)) {
    return false;
  }
  if (out_idx) {
    *out_idx = idx;
  }
  return true;
}

/* ------------------------------------------------------------------ */
/* Public API: lookups                                                */
/* ------------------------------------------------------------------ */

bool binn_keyed_get(const struct binn *binn, const char *key, struct binn *result) {
  const uint8_t *cp;
  size_t clen;
  uint32_t count;
  size_t keylen;
  const uint8_t *found;
  struct value_meta m;

  if (!binn || !key || !result || result == binn) {
    errno = EINVAL;
    return false;
  }
  if (!type_is_container(binn->type) || type_subtype(binn->type) != BINN_SUBTYPE_OBJECT) {
    errno = EINVAL;
    return false;
  }
  cp = container_children(binn, &clen, &count);
  if (!cp && clen) {
    errno = EINVAL;
    return false;
  }
  keylen = strlen(key);
  found = object_find(cp, cp ? cp + clen : 0, key, keylen);
  if (!found) {
    return false;
  }
  if (!parse_value(found, cp + clen, &m)) {
    errno = EINVAL;
    return false;
  }
  fill_wrapper(result, found, &m);
  return true;
}

bool binn_indexed_get(const struct binn *binn, uint32_t nkey, struct binn *result) {
  const uint8_t *cp;
  const uint8_t *cend;
  size_t clen;
  uint32_t count;
  int subtype;
  struct value_meta m;

  if (!binn || !result || result == binn) {
    errno = EINVAL;
    return false;
  }
  if (!type_is_container(binn->type)) {
    errno = EINVAL;
    return false;
  }
  subtype = type_subtype(binn->type);
  cp = container_children(binn, &clen, &count);
  if (!cp && clen) {
    errno = EINVAL;
    return false;
  }
  cend = cp ? cp + clen : 0;
  if (subtype == 0) { // list: zero based index
    uint32_t i = 0;
    while (cp < cend) {
      if (!parse_value(cp, cend, &m)) {
        errno = EINVAL;
        return false;
      }
      if (i == nkey) {
        fill_wrapper(result, cp, &m);
        return true;
      }
      cp += m.total;
      i++;
    }
    return false;
  }
  if (subtype == BINN_SUBTYPE_MAP) { // map: numeric id
    const uint8_t *found = map_find(cp, cend, (int32_t) nkey);
    if (!found) {
      return false;
    }
    if (!parse_value(found, cend, &m)) {
      errno = EINVAL;
      return false;
    }
    fill_wrapper(result, found, &m);
    return true;
  }
  errno = EINVAL;
  return false;
}

/* ------------------------------------------------------------------ */
/* Public API: iteration                                              */
/* ------------------------------------------------------------------ */

bool binn_iter_init(struct binn_val binn, struct binn_iter *iter) {
  const uint8_t *children;
  const uint8_t *cend;
  size_t clen;
  uint32_t count;
  binn_type_t ctype;

  if (!binn.buf || !iter) {
    errno = EINVAL;
    return false;
  }
  memset(iter, 0, sizeof(*iter));
  iter->idx = -1;

  if (binn_is_struct_instance(binn)) {
    const struct binn *b = (const struct binn*) binn.buf;
    if (!type_is_container(b->type)) {
      errno = EINVAL;
      return false;
    }
    ctype = b->type;
    children = container_children(b, &clen, &count);
    if (!children && clen) {
      errno = EINVAL;
      return false;
    }
  } else {
    const uint8_t *buf = (const uint8_t*) binn.buf;
    const uint8_t *end = buf + binn.size;
    struct value_meta m;
    if (!binn.size || !parse_value(buf, end, &m) || m.storage != BINN_ST_CONTAINER) {
      errno = EINVAL;
      return false;
    }
    ctype = m.type;
    children = m.payload;
    clen = m.payload_len;
    count = m.count;
  }
  cend = clen ? children + clen : children;
  iter->state.next = children;
  iter->state.end = cend;
  iter->state.remaining = count;
  iter->state.ctype = ctype;
  iter->exists = false;
  iter->key = 0;
  return true;
}

bool binn_iter_next(struct binn_iter *iter) {
  const uint8_t *p;
  const uint8_t *end;
  const uint8_t *child;
  struct value_meta m;
  int subtype;

  if (!iter || !iter->state.next) {
    return false;
  }
  iter->exists = false;
  iter->key = 0;
  iter->idx = -1;
  if (iter->state.remaining == 0) {
    return false;
  }
  p = iter->state.next;
  end = iter->state.end;
  subtype = type_subtype(iter->state.ctype);

  if (subtype == BINN_SUBTYPE_MAP) { // map
    if ((size_t) (end - p) < 4) {
      return false;
    }
    iter->idx = (int32_t) get_be32(p);
    p += 4;
  } else if (subtype == BINN_SUBTYPE_OBJECT) { // object
    uint8_t klen;
    if (p >= end) {
      return false;
    }
    klen = *p++;
    if ((size_t) (end - p) < klen || klen > BINN_KEY_MAXLEN) {
      return false;
    }
    memcpy(iter->keybuf, p, klen);
    iter->keybuf[klen] = 0;
    iter->key = iter->keybuf;
    p += klen;
  }

  child = p;
  if (!parse_value(p, end, &m) || m.total > (size_t) (end - child)) {
    return false;
  }
  fill_wrapper(&iter->binn, child, &m);
  iter->state.next = child + m.total;
  iter->state.remaining--;
  iter->exists = true;
  return true;
}

void binn_iter_close(struct binn_iter *iter) {
  if (!iter) {
    return;
  }
  iter->state.next = 0;
  iter->state.end = 0;
  iter->state.remaining = 0;
  iter->exists = false;
  iter->key = 0;
}

/* ------------------------------------------------------------------ */
/* Public API: traversal                                              */
/* ------------------------------------------------------------------ */

static bool visit_children(void *container, int (*visitor)(struct binn_iter *it, void *op), void *op, int depth) { // NOLINT(misc-no-recursion)
  struct binn_iter it;
  bool ok = true;

  if (depth > BINN_MAX_NESTING_LEVELS) {
    return false;
  }
  if (!binn_iter_init(binn_val_of_struct(container), &it)) {
    return true;
  }
  while (binn_iter_next(&it)) {
    int rc = visitor(&it, op);
    if (rc == -1) {
      ok = false;
      break;
    }
    if (type_is_container(it.binn.type)) {
      if (!visit_children(&it.binn, visitor, op, depth + 1)) {
        ok = false;
        break;
      }
    }
  }
  binn_iter_close(&it);
  return ok;
}

void binn_visit(const struct binn *binn, int (*visitor)(struct binn_iter *it, void *op), void *op) {
  if (!binn || !visitor) {
    return;
  }
  visit_children((void*) (uintptr_t) binn, visitor, op, 0);
}

/* ------------------------------------------------------------------ */
/* Public API: scalar accessors                                       */
/* ------------------------------------------------------------------ */

/// `true` for a single precision floating point scalar (`BINN_TYPE_FLOAT`).
static bool type_is_f32(binn_type_t type) {
  return type_storage(type) == BINN_ST_DWORD && type_subtype(type) == 2;
}

/// `true` for a double precision floating point scalar (`BINN_TYPE_DOUBLE`).
static bool type_is_f64(binn_type_t type) {
  return type_storage(type) == BINN_ST_QWORD && type_subtype(type) == 2;
}

/// `true` for an integer scalar (any numeric storage that is not a float/double).
static bool type_is_integer_scalar(binn_type_t type) {
  int storage = type_storage(type);
  return storage >= BINN_ST_BYTE && storage <= BINN_ST_QWORD && !type_is_f32(type) && !type_is_f64(type);
}

/// Reads any numeric scalar as a host order `double`.
static double scalar_as_double(const struct binn *b) {
  if (type_is_f32(b->type)) {
    float f;
    uint32_t bits = (uint32_t) b->val.nval;
    memcpy(&f, &bits, sizeof(f));
    return (double) f;
  }
  if (type_is_f64(b->type)) {
    double d;
    memcpy(&d, &b->val.nval, sizeof(d));
    return d;
  }
  if (type_is_signed_int(b->type)) {
    return (double) (int64_t) b->val.nval;
  }
  return (double) b->val.nval;
}

struct binn_val binn_blob_get(const struct binn *binn) {
  int storage;
  if (!binn) {
    return binn_val_make(0, 0);
  }
  storage = type_storage(binn->type);
  if (storage != BINN_ST_STRING && storage != BINN_ST_BLOB) {
    return binn_val_make(0, 0);
  }
  return binn_val_make(binn->val.bval, binn->used);
}

struct binn_val binn_text_get(const struct binn *binn) {
  if (!binn || type_storage(binn->type) != BINN_ST_STRING) {
    return binn_val_make(0, 0);
  }
  return binn_val_make(binn->val.bval, binn->used);
}

uint64_t binn_u64_get(const struct binn *binn) {
  if (!binn) {
    return 0;
  }
  if (type_is_integer_scalar(binn->type)) {
    return binn->val.nval;
  }
  if (type_is_f32(binn->type) || type_is_f64(binn->type)) {
    double d = scalar_as_double(binn);
    if (!(d >= 0.0) || d >= 18446744073709551616.0) { // negative, NaN or >= 2^64
      return 0;
    }
    return (uint64_t) d;
  }
  return 0;
}

int64_t binn_i64_get(const struct binn *binn) {
  if (!binn) {
    return 0;
  }
  if (type_is_integer_scalar(binn->type)) {
    return (int64_t) binn->val.nval;
  }
  if (type_is_f32(binn->type) || type_is_f64(binn->type)) {
    double d = scalar_as_double(binn);
    if (!(d >= -9223372036854775808.0) || d >= 9223372036854775808.0) { // NaN or outside int64
      return 0;
    }
    return (int64_t) d;
  }
  return 0;
}

double binn_f64_get(const struct binn *binn) {
  if (!binn) {
    return 0.0;
  }
  if (type_is_integer_scalar(binn->type) || type_is_f32(binn->type) || type_is_f64(binn->type)) {
    return scalar_as_double(binn);
  }
  return 0.0;
}

bool binn_bool_get(const struct binn *binn) {
  if (!binn) {
    return false;
  }
  return binn->type == BINN_TYPE_TRUE;
}

bool binn_is_int_number(const struct binn *binn) {
  return binn && type_is_integer_scalar(binn->type);
}

bool binn_is_fp_number(const struct binn *binn) {
  return binn && (type_is_f32(binn->type) || type_is_f64(binn->type));
}

bool binn_is_text(const struct binn *binn) {
  return binn && type_storage(binn->type) == BINN_ST_STRING;
}

bool binn_is_blob(const struct binn *binn) {
  return binn && type_storage(binn->type) == BINN_ST_BLOB;
}

bool binn_is_boolean(const struct binn *binn) {
  return binn && (binn->type == BINN_TYPE_TRUE || binn->type == BINN_TYPE_FALSE);
}

bool binn_is_container(const struct binn *binn) {
  return binn && type_is_container(binn->type);
}

bool binn_is_object(const struct binn *binn) {
  return binn && binn->type == BINN_TYPE_OBJECT;
}

bool binn_is_list(const struct binn *binn) {
  return binn && binn->type == BINN_TYPE_LIST;
}

bool binn_is_map(const struct binn *binn) {
  return binn && binn->type == BINN_TYPE_MAP;
}

bool binn_is_null(const struct binn *binn) {
  return binn && binn->type == BINN_TYPE_NULL;
}

bool binn_is_decimal(const struct binn *binn) {
  return binn && binn->type == BINN_TYPE_DECIMAL;
}

bool binn_is_datetime(const struct binn *binn) {
  return binn && binn->type == BINN_TYPE_DATETIME;
}

bool binn_is_date(const struct binn *binn) {
  return binn && binn->type == BINN_TYPE_DATE;
}

bool binn_is_time(const struct binn *binn) {
  return binn && binn->type == BINN_TYPE_TIME;
}
