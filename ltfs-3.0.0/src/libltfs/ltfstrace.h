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
*/

/*************************************************************************************
 ** FILE NAME:       ltfstrace.h
 **
 ** DESCRIPTION:     Definitions for LTFS trace
 **
 ** AUTHORS:         Atsushi Abe
 **                  IBM Tokyo Lab., Japan
 **                  piste@jp.ibm.com
 **
 *************************************************************************************
 */

#ifndef __LTFSTRACE_H__
#define __LTFSTRACE_H__

#ifdef __cplusplus
extern "C" {
#endif

#ifdef mingw_PLATFORM
#include "arch/win/win_util.h"
#define PROFILER_FILE_MODE "wb+"
#else
#define PROFILER_FILE_MODE "w+"
#include <sys/wait.h>
#endif

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <errno.h>
#include <stdint.h>

#include "libltfs/ltfslogging.h"
#include "libltfs/ltfs_thread.h"
#include "libltfs/ltfs_locking.h"
#include "libltfs/ltfs_error.h"
#include "libltfs/uthash.h"
#include "libltfs/arch/time_internal.h"

typedef enum {
	FILESYSTEM = 0,		/* Filesystem function trace */
	ADMIN = 1,		/* Admin function trace */
	ADMIN_COMPLETED = 2	/* Completed Admin function trace */
} FUNCTION_TRACE_TYPE;

/*
 *  Definitions for LTFS trace
 */
int  ltfs_trace_init(void);
int ltfs_trace_get_offset(char** val);
void ltfs_trace_destroy(void);
void ltfs_trace_set_work_dir(const char *dir);
int  ltfs_trace_dump(char *fname);
int ltfs_get_trace_status(char **val);
int ltfs_set_trace_status(char *mode);
int ltfs_dump(char *fname);
int ltfs_fn_trace_start(FUNCTION_TRACE_TYPE, uint32_t);
void ltfs_admin_function_trace_completed(uint32_t);

/*
 *  Definitions for LTFS request trace
 */

/*
 *  Reqest number definitions for LTFS request trace
 *
 *  Request value -> 0xASSSTTTT
 *    A: Status (enter, exit or others)
 *    S: Source (FUSE, admin channel or others)
 *    T: Request Type (defined by each sources)
 */
#define REQ_NUMBER(status, source, type) _REQ_NUMBER(status, source, type)
#define _REQ_NUMBER(status, source, type) (uint32_t)(0x##status##source##type)

#define REQ_STATUS_MASK (0xF0000000)
#define REQ_SOURCE_MASK (0x0FFF0000)
#define REQ_TYPE_MASK   (0x0000FFFF)

/* Value of request trace */
#define REQ_STAT_ENTER 0
#define REQ_STAT_EVENT 1
#define REQ_STAT_EXIT  8

/*  Request sources
 *    000:       FUSE
 *    001 - 00F: RESERVED
 *    010:       ADMINCHANNEL
 *    111:       IO Scheduler
 *    222:       Tape Backend
 *    333:       Changer Backend
 *    334 - FFF: RESERVED
 */
#define REQ_FUSE 000
#define REQ_ADM  010
#define REQ_IOS  111
#define REQ_DRV  222
#define REQ_CHG  333

void ltfs_request_trace(uint32_t req_num, uint64_t info1, uint64_t info2);
void ltfs_profiler_add_entry(FILE* file, ltfs_mutex_t *mutex, uint32_t req_num);

/*
 *  Definitions for LTFS function trace
 */

/*
 *  Definitions for LTFS trace file
 */
int ltfs_dump_trace(char* name);

/*
 *  Definitions for LTFS profiler
 */
#define PROF_REQ       (0x0000000000000001)
#define PROF_IOSCHED   (0x0000000000000002)
#define PROF_DRIVER    (0x0000000000000004)
#define PROF_CHANGER   (0x0000000000000008)

int ltfs_profiler_set(uint64_t source);

#define REQ_PROFILER_FILE        "prof_request.dat"
#define IOSCHED_PROFILER_FILE    "prof_iosched.dat"
#define DRIVER_PROFILER_FILE     "prof_driver.dat"

#define IOSCHED_REQ_ENTER(r)   REQ_NUMBER(REQ_STAT_ENTER, REQ_IOS, r)
#define IOSCHED_REQ_EXIT(r)    REQ_NUMBER(REQ_STAT_EXIT,  REQ_IOS, r)
#define IOSCHED_REQ_EVENT(r)   REQ_NUMBER(REQ_STAT_EVENT,  REQ_IOS, r)
extern FILE* ios_profiler;              /**< Profiler file pointer for IO scheduler */
extern ltfs_mutex_t ios_profiler_lock;  /**< lock file for Profiler file access */

#ifndef TAPEBEND_REQ_ENTER
#define TAPEBEND_REQ_ENTER(r)    REQ_NUMBER(REQ_STAT_ENTER, REQ_DRV, r)
#define TAPEBEND_REQ_EXIT(r)     REQ_NUMBER(REQ_STAT_EXIT,  REQ_DRV, r)
#endif
extern FILE* bend_profiler;             /**< Profiler file pointer for Backend driver */
extern ltfs_mutex_t bend_profiler_lock; /**< lock file for Profiler file access */

#define CHANGER_REQ_ENTER(r)    REQ_NUMBER(REQ_STAT_ENTER, REQ_CHG, r)
#define CHANGER_REQ_EXIT(r)     REQ_NUMBER(REQ_STAT_EXIT,  REQ_CHG, r)

#ifdef __cplusplus
}
#endif

#endif /* __LTFSTRACE_H__ */
