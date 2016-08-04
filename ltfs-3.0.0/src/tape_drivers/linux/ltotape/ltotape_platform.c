/************************************************************************************
**
**  Hewlett Packard LTFS backend for LTO and DAT tape drives
**
** FILE:            ltotape_platform.c
**
** CONTENTS:        Platform-specific portion of ltotape LTFS backend for Linux
**
** (C) Copyright 2015 Hewlett Packard Enterprise Development LP.
**
** This program is free software; you can redistribute it and/or modify it
**  under the terms of version 2.1 of the GNU Lesser General Public License
**  as published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful, but 
**  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
**  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
**  License for more details.
**
** You should have received a copy of the GNU General Public License along
**  with this program; if not, write to:
**    Free Software Foundation, Inc.
**    51 Franklin Street, Fifth Floor
**    Boston, MA 02110-1301, USA.
**
**   26 April 2010
**
*************************************************************************************
*/

#define __ltotape_platform_c

#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <scsi/sg.h>
#include <scsi/scsi.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include "../../../libltfs/ltfs_error.h"
#include "../../../ltfsprintf.h"
#include "../../../libltfs/ltfslogging.h"
#include "ltotape.h"
#include "ltotape_diag.h"
#include "ltotape_supdevs.h"

/*
 * Max transfer size to ask the SG driver to handle (1MB):
 */
#define REQUESTED_MAX_SG_LENGTH   1048576

/*
 * Platform-specific implementation functions contained in this file:
 */
int ltotape_open(const char *devname, void **handle);
int ltotape_reopen(const char *devname, void *handle);
int ltotape_close(void *device);
int ltotape_scsiexec (ltotape_scsi_io_type *scsi_io);

/*
 * Backend functions used below in ltotape_open:
 */
extern int ltotape_inquiry(void *device, struct tc_inq *inq);
extern int ltotape_modesense(void *device, const uint8_t page, const TC_MP_PC_TYPE pc, const uint8_t subpage, unsigned char *buf, const size_t size);
extern int ltotape_evpd_inquiry(void *device, int vpdpage, unsigned char* idata, int ilen);
extern int ltotape_rewind(void *device, struct tc_position *pos);
extern int ltotape_test_unit_ready (void *device);

/*
 * Default tape device
 */
const char *ltotape_default_device = "/dev/nst0";

/*****************************************************************************
 * Utility function to generate a hex representation of some data.
 *  Caller must free returned pointer after use.
 *****************************************************************************/
char *ltotape_printbytes(unsigned char *data, int num_bytes)
{
	int		i = 0, len = 0;
	char	*print_string = NULL;

	print_string = (char*) calloc(num_bytes * 4, sizeof(char));
	if (print_string == (char *) NULL) {
		ltfsmsg(LTFS_ERR, "10001E", "ltotape_printbytes: temp string data");
		return NULL;

	} else {
		for (i = 0, len = 0; i < num_bytes; i++, len += 3) {
			sprintf(print_string + len, "%2.2X ", *(data + i));
		}
		return print_string;
	}
}

/**
 * Set up and execute the SCSI command indicated by scsi_io.
 *
 * @param scsi_io Will contain the cdb which will indicate the command to be executed.
 * @return int -1 on failure, 0 on success, >0 (# of bytes transferred) if a read/write command is
 * successful.
 */
