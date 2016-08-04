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
** FILE NAME:       tape_drivers/linux/ibmtape/ibmtape_tc.c
**
** DESCRIPTION:     Implements LTFS backend interface for IBM tape drives on Linux.
**
** AUTHORS:         Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
*************************************************************************************
*/

#define __ibmtape_tc_c

#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>
#include <ctype.h>

#include "libltfs/ltfs_fuse_version.h"
#include "libltfs/arch/time_internal.h"
#include <fuse.h>

#include "libltfs/ltfslogging.h"
#include "ibmtape_cmn.h"
#include "reed_solomon_crc.h"
#include "crc32c_crc.h"

/*
 * Default tape device
 */
const char *ibmtape_default_device = "/dev/IBMtape0";

/*
 * Default changer device
 */
const char *ibmtape_default_changer_device = "/dev/IBMchanger0";

/*
 *  Definitions
 */
#define LOG_PAGE_HEADER_SIZE      (4)
#define LOG_PAGE_PARAMSIZE_OFFSET (3)
#define LOG_PAGE_PARAM_OFFSET     (4)

#define LINUX_MAX_BLOCK_SIZE (1 * MB)
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define DK_LENGTH 32
#define DKI_LENGTH 12

#define THREASHOLD_FORCE_WRITE_NO_WRITE (5)

#define CRC32C_CRC (0x02)

/*
 *  Global values
 */
struct ibmtape_global_data global_data;

/*
 *  Forward reference
 */
int ibmtape_readpos(void *device, struct tc_position *pos);
int ibmtape_rewind(void *device, struct tc_position *pos);
int ibmtape_modesense(void *device, const uint8_t page, const TC_MP_PC_TYPE pc, const uint8_t subpage,
					  unsigned char *buf, const size_t size);
int ibmtape_set_key(void *device, const unsigned char * const keyalias, const unsigned char * const key);
int ibmtape_set_lbp(void *device, bool enable);

/*
 *  Local Functions
 */

/**
 * Parse Log page contents
 * @param logdata pointer to logdata buffer to parse. including log sense header is expected
 * @param param parameter id to fetch
 * @param param_size size of value to fetch
 * @param buf pointer to the buffer to filled in teh fetched value. this function will update this value.
 * @param bufsize size of the buffer
 * @return 0 on success or negative value on error
 */
int parse_logPage(const unsigned char *logdata, const uint16_t param, int *param_size,
								unsigned char *buf, const size_t bufsize)
{
	uint16_t page_len, param_code, param_len;
	long i;

	page_len = ltfs_betou16(logdata + 2);
	i = LOG_PAGE_HEADER_SIZE;

	while (i < page_len) {
		param_code = ltfs_betou16(logdata + i);
		param_len = (uint16_t) logdata[i + LOG_PAGE_PARAMSIZE_OFFSET];
		if (param_code == param) {
			*param_size = param_len;
			if (bufsize < param_len) {
				ltfsmsg(LTFS_INFO, "12111I", bufsize, i + LOG_PAGE_PARAM_OFFSET);
				memcpy(buf, &logdata[i + LOG_PAGE_PARAM_OFFSET], bufsize);
				return -2;
			}
			else {
				memcpy(buf, &logdata[i + LOG_PAGE_PARAM_OFFSET], param_len);
				return 0;
			}
		}
		i += param_len + LOG_PAGE_PARAM_OFFSET;
	}

	return -1;
}

/**
 * Parse option for IBM tape driver
 * @param devname device name of the LTO tape driver
 * @return a pointer to the ibmtape backend on success or NULL on error
 */
#define IBMTAPE_OPT(templ,offset,value) { templ, offsetof(struct ibmtape_global_data, offset), value }

static struct fuse_opt ibmtape_global_opts[] = {
	IBMTAPE_OPT("autodump",          disable_auto_dump, 0),
	IBMTAPE_OPT("noautodump",        disable_auto_dump, 1),
	IBMTAPE_OPT("scsi_lbprotect=%s", str_crc_checking, 0),
	IBMTAPE_OPT("strict_drive",   strict_drive, 1),
	IBMTAPE_OPT("nostrict_drive", strict_drive, 0),
	FUSE_OPT_END
};

int null_parser(void *priv, const char *arg, int key, struct fuse_args *outargs)
{
	return 1;
}

int ibmtape_parse_opts(void *device, void *opt_args)
{
	struct fuse_args *args = (struct fuse_args *) opt_args;
	int ret;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_PARSEOPTS));
	/* fuse_opt_parse can handle a NULL device parameter just fine */
	ret = fuse_opt_parse(args, &global_data, ibmtape_global_opts, null_parser);
	if (ret < 0) {
		ltfsmsg(LTFS_INFO, "12112I", ret);
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

static bool is_supported_firmware(const DRIVE_TYPE drive_type, const unsigned char * const revision)
{
	const uint32_t rev = ltfs_betou32(revision);
	return true; /* temporary by nishida */

	switch (drive_type) {
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

/**
 * Open IBM tape backend.
 * @param devname device name of the LTO tape driver
 * @param[out] handle contains the handle to the IBM tape backend on success
 * @return 0 on success or a negative value on error
 */
int ibmtape_open(const char *devname, void **handle)
{
	struct ibmtape_data *priv;
	struct tc_inq inq_data;
	struct tc_inq_page inq_page_data;
	int ret, i, device_code;

	CHECK_ARG_NULL(handle, -LTFS_NULL_ARG);
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_OPEN));
	*handle = NULL;

	ret =  ibmtape_check_lin_tape_version();
	if (ret != DEVICE_GOOD) {
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_OPEN));
		return ret;
	}

	ltfsmsg(LTFS_INFO, "12158I", devname);

	priv = calloc(1, sizeof(struct ibmtape_data));
	if (! priv) {
		ltfsmsg(LTFS_ERR, "10001E", "ibmtape_open: device private data");
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_OPEN));
		return -EDEV_NO_MEMORY;
	}

	priv->fd = open(devname, O_RDWR | O_NDELAY);
	if (priv->fd < 0) {
		priv->fd = open(devname, O_RDONLY | O_NDELAY);
		if (priv->fd < 0) {
			if (errno == EAGAIN) {
				ltfsmsg(LTFS_ERR, "12113E", devname);
				ret = -EDEV_DEVICE_BUSY;
			} else {
				ltfsmsg(LTFS_INFO, "12114I", devname, errno);
				ret = -EDEV_DEVICE_UNOPENABLE;
			}
			free(priv);
			ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_OPEN));
			return ret;
		}
		ltfsmsg(LTFS_WARN, "12115W", devname);
	}

	ret = ibmtape_inquiry(priv, &inq_data);
	if (ret) {
		ltfsmsg(LTFS_INFO, "12116I", ret);
		close(priv->fd);
		free(priv);
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_OPEN));
		return ret;
	} else {
		i = 0;
		device_code = UNSUPPORTED;

		ltfsmsg(LTFS_INFO, "12118I", "Drive", inq_data.pid);
		ltfsmsg(LTFS_INFO, "12162I", inq_data.vid);
		while (supported_devices[i]) {
			if ((strncmp((char *) inq_data.pid, (char *) supported_devices[i]->product_id,
					 strlen((char *) supported_devices[i]->product_id)) == 0)) {
				device_code = supported_devices[i]->device_code;
				break;
			}
			i++;
		}

		if (device_code == UNSUPPORTED) {
			ltfsmsg(LTFS_INFO, "12117I", "Drive", inq_data.pid);
			close(priv->fd);
			free(priv);
			ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_OPEN));
			return -EDEV_DEVICE_UNSUPPORTABLE;
		} else {
			priv->device_code = device_code;
			priv->drive_type = supported_devices[i]->drive_type;
		}
	}

	memset(&inq_page_data, 0, sizeof(struct tc_inq_page));
	ret = ibmtape_inquiry_page(priv, TC_INQ_PAGE_DRVSERIAL, &inq_page_data);
	if (ret) {
		ltfsmsg(LTFS_INFO, "12161I", TC_INQ_PAGE_DRVSERIAL, ret);
		close(priv->fd);
		free(priv);
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_OPEN));
		return ret;
	}

	/* Set drive serial number to private data to put it to the dump file name */
	memset(priv->drive_serial, 0, sizeof(priv->drive_serial));
	for (i = 4; i < (int)sizeof(inq_page_data.data) - 1; i++) {
		if (inq_page_data.data[i] == ' ' || inq_page_data.data[i] == '\0')
			break;
		priv->drive_serial[i - 4] = inq_page_data.data[i];
	}

	ltfsmsg(LTFS_INFO, "12159I", inq_data.revision);
	if (! is_supported_firmware(priv->drive_type, inq_data.revision)) {
		ltfsmsg(LTFS_INFO, "12117I", "drive firmware", inq_data.revision);
		close(priv->fd);
		free(priv);
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_OPEN));
		return -EDEV_UNSUPPORTED_FIRMWARE;
	}

	ltfsmsg(LTFS_INFO, "12160I", "Drive", priv->drive_serial);

	priv->loaded = false; /* Assume tape is not loaded until a successful load call. */
	priv->type   = DEVICE_TAPE;
	priv->devname = strdup(devname);

	*handle = (void *) priv;
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_OPEN));
	return DEVICE_GOOD;
}

/**
 * Reopen IBM tape backend
 * @param devname device name of the LTO tape driver
 * @param device a pointer to the ibmtape backend
 * @return 0 on success or a negative value on error
 */
int ibmtape_reopen(const char *name, void *vstate)
{
	/* Do nothing */
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_REOPEN));
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_REOPEN));
	return 0;
}

/**
 * Close IBM tape backend
 * @param device a pointer to the ibmtape backend
 * @return 0 on success or a negative value on error
 */
int ibmtape_close(void *device)
{
	struct ibmtape_data *priv = (struct ibmtape_data *) device;
	struct tc_position pos;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_CLOSE));
	if (priv->loaded)
		ibmtape_rewind(device, &pos);

	ibmtape_set_lbp(device, false);

	if (priv->devname)
		free(priv->devname);

	close(priv->fd);
	free(priv);
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_CLOSE));
	return 0;
}

/**
 * Close only file descriptor
 * @param device a pointer to the ibmtape backend
 * @return 0 on success or a negative value on error
 */
int ibmtape_close_raw(void *device)
{
	struct ibmtape_data *priv = (struct ibmtape_data *) device;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_CLOSERAW));
	close(priv->fd);
	priv->fd = -1;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_CLOSERAW));
	return 0;
}

/**
 * Test if a given tape device is connected to the host
 * @param devname device name of the LTO tape driver
 * @return 0 on success, indicating that the drive is connected to the host,
 *  or a negative value on error.
 */
int ibmtape_is_connected(const char *devname)
{
	struct stat statbuf;
	int ret = 0;
	/*
	 * We assume that /dev is handled by a daemon such as Udev and that
	 * device entries are automatically removed and added upon hotplug events.
	 */
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_ISCONNECTED));
	ret = stat(devname, &statbuf);
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_ISCONNECTED));
	return ret;
}

int _mt_command(void *device, int cmd, char *cmd_name, int param, char **msg)
{
	int fd = *((int *) device);
	struct mtop mt = {.mt_op = cmd,.mt_count = param };
	struct request_sense sense_data;
	int rc;

	rc = ioctl(fd, MTIOCTOP, &mt);

	if (rc != 0) {
		ltfsmsg(LTFS_INFO, "12196I", cmd_name, cmd, rc, errno, ((struct ibmtape_data *) device)->drive_serial);
		rc = ibmtape_ioctlrc2err(device, fd, &sense_data, msg);
	}
	else {
		*msg = "Command succeeded";
		rc = DEVICE_GOOD;
	}

	return rc;
}

int _st_command(void *device, int cmd, char *cmd_name, int param, char **msg)
{
	int fd = *((int *) device);
    struct stop st = {.st_op = cmd, .st_count = param};
	struct request_sense sense_data;
	int rc;

	rc = ioctl(fd, STIOCTOP, &st);

	if (rc != 0) {
		ltfsmsg(LTFS_INFO, "12196I", cmd_name, cmd, rc, errno, ((struct ibmtape_data *) device)->drive_serial);
		rc = ibmtape_ioctlrc2err(device, fd, &sense_data, msg);
	}
	else {
		*msg = "Command succeeded";
		rc = DEVICE_GOOD;
	}

	return rc;
}

