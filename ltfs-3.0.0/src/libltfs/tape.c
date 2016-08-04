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
** FILE NAME:       tape.c
**
** DESCRIPTION:     Implements a backend-independent tape drive interface.
**
** AUTHORS:         Brian Biskeborn
**                  IBM Almaden Research Center
**                  bbiskebo@us.ibm.com
**
**                  Atsushi Abe
**                  IBM Yamato, Japan
**                  PISTE@jp.ibm.com
**
**                  Lucas C. Villa Real
**                  IBM Almaden Research Center
**                  lucasvr@us.ibm.com
**
*************************************************************************************
**
**  (C) Copyright 2015 Hewlett Packard Enterprise Development LP.
**  07/06/10 Added support for reading and setting a new MAM attribute (EWSTATE) to
**            track when Early Warning EOM is encountered.  
**           Added new function to determine whether a format operation may proceed 
**            (see also mkltfs.c)
**  10/18/10 Adjusted message identifiers to new architecture
**  02/21/11 Allow for different drive behaviour in tape_recover_eod_status()
**  08/09/11 Added new function to delete EWSTATE attribute: tape_remove_ewstate()
**  10/23/12 Change initial locating command in tape_format() and tape_unformat()
**            to perform Load instead of Locate.  Same effect but works better in
**            some situations.  Fix provided by Quantum Corporation.
**  07/09/13 Remove the unload in tape_recover_eod_status, it is unnecessary and 
**            breaks the recovery process
**
*************************************************************************************
**
** Copyright (C) 2012 OSR Open Systems Resources, Inc.
** 
************************************************************************************* 
*/

#ifdef mingw_PLATFORM
#include "arch/win/win_util.h"
#endif

#include <string.h>
#include <errno.h>
#include <unistd.h>

#ifdef __APPLE__
#include <ICU/unicode/utf8.h>
#include <ICU/unicode/ustring.h>
#else
/* Added to fix Windows compilation */
#ifndef HP_mingw_BUILD
#include <unicode/utf8.h>
#include <unicode/ustring.h>
#endif
#endif

#include "ltfs_error.h"
#include "tape.h"
#include "tape_ops.h"
#include "ltfs_endian.h"
#include "kmi.h"
#include "xattr.h"

enum partition_status {
	PART_WRITABLE = 0,  /* Device is writable */
	PART_LESS_SPACE,    /* Programmable early warning is reported. */
	PART_NO_SPACE       /* Early warning is reported. */
};

static bool is_key_set = false; /* If the value is true, set_key() was called with a valid key. */

/**
 * Required definitions for user interruption
 * These definitions should be used in only EOD recovery
 */
extern bool ltfs_is_interrupted(void);

#ifdef INTERRUPTED_RETURN
#undef INTERRUPTED_RETURN
#endif
#define INTERRUPTED_RETURN()					\
	do{											\
		if (ltfs_is_interrupted()) {			\
			ltfsmsg(LTFS_INFO, "17159I");		\
			free(buf);							\
			return -LTFS_INTERRUPTED;			\
		}										\
	}while (0)

/**
 * Allocate space for a tape device.
 * @param device on success, points to allocated device structure
 * @return 0 on success or a negative value on failure.
 */
int tape_device_alloc(struct device_data **device)
{
	int ret;

	struct device_data *newdev = calloc(1, sizeof(struct device_data));
	if (! newdev) {
		ltfsmsg(LTFS_ERR, "10001E", "tape_device_alloc: device data");
		return -LTFS_NO_MEMORY;
	}

	ret = ltfs_mutex_init(&newdev->backend_mutex);
	if (ret) {
		ltfsmsg(LTFS_ERR, "12008E", ret);
		free(newdev);
		return -LTFS_MUTEX_INIT;
	}
	ret = ltfs_mutex_init(&newdev->read_only_flag_mutex);
	if (ret) {
		ltfsmsg(LTFS_ERR, "12008E", ret);
		ltfs_mutex_destroy(&newdev->backend_mutex);
		free(newdev);
		return -LTFS_MUTEX_INIT;
	}
	ret = ltfs_mutex_init(&newdev->append_pos_mutex);
	if (ret) {
		ltfsmsg(LTFS_ERR, "12008E", ret);
		free(newdev);
		return -LTFS_MUTEX_INIT;
	}

	*device = newdev;
	return 0;
}

/**
 * Free a tape device structure, closing its associated device if necessary.
 * @param device device to close, set to NULL on success
 */
void tape_device_free(struct device_data **device, void * const kmi_handle, bool force)
{
	if (device && (*device)) {
		if ((*device)->backend_data)
			tape_device_close(*device, kmi_handle, force);
		ltfs_mutex_destroy(&(*device)->backend_mutex);
		ltfs_mutex_destroy(&(*device)->read_only_flag_mutex);
		free(*device);
		*device = NULL;
	}
}

/**
 * Get the default backend's tape device.
 * @param ops tape operations for the backend
 * @return a constant string to the backend's default device or NULL if the
 *  backend doesn't define a default one.
 */
const char *tape_default_device_name(struct tape_ops *ops)
{
	const char *devname = NULL;

	CHECK_ARG_NULL(ops, NULL);

	if (ops->default_device_name)
		devname = ops->default_device_name();
	return devname;
}

/**
 * Initialize a backend by opening the given device.
 * @param device device structure where the backend will be stored
 * @param devname device to open
 * @param ops tape operations for the backend
 * @return 0 on success or a negative value on error.
 */
int tape_device_open(struct device_data *device, const char *devname, struct tape_ops *ops,
	void * const kmi_handle)
{
	unsigned int i;
	int ret, reserve_tries = 0;

	CHECK_ARG_NULL(device, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(devname, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(ops, -LTFS_NULL_ARG);

	/* Validate the tape operations structure. */
	for (i=0; i<sizeof(struct tape_ops)/sizeof(void *); ++i) {
		if ((((void **)ops)[i]) == NULL) {
			ltfsmsg(LTFS_ERR, "12004E");
			return -LTFS_PLUGIN_INCOMPLETE;
		}
	}

	if (! device->backend)
		device->backend = ops;

	ret = device->backend->open(devname, &device->backend_data);
	if (ret < 0) {
		/* Cannot open device: backend open call failed */
		ltfsmsg(LTFS_ERR, "12012E");
		goto out_free;
	}

	ret = -1;
	while (ret < 0 && reserve_tries < 3) {
		++reserve_tries;
		ret = tape_reserve_device(device);
		if (ret < 0)
			sleep(1);
	}
	if (ret < 0) {
		/* Cannot open device: failed to reserve the device (%d) */
		ltfsmsg(LTFS_ERR, "12014E", ret);
		tape_device_close(device, kmi_handle, false);
		goto out_free;
	}

	/* Try to allow medium removal */
	tape_allow_medium_removal(device, true);

out_free:
	if (ret) {
		device->backend_data = NULL;
		device->backend = NULL;
	}
	return ret;
}

/**
 * Reopen the device and restore the connection without any re-reservation and
 * re-prevent removal. This functall calls after the fork of LTFS, the backend
 * doesn't need to reopen the device it may have a dummy function for reopen.
 * @param device the device to reopen
 * @param devname the device name to reopen
 */
int tape_device_reopen(struct device_data *device, const char *devname)
{
	int ret;

	CHECK_ARG_NULL(device, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(devname, -LTFS_NULL_ARG);

	ret = device->backend->reopen(devname, device->backend_data);
	if (ret < 0) {
		/* Cannot reopen device: backend reopen call failed */
		ltfsmsg(LTFS_ERR, "17181E");
	}

	return ret;
}

/**
 * Close a previously opened backend device. This does NOT unload the shared library that provides
 * the backend; use tape_unload_backend for that!
 * @param device the device to close
 * @param kmi_handle handle to the encryption key management I/F
 * @param skip_aom_setting skip append only mode setting
 */
void _tape_device_close(struct device_data *device, void * const kmi_handle,
						bool skip_aom_setting, bool force_release)
{
	if (! device) {
		ltfsmsg(LTFS_WARN, "10006W", "device", __FUNCTION__);
		return;
	}

	tape_clear_key(device, kmi_handle);
	tape_allow_medium_removal(device, force_release);
	if (!skip_aom_setting)
		tape_enable_append_only_mode(device, false);
	tape_release_device(device);

	if (device->backend && device->backend_data)
		device->backend->close(device->backend_data);
	device->backend_data = NULL;
	device->backend = NULL;

	/* Invalidate previous drive presence */
	device->previous_exist.tv_sec = 0;
	device->previous_exist.tv_nsec = 0;
}

/**
 * Just close device driver instance
 * @param device the device to close
 */
void tape_device_close_raw(struct device_data *device)
{
	if (! device) {
		ltfsmsg(LTFS_WARN, "10006W", "device", __FUNCTION__);
		return;
	}

	if (device->backend && device->backend_data)
		device->backend->close_raw(device->backend_data);
	device->backend_data = NULL;
	device->backend = NULL;

	/* Invalidate previous drive presence */
	device->previous_exist.tv_sec = 0;
	device->previous_exist.tv_nsec = 0;
}

/**
 * Verify if a given tape device is connected to the host.
 * @param devname device to test
 * @param ops tape operations for the backend
 * @return 0 on success, indicating that the device is connected to the host,
 *  or a negative value on error.
 */
int tape_device_is_connected(const char *devname, struct tape_ops *ops)
{
	CHECK_ARG_NULL(devname, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(ops, -LTFS_NULL_ARG);
	return ops->is_connected(devname);
}

/**
 * Load a tape in the device if it isn't already loaded.
 * @param dev the device to load
 * @return 0 on success or a negative value on error.
 */
int tape_load_tape(struct device_data *dev, void * const kmi_handle)
{
	int ret;
	struct tc_drive_param param;
	struct tc_remaining_cap cap;
	uint16_t pews;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);

	ret = tape_is_cartridge_loadable(dev);
	if (ret < 0)
		return ret;

	do {
		ret = dev->backend->load(dev->backend_data, &dev->position);
		if (ret == -EDEV_NO_MEDIUM) {
	           /*
	            * OSR
	            *
	            * Make this a warning instead of an error. We log errors to the
	            * event log and this just ends up being noise.
	            */
	#ifndef HP_mingw_BUILD
				ltfsmsg(LTFS_ERR, "12016E");
	#else
				ltfsmsg(LTFS_WARN, "12016E");
	#endif
			return -LTFS_NO_MEDIUM;
		} else if (ret < 0 && ! NEED_REVAL(ret)) {
			if (ret == -EDEV_MEDIUM_FORMAT_ERROR)
				ret = -LTFS_UNSUPPORTED_MEDIUM;
			return ret;
		}
	} while (NEED_REVAL(ret));

	ltfs_mutex_lock(&dev->append_pos_mutex);
	dev->append_pos[0] = dev->append_pos[1] = 0;
	ltfs_mutex_unlock(&dev->append_pos_mutex);

	ret = tape_wait_device_ready(dev, kmi_handle);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "12017E", ret);
		return -LTFS_DEVICE_UNREADY;
	}

	ret = tape_prevent_medium_removal(dev);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "12018E", ret);
		return ret;
	}

	ret = dev->backend->readpos(dev->backend_data, &dev->position);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "12019E", ret);
		return ret;
	}

	/* Set defaults for the drive
	 *    Blocksize should be set to variable
	 *    Read past filemark function should be set to false (IBM driver only?)
	 */
	ret = dev->backend->set_default(dev->backend_data);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "12020E", ret);
		return ret;
	}

	ret = tape_clear_key(dev, kmi_handle);
	if (ret < 0)
		return ret;

	/* Get remaining capacity of the tape */
	ret = tape_get_capacity(dev, &cap);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "11999E", ret);
		return ret;
	}

	/* Query device parameters */
	ret = dev->backend->get_parameters(dev->backend_data, &param);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "12021E", ret);
		return ret;
	}
	dev->max_block_size = param.max_blksize;

	/* Get programmable early warning size */
	ret = tape_get_pews(dev, &pews);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "17105E", ret);
		return ret;
	}
	pews += 10; /* 10MB is extra space not to miss PEW */

	/* Update read only flags */
	dev->physical_write_protect = param.write_protect;
	ltfs_mutex_lock(&dev->read_only_flag_mutex);
	if (param.write_protect || param.logical_write_protect)
		dev->write_protect = true;
	else
		dev->write_protect = false;
	dev->write_error = false;
	if (cap.max_p0 && cap.max_p1 && !cap.remaining_p0)
		dev->partition_space[0] = PART_NO_SPACE;
	else if (cap.remaining_p0 <= pews)
		dev->partition_space[0] = PART_LESS_SPACE;
	else
		dev->partition_space[0] = PART_WRITABLE;
	if (cap.max_p0 && cap.max_p1 && !cap.remaining_p1)
		dev->partition_space[1] = PART_NO_SPACE;
	else if (cap.remaining_p1 <= pews)
		dev->partition_space[1] = PART_LESS_SPACE;
	else
		dev->partition_space[1] = PART_WRITABLE;
	ltfs_mutex_unlock(&dev->read_only_flag_mutex);

	/* Check for previously passing Early Warning EOM: */
	ret = tape_get_ewstate(dev);
	if (ret == EWSTATE_SET) {
		dev->partition_space[1] = PART_NO_SPACE;
	}

#ifdef HP_mingw_BUILD
	if (ret == EWSTATE_CLEAR) {
		dev->position.early_warning = false;
		dev->partition_space[0] = PART_WRITABLE;
		dev->partition_space[1] = PART_WRITABLE;
	}
#endif /* HP_mingw_BUILD */

	return 0;
}

/**
 * Unroll operations made during tape_load_tape()
 * @param dev device to unload
 * @return 0 on success or a negative value on error
 */
int tape_unload_tape(struct device_data *dev)
{
	int ret;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);

	ltfsmsg(LTFS_INFO, "12022I");

	/* Invalidate previous drive presence */
	dev->previous_exist.tv_sec = 0;
	dev->previous_exist.tv_nsec = 0;

	tape_allow_medium_removal(dev, false);
	do {
		ret = tape_rewind(dev);
	} while (NEED_REVAL(ret));
	do {
		ret = dev->backend->unload(dev->backend_data, &dev->position);
	} while (NEED_REVAL(ret));

	ret = tape_enable_append_only_mode(dev, false);

	return ret;
}

/**
 * Lock the device. Each tape function calls this before using backend functions. A higher-level
 * function may lock the device before a series of tape calls to ensure exclusive access.
 * @param dev the device to lock
 * @return 0 on success or a negative value on error.
 */
int tape_device_lock(struct device_data *dev)
{
	int ret;
	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	ret = ltfs_mutex_lock(&dev->backend_mutex);
	if (ret)
		ret = -LTFS_MUTEX_INVALID;
	else if (dev->fence) {
		ret = -LTFS_DEVICE_FENCED;
		ltfs_mutex_unlock(&dev->backend_mutex);
	}
	return ret;
}

