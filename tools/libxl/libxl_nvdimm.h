/*
 * tools/libxl/libxl_nvdimm.h
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

#ifndef LIBXL_NVDIMM_H
#define LIBXL_NVDIMM_H

#include <stdint.h>
#include "libxl_internal.h"

#if defined(__linux__)

int libxl_nvdimm_add_device(libxl__gc *gc,
                            uint32_t domid, const char *path,
                            uint64_t spa, uint64_t length);

#else

int libxl_nvdimm_add_device(libxl__gc *gc,
                            uint32_t domid, const char *path,
                            uint64_t spa, uint64_t length)
{
    return -EINVAL;
}

#endif /* __linux__ */

#endif /* !LIBXL_NVDIMM_H */
