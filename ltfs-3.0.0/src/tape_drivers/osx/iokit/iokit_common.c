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
** FILE NAME:       tape_drivers/osx/iokit/iokit_common.h
**
** DESCRIPTION:     Implementation of common backend operations.
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


#include "libltfs/ltfs.h"
#include "scsi_command_blocks.h"
#include "libltfs/xml_libltfs.h"
#include "libltfs/fs.h"

#include "iokit_common.h"
#include "iokit_scsi_operations.h"
#include "iokit_scsi_base.h"

#define SENDDIAG_BUF_LEN (8)

static int _cdb_read_buffer(void *device, int id, unsigned char *buf, size_t offset, size_t len, int type)
{
	int ret = -1;

	iokit_device_t *iokit_device = (iokit_device_t *)device;

	char command_description[COMMAND_DESCRIPTION_LENGTH] = "READ_BUFFER";

	cdb_pass_through passthrough;
	unsigned char	 cdb[10];
	unsigned char	 sense[MAXSENSE];
	char *msg;

	bzero(&passthrough, sizeof(passthrough));
	bzero(cdb, sizeof(cdb));
	bzero(sense, sizeof(sense));

	passthrough.operation_descriptor = command_description;

	// Prepare data buffer
	passthrough.buffer_length	= len;
	passthrough.buffer			= buf;
	bzero(passthrough.buffer, passthrough.buffer_length);

	// Prepare CDB
	passthrough.cmd_length		= sizeof(cdb);
	passthrough.cdb				= (unsigned char *)cdb;
	passthrough.cdb[0]			= kSCSICmd_READ_BUFFER;	/* 0x3c SCSI read buffer code */
	passthrough.cdb[1]			= type;
	passthrough.cdb[2]			= id;
	passthrough.cdb[3]			= (unsigned char)(offset >> 16) & 0xFF;
	passthrough.cdb[4]			= (unsigned char)(offset >> 8)  & 0xFF;
	passthrough.cdb[5]			= (unsigned char) offset        & 0xFF;
	passthrough.cdb[6]			= (unsigned char)(len >> 16) & 0xFF;
	passthrough.cdb[7]			= (unsigned char)(len >> 8)  & 0xFF;
	passthrough.cdb[8]			= (unsigned char) len        & 0xFF;

	passthrough.data_direction  = SCSI_FROM_TARGET_TO_INITIATOR;
	passthrough.timeout			= SCSI_PASSTHRU_TIMEOUT_VALUE;

	// Prepare sense buffer
	passthrough.sense_length = sizeof(sense);
	passthrough.sense        = (unsigned char *)sense;

	ret = iokit_passthrough(iokit_device, &passthrough, &msg);
	if (ret != DEVICE_GOOD) {
		iokitosx_process_errors(iokit_device, ret, msg, command_description, false);
	}

out:
	return ret;
}

static int _cdb_forcedump(void *device)
{
	int ret = -1;

	iokit_device_t *iokit_device = (iokit_device_t *)device;

	char command_description[COMMAND_DESCRIPTION_LENGTH] = "FORCE_DUMP";

	cdb_pass_through passthrough;
	unsigned char	 cdb[6];
	unsigned char    buf[SENDDIAG_BUF_LEN];
	unsigned char	 sense[MAXSENSE];
	char *msg;

	bzero(&passthrough, sizeof(passthrough));
	bzero(cdb, sizeof(cdb));
	bzero(sense, sizeof(sense));

	passthrough.operation_descriptor = command_description;

	// Prepare data buffer
	passthrough.buffer_length	= sizeof(buf);
	passthrough.buffer			= buf;
	bzero(passthrough.buffer, passthrough.buffer_length);
	passthrough.buffer[0]    = 0x80;  /* page code */
	passthrough.buffer[2]    = 0x00;
	passthrough.buffer[3]    = 0x04;  /* page length */
	passthrough.buffer[4]    = 0x01;
	passthrough.buffer[5]    = 0x60;  /* diagnostic id */

	// Prepare CDB
	passthrough.cmd_length		= sizeof(cdb);
	passthrough.cdb				= (unsigned char *)cdb;
	passthrough.cdb[0]			= kSCSICmd_SEND_DIAGNOSTICS;	/* 0x1d SCSI send diagnostics code */
	passthrough.cdb[1]			= 0x10;  /* PF bit is set */
	passthrough.cdb[3]			= 0x00;
	passthrough.cdb[4]			= 0x08;  /* parameter length is 0x0008 */

	passthrough.data_direction  = SCSI_FROM_INITIATOR_TO_TARGET;
	passthrough.timeout			= SCSI_PASSTHRU_TIMEOUT_VALUE;

	// Prepare sense buffer
	passthrough.sense_length = sizeof(sense);
	passthrough.sense        = (unsigned char *)sense;

	ret = iokit_passthrough(iokit_device, &passthrough, &msg);
	if (ret != DEVICE_GOOD) {
		iokitosx_process_errors(iokit_device, ret, msg, command_description, false);
	}

out:
	return ret;
}


// Global Functions

int iokitosx_readbuffer(void *device, int id, unsigned char *buf, size_t offset, size_t len, int type)
{
	int ret = -1;

	ret = _cdb_read_buffer(device, id, buf, offset, len, type);

	return ret;
}

#define DUMP_HEADER_SIZE   (4)
#define DUMP_TRANSFER_SIZE (64 * KB)

