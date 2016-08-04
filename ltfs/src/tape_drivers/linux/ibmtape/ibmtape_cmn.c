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
** FILE NAME:       tape_drivers/linux/ibmtape/ibmtape_cmn.c
**
** DESCRIPTION:     Implements common operations for IBM tape devices.
**
** AUTHOR:          Atsushi Abe
**                  IBM Yamato, Japan
**                  PISTE@jp.ibm.com
**
*************************************************************************************
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include "ltfs_copyright.h"
#include "libltfs/ltfslogging.h"
#include "ibmtape_cmn.h"

volatile char *copyright = LTFS_COPYRIGHT_0"\n"LTFS_COPYRIGHT_1"\n"LTFS_COPYRIGHT_2"\n";

struct supported_device *supported_devices[] = {
		DEVICE( "ULTRIUM-TD5     ", IBM_3580, DRIVE_LTO5,    " [ULTRIUM-TD5] "), /* IBM Ultrium Gen 5 */
		DEVICE( "ULT3580-TD5     ", IBM_3580, DRIVE_LTO5,    " [ULT3580-TD5] "), /* IBM Ultrium Gen 5 */
		DEVICE( "ULTRIUM-HH5     ", IBM_3580, DRIVE_LTO5_HH, " [ULTRIUM-HH5] "), /* IBM Ultrium Gen 5 Half-High */
		DEVICE( "ULT3580-HH5     ", IBM_3580, DRIVE_LTO5_HH, " [ULT3580-HH5] "), /* IBM Ultrium Gen 5 Half-High */
		DEVICE( "HH LTO Gen 5    ", IBM_3580, DRIVE_LTO5_HH, " [HH LTO Gen 5]"), /* IBM Ultrium Gen 5 Half-High */
		DEVICE( "ULTRIUM-TD6     ", IBM_3580, DRIVE_LTO6,    " [ULTRIUM-TD6] "), /* IBM Ultrium Gen 6 */
		DEVICE( "ULT3580-TD6     ", IBM_3580, DRIVE_LTO6,    " [ULT3580-TD6] "), /* IBM Ultrium Gen 6 */
		DEVICE( "ULTRIUM-HH6     ", IBM_3580, DRIVE_LTO6_HH, " [ULTRIUM-HH6] "), /* IBM Ultrium Gen 6 Half-High */
		DEVICE( "ULT3580-HH6     ", IBM_3580, DRIVE_LTO6_HH, " [ULT3580-HH6] "), /* IBM Ultrium Gen 6 Half-High */
		DEVICE( "HH LTO Gen 6    ", IBM_3580, DRIVE_LTO6_HH, " [HH LTO Gen 6]"), /* IBM Ultrium Gen 6 Half-High */
		DEVICE( "03592E07        ", IBM_3592, DRIVE_TS1140,  " [03592E07]    "), /* TS1140 */
		DEVICE( "03592E08        ", IBM_3592, DRIVE_TS1150,  " [03592E08]    "), /* TS1150 */
		/* End of supported_devices */
		NULL
};

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
struct error_table ibmtape_tc_errors[] = {
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
struct error_table ibmtape_cc_errors[] = {
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

/* Global Functions */

int ibmtape_ioctlrc2err(void *device, int fd, struct request_sense *sense_data, char **msg)
{
	struct ibmtape_data *priv = NULL;
	int sense = 0;
	int rc, rc_sense;

	memset(sense_data, 0, sizeof(struct request_sense));
	rc_sense = ioctl(fd, SIOC_REQSENSE, sense_data);

	if (rc_sense == 0) {
		if (!sense_data->err_code) {
			ltfsmsg(LTFS_DEBUG, "12197D");

			if (msg)
				*msg = "Driver Error";
			rc = -EDEV_DRIVER_ERROR;
		}
		else {
			ltfsmsg(LTFS_DEBUG, "12194D", sense_data->key, sense_data->asc, sense_data->ascq);
			ltfsmsg(LTFS_DEBUG, "12195D", sense_data->vendor[27], sense_data->vendor[28], sense_data->vendor[29], sense_data->vendor[30],
										((struct ibmtape_data *) device)->drive_serial);

			sense += (uint32_t) sense_data->key << 16;
			sense += (uint32_t) sense_data->asc << 8;
			sense += (uint32_t) sense_data->ascq;

			rc = _sense2errcode(sense, standard_table, msg, MASK_WITH_SENSE_KEY);

			if (rc == -EDEV_VENDOR_UNIQUE) {
				priv = (struct ibmtape_data *)device;

				if (priv->type == DEVICE_CHANGER) {
					/* Convert sense code to ITD sense code when the device type is the changer */
					sense = ibmtape_conv_itd(sense, priv->itd_command, priv->itd_command_size);
					rc = _sense2errcode(sense, ibmtape_cc_errors, msg, MASK_WITH_SENSE_KEY);
				}
				else {
					rc = _sense2errcode(sense, ibmtape_tc_errors, msg, MASK_WITH_SENSE_KEY);
				}
			}
		}
	}
	else {
		ltfsmsg(LTFS_INFO, "12099I", rc_sense);
		if (msg)
			*msg = "Cannot get sense information";

		rc = -EDEV_CANNOT_GET_SENSE;
	}

	return rc;
}

static bool is_expected_error(int cmd, void *param, int rc)
{
	switch (cmd) {
	case SIOC_TEST_UNIT_READY:
		if (rc == -EDEV_NEED_INITIALIZE || rc == -EDEV_CONFIGURE_CHANGED) {
			return true;
		}
		break;
	case STIOC_LOCATE_16:
		if (((struct set_tape_position *)param)->logical_id == TAPE_BLOCK_MAX && rc == -EDEV_EOD_DETECTED) {
			return true;
		}
		break;
	case STIOC_SET_ACTIVE_PARTITION:
		if (((struct set_active_partition *)param)->logical_block_id == TAPE_BLOCK_MAX && rc == -EDEV_EOD_DETECTED) {
			return true;
		}
		break;
	}

	return false;
}

int _sioc_stioc_command(void *device, int cmd, char *cmd_name, void *param, char **msg)
{
	int fd = *((int *) device);
	int rc;
	struct request_sense sense_data;

	rc = ioctl(fd, cmd, param);

	if (rc != 0) {
		rc = ibmtape_ioctlrc2err(device, fd, &sense_data, msg);

		if (is_expected_error(cmd, param, rc)) {
			ltfsmsg(LTFS_DEBUG, "12198D", cmd_name, cmd, rc);
		}
		else {
			ltfsmsg(LTFS_INFO, "12196I", cmd_name, cmd, rc, errno, ((struct ibmtape_data *) device)->drive_serial);
		}
	}
	else {
		*msg = "Command succeeded";
		rc = DEVICE_GOOD;
	}

	return rc;
}

int ibmtape_check_lin_tape_version(void)
{
	int fd_version;
	char lin_tape_version[64], *tmp;
	int digit;
	int version_num[3];
	int version_base[3];
	static char base_lin_tape_version[] = "2.1.0";

	memset(lin_tape_version, 0, sizeof(lin_tape_version));

	fd_version = open("/sys/module/lin_tape/version", O_RDONLY);

	if (fd_version == -1) {
		ltfsmsg(LTFS_WARN, "12164W");
	} else {
		read(fd_version, lin_tape_version, sizeof(lin_tape_version));
		if ((tmp = strchr(lin_tape_version, '\n')) != NULL)
			*tmp = '\0';
		ltfsmsg(LTFS_INFO, "12165I", lin_tape_version);
		close(fd_version);
	}

	(void)sscanf(base_lin_tape_version, "%d.%d.%d", &version_base[0], &version_base[1], &version_base[2]);
	digit = sscanf(lin_tape_version, "%d.%d.%d", &version_num[0], &version_num[1], &version_num[2]);

	if (digit != 3
		|| (version_num[0] < version_base[0])
		|| (version_num[0] == version_base[0] && version_num[1] < version_base[1])
		|| (version_num[0] == version_base[0] && version_num[1] == version_base[1] && version_num[2] < version_base[2])) {
		ltfsmsg(LTFS_ERR, "12193E");
		return -EDEV_DRIVER_ERROR;
	}

	return DEVICE_GOOD;
}

/**
 * Get inquiry data from a specific page
 * @param device tape device
 * @param page page
 * @param inq pointer to inquiry data. This function will update this value
 * @return 0 on success or negative value on error
 */
int _ibmtape_inquiry_page(void *device, unsigned char page, struct tc_inq_page *inq, bool error_handle)
{
	char *msg;
	int rc;
	struct inquiry_page inq_page;

	if (!inq)
		return -EDEV_INVALID_ARG;

	ltfsmsg(LTFS_DEBUG, "12156D", "inquiry", page, ((struct ibmtape_data *) device)->drive_serial);

	/* init value */
	memset(inq_page.data, 0, sizeof(inq_page.data));
	inq_page.page_code = page;

	rc = _sioc_stioc_command(device, SIOC_INQUIRY_PAGE, "INQUIRY PAGE", &inq_page, &msg);

	if (rc != DEVICE_GOOD) {
		if(error_handle)
			ibmtape_process_errors(device, rc, msg, "inquiry", true);
	} else {
		memcpy(inq->data, inq_page.data, MAX_INQ_LEN);
	}

	return rc;
}

int ibmtape_inquiry_page(void *device, unsigned char page, struct tc_inq_page *inq)
{
	int ret = 0;
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_INQUIRYPAGE));
	ret = _ibmtape_inquiry_page(device, page, inq, true);
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_INQUIRYPAGE));
	return ret;
}

