/*
 * tools/xl/xl_nvdimm.c
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libxl.h>

#include "xl.h"
#include "xl_utils.h"

typedef void (*show_region_fn_t)(libxl_nvdimm_pmem_region *region,
                                 unsigned int idx);

static void show_raw_region(libxl_nvdimm_pmem_region *region, unsigned int idx)
{
    libxl_nvdimm_pmem_raw_region *raw = &region->u.raw;

    printf(" %u: mfn 0x%lx - 0x%lx, pxm %u\n",
           idx, raw->smfn, raw->emfn, raw->pxm);
}

static show_region_fn_t show_region_fn[] = {
    [LIBXL_NVDIMM_PMEM_REGION_TYPE_RAW] = show_raw_region,
};

static int list_regions(libxl_nvdimm_pmem_region_type type)
{
    int rc;
    libxl_nvdimm_pmem_region *regions = NULL;
    unsigned int nr, i;

    rc = libxl_nvdimm_pmem_get_regions(ctx, type, &regions, &nr);
    if (rc || !nr)
        goto out;

    printf("List of %s PMEM regions:\n",
           libxl_nvdimm_pmem_region_type_to_string(type));
    for (i = 0; i < nr; i++)
        show_region_fn[type](&regions[i], i);

 out:
    if (regions)
        free(regions);

    if (rc)
        fprintf(stderr, "Error: pmem-list failed: %s\n", strerror(errno));

    return rc;
}

int main_pmem_list(int argc, char **argv)
{
    static struct option opts[] = {
        { "raw", 0, 0, 'r' },
        COMMON_LONG_OPTS
    };

    bool all = true, raw = false;
    int opt, ret = 0;

    SWITCH_FOREACH_OPT(opt, "r", opts, "pmem-list", 0) {
    case 'r':
        all = false;
        raw = true;
        break;
    }

    if (all || raw)
        ret = list_regions(LIBXL_NVDIMM_PMEM_REGION_TYPE_RAW);

    return ret;
}