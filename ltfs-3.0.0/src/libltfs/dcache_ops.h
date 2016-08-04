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
** FILE NAME:       dcache_ops.h
**
** DESCRIPTION:     Defines operations that must be supported by the dentry cache managers.
**
** AUTHOR:          Lucas C. Villa Real
**                  IBM Brazil Research Lab
**                  lucasvr@br.ibm.com
**
*************************************************************************************
*/
#ifndef __dcache_ops_h
#define __dcache_ops_h

#include "ltfs.h"

#include <sys/types.h>
#include <dirent.h>

enum dcache_flush_flags {
	FLUSH_XATTRS           = 0x01, /**< Flush extended attribute to the dentry cache */
	FLUSH_EXTENT_LIST      = 0x02, /**< Flush extent lists to the dentry cache */
	FLUSH_METADATA         = 0x04, /**< Flush matadata to the dentry cache */
	FLUSH_RECURSIVE        = 0x08, /**< Flsh dentry cache recursive if directory is specified */
	FLUSH_ALL              = 0x07, /**< Flsh all dentry attribute to the dentry cache */
	FLUSH_CREATE           = 0x07, /**< Alias of FLUSH_ALL */
	FLUSH_ALL_RECURSIVE    = 0x0F, /**< Flsh all dentry attribute to the dentry cache recursive if directory is specified */
	FLUSH_CREATE_RECURSIVE = 0x0F, /**< Alias of FLUSH_ALL_RECURSIVE */
};

/**
 * Dentry cache options specified in the LTFS configuration file.
 */
struct dcache_options {
	bool enabled;  /**< Disk cache is enabled */
	int minsize;   /**< Minimum size (initial size of dcache image) in GB */
	int maxsize;   /**< Maximum size (final size of dcache image) in GB */
};

/**
 * dcache_ops structure.
 * Defines operations that must be supported by the dentry cache managers.
 */
struct dcache_ops {
	/* Initialization, deinitialization and management */

	/**
	 * Create and initialize dcache handle
	 * @param options pointer to the options for dcache handle
	 * @param vol pointer to the volume structure
	 * @return the handle of the dcache on success or NULL on error
	 */
	void    *(*init)(const struct dcache_options *options, struct ltfs_volume *vol);

	/**
	 * Destroy dcache handle
	 * @param dcache_handle dcache handle returned by init() call of dcache
	 * @return 0 on success or a negative value on error.
	 */
	int      (*destroy)(void *dcache_handle);

	/**
	 * Make dcache data from in-memory dentry to disk
	 * @param name dcache name which can be specify a cartridge
	 * @param dcache_handle dcache handle returned by init() call of dcache
	 * @return 0 on success or a negative value on error.
	 */
	int      (*mkcache)(const char *name, void *dcache_handle);

	/**
	 * Remove dcache data on the disk
	 * @param name dcache name which can be specify a cartridge
	 * @param dcache_handle dcache handle returned by init() call of dcache
	 * @return 0 on success or a negative value on error.
	 */
	int      (*rmcache)(const char *name, void *dcache_handle);

	/**
	 * Tell the dcache data of specified cartridge is exist or not
	 * @param name dcache name which can be specify a cartridge
	 * @param[out] exists true if dcache data is existed on the disk otherwise false
	 * @param dcache_handle dcache handle returned by init() call of dcache
	 * @return 0 on success or a negative value on error.
	 */
	int      (*cache_exists)(const char *name, bool *exists, void *dcache_handle);

	/**
	 * Set work directory of the LTFS
	 * @param workdir path to the work directory
	 * @param clean clean work directory
	 * @param dcache_handle dcache handle returned by init() call of dcache
	 * @return 0 on success or a negative value on error.
	 */
	int      (*set_workdir)(const char *workdir, bool clean, void *dcache_handle);

	/**
	 * Get work directory of the LTFS
	 * @param[out] workdir path to thh work directory
	 * @param dcache_handle dcache handle returned by init() call of dcache
	 * @return 0 on success or a negative value on error.
	 */
	int      (*get_workdir)(char **workdir, void *dcache_handle);

	/**
	 * Assign cartridge name to the dcache
	 * @param name dcache name which can be specify a cartridge
	 * @param dcache_handle dcache handle returned by init() call of dcache
	 * @return 0 on success or a negative value on error.
	 */
	int      (*assign_name)(const char *name, void *dcache_handle);

	/**
	 * Unassign cartridge name to the dcache
	 * @param dcache_handle dcache handle returned by init() call of dcache
	 * @return 0 on success or a negative value on error.
	 */
	int      (*unassign_name)(void *dcache_handle);

	/**
	 * Tell name is assigned to the cartridge or not
	 * @param[out] assigned true if dcache data is existed on the disk otherwise false
	 * @param dcache_handle dcache handle returned by init() call of dcache
	 * @return 0 on success or a negative value on error.
	 */
	int      (*is_name_assigned)(bool *assigned, void *dcache_handle);

