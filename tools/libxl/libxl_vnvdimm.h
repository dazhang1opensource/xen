/*
 * tools/libxl/libxl_vnvdimm.h
 *
 * Copyright (C) 2017,  Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License, version 2.1, as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBXL_VNVDIMM_H
#define LIBXL_VNVDIMM_H

#include <stdint.h>
#include "libxl_internal.h"

#if defined(__linux__)
int libxl_vnvdimm_add_pages(libxl__gc *gc, uint32_t domid,
                            xen_pfn_t mfn, xen_pfn_t gpfn, xen_pfn_t nr_pages);
#endif /* __linux__ */

#endif /* !LIBXL_VNVDIMM_H */
