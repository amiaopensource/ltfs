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
** FILE NAME:       ltfs_fuse.c
**
** DESCRIPTION:     Implements the interface of LTFS with FUSE.
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
**
** Copyright (C) 2012 OSR Open Systems Resources, Inc.
** 
************************************************************************************* 
*/

/*
 * OSR
 *
 * If _FILE_OFFSET_BITS_SET_FTRUNCATE is not defined, MinGW will
 * replace any instance of ftruncate with ftruncate64. In this
 * module that results in our usage of the ftruncate member
 * field of the fuse_operations being turned into ftruncate64,
 * which leads to a compiler error
*/
#ifdef HP_mingw_BUILD
#define _FILE_OFFSET_BITS_SET_FTRUNCATE 1
#endif /* HP_mingw_BUILD */

#include "ltfs_fuse.h"
#include "libltfs/ltfs_fsops.h"
#include "libltfs/iosched.h"
#include "libltfs/pathname.h"
#include "libltfs/xattr.h"
#include "libltfs/periodic_sync.h"
#include "libltfs/arch/time_internal.h"
#include "libltfs/arch/errormap.h"
#include "libltfs/kmi.h"

#ifdef mingw_PLATFORM
#include "libltfs/arch/win/win_util.h"
#include "libltfs/ltfs_internal.h"
#endif

#if (__WORDSIZE == 64)
#define FILEHANDLE_TO_STRUCT(fh) ((struct ltfs_file_handle *)(uint64_t)(fh))
#define STRUCT_TO_FILEHANDLE(de) ((uint64_t)(de))
#else
#define FILEHANDLE_TO_STRUCT(fh) ((struct ltfs_file_handle *)(uint32_t)(fh))
#define STRUCT_TO_FILEHANDLE(de) ((uint64_t)(uint32_t)(de))
#endif

/* 
 * OSR 
 *  
 * In our MinGW environment, fuse_get_context actually exists as 
 * a callable function 
 *  
 */
#ifdef mingw_PLATFORM
#ifndef HP_mingw_BUILD
static struct fuse_context *context;
#define fuse_get_context() context
#endif /* HP_mingw_BUILD */
#endif /* mingw_PLATFORM */
#define FUSE_REQ_ENTER(r)   REQ_NUMBER(REQ_STAT_ENTER, REQ_FUSE, r)
#define FUSE_REQ_EXIT(r)    REQ_NUMBER(REQ_STAT_EXIT,  REQ_FUSE, r)

struct ltfs_file_handle *_new_ltfs_file_handle(struct file_info *fi)
{
	int ret;
	struct ltfs_file_handle *file = calloc(1, sizeof(struct ltfs_file_handle));
	if (! file) {
		ltfsmsg(LTFS_ERR, "10001E", "file structure");
		return NULL;
	}
	ret = ltfs_mutex_init(&file->lock);
	if (ret) {
		ltfsmsg(LTFS_ERR, "10002E", ret);
		free(file);
		return NULL;
	}
	file->file_info = fi;
	file->dirty = false;
	return file;
}

void _free_ltfs_file_handle(struct ltfs_file_handle *file)
{
	if (file) {
		ltfs_mutex_destroy(&file->lock);
		free(file);
	}
}

static struct file_info *_new_file_info(const char *path)
{
	int ret;
	struct file_info *fi = calloc(1, sizeof(struct file_info));
	if (! fi) {
		ltfsmsg(LTFS_ERR, "10001E", __FUNCTION__);
		return NULL;
	}
	ret = ltfs_mutex_init(&fi->lock);
	if (ret) {
		ltfsmsg(LTFS_ERR, "10002E", ret);
		free(fi);
		return NULL;
	}
	if (path) {
		fi->path = strdup(path);
		if (! fi->path) {
			ltfsmsg(LTFS_ERR, "10001E", "_new_file_info: path");
			ltfs_mutex_destroy(&fi->lock);
			free(fi);
			return NULL;
		}
	}
	fi->open_count = 1;
	return fi;
}

static void _free_file_info(struct file_info *fi)
{
	if (fi) {
		if (fi->path)
			free(fi->path);
		ltfs_mutex_destroy(&fi->lock);
		free(fi);
	}
}

/**
 * Retrieve file handle information for a dentry.
 * If no handle information exists, it is allocated and saved.
 * The open_file structure returned from this function should be released later using
 * _file_close().
 * @param path Path used to open this file. May be NULL.
 * @param d File handle to get information for. If NULL, a dummy handle information structure
 *          is returned.
 * @param spare A preallocated open_file structure. If present, it will be used instead of
 *              allocating memory. May be NULL.
 * @param priv LTFS private data.
 * @return File handle information, or NULL if memory allocation failed or if 'priv' is NULL.
 */
static struct file_info *_file_open(const char *path, void *d, struct file_info *spare,
	struct ltfs_fuse_data *priv)
{
	struct file_info *fi = NULL;
	CHECK_ARG_NULL(priv, NULL);
	ltfs_mutex_lock(&priv->file_table_lock);
	if (priv->file_table)
		HASH_FIND_PTR(priv->file_table, &d, fi);
	if (! fi) {
		fi = spare ? spare : _new_file_info(path);
		if (! fi) {
			ltfs_mutex_unlock(&priv->file_table_lock);
			return NULL;
		}
		fi->dentry_handle = d;
		HASH_ADD_PTR(priv->file_table, dentry_handle, fi);
	} else {
		ltfs_mutex_lock(&fi->lock);
		fi->open_count++;
		ltfs_mutex_unlock(&fi->lock);
	}
	ltfs_mutex_unlock(&priv->file_table_lock);
	return fi;
}

/**
 * Release a file_info structure obtained using _file_open().
 * The file_info structure is freed if there are no references left.
 */
static void _file_close(struct file_info *fi, struct ltfs_fuse_data *priv)
{
	bool do_free = false;
	if (fi && priv) {
		ltfs_mutex_lock(&priv->file_table_lock);
		ltfs_mutex_lock(&fi->lock);
		fi->open_count--;
		if (fi->open_count == 0) {
			HASH_DEL(priv->file_table, fi);
			do_free = true;
		}
		ltfs_mutex_unlock(&fi->lock);
		ltfs_mutex_unlock(&priv->file_table_lock);
		if (do_free)
			_free_file_info(fi);
	}
}

const char *_dentry_name(const char *path, struct file_info *fi)
{
	if (path)
		return path;
	else if (fi->path)
		return fi->path;
	else
		return "(unnamed)";
}