/**
 * Unlock the device. This must be called exactly once for each call to tape_device_lock().
 * @param dev the device to unlock
 * @return 0 on success or a negative value on error.
 */
int tape_device_unlock(struct device_data *dev)
{
	int ret;
	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	ret = ltfs_mutex_unlock(&dev->backend_mutex);
	switch (ret) {
		case 0:
			return 0;
		case EPERM:
			return -LTFS_MUTEX_UNLOCKED;
		case EINVAL:
		default:
			return -LTFS_MUTEX_INVALID;
	}
}

/**
 * Start fencing device lock requests.
 * All calls to tape_device_lock() after this will return -LTFS_REVAL_RUNNING.
 * The caller must hold the device lock.
 * @param dev Device data.
 * @return 0 on success or -LTFS_NULL_ARG if dev is NULL.
 */
int tape_start_fence(struct device_data *dev)
{
	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	dev->fence = true;
	return 0;
}

/**
 * Stop fencing device lock requests.
 * @param dev Device data.
 * @return 0 on success or -LTFS_NULL_ARG if dev is NULL.
 */
int tape_release_fence(struct device_data *dev)
{
	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	dev->fence = false;
	return 0;
}

/**
 * Reserve the tape device.
 * @param dev device to reserve
 * @return 0 on success or a negative value on error.
 */
int tape_reserve_device(struct device_data *dev)
{
	int ret;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);

	ret = 0;
	if (! dev->device_reserved) {
		do {
			ltfsmsg(LTFS_DEBUG, "12023D");
			ret = dev->backend->reserve_unit(dev->backend_data);
		} while (NEED_REVAL(ret));
		if (ret != 0) {
			ltfsmsg(LTFS_ERR, "12024E", ret);
			ret = (ret < 0) ? ret : -ret;
		} else
			dev->device_reserved = true;
	}
	return ret;
}

/**
 * Release the tape device.
 * @param dev device to release
 */
void tape_release_device(struct device_data *dev)
{
	int ret;

	if (! dev) {
		ltfsmsg(LTFS_WARN, "10006W", "dev", __FUNCTION__);
		return;
	} else if (! dev->backend) {
		ltfsmsg(LTFS_WARN, "10006W", "dev->backend", __FUNCTION__);
		return;
	}

	if (dev->device_reserved) {
		do {
			ltfsmsg(LTFS_DEBUG, "12025D");
			ret = dev->backend->release_unit(dev->backend_data);
		} while (NEED_REVAL(ret));
		dev->device_reserved = (ret == 0) ? false : true;
	}
}

/**
 * Prevent manual eject of the cartridge.
 * @param dev tape device handle
 * @return 0 on success or a negative value on error.
 */
int tape_prevent_medium_removal(struct device_data *dev)
{
	int ret;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);

	ret = 0;
	if (! dev->medium_locked) {
		do {
			ltfsmsg(LTFS_DEBUG, "12026D");
			ret = dev->backend->prevent_medium_removal(dev->backend_data);
		} while (NEED_REVAL(ret));
		if (ret != 0) {
			ltfsmsg(LTFS_ERR, "12027E", ret);
			ret = (ret < 0) ? ret : -ret;
		} else
			dev->medium_locked = true;
	}
	return ret;
}

/**
 * Allow manual eject of the cartridge.
 * @param dev tape device handle
 */
void tape_allow_medium_removal(struct device_data *dev, bool force_release)
{
	int ret;

	if (! dev) {
		ltfsmsg(LTFS_WARN, "10006W", "dev", __FUNCTION__);
		return;
	} else if (! dev->backend) {
		ltfsmsg(LTFS_WARN, "10006W", "dev->backend", __FUNCTION__);
		return;
	}

	if (dev->medium_locked || force_release) {
		do {
			ltfsmsg(LTFS_DEBUG, "12028D");
			ret = dev->backend->allow_medium_removal(dev->backend_data);
		} while (NEED_REVAL(ret));
		dev->medium_locked = (ret == 0) ? false : true;
	}
}

/**
 * Test if unit is ready.
 * @param dev tape device handle
 * @return 0 on success or a negative value on error
 */
int _tape_test_unit_ready(struct device_data *dev)
{
	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend_data, -LTFS_NULL_ARG);

	return dev->backend->test_unit_ready(dev->backend_data);
}

int tape_test_unit_ready(struct device_data *dev)
{
	int ret;
	struct ltfs_timespec ts_now, ts_diff;

	get_current_timespec(&ts_now);
	timer_sub(&ts_now, &dev->previous_exist, &ts_diff);

	if (ts_diff.tv_sec == 0) {
		/* skip the operation in case that previous TUR has been invoked within 1 sec */
		return 0;
	}

	ret = _tape_test_unit_ready(dev);
	if (ret < 0)
		ltfsmsg(LTFS_ERR, "12029E", ret);

	dev->previous_exist.tv_sec = ts_now.tv_sec;
	dev->previous_exist.tv_nsec = ts_now.tv_nsec;

	return ret;
}

/**
 * Get total and remaining capacity for each partition.
 * @param dev tape device handle
 * @param cap output buffer where capacity will be stored
 * @return true if unit is ready or false if not.
 */
int tape_get_capacity(struct device_data *dev, struct tc_remaining_cap *cap)
{
	int ret;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(cap, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);

	ret = dev->backend->remaining_capacity(dev->backend_data, cap);
	if (ret < 0)
		ltfsmsg(LTFS_ERR, "12030E", ret);
	return ret;
}

/**
 * Enable or disable compression in the drive.
 * @param dev tape device handle
 * @param use_compression true to enable compression, false to disable it
 * @return 0 on success or a negative value on error
 */
int tape_set_compression(struct device_data *dev, bool use_compression)
{
	int ret;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);

	ret = dev->backend->set_compression(dev->backend_data, use_compression, &dev->position);
	if (ret < 0)
		ltfsmsg(LTFS_ERR, "12031E", ret);
	return ret;
}

/**
 * Get current append position of specified partition
 * @param dev tape device handle
 * @param prt partition to get appaned position
 */
int tape_get_append_position(struct device_data *dev, tape_partition_t prt, tape_block_t *pos)
{
	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);

	if (prt > 1) {
		ltfsmsg(LTFS_ERR, "12032E", (unsigned long)prt);
		return -LTFS_BAD_PARTNUM;
	}

	ltfs_mutex_lock(&dev->append_pos_mutex);
	*pos = dev->append_pos[prt];
	ltfs_mutex_unlock(&dev->append_pos_mutex);

	return 0;
}

/**
 * Override the automatically-computed append position. This is useful in the index partition,
 * where "append" usually means overwriting the last index file on the tape.
 */
int tape_set_append_position(struct device_data *dev, tape_partition_t prt, tape_block_t block)
{
	int ret = 0;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	if (prt > 1) {
		ltfsmsg(LTFS_ERR, "12032E", (unsigned long)prt);
		return -LTFS_BAD_PARTNUM;
	}

	ltfs_mutex_lock(&dev->append_pos_mutex);
	dev->append_pos[prt] = block;
	ltfs_mutex_unlock(&dev->append_pos_mutex);

	return ret;
}

/**
 * Seek to append position on the given partition. If the recorded append position is zero,
 * seek to EOD on the partition and record that position as the append position.
 */
int tape_seek_append_position(struct device_data *dev, tape_partition_t prt, bool unlock_write)
{
	int ret = 0;
	struct tc_position new_pos;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);

	new_pos.partition = prt;
	ltfs_mutex_lock(&dev->append_pos_mutex);
	new_pos.block = dev->append_pos[prt];
	ltfs_mutex_unlock(&dev->append_pos_mutex);
	/* Goto EOD with locate command with really big positon,
	   because space command cannot specify partition. */
	if (new_pos.block == 0)
		new_pos.block = TAPE_BLOCK_MAX;
	ret = tape_seek(dev, &new_pos);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "12033E", ret);
		return ret;
	}

	if (unlock_write && dev->append_only_mode && new_pos.block != TAPE_BLOCK_MAX)
		ret = dev->backend->allow_overwrite(dev->backend_data, dev->position);

	ltfs_mutex_lock(&dev->append_pos_mutex);
	if (dev->append_pos[prt] == 0)
		dev->append_pos[prt] = dev->position.block;
	ltfs_mutex_unlock(&dev->append_pos_mutex);

	return ret;
}

/**
 * Get the maximum block size for the device.
 * @param dev the device
 * @return the max blocksize on success or a negative value on error. The block size is
 *         guaranteed to fit in a uint32_t.
 */
int tape_get_max_blocksize(struct device_data *dev, unsigned int *size)
{
	int ret;
	struct tc_drive_param param;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);

	*size = 0;

	ret = dev->backend->get_parameters(dev->backend_data, &param);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "12034E", ret);
		return ret;
	}

	*size = param.max_blksize;

	return 0;
}

/**
 * Get logically read-only state of a device.
 * @param dev the device
 * @return 0 if the device is writable i.e. compatible medium or
 * -LTFS_LOGICAL_WRITE_PROTECT if the device is logically write protected i.e. incompatible medium or
 * a negative value on error.
 */
int tape_logically_read_only(struct device_data *dev)
{
	int ret;
	struct tc_drive_param param;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);

	ret = dev->backend->get_parameters(dev->backend_data, &param);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "12034E", ret);
		return ret;
	}

	if (param.logical_write_protect) {
		ret = -LTFS_LOGICAL_WRITE_PROTECT;
	}

	return ret;
}

/**
 * Get read-only state of a device.
 * @param dev the device
 * @param partition the partition to be checked
 * @return 0 if the device is writable or a negative value on error. In particular,
 *         -LTFS_NO_SPACE is returned if the specified partition is out of space,
 *         -LTFS_LESS_SPACE is returned if there is no space to create or update file,
 *         -LTFS_WRITE_PROTECT is returned if the medium is write protected and
 *         -LTFS_WRITE_ERROR is returned if a write error has previously occurred.
 */
int tape_read_only(struct device_data *dev, tape_partition_t partition)
{
	int ret = 0;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);

	/* need to grab the lock because partition_space could be set on a failed write in another thread. */
	ltfs_mutex_lock(&dev->read_only_flag_mutex);
	if (dev->write_protect)
		ret = -LTFS_WRITE_PROTECT;
	else if (dev->write_error)
		ret = -LTFS_WRITE_ERROR;
	else {
		switch (dev->partition_space[partition]) {
			case PART_WRITABLE:
				ret = 0;
				break;
			case PART_LESS_SPACE:
				ret = -LTFS_LESS_SPACE;
				break;
			case PART_NO_SPACE:
				ret = -LTFS_NO_SPACE;
				break;
		}
	}
	ltfs_mutex_unlock(&dev->read_only_flag_mutex);
	return ret;
}

/**
 * Force a device to become read-only. Useful for read-only mounts (e.g. of previous
 * tape generations).
 * @param dev Device data.
 * @return 0 on success or a negative value on error.
 */
int tape_force_read_only(struct device_data *dev)
{
	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	ltfs_mutex_lock(&dev->read_only_flag_mutex);
	dev->write_protect = true;
	ltfs_mutex_unlock(&dev->read_only_flag_mutex);
	return 0;
}

/**
 * Rewind a device.
 * @param dev the device
 * @return 0 on success or a negative value on error
 */
int tape_rewind(struct device_data *dev)
{
	int ret;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);

	ret = dev->backend->rewind(dev->backend_data, &dev->position);
	if (ret < 0)
		ltfsmsg(LTFS_ERR, "12035E", ret);
	return ret;
}

/**
 * Seek to a given location on the tape.
 * tape_seek(dev, pos) is replased to _tape_seek(dev, pos, false) by preprocessor (see tape.h)
 *
 * @param dev the device
 * @param pos the position to locate. The filemarks field is ignored.
 * @return 0 on success or a negative value on error.
 */
int tape_seek(struct device_data *dev, struct tc_position *pos)
{
	int ret;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(pos, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);

	if (pos->partition == dev->position.partition && pos->block == dev->position.block)
		ret = 0;
	else {
		ret = dev->backend->locate(dev->backend_data, *pos, &dev->position);
		if (ret < 0)
			ltfsmsg(LTFS_ERR, "12037E", ret);
		else {
			ltfs_mutex_lock(&dev->read_only_flag_mutex);
			if (dev->position.early_warning)
				dev->partition_space[dev->position.partition] = PART_NO_SPACE;
			if (dev->partition_space[dev->position.partition] != PART_NO_SPACE && dev->position.programmable_early_warning)
				dev->partition_space[dev->position.partition] = PART_LESS_SPACE;
			ltfs_mutex_unlock(&dev->read_only_flag_mutex);
		}
	}

	if (ret == 0 && (dev->position.partition != pos->partition ||
		(pos->block != TAPE_BLOCK_MAX && pos->block != dev->position.block))) {
		ltfsmsg(LTFS_ERR, "12036E");
		ret = -LTFS_BAD_LOCATE;
	}

	return ret;
}

/**
 * Locate to end of data on the given partition.
 * @param dev the device
 * @param partition partition to seek on
 * @return 0 on success or a negative value on error.
 */
int tape_seek_eod(struct device_data *dev, tape_partition_t partition)
{
	int ret;
	struct tc_position seekpos = { .partition = partition, .block = TAPE_BLOCK_MAX, .filemarks = 0 };

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);

	if (partition > 1) {
		ltfsmsg(LTFS_ERR, "12038E", (unsigned long)partition);
		return -LTFS_BAD_PARTNUM;
	}

	ret = dev->backend->locate(dev->backend_data, seekpos, &dev->position);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "12039E", ret);
		return ret;
	}

	/* Check that partition searched is correct */
	if (partition != dev->position.partition) {
		ltfsmsg(LTFS_ERR, "11327E", partition, dev->position.partition);
		return -LTFS_BAD_LOCATE;
	}

	ltfs_mutex_lock(&dev->read_only_flag_mutex);
	if (dev->position.early_warning)
		dev->partition_space[dev->position.partition] = PART_NO_SPACE;
	if (dev->partition_space[dev->position.partition] != PART_NO_SPACE && dev->position.programmable_early_warning)
		dev->partition_space[dev->position.partition] = PART_LESS_SPACE;
	ltfs_mutex_unlock(&dev->read_only_flag_mutex);

	ltfs_mutex_lock(&dev->append_pos_mutex);
	dev->append_pos[partition] = dev->position.block;
	ltfs_mutex_unlock(&dev->append_pos_mutex);

	return 0;
}

/**
 * Get current cached tape position.
 */
int tape_get_position(struct device_data *dev, struct tc_position *pos)
{
	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(pos, -LTFS_NULL_ARG);

	memcpy(pos, &dev->position, sizeof(struct tc_position));
	return 0;
}

/**
 * Get current tape position by querying the device.
 */
