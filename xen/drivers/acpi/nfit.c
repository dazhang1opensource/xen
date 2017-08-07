/*
 * xen/drivers/acpi/nfit.c
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

#include <xen/acpi.h>
#include <xen/init.h>
#include <xen/mm.h>
#include <xen/pfn.h>

/*
 * GUID of a byte addressable persistent memory region
 * (ref. ACPI 6.2, Section 5.2.25.2)
 */
static const uint8_t nfit_spa_pmem_guid[] =
{
    0x79, 0xd3, 0xf0, 0x66, 0xf3, 0xb4, 0x74, 0x40,
    0xac, 0x43, 0x0d, 0x33, 0x18, 0xb7, 0x8c, 0xdb,
};

struct nfit_spa_desc {
    struct list_head link;
    struct acpi_nfit_system_address *acpi_table;
    struct list_head memdev_list;
};

struct nfit_memdev_desc {
    struct list_head link;
    struct acpi_nfit_memory_map *acpi_table;
    struct list_head memdev_link;
};

struct acpi_nfit_desc {
    struct acpi_table_nfit *acpi_table;
    struct list_head spa_list;
    struct list_head memdev_list;
};

static struct acpi_nfit_desc nfit_desc = {
    .spa_list = LIST_HEAD_INIT(nfit_desc.spa_list),
    .memdev_list = LIST_HEAD_INIT(nfit_desc.memdev_list),
};

static void __init acpi_nfit_del_subtables(struct acpi_nfit_desc *desc)
{
    struct nfit_spa_desc *spa, *spa_next;
    struct nfit_memdev_desc *memdev, *memdev_next;

    list_for_each_entry_safe(spa, spa_next, &desc->spa_list, link)
    {
        list_del(&spa->link);
        xfree(spa);
    }
    list_for_each_entry_safe (memdev, memdev_next, &desc->memdev_list, link)
    {
        list_del(&memdev->link);
        xfree(memdev);
    }
}

static int __init acpi_nfit_add_subtables(struct acpi_nfit_desc *desc)
{
    struct acpi_table_nfit *nfit_table = desc->acpi_table;
    uint32_t hdr_offset = sizeof(*nfit_table);
    uint32_t nfit_length = nfit_table->header.length;
    struct acpi_nfit_header *hdr;
    struct nfit_spa_desc *spa_desc;
    struct nfit_memdev_desc *memdev_desc;
    int ret = 0;

#define INIT_DESC(desc, acpi_hdr, acpi_type, desc_list) \
    do {                                                \
        (desc) = xzalloc(typeof(*(desc)));              \
        if ( unlikely(!(desc)) ) {                      \
            ret = -ENOMEM;                              \
            goto nomem;                                 \
        }                                               \
        (desc)->acpi_table = (acpi_type *)(acpi_hdr);   \
        INIT_LIST_HEAD(&(desc)->link);                  \
        list_add_tail(&(desc)->link, (desc_list));      \
    } while ( 0 )

    while ( hdr_offset < nfit_length )
    {
        hdr = (void *)nfit_table + hdr_offset;
        hdr_offset += hdr->length;

        switch ( hdr->type )
        {
        case ACPI_NFIT_TYPE_SYSTEM_ADDRESS:
            INIT_DESC(spa_desc, hdr, struct acpi_nfit_system_address,
                      &desc->spa_list);
            break;

        case ACPI_NFIT_TYPE_MEMORY_MAP:
            INIT_DESC(memdev_desc, hdr, struct acpi_nfit_memory_map,
                      &desc->memdev_list);
            break;

        default:
            continue;
        }
    }

#undef INIT_DESC

    return 0;

 nomem:
    acpi_nfit_del_subtables(desc);

    return ret;
}

static void __init acpi_nfit_link_subtables(struct acpi_nfit_desc *desc)
{
    struct nfit_spa_desc *spa_desc;
    struct nfit_memdev_desc *memdev_desc;
    uint16_t spa_idx;

    list_for_each_entry(spa_desc, &desc->spa_list, link)
    {
        INIT_LIST_HEAD(&spa_desc->memdev_list);

        spa_idx = spa_desc->acpi_table->range_index;

        list_for_each_entry(memdev_desc, &desc->memdev_list, link)
        {
            if ( memdev_desc->acpi_table->range_index == spa_idx )
                list_add_tail(&memdev_desc->memdev_link,
                              &spa_desc->memdev_list);
        }
    }
}

