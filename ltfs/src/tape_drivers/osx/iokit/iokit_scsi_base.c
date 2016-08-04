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
** FILE NAME:       tape_drivers/osx/iokit/iokit_scsi_base.c
**
** DESCRIPTION:     Implements raw SCSI operations in user-space
**                  to control SCSI-based tape and tape changer
**                  devices.
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


#include <errno.h>
#include <inttypes.h>

#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOTypes.h>
#include <IOKit/scsi/SCSITaskLib.h>
#include <IOKit/storage/IOStorageProtocolCharacteristics.h>
#include <IOKit/storage/IOStorageDeviceCharacteristics.h>
#include <mach/mach.h>

#include "iokit_scsi_base.h"
#include "libltfs/ltfslogging.h"

/**
 * Prototypes for internal functions.
 */
void _create_matching_dictionary_for_device_class( CFMutableDictionaryRef *matchingDict,
												    SInt32 peripheralDeviceType );

void _create_matching_dictionary_for_ssc( CFMutableDictionaryRef *matchingDict );

void _create_matching_dictionary_for_smc( CFMutableDictionaryRef *matchingDict );

int32_t _find_device( iokit_device_t *device, int32_t device_number,
		          CFMutableDictionaryRef *matchingDict );

int32_t _get_device_count( CFMutableDictionaryRef *matchingDict );

void _cfdataptr2int(const uint8_t *dataPtr, uint64_t *result);