int tape_update_position(struct device_data *dev, struct tc_position *pos)
{
	int ret;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(pos, -LTFS_NULL_ARG);

	ret = dev->backend->readpos(dev->backend_data, &dev->position);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "17132E");
		return ret;
	}

	memcpy(pos, &dev->position, sizeof(struct tc_position));
	return 0;
}

/**
 * Space a device by the given number of filemarks.
 * @param dev the device
 * @param count number of filemarks to space, negative to space backwards
 * @return 0 on success or a negative value on error.
 */
int tape_spacefm(struct device_data *dev, int count)
{
	int ret;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);

	if (count > 0)
		ret = dev->backend->space(dev->backend_data, count, TC_SPACE_FM_F, &dev->position);
	else
		ret = dev->backend->space(dev->backend_data, -count, TC_SPACE_FM_B, &dev->position);

	if (ret < 0)
		ltfsmsg(LTFS_ERR, "12041E", ret);
	return ret;
}

/**
 * Write a block at the current location.
 * @param dev device to write to
 * @param buf buffer to write
 * @param count size of the buffer, must be no more than the maximum device blocksize
 * @param igore_less Ignore less space (programmable early warning) condition?
 * @param ignore_nospc Ignore an out of space (early warning) condition? Set when writing Indexes.
 * @return number of bytes written, or a negative value on error.
 */
ssize_t tape_write(struct device_data *dev, const char *buf, size_t count, bool ignore_less, bool ignore_nospc)
{
	ssize_t ret;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(buf, -LTFS_NULL_ARG);
	if (! dev->backend || ! dev->backend_data) {
		ltfsmsg(LTFS_ERR, "12042E");
		return -LTFS_NULL_ARG;
	}

	ret = 0;
	ltfs_mutex_lock(&dev->read_only_flag_mutex);
	if (dev->write_protect) {
		ltfsmsg(LTFS_ERR, "12043E");
		ret = -LTFS_WRITE_PROTECT;
	} else if (dev->write_error) {
		ltfsmsg(LTFS_ERR, "12043E");
		ret = -LTFS_WRITE_ERROR;
	} else if (dev->partition_space[dev->position.partition] == PART_NO_SPACE && !ignore_nospc) {
		ltfsmsg(LTFS_ERR, "12064E");
		ret = -LTFS_NO_SPACE;
	} else if (dev->partition_space[dev->position.partition] == PART_LESS_SPACE && !ignore_less) {
		ltfsmsg(LTFS_ERR, "12064E");
		ret = -LTFS_LESS_SPACE;
	} else if (count > dev->max_block_size) {
		ltfsmsg(LTFS_ERR, "12044E", count, (unsigned long)dev->max_block_size);
		ret = -LTFS_LARGE_BLOCKSIZE;
	}
	ltfs_mutex_unlock(&dev->read_only_flag_mutex);
	if (ret < 0)
		return ret;

	ret = dev->backend->write(dev->backend_data, buf, count, &dev->position);
	if (ret < 0) {
		/* If a "real" write error occurs, refuse any additional writes */
		if (! NEED_REVAL(ret)) {
			ltfsmsg(LTFS_ERR, "12045E", ret);
			ltfs_mutex_lock(&dev->read_only_flag_mutex);
			dev->write_error = true;
			ltfs_mutex_unlock(&dev->read_only_flag_mutex);
		}
		return ret;
	} else if (dev->position.early_warning) {
		ltfs_mutex_lock(&dev->read_only_flag_mutex);
		dev->partition_space[dev->position.partition] = PART_NO_SPACE;
		ltfs_mutex_unlock(&dev->read_only_flag_mutex);
		tape_set_ewstate (dev, EWSTATE_SET);  /* Attribute indicating Early Warning has been passed */
		if (! ignore_nospc)
			count = -LTFS_NO_SPACE;
	} else if (dev->position.programmable_early_warning) {
		ltfs_mutex_lock(&dev->read_only_flag_mutex);
		dev->partition_space[dev->position.partition] = PART_LESS_SPACE;
		ltfs_mutex_unlock(&dev->read_only_flag_mutex);
		if (! ignore_less)
			count = -LTFS_LESS_SPACE;
	}

	ltfs_mutex_lock(&dev->append_pos_mutex);
	dev->append_pos[dev->position.partition] = dev->position.block;
	ltfs_mutex_unlock(&dev->append_pos_mutex);

	return count;
}

/**
 * Write filemarks to a device.
 * @param dev the device
 * @param count number of filemarks to write
 * @param igore_less Ignore less space (programmable early warning) condition?
 * @param ignore_nospc Ignore out of space condition? Set when writing Indexes.
 * @param immed Enable immediate bit?
 * @return 0 on success or a negative value on error.
 */
int tape_write_filemark(struct device_data *dev, uint8_t count, bool ignore_less, bool ignore_nospc, bool immed)
{
	int ret;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	if (! dev->backend || ! dev->backend_data) {
		ltfsmsg(LTFS_ERR, "12046E");
		return -LTFS_NULL_ARG;
	}

	ret = 0;
	ltfs_mutex_lock(&dev->read_only_flag_mutex);
	if (dev->write_protect)
		ret = -LTFS_WRITE_PROTECT;
	else if (dev->write_error)
		ret = -LTFS_WRITE_ERROR;
	else if (dev->partition_space[dev->position.partition] == PART_NO_SPACE && !ignore_nospc)
		ret = -LTFS_NO_SPACE;
	else if (dev->partition_space[dev->position.partition] == PART_LESS_SPACE && !ignore_less)
		ret = -LTFS_LESS_SPACE;
	ltfs_mutex_unlock(&dev->read_only_flag_mutex);
	if (ret < 0)
		return ret;

	ret = dev->backend->writefm(dev->backend_data, count, &dev->position, immed);
	if (ret < 0) {
		/* If a "real" write error occurs, refuse all further writes */
		if (! NEED_REVAL(ret)) {
			ltfsmsg(LTFS_ERR, "12047E", ret);
			ltfs_mutex_lock(&dev->read_only_flag_mutex);
			dev->write_error = true;
			ltfs_mutex_unlock(&dev->read_only_flag_mutex);
		}
		return ret;
	} else if (dev->position.early_warning) {
		ltfs_mutex_lock(&dev->read_only_flag_mutex);
		dev->partition_space[dev->position.partition] = PART_NO_SPACE;
		ltfs_mutex_unlock(&dev->read_only_flag_mutex);
		if (! ignore_nospc)
			ret = -LTFS_NO_SPACE;
	} else if (dev->position.programmable_early_warning) {
		ltfs_mutex_lock(&dev->read_only_flag_mutex);
		dev->partition_space[dev->position.partition] = PART_LESS_SPACE;
		ltfs_mutex_unlock(&dev->read_only_flag_mutex);
		if (! ignore_less)
			ret = -LTFS_LESS_SPACE;
	}

	ltfs_mutex_lock(&dev->append_pos_mutex);
	dev->append_pos[dev->position.partition] = dev->position.block;
	ltfs_mutex_unlock(&dev->append_pos_mutex);

	return ret;
}

/**
 * Read a block from a device.
 * @param dev the device
 * @param buf output buffer
 * @param count number of bytes to read
 * @param unusual_size set to true if the expected block size is less than count
 * @param kmi_handle key manager interface handle for getting a key of a key-alias
 * @return number of bytes read, or a negative value on error.
 */
ssize_t tape_read(struct device_data *dev, char *buf, size_t count, const bool unusual_size,
	void * const kmi_handle)
{
	ssize_t ret;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(buf, -LTFS_NULL_ARG);
	if (! dev->backend || ! dev->backend_data) {
		ltfsmsg(LTFS_ERR, "12048E");
		return -LTFS_BAD_DEVICE_DATA;
	}

	ret = dev->backend->read(dev->backend_data, buf, count, &dev->position, unusual_size);
	if ((ret == -EDEV_CRYPTO_ERROR || ret == -EDEV_KEY_REQUIRED) && kmi_handle) {
		unsigned char *key = NULL;
		unsigned char *keyalias = NULL;
		int tmp = 0;

		do {
			tmp = tape_get_keyalias(dev, &keyalias);
			if (tmp < 0) {
				ltfsmsg(LTFS_ERR, "17175E", tmp);
				break;
			}
			tmp = kmi_get_key(&keyalias, &key, kmi_handle);
			if (tmp < 0) {
				ltfsmsg(LTFS_ERR, "17176E", tmp);
				break;
			}
			if (! key) {
				ltfsmsg(LTFS_ERR, "17177E");
				break;
			}
			tmp = tape_set_key(dev, keyalias, key);
			if (tmp < 0) {
				ltfsmsg(LTFS_ERR, "17178E", tmp);
				break;
			}

			/* try to read using the suitable data key */
			ret = dev->backend->read(dev->backend_data, buf, count, &dev->position, unusual_size);
		} while(0);
	}

	if (ret == -EDEV_CRYPTO_ERROR || ret == -EDEV_KEY_REQUIRED)
		ltfsmsg(LTFS_WARN, "17192W");
	if (ret < 0)
		ltfsmsg(LTFS_ERR, "12049E", ret);
	return ret;
}

/**
 * Issue erase command to the drive
 * @param dev the device
 * @param long_erase Set long bit
 * @return 0 on success or a negative value on error.
 */
int tape_erase(struct device_data *dev, bool long_erase)
{
	int ret;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);

	ret = dev->backend->erase(dev->backend_data, &dev->position, long_erase);
	if (ret < 0)
		ltfsmsg(LTFS_ERR, "17149E", ret);

	return ret;
}

/**
 * Reset tape medium's capacity proportion
 * @param dev device to format
 * @return 0 on success or a negative value on error
 */
int tape_reset_capacity(struct device_data *dev)
{
	int ret;
	struct tc_position bom = { .partition = 0, .block = 0, .filemarks = 0 };

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);

	/* Locate block 0 @ P0 */
	ret = dev->backend->locate(dev->backend_data, bom, &dev->position);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "17163E", ret);
		return ret;
	}

	/* Issue Set Capacity */
	ret = dev->backend->setcap(dev->backend_data, 0xFFFF);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "17164E", ret);
		return ret;
	}

	/* Clear expected unit attention condition: Mode Parameter Changed */
	_tape_test_unit_ready(dev);

	return 0;
}

/**
 * Format tape for LTFS (make dual-partition tape)
 * @param dev device to format
 * @param index_part partition number for index partition
 * @param vol_name An option volume name
 * @return 0 on success or a negative value on error
 */
int tape_format(struct device_data *dev, tape_partition_t index_part,
		const char *vol_name, const char *barcode_name)
{
	int ret, pagelen = 0;
	unsigned char mp_medium_partition[TC_MP_MEDIUM_PARTITION_SIZE+4];
	int page_length = TC_MP_MEDIUM_PARTITION_SIZE;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);

	/* Locate block 0 @ P0 */
	/*
	 * Locate command replaced with Load.
	 * Performing a locate won't work if the tape format is somehow corrupted
	 * (e.g. no EOD), so instead perform a SCSI Load which is guaranteed
	 * to position you at BOP0.
	 */
	ret = dev->backend->load(dev->backend_data, &dev->position);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "17152E", ret);
		return ret;
	}

	/* Issue Mode Sense (MP x11) */
	memset(mp_medium_partition, 0, TC_MP_MEDIUM_PARTITION_SIZE+4);
	ret = dev->backend->modesense(dev->backend_data, TC_MP_MEDIUM_PARTITION, TC_MP_PC_CURRENT, 0x00,
		mp_medium_partition, TC_MP_MEDIUM_PARTITION_SIZE);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "12051E", ret);
		return ret;
	}

	/*
	 * Extract returned length (may vary, depending on product) and add two
	 * (to account for the length bytes) */
	pagelen = ((int)mp_medium_partition[0] << 8) + ((int)mp_medium_partition[1]);
	pagelen += 2;

	/* Set appropriate values to the page and Issue Mode Select */
	mp_medium_partition[0]  = 0x00;
	mp_medium_partition[1]  = 0x00;
	mp_medium_partition[19] = 0x01;
	mp_medium_partition[20] = 0x20 | (mp_medium_partition[20] & 0x1F); /* Set FDP=0, SDP=0, IDP=1 ==> User Setting */
#if !(defined(HP_BUILD) || defined(GENERIC_OEM_BUILD) || defined(QUANTUM_BUILD))
	mp_medium_partition[22] = 0x00;
#endif
	if (index_part == 1) {
		mp_medium_partition[24] = 0xFF; /* Set Partition0 Capacity */
		mp_medium_partition[25] = 0xFF;
		/* Set Partition1 Capacity to 1GB, This value will round up to minimum partition size in FCR3175-r2 */
		/* This field meaning will be chnaged in FCR3175-r3. In r3 n of "minumim partition size * n" should be specified. */
		/* If set this parameter to 1, we can support both specs. */
		/* In r2, this value is rounded up to minimum partition size. In r3, this value is the correct value.*/
		mp_medium_partition[26] = 0x00; /* Set Partition1 Capacity */
		mp_medium_partition[27] = 1;    /* will round up to minimum partition size */
	} else {
		mp_medium_partition[24] = 0x00; /* Set Partition0 Capacity */
		mp_medium_partition[25] = 1;    /* will round up to minimum partition size */
		mp_medium_partition[26] = 0xFF; /* Set Partition1 Capacity */
		mp_medium_partition[27] = 0xFF;
	}

	if (mp_medium_partition[17] > 0x0A) {
		page_length += (mp_medium_partition[17] - 0x0A);
	}

	ret = dev->backend->modeselect(
		dev->backend_data,
		mp_medium_partition,
		pagelen
		);

	/* Issue Format Medium (destroy all medium data and make 2-partitition medium) */
	ret = dev->backend->format(dev->backend_data, TC_FORMAT_DEST_PART, vol_name, barcode_name);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "12053E", ret);
		return ret;
	}

	/* Clear the 'Passed Early Warning' attribute since tape is now empty */
	ret = tape_set_ewstate(dev, EWSTATE_CLEAR);
	if (ret < 0)
		ltfsmsg(LTFS_ERR, "12053E", ret);

	/* Reset partition space flag */
	dev->partition_space[0] = dev->partition_space[1] = PART_WRITABLE;
	return 0;
}

/**
 * Unformat tape (make single partition tape)
 * @param dev device to format
 * @return 0 on success or a negative value on error
 */
int tape_unformat(struct device_data *dev)
{
	int ret;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);

	/* Locate block 0 @ P0
	 * Locate command replaced with Load.
	 * Performing a locate won't work if the tape format is somehow corrupted
	 * (e.g. no EOD),  so instead perform a SCSI Load which is guaranteed to
	 * position you at BOP0.
	 */
	ret = dev->backend->load(dev->backend_data, &dev->position);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "17152E", ret);
		return ret;
	}

	/* Issue Format Medium */
	ret = dev->backend->format(dev->backend_data, TC_FORMAT_DEFAULT, NULL, NULL);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "12055E", ret);
		return ret;
	}

	/* Remove the "EWSTATE" attribute (if set).  Don't take any action if
	 *   it doesn't work since the important task has already been completed. */
	ret = tape_remove_ewstate(dev);

	/* Reset partition space flag */
	dev->partition_space[0] =
	dev->partition_space[1] = PART_WRITABLE;

	return 0;
}

