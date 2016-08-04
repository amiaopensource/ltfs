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
** FILE NAME:       tape_drivers/linux/ibmtape/ibmtape_cmn.h
**
** DESCRIPTION:     Prototypes for common IBM tape operations.
**
** AUTHOR:          Atsushi Abe
**                  IBM Yamato, Japan
**                  PISTE@jp.ibm.com
**
*************************************************************************************
*/

#ifndef __ibmtape_cmn_h
#define __ibmtape_cmn_h

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/mtio.h>
#include <sys/ioctl.h>
#include "libltfs/tape_ops.h"
#include "libltfs/ltfs_error.h"
#include "libltfs/ltfs_endian.h"
#include "tape_drivers/tape_drivers.h"
#include "IBM_tape.h"
#include "ibmtape_timeout.h"
#include "libltfs/ltfstrace.h"

/**
 *  Definitions
 */
#define DMP_DIR "/tmp"

#define DEVICE(i, c, t, n) &(struct supported_device){ i, c, t, n }

/**
 *  Supported device structures
 */
struct supported_device {
	char product_id[PRODUCT_ID_LENGTH + 1];     /**< product id */
	int  device_code;                           /**< device code defined by lin_tape driver */
	DRIVE_TYPE drive_type;                      /**< drive type defined by ltfs */
	char product_name[PRODUCT_NAME_LENGTH + 1]; /**< product name */
};

extern struct supported_device *supported_devices[];

/**
 *  Status definitions of lower scsi handling code
 *  This statuses are exposed into SIOC pass through interface
 */
enum scsi_status {
	SCSI_GOOD                 = 0x00,
	SCSI_CHECK_CONDITION      = 0x01,
	SCSI_CONDITION_GOOD       = 0x02,
	SCSI_BUSY                 = 0x04,
	SCSI_INTERMEDIATE_GOOD    = 0x08,
	SCSI_INTERMEDIATE_C_GOOD  = 0x0a,
	SCSI_RESERVATION_CONFRICT = 0x0c,
};

enum host_status {
	HOST_GOOD       = 0x00,
	HOST_NO_CONNECT = 0x01,
	HOST_BUS_BUSY   = 0x02,
	HOST_TIME_OUT   = 0x03,
	HOST_BAD_TARGET = 0x04,
	HOST_ABORT      = 0x05,
	HOST_PARITY     = 0x06,
	HOST_ERROR      = 0x07,
	HOST_RESET      = 0x08,
	HOST_BAD_INTR   = 0x09,
};

enum driver_status {
	DRIVER_GOOD     = 0x00,
	DRIVER_BUSY     = 0x01,
	DRIVER_SOFT     = 0x02,
	DRIVER_MEDIA    = 0x03,
	DRIVER_ERROR    = 0x04,
	DRIVER_INVALID  = 0x05,
	DRIVER_TIMEOUT  = 0x06,
	DRIVER_HARD     = 0x07,
	DRIVER_SENSE    = 0x08,
	SUGGEST_RETRY   = 0x10,
	SUGGEST_ABORT   = 0x20,
	SUGGEST_REMAP   = 0x30,
	SUGGEST_DIE     = 0x40,
	SUGGEST_SENSE   = 0x80,
	SUGGEST_IS_OK   = 0xff,
};

/**
 * ibmtape private data structure. Shared by ibmtape_tc.c and ibmtape_cc.c
 */
struct itd_conversion_entry {
	uint16_t src_asc_ascq; /**< ASC/ASCQ receive from device */
	uint16_t dst_asc_ascq; /**< ASC/ASCQ converted */
};

enum _device_type {
	DEVICE_TAPE,
	DEVICE_CHANGER,
};

