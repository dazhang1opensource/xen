/*
 * xen/common/pmem.c
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

#include <xen/errno.h>
#include <xen/list.h>
#include <xen/pmem.h>

/*
 * All PMEM regions presenting in NFIT SPA range structures are linked
 * in this list.
 */
static LIST_HEAD(pmem_raw_regions);
static unsigned int nr_raw_regions;

struct pmem {
    struct list_head link; /* link to one of PMEM region list */
    unsigned long smfn;    /* start MFN of the PMEM region */
    unsigned long emfn;    /* end MFN of the PMEM region */

    union {
        struct {
            unsigned int pxm; /* proximity domain of the PMEM region */
        } raw;
    } u;
};

static bool check_overlap(unsigned long smfn1, unsigned long emfn1,
                          unsigned long smfn2, unsigned long emfn2)
{
    return (smfn1 >= smfn2 && smfn1 < emfn2) ||
           (emfn1 > smfn2 && emfn1 <= emfn2);
}

/**
 * Add a PMEM region to a list. All PMEM regions in the list are
 * sorted in the ascending order of the start address. A PMEM region,
 * whose range is overlapped with anyone in the list, cannot be added
 * to the list.
 *
 * Parameters:
 *  list:       the list to which a new PMEM region will be added
 *  smfn, emfn: the range of the new PMEM region
 *  entry:      return the new entry added to the list
 *
 * Return:
 *  On success, return 0 and the new entry added to the list is
 *  returned via @entry. Otherwise, return an error number and the
 *  value of @entry is undefined.
 */
static int pmem_list_add(struct list_head *list,
                         unsigned long smfn, unsigned long emfn,
                         struct pmem **entry)
{
    struct list_head *cur;
    struct pmem *new_pmem;

    list_for_each_prev(cur, list)
    {
        struct pmem *cur_pmem = list_entry(cur, struct pmem, link);
        unsigned long cur_smfn = cur_pmem->smfn;
        unsigned long cur_emfn = cur_pmem->emfn;

        if ( check_overlap(smfn, emfn, cur_smfn, cur_emfn) )
            return -EEXIST;

        if ( cur_smfn < smfn )
            break;
    }

    new_pmem = xzalloc(struct pmem);
    if ( !new_pmem )
        return -ENOMEM;

    new_pmem->smfn = smfn;
    new_pmem->emfn = emfn;
    list_add(&new_pmem->link, cur);
    if ( entry )
        *entry = new_pmem;

    return 0;
}

/**
 * Register a pmem region to Xen.
 *
 * Parameters:
 *  smfn, emfn: start and end MFNs of the pmem region
 *  pxm:        the proximity domain of the pmem region
 *
 * Return:
 *  On success, return 0. Otherwise, an error number is returned.
 */
int pmem_register(unsigned long smfn, unsigned long emfn, unsigned int pxm)
{
    int rc;
    struct pmem *pmem;

    if ( smfn >= emfn )
        return -EINVAL;

    rc = pmem_list_add(&pmem_raw_regions, smfn, emfn, &pmem);
    if ( !rc )
    {
        pmem->u.raw.pxm = pxm;
        nr_raw_regions++;
    }

    return rc;
}
