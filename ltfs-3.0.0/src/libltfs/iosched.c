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
** FILE NAME:       iosched.c
**
** DESCRIPTION:     Implements the interface with the pluggable I/O schedulers.
**
** AUTHOR:          Lucas C. Villa Real
**                  IBM Almaden Research Center
**                  lucasvr@us.ibm.com
**
*************************************************************************************
*/
#include "ltfs_fuse.h"
#include "ltfs.h"
#include "iosched.h"

struct iosched_priv {
	void *dlopen_handle;           /**< Handle returned from dlopen */
	struct libltfs_plugin *plugin; /**< Reference to the plugin */
	struct iosched_ops *ops;       /**< I/O scheduler operations */
	void *backend_handle;          /**< Backend private data */
};

/**
 * Initialize the I/O scheduler.
 * @param plugin The plugin to take scheduler operations from.
 * @param vol LTFS volume
 * @return on success, 0 is returned and the I/O scheduler handle is stored in the ltfs_volume
 * structure. On failure a negative value is returned.
 */
int iosched_init(struct libltfs_plugin *plugin, struct ltfs_volume *vol)
{
	unsigned int i;
	struct iosched_priv *priv;

	CHECK_ARG_NULL(plugin, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(plugin->lib_handle, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);

	priv = calloc(1, sizeof(struct iosched_priv));
	if (! priv) {
		ltfsmsg(LTFS_ERR, "10001E", "iosched_init: private data");
		return -LTFS_NO_MEMORY;
	}

	priv->plugin = plugin;
	priv->ops = plugin->ops;

	/* Verify that backend implements all required operations */
	for (i=0; i<sizeof(struct iosched_ops)/sizeof(void *); ++i) {
		if (((void **)(priv->ops))[i] == NULL) {
			ltfsmsg(LTFS_ERR, "13003E");
			free(priv);
			return -LTFS_PLUGIN_INCOMPLETE;
		}
	} 

	priv->backend_handle = priv->ops->init(vol);
	if (! priv->backend_handle) {
		free(priv);
		return -1;
	}

	vol->iosched_handle = priv;
	return 0;
}

/**
 * Destroy the I/O scheduler.
 * @param vol LTFS volume
 * @return 0 on success or a negative value on error.
 */
int iosched_destroy(struct ltfs_volume *vol)
{
	struct iosched_priv *priv = (struct iosched_priv *) vol ? vol->iosched_handle : NULL;
	int ret;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->destroy, -LTFS_NULL_ARG);

	ret = priv->ops->destroy(priv->backend_handle);
	vol->iosched_handle = NULL;
	free(priv);

	return ret;
}

/**
 * Open a file and create the I/O scheduler private data for a dentry
 * @param path the file to open
 * @param flags open flags
 * @param dentry on success, points to the dentry.
 * @param vol LTFS volume
 * @return 0 on success or a negative value on error.
 */
int iosched_open(const char *path, bool open_write, struct dentry **dentry, struct ltfs_volume *vol)
{
	struct iosched_priv *priv = (struct iosched_priv *) vol ? vol->iosched_handle : NULL;
	int ret;

	CHECK_ARG_NULL(path, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dentry, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->open, -LTFS_NULL_ARG);
	
	ret = priv->ops->open(path, open_write, dentry, priv->backend_handle);
	return ret;
}

/**
 * Close a dentry and destroy the I/O scheduler private data from a dentry if appropriate.
 * @param d dentry
 * @param flush true to force a flush before closing.
 * @param vol LTFS volume
 * @return 0 on success or a negative value on error.
 */
int iosched_close(struct dentry *d, bool flush, struct ltfs_volume *vol)
{
	struct iosched_priv *priv = (struct iosched_priv *) vol ? vol->iosched_handle : NULL;
	int ret;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->close, -LTFS_NULL_ARG);

	ret = priv->ops->close(d, flush, priv->backend_handle);
	return ret;
}

/**
 * Checks if the I/O scheduler has been initialized for the given volume
 * @param vol LTFS volume
 * @return true to indicate that the I/O scheduler has been initialized or false if not
 */