/**
 * Read a record from tape
 * @param device a pointer to the ibmtape backend
 * @param buf a pointer to read buffer
 * @param count read size
 * @param pos a pointer to position data. This function will update position infomation.
 * @param unusual_size a flag specified unusual size or not
 * @return read length on success or a negative value on error
 */
int ibmtape_read(void *device, char *buf, size_t count, struct tc_position *pos,
				 const bool unusual_size)
{
	ssize_t len = -1, read_len;
	int rc;
	bool silion = unusual_size;
	char *msg;
	struct ibmtape_data *priv = (struct ibmtape_data *) device;
	int    fd = priv->fd;
	size_t datacount = count;

	/*
	 * TODO: Check count is smaller than max of SSIZE_MAX
	 *       The prototype of read system call is
	 *       ssize_t read(int fd, void *buf, size_t count);
	 */

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_READ));
	ltfsmsg(LTFS_DEBUG3, "12150D", "read", count, ((struct ibmtape_data *) device)->drive_serial);

	if (priv->force_readperm) {
		priv->read_counter++;
		if (priv->read_counter > priv->force_readperm) {
			ltfsmsg(LTFS_INFO, "12222I", "read");
			ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_READ));
			return -EDEV_READ_PERM;
		}
	}

	if(global_data.crc_checking) {
		datacount = count + 4;
		/* Never fall into this block, fail safe to adjust record length*/
		if (datacount > LINUX_MAX_BLOCK_SIZE)
			datacount = LINUX_MAX_BLOCK_SIZE;
	}
	read_len = read(fd, buf, datacount);

	if ((!silion && (size_t)read_len != datacount) || (read_len <= 0)) {
		struct request_sense sense_data;

		rc = ibmtape_ioctlrc2err(device, fd , &sense_data, &msg);

		switch (rc) {
		case -EDEV_NO_SENSE:
			if (sense_data.fm) {
				/* Filemark Detected */
				ltfsmsg(LTFS_DEBUG, "12119D");
				rc = DEVICE_GOOD;
				pos->block++;
				pos->filemarks++;
				len = 0;
			}
			else if (sense_data.ili) {
				/* Illegal Length */
				int32_t diff_len;

				diff_len = (int32_t)sense_data.info;

				if (diff_len < 0) {
					ltfsmsg(LTFS_INFO, "12188I", diff_len, count - diff_len); // "Detect overrun condition"
					rc = -EDEV_OVERRUN;
				}
				else {
					ltfsmsg(LTFS_DEBUG, "12189D", diff_len, count - diff_len); // "Detect underrun condition"
					len = count - diff_len;
					rc = DEVICE_GOOD;
					pos->block++;
				}
			}
			else if (errno == EOVERFLOW) {
				ltfsmsg(LTFS_INFO, "12188I", count - read_len, read_len); // "Detect overrun condition"
				rc = -EDEV_OVERRUN;
			}
			else if ((size_t)read_len < count) {
				ltfsmsg(LTFS_DEBUG, "12189D", count - read_len, read_len); // "Detect underrun condition"
				len = read_len;
				rc = DEVICE_GOOD;
				pos->block++;
			}
			break;
		case -EDEV_FILEMARK_DETECTED:
			ltfsmsg(LTFS_DEBUG, "12119D");
			rc = DEVICE_GOOD;
			pos->block++;
			pos->filemarks++;
			len = 0;
			break;
		}

		if (rc != DEVICE_GOOD) {
			if ((rc != -EDEV_CRYPTO_ERROR && rc != -EDEV_KEY_REQUIRED) || ((struct ibmtape_data *) device)->is_data_key_set) {
				ltfsmsg(LTFS_INFO, "12196I", "READ", count, rc, errno, ((struct ibmtape_data *) device)->drive_serial);
				ibmtape_process_errors(device, rc, msg, "read", true);
			}
			len = rc;
		}
	}
	else {
		len = silion ? read_len : (ssize_t)datacount;
		pos->block++;
	}

	if(global_data.crc_checking && len > 4) {
		if (priv->f_crc_check)
			len = priv->f_crc_check(buf, len - 4);
		if (len < 0) {
			ltfsmsg(LTFS_ERR, "12201E");
			len = -EDEV_LBP_READ_ERROR;
		}
	}

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_READ));
	return len;
}

/**
 * Write a record to tape
 * When the drive detect early warning condition, this function will return {-ENOSPC, true}
 *
 * @param device a pointer to the ibmtape backend
 * @param buf a pointer to read buffer
 * @param count write size
 * @param pos a pointer to position data. This function will update position infomation.
 * @param unusual_size a flag specified unusual size or not
 * @return rc 0 on success or a negative value on error
 */

int ibmtape_write(void *device, const char *buf, size_t count, struct tc_position *pos)
{
	int rc = -1;
	ssize_t written;
	char *msg = "";
	struct request_sense sense_data;

	struct ibmtape_data *priv = (struct ibmtape_data *) device;
	int    fd = priv->fd;
	size_t     datacount = count;

	/*
	 * TODO: Check count is smaller than max of SSIZE_MAX
	 *       The prototype of write system call is
	 *       ssize_t write(int fd, const void *buf, size_t count);
	 */

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_WRITE));
	ltfsmsg(LTFS_DEBUG3, "12150D", "write", count, ((struct ibmtape_data *) device)->drive_serial);

	if ( priv->force_writeperm ) {
		priv->write_counter++;
		if ( priv->write_counter > priv->force_writeperm ) {
			ltfsmsg(LTFS_INFO, "12222I", "write");
			ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_WRITE));
			return -EDEV_WRITE_PERM;
		} else if ( priv->write_counter > (priv->force_writeperm - THREASHOLD_FORCE_WRITE_NO_WRITE) ) {
			ltfsmsg(LTFS_INFO, "12223I");
			pos->block++;
			ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_WRITE));
			return DEVICE_GOOD;
		}
	}

	errno = 0;
	/* Invoke _ioctl to Write */
	if(global_data.crc_checking) {
		if (priv->f_crc_enc)
			priv->f_crc_enc((void *)buf, count);
		datacount = count + 4;
	}
	written = write(fd, buf, datacount);
	if ((size_t)written != datacount || errno == ENOSPC) {
		ltfsmsg(LTFS_INFO, "12196I", "WRITE", count, rc, errno, ((struct ibmtape_data *) device)->drive_serial);

		if (errno == ENOSPC) {
			ibmtape_readpos(device, pos);
			if (pos->early_warning) {
				ltfsmsg(LTFS_WARN, "12074W", "write");
				rc = DEVICE_GOOD;
			}
			else if (pos->programmable_early_warning) {
				ltfsmsg(LTFS_WARN, "12166W", "write");
				rc = DEVICE_GOOD;
			}
		} else {
			rc = ibmtape_ioctlrc2err(device, fd , &sense_data, &msg);

			switch (rc) {
			case -EDEV_EARLY_WARNING:
				ltfsmsg(LTFS_WARN, "12074W", "write");
				rc = DEVICE_GOOD;
				ibmtape_readpos(device, pos);
				pos->early_warning = true;
				break;
			case -EDEV_PROG_EARLY_WARNING:
				ltfsmsg(LTFS_WARN, "12166W", "write");
				rc = DEVICE_GOOD;
				ibmtape_readpos(device, pos);
				pos->programmable_early_warning = true;
				break;
			}
		}

		if (rc != DEVICE_GOOD)
			ibmtape_process_errors(device, rc, msg, "write", true);

		if (rc == -EDEV_LBP_WRITE_ERROR)
			ltfsmsg(LTFS_ERR, "12200E");
	} else {
		rc = DEVICE_GOOD;
		pos->block++;
	}

	((struct ibmtape_data *) device)->dirty_acq_loss_w = true;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_WRITE));
	return rc;
}

/**
 * Write filemark(s) to tape
 * @param device a pointer to the ibmtape backend
 * @param count count to write filemark. If 0 only flush.
 * @param pos a pointer to position data. This function will update position infomation.
 * @param immed Set immediate bit on
 * @return rc 0 on success or a negative value on error
 */
int ibmtape_writefm(void *device, size_t count, struct tc_position *pos, bool immed)
{
	int rc = -1;
	char *msg;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_WRITEFM));
	ltfsmsg(LTFS_DEBUG, "12151D", "writefm", count, ((struct ibmtape_data *) device)->drive_serial);

	errno = 0;
	rc = _mt_command(device, (immed? MTWEOFI : MTWEOF), "WRITE FM", count, &msg);
	ibmtape_readpos(device, pos);

	if (rc != DEVICE_GOOD) {
		switch (rc) {
		case -EDEV_EARLY_WARNING:
			ltfsmsg(LTFS_WARN, "12074W", "writefm");
			rc = DEVICE_GOOD;
			pos->early_warning = true;
			break;
		case -EDEV_PROG_EARLY_WARNING:
			ltfsmsg(LTFS_WARN, "12166W", "writefm");
			rc = DEVICE_GOOD;
			pos->programmable_early_warning = true;
			break;
		default:
			if (pos->early_warning) {
				ltfsmsg(LTFS_WARN, "12074W", "writefm");
				rc = DEVICE_GOOD;
			}
			if (pos->programmable_early_warning) {
				ltfsmsg(LTFS_WARN, "12166W", "writefm");
				rc = DEVICE_GOOD;
			}
			break;
		}

		if (rc != DEVICE_GOOD) {
			ibmtape_process_errors(device, rc, msg, "writefm", true);
		}
	} else {
		if (pos->early_warning) {
			ltfsmsg(LTFS_WARN, "12074W", "writefm");
			rc = DEVICE_GOOD;
		}
		if (pos->programmable_early_warning) {
			ltfsmsg(LTFS_WARN, "12166W", "writefm");
			rc = DEVICE_GOOD;
		}
	}

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_WRITEFM));
	return rc;
}

/**
 * Rewind tape
 * @param device a pointer to the ibmtape backend
 * @param pos a pointer to position data. This function will update position infomation.
 * @return 0 on success or a negative value on error
 */
int ibmtape_rewind(void *device, struct tc_position *pos)
{
	int rc;
	char *msg;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_REWIND));
	ltfsmsg(LTFS_DEBUG, "12153D", "rewind", ((struct ibmtape_data *) device)->drive_serial);

	rc = _mt_command(device, MTREW, "REWIND", 0, &msg);
	ibmtape_readpos(device, pos);

	if (rc != DEVICE_GOOD) {
		ibmtape_process_errors(device, rc, msg, "rewind", true);
	}

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_REWIND));
	return rc;
}

/**
 * Locate to position on tape
 * @param device a pointer to the ibmtape backend
 * @param dest a position data of destination.
 * @param pos a pointer to position data. This function will update position infomation.
 * @return rc 0 on success or a negative value on error
 */
int ibmtape_locate(void *device, struct tc_position dest, struct tc_position *pos)
{
	int rc;
	char *msg;
	struct set_active_partition set_part;
	struct set_tape_position setpos;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_LOCATE));
	ltfsmsg(LTFS_DEBUG, "12152D", "locate", (unsigned long long)dest.partition,
		(unsigned long long)dest.block, ((struct ibmtape_data *) device)->drive_serial);

	if (pos->partition != dest.partition) {
		memset(&set_part, 0, sizeof(struct set_active_partition));
		set_part.partition_number = dest.partition;
		set_part.logical_block_id = dest.block;

		rc = _sioc_stioc_command(device, STIOC_SET_ACTIVE_PARTITION, "LOCATE(PART)", &set_part, &msg);
	}
	else {
		memset(&setpos, 0, sizeof(struct set_tape_position));
		setpos.logical_id = dest.block;
		setpos.logical_id_type = LOGICAL_ID_BLOCK_TYPE;

		rc = _sioc_stioc_command(device, STIOC_LOCATE_16, "LOCATE", &setpos, &msg);
	}

	if (rc != DEVICE_GOOD) {
		if ((unsigned long long)dest.block == TAPE_BLOCK_MAX && rc == -EDEV_EOD_DETECTED) {
			ltfsmsg(LTFS_DEBUG, "12123D", "Locate");
			rc = DEVICE_GOOD;
		}

		if (rc != DEVICE_GOOD)
			ibmtape_process_errors(device, rc, msg, "locate", true);
	}

	ibmtape_readpos(device, pos);

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_LOCATE));
	return rc;
}

