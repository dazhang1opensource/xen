/*
 * libacpi/qemu_stub.c
 *
 * Stub functions of QEMU drivers. QEMU drivers are only used with
 * HVMLoader now. Add stub functions to ensure libacpi can be compiled
 * with others.
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

bool fw_cfg_exists(void)
{
    return false;
}

int fw_cfg_probe_roms(struct acpi_ctxt *ctxt)
{
    return -ENOSYS;
}

void fw_cfg_read_file(const struct fw_cfg_file *file, void *buf)
{
}

int loader_add_rom(struct acpi_ctxt* ctxt, const struct fw_cfg_file *file)
{
    return -ENOSYS;
}

int loader_exec(struct acpi_ctxt *ctxt)
{
    return -ENOSYS;
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
