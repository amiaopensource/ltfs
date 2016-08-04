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
** FILE NAME:       kmi/key_format_ltfs.h
**
** DESCRIPTION:     Header file for the LTFS specific format manager.
**
** AUTHOR:          Yutaka Oishi
**                  IBM Yamato, Japan
**                  oishi@jp.ibm.com
**
*************************************************************************************
*/

#ifndef __key_format_ltfs_h
#define __key_format_ltfs_h

#define DK_LENGTH 32
#define DKI_LENGTH 12
#define DKI_ASCII_LENGTH 3
#define	SEPARATOR_LENGTH 1 /* length of ':' and '/' */

struct key {
	unsigned char dk[DK_LENGTH];   /**< Data Key */
	unsigned char dki[DKI_LENGTH]; /**< Data Key Identifier */
};

struct key_format_ltfs {
	int num_of_keys;                  /**< Number of DK and DKi pairs */
	struct key *dk_list;              /**< DK and DKi pairs' list */
};

void *key_format_ltfs_init(struct ltfs_volume *vol, const char *id);
int key_format_ltfs_destroy(void * const kmi_handle, const char *id);
int key_format_ltfs_get_key(unsigned char **keyalias, unsigned char **key, void * const kmi_handle,
	unsigned char * const dk_list, unsigned char * const dki_for_format);

#endif /* __key_format_ltfs_h */
