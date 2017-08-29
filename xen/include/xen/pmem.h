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
#include <xen/mm.h>
#include <xen/types.h>

int pmem_register(unsigned long smfn, unsigned long emfn, unsigned int pxm);
int pmem_do_sysctl(struct xen_sysctl_nvdimm_op *nvdimm);

#ifdef CONFIG_X86

int pmem_dom0_setup_permission(struct domain *d);
int pmem_arch_setup(unsigned long smfn, unsigned long emfn, unsigned int pxm,
                    unsigned long mgmt_smfn, unsigned long mgmt_emfn,
                    unsigned long *used_mgmt_mfns);

struct xen_pmem_map_args {
    struct domain *domain;

    unsigned long mfn;     /* start MFN of pmems page to be mapped */
    unsigned long gfn;     /* start GFN of target domain */
    unsigned long nr_mfns; /* number of pmem pages to be mapped */

    /* For preemption ... */
    unsigned long nr_done; /* number of pmem pages processed so far */
    int preempted;         /* Is the operation preempted? */
};

int pmem_populate(struct xen_pmem_map_args *args);
void pmem_page_cleanup(struct page_info *page);

#else /* !CONFIG_X86 */

static inline int pmem_dom0_setup_permission(...)
{
    return -ENOSYS;
}

static inline int pmem_arch_setup(...)
{
    return -ENOSYS;
}

static inline int pmem_populate(...)
{
    return -ENOSYS;
}

static inline void pmem_page_cleanup(...)
{
}

#endif /* CONFIG_X86 */

#endif /* CONFIG_NVDIMM_PMEM */
#endif /* __XEN_PMEM_H__ */
