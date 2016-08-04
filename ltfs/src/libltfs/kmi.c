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
** FILE NAME:       libltfs/kmi.c
**
** DESCRIPTION:     Key manager interface API implementation.
**
** AUTHOR:          Yutaka Oishi
**                  IBM Yamato, Japan
**                  oishi@jp.ibm.com
**
*************************************************************************************
*/

#include "ltfs_fuse.h"
#include "kmi.h"

struct kmi_priv {
	void *dlopen_handle;           /**< Handle returned from dlopen */
	struct libltfs_plugin *plugin; /**< Reference to the plugin */
	struct kmi_ops *ops;           /**< Key manager interface operations */
	void *backend_handle;          /**< Backend private data */
};

/**
 * Initialize the key manager interface.
 * @param plugin The plugin to take key manager interface operations from.
 * @param vol LTFS volume
 * @return on success, 0 is returned and the key manager interface handle is stored in the ltfs_volume
 * structure. On failure a negative value is returned.
 */
int kmi_init(struct libltfs_plugin * const plugin, struct ltfs_volume * const vol)
{
	unsigned int i;
	struct kmi_priv *priv;

	CHECK_ARG_NULL(plugin, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);

	priv = calloc(1, sizeof(struct kmi_priv));
	if (! priv) {
		ltfsmsg(LTFS_ERR, "10001E", "kmi_init: private data");
		return -LTFS_NO_MEMORY;
	}

	priv->plugin = plugin;
	priv->ops = plugin->ops;

	/* Verify that backend implements all required operations */
	for (i=0; i<sizeof(struct kmi_ops)/sizeof(void *); ++i) {
		if (((void **)(priv->ops))[i] == NULL) {
			ltfsmsg(LTFS_ERR, "17174E");
			free(priv);
			return -LTFS_PLUGIN_INCOMPLETE;
		}
	}

	priv->backend_handle = priv->ops->init(vol);
	if (! priv->backend_handle) {
		free(priv);
		return -1;
	}

	vol->kmi_handle = priv;
	return 0;
}

/**
 * Destroy the key manager interface.
 * @param vol LTFS volume
 * @return 0 on success or a negative value on error.
 */
int kmi_destroy(struct ltfs_volume * const vol)
{
	struct kmi_priv *priv = (struct kmi_priv *) vol ? vol->kmi_handle : NULL;
	int ret;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->destroy, -LTFS_NULL_ARG);

	ret = priv->ops->destroy(priv->backend_handle);
	vol->kmi_handle = NULL;
	free(priv);

	return ret;
}

/**
 * Checks if the key manager interface has been initialized for the given volume
 * @param vol LTFS volume
 * @return true to indicate that the key manager interface has been initialized or false if not
 */
bool kmi_initialized(const struct ltfs_volume * const vol)
{
	CHECK_ARG_NULL(vol, false);
	return vol->kmi_handle;
}

/**
 * Get Key
 * @param keyalias Get key of the key-alias. If *keyalias is NULL, get key of default key-alias
 * @param key Memory is allocated and key is stored at the address.
 * @param kmi_handle Key manager interface handle
 * @return 0 on success or a negative value on error.
 */
int kmi_get_key(unsigned char **keyalias, unsigned char **key, void * const kmi_handle)
{
	struct kmi_priv *priv = (struct kmi_priv *) kmi_handle;

	CHECK_ARG_NULL(keyalias, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(key, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->get_key, -LTFS_NULL_ARG);

	return priv->ops->get_key(keyalias, key, priv->backend_handle);
}

/**
 * Print the backend's LTFS help message.
 * @param ops key manager interface operations for the backend
 */
int kmi_print_help_message(const struct kmi_ops * const ops)
{
	int ret = 0;

	if (! ops) {
		ltfsmsg(LTFS_WARN, "10006W", "ops", __FUNCTION__);
		return -LTFS_NULL_ARG;
	}

	if (ops->help_message)
		ret = ops->help_message();

	return ret;
}

int kmi_parse_opts(void * const kmi_handle, void *opt_args)
{
	struct kmi_priv *priv = (struct kmi_priv *) kmi_handle;
	int ret;

	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(opt_args, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->parse_opts, -LTFS_NULL_ARG);

	ret = priv->ops->parse_opts(opt_args);
	if (ret < 0)
		/* Cannot parse backend options: backend call failed (%d) */
		ltfsmsg(LTFS_ERR, "12040E", ret);

	return ret;
}
