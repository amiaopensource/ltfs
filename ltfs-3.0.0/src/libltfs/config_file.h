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
** FILE NAME:       config_file.h
**
** DESCRIPTION:     Declares the interface for reading LTFS configuration files.
**
** AUTHORS:         Brian Biskeborn
**                  IBM Almaden Research Center
**                  bbiskebo@us.ibm.com
**
**                  Lucas C. Villa Real
**                  IBM Almaden Research Center
**                  lucasvr@us.ibm.com
**
*************************************************************************************
*/

#ifndef __CONFIG_FILE_H__
#define __CONFIG_FILE_H__

#include "queue.h"

struct config_file;

/**
 * Read LTFS configuration information from the given file.
 * @param path File to read. If NULL, the default path is used.
 * @param config On success, points to a newly allocated configuration data structure.
 *               Its value is undefined on failure.
 * @return 0 on success or a negative value on error.
 */
int config_file_load(const char *path, struct config_file **config);

/**
 * Free an LTFS configuration structure.
 * @param config Configuration structure to free.
 */
void config_file_free(struct config_file *config);

/**
 * Read the default plugin from a config file structure.
 * @param type Plugin type.
 * @param config Configuration data structure to read. 
 * @return The default driver name, or NULL on error.
 */
const char *config_file_get_default_plugin(const char *type, struct config_file *config);

/**
 * Get the library path for a given plugin.
 * @param type Plugin type.
 * @param name Plugin to look up.
 * @param config Configuration structure to search.
 * @return The library path on success, or NULL on error.
 */
const char *config_file_get_lib(const char *type, const char *name, struct config_file *config);

/**
 * Get a list of all plugins found in the configuration file.
 * @param type Plugin type.
 * @param config Configuration structure to search.
 * @return A NULL-terminated list of plugin names on success, or NULL on failure.
 */
char **config_file_get_plugins(const char *type, struct config_file *config);

/**
 * Get a list of all default options found in the configuration file. 
 * @param type Option type 
 * @param config Configuration structure to search.
 * @return a NULL-terminated list of option names on success, or NULL on failure.
 */
char **config_file_get_options(const char *type, struct config_file *config);

#endif /* __CONFIG_FILE_H__ */

