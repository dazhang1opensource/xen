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
#include <libxlutil.h>

#include "xl.h"
#include "xl_parse.h"
#include "xl_utils.h"

typedef void (*show_region_fn_t)(libxl_nvdimm_pmem_region *region,
                                 unsigned int idx);

static void show_raw_region(libxl_nvdimm_pmem_region *region, unsigned int idx)
{
    libxl_nvdimm_pmem_raw_region *raw = &region->u.raw;

    printf(" %u: mfn 0x%lx - 0x%lx, pxm %u\n",
           idx, raw->smfn, raw->emfn, raw->pxm);
}

static void show_mgmt_region(libxl_nvdimm_pmem_region *region, unsigned int idx)
{
    libxl_nvdimm_pmem_mgmt_region *mgmt = &region->u.mgmt;

    printf(" %u: mfn 0x%lx - 0x%lx, used 0x%lx pages\n",
           idx, mgmt->smfn, mgmt->emfn, mgmt->used);
}

static show_region_fn_t show_region_fn[] = {
    [LIBXL_NVDIMM_PMEM_REGION_TYPE_RAW] = show_raw_region,
    [LIBXL_NVDIMM_PMEM_REGION_TYPE_MGMT] = show_mgmt_region,
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
        { "mgmt", 0, 0, 'm' },
        COMMON_LONG_OPTS
    };

    bool all = true, raw = false, mgmt = false;
    int opt, ret = 0;

    SWITCH_FOREACH_OPT(opt, "rm", opts, "pmem-list", 0) {
    case 'r':
        all = false;
        raw = true;
        break;

    case 'm':
        all = false;
        mgmt = true;
        break;
    }

    if (all || raw)
        ret = list_regions(LIBXL_NVDIMM_PMEM_REGION_TYPE_RAW);

    if (!ret && (all || mgmt))
        ret = list_regions(LIBXL_NVDIMM_PMEM_REGION_TYPE_MGMT);

    return ret;
}

int main_pmem_setup(int argc, char **argv)
{
    static struct option opts[] = {
        { "mgmt", 1, 0, 'm' },
        COMMON_LONG_OPTS
    };

    bool mgmt = false;
    unsigned long mgmt_smfn, mgmt_emfn;
    int opt, rc = 0;

#define CHECK_NR_ARGS(expected, option)                                 \
    do {                                                                \
        if (argc + 1 != optind + (expected)) {                          \
            fprintf(stderr,                                             \
                    "Error: 'xl pmem-setup %s' requires %u arguments\n\n", \
                    (option), (expected));                              \
            help("pmem-setup");                                         \
                                                                        \
            rc = ERROR_INVAL;                                           \
            errno = EINVAL;                                             \
                                                                        \
            goto out;                                                   \
        }                                                               \
    } while (0)

    SWITCH_FOREACH_OPT(opt, "m:", opts, "pmem-setup", 0) {
    case 'm':
        CHECK_NR_ARGS(2, "-m");

        mgmt = true;
        mgmt_smfn = parse_ulong(optarg);
        mgmt_emfn = parse_ulong(argv[optind]);

        break;
    }

#undef CHECK_NR_ARGS

    if (mgmt)
        rc = libxl_nvdimm_pmem_setup_mgmt(ctx, mgmt_smfn, mgmt_emfn);

 out:
    if (rc)
        fprintf(stderr, "Error: pmem-setup failed, %s\n", strerror(errno));

    return rc;
}