/**
 * Space to position on tape
 * @param device a pointer to the ibmtape backend
 * @param count specify record or fm count to move
 * @param type specify type of move
 * @param pos a pointer to position data. This function will update position infomation.
 * @return rc 0 on success or a negative value on error
 */
int ibmtape_space(void *device, size_t count, TC_SPACE_TYPE type, struct tc_position *pos)
{
	int cmd;
	int rc;
	char *msg;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_SPACE));
	switch (type) {
		case TC_SPACE_EOD:
			ltfsmsg(LTFS_DEBUG, "12153D", "space to EOD", ((struct ibmtape_data *) device)->drive_serial);
			cmd = MTEOM;
			count = 0;
			break;
		case TC_SPACE_FM_F:
			ltfsmsg(LTFS_DEBUG, "12154D", "space forward file marks", (unsigned long long)count,
					((struct ibmtape_data *) device)->drive_serial);
			cmd = MTFSF;
			break;
		case TC_SPACE_FM_B:
			ltfsmsg(LTFS_DEBUG, "12154D", "space back file marks", (unsigned long long)count,
					((struct ibmtape_data *) device)->drive_serial);
			cmd = MTBSF;
			break;
		case TC_SPACE_F:
			ltfsmsg(LTFS_DEBUG, "12154D", "space forward records", (unsigned long long)count,
					((struct ibmtape_data *) device)->drive_serial);
			cmd = MTFSR;
			break;
		case TC_SPACE_B:
			ltfsmsg(LTFS_DEBUG, "12154D", "space back records", (unsigned long long)count,
					((struct ibmtape_data *) device)->drive_serial);
			cmd = MTBSR;
			break;
		default:
			/* unexpected space type */
			ltfsmsg(LTFS_INFO, "12127I");
			ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_SPACE));
			return EDEV_INVALID_ARG;
	}

	if ((unsigned long long)count > 0xFFFFFF) {
		/* count is too large for SPACE 6 command */
		ltfsmsg(LTFS_INFO, "12199I", count);
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_SPACE));
		return EDEV_INVALID_ARG;
	}

	rc = _mt_command(device, cmd, "SPACE", count, &msg);
	ibmtape_readpos(device, pos);

	if (rc != DEVICE_GOOD) {
		ibmtape_process_errors(device, rc, msg, "space", true);
	}

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_SPACE));
	return rc;
}

int ibmtape_long_erase(void *device)
{
	struct sioc_pass_through spt;
	int rc;
	unsigned char cdb[6];
	unsigned char sense[MAXSENSE];
	char *msg;
	int device_code = ((struct ibmtape_data *) device)->device_code;

	memset(&spt, 0, sizeof(spt));
	memset(cdb, 0, sizeof(cdb));
	memset(sense, 0, sizeof(sense));

	/* Prepare Data Buffer */
	spt.buffer_length = 0;
	spt.buffer = NULL;

	/* Prepare CDB */
	spt.cmd_length = sizeof(cdb);
	spt.cdb = cdb;
	spt.cdb[0] = 0x19;			/* SCSI erase code */
	spt.cdb[1] = 0x03;			/* set long bit and immed bit */
	spt.data_direction = SCSI_DATA_NONE;
	spt.timeout = ComputeTimeOut(device_code, EraseTimeOut);

	/* Prepare sense buffer */
	spt.sense_length = sizeof(sense);
	spt.sense = sense;

	/* Invoke _ioctl to send a page */
	rc = sioc_paththrough(device, &spt, &msg);
	if (rc != DEVICE_GOOD)
		ibmtape_process_errors(device, rc, msg, "long erase", true);

	return rc;
}

/**
 * Erase tape from current position
 * @param device a pointer to the ibmtape backend
 * @param pos a pointer to position data. This function will update position infomation.
 * @param long_erase Set long bit and immed bit ON.
 * @return 0 on success or a negative value on error
 */
int ibmtape_erase(void *device, struct tc_position *pos, bool long_erase)
{
	int rc;
	char *msg;
	int fd = *((int *) device);
	int device_code = ((struct ibmtape_data *) device)->device_code;
	struct request_sense sense_data;
	int progress;
	struct ltfs_timespec ts_start, ts_now;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_ERASE));
	if (long_erase) {
		ltfsmsg(LTFS_DEBUG, "12153D", "long erase", ((struct ibmtape_data *) device)->drive_serial);
		get_current_timespec(&ts_start);

		rc = ibmtape_long_erase(device);

		while (true) {
			rc = ibmtape_ioctlrc2err(device, fd , &sense_data, &msg);

			if (rc != -EDEV_OPERATION_IN_PROGRESS) {
				/* Erase operation is NOT in progress */
				if (rc == -EDEV_NO_SENSE)
					rc = DEVICE_GOOD;
				break;
			}

			if (device_code==IBM_3592) {
				get_current_timespec(&ts_now);
				ltfsmsg(LTFS_INFO, "12224I", (ts_now.tv_sec - ts_start.tv_sec)/60);
			}
			else {
				progress = (int)(sense_data.field[0] & 0xFF)<<8;
				progress += (int)(sense_data.field[1] & 0xFF);
				ltfsmsg(LTFS_INFO, "12225I", progress*100/0xFFFF);
			}
			sleep(60);
		}

	}
	else {
		ltfsmsg(LTFS_DEBUG, "12153D", "erase", ((struct ibmtape_data *) device)->drive_serial);
		rc = _st_command(device, STERASE, "ERASE", 1, &msg);	// param=1 means invoking short erase.
	}

	ibmtape_readpos(device, pos);

	if (rc != DEVICE_GOOD) {
		ibmtape_process_errors(device, rc, msg, "erase", true);
	}

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_ERASE));
	return rc;
}

/**
 * Load tape or rewind when a tape is already loaded
 * @param device a pointer to the ibmtape backend
 * @param pos a pointer to position data. This function will update position infomation.
 * @return 0 on success or a negative value on error
 */

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
	TC_MP_JK          = 0xB2,   /* Jaguar JK cartridge */
	TC_MP_JC          = 0x93,   /* Jaguar JC cartridge */
	TC_MP_JY          = 0xA3,   /* Jaguar JY cartridge */
	TC_MP_JL          = 0xB3,   /* Jaguar JL cartridge */
	TC_MP_JD          = 0x94,   /* Jaguar JD cartridge */
	TC_MP_JZ          = 0xA4,   /* Jaguar JZ cartridge */
};

int supported_cart[] = {
	TC_MP_LTO6D_CART,
	TC_MP_LTO5D_CART,
	TC_MP_JB,
	TC_MP_JC,
	TC_MP_JD,
	TC_MP_JK,
	TC_MP_JY,
	TC_MP_JL,
	TC_MP_JZ
};

int _ibmtape_load_unload(void *device, bool load, struct tc_position *pos)
{
	int rc;
	char *msg;
	bool take_dump = true;
	struct ibmtape_data *priv = ((struct ibmtape_data *) device);

	if (load) {
		rc = _mt_command(device, MTLOAD, "LOAD", 0, &msg);
	}
	else {
		rc = _mt_command(device, MTUNLOAD, "UNLOAD", 0, &msg);
	}

	if (rc != DEVICE_GOOD) {
		switch (rc) {
		case -EDEV_LOAD_UNLOAD_ERROR:
			if (priv->loadfailed) {
				take_dump = false;
			}
			else {
				priv->loadfailed = true;
			}
			break;
		case -EDEV_NO_MEDIUM:
		case -EDEV_BECOMING_READY:
		case -EDEV_MEDIUM_MAY_BE_CHANGED:
			take_dump = false;
			break;
		default:
			break;
		}
		ibmtape_readpos(device, pos);
		ibmtape_process_errors(device, rc, msg, "load unload", take_dump);
	}
	else {
		if (load) {
			ibmtape_readpos(device, pos);
			priv->tape_alert = 0;
		}
		else {
			pos->partition = 0;
			pos->block = 0;
			priv->tape_alert = 0;
		}
		priv->loadfailed = false;
	}

	return rc;
}

int ibmtape_load(void *device, struct tc_position *pos)
{
	int rc;
	unsigned int i;
	unsigned char buf[TC_MP_SUPPORTEDPAGE_SIZE];
	struct ibmtape_data *priv = ((struct ibmtape_data *) device);

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_LOAD));
	ltfsmsg(LTFS_DEBUG, "12153D", "load", priv->drive_serial);

	rc = _ibmtape_load_unload(device, true, pos);
	if (rc < 0) {
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_LOAD));
		return rc;
	}

	/* Check Cartridge type */
	rc = ibmtape_modesense(device, TC_MP_SUPPORTEDPAGE, TC_MP_PC_CURRENT, 0x00, buf, sizeof(buf));
	if (rc < 0) {
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_LOAD));
		return rc;
	}

	priv->loaded = true;
	priv->is_worm = false;

	if (buf[2] == 0x00) {
		ltfsmsg(LTFS_WARN, "12187W");
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_LOAD));
		return 0;
	}

	rc = -LTFS_UNSUPPORTED_MEDIUM;
	for (i = 0; i < sizeof(supported_cart)/sizeof(supported_cart[0]); ++i) {
		if(buf[2] == supported_cart[i]) {
			if (buf[2] == TC_MP_JY || buf[2] == TC_MP_JZ) {
				/* Detect WORM cartridge */
				ltfsmsg(LTFS_DEBUG, "12226D");
				priv->is_worm = true;
			}
			rc = 0;
			break;
		}
	}

	if(rc == -LTFS_UNSUPPORTED_MEDIUM)
		ltfsmsg(LTFS_INFO, "12157I", buf[2]);

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_LOAD));
	return rc;
}

/**
 * Unload tape
 * @param device a pointer to the ibmtape backend
 * @param pos a pointer to position data. This function will update position infomation.
 * @return 0 on success or a negative value on error
 */
int ibmtape_unload(void *device, struct tc_position *pos)
{
	int rc;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_UNLOAD));
	ltfsmsg(LTFS_DEBUG, "12153D", "unload", ((struct ibmtape_data *) device)->drive_serial);

	rc = _ibmtape_load_unload(device, false, pos);
	if (rc < 0) {
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_UNLOAD));
		return rc;
	} else {
		((struct ibmtape_data *)device)->loaded = false;
		((struct ibmtape_data *)device)->is_worm = false;
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_UNLOAD));
		return rc;
	}
}

/**
 * Tell the current position
 * @param device a pointer to the ibmtape backend
 * @param pos a pointer to position data. This function will update position infomation.
 * @return 0 on success or a negative value on error
 */
int ibmtape_readpos(void *device, struct tc_position *pos)
{
	int rc;
	char *msg;
	struct read_tape_position rp;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_READPOS));
	memset(&rp, 0, sizeof(struct read_tape_position));

	rp.data_format = RP_LONG_FORM;

	rc = _sioc_stioc_command(device, STIOC_READ_POSITION_EX, "READPOS", &rp, &msg);

	if (rc == DEVICE_GOOD) {
		pos->early_warning = rp.rp_data.rp_long.eop? true : false;
		pos->programmable_early_warning = rp.rp_data.rp_long.bpew? true : false;
		pos->partition = rp.rp_data.rp_long.active_partition;
		pos->block = ltfs_betou64(rp.rp_data.rp_long.logical_obj_number);
		pos->filemarks = ltfs_betou64(rp.rp_data.rp_long.logical_file_id);

		ltfsmsg(LTFS_DEBUG, "12155D", "readpos", (unsigned long long)pos->partition,
				(unsigned long long)pos->block, (unsigned long long)pos->filemarks,
				((struct ibmtape_data *) device)->drive_serial);
	}
	else {
		ibmtape_process_errors(device, rc, msg, "readpos", true);
	}

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_READPOS));
	return rc;
}

/**
 * Make/Unmake partition
 * @param device a pointer to the ibmtape backend
 * @param format specify type of format
 * @return 0 on success or a negative value on error
 */
