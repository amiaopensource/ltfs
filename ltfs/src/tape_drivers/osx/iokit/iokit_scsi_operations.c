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
** FILE NAME:       tape_drivers/osx/iokit/iokit_scsi_operations.c
**
** DESCRIPTION:     Implements raw SCSI operations in user-space
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


#include <inttypes.h>

#include "libltfs/ltfslogging.h"
#include "libltfs/ltfs_endian.h"

#include "device_identifiers.h"
#include "iokit_scsi_base.h"
#include "iokit_scsi_operations.h"
#include "tape_timeout.h"
#include "scsi_command_blocks.h"
#include "scsi_command_operation_codes.h"
#include "scsi_mode_pages.h"
#include "iokit_common.h"

struct supported_device *supported_devices[] = {
		DEVICE( IBM_VENDOR_ID, "ULTRIUM-TD5     ", IBM_3580, DRIVE_LTO5,    " [ULTRIUM-TD5] " ), /* IBM Ultrium Gen 5 */
		DEVICE( IBM_VENDOR_ID, "ULT3580-TD5     ", IBM_3580, DRIVE_LTO5,    " [ULT3580-TD5] " ), /* IBM Ultrium Gen 5 */
		DEVICE( IBM_VENDOR_ID, "ULTRIUM-HH5     ", IBM_3580, DRIVE_LTO5_HH, " [ULTRIUM-HH5] " ), /* IBM Ultrium Gen 5 Half-High */
		DEVICE( IBM_VENDOR_ID, "ULT3580-HH5     ", IBM_3580, DRIVE_LTO5_HH, " [ULT3580-HH5] " ), /* IBM Ultrium Gen 5 Half-High */
		DEVICE( IBM_VENDOR_ID, "HH LTO Gen 5    ", IBM_3580, DRIVE_LTO5_HH, " [HH LTO Gen 5]" ), /* IBM Ultrium Gen 5 Half-High */
		DEVICE( IBM_VENDOR_ID, "ULTRIUM-TD6     ", IBM_3580, DRIVE_LTO6,    " [ULTRIUM-TD6] " ), /* IBM Ultrium Gen 6 */
		DEVICE( IBM_VENDOR_ID, "ULT3580-TD6     ", IBM_3580, DRIVE_LTO6,    " [ULT3580-TD6] " ), /* IBM Ultrium Gen 6 */
		DEVICE( IBM_VENDOR_ID, "ULTRIUM-HH6     ", IBM_3580, DRIVE_LTO6_HH, " [ULTRIUM-HH6] " ), /* IBM Ultrium Gen 6 Half-High */
		DEVICE( IBM_VENDOR_ID, "ULT3580-HH6     ", IBM_3580, DRIVE_LTO6_HH, " [ULT3580-HH6] " ), /* IBM Ultrium Gen 6 Half-High */
		DEVICE( IBM_VENDOR_ID, "HH LTO Gen 6    ", IBM_3580, DRIVE_LTO6_HH, " [HH LTO Gen 6]" ), /* IBM Ultrium Gen 6 Half-High */
		DEVICE( IBM_VENDOR_ID, "03592E07        ", IBM_3592, DRIVE_TS1140,  " [03592E07]    " ), /* IBM TS1140 */
		DEVICE( IBM_VENDOR_ID, "03592E08        ", IBM_3592, DRIVE_TS1150,  " [03592E08]    " ), /* IBM TS1150 */
		/* End of supported_devices */
		NULL
};

int32_t iokit_test_unit_ready( iokit_device_t *device )
{
	int32_t ret = -1;

	char command_description[COMMAND_DESCRIPTION_LENGTH] = "TEST_UNIT_READY";
	cdb_test_unit_ready cdb;
	uint8_t cdb_length;

	uint8_t *buffer;
	size_t buffer_size;
	uint8_t transfer_direction;
	char *msg;
	bool print_msg = true, take_dump = true;

	assert(device->scsiTaskInterface != NULL);

	// Setup buffer
	buffer = NULL;
	buffer_size = 0;

	// Zero out and setup CDB
	bzero(&cdb, sizeof( cdb ));
	// The TEST_UNIT_READY code consists of all zeroes so it is
	// not necessary to set any additional values in the CDB.
	// For completeness we will explicitly set the command.
	// Set the actual CDB in the task
	cdb.op_code = kSCSICmd_TEST_UNIT_READY;  /* 0x00 SCSI test unit read code */
	cdb_length = kSCSICDBSize_6Byte;

	transfer_direction = kSCSIDataTransfer_NoDataTransfer;

	ret = iokit_issue_cdb_command(device, (uint8_t *)&cdb, cdb_length,
						    buffer, buffer_size, transfer_direction,
						    ComputeTimeOut(device->device_code, TestUnitReadyTimeOut),
						    command_description, &msg);

	if(ret != DEVICE_GOOD) {
		switch (ret) {
			case -EDEV_NEED_INITIALIZE:
			case -EDEV_CONFIGURE_CHANGED:
				print_msg = false;
				/* fall throuh */
			case -EDEV_NO_MEDIUM:
			case -EDEV_BECOMING_READY:
			case -EDEV_MEDIUM_MAY_BE_CHANGED:
			case -EDEV_NOT_READY:
			case -EDEV_NOT_REPORTABLE:
			case -EDEV_MEDIUM_REMOVAL_REQ:
			case -EDEV_CLEANING_IN_PROGRESS:
				take_dump = false;
				break;
			default:
				break;
		}

		if (print_msg) {
			iokitosx_process_errors(device, ret, msg, command_description, take_dump);
		}
	}

out:
	return ret;
}

int32_t iokit_reserve_unit( iokit_device_t *device )
{
	int32_t ret = -1;

	char command_description[COMMAND_DESCRIPTION_LENGTH] = "RESERVE_UNIT";
	cdb_reserve cdb;
	uint8_t cdb_length;

	uint8_t *buffer;
	size_t buffer_size;
	uint8_t transfer_direction;
	char *msg;

	assert(device->scsiTaskInterface != NULL);

	// Setup buffer
	buffer = NULL;
	buffer_size = 0;

	// Zero out and setup CDB
	bzero(&cdb, sizeof( cdb ));
	cdb.op_code = kSCSICmd_RESERVE_6;  /* 0x16 SCSI reserve (6) code */
	cdb_length = kSCSICDBSize_6Byte;

	transfer_direction = kSCSIDataTransfer_NoDataTransfer;

	ret = iokit_issue_cdb_command(device, (uint8_t *)&cdb, cdb_length,
						    buffer, buffer_size, transfer_direction,
						    ComputeTimeOut(device->device_code, ReserveTimeOut),
						    command_description, &msg);

	if(ret != DEVICE_GOOD)
		iokitosx_process_errors(device, ret, msg, command_description, true);

out:
	return ret;
}