/**
 * Get inquiry data
 * @param inq pointer to inquiry data. This function will update this value
 * @return 0 on success or negative value on error
 */
int ibmtape_inquiry(void *device, struct tc_inq *inq)
{
	char *msg;
	int rc;
	struct inquiry_data inq_data;
	int device_code = ((struct ibmtape_data *) device)->device_code;
	int vendor_length;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_INQUIRY));
	memset(&inq_data, 0, sizeof(struct inquiry_data));

	rc = _sioc_stioc_command(device, SIOC_INQUIRY, "INQUIRY", &inq_data, &msg);

	if(rc == DEVICE_GOOD) {
		inq->devicetype = inq_data.type;
		inq->cmdque = inq_data.cmdque;
		strncpy((char *)inq->vid, (char *)inq_data.vid, 8);
		inq->vid[8] = '\0';
		strncpy((char *)inq->pid, (char *)inq_data.pid, 16);
		inq->pid[16] = '\0';
		strncpy((char *)inq->revision, (char *)inq_data.revision, 4);
		inq->revision[4] = '\0';

		if (device_code == IBM_3592) {
			vendor_length = 18;
		}
		else {
			vendor_length = 20;
		}
		strncpy((char *)inq->vendor, (char *)inq_data.vendor1, vendor_length);
		inq->vendor[vendor_length] = '\0';
	}
	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_INQUIRY));
	return rc;
}

