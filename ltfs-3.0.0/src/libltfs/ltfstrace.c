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
 ** FILE NAME:       ltfstrace.c
 **
 ** DESCRIPTION:     Routines for LTFS trace
 **
 ** AUTHORS:         Atsushi Abe
 **                  IBM Tokyo Lab., Japan
 **                  piste@jp.ibm.com
 **
 *************************************************************************************
 */

#include "libltfs/ltfstrace.h"
#include "libltfs/queue.h"

/*************************************************************************************
 * TRACE FILE STRUCTURE
 *  +==========================+
 *  |H      Trace Header       |
 *  +==========================+
 *  |H   Request Trace Header  |
 *  +--------------------------+------------------------------------------------------
 *  |                          | Request Trace
 *  |    Request Trace (Body)  | - All threads access to one trace structure
 *  |                          | - Store request entry and exit
 *  +==========================+======================================================
 *  |H  Function Trace Header  |
 *  +--------------------------+------------------------------------------------------
 *  |                          | FS (filesystem) Function Trace
 *  |   FS Function Trace #1   | - Create a trace structure on each thread
 *  |                  (Body)  | - Use each trace area with round robin strategy
 *  +--------------------------+ - Store function trace
 *  |           ...            |
 *  +--------------------------+
 *  |                          |
 *  |   FS Function Trace #n   |
 *  |                  (Body)  |
 *  +--------------------------+------------------------------------------------------
 *  |                          | Admin Function Trace
 *  |  Admin Function Trace #1 | - Create a trace structure on each thread (each request)
 *  |                  (Body)  | - Use each trace area with round robin strategy
 *  +--------------------------+ - Trace structure is freed with LRU
 *  |           ...            | - Store function trace
 *  +--------------------------+
 *  |                          |
 *  |  Admin Function Trace #n |
 *  |                  (Body)  |
 *  +--------------------------+------------------------------------------------------
 *  |                          | Completed Admin Function Trace
 *  |   Admin Fn Comp Trace #1 | - Create a trace structure on each thread (each request)
 *  |                  (Body)  | - Use each trace area with round robin strategy
 *  +--------------------------+ - Trace structure is freed with LRU
 *  |           ...            | - Store function trace
 *  +--------------------------+
 *  |                          |
 *  |   Admin Fn Comp Trace #n |
 *  |                  (Body)  |
 *  +==========================+------------------------------------------------------
 **************************************************************************************/

/*
 * Definition for LTFS trace header information
 */
#define LTFS_TRACE_SIGNATURE "LTFS_TRC"
#pragma pack(1)
struct trace_header {
	char signature[8];               /**< Signature for LTFS trace */
	uint32_t header_size;            /**< Size of trace header */
	uint32_t req_header_offset;      /**< Request trace header offset */
	uint32_t fn_header_offset;       /**< Function trace header offset */
	unsigned short endian_signature; /**< Endian signagure : 0x1234 or 0x3412 */
	uint32_t trace_size;             /**< Whole size of trace (all headers and bodies) */
	uint32_t crc;                    /**< CRC (reserved for future use) */
};
#pragma pack(0)

/*
 * Definitions for LTFS Request header information
 */
#pragma pack(1)
struct request_header {
	uint32_t header_size;            /**< Size of request header */
	uint32_t num_of_req_trace;       /**< Number of request trace descriptrs (always 1) */
	struct request_trace_descriptor {
		uint32_t  size_of_entry; /**< Size of entry */
		uint32_t  num_of_entry;  /**< Number of entry */
	} req_t_desc;                    /**< Request header descriptor */
	uint32_t crc;                    /**< CRC (reserved for future use) */
};
#pragma pack(0)

/*
 * Definitions for LTFS function trace header
 */
#pragma pack(1)
struct function_trace_header {
	uint32_t header_size;		/**< Size of function trace header */
	uint32_t num_of_fn_trace;	/**< Number of function trace */
	struct function_trace_descriptor {
		uint32_t type;		/**< Function trace type (admin or filesystem) */
		uint32_t size_of_entry;	/**< Size of function trace entry */
		uint32_t num_of_entry;	/**< Number of function trace entries */
	} *req_t_desc;
	uint32_t crc;                    /**< CRC (reserved for future use) */
};
#pragma pack(0)