int32_t iokit_release_unit( iokit_device_t *device )
{
	int32_t ret = -1;

	char command_description[COMMAND_DESCRIPTION_LENGTH] = "RELEASE_UNIT";
	cdb_release cdb;
	uint8_t cdb_length;

	uint8_t *buffer;
	size_t buffer_size;
	uint8_t transfer_direction;
	char *msg;
	bool take_dump = true;

	assert(device->scsiTaskInterface != NULL);

	// Setup buffer
	buffer = NULL;
	buffer_size = 0;

	// Zero out and setup CDB
	bzero(&cdb, sizeof( cdb ));
	cdb.op_code = kSCSICmd_RELEASE_6;  /* 0x17 SCSI release (6) code */
	cdb_length = kSCSICDBSize_6Byte;

	transfer_direction = kSCSIDataTransfer_NoDataTransfer;

	ret = iokit_issue_cdb_command(device, (uint8_t *)&cdb, cdb_length,
						    buffer, buffer_size, transfer_direction,
						    ComputeTimeOut(device->device_code, ReleaseTimeOut),
						    command_description, &msg);

	if (ret != DEVICE_GOOD) {
		switch (ret) {
		case -EDEV_POR_OR_BUS_RESET:
			take_dump = false;
			break;
		default:
			break;
		}
		iokitosx_process_errors(device, ret, msg, command_description, take_dump);
	}

out:
	return ret;
}


int32_t iokit_prevent_medium_removal( iokit_device_t *device, uint8_t prevent_medium_removal )
{
	int32_t ret = -1;

	char command_description[COMMAND_DESCRIPTION_LENGTH] = "PREVENT/ALLOW_MEDIUM_REMOVAL";
	cdb_medium_removal cdb;
	uint8_t cdb_length;

	uint8_t *buffer;
	size_t buffer_size;
	uint8_t transfer_direction;
	char *msg;

	assert(device->scsiTaskInterface != NULL);

	// Setup buffer
	buffer = NULL;
	buffer_size = 0;

	// Zero out and setup cdb
	bzero(&cdb, sizeof(cdb));
	cdb.op_code = kSCSICmd_PREVENT_ALLOW_MEDIUM_REMOVAL;  /* 0x1E SCSI prevent allow medium removal code */
	cdb.prevent = prevent_medium_removal;
	cdb_length = kSCSICDBSize_6Byte;

	transfer_direction = kSCSIDataTransfer_NoDataTransfer;

	ret = iokit_issue_cdb_command(device, (void *)&cdb, cdb_length,
								  buffer, buffer_size, transfer_direction,
								  ComputeTimeOut(device->device_code, PreventAllowMediaTimeOut),
								  command_description, &msg);

	if (ret != DEVICE_GOOD) {
		iokitosx_process_errors(device, ret, msg, command_description, true);
	}

out:
	return ret;
}


int32_t iokit_load_unload( iokit_device_t *device, uint8_t immediate, uint8_t load )
{
	int32_t ret = -1;

	char command_description[COMMAND_DESCRIPTION_LENGTH] = "LOAD_UNLOAD_MEDIUM";
	cdb_load_unload cdb;
	uint8_t cdb_length;

	uint8_t *buffer;
	size_t buffer_size;
	uint8_t transfer_direction;
	char *msg;
	bool take_dump = true;

	assert(device->scsiTaskInterface != NULL);

	// Setup buffer
	buffer = NULL;
	buffer_size = 0;

	// Zero out and setup CDB
	bzero(&cdb, sizeof( cdb ));
	cdb.op_code = kSCSICmd_LOAD_UNLOAD;  /* 0x1B SCSI load unload code */
	cdb.immediate = immediate;
	cdb.load = load;
	cdb_length = kSCSICDBSize_6Byte;

	transfer_direction = kSCSIDataTransfer_NoDataTransfer;

	ret = iokit_issue_cdb_command(device, (uint8_t *)&cdb, cdb_length,
						    buffer, buffer_size, transfer_direction,
						    ComputeTimeOut(device->device_code, (load == true) ? (LoadTimeOut) : (UnloadTimeOut)),
						    command_description, &msg);

	if (!load && ret == -EDEV_CLEANING_REQUIRED) {
		ltfsmsg(LTFS_INFO, "12109I");
		ret = DEVICE_GOOD;
	}
	if (ret != DEVICE_GOOD) {
		device->eom = false;
		device->eom_block_id = 0;

		switch (ret) {
			case -EDEV_NO_MEDIUM:
			case -EDEV_BECOMING_READY:
			case -EDEV_MEDIUM_MAY_BE_CHANGED:
				take_dump = false;
				break;
			default:
				break;
		}

		iokitosx_process_errors(device, ret, msg, command_description, take_dump);
	}

free:
	free(buffer);

out:
	return ret;
}


int32_t iokit_read_position( iokit_device_t *device, pos_info *read_pos_info )
{
	int32_t ret = -1;

	char command_description[COMMAND_DESCRIPTION_LENGTH] = "READ_POSITION";
	cdb_read_pos cdb;
	uint8_t cdb_length;

	uint8_t *buffer;
	size_t buffer_size;
	uint8_t transfer_direction;
	char *msg;

	assert(device->scsiTaskInterface != NULL);

	// Zero out the result buffer
	buffer_size = sizeof(read_pos);
	buffer = malloc(buffer_size);
	bzero(buffer, buffer_size);

	// Zero out and setup CDB
	bzero(&cdb, sizeof(cdb));
	cdb.op_code = kSCSICmd_READ_POSITION;  /* 0x34 SCSI read position code */
	cdb_length = kSCSICDBSize_10Byte;

	transfer_direction = kSCSIDataTransfer_FromTargetToInitiator;

	/* reset the position flag */
	device->eom = false;
	device->position_valid = false;

	ret = iokit_issue_cdb_command(device, (void *)&cdb, cdb_length,
								  buffer, buffer_size, transfer_direction,
								  ComputeTimeOut(device->device_code, ReadPositionTimeOut),
								  command_description, &msg);

	if(ret != DEVICE_GOOD) {
		/*
		 * CDB produced an error so invalidate cached position
		 * data and return error code.
		 */

		iokitosx_process_errors(device, ret, msg, command_description, true);
		device->position_valid = false;
		goto free;
	}

	read_pos *read_pos_buf =  (read_pos *)buffer;

	if (!read_pos_buf->bpu) {
		read_pos_info->bop = (uint8_t) read_pos_buf->bop;
		read_pos_info->eop = (uint8_t) read_pos_buf->eop;

		read_pos_info->first_blk_pos = (uint32_t)four_bytes_to_int(read_pos_buf->first_blk_pos);

		read_pos_info->last_blk_pos = (uint32_t)four_bytes_to_int(read_pos_buf->last_blk_pos);

		if (!read_pos_buf->bcu)
			read_pos_info->blks_in_buf = (uint32_t)three_bytes_to_int(read_pos_buf->blks_in_buf);
		else
			read_pos_info->blks_in_buf = 0;

		if (!read_pos_buf->bycu)
			read_pos_info->bytes_in_buf = (uint32_t)four_bytes_to_int(read_pos_buf->bytes_in_buf);
		else
			read_pos_info->bytes_in_buf = 0;

		device->position_valid = true;
		device->eom = read_pos_info->eop;
		device->current_position = read_pos_info->first_blk_pos;
	}

		device->lbot = four_bytes_to_int(read_pos_buf->last_blk_pos);
		device->num_blocks = three_bytes_to_int(read_pos_buf->blks_in_buf);
		device->num_blocks = four_bytes_to_int(read_pos_buf->bytes_in_buf);
		device->bot = read_pos_buf->bop;

free:
	if(buffer)
		free(buffer);

out:
	return ret;
}

