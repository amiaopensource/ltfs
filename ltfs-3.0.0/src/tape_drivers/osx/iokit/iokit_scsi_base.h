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
** FILE NAME:       tape_drivers/osx/iokit/iokit_scsi_base.h
**
** DESCRIPTION:     Defines API for raw SCSI operations in user-space
**                  to control SCSI-based tape and tape changer
**                  devices.
**
** AUTHOR:          Michael A. Richmond
**                  IBM Almaden Research Center
**                  mar@almaden.ibm.com
**
*************************************************************************************
*/


#ifndef __iokit_scsi_base_h
#define __iokit_scsi_base_h

#include <inttypes.h>

#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOTypes.h>
#include <IOKit/scsi/SCSITaskLib.h>
#include <IOKit/scsi/SCSICommandOperationCodes.h>

#include "libltfs/ltfs_error.h"
#include "libltfs/tape_ops.h"
#include "tape_drivers/tape_drivers.h"

#define SIZE_OF_SENSE_STRING			256

#define COMMAND_DESCRIPTION_LENGTH      32

/* 2000 second timeout values covers the majority of SCSI operations. */
#define SCSI_PASSTHRU_TIMEOUT_VALUE 2000000		// ms

#define SCSI_FROM_INITIATOR_TO_TARGET	1
#define SCSI_FROM_TARGET_TO_INITIATOR	2
#define SCSI_NO_DATA_TRANSFER			3


typedef struct iokit_device
{
	mach_port_t             masterPort;				/* OS X master port to kernel for IOKit calls */
	io_service_t            ioservice;				/* Service instance for the selected tape drive */
	IOCFPlugInInterface     **plugInInterface;		/* IOKit plugin interface */
	SCSITaskDeviceInterface **scsiTaskInterface;	/* IOKit task device interface */
	SCSITaskInterface 		**task;					/* Reusable task object */

	boolean_t				exclusive_lock;			/* OS X exclusive lock held on device */
	boolean_t               use_sili;               /* Use SILI bit for reading unusual length */

	int8_t					*device_name;           /* Identifier for drive on host */
	int8_t					device_code;			/* Identifier for device code (used to calculate timeout values) */
	SCSI_Sense_Data			lastSenseData;			/* SCSI sense data from most recent SCSI Task */
	SCSITaskStatus 			lastTaskStatus;			/* Task status from most recent SCSI Task */
	uint64_t				lastTransferCount;		/* Bytes transfered during most recent SCSI Task */
	boolean_t 				eot;					/* leading end of partition */
	uint32_t				current_position;		/* for qry position, for set position to go to */
	boolean_t				position_valid;			/* current position information is valid? */
	uint32_t				lbot;					/* last block written to tape */
	uint32_t				num_blocks;				/* number of blocks in buffer */
	uint32_t				num_bytes;				/* number of bytes in buffer */
	boolean_t				bot;					/* beginning of partition */
	uint8_t					current_partition;		/* Partition in which the tape is currently positioned */

	/* variables for the device bus information */
	int32_t					domain_id;				/* Domain id */
	int32_t					target_id;				/* Target id */
	int32_t					lun;					/* SCSI LUN */
	uint64_t				wwnn;					/* World-wide network name */
	uint64_t				wwpn;					/* World-wide port number */

	/* variables tracking the medium information */
	uint32_t				blk_size;
	uint32_t				blk_num;
	uint8_t		 			buffered_mode;
	uint8_t		 			capacity_scaling;	/* 0, 1, 2, 3, or 4 */
	uint8_t		 			capacity_scaling_hex_val;
	uint8_t 				compression;
	uint8_t 				density;
	uint8_t					ending_fm;
	uint8_t					logical_write_protect;
	uint8_t					medium_type;
	uint8_t					write_protect;

	/* variables tracking the operation state of the device */
	uint8_t					eom;
	uint32_t				eom_block_id;
	boolean_t				deferred_eom; /* Unused */

	/* variables used for erp */
	uint8_t					read_past_filemark;		/* drv->read_mode which is equal to "read_past_filemark" */

	uint32_t				max_blk_size;
	uint32_t				max_xfer_len;
	uint32_t				min_blk_size;

	size_t					resid;

	/* Latched value of tape alert */
	uint64_t				tape_alert;

	/* variables used for getting xattr */
	long					fetch_sec_acq_loss_w;
	bool					dirty_acq_loss_w;
	float					acq_loss_w;

	boolean_t               is_data_key_set;        /* Is a valid data key set? */
	unsigned char           dki[12];                /* key-alias */

	DRIVE_TYPE              drive_type;             /* drive type defined by ltfs */

	crc_enc       f_crc_enc;         /**< Pointer to CRC encode function */
	crc_check     f_crc_check;       /**< Pointer to CRC encode function */
} iokit_device_t;