/*
 *  Definitions for LTFS request trace
 */
#pragma pack(1)
struct request_entry {
	uint64_t time;
	uint32_t req_num;
	uint32_t tid;
	uint64_t info1;
	uint64_t info2;
};
#pragma pack(0)

#define REQ_TRACE_ENTRY_SIZE (sizeof(struct request_entry))
#define REQ_TRACE_SIZE       (4 * 1024 * 1024) /* 4MB */
#define REQ_TRACE_ENTRIES    (REQ_TRACE_SIZE / REQ_TRACE_ENTRY_SIZE)

struct request_trace {
	ltfs_mutex_t         req_trace_lock;
	uint32_t             max_index;
	uint32_t             cur_index;
	FILE*                profiler;
	struct request_entry entries[REQ_TRACE_ENTRIES];
};

/*
 * Definitions for LTFS function trace data
 */
#pragma pack(1)
struct function_entry {
	uint64_t time;
	uint64_t function;
	uint64_t info1;
	uint64_t info2;
};
#pragma pack(0)

#define FN_TRACE_ENTRY_SIZE (sizeof(struct function_entry))

/*
 * "Filesystem" Function Trace Data structure
 */
#define FS_FN_TRACE_SIZE       (1 * 1024 * 1024) /* 1MB */
#define FS_FN_TRACE_ENTRIES    (FS_FN_TRACE_SIZE / FN_TRACE_ENTRY_SIZE)

struct filesystem_function_trace {
	struct MultiReaderSingleWriter trace_lock;      /**< Lock for trace data */
	uint32_t                       max_index;
	uint32_t                       cur_index;
	struct function_entry          entries[FS_FN_TRACE_ENTRIES];
};

struct filesystem_trace_list {
	uint32_t                          tid;
	struct filesystem_function_trace *fn_entry;
	UT_hash_handle                    hh;
};

/*
 * "Admin" Function Trace Data structure
 */
#define ADMIN_FN_TRACE_ENTRIES	256
#define ADMIN_FN_TRACE_SIZE	(ADMIN_FN_TRACE_ENTRIES * FN_TRACE_ENTRY_SIZE)
struct admin_function_trace {
	struct MultiReaderSingleWriter trace_lock;      /**< Lock for trace data */
	uint32_t                       max_index;
	uint32_t                       cur_index;
	struct function_entry          entries[ADMIN_FN_TRACE_ENTRIES];
};

struct admin_trace_list {
	uint32_t                       tid;
	struct admin_function_trace    *fn_entry;
	UT_hash_handle                 hh;
};

/*
 * Definitions for Tail Q of Admin function trace
 */
#define MAX_ADMIN_COMP_NUM 512
struct admin_completed_function_trace {
	TAILQ_ENTRY(admin_completed_function_trace) list;
	uint32_t                       tid;
	struct admin_function_trace    *fn_entry;
	struct MultiReaderSingleWriter trace_lock;
};

/*
 *  Definitions for LTFS Profiler
 */
#pragma pack(1)
struct profiler_entry {
	uint64_t time;
	uint32_t req_num;
	uint32_t tid;
};
#pragma pack(0)

#define PROF_ENTRY_SIZE      (sizeof(struct profiler_entry))
#define REQ_PROF_ENTRY_SIZE  PROF_ENTRY_SIZE /* Don't record information fields in profler data */

struct trace_header           *trc_header    = NULL;
struct request_header         *req_header    = NULL;
struct function_trace_header  *fn_trc_header = NULL;

struct request_trace          *req_trace     = NULL;
struct filesystem_trace_list  *fs_tr_list    = NULL;
struct admin_trace_list       *admin_tr_list = NULL;
TAILQ_HEAD(admin_completed, admin_completed_function_trace);
struct admin_completed        *acomp         = NULL;

