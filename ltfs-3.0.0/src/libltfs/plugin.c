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
** FILE NAME:       plugin.c
**
** DESCRIPTION:     Provides functions for working with libltfs plugins.
**
** AUTHORS:         Brian Biskeborn
**                  IBM Almaden Research Center
**                  bbiskebo@us.ibm.com
**
**                  Lucas C. Villa Real
**                  IBM Almaden Research Center
**                  lucasvr@us.ibm.com
**
** Copyright (C) 2012 OSR Open Systems Resources, Inc.
** 
************************************************************************************* 
*/

#ifdef mingw_PLATFORM
#include "arch/win/win_util.h"
#endif
#include <stdlib.h>
#include <string.h>
#ifndef mingw_PLATFORM
#include <dlfcn.h>
#endif
#include <errno.h>

#include "libltfs/ltfs_error.h"
#include "libltfs/ltfslogging.h"
#include "config_file.h"
#include "plugin.h"
#include "tape.h"
#include "kmi.h"

int plugin_load(struct libltfs_plugin *pl, const char *type, const char *name,
	struct config_file *config)
{
	int ret;
	const char *lib_path, *message_bundle_name;
	void *message_bundle_data;
	void *(*get_ops)(void) = NULL;
	const char *(*get_messages)(void **) = NULL;

	CHECK_ARG_NULL(pl, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(type, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(name, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(config, -LTFS_NULL_ARG);

	pl->lib_handle = NULL;

	lib_path = config_file_get_lib(type, name, config);
	if (! lib_path) {
		ltfsmsg(LTFS_ERR, "11260E", name);
		return -LTFS_NO_PLUGIN;
	}

	pl->lib_handle = dlopen(lib_path, RTLD_NOW);
	if (! pl->lib_handle) {
		ltfsmsg(LTFS_ERR, "11261E", dlerror());
		return -LTFS_PLUGIN_LOAD;
	}

	/* Show loading plugins */
	ltfsmsg(LTFS_INFO, "17085I", name, type);

	/* Make sure the plugin knows how to describe its supported operations */
	if (! strcmp(type, "iosched"))
		get_ops = dlsym(pl->lib_handle, "iosched_get_ops");
	else if (! strcmp(type, "driver"))
		get_ops = dlsym(pl->lib_handle, "tape_dev_get_ops");
	else if (! strcmp(type, "changer"))
		get_ops = dlsym(pl->lib_handle, "changer_get_ops");
	else if (! strcmp(type, "dcache"))
		get_ops = dlsym(pl->lib_handle, "dcache_get_ops");
	else if (! strcmp(type, "kmi"))
		get_ops = dlsym(pl->lib_handle, "kmi_get_ops");
	else if (! strcmp(type, "crepos"))
		get_ops = dlsym(pl->lib_handle, "crepos_get_ops");
	/* config_file_get_lib already verified that "type" contains one of the values above */

	if (! get_ops) {
		ltfsmsg(LTFS_ERR, "11263E", dlerror());
#ifdef HP_mingw_BUILD
		/* Get rid of compiler warning for not checking the result*/
		(void)dlclose(pl->lib_handle);
#else
		dlclose(pl->lib_handle);
#endif
		pl->lib_handle = NULL;
		return -LTFS_PLUGIN_LOAD;
	}

	/* Make sure the plugin knows how to describe its message bundle (if any) */
	if (! strcmp(type, "iosched"))
		get_messages = dlsym(pl->lib_handle, "iosched_get_message_bundle_name");
	else if (! strcmp(type, "driver"))
		get_messages = dlsym(pl->lib_handle, "tape_dev_get_message_bundle_name");
	else if (! strcmp(type, "changer"))
		get_messages = dlsym(pl->lib_handle, "changer_get_message_bundle_name");
	else if (! strcmp(type, "dcache"))
		get_messages = dlsym(pl->lib_handle, "dcache_get_message_bundle_name");
	else if (! strcmp(type, "kmi"))
		get_messages = dlsym(pl->lib_handle, "kmi_get_message_bundle_name");
	else if (! strcmp(type, "crepos"))
		get_messages = dlsym(pl->lib_handle, "crepos_get_message_bundle_name");
	/* config_file_get_lib already verified that "type" contains one of the values above */

	if (! get_messages) {
		ltfsmsg(LTFS_ERR, "11284E", dlerror());
#ifdef HP_mingw_BUILD
		/* Get rid of compiler warning for not checking the result*/
		(void)dlclose(pl->lib_handle);
#else
		dlclose(pl->lib_handle);
#endif
		pl->lib_handle = NULL;
		return -LTFS_PLUGIN_LOAD;
	}

	/* Ask the plugin what operations and messages it provides */
	pl->ops = get_ops();
	if (! pl->ops) {
		ltfsmsg(LTFS_ERR, "11264E");
#ifdef HP_mingw_BUILD
		/* Get rid of compiler warning for not checking the result*/
		(void)dlclose(pl->lib_handle);
#else
		dlclose(pl->lib_handle);
#endif
		pl->lib_handle = NULL;
		return -LTFS_PLUGIN_LOAD;
	}

	message_bundle_name = get_messages(&message_bundle_data);
	if (message_bundle_name) {
		ret = ltfsprintf_load_plugin(message_bundle_name, message_bundle_data, &pl->messages);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, "11285E", type, name, ret);
			return ret;
		}
	}

	return 0;
}

int plugin_unload(struct libltfs_plugin *pl)
{
	if (! pl || ! pl->lib_handle)
		return 0;
	ltfsprintf_unload_plugin(pl->messages);
	if (dlclose(pl->lib_handle)) {
		ltfsmsg(LTFS_ERR, "11262E", dlerror());
		return -LTFS_PLUGIN_UNLOAD;
	}
	pl->lib_handle = NULL;
	pl->ops = NULL;
	return 0;
}

/**
 * Print the backend's LTFS help message.
 * @param ops tape operations for the backend
 */
static void print_help_message(const char *progname, void *ops, const char * const type)
{
	if (! ops) {
		ltfsmsg(LTFS_WARN, "10006W", "ops", __FUNCTION__);
		return;
	}

	if (! strcmp(type, "kmi")) {
		int ret = kmi_print_help_message(ops);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, "11316E");
		}
	} else if (! strcmp(type, "driver"))
		tape_print_help_message(progname, ops);
	else
		ltfsmsg(LTFS_ERR, "11317E", type);
}

void plugin_usage(const char* progname, const char *type, struct config_file *config)
{
	struct libltfs_plugin pl = {0};
	char **backends;
	int ret, i;

	backends = config_file_get_plugins(type, config);
	if (! backends) {
		if (! strcmp(type, "driver"))
			ltfsresult("14403I"); /* -o devname=<dev> */
		return;
	}

	for (i = 0; backends[i] != NULL; ++i) {
		/* Print the usage information of only the 'ltotape' driver. */
		if (! strcmp(backends[i], "ltotape")) {
			ret = plugin_load(&pl, type, backends[i], config);
			if (ret < 0)
				continue;
			print_help_message(progname, pl.ops, type);
			plugin_unload(&pl);
		}
	}

	for (i = 0; backends[i] != NULL; ++i)
		free(backends[i]);
	free(backends);
}
