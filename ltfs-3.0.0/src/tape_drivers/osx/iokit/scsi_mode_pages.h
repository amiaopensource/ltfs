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
** FILE NAME:       tape_drivers/osx/iokit/scsi_mode_pages.h
**
** DESCRIPTION:     Header file for raw SCSI mode pages.
**
** AUTHOR:          Michael A. Richmond
**                  IBM Almaden Research Center
**                  mar@almaden.ibm.com
**
*************************************************************************************
*/


#ifndef __scsi_mode_pages_h
#define __scsi_mode_pages_h

#define DATA_COMPRESSION_PAGE_NUMBER 			0x0F
#define DEVICE_CONFIGURATION_PAGE_NUMBER		0x10
#define MEDIUM_SENSE_PAGE_NUMBER				0x23

/* medium sense page (359x only) */
#define Capacity100 0x00
#define Capacity75  0xC0
#define Capacity50  0x80
#define Capacity25  0x40
#define Capacity100RangeHigh 0xff
#define Capacity100RangeLow  0xC1
#define Capacity75RangeHigh  0xC0
#define Capacity75RangeLow   0x81
#define Capacity50RangeHigh  0x80
#define Capacity50RangeLow   0x41
#define Capacity25RangeHigh  0x40
#define Capacity25RangeLow   0x01

#endif /* __scsi_mode_pages_h */