int32_t iokit_set_media_parameters(iokit_device_t *device,
								   media_modify_value modification,
								   all_device_params *params)
{
	int32_t ret = -1;

	char command_description[COMMAND_DESCRIPTION_LENGTH] = "MODE_SENSE (MEDIA PAGE)";

	cdb_modesense  sensecdb;
	cdb_modeselect selectcdb;
	uint8_t cdb_length;

	uint8_t *buffer;
	size_t buffer_size;
	uint8_t transfer_direction;
	char *msg;

	assert(device != NULL);

	// Zero out the result buffer
	buffer_size = sizeof(media_param);
	buffer = malloc(buffer_size);
	bzero(buffer, buffer_size);

	// Zero out and setup CDB
	bzero(&sensecdb, sizeof(sensecdb));
	sensecdb.op_code = kSCSICmd_MODE_SENSE_6;  /* 0x1A SCSI mode sense (6) code */
	sensecdb.dbd = false;
	sensecdb.page_code = DEVICE_CONFIGURATION_PAGE_NUMBER;
	sensecdb.alloc_length = buffer_size;
	cdb_length = kSCSICDBSize_6Byte;

	transfer_direction = kSCSIDataTransfer_FromTargetToInitiator;

	ret = iokit_issue_cdb_command(device, (uint8_t *)&sensecdb, cdb_length,
								  buffer, buffer_size, transfer_direction,
								  ComputeTimeOut(device->device_code, ModeSenseTimeOut),
								  command_description, &msg);

	/*
	 * Error is detected, return negative errno
	 */
	if (ret != DEVICE_GOOD) {
		iokitosx_process_errors(device, ret, msg, command_description, true);
		goto free;
	}

	media_param *media_buf = (media_param *)buffer;

	media_buf->header.mode_data_length = 0;

	switch(modification) {
		case mediaSet_BlockSizeOnly:
			media_buf->block.blk_length[0] = (uint8_t)(params->blksize >> 16 & 0xFF);
			media_buf->block.blk_length[1] = (uint8_t)(params->blksize >>  8 & 0xFF);
			media_buf->block.blk_length[2] = (uint8_t)(params->blksize       & 0xFF);
			break;
		case mediaSet_BufferModeOnly:
			if(params->buffered_mode) {
				media_buf->header.buffered_mode = true;
			} else {
				media_buf->header.buffered_mode = false;
			}
			break;
		case mediaSet_DensityCodeOnly:
				media_buf->block.density_code = params->density_code;
			break;
		default:
			media_buf->block.density_code = params->density_code;
			if(params->buffered_mode) {
				media_buf->header.buffered_mode = true;
			} else {
				media_buf->header.buffered_mode = false;
			}
			media_buf->block.blk_length[0] = (uint8_t)(params->blksize >> 16 & 0xFF);
			media_buf->block.blk_length[1] = (uint8_t)(params->blksize >>  8 & 0xFF);
			media_buf->block.blk_length[2] = (uint8_t)(params->blksize       & 0xFF);
			break;
	}

	strcpy(command_description, "MODE_SELECT (MEDIA PAGE)");

	// Zero out and setup CDB for MODE_SELECT
	bzero(&selectcdb, sizeof(selectcdb));

	selectcdb.op_code = kSCSICmd_MODE_SELECT_6;  /* 0x15 SCSI mode select (6) code */
	selectcdb.pf = true;
	selectcdb.parm_list_length = buffer_size;
	cdb_length = kSCSICDBSize_6Byte;

	transfer_direction = kSCSIDataTransfer_FromInitiatorToTarget;

	ret = iokit_issue_cdb_command(device, (uint8_t *)&selectcdb, cdb_length,
								  buffer, buffer_size, transfer_direction,
								  ComputeTimeOut(device->device_code, ModeSelectTimeOut),
								  command_description, &msg);

	/*
	 * Error is detected, return negative errno
	 */
	if (ret != DEVICE_GOOD) {
		iokitosx_process_errors(device, ret, msg, command_description, true);
		goto free;
	}
	else {
		switch(modification) {
			case mediaSet_BlockSizeOnly:
				device->blk_size = params->blksize;
				break;
			case mediaSet_BufferModeOnly:
				if(params->buffered_mode) {
					device->buffered_mode = true;
				} else {
					device->buffered_mode = false;
				}
				break;
			case mediaSet_DensityCodeOnly:
					device->density = params->density_code;
				break;
			default:
				device->density = params->density_code;
				if(params->buffered_mode) {
					device->buffered_mode = true;
				} else {
					device->buffered_mode = false;
				}
				device->blk_size = params->blksize;
				break;
		}
	}

free:
	free(buffer);
out:
	return ret;
}


