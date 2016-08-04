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
** FILE NAME:       xattr.c
**
** DESCRIPTION:     Implements extended attribute routines.
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
**  (C) Copyright 2015 Hewlett Packard Enterprise Development LP.
**  11/12/12 Change _xattr_get_virtual() to get software product name from ltfs.h
**            rather than using a hardcoded value
**
*************************************************************************************
*/

#ifdef mingw_PLATFORM
#include "arch/win/win_util.h"
#endif
#include "ltfs.h"
#include "ltfs_fsops.h"
#include "xattr.h"
#include "fs.h"
#include "xml_libltfs.h"
#include "pathname.h"
#include "tape.h"
#include "ltfs_internal.h"
#include "arch/time_internal.h"
#include "periodic_sync.h"
#ifdef __APPLE__
#include "arch/osx/osx_string.h"
#endif /* __APPLE__ */

int _xattr_seek(struct xattr_info **out, struct dentry *d, const char *name);
int _xattr_lock_dentry(const char *name, bool modify, struct dentry *d, struct ltfs_volume *vol);
void _xattr_unlock_dentry(const char *name, bool modify, struct dentry *d, struct ltfs_volume *vol);
const char *_xattr_strip_name(const char *name);
int _xattr_list_physicals(struct dentry *d, char *list, size_t size);
bool _xattr_is_virtual(struct dentry *d, const char *name, struct ltfs_volume *vol);
int _xattr_get_virtual(struct dentry *d, char *buf, size_t buf_size, const char *name,
	struct ltfs_volume *vol);
int _xattr_set_virtual(struct dentry *d, const char *name, const char *value,
	size_t size, struct ltfs_volume *vol);
int _xattr_remove_virtual(struct dentry *d, const char *name, struct ltfs_volume *vol);

/* Helper functions for formatting virtual EA output */
int _xattr_get_cartridge_health(cartridge_health_info *h, int64_t *val, char **outval,
	const char *msg, struct ltfs_volume *vol);
int _xattr_get_cartridge_health_u64(cartridge_health_info *h, uint64_t *val, char **outval,
	const char *msg, struct ltfs_volume *vol);
int _xattr_get_cartridge_capacity(struct device_capacity *cap, unsigned long *val, char **outval,
	const char *msg, struct ltfs_volume *vol);
int _xattr_get_time(struct ltfs_timespec *val, char **outval, const char *msg);
int _xattr_get_dentry_time(struct dentry *d, struct ltfs_timespec *val, char **outval,
	const char *msg);
int _xattr_get_string(const char *val, char **outval, const char *msg);
int _xattr_get_tapepos(struct tape_offset *val, char **outval, const char *msg);
int _xattr_get_partmap(struct ltfs_label *label, char **outval, const char *msg);
int _xattr_get_version(int version, char **outval, const char *msg);

int _xattr_set_time(struct dentry *d, struct ltfs_timespec *out, const char *value, size_t size,
	const char *msg, struct ltfs_volume *vol);

int _xattr_get_vendorunique_xattr(char **outval, const char *msg, struct ltfs_volume *vol);
int _xattr_set_vendorunique_xattr(const char *name, const char *value, size_t size, struct ltfs_volume *vol);

int xattr_do_set(struct dentry *d, const char *name, const char *value, size_t size,
	struct xattr_info *xattr)
{
	int ret = 0;

	/* clear existing xattr or set up new one */
	if (xattr) {
		if (xattr->value) {
			free(xattr->value);
			xattr->value = NULL;
		}
	} else {
		xattr = (struct xattr_info *) calloc(1, sizeof(struct xattr_info));
		if (! xattr) {
			ltfsmsg(LTFS_ERR, "10001E", "xattr_do_set: xattr");
			return -LTFS_NO_MEMORY;
		}
		xattr->key = strdup(name);
		if (! xattr->key) {
			ltfsmsg(LTFS_ERR, "10001E", "xattr_do_set: xattr key");
			ret = -LTFS_NO_MEMORY;
			goto out_free;
		}
		TAILQ_INSERT_HEAD(&d->xattrlist, xattr, list);
	}

	/* copy new value */
	xattr->size = size;
	if (size > 0) {
		xattr->value = (char *) calloc(1, size);
		if (! xattr->value) {
			ltfsmsg(LTFS_ERR, "10001E", "xattr_do_set: xattr value");
			ret = -LTFS_NO_MEMORY;
			goto out_remove;
		}
		memcpy(xattr->value, value, size);
	}
	return 0;

out_remove:
	TAILQ_REMOVE(&d->xattrlist, xattr, list);
out_free:
	if (xattr->key)
		free(xattr->key);
	free(xattr);
	return ret;
}

/**
 * Set an extended attribute.
 * @param d File or directory to set the xattr on.
 * @param name Name to set.
 * @param value Value to set, may be binary, not necessarily null-terminated.
 * @param size Size of value in bytes.
 * @param flags XATTR_REPLACE to fail if xattr doesn't exist, XATTR_CREATE to fail if it does
 *              exist, or 0 to ignore any existing value.
 * @return 0 on success or a negative value on error.
 */
int xattr_set(struct dentry *d, const char *name, const char *value, size_t size,
	int flags, struct ltfs_volume *vol)
{
	struct xattr_info *xattr;
	bool replace, create;
	int ret;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(name, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(value, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	if (size > LTFS_MAX_XATTR_SIZE)
		return -LTFS_LARGE_XATTR; /* this is the error returned by ext3 when the xattr is too large */

	replace = flags & XATTR_REPLACE;
	create = flags & XATTR_CREATE;

	ret = _xattr_lock_dentry(name, true, d, vol);
	if (ret < 0)
		return ret;

	/* Check if this is a user-writeable virtual xattr */
	if (_xattr_is_virtual(d, name, vol)) {
		ret = _xattr_set_virtual(d, name, value, size, vol);
		if (ret == -LTFS_NO_XATTR)
			ret = -LTFS_RDONLY_XATTR;
		goto out_unlock;
	}

	/* In the future, there could be user-writeable reserved xattrs. For now, just deny
	 * writes to all reserved xattrs not covered by the user-writeable virtual xattrs above. */
	if (strcasestr(name, "ltfs") == name && strcmp(name, "ltfs.spannedFileOffset") &&
				strcasestr(name, "ltfs.permissions.") != name &&
				strcasestr(name, "ltfs.hash.") != name) {
		ret = -LTFS_RDONLY_XATTR;
		goto out_unlock;
	}

	acquirewrite_mrsw(&d->meta_lock);

	/* Search for existing xattr with this name. */
	ret = _xattr_seek(&xattr, d, name);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "11122E", ret);
		releasewrite_mrsw(&d->meta_lock);
		goto out_unlock;
	}
	if (create && xattr) {
		releasewrite_mrsw(&d->meta_lock);
		ret = -LTFS_XATTR_EXISTS;
		goto out_unlock;
	} else if (replace && ! xattr) {
		releasewrite_mrsw(&d->meta_lock);
		ret = -LTFS_NO_XATTR;
		goto out_unlock;
	}

	/* Set extended attribute */
	ret = xattr_do_set(d, name, value, size, xattr);
	if (ret < 0) {
		releasewrite_mrsw(&d->meta_lock);
		goto out_unlock;
	}

	/* update metadata */
	get_current_timespec(&d->change_time);
	releasewrite_mrsw(&d->meta_lock);

	ltfs_set_index_dirty(true, false, vol->index);

	ret = 0;

out_unlock:
	_xattr_unlock_dentry(name, true, d, vol);
	return ret;
}

/**
 * Get an extended attribute. Returns an error if the provided buffer is not large enough
 * to contain the attribute value.
 * @param d File/directory to check
 * @param name Xattr name
 * @param value On success, contains xattr value
 * @param size Output buffer size in bytes
 * @param vol LTFS volume
 * @return if size is nonzero, number of bytes returned in the value buffer. if size is zero,
 *         number of bytes in the xattr value. returns a negative value on error. If the
 *         operation needs to be restarted, then -LTFS_RESTART_OPERATION is returned instead.
 */
