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
#include <xen/event.h>
#include <xen/list.h>
#include <xen/iocap.h>
#include <xen/paging.h>
#include <xen/pmem.h>
#include <xen/sched.h>

#include <asm/guest_access.h>

/*
 * All PMEM regions presenting in NFIT SPA range structures are linked
 * in this list.
 */
static LIST_HEAD(pmem_raw_regions);
static unsigned int nr_raw_regions;

/*
 * All PMEM regions reserved for management purpose are linked to this
 * list. All of them must be covered by one or multiple PMEM regions
 * in list pmem_raw_regions, and not appear in list pmem_data_regions.
 */
static LIST_HEAD(pmem_mgmt_regions);
static DEFINE_SPINLOCK(pmem_mgmt_lock);
static unsigned int nr_mgmt_regions;

/*
 * All PMEM regions that can be mapped to guest are linked to this
 * list. All of them must be covered by one or multiple PMEM regions
 * in list pmem_raw_regions, and not appear in list pmem_mgmt_regions.
 */
static LIST_HEAD(pmem_data_regions);
static DEFINE_SPINLOCK(pmem_data_lock);
static unsigned int nr_data_regions;

struct pmem {
    struct list_head link; /* link to one of PMEM region list */
    unsigned long smfn;    /* start MFN of the PMEM region */
    unsigned long emfn;    /* end MFN of the PMEM region */
    spinlock_t lock;

    union {
        struct {
            unsigned int pxm; /* proximity domain of the PMEM region */
        } raw;

        struct {
            unsigned long used; /* # of used pages in MGMT PMEM region */
        } mgmt;

        struct {
            unsigned long mgmt_smfn; /* start MFN of management region */
            unsigned long mgmt_emfn; /* end MFN of management region */
        } data;
    } u;
};

static bool check_overlap(unsigned long smfn1, unsigned long emfn1,
                          unsigned long smfn2, unsigned long emfn2)
{
    return (smfn1 >= smfn2 && smfn1 < emfn2) ||
           (emfn1 > smfn2 && emfn1 <= emfn2);
}

static bool check_cover(struct list_head *list,
                        unsigned long smfn, unsigned long emfn)
{
    struct list_head *cur;
    struct pmem *pmem;
    unsigned long pmem_smfn, pmem_emfn;

    list_for_each(cur, list)
    {
        pmem = list_entry(cur, struct pmem, link);
        pmem_smfn = pmem->smfn;
        pmem_emfn = pmem->emfn;

        if ( smfn < pmem_smfn )
            return false;

        if ( emfn <= pmem_emfn )
            return true;

        smfn = max(smfn, pmem_emfn);
    }

    return false;
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
    spin_lock_init(&new_pmem->lock);
    list_add(&new_pmem->link, cur);
    if ( entry )
        *entry = new_pmem;

    return 0;
}

/**
 * Delete the specified entry from the list to which it's currently linked.
 *
 * Parameters:
 *  entry: the entry to be deleted
 */
static void pmem_list_del(struct pmem *entry)
{
    list_del(&entry->link);
    xfree(entry);
}

static int pmem_get_regions_nr(xen_sysctl_nvdimm_pmem_regions_nr_t *regions_nr)
{
    int rc = 0;

    switch ( regions_nr->type )
    {
    case PMEM_REGION_TYPE_RAW:
        regions_nr->num_regions = nr_raw_regions;
        break;

    case PMEM_REGION_TYPE_MGMT:
        regions_nr->num_regions = nr_mgmt_regions;
        break;

    case PMEM_REGION_TYPE_DATA:
        regions_nr->num_regions = nr_data_regions;
        break;

    default:
        rc = -EINVAL;
    }

    return rc;
}

static int pmem_get_raw_regions(
    XEN_GUEST_HANDLE_64(xen_sysctl_nvdimm_pmem_raw_region_t) regions,
    unsigned int *num_regions)
{
    struct list_head *cur;
    unsigned int nr = 0, max = *num_regions;
    xen_sysctl_nvdimm_pmem_raw_region_t region;
    int rc = 0;

    if ( !guest_handle_okay(regions, max * sizeof(region)) )
        return -EINVAL;

    list_for_each(cur, &pmem_raw_regions)
    {
        struct pmem *pmem = list_entry(cur, struct pmem, link);

        if ( nr >= max )
            break;

        region.smfn = pmem->smfn;
        region.emfn = pmem->emfn;
        region.pxm = pmem->u.raw.pxm;

        if ( copy_to_guest_offset(regions, nr, &region, 1) )
        {
            rc = -EFAULT;
            break;
        }

        nr++;
    }

    *num_regions = nr;

    return rc;
}

