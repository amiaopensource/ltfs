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
 ** FILE NAME:       ltfslogging.c
 **
 ** DESCRIPTION:     Routines for logging via syslog and stderr. (LTFS messages)
 **
 ** AUTHORS:         Brian Biskeborn
 **                  IBM Almaden Research Center
 **                  bbiskebo@us.ibm.com
 **
 *************************************************************************************
 */

#ifdef mingw_PLATFORM
#include "arch/win/win_util.h"
#endif
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#ifndef mingw_PLATFORM
#include <syslog.h>
#endif

#ifdef __APPLE__
#include <ICU/unicode/ucnv.h>
#include <ICU/unicode/ures.h>
#include <ICU/unicode/utypes.h>
#include <ICU/unicode/udata.h>
#include <ICU/unicode/uclean.h>
#else

/* 
 * OSR
 * 
 * Some ICU header files are ill formed and do not include the
 * right files for the MinGW environment if __MINGW32__ is 
 * defined. Strange, yes, but true 
 *  
 */
#if defined(HP_mingw_BUILD) && defined(__MINGW32__)

#undef __MINGW32__
#include <unicode/ucnv.h>
#include <unicode/ures.h>
#include <unicode/utypes.h>
#define __MINGW32__
#else 
#include <unicode/ucnv.h>
#include <unicode/ures.h>
#include <unicode/utypes.h>
#endif /* #if defined(HP_mingw_BUILD) && defined(__MINGW32__) */
#include <unicode/udata.h>
#include <unicode/putil.h>
#include <unicode/uclean.h>

#endif
#ifdef mingw_PLATFORM
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "arch/win/winlog.h"
#else
#include <dlfcn.h>
#include <sys/types.h>
#endif

#include "libltfs/ltfslogging.h"
#include "libltfs/ltfs_thread.h"
#include "libltfs/ltfs_locking.h"
#include "libltfs/ltfs_error.h"
#include "queue.h"
#if 0
#include "ltfssnmp.h"
#endif /* 0 */

/* Some hard-coded message bits. */
#define MSG_PREFIX_POSIX_TID   "%016llx LTFS%s "
#define MSG_PREFIX_TID         "%lx LTFS%s "
#define MSG_PREFIX             "LTFS%s "
#define MSG_FALLBACK           "(could not generate message)"

#define OUTPUT_BUF_SIZE 4096  /* Output buffer size, should be big enough to hold any message. */

struct plugin_bundle {
	TAILQ_ENTRY(plugin_bundle) list;
	int32_t start_id;                  /**< First message ID allocated to this plugin */
	int32_t end_id;                    /**< Last message ID allocated to this plugin */
	UResourceBundle *bundle_root;      /**< Root resource bundle for this plugin */
	UResourceBundle *bundle_messages;  /**< Resource bundle containing this plugin's messages */
};

/* Syslog levels corresponding to the LTFS logging levels defined in libltfs/ltfslogging.h. */
static int syslog_levels[] = {
	LOG_ERR,      /* LTFS_ERR    */
	LOG_WARNING,  /* LTFS_WARN   */
	LOG_INFO,     /* LTFS_INFO   */
	LOG_DEBUG,    /* LTFS_DEBUG  */
	LOG_DEBUG,    /* LTFS_DEBUG1 */
	LOG_DEBUG,    /* LTFS_DEBUG2 */
	LOG_DEBUG,    /* LTFS_DEBUG3 */
	LOG_DEBUG,    /* LTFS_TRACE  */
};

#ifdef mingw_PLATFORM
/* 
 * OSR
 * 
 * In our MinGW environment, we dynamically link to the package 
 * data. We also change the name of the libltfs package data to 
 * be bin_libltfs to avoid collision 
 *  
 */
