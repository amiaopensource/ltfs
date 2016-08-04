/************************************************************************************
**
**  Hewlett Packard LTFS backend for LTO and DAT tape drives
**
** FILE:            ltotape_platform.c
**
** CONTENTS:        Platform-specific portion of ltotape LTFS backend for Microsoft Windows
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
**   17 August 2010
**
*************************************************************************************
**
** Copyright (C) 2012 OSR Open Systems Resources, Inc.
** 
************************************************************************************* 
*/

#define __ltotape_platform_c
#include "ltotape_diag.h"
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "ltfsprintf.h"

#include <windef.h>
#include <winbase.h>

#include "ntddscsi.h" /* MUST include this before ltotape.h because that includes IBMtape.h */
                      /*  which defines SCSI_PASS_THROUGH as an ioctl..                     */
#include "winioctl.h"


#include "ltotape.h"
#include "ltotape_supdevs.h"
#include "../../../libltfs/ltfs_error.h"

/*
 * Include Non-standard FUSE APIs for device communication
 */
#include <FUSE4Win_nonstd.h>


/*
 * Structure to contain the SCSI passthrough storage:
 */
#define SPT_SENSE_BUFFER_LENGTH 64
typedef struct {
   SCSI_PASS_THROUGH_DIRECT spt;
   ULONG                    pad;
   UCHAR                    senseBuffer [SPT_SENSE_BUFFER_LENGTH];
} SPTTfrType;

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
const char *ltotape_default_device = "TAPE0";

/*****************************************************************************
 * Utility function to generate a hex representation of some data.
 *  Caller must free returned pointer after use.
 *****************************************************************************/
char *ltotape_printbytes (unsigned char *data, int num_bytes)
{
  int i, len;
  char* pStr;

  pStr = (char*) calloc (num_bytes*4, sizeof(char));
  if (pStr == (char*)NULL) {
     ltfsmsg(LTFS_ERR, "10001E", "ltotape_printbytes: temp string data");
     return (NULL);

  } else {
     for (i = 0, len = 0; i < num_bytes; i++, len += 3) {
        sprintf (pStr+len, "%2.2X ", *(data+i));
     }
     return (pStr);
  }
}