static void _ltfs_fuse_attr_to_stat(struct stat *stbuf, struct dentry_attr *attr,
	struct ltfs_fuse_data *priv)
{
	stbuf->st_dev = LTFS_SUPER_MAGIC;
	stbuf->st_ino = attr->uid;
	if (attr->isslink) {
#ifndef HP_mingw_BUILD
		stbuf->st_mode = S_IFLNK | 0777;
#else
		stbuf->st_mode = 0777;
#endif /* HP_mingw_BUILD */
	} else {
		stbuf->st_mode = ((attr->isdir ? S_IFDIR : S_IFREG) | (attr->readonly ? 0555 : 0777)) &
			(attr->isdir ? priv->dir_mode : priv->file_mode);
	}
	stbuf->st_nlink = attr->nlink;
	stbuf->st_rdev = 0; /* no special files on LTFS volumes */
	if (priv->perm_override) {
		stbuf->st_uid = priv->mount_uid;
		stbuf->st_gid = priv->mount_gid;
	} else {
		stbuf->st_uid = fuse_get_context()->uid;
		stbuf->st_gid = fuse_get_context()->gid;
	}
	stbuf->st_size = attr->size;
	stbuf->st_blksize = attr->blocksize;
	stbuf->st_blocks = (attr->alloc_size + 511) / 512; /* this field is in 512-byte units */

#ifdef __APPLE__
	stbuf->st_atimespec = timespec_from_ltfs_timespec(&attr->access_time);
	stbuf->st_mtimespec = timespec_from_ltfs_timespec(&attr->modify_time);
	stbuf->st_ctimespec = timespec_from_ltfs_timespec(&attr->change_time);
	stbuf->st_birthtimespec = timespec_from_ltfs_timespec(&attr->create_time);
#else
	stbuf->st_atim = timespec_from_ltfs_timespec(&attr->access_time);
	stbuf->st_mtim = timespec_from_ltfs_timespec(&attr->modify_time);
	stbuf->st_ctim = timespec_from_ltfs_timespec(&attr->change_time);
#endif
}

int ltfs_fuse_fgetattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	struct ltfs_file_handle *file = FILEHANDLE_TO_STRUCT(fi->fh);
	struct dentry_attr attr;
	int ret;

#if 0
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_FGETATTR), (uint64_t)fi, 0);
#endif /* 0 */

	ltfsmsg(LTFS_DEBUG3, "14030D", _dentry_name(path, file->file_info));

	ret = ltfs_fsops_getattr(file->file_info->dentry_handle, &attr, priv->data);

	if (ret == 0)
		_ltfs_fuse_attr_to_stat(stbuf, &attr, priv);

#if 0
	ltfs_request_trace(FUSE_REQ_EXIT(REQ_FGETATTR), ret,
					   ((struct dentry *)(file->file_info->dentry_handle))->uid);
#endif /* 0 */

	return errormap_fuse_error(ret);
}

int ltfs_fuse_getattr(const char *path, struct stat *stbuf)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	struct dentry_attr attr;
	ltfs_file_id id;
	int ret;

#if 0
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_GETATTR), 0, 0);
#endif /* 0 */

	ltfsmsg(LTFS_DEBUG3, "14031D", path);

	ret = ltfs_fsops_getattr_path(path, &attr, &id, priv->data);

	if (ret == 0)
		_ltfs_fuse_attr_to_stat(stbuf, &attr, priv);

#if 0
	ltfs_request_trace(FUSE_REQ_EXIT(REQ_GETATTR), ret, id.uid);
#endif /* 0 */

	return errormap_fuse_error(ret);
}


int ltfs_fuse_access(const char *path, int mode)
{
#if 0
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_ACCESS), 0, 0);
	ltfs_request_trace(FUSE_REQ_EXIT(REQ_ACCESS), 0, 0);
#endif /* 0 */
	return 0;
}

int ltfs_fuse_statfs(const char *path, struct statvfs *buf)
{
	/*
	 * OSR
	 *
	 * We support the statvfs structure in our MinGW environmnet
	 */
#if !defined(mingw_PLATFORM) || defined(HP_mingw_BUILD)
	int ret = 0;
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	struct statvfs *stats = &priv->fs_stats;
	struct device_capacity blockstat;

#if 0
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_STATFS), 0, 0);
#endif /* 0 */

	memset(&blockstat, 0, sizeof(blockstat));

	ret = ltfs_capacity_data(&blockstat, priv->data);
	if (ret < 0) {
#if 0
		ltfs_request_trace(FUSE_REQ_EXIT(REQ_STATFS), ret, 0);
#endif /* 0 */
		return errormap_fuse_error(ret);
	}

	stats->f_blocks = blockstat.total_dp;           /* Total tape capacity */
	stats->f_bfree = blockstat.remaining_dp;        /* Remaining tape capacity */
	stats->f_bavail = stats->f_bfree;               /* Blocks available for normal user (ignored) */
	stats->f_files = ltfs_get_file_count(priv->data);

	stats->f_ffree = UINT32_MAX - stats->f_files;   /* Assuming file count fits in 32 bits. */
	memcpy(buf, stats, sizeof(struct statvfs));

#ifdef __APPLE__
	/* With MacFUSE, we use an f_frsize not equal to the file system block size.
	 * Need to adjust the block counts so they're in units of the reported f_frsize. */
	double scale = ltfs_get_blocksize(priv->data) / (double)stats->f_frsize;
	buf->f_blocks *= scale;
	buf->f_bfree  *= scale;
	buf->f_bavail *= scale;
#endif /* __APPLE__ */

#if 0
	ltfs_request_trace(FUSE_REQ_EXIT(REQ_STATFS), 0, 0);
#endif /* 0 */

#endif /* !defined(mingw_PLATFORM) || defined(HP_mingw_BUILD) */

	return errormap_fuse_error(ret);;
}

int ltfs_fuse_open(const char *path, struct fuse_file_info *fi)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	struct ltfs_file_handle *file;
	struct file_info *file_info;
	void *dentry_handle;
	int ret;
	bool open_write;

#if 0
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_OPEN), (uint64_t)fi->flags, 0);
#endif /* 0 */

	if ((fi->flags & O_WRONLY) == O_WRONLY)
		ltfsmsg(LTFS_DEBUG, "14032D", path, "write-only");
	else if ((fi->flags & O_RDWR) == O_RDWR)
		ltfsmsg(LTFS_DEBUG, "14032D", path, "read-write");
	else /* read-only */
		ltfsmsg(LTFS_DEBUG, "14032D", path, "read-only");
	open_write = (((fi->flags & O_WRONLY) == O_WRONLY) || ((fi->flags & O_RDWR) == O_RDWR));

	/* Open the file */
	ret = ltfs_fsops_open(path, open_write, true, (struct dentry **)&dentry_handle, priv->data);
	if (ret < 0) {
#if 0
		ltfs_request_trace(FUSE_REQ_EXIT(REQ_OPEN), ret, 0);
#endif /* 0 */
		return errormap_fuse_error(ret);
	}

	/* Get file information and create a file handle */
	file_info = _file_open(path, dentry_handle, NULL, priv);
	if (file_info)
		file = _new_ltfs_file_handle(file_info);
	if (! file_info || ! file) {
		if (file_info)
			_file_close(file_info, priv);
		ltfs_fsops_close(dentry_handle, false, open_write, true, priv->data);
#if 0
		ltfs_request_trace(FUSE_REQ_EXIT(REQ_OPEN), -ENOMEM, 0);
#endif /* 0 */
		return errormap_fuse_error(-LTFS_NO_MEMORY);
	}

	fi->fh = STRUCT_TO_FILEHANDLE(file);