_time_stamp_t                 start_offset;
struct ltfs_timespec          start;
struct timer_info             timerinfo;
char                          *work_dir      = NULL;
bool                          trace_enable   = true;

FILE* ios_profiler;              /**< Profiler file pointer for IO scheduler */
ltfs_mutex_t ios_profiler_lock;  /**< lock file for Profiler file access */
FILE* bend_profiler;             /**< Profiler file pointer for Backend driver */
ltfs_mutex_t bend_profiler_lock; /**< lock file for Profiler file access */

static int ltfs_request_trace_init(void)
{
	int ret = 0;

	req_trace = calloc(1, sizeof(struct request_trace));
	if (!req_trace) {
		ltfsmsg(LTFS_ERR, "10001E", __FUNCTION__);
		return -LTFS_NO_MEMORY;
	}

	ret = ltfs_mutex_init(&req_trace->req_trace_lock);
	if (ret) {
		ltfsmsg(LTFS_ERR, "10002E", ret);
		free(req_trace);
		return -LTFS_MUTEX_INIT;
	}

	req_trace->max_index = REQ_TRACE_ENTRIES - 1;

	return 0;
}

static void ltfs_request_trace_destroy(void)
{
	if (req_trace) {
		ltfs_thread_mutex_destroy(&req_trace->req_trace_lock);
		free(req_trace);
		req_trace = NULL;
	}
}

void ltfs_request_trace(uint32_t req_num, uint64_t info1, uint64_t info2)
{
	if (trace_enable == false)
		return;

	if (req_trace) {
		ltfs_mutex_lock(&req_trace->req_trace_lock);

		req_trace->entries[req_trace->cur_index].time = get_time_stamp(&start_offset);
		req_trace->entries[req_trace->cur_index].tid = ltfs_get_thread_id();
		req_trace->entries[req_trace->cur_index].req_num = req_num;
		req_trace->entries[req_trace->cur_index].info1 = info1;
		req_trace->entries[req_trace->cur_index].info2 = info2;

		if (req_trace->profiler)
			fwrite((void*)&req_trace->entries[req_trace->cur_index], REQ_PROF_ENTRY_SIZE, 1, req_trace->profiler);

		if(req_trace->cur_index >= req_trace->max_index)
			req_trace->cur_index = 0;
		else
			req_trace->cur_index++;

		ltfs_mutex_unlock(&req_trace->req_trace_lock);
	}
}

static int ltfs_fn_trace_init(void)
{
	acomp = (struct admin_completed *) calloc (1, sizeof(struct admin_completed));
	TAILQ_INIT(acomp);
	return 0;
}

int ltfs_fn_trace_start(FUNCTION_TRACE_TYPE type, uint32_t tid)
{
	if (trace_enable == false)
		return 0;

	if (type == FILESYSTEM) {
		struct filesystem_trace_list *item = NULL;
		struct filesystem_function_trace *tr_data = NULL;
		item = (struct filesystem_trace_list *) calloc(1, sizeof(struct filesystem_trace_list));
		if (!item) {
			ltfsmsg(LTFS_ERR, "10001E", __FUNCTION__);
			return -LTFS_NO_MEMORY;
		}
		item->tid = tid;

		tr_data = (struct filesystem_function_trace *) calloc(1, sizeof(struct filesystem_function_trace));
		if (!tr_data) {
			ltfsmsg(LTFS_ERR, "10001E", __FUNCTION__);
			return -LTFS_NO_MEMORY;
		}
		tr_data->max_index = FS_FN_TRACE_ENTRIES - 1;
		tr_data->cur_index = 0;
		item->fn_entry = tr_data;
		HASH_ADD_INT(fs_tr_list, tid, item);

	} else if (type == ADMIN) {
		struct admin_trace_list *item = NULL;
		struct admin_function_trace *tr_data = NULL;
		item = (struct admin_trace_list *) calloc(1, sizeof(struct admin_trace_list));
		if (!item) {
			ltfsmsg(LTFS_ERR, "10001E", __FUNCTION__);
			return -LTFS_NO_MEMORY;
		}
		item->tid = tid;

		tr_data = (struct admin_function_trace *) calloc(1, sizeof(struct admin_function_trace));
		if (!tr_data) {
			ltfsmsg(LTFS_ERR, "10001E", __FUNCTION__);
			return -LTFS_NO_MEMORY;
		}
		tr_data->max_index = ADMIN_FN_TRACE_ENTRIES - 1;
		tr_data->cur_index = 0;
		item->fn_entry = tr_data;
		HASH_ADD_INT(admin_tr_list, tid, item);
	}
	return 0;
}

