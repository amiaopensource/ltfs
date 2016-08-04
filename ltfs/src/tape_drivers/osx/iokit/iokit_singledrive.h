/*
**  %Z% %I% %W% %G% %U%
**
**  ZZ_Copyright_BEGIN
**
**
**  Licensed Materials - Property of IBM
**
**  IBM Linear Tape File System Single Drive Edition Version 2.2.0.2 for Linux and Mac OS X
**
**  Copyright IBM Corp. 2010, 2014
**
**  This file is part of the IBM Linear Tape File System Single Drive Edition for Linux and Mac OS X
**  (formally known as IBM Linear Tape File System)
**
**  The IBM Linear Tape File System Single Drive Edition for Linux and Mac OS X is free software;
**  you can redistribute it and/or modify it under the terms of the GNU Lesser
**  General Public License as published by the Free Software Foundation,
**  version 2.1 of the License.
**
**  The IBM Linear Tape File System Single Drive Edition for Linux and Mac OS X is distributed in the
**  hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
**  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
**  See the GNU Lesser General Public License for more details.
**
**  You should have received a copy of the GNU Lesser General Public
**  License along with this library; if not, write to the Free Software
**  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
**  or download the license from <http://www.gnu.org/licenses/>.
**
**
**  ZZ_Copyright_END
**
*************************************************************************************
**
** COMPONENT NAME:  IBM Linear Tape File System
**
** FILE NAME:       tape_drivers/osx/iokit/iokit_singledrive.h
**
** DESCRIPTION:     Header file for LTFS backend function prototypes
**
** AUTHOR:          Michael A. Richmond
**                  IBM Almaden Research Center
**                  mar@almaden.ibm.com
**
*************************************************************************************
*/


#include "libltfs/tape_ops.h"
#include "iokit_common.h"

#ifndef __iokit_singledrive_h
#define __iokit_singledrive_h

int32_t iokitosx_allow_medium_removal(void *device);

int32_t iokitosx_close(void *device);

int32_t iokitosx_erase(void *device, struct tc_position *pos, bool long_erase);

int32_t iokitosx_format(void *device, TC_FORMAT_TYPE format);

int32_t iokitosx_get_parameters(void *device, struct tc_drive_param *drive_param);

int32_t iokitosx_inquiry(void *device, struct tc_inq *inq);

int32_t iokitosx_inquiry_page(void *device, unsigned char page, struct tc_inq_page *inq);

int32_t iokitosx_load(void *device, struct tc_position *pos);

int32_t iokitosx_locate(void *device, struct tc_position dest,
				      struct tc_position *pos);

int32_t iokitosx_logsense_page(void *device, const uint8_t page, const uint8_t subpage,
							 uint8_t *buf, const size_t size);

int32_t iokitosx_logsense(void *device, const uint8_t page,
						uint8_t *buf, const size_t size);

int32_t iokitosx_modesense(void *device, const uint8_t page,
						 const TC_MP_PC_TYPE pc, const uint8_t subpage,
						 uint8_t *buf, const size_t size);

int32_t iokitosx_modeselect(void *device, uint8_t *buf, const size_t size);

int32_t iokitosx_open(const char *devname, void **handle);

int32_t iokitosx_prevent_medium_removal(void *device);

int32_t iokitosx_read(void *device, char *buf,
					size_t size, struct tc_position *pos,
					const bool unusual_size);

int32_t iokitosx_read_attribute(void *device, const tape_partition_t part,
							  const uint16_t id, uint8_t *buf, const size_t size);

int32_t iokitosx_readpos(void *device, struct tc_position *pos);

int32_t iokitosx_release_unit(void *device);

int32_t iokitosx_remaining_capacity(void *device, struct tc_remaining_cap *cap);

int32_t iokitosx_report_density(void *device, struct tc_density_report *rep, bool medium);

int32_t iokitosx_reserve_unit(void *device);

int32_t iokitosx_rewind(void *device, struct tc_position *pos);

int32_t iokitosx_space(void *device, size_t count, TC_SPACE_TYPE type,
					 struct tc_position *pos);

int32_t iokitosx_set_compression(void *device, const bool enable_compression,
							   struct tc_position *pos);

int32_t iokitosx_set_default(void *device);

int32_t iokitosx_test_unit_ready(void *device);

int32_t iokitosx_unload(void *device, struct tc_position *pos);

int32_t iokitosx_write(void *device, const char *buf,
					 size_t count, struct tc_position *pos);

int32_t iokitosx_write_attribute(void *device, const tape_partition_t part,
							   const uint8_t *buf, const size_t size);

int32_t iokitosx_writefm(void *device, size_t count, struct tc_position *pos, bool immed);

#endif // __iokit_singledrive_h
