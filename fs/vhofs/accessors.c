/**
 * @file fs/vhofs/accessors.c
 * @brief VHOFS filesystem accessor routines.
 *
 * This file contains accessor helper functions used to query and
 * manipulate internal VHOFS filesystem structures.
 *
 * @see https://github.com/torvalds/linux/blob/master/fs/btrfs/accessors.c
 *
 * Copyright (C) 2026 Allexander B.
 * All rights reserved.
 */

#include <linux/types.h>
#include <linux/string.h>
#include "messages.h"
#include "extent_io.h"
#include "fs.h"
#include "accessors.h"

/* Type definitions */
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

/* Compiler hints */
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define __always_inline inline
#define __cold

/* Constants */
#define INLINE_EXTENT_BUFFER_PAGES 1

/* Byte order conversions (assuming little-endian) */
#define le16_to_cpu(x) (x)
#define le32_to_cpu(x) (x)
#define le64_to_cpu(x) (x)
#define cpu_to_le16(x) (x)
#define cpu_to_le32(x) (x)
#define cpu_to_le64(x) (x)

/* Unaligned access functions */
static inline u16 get_unaligned_le16(const void *p) {
    const u8 *b = p;
    return b[0] | (b[1] << 8);
}

static inline u32 get_unaligned_le32(const void *p) {
    const u8 *b = p;
    return b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24);
}

static inline u64 get_unaligned_le64(const void *p) {
    const u8 *b = p;
    return (u64)b[0] | ((u64)b[1] << 8) | ((u64)b[2] << 16) | ((u64)b[3] << 24) |
           ((u64)b[4] << 32) | ((u64)b[5] << 40) | ((u64)b[6] << 48) | ((u64)b[7] << 56);
}

static inline void put_unaligned_le16(u16 val, void *p) {
    u8 *b = p;
    b[0] = val & 0xff;
    b[1] = (val >> 8) & 0xff;
}

static inline void put_unaligned_le32(u32 val, void *p) {
    u8 *b = p;
    b[0] = val & 0xff;
    b[1] = (val >> 8) & 0xff;
    b[2] = (val >> 16) & 0xff;
    b[3] = (val >> 24) & 0xff;
}

static inline void put_unaligned_le64(u64 val, void *p) {
    u8 *b = p;
    b[0] = val & 0xff;
    b[1] = (val >> 8) & 0xff;
    b[2] = (val >> 16) & 0xff;
    b[3] = (val >> 24) & 0xff;
    b[4] = (val >> 32) & 0xff;
    b[5] = (val >> 40) & 0xff;
    b[6] = (val >> 48) & 0xff;
    b[7] = (val >> 56) & 0xff;
}

/**
 * @brief Report an out-of-bounds access in an extent buffer.
 *
 * This function is called when a get or set operation attempts to
 * access a member of an extent buffer outside its valid range.
 * It emits a detailed warning with buffer info and the offending access.
 *
 * @param eb    Pointer to the extent buffer.
 * @param ptr   Base pointer to the member being accessed.
 * @param off   Offset (in bytes) from ptr to the start of the member.
 * @param size  Size (in bytes) of the member being accessed.
 *
 * @note This function is marked __cold because it is only executed
 *       on error paths.
 *
 * @see https://github.com/torvalds/linux/blob/master/fs/btrfs/accessors.c
 *
 * @todo Include the member name or type in the warning message for easier debugging.
 * @todo Optionally capture the calling function or stack trace for context.
 * @todo Consider adding runtime checks in hot paths to prevent reaching this function.
 */

static void __cold vhofs_report_setget_bounds(const struct extent_buffer *eb,
                                        const void *ptr, unsigned off, int size)
{
    /* Compute absolute start and end of the member in the buffer */
    unsigned long member_start = (unsigned long)ptr + off;
    unsigned long member_end   = member_start + size;

    /* Compute buffer boundaries */
    unsigned long buf_start = (unsigned long)eb->start;
    unsigned long buf_end   = buf_start + eb->len;

    const char *bound_hit;

    /* Determine which boundary is violated */
    if (member_start < buf_start)
        bound_hit = "start";
    else if (member_end > buf_end)
        bound_hit = "end";
    else
        bound_hit = "unknown"; /* should not happen if called correctly */

    /* TODO: Enhance the warning message by including member type or function context */
    /* Emit a warning with full context */
    vhofs_warn(eb->fs_info,
                "Out-of-bounds extent buffer access: %s bound violated.\n"
                "  member start: 0x%lx, member end: 0x%lx, buffer start: 0x%lx, buffer end: 0x%lx, size: %d",
                bound_hit, member_start, member_end, buf_start, buf_end, size);
}