int ltotape_scsiexec(ltotape_scsi_io_type *scsi_io)
{
	int			status = 0, scsi_status = 0, driver_status = 0;
	char		*sense_string = NULL;
	sg_io_hdr_t	sg_io;

	memset((void *) &sg_io, 0, sizeof(sg_io));

	/* Set up required fields */
	sg_io.interface_id		= (int) 'S';
	sg_io.timeout			= (unsigned int) scsi_io->timeout_ms;
	sg_io.flags				= SG_FLAG_LUN_INHIBIT;

	sg_io.cmd_len			= (unsigned char) scsi_io->cdb_length;
	sg_io.cmdp				= scsi_io->cdb;

	sg_io.mx_sb_len			= sizeof(scsi_io->sensedata);
	sg_io.sbp				= scsi_io->sensedata;

	sg_io.dxfer_len			= scsi_io->data_length;
	sg_io.dxferp			= (void*) scsi_io->data;
	sg_io.dxfer_direction	=
			(scsi_io->data_direction == HOST_READ) ? SG_DXFER_FROM_DEV :
			(scsi_io->data_direction == HOST_WRITE) ?
					SG_DXFER_TO_DEV : SG_DXFER_NONE;

	sense_string = ltotape_printbytes(scsi_io->cdb, scsi_io->cdb_length);
	ltfsmsg(LTFS_DEBUG, "20010D", sense_string, scsi_io->data_length);
	if (sense_string != (char *) NULL)
		free(sense_string);

	/* Here's the actual command execution */
	status = ioctl(scsi_io->fd, SG_IO, &sg_io);

	/* Now determine the outcome: */
	scsi_status = S_NO_STATUS; /* Until proven otherwise... */

	/* The command requested was not accepted by the driver */
	if ((status < 0) || ((sg_io.driver_status & 0xF) == SG_ERR_DRIVER_INVALID)) {
		driver_status = DS_ILLEGAL;

		/* Unit didn't respond to selection */
	} else if (sg_io.host_status == SG_ERR_DID_NO_CONNECT) {
		driver_status = DS_SELECTION_TIMEOUT;

		/* Unit timed out */
	} else if (sg_io.host_status == SG_ERR_DID_TIME_OUT) {
		driver_status = DS_TIMEOUT;
		errno = ETIMEDOUT;

		/* Bus (link, whatever) was reset */
	} else if (sg_io.host_status == SG_ERR_DID_RESET) {
		driver_status = DS_RESET;
		if (errno == 0) errno = EIO; /* ensure it doesn't go unnoticed */

		/* Good driver status */
	} else if (sg_io.host_status == SG_ERR_DID_OK) {
		driver_status = DS_GOOD;
		scsi_status = sg_io.status;

		/* For anything else, create a composite value of "driver status" to help with debug */
	} else
		driver_status = (DS_FAILED << 16) | (sg_io.host_status & 0xFF)<<8 | (sg_io.driver_status & 0xFF);

	scsi_io->actual_data_length	= sg_io.dxfer_len - sg_io.resid;
	scsi_io->sense_length		= sg_io.sb_len_wr;

	/* A driver error is always bad news */
	if (driver_status != DS_GOOD) {
		status = -1;

		ltfsmsg(LTFS_DEBUG, "20089D", "errno", errno);
		ltfsmsg(LTFS_DEBUG, "20089D", "host_status", sg_io.host_status);
		ltfsmsg(LTFS_DEBUG, "20089D", "driver_status", sg_io.driver_status);
		ltfsmsg(LTFS_DEBUG, "20089D", "status", sg_io.status);
		ltfsmsg(LTFS_DEBUG, "20089D", "resid", sg_io.resid);
		ltfsmsg(LTFS_DEBUG, "20089D", "duration", sg_io.duration);
		ltfsmsg(LTFS_DEBUG, "20089D", "info", sg_io.info);
		ltfsmsg(LTFS_DEBUG, "20089D", "sb_len_wr", sg_io.sb_len_wr);
		ltfsmsg(LTFS_DEBUG, "20089D", "msg_status", sg_io.msg_status);

		/*
		 * A SCSI error is bad, UNLESS:
		 * a) we were doing a read AND the only problem was an ILI condition.. OR
		 * b) we were doing a write/writeFM AND the only problem was EWEOM..
		 * in which case all was well really!
		 *
		 * Note that "real" EOM has sense key 0xD (VOLUME OVERFLOW); EWEOM has sense key 0x0 (NO SENSE).
		 * For early warning we pretend all was well but make a note to report it on the NEXT write
		 * For Real EOM, we must report EIO because there is physically no more space on tape
		 */
	} else if (scsi_status != S_GOOD) {
		if (scsi_status == S_CHECK_CONDITION) {
			if ((scsi_io->cdb[0] == CMDread) && (SENSE_HAS_ILI_SET(scsi_io->sensedata))) {
				int resid = ((int)scsi_io->sensedata[3] << 24) +
						((int)scsi_io->sensedata[4] << 16) +
						((int)scsi_io->sensedata[5] <<  8) +
						((int)scsi_io->sensedata[6]      );
				scsi_io->actual_data_length = scsi_io->data_length - resid;
				status = scsi_io->actual_data_length;
			} else if (((scsi_io->cdb[0] == CMDwrite) || (scsi_io->cdb[0] == CMDwrite_filemarks))
					&& (SENSE_IS_EARLY_WARNING_EOM(scsi_io->sensedata) || SENSE_IS_EARLY_WARNING_PEOM(scsi_io->sensedata))) {
				scsi_io->actual_data_length = scsi_io->data_length;
				status = scsi_io->actual_data_length;

				if (scsi_io->eweomstate == before_eweom)
					scsi_io->eweomstate = report_eweom; /* Already written the data, so set flag to report next time */
			} else if (((scsi_io->cdb[0] == CMDwrite) || (scsi_io->cdb[0] == CMDwrite_filemarks))
					&& (SENSE_IS_END_OF_MEDIA(scsi_io->sensedata))) {
				scsi_io->actual_data_length = 0;
				status = -1;
				errno = EIO;
			} else if (((scsi_io->cdb[0] == CMDmode_select10) && (scsi_io->family == drivefamily_lto))
					&& (SENSE_IS_MODE_PARAMETER_ROUNDED(scsi_io->sensedata))) {
				status = -EDEV_MODE_PARAMETER_ROUNDED;
			} else if ((scsi_io->family == drivefamily_lto)&&
		    		 (SENSE_IS_MEDIA_NOT_LOGICALLY_LOADED(scsi_io->sensedata))) {
		    	scsi_io->sensedata[12] = 0x3A;
		    	scsi_io->sensedata[13] = 0x00;
		    	status = -1;
		    }else
				status = -1;
		} else
			status = -1;  /* Not GOOD and not CHECK CONDITION = BAD */

		/* For successful read/write commands, return transferred length */
	} else if ((scsi_io->cdb[0] == CMDread) || (scsi_io->cdb[0] == CMDwrite))
		status = scsi_io->actual_data_length;
		/* For everything else, return 0 */
	else
		status = 0;

	ltfsmsg(LTFS_DEBUG, "20011D", driver_status, scsi_status, scsi_io->actual_data_length);
	if (scsi_status == S_CHECK_CONDITION) {
		sense_string = ltotape_printbytes(scsi_io->sensedata, scsi_io->sense_length);
		ltfsmsg(LTFS_DEBUG, "20012D", sense_string);
		if (sense_string)
			free(sense_string); sense_string = NULL;
	}

	return status;
}

