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
** FILE NAME:       xml.h
**
** DESCRIPTION:     Prototypes for XML read/write functions.
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

#ifndef __xml_libltfs_h
#define __xml_libltfs_h

#include <libxml/tree.h>
#include "ltfs.h"
#include "xml.h"

/* A couple of tag names */
#define BACKUPTIME_TAGNAME "backuptime"
#define NEXTUID_TAGNAME    "highestfileuid"
#define UID_TAGNAME        "fileuid"
#define FILEOFFSET_TAGNAME "fileoffset"

/* Functions for writing XML files. See xml_writer_libltfs.c */
xmlBufferPtr xml_make_label(const char *creator, tape_partition_t partition,
	const struct ltfs_label *label);
int xml_label_to_file(const char *filename, const char *creator, const struct ltfs_label *label);
xmlBufferPtr xml_make_schema(const char *creator, const struct ltfs_index *idx);
int xml_schema_to_file(const char *filename, const char *creator
					   , const char *reason, const struct ltfs_index *idx);
int xml_schema_to_tape(char *reason, struct ltfs_volume *vol);

/* Functions for reading XML files. See xml_reader_libltfs.c */
int xml_label_from_file(const char *filename, struct ltfs_label *label);
int xml_label_from_mem(const char *buf, int buf_size, struct ltfs_label *label);
int xml_schema_from_file(const char *filename, struct ltfs_index *idx, struct ltfs_volume *vol);
int xml_schema_from_tape(uint64_t eod_pos, struct ltfs_volume *vol);
int xml_extent_symlink_info_from_file(const char *filename, struct dentry *d);

#endif /* __xml_libltfs_h */