#define MAXSENSE        			255

#define UNIT_SERIAL_LENGTH 10

typedef struct _scsi_device_identifier {
	char vendor_id[VENDOR_ID_LENGTH + 1];
	char product_id[PRODUCT_ID_LENGTH + 1];
	char product_rev[PRODUCT_REV_LENGTH + 1];
	char unit_serial[UNIT_SERIAL_LENGTH + 1];
	DRIVE_TYPE drive_type;
} scsi_device_identifier;

typedef struct _cbd_pass_through {

	uint8_t cmd_length;				/* Input: Length of SCSI command 6, 10, 12 or 16               */
	uint8_t *cdb;					/* Input: SCSI command descriptor block                        */
	size_t buffer_length;			/* Input: Length of data buffer to be transferred              */
									/* Output: Number of bytes actually transferred                */
	uint8_t *buffer;				/* Input/Output: data transfer buffer or NULL                  */
	uint8_t data_direction;			/* Input: Data transfer direction                              */
	uint32_t timeout;				/* Input: Timeout in seconds                                   */
	uint8_t *sense;					/* Output: Sense data when sense length > 0                    */
	size_t sense_length;			/* Input/Output: Number of sense data bytes                    */
	uint8_t sense_valid;			/* Output: sense data contains valid data                      */
	uint8_t check_condition;		/* Output: passthrough resulted in check condition status      */
	int32_t residual;				/* Output: Number of bytes in residual after transfer          */
	int32_t result;					/* Output: result returned from lower level driver             */
	uint8_t message_status;			/* Output: message from SCSI transport layer                   */
	uint8_t target_status;			/* Output: target device status                                */
	int16_t driver_status;			/* Output: level driver status                                 */
	int16_t host_status;			/* Output: host bus adapter status                             */
	char *operation_descriptor;		/* Info: Human readable description of operation for debugging */

	uint8_t reserved[64];
} cdb_pass_through;

struct error_table {
	uint32_t sense;     /* sense data */
	int      err_code;  /* error code */
	char     *msg;      /* description of error */
};

struct iokit_global_data {
	char     *str_crc_checking; /**< option string for crc_checking */
	unsigned crc_checking;      /**< Is crc checking enabled? */
	unsigned strict_drive;      /**< Is bar code length checked strictly? */
	unsigned disable_auto_dump; /**< Is auto dump disabled? */
};

#define MASK_WITH_SENSE_KEY    (0xFFFFFF)
#define MASK_WITHOUT_SENSE_KEY (0x00FFFF)

extern struct error_table standard_table[];
extern struct error_table tape_errors[];
extern struct error_table changer_errors[];


/**
 * Convert the most recent sense to errno for device.
 * This conversion is not valid for write and read command.
 *
 * @param $device the SCSI device.
 */
int iokit_sense2errno( iokit_device_t *device, char **msg );


/**
 * Obtain an IOKit exclusive lock on the SCSI device. This lock
 * prevents other applications from interacting with the device
 * at the OS layer.
 *
 * @param $device the SCSI device.
 *
 * @return 0 on success.
 */
int32_t iokit_obtain_exclusive_access( iokit_device_t *device );


/**
 * Release the IOKit exclusive lock held on the SCSI device.
 *
 * @param $device the SCSI device.
 *
 * @return 0 on success.
 */
int32_t iokit_release_exclusive_access( iokit_device_t *device );


/**
 * Invokes a SCSI command block on a SCSI device.
 * The iokit_issue_cdb_command(...) method is primarily
 * intended to be used by internal callers in the iokit
 * scsi code. External callers should favor use of the
 * iokit_passthrough(...) method.
 *
 * @param $device the SCSI device.
 *
 * @param $cdb SCSI Command Description Block to issue to the
 *   device.
 *
 * @param $buffer buffer of data to be sent to or returned from
 *   device.
 *
 * @param $bufferSize size of buffer.
 *
 * @param $transferDirection direction of data transfer.
 *   Valid values: SCSI_FROM_INITIATOR_TO_TARGET,
 *   SCSI_FROM_TARGET_TO_INITIATOR, SCSI_NO_DATA_TRANSFER.
 *
 * @param $timeout maximum time to allow command before aborting. (milliseconds)
 *
 * @param $commandDescription description of cdb command.
 *   Used in debug messages when reporting operation.
 *
 * @param $msg description of the result of operation.
 *   Used in debug messages when reporting operation.
 *
 * @return 0 or positive value on success.
 */
