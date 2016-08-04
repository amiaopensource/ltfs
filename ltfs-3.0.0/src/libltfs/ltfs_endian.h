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
*/

/*************************************************************************************
 ** FILE NAME:       ltfs_endian.h
 **
 ** DESCRIPTION:     Implements macros for endian conversions.
 **
 ** AUTHORS:         Brian Biskeborn
 **                  IBM Almaden Research Center
 **                  bbiskebo@us.ibm.com
 **
 *************************************************************************************
 */

#ifndef __LTFS_ENDIAN_H__
#define __LTFS_ENDIAN_H__

/* TODO: verify that this is correct for mingw */
#ifdef mingw_PLATFORM
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif /* mingw_PLATFORM */

/**
 * Convert a uint64_t value (src) to big endian and store it in the 8-byte buffer
 * pointed to by dest.
 */
#define ltfs_u64tobe(dest, src) \
	do { \
		uint32_t *tmp = (uint32_t *)(dest); \
		uint64_t stmp = (src); \
		tmp[0] = htonl((stmp >> 32) & 0xffffffff); \
		tmp[1] = htonl(stmp & 0xffffffff); \
	} while (0)

/**
 * Convert a uint32_t value (src) to big endian and store it in the 4-byte buffer
 * pointed to by dest.
 */
#define ltfs_u32tobe(dest, src) \
	do { \
		*((uint32_t *)(dest)) = htonl((src)); \
	} while (0)

/**
 * Convert a uint16_t value (src) to big endian and store it in the 2-byte buffer
 * pointed to by dest.
 */
#define ltfs_u16tobe(dest, src) \
	do { \
		*((uint16_t *)(dest)) = htons((src)); \
	} while (0)

/**
 * Convert a big endian 64-bit unsigned integer (pointed to by buf)
 * to a uint64_t in local byte order.
 */
#define ltfs_betou64(buf) \
	(((uint64_t)ntohl(*((uint32_t *)(buf)))) << 32) + (uint64_t)ntohl(*(((uint32_t *)(buf))+1))

/**
 * Convert a big endian 32-bit unsigned integer (pointed to by buf)
 * to a uint32_t in local byte order.
 */
#define ltfs_betou32(buf) ntohl(*((uint32_t *)(buf)))

/**
 * Convert a big endian 16-bit unsigned integer (pointed to by buf)
 * to a uint16_t in local byte order.
 */
#define ltfs_betou16(buf) ntohs(*((uint16_t *)(buf)))

#endif /* __LTFS_ENDIAN_H__ */

