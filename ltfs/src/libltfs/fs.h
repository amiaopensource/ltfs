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
** FILE NAME:       fs.h
**
** DESCRIPTION:     Header file for the facilities to deal with the file system tree.
**
** AUTHOR:          Lucas C. Villa Real
**                  IBM Almaden Research Center
**                  lucasvr@us.ibm.com
**
*************************************************************************************
**
** Copyright (C) 2012 OSR Open Systems Resources, Inc.
** 
************************************************************************************* 
*/
#ifndef __fs_helper_h
#define __fs_helper_h

#include "ltfs.h"
#include "arch/filename_handling.h"

/* Lock requests that can be passed to fs_path_lookup */
#define LOCK_PARENT_CONTENTS_R (1)
#define LOCK_PARENT_CONTENTS_W (1 << 1)
#define LOCK_PARENT_META_R     (1 << 2)
#define LOCK_PARENT_META_W     (1 << 3)
#define LOCK_DENTRY_CONTENTS_R (1 << 4)
#define LOCK_DENTRY_CONTENTS_W (1 << 5)
#define LOCK_DENTRY_META_R     (1 << 6)
#define LOCK_DENTRY_META_W     (1 << 7)

struct dentry *fs_allocate_dentry(struct dentry *parent, const char *name, const char * platform_safe_name,
	bool dir, bool readonly, bool allocate_uid, struct ltfs_index *idx);
uint64_t fs_allocate_uid(struct ltfs_index *idx);
int fs_update_platform_safe_names(struct dentry* basedir, struct ltfs_index *idx, struct name_list *list);
bool fs_is_predecessor(struct dentry *d1, struct dentry *d2);
uint64_t fs_get_used_blocks(struct dentry *d);
void fs_dump_tree(struct dentry *root);
void fs_increment_file_count(struct ltfs_index *idx);
void fs_decrement_file_count(struct ltfs_index *idx);
int fs_init_inode(void);
int fs_hash_sort_by_uid(struct name_list *a, struct name_list *b);
struct name_list* fs_add_key_to_hash_table(struct name_list *list, struct dentry *add_entry, int *rc);
struct name_list* fs_find_key_from_hash_table(struct name_list *list, const char *name, int *rc);
void fs_gc_dentry(struct dentry *d);

/**
 * Decrement a dentry's reference count, freeing it if the reference count becomes 0.
 * Normally, the caller must not use its handle to the dentry any more after calling this function.
 * The caller should not hold any locks on the dentry.
 * @param d Dentry to release.
 */
void fs_release_dentry(struct dentry *d);

/**
 * Unlocked version of fs_release_dentry().
 * The caller must hold a write lock on d->meta_lock.
 * @param d Dentry to release.
 */
void fs_release_dentry_unlocked(struct dentry *d);

/**
 * Search a directory for a dentry by name.
 * The caller must hold basedir->contents_lock for read or write.
 * If a dentry is found, its reference count is incremented.
 * @param basedir Directory to search.
 * @param name Name to search for, in UTF-8 NFC.
 * @param dentry On success, points to the dentry that was found, or to NULL if no dentry was found.
 *               Undefined if this function returns a negative value.
 * @return 0 on success, -LTFS_NULL_ARG if an input argument is NULL, or -LTFS_NAMETOOLONG
 *         if 'name' is too long. On case-insensitive systems, other negative values may be
 *         returned to indicate name comparison errors.
 */
int fs_directory_lookup(struct dentry *basedir, const char *name, struct dentry **dentry);

/**
 * Look up the path corresponding to a dentry.
 * The caller must hold a read or write lock on the LTFS volume prior to calling this function.
 * @param dentry Dentry whose path is wanted.
 * @param name On success, points to an allocated buffer that holds the path corresponding to
 *             the dentry in UTF-8 NFC.
 * @return 0 on success or a negative value on error.
 */
int fs_dentry_lookup(struct dentry *dentry, char **name);

/**
 * Look up the dentry corresponding to a path.
 * If a dentry is found, its reference count is incremented.
 * The caller must hold a read or write lock on the LTFS volume to which 'idx' belongs.
 * @param path Path to search for, in UTF-8 NFC. The path should be checked
 *             for invalid characters by the caller. This function validates the length of each
 *             path component. If path points to an empty string, this function returns the
 *             root dentry.
 * @param flags Locks requested by the caller. It should be an OR of one or more
 *              LOCK_PARENT_* and LOCK_DENTRY_* flags (see fs.h).
 * @param dentry On success, points to the dentry that was found. Undefined on error.
 * @param idx LTFS index to search.
 * @return 0 on success (dentry found), -LTFS_NO_DENTRY if no dentry was found, -LTFS_NAMETOOLONG
 *         if any component of the path is too long, or another negative value if an internal
 *         (unexpected) error occurs.
 */
int fs_path_lookup(const char *path, int flags, struct dentry **dentry, struct ltfs_index *idx);

/**
 * Split a path into parent directory and file name components.
 * @param path Path to split.
 * @param filename On exit, points to the beginning of the file name. The preceding '/' is set
 *                 to '\0'.
 * @param len Length of path.
 */
void fs_split_path(char *path, char **filename, size_t len);

#endif /* __fs_helper_h */