/**
 * Test Unit Ready
 * @param device a pointer to the ibmtape backend
 * @param inq pointer to inquiry data. This function will update this valure
 * @return 0 on success or negative value on error
 */
int ibmtape_test_unit_ready(void *device)
{
	char *msg;
	int rc;
	bool take_dump = true, print_message = true;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_TUR));
	ltfsmsg(LTFS_DEBUG3, "12153D", "test unit ready",
			((struct ibmtape_data *) device)->drive_serial);

	rc = _sioc_stioc_command(device, SIOC_TEST_UNIT_READY, "TEST UNIT READY", NULL, &msg);

	if (rc != DEVICE_GOOD) {
		switch (rc) {
		case -EDEV_NEED_INITIALIZE:
		case -EDEV_CONFIGURE_CHANGED:
			print_message = false;
			/* Fall through */
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
		if (print_message)
			ibmtape_process_errors(device, rc, msg, "test unit ready", take_dump);
	}

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_TUR));
	return rc;
}

/**
 * Reserve the unit
 * @param device a pointer to the ibmtape backend
 * @param lun lun to reserve
 * @return 0 on success or a negative value on error
 */
int ibmtape_reserve_unit(void *device)
{
	int rc;
	char *msg;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_RESERVEUNIT));
	ltfsmsg(LTFS_DEBUG, "12153D", "reserve unit (6)", ((struct ibmtape_data *) device)->drive_serial);

	rc = _sioc_stioc_command(device, SIOC_RESERVE, "RESERVE", NULL, &msg);

	if (rc != DEVICE_GOOD)
		ibmtape_process_errors(device, rc, msg, "reserve unit(6)", true);

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_RESERVEUNIT));
	return rc;
}