/**
 * Get Volume Change Reference
 * @param dev device to format
 * @param vcr pointer to store volume chnage reference value
 * @return 0 on success or a negative value on error
 */
int tape_get_volume_change_reference(struct device_data *dev, uint64_t *volume_change_ref)
{
	int ret;
	unsigned char vcr_data[TC_MAM_PAGE_VCR_SIZE + TC_MAM_PAGE_HEADER_SIZE];

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);

	ret = dev->backend->read_attribute(dev->backend_data, 0,
									   TC_MAM_PAGE_VCR,
									   vcr_data,
									   sizeof(vcr_data));

	if (ret == 0) {
		*volume_change_ref = (uint64_t)ltfs_betou32(vcr_data + 5);
		if (*volume_change_ref == UINT32_MAX)
			*volume_change_ref = UINT64_MAX; /* maintain "unusable VCR" state correctly */
	} else {
		ltfsmsg(LTFS_WARN, "12056W", ret);
		*volume_change_ref = UINT64_MAX; /* disallow use of VCR */
	}

	return ret;
}

/**
 * Get cartridge coherency data
 * @param dev device to format
 * @return 0 on success or a negative value on error
 */
int tape_get_cart_coherency(struct device_data *dev, const tape_partition_t part,
	struct tc_coherency *coh)
{
	int ret;
	unsigned char coh_data[TC_MAM_PAGE_COHERENCY_SIZE + TC_MAM_PAGE_HEADER_SIZE];

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);

	ret = dev->backend->read_attribute(dev->backend_data, part,
									   TC_MAM_PAGE_COHERENCY,
									   coh_data,
									   sizeof(coh_data));

	if (ret == 0) {
		uint16_t id  = ltfs_betou16(coh_data);
		uint16_t len = ltfs_betou16(coh_data + 3);
		uint8_t  vcr_size = coh_data[5];

		if (id != TC_MAM_PAGE_COHERENCY) {
			ltfsmsg(LTFS_WARN, "12058W", id);
			return -LTFS_UNEXPECTED_VALUE;
		}

		if (len != TC_MAM_PAGE_COHERENCY_SIZE) {
			ltfsmsg(LTFS_WARN, "12059W", len);
			return -LTFS_UNEXPECTED_VALUE;
		}

		coh->volume_change_ref = 0;
		coh->set_id = 0;

		switch (vcr_size) {
			case 8:
				coh->volume_change_ref = ltfs_betou64(coh_data + 6);
				break;
			default:
				ltfsmsg(LTFS_WARN, "12060W", vcr_size);
				return -LTFS_UNEXPECTED_VALUE;
		}

		coh->count = ltfs_betou64(coh_data + 14);
		coh->set_id = ltfs_betou64(coh_data + 22);

		/* Allow ap_clent_specific_len is 42 and 43 to keep backward compatibility.
		 * It should be 43 but in LTFS 1.0 and 1.0.1, it was set 42 as a code bug...
		 */
		uint16_t ap_clent_specific_len = ltfs_betou16(coh_data + 30);
		if (ap_clent_specific_len != 42 && ap_clent_specific_len != 43) {
			ltfsmsg(LTFS_WARN, "12061W", ap_clent_specific_len);
			return -LTFS_UNEXPECTED_VALUE;
		} else if (strncmp((char *)coh_data + 32, "LTFS", sizeof("LTFS")) != 0) {
			ltfsmsg(LTFS_WARN, "12062W");
			return -LTFS_UNEXPECTED_VALUE;
		}

		memcpy(coh->uuid, coh_data + 37, 37);

		/* Don't need to check the version field because the values parsed above are guaranteed
		 * to be supported in every version of the LTFS MAM parameters.
		 */
		coh->version = coh_data[74];
	} else
		ltfsmsg(LTFS_WARN, "12057W", ret);

	return ret;
}

/**
 * Set cartridge coherency data
 * @param dev device to format
 * @return 0 on success or a negative value on error
 */
int tape_set_cart_coherency(struct device_data *dev, const tape_partition_t part,
	struct tc_coherency *coh)
{
	int ret;
	unsigned char coh_data[TC_MAM_PAGE_COHERENCY_SIZE + TC_MAM_PAGE_HEADER_SIZE];

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);

	ltfs_u16tobe(coh_data, TC_MAM_PAGE_COHERENCY);
	coh_data[2]  = 0;
	ltfs_u16tobe(coh_data + 3, TC_MAM_PAGE_COHERENCY_SIZE);
	coh_data[5]  = 0x08; /* Size of Volume Change Reference Value (VCR)*/
	ltfs_u64tobe(coh_data + 6, coh->volume_change_ref);
	ltfs_u64tobe(coh_data + 14, coh->count); /* VOLUME COHERENCY COUNT */
	ltfs_u64tobe(coh_data + 22, coh->set_id); /* VOLUME COHERENCY SET IDENTIFIER */
	/* APPLICATION CLIENT SPECIFIC INFORMATION LENGTH */
	coh_data[30] = 0;  /* Size of APPLICATION CLIENT SPECIFIC INFORMATION (Byte 1) */
	coh_data[31] = 43; /* Size of APPLICATION CLIENT SPECIFIC INFORMATION (Byte 0) */
	strcpy((char *)coh_data + 32, "LTFS");
	memcpy(coh_data + 37, coh->uuid, 37);
	/*
	   Version field
		0: GA and PGA1
		1: From PGA2
	*/
	coh_data[74] = coh->version; /* version field should be specified before calling this function */

	ret = dev->backend->write_attribute(dev->backend_data, part, coh_data, sizeof(coh_data));
	if (ret < 0)
		ltfsmsg(LTFS_WARN, "12063W", ret);
	return ret;
}

/**
 * Check EOD valility
 * @param dev device to check EOD status
 * @param part partition to check EOD status
 * @return 0 on success or a negative value on error
 */
int tape_check_eod_status(struct device_data *dev, const tape_partition_t part)
{
	int ret;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);

	ret = dev->backend->get_eod_status(dev->backend_data, part);
	switch(ret) {
	case -EDEV_UNSUPPORTED_FUNCTION:
		ret = EOD_UNKNOWN;
		break;
	case EOD_GOOD:
	case EOD_MISSING:
	case EOD_UNKNOWN:
		break;
	default:
		ret = EOD_UNKNOWN;
		break;
	}

	return ret;
}

/**
 * Recover EOD status of current partition
 * @param dev device to format
 * @return 0 on success or a negative value on error
 */
int tape_recover_eod_status(struct device_data *dev, void * const kmi_handle)
{
	int ret;
	char *buf;
	unsigned int recover_block_size;

	struct tc_position eod_pos;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);

	ret = tape_get_max_blocksize(dev, &recover_block_size);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "17195E", "eod", ret);
		return ret;
	}

#if 0
	buf = calloc(1, recover_block_size + LTFS_CRC_SIZE);
#endif /* 0 */
	buf = calloc(1, recover_block_size);
	if (! buf) {
		ltfsmsg(LTFS_ERR, "10001E", "tape_recover_eod_status: data buffer");
		return -LTFS_NO_MEMORY;
	}

	/* Read forward by hitting read perm (actual EOD), or EOD */
	ltfsmsg(LTFS_INFO, "17127I");
	ret = 0;
	while ( ret >= 0) {
		INTERRUPTED_RETURN();
		ret = tape_read(dev, buf, (size_t)recover_block_size, true, kmi_handle);
		if(ret == -EDEV_EOD_DETECTED) {
			ltfsmsg(LTFS_INFO, "17169I");
		} else if (ret == -EDEV_READ_PERM)
			ltfsmsg(LTFS_INFO, "17130I");
		else {
			if(ret < 0)
				ltfsmsg(LTFS_WARN, "17129W");
		}
	}
	free(buf);
	buf = NULL;

	/* Read position to specify the erase position */
	ret = dev->backend->readpos(dev->backend_data, &eod_pos);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "17132E");
		return ret;
	}

#if (defined HP_BUILD) || (defined QUANTUM_BUILD) || (defined GENERIC_OEM_BUILD)
	/* The last-reported position may be the unreadable block, so back off by 1 */
	eod_pos.block--;
#endif

	/* Unload -> Load -> locate(erase point) -> erase to avoid drive fence behavior */
	INTERRUPTED_RETURN();
	ltfsmsg(LTFS_INFO, "17131I", eod_pos.partition, eod_pos.block);

	/* HP Change - the unload is unnecessary with our drive and also causes problems */
#if !(defined(HP_BUILD) || defined(GENERIC_OEM_BUILD) || defined(QUANTUM_BUILD))
	ret = tape_unload_tape(dev);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "17133E");
		return ret;
	}
#endif

	INTERRUPTED_RETURN();
	ret = tape_load_tape(dev, kmi_handle);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "17134E");
		return ret;
	}

	INTERRUPTED_RETURN();
	ret = tape_seek(dev, &eod_pos);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "17135E");
		return ret;
	}

	INTERRUPTED_RETURN();
	ret = tape_erase(dev, false);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "17136E");
		return ret;
	}

	return ret;
}

/**
 * Get a list of available tape devices for LTFS found in the host. The caller is
 * responsible from allocating the buffer to contain the tape drive information
 * by get_device_count() call.
 * When buf is NULL, this function just returns an available tape device count.
 * @param ops tape operations for the backend
 * @param[out] buf Pointer to tc_drive_info structure array.
 *             The backend must fill this structure when this paramater is not NULL.
 * @param count size of array in buf.
 * @return on success, available device count on this system or a negative value on error.
 */
int tape_get_device_list(struct tape_ops *ops, struct tc_drive_info *buf, int count)
{
	CHECK_ARG_NULL(ops, -LTFS_NULL_ARG);
	return ops->get_device_list(buf, count);
}

/**
 * Print the backend's LTFS help message.
 * @param ops tape operations for the backend
 */
void tape_print_help_message(const char *progname, struct tape_ops *ops)
{
	if (! ops) {
		ltfsmsg(LTFS_WARN, "10006W", "ops", __FUNCTION__);
		return;
	}

	if (ops->help_message)
		ops->help_message(progname);
}

int tape_parse_opts(struct device_data *dev, void *opt_args)
{
	int ret;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(opt_args, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);

	ret = dev->backend->parse_opts(dev->backend_data, opt_args);
	if (ret < 0)
		/* Cannot parse backend options: backend call failed (%d) */
		ltfsmsg(LTFS_ERR, "12040E", ret);

	return ret;
}

int tape_parse_library_backend_opts(void *opts, void *opt_args)
{
	int rc;
	struct tape_ops *backend = (struct tape_ops *) opts;

	CHECK_ARG_NULL(opts, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(opt_args, -LTFS_NULL_ARG);

	rc = backend->parse_opts(NULL, opt_args);
	if (rc < 0)
		/* Cannot parse backend options: backend call failed (%d) */
		ltfsmsg(LTFS_ERR, "12040E", rc);

	return rc;
}

/**
 * Get inquiry data from the tape device
 * @param device handle to tape device
 * @param inq structure where inquiry data will be stored
 * @return 0 on success or a negative value on error
 */
int tape_inquiry(struct device_data *dev, struct tc_inq *inq)
{
	int ret;

	CHECK_ARG_NULL(inq, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend_data, -LTFS_NULL_ARG);

	ret = dev->backend->inquiry(dev->backend_data, inq);
	if (ret < 0) {
		/* Failed to inquiry the tape: backend call failed (%d) */
		ltfsmsg(LTFS_ERR, "12013E", ret);
	}

	return ret;
}

/**
 * Get inquiry data from a given page from the tape device
 * @param device handle to tape device
 * @param page page to get inquiry data from
 * @param inq structure where inquiry data will be stored
 * @return 0 on success or a negative value on error
 */
int tape_inquiry_page(struct device_data *dev, unsigned char page, struct tc_inq_page *inq)
{
	int ret;

	CHECK_ARG_NULL(inq, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend_data, -LTFS_NULL_ARG);

	ret = dev->backend->inquiry_page(dev->backend_data, page, inq);
	if (ret < 0) {
		/* Failed to inquiry tape page: backend call failed (%d) */
		ltfsmsg(LTFS_ERR, "12013E", ret);
	}

	return ret;
}

/**
 * Locate to next index from current position
 * @param device handle to tape device
 * @return 0 on success or a negative value on error
 */
int tape_locate_next_index(struct device_data *dev)
{
	int ret;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend_data, -LTFS_NULL_ARG);

	ret = tape_spacefm(dev, 1);

	return ret;
}

/**
 * Locate to previous index from current position
 * @param device handle to tape device
 * @return 0 on success or a negative value on error
 */
int tape_locate_previous_index(struct device_data *dev)
{
	int ret;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend_data, -LTFS_NULL_ARG);

	ret = tape_spacefm(dev, -4);
	if (! ret)
		ret = tape_spacefm(dev, 1);

	return ret;
}

/**
 * Locate to first index
 * @param device handle to tape device
 * @param partition partition to locate
 * @return 0 on success or a negative value on error
 */
int tape_locate_first_index(struct device_data *dev, tape_partition_t partition)
{
	int ret;
	struct tc_position seekpos = { .partition = partition, .block = 4, .filemarks = 0 };

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend_data, -LTFS_NULL_ARG);

	ret = tape_seek(dev, &seekpos);
	if (! ret)
		ret = tape_spacefm(dev, 1);

	return ret;
}

/**
 * Locate to last index
 * @param device handle to tape device
 * @param partition partition to locate
 * @return 0 on success or a negative value on error
 */
int tape_locate_last_index(struct device_data *dev, tape_partition_t partition)
{
	int ret;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend_data, -LTFS_NULL_ARG);

	ret = tape_seek_eod(dev, partition);
	if(ret)
		return ret;

	ret = tape_spacefm(dev, -2);
	if(!ret)
		ret = tape_spacefm(dev, 1);

	return ret;
}

/**
 * Get cartridge health info
 * @param device handle to tape device
 * @param hlt pointer to cartridge health info
 * @return 0 on success or a negative value on error
 */
int tape_get_cartridge_health(struct device_data *dev, cartridge_health_info *hlt)
{
	int ret;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend_data, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(hlt, -LTFS_NULL_ARG);

	ret = dev->backend->get_cartridge_health(dev->backend_data, hlt);
	return ret;
}

/**
 * Get cartridge tape alert
 * @param device handle to tape device
 * @param tape_alert pointer to tape alert. Tape alert showed by 64 bits flag, MSB is tape alert 64, LSB is tape alert 1
 * @return 0 on success or a negative value on error
 */