/**
 * Map st device to corresponding sg device.
 * On some Linux distro's the st driver doesn't seem to reliably allow
 * passthrough writes and reads of > 256kB... So try to find the
 * sg device that points to the same tape drive. Uses an ioctl with local
 * struct defn as described at
 * http://tldp.org/HOWTO/SCSI-Generic-HOWTO/scsi_g_idlun.html
 * 
 * @param devname Original device name provided by user
 * @param sgdevname Device name to be used by LTFS
 * @return 0 on success or a negative value on error
 */
int ltotape_map_st2sg(const char *devname, char *sgdevname)
{
	bool	found = FALSE, allocated = FALSE;
	int		fd = 0, outcome = 0, i = 0, length = 0;
	int		dev_host_no = 0, dev_channel = 0, dev_lun = 0, dev_scsi_id = 0;
	int		sg_hba = 0, sg_chan = 0, sg_addr = 0, sg_lun = 0, sg_devtype = 0;
	char	*nstdevname = NULL, *temp = NULL;
	FILE	*fp = NULL;

	struct sidl {
		int four_in_one;
		int host_unique_id;
	} devinfo;

	/* First check for a valid device name */
	if (devname == (const char *) NULL) {
		ltfsmsg(LTFS_ERR, "20032E", devname, "Null device name");
		return -EDEV_INVALID_ARG;
		/* If the given device is already a 'sg' device, nothing to be done. */
	} else if (strstr(devname, "/dev/sg") != (char*) NULL) {
		strcpy(sgdevname, devname);
		return DEVICE_GOOD;
		/* If the given device is a 'st' device, change that to a 'nst' device to ensure that
		 * upon 'close' the driver doesn't issue a rewind command to the tape.
		 */
	} else if (strstr(devname, "/dev/st") != (char *) NULL) {
		length = strlen(devname) + 2; /* +1 for 'n' and +1 for '\0' */
		asprintf(&temp, "%s", devname + strlen("/dev/st"));
		if (! temp) {
			/* Low on memory. Return failure. */
			ltfsmsg(LTFS_ERR, "20100E");

			sgdevname = NULL;
			return -EDEV_NO_MEMORY;
		}

		nstdevname = (char *) calloc(1, length);
		if (! nstdevname) {
			/* Low on memory. Return failure. */
			ltfsmsg(LTFS_ERR, "20100E");

			if (temp)
				free(temp); temp = NULL;

			sgdevname = NULL;
			return -EDEV_NO_MEMORY;
		}
		strncat(nstdevname, "/dev/nst", strlen("/dev/nst"));
		strncat(nstdevname, temp, strlen(temp));
		allocated = TRUE;

		ltfsmsg(LTFS_DEBUG, "20101D", devname, nstdevname);

		if (temp)
			free(temp); temp = NULL;
	} else {
		/* Given device name is already a 'nst' device or if 'something else' (/dev/something) is
		 * passed by the user, 'open' should handle that below. */
		nstdevname = (char *) devname;
	}

	ltfsmsg(LTFS_DEBUG, "20031D", nstdevname);

	if ((fd = open (nstdevname, O_RDWR | O_NDELAY))< 0) {
		ltfsmsg(LTFS_ERR, "20032E", devname, "Unable to open (check permissions)");

		/* Cleanup. */
		if (allocated)
			free(nstdevname); nstdevname = NULL;

		return -EDEV_DEVICE_UNOPENABLE;
	} else {
		outcome = ioctl(fd, SCSI_IOCTL_GET_IDLUN, &devinfo);
		close(fd);

		if (outcome < 0) {
			ltfsmsg(LTFS_ERR, "20032E", devname, "SCSI_IOCTL_GET_IDLUN failed");

			/* Cleanup. */
			if (allocated)
				free(nstdevname); nstdevname = NULL;

			return -EDEV_DRIVER_ERROR;
		} else if ((fp = fopen("/proc/scsi/sg/devices", "rb")) == (FILE *) NULL) {
			ltfsmsg(LTFS_ERR, "20032E", devname, "Unable to open /proc/scsi/sg/devices");

			/* Cleanup. */
			if (allocated)
				free(nstdevname); nstdevname = NULL;

			return -EDEV_INTERNAL_ERROR;
		} else {
			i = 0;
			dev_scsi_id = (devinfo.four_in_one        & 0xFF);
			dev_lun     = (devinfo.four_in_one >> 8)  & 0xFF;
			dev_channel = (devinfo.four_in_one >> 16) & 0xFF;
			dev_host_no = (devinfo.four_in_one >> 24) & 0xFF;
			found = FALSE;
			while (fscanf (fp, "%d %d %d %d %d %*d %*d %*d %*d",
					&sg_hba, &sg_chan, &sg_addr, &sg_lun, &sg_devtype) == 5) {
				if ((sg_hba     == dev_host_no) &&
						(sg_chan    == dev_channel) &&
						(sg_addr    == dev_scsi_id) &&
						(sg_lun     == dev_lun)) {
					found = TRUE;
					break;
				} else
					i++;
			}

			fclose (fp);

			if (! found) {
				/* Cleanup. */
				if (allocated)
					free(nstdevname); nstdevname = NULL;

				return -EDEV_DEVICE_UNSUPPORTABLE;
			}
			else {
				sprintf (sgdevname, "/dev/sg%d", i);
				ltfsmsg(LTFS_DEBUG, "20034D", nstdevname, sgdevname, dev_host_no,
						dev_channel, dev_scsi_id, dev_lun);

				/* Cleanup. */
				if (allocated)
					free(nstdevname); nstdevname = NULL;

				return DEVICE_GOOD;
			}
		}
	}
}

