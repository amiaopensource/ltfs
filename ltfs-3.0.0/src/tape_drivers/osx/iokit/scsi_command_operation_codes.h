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
** FILE NAME:       tape_drivers/osx/iokit/scsi_command_operation_codes.h
**
** DESCRIPTION:     Header file for raw SCSI command operations that are
**
** AUTHOR:          Michael A. Richmond
**                  IBM Almaden Research Center
**                  mar@almaden.ibm.com
**
*************************************************************************************
*/


#ifndef __scsi_command_opertation_codes_h
#define __scsi_command_opertation_codes_h

#pragma mark About this file
/* This file contains the operation code definitions for SCSI tape
 * commands. The values in this file fill-in gaps where the header
 * file provided by Apple has "#if 0" directives around some of the
 * commands required to drive sequential access devices.
 */

#pragma mark -
#pragma mark SSC Sequential Access Commands
/* Commands defined by the SCSI-3 Stream Commands (SSC) command
 * specification.
 */
enum
{
    kSCSICmd_ERASE                          = 0x19, /* 5.3.1: Mandatory */
    kSCSICmd_FORMAT_MEDIUM                  = 0x04, /* 5.3.2: Optional */

    kSCSICmd_LOAD_UNLOAD                    = 0x1B, /* 5.3.3: Optional */
    kSCSICmd_LOCATE                         = 0x2B, /* 5.3.4: Optional */

    kSCSICmd_READ_BLOCK_LIMITS              = 0x05, /* 5.3.6: Mandatory */

    kSCSICmd_READ_POSITION                  = 0x34, /* 5.3.7: Mandatory */

    kSCSICmd_REPORT_DENSITY_SUPPORT         = 0x44, /* 5.3.10: Mandatory*/

    kSCSICmd_REWIND                         = 0x01, /* 5.3.11: Mandatory*/

    kSCSICmd_SPACE                          = 0x11, /* 5.3.12: Mandatory*/

    kSCSICmd_WRITE_FILEMARKS                = 0x10, /* 5.3.15: Mandatory*/

	kSCSICmd_SET_CAPACITY                   = 0x0B,
	kSCSICmd_SECURITY_PROTOCOL_IN           = 0xA2,
	kSCSICmd_SECURITY_PROTOCOL_OUT          = 0xB5,
	kSCSICmd_ALLOW_OVERWRITE                = 0x82,
};

#endif	/* __scsi_command_opertation_codes_h */