int tape_get_tape_alert(struct device_data *dev, uint64_t *tape_alert)
{
	int ret;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend_data, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(tape_alert, -LTFS_NULL_ARG);

	ret = dev->backend->get_tape_alert(dev->backend_data, tape_alert);
	return ret;
}

/**
 * Clear cartridge tape alert
 * @param device handle to tape device
 * @param tape_alert clear bits of tape alert. Tape alert showed by 64 bits flag, MSB is tape alert 64, LSB is tape alert 1.
 * @return 0 on success or a negative value on error
 */
int tape_clear_tape_alert(struct device_data *dev, uint64_t tape_alert)
{
	int ret;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend_data, -LTFS_NULL_ARG);

	ret = dev->backend->clear_tape_alert(dev->backend_data, tape_alert);
	return ret;
}

/**
 * Get vendor unique (backend unique) xattribute
 * @param device handle to tape device
 * @param name pointer to vendor unique xattr name
 * @param buf pointer to vendor unique xattr value
 * @return 0 on success or a negative value on error
 */
int tape_get_vendorunique_xattr(struct device_data *dev, const char *name, char **buf)
{
	int ret;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend_data, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(name, -LTFS_NULL_ARG);

	ret = dev->backend->get_xattr(dev->backend_data, name, buf);
	return ret;
}

/**
 * Set vendor unique (backend unique) xattribute
 * @param device handle to tape device
 * @param name pointer to vendor unique xattr name
 * @param buf pointer to vendor unique xattr value
 * @return 0 on success or a negative value on error
 */
int tape_set_vendorunique_xattr(struct device_data *dev, const char *name, const char *value, size_t size)
{
	int ret;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend_data, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(name, -LTFS_NULL_ARG);

	ret = dev->backend->set_xattr(dev->backend_data, name, value, size);
	return ret;
}

/**
 * Set PEWS field of Device Configuration Extention Mode Page to enable programmable
 * early warning.
 * @param device handle to tape device
 * @param Size of programmable early warning size in mega-byte
 * @return 0 on success or a negative value on error
 */
int tape_set_pews(struct device_data *dev, bool set_value)
{
	int ret;
	unsigned char mp_dev_config_ext[TC_MP_DEV_CONFIG_EXT_SIZE];
	struct tc_remaining_cap cap;
	uint64_t half_of_max_p0 = 0;
	static const uint16_t max_pews = 0xFFFF;
	uint16_t pews;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend_data, -LTFS_NULL_ARG);

	/* Get remaining capacity of the tape */
	ret = tape_get_capacity(dev, &cap);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "11999E", ret);
		return ret;
	}

	if (set_value) {
		/* set PEW to half of capacity of index partition */
		half_of_max_p0 = cap.max_p0 / 2;
		pews = (uint16_t) (half_of_max_p0 < max_pews ? half_of_max_p0 : max_pews);
	}
	else {
		/* clear PEW value */
		pews = 0;
	}

	/* Issue Mode Sense (MP x10.01) */
	memset(mp_dev_config_ext, 0, TC_MP_DEV_CONFIG_EXT_SIZE);
	ret = dev->backend->modesense(dev->backend_data, TC_MP_DEV_CONFIG_EXT, TC_MP_PC_CURRENT, 0x01,
		mp_dev_config_ext, TC_MP_DEV_CONFIG_EXT_SIZE);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "17102E", ret);
		return ret;
	}

	/* Set appropriate values to the page and Issue Mode Select */
	mp_dev_config_ext[0]  = 0x00;
	mp_dev_config_ext[1]  = 0x00;
	mp_dev_config_ext[16] &= 0x7F;
	mp_dev_config_ext[22]  = (uint8_t)(pews >> 8 & 0xFF);
	mp_dev_config_ext[23]  = (uint8_t)(pews      & 0xFF);

	ret = dev->backend->modeselect(
		dev->backend_data,
		mp_dev_config_ext,
		TC_MP_DEV_CONFIG_EXT_SIZE
		);

	if (ret < 0)
		ltfsmsg(LTFS_ERR, "17103E", ret);
	return ret;
}

/**
 * Get PEWS field of Device Configuration Extention Mode Page to enable programmable
 * @param device handle to tape device
 * @param Pointer to return the size of programmable early warning size in mega-byte
 * @return 0 on success or a negative value on error
 */
int tape_get_pews(struct device_data *dev, uint16_t *pews)
{
	int ret;
	unsigned char mp_dev_config_ext[TC_MP_DEV_CONFIG_EXT_SIZE];

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend_data, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(pews, -LTFS_NULL_ARG);

	/* Issue Mode Sense (MP x10.01) */
	memset(mp_dev_config_ext, 0, TC_MP_DEV_CONFIG_EXT_SIZE);
	ret = dev->backend->modesense(dev->backend_data, TC_MP_DEV_CONFIG_EXT, TC_MP_PC_CURRENT, 0x01,
		mp_dev_config_ext, TC_MP_DEV_CONFIG_EXT_SIZE);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "17104E", ret);
		return ret;
	}

	*pews = mp_dev_config_ext[22] << 8 | mp_dev_config_ext[23];
	return 0;
}

/**
 * Enable/disable append only mode
 * @param device handle to tape device
 * @param true to enable append only mode. false to disable.
 * @return 0 on success or a negative value on error
 */
int tape_enable_append_only_mode(struct device_data *dev, bool enable)
{
	int ret = -1;
	int i;
	bool reload = false;
	bool loaded = false;
	unsigned char mp_dev_config_ext[TC_MP_DEV_CONFIG_EXT_SIZE];

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend_data, -LTFS_NULL_ARG);

	/* Check cartridge is already loaded not not */
	for (i = 0; i < 3 && ret < 0; i++) {
		ret = _tape_test_unit_ready(dev);
	}
	loaded = (ret == 0);

	/* Issue Mode Sense (MP x10.01) */
	memset(mp_dev_config_ext, 0, TC_MP_DEV_CONFIG_EXT_SIZE);
	ret = dev->backend->modesense(dev->backend_data, TC_MP_DEV_CONFIG_EXT, TC_MP_PC_CURRENT, 0x01,
		mp_dev_config_ext, TC_MP_DEV_CONFIG_EXT_SIZE);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "17154E", ret);
		return ret;
	}

	/* If cartridge is loaded and append-only mode is to be disabled,
	   the cartridge has to be unloaded before sending mode select. */
	if (loaded && !enable && (mp_dev_config_ext[21]& 0xF0) == 0x10) {
		ret = dev->backend->unload(dev->backend_data, &dev->position);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, "17151E", ret);
			return ret;
		}
		reload = true;
	} else if (loaded && enable) {
		/* If cartridge is loaded and and append-only mode is to be enabled,
		   the current position has to be a BOP */
		ret = dev->backend->load(dev->backend_data, &dev->position);
		if (ret == -EDEV_MEDIUM_FORMAT_ERROR)
			ret = -LTFS_UNSUPPORTED_MEDIUM;
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, "17152E", "BOP", ret);
			return ret;
		}
	}

	/* Set appropriate values to the page and Issue Mode Select */
	mp_dev_config_ext[0]  = 0x00;
	mp_dev_config_ext[1]  = 0x00;
	mp_dev_config_ext[16] &= 0x7F;
	mp_dev_config_ext[21] &= 0x0F;
	mp_dev_config_ext[21] |= enable ? 0x10 : 0x00;

	ret = dev->backend->modeselect(
		dev->backend_data,
		mp_dev_config_ext,
		TC_MP_DEV_CONFIG_EXT_SIZE);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "17155E", ret);
		return ret;
	}

	if (reload) {
		ret = dev->backend->load(dev->backend_data, &dev->position);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, "17152E", "Reload", ret);
			return ret;
		}
	}

	dev->append_only_mode = enable;

	return 0;
}

/**
 * Get append only mode setting
 * @param device handle to tape device
 * @param Pointer to return the append only mode setting
 * @return 0 on success or a negative value on error
 */
int tape_get_append_only_mode_setting(struct device_data *dev, bool *enabled)
{
	int ret;
	unsigned char mp_dev_config_ext[TC_MP_DEV_CONFIG_EXT_SIZE];

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dev->backend_data, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(enabled, -LTFS_NULL_ARG);

	/* Issue Mode Sense (MP x10.01) */
	memset(mp_dev_config_ext, 0, TC_MP_DEV_CONFIG_EXT_SIZE);
	ret = dev->backend->modesense(dev->backend_data, TC_MP_DEV_CONFIG_EXT, TC_MP_PC_CURRENT, 0x01,
		mp_dev_config_ext, TC_MP_DEV_CONFIG_EXT_SIZE);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "17156E", ret);
		return ret;
	}

	*enabled = (mp_dev_config_ext[21] >> 4) & 0x0F ? true : false;
	dev->append_only_mode = *enabled;
	return 0;
}

/**
 * Check the drive can be load a cartridge or not
 * @param device handle to tape device
 * @return 0 on success or a negative value on error
 */
int tape_is_cartridge_loadable(struct device_data *dev)
{
	int ret = -EDEV_UNKNOWN, i;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);

	for(i = 0; i < 300 && ret < 0; i++) {
		ret = _tape_test_unit_ready(dev);
		switch (ret) {
		case DEVICE_GOOD:
		case -EDEV_NEED_INITIALIZE:
			ret = DEVICE_GOOD;
			goto out;
			break;
		case -EDEV_NO_MEDIUM:
			ret = -LTFS_NO_MEDIUM;
			goto out;
			break;
		case -EDEV_BECOMING_READY:
		case -EDEV_NOT_SELF_CONFIGURED_YET:
			sleep(1);
			break;
		case -EDEV_MEDIUM_MAY_BE_CHANGED:
		case -EDEV_POR_OR_BUS_RESET:
		case -EDEV_CONFIGURE_CHANGED:
			break;
		default:
			goto out;
			break;
		}
	}

out:
	return ret;
}

/**
 * Wait the drive goes to ready state
 * @param device handle to tape device
 * @return 0 on success or a negative value on error
 */
int tape_wait_device_ready(struct device_data *dev, void * const kmi_handle)
{
	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);

	int ret = -EDEV_UNKNOWN, i;
	bool print_message = false;

make_ready:
	for(i = 0; i < 3 && ret < 0; i++) {
		ret = _tape_test_unit_ready(dev);
		if (ret == -EDEV_NEED_INITIALIZE || ret == -EDEV_BECOMING_READY) {
			if (!print_message) {
				switch (ret) {
				case -EDEV_NEED_INITIALIZE:
					ltfsmsg(LTFS_INFO, "17189I", ret);
					break;
				case -EDEV_BECOMING_READY:
					ltfsmsg(LTFS_INFO, "17189I", ret);
					print_message = true;
					break;
				default:
					ltfsmsg(LTFS_ERR, "17187E", ret);
					break;
				}
			}
			uint64_t tape_alert = 0;
			if (0 <= tape_get_tape_alert(dev, &tape_alert)) {
				const uint64_t cleaning_media        = 0x0020000000000000LL; /* 000Bh */
				const uint64_t expired_cleaning_tape = 0x0000040000000000LL; /* 0016h */
				const uint64_t invalid_cleaning_tape = 0x0000020000000000LL; /* 0017h */
				const uint64_t no_start_of_data      = 0x0000000000000400LL; /* 0036h */
				const uint64_t any_cleaning_media = cleaning_media | expired_cleaning_tape |
					invalid_cleaning_tape | no_start_of_data;

				if ((tape_alert & any_cleaning_media) != 0) {
					ltfsmsg(LTFS_INFO, "17179I", tape_alert);
					return ret;
				}

				/* Don't clear the tape alert flag because following load methid will clear it */
			}
			tape_load_tape(dev, kmi_handle);
			goto make_ready;
		} else if (ret == -LTFS_NULL_ARG)
			return ret;
	}

	for(i = 0; i < 30 && ret < 0; i++) {
		ret = _tape_test_unit_ready(dev);
		if (ret != -EDEV_BECOMING_READY)
			ltfsmsg(LTFS_INFO, "17188I", ret);
		if (ret == DEVICE_GOOD || ret == -EDEV_NO_MEDIUM || ret == -EDEV_DRIVER_ERROR ||
			IS_MEDIUM_ERROR(-ret) || IS_HARDWARE_ERROR(-ret) )
			break;
		sleep(1);
	}

	return ret;
}

/**
 * Set data key
 * @param device handle to tape device
 * @param keyalias DKi of the data key
 * @param key data key
 * @return 0 on success or a negative value on error
 */
int tape_set_key(struct device_data *dev, const unsigned char *keyalias, const unsigned char *key)
{
	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	/* allow null keyalias and null key */

	struct tc_position pos = {0};
	if (key) {
		const int ret = dev->backend->readpos(dev->backend_data, &pos);
		if (ret < 0)
			return ret;

		is_key_set = true;
	}
	const int ret = dev->backend->set_key(dev->backend_data, keyalias, key);
	if (0 <= ret) {
		static int last_message_id = 0;
		if (keyalias && key) {
			ltfsmsg(LTFS_INFO, "17190I"); /* Show the message at every DK setting because the difference DK may set. */
			last_message_id = 17190;
		} else {
			if (last_message_id != 17191) {
				ltfsmsg(LTFS_INFO, "17191I"); /* Do not show the message at redundant clear. */
				last_message_id = 17191;
			}
		}

		if (pos.block) {
			/* If a multiple data keys are used or there are both plain and encrypted data on a cartridge,
			 * LTFS forces read only mode because it is incompatible with LME and SME */
			tape_force_read_only(dev);
		}
	}

	return ret;
}

/**
 * Clear data key
 * @param device handle to tape device
 * @param kmi_handle handle to key manager interface
 * @return 0 on success or a negative value on error
 */
int tape_clear_key(struct device_data *device, void * const kmi_handle)
{
	if (kmi_handle || is_key_set)
		return tape_set_key(device, NULL, NULL);

	return 0;
}

/**
 * Get data key identifier of the next written block
 * @param device handle to tape device
 * @param keyalias DKi of the next written block
 * @return 0 on success or a negative value on error
 */
int tape_get_keyalias(struct device_data *dev, unsigned char **keyalias)
{
	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(keyalias, -LTFS_NULL_ARG);

	return dev->backend->get_keyalias(dev->backend_data, keyalias);
}

int tape_takedump_drive(struct device_data *dev)
{
	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);

	return dev->backend->takedump_drive(dev->backend_data);
}

const char *tape_get_media_encrypted(struct device_data *dev)
{
	unsigned char buf[TC_MP_READ_WRITE_CTRL_SIZE] = {0};
	const int rc = dev->backend->modesense(dev->backend_data, TC_MP_READ_WRITE_CTRL, TC_MP_PC_CURRENT, 0,
		buf, sizeof(buf));

	const char *encrypted = NULL;

	if (rc != 0)
		encrypted = "unknown";
	else {
		static const int encryption_status_1 = 24;
		static const int medium_supports_encrypted_data = 0x01;

		encrypted = (buf[16 + encryption_status_1] & medium_supports_encrypted_data) == 0 ? "false" : "true";
	}

	return encrypted;
}