/* Standard SCSI sense table */
struct error_table standard_table[] = {
	/* Sense Key 0 (No Sense) */
	{0x000000, -EDEV_NO_SENSE,                  "No Additional Sense Information"},
	{0x000001, -EDEV_FILEMARK_DETECTED,         "Filemark Detected"},
	{0x000002, -EDEV_EARLY_WARNING,             "End-of-Partition/Medium Detected (Early Warning)"},
	{0x000004, -EDEV_BOP_DETECTED,              "Beginning-of-Partition/Medium Detected"},
	{0x000007, -EDEV_PROG_EARLY_WARNING,        "End-of-Partition/Medium Detected (Programable Early Warning)"},
	{0x000016, -EDEV_OPERATION_IN_PROGRESS,     "Operation in Progress"},
	{0x000017, -EDEV_CLEANING_REQUIRED,         "Cleaning Required"},
	{0x000018, -EDEV_OPERATION_IN_PROGRESS,     "Erase Operation in Progress"},
	{0x001401, -EDEV_RECORD_NOT_FOUND,          "Record Not Found (String Search)"},
	{0x002E00, -EDEV_INSUFFICIENT_TIME,         "Insufficient Time For Operation (String Search)"},
	{0x003003, -EDEV_CLEANING_CART,             "Cleaning tape installed"},
	/* Sense Key 1 (Recovered Error) */
	{0x010000, -EDEV_RECOVERED_ERROR,           "No Additional Sense Information"},
	{0x010017, -EDEV_CLEANING_REQUIRED,         "Drive Needs Cleaning"},
	{0x010A00, -EDEV_RECOVERED_ERROR,           "Error log overflow"},
	{0x010C00, -EDEV_RECOVERED_ERROR,           "Write Error: A write error occurred, but was recovered."},
	{0x011100, -EDEV_RECOVERED_ERROR,           "Read Error: A read error occurred, but was recovered."},
	{0x011701, -EDEV_RECOVERED_ERROR,           "Recovered Data with Retries"},
	{0x011800, -EDEV_RECOVERED_ERROR,           "Recovered Data with Error Correction Applied"},
	{0x013700, -EDEV_MODE_PARAMETER_ROUNDED,    "Mode Parameters Rounded"},
	{0x014700, -EDEV_RECOVERED_ERROR,           "SCSI parity error"},
	{0x015B02, -EDEV_RECOVERED_ERROR,           "Log counter at maximum"},
	{0x015D00, -EDEV_RECOVERED_ERROR,           "Failure Prediction Threshold Exceeded"},
	{0x015DFF, -EDEV_RECOVERED_ERROR,           "Failure Prediction Threshold Exceeded (FALSE)"},
	{0x01EF13, -EDEV_RECOVERED_ERROR,           "Encryption - Key Translate"},
	/* Sense Key 2 (Not Ready) */
	{0x020017, -EDEV_CLEANING_IN_PROGRESS,      "Drive cleaning requested"},
	{0x020400, -EDEV_NOT_REPORTABLE,            "Logical Unit Not Ready, Cause Not Reportable"},
	{0x020401, -EDEV_BECOMING_READY,            "Logical Unit Is in Process of Becoming Ready"},
	{0x020402, -EDEV_NEED_INITIALIZE,           "Initializing Command Required"},
	{0x020403, -EDEV_NEED_INITIALIZE,           "Logical Unit Not Ready, Manual Intervention Required"},
	{0x020404, -EDEV_OPERATION_IN_PROGRESS,     "Logical Unit Not Ready, Format in Progress"},
	{0x020407, -EDEV_OPERATION_IN_PROGRESS,     "Operation in progress"},
	{0x020412, -EDEV_OFFLINE,                   "Logical Unit Not Ready, Offline"},
	{0x020413, -EDEV_OPERATION_IN_PROGRESS,     "Logical Unit Not Ready, SA Creation in Progress"},
	{0x020B01, -EDEV_OVER_TEMPERATURE,          "Warning - Specified Temperature Exceeded"},
	{0x023003, -EDEV_CLEANING_IN_PROGRESS,      "Cleaning Cartridge Installed"},
	{0x023007, -EDEV_NOT_READY,                 "Cleaning Failure"},
	{0x023A00, -EDEV_NO_MEDIUM,                 "Medium Not Present"},
	{0x023A02, -EDEV_IE_OPEN,                   "Medium Not Present - Tray Open"},
	{0x023A04, -EDEV_NO_MEDIUM,                 "Not Ready - Medium Auxiliary Memory Accessible"},
	{0x023B12, -EDEV_DOOR_OPEN,                 "Magazine removed"},
	{0x023E00, -EDEV_NOT_SELF_CONFIGURED_YET,   "Logical Unit Has Not Self-configured"},
	{0x025300, -EDEV_LOAD_UNLOAD_ERROR,         "Media Load or Eject Failed"},
	{0x027411, -EDEV_PARAMETER_VALUE_REJECTED,  "SA Creation Parameter Value Rejected"},
	/* Sense Key 3 (Medium Error) */
	{0x030302, -EDEV_WRITE_PERM,                "Excessive Write Errors"},
	{0x030410, -EDEV_CM_PERM,                   "Logical Unit Not Ready, Auxiliary Memory Not Accessible"},
	{0x030900, -EDEV_RW_PERM,                   "Track Following Error (Servo)"},
	{0x030C00, -EDEV_WRITE_PERM,                "Write Error"},
	{0x031100, -EDEV_READ_PERM,                 "Unrecovered Read Error"},
	{0x031101, -EDEV_READ_PERM,                 "Read Retries Exhausted"},
	{0x031108, -EDEV_READ_PERM,                 "Incomplete Block Read"},
	{0x031112, -EDEV_CM_PERM,                   "Auxiliary Memory Read Error"},
	{0x031400, -EDEV_RW_PERM,                   "Recorded Entity Not Found"},
	{0x031401, -EDEV_RW_PERM,                   "Record Not Found"},
	{0x031402, -EDEV_RW_PERM,                   "Filemark or Setmark Not Found"},
	{0x031403, -EDEV_RW_PERM,                   "End-of-Data Not Found"},
	{0x031404, -EDEV_RW_PERM,                   "Block Sequence Error"},
	{0x033000, -EDEV_MEDIUM_FORMAT_ERROR,       "Incompatible Medium Installed"},
	{0x033001, -EDEV_MEDIUM_FORMAT_ERROR,       "Cannot Read Medium, Unknown Format"},
	{0x033002, -EDEV_MEDIUM_FORMAT_ERROR,       "Cannot Read Medium, Incompatible Format"},
	{0x033003, -EDEV_MEDIUM_FORMAT_ERROR,       "Cleaning tape installed"},
	{0x033007, -EDEV_CLEANING_FALIURE,          "Cleaning failure"},
	{0x03300D, -EDEV_MEDIUM_ERROR,              "Medium Error/WORM Medium"},
	{0x033100, -EDEV_MEDIUM_FORMAT_CORRUPTED,   "Medium Format Corrupted"},
	{0x033101, -EDEV_MEDIUM_ERROR,              "Format Command Failed"},
	{0x033300, -EDEV_MEDIUM_ERROR,              "Tape Length Error"},
	{0x033B00, -EDEV_MEDIUM_ERROR,              "Sequential Positioning Error"},
	{0x035000, -EDEV_RW_PERM,                   "Write Append Error"},
	{0x035100, -EDEV_MEDIUM_ERROR,              "Erase Failure"},
	{0x035200, -EDEV_MEDIUM_ERROR,              "Cartridge Fault"},
	{0x035300, -EDEV_LOAD_UNLOAD_ERROR,         "Media Load or Eject Failed"},
	{0x035304, -EDEV_LOAD_UNLOAD_ERROR,         "Medium Thread or Unthread Failure"},
	/* Sense Key 4 (Hardware or Firmware Error) */
	{0x040403, -EDEV_HARDWARE_ERROR,            "Manual Intervention Required"},
	{0x040801, -EDEV_HARDWARE_ERROR,            "Logical Unit Communication Failure"},
	{0x040900, -EDEV_HARDWARE_ERROR,            "Track Following Error"},
	{0x041001, -EDEV_LBP_WRITE_ERROR,           "Logical Block Guard Check Failed"},
	{0x041004, -EDEV_HARDWARE_ERROR,            "Logical Block Protection Error On Recover Buffered Data"},
	{0x041501, -EDEV_HARDWARE_ERROR,            "Machanical Position Error"},
	{0x043B00, -EDEV_HARDWARE_ERROR,            "Sequential Positioning Error"},
	{0x043B08, -EDEV_HARDWARE_ERROR,            "Reposition Error"},
	{0x043B0D, -EDEV_HARDWARE_ERROR,            "Medium Destination Element Full"},
	{0x043B0E, -EDEV_HARDWARE_ERROR,            "Medium Source Element Empty"},
	{0x043F0F, -EDEV_HARDWARE_ERROR,            "Echo buffer overwritten"},
	{0x044000, -EDEV_HARDWARE_ERROR,            "Diagnostic Failure"},
	{0x044100, -EDEV_HARDWARE_ERROR,            "Data Path Failure"},
	{0x044400, -EDEV_HARDWARE_ERROR,            "Internal Target Failure"},
	{0x044C00, -EDEV_HARDWARE_ERROR,            "Logical Unit Failed Self-Configuration"},
	{0x045100, -EDEV_HARDWARE_ERROR,            "Erase Failure"},
	{0x045200, -EDEV_HARDWARE_ERROR,            "Cartridge Fault"},
	{0x045300, -EDEV_HARDWARE_ERROR,            "Media Load or Eject Failed"},
	{0x045301, -EDEV_HARDWARE_ERROR,            "A drive did not unload a cartridge."},
	{0x045304, -EDEV_HARDWARE_ERROR,            "Medium Thread or Unthread Failure"},
	/* Sense Key 5 (Illegal Request) */
	{0x050E03, -EDEV_ILLEGAL_REQUEST,           "Invalid Field in Command Information Unit (e.g., FCP_DL error)"},
	{0x051A00, -EDEV_ILLEGAL_REQUEST,           "Parameter List Length Error"},
	{0x052000, -EDEV_ILLEGAL_REQUEST,           "Invalid Command Operation Code"},
	{0x05200C, -EDEV_ILLEGAL_REQUEST,           "Illegal Command When Not In Append-Only Mode"},
	{0x052101, -EDEV_INVALID_ADDRESS,           "Invalid Element Address"},
	{0x052400, -EDEV_INVALID_FIELD_CDB,         "Invalid Field in CDB"},
	{0x052500, -EDEV_ILLEGAL_REQUEST,           "Logical Unit Not Supported"},
	{0x052600, -EDEV_ILLEGAL_REQUEST,           "Invalid Field in Parameter List"},
	{0x052601, -EDEV_ILLEGAL_REQUEST,           "Parameter list error: parameter not supported"},
	{0x052602, -EDEV_ILLEGAL_REQUEST,           "Parameter value invalid"},
	{0x052603, -EDEV_ILLEGAL_REQUEST,           "Threshold Parameters Not Supported"},
	{0x052604, -EDEV_ILLEGAL_REQUEST,           "Invalid release of persistent reservation"},
	{0x052611, -EDEV_ILLEGAL_REQUEST,           "Encryption - Incomplete Key-Associate Data Set"},
	{0x052612, -EDEV_ILLEGAL_REQUEST,           "Vendor Specific Key Reference Not Found"},
	{0x052690, -EDEV_ILLEGAL_REQUEST,           "Wrong firmware image, does not fit boot code"},
	{0x052691, -EDEV_ILLEGAL_REQUEST,           "Wrong personality firmware image"},
	{0x052693, -EDEV_ILLEGAL_REQUEST,           "Wrong firmware image, checksum error"},
	{0x052904, -EDEV_ILLEGAL_REQUEST,           "Device Internal Reset"},
	{0x052C00, -EDEV_ILLEGAL_REQUEST,           "Command Sequence Error"},
	{0x052C0B, -EDEV_ILLEGAL_REQUEST,           "Not Reserved"},
	{0x053000, -EDEV_ILLEGAL_REQUEST,           "Incompatible Medium Installed"},
	{0x053005, -EDEV_ILLEGAL_REQUEST,           "Cannot Write Medium - Incompatible Format"},
	{0x053900, -EDEV_ILLEGAL_REQUEST,           "Saving Parameters Not Supported"},
	{0x053B00, -EDEV_ILLEGAL_REQUEST,           "Sequential Positioning Error"},
	{0x053B0C, -EDEV_ILLEGAL_REQUEST,           "Position Past Beginning of Medium"},
	{0x053B0D, -EDEV_DEST_FULL,                 "Medium Destination Element Full"},
	{0x053B0E, -EDEV_SRC_EMPTY,                 "Medium Source Element Empty"},
	{0x053B11, -EDEV_MAGAZINE_INACCESSIBLE,     "Medium magazine not accessible"},
	{0x053B12, -EDEV_MAGAZINE_INACCESSIBLE,     "Media magazine not installed."},
	{0x053D00, -EDEV_ILLEGAL_REQUEST,           "Invalid Bits in Identify Message"},
	{0x054900, -EDEV_ILLEGAL_REQUEST,           "Invalid Message Error"},
	{0x055302, -EDEV_MEDIUM_LOCKED,             "Medium Removal Prevented"},
	{0x055303, -EDEV_MEDIUM_LOCKED,             "Drive media removal prevented state set"},
	{0x055508, -EDEV_ILLEGAL_REQUEST,           "Maximum Number of Supplemental Decryption Keys Exceeded"},
	{0x055B03, -EDEV_ILLEGAL_REQUEST,           "Log List Codes Exhausted"},
	{0x057408, -EDEV_ILLEGAL_REQUEST,           "Digital Signature Validation Failure"},
	{0x05740C, -EDEV_ILLEGAL_REQUEST,           "Unable to Decrypt Parameter List"},
	{0x057410, -EDEV_ILLEGAL_REQUEST,           "SA Creation Parameter Value Invalid"},
	{0x057411, -EDEV_ILLEGAL_REQUEST,           "SA Creation Parameter Value Rejected"},
	{0x057412, -EDEV_ILLEGAL_REQUEST,           "Invalid SA Usage"},
	{0x057430, -EDEV_ILLEGAL_REQUEST,           "SA Creation Parameter not Supported"},
	/* Sense Key 6 (Unit Attention) */
	{0x060002, -EDEV_EARLY_WARNING,             "End-of-Partition/Medium Detected, Early Warning"},
	{0x062800, -EDEV_MEDIUM_MAY_BE_CHANGED,     "Not Ready to Ready Transition, Medium May Have Changed"},
	{0x062801, -EDEV_IE_ACCESSED,               "Import or Export Element Accessed"},
	{0x062900, -EDEV_POR_OR_BUS_RESET,          "Power On, Reset, or Bus Device Reset Occurred"},
	{0x062901, -EDEV_POR_OR_BUS_RESET,          "Power on occurred"},
	{0x062902, -EDEV_POR_OR_BUS_RESET,          "SCSI Bus reset occurred"},
	{0x062903, -EDEV_POR_OR_BUS_RESET,          "Internal reset occurred"},
	{0x062904, -EDEV_POR_OR_BUS_RESET,          "Internal reset occurred"},
	{0x062905, -EDEV_UNIT_ATTENTION,            "Transceiver Mode Changed To Single-ended"},
	{0x062906, -EDEV_UNIT_ATTENTION,            "Transceiver Mode Changed To LVD"},
	{0x062A01, -EDEV_CONFIGURE_CHANGED,         "Mode Parameters Changed"},
	{0x062A02, -EDEV_CONFIGURE_CHANGED,         "Mode Parameters Changed"},
	{0x062A03, -EDEV_UNIT_ATTENTION,            "Reservations preempted"},
	{0x062A04, -EDEV_UNIT_ATTENTION,            "Reservations released"},
	{0x062A05, -EDEV_UNIT_ATTENTION,            "Registrations preempted"},
	{0x062A10, -EDEV_CONFIGURE_CHANGED,         "Time stamp changed"},
	{0x062A11, -EDEV_CRYPTO_ERROR,              "Encryption - Data Encryption Parameters Changed by Another I_T Nexus"},
	{0x062A12, -EDEV_CRYPTO_ERROR,              "Encryption - Data Encryption Parameters Changed by Vendor Specific Event"},
	{0x062A14, -EDEV_UNIT_ATTENTION,            "SA Creation Capabilities Data Has Changed"},
	{0x062F00, -EDEV_COMMAND_CLEARED,           "Commands Cleared by Another Initiator"},
	{0x063000, -EDEV_MEDIUM_ERROR,              "Incompatible Medium Installed"},
	{0x063B12, -EDEV_DOOR_CLOSED,               "Medium magazine removed"},
	{0x063B13, -EDEV_DOOR_CLOSED,               "Medium magazine inserted"},
	{0x063F01, -EDEV_CONFIGURE_CHANGED,         "Microcode Has Been Changed"},
	{0x063F02, -EDEV_CONFIGURE_CHANGED,         "Changed Operating Definition"},
	{0x063F03, -EDEV_CONFIGURE_CHANGED,         "Inquiry Data Has Changed"},
	{0x063F05, -EDEV_CONFIGURE_CHANGED,         "Device Identifier Changed"},
	{0x063F0E, -EDEV_CONFIGURE_CHANGED,         "Reported LUNs Data Has Changed"},
	{0x065302, -EDEV_MEDIA_REMOVAL_PREV,        "Media removal prevented"},
	{0x065A01, -EDEV_MEDIUM_REMOVAL_REQ,        "Operator Medium Removal Request"},
	/* Sense Key 7 (Data Protect) */
	{0x072610, -EDEV_CRYPTO_ERROR,              "Encryption - Data Decryption Key Fail Limit"},
	{0x072700, -EDEV_WRITE_PROTECTED,           "Write Protected"},
	{0x072A13, -EDEV_CRYPTO_ERROR,              "Encryption - Data Encryption Key Instance Counter Has Changed"},
	{0x073005, -EDEV_DATA_PROTECT,              "Cannot Write Medium, Incompatible Format"},
	{0x073000, -EDEV_WRITE_PROTECTED_WORM,      "Data Protect/WORM Medium"},
	{0x07300C, -EDEV_WRITE_PROTECTED_WORM,      "Data Protect/WORM Medium - Overwrite Attempted"},
	{0x07300D, -EDEV_WRITE_PROTECTED_WORM,      "Data Protect/WORM Medium - Integrity Check"},
	{0x075001, -EDEV_WRITE_PROTECTED_WORM,      "Write Append Position Error (WORM)"},
	{0x075200, -EDEV_DATA_PROTECT,              "Cartridge Fault"},
	{0x075A02, -EDEV_WRITE_PROTECTED_OPERATOR,  "Data Protect/Operator - Overwrite Attempted"},
	{0x077400, -EDEV_WRITE_PROTECTED_WORM,      "Security Error"},
	{0x077401, -EDEV_CRYPTO_ERROR,              "Encryption - Unable to Decrypt Data"},
	{0x077402, -EDEV_CRYPTO_ERROR,              "Encryption - Unencrypted Data Encountered While Decrypting"},
	{0x077403, -EDEV_CRYPTO_ERROR,              "Encryption - Incorrect Data Encryption Key"},
	{0x077404, -EDEV_CRYPTO_ERROR,              "Encryption - Cryptographic Integrity Validation Failed"},
	{0x077405, -EDEV_CRYPTO_ERROR,              "Encryption - Error Decrypting Data"},
	/* Sense Key 8 (Blank Check) */
	{0x080005, -EDEV_EOD_DETECTED,              "End-of-Data (EOD) Detected"},
	{0x081401, -EDEV_RECORD_NOT_FOUND,          "Record Not Found, Void Tape"},
	{0x081403, -EDEV_EOD_NOT_FOUND,             "End-of-Data (EOD) not found"},
	{0x080B01, -EDEV_OVER_TEMPERATURE,          "The drive detected an overtemperature condition."},
	/* Sense Key B (Aborted Command) */
	{0x0B0E01, -EDEV_ABORTED_COMMAND,           "Information Unit Too Short"},
	{0x0B1400, -EDEV_ABORTED_COMMAND,           "Recorded Entity Not Found"},
	{0x0B1401, -EDEV_ABORTED_COMMAND,           "Record Not Found"},
	{0x0B1402, -EDEV_ABORTED_COMMAND,           "Filemark or Setmark Not Found"},
	{0x0B1B00, -EDEV_ABORTED_COMMAND,           "Synchronous Data Transfer Error"},
	{0x0B3D00, -EDEV_ABORTED_COMMAND,           "Invalid Bits in Identify Message"},
	{0x0B3F0F, -EDEV_ABORTED_COMMAND,           "Echo Buffer Overwritten"},
	{0x0B4100, -EDEV_ABORTED_COMMAND,           "LDI command Failure"},
	{0x0B4300, -EDEV_ABORTED_COMMAND,           "Message Error"},
	{0x0B4400, -EDEV_ABORTED_COMMAND,           "Internal Target Failure"},
	{0x0B4500, -EDEV_ABORTED_COMMAND,           "Select/Reselect Failure"},
	{0x0B4700, -EDEV_ABORTED_COMMAND,           "SCSI Parity Error"},
	{0x0B4703, -EDEV_ABORTED_COMMAND,           "Information Unit iuCRC Error Detected"},
	{0x0B4800, -EDEV_ABORTED_COMMAND,           "Initiator Detected Error Message Received"},
	{0x0B4900, -EDEV_ABORTED_COMMAND,           "Invalid Message Error"},
	{0x0B4A00, -EDEV_ABORTED_COMMAND,           "Command Phase Error"},
	{0x0B4B00, -EDEV_ABORTED_COMMAND,           "Data Phase Error"},
	{0x0B4B02, -EDEV_ABORTED_COMMAND,           "Too Much Write Data"},
	{0x0B4B03, -EDEV_ABORTED_COMMAND,           "ACK/NAK Timeout"},
	{0x0B4B04, -EDEV_ABORTED_COMMAND,           "NAK Received"},
	{0x0B4B05, -EDEV_ABORTED_COMMAND,           "Data Offset Error"},
	{0x0B4B06, -EDEV_TIMEOUT,                   "Initiator Response Timeout"},
	{0x0B4E00, -EDEV_OVERLAPPED,                "Overlapped Commands"},
	{0x0B0801, -EDEV_ABORTED_COMMAND,           "LU Communication - Timeout"},