int xattr_get(struct dentry *d, const char *name, char *value, size_t size,
	struct ltfs_volume *vol)
{
	struct xattr_info *xattr = NULL;
	int ret;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(name, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	if (size > 0 && ! value) {
		ltfsmsg(LTFS_ERR, "11123E");
		return -LTFS_BAD_ARG;
	}

	ret = _xattr_lock_dentry(name, false, d, vol);
	if (ret < 0)
		return ret;

	/* Try to get a virtual xattr first. */
	if (_xattr_is_virtual(d, name, vol)) {
		ret = _xattr_get_virtual(d, value, size, name, vol);
		if (ret == -LTFS_DEVICE_FENCED) {
			_xattr_unlock_dentry(name, false, d, vol);
			ret = ltfs_wait_revalidation(vol);
			return (ret == 0) ? -LTFS_RESTART_OPERATION : ret;
		} else if (NEED_REVAL(ret)) {
			_xattr_unlock_dentry(name, false, d, vol);
			ret = ltfs_revalidate(false, vol);
			return (ret == 0) ? -LTFS_RESTART_OPERATION : ret;
		} else if (IS_UNEXPECTED_MOVE(ret)) {
			vol->reval = -LTFS_REVAL_FAILED;
			_xattr_unlock_dentry(name, false, d, vol);
			return ret;
		}else if (ret != -LTFS_NO_XATTR) {
			/* if ltfs.sync is specified, don't print any message */
			if (ret < 0 && ret != -LTFS_RDONLY_XATTR)
				ltfsmsg(LTFS_ERR, "11128E", ret);
			goto out_unlock;
		}
	}

	acquireread_mrsw(&d->meta_lock);

	/* Look for a real xattr. */
	ret = _xattr_seek(&xattr, d, name);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "11129E", ret);
		releaseread_mrsw(&d->meta_lock);
		goto out_unlock;
	}

	/* Generate output. */
	ret = 0;
	if (! xattr) {
		/* There's no such extended attribute */
		ret = -LTFS_NO_XATTR;
	} else if (size && xattr->size > size) {
		/* There is no space to fill the buffer */
		ret = -LTFS_SMALL_BUFFER;
	} else if (size) {
		/* Copy the extended attribute to the requester */
		memcpy(value, xattr->value, xattr->size);
		ret = xattr->size;
	} else /* size is zero */ {
		/* Return how many bytes will be necessary to read this xattr */
		ret = xattr->size;
	}

	releaseread_mrsw(&d->meta_lock);

out_unlock:
	_xattr_unlock_dentry(name, false, d, vol);
	return ret;
}

/**
 * Copy a list of extended attribute names to a user-provided buffer.
 * @param d File/directory to get the list of extended attributes from
 * @param list Output buffer for xattr names
 * @param size Output buffer size in bytes
 * @param vol LTFS volume
 * @return number of bytes in buffer on success, or a negative value on error.
 */
int xattr_list(struct dentry *d, char *list, size_t size, struct ltfs_volume *vol)
{
	int ret, nbytes = 0;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	if (size > 0 && ! list) {
		ltfsmsg(LTFS_ERR, "11130E");
		return -LTFS_BAD_ARG;
	}

	acquireread_mrsw(&d->meta_lock);

	/* Fill the buffer with only real xattrs. */
	if (size)
		memset(list, 0, size);

	ret = _xattr_list_physicals(d, list, size);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "11133E", ret);
		goto out;
	}
	nbytes += ret;

	/*
	 * There used to be an _xattr_list_virtuals function which was called here.
	 * Listing virtual xattrs causes problems with files copied from LTFS to another filesystem
	 * which are attempted to be brought back. Since the copy utility may also copy the
	 * reserved virtual extended attributes, the copy operation will fail with permission
	 * denied problems.
	 */

	/* Was the buffer large enough? */
	if (size && (size_t)nbytes > size)
		ret = -LTFS_SMALL_BUFFER;

out:
	releaseread_mrsw(&d->meta_lock);
	if (ret < 0)
		return ret;
	return nbytes;
}

/**
 * Actually remove an extended attribute.
 * @param dentry dentry to operate on
 * @param name xattr name to delete
 * @param force true to force removal, false to verify namespaces first
 * @param vol LTFS volume
 * @return 0 on success or a negative value on error
 */
int xattr_do_remove(struct dentry *d, const char *name, bool force, struct ltfs_volume *vol)
{
	int ret;
	struct xattr_info *xattr;

	acquirewrite_mrsw(&d->meta_lock);

	/* Look for a real extended attribute. */
	ret = _xattr_seek(&xattr, d, name);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "11140E", ret);
		releasewrite_mrsw(&d->meta_lock);
		return ret;
	} else if (! xattr) {
		releasewrite_mrsw(&d->meta_lock);
		return -LTFS_NO_XATTR;
	}

	if (! force) {
		/* If this xattr is in the reserved namespace, the user can't remove it. */
		/* TODO: in the future, there could be user-removable reserved xattrs. */
		if (strcasestr(name, "ltfs") == name && strcmp(name, "ltfs.spannedFileOffset") &&
						strcasestr(name, "ltfs.permissions.") != name &&
						strcasestr(name, "ltfs.hash.") != name) {
			releasewrite_mrsw(&d->meta_lock);
			return -LTFS_RDONLY_XATTR;
		}
	}

	/* Remove the xattr. */
	TAILQ_REMOVE(&d->xattrlist, xattr, list);
	get_current_timespec(&d->change_time);
	releasewrite_mrsw(&d->meta_lock);

	free(xattr->key);
	if (xattr->value)
		free(xattr->value);
	free(xattr);

	return 0;
}

/**
 * Remove an extended attribute.
 * @param d File/directory to operate on
 * @param name Extended attribute name to delete
 * @param vol LTFS volume
 * @return 0 on success or a negative value on error
 */
int xattr_remove(struct dentry *d, const char *name, struct ltfs_volume *vol)
{
	int ret;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(name, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);

	ret = _xattr_lock_dentry(name, true, d, vol);
	if (ret < 0)
		return ret;

	/* If this xattr is virtual, try the virtual removal function. */
	if (_xattr_is_virtual(d, name, vol)) {
		ret = _xattr_remove_virtual(d, name, vol);
		if (ret == -LTFS_NO_XATTR)
			ret = -LTFS_RDONLY_XATTR; /* non-removable virtual xattr */
		goto out_dunlk;
	}

	ret = xattr_do_remove(d, name, false, vol);

	ltfs_set_index_dirty(true, false, vol->index);

out_dunlk:
	_xattr_unlock_dentry(name, true, d, vol);
	return ret;
}


/**
 * set LTFS_LIVELINK_EA_NAME
 * @param path file path
 * @param d File operate on
 * @param vol LTFS volume
 * @return 0 on success or a negative value on error
 */
int xattr_set_mountpoint_length(struct dentry *d, const char* value, size_t size )
{
#ifdef POSIXLINK_ONLY
	return 0;
#else
	int ret=0;
	struct xattr_info *xattr = NULL;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(value, -LTFS_NULL_ARG);

	acquireread_mrsw(&d->meta_lock);
	ret = _xattr_seek(&xattr, d, LTFS_LIVELINK_EA_NAME);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "11129E", ret);
		releaseread_mrsw(&d->meta_lock);
		goto out_set;
	}
	ret = xattr_do_set(d, LTFS_LIVELINK_EA_NAME, value, size, xattr);
	releaseread_mrsw(&d->meta_lock);

out_set:
	return ret;
#endif
}


/**
 * Search for an xattr with the given name. Must call this function with a lock on
 * the dentry's meta_lock.
 * @param out On success, points to the xattr that was found.
 * @param d Dentry to search.
 * @param name Name to search for.
 * @return 1 if xattr was found, 0 if not, or a negative value on error.
 */