static int pmem_get_mgmt_regions(
    XEN_GUEST_HANDLE_64(xen_sysctl_nvdimm_pmem_mgmt_region_t) regions,
    unsigned int *num_regions)
{
    struct list_head *cur;
    unsigned int nr = 0, max = *num_regions;
    xen_sysctl_nvdimm_pmem_mgmt_region_t region;
    int rc = 0;

    if ( !guest_handle_okay(regions, max * sizeof(region)) )
        return -EINVAL;

    spin_lock(&pmem_mgmt_lock);

    list_for_each(cur, &pmem_mgmt_regions)
    {
        struct pmem *pmem = list_entry(cur, struct pmem, link);

        if ( nr >= max )
            break;

        region.smfn = pmem->smfn;
        region.emfn = pmem->emfn;
        region.used_mfns = pmem->u.mgmt.used;

        if ( copy_to_guest_offset(regions, nr, &region, 1) )
        {
            rc = -EFAULT;
            break;
        }

        nr++;
    }

    spin_unlock(&pmem_mgmt_lock);

    *num_regions = nr;

    return rc;
}

static int pmem_get_data_regions(
    XEN_GUEST_HANDLE_64(xen_sysctl_nvdimm_pmem_data_region_t) regions,
    unsigned int *num_regions)
{
    struct list_head *cur;
    unsigned int nr = 0, max = *num_regions;
    xen_sysctl_nvdimm_pmem_data_region_t region;
    int rc = 0;

    if ( !guest_handle_okay(regions, max * sizeof(region)) )
        return -EINVAL;

    spin_lock(&pmem_data_lock);

    list_for_each(cur, &pmem_data_regions)
    {
        struct pmem *pmem = list_entry(cur, struct pmem, link);

        if ( nr >= max )
            break;

        region.smfn = pmem->smfn;
        region.emfn = pmem->emfn;
        region.mgmt_smfn = pmem->u.data.mgmt_smfn;
        region.mgmt_emfn = pmem->u.data.mgmt_emfn;

        if ( copy_to_guest_offset(regions, nr, &region, 1) )
        {
            rc = -EFAULT;
            break;
        }

        nr++;
    }

    spin_unlock(&pmem_data_lock);

    *num_regions = nr;

    return rc;
}

static int pmem_get_regions(xen_sysctl_nvdimm_pmem_regions_t *regions)
{
    unsigned int type = regions->type, max = regions->num_regions;
    int rc = 0;

    if ( !max )
        return 0;

    switch ( type )
    {
    case PMEM_REGION_TYPE_RAW:
        rc = pmem_get_raw_regions(regions->u_buffer.raw_regions, &max);
        break;

    case PMEM_REGION_TYPE_MGMT:
        rc = pmem_get_mgmt_regions(regions->u_buffer.mgmt_regions, &max);
        break;

    case PMEM_REGION_TYPE_DATA:
        rc = pmem_get_data_regions(regions->u_buffer.data_regions, &max);
        break;

    default:
        rc = -EINVAL;
    }

    if ( !rc )
        regions->num_regions = max;

    return rc;
}

static bool check_mgmt_size(unsigned long mgmt_mfns, unsigned long total_mfns)
{
    unsigned long required =
        ((sizeof(struct page_info) * total_mfns) >> PAGE_SHIFT) +
        ((sizeof(*machine_to_phys_mapping) * total_mfns) >> PAGE_SHIFT);

    if ( required > mgmt_mfns )
        printk(XENLOG_DEBUG "PMEM: insufficient management pages, "
               "0x%lx pages required, 0x%lx pages available\n",
               required, mgmt_mfns);

    return mgmt_mfns >= required;
}

static bool check_address_and_pxm(unsigned long smfn, unsigned long emfn,
                                  unsigned int *ret_pxm)
{
    struct list_head *cur;
    long pxm = -1;

    list_for_each(cur, &pmem_raw_regions)
    {
        struct pmem *raw = list_entry(cur, struct pmem, link);
        unsigned long raw_smfn = raw->smfn, raw_emfn = raw->emfn;

        if ( !check_overlap(smfn, emfn, raw_smfn, raw_emfn) )
            continue;

        if ( smfn < raw_smfn )
            return false;

        if ( pxm != -1 && pxm != raw->u.raw.pxm )
            return false;
        pxm = raw->u.raw.pxm;

        smfn = min(emfn, raw_emfn);
        if ( smfn == emfn )
            break;
    }

    *ret_pxm = pxm;

    return smfn == emfn;
}

