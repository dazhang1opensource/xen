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
#include "libacpi.h"
#include "qemu.h"

struct rom {
    struct fw_cfg_file file;
    struct rom *next;
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
    roms = rom;

    if ( !strncmp(name, "etc/table-loader", FW_CFG_FILE_PATH_MAX_LENGTH) )
        bios_loader = rom;

    return 0;
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