int32_t iokit_get_media_parameters(iokit_device_t *device, all_device_params *params)
{
	int32_t ret = -1;

	char command_description[COMMAND_DESCRIPTION_LENGTH] = "MODE_SENSE (DEVICE CONFIG PAGE)";

	cdb_modesense  sensecdb;
	uint8_t cdb_length;

	uint8_t *buffer;
	size_t buffer_size;
	uint8_t transfer_direction;
	char *msg;

	assert(device != NULL);

	// Zero out the result buffer
	buffer_size = sizeof(media_param);
	buffer = malloc(buffer_size);
	bzero(buffer, buffer_size);

	// Zero out and setup CDB
	bzero(&sensecdb, sizeof(sensecdb));
	sensecdb.op_code = kSCSICmd_MODE_SENSE_6;  /* 0x1A SCSI mode sense (6) code */
	sensecdb.dbd = false;
	sensecdb.page_code = DEVICE_CONFIGURATION_PAGE_NUMBER;
	sensecdb.alloc_length = buffer_size;
	cdb_length = kSCSICDBSize_6Byte;

	transfer_direction = kSCSIDataTransfer_FromTargetToInitiator;

	ret = iokit_issue_cdb_command(device, (uint8_t *)&sensecdb, cdb_length,
								  buffer, buffer_size, transfer_direction,
								  ComputeTimeOut(device->device_code, ModeSenseTimeOut),
								  command_description, &msg);

	/*
	 * Error is detected, return negative errno
	 */
	if (ret != DEVICE_GOOD) {
		iokitosx_process_errors(device, ret, msg, command_description, true);
		goto free;
	}

	/* Extract data from medium buffer into device structure */
	media_param *media_buf = (media_param *)buffer;

	device->blk_size = three_bytes_to_int(media_buf->block.blk_length);
	device->density = media_buf->block.density_code;
	device->buffered_mode = media_buf->header.buffered_mode;
	device->medium_type = media_buf->header.medium_type;

	/* 3580 have write protect field in the mode parameter header */
	if (device->device_code == IBM_3580){
		device->write_protect = media_buf->header.wp;
		device->logical_write_protect = NO_PROTECT;
		device->capacity_scaling = SCALE_100;
		device->capacity_scaling_hex_val = Capacity100;
		goto copy_params;
	}

	free(buffer);

	strcpy(command_description, "MODE_SENSE (MEDIUM PAGE)");

	// Zero out the result buffer
	buffer_size = sizeof(medium_sense_page);
	buffer = malloc(buffer_size);
	bzero(buffer, buffer_size);

	// Zero out and setup CDB
	bzero(&sensecdb, sizeof(sensecdb));
	sensecdb.op_code = kSCSICmd_MODE_SENSE_6;  /* 0x1A SCSI mode sense (6) code */
	sensecdb.dbd = true;
	sensecdb.page_code = MEDIUM_SENSE_PAGE_NUMBER;
	sensecdb.alloc_length = buffer_size;
	cdb_length = kSCSICDBSize_6Byte;

	transfer_direction = kSCSIDataTransfer_FromTargetToInitiator;

	ret = iokit_issue_cdb_command(device, (uint8_t *)&sensecdb, cdb_length,
								  buffer, buffer_size, transfer_direction,
								  ComputeTimeOut(device->device_code, ModeSenseTimeOut),
								  command_description, &msg);

	/*
	 * Error is detected, return negative errno
	 */
	if (ret != DEVICE_GOOD) {
		iokitosx_process_errors(device, ret, msg, command_description, true);
		goto free;
	}

	/* Extract data from medium buffer into device structure */
	medium_sense_page *medium_buf = (medium_sense_page *)buffer;

	/* if medium_id is equal to 0x0000 then there is no medium present */
	if ((medium_buf->medium.medium_id[0] == 0x00) &&
		(medium_buf->medium.medium_id[1] == 0x00)) {
		device->write_protect = false;
		device->logical_write_protect = NO_PROTECT;
		device->capacity_scaling = SCALE_100;
	} else {
		/* if there are any forms of write protect on the medium */
		params->write_protect = medium_buf->medium.physical_wp;
		if (medium_buf->medium.permanent_wp) {
			device->logical_write_protect = WORM_PROTECT;
		} else if (medium_buf->medium.persistent_wp) {
			device->logical_write_protect = PERSISTENT_PROTECT;
		} else if (medium_buf->medium.associated_wp) {
			device->logical_write_protect = ASSOCIATED_PROTECT;
		} else {
			device->logical_write_protect = NO_PROTECT;
		}

		/* translate the capacity scaling */
		device->capacity_scaling_hex_val = medium_buf->medium.capacity_scaling;
		if (device->device_code != IBM_3592) {
			if( (medium_buf->medium.capacity_scaling == Capacity100) ||
				 (medium_buf->medium.capacity_scaling >= Capacity100RangeLow) )
				device->capacity_scaling = SCALE_100;
			else if( (medium_buf->medium.capacity_scaling==Capacity75) ||
					 ((medium_buf->medium.capacity_scaling<=Capacity75RangeHigh) &&
					  (medium_buf->medium.capacity_scaling>=Capacity75RangeLow)) )
				device->capacity_scaling = SCALE_75;
			else if( (medium_buf->medium.capacity_scaling == Capacity50) ||
					 ((medium_buf->medium.capacity_scaling<=Capacity50RangeHigh) &&
				      (medium_buf->medium.capacity_scaling>=Capacity50RangeLow)) )
				device->capacity_scaling = SCALE_50;
			else if( (medium_buf->medium.capacity_scaling==Capacity25) ||
					 ((medium_buf->medium.capacity_scaling<=Capacity25RangeHigh) &&
					  (medium_buf->medium.capacity_scaling>=Capacity25RangeLow)) )
				device->capacity_scaling = SCALE_25;
			else {
				device->capacity_scaling = SCALE_100;
			}
		} else if (device->device_code == IBM_3592) {
			/* always set to SCALE_VALUE */
			device->capacity_scaling = SCALE_VALUE;
		}
	}

copy_params:
	/* Copy from device structure to output params */
	params->blksize =                device->blk_size;
	params->density_code =           device->density;
	params->buffered_mode =          device->buffered_mode;
	params->medium_type =            device->medium_type;
	params->write_protect =          device->write_protect;
	params->logical_write_protect =  device->logical_write_protect;
	params->capacity_scaling =       device->capacity_scaling;
	params->capacity_scaling_value = device->capacity_scaling_hex_val;

	ret = iokit_update_block_limits(device);

	if (ret >= 0) {
		params->max_blksize = device->max_blk_size;
		params->min_blksize = device->min_blk_size;
	}

free:
	free(buffer);

out:
	return ret;
}


int32_t iokit_update_block_limits(iokit_device_t *device) {
	int32_t ret = -1;

	char command_description[COMMAND_DESCRIPTION_LENGTH] = "READ_BLOCK_LIMITS";
	cdb_read_block_limits cdb;
	uint8_t cdb_length;

	uint8_t *buffer;
	size_t buffer_size;
	uint8_t transfer_direction;

	block_limits *limit_data = NULL;
	char *msg;

	assert(device->scsiTaskInterface != NULL);

	// Zero out the result buffer
	buffer_size = sizeof(block_limits);
	buffer = malloc(buffer_size);
	bzero(buffer, buffer_size);

	// Zero out and setup CDB
	bzero(&cdb, sizeof(cdb));
	cdb.op_code = kSCSICmd_READ_BLOCK_LIMITS;  /* 0x05 SCSI read block limits code */
	cdb_length = kSCSICDBSize_6Byte;

	transfer_direction = kSCSIDataTransfer_FromTargetToInitiator;

	ret = iokit_issue_cdb_command(device, (void *)&cdb, cdb_length,
								  buffer, buffer_size, transfer_direction,
								  ComputeTimeOut(device->device_code, ReadBlockLimitsTimeOut),
								  command_description, &msg);

	if (ret != DEVICE_GOOD) {
		iokitosx_process_errors(device, ret, msg, command_description, true);
	}
	else {
		limit_data = (block_limits *)buffer;

		device->max_blk_size = (uint32_t)three_bytes_to_int(limit_data->max_blk_size);
		device->min_blk_size = (uint16_t)two_bytes_to_int(limit_data->min_blk_size);
	}

out:
	return ret;
}


