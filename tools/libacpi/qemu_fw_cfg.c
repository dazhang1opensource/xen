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

/* QEMU fw_cfg I/O ports on x86 */
#define FW_CFG_PORT_SEL         0x510
#define FW_CFG_PORT_DATA        0x511

/* QEMU fw_cfg entries */
#define FW_CFG_SIGNATURE        0x0000

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

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