void ltfs_admin_function_trace_completed(uint32_t tid)
{
	struct admin_trace_list *item;
	struct admin_completed_function_trace *tailq_item;
	uint32_t num_of_comp_adm = 0;

	if (trace_enable == false)
		return;

	HASH_FIND_INT(admin_tr_list, &tid, item);
	if (item != NULL) {
		TAILQ_FOREACH (tailq_item, acomp, list)
			num_of_comp_adm++;

		if (num_of_comp_adm > MAX_ADMIN_COMP_NUM) {
			/* Remove first tailq entry */
			tailq_item = TAILQ_FIRST(acomp);
			TAILQ_REMOVE(acomp, tailq_item, list);
			free(tailq_item->fn_entry);
			free(tailq_item);
		}
		tailq_item = (struct admin_completed_function_trace *) calloc(1, sizeof(struct admin_completed_function_trace));

		acquirewrite_mrsw(&tailq_item->trace_lock);
		struct admin_function_trace *ptr = NULL;
		ptr = (struct admin_function_trace *) calloc(1, sizeof(struct admin_function_trace));
		ptr->cur_index = item->fn_entry->cur_index;
		for (unsigned int j=0; j<ptr->cur_index; j++) {
			ptr->entries[j].time = item->fn_entry->entries[j].time;
			ptr->entries[j].function = item->fn_entry->entries[j].function;
			ptr->entries[j].info1 = item->fn_entry->entries[j].info1;
			ptr->entries[j].info2 = item->fn_entry->entries[j].info2;
		}
		tailq_item->fn_entry = ptr;
		tailq_item->tid = tid;
		TAILQ_INSERT_TAIL(acomp, tailq_item, list);
		releasewrite_mrsw(&tailq_item->trace_lock);

		HASH_DEL(admin_tr_list, item);
		free(item->fn_entry);
		free(item);
	}
}

static void ltfs_function_trace_destroy(void)
{
	if (fs_tr_list) {
		struct filesystem_trace_list *fsitem;
		for (fsitem=fs_tr_list; fsitem != NULL; fsitem=fsitem->hh.next) {
			destroy_mrsw(&fsitem->fn_entry->trace_lock);
			free(fsitem->fn_entry);
			free(fsitem);
		}
		fs_tr_list = NULL;
	}
	if (admin_tr_list) {
		struct admin_trace_list *aditem;
		for (aditem=admin_tr_list; aditem != NULL; aditem=aditem->hh.next) {
			destroy_mrsw(&aditem->fn_entry->trace_lock);
			free(aditem->fn_entry);
			free(aditem);
		}
		admin_tr_list = NULL;
	}
	if (acomp) {
		struct admin_completed_function_trace *tailq_item;
		TAILQ_FOREACH (tailq_item, acomp, list) {
			destroy_mrsw(&tailq_item->trace_lock);
			free(tailq_item->fn_entry);
			free(tailq_item);
		}
		free(acomp);
		acomp = NULL;
	}
}