#ifdef HP_mingw_BUILD
extern char bin_libltfs_dat[];
extern char internal_error_dat[];
#define libltfs_dat bin_libltfs_dat
#define LIBLTFS_BUNDLE_NAME "bin_libltfs"
#define INTERNALERRROR_BUNDLE_NAME "internal_error"
#else
char *libltfs_dat;
char *internal_error_dat;
#endif /* HP_mingw_BUILD */
#else
U_CFUNC char libltfs_dat[]; /* U_CFUNC is an ICU synonym for extern. */
U_CFUNC char internal_error_dat[]; /* U_CFUNC is an ICU synonym for extern. */
#define LIBLTFS_BUNDLE_NAME "libltfs"
#define INTERNALERRROR_BUNDLE_NAME "internal_error"
#endif /* mingw_PLATFORM */

static bool libltfs_dat_init = false;
int ltfs_log_level = LTFS_INFO;
int ltfs_syslog_level = LTFS_INFO;
bool ltfs_print_thread_id = false;
static bool ltfs_use_syslog = false;

/* Resource bundles, used for quick indexing into message arrays. */
static UResourceBundle *bundle_fallback;
static TAILQ_HEAD(message_struct, plugin_bundle) plugin_bundles;

/* Static output buffer: needed to avoid allocating memory on error. */
static ltfs_mutex_t output_lock;
static char output_buf[OUTPUT_BUF_SIZE];
static char msg_buf[OUTPUT_BUF_SIZE * 2];
static UConverter *output_conv = NULL;

/* This function does not exist in our code base */
#ifndef HP_mingw_BUILD
#ifdef mingw_PLATFORM
static int _open_message_file(char *bundle_name, void **bundle_data);
#endif
#endif

int ltfsprintf_init(int log_level, bool use_syslog, bool print_thread_id)
{
	int ret;
	UErrorCode err = U_ZERO_ERROR;
	struct plugin_bundle *pl;

	/* Open converter for generating output in the system locale. */
	ret = ltfs_mutex_init(&output_lock);
	if (ret > 0) {
		fprintf(stderr, "LTFS10002E Could not initialize mutex (%d)\n", ret);
		return -ret;
	}
#ifndef HP_mingw_BUILD
	output_conv = ucnv_open(NULL, &err);
#else
	/* We always use the UTF-8 converter */
	output_conv = ucnv_open("UTF-8", &err);
#endif
	if (U_FAILURE(err)) {
		fprintf(stderr, "LTFS9008E Could not open output converter (ucnv_open: %d)\n", err);
		output_conv = NULL;
		ltfsprintf_finish();
		return -1;
	}

	/* Initialize output lock and plugin list */
	TAILQ_INIT(&plugin_bundles);

#ifdef mingw_PLATFORM
/* 
 * OSR
 * 
 * In our MinGW environment, we dynamically link to the package 
 * data, thus we do not use this call
 *  
 */
#ifndef HP_mingw_BUILD
	u_setDataDirectory(LTFS_RB_DIR);
#endif
#endif

	/* Load the libltfs message bundle and the primary message set */
	ret = ltfsprintf_load_plugin(LIBLTFS_BUNDLE_NAME, libltfs_dat, (void **)&pl);
	if (ret < 0) {
		fprintf(stderr, "LTFS11293E Cannot load messages for libltfs (%d)\n", ret);
		ltfsprintf_finish();
		return ret;
	}

	/* Load fallback message set */
	bundle_fallback = ures_getByKey(pl->bundle_root, "fallback_messages", NULL, &err);
	if (U_FAILURE(err)) {
		fprintf(stderr, "LTFS9006E Could not load resource \"fallback_messages\" (ures_getByKey: %d)\n", err);
		bundle_fallback = NULL;
		ltfsprintf_finish();
		return -1;
	}

	/* Load the libltfs message bundle and the primary message set */
	ret = ltfsprintf_load_plugin(INTERNALERRROR_BUNDLE_NAME, internal_error_dat, (void **)&pl);
	if (ret < 0) {
		fprintf(stderr, "LTFS11293E Cannot load messages for internal error (%d)\n", ret);
		ltfsprintf_finish();
		return ret;
	}

	ltfs_log_level = log_level;
	ltfs_use_syslog = use_syslog;
	ltfs_print_thread_id = print_thread_id;
	libltfs_dat_init = true;

	return 0;
}