int32_t iokit_set_position(iokit_device_t *device, position_set_method method,
						   size_t count, struct tc_position *pos)
{
	int32_t ret = -1;
	char command_description[COMMAND_DESCRIPTION_LENGTH] = "LOCATE (SET POSITION)";
	uint8_t *cdb = NULL;
	uint8_t cdb_length = 0;

	cdb_locate *cdblocate;
	cdb_space *cdbspace;

	uint8_t *buffer;
	size_t buffer_size;
	uint8_t transfer_direction;
	uint32_t timeout = DefaultTimeOutValue;
	char *msg;

	if( (count & 0xFF100000) != 0xFF100000)
		if( (count & 0xFF100000) != 0)
			return -1; // Overflow

	assert(device->scsiTaskInterface != NULL);

	// Setup buffer
	buffer = NULL;
	buffer_size = 0;

	cdb = malloc(MAX_CDB_SIZE);

	// Zero out and setup CDB
	bzero(cdb, MAX_CDB_SIZE);

	switch(method) {
		case positionSet_SpaceFileMark:
			cdbspace = (cdb_space *)cdb;

			cdbspace->op_code = kSCSICmd_SPACE;  /* 0x11 SCSI space code */
			cdbspace->code = 0x1;
			cdbspace->count[0] = (uint8_t)(count >> 16 & 0xFF);
			cdbspace->count[1] = (uint8_t)(count >> 8  & 0xFF);
			cdbspace->count[2] = (uint8_t)(count       & 0xFF);

			cdb_length = kSCSICDBSize_6Byte;

			timeout = ComputeTimeOut(device->device_code, SpaceTimeOut);

			break;
		case positionSet_SpaceRecord:
			cdbspace = (cdb_space *)cdb;

			cdbspace->op_code = kSCSICmd_SPACE;  /* 0x11 SCSI space code */
			cdbspace->code = 0;
			cdbspace->count[0] = (uint8_t)(count >> 16 & 0xFF);
			cdbspace->count[1] = (uint8_t)(count >> 8  & 0xFF);
			cdbspace->count[2] = (uint8_t)(count       & 0xFF);

			cdb_length = kSCSICDBSize_6Byte;

			timeout = ComputeTimeOut(device->device_code, SpaceTimeOut);

			break;
		case positionSet_SpaceEOM:
			cdbspace = (cdb_space *)cdb;

			cdbspace->op_code = kSCSICmd_SPACE;  /* 0x11 SCSI space code */
			cdbspace->code = 0x3;

			cdb_length = kSCSICDBSize_6Byte;

			timeout = ComputeTimeOut(device->device_code, LocateTimeOut);

			break;
		case positionSet_LocateBlock:
			cdblocate = (cdb_locate *)cdb;

			cdblocate->op_code = kSCSICmd_LOCATE;  /* 0x2B SCSI locate code */
			cdblocate->blk_addr[0] = (uint8_t)(count >> 24 & 0xFF);
			cdblocate->blk_addr[1] = (uint8_t)(count >> 16 & 0xFF);
			cdblocate->blk_addr[2] = (uint8_t)(count >> 8  & 0xFF);
			cdblocate->blk_addr[3] = (uint8_t)(count       & 0xFF);

			cdb_length = kSCSICDBSize_10Byte;

			timeout = ComputeTimeOut(device->device_code, LocateTimeOut);

			break;
		case positionSet_LocateBlockImmediate:
			cdblocate = (cdb_locate *)cdb;

			cdblocate->op_code = kSCSICmd_LOCATE;  /* 0x2B SCSI locate code */
			cdblocate->immediate = true;
			cdblocate->blk_addr[0] = (uint8_t)(count >> 24 & 0xFF);
			cdblocate->blk_addr[1] = (uint8_t)(count >> 16 & 0xFF);
			cdblocate->blk_addr[2] = (uint8_t)(count >> 8  & 0xFF);
			cdblocate->blk_addr[3] = (uint8_t)(count       & 0xFF);

			cdb_length = kSCSICDBSize_10Byte;

			timeout = ComputeTimeOut(device->device_code, LocateTimeOut);

			break;
		case positionSet_SpaceSequentialFM:
			cdbspace = (cdb_space *)cdb;

			cdbspace->op_code = kSCSICmd_SPACE;  /* 0x11 SCSI space code */
			cdbspace->code = 2;
			cdbspace->count[0] = (uint8_t)(count >> 16 & 0xFF);
			cdbspace->count[1] = (uint8_t)(count >> 8  & 0xFF);
			cdbspace->count[2] = (uint8_t)(count       & 0xFF);

			cdb_length = kSCSICDBSize_6Byte;

			timeout = ComputeTimeOut(device->device_code, SpaceTimeOut);

			break;
		default:
			goto free;
			break;
	}

	assert(cdb_length != 0);
	assert(cdb != NULL);

	transfer_direction = kSCSIDataTransfer_NoDataTransfer;

	ret = iokit_issue_cdb_command(device, (void *)cdb, cdb_length,
								  buffer, buffer_size, transfer_direction,
								  timeout,
								  command_description, &msg);

	if(ret == DEVICE_GOOD) {
		device->eom = false;
	} else {
		device->position_valid = false;
		iokitosx_process_errors(device, ret, msg, command_description, true);
	}

free:
	free(cdb);
out:
	return ret;
}

int32_t iokit_rewind( iokit_device_t *device, uint8_t immediate)
{
	int32_t ret = -1;

	char command_description[COMMAND_DESCRIPTION_LENGTH] = "REWIND";
	cdb_rewind cdb;
	uint8_t cdb_length;

	uint8_t *buffer;
	size_t buffer_size;
	uint8_t transfer_direction;
	char *msg;

	assert(device->scsiTaskInterface != NULL);

	// Setup buffer
	buffer = NULL;
	buffer_size = 0;

	// Zero out and setup CDB
	bzero (&cdb, sizeof( cdb ));
	cdb.op_code = kSCSICmd_REWIND;  /* 0x01 SCSI rewind code */
	cdb.immediate = immediate;
	cdb_length = kSCSICDBSize_6Byte;

	transfer_direction = kSCSIDataTransfer_NoDataTransfer;

	ret = iokit_issue_cdb_command(device, (void *)&cdb, cdb_length,
								  buffer, buffer_size, transfer_direction,
								  ComputeTimeOut(device->device_code, RewindTimeOut),
								  command_description, &msg);

	if (ret != DEVICE_GOOD) {
		iokitosx_process_errors(device, ret, msg, command_description, true);
	}

	/* Update cached position data */
	device->eom = false;
	device->eom_block_id = 0;

	device->current_partition = 0;
	device->current_position = 0;
	device->position_valid = true;

out:
	return ret;

}