int32_t iokit_issue_cdb_command( iokit_device_t *device,
							 uint8_t *cdb,
							 uint8_t cdbSize,
							 void *buffer,
							 size_t bufferSize,
							 uint8_t transferDirection,
							 uint32_t timeout,
							 char *commandDescription,
							 char **msg );


/**
 * Invokes the SCSI command block in the passthrough
 * structure against the device. Result data (including
 * SCSI sense data) is returned to the caller in the
 * passthrough structure.
 *
 * @param $device
 *   the SCSI device.
 *
 * @param $passthrough
 *   structure holding SCSI cdb, result buffer, and operation
 *   metadata such as SCSI sense values.
 *
 * @param $msg
 *   description of the result of operation.
 *
 * @return 0 on success. (Note that a successful iokit_passthrough(...)
 *   call does not imply a successful SCSI operation. The
 *   passthrough->result and passthrough->checkcondition values
 *   should be checked for SCSI command status.
 */
int32_t iokit_passthrough( iokit_device_t *device,
						   cdb_pass_through *passthrough, char **msg );


/**
 * Allocate a SCSITask instance for the device.
 * The OS X IOKit framework uses the SCSITask structure
 * to issue CDB operations on SCSI devices.
 * The iokit_scsi_base API automatically creates a
 * SCSITask for the device when needed. This API also
 * re-uses any existing SCSITask structure when issuing
 * a CDB on a device.
 * This iokit_allocate_scsitask() method is intended for
 * situations where an application requires specific
 * control over SCSITask allocation and release.
 * The majority of applications will get optimal
 * performance from the default automatic allocation
 * strategy.
 *
 * @param $device the SCSI device.
 *
 * @return 0 on success.
 */
int32_t iokit_allocate_scsitask( iokit_device_t *device );


/**
 * Release the SCSITask instance for the device.
 * The OS X IOKit framework uses the SCSITask structure
 * to issue CDB operations on SCSI devices.
 * The iokit_scsi_base API automatically creates a
 * SCSITask for the device when needed. This API also
 * re-uses any existing SCSITask structure when issuing
 * a CDB on a device.
 * This iokit_release_scsitask() method is intended for
 * situations where an application requires specific
 * control over SCSITask allocation and release.
 * The majority of applications will get optimal
 * performance from the default automatic allocation
 * strategy.
 *
 * @param $device the SCSI device.
 */
void iokit_release_scsitask( iokit_device_t *device );

/**
 * Frees up resources associated with the device.
 * After this function succeeds the device structure should
 * be disposed of.
 *
 * @param $device the SCSI device.
 *
 * @return 0 on success.
 */
int32_t iokit_free_device( iokit_device_t *device );


/**
 * Provides an integer count of all medium changer devices connected
 * to the host.
 *
 * @return the number of smc devices connected to the host.
 */
int32_t iokit_get_smc_device_count();


/**
 * Provides an integer count of all sequential access devices connected
 * to the host.
 *
 * @return the number of ssc devices connected to the host.
 */
int32_t iokit_get_ssc_device_count();


/**
 * Finds all medium changer devices. Then selects the device changer_number
 * from that list and populates the device structure to allow access to that
 * device.
 * For example changer_number=0 will find the first smc device.
 *
 * @param $device the SCSI device.
 *
 * @param $changer_number the number of the smc device to find.
 *
 * @return 0 on success.
 */
int32_t iokit_find_smc_device( iokit_device_t *device,
							   int32_t changer_number );


/**
 * Finds all sequential access devices. Then selects the device drive_number
 * from that list and populates the device structure to allow access to that
 * device.
 * For example drive_number=0 will find the first ssc device.
 *
 * @param $device the SCSI device.
 *
 * @param $changer_number the number of the ssc device to find.
 *
 * @return 0 on success.
 */
int32_t iokit_find_ssc_device( iokit_device_t *device,
							   int32_t drive_number);

#endif // __iokit_scsi_base_h