/*F***************************************************************************
*
* Function Name:         ltotape_scsiexec
*
?* Function Prototype:    int ltotape_scsiexec (ltotape_scsi_io_type *scsi_io)
*
* Set up and execute the SCSI command indicated by scsi_io
*
******************************************************************************
*
* Function Return:   -1 if the command failed for any reason
*            0 if successful
*                    >0 if a read or write was successful (# bytes transferred)
*
****************************************************************************F*/
int ltotape_scsiexec (ltotape_scsi_io_type *scsi_io)
{
  int                 retval;
  int                 scsi_status;
  int                 driver_status;
  char               *pString;
  int                 i;
  BOOL                status;
  HANDLE              h;
  DWORD               syserr;
  DWORD               tfrlen;
  SPTTfrType          sptio;

/*
 * Set up some default "it didn't work" values:
 */
  driver_status = DS_ILLEGAL;
  scsi_status = S_NO_STATUS;
  retval = 0;

  h = (HANDLE)scsi_io->fd;

/*
 * To start with, clear the whole structure and request sense buffer:
 */
  ZeroMemory (&sptio, sizeof(SPTTfrType));

/*
 * Now start filling in the various structure members:
 */
  sptio.spt.Length             = sizeof(SCSI_PASS_THROUGH_DIRECT);
  sptio.spt.CdbLength          = scsi_io->cdb_length;
  sptio.spt.SenseInfoLength    = SPT_SENSE_BUFFER_LENGTH;
  sptio.spt.DataTransferLength = scsi_io->data_length;

  if (scsi_io->data_direction == HOST_READ) {
    sptio.spt.DataIn = SCSI_IOCTL_DATA_IN;
  } else if (scsi_io->data_direction == HOST_WRITE) {
    sptio.spt.DataIn = SCSI_IOCTL_DATA_OUT;
  } else {
    sptio.spt.DataIn = SCSI_IOCTL_DATA_UNSPECIFIED;
  }

  sptio.spt.TimeOutValue    = (scsi_io->timeout_ms / 1000);  /* convert ms to s */
  sptio.spt.DataBuffer      = (PVOID) scsi_io->data;
  sptio.spt.SenseInfoOffset = offsetof(SPTTfrType, senseBuffer);

  for (i = 0; i < scsi_io->cdb_length; i++) {
    sptio.spt.Cdb[i] = scsi_io->cdb[i];
  }

  pString = ltotape_printbytes (scsi_io->cdb, scsi_io->cdb_length);
  ltfsmsg(LTFS_DEBUG, "20010D", pString, scsi_io->data_length);
  if (pString != (char*)NULL) {
    free (pString);
  }

/*
 * Attempt to execute the command:
 */
  if (h == NULL) {

    /*
     * If the handle is NULL, we're in the mounted file system case
     * and must go through FUSE for device communication
     */
    status = fuse_media_ioctl(IOCTL_SCSI_PASS_THROUGH_DIRECT,
                              &sptio,
                              sizeof(SPTTfrType),
                              &sptio,
                              sizeof(SPTTfrType));

  } else {

   /*
    * We're running outside of the file system and must use Win32
    */
    status = DeviceIoControl(h,
                             IOCTL_SCSI_PASS_THROUGH_DIRECT,
                             &sptio,
                             sizeof(SPTTfrType),
                             &sptio,
                             sizeof(SPTTfrType),
                             &tfrlen,
                             NULL);
  }
    
  if (status == 0) {    
    syserr = GetLastError();

    ltfsmsg(LTFS_ERR, "20023E", "DeviceIOControl", syserr);
    scsi_io->actual_data_length = 0;

    if (syserr == ERROR_NOT_READY) {        // value = 21
      driver_status = DS_SELECTION_TIMEOUT;

    } else if (syserr == ERROR_IO_DEVICE) { // value = 1117
      driver_status = DS_TIMEOUT;

    } else {
      driver_status = DS_ILLEGAL;
    }

/*
 * Otherwise, the command was at least sent and completed, so let's see how it got on:
 */
  } else {
    driver_status = DS_GOOD;
    scsi_status   = sptio.spt.ScsiStatus;
    scsi_io->actual_data_length = sptio.spt.DataTransferLength;

    if (sptio.spt.ScsiStatus == S_CHECK_CONDITION) {
      scsi_io->sense_length = sptio.spt.SenseInfoLength;
      memcpy ((void*)scsi_io->sensedata, (void*)sptio.senseBuffer, scsi_io->sense_length);
    }
  }

/*
 * A driver error is always bad news:
 */
  if (driver_status != DS_GOOD) {
    retval = -1;

/*
 * A SCSI error is bad, UNLESS:
 *  a) we were doing a read AND the only problem was an ILI condition.. OR
 *  b) we were doing a write/writeFM AND the only problem was EWEOM..
 * in which case all was well really!
 *
 * Note that "real" EOM has sense key 0xD (VOLUME OVERFLOW); EWEOM has sense key 0x0 (NO SENSE).
 * For early warning we pretend all was well but make a note to report it on the NEXT write
 * For Real EOM, we must report EIO because there is physically no more space on tape
 */
  } else if (scsi_status != S_GOOD) {

      if (scsi_status == S_CHECK_CONDITION) {
         scsi_io->sense_length = scsi_io->sensedata[7] + 8;
     
     if ((scsi_io->cdb[0] == CMDread) && (SENSE_HAS_ILI_SET(scsi_io->sensedata))) {
            int resid = ((int)scsi_io->sensedata[3] << 24) + 
                ((int)scsi_io->sensedata[4] << 16) +
                    ((int)scsi_io->sensedata[5] <<  8) + 
            ((int)scsi_io->sensedata[6]      );
            scsi_io->actual_data_length = scsi_io->data_length - resid;
            retval = scsi_io->actual_data_length;

     } else if (((scsi_io->cdb[0] == CMDwrite) || (scsi_io->cdb[0] == CMDwrite_filemarks))  &&
    		 (SENSE_IS_EARLY_WARNING_EOM(scsi_io->sensedata) || SENSE_IS_EARLY_WARNING_PEOM(scsi_io->sensedata))) {
         scsi_io->actual_data_length = scsi_io->data_length;
         retval = scsi_io->actual_data_length;

         if (scsi_io->eweomstate == before_eweom) {
        scsi_io->eweomstate = report_eweom; /* Already written the data, so set flag to report next time */
         }

     } else if (((scsi_io->cdb[0] == CMDwrite) || (scsi_io->cdb[0] == CMDwrite_filemarks))  &&
            (SENSE_IS_END_OF_MEDIA(scsi_io->sensedata))) {
             scsi_io->actual_data_length = 0;
         retval = -1;
         errno = EIO;

     } else if (((scsi_io->cdb[0] == CMDmode_select10) && (scsi_io->family == drivefamily_lto))
				&& (SENSE_IS_MODE_PARAMETER_ROUNDED(scsi_io->sensedata))) {
    	 retval = -EDEV_MODE_PARAMETER_ROUNDED;

     }else if ((scsi_io->family == drivefamily_lto)&&
    		 (SENSE_IS_MEDIA_NOT_LOGICALLY_LOADED(scsi_io->sensedata))) {
    	 scsi_io->sensedata[12] = 0x3A;
    	 scsi_io->sensedata[13] = 0x00;
    	 retval = -1;
     }else {
         retval = -1;
     }

     } else {
        retval = -1;  /* Not GOOD and not CHECK CONDITION = BAD */
     }

/*
 * For successful read/write commands, return transferred length:
 */
  } else if ((scsi_io->cdb[0] == CMDread) || (scsi_io->cdb[0] == CMDwrite)) {
    retval = scsi_io->actual_data_length;

/*
 * For everything else, return 0:
 */
  } else {
    retval = 0;
  }

  ltfsmsg(LTFS_DEBUG, "20011D", driver_status, scsi_status, scsi_io->actual_data_length);
  if (scsi_status == S_CHECK_CONDITION) {
    pString = ltotape_printbytes (scsi_io->sensedata, scsi_io->sense_length);
    ltfsmsg(LTFS_DEBUG, "20012D", pString);
    if (pString != (char*)NULL) {
      free (pString);
    }
  }

  return (retval);
}

