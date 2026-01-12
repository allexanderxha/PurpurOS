/**
 * @file fs/vhofs/accessors.c
 * @brief VHOFS filesystem accessor routines.
 *
 * This file contains accessor helper functions used to query and
 * manipulate internal VHOFS filesystem structures.
 *
 * @see https://github.com/torvalds/linux/blob/master/fs/btrfs/accessors.c
 *
 * @todo: Rewrite the code to be more optimal.
 *
 * Copyright (C) 2026 Allexander B.
 * All rights reserved.
 */

#include <linux/unaligned.h>
#include "messages.h"
#include "extent_io.h"
#include "fs.h"
#include "accessors.h"

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

static void __cold report_setget_bounds(const struct extend_buffer *eb,
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
    btrfs_warn(eb->fs_info,
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
 #define DEFINE_BTRFS_SETGET_BITS(bits)                                      \
 u##bits btrfs_get_##bits(const struct extent_buffer *eb,                    \
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
         report_setget_bounds(eb, ptr, off, sizeof(u##bits));                \
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
 #define DEFINE_BTRFS_SET_BITS(bits)                                         \
 void btrfs_set_##bits(const struct extent_buffer *eb,                       \
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
         report_setget_bounds(eb, ptr, off, sizeof(u##bits));                \
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

DEFINE_BTRFS_SETGET_BITS(8)
DEFINE_BTRFS_SETGET_BITS(16)
DEFINE_BTRFS_SETGET_BITS(32)
DEFINE_BTRFS_SETGET_BITS(64)

void btrfs_node_key(const struct extent_buffer *eb,
		    struct btrfs_disk_key *disk_key, int nr)
{
	unsigned long ptr = btrfs_node_key_ptr_offset(eb, nr);
	read_eb_member(eb, (struct btrfs_key_ptr *)ptr,
		       struct btrfs_key_ptr, key, disk_key);
}
