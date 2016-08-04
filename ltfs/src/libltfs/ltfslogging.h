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

#ifndef __ltfslogging_h
#define __ltfslogging_h

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>

#include "ltfsprintf.h"

enum ltfs_log_levels {
	LTFS_NONE  = -1, /* Don't print any log (special use for mkltfs/ltfsck) */
	LTFS_ERR    = 0,  /* Fatal error or operation failed unexpectedly */
	LTFS_WARN   = 1,  /* Unexpected condition, but the program can continue */
	LTFS_INFO   = 2,  /* Helpful message */
	LTFS_DEBUG  = 3,  /* Diagnostic messages (Level 0: Base Level) */
	LTFS_DEBUG1 = 4,  /* Diagnostic messages (Level 1) */
	LTFS_DEBUG2 = 5,  /* Diagnostic messages (Level 2) */
	LTFS_DEBUG3 = 6,  /* Diagnostic messages (Level 3) */
	LTFS_TRACE  = 7,  /* Full call tracing */
};

extern int ltfs_log_level;
extern int ltfs_syslog_level;
extern bool ltfs_print_thread_id;

/* Wrapper for ltfsmsg_internal. It only invokes the message print function if the requested
 * log level is not too verbose. */
#define ltfsmsg(level, id, ...) \
	do { \
		if (level <= ltfs_log_level) \
			ltfsmsg_internal(true, level, NULL, id, ##__VA_ARGS__);	\
	} while (0)

/* Wrapper for ltfsmsg_internal. It only invokes the message print function if the requested
 * log level is not too verbose. */
#define ltfsmsg_buffer(level, id, buffer, ...)	\
	do { \
		*buffer = NULL; \
		if (level <= ltfs_log_level) \
			ltfsmsg_internal(true, level, buffer, id, ##__VA_ARGS__);	\
	} while (0)

/* Wrapper for ltfsmsg_internal that prints a message without the LTFSnnnnn prefix. It
 * always invokes the message print function, regardless of the message level. */
#define ltfsresult(id, ...) \
	do { \
		ltfsmsg_internal(false, LTFS_TRACE + 1, NULL, id, ##__VA_ARGS__); \
	} while (0)

/* Shortcut for asserting that a function argument is not NULL. It generates an error-level
 * message if the given argument is NULL. Functions for which a NULL argument is a warning
 * should not use this macro. */
#define CHECK_ARG_NULL(var, ret) \
	do { \
		if (! (var)) { \
			ltfsmsg(LTFS_ERR, "10005E", #var, __FUNCTION__); \
			return ret; \
		} \
	} while (0)

/**
 * Initialize the logging and error reporting functions.
 * @param log_level Logging level (generally one of LTFS_ERROR...LTFS_TRACE).
 * @param use_syslog Send error/warning/info messages to syslog? This function does not call
 *                   openlog(); the calling application may do so if it wants to.
 * @param print_thread_id Print thread ID to the message.
 * @return 0 on success or a negative value on error.
 */
int ltfsprintf_init(int log_level, bool use_syslog, bool print_thread_id);

/**
 * Tear down the logging and error reporting framework.
 */
void ltfsprintf_finish();

/**
 * Update ltfs_log_level
 */
int ltfsprintf_set_log_level(int log_level);

/**
 * Load messages for a plugin from the specified resource name.
 * @param bundle_name Message bundle name.
 * @param bundle_data Message bundle data structure.
 * @param messages On success, contains a handle to the loaded message bundles. That handle
 *                 should be passed to @ltfsprintf_unload_plugin later.
 * @return 0 on success or a negative value on error.
 */
int ltfsprintf_load_plugin(const char *bundle_name, void *bundle_data, void **messages);

/**
 * Stop using messages from the given plugin message bundle.
 * @param handle Message bundle handle, as returned by @ltfsprintf_load_plugin.
 */
void ltfsprintf_unload_plugin(void *handle);

/**
 * Print a message in the system locale. Any extra arguments are substituted into the
 * format string. The current logging level is ignored, so the ltfsmsg macro
 * (which calls this function) should be used instead.
 * The generated output goes to stderr. If syslog is enabled, messages of severity LTFS_INFO
 * through LTFS_ERR go to syslog as well. LTFS_DEBUG and LTFS_TRACE level messages always go
 * only to stderr.
 * @param print_id Print the message prefix LTFSnnnnn ?
 * @param level Log level of this message, must be one of the ltfs_log_levels (LTFS_ERROR, etc.).
 * @param id Unique ID of this error.
 * @return 0 if a message was printed or a negative value on error.
 */
int ltfsmsg_internal(bool print_id, int level, char **msg_out, const char *id, ...);

#ifdef __cplusplus
}
#endif

#endif /* __ltfslogging_h */
