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
** FILE NAME:       libltfs/dcache.h
**
** DESCRIPTION:     Dentry Cache API
**
** AUTHOR:          Lucas C. Villa Real
**                  IBM Brazil Research Lab
**                  lucasvr@br.ibm.com
**
*************************************************************************************
*/
#ifndef __dcache_h
#define __dcache_h

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _XOPEN_SOURCE
#undef _XOPEN_SOURCE
#endif
#define _XOPEN_SOURCE 500

#ifdef mingw_PLATFORM
#include "arch/win/win_util.h"
#endif

#ifndef mingw_PLATFORM
#include <ftw.h>
#endif
#include <sys/types.h>
#include <sys/time.h>
#include <utime.h>

#include "plugin.h"
#include "dcache_ops.h"

/* Initialization, deinitialization and management */
int  dcache_init(struct libltfs_plugin *plugin, const struct dcache_options *options,
		struct ltfs_volume *vol);
int  dcache_destroy(struct ltfs_volume *vol);
int  dcache_parse_options(const char **options, struct dcache_options **out);
void dcache_free_options(struct dcache_options **options);
bool dcache_initialized(struct ltfs_volume *vol);
int  dcache_mkcache(const char *name, struct ltfs_volume *vol);
int  dcache_rmcache(const char *name, struct ltfs_volume *vol);
int  dcache_cache_exists(const char *name, bool *exists, struct ltfs_volume *vol);
int  dcache_set_workdir(const char *workdir, bool clean, struct ltfs_volume *vol);
int  dcache_get_workdir(char **workdir, struct ltfs_volume *vol);
int  dcache_assign_name(const char *name, struct ltfs_volume *vol);
int  dcache_unassign_name(struct ltfs_volume *vol);
int  dcache_wipe_dentry_tree(struct ltfs_volume *vol);
int  dcache_get_vol_uuid(const char *work_dir, const char *barcode, char **uuid, struct ltfs_volume *vol);
int  dcache_set_vol_uuid(char *uuid, struct ltfs_volume *vol);
int  dcache_get_generation(const char *work_dir, const char *barcode, unsigned int *gen, struct ltfs_volume *vol);
int  dcache_set_generation(unsigned int gen, struct ltfs_volume *vol);
int  dcache_get_dirty(const char *work_dir, const char *barcode, bool *dirty, struct ltfs_volume *vol);
int  dcache_set_dirty(bool dirty, struct ltfs_volume *vol);
int  dcache_is_sharable(bool *sharable, struct ltfs_volume *vol);

/* Disk image management */
int dcache_diskimage_create(struct ltfs_volume *vol);
int dcache_diskimage_remove(struct ltfs_volume *vol);
int dcache_diskimage_mount(struct ltfs_volume *vol);
int dcache_diskimage_unmount(struct ltfs_volume *vol);
bool dcache_diskimage_is_full(struct ltfs_volume *vol);

/* Advisory lock operations */
int dcache_get_advisory_lock(const char *name, struct ltfs_volume *vol);
int dcache_put_advisory_lock(const char *name, struct ltfs_volume *vol);

/* File system operations */
int  dcache_open(const char *path, struct dentry **d, struct ltfs_volume *vol);
int  dcache_openat(const char *parent_path, struct dentry *parent, const char *name,
		struct dentry **result, struct ltfs_volume *vol);
int  dcache_close(struct dentry *d, bool lock_meta, bool descend, struct ltfs_volume *vol);
int  dcache_create(const char *path, struct dentry *d, struct ltfs_volume *vol);
int  dcache_unlink(const char *path, struct dentry *d, struct ltfs_volume *vol);
int  dcache_rename(const char *oldpath, const char *newpath, struct dentry **old_dentry,
		struct ltfs_volume *vol);
int  dcache_flush(struct dentry *d, enum dcache_flush_flags flags, struct ltfs_volume *vol);
int  dcache_readdir(struct dentry *d, bool dentries, void ***result, struct ltfs_volume *vol);
int dcache_read_direntry(struct dentry *d, struct ltfs_direntry *dirent, unsigned long index,  struct ltfs_volume *vol);
int  dcache_setxattr(const char *path, struct dentry *d, const char *xattr, const char *value,
		size_t size, int flags, struct ltfs_volume *vol);
int  dcache_removexattr(const char *path, struct dentry *d, const char *xattr,
		struct ltfs_volume *vol);
int  dcache_listxattr(const char *path, struct dentry *d, char *list, size_t size,
		struct ltfs_volume *vol);
int  dcache_getxattr(const char *path, struct dentry *d, const char *name,
		void *value, size_t size, struct ltfs_volume *vol);

/* Helper operations */
int  dcache_get_dentry(struct dentry *d, struct ltfs_volume *vol);
int  dcache_put_dentry(struct dentry *d, struct ltfs_volume *vol);

/* Out of sync state management */
int  dcache_is_out_of_sync(bool *out_of_sync, struct ltfs_volume *vol);
int  dcache_force_to_sync(struct ltfs_volume *vol);

#ifdef __cplusplus
}
#endif


#endif /* __dcache_h */