bool iosched_initialized(struct ltfs_volume *vol)
{
	CHECK_ARG_NULL(vol, false);
/*-- HP CHANGE:
 * Compare handle to NULL and return false/true explicitly
 *  rather than returning the value of the handle implicitly
 *  cast to a bool...  See also ltfs_fuse_mount in ltfs_fuse.c
 *  where the iosched_handle is now initialized to NULL.
 */
	return ((vol->iosched_handle == NULL) ? false:true);
}

/**
 * Read from tape through the I/O scheduler.
 * @param d dentry to read from
 * @param buf output data buffer
 * @param size output data buffer size
 * @param offset offset relative to the beginning of file to start reading from
 * @param vol LTFS volume
 * @return 0 on success or a negative value on error.
 */
ssize_t iosched_read(struct dentry *d, char *buf, size_t size, off_t offset, struct ltfs_volume *vol)
{
	ssize_t ret;
	struct iosched_priv *priv = (struct iosched_priv *) vol ? vol->iosched_handle : NULL;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->read, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);

	ret = priv->ops->read(d, buf, size, offset, priv->backend_handle);
	return ret;
}

/**
 * Write to tape through the I/O scheduler.
 * The caller must have a read 
 * @param d dentry to write to
 * @param buf input data buffer
 * @param size input data length
 * @param offset offset relative to the beginning of file to start writing to
 * @param vol LTFS volume
 * @return 0 on success or a negative value on error.
 */
ssize_t iosched_write(struct dentry *d, const char *buf, size_t size, off_t offset,
	bool isupdatetime, struct ltfs_volume *vol)
{
	ssize_t ret;
	struct iosched_priv *priv = (struct iosched_priv *) vol ? vol->iosched_handle : NULL;
	
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->write, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);

	ret = priv->ops->write(d, buf, size, offset, isupdatetime, priv->backend_handle);
	if (ret > 0 && (size_t) ret > size)
		ret = size;

	return ret;
}

/**
 * Flushes all pending operations to the tape.
 * @param d dentry to flush or NULL to flush all queued operations.
 * @param closeflag true if flushing before close(), false if not.
 * @param vol LTFS volume
 * @return 0 on success or a negative value on error.
 */
int iosched_flush(struct dentry *d, bool closeflag, struct ltfs_volume *vol)
{
	int ret;
	struct iosched_priv *priv = (struct iosched_priv *) vol ? vol->iosched_handle : NULL;
	
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->flush, -LTFS_NULL_ARG);

	ret = priv->ops->flush(d, closeflag, priv->backend_handle);
	return ret;
}

/**
 * Change the length of a file. This may either shorten or lengthen the file.
 * @param d Dentry to truncate.
 * @param length Desired file size.
 * @param vol LTFS volume
 * @return 0 on success or a negative value on error.
 */
int iosched_truncate(struct dentry *d, off_t length, struct ltfs_volume *vol)
{
	int ret;
	struct iosched_priv *priv = (struct iosched_priv *) vol ? vol->iosched_handle : NULL;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->truncate, -LTFS_NULL_ARG);

	ret = priv->ops->truncate(d, length, priv->backend_handle);
	return ret;
}

/**
 * Ask the I/O scheduler what's the current size of the file represented
 * by the dentry @d. The returned value takes into account dirty buffers
 * which didn't reach the tape yet.
 *
 * @param d dentry to flush or NULL to flush all queued operations.
 * @param vol LTFS volume
 * @return the file size.
 */
uint64_t iosched_get_filesize(struct dentry *d, struct ltfs_volume *vol)
{
	uint64_t ret;
	struct iosched_priv *priv = (struct iosched_priv *) vol ? vol->iosched_handle : NULL;
	
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->get_filesize, -LTFS_NULL_ARG);

	ret = priv->ops->get_filesize(d, priv->backend_handle);
	return ret;
}

/**
 * Update the data placement policy of data for a given dentry.
 *
 * @param d dentry
 * @param vol LTFS volume
 * @return 0 on success or a negative value on error.
 */
int iosched_update_data_placement(struct dentry *d, struct ltfs_volume *vol)
{
	int ret;
	struct iosched_priv *priv = (struct iosched_priv *) vol ? vol->iosched_handle : NULL;
	
	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->update_data_placement, -LTFS_NULL_ARG);

	ret = priv->ops->update_data_placement(d, priv->backend_handle);
	return ret;
}
