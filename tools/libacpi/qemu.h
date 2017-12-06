/*
 * libacpi/qemu.h
 *
 * Header file of QEMU drivers.
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

#ifndef __QEMU_H__
#define __QEMU_H__

#include LIBACPI_STDUTILS
#include "acpi2_0.h"
#include "libacpi.h"

#define FW_CFG_FILE_PATH_MAX_LENGTH 56

/* An individual file entry, 64 bytes total. */
struct fw_cfg_file {
    uint32_t size;      /* size of referenced fw_cfg item, big-endian */
    uint16_t select;    /* selector key of fw_cfg item, big-endian */
    uint16_t reserved;
    char name[FW_CFG_FILE_PATH_MAX_LENGTH]; /* fw_cfg item name,    */
                                            /* NUL-terminated ascii */
};

int fw_cfg_probe_roms(struct acpi_ctxt *ctxt);
void fw_cfg_read_file(const struct fw_cfg_file *file, void *buf);

int loader_add_rom(struct acpi_ctxt* ctxt, const struct fw_cfg_file *file);
int loader_exec(struct acpi_ctxt *ctxt);
struct acpi_20_rsdp *loader_get_rsdp(void);

#endif /* !__QEMU_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