#ifdef __APPLE__
    /* Comment from MacFUSE author about direct_io on OSX:
     * direct_io is a rather abnormal mode of operation from Mac OS X's
     * standpoint. Unless your file system requires this mode, I wouldn't
     * recommend using this option.
     */
    fi->direct_io  = 0;
    fi->keep_cache = 0;
#else
#if FUSE_VERSION <= 27
	/* for FUSE <= 2.7, set direct_io when opening for write */
	if (((fi->flags & O_WRONLY) == O_WRONLY) || ((fi->flags & O_RDWR) == O_RDWR))
		fi->direct_io = 1;
	fi->keep_cache = 0;
#else
	/* cannot set keep cache if any process has the file open with direct_io set! so only
	 * set it on newer FUSE versions, where we don't use direct_io. */
	fi->direct_io = 0;
	fi->keep_cache = 1;
#endif
#endif
	
#if 0
	ltfs_request_trace(FUSE_REQ_EXIT(REQ_OPEN), 0,
					   ((struct dentry *)(file->file_info->dentry_handle))->uid);
#endif /* 0 */

	return errormap_fuse_error(0);
}

int ltfs_fuse_release(const char *path, struct fuse_file_info *fi)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	struct ltfs_file_handle *file = FILEHANDLE_TO_STRUCT(fi->fh);
	bool dirty, write_index, open_write;
	uint64_t uid;
	int ret;

#if 0
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_RELEASE), 0, 0);
#endif /* 0 */

	ltfsmsg(LTFS_DEBUG, "14035D", _dentry_name(path, file->file_info));

	uid = ((struct dentry *)(file->file_info->dentry_handle))->uid;

	/* Should this file's buffers be flushed? */
	ltfs_mutex_lock(&file->lock);
	dirty = file->dirty;
	ltfs_mutex_unlock(&file->lock);

	/* Should an index be written? */
	ltfs_mutex_lock(&file->file_info->lock);
	write_index = (priv->sync_type == LTFS_SYNC_CLOSE) ? file->file_info->write_index : false;
	ltfs_mutex_unlock(&file->file_info->lock);

	open_write = (((fi->flags & O_WRONLY) == O_WRONLY) || ((fi->flags & O_RDWR) == O_RDWR));
	ret = ltfs_fsops_close(file->file_info->dentry_handle, dirty, open_write, true, priv->data);
	if (write_index)
		ltfs_sync_index(SYNC_CLOSE, true, priv->data);

	_file_close(file->file_info, priv);
	_free_ltfs_file_handle(file);

#if 0
	ltfs_request_trace(FUSE_REQ_EXIT(REQ_RELEASE), ret, uid);
#endif /* 0 */

	return errormap_fuse_error(ret);
}

int ltfs_fuse_opendir(const char *path, struct fuse_file_info *fi)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	struct ltfs_file_handle *file;
	struct file_info *file_info;
	void *dentry_handle;
	int ret = 0;
	bool open_write;

#if 0
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_OPENDIR), (uint64_t)fi->flags, 0);
#endif /* 0 */

	ltfsmsg(LTFS_DEBUG, "14033D", path);

	open_write = (((fi->flags & O_WRONLY) == O_WRONLY) || ((fi->flags & O_RDWR) == O_RDWR));

	/* Open the file */
	ret = ltfs_fsops_open(path, open_write, false, (struct dentry **)&dentry_handle,
						  priv->data);
	if (ret < 0) {
#if 0
		ltfs_request_trace(FUSE_REQ_EXIT(REQ_OPENDIR), ret, 0);
#endif /* 0 */
		return errormap_fuse_error(ret);
	}

	/* Get file information and create a file handle */
	file_info = _file_open(path, dentry_handle, NULL, priv);
	if (file_info)
		file = _new_ltfs_file_handle(file_info);
	if (! file_info || ! file) {
		if (file_info)
			_file_close(file_info, priv);
		ltfs_fsops_close(dentry_handle, false, false, false, priv->data);
#if 0
		ltfs_request_trace(FUSE_REQ_EXIT(REQ_OPENDIR), -ENOMEM, 0);
#endif /* 0 */
		return errormap_fuse_error(-LTFS_NO_MEMORY);
	}

	fi->fh = STRUCT_TO_FILEHANDLE(file);

#if 0
	ltfs_request_trace(FUSE_REQ_EXIT(REQ_OPENDIR), 0,
					   ((struct dentry *)(file->file_info->dentry_handle))->uid);
#endif /* 0 */

	return errormap_fuse_error(0);
}

int ltfs_fuse_releasedir(const char *path, struct fuse_file_info *fi)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	struct ltfs_file_handle *file = FILEHANDLE_TO_STRUCT(fi->fh);
	uint64_t uid;
	int ret;

#if 0
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_RELEASEDIR), 0, 0);
#endif /* 0 */

	ltfsmsg(LTFS_DEBUG, "14034D", _dentry_name(path, file->file_info));

	uid = ((struct dentry *)(file->file_info->dentry_handle))->uid;

	ret = ltfs_fsops_close(file->file_info->dentry_handle, false, false, false, priv->data);

	_file_close(file->file_info, priv);
	_free_ltfs_file_handle(file);
#if 0
	ltfs_request_trace(FUSE_REQ_EXIT(REQ_RELEASEDIR), ret, uid);
#endif /* 0 */

	return errormap_fuse_error(ret);
}

/* TODO: treat this like a regular fsync? */
int ltfs_fuse_fsyncdir(const char *path, int flags, struct fuse_file_info *fi)
{
#if 0
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_FSYNCDIR), 0, 0);
	ltfs_request_trace(FUSE_REQ_EXIT(REQ_FSYNCDIR), 0, 0);
#endif /* 0 */
	return 0;
}

static int _ltfs_fuse_do_flush(struct ltfs_file_handle *file, struct ltfs_fuse_data *priv,
	const char *caller)
{
	bool dirty;
	int ret = 0;

	ltfs_mutex_lock(&file->lock);
	dirty = file->dirty;
	ltfs_mutex_unlock(&file->lock);

	if (dirty) {
		ret = ltfs_fsops_flush(file->file_info->dentry_handle, false, priv->data);
		if (ret < 0)
			ltfsmsg(LTFS_ERR, "14022E", caller);
		else {
			ltfs_mutex_lock(&file->lock);
			file->dirty = false;
			ltfs_mutex_unlock(&file->lock);
		}
	}

	return errormap_fuse_error(ret);
}