/* Shut down the logging and error reporting framework. */
void ltfsprintf_finish()
{

	libltfs_dat_init = false;

	if (bundle_fallback) {
		ures_close(bundle_fallback);
		bundle_fallback = NULL;
	}
	while (1) {
		if (! TAILQ_EMPTY(&plugin_bundles))
			ltfsprintf_unload_plugin(TAILQ_LAST(&plugin_bundles, message_struct));
		else
			break;
	}
	if (output_conv) {
		ucnv_close(output_conv);
		output_conv = NULL;
	}

#ifdef mingw_PLATFORM
/* 
 * OSR
 * 
 * In our MinGW environment, we dynamically link to the package 
 * data, thus nothing to free here
 *  
 */
#ifndef HP_mingw_BUILD
	free(libltfs_dat);
#endif
#endif

	ltfs_mutex_destroy(&output_lock);
	u_cleanup();
}

/* Update ltfs_log_level */
int ltfsprintf_set_log_level(int log_level)
{
	if (log_level < LTFS_ERR) {
		fprintf(stderr, "LTFS11318W Unknown log level (%d), forced the level to (%d)\n", log_level, LTFS_ERR);
		log_level = LTFS_ERR;
	}
	else if (log_level > LTFS_TRACE) {
		fprintf(stderr, "LTFS11318W Unknown log level (%d), forced the level to (%d)\n", log_level, LTFS_TRACE);
		log_level = LTFS_TRACE;
	}
	else {
		ltfs_log_level = log_level;
	}
	return 0;
}