struct ibmtape_data {
	int           fd;                /**< file descriptor of the device */
	bool          loaded;            /**< Is cartridge loaded? */
	bool          loadfailed;        /**< Is load/unload failed? */
	unsigned char drive_serial[255]; /**< serial number of device */
	int           type;              /**< device type */
	int           device_code;       /**< device code */
	int           itd_command_size;  /**< ITD sense conversion table size for commands */
	struct itd_conversion_entry *itd_command; /**< ITD sense conversion table for commands */
	int           itd_slot_size;     /**< ITD sense conversion table size for RES data */
	struct itd_conversion_entry *itd_slot;    /**< ITD sense conversion table for RES data */
	long          fetch_sec_acq_loss_w; /**< Sec to fetch Active CQs loss write */
	bool          dirty_acq_loss_w;  /**< Is Active CQs loss write dirty */
	float         acq_loss_w;        /**< Active CQs loss write */
	uint64_t      tape_alert;        /**< Latched tape alert flag */
	bool          is_data_key_set;   /**< Is a valid data key set? */
	unsigned char dki[12];           /**< key-alias */
	DRIVE_TYPE    drive_type;        /**< drive type defined by ltfs */
	uint64_t      force_writeperm;   /**< pseudo write perm threshold */
	uint64_t      force_readperm;    /**< pseudo read perm threashold */
	uint64_t      write_counter;     /**< write call counter for pseudo write perm */
	uint64_t      read_counter;      /**< read call counter for pseudo write perm */
	char          *devname;          /**< device name */
	bool          is_worm;           /**< Is worm cartridge loaded? */
	crc_enc       f_crc_enc;         /**< Pointer to CRC encode function */
	crc_check     f_crc_check;       /**< Pointer to CRC encode function */
};

struct ibmtape_global_data {
	unsigned          disable_auto_dump; /**< Is auto dump disabled? */
	char              *str_crc_checking; /**< option string for crc_checking */
	unsigned          crc_checking;      /**< Is crc checking enabled? */
	unsigned          strict_drive;      /**< Is bar code length checked strictly? */
};

/**
 *  Macros
 */
#if 0
#define _ioctl(fd,x...) (  \
		fprintf(stderr, "[%s::%d]<@%s> Performing ioctl(%d, " ##x ")", __FILE__, __LINE__, __FUNCTION__, fd), \
		ioctl(fd, x)													\
        )
#else
#define _ioctl(fd,x...) (  \
		ioctl(fd, x)	   \
        )
#endif

/**
 *  Sense code <-> Error code conversion table
 */
struct error_table {
	uint32_t sense;    /**< sense data */
	int      err_code; /**< error code */
	char     *msg;     /**< desciption of error */
};

#define MASK_WITH_SENSE_KEY    (0xFFFFFF)
#define MASK_WITHOUT_SENSE_KEY (0x00FFFF)

extern struct error_table standard_table[];    /* Standard SCSI sense table */
extern struct error_table ibmtape_tc_errors[]; /* IBM LTO tape drive vendor unique sense table */
extern struct error_table ibmtape_cc_errors[]; /* IBM changer vendor unique sense table (includes ITD) */

/*
 *  Function Prototypes
 */
extern int ibmtape_ioctlrc2err(void *device, int fd, struct request_sense *sense, char **msg);
extern int _sioc_stioc_command(void *device, int cmd, char *cmd_name, void *param, char **msg);
extern int ibmtape_check_lin_tape_version(void);
extern int ibmtape_inquiry(void *device, struct tc_inq *inq);
extern int _ibmtape_inquiry_page(void *device, unsigned char page, struct tc_inq_page *inq, bool error_handle);
extern int ibmtape_inquiry_page(void *device, unsigned char page, struct tc_inq_page *inq);
extern int ibmtape_test_unit_ready(void *device);
extern int ibmtape_reserve_unit(void *device);
extern int ibmtape_release_unit(void *device);

extern int ibmtape_readbuffer(void *device, int id, unsigned char *buf, size_t offset, size_t len,
							  int type);
extern int ibmtape_takedump_drive(void *device);