int32_t iokit_erase( iokit_device_t *device, uint8_t long_bit, uint8_t immediate )
{
	int32_t ret = -1;

	char command_description[COMMAND_DESCRIPTION_LENGTH] = "ERASE";
	cdb_erase cdb;
	uint8_t cdb_length;

	uint8_t *buffer;
	size_t buffer_size;
	uint8_t transfer_direction;
	char *msg;

	assert(device->scsiTaskInterface != NULL);

	// Setup buffer
	buffer = NULL;
	buffer_size = 0;

	// Zero out and setup CDB
	bzero (&cdb, sizeof( cdb ));
	cdb.op_code = kSCSICmd_ERASE;  /* 0x19 SCSI erase code */
	cdb.long_bit = long_bit;
	cdb.immediate = immediate;
	cdb_length = kSCSICDBSize_6Byte;

	transfer_direction = kSCSIDataTransfer_NoDataTransfer;

	ret = iokit_issue_cdb_command(device, (void *)&cdb, cdb_length,
							 buffer, buffer_size, transfer_direction,
							 ComputeTimeOut(device->device_code, EraseTimeOut),
							 command_description, &msg);

	if (ret != DEVICE_GOOD) {
		iokitosx_process_errors(device, ret, msg, command_description, true);
	}

#if 0
	/* Update cached position data */
	device->position_valid = false;
#endif

out:
	return ret;
}


int32_t iokit_write_filemark( iokit_device_t *device, size_t count, uint8_t immediate )
{
	int32_t rc = -1;

	char command_description[COMMAND_DESCRIPTION_LENGTH] = "WRITE_FILEMARK";
	cdb_write_file_mark cdb;
	uint8_t cdb_length;

	uint8_t *buffer;
	size_t buffer_size;
	uint8_t transfer_direction;
	char *msg;

	assert(device->scsiTaskInterface != NULL);

	// Setup buffer
	buffer = NULL;
	buffer_size = 0;

	// Zero out and setup cdb
	bzero(&cdb, sizeof(cdb));
	cdb.op_code = kSCSICmd_WRITE_FILEMARKS;  /* 0x10 SCSI write file marks code */
	cdb.immediate = (uint8_t)immediate & 0x01;
	cdb.write_setmarks = false;
	cdb.count[0] = (uint8_t) (count >> 16 & 0xFF);
	cdb.count[1] = (uint8_t) (count >>  8 & 0xFF);
	cdb.count[2] = (uint8_t) (count & 0xFF);
	cdb_length = kSCSICDBSize_10Byte;

	transfer_direction = kSCSIDataTransfer_NoDataTransfer;

	rc = iokit_issue_cdb_command(device, (void *)&cdb, cdb_length,
							  buffer, buffer_size, transfer_direction,
							  ComputeTimeOut(device->device_code, WriteFileMarkTimeOut),
							  command_description, &msg);

	if(rc != DEVICE_GOOD) {
		switch (rc) {
			case -EDEV_EARLY_WARNING:
				ltfsmsg(LTFS_WARN, "12074W", "write filemarks");
				rc = DEVICE_GOOD;
				break;
			case -EDEV_PROG_EARLY_WARNING:
				ltfsmsg(LTFS_WARN, "12075W", "write filemarks");
				rc = DEVICE_GOOD;
				break;
			case -EDEV_CLEANING_REQUIRED:
				ltfsmsg(LTFS_INFO, "12109I");
				rc = DEVICE_GOOD;
				break;
			default:
				break;
		}

		if (rc != DEVICE_GOOD) {
			iokitosx_process_errors(device, rc, msg, command_description, true);
		}
	}

	if(rc < 0)
		goto out;

	if(rc < 0) {
		if(count != 0)
			device->position_valid = false;

		goto out;
	}

	/* keep track of how many filemarks we have at the eod (for tape flush) */
	if((rc == 0) && (count != 0)) {
		device->ending_fm = count;

		if(device->position_valid)
			device->current_partition += count;
	}

out:
	return rc;
}

int32_t iokit_set_compression(iokit_device_t *device, boolean_t enable_compression)
{
	int32_t ret = -1;

	char command_description[COMMAND_DESCRIPTION_LENGTH] = "MODE_SENSE (COMPRESSION PAGE)";

	cdb_modesense  sensecdb;
	cdb_modeselect selectcdb;
	uint8_t cdb_length;

	uint8_t *buffer;
	size_t buffer_size;
	uint8_t transfer_direction;
	char *msg;

	assert(device != NULL);

	// Zero out the result buffer
	buffer_size = sizeof(compression_parm);
	buffer = malloc(buffer_size);
	bzero(buffer, buffer_size);

	// Zero out and setup CDB
	bzero(&sensecdb, sizeof(sensecdb));
	sensecdb.op_code = kSCSICmd_MODE_SENSE_6;  /* 0x1A SCSI mode sense (6) code */
	sensecdb.dbd = true;
	sensecdb.page_code = DATA_COMPRESSION_PAGE_NUMBER;
	sensecdb.alloc_length = buffer_size;
	cdb_length = kSCSICDBSize_6Byte;

	transfer_direction = kSCSIDataTransfer_FromTargetToInitiator;

	ret = iokit_issue_cdb_command(device, (uint8_t *)&sensecdb, cdb_length,
								  buffer, buffer_size, transfer_direction,
								  ComputeTimeOut(device->device_code, ModeSenseTimeOut),
								  command_description, &msg);

	if (ret != DEVICE_GOOD) {
		iokitosx_process_errors(device, ret, msg, command_description, true);
		goto free;
	}

	compression_parm *compression_buf = (compression_parm *)buffer;

	compression_buf->header.mode_data_length = false;
	if(enable_compression) {
		compression_buf->page[2] = compression_buf->page[2] | 0x80;
	} else {
		compression_buf->page[2] = compression_buf->page[2] & 0x7E;
	}

	strcpy(command_description, "MODE_SELECT (COMPRESSION PAGE)");

	// Zero out and setup CDB for MODE_SELECT
	bzero(&selectcdb, sizeof(selectcdb));
	selectcdb.op_code = kSCSICmd_MODE_SELECT_6;  /* 0x15 SCSI mode seleect (6) code */
	selectcdb.pf = true;
	selectcdb.parm_list_length = buffer_size;
	cdb_length = kSCSICDBSize_6Byte;

	transfer_direction = kSCSIDataTransfer_FromInitiatorToTarget;

	ret = iokit_issue_cdb_command(device, (uint8_t *)&selectcdb, cdb_length,
								  buffer, buffer_size, transfer_direction,
								  ComputeTimeOut(device->device_code, ModeSelectTimeOut),
								  command_description, &msg);

	if (ret != DEVICE_GOOD) {
		iokitosx_process_errors(device, ret, msg, command_description, true);
	}
	else
		device->compression = enable_compression;

free:
	free(buffer);
out:
	return ret;
}


