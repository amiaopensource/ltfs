/************************************************************************************
**
**  Hewlett Packard LTFS backend for LTO and DAT tape drives
**
** FILE:            ltotape.c
**
** CONTENTS:        Platform-specific portion of ltotape LTFS backend for Mac OS X
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

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOTypes.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/scsi/SCSITaskLib.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include "../../../libltfs/ltfs_error.h"
#include "../../../ltfsprintf.h"
#include "../../../libltfs/ltfslogging.h"
#include "ltotape.h"
#include "ltotape_diag.h"
#include "ltotape_supdevs.h"
#include <syslog.h>

/*
 * Platform-specific implementation functions contained in this file:
 */
int ltotape_open(const char *devname, void **handle);
int ltotape_close(void *device);
int ltotape_scsiexec (ltotape_scsi_io_type *scsi_io);

int ltotape_release(void *device);
int ltotape_close_raw(void *device);
int ltotape_reopen(const char *devname, void *handle);

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
const char *ltotape_default_device = "0";

/*****************************************************************************
 * Utility function to generate a hex representation of some data.
 *  Caller must free returned pointer after use.
 *****************************************************************************/
char *ltotape_printbytes(
				unsigned char *data, int num_bytes)
{
	int i, len;
	char* pStr;

	pStr = (char*) calloc(num_bytes * 4, sizeof(char));
	if (pStr == (char*) NULL) {
		ltfsmsg(LTFS_ERR, "10001E", "ltotape_printbytes: temp string data");
		return (NULL);
	} else {
		for (i = 0, len = 0; i < num_bytes; i++, len += 3) {
			sprintf(pStr + len, "%2.2X ", *(data + i));
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
*		     0 if successful
*                    >0 if a read or write was successful (# bytes transferred)
*
****************************************************************************F*/
int ltotape_scsiexec (ltotape_scsi_io_type *scsi_io)
{
	int retval;
	SCSITaskStatus taskStatus;
	SCSI_Sense_Data senseData;
	SCSIServiceResponse serviceResponse;
	IOReturn kr = kIOReturnSuccess;
	IOVirtualRange *range = NULL;
	UInt64 transferCount = 0;
	UInt32 transferCountHi = 0;
	UInt32 transferCountLo = 0;
	int scsi_status;
	int driver_status;
	char *pString;

	/*
	 * Set up some default "it didn't work" values:
	 */
	driver_status = DS_ILLEGAL;
	scsi_status = S_NO_STATUS;
	retval = 0;

	/*
	 * Create a task:
	 */
	if ((scsi_io->task = (*(scsi_io->interface))->CreateSCSITask(
			scsi_io->interface)) == NULL) {
		ltfsmsg(LTFS_ERR, "20023E", "CreateSCSITask", errno);
		return -1;

		/*
		 * Provide space for sense data:
		 */
	} else if ((kr = (*scsi_io->task)->SetAutoSenseDataBuffer(scsi_io->task,
			(SCSI_Sense_Data*) scsi_io->sensedata, sizeof(scsi_io->sensedata)))
			!= kIOReturnSuccess) {
		ltfsmsg(LTFS_ERR, "20023E", "SetAutoSenseDataBuffer", kr);
		retval = -1;

		/*
		 * Allocate a virtual range for the buffer
		 */
	} else if ((range = (IOVirtualRange*) malloc(sizeof(IOVirtualRange)))
			== NULL) {
		ltfsmsg(LTFS_ERR, "10001E", "ltotape_scsiexec: IOVirtualRange\n");
		retval = -1;

	} else {
		range->address = (IOVirtualAddress) scsi_io->data;
		range->length = scsi_io->data_length;
	}

	/*
	 * Maybe give up here (last point before we have buffer memory to free as well)
	 */
	if (retval == -1) {
		(*scsi_io->task)->Release(scsi_io->task);
		return retval;
	}

	/*
	 * Point to the caller's CDB:
	 */
	if ((kr = (*scsi_io->task)->SetCommandDescriptorBlock(scsi_io->task,
			scsi_io->cdb, scsi_io->cdb_length)) != kIOReturnSuccess) {
		ltfsmsg(LTFS_ERR, "20023E", "SetCommandDescriptorBlock", kr);
		retval = -1;

		/*
		 * Set the Scatter-Gather entry:
		 */
	} else {
		kr = (*scsi_io->task)->SetScatterGatherEntries(scsi_io->task, range,
				(scsi_io->data_direction == NO_TRANSFER) ? 0 : 1, range->length,
				(scsi_io->data_direction == HOST_READ) ?
						kSCSIDataTransfer_FromTargetToInitiator :
				(scsi_io->data_direction == HOST_WRITE) ?
						kSCSIDataTransfer_FromInitiatorToTarget :
						kSCSIDataTransfer_NoDataTransfer);
		if (kr != kIOReturnSuccess) {
			ltfsmsg(LTFS_ERR, "20023E", "SetScatterGatherEntries", kr);
			retval = -1;

			/*
			 * Set the timeout:
			 */
		} else if ((kr = (*scsi_io->task)->SetTimeoutDuration(scsi_io->task,
				scsi_io->timeout_ms)) != kIOReturnSuccess) {
			ltfsmsg(LTFS_ERR, "20023E", "SetTimeoutDuration", kr);
			retval = -1;

		} else {
			pString = ltotape_printbytes(scsi_io->cdb, scsi_io->cdb_length);
			ltfsmsg(LTFS_DEBUG, "20010D", pString, scsi_io->data_length);
			if (pString != (char*) NULL) {
				free(pString);
			}
		}
	}

	/*
	 * Here's the actual command execution:
	 */
	if (retval == 0) {
		if ((kr = (*scsi_io->task)->ExecuteTaskSync(scsi_io->task, &senseData,
				&taskStatus, &transferCount)) != kIOReturnSuccess) {
			ltfsmsg(LTFS_ERR, "20023E", "ExecuteTaskSync", kr);
		}

		/*
		 * Now determine the outcome:
		 */
		if ((kr = (*scsi_io->task)->GetSCSIServiceResponse(scsi_io->task,
				&serviceResponse)) != kIOReturnSuccess) {
			ltfsmsg(LTFS_ERR, "20023E", "GetSCSIServiceResponse", kr);
			retval = -1;

		} else if (serviceResponse
				== kSCSIServiceResponse_SERVICE_DELIVERY_OR_TARGET_FAILURE) {
			switch (taskStatus) {
			case kSCSITaskStatus_TaskTimeoutOccurred:
			case kSCSITaskStatus_ProtocolTimeoutOccurred:
			case kSCSITaskStatus_DeviceNotResponding:
				driver_status = DS_TIMEOUT;
				break;

			case kSCSITaskStatus_DeviceNotPresent:
				driver_status = DS_SELECTION_TIMEOUT;
				break;

			case kSCSITaskStatus_DeliveryFailure:
				driver_status = DS_FAILED;
				break;

			default:
				driver_status = DS_FAILED;
				break;
			}

		} else {
			driver_status = DS_GOOD;
			scsi_status = taskStatus;

			transferCountHi = ((transferCount >> 32) & 0xFFFFFFFF);
			transferCountLo = (transferCount & 0xFFFFFFFF);

			scsi_io->actual_data_length = transferCountLo;
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

			if ((scsi_io->cdb[0] == CMDread)
					&& (SENSE_HAS_ILI_SET(scsi_io->sensedata))) {
				int resid = ((int) scsi_io->sensedata[3] << 24)
						+ ((int) scsi_io->sensedata[4] << 16)
						+ ((int) scsi_io->sensedata[5] << 8)
						+ ((int) scsi_io->sensedata[6]);
				scsi_io->actual_data_length = scsi_io->data_length - resid;
				retval = scsi_io->actual_data_length;

			} else if (((scsi_io->cdb[0] == CMDwrite)
					|| (scsi_io->cdb[0] == CMDwrite_filemarks))
					&& (SENSE_IS_EARLY_WARNING_EOM(scsi_io->sensedata) || SENSE_IS_EARLY_WARNING_PEOM(scsi_io->sensedata))) {
				scsi_io->actual_data_length = scsi_io->data_length;
				retval = scsi_io->actual_data_length;

				if (scsi_io->eweomstate == before_eweom) {
					scsi_io->eweomstate = report_eweom; /* Already written the data, so set flag to report next time */
				}

			} else if (((scsi_io->cdb[0] == CMDwrite)
					|| (scsi_io->cdb[0] == CMDwrite_filemarks))
					&& (SENSE_IS_END_OF_MEDIA(scsi_io->sensedata))) {
				scsi_io->actual_data_length = 0;
				retval = -1;
				errno = EIO;

			}else if (((scsi_io->cdb[0] == CMDmode_select10) && (scsi_io->family == drivefamily_lto))
					&& (SENSE_IS_MODE_PARAMETER_ROUNDED(scsi_io->sensedata))) {
				retval = -EDEV_MODE_PARAMETER_ROUNDED;

			}else if ((scsi_io->family == drivefamily_lto)&&
		    		 (SENSE_IS_MEDIA_NOT_LOGICALLY_LOADED(scsi_io->sensedata))) {
		    	 scsi_io->sensedata[12] = 0x3A;
		    	 scsi_io->sensedata[13] = 0x00;
		    	retval = -1;
		    } else {
				retval = -1;
			}

		} else {
			retval = -1; /* Not GOOD and not CHECK CONDITION = BAD */
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

	ltfsmsg(LTFS_DEBUG, "20011D",
			driver_status, scsi_status, scsi_io->actual_data_length);
	if (scsi_status == S_CHECK_CONDITION) {
		pString = ltotape_printbytes(scsi_io->sensedata, scsi_io->sense_length);
		ltfsmsg(LTFS_DEBUG, "20012D", pString);
		if (pString != (char*) NULL) {
			free(pString);
		}
	}

	/*
	 * Tidy up after ourselves, regardless of how it went:
	 */
	free(range);
	(*scsi_io->task)->Release(scsi_io->task);
	return (retval);
}

/*F***************************************************************************
*
* Function Name:         CreateMatchingDictionary
*
* Function Prototype:    void CreateMatchingDictionary (SInt32 deviceType,
*                                         CFMutableDictionaryRef *matchingDict)
*
* Creates a 'matching dictionary' to help locate devices..
*
* Based on code from sample Mac project 'SCSIOldAndNew'
*
******************************************************************************
*
* Parameters :
*
* Name               Type           Use       Description
* ----               ----           ---       -----------
* deviceType         SInt32         Input     SCSI Peripheral Device Type
* matchingDict  *CFMutableDictionaryRef I/O   Created dictionary
*
******************************************************************************
*
* Function Return:   Nothing
*
******************************************************************************
*
* Author:  Chris Martin
* Date  :  Tue, 23 Feb 2010 09:10:30
*
****************************************************************************F*/
void CreateMatchingDictionary(SInt32 deviceType,
				CFMutableDictionaryRef *matchingDict)
{
	CFMutableDictionaryRef subDict;
	SInt32 deviceTypeNumber = deviceType;
	CFNumberRef deviceTypeRef = NULL;

	assert(matchingDict != NULL);
    

	/*
	 * Create the dictionaries:
	 */
	*matchingDict = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
			&kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	if (*matchingDict != NULL) {
		subDict = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
				&kCFTypeDictionaryKeyCallBacks,
				&kCFTypeDictionaryValueCallBacks);

		if (subDict != NULL) {
			/*
			 * Create a dictionary with the "SCSITaskDeviceCategory" key with the appropriate
			 * value for the device type we're interested in:
			 */
			CFDictionarySetValue(subDict,
					CFSTR(kIOPropertySCSITaskDeviceCategory),
					CFSTR(kIOPropertySCSITaskUserClientDevice));

			deviceTypeRef = CFNumberCreate(kCFAllocatorDefault,
					kCFNumberIntType, &deviceTypeNumber);
			CFDictionarySetValue(subDict,
					CFSTR(kIOPropertySCSIPeripheralDeviceType), deviceTypeRef);

			CFRelease(deviceTypeRef);
		}

		/*
		 * Add the dictionary to the main dictionary with the key "IOPropertyMatch"
		 *  to narrow the search to the above dictionary:
		 */
		CFDictionarySetValue(*matchingDict, CFSTR(kIOPropertyMatchKey),
				subDict);

		CFRelease(subDict);
	}
}

/*F***************************************************************************
*
* Function Name:         FindDevices
*
* Function Prototype:    boolean_t FindDevices (mach_port_t masterPort,
*					        io_iterator_t *iterator)
*
* Find connected SCSI peripheral devices
*
******************************************************************************
*
* Parameters :
*
* Name               Type           Use       Description
* ----               ----           ---       -----------
* masterPort         mach_port_t    Input     The IOKit "Master port"
* iterator        *io_iterator_t    I/O       Pointer to a list of devices(?)
*
******************************************************************************
*
* Function Return:   true if matching device(s) found;
*                    false otherwise
*
******************************************************************************
*
* Author:  Chris Martin
* Date  :  Tue, 23 Feb 2010 09:20:27
*
****************************************************************************F*/
boolean_t FindDevices(mach_port_t masterPort, io_iterator_t *iterator)
{
	CFMutableDictionaryRef matchingDict = NULL;
	boolean_t result = false;
	kern_return_t kr;

	CreateMatchingDictionary(SCSI_PERIPHERAL_DEVICE_TYPE_SEQACCESS,
			&matchingDict);

	if (matchingDict == NULL) {
		ltfsmsg(LTFS_ERR, "20023E", "CreateMatchingDictionary", errno);

	} else {
		/*
		 * Now search I/O Registry for matching devices:
		 */
		kr = IOServiceGetMatchingServices(masterPort, matchingDict, iterator);

		if (*iterator && (kr == kIOReturnSuccess)) {
			result = true;
		}
	}

	/*
	 * Apparently,
	 *  "IOServiceGetMatchingServices consumes a reference to the matching
	 *   dictionary, so we don't need to release the dictionary ref."
	 * What a relief.
	 */
	return result;
}

/*F***************************************************************************
*
* Function Name:         CreateDeviceInterface
*
* Function Prototype:    int CreateDeviceInterface (io_object_t scsiDevice)
*
* Create a path (interface) to a specific device.  Updates the globals
*  'interface' and 'plugInInterface'
*
******************************************************************************
*
* Parameters :
*
* Name               Type           Use       Description
* ----               ----           ---       -----------
* scsiDevice         io_object_t    Input     Which device to connect to
* 
******************************************************************************
*
* Function Return:   0 if created ok,
*                   -1 if failed for some reason
*
******************************************************************************
*
* Author:  Chris Martin
* Date  :  Tue, 23 Feb 2010 09:30:15
*
****************************************************************************F*/
int CreateDeviceInterface(io_object_t scsiDevice, ltotape_scsi_io_type *device)
{
	kern_return_t kr = kIOReturnSuccess;
	HRESULT plugInResult = S_OK;
	SInt32 score = 0;

	assert(scsiDevice != IO_OBJECT_NULL);

	/*
	 * Create the base interface of type IOCFPlugInInterface.
	 * This object will be used to create the SCSI device interface object.
	 */
	kr = IOCreatePlugInInterfaceForService(scsiDevice,
			kIOSCSITaskDeviceUserClientTypeID, kIOCFPlugInInterfaceID,
			&(device->plugInInterface), &score);
	if (kr != kIOReturnSuccess) {
		ltfsmsg(LTFS_ERR, "20023E", "IOCreatePlugInInterfaceForService", kr);
		return (-1);
		/*
		 * Query the base plugin interface for an instance of the specific
		 *  SCSI device interface object:
		 */
	} else {
		plugInResult = (*(device->plugInInterface))->QueryInterface(
				device->plugInInterface,
				CFUUIDGetUUIDBytes(kIOSCSITaskDeviceInterfaceID),
				(LPVOID *) &(device->interface));
		if (plugInResult != S_OK) {
			ltfsmsg(LTFS_ERR, "20023E", "QueryInterface", plugInResult);
			return (-1);
		}
	}
	return (0);
}

/**------------------------------------------------------------------------**
 * Open LTO tape backend.
 * TODO: should be returning an integer instead of setting errno
 * @param devname device name of the LTO tape driver
 * @return a pointer to the ltotape backend on success or NULL on error
 */
int ltotape_open(const char *devname, void **handle)
{
	ltotape_scsi_io_type *device;
	int index;
	int instance;
	io_name_t className;
	kern_return_t kr = kIOReturnSuccess;
	io_object_t scsiDevice = IO_OBJECT_NULL;
	io_iterator_t iterator = IO_OBJECT_NULL;
	struct tc_inq inq_data;
	int ret;
	int i;
	unsigned char modepage[TC_MP_MEDIUM_PARTITION_SIZE];
	unsigned char snvpdpage[32];

	device = (ltotape_scsi_io_type *) calloc(1, sizeof(ltotape_scsi_io_type));
	if (device == (ltotape_scsi_io_type *) NULL) {
		ltfsmsg(LTFS_ERR, "10001E", "ltotape_open: device private data");
		return -EDEV_NO_MEMORY;
	}

	/*
	 * Platform-specific variable initialization:
	 */
	device->plugInInterface = NULL;
	device->interface = NULL;

	/*
	 * Default logfile directory - initially NULL; will get set if/when we parse FUSE options..
	 */
	device->logdir = NULL;

	/*
	 * Look for tape devices:
	 */
	if (FindDevices(kIOMasterPortDefault, &iterator) == false) {
		return -1;
	}
  

	/*
	 * Select the n'th instance according to the provided device name:
	 */
	index = 0;
	if (sscanf(devname, "%d", &instance) != 1) {
		return -1;
	}

	while (index <= instance) {
		scsiDevice = IOIteratorNext(iterator);

		if (scsiDevice == IO_OBJECT_NULL) {
			ltfsmsg(LTFS_ERR, "20027E", instance);
			return -1;

		} else {
			if ((kr = IOObjectGetClass(scsiDevice, className))
					!= kIOReturnSuccess) {
				ltfsmsg(LTFS_ERR, "20023E", "IOObjectGetClass", kr);
				return -1;

			} else {
				ltfsmsg(LTFS_DEBUG, "20028D", className, index);
			}

			/*
			 * If this isn't the one we're looking for, we're done with
			 *  this SCSI object from I/O Registry..
			 */
			if (index != instance) {
				if ((kr = IOObjectRelease(scsiDevice)) != kIOReturnSuccess) {
					ltfsmsg(LTFS_ERR, "20023E", "IOObjectRelease", kr);
					return -1;
				}

				/*
				 * This IS the one we want.. need to go to the next level, creating an interface
				 *  to allow us to talk, then getting exclusive access for ourselves:
				 */
			} else {
				if (CreateDeviceInterface(scsiDevice, device) != 0) {
					ltfsmsg(LTFS_ERR, "20029E");
					return -1;

				} else if ((kr = IOObjectRelease(scsiDevice))
						!= kIOReturnSuccess) {
					ltfsmsg(LTFS_ERR, "20023E", "IOObjectRelease", kr);
					return -1;

				} else if ((kr = (*(device->interface))->ObtainExclusiveAccess(
						device->interface)) != kIOReturnSuccess) {
					ltfsmsg(LTFS_ERR, "20030E", kr);
					return -1;
				}
			}
		}

		/*
		 * Go on to the next instance (unless we've reached our device already):
		 */
		index++;
	}

	/*
	 * Default timeout, should be overwritten by each backend function:
	 */
	device->timeout_ms = LTO_DEFAULT_TIMEOUT;

	/*
	 * Default Early Warning EOM state is that we're not yet at the warning point:
	 */
	device->eweomstate = before_eweom;

	/*
	 * Find out what we're dealing with:
	 */
	ret = ltotape_inquiry(device, &inq_data);
	if (ret) {
		ltfsmsg(LTFS_ERR, "20083E", ret);
		ltotape_release(device);
		return ret;

	} else {
		device->family = drivefamily_unknown;
		device->type = drive_unknown;
		memset(device->serialno, 0, sizeof(device->serialno));
		memset(snvpdpage, 0, sizeof(snvpdpage));
		i = 0;

		ltfsmsg(LTFS_DEBUG, "20084D", inq_data.pid);

		while (supported_devices[i].product_family != drivefamily_unknown) {
			if ((strncmp((char *) inq_data.pid,
					(char *) supported_devices[i].product_id,
					strlen((char *) supported_devices[i].product_id)) == 0)) {
				device->family = supported_devices[i].product_family;
				device->type = supported_devices[i].drive_type;

				if (ltotape_evpd_inquiry(device, VPD_PAGE_SERIAL_NUMBER,
						snvpdpage, sizeof(snvpdpage)) < 0) {
					strcpy(device->serialno, "Unknown");
				} else {
					strncpy(device->serialno, (const char*) (snvpdpage + 4),
							(size_t) snvpdpage[3]);
				}

				ltfsmsg(LTFS_INFO, "20013I",
						supported_devices[i].description, device->serialno);
				break;
			}
			i++;
		}

		if (device->family == drivefamily_unknown) {
			ltfsmsg(LTFS_ERR, "20085E", inq_data.pid);
			ltotape_release(device);
			errno = -EBADF; /* Unsupported device */
			return -EDEV_DEVICE_UNSUPPORTABLE;
		}
	}

#ifdef QUANTUM_BUILD
	/*
	 * Store the drive vendor
	 */

	if ( strncmp( inq_data.vid, "HP      ", 8 ) == 0 )
	{
		device->drive_vendor_id = drivevendor_hp;
	}
	else if ( strncmp( inq_data.vid, "QUANTUM ", 8 ) == 0 )
	{
		device->drive_vendor_id = drivevendor_quantum;
	}
	else
	{
		device->drive_vendor_id = drivevendor_unknown;
	}
#endif

	/*
	 * For an LTO drive, need to determine whether it is partition-capable or only partition-aware:
	 */
	if (device->family == drivefamily_lto) {
		ret = ltotape_test_unit_ready(device);

		if (SENSE_IS_UNIT_ATTENTION(device->sensedata)) {
			ret = ltotape_test_unit_ready(device);
		}

		ret = ltotape_modesense(device, TC_MP_MEDIUM_PARTITION,
				TC_MP_PC_CHANGEABLE, 0, modepage, sizeof(modepage));
		if (ret < 0) {
			ltotape_release(device);
			return -EBADF;

		} else if ((modepage[PARTTYPES_OFFSET] & PARTTYPES_MASK)
				!= PARTTYPES_MASK) {
			ltfsmsg(LTFS_ERR, "20014E", inq_data.revision);
			ltotape_release(device);
			return -EBADF;
		}
	}

	*handle = (void *) device;
	return DEVICE_GOOD;
}

/**------------------------------------------------------------------------**
 * Reopen a device. If reopen is not needed, do nothing in this call.
 *
 * @param devname The name of the device to be opened.
 * @param handle Device handle returned by the backend's open()
 * @return 0 on success, else a negative value indicating the status
 * is returned.
 */
int ltotape_reopen(const char *devname, void *handle)
{
	ltotape_scsi_io_type *device;
	int index;
	int instance;
	io_name_t className;
	kern_return_t kr = kIOReturnSuccess;
	io_object_t scsiDevice = IO_OBJECT_NULL;
	io_iterator_t iterator = IO_OBJECT_NULL;
	struct tc_inq inq_data;
	int ret;
	int i;
	unsigned char modepage[TC_MP_MEDIUM_PARTITION_SIZE];
	unsigned char snvpdpage[32];

	device = (ltotape_scsi_io_type *) handle;
	if (device == (ltotape_scsi_io_type *) NULL) {
		ltfsmsg(LTFS_ERR, "20087E", devname, errno);
		return -EDEV_DEVICE_UNOPENABLE;
	}

	/*
	 * Platform-specific variable initialization:
	 */
	device->plugInInterface = NULL;
	device->interface = NULL;

	/*
	 * Default logfile directory - initially NULL; will get set if/when we parse FUSE options..
	 */
	device->logdir = NULL;

	/*
	 * Look for tape devices:
	 */
	if (FindDevices(kIOMasterPortDefault, &iterator) == false) {
		return -1;
	}

	/*
	 * Select the n'th instance according to the provided device name:
	 */
	index = 0;
	if (sscanf(devname, "%d", &instance) != 1) {
		return -1;
	}

	while (index <= instance) {
		scsiDevice = IOIteratorNext(iterator);

		if (scsiDevice == IO_OBJECT_NULL) {
			ltfsmsg(LTFS_ERR, "20027E", instance);
			return -1;
		} else {
			if ((kr = IOObjectGetClass(scsiDevice, className))
					!= kIOReturnSuccess) {
				ltfsmsg(LTFS_ERR, "20023E", "IOObjectGetClass", kr);
				return -1;
			} else {
				ltfsmsg(LTFS_DEBUG, "20028D", className, index);
			}

			/*
			 * If this isn't the one we're looking for, we're done with
			 *  this SCSI object from I/O Registry..
			 */
			if (index != instance) {
				if ((kr = IOObjectRelease(scsiDevice)) != kIOReturnSuccess) {
					ltfsmsg(LTFS_ERR, "20023E", "IOObjectRelease", kr);
					return -1;
				}

				/*
				 * This IS the one we want.. need to go to the next level, creating an interface
				 *  to allow us to talk, then getting exclusive access for ourselves:
				 */
			} else {
				if (CreateDeviceInterface(scsiDevice, device) != 0) {
					ltfsmsg(LTFS_ERR, "20029E");
					return -1;

				} else if ((kr = IOObjectRelease(scsiDevice))
						!= kIOReturnSuccess) {
					ltfsmsg(LTFS_ERR, "20023E", "IOObjectRelease", kr);
					return -1;

				} else if ((kr = (*(device->interface))->ObtainExclusiveAccess(
						device->interface)) != kIOReturnSuccess) {
					ltfsmsg(LTFS_ERR, "20030E", kr);
					return -1;
				}
			}
		}

		/*
		 * Go on to the next instance (unless we've reached our device already):
		 */
		index++;
	}

	/*
	 * Default timeout, should be overwritten by each backend function:
	 */
	device->timeout_ms = LTO_DEFAULT_TIMEOUT;

	/*
	 * Default Early Warning EOM state is that we're not yet at the warning point:
	 */
	device->eweomstate = before_eweom;

	/*
	 * Find out what we're dealing with:
	 */
	ret = ltotape_inquiry (device, &inq_data);
	if (ret) {
		ltfsmsg(LTFS_ERR, "20083E", ret);
		ltotape_release(device);
		return ret;

	} else {
		device->family = drivefamily_unknown;
		memset(device->serialno, 0, sizeof(device->serialno));
		memset(snvpdpage, 0, sizeof(snvpdpage));
		i = 0;

		ltfsmsg(LTFS_DEBUG, "20084D", inq_data.pid);

		while (supported_devices[i].product_family != drivefamily_unknown) {
			if ((strncmp((char *) inq_data.pid,
					(char *) supported_devices[i].product_id,
					strlen((char *) supported_devices[i].product_id)) == 0)) {
				device->family = supported_devices[i].product_family;

				if (ltotape_evpd_inquiry(device, VPD_PAGE_SERIAL_NUMBER,
						snvpdpage, sizeof(snvpdpage)) < 0) {
					strcpy(device->serialno, "Unknown");
				} else {
					strncpy(device->serialno, (const char*) (snvpdpage + 4),
							(size_t) snvpdpage[3]);
				}

				ltfsmsg(LTFS_INFO, "20013I",
						supported_devices[i].description, device->serialno);
				break;
			}
			i++;
		}

		if (device->family == drivefamily_unknown) {
			ltfsmsg(LTFS_ERR, "20085E", inq_data.pid);
			ltotape_release(device);
			return -EBADF; /* Unsupported device */
		}
	}

#ifdef QUANTUM_BUILD
	/*
	 * Store the drive vendor
	 */

	if ( strncmp( inq_data.vid, "HP      ", 8 ) == 0 )
	{
		device->drive_vendor_id = drivevendor_hp;
	}
	else if ( strncmp( inq_data.vid, "QUANTUM ", 8 ) == 0 )
	{
		device->drive_vendor_id = drivevendor_quantum;
	}
	else
	{
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
			ltotape_release(device);
			return -EBADF;

		} else if ((modepage [PARTTYPES_OFFSET] & PARTTYPES_MASK) != PARTTYPES_MASK) {
			ltfsmsg(LTFS_ERR, "20014E", inq_data.revision);
			ltotape_release(device);
			return -EBADF;
		}
	}

	return DEVICE_GOOD;
}


/**------------------------------------------------------------------------**
 * Close LTO tape backend
 * @param device a pointer to the ltotape backend
 * @return 0 on success or a negative value on error
 */
int ltotape_close(void *device)
{
	struct tc_position pos;
	int status = 0;

	if (device != NULL) {
		ltotape_rewind(device, &pos);
		status = ltotape_release(device);
	}

	return status;
}

/**------------------------------------------------------------------------**
 * Close only the device.
 *
 * @param device The ltotape backend handle.
 * @return 0 on success, a negative value on error.
 */
int ltotape_close_raw(void *device)
{
	int status = 0;

	if (device != NULL) {
		status = ltotape_release(device);
	}

	return status;
}


/**------------------------------------------------------------------------**
 * Clean up and release the SCSI tape resource
 *  (pulled out of ltotape_close() for wider use)
 * @param device a pointer to the ltotape backend
 * @return 0 on success or a negative value on error
 */
int ltotape_release(void *device)
{
	int status = 0;
	kern_return_t kr;
	ltotape_scsi_io_type *sio = (ltotape_scsi_io_type *)device;

	if (device != NULL) {
		if ((kr = (*(sio->interface))->ReleaseExclusiveAccess(sio->interface))
				!= kIOReturnSuccess) {
			ltfsmsg(LTFS_ERR, "20023E", "ReleaseExclusiveAccess", kr);
			status = -1;
		}

		(*(sio->interface))->Release(sio->interface);

		if (sio->plugInInterface != NULL) {
			IODestroyPlugInInterface(sio->plugInInterface);
		}

		free(device);
	}

	return status;
}


#undef __ltotape_platform_c