/**
 * Open LTO tape backend.
 *
 * @param devname Device name of the LTO tape driver.
 * @param handle A pointer to the ltotape backend on success or NULL on error.
 * @return int DEVICE_GOOD on success else a -ve value on error.
 */
int ltotape_open(const char *devname, void **handle)
{
	unsigned char			modepage[TC_MP_MEDIUM_PARTITION_SIZE];
	unsigned char			snvpdpage[32];
	char					sgdevname[24];
	int						ret = DEVICE_GOOD, i =0, res_sz = 0;
	struct tc_inq			inq_data;
	ltotape_scsi_io_type	*device = NULL;

	CHECK_ARG_NULL(handle, -LTFS_NULL_ARG);
	*handle = NULL;

	memset(&inq_data, 0, sizeof(struct tc_inq));

	/* We use the SCSI generic driver for ioctls. Here we attempt to map a given (if) 'st' device
	 * to a corresponding 'sg' device. */
	ret = ltotape_map_st2sg(devname, sgdevname);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "20033E", devname);
		return ret;
	}

	device = (ltotape_scsi_io_type *) calloc(1, sizeof(ltotape_scsi_io_type));
	if (device == (ltotape_scsi_io_type *) NULL) {
		/* Memory allocation failed. Return failure. */
		ltfsmsg(LTFS_ERR, "20100E");
		return -EDEV_NO_MEMORY;
	}

	/* Open the device. */
	device->fd = open(sgdevname, O_RDWR | O_NDELAY);
	if (device->fd < 0) {
		device->fd = open(sgdevname, O_RDONLY | O_NDELAY);
		if (device->fd < 0) {
			if (errno == EAGAIN) {
				ltfsmsg(LTFS_ERR, "20086E", devname);
				ret = -EDEV_DEVICE_BUSY;
			} else {
				ltfsmsg(LTFS_ERR, "20087E", devname, errno);
				ret = -EDEV_DEVICE_UNOPENABLE;
			}
			free(device); device = NULL;
			return ret;
		}
		ltfsmsg(LTFS_WARN, "20088W", devname);
	}

	/* Lock the opened device. */
	if (flock(device->fd, LOCK_EX | LOCK_NB) != 0) {
		ltfsmsg(LTFS_ERR, "20035E", strerror(errno));
		close(device->fd);
		free(device);
		return -EDEV_DEVICE_BUSY;
	}

	/*
	 * Try to ensure the driver is set up for larger transfer sizes..
	 */
	res_sz = REQUESTED_MAX_SG_LENGTH;
	ioctl (device->fd, SG_SET_RESERVED_SIZE, &res_sz);
	ioctl (device->fd, SG_GET_RESERVED_SIZE, &res_sz);
	ltfsmsg(LTFS_DEBUG, "20020D", res_sz);

	/* Default timeout, should be overwritten by each backend function: */
	device->timeout_ms = LTO_DEFAULT_TIMEOUT;
	/* Default Early Warning EOM state is that we're not yet at the warning point:
	 */
	device->eweomstate = before_eweom;
	/* Default logfile directory - initially NULL; will get set if/when we
	 * parse FUSE options. */
	device->logdir = NULL;

	/* Find out what we're dealing with. */
	ret = ltotape_inquiry (device, &inq_data);
	if (ret) {
		ltfsmsg(LTFS_ERR, "20083E", ret);
		close (device->fd);
		free(device);
		return ret;
	} else {
		device->family = drivefamily_unknown;
		device->type = drive_unknown;
		memset (device->serialno,  0, sizeof(device->serialno));
		memset (snvpdpage, 0, sizeof(snvpdpage));
		i = 0;

		ltfsmsg(LTFS_DEBUG, "20084D", inq_data.pid);

		while (supported_devices[i].product_family != drivefamily_unknown) {
			if ((strncmp((char *)inq_data.pid, (char *)supported_devices[i].product_id,
					strlen((char *)supported_devices[i].product_id)) == 0)) {
				device->family = supported_devices[i].product_family;
				device->type = supported_devices[i].drive_type;

				if (ltotape_evpd_inquiry(device, VPD_PAGE_SERIAL_NUMBER, snvpdpage, sizeof(snvpdpage)) < 0) {
					strcpy (device->serialno, "Unknown");
				} else {
					strncpy (device->serialno, (const char*)(snvpdpage+4), (size_t)snvpdpage[3]);
				}

				ltfsmsg(LTFS_INFO, "20013I", supported_devices[i].description, device->serialno);
				break;
			}
			i++;
		}

		if(device->family == drivefamily_unknown) {
			ltfsmsg(LTFS_ERR, "20085E", inq_data.pid);
			close (device->fd);
			free(device);
			device = NULL;
			return -EDEV_DEVICE_UNSUPPORTABLE;
		}
	}

