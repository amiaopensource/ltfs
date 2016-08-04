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
** FILE NAME:       tape_drivers/osx/iokit/iokit_singledrive.c
**
** DESCRIPTION:     LTFS backend implementation for OS X using
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


#define __iokit_singledrive_c

#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>

#include "ltfs_copyright.h"
#include "scsi_command_blocks.h"
#include "libltfs/ltfslogging.h"
#include "libltfs/fs.h"
#include "libltfs/ltfs_endian.h"
#include "libltfs/arch/time_internal.h"
#include "kmi/key_format_ltfs.h"
#include "reed_solomon_crc.h"
#include "crc32c_crc.h"

#include "device_identifiers.h"
#include "iokit_common.h"
#include "iokit_scsi_base.h"
#include "iokit_scsi_operations.h"
#include "iokit_singledrive.h"
#include "scsi_command_operation_codes.h"

#include "libltfs/ltfs_fuse_version.h"
#include <fuse.h>

volatile char *copyright = LTFS_COPYRIGHT_0"\n"LTFS_COPYRIGHT_1"\n"LTFS_COPYRIGHT_2"\n" \
	LTFS_COPYRIGHT_3"\n"LTFS_COPYRIGHT_4"\n"LTFS_COPYRIGHT_5"\n";

// Default IOKit device name
const char *iokit_default_device = "0";

// Global values
struct iokit_global_data global_data;

// Function prototypes
int32_t _cdb_locate(void *device, struct tc_position *dest, struct tc_position *pos);

int32_t _cdb_readpos(void *device, uint8_t *buf, size_t buf_size);

int32_t _cdb_read(void *device, uint8_t *buf, size_t size, boolean_t sili);

int32_t _cdb_setcap(void *device, uint16_t proportion);

int32_t _cdb_format(void *device, TC_FORMAT_TYPE format);

int32_t _cdb_sense(void *device, const uint8_t page, const TC_MP_PC_TYPE pc,
				   const uint8_t subpage, uint8_t *buf, const size_t size);

int32_t _cdb_select(void *device, uint8_t *buf, size_t size);

int32_t _cdb_readattribute(void *device, const tape_partition_t partition,
						   const uint16_t id, uint8_t *buffer, size_t size);

int32_t _cdb_writeattribute(void *device, const tape_partition_t partition,
							const uint8_t *buffer, size_t size);

int32_t _cdb_allow_overwrite(void *device, const struct tc_position pos);

// Forward references
int32_t iokitosx_modesense(void *device, const uint8_t page, const TC_MP_PC_TYPE pc, const uint8_t subpage,
						 uint8_t *buf, const size_t size);
int32_t iokitosx_set_key(void *device, const unsigned char *keyalias, const unsigned char *key);
int32_t iokitosx_set_lbp(void *device, bool enable);

// Definitions
#define LOG_PAGE_HEADER_SIZE      (4)
#define LOG_PAGE_PARAMSIZE_OFFSET (3)
#define LOG_PAGE_PARAM_OFFSET     (4)

#define IOKIT_MAX_BLOCK_SIZE (1 * MB)
#define MIN(a, b) ((a) < (b) ? (a) : (b))

static inline int32_t _parse_logPage(const uint8_t *logdata,
									 const uint16_t param, uint32_t *param_size,
									 uint8_t *buf, const size_t bufsize)
{
	uint16_t page_len, param_code, param_len;
	uint32_t i;
	uint32_t ret = -1;

	page_len = ((uint16_t)logdata[2] << 8) + (uint16_t)logdata[3];
	i = LOG_PAGE_HEADER_SIZE;

	while(i < page_len)
	{
		param_code = ((uint16_t)logdata[i] << 8) + (uint16_t)logdata[i+1];
		param_len  = (uint16_t)logdata[i + LOG_PAGE_PARAMSIZE_OFFSET];

		if(param_code == param)
		{
			*param_size = param_len;
			if(bufsize < param_len){
				memcpy(buf, &logdata[i + LOG_PAGE_PARAM_OFFSET], bufsize);
				ret = -2;
				break;
			} else {
				memcpy(buf, &logdata[i + LOG_PAGE_PARAM_OFFSET], param_len);
				ret = 0;
				break;
			}
		}
		i += param_len + LOG_PAGE_PARAM_OFFSET;
	}

out:
	return ret;
}


int32_t iokitosx_allow_medium_removal(void *device)
{
	int32_t ret = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_ALLOWMREM));
	ret = iokit_prevent_medium_removal(iokit_device, false);
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_ALLOWMREM));

	return ret;
}


/**
 * Parse option for iokit driver
 * Option value cannot be stored into device instance because option parser calls
 * before opening a device.
 * @param devname device name of the iokit
 * @return a pointer to the iokit backend on success or NULL on error
 */

#define iokitosx_OPT(templ,offset,value) { templ, offsetof(struct iokit_global_data, offset), value }

static struct fuse_opt iokitosx_global_opts[] = {
	iokitosx_OPT("scsi_lbprotect=%s", str_crc_checking, 0),
	iokitosx_OPT("strict_drive",      strict_drive, 1),
	iokitosx_OPT("nostrict_drive",    strict_drive, 0),
	iokitosx_OPT("autodump",          disable_auto_dump, 0),
	iokitosx_OPT("noautodump",        disable_auto_dump, 1),
	FUSE_OPT_END
};

static int null_parser(void *priv, const char *arg, int key, struct fuse_args *outargs)
{
	return 1;
}

int iokitosx_parse_opts(void *device, void *opt_args)
{
	struct fuse_args *args = (struct fuse_args *) opt_args;
	int ret;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_PARSEOPTS));

	if (! device) {
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_PARSEOPTS));
		return -EINVAL;
	}

	ret = fuse_opt_parse(args, &global_data, iokitosx_global_opts, null_parser);
	if (ret < 0) {
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_PARSEOPTS));
		return ret;
	}

	/* Validate scsi logical block protection */
	if (global_data.str_crc_checking) {
		if (strcasecmp(global_data.str_crc_checking, "on") == 0)
			global_data.crc_checking = 1;
		else if (strcasecmp(global_data.str_crc_checking, "off") == 0)
			global_data.crc_checking = 0;
		else {
			ltfsmsg(LTFS_ERR, "12220E", global_data.str_crc_checking);
			ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_PARSEOPTS));
			return -EINVAL;
		}
	} else
		global_data.crc_checking = 0;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_PARSEOPTS));
	return 0;
}

int32_t iokitosx_open(const char *devname, void **handle)
{
	char *end;
	int32_t drive_number;
	int32_t device_code;
	int32_t ret = -1;

	scsi_device_identifier identifier;
	iokit_device_t *iokit_device; // Pointer to device status structure

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_OPEN));

	ltfsmsg(LTFS_INFO, "12158I", devname);

	iokit_device = malloc(sizeof(iokit_device_t));

	if(iokit_device == NULL) {
		ltfsmsg(LTFS_ERR, "10001E", "(iokit) iokitosx_open: device private data");
		errno = ENOMEM;
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_OPEN));
		return -EDEV_NO_MEMORY;
	}

	bzero(iokit_device, sizeof(iokit_device_t));

	if(iokit_device)
	{
		errno = 0;
		drive_number = strtoul(devname, &end, 10);
		if(errno || (*end != '\0')) {
			ltfsmsg(LTFS_INFO, "12163I", devname);
			errno = EDEVERR;
			ret = -EDEV_DEVICE_UNOPENABLE;
			goto free;
		}
	}

	iokit_device->exclusive_lock = false;

	if(iokit_find_ssc_device(iokit_device, drive_number) != 0)
	{
		errno = EDEVERR;
		ret = -EDEV_DEVICE_UNOPENABLE;
		goto free;
	}

	ret = iokit_obtain_exclusive_access(iokit_device);
	if(ret != 0) {
		errno = EPERM;
		goto free;
	}

	device_code = iokit_identify_device_code(iokit_device, &identifier);

	if(device_code > 0) {
		iokit_device->device_code = device_code;
		iokit_device->drive_type = identifier.drive_type;
	} else {
		ltfsmsg(LTFS_INFO, "12117I", identifier.product_id);
		ret = iokit_release_exclusive_access(iokit_device);
		if(ret != 0) {
			errno = EDEVERR;
			ret = -EDEV_DEVICE_UNSUPPORTABLE;
		}
		else {
			errno = EBADF; /* Unsupportd device */
			ret = -EDEV_DEVICE_UNSUPPORTABLE; /* Unsupported device */
		}
		if (device_code == -EDEV_UNSUPPORTED_FIRMWARE)
			ret = -EDEV_UNSUPPORTED_FIRMWARE;
		goto free;
	}

	/* Initialize drive state variables */
	iokit_device->deferred_eom = false;

out:
	*handle = (void *)iokit_device;
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_OPEN));
	return ret;

free:
	free(iokit_device);
	iokit_device = NULL;
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_OPEN));
	return ret;
}

int32_t iokitosx_reopen(const char *devname, void *device)
{
	char *end;
	int32_t drive_number;
	int32_t device_code;
	int32_t ret = -1;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_REOPEN));

	CHECK_ARG_NULL(device, -LTFS_NULL_ARG);

	iokit_device_t *iokit_device = (iokit_device_t*)device;
	scsi_device_identifier identifier;

	ltfsmsg(LTFS_INFO, "12214I", devname);

	errno = 0;
	drive_number = strtoul(devname, &end, 10);
	if(errno || (*end != '\0')) {
		ltfsmsg(LTFS_INFO, "12163I", devname);
		errno = EDEVERR;
		ret = -EDEV_DEVICE_UNOPENABLE;
		goto out;
	}

	iokit_device->exclusive_lock = false;

	if(iokit_find_ssc_device(iokit_device, drive_number) != 0)
	{
		errno = EDEVERR;
		ret = -EDEV_DEVICE_UNOPENABLE;
		goto out;
	}

	ret = iokit_obtain_exclusive_access(iokit_device);
	if(ret != 0) {
		errno = EPERM;
		goto out;
	}

	device_code = iokit_identify_device_code(iokit_device, &identifier);

	if(device_code > 0) {
		iokit_device->device_code = device_code;
		iokit_device->drive_type = identifier.drive_type;
	} else {
		ltfsmsg(LTFS_INFO, "12117I", identifier.product_id);
		ret = iokit_release_exclusive_access(iokit_device);
		if(ret != 0) {
			errno = EDEVERR;
			ret = -EDEV_DEVICE_UNSUPPORTABLE;
		}
		else {
			errno = EBADF; /* Unsupportd device */
			ret = -EDEV_DEVICE_UNSUPPORTABLE; /* Unsupported device */
		}
		if (device_code == -EDEV_UNSUPPORTED_FIRMWARE)
			ret = -EDEV_UNSUPPORTED_FIRMWARE;
		goto out;
	}

out:
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_REOPEN));
	return ret;
}

int32_t iokitosx_close(void *device)
{
	int32_t ret = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_CLOSE));

	iokitosx_set_lbp(device, false);

	if(iokit_device->exclusive_lock)
		ret = iokit_release_exclusive_access(iokit_device);

	ret = iokit_free_device(device);

	free(device);
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_CLOSE));
	return ret;
}


int32_t iokitosx_close_raw(void *device)
{
	int32_t ret = 0;
	/* This operation is called only after resource is forked. */
	/* On OSX environment, this operation is not required      */
	/* because file discripter is not inherited.               */
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_CLOSERAW));
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_CLOSERAW));
	return ret;
}


int iokitosx_is_connected(const char *devname)
{
	/*
	 * Temporary return false here.
	 * Current iokit driver uses index number as a devname and this
	 * index may be changed by drive hotplug.
	 * However LTFS's library code is assuming fixed devname
	 * during running LTFS even though drive plug/unplug.
	 */
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_ISCONNECTED));
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_ISCONNECTED));
	return false;
}

int32_t iokitosx_read(void *device, char *buf, size_t size,
					struct tc_position *pos, const bool unusual_size)
{
	int32_t ret = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;
	size_t datacount = size;
	struct tc_position pos_retry = {0, 0};

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_READ));
	ltfsmsg(LTFS_DEBUG3, "12150D", "read", size);

	if(global_data.crc_checking) {
		datacount = size + 4;
		/* Never fall into this block, fail safe to adjust record length*/
		if (datacount > IOKIT_MAX_BLOCK_SIZE)
			datacount = IOKIT_MAX_BLOCK_SIZE;
	}

	ret = _cdb_read(iokit_device, (uint8_t *)buf, datacount, unusual_size);
	if ( !(pos->block) && unusual_size && ret == size) {
		/*
		 *  Try to read again without sili bit, because some I/F doesn't support SILION read correctly
		 *  like USB connected LTO drive.
		 *  This recovery procedure is executed only when reading VOL1 on both partiton. Once this memod
		 *  is completed successfully, the iokit backend uses SILI off read always.
		 */
		pos_retry.partition = pos->partition;
		ret = iokitosx_locate(device, pos_retry, pos);
		if (ret) {
			ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_READ));
			return ret;
		}
		iokit_device->use_sili = false;
		ret = _cdb_read(iokit_device, (uint8_t *)buf, datacount, unusual_size);
	}

	if(ret == -EDEV_FILEMARK_DETECTED)
	{
		pos->filemarks++;
		ret = DEVICE_GOOD;
	}

	if(ret >= 0) {
		pos->block++;
		if(global_data.crc_checking && ret > 4) {
			if (iokit_device->f_crc_check)
				ret = iokit_device->f_crc_check(buf, ret - 4);
			if (ret < 0) {
				ltfsmsg(LTFS_ERR, "12201E");
				ret = -EDEV_LBP_READ_ERROR;
			}
		}
	}

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_READ));
	return ret;
}