const char *tape_get_drive_encryption_state(struct device_data *dev)
{
	unsigned char buf[TC_MP_READ_WRITE_CTRL_SIZE] = {0};
	const int rc = dev->backend->modesense(dev->backend_data, TC_MP_READ_WRITE_CTRL, TC_MP_PC_CURRENT, 0,
		buf, sizeof(buf));

	const char *state = NULL;

	if (rc != 0)
		state = "unknown";
	else {
		static const int encryption_control_1 = 20;
		static const int encryption_state = 0x03;

		switch ((buf[16 + encryption_control_1] & encryption_state)) {
		case 0x00:
			state = "off";
			break;
		case 0x01:
			state = "on";
			break;
		case 0x02:
			state = "unknown";
			break;
		case 0x03:
			state = "on";
			break;
		}
	}

	return state;
}

const char *tape_get_drive_encryption_method(struct device_data *dev)
{
	unsigned char buf[TC_MP_READ_WRITE_CTRL_SIZE] = {0};
	const int rc = dev->backend->modesense(dev->backend_data, TC_MP_READ_WRITE_CTRL, TC_MP_PC_CURRENT, 0,
		buf, sizeof(buf));

	const char *method = NULL;

	if (rc != 0)
		method = "Unknown";
	else {
		const unsigned char encryption_method = buf[16 + 27];

		switch (encryption_method) {
		case 0x00:
			method = "No Method";
			break;
		case 0x10:
			method = "System Managed";
			break;
		case 0x1F:
			method = "Controller Managed";
			break;
		case 0x50:
			method = "Application Managed";
			break;
		case 0x60:
			method = "Library Managed";
			break;
		case 0x70:
			method = "Internal";
			break;
		case 0xFF:
			method = "Custom";
			break;
		default:
			method = "Unknown";
			break;
		}
	}

	return method;
}

int tape_get_worm_status(struct device_data *dev, bool *is_worm)
{
	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(is_worm, -LTFS_NULL_ARG);

	return dev->backend->get_worm_status(dev->backend_data, is_worm);
}

void parse_vol(char *str, int start_len, int end_len)
{
	int i;

	if (start_len < end_len) {
		for (i = start_len; i < end_len; i++) {
			str[i] = ' ';
		}
	}

	str[end_len] = '\0';

	return ;
}

/**
 * Return truncate size
 * @param truncated string
 * @param string size
 * @param max size
 * @return positive: truncate size, -1: if string is not UTF-8
 */
int u_get_truncate_size(const char *name, int name_len, int max_size)
{
	int32_t size = 0, re_size;
	UChar32 c;
	UErrorCode err = U_ZERO_ERROR;

	/* check whether string is utf8 */
	u_strFromUTF8(NULL, 0, NULL, name, (int32_t)name_len, &err);
	if (U_FAILURE(err) && err != U_BUFFER_OVERFLOW_ERROR)
		return -LTFS_ICU_ERROR;

	while (size < (int32_t) max_size) {
		re_size = size;
		U8_NEXT(name, size, (int32_t)max_size, c);
	}

	return re_size;
}
/* Separate implementation for MAM attributes exist. */
#if 0
/**
 * Set tape attribute to struct *tape_attr
 * @param LTFS volume
 * @param Tape attribute
 * @return 0: success, negative : cannot set correct value to tape_attr
 */
void set_tape_attribute(struct ltfs_volume *vol, struct tape_attr *t_attr)
{
	int len_volname = 0;

	if (!vol) {
		ltfsmsg(LTFS_ERR, "17231E", "set", "dev");
		return;
	}

	if (!t_attr) {
		ltfsmsg(LTFS_ERR, "17231E", "set", "t_attr");
		return;
	}

	/*  APPLICATION VENDOR set */
	strncpy(t_attr->vender, LTFS_VENDOR_NAME, TC_MAM_APP_VENDER_SIZE);
	parse_vol(t_attr->vender, strlen(LTFS_VENDOR_NAME), TC_MAM_APP_VENDER_SIZE);

	/* APPLICATION NAME set */
	strncpy(t_attr->app_name, PACKAGE_NAME, TC_MAM_APP_NAME_SIZE);
	parse_vol(t_attr->app_name, strlen(PACKAGE_NAME), TC_MAM_APP_NAME_SIZE);


	/* APPLICATION VERSION set */
	strncpy(t_attr->app_ver, PACKAGE_VERSION, TC_MAM_APP_VERSION_SIZE);
	parse_vol(t_attr->app_ver, strlen(PACKAGE_VERSION), TC_MAM_APP_VERSION_SIZE);

	/* USER MEDIUM LABEL set */
	memset(t_attr->medium_label, '\0', TC_MAM_USER_MEDIUM_LABEL_SIZE + 1);
	if ( vol->index->volume_name ) {
		len_volname = strlen(vol->index->volume_name);
		if ( len_volname > TC_MAM_USER_MEDIUM_LABEL_SIZE - 1) {
			ltfsmsg(LTFS_DEBUG, "17229D", "USER MEDIUM TEXT LABEL",
					vol->index->volume_name, TC_MAM_USER_MEDIUM_LABEL_SIZE - 1);
			len_volname = u_get_truncate_size(vol->index->volume_name, len_volname, TC_MAM_USER_MEDIUM_LABEL_SIZE);
			if (len_volname == -LTFS_ICU_ERROR)
				len_volname = TC_MAM_USER_MEDIUM_LABEL_SIZE - 1;
		}
		strncpy(t_attr->medium_label, vol->index->volume_name, len_volname);
	}

	/* TEXT LOCALIZATION IDENTIFIER set */
	t_attr->tli = TEXT_LOCALIZATION_IDENTIFIER_UTF8;

	/* BARCODE set */
	if ( vol->label->barcode ) {
		if ( strlen(vol->label->barcode) > TC_MAM_BARCODE_SIZE)
			ltfsmsg(LTFS_WARN, "17203W", "BARCODE", vol->label->barcode, TC_MAM_BARCODE_SIZE);
		strncpy(t_attr->barcode, vol->label->barcode, TC_MAM_BARCODE_SIZE);
		parse_vol(t_attr->barcode, strlen(vol->label->barcode), TC_MAM_BARCODE_SIZE);
	} else {
		ltfsmsg(LTFS_WARN, "17230W");
		parse_vol(t_attr->barcode, 0, TC_MAM_BARCODE_SIZE);
	}

	/* APPLICATION FORMAT VERSION set */
	strncpy(t_attr->app_format_ver, LTFS_INDEX_VERSION_STR, TC_MAM_APP_FORMAT_VERSION_SIZE);
	parse_vol(t_attr->app_format_ver, strlen(LTFS_INDEX_VERSION_STR), TC_MAM_APP_FORMAT_VERSION_SIZE);

	return;
}

/**
 * Set tape attribute from struct *tape_attr to Cartridge Memory
 * @param Device data
 * @param Tape attribute
 * @param set attribute type
 * @return 0 positive : success, negative : cannot set value to Cartridge Memory
 */
int tape_set_attribute_to_cm(struct device_data *dev, struct tape_attr *t_attr, int type)
{

	int ret;
	int attr_size;
	uint8_t format;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(t_attr, -LTFS_NULL_ARG);

	if ( type == TC_MAM_APP_VENDER ) {
		attr_size = TC_MAM_APP_VENDER_SIZE;
		format = ASCII_FORMAT;
	} else if ( type == TC_MAM_APP_NAME) {
		attr_size = TC_MAM_APP_NAME_SIZE;
		format = ASCII_FORMAT;
	} else if ( type== TC_MAM_APP_VERSION ) {
		attr_size = TC_MAM_APP_VERSION_SIZE;
		format = ASCII_FORMAT;
	} else if ( type == TC_MAM_USER_MEDIUM_LABEL ) {
		attr_size = TC_MAM_USER_MEDIUM_LABEL_SIZE;
		format = TEXT_FORMAT;
	} else if ( type == TC_MAM_TEXT_LOCALIZATION_IDENTIFIER ) {
		attr_size = TC_MAM_TEXT_LOCALIZATION_IDENTIFIER_SIZE;
		format = BINARY_FORMAT;
	} else if ( type == TC_MAM_BARCODE ) {
		attr_size = TC_MAM_BARCODE_SIZE;
		format = ASCII_FORMAT;
	} else if ( type == TC_MAM_APP_FORMAT_VERSION ) {
		attr_size = TC_MAM_APP_FORMAT_VERSION_SIZE;
		format = ASCII_FORMAT;
	} else {
		ltfsmsg(LTFS_WARN, "17204W", type, "tape_set_attribute_to_cm");
		return -1;
	}

	unsigned char attr_data[attr_size + TC_MAM_PAGE_HEADER_SIZE];

	ltfs_u16tobe(attr_data, type); 			/* set attribute type	*/
	attr_data[2] = format;					/* set data format type */
	ltfs_u16tobe(attr_data + 3, attr_size);	/* set data size		*/


	/* set attribute data */
	if ( type == TC_MAM_APP_VENDER ) {
		strncpy((char *)attr_data + 5, t_attr->vender, attr_size);
	} else if ( type == TC_MAM_APP_NAME ) {
		strncpy((char *)attr_data + 5, t_attr->app_name, attr_size);
	} else if ( type == TC_MAM_APP_VERSION ) {
		strncpy((char *)attr_data + 5, t_attr->app_ver, attr_size);
	} else if ( type == TC_MAM_USER_MEDIUM_LABEL ) {
		strncpy((char *)attr_data + 5, t_attr->medium_label, attr_size);
	} else if ( type == TC_MAM_TEXT_LOCALIZATION_IDENTIFIER ) {
		attr_data[5] =  t_attr->tli;
	} else if ( type == TC_MAM_BARCODE ) {
		strncpy((char *)attr_data + 5, t_attr->barcode, attr_size);
	} else if ( type == TC_MAM_APP_FORMAT_VERSION ) {
		strncpy((char *)attr_data + 5, t_attr->app_format_ver, attr_size);
	}

	ret = dev->backend->write_attribute(dev->backend_data,
	                                      0,				/* partition	*/
	                                      attr_data,
	                                      sizeof(attr_data));

	if (ret < 0)
		ltfsmsg(LTFS_ERR, "17205E", type, "tape_set_attribute_to_cm");

	return ret;

}

/**
 * Set all tape attribute from struct *tape_attr to Cartridge Memory
 * @param Device data
 * @param Tape attribute
 * @return 0 positive : success, negative : cannot set value to Cartridge Memory
 */
int tape_format_attribute_to_cm(struct device_data *dev, struct tape_attr *t_attr)
{
	int ret, ret_save;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(t_attr, -LTFS_NULL_ARG);

	/*  APPLICATION VENDOR set */
	ret = ret_save = tape_set_attribute_to_cm(dev, t_attr, TC_MAM_APP_VENDER);
	if (ret < 0)
		ret_save = ret;

    /* APPRICATION NAME set */
	ret = tape_set_attribute_to_cm(dev, t_attr, TC_MAM_APP_NAME);
	if (ret < 0)
		ret_save = ret;

	/* APPLICATION VERSION set */
	ret = tape_set_attribute_to_cm(dev, t_attr, TC_MAM_APP_VERSION);
	if (ret < 0)
		ret_save = ret;

	/* USER MEDIUM LABEL set */
	ret = tape_set_attribute_to_cm(dev, t_attr, TC_MAM_USER_MEDIUM_LABEL);
	if (ret < 0)
		ret_save = ret;

	/* TEXT LOCALIZATION IDENTIFIER set */
	ret = tape_set_attribute_to_cm(dev, t_attr, TC_MAM_TEXT_LOCALIZATION_IDENTIFIER);
	if (ret < 0)
		ret_save = ret;

	/* BARCODE set */
	ret = tape_set_attribute_to_cm(dev, t_attr, TC_MAM_BARCODE);
	if (ret < 0)
		ret_save = ret;

	/* APPLICATION FORMAT VERSION set */
	ret = tape_set_attribute_to_cm(dev, t_attr, TC_MAM_APP_FORMAT_VERSION);
	if (ret < 0)
		ret_save = ret;

	if (!ret && ret_save)
		ret = ret_save;

	return ret;
}

/**
 * Get tape attribute from Cartridge Memory to struct *tape_attr
 * @param Device data
 * @param Tape attribute
 * @param got attribute type
 * @return 0 positive : success, negative : cannot get value from Cartridge Memory
 */
