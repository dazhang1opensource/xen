/*
 * tools/libxl/libxl_nvdimm.c
 *
 * <One line description of the file and what it does>
 *
 * Copyright (C) 2017  Intel Corporation
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

#include <xenctrl.h>
#include <xen-tools/libs.h>

#include "libxl_internal.h"

/*
 * Convert sizeof(libxl_nvdimm_pmem_*_region) to
 * sizeof(xen_sysctl_nvdimm_pmem_*_region_t).
 *
 * Indexed by LIBXL_NVDIMM_PMEM_REGION_TYPE_*.
 */
static size_t xc_pmem_region_struct_size[] = {
    [LIBXL_NVDIMM_PMEM_REGION_TYPE_RAW] = sizeof(libxl_nvdimm_pmem_raw_region),
    [LIBXL_NVDIMM_PMEM_REGION_TYPE_MGMT] = sizeof(libxl_nvdimm_pmem_mgmt_region),
    [LIBXL_NVDIMM_PMEM_REGION_TYPE_DATA] = sizeof(libxl_nvdimm_pmem_data_region),
};

static int get_xc_region_type(libxl_nvdimm_pmem_region_type type,
                               uint8_t *xc_type_r)
{
    static uint8_t xc_region_types[] = {
        [LIBXL_NVDIMM_PMEM_REGION_TYPE_RAW] = PMEM_REGION_TYPE_RAW,
        [LIBXL_NVDIMM_PMEM_REGION_TYPE_MGMT] = PMEM_REGION_TYPE_MGMT,
        [LIBXL_NVDIMM_PMEM_REGION_TYPE_DATA] = PMEM_REGION_TYPE_DATA,
    };
    static unsigned int nr_types =
        sizeof(xc_region_types) / sizeof(xc_region_types[0]);

    if (type >= nr_types)
        return -EINVAL;

    *xc_type_r = xc_region_types[type];

    return 0;
}

static void copy_from_xc_regions(libxl_nvdimm_pmem_region *tgt_regions,
                                 void *src_xc_regions, uint8_t xc_type,
                                 unsigned int nr)
{
    static size_t offset = offsetof(libxl_nvdimm_pmem_region, u);
    libxl_nvdimm_pmem_region *tgt = tgt_regions;
    libxl_nvdimm_pmem_region *end = tgt_regions + nr;
    void *src = src_xc_regions;
    size_t size = xc_pmem_region_struct_size[xc_type];

    BUILD_BUG_ON(sizeof(libxl_nvdimm_pmem_raw_region) !=
                 sizeof(xen_sysctl_nvdimm_pmem_raw_region_t));
    BUILD_BUG_ON(sizeof(libxl_nvdimm_pmem_mgmt_region) !=
                 sizeof(xen_sysctl_nvdimm_pmem_mgmt_region_t));
    BUILD_BUG_ON(sizeof(libxl_nvdimm_pmem_data_region) !=
                 sizeof(xen_sysctl_nvdimm_pmem_data_region_t));

    while (tgt < end) {
        memcpy((void *)tgt + offset, src, size);
        tgt += 1;
        src += size;
    }
}

int libxl_nvdimm_pmem_get_regions(libxl_ctx *ctx,
                                  libxl_nvdimm_pmem_region_type type,
                                  libxl_nvdimm_pmem_region **regions_r,
                                  unsigned int *nr_r)
{
    GC_INIT(ctx);
    libxl_nvdimm_pmem_region *regions;
    uint8_t xc_type;
    unsigned int nr;
    void *xc_regions;
    int rc = 0, err;

    err = get_xc_region_type(type, &xc_type);
    if (err) {
        LOGE(ERROR, "invalid PMEM region type %d required", type);
        rc = ERROR_INVAL;
        goto out;
    }

    err = xc_nvdimm_pmem_get_regions_nr(ctx->xch, xc_type, &nr);
    if (err) {
        rc = ERROR_FAIL;
        goto out;
    }

    if (!nr) {
        *nr_r = 0;
        goto out;
    }

    xc_regions = libxl__malloc(gc, nr * xc_pmem_region_struct_size[xc_type]);
    if (!xc_regions) {
        LOGE(ERROR, "cannot allocate xc buffer for %d regions", nr);
        err = -ENOMEM;
        rc = ERROR_NOMEM;
        goto out;
    }

    err = xc_nvdimm_pmem_get_regions(ctx->xch, xc_type, xc_regions, &nr);
    if (err) {
        LOGE(ERROR, "cannot get information of PMEM regions of type %d, err %d",
             type, err);
        rc = ERROR_FAIL;
        goto out;
    }

    regions = libxl__malloc(NOGC, sizeof(*regions) * nr);
    if (!regions) {
        LOGE(ERROR, "cannot allocate return buffer for %d regions", nr);
        err = -ENOMEM;
        rc = ERROR_NOMEM;
        goto out;
    }
    copy_from_xc_regions(regions, xc_regions, xc_type, nr);

    *regions_r = regions;
    *nr_r = nr;

 out:
    GC_FREE;

    if (rc)
        errno = -err;

    return rc;
}

int libxl_nvdimm_pmem_setup_mgmt(libxl_ctx *ctx,
                                 unsigned long smfn, unsigned long emfn)
{
    int rc = xc_nvdimm_pmem_setup_mgmt(ctx->xch, smfn, emfn);

    if (rc)
        errno = -rc;

    return errno ? ERROR_FAIL : 0;
}

int libxl_nvdimm_pmem_setup_data(libxl_ctx *ctx,
                                 unsigned long data_smfn, unsigned data_emfn,
                                 unsigned long mgmt_smfn, unsigned mgmt_emfn)
{
    int rc = xc_nvdimm_pmem_setup_data(ctx->xch, data_smfn, data_emfn,
                                       mgmt_smfn, mgmt_emfn);

    if (rc)
        errno = -rc;

    return errno ? ERROR_FAIL : 0;
}