#ifdef QUANTUM_BUILD
	/*
	 * Store the drive vendor
	 */
	if ( strncmp((char *)inq_data.vid, "HP      ", 8 ) == 0 ) {
		device->drive_vendor_id = drivevendor_hp;
	}
	else if ( strncmp((char *)inq_data.vid, "QUANTUM ", 8 ) == 0 ) {
		device->drive_vendor_id = drivevendor_quantum;
	}
	else {
		device->drive_vendor_id = drivevendor_unknown;
	}
#endif

	/*
	 * For an LTO drive, need to determine whether it is partition-capable or only partition-aware:
	 */
	if (device->family == drivefamily_lto) {
		ret = ltotape_test_unit_ready (device);

		if (SENSE_IS_UNIT_ATTENTION(device->sensedata)) {
			ret = ltotape_test_unit_ready (device);
		}

		ret = ltotape_modesense (device, TC_MP_MEDIUM_PARTITION, TC_MP_PC_CHANGEABLE, 0, modepage, sizeof(modepage));
		if (ret < 0) {
			close (device->fd);
			free (device); /* no need for ltfsmsg here since modesense will have done it already */
			return ret;

		} else if ((modepage [PARTTYPES_OFFSET] & PARTTYPES_MASK) != PARTTYPES_MASK) {
			ltfsmsg(LTFS_ERR, "20014E", inq_data.revision);
			close (device->fd);
			free (device);
			return ret;
		}
	}

	*handle = (void *) device;
	return DEVICE_GOOD;
}