int iokitosx_getdump_drive(void *device, const char *fname)
{
	long long               data_length, buf_offset;
	int                     dumpfd = -1;
	int                     transfer_size, num_transfers, excess_transfer;
	int                     rc = 0;
	int                     i, bytes;
	unsigned char           cap_buf[DUMP_HEADER_SIZE];
	unsigned char           *dump_buf;
	int                     buf_id;
	iokit_device_t *iokit_device = (iokit_device_t *)device;

#warning Only Support LTO Drive to take dump now

	/* Set transfer size */
	transfer_size = DUMP_TRANSFER_SIZE;
	dump_buf = calloc(1, DUMP_TRANSFER_SIZE);
	if(dump_buf == NULL){
		errno = ENOMEM;
		return -2;
	}

	/* Set buffer ID */
	if (iokit_device->device_code == IBM_3592) {
		buf_id = 0x00;
	}
	else {
		buf_id = 0x01;
	}

	/* Get buffer capacity */
	iokitosx_readbuffer(device, buf_id, cap_buf, 0, sizeof(cap_buf), 0x03);
	data_length = (cap_buf[1] << 16) + (cap_buf[2] << 8) + (int)cap_buf[3];

	/* Open dump file for write only*/
	dumpfd = open(fname, O_WRONLY|O_CREAT|O_TRUNC, 0666);
	if(dumpfd < 0){
		free(dump_buf);
		return -2;
	}

	/* get the total number of transfers */
	num_transfers   = data_length / transfer_size;
	excess_transfer = data_length % transfer_size;
	if(excess_transfer)
		num_transfers += 1;

	/* start to transfer data */
	buf_offset = 0;
	i = 0;
	while(num_transfers)
	{
		int length;

		i++;

		/* Allocation Length is transfer_size or excess_transfer*/
		if(excess_transfer && num_transfers == 1)
			length = excess_transfer;
		else
			length = transfer_size;

		iokitosx_readbuffer(device, buf_id, dump_buf, buf_offset, length, 0x02);
		/* write buffer data into dump file */
		bytes = write(dumpfd, dump_buf, length);
		if(bytes == -1)
		{
			free(dump_buf);
			close(dumpfd);
			return -1;
		}

		if(bytes != length)
		{
			free(dump_buf);
			close(dumpfd);
			return -2;
		}

		/* update offset and num_transfers, free buffer */
		buf_offset += transfer_size;
		num_transfers -= 1;

	} /* end of while(num_transfers) */

	close(dumpfd);

	return rc;
}

int iokitosx_forcedump_drive(void *device)
{
	int ret = -1;

	ret = _cdb_forcedump(device);

	return ret;
}

int iokitosx_takedump_drive(void *device)
{
	char      fname_base[1024];
	char      fname[1024];
	time_t    now;
	struct tm *tm_now;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_TAKEDUMPDRV));

	/* Make base filename */
	time(&now);
	tm_now = localtime(&now);
	sprintf(fname_base, "/tmp/ltfs_%d_%02d%02d_%02d%02d%02d"
		, tm_now->tm_year+1900
		, tm_now->tm_mon+1
		, tm_now->tm_mday
		, tm_now->tm_hour
		, tm_now->tm_min
		, tm_now->tm_sec);

	strcpy(fname, fname_base);
	strcat(fname, ".dmp");

	iokitosx_getdump_drive(device, fname);

	iokitosx_forcedump_drive(device);
	strcpy(fname, fname_base);
	strcat(fname, "_f.dmp");
	iokitosx_getdump_drive(device, fname);

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_TAKEDUMPDRV));
	return 0;
}

static bool is_dump_required_error(void *device, int ret)
{
	bool ans = false;
	int err = -ret;
	iokit_device_t *iokit_device = (iokit_device_t *)device;

	if (err == EDEV_NO_SENSE || err == EDEV_OVERRUN) {
		/* Sense Key 0 situation. */
		/* Drive may not exist or may not be able to transfer any data. */
		/* Checking capability of data transfer by logsense. */
		char command_description[COMMAND_DESCRIPTION_LENGTH] = "LOG_SENSE";
		cdb_logsense cdb;
		uint8_t cdb_length = kSCSICDBSize_10Byte;

		uint8_t *buffer;
		size_t buffer_size;
		uint8_t transfer_direction;
		char *msg;

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
		cdb.page_code = 0x17;	// volume status
		cdb.subpage_code = 0;
		cdb.alloc_length[0] = (uint8_t)(buffer_size >> 8) & 0xff;
		cdb.alloc_length[1] = (uint8_t) buffer_size       & 0xff;

		transfer_direction = kSCSIDataTransfer_FromTargetToInitiator;

		ret = iokit_issue_cdb_command(iokit_device, (void *)&cdb, cdb_length,
									  buffer, buffer_size, transfer_direction,
									  ComputeTimeOut(iokit_device->device_code, LogSenseTimeOut),
									  command_description, &msg);

		ans = (ret == DEVICE_GOOD);
	}
	else if (err >= EDEV_NOT_READY && err < EDEV_INTERNAL_ERROR) {
		ans = true;
	}

	return ans;
}

extern struct iokit_global_data global_data;

void iokitosx_process_errors(void *device, int rc, char *msg, char *cmd, bool take_dump)
{
	iokit_device_t *iokit_device = (iokit_device_t *)device;

	if (msg != NULL) {
		ltfsmsg(LTFS_INFO, "12173I", cmd, msg, rc, iokit_device->device_name);
	}
	else {
		ltfsmsg(LTFS_ERR, "12174E", cmd, rc, iokit_device->device_name);
	}

	if (device) {
		if (take_dump
			&& !global_data.disable_auto_dump
			&& is_dump_required_error(device, rc)) {
			(void)iokitosx_takedump_drive(device);
		}
	}
}