	/* Sense Key D (Volume Overflow) */
	{0x0D0002, -EDEV_OVERFLOW,                  "End-of-Partition/Medium Detected"},
	/* END MARK*/
	{0xFFFFFF, -EDEV_UNKNOWN,                   "Unknown Error code"},
};

/* IBM LTO tape drive vendor unique sense table */
struct error_table tape_errors[] = {
	/* Sense Key 0 (No Sense) */
	{0x008282, -EDEV_CLEANING_REQUIRED,         "IBM LTO - Cleaning Required"},
	/* Sense Key 1 (Recoverd Error) */
	{0x018252, -EDEV_DEGRADED_MEDIA,            "IBM LTO - Degraded Media"},
	{0x018383, -EDEV_RECOVERED_ERROR,           "Drive Has Been Cleaned"},
	{0x018500, -EDEV_RECOVERED_ERROR,           "Search Match List Limit (warning)"},
	{0x018501, -EDEV_RECOVERED_ERROR,           "Search Snoop Match Found"},
	/* Sense Key 3 (Medium Error) */
	{0x038500, -EDEV_DATA_PROTECT,              "Write Protected Because of Tape or Drive Failure"},
	{0x038501, -EDEV_DATA_PROTECT,              "Write Protected Because of Tape Failure"},
	{0x038502, -EDEV_DATA_PROTECT,              "Write Protected Because of Drive Failure"},
	/* Sense Key 5 (Illegal Request) */
	{0x058000, -EDEV_ILLEGAL_REQUEST,           "CU Mode, Vendor-Unique"},
	{0x058283, -EDEV_ILLEGAL_REQUEST,           "Bad Microcode Detected"},
	{0x058503, -EDEV_ILLEGAL_REQUEST,           "Write Protected Because of Current Tape Position"},
	{0x05A301, -EDEV_ILLEGAL_REQUEST,           "OEM Vendor-Specific"},
	/* Sense Key 6 (Unit Attention) */
	{0x065DFF, -EDEV_UNIT_ATTENTION,            "Failure Prediction False"},
	{0x068283, -EDEV_UNIT_ATTENTION,            "Drive Has Been Cleaned (older versions of microcode)"},
	{0x068500, -EDEV_UNIT_ATTENTION,            "Search Match List Limit (alert)"},
	/* Crypto Related Sense Code */
	{0x00EF13, -EDEV_CRYPTO_ERROR,              "Encryption - Key Translate"},
	{0x03EE60, -EDEV_CRYPTO_ERROR,              "Encryption - Proxy Command Error"},
	{0x03EED0, -EDEV_CRYPTO_ERROR,              "Encryption - Data Read Decryption Failure"},
	{0x03EED1, -EDEV_CRYPTO_ERROR,              "Encryption - Data Read after Write Decryption Failure"},
	{0x03EEE0, -EDEV_CRYPTO_ERROR,              "Encryption - Key Translation Failure"},
	{0x03EEE1, -EDEV_CRYPTO_ERROR,              "Encryption - Key Translation Ambiguous"},
	{0x03EEF0, -EDEV_CRYPTO_ERROR,              "Encryption - Decryption Fenced (Read)"},
	{0x03EEF1, -EDEV_CRYPTO_ERROR,              "Encryption - Encryption Fenced (Write)"},
	{0x044780, -EDEV_HARDWARE_ERROR,            "IBM LTO - Read Internal CRC Error"},
	{0x044781, -EDEV_HARDWARE_ERROR,            "IBM LTO - Write Internal CRC Error"},
	{0x04EE0E, -EDEV_KEY_SERVICE_ERROR,         "Encryption - Key Service Timeout"}, /* LTO5, Jag4 and earlier */
	{0x04EE0F, -EDEV_KEY_SERVICE_ERROR,         "Encryption - Key Service Failure"}, /* LTO5, Jag4 and earlier */
	{0x05EE00, -EDEV_CRYPTO_ERROR,              "Encryption - Key Service Not Enabled"},
	{0x05EE01, -EDEV_CRYPTO_ERROR,              "Encryption - Key Service Not Configured"},
	{0x05EE02, -EDEV_CRYPTO_ERROR,              "Encryption - Key Service Not Available"},
	{0x05EE0D, -EDEV_CRYPTO_ERROR,              "Encryption - Message Content Error"},
	{0x05EE10, -EDEV_CRYPTO_ERROR,              "Encryption - Key Required"},
	{0x05EE20, -EDEV_CRYPTO_ERROR,              "Encryption - Key Count Exceeded"},
	{0x05EE21, -EDEV_CRYPTO_ERROR,              "Encryption - Key Alias Exceeded"},
	{0x05EE22, -EDEV_CRYPTO_ERROR,              "Encryption - Key Reserved"},
	{0x05EE23, -EDEV_CRYPTO_ERROR,              "Encryption - Key Conflict"},
	{0x05EE24, -EDEV_CRYPTO_ERROR,              "Encryption - Key Method Change"},
	{0x05EE25, -EDEV_CRYPTO_ERROR,              "Encryption - Key Format Not Supported"},
	{0x05EE26, -EDEV_CRYPTO_ERROR,              "Encryption - Unauthorized Request - dAK"},
	{0x05EE27, -EDEV_CRYPTO_ERROR,              "Encryption - Unauthorized Request - dSK"},
	{0x05EE28, -EDEV_CRYPTO_ERROR,              "Encryption - Unauthorized Request - eAK"},
	{0x05EE29, -EDEV_CRYPTO_ERROR,              "Encryption - Authentication Failure"},
	{0x05EE2A, -EDEV_CRYPTO_ERROR,              "Encryption - Invalid RDKi"},
	{0x05EE2B, -EDEV_CRYPTO_ERROR,              "Encryption - Key Incorrect"},
	{0x05EE2C, -EDEV_CRYPTO_ERROR,              "Encryption - Key Wrapping Failure"},
	{0x05EE2D, -EDEV_CRYPTO_ERROR,              "Encryption - Sequencing Failure"},
	{0x05EE2E, -EDEV_CRYPTO_ERROR,              "Encryption - Unsupported Type"},
	{0x05EE2F, -EDEV_CRYPTO_ERROR,              "Encryption - New Key Encrypted Write Pending"},
	{0x05EE30, -EDEV_CRYPTO_ERROR,              "Encryption - Prohibited Request"},
	{0x05EE31, -EDEV_CRYPTO_ERROR,              "Encryption - Key Unknown"},
	{0x05EE32, -EDEV_CRYPTO_ERROR,              "Encryption - Unauthorized Request - dCERT"},
	{0x05EE42, -EDEV_CRYPTO_ERROR,              "Encryption - EKM Challenge Pending"},
	{0x05EEE2, -EDEV_CRYPTO_ERROR,              "Encryption - Key Translation Disallowed"},
	{0x05EEFF, -EDEV_CRYPTO_ERROR,              "Encryption - Security Prohibited Function"},
	{0x05EF01, -EDEV_CRYPTO_ERROR,              "Encryption - Key Service Not Configured"},
	{0x06EE11, -EDEV_CRYPTO_ERROR,              "Encryption - Key Generation"},
	{0x06EE12, -EDEV_KEY_CHANGE_DETECTED,       "Encryption - Key Change Detected"},
	{0x06EE13, -EDEV_CRYPTO_ERROR,              "Encryption - Key Translation"},
	{0x06EE18, -EDEV_KEY_CHANGE_DETECTED,       "Encryption - Changed (Read)"},
	{0x06EE19, -EDEV_KEY_CHANGE_DETECTED,       "Encryption - Changed (Write)"},
	{0x06EE40, -EDEV_CRYPTO_ERROR,              "Encryption - EKM Identifier Changed"},
	{0x06EE41, -EDEV_CRYPTO_ERROR,              "Encryption - EKM Challenge Changed"},
	{0x06EE50, -EDEV_CRYPTO_ERROR,              "Encryption - Initiator Identifier Changed"},
	{0x06EE51, -EDEV_CRYPTO_ERROR,              "Encryption - Initiator Response Changed"},
	{0x06EF01, -EDEV_CRYPTO_ERROR,              "Encryption - Key Service Not Configured"},
	{0x06EF10, -EDEV_CRYPTO_ERROR,              "Encryption - Key Required"},
	{0x06EF11, -EDEV_CRYPTO_ERROR,              "Encryption - Key Generation"},
	{0x06EF13, -EDEV_CRYPTO_ERROR,              "Encryption - Key Translation"},
	{0x06EF1A, -EDEV_CRYPTO_ERROR,              "Encryption - Key Optional (i.e., chose encryption enabled/disabled)"},
	{0x07EE0E, -EDEV_KEY_SERVICE_ERROR,         "Encryption - Key Service Timeout"}, /* LTO6, Jag5 and later */
	{0x07EE0F, -EDEV_KEY_SERVICE_ERROR,         "Encryption - Key Service Failure"}, /* LTO6, Jag5 and later */
	{0x07EF10, -EDEV_KEY_REQUIRED,              "Encryption - Key Required"},
	{0x07EF11, -EDEV_CRYPTO_ERROR,              "Encryption - Key Generation"},
	{0x07EF13, -EDEV_CRYPTO_ERROR,              "Encryption - Key Translate"},
	{0x07EF1A, -EDEV_CRYPTO_ERROR,              "Encryption - Key Optional"},
	{0x07EF31, -EDEV_CRYPTO_ERROR,              "Encryption - Key Unknown"},
	{0x07EFC0, -EDEV_CRYPTO_ERROR,              "Encryption - No Operation"},
	/* END MARK*/
	{0xFFFFFF, -EDEV_UNKNOWN,                   "Unknown Error code"},
};