int _xattr_seek(struct xattr_info **out, struct dentry *d, const char *name)
{
	struct xattr_info *entry;

	*out = NULL;
	TAILQ_FOREACH(entry, &d->xattrlist, list) {
		if (! strcmp(entry->key, name)) {
			*out = entry;
			break;
		}
	}

	if (*out)
		return 1;
	else
		return 0;
}

/**
 * Take the volume lock and dentry contents_lock as appropriate for the EA and the access type.
 * @param name EA name being read or written.
 * @param modify True if EA will be set or deleted, false if it will be read.
 * @param d Dentry under consideration.
 * @param vol LTFS volume.
 * @return 0 on success or -LTFS_REVAL_FAILED if the medium is invalid.
 */
int _xattr_lock_dentry(const char *name, bool modify, struct dentry *d, struct ltfs_volume *vol)
{
	/* EAs that read the extent list need to take the contents_lock */
	if (! strcmp(name, "ltfs.startblock")
		|| ! strcmp(name, "ltfs.partition")) {
		acquireread_mrsw(&d->contents_lock);
	}

	/* Other EAs either need no additional locks, or they need the meta_lock.
	 * The caller is responsible for taking the meta_lock as necessary. */
	return 0;
}

/**
 * Undo locking performed in _xattr_lock_dentry.
 * @param name EA name being read or written.
 * @param modify True if EA was set or deleted, false if it was read.
 * @param d Dentry under consideration.
 * @param vol LTFS volume.
 */
void _xattr_unlock_dentry(const char *name, bool modify, struct dentry *d, struct ltfs_volume *vol)
{
	/* EAs that read the extent list need to take the contents_lock */
	if (! strcmp(name, "ltfs.startblock")
		|| ! strcmp(name, "ltfs.partition")) {
		releaseread_mrsw(&d->contents_lock);
	}
}

/**
 * Strip a Linux namespace prefix from the given xattr name and return the position of the suffix.
 * If the name is "user.X", return the "X" portion. Otherwise, return an error.
 * This function does nothing on Mac OS X.
 * @param name Name to strip.
 * @return A pointer to the name suffix, or NULL to indicate an invalid name. On Mac OS X,
 *         always returns @name.
 */
const char *_xattr_strip_name(const char *name)
{
#if (defined (__APPLE__) || defined (mingw_PLATFORM))
	return name;
#else
	if (strstr(name, "user.") == name)
		return name + 5;
	else
		return NULL;
#endif
}

/**
 * List real extended attributes for a dentry. Must be called with a read lock held on
 * the dentry's meta_lock.
 * @param d Dentry to list.
 * @param list Output buffer.
 * @param size Output buffer size, may be 0.
 * @return Number of bytes in listed extended attributes, or a negative value on error.
 */
int _xattr_list_physicals(struct dentry *d, char *list, size_t size)
{
	struct xattr_info *entry;
	char *prefix = "\0", *new_name;
	int prefixlen = 0, namelen;
	int ret = 0, nbytes = 0;

#if ((!defined (__APPLE__)) && (!defined (mingw_PLATFORM)))
	ret = pathname_unformat("user.", &prefix);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "11141E", ret);
		return ret;
	}
	prefixlen = strlen(prefix);
#endif /* (!defined (__APPLE__)) && (!defined (mingw_PLATFORM)) */

	TAILQ_FOREACH(entry, &d->xattrlist, list) {
		ret = pathname_unformat(entry->key, &new_name);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, "11142E", ret);
			goto out;
		}

		if(strncmp(new_name, LTFS_LIVELINK_EA_NAME, strlen(LTFS_LIVELINK_EA_NAME) + 1)) {
			namelen = strlen(new_name);

			nbytes += prefixlen + namelen + 1;
			if (size && (size_t)nbytes <= size) {
				memcpy(list, prefix, prefixlen);
				list += prefixlen;
				memcpy(list, new_name, namelen);
				list += namelen + 1;
			}
		}
		free(new_name);
	}

out:
#if ((!defined (__APPLE__)) && (!defined (mingw_PLATFORM)))
	free(prefix);
#endif /* (!defined (__APPLE__)) && (!defined (mingw_PLATFORM)) */
	if (ret < 0)
		return ret;
	return nbytes;
}

/**
 * Determine whether an extended attribute name exists and is virtual for a given dentry.
 * @param d Dentry to check.
 * @param name Name to check.
 * @param vol LTFS volume to which the dentry belongs.
 * @return true if the name exists and is virtual, false otherwise.
 */
bool _xattr_is_virtual(struct dentry *d, const char *name, struct ltfs_volume *vol)
{
	/* xattrs on all dentries */
	if (! strcmp(name, "ltfs.createTime")
		|| ! strcmp(name, "ltfs.modifyTime")
		|| ! strcmp(name, "ltfs.accessTime")
		|| ! strcmp(name, "ltfs.changeTime")
		|| ! strcmp(name, "ltfs.backupTime")
		|| ! strcmp(name, "ltfs.fileUID")
		|| ! strcmp(name, "ltfs.volumeUUID")
		|| ! strcmp(name, "ltfs.volumeName")
		|| ! strcmp(name, "ltfs.driveCaptureDump")
		|| ! strcmp(name, "ltfs.softwareVersion")
		|| ! strcmp(name, "ltfs.softwareFormatSpec")
		|| ! strcmp(name, "ltfs.softwareVendor")
		|| ! strcmp(name, "ltfs.softwareProduct")

		/* xattrs: MAM extended attributes */
		|| ! strcmp(name, "ltfs.mamApplicationVendor")
		|| ! strcmp(name, "ltfs.mamApplicationName")
		|| ! strcmp(name, "ltfs.mamApplicationVersion")
		|| ! strcmp(name, "ltfs.mamApplicationFormatVersion")
		|| ! strcmp(name, "ltfs.mamVolumeName")
		|| ! strcmp(name, "ltfs.mamBarcode")
		)
		return true;

	/* xattrs on files */
	if (! d->isdir) {
		if (! TAILQ_EMPTY(&d->extentlist)
			&& (! strcmp(name, "ltfs.partition") || ! strcmp(name, "ltfs.startblock")))
			return true;
	}

	/* xattrs on the root dentry */
	if (d == vol->index->root) {
		if (vol->index->index_criteria.have_criteria && ! strcmp(name, "ltfs.policyMaxFileSize"))
			return true;
		if (! strcmp(name, "ltfs.commitMessage")
			|| ! strcmp(name, "ltfs.indexVersion")
			|| ! strcmp(name, "ltfs.labelVersion")
			|| ! strcmp(name, "ltfs.sync")
			|| ! strcmp(name, "ltfs.indexGeneration")
			|| ! strcmp(name, "ltfs.indexTime")
			|| ! strcmp(name, "ltfs.policyExists")
			|| ! strcmp(name, "ltfs.policyAllowUpdate")
			|| ! strcmp(name, "ltfs.volumeFormatTime")
			|| ! strcmp(name, "ltfs.volumeBlocksize")
			|| ! strcmp(name, "ltfs.volumeCompression")
			|| ! strcmp(name, "ltfs.indexLocation")
			|| ! strcmp(name, "ltfs.indexPrevious")
			|| ! strcmp(name, "ltfs.indexCreator")
			|| ! strcmp(name, "ltfs.labelCreator")
			|| ! strcmp(name, "ltfs.partitionMap")
			|| ! strcmp(name, "ltfs.volumeSerial")
			|| ! strcmp(name, "ltfs.mediaLoads")
			|| ! strcmp(name, "ltfs.mediaRecoveredWriteErrors")
			|| ! strcmp(name, "ltfs.mediaPermanentWriteErrors")
			|| ! strcmp(name, "ltfs.mediaRecoveredReadErrors")
			|| ! strcmp(name, "ltfs.mediaPermanentReadErrors")
			|| ! strcmp(name, "ltfs.mediaPreviousPermanentWriteErrors")
			|| ! strcmp(name, "ltfs.mediaPreviousPermanentReadErrors")
			|| ! strcmp(name, "ltfs.mediaBeginningMediumPasses")
			|| ! strcmp(name, "ltfs.mediaMiddleMediumPasses")
			|| ! strcmp(name, "ltfs.mediaEfficiency")
			|| ! strcmp(name, "ltfs.mediaStorageAlert")
			|| ! strcmp(name, "ltfs.mediaDatasetsWritten")
			|| ! strcmp(name, "ltfs.mediaDatasetsRead")
			|| ! strcmp(name, "ltfs.mediaMBWritten")
			|| ! strcmp(name, "ltfs.mediaMBRead")
			|| ! strcmp(name, "ltfs.mediaDataPartitionTotalCapacity")
			|| ! strcmp(name, "ltfs.mediaDataPartitionAvailableSpace")
			|| ! strcmp(name, "ltfs.mediaIndexPartitionTotalCapacity")
			|| ! strcmp(name, "ltfs.mediaIndexPartitionAvailableSpace")
			|| ! strcmp(name, "ltfs.mediaEncrypted")
			|| ! strcmp(name, "ltfs.driveEncryptionState")
			|| ! strcmp(name, "ltfs.driveEncryptionMethod")
			/* Vendor specific EAs */
			|| ! strcmp(name, "ltfs.vendor.IBM.referencedBlocks")
			|| ! strcmp(name, "ltfs.vendor.IBM.trace")
			|| ! strcmp(name, "ltfs.vendor.IBM.totalBlocks")
			|| ! strcmp(name, "ltfs.vendor.IBM.cartridgeMountNode")
			|| ! strcmp(name, "ltfs.vendor.IBM.logLevel")
			|| ! strcmp(name, "ltfs.vendor.IBM.syslogLevel")
			|| ! strncmp(name, "ltfs.vendor", strlen("ltfs.vendor")))
			return true;
	}

	return false;
}

