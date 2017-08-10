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
#include <string.h>
#include <xenctrl.h>

static xc_interface *xch;

static int handle_help(int argc, char *argv[]);
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
