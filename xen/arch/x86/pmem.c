/******************************************************************************
 * arch/x86/pmem.c
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

#include <xen/guest_access.h>
#include <xen/list.h>
#include <xen/spinlock.h>
#include <xen/pmem.h>
#include <xen/iocap.h>
#include <asm-x86/mm.h>

/*
 * All pmem regions reported from Dom0 are linked in pmem_list, which
 * is proected by pmem_list_lock. Its entries are of type struct pmem
 * and sorted incrementally by field spa.
 */
static DEFINE_SPINLOCK(pmem_list_lock);
static LIST_HEAD(pmem_list);

struct pmem {
    struct list_head link;   /* link to pmem_list */
    unsigned long spfn;      /* start PFN of the whole pmem region */
    unsigned long epfn;      /* end PFN of the whole pmem region */
    unsigned long rsv_spfn;  /* start PFN of the reserved area */
    unsigned long rsv_epfn;  /* end PFN of the reserved area */
    unsigned long data_spfn; /* start PFN of the data area */
    unsigned long data_epfn; /* end PFN of the data area */
};

static int is_included(unsigned long s1, unsigned long e1,
                       unsigned long s2, unsigned long e2)
{
    return s1 <= s2 && s2 < e2 && e2 <= e1;
}

static int is_overlaped(unsigned long s1, unsigned long e1,
                        unsigned long s2, unsigned long e2)
{
    return (s1 <= s2 && s2 < e1) || (s2 < s1 && s1 < e2);
}

static int check_reserved_size(unsigned long rsv_mfns, unsigned long total_mfns)
{
    return rsv_mfns >=
        ((sizeof(struct page_info) * total_mfns) >> PAGE_SHIFT) +
        ((sizeof(*machine_to_phys_mapping) * total_mfns) >> PAGE_SHIFT);
}

static int pmem_add_check(unsigned long spfn, unsigned long epfn,
                          unsigned long rsv_spfn, unsigned long rsv_epfn,
                          unsigned long data_spfn, unsigned long data_epfn)
{
    if ( spfn >= epfn || rsv_spfn >= rsv_epfn || data_spfn >= data_epfn )
        return 0;

    if ( !is_included(spfn, epfn, rsv_spfn, rsv_epfn) ||
         !is_included(spfn, epfn, data_spfn, data_epfn) )
        return 0;

    if ( is_overlaped(rsv_spfn, rsv_epfn, data_spfn, data_epfn) )
        return 0;

    if ( !check_reserved_size(rsv_epfn - rsv_spfn, epfn - spfn) )
        return 0;

    return 1;
}

static int pmem_list_add(unsigned long spfn, unsigned long epfn,
                         unsigned long rsv_spfn, unsigned long rsv_epfn,
                         unsigned long data_spfn, unsigned long data_epfn)
{
    struct list_head *cur;
    struct pmem *new_pmem;
    int ret = 0;

    spin_lock(&pmem_list_lock);

    list_for_each_prev(cur, &pmem_list)
    {
        struct pmem *cur_pmem = list_entry(cur, struct pmem, link);
        unsigned long cur_spfn = cur_pmem->spfn;
        unsigned long cur_epfn = cur_pmem->epfn;

        if ( (cur_spfn <= spfn && spfn < cur_epfn) ||
             (spfn <= cur_spfn && cur_spfn < epfn) )
        {
            ret = -EINVAL;
            goto out;
        }

        if ( cur_spfn < spfn )
            break;
    }

    new_pmem = xmalloc(struct pmem);
    if ( !new_pmem )
    {
        ret = -ENOMEM;
        goto out;
    }
    new_pmem->spfn      = spfn;
    new_pmem->epfn      = epfn;
    new_pmem->rsv_spfn  = rsv_spfn;
    new_pmem->rsv_epfn  = rsv_epfn;
    new_pmem->data_spfn = data_spfn;
    new_pmem->data_epfn = data_epfn;
    list_add(&new_pmem->link, cur);

 out:
    spin_unlock(&pmem_list_lock);
    return ret;
}

int pmem_add(unsigned long spfn, unsigned long epfn,
             unsigned long rsv_spfn, unsigned long rsv_epfn,
             unsigned long data_spfn, unsigned long data_epfn)
{
    int ret;

    if ( !pmem_add_check(spfn, epfn, rsv_spfn, rsv_epfn, data_spfn, data_epfn) )
        return -EINVAL;

    ret = pmem_setup(spfn, epfn, rsv_spfn, rsv_epfn, data_spfn, data_epfn);
    if ( ret )
        goto out;

    ret = iomem_deny_access(current->domain, rsv_spfn, rsv_epfn);
    if ( ret )
        goto out;

    ret = pmem_list_add(spfn, epfn, rsv_spfn, rsv_epfn, data_spfn, data_epfn);
    if ( ret )
        goto out;

    printk(XENLOG_INFO
           "pmem: pfns     0x%lx - 0x%lx\n"
           "      reserved 0x%lx - 0x%lx\n"
           "      data     0x%lx - 0x%lx\n",
           spfn, epfn, rsv_spfn, rsv_epfn, data_spfn, data_epfn);

 out:
    return ret;
}