/**
 * Get the value of a virtual extended attribute.
 * @param d Dentry to check.
 * @param buf Output buffer.
 * @param buf_size Output buffer size, may be zero.
 * @param name Name to check for.
 * @param vol LTFS volume
 * @return Number of bytes in output buffer (or if buf_size==0, number of bytes needed for output),
 *         -LTFS_NO_XATTR if no such readable virtual xattr exists,
 *         -LTFS_RDONLY_XATTR for write-only virtual EAs, or another negative value on error.
 */
int _xattr_get_virtual(struct dentry *d, char *buf, size_t buf_size, const char *name,
	struct ltfs_volume *vol)
{
	int ret = -LTFS_NO_XATTR;
	char *val = NULL;
	struct index_criteria *ic = &vol->index->index_criteria;
	cartridge_health_info h = {
		.mounts           = UNSUPPORTED_CARTRIDGE_HEALTH,
		.written_ds       = UNSUPPORTED_CARTRIDGE_HEALTH,
		.write_temps      = UNSUPPORTED_CARTRIDGE_HEALTH,
		.write_perms      = UNSUPPORTED_CARTRIDGE_HEALTH,
		.read_ds          = UNSUPPORTED_CARTRIDGE_HEALTH,
		.read_temps       = UNSUPPORTED_CARTRIDGE_HEALTH,
		.read_perms       = UNSUPPORTED_CARTRIDGE_HEALTH,
		.write_perms_prev = UNSUPPORTED_CARTRIDGE_HEALTH,
		.read_perms_prev  = UNSUPPORTED_CARTRIDGE_HEALTH,
		.written_mbytes   = UNSUPPORTED_CARTRIDGE_HEALTH,
		.read_mbytes      = UNSUPPORTED_CARTRIDGE_HEALTH,
		.passes_begin     = UNSUPPORTED_CARTRIDGE_HEALTH,
		.passes_middle    = UNSUPPORTED_CARTRIDGE_HEALTH,
		.tape_efficiency  = UNSUPPORTED_CARTRIDGE_HEALTH,
	};
	uint64_t tape_alert = 0;
	uint64_t append_pos = 0;
	struct device_capacity cap;

	/* EAs on all dentries */
	if (! strcmp(name, "ltfs.createTime")) {
		ret = _xattr_get_dentry_time(d, &d->creation_time, &val, name);
		if (ret == LTFS_TIME_OUT_OF_RANGE) {
			ltfsmsg(LTFS_WARN, "17222W", name, d->name, d->uid, d->creation_time);
			ret = 0;
		}
	} else if (! strcmp(name, "ltfs.modifyTime")) {
		ret = _xattr_get_dentry_time(d, &d->modify_time, &val, name);
		if (ret == LTFS_TIME_OUT_OF_RANGE) {
			ltfsmsg(LTFS_WARN, "17222W", name, d->name, d->uid, d->modify_time);
			ret = 0;
		}
	} else if (! strcmp(name, "ltfs.accessTime")) {
		ret = _xattr_get_dentry_time(d, &d->access_time, &val, name);
		if (ret == LTFS_TIME_OUT_OF_RANGE) {
			ltfsmsg(LTFS_WARN, "17222W", name, d->name, d->uid, d->access_time);
			ret = 0;
		}
	} else if (! strcmp(name, "ltfs.changeTime")) {
		ret = _xattr_get_dentry_time(d, &d->change_time, &val, name);
		if (ret == LTFS_TIME_OUT_OF_RANGE) {
			ltfsmsg(LTFS_WARN, "17222W", name, d->name, d->uid, d->change_time);
			ret = 0;
		}
	} else if (! strcmp(name, "ltfs.backupTime")) {
		ret = _xattr_get_dentry_time(d, &d->backup_time, &val, name);
		if (ret == LTFS_TIME_OUT_OF_RANGE) {
			ltfsmsg(LTFS_WARN, "17222W", name, d->name, d->uid, d->backup_time);
			ret = 0;
		}
	} else if (! strcmp(name, "ltfs.driveCaptureDump")) {
		ret = tape_takedump_drive(vol->device);
	} else if (! strcmp(name, "ltfs.fileUID")) {
		ret = _xattr_get_u64(d->uid, &val, name);
	} else if (! strcmp(name, "ltfs.volumeUUID")) {
		ret = _xattr_get_string(vol->label->vol_uuid, &val, name);
	} else if (! strcmp(name, "ltfs.volumeName")) {
		ltfs_mutex_lock(&vol->index->dirty_lock);
		ret = _xattr_get_string(vol->index->volume_name, &val, name);
		ltfs_mutex_unlock(&vol->index->dirty_lock);
	} else if (! strcmp(name, "ltfs.softwareVersion")) {
		ret = _xattr_get_string(PACKAGE_VERSION, &val, name);
	} else if (! strcmp(name, "ltfs.softwareFormatSpec")) {
		ret = _xattr_get_string(LTFS_INDEX_VERSION_STR, &val, name);
	} else if (! strcmp(name, "ltfs.softwareVendor")) {
		ret = _xattr_get_string(LTFS_VENDOR_NAME, &val, name);
	} else if (! strcmp(name, "ltfs.softwareProduct")) {
		ret = _xattr_get_string(SOFTWARE_PRODUCT_NAME, &val, name);
	} else if (! strcmp(name, "ltfs.driveCaptureDump")) {
		ret = tape_takedump_drive(vol->device);
	} else if (!strcmp(name, "ltfs.mamApplicationVendor")) {
		ret = _xattr_get_string(vol->mam_attr.appl_vendor, &val, name);
	} else if (!strcmp(name, "ltfs.mamApplicationName")) {
		ret = _xattr_get_string(vol->mam_attr.appl_name, &val, name);
	} else if (!strcmp(name, "ltfs.mamApplicationVersion")) {
		ret = _xattr_get_string(vol->mam_attr.appl_ver, &val, name);
	} else if (!strcmp(name, "ltfs.mamApplicationFormatVersion")) {
		ret = _xattr_get_string(vol->mam_attr.appl_format_ver, &val, name);
	} else if (!strcmp(name, "ltfs.mamVolumeName")) {
		ret = _xattr_get_string(vol->mam_attr.volume_name, &val, name);
	} else if (!strcmp(name, "ltfs.mamBarcode")) {
		ret = _xattr_get_string(vol->mam_attr.barcode, &val, name);
	} else if (! strcmp(name, "ltfs.vendor.IBM.logLevel")) {
		ret = asprintf(&val, "%d", ltfs_log_level);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, "10001E", name);
			val = NULL;
			ret = -LTFS_NO_MEMORY;
		}
	} else if (! strcmp(name, "ltfs.vendor.IBM.syslogLevel")) {
		ret = asprintf(&val, "%d", ltfs_syslog_level);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, "10001E", name);
			val = NULL;
			ret = -LTFS_NO_MEMORY;
		}
	} else if (! strcmp(name, "ltfs.vendor.IBM.profiler")) {
		ret = ltfs_trace_get_offset(&val);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, "10001E", name);
			val = NULL;
			ret = -LTFS_NO_MEMORY;
		}
	}

