/*
 * xen/include/xen/pmem.h
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

#ifndef __XEN_PMEM_H__
#define __XEN_PMEM_H__

#include <xen/types.h>

struct xen_pmemmap_args {
    struct domain *domain;
    xen_pfn_t mfn;
    xen_pfn_t gpfn;
    unsigned int nr_mfns;
    unsigned int nr_done;
    int preempted;
};

int pmem_add(unsigned long spfn, unsigned long epfn,
             unsigned long rsv_spfn, unsigned long rsv_epfn,
             unsigned long data_spfn, unsigned long data_epfn);
int pmem_populate(struct xen_pmemmap_args *args);

#endif /* __XEN_PMEM_H__ */
