/*
 * xen-ndctl.c
 *
 * Xen NVDIMM management tool
 *
 * Copyright (C) 2017,  Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xenctrl.h>

static xc_interface *xch;

static int handle_help(int argc, char *argv[]);
static int handle_list(int argc, char *argv[]);
static int handle_list_cmds(int argc, char *argv[]);
static int handle_setup_mgmt(int argc, char *argv[]);
static int handle_setup_data(int argc, char *argv[]);

static const struct xen_ndctl_cmd
{
    const char *name;
    const char *syntax;
    const char *help;
    int (*handler)(int argc, char **argv);
    bool need_xc;
} cmds[] =
{
    {
        .name    = "help",
        .syntax  = "[command]",
        .help    = "Show this message or the help message of 'command'.\n"
                   "Use command 'list-cmds' to list all supported commands.\n",
        .handler = handle_help,
    },

    {
        .name    = "list",
        .syntax  = "[--all | --raw | --mgmt | --data]",
        .help    = "--all: the default option, list all PMEM regions of following types.\n"
                   "--raw: list all PMEM regions detected by Xen hypervisor.\n"
                   "--mgmt: list all PMEM regions for management usage.\n"
                   "--data: list all PMEM regions that can be mapped to guest.\n",
        .handler = handle_list,
        .need_xc = true,
    },

    {
        .name    = "list-cmds",
        .syntax  = "",
        .help    = "List all supported commands.\n",
        .handler = handle_list_cmds,
    },

    {
        .name    = "setup-data",
        .syntax  = "<smfn> <emfn> <mgmt_smfn> <mgmt_emfn>",
        .help    = "Setup a PMEM region from MFN 'smfn' to 'emfn' for guest data usage,\n"
                   "which can be used as the backend of the virtual NVDIMM devices.\n\n"
                   "PMEM pages from MFN 'mgmt_smfn' to 'mgmt_emfn' is used to manage\n"
                   "the above PMEM region, and should not overlap with MFN from 'smfn'\n"
                   "to 'emfn'.\n",
        .handler = handle_setup_data,
        .need_xc = true,
    },

    {
        .name    = "setup-mgmt",
        .syntax  = "<smfn> <emfn>",
        .help    = "Setup a PMEM region from MFN 'smfn' to 'emfn' for management usage.\n\n",
        .handler = handle_setup_mgmt,
        .need_xc = true,
    },
};

static const unsigned int nr_cmds = sizeof(cmds) / sizeof(cmds[0]);

static void show_help(const char *cmd)
{
    unsigned int i;

    if ( !cmd )
    {
        fprintf(stderr,
                "Usage: xen-ndctl <command> [args]\n\n"
                "List all supported commands by 'xen-ndctl list-cmds'.\n"
                "Get help of a command by 'xen-ndctl help <command>'.\n");
        return;
    }

    for ( i = 0; i < nr_cmds; i++ )
        if ( !strcmp(cmd, cmds[i].name) )
        {
            fprintf(stderr, "Usage: xen-ndctl %s %s\n\n%s",
                    cmds[i].name, cmds[i].syntax, cmds[i].help);
            break;
        }

    if ( i == nr_cmds )
        fprintf(stderr, "Unsupported command '%s'.\n"
                "List all supported commands by 'xen-ndctl list-cmds'.\n",
                cmd);
}

static int handle_unrecognized_argument(const char *cmd, const char *argv)
{
    fprintf(stderr, "Unrecognized argument: %s.\n\n", argv);
    show_help(cmd);

    return -EINVAL;
}

static int handle_help(int argc, char *argv[])
{
    if ( argc == 1 )
        show_help(NULL);
    else if ( argc == 2 )
        show_help(argv[1]);
    else
        return handle_unrecognized_argument(argv[0], argv[2]);

    return 0;
}

static int handle_list_raw(void)
{
    int rc;
    unsigned int nr = 0, i;
    xen_sysctl_nvdimm_pmem_raw_region_t *raw_list;

    rc = xc_nvdimm_pmem_get_regions_nr(xch, PMEM_REGION_TYPE_RAW, &nr);
    if ( rc )
    {
        fprintf(stderr, "Cannot get the number of PMEM regions: %s.\n",
                strerror(-rc));
        return rc;
    }

    raw_list = malloc(nr * sizeof(*raw_list));
    if ( !raw_list )
        return -ENOMEM;

    rc = xc_nvdimm_pmem_get_regions(xch, PMEM_REGION_TYPE_RAW, raw_list, &nr);
    if ( rc )
        goto out;

    printf("Raw PMEM regions:\n");
    for ( i = 0; i < nr; i++ )
        printf(" %u: MFN 0x%lx - 0x%lx, PXM %u\n",
               i, raw_list[i].smfn, raw_list[i].emfn, raw_list[i].pxm);

 out:
    free(raw_list);

    return rc;
}

static int handle_list_mgmt(void)
{
    int rc;
    unsigned int nr = 0, i;
    xen_sysctl_nvdimm_pmem_mgmt_region_t *mgmt_list;

    rc = xc_nvdimm_pmem_get_regions_nr(xch, PMEM_REGION_TYPE_MGMT, &nr);
    if ( rc )
    {
        fprintf(stderr, "Cannot get the number of PMEM regions: %s.\n",
                strerror(-rc));
        return rc;
    }

    mgmt_list = malloc(nr * sizeof(*mgmt_list));
    if ( !mgmt_list )
        return -ENOMEM;

    rc = xc_nvdimm_pmem_get_regions(xch, PMEM_REGION_TYPE_MGMT, mgmt_list, &nr);
    if ( rc )
        goto out;

    printf("Management PMEM regions:\n");
    for ( i = 0; i < nr; i++ )
        printf(" %u: MFN 0x%lx - 0x%lx, used 0x%lx\n",
               i, mgmt_list[i].smfn, mgmt_list[i].emfn, mgmt_list[i].used_mfns);

 out:
    free(mgmt_list);

    return rc;
}

static int handle_list_data(void)
{
    int rc;
    unsigned int nr = 0, i;
    xen_sysctl_nvdimm_pmem_data_region_t *data_list;

    rc = xc_nvdimm_pmem_get_regions_nr(xch, PMEM_REGION_TYPE_DATA, &nr);
    if ( rc )
    {
        fprintf(stderr, "Cannot get the number of PMEM regions: %s.\n",
                strerror(-rc));
        return rc;
    }

    data_list = malloc(nr * sizeof(*data_list));
    if ( !data_list )
        return -ENOMEM;

    rc = xc_nvdimm_pmem_get_regions(xch, PMEM_REGION_TYPE_DATA, data_list, &nr);
    if ( rc )
        goto out;

    printf("Data PMEM regions:\n");
    for ( i = 0; i < nr; i++ )
        printf(" %u: MFN 0x%lx - 0x%lx, MGMT MFN 0x%lx - 0x%lx\n",
               i, data_list[i].smfn, data_list[i].emfn,
               data_list[i].mgmt_smfn, data_list[i].mgmt_emfn);

 out:
    free(data_list);

    return rc;
}

static const struct list_handlers {
    const char *option;
    int (*handler)(void);
} list_hndrs[] =
{
    { "--raw", handle_list_raw },
    { "--mgmt", handle_list_mgmt },
    { "--data", handle_list_data },
};

static const unsigned int nr_list_hndrs =
    sizeof(list_hndrs) / sizeof(list_hndrs[0]);

static int handle_list(int argc, char *argv[])
{
    bool list_all = argc <= 1 || !strcmp(argv[1], "--all");
    unsigned int i;
    bool handled = false;
    int rc = 0;

    for ( i = 0; i < nr_list_hndrs && !rc; i++)
        if ( list_all || !strcmp(argv[1], list_hndrs[i].option) )
        {
            rc = list_hndrs[i].handler();
            handled = true;
        }

    if ( !handled )
        return handle_unrecognized_argument(argv[0], argv[1]);

    return rc;
}

static int handle_list_cmds(int argc, char *argv[])
{
    unsigned int i;

    if ( argc > 1 )
        return handle_unrecognized_argument(argv[0], argv[1]);

    for ( i = 0; i < nr_cmds; i++ )
        fprintf(stderr, "%s\n", cmds[i].name);

    return 0;
}

static bool string_to_mfn(const char *str, unsigned long *ret)
{
    unsigned long l;

    errno = 0;
    l = strtoul(str, NULL, 0);

    if ( !errno )
        *ret = l;
    else
        fprintf(stderr, "Invalid MFN %s: %s\n", str, strerror(errno));

    return !errno;
}

static int handle_setup_mgmt(int argc, char **argv)
{
    unsigned long smfn, emfn;

    if ( argc < 3 )
    {
        fprintf(stderr, "Too few arguments.\n\n");
        show_help(argv[0]);
        return -EINVAL;
    }

    if ( !string_to_mfn(argv[1], &smfn) ||
         !string_to_mfn(argv[2], &emfn) )
        return -EINVAL;

    if ( argc > 3 )
        return handle_unrecognized_argument(argv[0], argv[3]);

    return xc_nvdimm_pmem_setup_mgmt(xch, smfn, emfn);
}

static int handle_setup_data(int argc, char **argv)
{
    unsigned long smfn, emfn, mgmt_smfn, mgmt_emfn;

    if ( argc < 5 )
    {
        fprintf(stderr, "Too few arguments.\n\n");
        show_help(argv[0]);
        return -EINVAL;
    }

    if ( !string_to_mfn(argv[1], &smfn) ||
         !string_to_mfn(argv[2], &emfn) ||
         !string_to_mfn(argv[3], &mgmt_smfn) ||
         !string_to_mfn(argv[4], &mgmt_emfn) )
        return -EINVAL;

    if ( argc > 5 )
        return handle_unrecognized_argument(argv[0], argv[5]);

    return xc_nvdimm_pmem_setup_data(xch, smfn, emfn, mgmt_smfn, mgmt_emfn);
}

int main(int argc, char *argv[])
{
    unsigned int i;
    int rc = 0;
    const char *cmd;

    if ( argc <= 1 )
    {
        show_help(NULL);
        return 0;
    }

    cmd = argv[1];

    for ( i = 0; i < nr_cmds; i++ )
        if ( !strcmp(cmd, cmds[i].name) )
        {
            if ( cmds[i].need_xc )
            {
                xch = xc_interface_open(0, 0, 0);
                if ( !xch )
                {
                    rc = -errno;
                    fprintf(stderr, "Cannot get xc handler: %s\n",
                            strerror(errno));
                    break;
                }
            }
            rc = cmds[i].handler(argc - 1, &argv[1]);
            if ( rc )
                fprintf(stderr, "\n'%s' failed: %s\n",
                        cmds[i].name, strerror(-rc));
            break;
        }

    if ( i == nr_cmds )
    {
        fprintf(stderr, "Unsupported command '%s'. "
                "List all supported commands by 'xen-ndctl list-cmds'.\n",
                cmd);
        rc = -ENOSYS;
    }

    if ( xch )
        xc_interface_close(xch);

    return rc;
}
