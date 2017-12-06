/*
 * libacpi/qemu_loader.c
 *
 * Driver of QEMU BIOSLinkerLoader interface. The reference document
 * can be found at
 * https://github.com/qemu/qemu/blob/master/hw/acpi/bios-linker-loader.c.
 *
 * Copyright (C) 2017,  Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License, version 2.1, as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; If not, see <http://www.gnu.org/licenses/>.
 */

#include LIBACPI_STDUTILS
#include "acpi2_0.h"
#include "libacpi.h"
#include "qemu.h"

/* QEMU BIOSLinkerLoader interface. All fields in little-endian. */
struct loader_entry {
    uint32_t command;
    union {
        /*
         * COMMAND_ALLOCATE - allocate a table from @alloc.file
         * subject to @alloc.align alignment (must be power of 2)
         * and @alloc.zone (can be HIGH or FSEG) requirements.
         *
         * Must appear exactly once for each file, and before
         * this file is referenced by any other command.
         */
        struct {
            char file[FW_CFG_FILE_PATH_MAX_LENGTH];
            uint32_t align;
            uint8_t zone;
        } alloc;

        /*
         * COMMAND_ADD_POINTER - patch the table (originating from
         * @dest_file) at @pointer.offset, by adding a pointer to the table
         * originating from @src_file. 1,2,4 or 8 byte unsigned
         * addition is used depending on @pointer.size.
         */
        struct {
            char dest_file[FW_CFG_FILE_PATH_MAX_LENGTH];
            char src_file[FW_CFG_FILE_PATH_MAX_LENGTH];
            uint32_t offset;
            uint8_t size;
        } pointer;

        /*
         * COMMAND_ADD_CHECKSUM - calculate checksum of the range specified by
         * @cksum_start and @cksum_length fields,
         * and then add the value at @cksum.offset.
         * Checksum simply sums -X for each byte X in the range
         * using 8-bit math.
         */
        struct {
            char file[FW_CFG_FILE_PATH_MAX_LENGTH];
            uint32_t offset;
            uint32_t start;
            uint32_t length;
        } cksum;

        /* padding */
        char pad[124];
    };
} __attribute__ ((packed));

enum {
    BIOS_LINKER_LOADER_COMMAND_ALLOCATE         = 0x1,
    BIOS_LINKER_LOADER_COMMAND_ADD_POINTER      = 0x2,
    BIOS_LINKER_LOADER_COMMAND_ADD_CHECKSUM     = 0x3,
};

enum {
    BIOS_LINKER_LOADER_ALLOC_ZONE_HIGH = 0x1,
    BIOS_LINKER_LOADER_ALLOC_ZONE_FSEG = 0x2,
};

struct rom {
    struct fw_cfg_file file;
    struct rom *next;
    void *data;
};

static struct rom *roms = NULL;
static struct rom *bios_loader = NULL;

static bool rom_needed(const char *file_name)
{
    return
        !strncmp(file_name, "etc/acpi/rsdp", FW_CFG_FILE_PATH_MAX_LENGTH) ||
        !strncmp(file_name, "etc/acpi/tables", FW_CFG_FILE_PATH_MAX_LENGTH) ||
        !strncmp(file_name, "etc/table-loader", FW_CFG_FILE_PATH_MAX_LENGTH) ||
        !strncmp(file_name, "etc/acpi/nvdimm-mem", FW_CFG_FILE_PATH_MAX_LENGTH);
}

static int loader_load(struct acpi_ctxt *ctxt, struct rom *loader)
{
    struct fw_cfg_file *file = &loader->file;
    uint32_t size = be32_to_cpu(file->size);

    loader->data = ctxt->mem_ops.alloc(ctxt, size, 0);
    if ( !loader->data )
        return -ENOMEM;

    fw_cfg_read_file(file, loader->data);

    return 0;
}

static struct rom *loader_find_rom(const char *file_name)
{
    struct rom *rom = roms;