/**
 * Open HP tape backend.
 * TODO: should be returning an integer instead of setting errno
 * @param devname device name of the LTO tape drive (e.g. TAPE0)
 * @return a pointer to the ltotape backend on success or NULL on error
 */
int ltotape_open(const char *devname, void **handle)
{
	int						ret = 0, i = 0;
	unsigned char			modepage[TC_MP_MEDIUM_PARTITION_SIZE];
	unsigned char			snvpdpage[32];
	DWORD					returned;
	HANDLE					tape_handle;
	char					tapedevice[24];
	struct tc_inq			inq_data;
	ltotape_scsi_io_type	*device = NULL;

	CHECK_ARG_NULL(handle, -LTFS_NULL_ARG);
	*handle = NULL;

	device = (ltotape_scsi_io_type *) calloc(1, sizeof(ltotape_scsi_io_type));
	if (device == (ltotape_scsi_io_type *) NULL) {
		ltfsmsg(LTFS_ERR, "10001E", "ltotape_open: device private data");
		return -EDEV_NO_MEMORY;
	}
	device->fd = NULL;

	/* If fuse_get_media_device returns NULL, it means that we're not running this code as part
	 * of a mounted volume instance. Thus, we'll use the Win32 API for communication. If the value
	 * is non-NULL, we have exclusive access to the volume by way of being the mounted file system
	 * instance.
	 */
	if (fuse_get_media_device() == NULL) {
		sprintf(tapedevice, "\\\\.\\%s", devname);

		/* Open with sharing specified. We'll lock explicitly via FSCTL_LOCK_VOLUME */
		tape_handle = CreateFile(tapedevice,
				GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE,
				NULL,
				OPEN_EXISTING,
				0,
				NULL);

		if (tape_handle == INVALID_HANDLE_VALUE) {
			ltfsmsg(LTFS_ERR, "20029E");
			free(device); device = NULL;
			*handle = NULL;
			return -EDEV_DEVICE_UNOPENABLE;
		}
		device->fd = (void *) tape_handle;

		/* Lock the volume */
		if (! DeviceIoControl(tape_handle,
				FSCTL_LOCK_VOLUME,
				NULL,
				0,
				NULL,
				0,
				&returned,
				NULL)) {
			ltfsmsg(LTFS_ERR, "20030E", GetLastError());
			free(device); device = NULL;
			CloseHandle(tape_handle);
			*handle = NULL;
			return -EDEV_DEVICE_BUSY;
		}
	}

	/* Default timeout, should be overwritten by each backend function: */
	device->timeout_ms = LTO_DEFAULT_TIMEOUT;

	/* Default Early Warning EOM state is that we're not yet at the warning point: */
	device->eweomstate = before_eweom;

	/* Default logfile directory - initially NULL; will get set if/when we parse FUSE options. */
	device->logdir = NULL;

	/* Find out what we're dealing with: */
	ret = ltotape_inquiry(device, &inq_data);
	if (ret) {
		ltfsmsg(LTFS_ERR, "20083E", ret);
		free(device); device = NULL;
		CloseHandle(tape_handle);
		*handle = NULL;
		return ret;
	} else {
		device->family = drivefamily_unknown;
		device->type = drive_unknown;
		memset(device->serialno, 0, sizeof(device->serialno));
		memset(snvpdpage, 0, sizeof(snvpdpage));
		i = 0;

		ltfsmsg(LTFS_DEBUG, "20084D", inq_data.pid);

		while (supported_devices[i].product_family != drivefamily_unknown) {
			if ((strncmp((char *) inq_data.pid, (char *) supported_devices[i].product_id,
					strlen((char *) supported_devices[i].product_id)) == 0)) {
				device->family = supported_devices[i].product_family;
				device->type = supported_devices[i].drive_type;

				if (ltotape_evpd_inquiry(device, VPD_PAGE_SERIAL_NUMBER, snvpdpage,
						sizeof(snvpdpage)) < 0) {
					strcpy(device->serialno, "Unknown");
				} else {
					strncpy(device->serialno, (const char*) (snvpdpage + 4), (size_t)snvpdpage[3]);
				}

				ltfsmsg(LTFS_INFO, "20013I", supported_devices[i].description, device->serialno);
				break;
			}
			i++;
		}

		if (device->family == drivefamily_unknown) {
			ltfsmsg(LTFS_ERR, "20085E", inq_data.pid);
			free(device); device = NULL;
			CloseHandle(tape_handle);
			*handle = NULL;
			return -EDEV_DEVICE_UNSUPPORTABLE;
		}
	}

	/* For an LTO drive, need to determine whether it is partition-capable or only
	 * partition-aware:
	 */
	if (device->family == drivefamily_lto) {
		ret = ltotape_test_unit_ready(device);

		if (SENSE_IS_UNIT_ATTENTION(device->sensedata)) {
			ret = ltotape_test_unit_ready(device);
		}

		ret = ltotape_modesense(device, TC_MP_MEDIUM_PARTITION,
				TC_MP_PC_CHANGEABLE, 0, modepage, sizeof(modepage));
		if (ret < 0) {
			free(device); device = NULL;
			CloseHandle(tape_handle);
			*handle = NULL;
			return ret;
		} else if ((modepage[PARTTYPES_OFFSET] & PARTTYPES_MASK) != PARTTYPES_MASK) {
			ltfsmsg(LTFS_ERR, "20014E", inq_data.revision);
			free(device); device = NULL;
			CloseHandle(tape_handle);
			*handle = NULL;
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
 * Close HP tape backend
 * @param device a pointer to the ltotape backend
 * @return 0 on success or a negative value on error
 */
int ltotape_close(void *device)
{
	int						status = DEVICE_GOOD;
	DWORD					returned;
	struct tc_position		pos;
	ltotape_scsi_io_type	*sio = (ltotape_scsi_io_type*)device;

	if (device != NULL) {
		ltotape_rewind(device, &pos);

		if (sio->fd != NULL) {
			/* Dismount the FSD */
			if (! DeviceIoControl((HANDLE)sio->fd,
					FSCTL_DISMOUNT_VOLUME,
					NULL,
					0,
					NULL,
					0,
					&returned,
					NULL)) {
				ltfsmsg(LTFS_ERR, "20023E", "DeviceIoControl", GetLastError());
			}

			CloseHandle((HANDLE) sio->fd);
			sio->fd = NULL;
		}
		free(device);
		device = NULL;
	}

	return status;
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

	CloseHandle((HANDLE)sio->fd);
	sio->fd = NULL;

	return DEVICE_GOOD;
}

#undef __ltotape_platform_c