int32_t iokitosx_locate(void *device, struct tc_position dest, struct tc_position *pos)
{
	int32_t rc = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_LOCATE));
	ltfsmsg(LTFS_DEBUG, "12152D", "locate", (unsigned long long)dest.partition,
		(unsigned long long)dest.block);

	rc = _cdb_locate(iokit_device, &dest, pos);

	if(rc >= 0)
		rc = iokitosx_readpos(device, pos);

	if(rc >= 0) {
		if(pos->early_warning)
			ltfsmsg(LTFS_WARN, "12074W", "locate");
		else if(pos->programmable_early_warning)
			ltfsmsg(LTFS_WARN, "12075W", "locate");
	}

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_LOCATE));
	return rc;
}


int32_t iokitosx_erase(void *device, struct tc_position *pos, bool long_erase)
{
	int32_t ret = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;
	uint8_t long_bit = 0, immed_bit = 0;
	int device_code = iokit_device->device_code;
	struct ltfs_timespec ts_start, ts_now;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_ERASE));

	if (long_erase) {
		ltfsmsg(LTFS_DEBUG, "12153D", "long", "erase");
		long_bit = 1;
		immed_bit = 1;
	}
	else {
		ltfsmsg(LTFS_DEBUG, "12153D", "short", "erase");
	}

	get_current_timespec(&ts_start);

	/* Invoke immediate long erase or non-immediate short erase */
	ret = iokit_erase(iokit_device, long_bit, immed_bit);
	if (long_erase) {
		char sense_buf[MAXSENSE];
		uint32_t sense_data;
		uint32_t progress;

		while (ret == 0) {
			memset(sense_buf, 0, sizeof(sense_buf));
			ret= iokit_request_sense(iokit_device, sense_buf, MAXSENSE);

			sense_data = ((uint32_t) sense_buf[2] & 0x0F) << 16;
			sense_data += ((uint32_t) sense_buf[12] & 0xFF) << 8;
			sense_data += ((uint32_t) sense_buf[13] & 0xFF);

			if (sense_data != 0x000016 && sense_data != 0x000018) {
				/* Erase operation is NOT in progress */
				break;
			}

			if (device_code == IBM_3592) {
				get_current_timespec(&ts_now);
				ltfsmsg(LTFS_INFO, "12224I", (ts_now.tv_sec - ts_start.tv_sec)/60);
			}
			else {
				progress = ((uint32_t) sense_buf[16] & 0xFF) << 8;
				progress += ((uint32_t) sense_buf[17] & 0xFF);
				ltfsmsg(LTFS_INFO, "12225I", (progress*100/0xFFFF));
			}

			sleep(60);
		}
	}

	iokitosx_readpos(device, pos);

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_ERASE));
	return ret;
}


int32_t iokitosx_get_parameters(void *device, struct tc_drive_param *drive_param)
{
	int32_t ret = -1;
	all_device_params params;
	iokit_device_t *iokit_device = (iokit_device_t*)device;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_GETPARAM));

	ret = iokit_get_media_parameters(iokit_device, &params);

	if(ret < 0)
		goto out;

	if (global_data.crc_checking)
		drive_param->max_blksize = MIN(params.max_blksize, IOKIT_MAX_BLOCK_SIZE - 4);
	else
		drive_param->max_blksize = MIN(params.max_blksize, IOKIT_MAX_BLOCK_SIZE);

	drive_param->write_protect = params.write_protect;
	drive_param->logical_write_protect = params.logical_write_protect;

out:
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_GETPARAM));
	return ret;
}


int32_t iokitosx_inquiry(void *device, struct tc_inq *inq)
{
	int32_t ret = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;
	inquiry_data inquiry_buf;
	int vendor_length;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_INQUIRY));

	ret = iokit_std_inquiry(iokit_device, (void *)&inquiry_buf, sizeof(inquiry_data));
	if( ret < 0 ) {
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_INQUIRY));
		return ret;
	}

	inq->devicetype = PERIPHERAL_DEVICE_TYPE((&inquiry_buf.standard));
	inq->cmdque = CmdQue((&inquiry_buf.standard));

	strncpy((char *) inq->vid, (char *) inquiry_buf.standard.vendor_identification, 8);
	inq->vid[8] = '\0';

	strncpy((char *) inq->pid, (char *) inquiry_buf.standard.product_identification, 16);
	inq->pid[16] = '\0';

	strncpy((char *) inq->revision, (char *) inquiry_buf.standard.product_revision_level, 4);
	inq->revision[4] = '\0';

	if (iokit_device->device_code == IBM_3592) {
		vendor_length = 18;
	}
	else {
		vendor_length = 20;
	}

	strncpy((char *) inq->vendor, (char *) inquiry_buf.vendor_specific, vendor_length);
	inq->vendor[vendor_length] = '\0';

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_INQUIRY));
	return 0;
}


int iokitosx_inquiry_page(void *device, unsigned char page, struct tc_inq_page *inq)
{
	int ret;
	iokit_device_t *iokit_device = (iokit_device_t*)device;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_INQUIRYPAGE));

	if (! inq) {
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_INQUIRYPAGE));
		return -EINVAL;
	}

	inq->page_code = page;
	ret = iokit_inquiry(iokit_device, page, inq->data, sizeof(inq->data));

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_INQUIRYPAGE));
	return ret;
}

/* Cartridge type in mode page header */
enum {
	TC_MP_LTO1D_CART  = 0x18,   /* LTO1 Data cartridge */
	TC_MP_LTO2D_CART  = 0x28,   /* LTO2 Data cartridge */
	TC_MP_LTO3D_CART  = 0x38,   /* LTO3 Data cartridge */
	TC_MP_LTO4D_CART  = 0x48,   /* LTO4 Data cartridge */
	TC_MP_LTO5D_CART  = 0x58,   /* LTO5 Data cartridge */
	TC_MP_LTO6D_CART  = 0x68,   /* LTO6 Data cartridge */
	TC_MP_LTO3W_CART  = 0x3C,   /* LTO3 WORM cartridge */
	TC_MP_LTO4W_CART  = 0x4C,   /* LTO4 WORM cartridge */
	TC_MP_LTO5W_CART  = 0x5C,   /* LTO5 WORM cartridge */
	TC_MP_LTO6W_CART  = 0x6C,   /* LTO6 WORM cartridge */
	TC_MP_JA          = 0x91,   /* Jaguar JA cartridge */
	TC_MP_JW          = 0xA1,   /* Jaguar JW cartridge */
	TC_MP_JJ          = 0xB1,   /* Jaguar JJ cartridge */
	TC_MP_JR          = 0xC1,   /* Jaguar JR cartridge */
	TC_MP_JB          = 0x92,   /* Jaguar JB cartridge */
	TC_MP_JX          = 0xA2,   /* Jaguar JX cartridge */
	TC_MP_JC          = 0x93,   /* Jaguar JC cartridge */
	TC_MP_JK          = 0xB2,   /* Jaguar JK cartridge */
	TC_MP_JD          = 0x94,   /* Jaguar JD cartridge */
	TC_MP_JL          = 0xB3,   /* Jaguar JL cartridge */
};

int supported_cart[] = {
	TC_MP_LTO6D_CART,
	TC_MP_LTO5D_CART,
	TC_MP_JB,
	TC_MP_JC,
	TC_MP_JD,
	TC_MP_JK,
	TC_MP_JL
};

int32_t iokitosx_load(void *device, struct tc_position *pos)
{
	int32_t ret = -1;
	int32_t i;;
	iokit_device_t *iokit_device = (iokit_device_t*)device;
	unsigned char buf[TC_MP_SUPPORTEDPAGE_SIZE];

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_LOAD));

	ret = iokit_load_unload(iokit_device, false, true);
	iokitosx_readpos(device, pos);
	if (ret < 0) {
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_LOAD));
		return ret;
	}

	iokit_device->tape_alert = 0;

	ret = iokitosx_modesense(device, TC_MP_SUPPORTEDPAGE, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
	if (ret < 0) {
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_LOAD));
		return ret;
	}

	ret = -LTFS_UNSUPPORTED_MEDIUM;
	for (i = 0; i < sizeof(supported_cart)/sizeof(supported_cart[0]); ++i) {
		if(buf[2] == supported_cart[i]) {
			ret = 0;
			break;
		}
	}

	if(ret == -LTFS_UNSUPPORTED_MEDIUM)
		ltfsmsg(LTFS_INFO, "12157I", buf[2]);

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_LOAD));
	return ret;
}


int32_t iokitosx_unload(void *device, struct tc_position *pos)
{
	int32_t ret = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_UNLOAD));

	ret = iokit_load_unload(iokit_device, false, false);
	iokit_device->tape_alert = 0;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_UNLOAD));
	return ret;
}

int32_t iokitosx_readpos(void *device, struct tc_position *pos)
{
	int32_t ret = -1;
	uint8_t buf[32];

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_READPOS));

	bzero(buf, sizeof(buf));
	ret = _cdb_readpos(device, buf, sizeof(buf));

	if(ret == 0) {
		// TODO: switch to using eight_bytes_to_int()
		pos->partition = ((tape_partition_t)buf[4] << 24) + ((tape_partition_t)buf[5] << 16)
					   + ((tape_partition_t)buf[6] << 8) + (tape_partition_t)buf[7];

		pos->block     = ((tape_block_t)buf[8] << 56) + ((tape_block_t)buf[9] << 48)
					   + ((tape_block_t)buf[10] << 40) + ((tape_block_t)buf[11] << 32)
					   + ((tape_block_t)buf[12] << 24) + ((tape_block_t)buf[13] << 16)
					   + ((tape_block_t)buf[14] << 8) + (tape_block_t)buf[15];

		pos->filemarks = ((tape_block_t)buf[16] << 56) + ((tape_block_t)buf[17] << 48)
					   + ((tape_block_t)buf[18] << 40) + ((tape_block_t)buf[19] << 32)
					   + ((tape_block_t)buf[20] << 24) + ((tape_block_t)buf[21] << 16)
					   + ((tape_block_t)buf[22] << 8) + (tape_block_t)buf[23];
	}

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_READPOS));
	return ret;
}


int32_t iokitosx_setcap(void *device, uint16_t proportion)
{
	int32_t ret = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;
	int device_code = iokit_device->device_code;
	unsigned char buf[TC_MP_MEDIUM_SENSE_SIZE];

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_SETCAP));

	if (device_code == IBM_3592) {
		/* scale media insetad of setcap */
		ret = iokitosx_modesense(device, TC_MP_MEDIUM_SENSE, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
		if (ret < 0)
			goto out;

		if (buf[2] == TC_MP_JK || buf[2] == TC_MP_JL) {
			/* JK media cannot be scaled */
			goto out;
		}

		buf[0]   = 0x00;
		buf[1]   = 0x00;
		buf[27] |= 0x01;
		buf[28]  = 0x00;

		ret = iokitosx_modeselect(device, buf, sizeof(buf));
	}
	else {
		ret = _cdb_setcap(iokit_device, proportion);
	}

out:
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_SETCAP));
	return ret;
}


int32_t iokitosx_format(void *device, TC_FORMAT_TYPE format)
{
	int32_t ret = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_FORMAT));
	ret = _cdb_format(iokit_device, format);
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_FORMAT));
	return ret;
}


int32_t iokitosx_logsense_page(void *device, const uint8_t page, const uint8_t subpage, uint8_t *buf, const size_t size)
{
	int32_t ret = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;


	ltfsmsg(LTFS_DEBUG3, "12156D", "logsense", page, "");
	ret = iokit_log_sense_page(iokit_device, page, subpage, buf, size);

	return ret;
}


int32_t iokitosx_logsense(void *device, const uint8_t page, uint8_t *buf, const size_t size)
{
	int ret = 0;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_LOGSENSE));
	ret = iokitosx_logsense_page(device, page, 0, buf, size);
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_LOGSENSE));
	return ret;
}


int32_t iokitosx_modesense(void *device, const uint8_t page, const TC_MP_PC_TYPE pc, const uint8_t subpage,
						 uint8_t *buf, const size_t size)
{
	int32_t ret = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_MODESENSE));
	ltfsmsg(LTFS_DEBUG3, "12156D", "modesense", page, "");
	ret = _cdb_sense(iokit_device, page, pc, subpage, buf, size);
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_MODESENSE));
	return ret;
}

int32_t iokitosx_modeselect(void *device, unsigned char *buf, const size_t size)
{
	int32_t ret = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_MODESELECT));
	ltfsmsg(LTFS_DEBUG3, "12153D", "modeselect", "");
	ret = _cdb_select(iokit_device, buf, size);
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_MODESELECT));
	return ret;
}

