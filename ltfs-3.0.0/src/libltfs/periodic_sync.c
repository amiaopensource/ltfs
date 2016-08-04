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
** FILE NAME:       periodic_sync.c
**
** DESCRIPTION:     Implements the periodic sync feature
**
** AUTHOR:          Atsushi Abe
**                  IBM Yamato, Japan
**                  PISTE@jp.ibm.com
*************************************************************************************
*/

#include "ltfs.h"
#include "ltfs_fsops.h"

/**
 * Periodic sync scheduler private data structure.
 */
struct periodic_sync_data {
	ltfs_thread_cond_t   periodic_sync_thread_cond;  /**< Used to wake up the periodic sync thread */
	ltfs_thread_mutex_t  periodic_sync_thread_mutex; /**< Used to handle the periodic sync thread */
	ltfs_thread_t        periodic_sync_thread_id;    /**< Thread id of the periodic sync thread */
	bool             keepalive;                  /**< Used to terminate the background thread */
	int              period_sec;                 /**< Period between sync (sec) */
	struct ltfs_volume *vol;                     /**< A reference to the LTFS volume structure */
};

/**
 * Main routine for periodic sync.
 * @param data Periodic sync private data
 * @return NULL.
 */
#define FUSE_REQ_ENTER(r)   REQ_NUMBER(REQ_STAT_ENTER, REQ_FUSE, r)
#define FUSE_REQ_EXIT(r)    REQ_NUMBER(REQ_STAT_EXIT,  REQ_FUSE, r)

#define REQ_SYNC        fffe

ltfs_thread_return periodic_sync_thread(void* data)
{
	struct periodic_sync_data *priv = (struct periodic_sync_data *) data;
#if (defined QUANTUM_BUILD) && (! defined osx_PLATFORM)
    struct timespec now;
 #else
    struct timeval now;
#endif

	int ret;

	ltfs_thread_mutex_lock(&priv->periodic_sync_thread_mutex);
#if (defined QUANTUM_BUILD) && (! defined osx_PLATFORM)
    while (priv->keepalive && clock_gettime(CLOCK_MONOTONIC, &now) == 0) {
#else
	while (priv->keepalive && gettimeofday(&now, NULL) == 0) {
#endif
		ltfs_thread_cond_timedwait(&priv->periodic_sync_thread_cond,
								   &priv->periodic_sync_thread_mutex,
								   priv->period_sec);
		if (! priv->keepalive)
			break;

#if 0
		ltfs_request_trace(FUSE_REQ_ENTER(REQ_SYNC), 0, 0);
#endif /* 0 */

		ltfsmsg(LTFS_DEBUG, "17067D", "Sync-by-Time");
		ret = ltfs_fsops_flush(NULL, false, priv->vol);
		if (ret < 0) {
			/* Failed to flush file data */
			ltfsmsg(LTFS_WARN, "17063W", __FUNCTION__);
		}

		ltfs_sync_index(SYNC_PERIODIC, true, priv->vol);

#if 0
		ltfs_request_trace(FUSE_REQ_EXIT(REQ_SYNC), ret, 0);
#endif /* 0 */
	}
	ltfs_thread_mutex_unlock(&priv->periodic_sync_thread_mutex);

	ltfsmsg(LTFS_DEBUG, "17064D", "Sync-by-Time");
	ltfs_thread_exit();

	return LTFS_THREAD_RC_NULL;
}

/**
 * Verifies if the periodic sync thread is currently running.
 * @param vol LTFS volume
 * @return true if the thread is running, false if not.
 */
bool periodic_sync_thread_initialized(struct ltfs_volume *vol)
{
	struct periodic_sync_data *priv = vol ? vol->periodic_sync_handle : NULL;
	bool initialized = false;

	if (priv) {
		ltfs_thread_mutex_lock(&priv->periodic_sync_thread_mutex);
		initialized = priv->keepalive;
		ltfs_thread_mutex_unlock(&priv->periodic_sync_thread_mutex);
	}

	return initialized;
}

/**
 * Initialize the periodic sync thread.
 * @param sec timer in which the syncing will be performed
 * @param vol LTFS volume
 * @return 0 on success or a negative value on error.
 */
int periodic_sync_thread_init(int sec, struct ltfs_volume *vol)
{
	int ret;
#if (defined QUANTUM_BUILD) && (! defined osx_PLATFORM)
    pthread_condattr_t cond_attr;
#endif
    
	struct periodic_sync_data *priv;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);

	priv = calloc(1, sizeof(struct periodic_sync_data));
	if (! priv) {
		ltfsmsg(LTFS_ERR, "10001E", "periodic_sync_thread_init: periodic sync data");
		return -LTFS_NO_MEMORY;
	}

	priv->vol = vol;
	priv->keepalive = true;
	priv->period_sec = sec;

#if (defined QUANTUM_BUILD) && (! defined osx_PLATFORM)
    pthread_condattr_init( &cond_attr );
    pthread_condattr_setclock( &cond_attr, CLOCK_MONOTONIC ); // unaffected by clock changes
    ret = pthread_cond_init(&priv->periodic_sync_thread_cond, &cond_attr);
    pthread_condattr_destroy( &cond_attr );
#else
    ret = pthread_cond_init(&priv->periodic_sync_thread_cond, NULL);
#endif

	if (ret) {
		ltfsmsg(LTFS_ERR, "10003E", ret);
		free(priv);
		return -ret;
	}
	ret = ltfs_thread_mutex_init(&priv->periodic_sync_thread_mutex);
	if (ret) {
		ltfsmsg(LTFS_ERR, "10002E", ret);
		ltfs_thread_cond_destroy(&priv->periodic_sync_thread_cond);
		free(priv);
		return -ret;
	}
	ret = ltfs_thread_create(&priv->periodic_sync_thread_id, periodic_sync_thread, priv);
	if (ret < 0) {
		/* Failed to spawn the periodic sync thread (%d) */
		ltfsmsg(LTFS_ERR, "17099E", ret);
		ltfs_thread_mutex_destroy(&priv->periodic_sync_thread_mutex);
		ltfs_thread_cond_destroy(&priv->periodic_sync_thread_cond);
		free(priv);
		return -ret;
	}

	ltfsmsg(LTFS_DEBUG, "17065D");
	vol->periodic_sync_handle = priv;

	return 0;
}

