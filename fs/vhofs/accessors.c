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
