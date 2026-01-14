/**
 * @file fs/vhofs/accessors.h
 * @brief VHOFS filesystem accessor header file.
 *
 * This file declares accessor helper functions used to query and
 * manipulate internal VHOFS filesystem structures.
 *
 * @see https://github.com/torvalds/linux/blob/master/fs/btrfs/accessors.h
 *
 * @todo: Rewrite the code to be more optimal.
 * @todo: Rename core file system to VHOFS.
 *
 * Copyright (C) 2026 Allexander B.
 * All rights reserved.
 */

#ifndef BTRFS_ACCESSORS_H
#define BTRFS_ACCESSORS_H
 
#include <linux/unaligned.h>
#include <linux/stddef.h>
#include <linux/types.h>
#include <linux/align.h>
#include <linux/build_bug.h>
#include <linux/compiler.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <uapi/linux/btrfs_tree.h>
#include "fs.h"
#include "extent_io.h"

/*
 * Forward declaration of struct extend_buffer.
 * This allows pointers to this structure to be used without
 * including its full definition here.
 */
struct extend_buffer;

/*
 * Convert an 8-bit little-endian value to CPU endianness.
 * For 8-bit values, endianness does not matter, so this is a no-op.
 */
#define le8_to_cpu(v) (v)

/*
 * Convert an 8-bit CPU-endian value to little-endian.
 * For 8-bit values, this is also a no-op.
 */
#define cpu_to_le8(v) (v)

/*
 * Define a type representing an 8-bit little-endian value.
 * Since it is only one byte, it is equivalent to u8.
 */
#define __le8 u8

/*
 * Read an unaligned 8-bit little-endian value from memory.
 *
 * @p: Pointer to raw memory (possibly unaligned)
 *
 * This function simply reads one byte from the given address.
 * Alignment and endianness are irrelevant for 1-byte values,
 * but this helper exists for consistency with other accessors
 * such as get_unaligned_le16() and get_unaligned_le32().
 */
static inline u8 get_unaligned_le8(const void *p)
{
    return *(const u8 *)p;
}

/*
 * Write an unaligned 8-bit little-endian value to memory.
 *
 * @val: 8-bit value to be written
 * @p:   Pointer to raw memory (possibly unaligned)
 *
 * This function stores a single byte at the given memory address.
 * Because the value is only 8 bits wide, alignment and endianness
 * do not matter, and the value can be written directly.
 *
 * This helper exists mainly for symmetry and readability alongside
 * other accessors such as put_unaligned_le16() and put_unaligned_le32().
 */
static inline void put_unaligned_le8(u8 val, void *p)
{
    /*
     * Cast the void pointer to a u8 pointer and store the value.
     * This writes exactly one byte to memory.
     */
    *(u8 *)p = val;
}


#endif /* BTRFS_ACCESSORS_H */