/**
 * Release the unit
 * @param device a pointer to the ibmtape backend
 * @param lun lun to release
 * @return 0 on success or a negative value on error
 */
int ibmtape_release_unit(void *device)
{
	int rc;
	char *msg;
	bool take_dump = true, print_message = true;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_RELEASEUNIT));
	ltfsmsg(LTFS_DEBUG, "12153D", "release unit (6)", ((struct ibmtape_data *) device)->drive_serial);

	/* Invoke _ioctl to Release Unit */
	rc = _sioc_stioc_command(device, SIOC_RELEASE, "RELEASE", NULL, &msg);

	if (rc != DEVICE_GOOD) {
		switch (rc) {
		case -EDEV_POR_OR_BUS_RESET:
			take_dump = false;
			break;
		default:
			break;
		}
		if (print_message)
			ibmtape_process_errors(device, rc, msg, "release unit(6)", take_dump);
	}

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_RELEASEUNIT));
	return rc;
}

/**
 * Read buffer
 * @param device a pointer to the ibmtape backend
 * @param id
 * @param buf
 * @param offset
 * @param len
 * @param type
 * @return 0 on success or negative value on error
 */
int ibmtape_readbuffer(void *device, int id, unsigned char *buf, size_t offset, size_t len,
					   int type)
{
	struct sioc_pass_through spt;
	unsigned char cdb[10];
	unsigned char sense[MAXSENSE];
	char *msg;
	int rc;
	int device_code = ((struct ibmtape_data *) device)->device_code;

	ltfsmsg(LTFS_DEBUG, "12156D", "read buffer", id, ((struct ibmtape_data *) device)->drive_serial);

	memset(&spt, 0, sizeof(spt));
	memset(cdb, 0, sizeof(cdb));
	memset(sense, 0, sizeof(sense));

	/* Prepare Data Buffer */
	spt.buffer_length = len;
	spt.buffer = (unsigned char *) buf;
	memset(spt.buffer, 0, spt.buffer_length);

	/* Prepare CDB */
	spt.cmd_length = sizeof(cdb);
	spt.cdb = cdb;
	spt.cdb[0] = 0x3c;			/* SCSI ReadBuffer(10) Code */
	spt.cdb[1] = type;
	spt.cdb[2] = id;
	spt.cdb[3] = (unsigned char) (offset >> 16);
	spt.cdb[4] = (unsigned char) (offset >> 8);
	spt.cdb[5] = (unsigned char) (offset & 0xFF);
	spt.cdb[6] = (unsigned char) (len >> 16);
	spt.cdb[7] = (unsigned char) (len >> 8);
	spt.cdb[8] = (unsigned char) (len & 0xFF);

	spt.data_direction = SCSI_DATA_IN;
	spt.timeout = ComputeTimeOut(device_code, ReadBufferTimeOut);

	/* Prepare sense buffer */
	spt.sense_length = sizeof(sense);
	spt.sense = sense;

	/* Invoke SCSI command and check error */
	rc = sioc_paththrough(device, &spt, &msg);
	if (rc != DEVICE_GOOD)
		ibmtape_process_errors(device, rc, msg, "read buffer", false);

	return rc;
}