static inline int _sioc_paththrough(void *device, struct sioc_pass_through *spt);
static inline int _sioc_sense2errno(struct sioc_pass_through *spt, struct error_table *table, char **msg);
static inline int sioc_paththrough(void *device, struct sioc_pass_through *spt, char **msg);

/**
 * Get Dump
 * @param device a pointer to the ibmtape backend
 * @return none
 */
static inline void ibmtape_get_dump(void *device)
{
	ibmtape_takedump_drive(device);
	return;
}

/**
 * Convert sense code to ITD sense code
 * @param sense sense code packed to uint32_t
 * @param table pointer to itd conversion table
 * @param int the size of itd conversion table
 * @return LTFS internal error code
 */
static inline uint32_t ibmtape_conv_itd(uint32_t sense, struct itd_conversion_entry *table, int size)
{
	uint32_t ret = sense;
	uint16_t src = sense & 0xFFFF;
	int i;

	for(i = 0 ; i < size; i++) {
		if( src == table[i].src_asc_ascq )
			ret = (sense & 0xFF0000) + table[i].dst_asc_ascq;
	}

	return ret;
}

/**
 * Get sense from sioc_pass_through interface
 * @param device a pointer to the ibmtape backend
 * @param spt_org pointer to sioc_pass_through structure
 * @return 0 on success or return code of _sioc_pass_through
 */
static inline int _sioc_get_sense(void *device, struct sioc_pass_through *spt_org)
{
	struct sioc_pass_through spt;
	int ret, err;
	unsigned char cdb[6];
	unsigned char buffer[MAXSENSE];
	unsigned char sense[MAXSENSE];
	int device_code = ((struct ibmtape_data *) device)->device_code;

	memset(&spt, 0, sizeof(spt));
	memset(cdb, 0, sizeof(cdb));
	memset(sense, 0, sizeof(sense));

	// Prepare Data Buffer
	spt.buffer_length = sizeof(buffer);
	spt.buffer = buffer;

	// Prepare CDB
	spt.cmd_length = sizeof(cdb);
	spt.cdb = cdb;
	spt.cdb[0] = 0x03;			/* 0x03 SCSI request sense code */
	spt.cdb[4] = sizeof(buffer);	/* allocation length */
	spt.data_direction = SCSI_DATA_IN;
	/* Set timeout as as same as read position (Assume to return immidiately) */
	spt.timeout = ComputeTimeOut(device_code, ReadPositionTimeOut);

	// Prepare sense buffer
	spt.sense_length = sizeof(sense);
	spt.sense = sense;

	/* Invoke _ioctl to get sense data */
	ret = _sioc_paththrough(device, &spt);
	if (ret == 0 && (spt.buffer[0] & 0x80) ) {
		spt_org->sense_length = spt.buffer[7] + 7;
		memcpy(spt_org->sense, spt.buffer, spt_org->sense_length);
	} else {
		err = errno;
		ltfsmsg(LTFS_INFO, "12099I", err);
	}

	return ret;
}