int tape_get_attribute_from_cm(struct device_data *dev, struct tape_attr *t_attr, int type)
{
	int ret;
	int attr_len;

	CHECK_ARG_NULL(dev, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(t_attr, -LTFS_NULL_ARG);

	switch (type) {
    case TC_MAM_APP_VENDER:
		attr_len = TC_MAM_APP_VENDER_SIZE;
		break;
	case TC_MAM_APP_NAME:
		attr_len = TC_MAM_APP_NAME_SIZE;
		break;
	case TC_MAM_APP_VERSION:
		attr_len = TC_MAM_APP_VERSION_SIZE;
		break;
	case TC_MAM_USER_MEDIUM_LABEL:
		attr_len = TC_MAM_USER_MEDIUM_LABEL_SIZE;
		break;
	case TC_MAM_TEXT_LOCALIZATION_IDENTIFIER:
		attr_len = TC_MAM_TEXT_LOCALIZATION_IDENTIFIER_SIZE;
		break;
	case TC_MAM_BARCODE:
		attr_len = TC_MAM_BARCODE_SIZE;
		break;
	case TC_MAM_APP_FORMAT_VERSION:
		attr_len = TC_MAM_APP_FORMAT_VERSION_SIZE;
		break;
	default:
		ltfsmsg(LTFS_WARN, "17204W", type, "tape_get_attribute_from_cm");
		return -LTFS_BAD_ARG;
		break;
	}

	unsigned char attr_data[attr_len + TC_MAM_PAGE_HEADER_SIZE];

	ret = dev->backend->read_attribute(dev->backend_data,
										0,					/* partition	*/
										type,
										attr_data,
										sizeof(attr_data));

	if (ret == 0) {
		uint16_t id = ltfs_betou16(attr_data);
		uint16_t len = ltfs_betou16(attr_data + 3);

		if (id != type) {
			ltfsmsg(LTFS_WARN, "17196W", id);
			return -LTFS_UNEXPECTED_VALUE;
		}
		if (len != attr_len) {
			ltfsmsg(LTFS_WARN, "17197W", len);
			return -LTFS_UNEXPECTED_VALUE;
		}

		if (type == TC_MAM_APP_VENDER) {
			memcpy(t_attr->vender, attr_data + 5, attr_len);
			t_attr->vender[attr_len] = '\0';
		} else if (type == TC_MAM_APP_NAME) {
			memcpy(t_attr->app_name, attr_data + 5, attr_len);
			t_attr->app_name[attr_len] = '\0';
		} else if (type == TC_MAM_APP_VERSION) {
			memcpy(t_attr->app_ver, attr_data + 5, attr_len);
			t_attr->app_ver[attr_len] = '\0';
		} else if (type == TC_MAM_USER_MEDIUM_LABEL) {
			memcpy(t_attr->medium_label, attr_data + 5, attr_len);
			t_attr->medium_label[attr_len] = '\0';
		} else if (type == TC_MAM_TEXT_LOCALIZATION_IDENTIFIER) {
			t_attr->tli = attr_data[5];
		} else if (type == TC_MAM_BARCODE) {
			memcpy(t_attr->barcode, attr_data + 5, attr_len);
			t_attr->barcode[attr_len] = '\0';
		} else if (type == TC_MAM_APP_FORMAT_VERSION) {
			memcpy(t_attr->app_format_ver, attr_data + 5, attr_len);
			t_attr->app_format_ver[attr_len] = '\0';
		}
	} else
		ltfsmsg(LTFS_DEBUG, "17198D", type, "tape_get_attribute_from_cm");

	return ret;
}

/**
 * Get all tape attribute from Cartridge Memory to struct *tape_attr
 * @param Device data
 * @param Tape attribute
 * @return 0 positive : success, negative : cannot get value from Cartridge Memory
 */
void tape_load_all_attribute_from_cm(struct device_data *dev, struct tape_attr *t_attr)
{
	int ret;

	if (!dev) {
		ltfsmsg(LTFS_ERR, "17231E", "get", "dev");
		return;
	}

	if (!t_attr) {
		ltfsmsg(LTFS_ERR, "17231E", "get", "t_attr");
		return;
	}

	/* get APPLICATION VENDER */
	ret = tape_get_attribute_from_cm(dev, t_attr, TC_MAM_APP_VENDER);
	if (ret < 0)
		t_attr->vender[0] = '\0';

	/* get APPLICATION NAME */
	ret = tape_get_attribute_from_cm(dev, t_attr, TC_MAM_APP_NAME);
	if (ret < 0)
		t_attr->app_name[0] = '\0';

	/* get APPLICATION VERSION */
	ret = tape_get_attribute_from_cm(dev, t_attr, TC_MAM_APP_VERSION);
	if (ret < 0)
		t_attr->app_ver[0] = '\0';

	/* get USER MEDIUM TEXT LABEL */
	ret = tape_get_attribute_from_cm(dev, t_attr, TC_MAM_USER_MEDIUM_LABEL);
	if (ret < 0)
		t_attr->medium_label[0] = '\0';

	/* get TEXT LOCALIZATION IDENTIFIER */
	ret = tape_get_attribute_from_cm(dev, t_attr, TC_MAM_TEXT_LOCALIZATION_IDENTIFIER);
	if (ret < 0)
		t_attr->tli = '\0';

	/* get BARCODE */
	ret = tape_get_attribute_from_cm(dev, t_attr, TC_MAM_BARCODE);
	if (ret < 0)
		t_attr->barcode[0] = '\0';

	/* get APPLICATION FORMAT VERSION */
	ret = tape_get_attribute_from_cm(dev, t_attr, TC_MAM_APP_FORMAT_VERSION);
	if (ret < 0)
		t_attr->app_format_ver[0] = '\0';

	ltfsmsg(LTFS_INFO, "17227I", "Vendor", t_attr->vender);
	ltfsmsg(LTFS_INFO, "17227I", "Application Name", t_attr->app_name);
	ltfsmsg(LTFS_INFO, "17227I", "Application Version", t_attr->app_ver);
	ltfsmsg(LTFS_INFO, "17227I", "Medium Label", t_attr->medium_label);
	ltfsmsg(LTFS_INFO, "17228I", "Text Localization ID", t_attr->tli);
	ltfsmsg(LTFS_INFO, "17227I", "Barcode", t_attr->barcode);
	ltfsmsg(LTFS_INFO, "17227I", "Application Format Version", t_attr->app_format_ver);

	return;
}

/**
 * Update tape attribute to Cartridge Memory
 * @param LTFS volume
 * @param set value
 * @param attribute type
 * @param set value size
 * @return 0 positive : success, negative : cannot update value to Cartridge Memory
 */
int update_tape_attribute (struct ltfs_volume *vol, const char *new_value, int type, int size)
{
	int ret;
	char *pre_attr;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);

	/* type check */
	if (type != TC_MAM_USER_MEDIUM_LABEL && type != TC_MAM_BARCODE) {
		ltfsmsg(LTFS_WARN, "17204W", type, "update_tape_attribute");
		return -1;
	}

	if (!vol->t_attr)
		return -1;

	/* Attribute size check */
	if ( type == TC_MAM_USER_MEDIUM_LABEL ) {
		if ( size > TC_MAM_USER_MEDIUM_LABEL_SIZE - 1) {
			ltfsmsg(LTFS_DEBUG, "17229D", "USER MEDIUM TEXT LABEL",
					vol->index->volume_name, TC_MAM_USER_MEDIUM_LABEL_SIZE - 1);
			size = u_get_truncate_size(vol->index->volume_name, size, TC_MAM_USER_MEDIUM_LABEL_SIZE);
			if (size == -LTFS_ICU_ERROR)
				size = TC_MAM_USER_MEDIUM_LABEL_SIZE - 1;
		}
		pre_attr = strdup(vol->t_attr->medium_label);
		if (! pre_attr) {
			ltfsmsg(LTFS_ERR, "10001E", "update_tape_attribute: pre_attr");
		    ret = -ENOMEM;
		    return ret;
		}
		memset(vol->t_attr->medium_label, '\0', TC_MAM_USER_MEDIUM_LABEL_SIZE + 1);
		if ( new_value ) {
			strncpy(vol->t_attr->medium_label, new_value, size);
		}
	} else if (type == TC_MAM_BARCODE) {
		if ( size > TC_MAM_BARCODE_SIZE) {
			ltfsmsg(LTFS_WARN, "17226W", "BARCODE", TC_MAM_BARCODE_SIZE);
			return -LTFS_LARGE_XATTR;
		}
		pre_attr = strdup(vol->t_attr->barcode);
		if (! pre_attr) {
		    ltfsmsg(LTFS_ERR, "10001E", "update_tape_attribute: pre_attr");
		    ret = -ENOMEM;
		    return ret;
		}
		memset(vol->t_attr->barcode, '\0', TC_MAM_BARCODE_SIZE + 1);
		if ( new_value ) {
			strncpy(vol->t_attr->barcode, new_value, size);
		}
		parse_vol(vol->t_attr->barcode, strlen(new_value), TC_MAM_BARCODE_SIZE);
	}

	ret = tape_set_attribute_to_cm(vol->device, vol->t_attr, type);
	if (ret < 0) {
		if ( type == TC_MAM_USER_MEDIUM_LABEL ) {
			memset(vol->t_attr->medium_label, '\0', TC_MAM_USER_MEDIUM_LABEL_SIZE + 1);
			strncpy(vol->t_attr->medium_label, pre_attr, strlen(pre_attr));
		} else if (type != TC_MAM_BARCODE) {
			memset(vol->t_attr->barcode, '\0', TC_MAM_BARCODE_SIZE + 1);
			strncpy(vol->t_attr->barcode, pre_attr, strlen(pre_attr));
		}
	}

	free(pre_attr);

	return ret;
}

/**
 * Read tape attribute from struct *tape_attr
 * This function for ExtendedAttribute
 * @param LTFS volume
 * @param result value
 * @param ExtendedAttribute name
 * @return 0 : success, negative : cannot read value
 */
int read_tape_attribute (struct ltfs_volume *vol, char **val, const char *name)
{
	int ret = 0;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(val, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(name, -LTFS_NULL_ARG);

	/* EA name check */
	if ( strcmp(name, "ltfs.mamBarcode") && strcmp(name, "ltfs.mamApplicationVendor") &&
			strcmp(name, "ltfs.mamApplicationVersion") &&  strcmp(name, "ltfs.mamApplicationFormatVersion")) {
		return -LTFS_UNEXPECTED_VALUE;
	}

	if (!vol->t_attr)
		return 0;

	if (! strcmp(name, "ltfs.mamBarcode")) {
		if (vol->t_attr->barcode[0] == '\0')
			return 0;
		*val = strdup(vol->t_attr->barcode);
	} else if (! strcmp(name, "ltfs.mamApplicationVendor")) {
		if (vol->t_attr->barcode[0] == '\0')
			return 0;
		*val = strdup(vol->t_attr->vender);
	} else if (! strcmp(name, "ltfs.mamApplicationVersion")) {
		if (vol->t_attr->barcode[0] == '\0')
			return 0;
		*val = strdup(vol->t_attr->app_ver);
	} else if (! strcmp(name, "ltfs.mamApplicationFormatVersion")) {
		if (vol->t_attr->barcode[0] == '\0')
			return 0;
		*val = strdup(vol->t_attr->app_format_ver);
	}

	if (!*val) {
		ltfsmsg(LTFS_ERR, "10001E", "read_tape_attribute: *val");
		return -LTFS_UNEXPECTED_VALUE;
	}

	return ret;
}
#endif /* 0 */

/**
 * Determine whether to proceed with format operation
 * @param dev device to format
 * @param force whether to force reformat of existing LTFS volume
 * @return 0 on success (ok) or a negative value on error (not ok)
 */
int tape_check_reformat_ok(struct device_data *dev, bool force)
{
#if 0
	int      ret;
	int      ret2;
	unsigned char appname[TC_MAM_PAGE_APP_NAME_SIZE + TC_MAM_PAGE_HEADER_SIZE];

	memset (appname, 0, sizeof(appname));

	ret = tape_device_lock(dev);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "12010E", __FUNCTION__);
		return ret;
	}

	ret = dev->backend->read_attribute(dev->backend_data, 0, TC_MAM_PAGE_APP_NAME, appname, sizeof(appname));
	/*
	 * Found an Application Name attribute... so if not told to FORCE the format,
	 *  return an error to indicate unable to overwrite existing media:
	 */
	if (ret == 0) {
		ltfsmsg(LTFS_INFO, "20026I", (char*)(appname+TC_MAM_PAGE_HEADER_SIZE));

		if (!force) {
			ret = -1;
			/*
			 * Found an Application Name attribute, and told to FORCE the format...
			 *  so do that by overwriting the ReadOnly flag which may have been set
			 *  by the backend for a full tape..  Potential for problems if the tape
			 *  has been hardware-write-protected, but in that case the format will
			 *  fail soon anyway.
			 */
		} else {
			dev->read_only = DEV_WRITABLE;
		}

		/*
		 * Failed to read an Application Name attribute, so assume we can go ahead
		 *  with the format.
		 */
	} else {
		ret = 0;
	}

	ret2 = tape_device_unlock(dev);

#ifdef HP_mingw_BUILD
	/* Get rid of variable set but not checked warning */
	if (ret2 != 0) {}
#endif

	return ret;
#endif

	return 0;
}

/**
 * Determine whether to proceed with unformat operation
 * @param dev device to unformat
 * @return 0 on success (ok) or a negative value on error (not ok)
 */
int tape_check_unformat_ok(struct device_data *dev)
{
	int ret = 0;

	unsigned char appname[TC_MAM_PAGE_APP_NAME_SIZE + TC_MAM_PAGE_HEADER_SIZE];

	memset (appname, 0, sizeof(appname));

	ret = dev->backend->read_attribute(dev->backend_data, 0, TC_MAM_PAGE_APP_NAME, appname, sizeof(appname));

	/*
	 * Found an Application Name attribute... so if not told to FORCE the format,
	 *  return an error to indicate unable to overwrite existing media:
	 */
	if (ret != 0) {
		ret = -1000; /* Unique return value indicating tape already unformated with LTFS */
	} else {
		ret = 0;
	}

	return ret;
}

/**
 * Get early warning state attribute from Cartridge Memory
 * @param dev device to read from
 * @return 1 (EWSTATE_SET) if known to have passed EWEOM previously, else 0 (EWSTATE_CLEAR)
 */
int tape_get_ewstate(struct device_data *dev)
{
	unsigned char	buf[TC_MAM_PAGE_EWSTATE_LEN + TC_MAM_PAGE_HEADER_SIZE];int status;
	int				ewstate = 0;

	status = dev->backend->read_attribute(dev->backend_data,
			(const tape_partition_t)0, TC_MAM_PAGE_EWSTATE, buf, sizeof(buf));
	if (status < 0)
		ewstate = 0;
	else if (TC_MAM_PAGE_IS_EWSTATE(buf))
		ewstate = (int)TC_MAM_PAGE_EWSTATE_VALUE(buf);
	else
		ewstate = EWSTATE_CLEAR;

	return ewstate;
}

/**
 * Set early warning state attribute in Cartridge Memory
 * @param dev device to read from
 * @param ewstate the early warning state to be stored (0 or 1)
 * @return 0 if set ok, negative value on error
 */
int tape_set_ewstate(struct device_data *dev, int ewstate)
{
	int				status = 0;
	unsigned char	buf[9];

	buf[0] = (unsigned char)(TC_MAM_PAGE_EWSTATE >> 8);
	buf[1] = (unsigned char)(TC_MAM_PAGE_EWSTATE & 0xFF);
	buf[2] = 0;  /* format = binary (00b)  */
	buf[3] = 0;  /* length MSB is always 0 */
	buf[4] = TC_MAM_PAGE_EWSTATE_LEN;
	TC_MAM_PAGE_EWSTATE_SETSIGNATURE(buf);  /* mark this as 'our' attribute.. */
	buf[8] = (unsigned char)ewstate;

	status = dev->backend->write_attribute(dev->backend_data,
			(const tape_partition_t)0, buf, sizeof(buf));

	if (status < 0) {
		ltfsmsg(LTFS_WARN, "20024W", TC_MAM_PAGE_EWSTATE, status);
	} else {
		ltfsmsg(LTFS_DEBUG, "20025D", ewstate);
	}

	return status;
}

/**
 * Remove early warning state attribute from Cartridge Memory
 * @param dev device to write to
 * @return 0 if deleted ok, negative value on error
 */
int tape_remove_ewstate (struct device_data *dev)
{
	unsigned char	buf[5];
	int				status;

	buf[0] = (unsigned char)(TC_MAM_PAGE_EWSTATE >> 8);
	buf[1] = (unsigned char)(TC_MAM_PAGE_EWSTATE & 0xFF);
	buf[2] = 0;  /* format = binary (00b)  */
	buf[3] = 0;  /* length MSB is always 0 */
	buf[4] = 0;  /* set length LSB to 0 to delete the attribute */

	status = dev->backend->write_attribute(dev->backend_data, (const tape_partition_t)0, buf, sizeof(buf));

	return status;
}

/**
 * Get tape's MAM attributes.
 * @param dev Device from which the attributes have to be retrieved.
 * @param attribute_id the attribute whose value to get.
 * @param part The partition of tape.
 * @param mam_attr Attributes read will be stored here.
 * @return 0 on success or a negative value on error.
 */