static int pmem_setup_mgmt(unsigned long smfn, unsigned long emfn)
{
    struct pmem *mgmt;
    unsigned long used_mgmt_mfns;
    unsigned int pxm;
    int rc;

    if ( smfn == mfn_x(INVALID_MFN) || emfn == mfn_x(INVALID_MFN) ||
         smfn >= emfn )
        return -EINVAL;

    /*
     * Require the PMEM region in one proximity domain, in order to
     * avoid the error recovery from multiple calls to pmem_arch_setup()
     * which is not revertible.
     */
    if ( !check_address_and_pxm(smfn, emfn, &pxm) )
        return -EINVAL;

    if ( !check_mgmt_size(emfn - smfn, emfn - smfn) )
        return -ENOSPC;

    spin_lock(&pmem_mgmt_lock);

    rc = pmem_list_add(&pmem_mgmt_regions, smfn, emfn, &mgmt);
    if ( rc )
        goto out;

    rc = pmem_arch_setup(smfn, emfn, pxm, smfn, emfn, &used_mgmt_mfns);
    if ( rc )
    {
        pmem_list_del(mgmt);
        goto out;
    }

    mgmt->u.mgmt.used = used_mgmt_mfns;
    nr_mgmt_regions++;

 out:
    spin_unlock(&pmem_mgmt_lock);

    return rc;
}

static struct pmem *find_mgmt_region(unsigned long smfn, unsigned long emfn)
{
    struct list_head *cur;

    ASSERT(spin_is_locked(&pmem_mgmt_lock));

    list_for_each(cur, &pmem_mgmt_regions)
    {
        struct pmem *mgmt = list_entry(cur, struct pmem, link);

        if ( smfn >= mgmt->smfn && emfn <= mgmt->emfn )
            return mgmt;
    }

    return NULL;
}

static int pmem_setup_data(unsigned long smfn, unsigned long emfn,
                           unsigned long mgmt_smfn, unsigned long mgmt_emfn)
{
    struct pmem *data, *mgmt = NULL;
    unsigned long used_mgmt_mfns;
    unsigned int pxm;
    int rc;

    if ( smfn == mfn_x(INVALID_MFN) || emfn == mfn_x(INVALID_MFN) ||
         smfn >= emfn )
        return -EINVAL;

    /*
     * Require the PMEM region in one proximity domain, in order to
     * avoid the error recovery from multiple calls to pmem_arch_setup()
     * which is not revertible.
     */
    if ( !check_address_and_pxm(smfn, emfn, &pxm) )
        return -EINVAL;

    if ( mgmt_smfn == mfn_x(INVALID_MFN) || mgmt_emfn == mfn_x(INVALID_MFN) ||
         mgmt_smfn >= mgmt_emfn )
        return -EINVAL;

    spin_lock(&pmem_mgmt_lock);
    mgmt = find_mgmt_region(mgmt_smfn, mgmt_emfn);
    if ( !mgmt )
    {
        spin_unlock(&pmem_mgmt_lock);
        return -ENXIO;
    }
    spin_unlock(&pmem_mgmt_lock);

    spin_lock(&mgmt->lock);

    mgmt_smfn = mgmt->smfn + mgmt->u.mgmt.used;
    if ( !check_mgmt_size(mgmt_emfn - mgmt_smfn, emfn - smfn) )
    {
        spin_unlock(&mgmt->lock);
        return -ENOSPC;
    }

    spin_lock(&pmem_data_lock);

    rc = pmem_list_add(&pmem_data_regions, smfn, emfn, &data);
    if ( rc )
        goto out;
    data->u.data.mgmt_smfn = data->u.data.mgmt_emfn = mfn_x(INVALID_MFN);

    rc = pmem_arch_setup(smfn, emfn, pxm,
                         mgmt_smfn, mgmt_emfn, &used_mgmt_mfns);
    if ( rc )
    {
        pmem_list_del(data);
        goto out;
    }

    mgmt->u.mgmt.used = mgmt_smfn - mgmt->smfn + used_mgmt_mfns;
    data->u.data.mgmt_smfn = mgmt_smfn;
    data->u.data.mgmt_emfn = mgmt->smfn + mgmt->u.mgmt.used;

    nr_data_regions++;

 out:
    spin_unlock(&pmem_data_lock);
    spin_unlock(&mgmt->lock);

    return rc;
}

