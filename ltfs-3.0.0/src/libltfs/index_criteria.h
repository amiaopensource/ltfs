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
** FILE NAME:       index_criteria.h
**
** DESCRIPTION:     Header file for the routines that deal with the index partition 
**                  criteria.
**
** AUTHOR:          Lucas C. Villa Real
**                  IBM Almaden Research Center
**                  lucasvr@us.ibm.com
**
*************************************************************************************
*/
#ifndef __index_criteria_h
#define __index_criteria_h

#ifdef __cplusplus
extern "C" {
#endif

int index_criteria_parse(const char *filterrules, struct ltfs_volume *vol);
int index_criteria_dup_rules(struct index_criteria *dest_ic, struct index_criteria *src_ic);
int index_criteria_set_allow_update(bool allow, struct ltfs_volume *vol);
bool index_criteria_match(struct dentry *d, struct ltfs_volume *vol);
size_t index_criteria_get_max_filesize(struct ltfs_volume *vol);
const char **index_criteria_get_glob_patterns(struct ltfs_volume *vol);
void index_criteria_free(struct index_criteria *ic);

#ifdef __cplusplus
}
#endif

#endif /* __index_criteria_h */