int ibmtape_format(void *device, TC_FORMAT_TYPE format)
{
	struct sioc_pass_through spt;
	int rc;
	unsigned char cdb[6];
	unsigned char sense[MAXSENSE];
	char *msg;
	int device_code = ((struct ibmtape_data *) device)->device_code;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_FORMAT));
	ltfsmsg(LTFS_DEBUG, "12153D", "format", ((struct ibmtape_data *) device)->drive_serial);

	if ((unsigned char) format >= (unsigned char) TC_FORMAT_MAX) {
		ltfsmsg(LTFS_INFO, "12131I", format);
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_FORMAT));
		return -1;
	}

	memset(&spt, 0, sizeof(spt));
	memset(cdb, 0, sizeof(cdb));
	memset(sense, 0, sizeof(sense));

	/* Prepare Data Buffer */
	spt.buffer_length = 0;
	spt.buffer = NULL;

	/* Prepare CDB */
	spt.cmd_length = sizeof(cdb);
	spt.cdb = cdb;
	spt.cdb[0] = 0x04;			/* SCSI medium format code */
	spt.cdb[2] = (unsigned char) format;
	spt.data_direction = SCSI_DATA_NONE;
	spt.timeout = ComputeTimeOut(device_code, FormatTimeOut);

	/* Prepare sense buffer */
	spt.sense_length = sizeof(sense);
	spt.sense = sense;

	/* Invoke _ioctl to send a page */
	rc = sioc_paththrough(device, &spt, &msg);
	if (rc != DEVICE_GOOD)
		ibmtape_process_errors(device, rc, msg, "format", true);

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_FORMAT));
	return rc;
}

/**
 * Tell log data from the drive
 * @param device a pointer to the ibmtape backend
 * @param page page code of log sense
 * @param buf pointer to buffer to store log data
 * @param size length of the buffer
 * @return 0 on success or a negative value on error
 */
#define MAX_UINT16 (0x0000FFFF)

int ibmtape_logsense_page(void *device, const uint8_t page, const uint8_t subpage,
						  unsigned char *buf, const size_t size)
{
	int rc;
	char *msg;
	struct log_sense10_page log_page;

	ltfsmsg(LTFS_DEBUG3, "12152D", "logsense", (unsigned long long)page, (unsigned long long)subpage,
			((struct ibmtape_data *) device)->drive_serial);

	log_page.page_code = page;
	log_page.subpage_code = subpage;
	log_page.len = 0;
	log_page.parm_pointer = 0;
	memset(log_page.data, 0, LOGSENSEPAGE);

	rc = _sioc_stioc_command(device, SIOC_LOG_SENSE10_PAGE, "LOGSENSE", &log_page, &msg);

	if (rc != DEVICE_GOOD) {
		ibmtape_process_errors(device, rc, msg, "logsense page", true);
	}
	else {
		memcpy(buf, log_page.data, size);
	}

	return rc;
}

int ibmtape_logsense(void *device, const uint8_t page, unsigned char *buf, const size_t size)
{
	int ret = 0;
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_LOGSENSE));
	ret = ibmtape_logsense_page(device, page, 0, buf, size);
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_LOGSENSE));
	return ret;
}

/**
 * Tell the remaining capacity
 * @param device a pointer to the ibmtape backend
 * @param cap pointer to teh capasity data. This function will update capasity infomation.
 * @return 0 on success or a negative value on error
 */
#define LOG_TAPECAPACITY         (0x31)

enum {
	TAPECAP_REMAIN_0 = 0x0001,	/* < Partition0 Remaining Capacity */
	TAPECAP_REMAIN_1 = 0x0002,	/* < Partition1 Remaining Capacity */
	TAPECAP_MAX_0 = 0x0003,		/* < Partition0 MAX Capacity */
	TAPECAP_MAX_1 = 0x0004,		/* < Partition1 MAX Capacity */
	TAPECAP_SIZE = 0x0005,
};

#define LOG_VOLUMESTATS          (0x17)
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
	VOLSTATUS_PARTITION_CAP   = 0x0202, /* < Approximate native capacity of partitions */
	VOLSTATUS_PART_USED_CAP   = 0x0203, /* < Approximate used native capacity of partitions */
	VOLSTATUS_PART_REMAIN_CAP = 0x0204, /* < Approximate remaining capacity of partitions */
};

#define PARTITIOIN_REC_HEADER_LEN (4)

int ibmtape_remaining_capacity(void *device, struct tc_remaining_cap *cap)
{
	unsigned char logdata[LOGSENSEPAGE];
	unsigned char buf[32];
	int param_size, i;
	int length;
	int offset;
	uint32_t logcap;
	int rc;
	const DRIVE_TYPE drive_type = ((struct ibmtape_data *) device)->drive_type;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_REMAINCAP));
	if (drive_type == DRIVE_LTO5 || drive_type == DRIVE_LTO5_HH) {
		/* Issue LogPage 0x31 */
		rc = ibmtape_logsense(device, LOG_TAPECAPACITY, logdata, LOGSENSEPAGE);
		if (rc) {
			ltfsmsg(LTFS_INFO, "12135I", LOG_TAPECAPACITY, rc);
			ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_REMAINCAP));
			return rc;
		}

		for(i = TAPECAP_REMAIN_0; i < TAPECAP_SIZE; i++) {
			if (parse_logPage(logdata, (uint16_t) i, &param_size, buf, sizeof(buf))
				|| param_size != sizeof(uint32_t)) {
				ltfsmsg(LTFS_INFO, "12136I");
				ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_REMAINCAP));
				return -EDEV_NO_MEMORY;
			}

			logcap = ltfs_betou32(buf);

			switch (i) {
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
				ltfsmsg(LTFS_INFO, "12137I", i);
				ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_REMAINCAP));
				return -EDEV_INVALID_ARG;
				break;
			}
		}
	}
	else {
		/* Issue LogPage 0x17 */
		rc = ibmtape_logsense(device, LOG_VOLUMESTATS, logdata, LOGSENSEPAGE);
		if (rc) {
			ltfsmsg(LTFS_INFO, "12135I", LOG_VOLUMESTATS, rc);
			ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_REMAINCAP));
			return rc;
		}

		/* parse param 0x202 - nominal capacity of the partitions */
		if (parse_logPage(logdata, (uint16_t)VOLSTATUS_PARTITION_CAP, &param_size, buf, sizeof(buf))) {
			ltfsmsg(LTFS_INFO, "12136I");
			ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_REMAINCAP));
			return -EDEV_NO_MEMORY;
		}

		memset(cap, 0, sizeof(struct tc_remaining_cap));

		cap->max_p0 = ltfs_betou32(&buf[PARTITIOIN_REC_HEADER_LEN]);
		offset = (int)buf[0] + 1;
		length = (int)buf[offset] + 1;

		if (offset + length <= param_size) {
			cap->max_p1 = ltfs_betou32(&buf[offset + PARTITIOIN_REC_HEADER_LEN]);
		}

		/* parse param 0x204 - remaining capacity of the partitions */
		if (parse_logPage(logdata, (uint16_t)VOLSTATUS_PART_REMAIN_CAP, &param_size, buf, sizeof(buf))) {
			ltfsmsg(LTFS_INFO, "12136I");
			ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_REMAINCAP));
			return -EDEV_NO_MEMORY;
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
	}

	ltfsmsg(LTFS_DEBUG3, "12152D", "capacity part0", (unsigned long long)cap->remaining_p0,
			(unsigned long long)cap->max_p0, ((struct ibmtape_data *) device)->drive_serial);
	ltfsmsg(LTFS_DEBUG3, "12152D", "capacity part1", (unsigned long long)cap->remaining_p1,
		(unsigned long long)cap->max_p1, ((struct ibmtape_data *) device)->drive_serial);

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_REMAINCAP));
	return 0;
}


/**
 * Get mode data
 * @param device a pointer to the ibmtape backend
 * @param page a page id of mode data
 * @param pc page control value for mode sense command
 * @param buf pointer to mode page data. this function will update this data
 * @param size length of buf
 * @return 0 on success or a negative value on error
 */
int ibmtape_modesense(void *device, const uint8_t page, const TC_MP_PC_TYPE pc, const uint8_t subpage,
					  unsigned char *buf, const size_t size)
{
	struct sioc_pass_through spt;
	int rc;
	unsigned char cdb[10];
	unsigned char sense[MAXSENSE];
	char *msg;
	int device_code = ((struct ibmtape_data *) device)->device_code;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_MODESENSE));
	ltfsmsg(LTFS_DEBUG3, "12156D", "modesense", page, ((struct ibmtape_data *) device)->drive_serial);

	memset(&spt, 0, sizeof(spt));
	memset(cdb, 0, sizeof(cdb));
	memset(sense, 0, sizeof(sense));

	/* Prepare Data Buffer */
	if (size > MAX_UINT16)
		spt.buffer_length = MAX_UINT16;
	else
		spt.buffer_length = size;
	spt.buffer = buf;

	/* Prepare CDB */
	spt.cmd_length = sizeof(cdb);
	spt.cdb = cdb;
	spt.cdb[0] = 0x5a;			/* SCSI mode sense code */
	spt.cdb[2] = pc | page;
	spt.cdb[3] = subpage;
	ltfs_u16tobe(spt.cdb + 7, spt.buffer_length);
	spt.data_direction = SCSI_DATA_IN;
	spt.timeout = ComputeTimeOut(device_code, ModeSenseTimeOut);

	/* Prepare sense buffer */
	spt.sense_length = sizeof(sense);
	spt.sense = sense;

	/* Invoke _ioctl to modesense */
	rc = sioc_paththrough(device, &spt, &msg);
	if (rc != DEVICE_GOOD)
		ibmtape_process_errors(device, rc, msg, "modesense", true);

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_MODESENSE));
	return rc;
}

/**
 * Set mode data
 * @param device a pointer to the ibmtape backend
 * @param buf pointer to mode page data. This data will be sent to the drive
 * @param size length of buf
 * @return 0 on success or a negative value on error
 */
int ibmtape_modeselect(void *device, unsigned char *buf, const size_t size)
{
	struct sioc_pass_through spt;
	int rc;
	unsigned char cdb[10];
	unsigned char sense[MAXSENSE];
	char *msg;
	int device_code = ((struct ibmtape_data *) device)->device_code;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_MODESELECT));
	ltfsmsg(LTFS_DEBUG3, "12153D", "modeselect", ((struct ibmtape_data *) device)->drive_serial);

	memset(&spt, 0, sizeof(spt));
	memset(cdb, 0, sizeof(cdb));
	memset(sense, 0, sizeof(sense));

	/* Prepare Data Buffer */
	spt.buffer_length = size;
	spt.buffer = buf;

	/* Prepare CDB */
	spt.cmd_length = sizeof(cdb);
	spt.cdb = cdb;
	spt.cdb[0] = 0x55;			/* SCSI mode select code */
	ltfs_u16tobe(spt.cdb + 7, spt.buffer_length);
	spt.data_direction = SCSI_DATA_OUT;
	spt.timeout = ComputeTimeOut(device_code, ModeSelectTimeOut);

	/* Prepare sense buffer */
	spt.sense_length = sizeof(sense);
	spt.sense = sense;

	/* Invoke _ioctl to modeselect */
	rc = sioc_paththrough(device, &spt, &msg);
	if (rc != DEVICE_GOOD) {
		if (rc == -EDEV_MODE_PARAMETER_ROUNDED)
			rc = DEVICE_GOOD;

		if (rc != DEVICE_GOOD)
			ibmtape_process_errors(device, rc, msg, "modeselect", true);
	}

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_MODESELECT));
	return rc;
}

/**
 * Prevent medium removal
 * @param device a pointer to the ibmtape backend
 * @return 0 on success or a negative value on error
 */
int ibmtape_prevent_medium_removal(void *device)
{
	int rc;
	char *msg;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_PREVENTM));
	ltfsmsg(LTFS_DEBUG, "12153D", "prevent medium removal", ((struct ibmtape_data *) device)->drive_serial);

	rc = _sioc_stioc_command(device, STIOC_PREVENT_MEDIUM_REMOVAL, "PREVENT MED REMOVAL", NULL, &msg);

	if (rc != DEVICE_GOOD) {
		ibmtape_process_errors(device, rc, msg, "prevent medium removal", true);
	}

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_PREVENTM));
	return rc;
}

/**
 * Allow medium removal
 * @param device a pointer to the ibmtape backend
 * @return 0 on success or a negative value on error
 */