/**
 * Take drive dump
 * @param device a pointer to the ibmtape backend
 * @param fname a file name of dump
 * @return 0 on success or negative value on error
 */
#define DUMP_HEADER_SIZE   (4)
#define DUMP_TRANSFER_SIZE (64 * KB)

int ibmtape_getdump_drive(void *device, const char *fname)
{
	long long data_length, buf_offset;
	int dumpfd = -1;
	int transfer_size, num_transfers, excess_transfer;
	int rc = 0;
	int i, bytes;
	int buf_id;
	int device_code = ((struct ibmtape_data *) device)->device_code;
	unsigned char cap_buf[DUMP_HEADER_SIZE];
	unsigned char *dump_buf;

	ltfsmsg(LTFS_INFO, "12086I", fname);

	/* Set transfer size */
	transfer_size = DUMP_TRANSFER_SIZE;
	dump_buf = calloc(1, DUMP_TRANSFER_SIZE);
	if (dump_buf == NULL) {
		ltfsmsg(LTFS_ERR, "10001E", "ibmtape_getdump_drive: dump buffer");
		return -EDEV_NO_MEMORY;
	}

	/* Set buffer ID */
	if (device_code == IBM_3592) {
		buf_id = 0x00;
	}
	else {
		buf_id = 0x01;
	}

	/* Get buffer capacity */
	ibmtape_readbuffer(device, buf_id, cap_buf, 0, sizeof(cap_buf), 0x03);
	data_length = (cap_buf[1] << 16) + (cap_buf[2] << 8) + (int) cap_buf[3];

	/* Open dump file for write only */
	dumpfd = open(fname, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (dumpfd < 0) {
		rc = -errno;
		ltfsmsg(LTFS_WARN, "12085W", rc);
		free(dump_buf);
		return rc;
	}

	/* get the total number of transfers */
	num_transfers = data_length / transfer_size;
	excess_transfer = data_length % transfer_size;
	if (excess_transfer)
		num_transfers += 1;

	/* Total dump data length is %lld. Total number of transfers is %d. */
	ltfsmsg(LTFS_DEBUG, "12087D", data_length);
	ltfsmsg(LTFS_DEBUG, "12088D", num_transfers);

	/* start to transfer data */
	buf_offset = 0;
	i = 0;
	ltfsmsg(LTFS_DEBUG, "12089D");
	while (num_transfers) {
		int length;

		i++;

		/* Allocation Length is transfer_size or excess_transfer */
		if (excess_transfer && num_transfers == 1)
			length = excess_transfer;
		else
			length = transfer_size;

		rc = ibmtape_readbuffer(device, buf_id, dump_buf, buf_offset, length, 0x02);
		if (rc) {
			ltfsmsg(LTFS_WARN, "12090W", rc);
			free(dump_buf);
			close(dumpfd);
			return rc;
		}

		/* write buffer data into dump file */
		bytes = write(dumpfd, dump_buf, length);
		if (bytes == -1) {
			rc = -errno;
			ltfsmsg(LTFS_WARN, "12091W", rc);
			free(dump_buf);
			close(dumpfd);
			return rc;
		}

		ltfsmsg(LTFS_DEBUG, "12092D", i, bytes);
		if (bytes != length) {
			ltfsmsg(LTFS_WARN, "12093W", bytes, length);
			free(dump_buf);
			close(dumpfd);
			return -EDEV_DUMP_EIO;
		}

		/* update offset and num_transfers, free buffer */
		buf_offset += transfer_size;
		num_transfers -= 1;

	}							/* end of while(num_transfers) */

	free(dump_buf);
	close(dumpfd);

	return rc;
}

/**
 * Force Dump for Drive
 * @param device a pointer to the ibmtape backend
 * @return 0 on success or negative value on error
 */
#define SENDDIAG_BUF_LEN (8)
int ibmtape_forcedump_drive(void *device)
{
	struct sioc_pass_through spt;
	unsigned char cdb[6];
	unsigned char buf[SENDDIAG_BUF_LEN];
	unsigned char sense[MAXSENSE];
	int rc;
	char *msg;
	int device_code = ((struct ibmtape_data *) device)->device_code;

	ltfsmsg(LTFS_DEBUG, "12156D", "force dump", 0, ((struct ibmtape_data *) device)->drive_serial);

	memset(&spt, 0, sizeof(spt));
	memset(cdb, 0, sizeof(cdb));
	memset(sense, 0, sizeof(sense));

	/* Prepare Data Buffer */
	spt.buffer_length = SENDDIAG_BUF_LEN;
	spt.buffer = buf;
	memset(spt.buffer, 0, spt.buffer_length);

	/* Prepare CDB */
	spt.cmd_length = sizeof(cdb);
	spt.cdb = cdb;
	spt.cdb[0] = 0x1d;			/* SCSI Send Diag Code */
	spt.cdb[1] = 0x10;			/* PF bit is set to 1 */
	spt.cdb[3] = 0x00;
	spt.cdb[4] = 0x08;			/* parameter length is 0x0008 */

	/* Prepare payload */
	spt.buffer[0] = 0x80;		/* page code */
	spt.buffer[2] = 0x00;
	spt.buffer[3] = 0x04;		/* page length */
	spt.buffer[4] = 0x01;
	spt.buffer[5] = 0x60;		/* diagnostic id */

	spt.data_direction = SCSI_DATA_OUT;
	spt.timeout = ComputeTimeOut(device_code, SendDiagnosticTimeOut);

	/* Prepare sense buffer */
	spt.sense_length = sizeof(sense);
	spt.sense = sense;

	/* Invoke SCSI command and check error */
	rc = sioc_paththrough(device, &spt, &msg);
	if (rc != DEVICE_GOOD)
		ibmtape_process_errors(device, rc, msg, "force dump", false);

	return rc;
}

/**
 * Take normal drive dump and forces drive dump
 * @param device a pointer to the ibmtape backend
 * @return 0 on success or negative value on error
 */
int ibmtape_takedump_drive(void *device)
{
	char fname_base[1024];
	char fname[1024];
	time_t now;
	struct tm *tm_now;
	unsigned char *serial = ((struct ibmtape_data *) device)->drive_serial;

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_ENTER(REQ_TC_TAKEDUMPDRV));

	/* Make base filename */
	time(&now);
	tm_now = localtime(&now);
	sprintf(fname_base, DMP_DIR "/ltfs_%s_%d_%02d%02d_%02d%02d%02d", serial, tm_now->tm_year + 1900,
			tm_now->tm_mon + 1, tm_now->tm_mday, tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec);

	strcpy(fname, fname_base);
	strcat(fname, ".dmp");

	ltfsmsg(LTFS_INFO, "12097I");
	ibmtape_getdump_drive(device, fname);

	ltfsmsg(LTFS_INFO, "12098I");
	ibmtape_forcedump_drive(device);
	strcpy(fname, fname_base);
	strcat(fname, "_f.dmp");
	ibmtape_getdump_drive(device, fname);

	ltfs_profiler_add_entry(bend_profiler, &bend_profiler_lock, TAPEBEND_REQ_EXIT(REQ_TC_TAKEDUMPDRV));
	return 0;
}