/* IBM libraries vendor unique sense table (Include ITD) */
struct error_table changer_errors[] = {
	/* Sense Key 0 (No Sense) */
	{0x001100, -EDEV_VOLTAG_NOT_READABLE,       "TS3500/TS4500 RES - Voltag not readable (Should be converted to 00/8301)"},
	{0x003003, -EDEV_CLEANING_CART,             "ITD_RES - Media is cleaning cartridge"},
	{0x003083, -EDEV_CLEANING_CART,             "TS3200/TS3100 RES - Media is cleaning cartridge w/o voltag"},
	{0x003B12, -EDEV_DOOR_OPEN,                 "TS3200/TS3100 RES - Magazine Removed"},
	{0x008100, -EDEV_SLOT_UNKNOWN_STATE,        "TS3500/TS4500 RES - Status is Questionable"},
	{0x008200, -EDEV_DRIVE_NOT_PRESENT,         "TS3500/TS4500 RES - Status is Questionable, Drive is not present"},
	{0x008300, -EDEV_NOT_READY,                 "TS3200/TS3100 RES - Element not yet scanned"},
	{0x008301, -EDEV_VOLTAG_NOT_READABLE,       "ITD_RES - Voltag not readable"},
	{0x008302, -EDEV_LOCATION_NOT_PRESENT,      "ITD_RES - Element location is not present"},
	{0x008303, -EDEV_MEDIA_PRESENSE_UNKNOWN,    "ITD_RES - Media presense is not determined"},
	{0x008311, -EDEV_CLEANING_CART,             "ITD_RES - Media present is cleaning cartridge and voltag is not readable."},

