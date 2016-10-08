/*
 * tools/libacpi/aml_build.c
 *
 * Copyright (c) 2016, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Haozhong Zhang <haozhong.zhang@intel.com>
 */

#include LIBACPI_STDUTILS
#include "libacpi.h"
#include "aml_build.h"

#define AML_OP_SCOPE     0x10
#define AML_OP_EXT       0x5B
#define AML_OP_DEVICE    0x82

#define ACPI_NAMESEG_LEN 4

struct aml_build_alloctor {
    struct acpi_ctxt *ctxt;
    uint8_t *buf;
    uint32_t capacity;
    uint32_t used;
};
static struct aml_build_alloctor alloc;

enum { ALLOC_OVERFLOW, ALLOC_NOT_NEEDED, ALLOC_NEEDED };

static int alloc_needed(uint32_t size)
{
    uint32_t len = alloc.used + size;

    if ( len < alloc.used )
        return ALLOC_OVERFLOW;
    else if ( len <= alloc.capacity )
        return ALLOC_NOT_NEEDED;
    else
        return ALLOC_NEEDED;
}

static uint8_t *aml_buf_alloc(uint32_t size)
{
    int needed = alloc_needed(size);
    uint8_t *buf = NULL;
    struct acpi_ctxt *ctxt = alloc.ctxt;
    uint32_t alloc_size, alloc_align = ctxt->min_alloc_align;

    switch ( needed )
    {
    case ALLOC_OVERFLOW:
        break;

    case ALLOC_NEEDED:
        alloc_size = (size + alloc_align) & ~(alloc_align - 1);
        buf = ctxt->mem_ops.alloc(ctxt, alloc_size, alloc_align);
        if ( !buf )
            break;
        if ( alloc.buf + alloc.capacity != buf )
        {
            buf = NULL;
            break;
        }
        alloc.capacity += alloc_size;
        alloc.used += size;
        break;

    case ALLOC_NOT_NEEDED:
        buf = alloc.buf + alloc.used;
        alloc.used += size;
        break;

    default:
        break;
    }

    return buf;
}

static uint32_t get_package_length(uint8_t *pkg)
{
    uint32_t len;

    len = pkg - alloc.buf;
    len = alloc.used - len;

    return len;
}

static void build_prepend_byte(uint8_t *buf, uint8_t byte)
{
    uint32_t len;

    len = buf - alloc.buf;
    len = alloc.used - len;

    aml_buf_alloc(sizeof(uint8_t));
    if ( len )
        memmove(buf + 1, buf, len);
    buf[0] = byte;
}

/*
 * XXX: names of multiple segments (e.g. X.Y.Z) are not supported
 */
static void build_prepend_name(uint8_t *buf, const char *name)
{
    uint8_t *p = buf;
    const char *s = name;
    uint32_t len, name_len;

    while ( *s == '\\' || *s == '^' )
    {
        build_prepend_byte(p, (uint8_t) *s);
        ++p;
        ++s;
    }

    if ( !*s )
    {
        build_prepend_byte(p, 0x00);
        return;
    }

    len = p - alloc.buf;
    len = alloc.used - len;
    name_len = strlen(s);
    ASSERT(strlen(s) <= ACPI_NAMESEG_LEN);

    aml_buf_alloc(ACPI_NAMESEG_LEN);
    if ( len )
        memmove(p + ACPI_NAMESEG_LEN, p, len);
    memcpy(p, s, name_len);
    memcpy(p + name_len, "____", ACPI_NAMESEG_LEN - name_len);
}

enum {
    PACKAGE_LENGTH_1BYTE_SHIFT = 6, /* Up to 63 - use extra 2 bits. */
    PACKAGE_LENGTH_2BYTE_SHIFT = 4,
    PACKAGE_LENGTH_3BYTE_SHIFT = 12,
    PACKAGE_LENGTH_4BYTE_SHIFT = 20,
};

static void build_prepend_package_length(uint8_t *pkg, uint32_t length)
{
    uint8_t byte;
    unsigned length_bytes;

    if ( length + 1 < (1 << PACKAGE_LENGTH_1BYTE_SHIFT) )
        length_bytes = 1;
    else if ( length + 2 < (1 << PACKAGE_LENGTH_3BYTE_SHIFT) )
        length_bytes = 2;
    else if ( length + 3 < (1 << PACKAGE_LENGTH_4BYTE_SHIFT) )
        length_bytes = 3;
    else
        length_bytes = 4;

    length += length_bytes;

    switch ( length_bytes )
    {
    case 1:
        byte = length;
        build_prepend_byte(pkg, byte);
        return;
    case 4:
        byte = length >> PACKAGE_LENGTH_4BYTE_SHIFT;
        build_prepend_byte(pkg, byte);
        length &= (1 << PACKAGE_LENGTH_4BYTE_SHIFT) - 1;
        /* fall through */
    case 3:
        byte = length >> PACKAGE_LENGTH_3BYTE_SHIFT;
        build_prepend_byte(pkg, byte);
        length &= (1 << PACKAGE_LENGTH_3BYTE_SHIFT) - 1;
        /* fall through */
    case 2:
        byte = length >> PACKAGE_LENGTH_2BYTE_SHIFT;
        build_prepend_byte(pkg, byte);
        length &= (1 << PACKAGE_LENGTH_2BYTE_SHIFT) - 1;
        /* fall through */
    }
    /*
     * Most significant two bits of byte zero indicate how many following bytes
     * are in PkgLength encoding.
     */
    byte = ((length_bytes - 1) << PACKAGE_LENGTH_1BYTE_SHIFT) | length;
    build_prepend_byte(pkg, byte);
}

static void build_prepend_package(uint8_t *buf, uint8_t op)
{
    uint32_t length = get_package_length(buf);
    build_prepend_package_length(buf, length);
    build_prepend_byte(buf, op);
}

static void build_prepend_ext_packge(uint8_t *buf, uint8_t op)
{
    build_prepend_package(buf, op);
    build_prepend_byte(buf, AML_OP_EXT);
}

void *aml_build_begin(struct acpi_ctxt *ctxt)
{
    alloc.ctxt = ctxt;
    alloc.buf = ctxt->mem_ops.alloc(ctxt,
                                    ctxt->min_alloc_unit, ctxt->min_alloc_align);
    alloc.capacity = ctxt->min_alloc_unit;
    alloc.used = 0;
    return alloc.buf;
}

uint32_t aml_build_end(void)
{
    return alloc.used;
}

void aml_prepend_blob(uint8_t *buf, const void *blob, uint32_t blob_length)
{
    uint32_t len;

    len = buf - alloc.buf;
    len = alloc.used - len;

    aml_buf_alloc(blob_length);
    if ( len )
        memmove(buf + blob_length, buf, len);

    memcpy(buf, blob, blob_length);
}

void aml_prepend_device(uint8_t *buf, const char *name)
{
    build_prepend_name(buf, name);
    build_prepend_ext_packge(buf, AML_OP_DEVICE);
}

void aml_prepend_scope(uint8_t *buf, const char *name)
{
    build_prepend_name(buf, name);
    build_prepend_package(buf, AML_OP_SCOPE);
}