void ltfs_function_trace(uint64_t func, uint64_t info1, uint64_t info2)
{
	struct admin_trace_list *item;
	uint32_t tid;
	uint64_t time;

	if (trace_enable == false)
		return;

	time = get_time_stamp(&start_offset);
	tid = ltfs_get_thread_id();
	HASH_FIND_INT(admin_tr_list, &tid, item);
	if (item != NULL) {
		acquirewrite_mrsw(&item->fn_entry->trace_lock);
		item->fn_entry->entries[item->fn_entry->cur_index].time = time;
		item->fn_entry->entries[item->fn_entry->cur_index].function = func;
		item->fn_entry->entries[item->fn_entry->cur_index].info1 = info1;
		item->fn_entry->entries[item->fn_entry->cur_index].info2 = info2;
		if (item->fn_entry->cur_index >= item->fn_entry->max_index)
			item->fn_entry->cur_index = 0;
		else
			item->fn_entry->cur_index++;
		releasewrite_mrsw(&item->fn_entry->trace_lock);
	} else {
		struct filesystem_trace_list *item;
		HASH_FIND_INT(fs_tr_list, &tid, item);
		if (item != NULL) {
			acquirewrite_mrsw(&item->fn_entry->trace_lock);
			item->fn_entry->entries[item->fn_entry->cur_index].time = time;
			item->fn_entry->entries[item->fn_entry->cur_index].function = func;
			item->fn_entry->entries[item->fn_entry->cur_index].info1 = info1;
			item->fn_entry->entries[item->fn_entry->cur_index].info2 = info2;

			if (item->fn_entry->cur_index >= item->fn_entry->max_index)
				item->fn_entry->cur_index = 0;
			else
				item->fn_entry->cur_index++;

			releasewrite_mrsw(&item->fn_entry->trace_lock);
		} else {
			ltfs_fn_trace_start(FILESYSTEM, tid);
		}
	}
}

int ltfs_request_profiler_start(char *worK_dir)
{
	int ret;
	char *path;

	if (req_trace->profiler)
		return 0;

	if(!work_dir)
		return -LTFS_BAD_ARG;

	ret = asprintf(&path, "%s/%s", work_dir, REQ_PROFILER_FILE);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "10001E", __FILE__);
		return -LTFS_NO_MEMORY;
	}

	req_trace->profiler = fopen(path, PROFILER_FILE_MODE);

	free(path);

	if (! req_trace->profiler)
		ret = -LTFS_FILE_ERR;
	else {
		fwrite((void*)&timerinfo, sizeof(timerinfo), 1, req_trace->profiler);
		ret = 0;
	}

	return ret;
}

int ltfs_request_profiler_stop(void)
{
	if (req_trace->profiler) {
		fclose(req_trace->profiler);
		req_trace->profiler = NULL;
	}

	return 0;
}

int iosched_profiler_start(char *worK_dir)
{
	int ret;
	char *path;

	if (ios_profiler)
		return 0;

	if(!work_dir)
		return -LTFS_BAD_ARG;

	ret = asprintf(&path, "%s/%s", work_dir, IOSCHED_PROFILER_FILE);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "10001E", __FILE__);
		return -LTFS_NO_MEMORY;
	}

	ios_profiler = fopen(path, PROFILER_FILE_MODE);

	free(path);

	if (! ios_profiler)
		ret = -LTFS_FILE_ERR;
	else {
		fwrite((void*)&timerinfo, sizeof(timerinfo), 1, ios_profiler);
		ret = 0;
	}

	return ret;
}

int iosched_profiler_stop(void)
{
	if (ios_profiler) {
		fclose(ios_profiler);
		ios_profiler = NULL;
	}

	return 0;
}

int tape_profiler_start(char *worK_dir)
{
	int ret;
	char *path;

	if (bend_profiler)
		return 0;

	if(!work_dir)
		return -LTFS_BAD_ARG;

	ret = asprintf(&path, "%s/%s", work_dir, DRIVER_PROFILER_FILE);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "10001E", __FILE__);
		return -LTFS_NO_MEMORY;
	}

	bend_profiler = fopen(path, PROFILER_FILE_MODE);

	free(path);

	if (! bend_profiler)
		ret = -LTFS_FILE_ERR;
	else {
		fwrite((void*)&timerinfo, sizeof(timerinfo), 1, bend_profiler);
		ret = 0;
	}

	return ret;
}

int tape_profiler_stop(void)
{
	if (bend_profiler) {
		fclose(bend_profiler);
		bend_profiler = NULL;
	}

	return 0;
}