/* The "ew" and "pew" argument is temporary added until backend interface re-design to return
   more efficient return code becomes available. */
int32_t iokit_write( iokit_device_t *device, uint8_t *buf, size_t size, bool *ew, bool *pew )
{
	int32_t rc = -1;

	char command_description[COMMAND_DESCRIPTION_LENGTH] = "WRITE";
	cdb_read_write cdb;
	uint8_t cdb_length;

	uint8_t *buffer;
	size_t buffer_size;
	uint8_t transfer_direction;
	char *msg;

	assert(device->scsiTaskInterface != NULL);

	*ew = false;
	*pew = false;

	if(buf == NULL) {
		rc = -EDEV_INTERNAL_ERROR;
		goto out;
	}

	if(size < 0) {
		rc = -EDEV_INTERNAL_ERROR;
		goto out;
	}

	// Setup buffer
	buffer_size = size;
	buffer = buf;

	// Zero out and setup CDB
	bzero(&cdb, sizeof( cdb ));
	cdb.op_code = kSCSICmd_WRITE_6;  /* 0x0A SCSI write (6) code */
	cdb.fixed = (device->blk_size) ? true : false;
	cdb.xfer_length[0] = (uint8_t)(buffer_size >> 16) & 0xFF;
	cdb.xfer_length[1] = (uint8_t)(buffer_size >>  8) & 0xFF;
	cdb.xfer_length[2] = (uint8_t) buffer_size        & 0xFF;

	cdb_length = kSCSICDBSize_6Byte;

	transfer_direction = kSCSIDataTransfer_FromInitiatorToTarget;

	rc = iokit_issue_cdb_command(device, (uint8_t *)&cdb, cdb_length,
								 buffer, buffer_size, transfer_direction,
								 ComputeTimeOut(device->device_code, WriteTimeOut),
								 command_description, &msg);

	if (rc != DEVICE_GOOD) {
		switch (rc) {
			case -EDEV_EARLY_WARNING:
				ltfsmsg(LTFS_WARN, "12074W", "write");
				*ew = true;
				*pew = true;
				rc = DEVICE_GOOD;
				break;
			case -EDEV_PROG_EARLY_WARNING:
				ltfsmsg(LTFS_WARN, "12075W", "write");
				*pew = true;
				rc = DEVICE_GOOD;
				break;
			case -EDEV_CLEANING_REQUIRED:
				ltfsmsg(LTFS_INFO, "12109I");
				rc = DEVICE_GOOD;
				break;
			default:
				break;
		}
		if (rc != DEVICE_GOOD) {
			iokitosx_process_errors(device, rc, msg, command_description, true);
		}
	}

out:
	return rc;
}


int32_t iokit_log_sense_page( iokit_device_t *device, uint8_t page_code, uint8_t subpage, uint8_t *logsense_buffer, size_t logsense_buffer_size )
{
	int32_t ret = -1;

	char command_description[COMMAND_DESCRIPTION_LENGTH] = "LOG_SENSE";
	cdb_logsense cdb;
	uint8_t cdb_length;

	uint8_t *buffer;
	size_t buffer_size;
	uint8_t transfer_direction;
	char *msg;

	assert(device->scsiTaskInterface != NULL);

	// Zero out the result buffer
	buffer_size = sizeof(log_sense_page);
	buffer = malloc(sizeof(log_sense_page));
	bzero(buffer, buffer_size);

	// Zero out and setup CDB
	bzero(&cdb, sizeof(cdb));
	cdb.op_code = kSCSICmd_LOG_SENSE;  /* 0x4d SCSI log sense code */
	cdb.sp = false;
	cdb.ppc = false;
	cdb.pc = true;
	cdb.page_code = page_code;
	cdb.subpage_code = subpage;
	cdb.alloc_length[0] = (uint8_t)(buffer_size >> 8) & 0xff;
	cdb.alloc_length[1] = (uint8_t) buffer_size       & 0xff;

	cdb_length = kSCSICDBSize_10Byte;

	transfer_direction = kSCSIDataTransfer_FromTargetToInitiator;

	ret = iokit_issue_cdb_command(device, (void *)&cdb, cdb_length,
								  buffer, buffer_size, transfer_direction,
								  ComputeTimeOut(device->device_code, LogSenseTimeOut),
								  command_description, &msg);

	if(ret != DEVICE_GOOD) {
		iokitosx_process_errors(device, ret, msg, command_description, true);
	}

	/*
	 * SCSI using big endian for multi byte value.
	 * So, log_sense_header->len is flipped in little endian systems.
	 * It is the safe way to handle this value myself.
	 */
	uint8_t *log_sense_buf = (uint8_t *)buffer;
	uint16_t len = ((uint16_t)log_sense_buf[2] << 8) | log_sense_buf[3];

	/* return logsense page including 4 byte header */
	if( len <= logsense_buffer_size)
		memcpy(logsense_buffer, buffer, logsense_buffer_size);
	else
		ret = ENOMEM;

free:
	free(buffer);
out:
	return ret;
}


int32_t iokit_report_density( iokit_device_t *device, uint8_t media,
		                      uint8_t *report_count, report_density *report_buffer )
{
	int32_t ret = -1;

	char command_description[COMMAND_DESCRIPTION_LENGTH] = "REPORT_DENSITY";
	cdb_report_density cdb;
	uint8_t cdb_length;

	uint8_t *buffer;
	size_t buffer_size;
	uint8_t transfer_direction;

	uint16_t buf_len;
	char *msg;

	assert(device->scsiTaskInterface != NULL);

	// Zero out the result buffer
	buffer_size = sizeof(report_density);
	buffer = (uint8_t*)report_buffer;
	bzero(buffer, buffer_size);

	// Zero out and setup CDB
	bzero(&cdb, sizeof(cdb));
	cdb.op_code = kSCSICmd_REPORT_DENSITY_SUPPORT;  /* 0x44 SCSI report density support code */
	cdb.media = media;
	cdb.alloc_length[0] = (buffer_size >> 8) & 0xff;
	cdb.alloc_length[1] =  buffer_size       & 0xff;

	cdb_length = kSCSICDBSize_10Byte;

	transfer_direction = kSCSIDataTransfer_FromTargetToInitiator;

	ret = iokit_issue_cdb_command(device, (void *)&cdb, cdb_length,
								  buffer, buffer_size, transfer_direction,
								  ComputeTimeOut(device->device_code, ReportDensityTimeOut),
								  command_description, &msg);

	if (ret != DEVICE_GOOD) {
		iokitosx_process_errors(device, ret, msg, command_description, true);
		goto out;
	}

	buf_len = (report_buffer->header.avail_length[0] << 8 ) +
			   report_buffer->header.avail_length[1];

	/* buf_len includes 2 bytes of reserved fields; we need take them out */
	*report_count = (unsigned short)((buf_len-2) /
					sizeof(report_density_descriptor));

	/* 3580 reports back 54 length instead of 56 */
	if ((*report_count == 0) && (buf_len > sizeof(report_density_header)))
		*report_count = 1;

out:
	return ret;
}