static int pmem_setup(unsigned long smfn, unsigned long emfn,
                      unsigned long mgmt_smfn, unsigned long mgmt_emfn,
                      unsigned int type)
{
    int rc;

    switch ( type )
    {
    case PMEM_REGION_TYPE_MGMT:
        if ( smfn != mgmt_smfn || emfn != mgmt_emfn )
        {
            rc = -EINVAL;
            break;
        }

        rc = pmem_setup_mgmt(smfn, emfn);

        break;

    case PMEM_REGION_TYPE_DATA:
        rc = pmem_setup_data(smfn, emfn, mgmt_smfn, mgmt_emfn);
        break;

    default:
        rc = -EINVAL;
    }

    return rc;
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

/**
 * Top-level hypercall handler of XEN_SYSCTL_nvdimm_pmem_*.
 *
 * Parameters:
 *  nvdimm: the hypercall parameters
 *
 * Return:
 *  On success, return 0. Otherwise, return a non-zero error code.
 */
int pmem_do_sysctl(struct xen_sysctl_nvdimm_op *nvdimm)
{
    int rc;

    switch ( nvdimm->cmd )
    {
    case XEN_SYSCTL_nvdimm_pmem_get_regions_nr:
        rc = pmem_get_regions_nr(&nvdimm->u.pmem_regions_nr);
        break;

    case XEN_SYSCTL_nvdimm_pmem_get_regions:
        rc = pmem_get_regions(&nvdimm->u.pmem_regions);
        break;

    case XEN_SYSCTL_nvdimm_pmem_setup:
    {
        struct xen_sysctl_nvdimm_pmem_setup *setup = &nvdimm->u.pmem_setup;
        rc = pmem_setup(setup->smfn, setup->emfn,
                        setup->mgmt_smfn, setup->mgmt_emfn,
                        setup->type);
        break;
    }

    default:
        rc = -ENOSYS;
    }

    nvdimm->err = -rc;

    return rc;
}

#ifdef CONFIG_X86

static int pmem_assign_page(struct domain *d, struct page_info *pg,
                            unsigned long gfn)
{
    int rc;

    if ( pg->count_info != (PGC_state_free | PGC_pmem_page) )
        return -EBUSY;

    pg->count_info = PGC_allocated | PGC_state_inuse | PGC_pmem_page | 1;
    pg->u.inuse.type_info = 0;
    page_set_owner(pg, d);

    rc = guest_physmap_add_page(d, _gfn(gfn), _mfn(page_to_mfn(pg)), 0);
    if ( rc )
    {
        page_set_owner(pg, NULL);
        pg->count_info = PGC_state_free | PGC_pmem_page;

        return rc;
    }

    spin_lock(&d->pmem_lock);
    page_list_add_tail(pg, &d->pmem_page_list);
    spin_unlock(&d->pmem_lock);

    return 0;
}

static int pmem_unassign_page(struct domain *d, struct page_info *pg,
                              unsigned long gfn)
{
    int rc;

    spin_lock(&d->pmem_lock);
    page_list_del(pg, &d->pmem_page_list);
    spin_unlock(&d->pmem_lock);

    rc = guest_physmap_remove_page(d, _gfn(gfn), _mfn(page_to_mfn(pg)), 0);

    page_set_owner(pg, NULL);
    pg->count_info = PGC_state_free | PGC_pmem_page;

    return 0;
}

int pmem_populate(struct xen_pmem_map_args *args)
{
    struct domain *d = args->domain;
    unsigned long i = args->nr_done;
    unsigned long mfn = args->mfn + i;
    unsigned long emfn = args->mfn + args->nr_mfns;
    unsigned long gfn = args->gfn + i;
    struct page_info *page;
    int rc = 0, err = 0;

    if ( unlikely(d->is_dying) )
        return -EINVAL;

    if ( !is_hvm_domain(d) )
        return -EINVAL;

    spin_lock(&pmem_data_lock);

    if ( !check_cover(&pmem_data_regions, mfn, emfn) )
    {
        rc = -ENXIO;
        goto out;
    }

    for ( ; mfn < emfn; i++, mfn++, gfn++ )
    {
        if ( i != args->nr_done && hypercall_preempt_check() )
        {
            args->preempted = 1;
            rc = -ERESTART;
            break;
        }

        page = mfn_to_page(mfn);
        if ( !page_state_is(page, free) )
        {
            rc = -EBUSY;
            break;
        }

        rc = pmem_assign_page(d, page, gfn);
        if ( rc )
            break;
    }

 out:
    if ( rc && rc != -ERESTART )
        while ( i-- && !err )
            err = pmem_unassign_page(d, mfn_to_page(--mfn), --gfn);

    spin_unlock(&pmem_data_lock);

    if ( unlikely(err) )
    {
        /*
         * If we unfortunately fails to recover from the previous
         * failure, some PMEM pages may still be mapped to the
         * domain. As pmem_populate() is now called only during domain
         * creation, let's crash the domain.
         */
        domain_crash(d);
        rc = err;
    }

    args->nr_done = i;

    return rc;
}

int __init pmem_dom0_setup_permission(struct domain *d)
{
    struct list_head *cur;
    struct pmem *pmem;
    int rc = 0;

    list_for_each(cur, &pmem_raw_regions)
    {
        pmem = list_entry(cur, struct pmem, link);
        rc |= iomem_deny_access(d, pmem->smfn, pmem->emfn - 1);
    }

    return rc;
}

#endif /* CONFIG_X86 */
