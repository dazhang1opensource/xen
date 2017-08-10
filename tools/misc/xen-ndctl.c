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
        .syntax  = "[--all | --raw ]",
        .help    = "--all: the default option, list all PMEM regions of following types.\n"
                   "--raw: list all PMEM regions detected by Xen hypervisor.\n",
        .handler = handle_list,
        .need_xc = true,
    },

    {
        .name    = "list-cmds",
        .syntax  = "",
        .help    = "List all supported commands.\n",
        .handler = handle_list_cmds,
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

static const struct list_handlers {
    const char *option;
    int (*handler)(void);
} list_hndrs[] =
{
    { "--raw", handle_list_raw },
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