int32_t iokit_inquiry( iokit_device_t *device, uint8_t page, uint8_t *buf, size_t size )
{
	int32_t ret = -1;

	char command_description[COMMAND_DESCRIPTION_LENGTH] = "INQUIRY";
	cdb_inquiry cdb;
	uint8_t cdb_length;

	uint8_t *buffer;
	size_t buffer_size;
	uint8_t transfer_direction;
	char *msg;

	assert(device->scsiTaskInterface != NULL);

	// Zero out the result buffer
	buffer_size = size;
	buffer = buf;
	bzero(buffer, buffer_size);

	// Zero out and setup CDB
	bzero(&cdb, sizeof(cdb));
	cdb.op_code = kSCSICmd_INQUIRY;  /* 0x12 SCSI inquiry code */
	cdb.page_code = page & 0xFF;
	if(cdb.page_code)
		cdb.evpd = 1;
	cdb.alloc_length = buffer_size;

	cdb_length = kSCSICDBSize_6Byte;

	transfer_direction = kSCSIDataTransfer_FromTargetToInitiator;

	ret = iokit_issue_cdb_command(device, (void *)&cdb, cdb_length,
								  buffer, buffer_size, transfer_direction,
								  ComputeTimeOut(device->device_code, InquiryTimeOut),
								  command_description, &msg);

	if(ret != DEVICE_GOOD) {
		iokitosx_process_errors(device, ret, msg, command_description, true);
	}

	// TODO: populate result structure with data

out:
	return ret;
}

int32_t iokit_std_inquiry( iokit_device_t *device, uint8_t *buf, size_t size )
{
	return iokit_inquiry(device, 0, buf, size);
}

int32_t iokit_request_sense( iokit_device_t *device, char *sense_buf, int size)
{
	int32_t ret = -1;

	char command_description[COMMAND_DESCRIPTION_LENGTH] = "REQUEST SENSE";
	cdb_request_sense cdb;
	uint8_t cdb_length;

	uint8_t *buffer;
	size_t buffer_size;
	uint8_t transfer_direction;
	char *msg;

	if(size > MAXSENSE)
		return -EDEV_INTERNAL_ERROR;

	assert(device->scsiTaskInterface != NULL);

	// Zero out the result buffer
	buffer_size = MAXSENSE;
	buffer = malloc(MAXSENSE);
	bzero(buffer, buffer_size);

	// Zero out and setup CDB
	bzero(&cdb, sizeof( cdb ));
	cdb.op_code = kSCSICmd_REQUEST_SENSE;	/* 0x03 SCSI request sense code */
	cdb.allocation_length = MAXSENSE;
	cdb_length = kSCSICDBSize_6Byte;

	transfer_direction = kSCSIDataTransfer_FromTargetToInitiator;

	ret = iokit_issue_cdb_command(device, (uint8_t *)&cdb, cdb_length,
						    buffer, buffer_size, transfer_direction,
						    ComputeTimeOut(device->device_code, InquiryTimeOut),
						    command_description, &msg);

	if(ret != DEVICE_GOOD) {
		iokitosx_process_errors(device, ret, msg, command_description, true);
	}
	else {
		memcpy(sense_buf, buffer, size);
	}

free:
	free(buffer);

out:
	return ret;
}

static bool is_supported_firmware(scsi_device_identifier *id_data)
{
	const uint32_t rev = ltfs_betou32(id_data->product_rev);

	switch (id_data->drive_type) {
	case DRIVE_LTO5:
	case DRIVE_LTO5_HH:
		if (rev < ltfs_betou32(base_firmware_level_lto5)) {
			ltfsmsg(LTFS_WARN, "12181W", base_firmware_level_lto5);
			ltfsmsg(LTFS_WARN, "12182W");
		}
		break;
	case DRIVE_TS1140:
		if (rev < ltfs_betou32(base_firmware_level_ts1140)) {
			ltfsmsg(LTFS_WARN, "12181W", base_firmware_level_ts1140);
			return false;
		}
		break;
	case DRIVE_LTO6:
	case DRIVE_LTO6_HH:
	case DRIVE_TS1150:
	default:
		break;
	}

	return true;
}

int32_t iokit_identify_device_code(iokit_device_t *iokit_device, scsi_device_identifier *identifier)
{
	int32_t ret = -1;
	int32_t device_code = -1;
	int32_t i = 0;

	scsi_device_identifier id_data;

	inquiry_data inquiry_buf;
	char serial_no_buf[MAX_INQ_LEN];

	ret = iokit_std_inquiry(iokit_device, (void *)&inquiry_buf, sizeof(inquiry_data));

	if( ret < 0 ) {
		ltfsmsg(LTFS_INFO, "12116I", ret);
		goto out;
	}

	bzero(&id_data, sizeof(scsi_device_identifier));

	strncpy(id_data.vendor_id,   inquiry_buf.standard.vendor_identification, VENDOR_ID_LENGTH);
	strncpy(id_data.product_id,  inquiry_buf.standard.product_identification, PRODUCT_ID_LENGTH);
	strncpy(id_data.product_rev, inquiry_buf.standard.product_revision_level, PRODUCT_REV_LENGTH);

	ltfsmsg(LTFS_INFO, "12118I", id_data.product_id);
	ltfsmsg(LTFS_INFO, "12162I", id_data.vendor_id);

	ret = iokit_inquiry(iokit_device, 0x80, (uint8_t *)serial_no_buf, MAX_INQ_LEN);

	if( ret < 0 ) {
		ltfsmsg(LTFS_INFO, "12161I", 0x80, ret);
		goto out;
	}

	strncpy(id_data.unit_serial, &(serial_no_buf[4]), UNIT_SERIAL_LENGTH);

	while(supported_devices[i]) {
		if( (strncmp(id_data.vendor_id,
					 supported_devices[i]->vendor_id,
					 strlen(supported_devices[i]->vendor_id)) == 0) &&
			 (strncmp(id_data.product_id,
					 supported_devices[i]->product_id,
					 strlen(supported_devices[i]->product_id)) == 0) ){

			device_code = supported_devices[i]->device_code;
			id_data.drive_type = supported_devices[i]->drive_type;
			goto found;
		}
		i++;
	}

found:
	if(identifier != NULL)
		memcpy(identifier, &id_data, sizeof(scsi_device_identifier));

	ltfsmsg(LTFS_INFO, "12159I", id_data.product_rev);
	if (!is_supported_firmware(&id_data))
		device_code = -EDEV_UNSUPPORTED_FIRMWARE;

	ltfsmsg(LTFS_INFO, "12160I", id_data.unit_serial);

out:
	return device_code;
}
