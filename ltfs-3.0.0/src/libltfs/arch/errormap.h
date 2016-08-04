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
** FILE NAME:       errormap.h
**
** DESCRIPTION:     Platform-specific error code mapping functions.
**
** AUTHORS:         Brian Biskeborn
**                  IBM Almaden Research Center
**                  bbiskebo@us.ibm.com
**
*************************************************************************************
*/

#ifndef arch_error_h__
#define arch_error_h__

#ifdef __cplusplus
extern "C" {
#endif


/** Initialize the error map. Call this function before using the error map functions.
 * @return 0 on success or -LTFS_NO_MEMORY if memory allocation fails.
 */
int errormap_init();

/** Free the error map. Call this function when the error map is no longer needed. */
void errormap_finish();

/** Map a libltfs error code to the corresponding operating system error code.
 * @param val Error code to look up in the table. This value must be less than or equal to zero.
 * @return The mapped error code. If val "looks like" an OS error code already,
 *         i.e. abs(val) < LTFS_ERR_MIN, then val is returned unmodified. If val is not found in
 *         the error table, -EIO is returned.
 */
int errormap_fuse_error(int val);

/** Map a libltfs error code to the corresponding error message.
 * @param val Error code to look up in the table. This value must be less than or equal to zero.
 * @return pointer of the mapped message. if val is not found in the error table, NULL is returned.
 */
char* errormap_msg_id(int val);

#ifdef __cplusplus
}
#endif

#endif /* arch_error_h__ */
