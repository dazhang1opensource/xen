/*
 * xen/include/xen/pmem.h
 *
 * Copyright (C) 2017, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms and conditions of the GNU General Public
 * License, version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __XEN_PMEM_H__
#define __XEN_PMEM_H__
#ifdef CONFIG_NVDIMM_PMEM

#include <public/sysctl.h>
#include <xen/types.h>

int pmem_register(unsigned long smfn, unsigned long emfn, unsigned int pxm);
int pmem_do_sysctl(struct xen_sysctl_nvdimm_op *nvdimm);

#ifdef CONFIG_X86

int pmem_dom0_setup_permission(struct domain *d);

#else /* !CONFIG_X86 */

static inline int pmem_dom0_setup_permission(...)
{
    return -ENOSYS;
}

#endif /* CONFIG_X86 */

#endif /* CONFIG_NVDIMM_PMEM */
#endif /* __XEN_PMEM_H__ */