static inline int _ibmtape_get_sense(void *device, struct sioc_pass_through *spt_org)
{
	int fd = *((int *) device);
	struct request_sense sense_data;
	int rc, i;

	memset(&sense_data, 0, sizeof(struct request_sense));
	rc = _ioctl(fd, SIOC_REQSENSE, &sense_data);
	if (rc == 0) {
		spt_org->sense_length = sense_data.addlen + 7;
		spt_org->sense[0]  = (sense_data.valid << 7) | (sense_data.err_code & 0x7F);
		spt_org->sense[1]  = sense_data.segnum;
		spt_org->sense[2]  = (sense_data.fm << 7) | (sense_data.eom << 6) | (sense_data.ili << 5) |
			                 (sense_data.resvd1 << 4) | (sense_data.key & 0x0F);
		spt_org->sense[3]  = (sense_data.info >> 24) & 0xFF;
		spt_org->sense[4]  = (sense_data.info >> 16) & 0xFF;
		spt_org->sense[5]  = (sense_data.info >> 8) & 0xFF;
		spt_org->sense[6]  = (sense_data.info & 0xFF);
		spt_org->sense[7]  = sense_data.addlen;
		spt_org->sense[8]  = (sense_data.cmdinfo >> 24) & 0xFF;
		spt_org->sense[9]  = (sense_data.cmdinfo >> 16) & 0xFF;
		spt_org->sense[10] = (sense_data.cmdinfo >> 8) & 0xFF;
		spt_org->sense[11] = (sense_data.cmdinfo & 0xFF);
		spt_org->sense[12] = sense_data.asc;
		spt_org->sense[13] = sense_data.ascq;
		spt_org->sense[14] = sense_data.fru;
		spt_org->sense[15] = (sense_data.sksv << 7) | (sense_data.cd << 6) | (sense_data.resvd2 << 4) |
			                 (sense_data.bpv << 3) | (sense_data.sim & 0x07);
		spt_org->sense[16] = sense_data.field[0];
		spt_org->sense[17] = sense_data.field[1];

		for(i = 0; i < 109; ++i)
			spt_org->sense[18 + i] = sense_data.vendor[i];
	}
	else {
		rc = -EDEV_INTERNAL_ERROR;
		ltfsmsg(LTFS_INFO, "12099I", rc);
	}

	return rc;
}

/**
 * Issue SCSI command through sioc_pass_through interface
 * @param device a pointer to the ibmtape backend
 * @param spt pointer to sioc_pass_through structure
 * @return 0 on success, -1 on command error with sense
 *         -2 on command error without sense or -3 on error of ioctl
 */
static inline int _sioc_paththrough(void *device, struct sioc_pass_through *spt)
{
	int fd = ((struct ibmtape_data *) device)->fd;
	int ret;

	ret = _ioctl(fd, SIOC_PASS_THROUGH, spt);
	if (ret == -1) {
		ltfsmsg(LTFS_INFO, "12100I", errno, ((struct ibmtape_data *) device)->drive_serial);
		ret = -3;
	}

	/* check returned status */
	if (spt->target_status || spt->msg_status || spt->host_status || spt->driver_status) {
		if (!spt->sense_length) {
			ltfsmsg(LTFS_DEBUG, "12104D", spt->target_status,
					spt->msg_status, spt->host_status, spt->driver_status,
					((struct ibmtape_data *) device)->drive_serial);
			if(spt->driver_status & (DRIVER_SENSE | SUGGEST_SENSE))
				_ibmtape_get_sense(device, spt);
		}

		if (spt->sense_length) {
			ltfsmsg(LTFS_DEBUG, "12105D", spt->sense[2] & 0x0F, spt->sense[12], spt->sense[13]);
			ltfsmsg(LTFS_DEBUG, "12106D", spt->sense[45], spt->sense[46], spt->sense[47], spt->sense[48],
					((struct ibmtape_data *) device)->drive_serial);
			ret = -1;
		} else {
			ltfsmsg(LTFS_INFO, "12107I");
			ltfsmsg(LTFS_INFO, "12108I", spt->target_status, spt->msg_status,
					spt->host_status, spt->driver_status, ((struct ibmtape_data *) device)->drive_serial);
			if(spt->target_status)
				ret = EDEV_TARGET_ERROR;
			else if (spt->host_status)
				ret = EDEV_HOST_ERROR;
			else if (spt->driver_status)
				ret = EDEV_DRIVER_ERROR;
			else
				ret = -2;
		}
	} else if(ret == -3 && errno == EIO &&
			(spt->cdb[0] == 0x0A || spt->cdb[0] == 0x10)) { /* EIO against write and writefm command */
		ltfsmsg(LTFS_DEBUG, "12104D", spt->target_status,
				spt->msg_status, spt->host_status, spt->driver_status,
				((struct ibmtape_data *) device)->drive_serial);

		/*
		 *  When the issued command is write or writefm command and hit
		 *  early warning condition, lin_tape doesn't return correct sense.
		 *  (Return 00/0000 with EIO in errno) So always need to call
		 *  _sioc_get_sense() here.
		 */
		_ibmtape_get_sense(device, spt);

		if (spt->sense_length) {
			ltfsmsg(LTFS_DEBUG, "12105D", spt->sense[2] & 0x0F, spt->sense[12], spt->sense[13]);
			ltfsmsg(LTFS_DEBUG, "12106D", spt->sense[45], spt->sense[46], spt->sense[47], spt->sense[48],
					((struct ibmtape_data *) device)->drive_serial);
			ret = -1;
		} else {
			ltfsmsg(LTFS_INFO, "12107I");
			ltfsmsg(LTFS_INFO, "12108I", spt->target_status, spt->msg_status,
					spt->host_status, spt->driver_status, ((struct ibmtape_data *) device)->drive_serial);
			if(spt->target_status)
				ret = EDEV_TARGET_ERROR;
			else if (spt->host_status)
				ret = EDEV_HOST_ERROR;
			else if (spt->driver_status)
				ret = EDEV_DRIVER_ERROR;
			else
				ret = -2;
		}
	}

	return ret;
}