#define LOG_VOLUMESTATS         (0x17)
enum {
	VOLSTATS_MOUNTS           = 0x0001,	/* < Volume Mounts */
	VOLSTATS_WRITTEN_DS       = 0x0002,	/* < Volume Written DS */
	VOLSTATS_WRITE_TEMPS      = 0x0003,	/* < Volume Temp Errors on Write */
	VOLSTATS_WRITE_PERMS      = 0x0004,	/* < Volume Perm Errors_on Write */
	VOLSTATS_READ_DS          = 0x0007,	/* < Volume Read DS */
	VOLSTATS_READ_TEMPS       = 0x0008,	/* < Volume Temp Errors on Read */
	VOLSTATS_READ_PERMS       = 0x0009,	/* < Volume Perm Errors_on Read */
	VOLSTATS_WRITE_PERMS_PREV = 0x000C,	/* < Volume Perm Errors_on Write (previous mount)*/
	VOLSTATS_READ_PERMS_PREV  = 0x000D,	/* < Volume Perm Errors_on Read (previous mount) */
	VOLSTATS_WRITE_MB         = 0x0010,	/* < Volume Written MB */
	VOLSTATS_READ_MB          = 0x0011,	/* < Volume Read MB */
	VOLSTATS_PASSES_BEGIN     = 0x0101,	/* < Beginning of medium passes */
	VOLSTATS_PASSES_MIDDLE    = 0x0102,	/* < Middle of medium passes */
	VOLSTATS_PARTITION_CAP    = 0x0202,	/* < Native capacity of partitions */
	VOLSTATS_PART_USED_CAP    = 0x0203,	/* < Used capacity of partitions */
	VOLSTATS_PART_REMAIN_CAP  = 0x0204,	/* < Remaining capacity of partitions */
};

#define PARTITIOIN_REC_HEADER_LEN (4)

#define LOG_TAPECAPACITY         0x31

enum {
	TAPECAP_REMAIN_0 = 0x0001, /*< Partition0 Remaining Capacity */
	TAPECAP_REMAIN_1 = 0x0002, /*< Partition1 Remaining Capacity */
	TAPECAP_MAX_0    = 0x0003, /*< Partition0 MAX Capacity */
	TAPECAP_MAX_1    = 0x0004, /*< Partition1 MAX Capacity */
	TAPECAP_SIZE     = 0x0005,
};

int32_t iokitosx_remaining_capacity(void *device, struct tc_remaining_cap *cap)
{

	int32_t scsi_ret = -1;
	int32_t ret = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;
	int drive_type = iokit_device->drive_type;
	uint8_t buffer[LOGSENSEPAGE];
	uint8_t buf[32];
	uint32_t param_size;
	int32_t i;
	uint32_t logcap;
	int offset, length;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_REMAINCAP));

	bzero(&buffer, LOGSENSEPAGE);

	if (drive_type == DRIVE_LTO5 || drive_type == DRIVE_LTO5_HH) {
		// Use LogPage 0x31
		scsi_ret = iokitosx_logsense(iokit_device, (uint8_t)LOG_TAPECAPACITY, (void *)buffer, LOGSENSEPAGE);

		if(scsi_ret != 0)
		{
			ltfsmsg(LTFS_INFO, "12135I", LOG_VOLUMESTATS, scsi_ret);
			ret = -1;
			goto out;
		}

		for( i = TAPECAP_REMAIN_0; i < TAPECAP_SIZE; i++)
		{
			if(_parse_logPage(buffer, (uint16_t)i, &param_size, buf, sizeof(buf)) || param_size != sizeof(uint32_t))
			{
				ret = -1;
				goto out;
			}

			logcap = ((uint32_t)buf[0] << 24) + ((uint32_t)buf[1] << 16)
				   + ((uint32_t)buf[2] << 8) + (uint32_t)buf[3];

			switch(i)
			{
				case TAPECAP_REMAIN_0:
					cap->remaining_p0 = logcap;
					break;
				case TAPECAP_REMAIN_1:
					cap->remaining_p1 = logcap;
					break;
				case TAPECAP_MAX_0:
					cap->max_p0 = logcap;
					break;
				case TAPECAP_MAX_1:
					cap->max_p1 = logcap;
					break;
				default:
					ret = -2;
					goto out;
					break;
			}
		}
		ret = 0;
	}
	else {
		// Use LogPage 0x17
		scsi_ret = iokitosx_logsense(iokit_device, LOG_VOLUMESTATS, (void *)buffer, LOGSENSEPAGE);
		if(scsi_ret != 0)
		{
			ltfsmsg(LTFS_INFO, "12135I", LOG_VOLUMESTATS, scsi_ret);
			ret = -1;
			goto out;
		}

		if (_parse_logPage(buffer, (uint16_t)VOLSTATS_PARTITION_CAP, &param_size, buf, sizeof(buf))) {
			ltfsmsg(LTFS_INFO, "12136I");
			ret = -1;
			goto out;
		}

		memset(cap, 0, sizeof(struct tc_remaining_cap));

		cap->max_p0 = ltfs_betou32(&buf[PARTITIOIN_REC_HEADER_LEN]);
		offset = (int)buf[0] + 1;
		length = (int)buf[offset] + 1;

		if (offset + length <= param_size) {
			cap->max_p1 = ltfs_betou32(&buf[offset + PARTITIOIN_REC_HEADER_LEN]);
		}

		if (_parse_logPage(buffer, (uint16_t)VOLSTATS_PART_REMAIN_CAP, &param_size, buf, sizeof(buf))) {
			ltfsmsg(LTFS_INFO, "12136I");
			ret = -1;
			goto out;
		}

		cap->remaining_p0 = ltfs_betou32(&buf[PARTITIOIN_REC_HEADER_LEN]);
		offset = (int)buf[0] + 1;
		length = (int)buf[offset] + 1;

		if (offset + length <= param_size) {
			cap->remaining_p1 = ltfs_betou32(&buf[offset + PARTITIOIN_REC_HEADER_LEN]);
		}

		/* Convert MB to MiB -- Need to consider about overflow when max cap reaches to 18EB */
		cap->max_p0 = (cap->max_p0 * 1000 * 1000) >> 20;
		cap->max_p1 = (cap->max_p1 * 1000 * 1000) >> 20;
		cap->remaining_p0 = (cap->remaining_p0 * 1000 * 1000) >> 20;
		cap->remaining_p1 = (cap->remaining_p1 * 1000 * 1000) >> 20;

		ret = 0;
	}
out:
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_REMAINCAP));
	return ret;
}

int32_t iokitosx_reserve_unit(void *device)
{
	int32_t ret = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_RESERVEUNIT));
	ret = iokit_reserve_unit(iokit_device);
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_RESERVEUNIT));

	return ret;
}

int32_t iokitosx_release_unit(void *device)
{
	int32_t ret = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_RELEASEUNIT));
	ret = iokit_release_unit(iokit_device);
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_RELEASEUNIT));

    return ret;
}

int32_t iokitosx_prevent_medium_removal(void *device)
{
	int32_t ret = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_PREVENTM));
	ret = iokit_prevent_medium_removal(iokit_device, true);
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_PREVENTM));

	return ret;
}


int32_t iokitosx_read_attribute(void *device, const tape_partition_t part,
						      const uint16_t id, uint8_t *buf, const size_t size)
{
	int32_t ret = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_READATTR));
	ltfsmsg(LTFS_DEBUG3, "12152D", "readattr", (unsigned long)part, id);
	ret = _cdb_readattribute(iokit_device, part, id, buf, size);
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_READATTR));

	return ret;
}

int32_t iokitosx_allow_overwrite(void *device, const struct tc_position pos)
{
	int32_t ret = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_ALLOWOVERW));
	ltfsmsg(LTFS_DEBUG, "12152D", "allow overwrite", (unsigned long long)pos.partition,
		(unsigned long long)pos.block);

	ret = _cdb_allow_overwrite(iokit_device, pos);
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_ALLOWOVERW));

	return DEVICE_GOOD;
}

int32_t iokitosx_report_density(void *device, struct tc_density_report *rep, bool medium)
{
	int32_t ret = -1;
	int32_t i;
	iokit_device_t *iokit_device = (iokit_device_t*)device;
	report_density report_buffer;
	uint8_t report_count = TC_MAX_DENSITY_REPORTS;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_REPDENSITY));
	rep->size = 0;
	ret = iokit_report_density(iokit_device, CURRENT_MEDIA_DENSITY, &report_count, &report_buffer);
	if(ret == 0) {
		rep->size = report_count;
		for(i = 0; i < report_count; i++) {
			rep->density[i].primary   = report_buffer.descriptor[i].primary_density;
			rep->density[i].secondary = report_buffer.descriptor[i].second_density;
		}
	}
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_REPDENSITY));
	return ret;
}

int32_t iokitosx_rewind(void *device, struct tc_position *pos)
{
	int32_t ret = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_REWIND));
	ret = iokit_rewind(iokit_device, 0);
	iokitosx_readpos(device, pos);
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_REWIND));

	return ret;
}

int32_t iokitosx_set_compression(void *device, const bool enable_compression, struct tc_position *pos)
{
	int32_t ret = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_SETCOMPRS));
	ret = iokit_set_compression(iokit_device, enable_compression);
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_SETCOMPRS));

	return ret;
}

int32_t iokitosx_set_default(void *device)
{
	int32_t ret = -1;
	struct _all_device_params params;
	iokit_device_t *iokit_device = (iokit_device_t*)device;

	params.blksize = 0;
	iokit_device->read_past_filemark = 0;
	iokit_device->use_sili = true;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_SETDEFAULT));
	ret = iokit_set_media_parameters(iokit_device, mediaSet_BlockSizeOnly, &params);
	if (ret < 0)
		goto out;

	/* Disable Read across EOD on 3592 drive */
	if (iokit_device->device_code == IBM_3592) {
		unsigned char buf[TC_MP_READ_WRITE_CTRL_SIZE];
		ltfsmsg(LTFS_DEBUG, "12153D", __FUNCTION__, "Disabling read across EOD");
		ret = iokitosx_modesense(device, TC_MP_READ_WRITE_CTRL, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
		if (ret < 0)
			goto out;

		buf[0]  = 0x00;
		buf[1]  = 0x00;
		buf[24] = 0x0C;

		ret = iokitosx_modeselect(device, buf, sizeof(buf));
		if (ret < 0)
			goto out;
	}

	/* set logical block protection */
	if (global_data.crc_checking) {
		ltfsmsg(LTFS_DEBUG, "12153D", __FUNCTION__, "Setting LBP");
		ret = iokitosx_set_lbp(device, true);
	}
	else {
		ltfsmsg(LTFS_DEBUG, "12153D", __FUNCTION__, "Resetting LBP");
		ret = iokitosx_set_lbp(device, false);
	}

out:
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_SETDEFAULT));
	return ret;
}

/**
 * Get cartridge health information
 * @param device a pointer to the ibmtape backend
 * @return 0 on success or a negative value on error
 */

#define LOG_TAPE_ALERT          (0x2E)
#define LOG_PERFORMANCE         (0x37)

#define LOG_PERFORMANCE_CAPACITY_SUB (0x64)	// Scope(7-6): Mount Values
											// Level(5-4): Return Advanced Counters
											// Group(3-0): Capacity
static uint16_t volstats[] = {
	VOLSTATS_MOUNTS,
	VOLSTATS_WRITTEN_DS,
	VOLSTATS_WRITE_TEMPS,
	VOLSTATS_WRITE_PERMS,
	VOLSTATS_READ_DS,
	VOLSTATS_READ_TEMPS,
	VOLSTATS_READ_PERMS,
	VOLSTATS_WRITE_PERMS_PREV,
	VOLSTATS_READ_PERMS_PREV,
	VOLSTATS_WRITE_MB,
	VOLSTATS_READ_MB,
	VOLSTATS_PASSES_BEGIN,
	VOLSTATS_PASSES_MIDDLE,
};

enum {
	PERF_CART_CONDITION       = 0x0001,	/* < Media Efficiency */
	PERF_ACTIVE_CQ_LOSS_W     = 0x7113,	/* < Active CQ loss Write */
};

static uint16_t perfstats[] = {
	PERF_CART_CONDITION,
};

