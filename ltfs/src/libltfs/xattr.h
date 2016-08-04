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
** FILE NAME:       xattr.h
**
** DESCRIPTION:     Header for extended attribute routines.
**
** AUTHORS:         Brian Biskeborn
**                  IBM Almaden Research Center
**                  bbiskebo@us.ibm.com
**
**                  Lucas C. Villa Real
**                  IBM Almaden Research Center
**                  lucasvr@us.ibm.com
**
*************************************************************************************
*/

#ifndef __xattr_h__
#define __xattr_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "ltfs.h"

#define LTFS_PRIVATE_PREFIX "ltfs."

int xattr_set(struct dentry *d, const char *name, const char *value, size_t size, int flags,
	struct ltfs_volume *vol);
int xattr_get(struct dentry *d, const char *name, char *value, size_t size,
	struct ltfs_volume *vol);
int xattr_list(struct dentry *d, char *list, size_t size, struct ltfs_volume *vol);
int xattr_remove(struct dentry *d, const char *name, struct ltfs_volume *vol);

int _xattr_get_string(const char *val, char **outval, const char *msg);
int _xattr_get_u64(uint64_t val, char **outval, const char *msg);

/** For internal use only */
int xattr_do_set(struct dentry *d, const char *name, const char *value, size_t size,
	struct xattr_info *xattr);
int xattr_do_remove(struct dentry *d, const char *name, bool force, struct ltfs_volume *vol);
const char *_xattr_strip_name(const char *name);
int xattr_set_mountpoint_length(struct dentry *d, const char* value, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* __xattr_h__ */