/**
 * Convert sense information to negative errno
 * @param spt pointer to sioc_pass_through
 * @return negative errno
 */
static inline int _sense2errcode(uint32_t sense, struct error_table *table, char **msg, uint32_t mask)
{
	int rc = -EDEV_UNKNOWN;
	int i;

	if (msg)
		*msg = NULL;

	if ( (sense & 0xFFFF00) == 0x044000 )
		sense = 0x044000;
	else if ( (sense & 0xFFF000) == 0x048000 ) /* 04/8xxx in TS3100/TS3200 */
		sense = 0x048000;
	else if ( (sense & 0xFFF000) == 0x0B4100 ) /* 0B/41xx in TS2900 */
		sense = 0x0B4100;

	if ( (sense & 0x00FF00) >= 0x008000 || (sense & 0x0000FF) >= 0x000080)
		rc = -EDEV_VENDOR_UNIQUE;

	i = 0;
	while (table[i].sense != 0xFFFFFF) {
		if((table[i].sense & mask) == (sense & mask)) {
			rc  = table[i].err_code;
			if (msg)
				*msg = table[i].msg;
			break;
		}
		i++;
	}

	if (table[i].err_code == -EDEV_RECOVERED_ERROR)
		rc = DEVICE_GOOD;
	else if (table[i].sense == 0xFFFFFF && table[i].err_code == rc && msg)
		*msg = table[i].msg;

	return rc;
}

/**
 * Convert sense information to negative errno
 * @param spt pointer to sioc_pass_through
 * @return negative errno
 */
static inline int _sioc_sense2errno(struct sioc_pass_through *spt, struct error_table *table, char **msg)
{
	int rc = -EDEV_UNKNOWN;
	uint32_t sense = 0;

	sense += (uint32_t) (spt->sense[2] & 0x0F) << 16;
	sense += (uint32_t) spt->sense[12] << 8;
	sense += (uint32_t) spt->sense[13];

	rc = _sense2errcode(sense, standard_table, msg, MASK_WITH_SENSE_KEY);
	if (rc == -EDEV_VENDOR_UNIQUE)
		rc = _sense2errcode(sense, table, msg, MASK_WITH_SENSE_KEY);

	return rc;
}

/**
 * Issue SCSI command through sioc_pass_through interface
 * @param device a pointer to the ibmtape backend
 * @param spt pointer to sioc_pass_through structure
 * @param msg pointer to the description of error (on success NULL)
 * @return internal error code defined in ltfs_error.h (Only prefix DEVICE_)
 */
