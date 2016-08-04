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
** FILE NAME:       tape_drivers/linux/ibmtape/ibmtape_timeout.h
**
** DESCRIPTION:     Defines timeout values for tape operations.
**
** AUTHOR:          Atsushi Abe
**                  IBM Yamato, Japan
**                  PISTE@jp.ibm.com
**
*************************************************************************************
*/

#ifndef _ibmtape_timeout_h
#define _ibmtape_timeout_h

/* tape device id */
#define UNSUPPORTED 0
#define IBM_3580    1
#define IBM_3592    3

#define EraseTimeOut             0
#define InquiryTimeOut           1
#define LoadTimeOut              2
#define LocateTimeOut            3
#define LogSelectTimeOut         4
#define LogSenseTimeOut          5
#define ModeSelectTimeOut        6
#define ModeSenseTimeOut         7
#define UnloadTimeOut            8
#define PreventAllowMediaTimeOut 9
#define ReadTimeOut             10
#define ReadBlockLimitsTimeOut  11
#define ReadBufferTimeOut       12
#define ReadPositionTimeOut     13
#define ReleaseTimeOut          14
#define ReportDensityTimeOut    15
#define ReserveTimeOut          16
#define RewindTimeOut           17
#define SpaceTimeOut            18
#define TestUnitReadyTimeOut    19
#define WriteTimeOut            20
#define WriteFileMarkTimeOut    21
#define DisplayMessageTimeOut   22
#define SendDiagnosticTimeOut   23
#define ChangeDefinitionTimeOut 24
#define DefaultTimeOut          25
#define PersistReserveInTimeOut  26
#define PersistReserveOutTimeOut 27
#define FormatTimeOut            28
#define SetcapTimeout            29
#define SecurityProtocolIn       30
#define SecurityProtocolOut      31

// Default timeout covers most SCSI operations
#define DefaultTimeOutValue     2000

/*
 * Device timeout tables for SCSI operations. These tables contain
 * timeout values measured in seconds. The ComputeTimeOut macro
 * above looks up the correct table based on the drive type and
 * converts to milliseconds. (IOKit expects timeouts to be
 * measured in milliseconds.)
 */

static unsigned short IBM3580TimeOut[] = {
  15600, /* Erase */
  90,    /* Inquiry */
  1000,  /* Load */
  2500,  /* Locate */
  90,    /* LogSelect */
  90,    /* LogSense */
  300,   /* ModeSelect */
  90,    /* ModeSense */
  1080,  /* Unload */
  90,    /* PreventAllowMedia */
  2000,  /* Read */
  90,    /* ReadBlockLimits */
  720,   /* ReadBuffer */
  90,    /* ReadPosition */
  90,    /* Release */
  90,    /* ReportDensity */
  90,    /* Reserve */
  900,   /* Rewind */
  2500,  /* Space */
  90,    /* TestUnitReady */
  2000,  /* Write */
  1800,  /* WriteFileMark */
  0,     /* DisplayMessage */
  2900,  /* SendDiagnosic */
  60,    /* ChangeDefinition */
  500,   /* Default */
  90,    /* Persistent Reserve In */
  90,    /* Persistent Reserve Out */
  1560,  /* Format */
  1560,  /* Set Capacity */
  90,    /* Security Protocol In */
  90,    /* Security Protocol Out */
}; // IBM 3580 Time Out Values

static unsigned short IBM3592TimeOut[] = {
  22000, // Erase
  60,    // Inquiry
  850,   // Load
  8400,  // Locate
  60,    // LogSelect
  60,    // LogSense
  200,   // ModeSelect
  60,    // ModeSense
  1300,  // Unload
  60,    // PreventAllowMedia
  1200,  // Read
  60,    // ReadBlockLimites
  350,   // ReadBufferTimeOut
  60,    // ReadPosition
  60,    // Release
  60,    // ReportDensity
  960,   // Reserve
  850,   // Rewind
  1300,  // Space
  60,    // TestUnitReady
  1200,  // Write
  1200,  // WriteFileMark
  60,    // DisplayMessage
  2200,  // SendDianostic
  60,    // ChangeDefinition
  500,   // Default
  60,    // Persistent Reserve In
  1080,  // Persistent Reserve Out
  1560,  // Format
  1560,  // Set Capacity
  60,    // Security Protocol In
  60,    // Security Protocol Out
}; // IBM 3592 Time Out Values

#define ComputeTimeOut(DevID, CommandTimeOut) \
		((DevID == IBM_3592)  ? (IBM3592TimeOut[CommandTimeOut]) : \
		(DevID == IBM_3580)  ? (IBM3580TimeOut[CommandTimeOut]) : \
		(DefaultTimeOutValue))

#endif  /* _ibmtape_timeout_h */