    while ( rom )
    {
        if ( !strncmp(rom->file.name, file_name, FW_CFG_FILE_PATH_MAX_LENGTH) )
            break;
        rom = rom->next;
    }

    if ( !rom )
        printf("ERROR: File %s not exist\n", file_name);

    return rom;
}

static void loader_cmd_display(struct loader_entry *entry)
{
    switch ( entry->command )
    {
    case BIOS_LINKER_LOADER_COMMAND_ALLOCATE:
        entry->alloc.file[FW_CFG_FILE_PATH_MAX_LENGTH - 1] = '\0';
        printf("COMMAND_ALLOCATE: file %s, align %u, zone %u\n",
               entry->alloc.file, entry->alloc.align, entry->alloc.zone);
        break;

    case BIOS_LINKER_LOADER_COMMAND_ADD_POINTER:
        entry->pointer.dest_file[FW_CFG_FILE_PATH_MAX_LENGTH - 1] = '\0';
        entry->pointer.src_file[FW_CFG_FILE_PATH_MAX_LENGTH - 1] = '\0';
        printf("COMMAND_ADD_POINTER: dst %s, src %s, offset %u, size %u\n",
               entry->pointer.dest_file, entry->pointer.src_file,
               entry->pointer.offset, entry->pointer.size);
        break;

    case BIOS_LINKER_LOADER_COMMAND_ADD_CHECKSUM:
        entry->cksum.file[FW_CFG_FILE_PATH_MAX_LENGTH - 1] = '\0';
        printf("COMMAND_ADD_CHECKSUM: file %s, offset %u, offset %u, len %u\n",
               entry->cksum.file, entry->cksum.offset,
               entry->cksum.start, entry->cksum.length);
        break;

    default:
        printf("Unsupported command %u\n", entry->command);
    }
}

static int loader_exec_allocate(struct acpi_ctxt *ctxt,
                                const struct loader_entry *entry)
{
    uint32_t align = entry->alloc.align;
    uint8_t zone = entry->alloc.zone;
    struct rom *rom;
    struct fw_cfg_file *file;

    rom = loader_find_rom(entry->alloc.file);
    if ( !rom )
        return -ENOENT;
    file = &rom->file;

    if ( align & (align - 1) )
    {
        printf("ERROR: Invalid alignment %u, not power of 2\n", align);
        return -EINVAL;
    }

    if ( zone != BIOS_LINKER_LOADER_ALLOC_ZONE_HIGH &&
         zone != BIOS_LINKER_LOADER_ALLOC_ZONE_FSEG )
    {
        printf("ERROR: Unsupported zone type %u\n", zone);
        return -EINVAL;
    }

    rom->data = ctxt->mem_ops.alloc(ctxt, be32_to_cpu(file->size), align);
    fw_cfg_read_file(file, rom->data);

    return 0;
}

static int loader_exec_add_pointer(struct acpi_ctxt *ctxt,
                                   const struct loader_entry *entry)
{
    uint32_t offset = entry->pointer.offset;
    uint8_t size = entry->pointer.size;
    struct rom *dst, *src;
    uint64_t pointer, old_pointer;

    dst = loader_find_rom(entry->pointer.dest_file);
    src = loader_find_rom(entry->pointer.src_file);
    if ( !dst || !src )
        return -ENOENT;

    if ( !dst->data )
    {
        printf("ERROR: No space allocated for file %s\n",
               entry->pointer.dest_file);
        return -ENOSPC;
    }
    if ( !src->data )
    {
        printf("ERROR: No space allocated for file %s\n",
               entry->pointer.src_file);
        return -ENOSPC;
    }
    if ( offset + size < offset ||
         offset + size > be32_to_cpu(dst->file.size) )
    {
        printf("ERROR: Invalid size\n");
        return -EINVAL;
    }
    if ( size != 1 && size != 2 && size != 4 && size != 8 )
    {
        printf("ERROR: Invalid pointer size %u\n", size);
        return -EINVAL;
    }

    memcpy(&pointer, dst->data + offset, size);
    old_pointer = pointer;
    pointer += ctxt->mem_ops.v2p(ctxt, src->data);
    memcpy(dst->data + offset, &pointer, size);

    return 0;
}