int ltfs_fuse_fsync(const char *path, int isdatasync, struct fuse_file_info *fi)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	struct ltfs_file_handle *file = FILEHANDLE_TO_STRUCT(fi->fh);
	uint64_t uid;
	int ret;

#if 0
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_FSYNC), (uint64_t)isdatasync, 0);
#endif /* 0 */

	ltfsmsg(LTFS_DEBUG, "14036D", _dentry_name(path, file->file_info));
	uid = ((struct dentry *)(file->file_info->dentry_handle))->uid;
	ret = _ltfs_fuse_do_flush(file, priv, __FUNCTION__);

#if 0
	ltfs_request_trace(FUSE_REQ_EXIT(REQ_FSYNC), ret, uid);
#endif /* 0 */

	return errormap_fuse_error(ret);
}

int ltfs_fuse_flush(const char *path, struct fuse_file_info *fi)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	struct ltfs_file_handle *file = FILEHANDLE_TO_STRUCT(fi->fh);
	uint64_t uid;
	int ret;

#if 0
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_FLUSH), 0, 0);
#endif /* 0 */

	ltfsmsg(LTFS_DEBUG, "14037D", _dentry_name(path, file->file_info));
	uid = ((struct dentry *)(file->file_info->dentry_handle))->uid;
	ret = _ltfs_fuse_do_flush(file, priv, __FUNCTION__);

#if 0
	ltfs_request_trace(FUSE_REQ_EXIT(REQ_FLUSH), ret, uid);
#endif /* 0 */

	return errormap_fuse_error(ret);
}

int ltfs_fuse_utimens(const char *path, const struct timespec ts[2])
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	struct ltfs_timespec tsTmp[2];
	ltfs_file_id id;
	int ret = 0;

#if 0
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_UTIMENS), 0, 0);
#endif /* 0 */

	tsTmp[0] = ltfs_timespec_from_timespec(&ts[0]);
	tsTmp[1] = ltfs_timespec_from_timespec(&ts[1]);

#ifdef HP_mingw_BUILD
	if (tsTmp[0].tv_sec == 0 && tsTmp[0].tv_nsec == 0
			&& tsTmp[1].tv_sec == 0 && tsTmp[1].tv_nsec == 0) {
		ltfsmsg(LTFS_WARN, "14117W");
		return errormap_fuse_error(ret);
	}
#endif /* HP_mingw_BUILD */

	ltfsmsg(LTFS_DEBUG, "14038D", path);
	ret = ltfs_fsops_utimens_path(path, tsTmp, &id, priv->data);

#if 0
	ltfs_request_trace(FUSE_REQ_EXIT(REQ_UTIMENS), ret, id.uid);
#endif /* 0 */

	return errormap_fuse_error(ret);
}

/**
 * Change the mode of a file or directory. Since LTFS does not support full Unix permissions,
 * this function just sets or clears the read-only flag.
 */
int ltfs_fuse_chmod(const char *path, mode_t mode)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	ltfs_file_id id;
	int ret;
	bool new_readonly = (mode & 0222) ? false : true;

#if 0
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_CHMOD), (uint64_t)mode, 0);
#endif /* 0 */

	ltfsmsg(LTFS_DEBUG, "14039D", path);
	ret = ltfs_fsops_set_readonly_path(path, new_readonly, &id, priv->data);

#if 0
	ltfs_request_trace(FUSE_REQ_EXIT(REQ_CHMOD), ret, id.uid);
#endif /* 0 */

	return errormap_fuse_error(ret);
}

/**
 * Set ownership of a file or directory. Succeeds, but has no effect: user/group are
 * controlled by mount-time options uid and gid.
 */
int ltfs_fuse_chown(const char *path, uid_t user, gid_t group)
{
#if 0
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_CHOWN), ((uint64_t)user << 32) + group, 0);
	ltfs_request_trace(FUSE_REQ_EXIT(REQ_CHOWN), 0, 0);
#endif /* 0 */
	return 0;
}

int ltfs_fuse_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	struct ltfs_file_handle *file;
	struct file_info *file_info, *new_file_info;
	void *dentry_handle; /* might be a dentry or a dentry_proxy */
	bool readonly;
	int ret;

#if 0
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_CREATE), (uint64_t)fi->flags, 0);
#endif /* 0 */

	ltfsmsg(LTFS_DEBUG, "14040D", path);

	readonly = ! (mode & priv->file_mode & 0222);

	/* Allocate file handle and information */
	file = _new_ltfs_file_handle(NULL);
	if (! file) {
#if 0
		ltfs_request_trace(FUSE_REQ_EXIT(REQ_CREATE), -ENOMEM, 0);
#endif /* 0 */
		return errormap_fuse_error(-LTFS_NO_MEMORY);
	}
	file_info = _new_file_info(path);
	if (! file_info) {
		_free_ltfs_file_handle(file);
#if 0
		ltfs_request_trace(FUSE_REQ_EXIT(REQ_CREATE), -ENOMEM, 1);
#endif /* 0 */
		return errormap_fuse_error(-LTFS_NO_MEMORY);
	}

	/* Create the file */
	ret = ltfs_fsops_create(path, false, readonly, (struct dentry **)&dentry_handle,
							priv->data);
	if (ret < 0) {
		_free_file_info(file_info);
		_free_ltfs_file_handle(file);
#if 0
		ltfs_request_trace(FUSE_REQ_EXIT(REQ_CREATE), ret, 0);
#endif /* 0 */
		return errormap_fuse_error(ret);
	}

	/* Save handle */
	new_file_info = _file_open(path, dentry_handle, file_info, priv);
	if (file_info != new_file_info)
		_free_file_info(file_info);
	file->file_info = new_file_info;

	fi->fh = STRUCT_TO_FILEHANDLE(file);

#ifdef __APPLE__
    /* Comment from MacFUSE author about direct_io on OSX:
     * direct_io is a rather abnormal mode of operation from Mac OS X's
     * standpoint. Unless your file system requires this mode, I wouldn't
     * recommend using this option.
     */
    fi->direct_io  = 0;
    fi->keep_cache = 0;
#else
#if FUSE_VERSION <= 27
	/* for FUSE <= 2.7, set direct_io when creating */
	fi->direct_io = 1;
	fi->keep_cache = 0;
#else
	/* cannot set keep cache if any process has the file open with direct_io set! so only
	 * set it on newer FUSE versions, where we don't use direct_io. */
	fi->direct_io = 0;
	fi->keep_cache = 1;
#endif
#endif

#if 0
	ltfs_request_trace(FUSE_REQ_EXIT(REQ_CREATE), 0,
					   ((struct dentry *)(file->file_info->dentry_handle))->uid);
#endif /* 0 */

	return errormap_fuse_error(0);
}

