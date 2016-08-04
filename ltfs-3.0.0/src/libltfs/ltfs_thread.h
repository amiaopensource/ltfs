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
** FILE NAME:       ltfs_thread.h
**
** DESCRIPTION:     LTFS thread operation imprementation
**
** AUTHORS:         Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
*************************************************************************************
*/

#ifndef __LTFS_THREAD_H__
#define __LTFS_THREAD_H__

#ifdef __cplusplus
extern "C" {
#endif

/* HP: We don't have access to this file. */
#if !defined(HP_mingw_BUILD) && defined(mingw_PLATFORM)
#include "arch/win/win_thread.h"
#else

#include <pthread.h>
#include <sys/time.h>
#ifndef HP_mingw_BUILD
#include <sys/syscall.h>
#endif /* HP_mingw_BUILD */
#include <unistd.h>

typedef pthread_t          ltfs_thread_t;
typedef pthread_mutex_t    ltfs_thread_mutex_t;
typedef pthread_attr_t     ltfs_thread_attr_t;
typedef pthread_cond_t     ltfs_thread_cond_t;
typedef pthread_condattr_t ltfs_thread_condattr_t;
typedef void               *ltfs_thread_return;
typedef void               *ltfs_thread_return_detached;

#define LTFS_THREAD_RC_NULL    (NULL)

enum {
	LTFS_THREAD_CREATE_DETACHED = PTHREAD_CREATE_DETACHED,
	LTFS_THREAD_CREATE_JOINABLE = PTHREAD_CREATE_JOINABLE
};

static inline int ltfs_thread_mutex_init(ltfs_thread_mutex_t *mutex)
{
	return pthread_mutex_init(mutex, NULL);
}

static inline int ltfs_thread_mutex_destroy(ltfs_thread_mutex_t *mutex)
{
	return pthread_mutex_destroy(mutex);
}

static inline int ltfs_thread_mutex_lock(ltfs_thread_mutex_t *mutex)
{
	return pthread_mutex_lock(mutex);
}

static inline int ltfs_thread_mutex_unlock(ltfs_thread_mutex_t *mutex)
{
	return pthread_mutex_unlock(mutex);
}

static inline int ltfs_thread_mutex_trylock(ltfs_thread_mutex_t *mutex)
{
	return pthread_mutex_trylock(mutex);
}

static inline int ltfs_thread_attr_init(ltfs_thread_attr_t *attr)
{
	return pthread_attr_init(attr);
}

static inline int ltfs_thread_attr_destroy(ltfs_thread_attr_t *attr)
{
	return pthread_attr_destroy(attr);
}

static inline int ltfs_thread_attr_setdetachstate(ltfs_thread_attr_t *attr, int detachstate)
{
	return pthread_attr_setdetachstate(attr, detachstate);
}

static inline int ltfs_thread_cond_broadcast(ltfs_thread_cond_t *cond)
{
	return pthread_cond_broadcast(cond);
}

static inline int ltfs_thread_cond_signal(ltfs_thread_cond_t *cond)
{
	return pthread_cond_signal(cond);
}

static inline int ltfs_thread_cond_destroy(ltfs_thread_cond_t *cond)
{
	return pthread_cond_destroy(cond);
}

	static inline int ltfs_thread_cond_init(ltfs_thread_cond_t *restrict cond)
{
	return pthread_cond_init(cond, NULL);
}

static inline int ltfs_thread_cond_timedwait(ltfs_thread_cond_t *restrict cond,
											 ltfs_thread_mutex_t *restrict mutex,
											 const int sec)
{
	struct timeval now;
	struct timespec timeout;

	gettimeofday(&now, NULL);
	timeout.tv_sec = now.tv_sec + sec;
	timeout.tv_nsec = 0;

	return pthread_cond_timedwait(cond, mutex, &timeout);
}

static inline int ltfs_thread_cond_wait(ltfs_thread_cond_t *restrict cond,
										ltfs_thread_mutex_t *restrict mutex)
{
	return pthread_cond_wait(cond, mutex);
}

static inline int ltfs_thread_create(ltfs_thread_t *thread,
									 ltfs_thread_return (*start_routine) (void *),
									 void *arg)
{
	return pthread_create(thread, NULL, start_routine, arg);
}

static inline int ltfs_thread_create_detached(ltfs_thread_t *thread,
									 const ltfs_thread_attr_t *attr,
									 ltfs_thread_return_detached (*start_routine) (void *),
									 void *arg)
{
	return pthread_create(thread, attr, start_routine, arg);
}

static inline void ltfs_thread_exit(void)
{
	return pthread_exit(NULL);
}

static inline void ltfs_thread_exit_detached(void)
{
	return pthread_exit(NULL);
}

static inline int ltfs_thread_join(ltfs_thread_t thread)
{
	return pthread_join(thread, NULL);
}

static inline ltfs_thread_t ltfs_thread_self(void)
{
	return pthread_self();
}

#ifdef __APPLE__
extern uint32_t ltfs_get_thread_id(void);
#elif defined(HP_mingw_BUILD)
static inline void* ltfs_get_thread_id(void)
{
	return (pthread_self().p);
}
#else
static inline uint32_t ltfs_get_thread_id(void)
{
	uint32_t tid;

	tid = (uint32_t)syscall(SYS_gettid);

	return tid;
}
#endif

#endif

#ifdef __cplusplus
}
#endif

#endif /* __LTFS_THREAD_H__ */