int ibmtape_allow_medium_removal(void *device)
{
	int rc;
	char *msg;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_ALLOWMREM));
	ltfsmsg(LTFS_DEBUG, "12153D", "allow medium removal", ((struct ibmtape_data *) device)->drive_serial);

	rc = _sioc_stioc_command(device, STIOC_ALLOW_MEDIUM_REMOVAL, "ALLOW MED REMOVAL", NULL, &msg);

	if (rc != DEVICE_GOOD) {
		ibmtape_process_errors(device, rc, msg, "allow medium removal", true);
	}

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_ALLOWMREM));
	return rc;
}

/**
 * Read attribute
 * @param device a pointer to the ibmtape backend
 * @param part partition to read attribute
 * @param id attribute id to get
 * @param buf pointer to the attribute buffer. This function will update this value.
 * @param size length of the buffer
 * @return 0 on success or a negative value on error
 */
int ibmtape_read_attribute(void *device, const tape_partition_t part, const uint16_t id,
						   unsigned char *buf, const size_t size)
{
	struct sioc_pass_through spt;
	int rc;
	unsigned char cdb[16];
	unsigned char sense[MAXSENSE];
	char *msg;
	bool take_dump= true;
	int device_code = ((struct ibmtape_data *) device)->device_code;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_READATTR));
	ltfsmsg(LTFS_DEBUG3, "12152D", "readattr", (unsigned long long)part,
			(unsigned long long)id, ((struct ibmtape_data *) device)->drive_serial);

	memset(&spt, 0, sizeof(spt));
	memset(cdb, 0, sizeof(cdb));
	memset(sense, 0, sizeof(sense));

	/* Prepare Data Buffer */
	spt.buffer_length = size + 4;
	spt.buffer = calloc(1, spt.buffer_length);
	if (spt.buffer == NULL) {
		ltfsmsg(LTFS_ERR, "10001E", "ibmtape_read_attribute: data buffer");
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_READATTR));
		return -EDEV_NO_MEMORY;
	}

	/* Prepare CDB */
	spt.cmd_length = sizeof(cdb);
	spt.cdb = cdb;
	spt.cdb[0] = 0x8C;			/* 0x8C SCSI read Attribute code */
	spt.cdb[1] = 0x00;			/* Service Action 0x00: VALUE */
	spt.cdb[7] = part;
	ltfs_u16tobe(spt.cdb + 8, id);
	ltfs_u32tobe(spt.cdb + 10, spt.buffer_length);
	spt.data_direction = SCSI_DATA_IN;
	spt.timeout = ComputeTimeOut(device_code, DefaultTimeOut);

	/* Prepare sense buffer */
	spt.sense_length = sizeof(sense);
	spt.sense = sense;

	/* Invoke _ioctl to Read Attribute */
	rc = sioc_paththrough(device, &spt, &msg);
	if (rc != DEVICE_GOOD) {
		if ( rc == -EDEV_INVALID_FIELD_CDB )
			take_dump = false;

		ibmtape_process_errors(device, rc, msg, "readattr", take_dump);

		if (rc < 0 &&
			id != TC_MAM_PAGE_COHERENCY &&
			id != TC_MAM_APP_VENDER &&
			id != TC_MAM_APP_NAME &&
			id != TC_MAM_APP_VERSION &&
			id != TC_MAM_USER_MEDIUM_LABEL &&
			id != TC_MAM_TEXT_LOCALIZATION_IDENTIFIER &&
			id != TC_MAM_BARCODE &&
			id != TC_MAM_APP_FORMAT_VERSION)
			ltfsmsg(LTFS_INFO, "12144I", rc);
	} else {
		memcpy(buf, (spt.buffer + 4), size);
		free(spt.buffer);
	}

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_READATTR));
	return rc;
}

/**
 * Write attribute
 * @param device a pointer to the ibmtape backend
 * @param part partition to read attribute
 * @param id attribute id to get
 * @param buf pointer to the attribute buffer. This function will send this value to the tape.
 *            This function expected this buffer does not contain attribute header.
 * @param size length of the buffer
 * @return 0 on success or a negative value on error
 */
int ibmtape_write_attribute(void *device, const tape_partition_t part, const unsigned char *buf,
							const size_t size)
{
	struct sioc_pass_through spt;
	int rc;
	unsigned char cdb[16];
	unsigned char sense[MAXSENSE];
	char *msg;
	int device_code = ((struct ibmtape_data *) device)->device_code;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_WRITEATTR));
	ltfsmsg(LTFS_DEBUG3, "12154D", "writeattr", (unsigned long long)part,
			((struct ibmtape_data *) device)->drive_serial);

	memset(&spt, 0, sizeof(spt));
	memset(cdb, 0, sizeof(cdb));
	memset(sense, 0, sizeof(sense));

	/* Prepare Data Buffer */
	spt.buffer_length = size + 4;
	spt.buffer = calloc(1, spt.buffer_length);
	if (spt.buffer == NULL) {
		ltfsmsg(LTFS_ERR, "10001E", "ibmtape_write_attribute: data buffer");
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_WRITEATTR));
		return -EDEV_NO_MEMORY;
	}
	ltfs_u32tobe(spt.buffer, size);
	memcpy((spt.buffer + 4), buf, size);

	/* Prepare CDB */
	spt.cmd_length = sizeof(cdb);
	spt.cdb = cdb;
	spt.cdb[0] = 0x8D;			/* SCSI Write Attribute code */
	spt.cdb[1] = 0x01;			/* Write through bit on */
	spt.cdb[7] = part;
	ltfs_u32tobe(spt.cdb + 10, spt.buffer_length);
	spt.data_direction = SCSI_DATA_OUT;
	spt.timeout = ComputeTimeOut(device_code, DefaultTimeOut);

	/* Prepare sense buffer */
	spt.sense_length = sizeof(sense);
	spt.sense = sense;

	/* Invoke _ioctl to Write Attribute */
	rc = sioc_paththrough(device, &spt, &msg);
	if (rc != DEVICE_GOOD)
		ibmtape_process_errors(device, rc, msg, "writeattr", true);

	free(spt.buffer);

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_WRITEATTR));
	return rc;
}

int ibmtape_allow_overwrite(void *device, const struct tc_position pos)
{
	int rc;
	char *msg;
	struct allow_data_overwrite append_pos;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_ALLOWOVERW));
	ltfsmsg(LTFS_DEBUG, "12152D", "allow overwrite", (unsigned long long)pos.partition,
		(unsigned long long)pos.block, ((struct ibmtape_data *) device)->drive_serial);

	memset(&append_pos, 0, sizeof(append_pos));

	append_pos.partition_number = pos.partition;
	append_pos.logical_block_id = pos.block;

	rc = _sioc_stioc_command(device, STIOC_ALLOW_DATA_OVERWRITE, "ALLOW OVERWRITE", &append_pos, &msg);

	if (rc != DEVICE_GOOD) {
		if (rc == -EDEV_EOD_DETECTED) {
			ltfsmsg(LTFS_DEBUG, "12123D", "Allow Overwrite");
			rc = DEVICE_GOOD;
		}

		if (rc != DEVICE_GOOD)
			ibmtape_process_errors(device, rc, msg, "allow overwrite", true);
	}

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_ALLOWOVERW));
	return rc;
}

/**
 * Report density infomation
 * @param device a pointer to the ibmtape backend
 * @param rep pointer to the density infomation. this function will update this value.
 * @param medium set medium bit on
 * @return 0 on success or a negative value on error
 */
#define DENSITY_HEADER_SIZE     (4)
#define DENSITY_DESCRIPTER_SIZE (52)

int ibmtape_report_density(void *device, struct tc_density_report *rep, bool medium)
{
	struct sioc_pass_through spt;
	int rc, i;
	unsigned char cdb[10];
	unsigned char sense[MAXSENSE];
	char *msg;
	int device_code = ((struct ibmtape_data *) device)->device_code;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_REPDENSITY));
	ltfsmsg(LTFS_DEBUG, "12156D", "report density", medium, ((struct ibmtape_data *) device)->drive_serial);

	memset(&spt, 0, sizeof(spt));
	memset(cdb, 0, sizeof(cdb));
	memset(sense, 0, sizeof(sense));

	/* Prepare Data Buffer */
	spt.buffer_length = MAX_UINT16;
	spt.buffer = calloc(1, spt.buffer_length);
	if (spt.buffer == NULL) {
		ltfsmsg(LTFS_ERR, "10001E", "ibmtape_report_density: data buffer");
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_REPDENSITY));
		return -EDEV_NO_MEMORY;
	}

	/* Prepare CDB */
	spt.cmd_length = sizeof(cdb);
	spt.cdb = cdb;
	spt.cdb[0] = 0x44;			/* 0x44 SCSI Report Density code */
	if (medium)
		spt.cdb[1] = 0x01;			/* Set media bit to  */
	ltfs_u16tobe(spt.cdb + 7, spt.buffer_length);
	spt.data_direction = SCSI_DATA_IN;
	spt.timeout = ComputeTimeOut(device_code, ReportDensityTimeOut);

	/* Prepare sense buffer */
	spt.sense_length = sizeof(sense);
	spt.sense = sense;

	/* Invoke _ioctl to Read Attribute */
	rc = sioc_paththrough(device, &spt, &msg);
	if (rc != DEVICE_GOOD)
		ibmtape_process_errors(device, rc, msg, "report density", true);
	else {
		rep->size = ltfs_betou16(spt.buffer) / DENSITY_DESCRIPTER_SIZE;

		if((rep->size) > TC_MAX_DENSITY_REPORTS)
			rep->size = TC_MAX_DENSITY_REPORTS;

		for(i = 0; i < rep->size - 1; i++) {
			rep->density[i].primary = spt.buffer[DENSITY_HEADER_SIZE + (i * DENSITY_DESCRIPTER_SIZE)];
			rep->density[i].secondary = spt.buffer[DENSITY_HEADER_SIZE + (i * DENSITY_DESCRIPTER_SIZE) + 1];
		}

		free(spt.buffer);
	}

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_REPDENSITY));
	return rc;
}

/**
 * Set compression setting
 * @param device a pointer to the ibmtape backend
 * @param enable_compression enable: true, disable: false
 * @param pos a pointer to position data. This function will update position infomation.
 * @return 0 on success or a negative value on error
 */
int ibmtape_set_compression(void *device, const bool enable_compression, struct tc_position *pos)
{
	int rc;
	unsigned char buf[TC_MP_COMPRESSION_SIZE];

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_SETCOMPRS));
	rc = ibmtape_modesense(device, TC_MP_COMPRESSION, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
	if (rc != DEVICE_GOOD) {
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_SETCOMPRS));
		return rc;
	}

	buf[0] = 0x00;
	buf[1] = 0x00;
	if(enable_compression)
		buf[18] |= 0x80; /* Set DCE field*/
	else
		buf[18] &= 0x7F; /* Unset DCE field*/

	rc = ibmtape_modeselect(device, buf, sizeof(buf));

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_SETCOMPRS));
	return rc;
}

/**
 * Return drive setting in default
 * @param device a pointer to the ibmtape backend
 * @return 0 on success or a negative value on error
 */