static inline int sioc_paththrough(void *device, struct sioc_pass_through *spt, char **msg)
{
	struct ibmtape_data *priv = NULL;
	int sioc_rc, rc;
	uint32_t s = 0, s_itd;

	sioc_rc = _sioc_paththrough(device, spt);
	if (sioc_rc == 0){
		if (msg)
			*msg = "Command successed";
		rc = DEVICE_GOOD;
	} else if (sioc_rc == -1 && spt->sense_length) {
		priv = (struct ibmtape_data *) device;
		if (priv->type == DEVICE_CHANGER) {
			/* Convert sense code to ITD sense code when the device type is the changer */
			s += (uint32_t) (spt->sense[2] & 0x0F) << 16;
			s += (uint32_t) spt->sense[12] << 8;
			s += (uint32_t) spt->sense[13];
			s_itd = ibmtape_conv_itd(s, priv->itd_command, priv->itd_command_size);
			spt->sense[2]  = (unsigned char)((s_itd >> 16) & 0x0F);
			spt->sense[12] = (unsigned char)((s_itd >> 8) & 0xFF);
			spt->sense[13] = (unsigned char)(s_itd & 0xFF);

			rc = _sioc_sense2errno(spt, ibmtape_cc_errors, msg);
		} else
			rc = _sioc_sense2errno(spt, ibmtape_tc_errors, msg);
	} else if (sioc_rc == -1) {
		/* Program Error (unexpected condition) */
		if (msg)
			*msg = "Program Error (Unexpected condition)";
		rc = -EDEV_INTERNAL_ERROR;
	} else if (sioc_rc == -2) {
		/* Cannot get sense info */
		if (msg)
			*msg = "Cannot get sense information";
		rc = -EDEV_CANNOT_GET_SENSE;
	} else if (sioc_rc == -3) {
		/* Error occures within ioctl */
		if (msg)
			*msg = "Driver error";
		rc = -EDEV_DRIVER_ERROR;
	} else {
		/* Program Error (unexpected condition) */
		if (msg)
			*msg = "Program Error (Unexpected return code)";
		rc  = -EDEV_INTERNAL_ERROR;
	}

	return rc;
}

static inline bool is_dump_required_error(void *device, int ret)
{
	int rc, err = -ret;
	bool ans = FALSE;
	char *msg;
	struct log_sense10_page log_page;

	if (err == EDEV_NO_SENSE || err == EDEV_OVERRUN) {
		/* Sense Key 0 situation. */
		/* Drive may not exist or may not be able to transfer any data. */
		/* Checking capability of data transfer by logsense. */
		log_page.page_code = 0x17;	// volume status
		log_page.subpage_code = 0;
		log_page.len = 0;
		log_page.parm_pointer = 0;
		memset(log_page.data, 0, LOGSENSEPAGE);

		rc = _sioc_stioc_command(device, SIOC_LOG_SENSE10_PAGE, "LOGSENSE", &log_page, &msg);

		ans = (rc == DEVICE_GOOD);
	}
	else if (err >= EDEV_NOT_READY && err < EDEV_INTERNAL_ERROR) {
		ans = TRUE;
	}

	return ans;
}

extern struct ibmtape_global_data global_data;

static inline void ibmtape_process_errors(void *device, int rc, char *msg, char *cmd, bool take_dump)
{
	struct ibmtape_data *priv = device;

	if(msg != NULL)
		ltfsmsg(LTFS_INFO, "12173I", cmd, msg, rc, priv->drive_serial);
	else
		ltfsmsg(LTFS_ERR, "12174E", cmd, rc, priv->drive_serial);

	if (device) {
		priv = (struct ibmtape_data *) device;
		if ( priv->type == DEVICE_TAPE &&
			 take_dump &&
			 !global_data.disable_auto_dump &&
			 is_dump_required_error(device, rc) )
			ibmtape_get_dump(device);
	}
}

#endif // __ibmtape_cmn_h