int ltfs_fuse_mkdir(const char *path, mode_t mode)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	void *dentry_handle;
	uint64_t uid = 0;
	int ret;

#if 0
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_MKDIR), (uint64_t)mode, 0);
#endif /* 0 */

	ltfsmsg(LTFS_DEBUG, "14041D", path);

	ret = ltfs_fsops_create(path, true, false, (struct dentry **)&dentry_handle, priv->data);
	if (ret == 0) {
		uid = ((struct dentry *)dentry_handle)->uid;
		ltfs_fsops_close(dentry_handle, false, false, false, priv->data);
	}
	
#if 0
	ltfs_request_trace(FUSE_REQ_EXIT(REQ_MKDIR), ret, uid);
#endif /* 0 */

	return errormap_fuse_error(ret);
}

int ltfs_fuse_truncate(const char *path, off_t length)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	ltfs_file_id id;
	int ret = 0;

#if 0
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_TRUNCATE), (uint64_t)length, 0);
#endif /* 0 */

	ltfsmsg(LTFS_DEBUG, "14042D", path, (long long)length);

	ret = ltfs_fsops_truncate_path(path, length, &id, priv->data);

#if 0
	ltfs_request_trace(FUSE_REQ_EXIT(REQ_TRUNCATE), ret, id.uid);
#endif /* 0 */

	return errormap_fuse_error(ret);
}

int ltfs_fuse_ftruncate(const char *path, off_t length, struct fuse_file_info *fi)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	struct ltfs_file_handle *file = FILEHANDLE_TO_STRUCT(fi->fh);
	int ret;

#if 0
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_FTRUNCATE), (uint64_t)length, 0);
#endif /* 0 */

	ltfsmsg(LTFS_DEBUG, "14043D", _dentry_name(path, file->file_info), (long long) length);

	ret = ltfs_fsops_truncate(file->file_info->dentry_handle, length, priv->data);

#if 0
	ltfs_request_trace(FUSE_REQ_EXIT(REQ_FTRUNCATE), ret,
					   ((struct dentry *)(file->file_info->dentry_handle))->uid);
#endif /* 0 */

	return errormap_fuse_error(ret);
}

int ltfs_fuse_unlink(const char *path)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	ltfs_file_id id;
	int ret;

#if 0
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_UNLINK), 0, 0);
#endif /* 0 */

	ltfsmsg(LTFS_DEBUG, "14044D", path);

	ret = ltfs_fsops_unlink(path, &id, priv->data);

#if 0
	ltfs_request_trace(FUSE_REQ_EXIT(REQ_UNLINK), ret, id.uid);
#endif /* 0 */

	return errormap_fuse_error(ret);
}

int ltfs_fuse_rmdir(const char *path)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	ltfs_file_id id;
	int ret;

#if 0
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_RMDIR), 0, 0);
#endif /* 0 */

	ltfsmsg(LTFS_DEBUG, "14045D", path);

	ret = ltfs_fsops_unlink(path, &id, priv->data);

#if 0
 	ltfs_request_trace(FUSE_REQ_EXIT(REQ_RMDIR), ret, id.uid);
#endif /* 0 */

	return errormap_fuse_error(ret);
}

int ltfs_fuse_rename(const char *from, const char *to)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	ltfs_file_id id;
	int ret;

#if 0
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_RENAME), 0, 0);
#endif /* 0 */

	ltfsmsg(LTFS_DEBUG, "14046D", from, to);

	ret = ltfs_fsops_rename(from, to, &id, priv->data);

#if 0
	ltfs_request_trace(FUSE_REQ_EXIT(REQ_RENAME), ret, id.uid);
#endif /* 0 */

	return errormap_fuse_error(ret);
}

int _ltfs_fuse_filldir(void *buf, const char *name, void *priv)
{
	int ret;
	char *new_name;
	fuse_fill_dir_t filler = priv;

	ret = pathname_unformat(name, &new_name);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "14027E", "unformat", ret);
		return ret;
	}

#ifdef __APPLE__
	if (new_name)
		free(new_name); new_name = NULL;

	ret = pathname_nfd_normaize(name, &new_name);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "14027E", "nfd", ret);
		if (new_name)
			free(new_name); new_name = NULL;
		return ret;
	}

	ret = filler(buf, new_name, NULL, 0);
#else
	ret = filler(buf, name, NULL, 0);
#endif

	if (new_name)
		free(new_name); new_name = NULL;
	if (ret)
		return -ENOBUFS;
	return 0;
}

int ltfs_fuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
	off_t offset, struct fuse_file_info *fi)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	struct ltfs_file_handle *file = FILEHANDLE_TO_STRUCT(fi->fh);
	int ret;

#if 0
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_READDIR), (uint64_t)offset, 0);
#endif /* 0 */

	ltfsmsg(LTFS_DEBUG, "14047D", _dentry_name(path, file->file_info));

	if (filler(buf, ".",  NULL, 0)) {
		/* No buffer space */
		ltfsmsg(LTFS_DEBUG, "14026D");
		return errormap_fuse_error(-LTFS_NO_MEMORY);
	}
	if (filler(buf, "..", NULL, 0)) {
		/* No buffer space */
		return errormap_fuse_error(-LTFS_NO_MEMORY);
		return -ENOBUFS;
	}

	ret = ltfs_fsops_readdir(file->file_info->dentry_handle, buf, _ltfs_fuse_filldir,
							 filler, priv->data);

#if 0
	ltfs_request_trace(FUSE_REQ_EXIT(REQ_READDIR), ret,
					   ((struct dentry *)(file->file_info->dentry_handle))->uid);
#endif /* 0 */

	return errormap_fuse_error(ret);
}

int ltfs_fuse_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	struct ltfs_file_handle *file = FILEHANDLE_TO_STRUCT(fi->fh);
	int ret;

#if 0
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_WRITE), (uint64_t)offset, (uint64_t)size);
#endif /* 0 */

	ltfsmsg(LTFS_DEBUG3, "14048D", _dentry_name(path, file->file_info), (long long)offset, size);

	ret = ltfs_fsops_write(file->file_info->dentry_handle, buf, size, offset, true, priv->data);

	if (ret == -LTFS_NO_SPACE)
		ret = 0;

	if (ret == 0) {
		ltfs_mutex_lock(&file->lock);
		file->dirty = true;
		ltfs_mutex_unlock(&file->lock);

		ltfs_mutex_lock(&file->file_info->lock);
		file->file_info->write_index = true;
		ltfs_mutex_unlock(&file->file_info->lock);

#if 0
		ltfs_request_trace(FUSE_REQ_EXIT(REQ_WRITE), (uint64_t)size,
						   ((struct dentry *)(file->file_info->dentry_handle))->uid);
#endif /* 0 */

		return size;
	} else {
#if 0
		ltfs_request_trace(FUSE_REQ_EXIT(REQ_WRITE), (uint64_t)ret,
						   ((struct dentry *)(file->file_info->dentry_handle))->uid);
#endif /* 0 */
		return errormap_fuse_error(ret);
	}
}