int ltfsprintf_load_plugin(const char *bundle_name, void *bundle_data, void **messages)
{
	UErrorCode err = U_ZERO_ERROR;
	UResourceBundle *bundle;
	struct plugin_bundle *pl;

	CHECK_ARG_NULL(bundle_name, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(messages, -LTFS_NULL_ARG);

/* 
 * OSR
 * 
 * In our MinGW environment, we dynamically link to the package 
 * data, thus we need this call
 *  
 */
#if !defined(mingw_PLATFORM) || defined(HP_mingw_BUILD)
	udata_setAppData(bundle_name, bundle_data, &err);
	if (U_FAILURE(err)) {
		if (libltfs_dat_init)
			ltfsmsg(LTFS_ERR, "11287E", err);
		else
			fprintf(stderr, "LTFS11287E Cannot load messages: failed to register message data (%d)\n", err);
		return -1;
	}
#endif

	pl = calloc(1, sizeof(struct plugin_bundle));
	if (! pl) {
		if (libltfs_dat_init)
			ltfsmsg(LTFS_ERR, "10001E", __FUNCTION__);
		else
			fprintf(stderr, "LTFS10001E Memory allocation failed (%s)\n", __FUNCTION__);
		return -LTFS_NO_MEMORY;
	}

	/* Load messages table */
	pl->bundle_root = ures_open(bundle_name, NULL, &err);
	if (U_FAILURE(err)) {
		if (libltfs_dat_init)
			ltfsmsg(LTFS_ERR, "11286E", err);
		else
			fprintf(stderr, "LTFS11286E Cannot load messages: failed to open resource bundle (%d)\n", err);
		free(pl);
		return -1;
	}
	pl->bundle_messages = ures_getByKey(pl->bundle_root, "messages", NULL, &err);
	if (U_FAILURE(err)) {
		if (libltfs_dat_init)
			ltfsmsg(LTFS_ERR, "11281E", err);
		else
			fprintf(stderr, "LTFS11281E Cannot load messages: failed to get message table (%d)\n", err);
		ures_close(pl->bundle_root);
		free(pl);
		return -1;
	}

	/* Figure out the start ID for this component. */
	bundle = ures_getByKey(pl->bundle_messages, "start_id", NULL, &err);
	if (U_FAILURE(err)) {
		if (libltfs_dat_init)
			ltfsmsg(LTFS_ERR, "11282E", err);
		else
			fprintf(stderr, "LTFS11282E Cannot load messages: failed to determine first message ID (ures_getByKey: %d)\n", err);
		ures_close(pl->bundle_messages);
		ures_close(pl->bundle_root);
		free(pl);
		return -1;
	}

	pl->start_id = ures_getInt(bundle, &err);
	if (U_FAILURE(err)) {
		if (libltfs_dat_init)
			ltfsmsg(LTFS_ERR, "11283E", err);
		else
			fprintf(stderr, "LTFS11283E Cannot load messages: failed to determine first message ID (ures_getInt: %d)\n", err);
		ures_close(bundle);
		ures_close(pl->bundle_messages);
		ures_close(pl->bundle_root);
		free(pl);
		return -1;
	}
	ures_close(bundle);

	/* Check for an end ID for this component, or default it to start_id + 999 if not present */
	bundle = ures_getByKey(pl->bundle_messages, "end_id", NULL, &err);
	if (U_SUCCESS(err)) {
		pl->end_id = ures_getInt(bundle, &err);
		if (U_FAILURE(err)) {
			if (libltfs_dat_init)
				ltfsmsg(LTFS_WARN, "11288W");
			else
				fprintf(stderr, "LTFS11288W No end ID found for this message bundle, assigning 1000 message IDs\n");
			pl->end_id = pl->start_id + 999;
		}
		ures_close(bundle);
	} else
		pl->end_id = pl->start_id + 999;

	*messages = pl;
	ltfs_mutex_lock(&output_lock);
	TAILQ_INSERT_HEAD(&plugin_bundles, pl, list);
	ltfs_mutex_unlock(&output_lock);
	return 0;
}

void ltfsprintf_unload_plugin(void *handle)
{
	struct plugin_bundle *pl = handle;

	if (pl) {
		ltfs_mutex_lock(&output_lock);
		TAILQ_REMOVE(&plugin_bundles, pl, list);
		ltfs_mutex_unlock(&output_lock);
		ures_close(pl->bundle_messages);
		ures_close(pl->bundle_root);
		free(pl);
	}
}

/* Print a formatted message in the current system locale. */
int ltfsmsg_internal(bool print_id, int level, char **msg_out, const char *id, ...)
{
	const UChar *format_uc = NULL;
	int32_t prefix_len, format_len;
	int32_t id_val;
	UErrorCode err = U_ZERO_ERROR;
	va_list argp;
	struct plugin_bundle *entry;

	id_val = atol(id);

	/* Check loaded plugins for the message, most recently loaded first */
	if (! TAILQ_EMPTY(&plugin_bundles)) {
		ltfs_mutex_lock(&output_lock);
		TAILQ_FOREACH(entry, &plugin_bundles, list) {
			if (entry->start_id <= id_val && id_val <= entry->end_id) {
				err = U_ZERO_ERROR;
				format_uc = ures_getStringByKey(entry->bundle_messages, id, &format_len, &err);
				if (U_FAILURE(err) && err != U_MISSING_RESOURCE_ERROR) {
					ltfs_mutex_unlock(&output_lock);
					goto internal_error;
				} else if (U_SUCCESS(err))
					break;
				format_uc = NULL;
			} else if (id[0] == 'I' || id[0] == 'D') {
				err = U_ZERO_ERROR;
				format_uc = ures_getStringByKey(entry->bundle_messages, id, &format_len, &err);
				if (U_SUCCESS(err))
					break;
				format_uc = NULL;
			}
		}
		ltfs_mutex_unlock(&output_lock);
		err = U_ZERO_ERROR;
	}

	/* Try to get a fallback message if we didn't find the real message */
	if (! format_uc) {
		format_uc = ures_getStringByKey(bundle_fallback, "notfound", &format_len, &err);
		if (U_FAILURE(err))
			goto internal_error;
	}

	/* Format and print the message string. */
	ltfs_mutex_lock(&output_lock);
	if (ltfs_print_thread_id)
		prefix_len = print_id ? sprintf(output_buf, MSG_PREFIX_TID, (unsigned long)ltfs_get_thread_id(), id) : 0;
	else
		prefix_len = print_id ? sprintf(output_buf, MSG_PREFIX, id) : 0;
	ucnv_fromUChars(output_conv, output_buf + prefix_len, OUTPUT_BUF_SIZE - prefix_len - 1,
		format_uc, format_len, &err);
	if (err == U_BUFFER_OVERFLOW_ERROR) {
		err = U_ZERO_ERROR;
		format_uc = ures_getStringByKey(bundle_fallback, "overflow", &format_len, &err);
		if (U_FAILURE(err)) {
			ltfs_mutex_unlock(&output_lock);
			goto internal_error;
		}

		ucnv_fromUChars(output_conv, output_buf + prefix_len, OUTPUT_BUF_SIZE - prefix_len - 1,
			format_uc, format_len, &err);
		if (U_FAILURE(err)) {
			ltfs_mutex_unlock(&output_lock);
			goto internal_error;
		}
	} else if (U_FAILURE(err)) {
		ltfs_mutex_unlock(&output_lock);
		goto internal_error;
	}

#ifdef mingw_PLATFORM
	va_start(argp, id);
	vsyslog(level, output_buf, argp);
	va_end(argp);
#else
	va_start(argp, id);
	vfprintf(stderr, output_buf, argp);
	va_end(argp);
	fprintf(stderr, "\n");

	if (level <= ltfs_syslog_level && ltfs_use_syslog) {
		va_start(argp, id);
		if (level <= LTFS_ERR)
			vsyslog(syslog_levels[LTFS_ERR], output_buf, argp);
		else if (level >= LTFS_TRACE)
			vsyslog(syslog_levels[LTFS_TRACE], output_buf, argp);
		else
			vsyslog(syslog_levels[level], output_buf, argp);
		va_end(argp);
	}
#endif

	if (msg_out) {
		va_start(argp, id);
		vsprintf(msg_buf, output_buf, argp);
		va_end(argp);
		*msg_out = strdup(msg_buf);
	}

#if ((!defined (__APPLE__)) && (!defined (mingw_PLATFORM)))
#if 0
	if (is_snmp_enabled()) {
#if 0 // SNMP Error Trap
		if (level <= LTFS_ERR) {
			/* Send a trap of Error (id and pos+1) */
			char *pos;
			va_start(argp, id);
			vsprintf(msg_buf, output_buf, argp);
			va_end(argp);
			pos = strstr(msg_buf, " ");
			send_ltfsErrorTrap(pos+1);
		} else  {
#endif
			if (is_snmp_trapid(id) == true) {
				/* Send a trap of Info (id and pos+1) */
				char *pos;
				va_start(argp, id);
				vsprintf(msg_buf, output_buf, argp);
				va_end(argp);
				pos = strstr(msg_buf, " ");
				send_ltfsInfoTrap(pos+1);
			}
#if 0 // SNMP Error Trap
		}
#endif
	}
#endif /* 0 */
#endif /* ((!defined (__APPLE__)) && (!defined (mingw_PLATFORM))) */

	ltfs_mutex_unlock(&output_lock);

	return 0;

internal_error:
	if (ltfs_print_thread_id)
		fprintf(stderr, MSG_PREFIX_TID MSG_FALLBACK "\n", (unsigned long)ltfs_get_thread_id(), id);
	else
		fprintf(stderr, MSG_PREFIX MSG_FALLBACK "\n", id);

	if (level < LTFS_DEBUG && ltfs_use_syslog) {
		if (ltfs_print_thread_id) {
			if (level <= LTFS_ERR)
				syslog(syslog_levels[LTFS_ERR], MSG_PREFIX_TID MSG_FALLBACK, (unsigned long)ltfs_get_thread_id(), id);
			else if (level >= LTFS_TRACE)
				syslog(syslog_levels[LTFS_TRACE], MSG_PREFIX_TID MSG_FALLBACK, (unsigned long)ltfs_get_thread_id(), id);
			else
				syslog(syslog_levels[level], MSG_PREFIX_TID MSG_FALLBACK, (unsigned long)ltfs_get_thread_id(), id);
		} else {
			if (level <= LTFS_ERR)
				syslog(syslog_levels[LTFS_ERR], MSG_PREFIX MSG_FALLBACK, id);
			else if (level >= LTFS_TRACE)
				syslog(syslog_levels[LTFS_TRACE], MSG_PREFIX MSG_FALLBACK, id);
			else
				syslog(syslog_levels[level], MSG_PREFIX MSG_FALLBACK, id);
		}
	}
	return -1;
}