int iokitosx_get_cartridge_health(void *device, struct tc_cartridge_health *cart_health)
{
	unsigned char logdata[LOGSENSEPAGE];
	unsigned char buf[16];
	int i;
	uint32_t param_size;
	uint64_t loghlt;
	int rc;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_GETCARTHLTH));

	/* Issue LogPage 0x37 */
	cart_health->tape_efficiency  = UNSUPPORTED_CARTRIDGE_HEALTH;
	rc = iokitosx_logsense(device, LOG_PERFORMANCE, logdata, LOGSENSEPAGE);
	if (rc)
		ltfsmsg(LTFS_INFO, "12217I", LOG_PERFORMANCE, rc, "get cart health");
	else {
		for(i = 0; i < (int)((sizeof(perfstats)/sizeof(perfstats[0]))); i++) {
			if (_parse_logPage(logdata, perfstats[i], &param_size, buf, 16)) {
				ltfsmsg(LTFS_INFO, "12218I", LOG_PERFORMANCE, "get cart health");
			} else {
				switch(param_size) {
				case sizeof(uint8_t):
					loghlt = (uint64_t)(buf[0]);
					break;
				case sizeof(uint16_t):
					loghlt = ((uint64_t)buf[0] << 8) + (uint64_t)buf[1];
					break;
				case sizeof(uint32_t):
					loghlt = ((uint64_t)buf[0] << 24) + ((uint64_t)buf[1] << 16)
						+ ((uint64_t)buf[2] << 8) + (uint64_t)buf[3];
					break;
				case sizeof(uint64_t):
					loghlt = ((uint64_t)buf[0] << 56) + ((uint64_t)buf[1] << 48)
						+ ((uint64_t)buf[2] << 40) + ((uint64_t)buf[3] << 32)
						+ ((uint64_t)buf[4] << 24) + ((uint64_t)buf[5] << 16)
						+ ((uint64_t)buf[6] << 8) + (uint64_t)buf[7];
					break;
				default:
					loghlt = UNSUPPORTED_CARTRIDGE_HEALTH;
					break;
				}

				switch(perfstats[i]) {
				case PERF_CART_CONDITION:
					cart_health->tape_efficiency = loghlt;
					break;
				default:
					break;
				}
			}
		}
	}

	/* Issue LogPage 0x17 */
	cart_health->mounts           = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->written_ds       = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->write_temps      = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->write_perms      = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->read_ds          = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->read_temps       = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->read_perms       = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->write_perms_prev = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->read_perms_prev  = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->written_mbytes   = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->read_mbytes      = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->passes_begin     = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->passes_middle    = UNSUPPORTED_CARTRIDGE_HEALTH;
	rc = iokitosx_logsense(device, LOG_VOLUMESTATS, logdata, LOGSENSEPAGE);
	if (rc)
		ltfsmsg(LTFS_INFO, "12217I", LOG_VOLUMESTATS, rc, "get cart health");
	else {
		for(i = 0; i < (int)((sizeof(volstats)/sizeof(volstats[0]))); i++) {
			if (_parse_logPage(logdata, volstats[i], &param_size, buf, 16)) {
				ltfsmsg(LTFS_INFO, "12218I", LOG_VOLUMESTATS, "get cart health");
			} else {
				switch(param_size) {
				case sizeof(uint8_t):
					loghlt = (uint64_t)(buf[0]);
					break;
				case sizeof(uint16_t):
					loghlt = ((uint64_t)buf[0] << 8) + (uint64_t)buf[1];
					break;
				case sizeof(uint32_t):
					loghlt = ((uint64_t)buf[0] << 24) + ((uint64_t)buf[1] << 16)
						+ ((uint64_t)buf[2] << 8) + (uint64_t)buf[3];
					break;
				case sizeof(uint64_t):
					loghlt = ((uint64_t)buf[0] << 56) + ((uint64_t)buf[1] << 48)
						+ ((uint64_t)buf[2] << 40) + ((uint64_t)buf[3] << 32)
						+ ((uint64_t)buf[4] << 24) + ((uint64_t)buf[5] << 16)
						+ ((uint64_t)buf[6] << 8) + (uint64_t)buf[7];
					break;
				default:
					loghlt = UNSUPPORTED_CARTRIDGE_HEALTH;
					break;
				}

				switch(volstats[i]) {
				case VOLSTATS_MOUNTS:
					cart_health->mounts = loghlt;
					break;
				case VOLSTATS_WRITTEN_DS:
					cart_health->written_ds = loghlt;
					break;
				case VOLSTATS_WRITE_TEMPS:
					cart_health->write_temps = loghlt;
					break;
				case VOLSTATS_WRITE_PERMS:
					cart_health->write_perms = loghlt;
					break;
				case VOLSTATS_READ_DS:
					cart_health->read_ds = loghlt;
					break;
				case VOLSTATS_READ_TEMPS:
					cart_health->read_temps = loghlt;
					break;
				case VOLSTATS_READ_PERMS:
					cart_health->read_perms = loghlt;
					break;
				case VOLSTATS_WRITE_PERMS_PREV:
					cart_health->write_perms_prev = loghlt;
					break;
				case VOLSTATS_READ_PERMS_PREV:
					cart_health->read_perms_prev = loghlt;
					break;
				case VOLSTATS_WRITE_MB:
					cart_health->written_mbytes = loghlt;
					break;
				case VOLSTATS_READ_MB:
					cart_health->read_mbytes = loghlt;
					break;
				case VOLSTATS_PASSES_BEGIN:
					cart_health->passes_begin = loghlt;
					break;
				case VOLSTATS_PASSES_MIDDLE:
					cart_health->passes_middle = loghlt;
					break;
				default:
					break;
				}
			}
		}
	}

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_GETCARTHLTH));
	return 0;
}

int iokitosx_get_tape_alert(void *device, uint64_t *tape_alert)
{
	unsigned char logdata[LOGSENSEPAGE];
	unsigned char buf[16];
	uint32_t param_size;
	int i;
	int rc;
	iokit_device_t *iokit_device = (iokit_device_t*)device;
	uint64_t ta;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_GETTAPEALT));

	/* Issue LogPage 0x2E */
	ta = 0;
	rc = iokitosx_logsense(device, LOG_TAPE_ALERT, logdata, LOGSENSEPAGE);
	if (rc)
		ltfsmsg(LTFS_INFO, "12217I", LOG_TAPE_ALERT, rc, "get tape alert");
	else {
		for(i = 1; i <= 64; i++) {
			if (_parse_logPage(logdata, (uint16_t) i, &param_size, buf, 16)
				|| param_size != sizeof(uint8_t)) {
				ltfsmsg(LTFS_INFO, "12218I", LOG_VOLUMESTATS, "get tape alert");
				ta = 0;
			}

			if(buf[0])
				ta += (uint64_t)(1) << (i - 1);
		}
	}

	iokit_device->tape_alert |= ta;
	*tape_alert = iokit_device->tape_alert;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_GETTAPEALT));
	return rc;
}

int iokitosx_clear_tape_alert(void *device, uint64_t tape_alert)
{
	iokit_device_t *iokit_device = (iokit_device_t*)device;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_CLRTAPEALT));
	iokit_device->tape_alert &= ~tape_alert;
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_CLRTAPEALT));
	return 0;
}

int iokitosx_get_xattr(void *device, const char *name, char **buf)
{
	int rc = -LTFS_NO_XATTR;
	unsigned char logdata[LOGSENSEPAGE];
	unsigned char logbuf[16];
	uint32_t param_size;
	uint32_t value32;
	iokit_device_t *iokit_device =(iokit_device_t*)device;
	struct ltfs_timespec now;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_GETXATTR));

	if (!strcmp(name, "ltfs.vendor.IBM.mediaCQsLossRate"))
	{
		rc = DEVICE_GOOD;

		/* If first fetch or cache value is too old and value is dirty, refresh value. */
		get_current_timespec(&now);
		if (iokit_device->fetch_sec_acq_loss_w == 0 ||
			((iokit_device->fetch_sec_acq_loss_w + 60 < now.tv_sec) && iokit_device->dirty_acq_loss_w))
		{
			rc = iokitosx_logsense_page(device, LOG_PERFORMANCE, LOG_PERFORMANCE_CAPACITY_SUB, logdata, LOGSENSEPAGE);

			if (rc != DEVICE_GOOD) {
				ltfsmsg(LTFS_INFO, "12217I", LOG_PERFORMANCE, rc, "get xattr");
			}
			else {
				if (_parse_logPage(logdata, PERF_ACTIVE_CQ_LOSS_W, &param_size, logbuf, 16)) {
					ltfsmsg(LTFS_INFO, "12218I", LOG_PERFORMANCE,  "get xattr");
					rc = -LTFS_NO_XATTR;
				}
				else {
					switch (param_size) {
						case sizeof(uint32_t):
							value32 = (uint32_t)ltfs_betou32(logbuf);
							iokit_device->acq_loss_w = (float)value32 / 65536.0;
							iokit_device->fetch_sec_acq_loss_w = now.tv_sec;
							iokit_device->dirty_acq_loss_w = false;
							break;
						default:
							ltfsmsg(LTFS_INFO, "12191I", param_size);
							rc = -LTFS_NO_XATTR;
							break;
					}
				}
			}
		}
	}

	if (rc == DEVICE_GOOD) {
		/* The buf allocated here shall be freed in xattr_get_virtual() */
		rc = asprintf(buf, "%2.2f", iokit_device->acq_loss_w);
		if (rc < 0) {
			ltfsmsg(LTFS_INFO, "12192I", "getting active CQ loss write");
			rc = -LTFS_NO_MEMORY;
		}
		else {
			rc = DEVICE_GOOD;
		}
	}
	else {
		iokit_device->fetch_sec_acq_loss_w = 0;
	}

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_GETXATTR));
	return rc;
}

int iokitosx_set_xattr(void *device, const char *name, const char *buf, size_t size)
{
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_SETXATTR));
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_SETXATTR));
	return -LTFS_NO_XATTR;
}

int32_t iokitosx_space(void *device, size_t count, TC_SPACE_TYPE type, struct tc_position *pos)
{
	int32_t rc = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_SPACE));
	switch(type) {
	case TC_SPACE_EOD:
		ltfsmsg(LTFS_DEBUG, "12153D", "space to EOD", "");
		rc = iokit_set_position(iokit_device, positionSet_SpaceEOM, 0, pos);
		break;
	case TC_SPACE_FM_F:
		ltfsmsg(LTFS_DEBUG, "12154D", "space forward file marks", (unsigned long long)count);
		rc = iokit_set_position(iokit_device, positionSet_SpaceFileMark, count, pos);
		break;
	case TC_SPACE_FM_B:
		ltfsmsg(LTFS_DEBUG, "12154D", "space back file marks", (unsigned long long)count);
		rc = iokit_set_position(iokit_device, positionSet_SpaceFileMark, -count, pos);
		break;
	case TC_SPACE_F:
		ltfsmsg(LTFS_DEBUG, "12154D", "space forward records", (unsigned long long)count);
		rc = iokit_set_position(iokit_device, positionSet_SpaceRecord, count, pos);
	case TC_SPACE_B:
		ltfsmsg(LTFS_DEBUG, "12154D", "space back records", (unsigned long long)count);
		rc = iokit_set_position(iokit_device, positionSet_SpaceRecord, -count, pos);
	default:
		/* unexpected space type */
		ltfsmsg(LTFS_INFO, "12127I");
		rc = -EDEV_INVALID_ARG;
		break;
	}

	if(rc >= 0)
		rc = iokitosx_readpos(device, pos);

	if(rc >= 0) {
		if(pos->early_warning)
			ltfsmsg(LTFS_WARN, "12074W", "space");
		else if(pos->programmable_early_warning)
			ltfsmsg(LTFS_WARN, "12075W", "locate");
	}

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_SPACE));
	return rc;
}


int32_t iokitosx_test_unit_ready(void *device)
{
	int32_t ret = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_TUR));
	ret = iokit_test_unit_ready(iokit_device);
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_TUR));

	return ret;
}

int32_t iokitosx_write(void *device, const char *buf, size_t count, struct tc_position *pos)
{
	int32_t rc;
	bool ew, pew;
	iokit_device_t *iokit_device = (iokit_device_t*)device;
	size_t datacount = count;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_WRITE));
	ltfsmsg(LTFS_DEBUG3, "12150D", "write", count);

	if(global_data.crc_checking) {
		if (iokit_device->f_crc_enc)
			iokit_device->f_crc_enc((void *)buf, count);
		datacount = count + 4;
	}

	rc = iokit_write(iokit_device, (uint8_t *)buf, datacount, &ew, &pew);
	if(rc == 0) {
		pos->block++;
		pos->early_warning = ew;
		pos->programmable_early_warning = pew;
	}
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_WRITE));

	return rc;
}


int32_t iokitosx_writefm(void *device, size_t count, struct tc_position *pos, bool immed)
{
	int32_t rc;
	iokit_device_t *iokit_device = (iokit_device_t*)device;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_WRITEFM));
	ltfsmsg(LTFS_DEBUG, "12151D", "write file marks", count);

	rc = iokit_write_filemark(iokit_device, count, immed);
	if(rc == 0)
		rc = iokitosx_readpos(device, pos);

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_WRITEFM));
	return rc;
}


int32_t iokitosx_write_attribute(void *device, const tape_partition_t part,
							   const uint8_t *buf, const size_t size)
{
	int32_t ret = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_WRITEATTR));
	ltfsmsg(LTFS_DEBUG3, "12154D", "writeattr", (unsigned long long)part);
	ret = _cdb_writeattribute(iokit_device, part, buf, size);
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_WRITEATTR));

	return ret;
}

static const char *generate_product_name(const char *product_id)
{
	const char *product_name = "";
	int i = 0;

	for (i = 0; supported_devices[i]; ++i) {
		if (strncmp(supported_devices[i]->product_id, product_id, strlen(product_id)) == 0) {
			product_name = supported_devices[i]->product_name;
			break;
		}
	}

	return product_name;
}

int iokitosx_get_device_list(struct tc_drive_info *buf, int count)
{
	int i, ret;
	int found = 0;
	int32_t devs = iokit_get_ssc_device_count();
	uint32_t device_code;
	scsi_device_identifier identifier;
	iokit_device_t *iokit_device; // Pointer to device status structure

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_GETDLIST));

	iokit_device = malloc(sizeof(iokit_device_t));
	if(iokit_device == NULL) {
		ltfsmsg(LTFS_ERR, "10001E", "(iokit) iokitosx_open: device private data");
		errno = ENOMEM;
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_GETDLIST));
		return -EDEV_NO_MEMORY;
	}

	bzero(iokit_device, sizeof(iokit_device_t));
	if( devs > 0 ) {
		for (i = 0; i < devs; i++) {
			if(iokit_find_ssc_device(iokit_device, i) != 0)
			{
				errno = EDEVERR;
				ret = -EDEV_DEVICE_UNOPENABLE;
				continue;
			}
			device_code = iokit_identify_device_code(iokit_device, &identifier);
			if (device_code != -1) {
				if (found < count && buf) {
					snprintf(buf[i].name, TAPE_DEVNAME_LEN_MAX, "%d", i);
					snprintf(buf[i].vendor, TAPE_VENDOR_NAME_LEN_MAX, "%s", identifier.vendor_id);
					snprintf(buf[i].model, TAPE_MODEL_NAME_LEN_MAX, "%s", identifier.product_id);
					snprintf(buf[i].serial_number, TAPE_SERIAL_LEN_MAX, "%s", identifier.unit_serial);
					snprintf(buf[i].product_name, PRODUCT_NAME_LENGTH, "%s", generate_product_name(identifier.product_id));
				}
				found ++;
			}
			ret = iokit_free_device(iokit_device);
		}
	}

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_GETDLIST));
	return found;
}

