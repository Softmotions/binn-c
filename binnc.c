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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

static uint32_t get_be32(const uint8_t *p) {
  return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | (uint32_t) p[3];
}

static void put_be32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t) (v >> 24);
  p[1] = (uint8_t) (v >> 16);
  p[2] = (uint8_t) (v >> 8);
  p[3] = (uint8_t) v;
}

static uint64_t get_scalar_be(const uint8_t *p, size_t n) {
  uint64_t v = 0;
  for (size_t i = 0; i < n; i++) {
    v = (v << 8) | p[i];
  }
  return v;
}

static void put_scalar_be(uint8_t *p, uint64_t v, size_t n) {
  for (size_t i = 0; i < n; i++) {
    p[i] = (uint8_t) (v >> (8 * (n - 1 - i)));
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
static size_t storage_bytes(int storage) {
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
static size_t leaf_serialized_size(binn_type_t type, size_t payload_len) {
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

/// Recursively validates the children of a container.
/// The recursion is bounded by `BINN_MAX_NESTING_LEVELS`.
static bool validate_children(const uint8_t *cp, const uint8_t *cend, binn_type_t ctype, uint32_t count, int depth) { // NOLINT(misc-no-recursion)
  uint32_t cnt = 0;
  int subtype;
  if (depth > BINN_MAX_NESTING_LEVELS) {
    return false;
  }
  subtype = type_subtype(ctype);
  while (cp < cend) {
    struct value_meta cm;
    const uint8_t *np;
    if (subtype == BINN_SUBTYPE_MAP) { // map key: 4 bytes big endian
      if ((size_t) (cend - cp) < 4) {
        return false;
      }
      cp += 4;
    } else if (subtype == BINN_SUBTYPE_OBJECT) { // object key: 1 byte length + bytes
      uint8_t klen;
      if (cp >= cend) {
        return false;
      }
      klen = *cp++;
      if ((size_t) (cend - cp) < klen) {
        return false;
      }
      cp += klen;
    }
    np = parse_value(cp, cend, &cm);
    if (!np) {
      return false;
    }
    if (cm.storage == BINN_ST_CONTAINER) {
      if (!validate_children(cm.payload, cm.payload + cm.payload_len, cm.type, cm.count, depth + 1)) {
        return false;
      }
    }
    cp = np;
    cnt++;
    if (cnt > count) {
      return false;
    }
  }
  return (cp == cend) && (cnt == count);
}

/// Validates that `p` holds a structurally sound serialized binn value.
static bool validate_value(const uint8_t *p, const uint8_t *end, int depth) {
  struct value_meta m;
  if (!parse_value(p, end, &m)) {
    return false;
  }
  if (m.storage == BINN_ST_CONTAINER) {
    return validate_children(m.payload, m.payload + m.payload_len, m.type, m.count, depth);
  }
  return true;
}

/// Computes the container nesting depth of a serialized value.
/// A leaf has depth 0, a container has depth `1 + max(child depth)`.
/// Returns -1 if the value is malformed or deeper than `limit`.
/// The recursion is bounded by `limit`.
static int serialized_depth_limit(const uint8_t *p, const uint8_t *end, int limit) { // NOLINT(misc-no-recursion)
  struct value_meta m;
  int subtype;
  const uint8_t *cp;
  const uint8_t *cend;
  int maxd = 0;

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
    if (subtype == BINN_SUBTYPE_MAP) {
      if ((size_t) (cend - cp) < 4) {
        return -1;
      }
      cp += 4;
    } else if (subtype == BINN_SUBTYPE_OBJECT) {
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
      int d = serialized_depth_limit(cp, cend, limit - 1);
      if (d < 0) {
        return -1;
      }
      if (d > maxd) {
        maxd = d;
      }
    }
    cp = np;
  }
  if (maxd + 1 > limit) {
    return -1;
  }
  return maxd + 1;
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
  void  (*consumer)(struct binn_val val, void *op);
  void *op;
};

static bool stream_sink_write(void *ctx, const uint8_t *data, size_t len) {
  struct stream_sink *s = (struct stream_sink*) ctx;
  s->consumer(binn_val_make(data, len), s->op);
  return true;
}

/* ------------------------------------------------------------------ */
/* Sizes and emitting                                                 */
/* ------------------------------------------------------------------ */

/// Total serialized size of a construction mode container (header + children).
static size_t container_serialized_size(const struct binn *b) {
  size_t content = b->used;
  size_t count_sz = size_param_size(b->count);
  size_t total = 1 + 1 + count_sz + content;
  if (total > 127) {
    total = 1 + 4 + count_sz + content;
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
  memset(out, 0, sizeof(*out));
  out->magic = BINN_MAJIC_NUMBER;
  out->type = m->type;
  out->size = m->total;
  out->count = m->count;
  out->used = (uint32_t) m->payload_len;
  if (m->storage == BINN_ST_CONTAINER) {
    out->flags = BINN_FLG_WRAPPED_BINN;
    out->wrapped = binn_val_make(start, m->total);
  } else {
    out->flags = BINN_FLG_WRAPPED_PAYLOAD;
    if (m->storage == BINN_ST_STRING || m->storage == BINN_ST_BLOB) {
      out->wrapped = binn_val_make(m->payload, m->payload_len);
    } else {
      out->val.nval = load_scalar(m->payload, m->storage, type_is_signed_int(m->type));
    }
  }
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
    value_len = value_serialized_size(v);
  } else {
    const uint8_t *end;
    struct value_meta m;
    if (!value.size) {
      errno = EINVAL;
      return false;
    }
    end = (const uint8_t*) value.buf + value.size;
    if (!parse_value(value.buf, end, &m) || !validate_value(value.buf, end, 0)) {
      errno = EINVAL;
      return false;
    }
    raw = value.buf;
    value_len = m.total;
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
  if (value_len && serialized_depth_limit(dst, dst + value_len, BINN_MAX_NESTING_LEVELS - 1) < 0) {
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
      b->val.bval = (uint8_t*) malloc(len);
      if (!b->val.bval) {
        errno = ENOMEM;
        free_holder(b, holder);
        return 0;
      }
      memcpy(b->val.bval, sval.buf, len);
      b->allocated = (uint32_t) len;
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

struct binn* binn_from(struct binn *holder, struct binn_val value, bool *out_casted) {
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
  if (!parse_value(buf, end, &m) || !validate_value(buf, end, 0)) {
    errno = EINVAL;
    return 0;
  }
  b = alloc_holder(holder);
  if (!b) {
    return 0;
  }
  b->type = m.type;
  b->count = m.count;
  b->size = m.total;
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
  return b;
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
  dst->size = m.total;
  dst->used = (uint32_t) m.payload_len;
  if (m.storage == BINN_ST_CONTAINER || m.storage == BINN_ST_STRING || m.storage == BINN_ST_BLOB) {
    if (m.payload_len) {
      dst->val.bval = (uint8_t*) malloc(m.payload_len);
      if (!dst->val.bval) {
        errno = ENOMEM;
        free_holder(dst, holder);
        return 0;
      }
      memcpy(dst->val.bval, m.payload, m.payload_len);
      dst->allocated = (uint32_t) m.payload_len;
    }
  } else {
    dst->val.nval = load_scalar(m.payload, m.storage, type_is_signed_int(m.type));
  }
  return dst;
}

struct binn* binn_copy_from(struct binn *holder, struct binn_val value) {
  if (!value.buf || !value.size) {
    errno = EINVAL;
    return 0;
  }
  if (binn_is_struct_instance(value)) {
    const struct binn *v = (const struct binn*) value.buf;
    struct mem_sink mc;
    struct binn_sink sink;
    struct binn *r;
    size_t total = value_serialized_size(v);
    uint8_t *tmp;

    tmp = (uint8_t*) malloc(total ? total : 1);
    if (!tmp) {
      errno = ENOMEM;
      return 0;
    }
    mc.dst = tmp;
    mc.cap = total;
    mc.pos = 0;
    sink.write = mem_sink_write;
    sink.ctx = &mc;
    if (!emit_value(&sink, v, 0) || mc.pos != total) {
      free(tmp);
      errno = EINVAL;
      return 0;
    }
    r = copy_from_serialized(holder, tmp, total);
    free(tmp);
    return r;
  }
  if (!validate_value((const uint8_t*) value.buf, (const uint8_t*) value.buf + value.size, 0)) {
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
  if (!value.buf || value.size < sizeof(struct binn)) {
    return false;
  }
  return *(const uint32_t*) value.buf == BINN_MAJIC_NUMBER;
}

bool binn_is_serialized_buffer(struct binn_val value) {
  if (!value.buf || value.size == 0 || binn_is_struct_instance(value)) {
    return false;
  }
  return validate_value((const uint8_t*) value.buf, (const uint8_t*) value.buf + value.size, 0);
}

bool binn_is_any(struct binn_val value) {
  return binn_is_struct_instance(value) || binn_is_serialized_buffer(value);
}

/* ------------------------------------------------------------------ */
/* Public API: serialization                                          */
/* ------------------------------------------------------------------ */

bool binn_serialize(const struct binn *binn, void (*consumer)(struct binn_val val, void *op), void *op) {
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
  consumer(binn_val_make(0, 0), op);
  return true;
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

/// Iterator internal state. Stored in `binn_iter::next`.
struct binn_iter_ctx {
  const uint8_t *cur;
  const uint8_t *end;
  uint32_t       remaining;
  binn_type_t    ctype;
};

bool binn_iter_init(struct binn_val binn, struct binn_iter *iter) {
  const uint8_t *children;
  const uint8_t *cend;
  size_t clen;
  uint32_t count;
  binn_type_t ctype;
  struct binn_iter_ctx *ctx;

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
  ctx = (struct binn_iter_ctx*) malloc(sizeof(*ctx));
  if (!ctx) {
    errno = ENOMEM;
    return false;
  }
  ctx->cur = children;
  ctx->end = cend;
  ctx->remaining = count;
  ctx->ctype = ctype;
  iter->next = ctx;
  iter->exists = false;
  iter->key = 0;
  return true;
}

bool binn_iter_next(struct binn_iter *iter) {
  struct binn_iter_ctx *ctx;
  const uint8_t *p;
  const uint8_t *end;
  const uint8_t *child;
  struct value_meta m;
  int subtype;

  if (!iter || !iter->next) {
    return false;
  }
  ctx = (struct binn_iter_ctx*) iter->next;
  iter->exists = false;
  iter->key = 0;
  iter->idx = -1;
  if (ctx->remaining == 0) {
    return false;
  }
  p = ctx->cur;
  end = ctx->end;
  subtype = type_subtype(ctx->ctype);

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
  ctx->cur = child + m.total;
  ctx->remaining--;
  iter->exists = true;
  return true;
}

void binn_iter_close(struct binn_iter *iter) {
  if (!iter) {
    return;
  }
  free(iter->next);
  iter->next = 0;
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