int ibmtape_set_default(void *device)
{
	struct ibmtape_data *priv = (struct ibmtape_data *) device;
	int rc;
	unsigned char buf[TC_MP_READ_WRITE_CTRL_SIZE];
	char *msg;
	struct stchgp_s param;
	struct eot_warn eot;
	int device_code = ((struct ibmtape_data *) device)->device_code;
	int retry;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_SETDEFAULT));
	/* Disable Read across EOD on 3592 drive */
	if (device_code == IBM_3592) {
		ltfsmsg(LTFS_DEBUG, "12153D", __FUNCTION__, "Disabling read across EOD");
		rc = ibmtape_modesense(device, TC_MP_READ_WRITE_CTRL, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
		if (rc != DEVICE_GOOD) {
			ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_SETDEFAULT));
			return rc;
		}

		buf[0]  = 0x00;
		buf[1]  = 0x00;
		buf[24] = 0x0C;

		rc = ibmtape_modeselect(device, buf, sizeof(buf));
		if (rc != DEVICE_GOOD) {
			ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_SETDEFAULT));
			return rc;
		}
	}

	/* set SILI bit */
	ltfsmsg(LTFS_DEBUG, "12153D", __FUNCTION__, "Setting SILI bit");
	while (true) {
		memset(&param, 0, sizeof(struct stchgp_s));
		rc = _sioc_stioc_command(device, STIOCQRYP, "GET PARAM", &param, &msg);
		if (rc != DEVICE_GOOD) {
			ibmtape_process_errors(device, rc, msg, "get default parameter", true);
			ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_SETDEFAULT));
			return rc;
		}

		param.read_sili_bit = true;

		rc = _sioc_stioc_command(device, STIOCSETP, "SET PARAM", &param, &msg);
		if (rc == DEVICE_GOOD || retry > 10)
			break;

		/* In case of STIOCSETP error, reopen the device and retry */
		close(priv->fd);
		priv->fd = open(priv->devname, O_RDWR | O_NDELAY);

		if (priv->fd < 0) {
			priv->fd = open(priv->devname, O_RDONLY | O_NDELAY);
			if (priv->fd < 0) {
				ltfsmsg(LTFS_INFO, "12114I", priv->devname, errno);
				rc = -EDEV_DEVICE_UNOPENABLE;
				break;
			}
			ltfsmsg(LTFS_WARN, "12115W", priv->devname);
		}
		retry ++;
	}

	if (rc != DEVICE_GOOD) {
		ibmtape_process_errors(device, rc, msg, "set default parameter", true);
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_SETDEFAULT));
		return rc;
	}

	/* set logical block protection */
	if (global_data.crc_checking) {
		ltfsmsg(LTFS_DEBUG, "12153D", __FUNCTION__, "Setting LBP");
		rc = ibmtape_set_lbp(device, true);
	} else {
		ltfsmsg(LTFS_DEBUG, "12153D", __FUNCTION__, "Resetting LBP");
		rc = ibmtape_set_lbp(device, false);
	}
	if (rc != DEVICE_GOOD) {
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_SETDEFAULT));
		return rc;
	}

	/* EOT handling */
	memset(&eot, 0, sizeof(struct eot_warn));
	rc = _sioc_stioc_command(device, STIOC_QUERY_EOT_WARN, "GET EOT WARN", &eot, &msg);
	if (rc != DEVICE_GOOD) {
		ibmtape_process_errors(device, rc, msg, "get default parameter (EOT handling)", true);
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_SETDEFAULT));
		return rc;
	}

	if (eot.warn == 0) {
		eot.warn = 1;
		rc = _sioc_stioc_command(device, STIOC_SET_EOT_WARN, "SET EOT WARN", &eot, &msg);
		if (rc != DEVICE_GOOD) {
			ibmtape_process_errors(device, rc, msg, "set default parameter (EOT handling)", true);
			ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_SETDEFAULT));
			return rc;
		}
	}

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_SETDEFAULT));
	return rc;
}

/**
 * Get cartridge health information
 * @param device a pointer to the ibmtape backend
 * @return 0 on success or a negative value on error
 */

#define LOG_TAPE_ALERT               (0x2E)
#define LOG_PERFORMANCE              (0x37)
#define LOG_PERFORMANCE_CAPACITY_SUB (0x64) // Scope(7-6): Mount Values
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
	PERF_ACTIVE_CQ_LOSS_W     = 0x7113, /* < Active CQ loss Write */
};

static uint16_t perfstats[] = {
	PERF_CART_CONDITION,
};

int ibmtape_get_cartridge_health(void *device, struct tc_cartridge_health *cart_health)
{
	unsigned char logdata[LOGSENSEPAGE];
	unsigned char buf[16];
	int param_size, i;
	uint64_t loghlt;
	int rc;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_GETCARTHLTH));

	/* Issue LogPage 0x37 */
	cart_health->tape_efficiency  = UNSUPPORTED_CARTRIDGE_HEALTH;
	rc = ibmtape_logsense(device, LOG_PERFORMANCE, logdata, LOGSENSEPAGE);
	if (rc)
		ltfsmsg(LTFS_INFO, "12217I", LOG_PERFORMANCE, rc, "get cart health");
	else {
		for(i = 0; i < (int)((sizeof(perfstats)/sizeof(perfstats[0]))); i++) { /* BEAM: loop doesn't iterate - Use loop for future enhancement. */
			if (parse_logPage(logdata, perfstats[i], &param_size, buf, 16)) {
				ltfsmsg(LTFS_INFO, "12218I", LOG_PERFORMANCE, "get cart health");
			} else {
				switch(param_size) {
				case sizeof(uint8_t):
					loghlt = (uint64_t)(buf[0]);
					break;
				case sizeof(uint16_t):
					loghlt = (uint64_t)ltfs_betou16(buf);
					break;
				case sizeof(uint32_t):
					loghlt = (uint32_t)ltfs_betou32(buf);
					break;
				case sizeof(uint64_t):
					loghlt = ltfs_betou64(buf);
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
	rc = ibmtape_logsense(device, LOG_VOLUMESTATS, logdata, LOGSENSEPAGE);
	if (rc)
		ltfsmsg(LTFS_INFO, "12217I", LOG_VOLUMESTATS, rc, "get cart health");
	else {
		for(i = 0; i < (int)((sizeof(volstats)/sizeof(volstats[0]))); i++) {
			if (parse_logPage(logdata, volstats[i], &param_size, buf, 16)) {
				ltfsmsg(LTFS_INFO, "12218I", LOG_VOLUMESTATS, "get cart health");
			} else {
				switch(param_size) {
				case sizeof(uint8_t):
					loghlt = (uint64_t)(buf[0]);
					break;
				case sizeof(uint16_t):
					loghlt = (uint64_t)ltfs_betou16(buf);
					break;
				case sizeof(uint32_t):
					loghlt = (uint32_t)ltfs_betou32(buf);
					break;
				case sizeof(uint64_t):
					loghlt = ltfs_betou64(buf);
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

/**
 * Get tape alert from the drive this value shall be latched by backends and shall be cleard by
 * clear_tape_alert() on write clear method
 * @param device Device handle returned by the backend's open().
 * @param tape alert On success, the backend must fill this value with the tape alert
 *                    "-1" shows the unsupported value except tape alert.
 * @return 0 on success or a negative value on error.
 */
int ibmtape_get_tape_alert(void *device, uint64_t *tape_alert)
{
	unsigned char logdata[LOGSENSEPAGE];
	unsigned char buf[16];
	int param_size, i;
	int rc;
	uint64_t ta;
	struct ibmtape_data *priv = (struct ibmtape_data *) device;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_GETTAPEALT));
	/* Issue LogPage 0x2E */
	ta = 0;
	rc = ibmtape_logsense(device, LOG_TAPE_ALERT, logdata, LOGSENSEPAGE);
	if (rc)
		ltfsmsg(LTFS_INFO, "12217I", LOG_TAPE_ALERT, rc, "get tape alert");
	else {
		for(i = 1; i <= 64; i++) {
			if (parse_logPage(logdata, (uint16_t) i, &param_size, buf, 16)
				|| param_size != sizeof(uint8_t)) {
				ltfsmsg(LTFS_INFO, "12218I", LOG_TAPE_ALERT, "get tape alert");
				ta = 0;
			}

			if(buf[0])
				ta += (uint64_t)(1) << (i - 1);
		}
	}

	priv->tape_alert |= ta;
	*tape_alert = priv->tape_alert;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_GETTAPEALT));
	return rc;
}

/**
 * clear latched tape alert from the drive
 * @param device Device handle returned by the backend's open().
 * @param tape_alert value to clear tape alert. Backend shall be clear the specicied bits in this value.
 * @return 0 on success or a negative value on error.
 */
int ibmtape_clear_tape_alert(void *device, uint64_t tape_alert)
{
	struct ibmtape_data *priv = (struct ibmtape_data *) device;
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_CLRTAPEALT));
	priv->tape_alert &= ~tape_alert;
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_CLRTAPEALT));
	return 0;
}

/**
 * Get drive parameter
 * @param device a pointer to the ibmtape backend
 * @param drive_param pointer to the drive parameter infomation. This function will update this value.
 * @return 0 on success or a negative value on error
 */
 uint32_t _ibmtape_get_block_limits(void *device)
{
	struct sioc_pass_through spt;
	int rc;
	unsigned char cdb[6];
	unsigned char buf[6];
	unsigned char sense[MAXSENSE];
	char *msg;
	uint32_t length = 0;
	int device_code = ((struct ibmtape_data *) device)->device_code;

	ltfsmsg(LTFS_DEBUG, "12153D", "read block limits", ((struct ibmtape_data *) device)->drive_serial);

	memset(&spt, 0, sizeof(spt));
	memset(cdb, 0, sizeof(cdb));
	memset(sense, 0, sizeof(sense));

	/* Prepare Data Buffer */
	spt.buffer_length = sizeof(buf);
	spt.buffer = buf;

	/* Prepare CDB (Never issue long erase) */
	spt.cmd_length = sizeof(cdb);
	spt.cdb = cdb;
	spt.cdb[0] = 0x05;			/* SCSI read block limits code */
	spt.data_direction = SCSI_DATA_IN;
	spt.timeout = ComputeTimeOut(device_code, ReadBlockLimitsTimeOut);

	/* Prepare sense buffer */
	spt.sense_length = sizeof(sense);
	spt.sense = sense;

	/* Invoke _ioctl to Erase */
	rc = sioc_paththrough(device, &spt, &msg);
	if (rc != DEVICE_GOOD)
		ibmtape_process_errors(device, rc, msg, "read block limits", true);
	else {
		length = (buf[1] << 16) + (buf[2] << 8) + (buf[3] & 0xFF);
		if(length > 1 * MB)
			length = 1 * MB;
	}

	return length;
}

int ibmtape_get_parameters(void *device, struct tc_drive_param *drive_param)
{
	int rc;
	unsigned char buf[16];

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_GETPARAM));
	if (global_data.crc_checking)
		drive_param->max_blksize = MIN(_ibmtape_get_block_limits(device),  LINUX_MAX_BLOCK_SIZE - 4);
	else
		drive_param->max_blksize = MIN(_ibmtape_get_block_limits(device),  LINUX_MAX_BLOCK_SIZE);

	/* Set block size to variable length (0x00) */
	rc = ibmtape_modesense(device, 0x00, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
	if (rc != DEVICE_GOOD) {
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_GETPARAM));
		return rc;
	}

	drive_param->write_protect = buf[3] & 0x80 ? true : false;
	drive_param->logical_write_protect = false; /* In LTO, always false */

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_GETPARAM));
	return rc;
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

/**
 * Get a list of available tape devices for LTFS found in the host. The caller is
 * responsible from allocating the buffer to contain the tape drive information
 * by get_device_count() call.
 * When buf is NULL, this function just returns an available tape device count.
 * @param[out] buf Pointer to tc_drive_info structure array.
 *             The backend must fill this structure when this paramater is not NULL.
 * @param count size of array in buf.
 * @return on success, available device count on this system or a negative value on error.
 */
#define DEV_LIST_FILE "/proc/scsi/IBMtape"
int ibmtape_get_device_list(struct tc_drive_info *buf, int count)
{
	FILE *list;
	int  i = 0, n, dev;
	char line[1024];
	char *name, *model, *sn, *cur;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_GETDLIST));
	list = fopen(DEV_LIST_FILE, "r");
	if (!list) {
		/* Failed to open file '%s' (%d) */
		ltfsmsg(LTFS_ERR, "12219E", DEV_LIST_FILE, errno);
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_GETDLIST));
		return i;
	}

	while (fgets(line, sizeof(line), list) != NULL) {
		cur = line;
		cur = strtok(cur, " ");
		if(! cur ) continue;
		name = cur;
		cur += strlen(cur) + 1;
		while(*cur == ' ') cur++;

		cur = strtok(cur, " ");
		if(! cur ) continue;
		model = cur;
		cur += strlen(cur) + 1;
		while(*cur == ' ') cur++;

		cur = strtok(cur, " ");
		if(! cur ) continue;
		sn = cur;

		n = sscanf(name, "%d", &dev);
		if (n == 1) {
			if (buf && i < count) {
				snprintf(buf[i].name, TAPE_DEVNAME_LEN_MAX, "/dev/IBMtape%d", dev);
				snprintf(buf[i].vendor, TAPE_VENDOR_NAME_LEN_MAX, "IBM");
				snprintf(buf[i].model, TAPE_MODEL_NAME_LEN_MAX, "%s", model);
				snprintf(buf[i].serial_number, TAPE_SERIAL_LEN_MAX, "%s", sn);
				snprintf(buf[i].product_name, PRODUCT_NAME_LENGTH, "%s", generate_product_name(model));
			}
			i++;
		}
	}

	fclose(list);

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_GETDLIST));
	return i;
}