int ltfs_header_init(void)
{
	/* Trace header */
	trc_header = calloc(1, sizeof(struct trace_header));
	if (!trc_header) {
		ltfsmsg(LTFS_ERR, "10001E", __FUNCTION__);
		return -LTFS_NO_MEMORY;
	}
	strncpy(trc_header->signature, LTFS_TRACE_SIGNATURE, strlen(LTFS_TRACE_SIGNATURE));
	trc_header->header_size = sizeof(struct trace_header);
	trc_header->req_header_offset = sizeof(struct trace_header);
	trc_header->fn_header_offset = sizeof(struct trace_header) + sizeof(struct request_header) + REQ_TRACE_SIZE;
	trc_header->endian_signature = 0x1234;
	trc_header->crc = 0xFACEFEED;

	/* Request trace header */
	req_header = calloc(1, sizeof(struct request_header));
	if (!trc_header) {
		ltfsmsg(LTFS_ERR, "10001E", __FUNCTION__);
		return -LTFS_NO_MEMORY;
	}
	req_header->header_size = sizeof(struct request_header);
	req_header->num_of_req_trace = 1;
	req_header->crc = 0xCAFEBABE;

	/* Function trace header */
	fn_trc_header = calloc(1, sizeof(struct function_trace_header));
	if (!fn_trc_header) {
		ltfsmsg(LTFS_ERR, "10001E", __FUNCTION__);
		return -LTFS_NO_MEMORY;
	}
	fn_trc_header->crc = 0xDEADBEEF;
	return 0;
}

int ltfs_trace_init(void)
{
	int ret = 0;

	if (trace_enable == false)
		return ret;

	/* Store launch time */
	get_current_timespec(&start);
	__get_time(&start_offset);

	/* Get timer info (architecture dependent) */
	get_timer_info(&timerinfo);

	/* Initialize trace header */
	ret = ltfs_header_init();

	/* Initalize trace structures */
	ret = ltfs_request_trace_init();

	/* Initialize function trace structures */
	ret = ltfs_fn_trace_init();

	return ret;
}

int ltfs_trace_get_offset(char** val)
{
#ifdef __APPLE__
	return asprintf(val, "%llu", start_offset);
#elif defined(mingw_PLATFORM)
	return asprintf(val, "%llu", start_offset);
#else
	return asprintf(val, "%lu.%09lu", start_offset.tv_sec, start_offset.tv_nsec);
#endif
}

void ltfs_trace_destroy(void)
{
	/* Destroy trace structures */
	ltfs_request_trace_destroy();

	/* Destroy function trace structures */
	ltfs_function_trace_destroy();

	free(trc_header);
	trc_header    = NULL;

	free(req_header);
	req_header    = NULL;

	free(fn_trc_header);
	fn_trc_header = NULL;
}

void ltfs_trace_set_work_dir(const char *dir)
{
	work_dir = (char *)dir;
}

int ltfs_dump(char *fname)
{
#ifndef mingw_PLATFORM
	int ret = 0, num_args = 0, status;
	char *path, *pid;
	pid_t fork_pid;
	const unsigned int max_arguments = 32;
	const char *args[max_arguments];

	if(!work_dir)
		return -LTFS_BAD_ARG;

	ret = asprintf(&path, "%s/%s", work_dir, fname);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "10001E", __FILE__);
		return -LTFS_NO_MEMORY;
	}

	ret = asprintf(&pid, "%ld", (long)getpid());
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "10001E", __FILE__);
		return -LTFS_NO_MEMORY;
	}

	fork_pid = fork();
	if (fork_pid < 0) {
		ltfsmsg(LTFS_ERR, "17233E");
	} else  if (fork_pid == 0) {
		args[num_args++] = "/usr/bin/gcore";
		args[num_args++] = "-o";
		args[num_args++] = path;
		args[num_args++] = pid;
		args[num_args++] = NULL;

		execv(args[0], (char **) args);
		exit(errno);
	} else {
		waitpid(fork_pid, &status, 0);
		ret = WEXITSTATUS(status);
	}
#endif
	return 0;
}