void iokitosx_help_message(void)
{
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_HELPMSG));
	ltfsresult("12221I", iokit_default_device);
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_HELPMSG));
}

const char *iokitosx_default_device_name(void)
{
	const char *devname;
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_DEFDEVNAME));
	devname = iokit_default_device;
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_DEFDEVNAME));
	return devname;
}

int32_t _cdb_locate(void *device, struct tc_position *dest, struct tc_position *pos)
{
	int32_t ret = -1;
	int32_t pos_ret = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;
	char command_description[COMMAND_DESCRIPTION_LENGTH] = "LOCATE";
	cdb_pass_through passthrough;
	uint8_t cdb[16];
	uint8_t sense[MAXSENSE];
	char *msg;

	bzero(&passthrough, sizeof(passthrough));
	bzero(cdb, sizeof(cdb));
	bzero(sense, sizeof(sense));

	passthrough.operation_descriptor = command_description;

	// Prepare Data Buffer
	passthrough.buffer_length = 0;
	passthrough.buffer        = NULL;

	// TODO: LOCATE_16 (0x92) appears to conflict with kSCSI_Cmd_LOCK_UNLOCK_CACHE_16

	// Prepare CDB
	passthrough.cmd_length     = sizeof(cdb);
	passthrough.cdb            = cdb;
	passthrough.cdb[0]         = 0x92;  /* SCSI Locate(16) Code  */
	passthrough.cdb[1]         = 0x02;  /* Set Change partition(CP) flag  */
	passthrough.cdb[3]         = (unsigned char)(dest->partition & 0xff);
	passthrough.cdb[4]         = (dest->block >> 56) & 0xff;
	passthrough.cdb[5]         = (dest->block >> 48) & 0xff;
	passthrough.cdb[6]         = (dest->block >> 40) & 0xff;
	passthrough.cdb[7]         = (dest->block >> 32) & 0xff;
	passthrough.cdb[8]         = (dest->block >> 24) & 0xff;
	passthrough.cdb[9]         = (dest->block >> 16) & 0xff;
	passthrough.cdb[10]        = (dest->block >> 8) & 0xff;
	passthrough.cdb[11]        = (dest->block & 0xff);
	passthrough.data_direction = SCSI_NO_DATA_TRANSFER;
	passthrough.timeout        = SCSI_PASSTHRU_TIMEOUT_VALUE;

	// Prepare sense buffer
	passthrough.sense_length = sizeof(sense);
	passthrough.sense        = sense;

	ret = iokit_passthrough(iokit_device, &passthrough, &msg);

	if (ret != DEVICE_GOOD || passthrough.check_condition) {
		if (dest->block == TAPE_BLOCK_MAX && ret == -EDEV_EOD_DETECTED) {
			ltfsmsg(LTFS_DEBUG, "12123D", "Locate");
			ret = DEVICE_GOOD;
		}
		else {
			iokitosx_process_errors(iokit_device, ret, msg, command_description, true);
		}
	}

	pos_ret = iokitosx_readpos(device, pos);

out:
	return ret;
}

int32_t _cdb_readpos(void *device, uint8_t *buf, size_t buf_size)
{
	int32_t ret = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;
	char command_description[COMMAND_DESCRIPTION_LENGTH] = "READ_POSITION";
	cdb_pass_through passthrough;
	uint8_t cdb[10];
	uint8_t sense[MAXSENSE];
	char *msg;

	bzero(&passthrough, sizeof(passthrough));

	passthrough.operation_descriptor = command_description;

	// Prepare data buffer
	bzero(buf, buf_size);
	passthrough.buffer_length	= buf_size;
	passthrough.buffer			= buf;

	// Zero out and setup CDB
	bzero(&cdb, sizeof(cdb));
	cdb[0] = 0x34;  /* 0x34 SCSI read position code */
	cdb[1] = 0x06;  /* Service Action 0x06: Long form */

	// Prepare CBD
	passthrough.cmd_length		= sizeof(cdb);
	passthrough.cdb				= (uint8_t *)&cdb;

	passthrough.data_direction  = SCSI_FROM_TARGET_TO_INITIATOR;
	passthrough.timeout			= SCSI_PASSTHRU_TIMEOUT_VALUE;	//ComputeTimeOut(iokit_device->device_code, ReadBlockLimitsTimeOut);

	// Prepare sense buffer
	bzero(sense, sizeof(sense));
	passthrough.sense_length = sizeof(sense);
	passthrough.sense        = sense;

	ret = iokit_passthrough(iokit_device, &passthrough, &msg);

	if (ret != DEVICE_GOOD || passthrough.check_condition ) {
		iokitosx_process_errors(iokit_device, ret, msg, command_description, true);
	}

out:
	return ret;
}


int32_t _cdb_read(void *device, uint8_t *buf, size_t size, boolean_t sili)
{
	int32_t ret = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;
	char command_description[COMMAND_DESCRIPTION_LENGTH] = "READ";
	cdb_pass_through passthrough;
	uint8_t cdb[6];
	uint8_t sense[MAXSENSE];
	size_t length = -1;
	char *msg;

	bzero(&passthrough, sizeof(passthrough));
	bzero(cdb, sizeof(cdb));
	bzero(sense, sizeof(sense));

	passthrough.operation_descriptor = command_description;

	// Prepare data buffer
	passthrough.buffer_length	= size;
	passthrough.buffer			= (uint8_t *)buf;

	// Prepare CBD
	passthrough.cmd_length		= sizeof(cdb);
	passthrough.cdb				= (unsigned char *)cdb;
	passthrough.cdb[0]			= kSCSICmd_READ_6;	/* 0x08 SCSI Read code */
	if(iokit_device->use_sili && sili)
		passthrough.cdb[1]		= 0x02;				/* SILI */
	passthrough.cdb[2]			= (size >> 16) & 0xFF;
	passthrough.cdb[3]			= (size >> 8)  & 0xFF;
	passthrough.cdb[4]			=  size        & 0xFF;
	passthrough.data_direction  = SCSI_FROM_TARGET_TO_INITIATOR;
	passthrough.timeout			= SCSI_PASSTHRU_TIMEOUT_VALUE;	//ComputeTimeOut(iokit_device->device_code, ReadBlockLimitsTimeOut);

	// Prepare sense buffer
	passthrough.sense_length = sizeof(sense);
	passthrough.sense        = (unsigned char *)sense;

	ret = iokit_passthrough(iokit_device, &passthrough, &msg);

	if (ret != DEVICE_GOOD || passthrough.check_condition || passthrough.buffer_length != size) {
		int32_t diff_len = 0;
		SCSI_Sense_Data *scsi_sense_data = (SCSI_Sense_Data *)passthrough.sense;

		switch (ret) {
			case DEVICE_GOOD:
			case -EDEV_NO_SENSE:
				if (passthrough.buffer_length != size) {
					ltfsmsg(LTFS_DEBUG, "12120D", passthrough.buffer_length);
					length = passthrough.buffer_length;
					ret = DEVICE_GOOD;
				}
				else if (scsi_sense_data->SENSE_KEY & kSENSE_ILI_Set) {
					diff_len = ((int32_t)scsi_sense_data->INFORMATION_1 << 24) +
					((int32_t)scsi_sense_data->INFORMATION_2 << 16) +
					((int32_t)scsi_sense_data->INFORMATION_3 << 8) +
					((int32_t)scsi_sense_data->INFORMATION_4);
					if (diff_len < 0) {
						ltfsmsg(LTFS_INFO, "12188I", diff_len, size - diff_len); // "Detect overrun condition"
						ret = -EDEV_OVERRUN;
					} else {
						ltfsmsg(LTFS_DEBUG, "12189D", diff_len, size - diff_len); // "Detect underrun condition"
						length = size - diff_len;
						ret = DEVICE_GOOD;
					}
				}
				else if (scsi_sense_data->SENSE_KEY & kSENSE_FILEMARK_Set) {
					ltfsmsg(LTFS_DEBUG, "12119D");
					length = 0;
					ret = -EDEV_FILEMARK_DETECTED;
				}
				break;
			case -EDEV_FILEMARK_DETECTED:
				ltfsmsg(LTFS_DEBUG, "12119D");
				length = 0;
				break;
			case -EDEV_CLEANING_REQUIRED:
				ltfsmsg(LTFS_INFO, "12109I");
				length = 0;
				ret = DEVICE_GOOD;
				break;
		}
		if (ret != DEVICE_GOOD && ret != -EDEV_FILEMARK_DETECTED) {
			if ((ret != -EDEV_CRYPTO_ERROR && ret != -EDEV_KEY_REQUIRED) || iokit_device->is_data_key_set)
				iokitosx_process_errors(device, ret, msg, command_description, true);
			length = ret;
		}
	}
	else {
		/* check condition is not set so we have a good read and can trust the length value */
		length = passthrough.buffer_length;
		goto out;
	}

out:
	return length;
}


int32_t _cdb_setcap(void *device, uint16_t proportion)
{
	int32_t ret = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;
	char command_description[COMMAND_DESCRIPTION_LENGTH] = "SETCAP";
	cdb_pass_through passthrough;
	uint8_t cdb[6];
	uint8_t sense[MAXSENSE];
	char *msg;

	bzero(&passthrough, sizeof(passthrough));
	bzero(cdb, sizeof(cdb));
	bzero(sense, sizeof(sense));

	passthrough.operation_descriptor = command_description;

	// Prepare data buffer
	passthrough.buffer_length	= 0;
	passthrough.buffer			= NULL;

	// Prepare CBD
	passthrough.cmd_length		= sizeof(cdb);
	passthrough.cdb				= (unsigned char *)cdb;
	passthrough.cdb[0]			= kSCSICmd_SET_CAPACITY;	/* 0x0B SCSI medium format code */
	passthrough.cdb[3]			= (unsigned char)(proportion >> 8);
	passthrough.cdb[4]			= (unsigned char)(proportion & 0xFF);
	passthrough.data_direction  = SCSI_NO_DATA_TRANSFER;
	passthrough.timeout			= ComputeTimeOut(iokit_device->device_code, Format);

	// Prepare sense buffer
	passthrough.sense_length = sizeof(sense);
	passthrough.sense        = (unsigned char *)sense;

	ret = iokit_passthrough(iokit_device, &passthrough, &msg);

	if (ret != DEVICE_GOOD || passthrough.check_condition) {
		iokitosx_process_errors(iokit_device, ret, msg, command_description, true);
	}

out:
	return ret;
}


int32_t _cdb_format(void *device, TC_FORMAT_TYPE format)
{
	int32_t ret = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;
	char command_description[COMMAND_DESCRIPTION_LENGTH] = "FORMAT";
	cdb_pass_through passthrough;
	uint8_t cdb[6];
	uint8_t sense[MAXSENSE];
	char *msg;

	if( (uint8_t)format >= (uint8_t)TC_FORMAT_MAX)
	{
		ret = -1;
		goto out;
	}

	bzero(&passthrough, sizeof(passthrough));
	bzero(cdb, sizeof(cdb));
	bzero(sense, sizeof(sense));

	passthrough.operation_descriptor = command_description;

	// Prepare data buffer
	passthrough.buffer_length	= 0;
	passthrough.buffer			= NULL;

	// Prepare CBD
	passthrough.cmd_length		= sizeof(cdb);
	passthrough.cdb				= (unsigned char *)cdb;
	passthrough.cdb[0]			= kSCSICmd_FORMAT_MEDIUM;	/* 0x04 SCSI medium format code */
	passthrough.cdb[2]			= (unsigned char)format;
	passthrough.data_direction  = SCSI_NO_DATA_TRANSFER;
	passthrough.timeout			= ComputeTimeOut(iokit_device->device_code, Format);

	// Prepare sense buffer
	passthrough.sense_length = sizeof(sense);
	passthrough.sense        = (unsigned char *)sense;

	ret = iokit_passthrough(iokit_device, &passthrough, &msg);

	if (ret != DEVICE_GOOD || passthrough.check_condition) {
		iokitosx_process_errors(iokit_device, ret, msg, command_description, true);
	}

out:
	return ret;
}


#define MAX_UINT16 (0x0000FFFF)

int32_t _cdb_sense(void *device, const uint8_t page, const TC_MP_PC_TYPE pc,
				   const uint8_t subpage, uint8_t *buf, const size_t size)
{
	int32_t ret = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;
	char command_description[COMMAND_DESCRIPTION_LENGTH] = "SENSE";
	cdb_pass_through passthrough;
	uint8_t cdb[10];
	uint8_t sense[MAXSENSE];
	char *msg;

	bzero(&passthrough, sizeof(passthrough));
	bzero(cdb, sizeof(cdb));
	bzero(sense, sizeof(sense));

	passthrough.operation_descriptor = command_description;

	// Prepare Data Buffer
	if(size > MAX_UINT16)
		passthrough.buffer_length = MAX_UINT16;
	else
		passthrough.buffer_length = size;
	passthrough.buffer        = buf;

	// Prepare CDB
	passthrough.cmd_length     = sizeof(cdb);
	passthrough.cdb            = cdb;
	passthrough.cdb[0]         = kSCSICmd_MODE_SENSE_10;  /* 0x5A SCSI MODE sense (10) code  */
	passthrough.cdb[2]         = pc | page;
	passthrough.cdb[3]         = subpage;
	passthrough.cdb[7]         = (passthrough.buffer_length >>  8) & 0xff;
	passthrough.cdb[8]         =  passthrough.buffer_length        & 0xff;
	passthrough.data_direction = SCSI_FROM_TARGET_TO_INITIATOR;
	passthrough.timeout        = SCSI_PASSTHRU_TIMEOUT_VALUE;

	// Prepare sense buffer
	passthrough.sense_length = sizeof(sense);
	passthrough.sense        = sense;

	ret = iokit_passthrough(iokit_device, &passthrough, &msg);

	if (ret != DEVICE_GOOD || passthrough.check_condition) {
		iokitosx_process_errors(iokit_device, ret, msg, command_description, true);
	}

	return ret;
}