static int loader_exec_add_checksum(const struct loader_entry *entry)
{
    uint32_t offset = entry->cksum.offset;
    uint32_t start = entry->cksum.start;
    uint32_t length = entry->cksum.length;
    uint32_t size;
    struct rom *rom;

    rom = loader_find_rom(entry->cksum.file);
    if ( !rom )
        return -ENOENT;

    if ( !rom->data )
    {
        printf("ERROR: No space allocated for file %s\n", entry->cksum.file);
        return -ENOSPC;
    }

    size = be32_to_cpu(rom->file.size);
    if ( offset >= size || start + length < start || start + length > size )
    {
        printf("ERROR: Invalid size\n");
        return -EINVAL;
    }

    set_checksum(rom->data + start, offset - start, length);

    return 0;
}

int loader_add_rom(struct acpi_ctxt *ctxt, const struct fw_cfg_file *file)
{
    const char *name = file->name;
    struct rom *rom;

    if ( !rom_needed(name) )
        return 0;

    rom = roms;
    while ( rom )
    {
        if ( !strncmp(rom->file.name, name, FW_CFG_FILE_PATH_MAX_LENGTH) )
            return -EEXIST;
        rom = rom->next;
    }

    rom = ctxt->mem_ops.alloc(ctxt, sizeof(*rom), 0);
    if ( !rom )
        return -ENOMEM;

    memcpy(&rom->file, file, sizeof(*file));
    rom->next = roms;
    rom->data = NULL;
    roms = rom;

    if ( !strncmp(name, "etc/table-loader", FW_CFG_FILE_PATH_MAX_LENGTH) )
        bios_loader = rom;

    return 0;
}

int loader_exec(struct acpi_ctxt *ctxt)
{
    struct loader_entry *entry;
    struct fw_cfg_file *file;
    unsigned long size, offset = 0;
    void *data;
    int rc = 0;

    if ( !bios_loader )
    {
        printf("ERROR: Cannot find BIOSLinkerLoader\n");
        return -ENODEV;
    }

    file = &bios_loader->file;
    size = be32_to_cpu(file->size);

    if ( size % sizeof(*entry) )
    {
        printf("ERROR: Invalid BIOSLinkerLoader size %ld, "
               "not multiples of entry size %ld\n",
               size, (unsigned long)sizeof(*entry));
        return -EINVAL;
    }

    rc = loader_load(ctxt, bios_loader);
    if ( rc )
    {
        printf("ERROR: Failed to load BIOSLinkerLoader, err %d\n", rc);
        return rc;
    }

    data = bios_loader->data;

    while ( offset < size )
    {
        entry = data + offset;

        switch ( entry->command )
        {
        case BIOS_LINKER_LOADER_COMMAND_ALLOCATE:
            rc = loader_exec_allocate(ctxt, entry);
            break;

        case BIOS_LINKER_LOADER_COMMAND_ADD_POINTER:
            rc = loader_exec_add_pointer(ctxt, entry);
            break;

        case BIOS_LINKER_LOADER_COMMAND_ADD_CHECKSUM:
            rc = loader_exec_add_checksum(entry);
            break;

        default:
            /* Skip unsupported commands */
            break;
        }

        if ( rc )
        {
            printf("ERROR: Failed to execute BIOSLinkerLoader command:\n");
            loader_cmd_display(entry);

            break;
        }

        offset += sizeof(*entry);
    }

    return rc;
}

struct acpi_20_rsdp *loader_get_rsdp(void)
{
    struct rom *rsdp = loader_find_rom("etc/acpi/rsdp");

    return rsdp ? rsdp->data : NULL;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