int ltfs_trace_dump(char *fname)
{
	int ret = 0, fd;
	char *path;
	size_t written;

	if(trace_enable == false)
		return 0;

	if(!work_dir)
		return -LTFS_BAD_ARG;

	ret = asprintf(&path, "%s/%s", work_dir, fname);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "10001E", __FILE__);
		return -LTFS_NO_MEMORY;
	}

	/* Open file */
	fd = open(path, O_WRONLY|O_CREAT|O_TRUNC, 0666);
	if(fd < 0)
		return -errno;

	free(path);

	if (req_trace)
	{
		uint32_t num_of_fn_trace = 0, num_of_fs_fn_trace = 0, num_of_adm_fn_trace = 0, n = 0;
		struct admin_completed_function_trace *tailq_item;
		struct filesystem_trace_list *fsitem;
		struct admin_trace_list *admitem;

		/* Calculate the number of function traces */
		num_of_fs_fn_trace += HASH_COUNT(fs_tr_list);
		num_of_adm_fn_trace += HASH_COUNT(admin_tr_list);
		TAILQ_FOREACH (tailq_item, acomp, list)
			num_of_adm_fn_trace++;
		num_of_fn_trace = num_of_fs_fn_trace + num_of_adm_fn_trace;

		fn_trc_header->num_of_fn_trace = num_of_fn_trace;
		fn_trc_header->header_size = 8 + 12 * num_of_fn_trace;

		fn_trc_header->req_t_desc =
			(struct function_trace_descriptor *) calloc(num_of_fn_trace, sizeof(struct function_trace_descriptor));
		if (!fn_trc_header->req_t_desc) {
			ltfsmsg(LTFS_ERR, "10001E", __FUNCTION__);
			return -LTFS_NO_MEMORY;
		}

		for (fsitem=fs_tr_list; fsitem != NULL; fsitem=fsitem->hh.next) {
			fn_trc_header->req_t_desc[n].type = FILESYSTEM;
			fn_trc_header->req_t_desc[n].size_of_entry = FS_FN_TRACE_SIZE;
			acquireread_mrsw(&fsitem->fn_entry->trace_lock);
			fn_trc_header->req_t_desc[n++].num_of_entry = fsitem->fn_entry->cur_index;
			releaseread_mrsw(&fsitem->fn_entry->trace_lock);
		}
		for (admitem=admin_tr_list; admitem != NULL; admitem=admitem->hh.next) {
			fn_trc_header->req_t_desc[n].type = ADMIN;
			fn_trc_header->req_t_desc[n].size_of_entry = ADMIN_FN_TRACE_SIZE;
			acquireread_mrsw(&admitem->fn_entry->trace_lock);
			fn_trc_header->req_t_desc[n++].num_of_entry = admitem->fn_entry->cur_index;
			releaseread_mrsw(&admitem->fn_entry->trace_lock);
		}
		TAILQ_FOREACH (tailq_item, acomp, list) {
			fn_trc_header->req_t_desc[n].type = ADMIN_COMPLETED;
			fn_trc_header->req_t_desc[n].size_of_entry = ADMIN_FN_TRACE_SIZE;
			acquireread_mrsw(&tailq_item->fn_entry->trace_lock);
			fn_trc_header->req_t_desc[n++].num_of_entry = tailq_item->fn_entry->cur_index;
			releaseread_mrsw(&tailq_item->fn_entry->trace_lock);
		}

		/* Set header information */
		req_header->req_t_desc.num_of_entry = req_trace->cur_index;
		req_header->req_t_desc.size_of_entry = REQ_TRACE_SIZE;
		trc_header->trace_size =
			req_header->req_t_desc.size_of_entry +		/* Request trace */
			(num_of_fs_fn_trace * FS_FN_TRACE_SIZE) +	/* Function trace (filesystem) */
			(num_of_adm_fn_trace * ADMIN_FN_TRACE_SIZE) +	/* Function trace (admin) */
			trc_header->header_size + req_header->header_size + fn_trc_header->header_size;

		/* Write headers */
		written = write(fd, trc_header, sizeof(struct trace_header));
		written = write(fd, req_header, sizeof(struct request_header));

		/* Write request trace data */
		ltfs_mutex_lock(&req_trace->req_trace_lock);
		written = write(fd, req_trace->entries, REQ_TRACE_SIZE);
		ltfs_mutex_unlock(&req_trace->req_trace_lock);

		/* Write function trace header */
		written = write(fd, &fn_trc_header->header_size, sizeof(uint32_t));
		written = write(fd, &fn_trc_header->num_of_fn_trace, sizeof(uint32_t));
		for (unsigned int i=0; i<n; i++)
			written = write(fd, &fn_trc_header->req_t_desc[i], sizeof(struct function_trace_descriptor));
		written = write(fd, &fn_trc_header->crc, sizeof(uint32_t));
		free(fn_trc_header->req_t_desc);
		fn_trc_header->req_t_desc = NULL;

		/* Write function trace data */
		for (fsitem=fs_tr_list; fsitem != NULL; fsitem=fsitem->hh.next) {
			acquireread_mrsw(&fsitem->fn_entry->trace_lock);
			written = write(fd, fsitem->fn_entry->entries, FS_FN_TRACE_SIZE);
			releaseread_mrsw(&fsitem->fn_entry->trace_lock);
		}
		for (admitem=admin_tr_list; admitem != NULL; admitem=admitem->hh.next) {
			acquireread_mrsw(&admitem->fn_entry->trace_lock);
			written = write(fd, admitem->fn_entry->entries, ADMIN_FN_TRACE_SIZE);
			releaseread_mrsw(&admitem->fn_entry->trace_lock);
		}
		TAILQ_FOREACH (tailq_item, acomp, list) {
			acquireread_mrsw(&tailq_item->fn_entry->trace_lock);
			written = write(fd, tailq_item->fn_entry->entries, ADMIN_FN_TRACE_SIZE);
			releaseread_mrsw(&tailq_item->fn_entry->trace_lock);
		}
	}
	close(fd);

	return 0;
}