int32_t _cdb_select(void *device, uint8_t *buf, size_t size)
{
	int32_t ret = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;
	char command_description[COMMAND_DESCRIPTION_LENGTH] = "SELECT";
	cdb_pass_through passthrough;
	uint8_t cdb[10];
	uint8_t sense[MAXSENSE];
	char *msg;

	bzero(&passthrough, sizeof(passthrough));
	bzero(cdb, sizeof(cdb));
	bzero(sense, sizeof(sense));

	passthrough.operation_descriptor = command_description;

	// Prepare Data Buffer
	passthrough.buffer_length = size;
	passthrough.buffer        = buf;

	// Prepare CDB
	passthrough.cmd_length     = sizeof(cdb);
	passthrough.cdb            = cdb;
	passthrough.cdb[0]         = kSCSICmd_MODE_SELECT_10;  /* 0x55 SCSI mode select (10) code  */
	passthrough.cdb[7]         = (passthrough.buffer_length >>  8) & 0xff;
	passthrough.cdb[8]         =  passthrough.buffer_length        & 0xff;
	passthrough.data_direction = SCSI_FROM_INITIATOR_TO_TARGET;
	passthrough.timeout        = SCSI_PASSTHRU_TIMEOUT_VALUE;

	// Prepare sense buffer
	passthrough.sense_length = sizeof(sense);
	passthrough.sense        = sense;

	ret = iokit_passthrough(iokit_device, &passthrough, &msg);

	if (ret != DEVICE_GOOD || passthrough.check_condition) {
		if (ret == -EDEV_MODE_PARAMETER_ROUNDED) {
			ret = DEVICE_GOOD;
		}
		else {
			iokitosx_process_errors(iokit_device, ret, msg, command_description, true);
		}
	}

	return ret;
}


int32_t _cdb_readattribute(void *device, const tape_partition_t partition,
						   const uint16_t id, uint8_t *buffer, size_t size)
{
	int32_t ret = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;
	char command_description[COMMAND_DESCRIPTION_LENGTH] = "READ_ATTRIBUTE";
	cdb_pass_through passthrough;
	uint8_t cdb[16];
	uint8_t sense[MAXSENSE];
	char *msg;
	bool take_dump = true;

	bzero(&passthrough, sizeof(passthrough));
	bzero(cdb, sizeof(cdb));
	bzero(sense, sizeof(sense));

	passthrough.operation_descriptor = command_description;

	// Prepare Data Buffer
	passthrough.buffer_length = size + 4;
	passthrough.buffer        = calloc(1, passthrough.buffer_length);
	if(passthrough.buffer == NULL) {
		errno = ENOMEM;
		ret = -ENOMEM;
		goto out;
	}

	// Prepare CDB
	passthrough.cmd_length     = sizeof(cdb);
	passthrough.cdb            = cdb;
	passthrough.cdb[0]         = kSCSICmd_READ_ATTRIBUTE;  /* 0x8C SCSI read attribute code */
	passthrough.cdb[1]         = 0x00;  /* Service Action 0x00: VALUE */
	passthrough.cdb[7]         = partition;
	passthrough.cdb[8]         = (id >> 8) & 0xff;
	passthrough.cdb[9]         =  id       & 0xff;
	passthrough.cdb[10]        = (passthrough.buffer_length >> 24) & 0xff;
	passthrough.cdb[11]        = (passthrough.buffer_length >> 16) & 0xff;
	passthrough.cdb[12]        = (passthrough.buffer_length >> 8)  & 0xff;
	passthrough.cdb[13]        =  passthrough.buffer_length        & 0xff;
	passthrough.data_direction = SCSI_FROM_TARGET_TO_INITIATOR;
	passthrough.timeout        = SCSI_PASSTHRU_TIMEOUT_VALUE;

	// Prepare sense buffer
	passthrough.sense_length = sizeof(sense);
	passthrough.sense        = sense;

	ret = iokit_passthrough(iokit_device, &passthrough, &msg);

	if (ret != DEVICE_GOOD || passthrough.check_condition) {
		if (ret == -EDEV_INVALID_FIELD_CDB) {
			take_dump = false;
		}

		iokitosx_process_errors(iokit_device, ret, msg, command_description, take_dump);

		if (ret < 0 &&
			id != TC_MAM_PAGE_COHERENCY &&
			id != TC_MAM_APP_VENDER &&
			id != TC_MAM_APP_NAME &&
			id != TC_MAM_APP_VERSION &&
			id != TC_MAM_USER_MEDIUM_LABEL &&
			id != TC_MAM_TEXT_LOCALIZATION_IDENTIFIER &&
			id != TC_MAM_BARCODE &&
			id != TC_MAM_APP_FORMAT_VERSION)
			ltfsmsg(LTFS_INFO, "12144I", ret);

		goto out_free;
	}

	memcpy(buffer, (passthrough.buffer + 4), size);

out_free:
	free(passthrough.buffer);

out:
	return ret;
}


int32_t _cdb_writeattribute(void *device, const tape_partition_t partition,
							const uint8_t *buffer, size_t size)
{
	int32_t ret = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;
	char command_description[COMMAND_DESCRIPTION_LENGTH] = "WRITE_ATTRIBUTE";
	cdb_pass_through passthrough;
	uint8_t cdb[16];
	uint8_t sense[MAXSENSE];
	char *msg;

	bzero(&passthrough, sizeof(passthrough));
	bzero(cdb, sizeof(cdb));
	bzero(sense, sizeof(sense));

	passthrough.operation_descriptor = command_description;

	// Prepare Data Buffer
	passthrough.buffer_length = size + 4;
	passthrough.buffer        = calloc(1, passthrough.buffer_length);
	if(passthrough.buffer == NULL) {
		errno = ENOMEM;
		ret = -ENOMEM;
		goto out;
	}
	passthrough.buffer[0] = (size >> 24) & 0xff;
	passthrough.buffer[1] = (size >> 16) & 0xff;
	passthrough.buffer[2] = (size >>  8) & 0xff;
	passthrough.buffer[3] = (size & 0xff);
	memcpy((passthrough.buffer + 4), buffer, size);

	// Prepare CDB
	passthrough.cmd_length     = sizeof(cdb);
	passthrough.cdb            = cdb;
	passthrough.cdb[0]         = kSCSICmd_WRITE_ATTRIBUTE;  /* 0x8D SCSI write attribute code */
	passthrough.cdb[1]         = 0x01;                      /* Write through bit on */
	passthrough.cdb[7]         = partition;
	passthrough.cdb[10]        = (passthrough.buffer_length >> 24) & 0xff;
	passthrough.cdb[11]        = (passthrough.buffer_length >> 16) & 0xff;
	passthrough.cdb[12]        = (passthrough.buffer_length >> 8)  & 0xff;
	passthrough.cdb[13]        =  passthrough.buffer_length        & 0xff;
	passthrough.data_direction = SCSI_FROM_INITIATOR_TO_TARGET;
	passthrough.timeout        = SCSI_PASSTHRU_TIMEOUT_VALUE;

	// Prepare sense buffer
	passthrough.sense_length = sizeof(sense);
	passthrough.sense        = sense;

	ret = iokit_passthrough(iokit_device, &passthrough, &msg);

	if (ret != DEVICE_GOOD || passthrough.check_condition) {
		iokitosx_process_errors(iokit_device, ret, msg, command_description, true);
	}

out:

	return ret;
}

int32_t _cdb_allow_overwrite(void *device, const struct tc_position pos)
{
	int32_t ret = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;
	char command_description[COMMAND_DESCRIPTION_LENGTH] = "ALLOW_OVERWRITE";
	cdb_pass_through passthrough;
	uint8_t cdb[16];
	uint8_t sense[MAXSENSE];
	char *msg;

	bzero(&passthrough, sizeof(passthrough));
	bzero(cdb, sizeof(cdb));
	bzero(sense, sizeof(sense));

	passthrough.operation_descriptor = command_description;

	// Prepare CDB
	passthrough.cmd_length     = sizeof(cdb);
	passthrough.cdb            = cdb;
	passthrough.cdb[0]         = kSCSICmd_ALLOW_OVERWRITE;  /* 0x82 SCSI allow overwrite code */
	passthrough.cdb[2]         = 0x01;                      /* ALLOW_OVERWRITE Current Position */
	passthrough.cdb[3]         = (unsigned char)(pos.partition & 0xff);
	passthrough.cdb[4]         = (pos.block >> 56) & 0xff;
	passthrough.cdb[5]         = (pos.block >> 48) & 0xff;
	passthrough.cdb[6]         = (pos.block >> 40) & 0xff;
	passthrough.cdb[7]         = (pos.block >> 32) & 0xff;
	passthrough.cdb[8]         = (pos.block >> 24) & 0xff;
	passthrough.cdb[9]         = (pos.block >> 16) & 0xff;
	passthrough.cdb[10]        = (pos.block >> 8) & 0xff;
	passthrough.cdb[11]        = (pos.block & 0xff);

	passthrough.data_direction = SCSI_NO_DATA_TRANSFER;
	passthrough.timeout        = SCSI_PASSTHRU_TIMEOUT_VALUE;

	// Prepare sense buffer
	passthrough.sense_length = sizeof(sense);
	passthrough.sense        = sense;

	ret = iokit_passthrough(iokit_device, &passthrough, &msg);

	if (ret != DEVICE_GOOD || passthrough.check_condition) {
		if (pos.block == TAPE_BLOCK_MAX && ret == -EDEV_EOD_DETECTED) {
			ltfsmsg(LTFS_DEBUG, "12123D", "Allow Overwrite");
			ret = DEVICE_GOOD;
		}
		else {
			iokitosx_process_errors(iokit_device, ret, msg, command_description, true);
		}
	}

out:
	return ret;
}

#define LOG_VOL_PART_HEADER_SIZE   (4)

int iokitosx_get_eod_status(void *device, int part)
{
	/*
	 * This feature requires new tape drive firmware
	 * to support logpage 17h correctly
	 */
	unsigned char logdata[LOGSENSEPAGE];
	unsigned char buf[16];
	int rc;
	unsigned int i;
	uint32_t param_size;
	uint32_t part_cap[2] = {EOD_UNKNOWN, EOD_UNKNOWN};

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_GETEODSTAT));

	/* Issue LogPage 0x17 */
	rc = iokitosx_logsense(device, LOG_VOLUMESTATS, logdata, LOGSENSEPAGE);
	if (rc) {
		ltfsmsg(LTFS_WARN, "12170W", LOG_VOLUMESTATS, rc);
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_GETEODSTAT));
		return EOD_UNKNOWN;
	}

	/* Parse Approximate used native capacity of partitions (0x203)*/
	if (_parse_logPage(logdata, (uint16_t)VOLSTATS_PART_USED_CAP, &param_size, buf, sizeof(buf))
		|| (param_size != sizeof(buf) ) ) {
		ltfsmsg(LTFS_WARN, "12171W");
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_GETEODSTAT));
		return EOD_UNKNOWN;
	}

	i = 0;
	while (i < sizeof(buf)) {
		unsigned char len;
		uint16_t part_buf;

		len = buf[i];
		part_buf = (uint16_t)(buf[i + 2] << 8) + (uint16_t) buf[i + 3];
		/* actual length - 1 is stored into len */
		if ( (len - LOG_VOL_PART_HEADER_SIZE + 1) == sizeof(uint32_t) && part_buf < 2) {
			part_cap[part_buf] = ((uint32_t) buf[i + 4] << 24) +
				((uint32_t) buf[i + 5] << 16) +
				((uint32_t) buf[i + 6] << 8) +
				(uint32_t) buf[i + 7];
		} else
			ltfsmsg(LTFS_WARN, "12172W", i, part_buf, len);

		i += (len + 1);
	}

	/* Create return value */
	if(part_cap[part] == 0xFFFFFFFF)
		rc = EOD_MISSING;
	else
		rc = EOD_GOOD;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_GETEODSTAT));
	return rc;
}

int32_t _cdb_securityprotocolin(void *device, const uint16_t sps, uint8_t **buffer, size_t * const size)
{
	int32_t ret = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;
	char command_description[COMMAND_DESCRIPTION_LENGTH] = "SPIN";

	if (! device || ! buffer || ! size) {
		errno = ENOMEM;
		ret = -ENOMEM;
		goto out;
	}

	cdb_pass_through passthrough = {0};
	uint8_t cdb[12] = {0};
	uint8_t sense[MAXSENSE] = {0};
	char *msg = NULL;

	passthrough.operation_descriptor = command_description;

	passthrough.buffer_length = *size + 4;
	passthrough.buffer = *buffer = calloc(passthrough.buffer_length, sizeof(uint8_t));
	if (! passthrough.buffer) {
		errno = ENOMEM;
		ret = -ENOMEM;
		goto out;
	}

	passthrough.cmd_length     = sizeof(cdb);
	passthrough.cdb            = cdb;
	passthrough.cdb[0]         = kSCSICmd_SECURITY_PROTOCOL_IN; /* 0xA2 SECURITY PROTOCOL IN */
	passthrough.cdb[1]         = 0x20; /* Tape Data Encryption security protocol */
	ltfs_u16tobe(passthrough.cdb + 2, sps); /* SECURITY PROTOCOL SPECIFIC */
	ltfs_u32tobe(passthrough.cdb + 6, passthrough.buffer_length);
	passthrough.data_direction = SCSI_FROM_TARGET_TO_INITIATOR;
	passthrough.timeout        = SCSI_PASSTHRU_TIMEOUT_VALUE;

	passthrough.sense_length = sizeof(sense);
	passthrough.sense        = sense;

	ret = iokit_passthrough(iokit_device, &passthrough, &msg);

	if (ret != DEVICE_GOOD || passthrough.check_condition) {
		iokitosx_process_errors(iokit_device, ret, msg, command_description, true);
		goto out;
	}

	*size = ltfs_betou16(passthrough.buffer + 2);

out:
	return ret;
}

