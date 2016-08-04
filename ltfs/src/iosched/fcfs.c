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
** FILE NAME:       iosched/fcfs.c
**
** DESCRIPTION:     Implements the First-Come, First-Served I/O scheduler example.
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

#include "libltfs/ltfs.h"
#include "libltfs/ltfs_fsops_raw.h"
#include "ltfs_copyright.h"
#include "libltfs/iosched_ops.h"

volatile char *copyright = LTFS_COPYRIGHT_0"\n"LTFS_COPYRIGHT_1"\n"LTFS_COPYRIGHT_2"\n" \
	LTFS_COPYRIGHT_3"\n"LTFS_COPYRIGHT_4"\n"LTFS_COPYRIGHT_5"\n";

struct fcfs_data {
	ltfs_mutex_t sched_lock;    /**< Serializes read and write access */
	struct ltfs_volume *vol;    /**< A reference to the LTFS volume structure */
};

/**
 * Initialize the FCFS I/O scheduler.
 * @param vol LTFS volume
 * @return a pointer to the private data on success or NULL on error.
 */
void *fcfs_init(struct ltfs_volume *vol)
{
	int ret;
	struct fcfs_data *priv = calloc(1, sizeof(struct fcfs_data));
	if (! priv) {
		ltfsmsg(LTFS_ERR, "10001E", __FUNCTION__);
		return NULL;
	}
	ret = ltfs_mutex_init(&priv->sched_lock);
	if (ret) {
		ltfsmsg(LTFS_ERR, "10002E", ret);
		free(priv);
		return NULL;
	}
	priv->vol = vol;
	ltfsmsg(LTFS_INFO, "13019I");
	return priv;
}

/**
 * Destroy the FCFS I/O scheduler.
 * @param iosched_handle the I/O scheduler handle
 * @return 0 on success or a negative value on error.
 */
int fcfs_destroy(void *iosched_handle)
{
	struct fcfs_data *priv = (struct fcfs_data *) iosched_handle;
	CHECK_ARG_NULL(iosched_handle, -LTFS_NULL_ARG);

	ltfs_mutex_destroy(&priv->sched_lock);
	free(priv);
	ltfsmsg(LTFS_INFO, "13020I");
	return 0;
}

/**
 * Open a file and create the I/O scheduler private data for a dentry
 * @param path the file to open
 * @param open_write true if opening the file for write.
 * @param dentry on success, points to the dentry.
 * @param iosched_handle the I/O scheduler handle
 * @return 0 on success or a negative value on error.
 */
int fcfs_open(const char *path, bool open_write, struct dentry **dentry, void *iosched_handle)
{
	struct fcfs_data *priv = (struct fcfs_data *) iosched_handle;

	CHECK_ARG_NULL(path, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dentry, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(iosched_handle, -LTFS_NULL_ARG);

	return ltfs_fsraw_open(path, open_write, dentry, priv->vol);
}

/**
 * Close a dentry and destroy the I/O scheduler private data from a dentry if appropriate.
 * @param d dentry
 * @param flush true to force a flush before closing.
 * @param iosched_handle the I/O scheduler handle
 * @return 0 on success or a negative value on error.
 */
int fcfs_close(struct dentry *d, bool flush, void *iosched_handle)
{
	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(iosched_handle, -LTFS_NULL_ARG);

	return ltfs_fsraw_close(d);
}

/**
 * Read contents from tape.
 * The caller must NOT have a lock held on the dentry @d.
 *
 * @param d dentry to read from
 * @param buf output data buffer
 * @param size output data buffer size
 * @param offset offset relative to the beginning of file to start reading from
 * @param iosched_handle the I/O scheduler handle
 * @return the number of bytes read or a negative value on error
 */
ssize_t fcfs_read(struct dentry *d, char *buf, size_t size, off_t offset, void *iosched_handle)
{
	struct fcfs_data *priv = (struct fcfs_data *) iosched_handle;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(buf, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(iosched_handle, -LTFS_NULL_ARG);

	return ltfs_fsraw_read(d, buf, size, offset, priv->vol);
}

/**
 * Enqueue a write request to the tape.
 * The caller must NOT have a lock held on the dentry @d.
 *
 * @param d dentry to write to
 * @param buf input data buffer
 * @param size input data length
 * @param offset offset relative to the beginning of file to start writing to
 * @param iosched_handle the I/O scheduler handle
 * @return the number of bytes enqueued for writing or a negative value on error
 */
ssize_t fcfs_write(struct dentry *d, const char *buf, size_t size, off_t offset,
				   bool isupdatetime, void *iosched_handle)
{
	struct fcfs_data *priv = (struct fcfs_data *) iosched_handle;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(buf, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(iosched_handle, -LTFS_NULL_ARG);

	return ltfs_fsraw_write(d, buf, size, offset, ltfs_dp_id(priv->vol), true, priv->vol);
}

/**
 * Forces all pending operations to meet the tape.
 *
 * @param d dentry to flush or NULL to flush all queued operations.
 * @param closeflag true if flushing before close(), false if not.
 * @param iosched_handle the I/O scheduler handle.
 * @return 0 on success or a negative value on error.
 */
int fcfs_flush(struct dentry *d, bool closeflag, void *iosched_handle)
{
	(void) closeflag;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(iosched_handle, -LTFS_NULL_ARG);

	return 0;
}

int fcfs_truncate(struct dentry *d, off_t length, void *iosched_handle)
{
	struct fcfs_data *priv = (struct fcfs_data *) iosched_handle;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(iosched_handle, -LTFS_NULL_ARG);

	return ltfs_fsraw_truncate(d, length, priv->vol);
}

/**
 * Get the file size, considering data stored in working buffers.
 *
 * @param d dentry to flush or NULL to flush all queued operations.
 * @param iosched_handle the I/O scheduler handle.
 * @return the file size.
 */
uint64_t fcfs_get_filesize(struct dentry *d, void *iosched_handle)
{
	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(iosched_handle, -LTFS_NULL_ARG);

	return d->size;
}

/**
 * Update the data placement policy of data for a given dentry.
 *
 * @param d dentry
 * @param iosched_handle the I/O scheduler handle.
 * @return 0 on success or a negative value on error.
 */
int fcfs_update_data_placement(struct dentry *d, void *iosched_handle)
{
	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(iosched_handle, -LTFS_NULL_ARG);

	return 0;
}

struct iosched_ops fcfs_ops = {
	.init         = fcfs_init,
	.destroy      = fcfs_destroy,
	.open         = fcfs_open,
	.close        = fcfs_close,
	.read         = fcfs_read,
	.write        = fcfs_write,
	.flush        = fcfs_flush,
	.truncate     = fcfs_truncate,
	.get_filesize = fcfs_get_filesize,
	.update_data_placement = fcfs_update_data_placement,
};

struct iosched_ops *iosched_get_ops(void)
{
	return &fcfs_ops;
}

/* 
 * OSR
 * 
 * In our MinGW environment, we dynamically link to the package 
 * data. 
 *  
 */
#if !defined(mingw_PLATFORM) || defined(HP_mingw_BUILD)
extern char iosched_fcfs_dat[];
#endif

const char *iosched_get_message_bundle_name(void **message_data)
{
    /* 
     * OSR
     * 
     * In our MinGW environment, we dynamically link to the package 
     * data. 
     *  
     */
#if !defined(mingw_PLATFORM) || defined(HP_mingw_BUILD)
	*message_data = iosched_fcfs_dat;
#else
	*message_data = NULL;
#endif
	return "iosched_fcfs";
}
