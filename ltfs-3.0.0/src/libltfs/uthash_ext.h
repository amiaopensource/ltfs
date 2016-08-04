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
 ** FILE NAME:       uthash_ext.h
 **
 ** DESCRIPTION:     Header file for HASH function extensions.
 **
 ** AUTHORS:
 **                  IBM Yamato, Japan
 **
 *************************************************************************************
 */
#ifndef __uthash_ext_h
#define __uthash_ext_h

#include "uthash.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HASH_FIND_PARTIAL(structure,start,length,table,search,result)	\
do {																	\
	structure *s, *tmp;													\
	result = NULL;														\
	HASH_ITER(hh, table, s, tmp) {										\
		if (HASH_KEYCMP(&s->start, &search.start, length) == 0) {		\
			result = s;													\
			break;														\
		}																\
	}																	\
} while (0)

#ifdef __cplusplus
}
#endif

#endif /* uthash_ext_h */
