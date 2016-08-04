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
** FILE NAME:       tape_drivers/tape_drivers.h
**
** DESCRIPTION:     Prototypes for common tape operations.
**
** AUTHOR:          Yutaka Oishi
**                  IBM Yamato, Japan
**                  OISHI@jp.ibm.com
**
*************************************************************************************
*/

#ifndef __tape_drivers_h
#define __tape_drivers_h

#define KB   (1024)
#define MB   (KB * 1024)
#define GB   (MB * 1024)

#define VENDOR_ID_LENGTH    8
#define PRODUCT_ID_LENGTH   16
#define PRODUCT_NAME_LENGTH (PRODUCT_ID_LENGTH + 3) /* " [PRODUCT_ID]" */
#define PRODUCT_REV_LENGTH  4

static const char base_firmware_level_lto5[] = "B170";
static const char base_firmware_level_ts1140[] = "3694";

typedef void  (*crc_enc)(void *buf, size_t n);
typedef int   (*crc_check)(void *buf, size_t n);
typedef void* (*memcpy_crc_enc)(void *dest, const void *src, size_t n);
typedef int   (*memcpy_crc_check)(void *dest, const void *src, size_t n);

typedef enum {
	DRIVE_UNSUPPORTED, /* Unsupported drive */
	DRIVE_LTO5,        /* IBM Ultrium Gen 5 */
	DRIVE_LTO5_HH,     /* IBM Ultrium Gen 5 Half-High */
	DRIVE_LTO6,        /* IBM Ultrium Gen 6 */
	DRIVE_LTO6_HH,     /* IBM Ultrium Gen 6 Half-High */
	DRIVE_TS1140,      /* TS1140 */
	DRIVE_TS1150,      /* TS1150 */
} DRIVE_TYPE;

#endif // __tape_drivers_h