static void __init acpi_nfit_register_pmem(struct acpi_nfit_desc *desc)
{
    struct nfit_spa_desc *spa_desc;
    struct nfit_memdev_desc *memdev_desc;
    struct acpi_nfit_system_address *spa;
    unsigned long smfn, emfn;
    bool failed;

    list_for_each_entry(spa_desc, &desc->spa_list, link)
    {
        spa = spa_desc->acpi_table;

        /* Skip non-pmem entry. */
        if ( memcmp(spa->range_guid, nfit_spa_pmem_guid, 16) )
            continue;

        smfn = paddr_to_pfn(spa->address);
        emfn = paddr_to_pfn(spa->address + spa->length);
        failed = false;

        list_for_each_entry(memdev_desc, &spa_desc->memdev_list, memdev_link)
        {
            if ( memdev_desc->acpi_table->flags &
                 (ACPI_NFIT_MEM_SAVE_FAILED |
                  ACPI_NFIT_MEM_RESTORE_FAILED |
                  ACPI_NFIT_MEM_FLUSH_FAILED |
                  ACPI_NFIT_MEM_NOT_ARMED |
                  ACPI_NFIT_MEM_MAP_FAILED) )
            {
                failed = true;
                break;
            }
        }

        if ( failed )
        {
            printk(XENLOG_INFO
                   "NFIT: detected failures on PMEM MFNs 0x%lx - 0x%lx, skipped\n",
                   smfn, emfn);
            continue;
        }

        printk(XENLOG_INFO "NFIT: PMEM MFNs 0x%lx - 0x%lx\n", smfn, emfn);
    }
}

void __init acpi_nfit_boot_init(void)
{
    acpi_status status;
    acpi_physical_address nfit_addr;
    acpi_native_uint nfit_len;

    status = acpi_get_table_phys(ACPI_SIG_NFIT, 0, &nfit_addr, &nfit_len);
    if ( ACPI_FAILURE(status) )
        return;

    nfit_desc.acpi_table = (struct acpi_table_nfit *)__va(nfit_addr);
    map_pages_to_xen((unsigned long)nfit_desc.acpi_table, PFN_DOWN(nfit_addr),
                     PFN_UP(nfit_addr + nfit_len) - PFN_DOWN(nfit_addr),
                     PAGE_HYPERVISOR);
}

void __init acpi_nfit_init(void)
{
    if ( !nfit_desc.acpi_table )
        return;

    /* Collect all SPA and memory map sub-tables. */
    if ( acpi_nfit_add_subtables(&nfit_desc) )
    {
        printk(XENLOG_ERR "NFIT: no memory for NFIT management\n");
        return;
    }

    /* Link descriptors of SPA and memory map sub-tables. */
    acpi_nfit_link_subtables(&nfit_desc);

    /* Register valid pmem regions to Xen hypervisor. */
    acpi_nfit_register_pmem(&nfit_desc);
}

/**
 * Search pmem regions overlapped with the specified address range.
 *
 * Parameters:
 *  @smfn, @emfn: the start and end MFN of address range to search
 *  @ret_smfn, @ret_emfn: return the address range of the first pmem region
 *                        in above range
 *
 * Return:
 *  Return true if a pmem region is overlapped with @smfn - @emfn. The
 *  start and end MFN of the lowest pmem region are returned via
 *  @ret_smfn and @ret_emfn respectively.
 *
 *  Return false if no pmem region is overlapped with @smfn - @emfn.
 */
bool __init acpi_nfit_boot_search_pmem(unsigned long smfn, unsigned long emfn,
                                       unsigned long *ret_smfn,
                                       unsigned long *ret_emfn)
{
    struct acpi_table_nfit *nfit_table = nfit_desc.acpi_table;
    uint32_t hdr_offset = sizeof(*nfit_table);
    unsigned long saddr = pfn_to_paddr(smfn), eaddr = pfn_to_paddr(emfn);
    unsigned long ret_saddr = 0, ret_eaddr = 0;

    if ( !nfit_table )
        return false;

    while ( hdr_offset < nfit_table->header.length )
    {
        struct acpi_nfit_header *hdr = (void *)nfit_table + hdr_offset;
        struct acpi_nfit_system_address *spa;
        unsigned long pmem_saddr, pmem_eaddr;

        hdr_offset += hdr->length;

        if ( hdr->type != ACPI_NFIT_TYPE_SYSTEM_ADDRESS )
            continue;

        spa = (struct acpi_nfit_system_address *)hdr;
        if ( memcmp(spa->range_guid, nfit_spa_pmem_guid, 16) )
            continue;

        pmem_saddr = spa->address;
        pmem_eaddr = pmem_saddr + spa->length;
        if ( pmem_saddr >= eaddr || pmem_eaddr <= saddr )
            continue;

        if ( ret_saddr < pmem_saddr )
            continue;
        ret_saddr = pmem_saddr;
        ret_eaddr = pmem_eaddr;
    }

    if ( ret_saddr == ret_eaddr )
        return false;

    *ret_smfn = paddr_to_pfn(ret_saddr);
    *ret_emfn = paddr_to_pfn(ret_eaddr);

    return true;
}