	/* Sense Key 2 (Not Ready) */
	{0x020482, -EDEV_NOT_READY,                 "TS3500/TS4500 - Library has not been calibrated"},
	{0x020483, -EDEV_NOT_READY,                 "TS3500/TS4500 - Library has not been set up "
		"TS3200/TS3100 - Door open (Should be converted to 02/8275) "
		"TS3310 - Not ready due to aisle power being disabled (Should be converted to 02/8275)"},
	{0x020484, -EDEV_IE_OPEN,                   "TS3500/TS4500 - I/O Station is open"},
	{0x020485, -EDEV_DOOR_OPEN,                 "TS3500/TS4500 - Door is Open (Should be converted to 02/8275), TS3200/TS3100 - Firmware upgrade in progress"},
	{0x020487, -EDEV_NOT_READY,                 "TS3200/TS3100 - The drive is not enabled"},
	{0x020488, -EDEV_NOT_READY,                 "TS3200/TS3100 - The drive is busy"},
	{0x020489, -EDEV_NOT_READY,                 "TS3200/TS3100 - The drive is not empty"},
	{0x02048D, -EDEV_NOT_READY,                 "TS3310 - The library is not ready because it is offline (Should be converted to 02/8275)"},
	{0x02049A, -EDEV_NOT_READY,                 "TS3200/TS3100 - The drive fibre down"},
	{0x02048E, -EDEV_NOT_READY,                 "TS3200/TS3100 - The media changer is in sequential mode (Should be converted to 02/8272)"},
	{0x028005, -EDEV_NOT_READY,                 "TS2900 - During Reprogramming Mode, New firmware is being downloaded"},
	{0x028272, -EDEV_NOT_READY,                 "ITD - Sequential mode"},
	{0x028274, -EDEV_OFFLINE,                   "ITD - Librray off line"},
	{0x028275, -EDEV_DOOR_OPEN,                 "ITD - Library door open"},
	{0x028276, -EDEV_NOT_READY,                 "ITD - Manual Mode"},
	/* Sense Key 4 (Hardware or Firmware Error) */
	{0x044080, -EDEV_HARDWARE_ERROR,            "TS3310 - Component failure."},
	{0x048000, -EDEV_HARDWARE_ERROR,            "TS3200/TS3100 - Hardware Error"},
	{0x048004, -EDEV_HARDWARE_ERROR,            "TS2900 - Fan Alarm"},
	{0x048007, -EDEV_HARDWARE_ERROR,            "TS2900 - NVRAM Failure"},
	{0x045382, -EDEV_HARDWARE_ERROR,            "TS3310 - Cannot lock the I/O Station."},
	{0x045383, -EDEV_HARDWARE_ERROR,            "TS3310 - Cannot unlock the I/O Station."},
	{0x048300, -EDEV_HARDWARE_ERROR,            "TS3310 - Label too short or too long."},

	/* Sense Key 5 (Illegal Request) */
	{0x053B80, -EDEV_ILLEGAL_REQUEST,           "TS3500/TS4500 - Medium Transport Element Full (Should be converted to 05/8273)"},
	{0x053B81, -EDEV_ILLEGAL_REQUEST,           "TS3500/TS4500 - Element Not Accessible, Cartridge Present is Assigned to Another Logical Library"},
	{0x053B82, -EDEV_ILLEGAL_REQUEST,           "TS3500/TS4500 - Element Not Accessible, Drive is Not Present"},
	{0x053B83, -EDEV_ILLEGAL_REQUEST,           "TS2900 - Source drive not unloaded"},
	{0x053BA0, -EDEV_ILLEGAL_REQUEST,           "TS3200/TS3100 - Medium transfer element full "
		"TS3310 - Media type does not match destination media type (Should be converted to 05/3000"},
	{0x054480, -EDEV_ILLEGAL_REQUEST,           "TS3200/TS3100 - Bad status library controller"},
	{0x054481, -EDEV_ILLEGAL_REQUEST,           "TS3200/TS3100 - Source not ready"},
	{0x054482, -EDEV_ILLEGAL_REQUEST,           "TS3200/TS3100 - Destination not ready"},
	{0x054483, -EDEV_ILLEGAL_REQUEST,           "TS3200/TS3100 - Cannot make reservation"},
	{0x054484, -EDEV_ILLEGAL_REQUEST,           "TS3200/TS3100 - Wrong drive type"},
	{0x054485, -EDEV_ILLEGAL_REQUEST,           "TS3200/TS3100 - Invalid slave robotic controller request"},
	{0x054486, -EDEV_ILLEGAL_REQUEST,           "TS3200/TS3100 - Accessor not initialized"},
	{0x055381, -EDEV_ILLEGAL_REQUEST,           "TS3310 - I/O Station door is open. (RES-> 8301)"},
	{0x058302, -EDEV_ILLEGAL_REQUEST,           "TS3310 - Barcode label questionable (COM->05/8273, RES->00/8301)"},
	{0x058303, -EDEV_ILLEGAL_REQUEST,           "TS3310 - Cell status and bar code label questionable."},
	{0x058304, -EDEV_DRIVE_NOT_PRESENT,         "TS3310 - Data transfer element not installed."},
	{0x058305, -EDEV_ILLEGAL_REQUEST,           "TS3310 - Data transfer element is varied off and not accessible for library operations. (RES->00/8302)"},
	{0x058306, -EDEV_ILLEGAL_REQUEST,           "TS3310 - Element is contained within an offline tower or I/O Station and is not accessible for library operations. COM->05/3B11 RES->00/8302)"},
	{0x058010, -EDEV_ILLEGAL_REQUEST,           "TS2900 - Drive Failure"},
	{0x058273, -EDEV_ILLEGAL_REQUEST,           "ITD - Medium Transport Element Full"},
	/* Sense Key 6 (Unit Attention) */
	{0x0641FE, -EDEV_UNIT_ATTENTION,            "TS2900 - Drive Error Meassage Detected"},
	{0x068270, -EDEV_IE_ACCESSED,               "ITD - Library inventory changed"},
	{0x068271, -EDEV_IE_ACCESSED,               "ITD - Library inventory changed"},
	/* Sense Key B (Aborted Command) */
	{0x0B0880, -EDEV_ABORTED_COMMAND,           "TS3310 - SCSI failure"},
	{0x0B0882, -EDEV_ABORTED_COMMAND,           "TS3310 - SCSI command execution or queuing failure"},
	{0x0B0883, -EDEV_ABORTED_COMMAND,           "TS3310 - SCSI command failed"},
	{0x0B0884, -EDEV_ABORTED_COMMAND,           "TS3310 - SCSI time-out"},
	{0x0B0885, -EDEV_ABORTED_COMMAND,           "TS3310 - SCSI autosense failed"},
	{0x0B0886, -EDEV_ABORTED_COMMAND,           "TS3310 - SCSI aborted"},
	{0x0B0887, -EDEV_ABORTED_COMMAND,           "TS3310 - SCSI abort failed"},
	{0x0B0888, -EDEV_ABORTED_COMMAND,           "TS3310 - SCSI status failed"},
	{0x0B08B0, -EDEV_ABORTED_COMMAND,           "TS3310 - FC data underrun"},
	{0x0B08B1, -EDEV_ABORTED_COMMAND,           "TS3310 - FC DMA error"},
	{0x0B08B2, -EDEV_ABORTED_COMMAND,           "TS3310 - FC reset"},
	{0x0B08B3, -EDEV_ABORTED_COMMAND,           "TS3310 - FC data overrun"},
	{0x0B08B4, -EDEV_ABORTED_COMMAND,           "TS3310 - FC queue full"},
	{0x0B08B5, -EDEV_ABORTED_COMMAND,           "TS3310 - Port unavailable"},
	{0x0B08B6, -EDEV_ABORTED_COMMAND,           "TS3310 - Port logged out"},
	{0x0B08B7, -EDEV_ABORTED_COMMAND,           "TS3310 - Port configuration changed"},
	/* END MARK*/
	{0xFFFFFF, -EDEV_UNKNOWN,                   "Unknown Error code"},
};