/* Separate implementation for MAM attributes exist. */
#if 0
	} else if (! strcmp(name, "ltfs.mamBarcode")) {
		ret = read_tape_attribute (vol, &val, name);
		if (ret < 0) {
			ltfsmsg(LTFS_DEBUG, "17198D", TC_MAM_BARCODE, "_xattr_get_virtual");
			val = NULL;
		}
	} else if (! strcmp(name, "ltfs.mamApplicationVendor")) {
		ret = read_tape_attribute (vol, &val, name);
		if (ret < 0) {
			ltfsmsg(LTFS_DEBUG, "17198D", TC_MAM_APP_VENDER, "_xattr_get_virtual");
			val = NULL;
		}
	} else if (! strcmp(name, "ltfs.mamApplicationVersion")) {
		ret = read_tape_attribute (vol, &val, name);
		if (ret < 0) {
			ltfsmsg(LTFS_DEBUG, "17198D", TC_MAM_APP_VERSION, "_xattr_get_virtual");
			val = NULL;
		}
	} else if (! strcmp(name, "ltfs.mamApplicationFormatVersion")) {
		ret = read_tape_attribute (vol, &val, name);
		if (ret < 0) {
			ltfsmsg(LTFS_DEBUG, "17198D", TC_MAM_APP_FORMAT_VERSION, "_xattr_get_virtual");
			val = NULL;
		}
	}
