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
** FILE NAME:       ltfs_internal.h
**
** DESCRIPTION:     Defines private interfaces to core LTFS functionality.
**
** AUTHORS:         Brian Biskeborn
**                  IBM Almaden Research Center
**                  bbiskebo@us.ibm.com
**
*************************************************************************************
*/

#ifndef __ltfs_internal_h__
#define __ltfs_internal_h__

/** \file
 * Low-level functions for libltfs. Normal applications should use the functions prototyped
 * in ltfs.h, but the functions here may be useful for an application (e.g., mkltfs)
 * which needs to interact at a lower level with the tape and/or the LTFS data structures.
 */

#include "ltfs.h"

int ltfs_index_alloc(struct ltfs_index **index, struct ltfs_volume *vol);

void _ltfs_index_free(bool force, struct ltfs_index **index);
#define ltfs_index_free(idx)					\
	_ltfs_index_free(false, idx)
#define ltfs_index_free_force(idx)				\
	_ltfs_index_free(true, idx)

int ltfs_check_medium(bool fix, bool deep, bool recover_extra, bool recover_symlink, struct ltfs_volume *vol);
int ltfs_read_labels(bool trial, struct ltfs_volume *vol);
int ltfs_read_one_label(tape_partition_t partition, struct ltfs_label *label,
	struct ltfs_volume *vol);
int ltfs_read_index(uint64_t eod_pos, bool recover_symlink, struct ltfs_volume *vol);

int ltfs_update_cart_coherency(struct ltfs_volume *vol);
int ltfs_write_index_conditional(char partition, struct ltfs_volume *vol);
bool ltfs_is_valid_partid(char id);

int ltfs_seek_index(char partition, tape_block_t *eod_pos, tape_block_t *index_end_pos,
			bool *fm_after, bool *blocks_after, bool recover_symlink, struct ltfs_volume *vol);
void _ltfs_last_ref(struct dentry *d, tape_block_t *dp_last, tape_block_t *ip_last,
					struct ltfs_volume *vol);
int ltfs_split_symlink( struct ltfs_volume *vol );

#endif /* __ltfs_internal_h__ */