int ltfs_fuse_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	struct ltfs_file_handle *file = FILEHANDLE_TO_STRUCT(fi->fh);
	int ret;

#if 0
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_READ), (uint64_t)offset, (uint64_t)size);
#endif /* 0 */

	ltfsmsg(LTFS_DEBUG3, "14049D", _dentry_name(path, file->file_info), (long long)offset, size);

	ret = ltfs_fsops_read(file->file_info->dentry_handle, buf, size, offset, priv->data);

#if 0
	ltfs_request_trace(FUSE_REQ_EXIT(REQ_READ), (uint64_t)ret,
					   ((struct dentry *)(file->file_info->dentry_handle))->uid);
#endif /* 0 */

	return errormap_fuse_error(ret);
}

#ifdef __APPLE__
int ltfs_fuse_setxattr(const char *path, const char *name, const char *value, size_t size,
	int flags, uint32_t position)
#else
int ltfs_fuse_setxattr(const char *path, const char *name, const char *value, size_t size,
	int flags)
#endif /* __APPLE__ */
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	ltfs_file_id id;
	int ret;

#if 0
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_SETXATTR), (uint64_t)size, 0);
#endif /* 0 */

	ltfsmsg(LTFS_DEBUG3, "14050D", path, name, size);

	/* position argument is only supported for resource forks
	 * on OS X, and we have no resource forks
	 * TODO: is it correct to behave this way?
	 */
#ifdef __APPLE__
	if (position) {
		/* Position argument must be zero */
		ltfsmsg(LTFS_ERR, "14023E");
#if 0
		ltfs_request_trace(FUSE_REQ_EXIT(REQ_SETXATTR), -EINVAL, 0);
#endif /* 0 */
		return errormap_fuse_error(-LTFS_NULL_ARG);
	}
#endif /* __APPLE__ */

	ret = ltfs_fsops_setxattr(path, name, value, size, flags, &id, priv->data);

#if 0
	ltfs_request_trace(FUSE_REQ_EXIT(REQ_SETXATTR), ret, id.uid);
#endif /* 0 */

	return errormap_fuse_error(ret);
}

#ifdef __APPLE__
int ltfs_fuse_getxattr(const char *path, const char *name, char *value, size_t size,
	uint32_t position)
#else
int ltfs_fuse_getxattr(const char *path, const char *name, char *value, size_t size)
#endif /* __APPLE__ */
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	ltfs_file_id id;
	int ret;

#if 0
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_GETXATTR), (uint64_t)size, 0);
#endif /* 0 */

	ltfsmsg(LTFS_DEBUG3, "14051D", path, name);

	/* position argument is only supported for resource forks
	 * on OS X, and we have no resource forks
	 * TODO: is it correct to behave this way?
	 */
#ifdef __APPLE__
	if (position) {
		/* Position argument must be zero */
		ltfsmsg(LTFS_ERR, "14024E");
#if 0
		ltfs_request_trace(FUSE_REQ_EXIT(REQ_GETXATTR), -EINVAL, 0);
#endif /* 0 */

		return errormap_fuse_error(-LTFS_NULL_ARG);
	}
#else
	/* Short-circuit requests for system EAs to avoid mounting the same unnecessarily in
	 * library mode. */
	if (strstr(name, "system.") == name || strstr(name, "security.") == name) {
#if 0
		ltfs_request_trace(FUSE_REQ_EXIT(REQ_GETXATTR), -LTFS_NO_XATTR, 0);
#endif /* 0 */
		return errormap_fuse_error(-LTFS_NO_XATTR);
	}
#endif /* __APPLE__ */

	ret = ltfs_fsops_getxattr(path, name, value, size, &id, priv->data);

#if 0
	ltfs_request_trace(FUSE_REQ_EXIT(REQ_GETXATTR), ret, id.uid);
#endif /* 0 */

	return errormap_fuse_error(ret);
}

int ltfs_fuse_listxattr(const char *path, char *list, size_t size)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	ltfs_file_id id;
	int ret;
	
#if 0
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_LISTXATTR), (uint64_t)size, 0);
#endif /* 0 */

	ltfsmsg(LTFS_DEBUG, "14052D", path);

	ret = ltfs_fsops_listxattr(path, list, size, &id, priv->data);

#if 0
	ltfs_request_trace(FUSE_REQ_EXIT(REQ_LISTXATTR), ret, id.uid);
#endif /* 0 */

	return errormap_fuse_error(ret);
}

int ltfs_fuse_removexattr(const char *path, const char *name)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	ltfs_file_id id;
	int ret;

#if 0
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_REMOVEXATTR), 0, 0);
#endif /* 0 */

	ltfsmsg(LTFS_DEBUG, "14053D", path, name);

	ret = ltfs_fsops_removexattr(path, name, &id, priv->data);

#if 0
	ltfs_request_trace(FUSE_REQ_EXIT(REQ_REMOVEXATTR), ret, id.uid);
#endif /* 0 */

	return errormap_fuse_error(ret);
}

/**
 * Mount the filesystem. This function assumes a volume has been
 * allocated and ltfs_mount has been called; it just does some secondary setup.
 */
void * ltfs_fuse_mount(struct fuse_conn_info *conn)
{
	int						ret = 0;
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	struct statvfs *stats = &priv->fs_stats;
#ifdef HP_mingw_BUILD
	int						iter = 0;
	char					*index_rules_utf8 = NULL;
#endif /* HP_mingw_BUILD */

#if 0
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_MOUNT), 0, 0);
#endif /* 0 */

