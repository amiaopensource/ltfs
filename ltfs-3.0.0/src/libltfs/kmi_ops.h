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
** FILE NAME:       kmi_ops.h
**
** DESCRIPTION:     Defines operations that must be supported by the key manager interface plugins.
**
** AUTHOR:          Yutaka Oishi
**                  IBM Yamato, Japan
**                  oishi@jp.ibm.com
**
*************************************************************************************
*/
#ifndef __kmi_ops_h
#define __kmi_ops_h

#include "ltfs.h"

/**
 * kmi_ops structure.
 * Defines operations that must be supported by the key manager interface plugins.
 */
struct kmi_ops {
	void    *(*init)(struct ltfs_volume *vol);
	int      (*destroy)(void * const kmi_handle);
	int      (*get_key)(unsigned char ** const keyalias, unsigned char ** const key, void * const kmi_handle);
	int      (*help_message)(void);
	int      (*parse_opts)(void *opt_args);
};

struct kmi_ops *kmi_get_ops(void);
const char *kmi_get_message_bundle_name(void ** const message_data);

#endif /* __kmi_ops_h */