#endif /* 0 */

	/* EAs on non-empty files */
	if (ret == -LTFS_NO_XATTR && ! d->isdir && ! TAILQ_EMPTY(&d->extentlist)) {
		if (! strcmp(name, "ltfs.partition")) {
			ret = 0;
			val = malloc(2 * sizeof(char));
			if (! val) {
				ltfsmsg(LTFS_ERR, "10001E", name);
				ret = -LTFS_NO_MEMORY;
			} else {
				val[0] = TAILQ_FIRST(&d->extentlist)->start.partition;
				val[1] = '\0';
			}
		} else if (! strcmp(name, "ltfs.startblock")) {
			ret = _xattr_get_u64(TAILQ_FIRST(&d->extentlist)->start.block, &val, name);
		}
	}

	/* EAs on root dentry */
	if (ret == -LTFS_NO_XATTR && d == vol->index->root) {
		if (! strcmp(name, "ltfs.commitMessage")) {
			ltfs_mutex_lock(&vol->index->dirty_lock);
			ret = _xattr_get_string(vol->index->commit_message, &val, name);
			ltfs_mutex_unlock(&vol->index->dirty_lock);
		} else if (! strcmp(name, "ltfs.volumeSerial")) {
			ret = _xattr_get_string(vol->label->barcode, &val, name);
		} else if (! strcmp(name, "ltfs.volumeFormatTime")) {
			ret = _xattr_get_time(&vol->label->format_time, &val, name);
			if (ret == LTFS_TIME_OUT_OF_RANGE) {
				ltfsmsg(LTFS_WARN, "17222W", name, "root", 0, vol->label->format_time.tv_sec);
				ret = 0;
			}
		} else if (! strcmp(name, "ltfs.volumeBlocksize")) {
			ret = _xattr_get_u64(vol->label->blocksize, &val, name);
		} else if (! strcmp(name, "ltfs.indexGeneration")) {
			ret = _xattr_get_u64(vol->index->generation, &val, name);
		} else if (! strcmp(name, "ltfs.indexTime")) {
			ret = _xattr_get_time(&vol->index->mod_time, &val, name);
			if (ret == LTFS_TIME_OUT_OF_RANGE) {
				ltfsmsg(LTFS_WARN, "17222W", name, "root", 0, vol->label->format_time.tv_sec);
				ret = 0;
			}
		} else if (! strcmp(name, "ltfs.policyExists")) {
			ret = _xattr_get_string(ic->have_criteria ? "true" : "false", &val, name);
		} else if (! strcmp(name, "ltfs.policyAllowUpdate")) {
			ret = _xattr_get_string(vol->index->criteria_allow_update ? "true" : "false",
				&val, name);
		} else if (! strcmp(name, "ltfs.policyMaxFileSize") && ic->have_criteria) {
			ret = _xattr_get_u64(ic->max_filesize_criteria, &val, name);
		} else if (! strcmp(name, "ltfs.volumeCompression")) {
			ret = _xattr_get_string(vol->label->enable_compression ? "true" : "false", &val, name);
		} else if (! strcmp(name, "ltfs.indexLocation")) {
			ret = _xattr_get_tapepos(&vol->index->selfptr, &val, name);
		} else if (! strcmp(name, "ltfs.indexPrevious")) {
			ret = _xattr_get_tapepos(&vol->index->backptr, &val, name);
		} else if (! strcmp(name, "ltfs.indexCreator")) {
			ret = _xattr_get_string(vol->index->creator, &val, name);
		} else if (! strcmp(name, "ltfs.labelCreator")) {
			ret = _xattr_get_string(vol->label->creator, &val, name);
		} else if (! strcmp(name, "ltfs.indexVersion")) {
			ltfs_mutex_lock(&vol->index->dirty_lock);
			ret = _xattr_get_version(vol->index->version, &val, name);
			ltfs_mutex_unlock(&vol->index->dirty_lock);
		} else if (! strcmp(name, "ltfs.labelVersion")) {
			ret = _xattr_get_version(vol->label->version, &val, name);
		} else if (! strcmp(name, "ltfs.partitionMap")) {
			ret = _xattr_get_partmap(vol->label, &val, name);
		} else if (! strcmp(name, "ltfs.mediaLoads")) {
			ret = _xattr_get_cartridge_health(&h, &h.mounts, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaRecoveredWriteErrors")) {
			ret = _xattr_get_cartridge_health(&h, &h.write_temps, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaPermanentWriteErrors")) {
			ret = _xattr_get_cartridge_health(&h, &h.write_perms, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaRecoveredReadErrors")) {
			ret = _xattr_get_cartridge_health(&h, &h.read_temps, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaPermanentReadErrors")) {
			ret = _xattr_get_cartridge_health(&h, &h.read_perms, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaPreviousPermanentWriteErrors")) {
			ret = _xattr_get_cartridge_health(&h, &h.write_perms_prev, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaPreviousPermanentReadErrors")) {
			ret = _xattr_get_cartridge_health(&h, &h.read_perms_prev, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaBeginningMediumPasses")) {
			ret = _xattr_get_cartridge_health(&h, &h.passes_begin, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaMiddleMediumPasses")) {
			ret = _xattr_get_cartridge_health(&h, &h.passes_middle, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaEfficiency")) {
			ret = _xattr_get_cartridge_health(&h, &h.tape_efficiency, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaDatasetsWritten")) {
			ret = _xattr_get_cartridge_health_u64(&h, &h.written_ds, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaDatasetsRead")) {
			ret = _xattr_get_cartridge_health_u64(&h, &h.read_ds, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaMBWritten")) {
			ret = _xattr_get_cartridge_health_u64(&h, &h.written_mbytes, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaMBRead")) {
			ret = _xattr_get_cartridge_health_u64(&h, &h.read_mbytes, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaStorageAlert")) {
			ret = ltfs_get_tape_alert_unlocked(&tape_alert, vol);
			if (ret < 0)
				val = NULL;
			else {
				ret = asprintf(&val, "0x%016"PRIx64, tape_alert);
				if (ret < 0) {
					ltfsmsg(LTFS_ERR, "10001E", name);
					val = NULL;
					ret = -LTFS_NO_MEMORY;
				}
			}
		} else if (! strcmp(name, "ltfs.mediaDataPartitionTotalCapacity")) {
			ret = _xattr_get_cartridge_capacity(&cap, &cap.total_dp, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaDataPartitionAvailableSpace")) {
			ret = _xattr_get_cartridge_capacity(&cap, &cap.remaining_dp, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaIndexPartitionTotalCapacity")) {
			ret = _xattr_get_cartridge_capacity(&cap, &cap.total_ip, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaIndexPartitionAvailableSpace")) {
			ret = _xattr_get_cartridge_capacity(&cap, &cap.remaining_ip, &val, name, vol);
		} else if (! strcmp(name, "ltfs.mediaEncrypted")) {
			ret = _xattr_get_string(tape_get_media_encrypted(vol->device), &val, name);
		} else if (! strcmp(name, "ltfs.driveEncryptionState")) {
			ret = _xattr_get_string(tape_get_drive_encryption_state(vol->device), &val, name);
		} else if (! strcmp(name, "ltfs.driveEncryptionMethod")) {
			ret = _xattr_get_string(tape_get_drive_encryption_method(vol->device), &val, name);
		} else if (! strcmp(name, "ltfs.vendor.IBM.referencedBlocks")) {
			ret = _xattr_get_u64(ltfs_get_valid_block_count_unlocked(vol), &val, name);
		} else if (! strcmp(name, "ltfs.vendor.IBM.trace")) {
			ret = ltfs_get_trace_status(&val);
		} else if (! strcmp(name, "ltfs.vendor.IBM.totalBlocks")) {
			ret = ltfs_get_append_position(&append_pos, vol);
			if (ret < 0)
				val = NULL;
			else
				ret = _xattr_get_u64(append_pos, &val, name);
		} else if (! strcmp(name, "ltfs.vendor.IBM.cartridgeMountNode")) {
			ret = asprintf(&val, "localhost");
			if (ret < 0) {
				ltfsmsg(LTFS_ERR, "10001E", name);
				val = NULL;
				ret = -LTFS_NO_MEMORY;
			}
		} else if (! strncmp(name, "ltfs.vendor", strlen("ltfs.vendor"))) {
			if (! strncmp(name + strlen("ltfs.vendor."), LTFS_VENDOR_NAME, strlen(LTFS_VENDOR_NAME))) {
				ret = _xattr_get_vendorunique_xattr(&val, name, vol);
			}
		} else if (! strcmp(name, "ltfs.sync")) {
			ret = -LTFS_RDONLY_XATTR;
		}
	}

	if (val) {
		ret = strlen(val);
		if (buf_size) {
			if (buf_size < (size_t)ret)
				ret = -LTFS_SMALL_BUFFER;
			else
				memcpy(buf, val, ret);
		}
		free(val);
	}

	return ret;
}

/**
 * Write user-supplied data to a virtual extended attribute for a given dentry.
 * The caller always has a write lock on vol-index->lock.
 * @param d Dentry to set the xattr on.
 * @param name Name to set.
 * @param value Value to set, may be binary, not necessarily null-terminated.
 * @param size Size of value in bytes.
 * @param vol LTFS volume
 * @return 0 on success, -LTFS_NO_XATTR if the xattr is not a settable virtual xattr,
 *         or another negative value on error.
 */
int _xattr_set_virtual(struct dentry *d, const char *name, const char *value,
	size_t size, struct ltfs_volume *vol)
{
	int ret = 0;

	if (! strcmp(name, "ltfs.sync") && d == vol->index->root) {
		/* If sync_type is selected as time */
		if (vol->periodic_sync_handle) {
			ret = periodic_sync_thread_signal(vol->periodic_sync_handle);
			if (ret < 0) {
				ltfsmsg(LTFS_ERR, "17069E");
				return ret;
			}
		} else {
			/* If sync_type is selected as close or unmount */
			ret = ltfs_sync_index(SYNC_EA, false, vol);
		}
	} else if (! strcmp(name, "ltfs.commitMessage") && d == vol->index->root) {
		char *value_null_terminated, *new_value;

		if (size > INDEX_MAX_COMMENT_LEN) {
			ltfsmsg(LTFS_ERR, "11308E");
			ret = -LTFS_LARGE_XATTR;
		}

		ltfs_mutex_lock(&vol->index->dirty_lock);
		if (! value || ! size) {
			/* Clear the current comment field */
			if (vol->index->commit_message) {
				free(vol->index->commit_message);
				vol->index->commit_message = NULL;
			}
		} else {
			value_null_terminated = malloc(size + 1);
			if (! value_null_terminated) {
				ltfsmsg(LTFS_ERR, "10001E", "_xattr_set_virtual: commit_message");
				ltfs_mutex_unlock(&vol->index->dirty_lock);
				return -LTFS_NO_MEMORY;
			}
			memcpy(value_null_terminated, value, size);
			value_null_terminated[size] = '\0';

			ret = pathname_format(value_null_terminated, &new_value, false, true);
			free(value_null_terminated);
			if (ret < 0) {
				ltfs_mutex_unlock(&vol->index->dirty_lock);
				return ret;
			}
			ret = 0;

			/* Update the commit message in the index */
			if (vol->index->commit_message)
				free(vol->index->commit_message);
			vol->index->commit_message = new_value;
		}

		ltfs_set_index_dirty(false, false, vol->index);
		ltfs_mutex_unlock(&vol->index->dirty_lock);

	} else if ((! strcmp(name, "ltfs.volumeName")
			|| ! strcmp(name, "ltfs.mamVolumeName")) && d == vol->index->root) {
		char *value_null_terminated, *new_value = NULL;

		ltfs_mutex_lock(&vol->index->dirty_lock);
		if (! value || ! size) {
			/* Clear the current volume name field */
			if (vol->index->volume_name) {
				free(vol->index->volume_name);
				vol->index->volume_name = NULL;
			}
		} else {
			if (size > MAX_VOLUME_NAME_SIZE)
				size = MAX_VOLUME_NAME_SIZE;
			value_null_terminated = malloc(size + 1);
			if (! value_null_terminated) {
				ltfsmsg(LTFS_ERR, "10001E", "_xattr_set_virtual: volume name");
				ltfs_mutex_unlock(&vol->index->dirty_lock);
				return -LTFS_NO_MEMORY;
			}
			memcpy(value_null_terminated, value, size);
			value_null_terminated[size] = '\0';

			ret = pathname_format(value_null_terminated, &new_value, true, false);
			free(value_null_terminated);
			if (ret < 0) {
				ltfs_mutex_unlock(&vol->index->dirty_lock);
				return ret;
			}
			ret = 0;

			/* Update the volume name in the index */
			if (vol->index->volume_name)
				free(vol->index->volume_name);
			vol->index->volume_name = new_value;
		}

		ltfs_set_index_dirty(false, false, vol->index);
		ltfs_mutex_unlock(&vol->index->dirty_lock);

		/* Update the CM volume Name attribute */
		ret = tape_update_mam_attributes(vol->device, new_value, TC_MAM_USR_MED_TXT_LABEL, NULL);
		if (! ret) {
			ret = tape_get_MAMattributes(vol->device, TC_MAM_USR_MED_TXT_LABEL,
			            ltfs_part_id2num(vol->label->partid_ip, vol), &vol->mam_attr);
		}
	} else if (! strcmp(name, "ltfs.mamBarcode") && d == vol->index->root) {
		char *value_null_terminated = NULL, *new_value = NULL;

		if (value && size) {
			value_null_terminated = malloc(size + 1);
			if (! value_null_terminated) {
				ltfsmsg(LTFS_ERR, "10001E", "_xattr_set_virtual: barcode name");
				return -LTFS_NO_MEMORY;
			}
			memcpy(value_null_terminated, value, size);
			value_null_terminated[size] = '\0';

			ret = pathname_format(value_null_terminated, &new_value, true, false);
			free(value_null_terminated);
			if (ret < 0) {
				return ret;
			}
			ret = 0;
		}

		/* Convert Barcode values to uppercase if lowercase as only Capital letters are allowed */
		ret = ltfs_string_toupper(new_value);

		/* Validate new barcode value*/
		if (!ret)
			ret = ltfs_validate_barcode(new_value);

		if (ret < 0) {
			if (ret == -LTFS_BARCODE_LENGTH)
				ltfsmsg(LTFS_ERR, "17303E");
			else if (ret == -LTFS_BARCODE_INVALID)
				ltfsmsg(LTFS_ERR, "17304E");
			return ret;
		}

		/* Update the CM Barcode Name attribute */
		ret = tape_update_mam_attributes(vol->device, NULL, TC_MAM_BARCODE,
				(const char*) new_value);
		if (! ret) {
			ret = tape_get_MAMattributes(vol->device,TC_MAM_BARCODE,
			         ltfs_part_id2num(vol->label->partid_ip, vol), &vol->mam_attr);
		}
	} else if (! strcmp(name, "ltfs.createTime")) {
		ret = _xattr_set_time(d, &d->creation_time, value, size, name, vol);
		if (ret == LTFS_TIME_OUT_OF_RANGE) {
			ltfsmsg(LTFS_WARN, "17221W", name, d->name, d->uid, value);
			ret = 0;
		}
	} else if (! strcmp(name, "ltfs.modifyTime")) {
		get_current_timespec(&d->change_time);
		ret = _xattr_set_time(d, &d->modify_time, value, size, name, vol);
		if (ret == LTFS_TIME_OUT_OF_RANGE) {
			ltfsmsg(LTFS_WARN, "17221W", name, d->name, d->uid, value);
			ret = 0;
		}
	} else if (! strcmp(name, "ltfs.changeTime")) {
		ret = _xattr_set_time(d, &d->change_time, value, size, name, vol);
		if (ret == LTFS_TIME_OUT_OF_RANGE) {
			ltfsmsg(LTFS_WARN, "17221W", name, d->name, d->uid, value);
			ret = 0;
		}
	} else if (! strcmp(name, "ltfs.accessTime")) {
		ret = _xattr_set_time(d, &d->access_time, value, size, name, vol);
		if (ret == LTFS_TIME_OUT_OF_RANGE) {
			ltfsmsg(LTFS_WARN, "17221W", name, d->name, d->uid, value);
			ret = 0;
		}
	} else if (! strcmp(name, "ltfs.backupTime")) {
		ret = _xattr_set_time(d, &d->backup_time, value, size, name, vol);
		if (ret == LTFS_TIME_OUT_OF_RANGE) {
			ltfsmsg(LTFS_WARN, "17221W", name, d->name, d->uid, value);
			ret = 0;
		}
	} else if (! strcmp(name, "ltfs.driveCaptureDump")) {
		ret = tape_takedump_drive(vol->device);
	} else if (! strcmp(name, "ltfs.mediaStorageAlert")) {
		uint64_t tape_alert = 0;
		char *invalid_start, *v;

		v = strndup(value, size);
		if (! v) {
			ltfsmsg(LTFS_ERR, "10001E", __FUNCTION__);
			return -LTFS_NO_MEMORY;
		}

		/* ltfs.mediaStorageAlert shall be specified by hexadecimal text */
		tape_alert = strtoull(v, &invalid_start, 16);
		if( (*invalid_start == '\0') && v )
			ret = ltfs_clear_tape_alert(tape_alert, vol);
		else
			ret = -LTFS_STRING_CONVERSION;
		free(v);
	} else if (! strcmp(name, "ltfs.vendor.IBM.logLevel")) {
		int level = 0;
		char *invalid_start, *v;

		v = strndup(value, size);
		if (! v) {
			ltfsmsg(LTFS_ERR, "10001E", __FUNCTION__);
			return -LTFS_NO_MEMORY;
		}

		/* ltfs.vendor.IBM.logLevel shall be specified by hexadecimal text */
		level = strtoul(v, &invalid_start, 0);
		if( (*invalid_start == '\0') && v ) {
			ret = 0;
			ltfs_set_log_level(level);
		} else
			ret = -LTFS_STRING_CONVERSION;
		free(v);
	} else if (! strcmp(name, "ltfs.vendor.IBM.syslogLevel")) {
		int level = 0;
		char *invalid_start, *v;

		v = strndup(value, size);
		if (! v) {
			ltfsmsg(LTFS_ERR, "10001E", __FUNCTION__);
			return -LTFS_NO_MEMORY;
		}

		/* ltfs.vendor.IBM.syslogLevel shall be specified by hexadecimal text */
		level = strtoul(v, &invalid_start, 0);
		if( (*invalid_start == '\0') && v ) {
			ret = 0;
			ltfs_set_syslog_level(level);
		} else
			ret = -LTFS_STRING_CONVERSION;
		free(v);
	} else if (! strcmp(name, "ltfs.vendor.IBM.trace")) {
		char *v;

		v = strndup(value, size);
		if (! v) {
			ltfsmsg(LTFS_ERR, "10001E", __FUNCTION__);
			return -LTFS_NO_MEMORY;
		}

		ret = ltfs_set_trace_status(v);
		free(v);
	} else if (! strcmp(name, "ltfs.vendor.IBM.dump")) {
		char *v;

		v = strndup(value, size);
		if (! v) {
			ltfsmsg(LTFS_ERR, "10001E", __FUNCTION__);
			return -LTFS_NO_MEMORY;
		}

		ret = ltfs_dump(v);
		free(v);
	} else if (! strcmp(name, "ltfs.vendor.IBM.dumpTrace")) {
		char *v;

		v = strndup(value, size);
		if (! v) {
			ltfsmsg(LTFS_ERR, "10001E", __FUNCTION__);
			return -LTFS_NO_MEMORY;
		}

		ret = ltfs_trace_dump(v);
		free(v);
	} else if (! strcmp(name, "ltfs.vendor.IBM.profiler")) {
		uint64_t source = 0;
		char *invalid_start, *v;

		v = strndup(value, size);
		if (! v) {
			ltfsmsg(LTFS_ERR, "10001E", __FUNCTION__);
			return -LTFS_NO_MEMORY;
		}

		source = strtoull(v, &invalid_start, 0);
		if( (*invalid_start == '\0') && v ) {
			ret = ltfs_profiler_set(source);
		} else
			ret = -LTFS_STRING_CONVERSION;
		free(v);
	} else if (! strncmp(name, "ltfs.vendor", strlen("ltfs.vendor"))) {
			if (! strncmp(name + strlen("ltfs.vendor."), LTFS_VENDOR_NAME, strlen(LTFS_VENDOR_NAME))) {
				ret = _xattr_set_vendorunique_xattr(name, value, size, vol);
			}
	} 
	/* Separate implementation for MAM attributes exist. */		
#if 0
	else if (! strcmp(name, "ltfs.mamBarcode")) {
		ret =  update_tape_attribute (vol, value, TC_MAM_BARCODE, size);
		if ( ret < 0 ) {
			ltfsmsg(LTFS_WARN, "17199W", TC_MAM_USER_MEDIUM_LABEL, "_xattr_set_virtual");
			return ret;
		}	
	}
#endif /* 0 */
	else
		ret = -LTFS_NO_XATTR;

	return ret;
}

/**
 * "Remove" a virtual extended attribute. This is disallowed for many virtual xattrs,
 * but some have a meaningful removal operation.
 * @param d Dentry to remove xattr from.
 * @param name Attribute to remove.
 * @param vol LTFS volume.
 * @return 0 on success, -LTFS_NO_XATTR if the xattr is not a removable virtual xattr, or another
 *         negative value on error.
 */
int _xattr_remove_virtual(struct dentry *d, const char *name, struct ltfs_volume *vol)
{
	int ret = 0;

	if (! strcmp(name, "ltfs.commitMessage") && d == vol->index->root) {
		ltfs_mutex_lock(&vol->index->dirty_lock);
		if (vol->index->commit_message) {
			free(vol->index->commit_message);
			vol->index->commit_message = NULL;
			ltfs_set_index_dirty(false, false, vol->index);
		}
		ltfs_mutex_unlock(&vol->index->dirty_lock);
	} else if (! strcmp(name, "ltfs.volumeName") && d == vol->index->root) {
		ltfs_mutex_lock(&vol->index->dirty_lock);
		if (vol->index->volume_name) {
			free(vol->index->volume_name);
			vol->index->volume_name = NULL;
			ltfs_set_index_dirty(false, false, vol->index);
		}
/* Since the volume name is removed the mam volume name will be updated to NULL */
		ret = tape_update_mam_attributes(vol->device, NULL, 0, NULL);
		tape_get_MAMattributes(vol->device, TC_MAM_USR_MED_TXT_LABEL,
					            ltfs_part_id2num(vol->label->partid_ip, vol), &vol->mam_attr);
/* Separate implementation for MAM attributes exist. */		
#if 0
		/* Clear tape attribute(TC_MAM_USER_MEDIUM_LABEL) */
		ret =  update_tape_attribute (vol, NULL, TC_MAM_USER_MEDIUM_LABEL, 0);
		if ( ret < 0 ) {
			ltfsmsg(LTFS_WARN, "17199W", TC_MAM_USER_MEDIUM_LABEL, "_xattr_set_virtual");
		}
#endif /* 0 */
		ltfs_mutex_unlock(&vol->index->dirty_lock);
	} else
		ret = -LTFS_NO_XATTR;

	return ret;
}

int _xattr_get_cartridge_health(cartridge_health_info *h, int64_t *val, char **outval,
	const char *msg, struct ltfs_volume *vol)
{
	int ret = ltfs_get_cartridge_health(h, vol);
	if (ret == 0) {
		ret = asprintf(outval, "%"PRId64, *val);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, "10001E", msg);
			*outval = NULL;
			return -LTFS_NO_MEMORY;
		}
	} else
		*outval = NULL;
	return ret;
}

int _xattr_get_cartridge_health_u64(cartridge_health_info *h, uint64_t *val, char **outval,
	const char *msg, struct ltfs_volume *vol)
{
	int ret = ltfs_get_cartridge_health(h, vol);
	if (ret == 0 && (int64_t)(*val) != UNSUPPORTED_CARTRIDGE_HEALTH) {
		ret = asprintf(outval, "%"PRIu64, *val);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, "10001E", msg);
			*outval = NULL;
			ret = -LTFS_NO_MEMORY;
		}
	} else if (ret == 0) {
		ret = asprintf(outval, "%"PRId64, UNSUPPORTED_CARTRIDGE_HEALTH);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, "10001E", msg);
			*outval = NULL;
			ret = -LTFS_NO_MEMORY;
		}
	} else
		*outval = NULL;
	return ret;
}

int _xattr_get_cartridge_capacity(struct device_capacity *cap, unsigned long *val, char **outval,
	const char *msg, struct ltfs_volume *vol)
{
	double scale = vol->label->blocksize / 1048576.0;
	int ret = ltfs_capacity_data_unlocked(cap, vol);
	if (ret == 0) {
		ret = asprintf(outval, "%lu", (unsigned long)((*val) * scale));
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, "10001E", msg);
			*outval = NULL;
			return -LTFS_NO_MEMORY;
		}
	} else
		*outval = NULL;
	return ret;
}

int _xattr_get_time(struct ltfs_timespec *val, char **outval, const char *msg)
{
	int ret;

	ret = xml_format_time(*val, outval);
	if (! (*outval)) {
		ltfsmsg(LTFS_ERR, "11145E", msg);
		return -LTFS_NO_MEMORY;
	}

	return ret;
}

int _xattr_get_dentry_time(struct dentry *d, struct ltfs_timespec *val, char **outval,
	const char *msg)
{
	int ret;
	acquireread_mrsw(&d->meta_lock);
	ret = _xattr_get_time(val, outval, msg);
	releaseread_mrsw(&d->meta_lock);
	return ret;
}

int _xattr_get_string(const char *val, char **outval, const char *msg)
{
	if (! val)
		return 0;
	*outval = strdup(val);
	if (! (*outval)) {
		ltfsmsg(LTFS_ERR, "10001E", msg);
		return -LTFS_NO_MEMORY;
	}
	return 0;
}

int _xattr_get_u64(uint64_t val, char **outval, const char *msg)
{
	int ret = asprintf(outval, "%"PRIu64, val);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "10001E", msg);
		*outval = NULL;
		ret = -LTFS_NO_MEMORY;
	}
	return ret;
}

int _xattr_get_tapepos(struct tape_offset *val, char **outval, const char *msg)
{
	int ret = asprintf(outval, "%c:%"PRIu64, val->partition, val->block);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "10001E", msg);
		return -LTFS_NO_MEMORY;
	}
	return 0;
}

int _xattr_get_partmap(struct ltfs_label *label, char **outval, const char *msg)
{
	int ret = asprintf(outval, "I:%c,D:%c", label->partid_ip, label->partid_dp);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "10001E", msg);
		return -LTFS_NO_MEMORY;
	}
	return 0;
}

int _xattr_get_version(int version, char **outval, const char *msg)
{
	int ret;
	if (version == 10000) {
		*outval = strdup("1.0");
		if (! (*outval)) {
			ltfsmsg(LTFS_ERR, "10001E", msg);
			return -LTFS_NO_MEMORY;
		}
	} else {
		ret = asprintf(outval, "%d.%d.%d", version/10000, (version % 10000)/100, version % 100);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, "10001E", msg);
			return -LTFS_NO_MEMORY;
		}
	}
	return 0;
}