/**
 * Set the capacity proprotion of the medium
 * @param device a pointer to the ibmtape backend
 * @param proportion specify the proportion
 * @return 0 on success or a negative value on error
 */
int ibmtape_setcap(void *device, uint16_t proportion)
{
	struct sioc_pass_through spt;
	int rc;
	unsigned char cdb[6];
	unsigned char sense[MAXSENSE];
	char *msg;
	int device_code = ((struct ibmtape_data *) device)->device_code;
	unsigned char buf[TC_MP_MEDIUM_SENSE_SIZE];

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_SETCAP));
	if (device_code == IBM_3592) {
		memset(buf, 0, sizeof(buf));

		/* Scale media instead of setcap */
		rc = ibmtape_modesense(device, TC_MP_MEDIUM_SENSE, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));

		/* Check Cartridge type */
		if (rc == 0 &&
			(buf[2] == TC_MP_JK || buf[2] == TC_MP_JY || buf[2] == TC_MP_JL || buf[2] == TC_MP_JZ)) {
			/* JK & JY cartridge cannot be scaled */
			ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_SETCAP));
			return DEVICE_GOOD;
		}

		buf[0]   = 0x00;
		buf[1]   = 0x00;
		buf[27] |= 0x01;
		buf[28]  = 0x00;

		rc = ibmtape_modeselect(device, buf, sizeof(buf));
	} else {

		memset(&spt, 0, sizeof(spt));
		memset(cdb, 0, sizeof(cdb));
		memset(sense, 0, sizeof(sense));

		/* Prepare Data Buffer */
		spt.buffer_length = 0;
		spt.buffer = NULL;

		/* Prepare CDB */
		spt.cmd_length = sizeof(cdb);
		spt.cdb = cdb;
		spt.cdb[0] = 0x0B;			/* SCSI medium set capacity code */
		spt.cdb[3] = (unsigned char) (proportion >> 8);
		spt.cdb[4] = (unsigned char) (proportion & 0xFF);
		spt.data_direction = SCSI_DATA_NONE;
		spt.timeout = IBM3580TimeOut[FormatTimeOut];

		/* Prepare sense buffer */
		spt.sense_length = sizeof(sense);
		spt.sense = sense;

		/* Invoke _ioctl to send a page */
		rc = sioc_paththrough(device, &spt, &msg);
		if (rc != DEVICE_GOOD)
			ibmtape_process_errors(device, rc, msg, "setcap", true);
	}

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_SETCAP));
	return rc;
}

/**
 * Get EOD status of a partition.
 * @param device Device handle returned by the backend's open().
 * @param part Partition to read the parameter from.
 * @return enum eod_status or UNSUPPORTED_FUNCTION if not supported.
 */
#define LOG_VOL_STATISTICS         (0x17)
#define LOG_VOL_USED_CAPACITY      (0x203)
#define LOG_VOL_PART_HEADER_SIZE   (4)

int ibmtape_get_eod_status(void *device, int part)
{
	/*
	 * This feature requires new tape drive firmware
	 * to support logpage 17h correctly
	 */

	unsigned char logdata[LOGSENSEPAGE];
	unsigned char buf[16] = {0};
	int param_size, rc;
	unsigned int i;
	uint32_t part_cap[2] = {EOD_UNKNOWN, EOD_UNKNOWN};

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_GETEODSTAT));
	/* Issue LogPage 0x17 */
	rc = ibmtape_logsense(device, LOG_VOL_STATISTICS, logdata, LOGSENSEPAGE);
	if (rc) {
		ltfsmsg(LTFS_WARN, "12170W", LOG_VOL_STATISTICS, rc);
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_GETEODSTAT));
		return EOD_UNKNOWN;
	}

	/* Parse Approximate used native capacity of partitions (0x203)*/
	if (parse_logPage(logdata, (uint16_t)LOG_VOL_USED_CAPACITY, &param_size, buf, sizeof(buf))
		|| (param_size != sizeof(buf) ) ) {
		ltfsmsg(LTFS_WARN, "12171W");
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_GETEODSTAT));
		return EOD_UNKNOWN;
	}

	i = 0;
	while (i + LOG_VOL_PART_HEADER_SIZE <= sizeof(buf)) {
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

/**
 * Get vendor unique backend xattr
 * @param device Device handle returned by the backend's open().
 * @param name   Name of xattr
 * @param buf    On success, fill this value with the pointer of data buffer for xattr
 * @return 0 on success or a negative value on error.
 */
int ibmtape_get_xattr(void *device, const char *name, char **buf)
{
	struct ibmtape_data *priv = (struct ibmtape_data *) device;
	unsigned char logdata[LOGSENSEPAGE];
	unsigned char logbuf[16];
	int param_size;
	int rc = -LTFS_NO_XATTR;
	uint32_t value32;
	struct ltfs_timespec now;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_GETXATTR));
	if (! strcmp(name, "ltfs.vendor.IBM.mediaCQsLossRate")) {
		rc = DEVICE_GOOD;

		/* If first fetch or cache value is too old and valuie is dirty, refetch value */
		get_current_timespec(&now);
		if ( priv->fetch_sec_acq_loss_w == 0 ||
			 ((priv->fetch_sec_acq_loss_w + 60 < now.tv_sec) && priv->dirty_acq_loss_w)) {
			rc = ibmtape_logsense_page(device, LOG_PERFORMANCE, LOG_PERFORMANCE_CAPACITY_SUB,
									   logdata, LOGSENSEPAGE);
			if (rc)
				ltfsmsg(LTFS_INFO, "12217I", LOG_PERFORMANCE, rc, "get xattr");
			else {
				if (parse_logPage(logdata, PERF_ACTIVE_CQ_LOSS_W, &param_size, logbuf, 16)) {
					ltfsmsg(LTFS_INFO, "12218I", LOG_PERFORMANCE, "get xattr");
					rc = -LTFS_NO_XATTR;
				} else {
					switch(param_size) {
					case sizeof(uint32_t):
						value32 = (uint32_t)ltfs_betou32(logbuf);
						priv->acq_loss_w = (float)value32 / 65536.0;
						priv->fetch_sec_acq_loss_w = now.tv_sec;
						priv->dirty_acq_loss_w = false;
						break;
					default:
						ltfsmsg(LTFS_INFO, "12191I", param_size);
						rc = -LTFS_NO_XATTR;
						break;
					}
				}
			}
		}

		if(rc == DEVICE_GOOD) {
			/* The buf allocated here shall be freed in xattr_get_virtual() */
			rc = asprintf(buf, "%2.2f", priv->acq_loss_w);
			if (rc < 0) {
				rc = -LTFS_NO_MEMORY;
				ltfsmsg(LTFS_INFO, "12192I", "getting active CQ loss write");
			}
			else
				rc = DEVICE_GOOD;
		} else
			priv->fetch_sec_acq_loss_w = 0;
	}

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_GETXATTR));
	return rc;
}

/**
 * Set vendor unique backend xattr
 * @param device Device handle returned by the backend's open().
 * @param name   Name of xattr
 * @param buf    Data buffer to set the value
 * @param size   Length of data buffer
 * @return 0 on success or a negative value on error.
 */
int ibmtape_set_xattr(void *device, const char *name, const char *buf, size_t size)
{
	struct ibmtape_data *priv = (struct ibmtape_data *) device;
	int rc = -LTFS_NO_XATTR;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_SETXATTR));
	if (! strcmp(name, "ltfs.vendor.IBM.forceErrorWrite")) {
		priv->force_writeperm = strtoull(buf, NULL, 0);
		if (priv->force_writeperm && priv->force_writeperm < THREASHOLD_FORCE_WRITE_NO_WRITE)
			priv->force_writeperm = THREASHOLD_FORCE_WRITE_NO_WRITE;
		rc = DEVICE_GOOD;
	} else if (! strcmp(name, "ltfs.vendor.IBM.forceErrorRead")) {
		priv->force_readperm = strtoull(buf, NULL, 0);
		priv->read_counter = 0;
		rc = DEVICE_GOOD;
	}

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_SETXATTR));
	return rc;
}

void ibmtape_help_message(void)
{
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_HELPMSG));
	ltfsresult("12221I", ibmtape_default_device);
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_HELPMSG));
}

const char *ibmtape_default_device_name(void)
{
	const char *devname;
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_DEFDEVNAME));
	devname = ibmtape_default_device;
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_DEFDEVNAME));
	return devname;
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
	const int rc = ibmtape_modesense(device, TC_MP_READ_WRITE_CTRL, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));

	if (rc != 0) {
		char message[100] = {0};
		sprintf(message, "failed to get MP %02Xh (%d)", TC_MP_READ_WRITE_CTRL, rc);
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
	const int device_code = ((struct ibmtape_data *) device)->device_code;
	if (device_code != IBM_3580) {
		ltfsmsg(LTFS_ERR, "12205E", device_code);
		return -EDEV_INTERNAL_ERROR;
	}

	if (! is_ame(device))
		return -EDEV_INTERNAL_ERROR;

	return DEVICE_GOOD;
}

/**
 * Security protocol out (SPOUT)
 * @param device a pointer to the ibmtape backend
 * @param sps a security protocol specific
 * @param buf pointer to spout. This data will be sent to the drive
 * @param size length of buf
 * @return 0 on success or a negative value on error
 */
int ibmtape_security_protocol_out(void *device, const uint16_t sps, unsigned char *buf, const size_t size)
{
	struct sioc_pass_through spt = {0};
	unsigned char cdb[12] = {0};
	unsigned char sense[MAXSENSE] = {0};
	char *msg = NULL;
	const int device_code = ((struct ibmtape_data *) device)->device_code;

	ltfsmsg(LTFS_DEBUG, "12153D", "Security Protocol Out (SPOUT)", ((struct ibmtape_data *) device)->drive_serial);

	/* Prepare Data Buffer */
	spt.buffer_length = size;
	spt.buffer = buf;

	/* Prepare CDB */
	spt.cmd_length = sizeof(cdb);
	spt.cdb = cdb;
	spt.cdb[0] = 0xB5; /* SCSI SECURITY PROTOCOL OUT */
	spt.cdb[1] = 0x20; /* Tape Data Encryption security protocol */
	ltfs_u16tobe(spt.cdb + 2, sps); /* SECURITY PROTOCOL SPECIFIC */
	ltfs_u32tobe(spt.cdb + 6, spt.buffer_length);
	spt.data_direction = SCSI_DATA_OUT;
	spt.timeout = ComputeTimeOut(device_code, SecurityProtocolOut);

	/* Prepare sense buffer */
	spt.sense_length = sizeof(sense);
	spt.sense = sense;

	/* Invoke _ioctl to modeselect */
	const int rc = sioc_paththrough(device, &spt, &msg);
	if (rc != DEVICE_GOOD) {
		ibmtape_process_errors(device, rc, msg, "security protocol out", true);
	}

	return rc;
}