/**
 * Reopen a device. If reopen is not needed, do nothing in this call.
 *
 * @param devname The name of the device to be opened.
 * @param handle Device handle returned by the backend's open()
 * @return DEVICE_GOOD on success, else a negative value indicating the status
 * is returned.
 */
int ltotape_reopen(const char *devname, void *handle)
{
	int		ret = DEVICE_GOOD;

	return ret;
}

/**
 * Close a previously opened device and clear the ltotape backend.
 *
 * @param device Pointer to the ltotape backend.
 * @return 0 on sucess, a negative value on error.
 */
int ltotape_close(void *device)
{
	struct tc_position		pos;
	ltotape_scsi_io_type	*sio = (ltotape_scsi_io_type *) device;

	CHECK_ARG_NULL(sio, -EDEV_INVALID_ARG);

	ltotape_rewind(sio, &pos);
	close (sio->fd);
	free(sio);

	return 0;
}

/**
 * Close only the device file descriptor.
 *
 * @param device The ltotape backend handle.
 * @return 0 on success, a negative value on error.
 */
int ltotape_close_raw(void *device)
{
	ltotape_scsi_io_type	*sio = (ltotape_scsi_io_type *) device;

	CHECK_ARG_NULL(sio, -EDEV_INVALID_ARG);

	close(sio->fd);
	sio->fd = -1;

	return DEVICE_GOOD;
}

#undef __ltotape_platform_c
