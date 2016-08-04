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
** FILE NAME:       label.c
**
** DESCRIPTION:     Implements label handling functions.
**
** AUTHOR:          Takashi Ashida
**                  IBM Yamato, Japan
**                  ashida@jp.ibm.com
**
*************************************************************************************
*/

#include "label.h"
#include "libltfs/ltfslogging.h"
#include "ltfs_internal.h"

/**
 * Allocate label.
 * @param label To be set the allocated label pointer
 * @return 0 if labels match or a negative value otherwise.
 */
int label_alloc(struct ltfs_label **label)
{
	struct ltfs_label *newlabel;

	CHECK_ARG_NULL(label, -LTFS_NULL_ARG);

	newlabel = calloc(1, sizeof(struct ltfs_label));
	if (!newlabel) {
		ltfsmsg(LTFS_ERR, "10001E", __FUNCTION__);
		return -LTFS_NO_MEMORY;
	}
	newlabel->version = LTFS_LABEL_VERSION;

	*label = newlabel;
	return 0;
}

/**
 * Free label
 * @param label label to be free
 */
void label_free(struct ltfs_label **label)
{
	if (label && *label) {
		if ((*label)->creator)
			free((*label)->creator);
		free(*label);
		*label = NULL;
	}
}

/**
 * Check whether two labels are equal.
 * @param label1 the first label
 * @param label2 the second label
 * @return 0 if labels match or a negative value otherwise.
 */
int label_compare(struct ltfs_label *label1, struct ltfs_label *label2)
{
	char *tmp;

	CHECK_ARG_NULL(label1, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(label2, -LTFS_NULL_ARG);

	if (strncmp(label1->barcode, label2->barcode, 6)) {
		ltfsmsg(LTFS_ERR, "11182E");
		return -LTFS_LABEL_MISMATCH;

	} else if (strncmp(label1->vol_uuid, label2->vol_uuid, 36)) {
		ltfsmsg(LTFS_ERR, "11183E");
		return -LTFS_LABEL_MISMATCH;

	} else if (label1->format_time.tv_sec != label2->format_time.tv_sec ||
		label1->format_time.tv_nsec != label2->format_time.tv_nsec) {
		ltfsmsg(LTFS_ERR, "11184E");
		return -LTFS_LABEL_MISMATCH;

	} else if (label1->blocksize != label2->blocksize) {
		ltfsmsg(LTFS_ERR, "11185E");
		return -LTFS_LABEL_MISMATCH;

	} else if (label1->enable_compression != label2->enable_compression) {
		ltfsmsg(LTFS_ERR, "11186E");
		return -LTFS_LABEL_MISMATCH;

	} else if (! ltfs_is_valid_partid(label1->partid_dp) ||
			   ! ltfs_is_valid_partid(label1->partid_ip)) {
		ltfsmsg(LTFS_ERR, "11187E");
		return -LTFS_LABEL_MISMATCH;

	} else if (label1->partid_dp == label1->partid_ip) {
		ltfsmsg(LTFS_ERR, "11188E");
		return -LTFS_LABEL_MISMATCH;

	} else if (label2->partid_dp != label1->partid_dp ||
			   label2->partid_ip != label1->partid_ip) {
		ltfsmsg(LTFS_ERR, "11189E");
		return -LTFS_LABEL_MISMATCH;

	} else if ((label1->this_partition != label1->partid_dp &&
			    label1->this_partition != label1->partid_ip) ||
			   (label2->this_partition != label1->partid_dp &&
			    label2->this_partition != label1->partid_ip)) {
		ltfsmsg(LTFS_ERR, "11190E");
		return -LTFS_LABEL_MISMATCH;

	} else if (label1->this_partition == label2->this_partition) {
		ltfsmsg(LTFS_ERR, "11191E", label1->this_partition);
		return -LTFS_LABEL_MISMATCH;

	} else if (label1->version != label2->version) {
		ltfsmsg(LTFS_ERR, "11197E");
		return -LTFS_LABEL_MISMATCH;
	}

	/* check for valid barcode number */
	if (label1->barcode[0] != ' ') {
		tmp = label1->barcode;
		while (*tmp) {
			if ((*tmp < '0' || *tmp > '9') && (*tmp < 'A' || *tmp > 'Z')) {
				ltfsmsg(LTFS_ERR, "11192E");
				return -LTFS_LABEL_MISMATCH;
			}
			++tmp;
		}
	}

	return 0;
}

/**
 * Generate an 80-byte ANSI label.
 * @param vol LTFS volume containing the barcode number 
 * @param label output buffer, must be at least 80 bytes
 */
void label_make_ansi_label(struct ltfs_volume *vol, char *label, size_t size)
{
	size_t barcode_len;
	memset(label,' ',size);
	memcpy(label,"VOL1",4);
	barcode_len = strlen(vol->label->barcode);
	if (barcode_len > 0)
		memcpy(label+4, vol->label->barcode, barcode_len > 6 ? 6 : barcode_len);
	label[10] = 'L';
	memcpy(label+24,"LTFS",4);
	/* TODO: fill "owner identifier" field? */
	label[size-1] = '4';
}