int ltfs_get_trace_status(char **val)
{
	int ret = 0;
	char *trstat = NULL;

	ret = asprintf(&trstat, "%s", (trace_enable == true) ? "on" : "off" );
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "10001E", __FILE__);
		return -LTFS_NO_MEMORY;
	}
	*val = strdup(trstat);
	if (! (*val)) {
		ltfsmsg(LTFS_ERR, "10001E", __FILE__);
		return -LTFS_NO_MEMORY;
	}
	free(trstat);
	return 0;
}

int ltfs_set_trace_status(char *mode)
{
	int ret = 0;

	if (! strcmp(mode, "on")) {
		trace_enable = true;
		ltfs_trace_init();
	} else {
		if (trace_enable == true)
			ltfs_trace_destroy();
		trace_enable = false;
	}
	return ret;
}

void ltfs_profiler_add_entry(FILE* file, ltfs_mutex_t *mutex, uint32_t req_num)
{
	struct profiler_entry entry;

	if (file) {
		entry.time = get_time_stamp(&start_offset);
		entry.tid = ltfs_get_thread_id();
		entry.req_num = req_num;
		ltfs_mutex_lock(mutex);
		fwrite((void*)&entry, PROF_ENTRY_SIZE, 1, file);
		ltfs_mutex_unlock(mutex);
	}
}

int ltfs_profiler_set(uint64_t source)
{
	int ret, ret_save = 0;

	/* Set request profiler */
	if (source & PROF_REQ)
		ret = ltfs_request_profiler_start(work_dir);
	else
		ret = ltfs_request_profiler_stop();

	if(ret)
		ret_save = ret;

	/* Set ioscheduler profiler */
	if (source & PROF_IOSCHED)
		ret = iosched_profiler_start(work_dir);
	else
		ret = iosched_profiler_stop();

	if(ret)
		ret_save = ret;

	/* Set tape backend or changer profiler */
	if (source & PROF_DRIVER || source & PROF_CHANGER)
		ret = tape_profiler_start(work_dir);
	else
		ret = tape_profiler_stop();

	if(ret)
		ret_save = ret;

	if(ret_save)
		ret = ret_save;

	return ret;
}