int tape_get_MAMattributes(struct device_data *dev, unsigned int attribute_id, const tape_partition_t part,
		struct tc_mam_attr *mam_attr) {

	int status = -1, iter = 0, i = 0, ret = 0;
	char *mam_buf = NULL;

	switch (attribute_id) {

		case TC_MAM_APPLICATION_VENDOR:
			/* Get Application Vendor: */
			mam_buf = (char *) calloc(1, 40);
			status = dev->backend->read_attribute(dev->backend_data
												, part
												, TC_MAM_APPLICATION_VENDOR
												, (unsigned char *)mam_buf
												, 20);
			if (! status) {
				/* The first 5 bytes of MAM buffer contains attribute header so extracting from 5th byte */
				mam_attr->appl_vendor = (char *) calloc(1, (TC_MAM_APPLICATION_VENDOR_LEN + 1));
				while (iter < TC_MAM_APPLICATION_VENDOR_LEN) {
					mam_attr->appl_vendor[iter] = mam_buf[iter + 5];
					iter++;
				}
				mam_attr->appl_vendor[iter] = '\0';
				iter = 0;
				free(mam_buf);
				mam_buf = NULL;
			} else {
				ltfsmsg(LTFS_WARN, "17302W", "Application Vendor", TC_MAM_APPLICATION_VENDOR);
				free(mam_buf);
				mam_buf = NULL;
				ret = status;
			}
			break;

		case TC_MAM_APPLICATION_NAME:
			/* Get Application Name: */
			mam_buf = (char *) calloc(1, 40);
			status = dev->backend->read_attribute(dev->backend_data
												, part
												, TC_MAM_APPLICATION_NAME
												, (unsigned char *)mam_buf
												, 40);
			if (! status) {
				/* The first 5 bytes of MAM buffer contains attribute header so extracting from 5th byte */
				mam_attr->appl_name = (char *) calloc(1, (TC_MAM_APPLICATION_NAME_LEN + 1));

				while (iter < TC_MAM_APPLICATION_NAME_LEN) {
					mam_attr->appl_name[iter] = mam_buf[iter + 5];
					iter++;
				}
				mam_attr->appl_name[iter] = '\0';
				iter = 0;
				free(mam_buf);
				mam_buf = NULL;
			} else {
				ltfsmsg(LTFS_WARN, "17302W", "Application Name", TC_MAM_APPLICATION_NAME);
				free(mam_buf);
				mam_buf = NULL;
				ret = status;
			}
			break;

		case TC_MAM_APPLICATION_VERSION:
				/* Get Application Version: */
				mam_buf = (char *) calloc(1, 40);
				status = dev->backend->read_attribute(dev->backend_data
													, part
													, TC_MAM_APPLICATION_VERSION
													, (unsigned char *)mam_buf
													, 20);
				if (! status) {
					/* The first 5 bytes of MAM buffer contains attribute header so extracting from 5th byte */
					mam_attr->appl_ver = (char *) calloc(1, (TC_MAM_APPLICATION_VERSION_LEN + 1));

					while (iter < TC_MAM_APPLICATION_VERSION_LEN) {
						mam_attr->appl_ver[iter] = mam_buf[iter + 5];
						iter++;
					}
					mam_attr->appl_ver[iter] = '\0';
					iter = 0;
					free(mam_buf);
					mam_buf = NULL;
				} else {
					ltfsmsg(LTFS_WARN, "17302W", "Application Version", TC_MAM_APPLICATION_VERSION);
					free(mam_buf);
					mam_buf = NULL;
					ret = status;
				}
				break;


	 case TC_MAM_APP_FORMAT_VERSION:
		    /* Get Application Format Version (to the value used in the format label, not the index -
		 		 * though they should be the same at format time):*/
		    mam_buf = (char *) calloc(1, 40);
			status = dev->backend->read_attribute(dev->backend_data
											, part
											, TC_MAM_APP_FORMAT_VERSION
											, (unsigned char *)mam_buf
											, 30);

			if (! status) {
				/* The first 5 bytes of MAM buffer contains attribute header so extracting from 5th byte */
				mam_attr->appl_format_ver = (char *) calloc(1, (TC_MAM_APP_FORMAT_VERSION_LEN + 1));

				while (iter < TC_MAM_APP_FORMAT_VERSION_LEN) {
					mam_attr->appl_format_ver[iter] = mam_buf[iter + 5];
					iter++;
				}
				mam_attr->appl_format_ver[iter] = '\0';
				iter = 0;
				free(mam_buf);
				mam_buf = NULL;
			} else {
				ltfsmsg(LTFS_WARN, "17302W", "Application Format Version", TC_MAM_APP_FORMAT_VERSION);
				free(mam_buf);
				mam_buf = NULL;
				ret = status;
			}
		break;

	 case TC_MAM_USR_MED_TXT_LABEL:
		   /* Get User Medium Text Label */
		   mam_buf = (char *)calloc(1,(TC_MAM_USR_MED_TXT_LABEL_LEN+TC_MAM_PAGE_HEADER_SIZE));
	       status = dev->backend->read_attribute(dev->backend_data
										       , part
										       , TC_MAM_USR_MED_TXT_LABEL
										       , (unsigned char *)mam_buf
										       , (TC_MAM_USR_MED_TXT_LABEL_LEN+TC_MAM_PAGE_HEADER_SIZE));

			if (! status) {
				/* The first 5 bytes of MAM buffer contains attribute header so extracting from 5th byte */
				mam_attr->volume_name = (char *) calloc(1, (TC_MAM_USR_MED_TXT_LABEL_LEN + 1));

				iter = (TC_MAM_USR_MED_TXT_LABEL_LEN+TC_MAM_PAGE_HEADER_SIZE-1);

				while ((i + 5) <= iter) {
					if (mam_buf[i + 5] == '\0')
						break;
					else  {
						mam_attr->volume_name[i] = mam_buf[i + 5];
					}
					i++;
				}

				mam_attr->volume_name[i] = '\0';
				iter = 0;
				i = 0;
				free(mam_buf);
				mam_buf = NULL;
			} else {
				ltfsmsg(LTFS_WARN, "17302W", "User Medium Text Label", TC_MAM_USR_MED_TXT_LABEL);
				free(mam_buf);
				mam_buf = NULL;
				ret = status;
				}
		break;

    case TC_MAM_BARCODE:
		/* Get Barcode */
	     mam_buf = (char *)calloc(1,(TC_MAM_BARCODE_LEN+TC_MAM_PAGE_HEADER_SIZE));
	     status = dev->backend->read_attribute(dev->backend_data
										     , part
										     , TC_MAM_BARCODE
										     , (unsigned char *)mam_buf
										     , (TC_MAM_BARCODE_LEN+TC_MAM_PAGE_HEADER_SIZE));

			if (! status) {
				/* The first 5 bytes of MAM buffer contains attribute header so extracting from 5th byte */
				mam_attr->barcode = (char *) calloc(1, TC_MAM_BARCODE_LEN + 1);

				while (iter < TC_MAM_BARCODE_LEN) {
					mam_attr->barcode[iter] = mam_buf[iter + TC_MAM_PAGE_HEADER_SIZE];
					iter++;
				}

				iter = 0;
				free(mam_buf);
				mam_buf = NULL;
			} else {
				ltfsmsg(LTFS_WARN, "17302W", "Barcode", TC_MAM_BARCODE);
				free(mam_buf);
				mam_buf = NULL;
				ret = status;
				}

				return ret;

		break;

    default:
			/* Get Application Vendor: */
			mam_buf = (char *) calloc(1, 40);

			status = dev->backend->read_attribute(dev->backend_data
												, part
												, TC_MAM_APPLICATION_VENDOR
												, (unsigned char *) mam_buf
												, 20);
			if (!status) {
				/* The first 5 bytes of MAM buffer contains attribute header so extracting from 5th byte */
				mam_attr->appl_vendor = (char *) calloc(1,
						(TC_MAM_APPLICATION_VENDOR_LEN + 1));
				while (iter < TC_MAM_APPLICATION_VENDOR_LEN) {
					mam_attr->appl_vendor[iter] = mam_buf[iter + 5];
					iter++;
				}
				mam_attr->appl_vendor[iter] = '\0';
				iter = 0;
				free(mam_buf);
				mam_buf = NULL;
			} else {
				ltfsmsg(LTFS_WARN, "17302W",
						"Application Vendor", TC_MAM_APPLICATION_VENDOR);
				free(mam_buf);
				mam_buf = NULL;
				ret = status;
			}

			/* Get Application Name: */
			mam_buf = (char *) calloc(1, 40);
			status = dev->backend->read_attribute(dev->backend_data
												, part
												, TC_MAM_APPLICATION_NAME
												, (unsigned char *) mam_buf
												, 40);
			if (!status) {
				/* The first 5 bytes of MAM buffer contains attribute header so extracting from 5th byte */
				mam_attr->appl_name = (char *) calloc(1,
						(TC_MAM_APPLICATION_NAME_LEN + 1));

				while (iter < TC_MAM_APPLICATION_NAME_LEN) {
					mam_attr->appl_name[iter] = mam_buf[iter + 5];
					iter++;
				}
				mam_attr->appl_name[iter] = '\0';
				iter = 0;
				free(mam_buf);
				mam_buf = NULL;
			} else {
				ltfsmsg(LTFS_WARN, "17302W",
						"Application Name", TC_MAM_APPLICATION_NAME);
				free(mam_buf);
				mam_buf = NULL;
				ret = status;
			}
			/* Get Application Version: */
			mam_buf = (char *) calloc(1, 40);
			status = dev->backend->read_attribute(dev->backend_data
												, part
												, TC_MAM_APPLICATION_VERSION
												, (unsigned char *) mam_buf
												, 20);
			if (!status) {
				/* The first 5 bytes of MAM buffer contains attribute header so extracting from 5th byte */
				mam_attr->appl_ver = (char *) calloc(1,
						(TC_MAM_APPLICATION_VERSION_LEN + 1));

				while (iter < TC_MAM_APPLICATION_VERSION_LEN) {
					mam_attr->appl_ver[iter] = mam_buf[iter + 5];
					iter++;
				}
				mam_attr->appl_ver[iter] = '\0';
				iter = 0;
				free(mam_buf);
				mam_buf = NULL;
			} else {
				ltfsmsg(LTFS_WARN, "17302W",
						"Application Version", TC_MAM_APPLICATION_VERSION);
				free(mam_buf);
				mam_buf = NULL;
				ret = status;
			}
			/* Get Application Format Version (to the value used in the format label, not the index -
			 * though they should be the same at format time):
			 */
			mam_buf = (char *) calloc(1, 40);
			status = dev->backend->read_attribute(dev->backend_data
												, part
												, TC_MAM_APP_FORMAT_VERSION
												, (unsigned char *) mam_buf
												, 30);

			if (!status) {
				/* The first 5 bytes of MAM buffer contains attribute header so extracting from 5th byte */
				mam_attr->appl_format_ver = (char *) calloc(1,
						(TC_MAM_APP_FORMAT_VERSION_LEN + 1));

				while (iter < TC_MAM_APP_FORMAT_VERSION_LEN) {
					mam_attr->appl_format_ver[iter] = mam_buf[iter + 5];
					iter++;
				}
				mam_attr->appl_format_ver[iter] = '\0';
				iter = 0;
				free(mam_buf);
				mam_buf = NULL;
			} else {
				ltfsmsg(LTFS_WARN, "17302W",
						"Application Format Version", TC_MAM_APP_FORMAT_VERSION);
				free(mam_buf);
				mam_buf = NULL;
				ret = status;
			}

			/* Get User Medium Text Label */
			mam_buf = (char *) calloc(1,
					(TC_MAM_USR_MED_TXT_LABEL_LEN + TC_MAM_PAGE_HEADER_SIZE));
			status = dev->backend->read_attribute(dev->backend_data
												, part
												, TC_MAM_USR_MED_TXT_LABEL
												, (unsigned char *) mam_buf
												, (TC_MAM_USR_MED_TXT_LABEL_LEN + TC_MAM_PAGE_HEADER_SIZE));

			if (!status) {
				/* The first 5 bytes of MAM buffer contains attribute header so extracting from 5th byte */
				mam_attr->volume_name = (char *) calloc(1,
						(TC_MAM_USR_MED_TXT_LABEL_LEN + 1));

				iter = (TC_MAM_USR_MED_TXT_LABEL_LEN + TC_MAM_PAGE_HEADER_SIZE - 1);

				while ((i + 5) <= iter) {
					if (mam_buf[i + 5] == '\0')
						break;
					else {
						mam_attr->volume_name[i] = mam_buf[i + 5];
					}
					i++;
				}

				mam_attr->volume_name[i] = '\0';
				iter = 0;
				i = 0;
				free(mam_buf);
				mam_buf = NULL;
			} else {
				ltfsmsg(LTFS_WARN, "17302W",
						"User Medium Text Label", TC_MAM_USR_MED_TXT_LABEL);
				free(mam_buf);
				mam_buf = NULL;
				ret = status;
			}

			/* Get Barcode */
			mam_buf = (char *) calloc(1,
					(TC_MAM_BARCODE_LEN + TC_MAM_PAGE_HEADER_SIZE));
			status = dev->backend->read_attribute(dev->backend_data
												, part
												, TC_MAM_BARCODE, (unsigned char *) mam_buf
												, (TC_MAM_BARCODE_LEN + TC_MAM_PAGE_HEADER_SIZE));

			if (!status) {
				/* The first 5 bytes of MAM buffer contains attribute header so extracting from 5th byte */
				mam_attr->barcode = (char *) calloc(1, TC_MAM_BARCODE_LEN + 1);

				while (iter < TC_MAM_BARCODE_LEN) {
					mam_attr->barcode[iter] =
							mam_buf[iter + TC_MAM_PAGE_HEADER_SIZE];
					iter++;
				}

				iter = 0;
				free(mam_buf);
				mam_buf = NULL;
			} else {
				ltfsmsg(LTFS_WARN, "17302W", "Barcode", TC_MAM_BARCODE);
				free(mam_buf);
				mam_buf = NULL;
				ret = status;
			}
		}
	return ret;
}
/**
 * Update the MAM attributes.
 * @param device The device whose MAM attributes have to be updated.
 * @param usr_def_vol_name An optional volume name.
 * @return 0 on success, a negative value on error.
 */
int tape_update_mam_attributes(struct device_data *device,
		const char *usr_def_vol_name, unsigned int attribute_id, const char *usr_def_barcode) {
	int status = -1;

	CHECK_ARG_NULL(device, -LTFS_NULL_ARG);

	status = device->backend->update_mam_attr(device->backend_data,
			TC_FORMAT_DEST_PART, usr_def_vol_name, attribute_id, usr_def_barcode);

	return status;
}

/* End of file */