int ibmtape_set_key(void *device, const unsigned char * const keyalias, const unsigned char * const key)
{
	/*
	 * Encryption  Decryption     Key         DKi      keyalias
	 *    Mode        Mode
	 * 0h Disable  0h Disable  Prohibited  Prohibited    NULL
	 * 2h Encrypt  3h Mixed    Mandatory    Optional    !NULL
	 */

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_SETKEY));

	int rc = is_encryption_capable(device);
	if (rc < 0) {
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_SETKEY));
		return rc;
	}

	const uint16_t sps = 0x10;
	const size_t size = keyalias ? 20 + DK_LENGTH + 4 + DKI_LENGTH : 20;
	unsigned char *buffer = calloc(size, sizeof(unsigned char));

	if (! buffer) {
		rc = -ENOMEM;
		goto out;
	}

	unsigned char buf[TC_MP_READ_WRITE_CTRL_SIZE] = {0};
	rc = ibmtape_modesense(device, TC_MP_READ_WRITE_CTRL, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
	if (rc != DEVICE_GOOD)
		goto free;

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
			rc = -EINVAL;
			goto free;
		}
		memcpy(buffer + 20, key, DK_LENGTH); /* LOGICAL BLOCK ENCRYPTION KEY */
		buffer[20 + DK_LENGTH] = 0x01; /* KEY DESCRIPTOR TYPE: 01h DKi (Data Key Identifier) */
		ltfs_u16tobe(buffer + 20 + DK_LENGTH + 2, DKI_LENGTH);
		memcpy(buffer + 20 + 0x20 + 4, keyalias, DKI_LENGTH);
	}

	const char * const title = "set key:";
	ltfsmsg_keyalias(title, keyalias);

	rc = ibmtape_security_protocol_out(device, sps, buffer, size);
	if (rc != DEVICE_GOOD)
		goto free;

	((struct ibmtape_data *) device)->is_data_key_set = keyalias != NULL;

	memset(buf, 0, sizeof(buf));
	rc = ibmtape_modesense(device, TC_MP_READ_WRITE_CTRL, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
	if (rc != DEVICE_GOOD)
		goto free;

free:
	free(buffer);

out:
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_SETKEY));
	return rc;
}

static void show_hex_dump(const char * const title, const unsigned char * const buf, const uint size)
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

int ibmtape_get_keyalias(void *device, unsigned char **keyalias) /* This is not IBM method but T10 method. */
{
	int i;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_GETKEYALIAS));

	int rc = is_encryption_capable(device);
	if (rc < 0) {
		ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_GETKEYALIAS));
		return rc;
	}

	static const int page_header_length = 4;
	struct sioc_pass_through spt = { .buffer_length = page_header_length };
	const int device_code = ((struct ibmtape_data *) device)->device_code;

	memset(((struct ibmtape_data*) device)->dki, 0, sizeof(((struct ibmtape_data*) device)->dki));
	*keyalias = NULL;

	/*
	 * 1st loop: Get the page length.
	 * 2nd loop: Get full data in the page.
	 */
	for (i = 0; i < 2; ++i) {
		/* Prepare Data Buffer */
		free(spt.buffer);
		spt.buffer = (unsigned char*) calloc(spt.buffer_length, sizeof(unsigned char));
		if (spt.buffer == NULL) {
			ltfsmsg(LTFS_ERR, "10001E", "ibmtape_get_keyalias: data buffer");
			ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_GETKEYALIAS));
			return -EDEV_NO_MEMORY;
		}

		/* Prepare CDB */
		unsigned char cdb[12] = {0};
		spt.cmd_length = sizeof(cdb);
		spt.cdb = cdb;
		spt.cdb[0] = 0xA2; /* operation code: SECURITY PROTOCOL IN */
		spt.cdb[1] = 0x20; /* security protocol */
		spt.cdb[3] = 0x21; /* security protocol specific: Next Block Encryption Status page */
		ltfs_u32tobe(spt.cdb + 6, spt.buffer_length); /* allocation length */
		spt.data_direction = SCSI_DATA_IN;
		spt.timeout = ComputeTimeOut(device_code, SecurityProtocolIn);

		/* Prepare sense buffer */
		unsigned char sense[MAXSENSE] = {0};
		spt.sense_length = sizeof(sense);
		spt.sense = sense;

		/* Invoke _ioctl to get key-alias */
		char *msg = NULL;
		rc = sioc_paththrough(device, &spt, &msg);

		if (rc != DEVICE_GOOD) {
			ibmtape_process_errors(device, rc, msg, "get key-alias", true);
			free(spt.buffer);
			ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_GETKEYALIAS));
			return rc;
		}

		show_hex_dump("SPIN:", spt.buffer, spt.buffer_length);

		spt.buffer_length = page_header_length + ltfs_betou16(spt.buffer + 2);
	}

	const unsigned char encryption_status = spt.buffer[12] & 0xF;
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
		while (offset + 4 <= spt.buffer_length && spt.buffer[offset] != 1) {
			offset += ltfs_betou16(spt.buffer + offset + 2) + 4;
		}
		if (offset + 4 <= spt.buffer_length && spt.buffer[offset] == 1) {
			const uint dki_length = ((int) spt.buffer[offset + 2]) << 8 | spt.buffer[offset + 3];
			if (offset + 4 + dki_length <= spt.buffer_length) {
				int n = dki_length < sizeof(((struct ibmtape_data*) device)->dki) ? dki_length :
					sizeof(((struct ibmtape_data*) device)->dki);
				memcpy(((struct ibmtape_data*) device)->dki, &spt.buffer[offset + 4], n);
				*keyalias = ((struct ibmtape_data*) device)->dki;
			}
		}
	}

	const char * const title = "get key-alias:";
	ltfsmsg_keyalias(title, ((struct ibmtape_data*) device)->dki);

	free(spt.buffer);
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_GETKEYALIAS));
	return rc;
}

#define TC_MP_INIT_EXT_LBP_RS         (0x40)
#define TC_MP_INIT_EXT_LBP_CRC32C     (0x20)

int ibmtape_set_lbp(void *device, bool enable)
{
	struct ibmtape_data *priv = (struct ibmtape_data *) device;
	struct logical_block_protection lbp;
	char *msg;
	unsigned char lbp_method = LBP_DISABLE;
	unsigned char buf[TC_MP_INIT_EXT_SIZE];
	int rc = DEVICE_GOOD;

	/* Check logical block protection capability */
	rc = ibmtape_modesense(device, TC_MP_INIT_EXT, TC_MP_PC_CURRENT, 0x00, buf, sizeof(buf));
	if (rc < 0)
		return rc;

	if (buf[0x12] & TC_MP_INIT_EXT_LBP_CRC32C)
		lbp_method = CRC32C_CRC;
	else
		lbp_method = REED_SOLOMON_CRC;

	/* set logical block protection */
	ltfsmsg(LTFS_DEBUG, "12156D", "LBP Enable", enable, "");
	ltfsmsg(LTFS_DEBUG, "12156D", "LBP Method", lbp_method, "");
	memset(&lbp, 0, sizeof(struct logical_block_protection));
	rc = _sioc_stioc_command(device, STIOC_QUERY_BLK_PROTECTION, "GET LBP", &lbp, &msg);
	if (rc != DEVICE_GOOD) {
		ibmtape_process_errors(device, rc, msg, "get lbp", true);
		return rc;
	}

	if (enable && lbp.lbp_capable) {
		lbp.lbp_method      = lbp_method;
		lbp.lbp_info_length = 4;
		lbp.lbp_w           = 1;
		lbp.lbp_r           = 1;
	} else
		lbp.lbp_method = LBP_DISABLE;

	rc = _sioc_stioc_command(device, STIOC_SET_BLK_PROTECTION, "SET LBP", &lbp, &msg);
	if (rc != DEVICE_GOOD) {
		ibmtape_process_errors(device, rc, msg, "set lbp", true);
		return rc;
	}

	if (enable && lbp.lbp_capable) {
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
	{ DRIVE_GEN_JAG5, 0x54 },
	{ DRIVE_GEN_JAG5, 0x55 },
};
static int num_jaguar_drive_density = sizeof(jaguar_drive_density) / sizeof(DRIVE_DENSITY_SUPPORT_MAP);

static DRIVE_DENSITY_SUPPORT_MAP jaguar_drive_density_strict[] = {
		// Jaguar 4 drive supports only density code 54h medium
	{ DRIVE_GEN_JAG4, 0x54 },
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

bool ibmtape_is_mountable(void *device, const char *barcode, const unsigned char density_code)
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
	struct ibmtape_data *priv = (struct ibmtape_data *) device;
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
				!strncmp(barcode+6, "JK", 2) ||
				!strncmp(barcode+6, "JY", 2)) {
				// assume density code 54h
				dcode = 0x54;
				}
			else if (!strncmp(barcode+6, "JD", 2) ||
					 !strncmp(barcode+6, "JL", 2) ||
					 !strncmp(barcode+6, "JZ", 2)) {
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
						/* J5 drive does not support JB cartridge */
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
		ltfsmsg(LTFS_DEBUG, "12212D", priv->drive_serial, barcode, density_code);
	} else {
		ltfsmsg(LTFS_DEBUG, "12213D", priv->drive_serial, barcode, density_code);
	}
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_ISMOUNTABLE));
	return supported;
}

/* This function should be called after the cartridge is loaded. */
int ibmtape_get_worm_status(void *device, bool *is_worm)
{
	int rc = 0;
	struct ibmtape_data *priv = ((struct ibmtape_data *) device);

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_GETWORMSTAT));
	if (priv->loaded) {
		*is_worm = priv->is_worm;
	}
	else {
		ltfsmsg(LTFS_INFO, "12227I");
		*is_worm = false;
		rc = -1;
	}

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_GETWORMSTAT));
	return rc;
}

struct tape_ops ibmtape_drive_handler = {
	.open                   = ibmtape_open,
	.reopen                 = ibmtape_reopen,
	.close                  = ibmtape_close,
	.close_raw              = ibmtape_close_raw,
	.is_connected           = ibmtape_is_connected,
	.inquiry                = ibmtape_inquiry,
	.inquiry_page           = ibmtape_inquiry_page,
	.test_unit_ready        = ibmtape_test_unit_ready,
	.read                   = ibmtape_read,
	.write                  = ibmtape_write,
	.writefm                = ibmtape_writefm,
	.rewind                 = ibmtape_rewind,
	.locate                 = ibmtape_locate,
	.space                  = ibmtape_space,
	.erase                  = ibmtape_erase,
	.load                   = ibmtape_load,
	.unload                 = ibmtape_unload,
	.readpos                = ibmtape_readpos,
	.setcap                 = ibmtape_setcap,
	.format                 = ibmtape_format,
	.remaining_capacity     = ibmtape_remaining_capacity,
	.logsense               = ibmtape_logsense,
	.modesense              = ibmtape_modesense,
	.modeselect             = ibmtape_modeselect,
	.reserve_unit           = ibmtape_reserve_unit,
	.release_unit           = ibmtape_release_unit,
	.prevent_medium_removal = ibmtape_prevent_medium_removal,
	.allow_medium_removal   = ibmtape_allow_medium_removal,
	.write_attribute        = ibmtape_write_attribute,
	.read_attribute         = ibmtape_read_attribute,
	.allow_overwrite        = ibmtape_allow_overwrite,
	.report_density         = ibmtape_report_density,
	// May be command combination
	.set_compression        = ibmtape_set_compression,
	.set_default            = ibmtape_set_default,
	.get_cartridge_health   = ibmtape_get_cartridge_health,
	.get_tape_alert         = ibmtape_get_tape_alert,
	.clear_tape_alert       = ibmtape_clear_tape_alert,
	.get_xattr              = ibmtape_get_xattr,
	.set_xattr              = ibmtape_set_xattr,
	.get_parameters         = ibmtape_get_parameters,
	.get_eod_status         = ibmtape_get_eod_status,
	.get_device_list        = ibmtape_get_device_list,
	.help_message           = ibmtape_help_message,
	.parse_opts             = ibmtape_parse_opts,
	.default_device_name    = ibmtape_default_device_name,
	.set_key                = ibmtape_set_key,
	.get_keyalias           = ibmtape_get_keyalias,
	.takedump_drive         = ibmtape_takedump_drive,
	.is_mountable           = ibmtape_is_mountable,
	.get_worm_status        = ibmtape_get_worm_status,
};

struct tape_ops *tape_dev_get_ops(void)
{
        /* Initialize profile related parameters */
        int ret = 0;
        bend_profiler = NULL;
        ret = ltfs_mutex_init(&bend_profiler_lock);
        if (ret) {
                /* Cannot initialize mutex */
                ltfsmsg(LTFS_ERR, "10002E", ret);
                return NULL;
        }
	return &ibmtape_drive_handler;
}

extern char driver_linux_ibmtape_dat[];

const char *tape_dev_get_message_bundle_name(void **message_data)
{
	*message_data = driver_linux_ibmtape_dat;
	return "driver_linux_ibmtape";
}

#undef __ibmtape_tc_c
