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
** FILE NAME:       tape_drivers/osx/iokit/iokit_scsi_operations.h
**
** DESCRIPTION:     Header file for raw SCSI operations in user-space
**
** AUTHORS:         Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
**                  Michael A. Richmond
**                  IBM Almaden Research Center
**                  mar@almaden.ibm.com
**
*************************************************************************************
*/


#ifndef __iokit_scsi_operations_h
#define __iokit_scsi_operations_h

#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOTypes.h>
#include <IOKit/scsi/SCSITaskLib.h>
#include <IOKit/scsi/SCSICommandOperationCodes.h>

#include "libltfs/tape_ops.h"
#include "iokit_scsi_base.h"
#include "scsi_command_blocks.h"

typedef enum {
	mediaSet_All,
	mediaSet_BlockSizeOnly,
	mediaSet_BufferModeOnly,
	mediaSet_DensityCodeOnly
} media_modify_value;


typedef enum {
	positionSet_SpaceFileMark,
	positionSet_SpaceRecord,
	positionSet_SpaceEOM,
	positionSet_LocateBlock,
	positionSet_LocateBlockImmediate,
	positionSet_SpaceSequentialFM
} position_set_method;

int32_t iokit_test_unit_ready( iokit_device_t *device );

int32_t iokit_reserve_unit( iokit_device_t *device );

int32_t iokit_release_unit( iokit_device_t *device );

int32_t iokit_reserve_unit( iokit_device_t *device );

int32_t iokit_prevent_medium_removal( iokit_device_t *device, uint8_t prevent_medium_removal );

int32_t iokit_load_unload( iokit_device_t *device, uint8_t immediate, uint8_t load );

int32_t iokit_read_position( iokit_device_t *device, pos_info *read_pos_info );

int32_t iokit_set_compression(iokit_device_t *device, boolean_t enable_compression);

int32_t iokit_set_media_parameters(iokit_device_t *device, media_modify_value modification, all_device_params *params);

int32_t iokit_get_media_parameters(iokit_device_t *device, all_device_params *params);

int32_t iokit_update_block_limits(iokit_device_t *device);

// TODO: Replace struct tc_position with driver-internal structure.
int32_t iokit_set_position(iokit_device_t *device, position_set_method method, size_t count, struct tc_position *pos);

int32_t iokit_rewind( iokit_device_t *device, uint8_t immediate );

int32_t iokit_erase( iokit_device_t *device, uint8_t long_bit, uint8_t immediate );

int32_t iokit_write( iokit_device_t *device, uint8_t *buffer, size_t buffer_size, bool *ew, bool *pew );

int32_t iokit_write_filemark( iokit_device_t *device, size_t count, uint8_t immediate );

int32_t iokit_log_sense_page( iokit_device_t *device, uint8_t page_code, uint8_t subpage, uint8_t *logsense_buffer, size_t logsense_buffer_size );

int32_t iokit_report_density( iokit_device_t *device, uint8_t media,
		                      uint8_t *report_count, report_density *report_buffer );

int32_t iokit_inquiry( iokit_device_t *device, uint8_t page, uint8_t *buf, size_t size );

int32_t iokit_std_inquiry( iokit_device_t *device,
						   uint8_t *buf, size_t size );

int32_t iokit_request_sense( iokit_device_t *device, char *sense_buf, int size);

int32_t iokit_identify_device_code( iokit_device_t *iokit_device, scsi_device_identifier *identifier );

#endif // __iokit_scsi_operations_h