/**
 * Destroy the periodic sync thread.
 * @param vol LTFS volume
 * @return 0 on success or a negative value on error.
 */
int periodic_sync_thread_destroy(struct ltfs_volume *vol)
{
	struct periodic_sync_data *priv = vol ? vol->periodic_sync_handle : NULL;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);

	ltfs_thread_mutex_lock(&priv->periodic_sync_thread_mutex);
	priv->keepalive = false;
	ltfs_thread_cond_signal(&priv->periodic_sync_thread_cond);
	ltfs_thread_mutex_unlock(&priv->periodic_sync_thread_mutex);

	ltfs_thread_join(priv->periodic_sync_thread_id);
	ltfs_thread_cond_destroy(&priv->periodic_sync_thread_cond);
	ltfs_thread_mutex_destroy(&priv->periodic_sync_thread_mutex);
	free(priv);

	vol->periodic_sync_handle = NULL;

	ltfsmsg(LTFS_DEBUG, "17066D");
	return 0;
}

/**
 * We use this function to signal the periodic sync thread to wake up and sync an index.
 * @param syncer_handle Of type 'periodic_sync_data' is the handle for the periodic sync thread.
 * @return int 0 on success, -LTFS_NULL_ARG if the handle passed is invalid.
 */
int periodic_sync_thread_signal(void *syncer_handle)
{
	int ret = 0;
	struct periodic_sync_data *priv = (struct periodic_sync_data *) syncer_handle;

	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);

	pthread_mutex_lock(&priv->periodic_sync_thread_mutex);
	pthread_cond_signal(&priv->periodic_sync_thread_cond);
	pthread_mutex_unlock(&priv->periodic_sync_thread_mutex);

	return ret;
}

/* End of file */
