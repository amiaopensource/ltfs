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
** FILE NAME:       tape_drivers/osx/iokit/iokit_common.h
**
** DESCRIPTION:     Header file for common backend operations.
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


#include "libltfs/tape_ops.h"

#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOTypes.h>
#include <IOKit/scsi/SCSITaskLib.h>
#include <IOKit/scsi/SCSICommandOperationCodes.h>

#include "tape_timeout.h"
#include "ltfsprintf.h"
#include "scsi_command_blocks.h"
#include "scsi_mode_pages.h"
#include "libltfs/arch/osx/osx_string.h"
#include "tape_drivers/tape_drivers.h"

#ifndef __iokit_common_h
#define __iokit_common_h

// Function prototypes
extern int iokitosx_inquiry(void *device, struct tc_inq *inq);
extern int iokitosx_test_unit_ready(void *device);
extern int iokitosx_readbuffer(void *device, int id, unsigned char *buf, size_t offset, size_t len, int type);
extern int iokitosx_takedump_drive(void * device);
extern void iokitosx_process_errors(void *device, int rc, char *msg, char *cmd, bool take_dump);

static inline void iokitosx_get_dump(void *device)
{
    //int fd = *((int *)device);

#ifdef __iokit_tc_c
    iokitosx_takedump_drive(device);
#endif

    return;
}

#endif // __iokit_common_h