/**
 * Functions
 */

static int _sense2errorcode(uint32_t sense, struct error_table *table, char **msg, uint32_t mask)
{
	int rc = -EDEV_UNKNOWN;
	int i;

	if (msg) {
		*msg = NULL;
	}

	if ((sense & 0xFFFF00) == 0x044000) {
		sense = 0x044000;
	}
	else if ((sense & 0xFFF000) == 0x048000) { /* 04/8xxx in TS3100/TS3200 */
		sense = 0x048000;
	}
	else if ((sense & 0xFFF000) == 0x0B4100) { /* 0B/41xx in TS2900 */
		sense = 0x0B4100;
	}

	if ((sense & 0x00FF00) >= 0x0008000 || (sense & 0x0000FF) >= 0x000080) {
		rc = -EDEV_VENDOR_UNIQUE;
	}

	i = 0;

	while (table[i].sense != 0xFFFFFF) {
		if ((table[i].sense & mask) == (sense & mask)) {
			rc = table[i].err_code;
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

int iokit_sense2errno(iokit_device_t *device, char **msg)
{
	SCSI_Sense_Data *sense = &device->lastSenseData;
	uint32_t        sense_value = 0;
	int             rc = -EDEV_UNKNOWN;

	sense_value += (uint32_t) ((sense->SENSE_KEY & kSENSE_KEY_Mask) & 0x0F) << 16;
	sense_value += (uint32_t) (sense->ADDITIONAL_SENSE_CODE) << 8;
	sense_value += (uint32_t) sense->ADDITIONAL_SENSE_CODE_QUALIFIER;

	rc = _sense2errorcode(sense_value, standard_table, msg, MASK_WITH_SENSE_KEY);
	/* NOTE: error table must be changed in library edition */
	if (rc == -EDEV_VENDOR_UNIQUE)
		rc = _sense2errorcode(sense_value, tape_errors, msg, MASK_WITH_SENSE_KEY);

	return rc;
}


static bool is_expected_error(iokit_device_t *device, uint8_t *cdb, int32_t rc )
{
	int cmd = (cdb[0]&0xFF);
	uint64_t destination;
	uint64_t cdb_dest[8];
	int i;

	switch (cmd) {
		case (0x00):	// test unit ready
			if (rc == -EDEV_NEED_INITIALIZE || rc == -EDEV_CONFIGURE_CHANGED)
				return true;
			break;
		case (0x08):	// read
			if (rc == -EDEV_FILEMARK_DETECTED || rc == -EDEV_NO_SENSE || rc == -EDEV_CLEANING_REQUIRED)
				return true;
			if ((rc == -EDEV_CRYPTO_ERROR || rc == -EDEV_KEY_REQUIRED) && !device->is_data_key_set)
				return true;
			break;
		case (0x0A):	// write
			if (rc == -EDEV_EARLY_WARNING || rc == -EDEV_PROG_EARLY_WARNING || rc == -EDEV_CLEANING_REQUIRED)
				return true;
			break;
		case (0x10):	// write fm
			if (rc == -EDEV_EARLY_WARNING || rc == -EDEV_PROG_EARLY_WARNING || rc == -EDEV_CLEANING_REQUIRED)
				return true;
			break;
		case (0x1B):	// load/unload
			if ((cdb[4] & 0x01) == 0)	// unload
				if (rc == -EDEV_CLEANING_REQUIRED)
					return true;
			break;
		case (0x55):	// mode select
			if (rc == -EDEV_MODE_PARAMETER_ROUNDED)
				return true;
			break;
		case (0x92):	// locate
			for (i=0; i<8; i++)
				cdb_dest[i] = (uint64_t)cdb[i+4] & 0xff;

			destination = (cdb_dest[0] << 56) + (cdb_dest[1] << 48)
			+ (cdb_dest[2] << 40) + (cdb_dest[3] << 32)
			+ (cdb_dest[4] << 24) + (cdb_dest[5] << 16)
			+ (cdb_dest[6] << 8) + cdb_dest[7];

			if (destination == TAPE_BLOCK_MAX && rc == -EDEV_EOD_DETECTED)
				return true;
			break;
	}

	return false;
}


int32_t iokit_obtain_exclusive_access( iokit_device_t *device )
{
	int32_t ret = -EDEV_UNKNOWN;

	IOReturn scsiResult = IO_OBJECT_NULL;
	scsiResult = (*device->scsiTaskInterface)->ObtainExclusiveAccess(device->scsiTaskInterface);

	switch (scsiResult) {
		case kIOReturnSuccess:
			/* Got exclusive access */
			device->exclusive_lock = true;
			ret = DEVICE_GOOD;
			break;
		case kIOReturnBusy:
			ret = -EDEV_DEVICE_BUSY;
			break;
		default:
			ret = -EDEV_DEVICE_UNOPENABLE;
			break;
	}

out:
	return ret;
}

int32_t iokit_release_exclusive_access( iokit_device_t *device )
{
	int32_t ret = -1;

	IOReturn scsiResult = IO_OBJECT_NULL;
	scsiResult = (*device->scsiTaskInterface)->ReleaseExclusiveAccess(device->scsiTaskInterface);

	switch (scsiResult) {
		case kIOReturnSuccess:
			/* Released exclusive access */
			device->exclusive_lock = false;
			ret = 0;
			break;
		default:
			ret = -1;
			break;
	}

out:
	return ret;
}


int32_t iokit_allocate_scsitask( iokit_device_t *device )
{
	if(device == NULL) {
		errno = EFAULT;
		return -100;
	}

	if(device->task == NULL) {
		// Create a SCSI task for the device. This task will be re-used for all future SCSI operations.
		device->task = (*device->scsiTaskInterface)->CreateSCSITask(device->scsiTaskInterface);

		if (device->task == NULL) {
			errno = EPERM;
			return -101;
		}
	}

	return 0;
}


void iokit_release_scsitask( iokit_device_t *device)
{
	if(device != NULL) {
		// Release IOKit task interface
		if(device->scsiTaskInterface != NULL)
			(*device->scsiTaskInterface)->Release(device->scsiTaskInterface);
	}
}

int32_t iokit_issue_cdb_command( iokit_device_t *device,
								 uint8_t *cdb,
								 uint8_t cdbSize,
								 void *buffer,
								 size_t bufferSize,
								 uint8_t transferDirection,
								 uint32_t timeout,
								 char *commandDescription,
								 char **msg )
{

	int32_t ret = -1;

    IOReturn kernelReturn = kIOReturnSuccess;
    IOVirtualRange *range = NULL;
    uint64_t transferCount = 0;
    uint32_t transferCountHi = 0;
    uint32_t transferCountLo = 0;

	assert(device->scsiTaskInterface != NULL);

	ret = iokit_allocate_scsitask(device);
	if(ret != 0) {
		ret = -EDEV_INTERNAL_ERROR;
		if (msg) {
			*msg = "Error on allocating scsi task";
		}
		goto out;
	}

    // Zero the senseData, clear cache of last task status, and reset last transfer count
    bzero(&device->lastSenseData, sizeof(device->lastSenseData));
    device->lastTaskStatus = -1;
    device->lastTransferCount = -1;

	if( (buffer != NULL) && (bufferSize > 0) ) {
		// Allocate a virtual range for the buffer. If we had more than 1 scatter-gather entry,
		// we would allocate more than 1 IOVirtualRange.
		range = (IOVirtualRange *) malloc(sizeof(IOVirtualRange));
    	if (range == NULL) {
        	errno = ENOMEM;
			ret = -EDEV_NO_MEMORY;
			if (msg) {
				*msg = "Error on allocating virtual range for the buffer";
			}
        	goto free;
    	}

    	// Set up the range. The address is just the buffer's address. The length is our request size.
    	range->address = (IOVirtualAddress) buffer;
    	range->length = bufferSize;

    	// Set the scatter-gather entry in the task.
    	kernelReturn = (*device->task)->SetScatterGatherEntries(device->task, range, 1, bufferSize, transferDirection);
    	if (kernelReturn != kIOReturnSuccess) {
			errno = EIO;
        	ret = -EDEV_DRIVER_ERROR;
			if (msg) {
				*msg = "Error setting scatter-gather entries";
			}
        	goto free;
    	}
	}

    // Set the actual cdb in the task.
    kernelReturn = (*device->task)->SetCommandDescriptorBlock(device->task, (UInt8 *)cdb, cdbSize);

    if (kernelReturn != kIOReturnSuccess) {
    	errno = EIO;
        ret = -EDEV_DRIVER_ERROR;
		if (msg) {
			*msg = "Error setting CDB";
		}
        goto free;
    }

    // Set the timeout in the task
    kernelReturn = (*device->task)->SetTimeoutDuration(device->task, timeout);
    if (kernelReturn != kIOReturnSuccess) {
    	errno = EIO;
        ret = -EDEV_DRIVER_ERROR;
		if (msg) {
			*msg = "Error setting timeout";
		}
        goto free;
    }

    kernelReturn = (*device->task)->ExecuteTaskSync(device->task, &device->lastSenseData, &device->lastTaskStatus, &transferCount);

    device->lastTransferCount = transferCount;
    device->resid = bufferSize - transferCount;

    if( (buffer != NULL) && (bufferSize > 0) ) {
    	// Get the transfer counts
    	transferCountHi = ((transferCount >> 32) & 0xFFFFFFFF);
    	transferCountLo = (transferCount & 0xFFFFFFFF);
    }

    switch(device->lastTaskStatus) {
    	case kSCSITaskStatus_GOOD:
			ret = DEVICE_GOOD;
			if (msg) {
				*msg = "Command successed";
			}
    		break;
    	case kSCSITaskStatus_CHECK_CONDITION:
			ret = iokit_sense2errno(device, msg);
            break;
    	default:
        	errno = -EIO;
			ret = -EDEV_DRIVER_ERROR;
			ltfsmsg(LTFS_DEBUG, "12197D");
			if (msg) {
				*msg = "CDB command returned error but no sense";
			}
            break;
    }

free:
	if (ret != DEVICE_GOOD) {
		if (is_expected_error(device, cdb, ret)) {
			ltfsmsg(LTFS_DEBUG, "12198D", commandDescription, cdb[0], ret);
		}
		else {
			ltfsmsg(LTFS_INFO, "12196I", commandDescription, cdb[0], ret, device->device_name);
		}
	}

    // Be a good citizen and clean up.
    free(range);

    // Clean up task for future use
    (*device->task)->SetTimeoutDuration(device->task, 0);
    (*device->task)->SetCommandDescriptorBlock(device->task, (UInt8 *)NULL, 0);
    (*device->task)->SetScatterGatherEntries(device->task, NULL, 0, 0, kSCSIDataTransfer_NoDataTransfer);

out:
	return ret;
}


int32_t iokit_passthrough( iokit_device_t *device,
						   cdb_pass_through *passthrough, char **msg )
{
	int32_t ret = -1;

	char command_description[COMMAND_DESCRIPTION_LENGTH] = "PASSTHROUGH";
	uint8_t transfer_direction;

	strcat(command_description, " - ");
	strcat(command_description, passthrough->operation_descriptor);

	switch(passthrough->data_direction) {
		case SCSI_FROM_INITIATOR_TO_TARGET:
			transfer_direction = kSCSIDataTransfer_FromInitiatorToTarget;
			break;
		case SCSI_FROM_TARGET_TO_INITIATOR:
			transfer_direction = kSCSIDataTransfer_FromTargetToInitiator;
			break;
		case SCSI_NO_DATA_TRANSFER:
			transfer_direction = kSCSIDataTransfer_NoDataTransfer;
			break;
		default:
			goto out;
			break;
	}

	ret = iokit_issue_cdb_command(device, passthrough->cdb, passthrough->cmd_length,
								  passthrough->buffer, passthrough->buffer_length,
								  transfer_direction, passthrough->timeout,
								  command_description, msg);

	passthrough->result = ret;
	passthrough->buffer_length = device->lastTransferCount;

	passthrough->check_condition = (device->lastTaskStatus == kSCSITaskStatus_CHECK_CONDITION);
	passthrough->sense_valid = (device->lastSenseData.VALID_RESPONSE_CODE & kSENSE_DATA_VALID_Mask)
							   == kSENSE_DATA_VALID;

	if(ret == 0) {
		passthrough->sense_length = 0;
	} else {
		if(sizeof(SCSI_Sense_Data) <= passthrough->sense_length) {
			memcpy(passthrough->sense, &device->lastSenseData, sizeof(SCSI_Sense_Data));
			passthrough->sense_length = sizeof(SCSI_Sense_Data);
		} else {
			ret = -EDEV_NO_MEMORY;
		}
	}

out:
	return ret;
}


int32_t iokit_get_ssc_device_count()
{
	int32_t count = -1;
	CFMutableDictionaryRef matchingDict = NULL;

	_create_matching_dictionary_for_ssc(&matchingDict);
	count = _get_device_count(&matchingDict);

	return count;
}


int32_t iokit_get_smc_device_count()
{
	int32_t count = -1;
	CFMutableDictionaryRef matchingDict = NULL;

	_create_matching_dictionary_for_smc(&matchingDict);
	count = _get_device_count(&matchingDict);

	return count;
}


int32_t iokit_find_ssc_device( iokit_device_t *device,
							   int32_t drive_number )
{
	int32_t ret = -1;
    CFMutableDictionaryRef matchingDict = NULL;

    _create_matching_dictionary_for_ssc(&matchingDict);
    ret = _find_device(device, drive_number, &matchingDict);
	return ret;
}


int32_t iokit_find_smc_device( iokit_device_t *device,
							   int32_t changer_number )
{
	int32_t ret = -1;
    CFMutableDictionaryRef matchingDict = NULL;

    _create_matching_dictionary_for_smc(&matchingDict);
    ret = _find_device(device, changer_number, &matchingDict);
	return ret;
}


int32_t iokit_free_device( iokit_device_t *device )
{
	int32_t ret = 0;
	kern_return_t kernelResult;

	if(device == NULL)
		goto out;

	iokit_release_scsitask(device);

	// Release PlugInInterface
	if(device->plugInInterface != NULL) {
		kernelResult = IODestroyPlugInInterface(device->plugInInterface);
		if(kernelResult != kIOReturnSuccess) {
			ret = -100;
		}
	}

	// Release SCSI object from I/O Registry.
	kernelResult = IOObjectRelease(device->ioservice);
	if(kernelResult != kIOReturnSuccess) {
		ret = -101;
	}

out:
	return ret;
}


int32_t _get_device_count( CFMutableDictionaryRef *matchingDict )
{

	int32_t count = -1;
	kern_return_t kernelResult;

	mach_port_t masterPort = kIOMasterPortDefault;
	io_iterator_t serviceIterator = IO_OBJECT_NULL;

	if (matchingDict == NULL) {
		count = -100;
		goto out;
	}

	// Search I/O Registry for matching devices
	kernelResult = IOServiceGetMatchingServices(masterPort, *matchingDict, &serviceIterator);

	if( (serviceIterator == IO_OBJECT_NULL) || (IOIteratorNext(serviceIterator) == 0) ) {
		count = -101;
		goto out;
	}

	if(serviceIterator && kernelResult == kIOReturnSuccess) {
		io_service_t scsiDevice = IO_OBJECT_NULL;
		count = 0;

		IOIteratorReset(serviceIterator);

		if(! IOIteratorIsValid(serviceIterator)) {
			count = -102;
			goto out;
		}

		// Count devices matching service class
		while( (scsiDevice = IOIteratorNext(serviceIterator)) ) {
			count++;
		}

		IOIteratorReset(serviceIterator);
		while( (scsiDevice = IOIteratorNext(serviceIterator)) ) {
			kernelResult = IOObjectRelease(scsiDevice); // Done with SCSI object from I/O Registry.
		}
	}

out:
	return count;
}

void _cfdataptr2int(const uint8_t *dataPtr, uint64_t *result)
{
    *result =  (uint64_t)*dataPtr       << 56;
    *result += (uint64_t)*(dataPtr + 1) << 48;
    *result += (uint64_t)*(dataPtr + 2) << 40;
    *result += (uint64_t)*(dataPtr + 3) << 32;
    *result += (uint64_t)*(dataPtr + 4) << 24;
    *result += (uint64_t)*(dataPtr + 5) << 16;
    *result += (uint64_t)*(dataPtr + 6) << 8;
    *result += (uint64_t)*(dataPtr + 7);
}


int32_t _find_device( iokit_device_t *device, int32_t device_number, CFMutableDictionaryRef *matchingDict)
{

	int32_t ret = -1;
    kern_return_t kernelResult;

    device->masterPort = kIOMasterPortDefault;

    if (matchingDict == NULL) {
        ret = -100;
        goto out;
    }

    io_iterator_t serviceIterator = IO_OBJECT_NULL;

    // Search I/O Registry for matching devices
    kernelResult = IOServiceGetMatchingServices(device->masterPort, *matchingDict, &serviceIterator);

    if( (serviceIterator == IO_OBJECT_NULL) || (IOIteratorNext(serviceIterator) == 0) ) {
        ret = -101;
        goto out;
    }

    if(serviceIterator && kernelResult == kIOReturnSuccess) {
        io_service_t scsiDevice = IO_OBJECT_NULL;
        int32_t count = 0;

        IOIteratorReset(serviceIterator);

        // Select N'th tape drive based on driveNumber value
        while( (scsiDevice = IOIteratorNext(serviceIterator)) ) {
            if(count == device_number) {
                break;
            } else {
                kernelResult = IOObjectRelease(scsiDevice); // Done with SCSI object from I/O Registry.
                count++;
            }
        }

        if(scsiDevice == IO_OBJECT_NULL) {
            ret = -1;
            goto out;
        }

        CFTypeRef cf = IORegistryEntryCreateCFProperty(scsiDevice, CFSTR(kIOPropertyProtocolCharacteristicsKey), kCFAllocatorDefault, 0);
        if (cf != NULL) {
            CFNumberRef nr;
            CFDataRef dr;

            nr = CFDictionaryGetValue(cf, CFSTR(kIOPropertySCSIDomainIdentifierKey));
            if (nr != NULL) {
                CFNumberGetValue(nr, kCFNumberSInt32Type, &device->domain_id);
            }

            nr = CFDictionaryGetValue(cf, CFSTR(kIOPropertySCSITargetIdentifierKey));
            if (nr != NULL) {
                CFNumberGetValue(nr, kCFNumberSInt32Type, &device->target_id);
            }

            nr = CFDictionaryGetValue(cf, CFSTR(kIOPropertySCSILogicalUnitNumberKey));
            if (nr != NULL) {
                CFNumberGetValue(nr, kCFNumberSInt32Type, &device->lun);
            }

            dr = CFDictionaryGetValue(cf, CFSTR(kIOPropertyFibreChannelNodeWorldWideNameKey));
            if (dr != NULL) {
                const UInt8 *name = CFDataGetBytePtr(dr);
                _cfdataptr2int(name, &device->wwnn);
            }

            dr = CFDictionaryGetValue(cf, CFSTR(kIOPropertyFibreChannelPortWorldWideNameKey));
            if (dr != NULL) {
                const UInt8 *name = CFDataGetBytePtr(dr);
                _cfdataptr2int(name, &device->wwpn);
            }

            CFRelease(cf);
        }

        device->ioservice = IO_OBJECT_NULL;
        device->ioservice = scsiDevice;

        // Create DeviceInterface and store in lto_osx_data struct.
        assert(device->ioservice != IO_OBJECT_NULL);
        IOCFPlugInInterface        **plugin_interface = NULL;
        SCSITaskDeviceInterface    **task_device_interface = NULL;
        HRESULT                    plugin_query_result = S_OK;
        SInt32 score = 0;

        kernelResult = IOCreatePlugInInterfaceForService(device->ioservice,
                                                         kIOSCSITaskDeviceUserClientTypeID,
                                                         kIOCFPlugInInterfaceID,
                                                         &plugin_interface,
                                                         &score);
        if (kernelResult != kIOReturnSuccess) {
            ret = -1;
            goto out;
        } else {
            // Query the base plugin interface for an instance of the specific SCSI device interface
            // object.
            plugin_query_result = (*plugin_interface)->QueryInterface(plugin_interface,
                                                                      CFUUIDGetUUIDBytes(kIOSCSITaskDeviceInterfaceID),
                                                                      (LPVOID *) &task_device_interface);

            if (plugin_query_result != S_OK) {
                ret = -2;
                goto out;
            }
        }

        // Set the return values.
        device->plugInInterface = plugin_interface;
        device->scsiTaskInterface = task_device_interface;
        device->task = NULL;
        ret = 0;
    }

out:
    return ret;
}


void _create_matching_dictionary_for_device_class( CFMutableDictionaryRef *matchingDict,
												   SInt32 peripheralDeviceType )
{

    CFMutableDictionaryRef subDictionary;

    assert(matchingDict != NULL);

    // Create the matching dictionaries...
    *matchingDict = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                                                    &kCFTypeDictionaryValueCallBacks);
    if(*matchingDict != NULL) {
        // Create a sub-dictionary to hold the required device patterns.
        subDictionary = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                                                    &kCFTypeDictionaryValueCallBacks);

        if (subDictionary != NULL) {
            // Set the "SCSITaskDeviceCategory" key so that we match
            // devices that understand SCSI commands.
            CFDictionarySetValue(subDictionary, CFSTR(kIOPropertySCSITaskDeviceCategory),
                                                 CFSTR(kIOPropertySCSITaskUserClientDevice));
            // Set the "PeripheralDeviceType" key so that we match
            // sequential storage (tape) devices.
            SInt32 deviceTypeNumber = peripheralDeviceType;
            CFNumberRef deviceTypeRef = NULL;
            deviceTypeRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &deviceTypeNumber);
            CFDictionarySetValue(subDictionary, CFSTR(kIOPropertySCSIPeripheralDeviceType), deviceTypeRef);
            CFRelease (deviceTypeRef);
        }

        // Add the sub-dictionary pattern to the main dictionary with the key "IOPropertyMatch" to
        // narrow the search to the above dictionary.
        CFDictionarySetValue(*matchingDict, CFSTR(kIOPropertyMatchKey), subDictionary);

        CFRelease(subDictionary);
    }
}


void _create_matching_dictionary_for_ssc( CFMutableDictionaryRef *matchingDict )
{
    _create_matching_dictionary_for_device_class(matchingDict, kINQUIRY_PERIPHERAL_TYPE_SequentialAccessSSCDevice);
}


void _create_matching_dictionary_for_smc( CFMutableDictionaryRef *matchingDict )
{
    _create_matching_dictionary_for_device_class(matchingDict, kINQUIRY_PERIPHERAL_TYPE_MediumChangerSMCDevice);
}