	/**
	 * Free dentry tree to reduce memory usage
	 * TODO Must be called with appropriate locks held. (see library_mount_request)
	 * @param dcache_handle The disk cache handle
	 * @return 0 on success or a negative value on error.
	 */
	int      (*wipe_dentry_tree)(void *dcache_handle);

	/**
	 * Tell dcache module can be sharable by multinode
	 * @param[out] sharable flag show this dcache module can handle nultinode environment or not
	 * @return 0 on success or a negative value on error.
	 */
	int      (*is_sharable)(bool *sharable);

	/* Dcache validation metrics */

	/**
	 * Set volume UUID to the dcache data
	 * @param uuid NULL terminated uuid string to set
	 * @param dcache_handle dcache handle returned by init() call of dcache
	 * @return 0 on success or a negative value on error.
	 */
	int      (*set_vol_uuid)(const char *uuid, void *dcache_handle);

	/**
	 * Get volume UUID to the dcache data
	 * @param workdir path to the work directory
	 * @param name dcache name which can be specify a cartridge
	 * @param[out] uuid NULL terminated uuid string to set
	 * @return 0 on success or a negative value on error.
	 */
	int      (*get_vol_uuid)(const char *work_dir, const char *name, char **uuid);

	/**
	 * Set generation number to the dcache data
	 * @param gen generation number of index to set
	 * @param dcache_handle dcache handle returned by init() call of dcache
	 * @return 0 on success or a negative value on error.
	 */
	int      (*set_generation)(unsigned int gen, void *dcache_handle);

	/**
	 * Get generation number from dcache data
	 * @param workdir path to the work directory
	 * @param name dcache name which can be specify a cartridge
	 * @param[out] gen generation number
	 * @return 0 on success or a negative value on error.
	 */
	int      (*get_generation)(const char *work_dir, const char *name, unsigned int *gen);

	/**
	 * Set dirty flag to the dcache data
	 * @param dirty flag show dirty or not
	 * @param dcache_handle dcache handle returned by init() call of dcache
	 * @return 0 on success or a negative value on error.
	 */
	int      (*set_dirty)(bool dirty, void *dcache_handle);

	/**
	 * Get generation number from dcache data
	 * @param workdir path to the work directory
	 * @param name dcache name which can be specify a cartridge
	 * @param[out] dirty flag show dirty or not
	 * @return 0 on success or a negative value on error.
	 */
	int      (*get_dirty)(const char *work_dir, const char *name, bool *dirty);

	/* Disk image management */
	int      (*diskimage_create)(void *dcache_handle);
	int      (*diskimage_remove)(void *dcache_handle);
	int      (*diskimage_mount)(void *dcache_handle);
	int      (*diskimage_unmount)(void *dcache_handle);
	bool     (*diskimage_is_full)(void);

	/* Advisory lock operations */
	int      (*get_advisory_lock)(const char *name, void *dcache_handle);
	int      (*put_advisory_lock)(const char *name, void *dcache_handle);

	/* File system operations */
	int      (*open)(const char *path, struct dentry **d, void *dcache_handle);
	int      (*openat)(const char *parent_path, struct dentry *parent, const char *name,
						struct dentry **result, void *dcache_handle);
	int      (*close)(struct dentry *d, bool lock_meta, bool descend, void *dcache_handle);
	int      (*create)(const char *path, struct dentry *d, void *dcache_handle);
	int      (*unlink)(const char *path, struct dentry *d, void *dcache_handle);
	int      (*rename)(const char *oldpath, const char *newpath, struct dentry **old_dentry,
						void *dcache_handle);
	int      (*flush)(struct dentry *d, enum dcache_flush_flags flags, void *dcache_handle);
	int      (*readdir)(struct dentry *d, bool dentries, void ***result, void *dcache_handle);
	int      (*read_direntry)(struct dentry *d, struct ltfs_direntry *dirent, unsigned long index, void *dcache_handle);
	int      (*setxattr)(const char *path, struct dentry *d, const char *xattr, const char *value,
						size_t size, int flags, void *dcache_handle);
	int      (*removexattr)(const char *path, struct dentry *d, const char *xattr,
						void *dcache_handle);
	int      (*listxattr)(const char *path, struct dentry *d, char *list, size_t size,
						void *dcache_handle);
	int      (*getxattr)(const char *path, struct dentry *d, const char *name,
						void *value, size_t size, void *dcache_handle);

	/* Helper operations */
	int      (*get_dentry)(struct dentry *d, void *dcache_handle);
	int      (*put_dentry)(struct dentry *d, void *dcache_handle);

	/* Out of sync state management */
	int      (*is_out_of_sync)(bool *out_of_sync, void *dcache_handle);
	int      (*force_to_sync)(void *dcache_handle);
};

struct dcache_ops *dcache_get_ops(void);
const char *dcache_get_message_bundle_name(void **message_data);

#endif /* __dcache_ops_h */