/**
 * @brief Copy data from two source buffers into a single destination buffer.
 *
 * Copies @p len1 bytes from @p src1 into @p dest, followed by
 * (total - len1) bytes from @p src2.
 *
 * @param dest   Destination buffer (must be at least @p total bytes).
 * @param src1   First source buffer.
 * @param src2   Second source buffer.
 * @param len1   Number of bytes to copy from @p src1.
 * @param total  Total number of bytes to copy.
 *
 * @note Caller must ensure:
 *       - dest is valid for @p total bytes
 *       - src1 is valid for @p len1 bytes
 *       - src2 is valid for (total - len1) bytes
 */
 static __always_inline void
 memcpy_split_src(void *dest, const void *src1, const void *src2,
                  size_t len1, size_t total)
 {
     size_t len2 = total - len1;
 
     /* First segment */
     memcpy(dest, src1, len1);
 
     /* Second segment */
     memcpy((char *)dest + len1, src2, len2);
 }
 
 /*
  * Define get helpers for reading little-endian values of a given bit size
  * from an extent buffer. Handles accesses spanning multiple folios.
  */
  #define DEFINE_VHOFS_SETGET_BITS(bits)                                      \
  u##bits vhofs_get_##bits(const struct extent_buffer *eb,                    \
                           const void *ptr, unsigned long off)               \
  {                                                                           \
      unsigned long member_offset;                                            \
      unsigned long idx;                                                      \
      unsigned long oif;                                                      \
      unsigned long part;                                                     \
      char *kaddr;                                                           \
      u8 lebytes[sizeof(u##bits)];                                            \
                                                                              \
      member_offset = (unsigned long)ptr + off;                               \
                                                                              \
      /* Bounds check */                                                      \
      if (unlikely(member_offset + sizeof(u##bits) > eb->len)) {              \
          vhofs_report_setget_bounds(eb, ptr, off, sizeof(u##bits));          \
          return 0;                                                           \
      }                                                                       \
                                                                              \
      idx  = get_eb_folio_index(eb, member_offset);                           \
      oif  = get_eb_offset_in_folio(eb, member_offset);                       \
      kaddr = folio_address(eb->folios[idx]) + oif;                           \
      part = eb->folio_size - oif;                                            \
                                                                              \
      /* Fast path: data fully contained in one folio */                      \
      if (INLINE_EXTENT_BUFFER_PAGES == 1 || sizeof(u##bits) == 1 ||           \
          likely(sizeof(u##bits) <= part))                                    \
          return get_unaligned_le##bits(kaddr);                               \
                                                                              \
      /* Slow path: value spans two folios */                                 \
      if (sizeof(u##bits) == 2) {                                             \
          lebytes[0] = kaddr[0];                                              \
          lebytes[1] = folio_address(eb->folios[idx + 1])[0];                 \
      } else {                                                                \
          memcpy_split_src(lebytes, kaddr,                                    \
                            folio_address(eb->folios[idx + 1]),               \
                            part, sizeof(u##bits));                           \
      }                                                                       \
                                                                              \
      return get_unaligned_le##bits(lebytes);                                 \
  }

/*
 * Define set helpers for writing little-endian values of a given bit size
 * into an extent buffer. Handles writes spanning multiple folios.
 */
  #define DEFINE_VHOFS_SET_BITS(bits)                                         \
  void vhofs_set_##bits(const struct extent_buffer *eb,                       \
                        void *ptr, unsigned long off, u##bits val)            \
  {                                                                           \
      unsigned long member_offset;                                            \
      unsigned long idx;                                                      \
      unsigned long oif;                                                      \
      unsigned long part;                                                     \
      char *kaddr;                                                           \
      u8 lebytes[sizeof(u##bits)];                                            \
                                                                              \
      member_offset = (unsigned long)ptr + off;                               \
                                                                              \
      /* Bounds check */                                                      \
      if (unlikely(member_offset + sizeof(u##bits) > eb->len)) {              \
          vhofs_report_setget_bounds(eb, ptr, off, sizeof(u##bits));          \
          return;                                                             \
      }                                                                       \
                                                                              \
      idx  = get_eb_folio_index(eb, member_offset);                           \
      oif  = get_eb_offset_in_folio(eb, member_offset);                       \
      kaddr = folio_address(eb->folios[idx]) + oif;                           \
      part = eb->folio_size - oif;                                            \
                                                                              \
      /* Fast path: value fits in a single folio */                           \
      if (INLINE_EXTENT_BUFFER_PAGES == 1 || sizeof(u##bits) == 1 ||           \
          likely(sizeof(u##bits) <= part)) {                                  \
          put_unaligned_le##bits(val, kaddr);                                 \
          return;                                                             \
      }                                                                       \
                                                                              \
      /* Slow path: value spans two folios */                                 \
      put_unaligned_le##bits(val, lebytes);                                   \
                                                                              \
      if (sizeof(u##bits) == 2) {                                             \
          kaddr[0] = lebytes[0];                                              \
          folio_address(eb->folios[idx + 1])[0] = lebytes[1];                 \
      } else {                                                                \
          memcpy(kaddr, lebytes, part);                                       \
          memcpy(folio_address(eb->folios[idx + 1]),                          \
                 lebytes + part, sizeof(u##bits) - part);                     \
      }                                                                       \
  }

DEFINE_VHOFS_SETGET_BITS(8)
DEFINE_VHOFS_SETGET_BITS(16)
DEFINE_VHOFS_SETGET_BITS(32)
DEFINE_VHOFS_SETGET_BITS(64)

DEFINE_VHOFS_SET_BITS(8)
DEFINE_VHOFS_SET_BITS(16)
DEFINE_VHOFS_SET_BITS(32)
DEFINE_VHOFS_SET_BITS(64)

void vhofs_node_key(const struct extent_buffer *eb,
		    struct vhofs_disk_key *disk_key, int nr)
{
	unsigned long ptr = vhofs_node_key_ptr_offset(eb, nr);
	vhofs_read_eb_member(eb, (struct vhofs_key_ptr *)ptr,
		       struct vhofs_key_ptr, key, disk_key);
}