int32_t _cdb_securityprotocolout(void *device, const uint16_t sps, uint8_t * const buffer,
	const size_t size)
{
	int32_t ret = -1;
	iokit_device_t *iokit_device = (iokit_device_t*)device;
	char command_description[COMMAND_DESCRIPTION_LENGTH] = "SPOUT";
	cdb_pass_through passthrough = {0};
	uint8_t cdb[12] = {0};
	uint8_t sense[MAXSENSE] = {0};
	char *msg;

	if (! device || ! buffer) {
		errno = ENOMEM;
		ret = -ENOMEM;
		goto out;
	}

	passthrough.operation_descriptor = command_description;

	passthrough.buffer_length = size;
	passthrough.buffer = buffer;

	passthrough.cmd_length     = sizeof(cdb);
	passthrough.cdb            = cdb;
	passthrough.cdb[0]         = kSCSICmd_SECURITY_PROTOCOL_OUT; /* 0xB5 SECURITY PROTOCOL OUT */
	passthrough.cdb[1]         = 0x20; /* Tape Data Encryption security protocol */
	ltfs_u16tobe(passthrough.cdb + 2, sps); /* SECURITY PROTOCOL SPECIFIC */
	ltfs_u32tobe(passthrough.cdb + 6, passthrough.buffer_length);
	passthrough.data_direction = SCSI_FROM_INITIATOR_TO_TARGET;
	passthrough.timeout        = SCSI_PASSTHRU_TIMEOUT_VALUE;

	passthrough.sense_length = sizeof(sense);
	passthrough.sense        = sense;

	ret = iokit_passthrough(iokit_device, &passthrough, &msg);

	if (ret != DEVICE_GOOD || passthrough.check_condition) {
		iokitosx_process_errors(iokit_device, ret, msg, command_description, true);
	}

out:
	return ret;
}

static void ltfsmsg_keyalias(const char * const title, const unsigned char * const keyalias)
{
	char s[128] = {'\0'};

	if (keyalias)
		sprintf(s, "keyalias = %c%c%c%02X%02X%02X%02X%02X%02X%02X%02X%02X", keyalias[0],
				keyalias[1], keyalias[2], keyalias[3], keyalias[4], keyalias[5], keyalias[6],
				keyalias[7], keyalias[8], keyalias[9], keyalias[10], keyalias[11]);
	else
		sprintf(s, "keyalias: NULL");

	ltfsmsg(LTFS_DEBUG, "12153D", title, s);
}

static bool is_ame(void *device)
{
	unsigned char buf[TC_MP_READ_WRITE_CTRL_SIZE] = {0};
	const int ret = iokitosx_modesense(device, TC_MP_READ_WRITE_CTRL, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));

	if (ret != 0) {
		char message[100] = {0};
		sprintf(message, "failed to get MP %02Xh (%d)", TC_MP_READ_WRITE_CTRL, ret);
		ltfsmsg(LTFS_DEBUG, "12153D", __FUNCTION__, message);

		return false; /* Consider that the encryption method is not AME */
	} else {
		const unsigned char encryption_method = buf[16 + 27];
		char message[100] = {0};
		char *method = NULL;
		switch (encryption_method) {
		case 0x00:
			method = "None";
			break;
		case 0x10:
			method = "System";
			break;
		case 0x1F:
			method = "Controller";
			break;
		case 0x50:
			method = "Application";
			break;
		case 0x60:
			method = "Library";
			break;
		case 0x70:
			method = "Internal";
			break;
		case 0xFF:
			method = "Custom";
			break;
		default:
			method = "Unknown";
			break;
		}
		sprintf(message, "Encryption Method is %s (0x%02X)", method, encryption_method);
		ltfsmsg(LTFS_DEBUG, "12153D", __FUNCTION__, message);

		if (encryption_method != 0x50) {
			ltfsmsg(LTFS_ERR, "12204E", method, encryption_method);
		}
		return encryption_method == 0x50;
	}
}

static int is_encryption_capable(void *device)
{
	const iokit_device_t * const iokit_device = (iokit_device_t*)device;
	if (iokit_device->device_code != IBM_3580) {
		ltfsmsg(LTFS_ERR, "12205E", iokit_device->device_code);
		return -EDEV_INTERNAL_ERROR;
	}

	if (! is_ame(device))
		return -EDEV_INTERNAL_ERROR;

	return DEVICE_GOOD;
}

int32_t iokitosx_set_key(void *device, const unsigned char *keyalias, const unsigned char *key)
{
	int32_t ret;

	/*
	 * Encryption  Decryption     Key         DKi      keyalias
	 *    Mode        Mode
	 * 0h Disable  0h Disable  Prohibited  Prohibited    NULL
	 * 2h Encrypt  3h Mixed    Mandatory    Optional    !NULL
	 */
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_SETKEY));
	ret = is_encryption_capable(device);
	if (ret < 0) {
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_SETKEY));
		return ret;
	}

	const uint16_t sps = 0x10;
	const size_t size = keyalias ? 20 + DK_LENGTH + 4 + DKI_LENGTH : 20;
	uint8_t *buffer = calloc(size, sizeof(uint8_t));
	iokit_device_t *iokit_device = (iokit_device_t*)device;

	if (! buffer) {
		ret = -ENOMEM;
		goto out;
	}

	unsigned char buf[TC_MP_READ_WRITE_CTRL_SIZE] = {0};
	ret = iokitosx_modesense(device, TC_MP_READ_WRITE_CTRL, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
	if (ret != DEVICE_GOOD)
		goto out;

	ltfs_u16tobe(buffer + 0, sps);
	ltfs_u16tobe(buffer + 2, size - 4);
	buffer[4] = 0x40; /* SCOPE: 010b All I_T Nexus, LOCK: 0 */
	/*
	 * CEEM: 00b Vendor specific
	 * RDMC: 00b The device entity shall mark each encrypted logical block per the default setting
	 *           for the algorithm.
	 * SDK:   0b The logical block encryption key sent in this page shall be the logical block
	 *           encryption key used for both encryption and decryption.
	 * CKOD:  0b The demounting of a volume shall not affect the logical block encryption parameters.
	 * CKORP: 0b Clear key on reservation preempt (CKORP) bit
	 * CKORL: 0b Clear key on reservation loss (CKORL) bit
	 */
	buffer[5] = 0x00;
	enum { DISABLE = 0, EXTERNAL = 1, ENCRYPT = 2 };
	buffer[6] = keyalias ? ENCRYPT : DISABLE; /* ENCRYPTION MODE */
	enum { /* DISABLE = 0, */ RAW = 1, DECRYPT = 2, MIXED = 3 };
	buffer[7] = keyalias ? MIXED : DISABLE; /* DECRYPTION MODE */
	buffer[8] = 1; /* ALGORITHM INDEX */
	buffer[9] = 0; /* LOGICAL BLOCK ENCRYPTION KEY FORMAT: plain-text key */
	buffer[10] = 0; /* KAD FORMAT: Unspecified */
	ltfs_u16tobe(buffer + 18, keyalias ? DK_LENGTH : 0x00); /* LOGICAL BLOCK ENCRYPTION KEY LENGTH */
	if (keyalias) {
		if (! key) {
			ret = -EINVAL;
			goto free;
		}
		memcpy(buffer + 20, key, DK_LENGTH); /* LOGICAL BLOCK ENCRYPTION KEY */
		buffer[20 + DK_LENGTH] = 0x01; /* KEY DESCRIPTOR TYPE: 01h DKi (Data Key Identifier) */
		ltfs_u16tobe(buffer + 20 + DK_LENGTH + 2, DKI_LENGTH);
		memcpy(buffer + 20 + 0x20 + 4, keyalias, DKI_LENGTH);
	}

	const char * const title = "set key:";
	ltfsmsg_keyalias(title, keyalias);

	ret = _cdb_securityprotocolout(iokit_device, sps, buffer, size);
	if (ret != DEVICE_GOOD)
		goto free;

	iokit_device->is_data_key_set = keyalias != NULL;

	memset(buf, 0, sizeof(buf));
	ret = iokitosx_modesense(device, TC_MP_READ_WRITE_CTRL, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
	if (ret != DEVICE_GOOD)
		goto out;

free:
	free(buffer);

out:
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_SETKEY));
	return ret;
}

static void show_hex_dump(const char * const title, const uint8_t * const buf, const size_t size)
{
	/*
	 * "         1         2         3         4         5         6         7         8"
	 * "12345678901234567890123456789012345678901234567890123456789012345678901234567890"
	 * "xxxxxx  00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F  0123456789ABCDEF\n" < 100
	 */
	char * const s = calloc((size / 0x10 + 1) * 100, sizeof(char));
	char *p = s;
	uint i = 0;
	int j = 0;
	int k = 0;

	if (p == NULL)
		return;

	for (i = 0; i < size; ++i) {
		if (i % 0x10 == 0) {
			if (i) {
				for (j = 0x10; 0 < j; --j) {
					p += sprintf(p, "%c", isprint(buf[i-j]) ? buf[i-j] : '.');
				}
			}
			p += sprintf(p, "\n%06X  ", i);
		}
		p += sprintf(p, "%02X %s", buf[i] & 0xFF, i % 8 == 7 ? " " : "");
	}
	for (; (i + k) % 0x10; ++k) {
		p += sprintf(p, "   %s", (i + k) % 8 == 7 ? " " : "");
	}
	for (j = 0x10 - k; 0 < j; --j) {
		p += sprintf(p, "%c", isprint(buf[i-j]) ? buf[i-j] : '.');
	}

	ltfsmsg(LTFS_DEBUG, "12153D", title, s);
}

int iokitosx_get_keyalias(void *device, unsigned char **keyalias)
{
	int32_t ret;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_GETKEYALIAS));
	ret = is_encryption_capable(device);
	if (ret < 0) {
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_GETKEYALIAS));
		return ret;
	}

	const uint16_t sps = 0x21;
	uint8_t *buffer = NULL;
	size_t size = 0;
	iokit_device_t *iokit_device = (iokit_device_t*)device;
	int i = 0;

	if (! device || ! keyalias)
		goto out;

	memset(iokit_device->dki, 0, sizeof(iokit_device->dki));
	*keyalias = NULL;

	/*
	 * 1st loop: Get the page length.
	 * 2nd loop: Get full data in the page.
	 */
	for (i = 0; i < 2; ++i) {
		free(buffer);
		ret = _cdb_securityprotocolin(iokit_device, sps, &buffer, &size);
		if (ret != DEVICE_GOOD)
			goto free;
	}

	show_hex_dump("SPIN:", buffer, size + 4);

	const unsigned char encryption_status = buffer[12] & 0xF;
	enum {
		ENC_STAT_INCAPABLE                          = 0,
		ENC_STAT_NOT_YET_BEEN_READ                  = 1,
		ENC_STAT_NOT_A_LOGICAL_BLOCK                = 2,
		ENC_STAT_NOT_ENCRYPTED                      = 3,
		ENC_STAT_ENCRYPTED_BY_UNSUPPORTED_ALGORITHM = 4,
		ENC_STAT_ENCRYPTED_BY_SUPPORTED_ALGORITHM   = 5,
		ENC_STAT_ENCRYPTED_BY_OTHER_KEY             = 6,
		ENC_STAT_RESERVED, /* 7h-Fh */
	};
	if (encryption_status == ENC_STAT_ENCRYPTED_BY_UNSUPPORTED_ALGORITHM ||
		encryption_status == ENC_STAT_ENCRYPTED_BY_SUPPORTED_ALGORITHM ||
		encryption_status == ENC_STAT_ENCRYPTED_BY_OTHER_KEY) {
		uint offset = 16; /* offset of key descriptor */
		while (offset <= size && buffer[offset] != 1) {
			offset += ltfs_betou16(buffer + offset + 2) + 4;
		}
		if (offset <= size && buffer[offset] == 1) {
			const uint dki_length = ((int) buffer[offset + 2]) << 8 | buffer[offset + 3];
			if (offset + dki_length <= size) {
				int n = dki_length < sizeof(iokit_device->dki) ? dki_length : sizeof(iokit_device->dki);
				memcpy(iokit_device->dki, &buffer[offset + 4], n);
				*keyalias = iokit_device->dki;
			}
		}
	}

	const char * const title = "get key-alias:";
	ltfsmsg_keyalias(title, iokit_device->dki);

free:
	free(buffer);

out:
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_GETKEYALIAS));
	return ret;
}

#define LBP_DISABLE             (0x00)
#define REED_SOLOMON_CRC        (0x01)
#define CRC32C_CRC              (0x02)

#define TC_MP_INIT_EXT_LBP_RS         (0x40)
#define TC_MP_INIT_EXT_LBP_CRC32C     (0x20)