#ifdef HP_mingw_BUILD
	/*
	 * OSR
	 *
	 * In our MinGW environmnet, we need to determine at this point
	 * if there's a valid tape in the drive. Thus, multiple routines
	 * previously performed during main() processing are now
	 * performed here
	 *
	 */
	if (ltfs_device_open(priv->devname, priv->driver_plugin.ops, priv->data) < 0) {
		/* Could not open device */
		ltfsmsg(LTFS_ERR, "10004E", priv->devname);
		conn->reserved[0] = -LTFS_UNSUPPORTED_MEDIUM;
		return NULL;
	}

	if (ltfs_parse_tape_backend_opts(priv->args, priv->data)) {
		/* Backend option parsing failed */
		ltfsmsg(LTFS_ERR, "14012E");
		conn->reserved[0] = -LTFS_UNSUPPORTED_MEDIUM;
		ltfs_device_close(priv->data);
		return NULL;
	}

	/* Check EOD validation is skipped or not */
	if ( priv->skip_eod_check ) {
		ltfsmsg(LTFS_INFO, "14076I");
		ltfsmsg(LTFS_INFO, "14077I");
		priv->data->skip_eod_check = priv->skip_eod_check;
	}

	/* Setup tape drive.  Trap and handle the special case of no media present... */
	ret = ltfs_setup_device(priv->data);
	if ((ret == -EDEV_NO_MEDIUM) || (ret == -LTFS_NO_MEDIUM)) {
		ltfsmsg(LTFS_ERR, "14075E");
		conn->reserved[0] = -LTFS_NO_MEDIUM;
		ltfs_device_close(priv->data);
		return NULL;

	} else if (ret < 0) {
		ltfsmsg(LTFS_ERR, "14075E");
		conn->reserved[0] = -LTFS_UNSUPPORTED_MEDIUM;
		ltfs_device_close(priv->data);
		return NULL;
	}

	/* If the index is NULL then we are returning NULL setting the error code as
	 * invalid index.This generally happens when user tries to mount an inconsistent
	 * tape with huge index.
	 */
	if (! priv->data->index) {
		conn->reserved[0] = -LTFS_INDEX_INVALID;
		ltfs_device_close(priv->data);
		return NULL;
	}

	ret = ltfs_mount(false, false, false, false, priv->rollback_gen, priv->data);
	if (ret < 0) {
		/* The return type -LTFS_NO_MEMORY happens when memory allocation fails */
		if (ret == -LTFS_NO_MEMORY) {
			conn->reserved[0] = -LTFS_INDEX_INVALID;
			ltfs_index_free_force(&priv->data->index);
			ltfs_device_close(priv->data);
			return NULL;
		} else {
			ltfsmsg(LTFS_ERR, "14013E");
			conn->reserved[0] = ret;
			ltfs_device_close(priv->data);
			return NULL;
		}
	}

	/* Set up index criteria */
	if (priv->index_rules) {
		ret = pathname_format(priv->index_rules, &index_rules_utf8, false, false);
		if (ret < 0) {
			/* Could not format data placement rules. */
			ltfsmsg(LTFS_ERR, "14016E", ret);
			ltfs_volume_free(&priv->data);
			return NULL;
		}
		ret = ltfs_override_policy(index_rules_utf8, false, priv->data);
		free(index_rules_utf8);
		if (ret == -LTFS_POLICY_IMMUTABLE) {
			/* Volume doesn't allow override. Ignoring user-specified criteria. */
			ltfsmsg(LTFS_WARN, "14015W");
		} else if (ret < 0) {
			/* Could not parse data placement rules */
			ltfsmsg(LTFS_ERR, "14017E", ret);
			ltfs_volume_free(&priv->data);
			return NULL;
		}
	}

	/* Configure I/O scheduler cache */
	ltfs_set_scheduler_cache(priv->min_pool_size, priv->max_pool_size, priv->data);

	/* mount read-only if underlying medium is write-protected */
	ret = ltfs_get_tape_readonly(priv->data);
	if (ret < 0 && ret != -LTFS_WRITE_PROTECT && ret != -LTFS_WRITE_ERROR
			&& ret != -LTFS_NO_SPACE &&
		ret != -LTFS_LESS_SPACE) { /* No other errors are expected. */
		/* Could not get read-only status of medium */
		ltfsmsg(LTFS_ERR, "14018E");
		ltfs_volume_free(&priv->data);
		return NULL;
	} else if (ret == -LTFS_WRITE_PROTECT || ret == -LTFS_WRITE_ERROR
			|| ret == -LTFS_NO_SPACE || ret == -LTFS_LESS_SPACE
			|| priv->rollback_gen != 0) {
		if (ret == -LTFS_WRITE_PROTECT || ret == -LTFS_WRITE_ERROR
				|| ret == -LTFS_NO_SPACE) {
			ret = ltfs_get_partition_readonly(
					ltfs_ip_id(priv->data), priv->data);
			if (ret == -LTFS_WRITE_PROTECT || ret == -LTFS_WRITE_ERROR) {
				if (priv->data->rollback_mount) {
					/* The cartridge will be mounted as read-only if a valid generation number is supplied with
					 * rollback_mount
					 */
					ltfsmsg(LTFS_INFO, "14072I", priv->rollback_gen);
				} else {
					if (ltfs_get_tape_logically_readonly(priv->data) == -LTFS_LOGICAL_WRITE_PROTECT) {
						/* The tape is logically write protected i.e. incompatible medium*/
						ltfsmsg(LTFS_INFO, "14118I");
					} else {
						/* The tape is really write protected */
						ltfsmsg(LTFS_INFO, "14019I");
					}
				}
			} else if (ret == -LTFS_NO_SPACE) {
				/* The index partition is in early warning zone.
				 * To be mounted read-only */
				ltfsmsg(LTFS_INFO, "14073I");
			} else { /* 0 or -LTFS_LESS_SPACE */
				/* The data partition may be in early warning zone.
				 * To be mounted read-only */
				ltfsmsg(LTFS_INFO, "14074I");
			}
		} else if (ret == -LTFS_LESS_SPACE)
			ltfsmsg(LTFS_INFO, "14071I");

		ret = fuse_opt_add_arg(priv->args, "-oro");
		if (ret < 0) {
			/* Could not set FUSE option */
			ltfsmsg(LTFS_ERR, "14001E", "ro", ret);
			ltfs_volume_free(&priv->data);
			return NULL;
		}
	}

	/* Setting the drive as write-protected if user mounts as readonly */

	while (iter < priv->args->argc) {
		if (!strcmp(priv->args->argv[iter], "-oro") ||
				(!strcmp(priv->args->argv[iter], "-o") &&
						!strcmp(priv->args->argv[iter+1], "ro"))) {
			ltfs_fsops_set_write_protected(priv->data);
			break;
		}
		iter++;
	}

#else
	if (priv->pid_orig != getpid()) {
		/*
		 * Reopen device when LTFS was forked in fuse_main().
		 * Backend must handle reopen correctly if it sis needed.
		 * For example, iokit backend must handle reopen. But ibmtape backend
		 * doesn't need handle reopen because file descriptor is took over to a child
		 * process.
		 */
		ltfs_device_reopen(priv->devname, priv->data);
	}
#endif /* HP_mingw_BUILD */


	/* Suppress unused variable warning. */
	(void) ret;


	/* Initialize the iosched_handle to NULL before use; it will be checked in
	 * iosched_initialized() so should have a defined default value */
	priv->data->iosched_handle = NULL;

#if !defined(mingw_PLATFORM) || defined(HP_mingw_BUILD)
	/*
	 * Open the I/O scheduler, if one has been specified by the user.
	 * Please note that when we run in library mode the I/O scheduler
	 * is loaded individually for each mounted volume.
	 */
	/*
	 * OSR
	 *
	 * In our MinGW environmnet, we load I/O scheduler at this point
	 *
	 */
	if (iosched_init(&priv->iosched_plugin, priv->data) < 0) {
		/* I/O scheduler disabled. Performance down, memory usage up. */
		ltfsmsg(LTFS_WARN, "14028W");
	}

	/* fill in fixed filesystem stats */
	stats->f_bsize = ltfs_get_blocksize(priv->data); /* Filesystem optimal transfer block size */
	/*
	 * OSR
	 *
	 * In our MinGW environmnet, we support the statvfs structure,
	 * thus we need the block size here
	 *
	 */