int _xattr_set_time(struct dentry *d, struct ltfs_timespec *out, const char *value, size_t size,
	const char *msg, struct ltfs_volume *vol)
{
	int ret;
	struct ltfs_timespec t;
	char *value_null_terminated;

	value_null_terminated = malloc(size + 1);
	if (! value_null_terminated) {
		ltfsmsg(LTFS_ERR, "10001E", msg);
		return -LTFS_NO_MEMORY;
	}
	memcpy(value_null_terminated, value, size);
	value_null_terminated[size] = '\0';

	ret = xml_parse_time(false, value_null_terminated, &t);
	free(value_null_terminated);
	if (ret < 0)
		return -LTFS_BAD_ARG;

	acquirewrite_mrsw(&d->meta_lock);
	*out = t;
	releasewrite_mrsw(&d->meta_lock);

	ltfs_set_index_dirty(true, false, vol->index);
	return ret;
}

int _xattr_get_vendorunique_xattr(char **outval, const char *msg, struct ltfs_volume *vol)
{
	int ret;

	ret = ltfs_get_vendorunique_xattr(msg, outval, vol);
	if (ret != 0)
		*outval = NULL;

	return ret;
}

int _xattr_set_vendorunique_xattr(const char *name, const char *value, size_t size, struct ltfs_volume *vol)
{
	int ret;

	ret = ltfs_set_vendorunique_xattr(name, value, size, vol);

	return ret;
}