int32_t iokitosx_set_lbp(void *device, bool enable)
{
	iokit_device_t *priv = (iokit_device_t *) device;
	unsigned char buf[TC_MP_SUB_DP_CTRL_SIZE];
	unsigned char buf_ext[TC_MP_INIT_EXT_SIZE];
	unsigned char lbp_method = LBP_DISABLE;
	int rc = DEVICE_GOOD;

	/* Check logical block protection capability */
	rc = iokitosx_modesense(device, TC_MP_INIT_EXT, TC_MP_PC_CURRENT, 0x00, buf_ext, sizeof(buf));
	if (rc < 0)
		return rc;

	if (buf_ext[0x12] & TC_MP_INIT_EXT_LBP_CRC32C)
		lbp_method = CRC32C_CRC;
	else
		lbp_method = REED_SOLOMON_CRC;

	/* set logical block protection */
	ltfsmsg(LTFS_DEBUG, "12156D", "LBP Enable", enable, "");
	ltfsmsg(LTFS_DEBUG, "12156D", "LBP Method", lbp_method, "");
	rc = iokitosx_modesense(device, TC_MP_CTRL, TC_MP_PC_CURRENT,
						   TC_MP_SUB_DP_CTRL, buf, sizeof(buf));
	if (rc < 0)
		goto out;

	buf[0]  = 0x00;
	buf[1]  = 0x00;
	if (enable) {
		buf[20] = lbp_method;
		buf[21] = 0x04;
		buf[22] = 0xc0;
	} else {
		buf[20] = LBP_DISABLE;
		buf[21] = 0;
		buf[22] = 0;
	}

	rc = iokitosx_modeselect(device, buf, sizeof(buf));

	if (rc == DEVICE_GOOD) {
		if (enable) {
			switch (lbp_method) {
			case CRC32C_CRC:
				priv->f_crc_enc = crc32c_enc;
				priv->f_crc_check = crc32c_check;
				break;
			case REED_SOLOMON_CRC:
				priv->f_crc_enc = rs_gf256_enc;
				priv->f_crc_check = rs_gf256_check;
				break;
			default:
				priv->f_crc_enc   = NULL;
				priv->f_crc_check = NULL;
				break;
			}
			ltfsmsg(LTFS_INFO, "12206I");
		} else {
			priv->f_crc_enc   = NULL;
			priv->f_crc_check = NULL;
			ltfsmsg(LTFS_INFO, "12207I");
		}
	}

out:
	return rc;
}

enum {
	TAPE_RANGE_UNKNOWN = 0,
	TAPE_RANGE_ENTERPRISE = 1,
	TAPE_RANGE_MIDDLE = 2,
} TAPE_RANGE;

enum {
	DRIVE_GEN_UNKNOWN = 0,
	DRIVE_GEN_LTO5 = 5,
	DRIVE_GEN_LTO6 = 6,
	DRIVE_GEN_JAG4 = 0x104,
	DRIVE_GEN_JAG5 = 0x105,
};

typedef struct {
	DRIVE_TYPE drive_type;
	int drive_range;
	int drive_generation;
} DRIVE_TYPE_MAP;

static DRIVE_TYPE_MAP drive_type_map[] = {
	{ DRIVE_LTO5, TAPE_RANGE_MIDDLE, DRIVE_GEN_LTO5 },
	{ DRIVE_LTO5_HH, TAPE_RANGE_MIDDLE, DRIVE_GEN_LTO5 },
	{ DRIVE_LTO6, TAPE_RANGE_MIDDLE, DRIVE_GEN_LTO6 },
	{ DRIVE_LTO6_HH, TAPE_RANGE_MIDDLE, DRIVE_GEN_LTO6 },
	{ DRIVE_TS1140, TAPE_RANGE_ENTERPRISE, DRIVE_GEN_JAG4 },
	{ DRIVE_TS1150, TAPE_RANGE_ENTERPRISE, DRIVE_GEN_JAG5 },
};
static int num_drive_type_map = sizeof(drive_type_map) / sizeof(DRIVE_TYPE_MAP);

typedef struct {
	char barcode_suffix[3];
	int density_code;
} BARCODE_DENSITY_MAP;

typedef struct {
	int drive_generation;
	int density_code;
} DRIVE_DENSITY_SUPPORT_MAP;

static DRIVE_DENSITY_SUPPORT_MAP jaguar_drive_density[] = {
	// Jaguar 4 drive supports only density code 54h medium
	{ DRIVE_GEN_JAG4, 0x54 },
	// Jaguar 5 drive supports density code 54h and 55h medium
	{ DRIVE_GEN_JAG5, 0x54 },
	{ DRIVE_GEN_JAG5, 0x55 },
};
static int num_jaguar_drive_density = sizeof(jaguar_drive_density) / sizeof(DRIVE_DENSITY_SUPPORT_MAP);

static DRIVE_DENSITY_SUPPORT_MAP jaguar_drive_density_strict[] = {
	// Jaguar 4 drive supports only density code 54h medium
	{ DRIVE_GEN_JAG4, 0x54 },
	// Jaguar 5 drive supports only density code 55h medium
	{ DRIVE_GEN_JAG5, 0x55 },
};
static int num_jaguar_drive_density_strict = sizeof(jaguar_drive_density_strict) / sizeof(DRIVE_DENSITY_SUPPORT_MAP);

static BARCODE_DENSITY_MAP lto_barcode_density[] = {
	{ "L5", 0x58 },
	{ "L6", 0x5A },
};
static int num_lto_barcode_density = sizeof(lto_barcode_density) / sizeof(BARCODE_DENSITY_MAP);

static DRIVE_DENSITY_SUPPORT_MAP lto_drive_density[] = {
	// LTO5 drive supports only LTO5 medium
	{ DRIVE_GEN_LTO5, 0x58 },
	// LTO6 drive supports both LTO5 and LTO6 media
	{ DRIVE_GEN_LTO6, 0x58 },
	{ DRIVE_GEN_LTO6, 0x5A },
};
static int num_lto_drive_density = sizeof(lto_drive_density) / sizeof(DRIVE_DENSITY_SUPPORT_MAP);

static DRIVE_DENSITY_SUPPORT_MAP lto_drive_density_strict[] = {
	// LTO5 drive supports only LTO5 medium
	{ DRIVE_GEN_LTO5, 0x58 },
	// LTO6 drive supports only LTO6 medium for strict mode
	{ DRIVE_GEN_LTO6, 0x5A },
};
static int num_lto_drive_density_strict = sizeof(lto_drive_density_strict) / sizeof(DRIVE_DENSITY_SUPPORT_MAP);

bool iokitosx_is_mountable(void *device, const char *barcode, const unsigned char density_code)
{
	int i;
	int drive_range, drive_generation;
	int dcode = 0;
	bool supported = false;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_ISMOUNTABLE));

	// check bar code length
	int bc_len = strlen(barcode);
	switch (bc_len) {
	case 6:
		// always supported
		ltfsmsg(LTFS_DEBUG, "12209D", barcode);
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_ISMOUNTABLE));
		return true;
	case 8:
		break;
	default:
		// invalid bar code length
		ltfsmsg(LTFS_ERR, "12208E", barcode);
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_ISMOUNTABLE));
		return false;
	}

	// check driver type
	iokit_device_t *priv = (iokit_device_t *) device;
	drive_range = TAPE_RANGE_UNKNOWN;
	for (i = 0; i < num_drive_type_map; i++) {
		if (drive_type_map[i].drive_type == priv->drive_type) {
			drive_range = drive_type_map[i].drive_range;
			drive_generation = drive_type_map[i].drive_generation;
			break;
		}
	}
	if (drive_range == TAPE_RANGE_UNKNOWN) {
		// unknown drive
		ltfsmsg(LTFS_ERR, "12210E", priv->drive_type);
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_ISMOUNTABLE));
		return false;
	}

	if (drive_range == TAPE_RANGE_ENTERPRISE) {
		// Jaguar tape drive
		if (density_code == 0) {
			// density code is not defined
			// use bar code to find supported cartridges
			if (!strncmp(barcode+6, "JB", 2) ||
				!strncmp(barcode+6, "JC", 2) ||
				!strncmp(barcode+6, "JK", 2)) {
				// assume density code 54h
				dcode = 0x54;
				}
			else if (!strncmp(barcode+6, "JD", 2) ||
					 !strncmp(barcode+6, "JL", 2)) {
				// assume density code 55h
				dcode = 0x55;
				}
			else {
				// none of supported medium
				ltfsmsg(LTFS_INFO, "12211I", barcode);
				ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_ISMOUNTABLE));
				return false;
				}
		} else {
			dcode = density_code;
		}
		if (global_data.strict_drive) {
			for (i = 0; i < num_jaguar_drive_density_strict; i++) {
				if ((jaguar_drive_density_strict[i].drive_generation == drive_generation) &&
					(jaguar_drive_density_strict[i].density_code == dcode)) {
					supported = true;
					break;
				}
			}
		} else {
			for (i = 0; i < num_jaguar_drive_density; i++) {
				if ((jaguar_drive_density[i].drive_generation == drive_generation) &&
					(jaguar_drive_density[i].density_code == dcode)) {
					if (drive_generation != DRIVE_GEN_JAG5 || strncmp(barcode+6, "JB", 2)) {
						/* Jag5 drive does not support JB cartridge */
						supported = true;
						}
					break;
				}
			}
		}
	} else {
		// LTO tape drive
		for (i = 0; i < num_lto_barcode_density; i++) {
			if (!strncmp(barcode+6, &lto_barcode_density[i].barcode_suffix[0], 2)) {
				dcode = lto_barcode_density[i].density_code;
				break;
			}
		}
		if (dcode == 0) {
			// unknown generation
			ltfsmsg(LTFS_INFO, "12211I", barcode);
			ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_ISMOUNTABLE));
			return false;
		}
		if (global_data.strict_drive) {
			for (i = 0; i < num_lto_drive_density_strict; i++) {
				if ((lto_drive_density_strict[i].drive_generation == drive_generation) &&
					(lto_drive_density_strict[i].density_code == dcode)) {
					supported = true;
					break;
				}
			}
		} else {
			for (i = 0; i < num_lto_drive_density; i++) {
				if ((lto_drive_density[i].drive_generation == drive_generation) &&
					(lto_drive_density[i].density_code == dcode)) {
					supported = true;
					break;
				}
			}
		}
	}
	if (supported) {
		ltfsmsg(LTFS_DEBUG, "12212D", priv->device_name, barcode, density_code);
	} else {
		ltfsmsg(LTFS_DEBUG, "12213D", priv->device_name, barcode, density_code);
	}

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_ISMOUNTABLE));
	return supported;
}

int iokitosx_get_worm_status(void *device, bool *is_worm)
{
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_GETWORMSTAT));
	*is_worm = false;
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_GETWORMSTAT));
	return 0;
}

struct tape_ops iokitosx_drive_handler = {
	.open                   = iokitosx_open,
	.reopen                 = iokitosx_reopen,
	.close                  = iokitosx_close,
	.close_raw              = iokitosx_close_raw,
	.is_connected           = iokitosx_is_connected,
	.inquiry                = iokitosx_inquiry,
	.inquiry_page           = iokitosx_inquiry_page,
	.test_unit_ready        = iokitosx_test_unit_ready,
	.read                   = iokitosx_read,
	.write                  = iokitosx_write,
	.writefm                = iokitosx_writefm,
	.rewind                 = iokitosx_rewind,
	.locate                 = iokitosx_locate,
	.space                  = iokitosx_space,
	.erase                  = iokitosx_erase,
	.load                   = iokitosx_load,
	.unload                 = iokitosx_unload,
	.readpos                = iokitosx_readpos,
	.setcap                 = iokitosx_setcap,
	.format                 = iokitosx_format,
	.remaining_capacity     = iokitosx_remaining_capacity,
	.logsense               = iokitosx_logsense,
	.modesense              = iokitosx_modesense,
	.modeselect             = iokitosx_modeselect,
	.reserve_unit           = iokitosx_reserve_unit,
	.release_unit           = iokitosx_release_unit,
	.prevent_medium_removal = iokitosx_prevent_medium_removal,
	.allow_medium_removal   = iokitosx_allow_medium_removal,
	.write_attribute        = iokitosx_write_attribute,
	.read_attribute         = iokitosx_read_attribute,
	.allow_overwrite        = iokitosx_allow_overwrite,
	.report_density         = iokitosx_report_density,
	// May be command combination
	.set_compression        = iokitosx_set_compression,
	.set_default            = iokitosx_set_default,
	.get_cartridge_health   = iokitosx_get_cartridge_health,
	.get_tape_alert         = iokitosx_get_tape_alert,
	.clear_tape_alert       = iokitosx_clear_tape_alert,
	.get_xattr              = iokitosx_get_xattr,
	.set_xattr              = iokitosx_set_xattr,
	.get_parameters         = iokitosx_get_parameters,
	.get_eod_status         = iokitosx_get_eod_status,
	.get_device_list        = iokitosx_get_device_list,
	.help_message           = iokitosx_help_message,
	.parse_opts             = iokitosx_parse_opts,
	.default_device_name    = iokitosx_default_device_name,
	.set_key                = iokitosx_set_key,
	.get_keyalias           = iokitosx_get_keyalias,
	.takedump_drive         = iokitosx_takedump_drive,
	.is_mountable           = iokitosx_is_mountable,
	.get_worm_status        = iokitosx_get_worm_status,
};

struct tape_ops *tape_dev_get_ops(void)
{
	return &iokitosx_drive_handler;
}

extern char driver_osx_iokit_dat[];

const char *tape_dev_get_message_bundle_name(void **message_data)
{
	*message_data = driver_osx_iokit_dat;
	return "driver_osx_iokit";
}
