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
** FILE NAME:       tape_drivers/osx/iokit/device_identifiers.h
**
** DESCRIPTION:     Header file defining devices supported for
**                  LTFS.
**
** AUTHOR:          Michael A. Richmond
**                  IBM Almaden Research Center
**                  mar@almaden.ibm.com
**
*************************************************************************************
*/


#include "tape_timeout.h"
#include "tape_drivers/tape_drivers.h"

#ifndef __device_identifiers_h
#define __device_identifiers_h

#define IBM_VENDOR_ID	"IBM"
struct supported_device {
	char vendor_id[VENDOR_ID_LENGTH + 1];
	char product_id[PRODUCT_ID_LENGTH + 1];
	int  device_code;
	DRIVE_TYPE drive_type;
	char product_name[PRODUCT_NAME_LENGTH + 1];
};

#define DEVICE(v, p, c, t, n)	&(struct supported_device){ v, p, c, t, n }

extern struct supported_device *supported_devices[];

#endif /* __device_identifiers_h */