#ifdef HP_mingw_BUILD
	stats->f_bsize = priv->data->label->blocksize;
#endif

	/* Filesystem fragment size. Linux allows any f_frsize, whereas OS X (with MacFUSE) expects
	 * a power of 2 between 512 and 131072. */
#ifdef __APPLE__
	int nshift;

	if (stats->f_bsize > 131072)
		stats->f_frsize = 131072;
	else if (stats->f_bsize < 512)
		stats->f_frsize = 512;
	else {
		nshift = 0;
		stats->f_frsize = stats->f_bsize;
		while (stats->f_frsize != 1) {
			stats->f_frsize >>= 1;
			++nshift;
		}
		stats->f_frsize = 1 << nshift;
		if (stats->f_frsize < stats->f_bsize)
			stats->f_frsize <<= 1;
	}

	/* Having f_bsize different from f_frsize should technically be okay, but it
	 * seems that many (most?) programs don't understand the difference. So the only
	 * way to get consistent space usage results is to make them the same. */
	stats->f_bsize = stats->f_frsize;
#else
	stats->f_frsize = stats->f_bsize;
#endif /* __APPLE__ */

	stats->f_favail = 0;                               /* Ignored by FUSE */
	stats->f_flag = 0;                                 /* Ignored by FUSE */
	stats->f_fsid = LTFS_SUPER_MAGIC;                  /* Ignored by FUSE */
	stats->f_namemax = LTFS_FILENAME_MAX;

	ltfsmsg(LTFS_INFO, "14029I");
#endif /* !defined(mingw_PLATFORM) || defined(HP_mingw_BUILD) */

	/* Kick timer thread for sync by time */
	if (priv->sync_type == LTFS_SYNC_TIME)
		periodic_sync_thread_init(priv->sync_time, priv->data);

	/* If user has selected to capture the index, we do that here. */
	if (priv->capture_index)
		ltfs_save_index_to_disk(priv->work_directory, NULL, false, priv->data);

#if 0
	ltfs_trace_set_work_dir(priv->work_directory);
	ltfs_request_trace(FUSE_REQ_EXIT(REQ_MOUNT), (uint64_t)priv, 0);
#endif /* 0 */

	return priv;
}

/**
 * Unmount a filesystem. This function flushes all data to tape, makes the cartridge consistent,
 * closes the device, and frees the ltfs_volume field of the FUSE private data.
 */
void ltfs_fuse_umount(void *userdata)
{
	struct ltfs_fuse_data *priv = userdata;

#if 0
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_UNMOUNT), 0, 0);
#endif /* 0 */

	if (periodic_sync_thread_initialized(priv->data))
		periodic_sync_thread_destroy(priv->data);

	/*
	 * Destroy the I/O scheduler, if one has been specified by the user.
	 * Please note that when we run in library mode the I/O scheduler
	 * is destroyed individually for each mounted volume.
	 */
	ltfs_fsops_flush(NULL, true, priv->data);
	if (iosched_initialized(priv->data))
		iosched_destroy(priv->data);

	if (kmi_initialized(priv->data))
		kmi_destroy(priv->data);

	ltfs_unmount(SYNC_UNMOUNT, priv->data);

	if (priv->capture_index)
		ltfs_save_index_to_disk(priv->work_directory, SYNC_UNMOUNT, false, priv->data);

#if 0
	ltfs_request_trace(FUSE_REQ_EXIT(REQ_UNMOUNT), 0, 0);
#endif /* 0 */
	
	/*
	 * OSR
	 *
	 * In our MinGW environment, we're called here to actually
	 * dismount the device. Thus, we'll eject and clean up at this
	 * point instead of doing it in main()
	 *
	 */
#ifdef HP_mingw_BUILD
	if (priv->eject)
		ltfs_eject_tape(priv->data);

	ltfs_device_close(priv->data);
#endif
}

int ltfs_fuse_symlink(const char* to, const char* from)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	ltfs_file_id id;
	int ret;

#if 0
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_SYMLINK), 0, 0);
#endif /* 0 */

	ret = ltfs_fsops_symlink_path(to, from, &id, priv->data);

#if 0
	ltfs_request_trace(FUSE_REQ_EXIT(REQ_SYMLINK), ret, id.uid);
#endif /* 0 */

	return errormap_fuse_error(ret);
}

int ltfs_fuse_readlink(const char* path, char* buf, size_t size)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	ltfs_file_id id;
	int ret;

#if 0
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_READLINK), (uint64_t)size, 0);
#endif /* 0 */

	ret = ltfs_fsops_readlink_path(path, buf, size, &id, priv->data);

#if 0
	ltfs_request_trace(FUSE_REQ_EXIT(REQ_READLINK), ret, id.uid);
#endif /* 0 */

	return errormap_fuse_error(ret);
}

struct fuse_operations ltfs_ops = {
	.init        = ltfs_fuse_mount,
	.destroy     = ltfs_fuse_umount,
	.getattr     = ltfs_fuse_getattr,
	.fgetattr    = ltfs_fuse_fgetattr,
	.access      = ltfs_fuse_access,
	.statfs      = ltfs_fuse_statfs,
	.open        = ltfs_fuse_open,
	.release     = ltfs_fuse_release,
	.fsync       = ltfs_fuse_fsync,
	.flush       = ltfs_fuse_flush,
	.utimens     = ltfs_fuse_utimens,
	.chmod       = ltfs_fuse_chmod,
	.chown       = ltfs_fuse_chown,
	.create      = ltfs_fuse_create,
	.truncate    = ltfs_fuse_truncate,
	.ftruncate   = ltfs_fuse_ftruncate,
	.unlink      = ltfs_fuse_unlink,
	.rename      = ltfs_fuse_rename,
	.mkdir       = ltfs_fuse_mkdir,
	.rmdir       = ltfs_fuse_rmdir,
	.opendir     = ltfs_fuse_opendir,
	.readdir     = ltfs_fuse_readdir,
	.releasedir  = ltfs_fuse_releasedir,
	.fsyncdir    = ltfs_fuse_fsyncdir,
	.write       = ltfs_fuse_write,
	.read        = ltfs_fuse_read,
	.setxattr    = ltfs_fuse_setxattr,
	.getxattr    = ltfs_fuse_getxattr,
	.listxattr   = ltfs_fuse_listxattr,
	.removexattr = ltfs_fuse_removexattr,
	.symlink     = ltfs_fuse_symlink,
	.readlink    = ltfs_fuse_readlink,
#if FUSE_VERSION >= 28
	.flag_nullpath_ok = 1,
#endif
};
