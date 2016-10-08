/*
 * tools/libacpi/aml_build.h
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

#ifndef _AML_BUILD_H_
#define _AML_BUILD_H_

#include <stdint.h>
#include "libacpi.h"

/*
 * NB: All aml_prepend_* calls, which build AML code in one ACPI
 *     table, should be placed between a pair of calls to
 *     aml_build_begin() and aml_build_end().
 */

/**
 * Reset the AML builder and begin a new round of building.
 *
 * Parameters:
 *   @ctxt: ACPI context used by the AML builder
 *
 * Returns:
 *   a pointer to the builder buffer where the AML code will be stored
 */
void *aml_build_begin(struct acpi_ctxt *ctxt);

/**
 * Mark the end of a round of AML building.
 *
 * Returns:
 *  the number of bytes in the builder buffer built in this round
 */
uint32_t aml_build_end(void);

/**
 * Prepend a blob, which can contain arbitrary content, to the builder buffer.
 *
 * Parameters:
 *   @buf:    pointer to the builder buffer
 *   @blob:   pointer to the blob
 *   @length: the number of bytes in the blob
 */
void aml_prepend_blob(uint8_t *buf, const void *blob, uint32_t length);

/**
 * Prepend an AML device structure to the builder buffer. The existing
 * data in the builder buffer is included in the AML device.
 *
 * Parameters:
 *   @buf:  pointer to the builder buffer
 *   @name: the name of the device
 */
void aml_prepend_device(uint8_t *buf, const char *name);

/**
 * Prepend an AML scope structure to the builder buffer. The existing
 * data in the builder buffer is included in the AML scope.
 *
 * Parameters:
 *   @buf:  pointer to the builder buffer
 *   @name: the name of the scope
 */
void aml_prepend_scope(uint8_t *buf, const char *name);

#endif /* _AML_BUILD_H_ */
