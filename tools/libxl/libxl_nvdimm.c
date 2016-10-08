/*
 * tools/libxl/libxl_nvdimm.c
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

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <linux/fiemap.h>

#include "libxl_internal.h"
#include "libxl_arch.h"
#include "libxl_nvdimm.h"

#include <xc_dom.h>

#define BLK_DEVICE_ROOT "/sys/dev/block"

static int nvdimm_sysfs_read(libxl__gc *gc,
                             unsigned int major, unsigned int minor,
                             const char *name, void **data_r)
{
    char *path = libxl__sprintf(gc, BLK_DEVICE_ROOT"/%u:%u/device/%s",
                                major, minor, name);
    return libxl__read_sysfs_file_contents(gc, path, data_r, NULL);
}

static int nvdimm_get_spa(libxl__gc *gc, unsigned int major, unsigned int minor,
                          uint64_t *spa_r)
{
    void *data;
    int ret = nvdimm_sysfs_read(gc, major, minor, "resource", &data);

    if ( ret )
        return ret;

    *spa_r = strtoll(data, NULL, 0);
    return 0;
}

static int nvdimm_get_size(libxl__gc *gc, unsigned int major, unsigned int minor,
                           uint64_t *size_r)
{
    void *data;
    int ret = nvdimm_sysfs_read(gc, major, minor, "size", &data);

    if ( ret )
        return ret;

    *size_r = strtoll(data, NULL, 0);

    return 0;
}

static int add_pages(libxl__gc *gc, uint32_t domid,
                     xen_pfn_t mfn, xen_pfn_t gpfn, unsigned long nr_mfns)
{
    unsigned int nr;
    int ret = 0;

    while ( nr_mfns )
    {
        nr = min(nr_mfns, (unsigned long) UINT_MAX);

        ret = xc_domain_populate_pmemmap(CTX->xch, domid, mfn, gpfn, nr);
        if ( ret )
        {
            LOG(ERROR, "failed to map pmem pages, "
                "mfn 0x%" PRIx64", gpfn 0x%" PRIx64 ", nr_mfns %u, err %d",
                mfn, gpfn, nr, ret);
            break;
        }

        nr_mfns -= nr;
        mfn += nr;
        gpfn += nr;
    }

    return ret;
}

static uint64_t
get_file_extents(libxl__gc *gc, int fd, unsigned long length,
                 struct fiemap_extent **extents_r)
{
    struct fiemap *fiemap;
    uint64_t nr_extents = 0, extents_size;

    fiemap = libxl__zalloc(gc, sizeof(*fiemap));
    if ( !fiemap )
        goto out;

    fiemap->fm_length = length;
    if ( ioctl(fd, FS_IOC_FIEMAP, fiemap) < 0 )
        goto out;

    nr_extents = fiemap->fm_mapped_extents;
    extents_size = sizeof(struct fiemap_extent) * nr_extents;
    fiemap = libxl__realloc(gc, fiemap, sizeof(*fiemap) + extents_size);
    if ( !fiemap )
        goto out;

    memset(fiemap->fm_extents, 0, extents_size);
    fiemap->fm_extent_count = nr_extents;
    fiemap->fm_mapped_extents = 0;

    if ( ioctl(fd, FS_IOC_FIEMAP, fiemap) < 0 )
        goto out;

    *extents_r = fiemap->fm_extents;

 out:
    return nr_extents;
}

static int add_file(libxl__gc *gc, uint32_t domid, int fd,
                    xen_pfn_t mfn, xen_pfn_t gpfn, unsigned long nr_mfns)
{
    struct fiemap_extent *extents;
    uint64_t nr_extents, i;
    int ret = 0;

    nr_extents = get_file_extents(gc, fd, nr_mfns << XC_PAGE_SHIFT, &extents);
    if ( !nr_extents )
        return -EIO;

    for ( i = 0; i < nr_extents; i++ )
    {
        uint64_t p_offset = extents[i].fe_physical;
        uint64_t l_offset = extents[i].fe_logical;
        uint64_t length = extents[i].fe_length;

        if ( extents[i].fe_flags & ~FIEMAP_EXTENT_LAST )
        {
            ret = -EINVAL;
            break;
        }

        if ( (p_offset | l_offset | length) & ~XC_PAGE_MASK )
        {
            ret = -EINVAL;
            break;
        }

        ret = add_pages(gc, domid,
                        mfn + (p_offset >> XC_PAGE_SHIFT),
                        gpfn + (l_offset >> XC_PAGE_SHIFT),
                        length >> XC_PAGE_SHIFT);
        if ( ret )
            break;
    }

    return ret;
}

int libxl_nvdimm_add_device(libxl__gc *gc,
                            uint32_t domid, const char *path,
                            uint64_t guest_spa, uint64_t guest_size)
{
    int fd;
    struct stat st;
    unsigned int major, minor;
    uint64_t host_spa, host_size;
    xen_pfn_t mfn, gpfn;
    unsigned long nr_gpfns;
    int ret;

    if ( (guest_spa & ~XC_PAGE_MASK) || (guest_size & ~XC_PAGE_MASK) )
        return -EINVAL;

    fd = open(path, O_RDONLY);
    if ( fd < 0 )
    {
        LOG(ERROR, "failed to open file %s (err: %d)", path, errno);
        return -EIO;
    }

    ret = fstat(fd, &st);
    if ( ret )
    {
        LOG(ERROR, "failed to get status of file %s (err: %d)",
            path, errno);
        goto out;
    }

    switch ( st.st_mode & S_IFMT )
    {
    case S_IFBLK:
        major = major(st.st_rdev);
        minor = minor(st.st_rdev);
        break;

    case S_IFREG:
        major = major(st.st_dev);
        minor = minor(st.st_dev);
        break;

    default:
        LOG(ERROR, "%s is neither a block device nor a regular file", path);
        ret = -EINVAL;
        goto out;
    }

    ret = nvdimm_get_spa(gc, major, minor, &host_spa);
    if ( ret )
    {
        LOG(ERROR, "failed to get SPA of device %u:%u", major, minor);
        goto out;
    }
    else if ( host_spa & ~XC_PAGE_MASK )
    {
        ret = -EINVAL;
        goto out;
    }

    ret = nvdimm_get_size(gc, major, minor, &host_size);
    if ( ret )
    {
        LOG(ERROR, "failed to get size of device %u:%u", major, minor);
        goto out;
    }
    else if ( guest_size > host_size )
    {
        LOG(ERROR, "vNVDIMM size %" PRIu64 " expires NVDIMM size %" PRIu64,
            guest_size, host_size);
        ret = -EINVAL;
        goto out;
    }

    mfn = host_spa >> XC_PAGE_SHIFT;
    gpfn = guest_spa >> XC_PAGE_SHIFT;
    nr_gpfns = guest_size >> XC_PAGE_SHIFT;

    switch ( st.st_mode & S_IFMT )
    {
    case S_IFBLK:
        ret = add_pages(gc, domid, mfn, gpfn, nr_gpfns);
        break;

    case S_IFREG:
        ret = add_file(gc, domid, fd, mfn, gpfn, nr_gpfns);
        break;

    default:
        LOG(ERROR, "%s is neither a block device nor a regular file", path);
        ret = -EINVAL;
    }

 out:
    close(fd);
    return ret;
}

/*
 * Local variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
