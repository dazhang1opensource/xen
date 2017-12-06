/*
 * libacpi/qemu_fw_cfg.c
 *
 * Driver of QEMU fw_cfg interface. The reference document can be found at
 * https://github.com/qemu/qemu/blob/master/docs/specs/fw_cfg.txt.
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

/* QEMU fw_cfg I/O ports on x86 */
#define FW_CFG_PORT_SEL         0x510
#define FW_CFG_PORT_DATA        0x511

/* QEMU fw_cfg entries */
#define FW_CFG_SIGNATURE        0x0000
#define FW_CFG_FILE_DIR         0x0019

static inline void fw_cfg_select(uint16_t entry)
{
    outw(FW_CFG_PORT_SEL, entry);
}

static inline void fw_cfg_read(void *buf, uint32_t len)
{
    while ( len-- )
        *(uint8_t *)(buf++) = inb(FW_CFG_PORT_DATA);
}

static void fw_cfg_read_entry(uint16_t entry, void *buf, uint32_t len)
{
    fw_cfg_select(entry);
    fw_cfg_read(buf, len);
}

bool fw_cfg_exists(void)
{
    uint32_t sig;

    fw_cfg_read_entry(FW_CFG_SIGNATURE, &sig, sizeof(sig));

    return sig == 0x554d4551 /* "QEMU" */;
}

int fw_cfg_probe_roms(struct acpi_ctxt *ctxt)
{
    struct fw_cfg_file file;
    uint32_t count, i;
    int rc = 0;

    fw_cfg_read_entry(FW_CFG_FILE_DIR, &count, sizeof(count));
    count = be32_to_cpu(count);

    for ( i = 0; i < count; i++ )
    {
        fw_cfg_read(&file, sizeof(file));
        rc = loader_add_rom(ctxt, &file);
        if ( rc )
        {
            file.name[FW_CFG_FILE_PATH_MAX_LENGTH - 1] = '\0';
            printf("ERROR: failed to load QEMU ROM %s, err %d\n",
                   file.name, rc);
            break;
        }
    }

    return rc;
}

void fw_cfg_read_file(const struct fw_cfg_file *file, void *buf)
{
    fw_cfg_read_entry(be16_to_cpu(file->select), buf,
                      be32_to_cpu(file->size));
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
