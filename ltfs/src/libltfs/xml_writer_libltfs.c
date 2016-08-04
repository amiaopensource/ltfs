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
** FILE NAME:       xml_writer.c
**
** DESCRIPTION:     XML writer routines for Indexes and Labels.
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

#include "ltfs.h"
#include "xml_libltfs.h"
#include "fs.h"
#include "tape.h"
#include "pathname.h"
#include "arch/time_internal.h"

int _xml_write_schema(xmlTextWriterPtr writer, const char *creator,
	const struct ltfs_index *idx);
int _xml_write_dirtree(xmlTextWriterPtr writer, struct dentry *dir,
	const struct ltfs_index *idx);
int _xml_write_file(xmlTextWriterPtr writer, const struct dentry *file);
int _xml_write_dentry_times(xmlTextWriterPtr writer, const struct dentry *d);
int _xml_write_xattr(xmlTextWriterPtr writer, const struct dentry *file);

/**
 * Generate an XML tape label.
 * @param partition the partition number to which the label will be written
 * @param label data structure containing format parameters
 * @return buffer containing the label, which the caller should free using xmlBufferFree
 */
xmlBufferPtr xml_make_label(const char *creator, tape_partition_t partition,
	const struct ltfs_label *label)
{
	int ret;
	char *fmt_time;
	xmlBufferPtr buf = NULL;
	xmlTextWriterPtr writer;

	CHECK_ARG_NULL(creator, NULL);
	CHECK_ARG_NULL(label, NULL);

	buf = xmlBufferCreate();
	if (!buf) {
		ltfsmsg(LTFS_ERR, "17047E");
		return NULL;
	}

	writer = xmlNewTextWriterMemory(buf, 0);
	if (!writer) {
		ltfsmsg(LTFS_ERR, "17043E");
		return NULL;
	}

	ret = xmlTextWriterStartDocument(writer, NULL, "UTF-8", NULL);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "17044E", ret);
		return NULL;
	}

	xmlTextWriterSetIndent(writer, 1);
	xmlTextWriterSetIndentString(writer, BAD_CAST "    ");

	/* write tags */
	xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "ltfslabel"), NULL);
	xml_mktag(xmlTextWriterWriteAttribute(writer, BAD_CAST "version",
		BAD_CAST LTFS_LABEL_VERSION_STR), NULL);
	xml_mktag(xmlTextWriterWriteElement(writer, BAD_CAST "creator", BAD_CAST creator), NULL);

	ret = xml_format_time(label->format_time, &fmt_time);
	if (!fmt_time) {
		ltfsmsg(LTFS_ERR, "17045E");
		return NULL;
	} else if (ret == LTFS_TIME_OUT_OF_RANGE)
		ltfsmsg(LTFS_WARN, "17223W", "formattime", label->format_time.tv_sec);

	xml_mktag(xmlTextWriterWriteElement(writer, BAD_CAST "formattime", BAD_CAST fmt_time), NULL);
	free(fmt_time);
	xml_mktag(xmlTextWriterWriteElement(
		writer, BAD_CAST "volumeuuid", BAD_CAST label->vol_uuid), NULL);
    xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "location"), NULL);
	xml_mktag(xmlTextWriterWriteFormatElement(
		writer, BAD_CAST "partition", "%c", label->part_num2id[partition]), NULL);
	xml_mktag(xmlTextWriterEndElement(writer), NULL);
	xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "partitions"), NULL);
	xml_mktag(xmlTextWriterWriteFormatElement(
		writer, BAD_CAST "index", "%c", label->partid_ip), NULL);
	xml_mktag(xmlTextWriterWriteFormatElement(
		writer, BAD_CAST "data", "%c", label->partid_dp), NULL);
	xml_mktag(xmlTextWriterEndElement(writer), NULL);
	xml_mktag(xmlTextWriterWriteFormatElement(
		writer, BAD_CAST "blocksize", "%ld", label->blocksize), NULL);
	xml_mktag(xmlTextWriterWriteElement(
		writer, BAD_CAST "compression", BAD_CAST (label->enable_compression ? "true" : "false")),
		NULL);
	xml_mktag(xmlTextWriterEndElement(writer), NULL);

	ret = xmlTextWriterEndDocument(writer);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "17046E", ret);
		return NULL;
	}

	xmlFreeTextWriter(writer);
	return buf;
}

/**
 * Create an XML schema in memory.
 * @param priv LTFS data
 * @return buffer containing the index, which the caller should free using xmlBufferFree
 */
xmlBufferPtr xml_make_schema(const char *creator, const struct ltfs_index *idx)
{
	xmlBufferPtr buf = NULL;
	xmlTextWriterPtr writer;

	CHECK_ARG_NULL(creator, NULL);
	CHECK_ARG_NULL(idx, NULL);

	buf = xmlBufferCreate();
	if (!buf) {
		ltfsmsg(LTFS_ERR, "17048E");
		return NULL;
	}

	writer = xmlNewTextWriterMemory(buf, 0);
	if (!writer) {
		ltfsmsg(LTFS_ERR, "17049E");
		return NULL;
	}

	if (_xml_write_schema(writer, creator, idx) < 0) {
		ltfsmsg(LTFS_ERR, "17050E");
		xmlBufferFree(buf);
		buf = NULL;
	}
	xmlFreeTextWriter(writer);
	return buf;
}

/**
 * Generate an XML schema file based on the priv->root directory tree.
 * @param filename output XML file
 * @param priv ltfs private data
 * @return 0 on success or a negative value on error.
 */
int xml_schema_to_file(const char *filename, const char *creator
					   , const char *reason, const struct ltfs_index *idx)
{
	xmlTextWriterPtr writer;
	int ret;
	char *alt_creator = NULL;

	CHECK_ARG_NULL(creator, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(idx, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(filename, -LTFS_NULL_ARG);

	writer = xmlNewTextWriterFilename(filename, 0);
	if (! writer) {
		ltfsmsg(LTFS_ERR, "17051E", filename);
		return -1;
	}

	if (reason)
		asprintf(&alt_creator, "%s - %s", creator , reason);
	else
		alt_creator = strdup(creator);

	if (alt_creator) {
		ret = _xml_write_schema(writer, alt_creator, idx);
		if (ret < 0)
			ltfsmsg(LTFS_ERR, "17052E", ret, filename);
		xmlFreeTextWriter(writer);
		free(alt_creator);
	} else {
		ltfsmsg(LTFS_ERR, "10001E", "xml_schema_to_file: alt creator string");
		return -1;
	}

	return ret;
}

/**
 * Generate an XML Index based on the vol->index->root directory tree.
 * The generated data are written directly to the tape with the appropriate blocksize.
 * @param vol LTFS volume.
 * @return 0 on success or a negative value on error.
 */
int xml_schema_to_tape(char *reason, struct ltfs_volume *vol)
{
	int ret;
	xmlOutputBufferPtr write_buf;
	xmlTextWriterPtr writer;
	struct xml_output_tape *out_ctx;
	char *creator = NULL;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(reason, -LTFS_NULL_ARG);

	/* Create output callback context data structure. */
	out_ctx = malloc(sizeof(struct xml_output_tape));
	if (! out_ctx) {
		ltfsmsg(LTFS_ERR, "10001E", "xml_schema_to_tape: output context");
		return -LTFS_NO_MEMORY;
	}
#if 0
	out_ctx->buf = malloc(vol->label->blocksize + LTFS_CRC_SIZE);
#endif /* 0 */
	out_ctx->buf = malloc(vol->label->blocksize);
	if (! out_ctx->buf) {
		ltfsmsg(LTFS_ERR, "10001E", "xml_schema_to_tape: output buffer");
		free(out_ctx);
		return -LTFS_NO_MEMORY;
	}
	out_ctx->buf_size = vol->label->blocksize;
	out_ctx->buf_used = 0;
	out_ctx->device = vol->device;

	/* Create output buffer pointer. */
	write_buf = xmlOutputBufferCreateIO(xml_output_tape_write_callback,
										xml_output_tape_close_callback,
										out_ctx, NULL);
	if (! write_buf) {
		ltfsmsg(LTFS_ERR, "17053E");
		free(out_ctx->buf);
		free(out_ctx);
		return -1;
	}

	/* Create XML writer. */
	writer = xmlNewTextWriter(write_buf);
	if (! writer) {
		ltfsmsg(LTFS_ERR, "17054E");
		xmlOutputBufferClose(write_buf);
		return -1;
	}

	/* Generate the Index. */
	asprintf(&creator, "%s - %s", vol->creator, reason);
	if (creator) {
		ret = _xml_write_schema(writer, creator, vol->index);
		if (ret < 0)
			ltfsmsg(LTFS_ERR, "17055E", ret);
		xmlFreeTextWriter(writer);
		free(creator);

		/* Update the creator string */
		if (! vol->index->creator || strcmp(vol->creator, vol->index->creator)) {
			if (vol->index->creator)
				free(vol->index->creator);
			vol->index->creator = strdup(vol->creator);
			if (! vol->index->creator) {
				ltfsmsg(LTFS_ERR, "10001E", "xml_schema_to_tape: new creator string");
				return -1;
			}
		}
	} else {
		ltfsmsg(LTFS_ERR, "10001E", "xml_schema_to_tape: creator string");
		return -1;
	}

	return ret;
}

/**
 * Generate an XML schema, sending it to a user-provided output (memory or file).
 * Note: this function does very little input validation; any user-provided information
 * must be verified by the caller.
 * @param writer the XML writer to send output to
 * @param priv LTFS data
 * @param pos position on tape where the schema will be written
 * @return 0 on success, negative on failure
 */
int _xml_write_schema(xmlTextWriterPtr writer, const char *creator,
	const struct ltfs_index *idx)
{
	int ret;
	size_t i;
	char *update_time, **name_criteria;

	ret = xml_format_time(idx->mod_time, &update_time);
	if (!update_time)
		return -1;
	else if (ret == LTFS_TIME_OUT_OF_RANGE)
		ltfsmsg(LTFS_WARN, "17224W", "modifytime", idx->mod_time.tv_sec);

	ret = xmlTextWriterStartDocument(writer, NULL, "UTF-8", NULL);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "17057E", ret);
		return -1;
	}

	xmlTextWriterSetIndent(writer, 1);
	/* Define INDENT_INDEXES to write Indexes to tape with full indentation.
	 * This is normally a waste of space, but it may be useful for debugging. */
#ifdef INDENT_INDEXES
	xmlTextWriterSetIndentString(writer, BAD_CAST "    ");
#else
	xmlTextWriterSetIndentString(writer, BAD_CAST "");
#endif

	/* write index properties */
	xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "ltfsindex"), -1);
	xml_mktag(xmlTextWriterWriteAttribute(writer, BAD_CAST "version",
		BAD_CAST LTFS_INDEX_VERSION_STR), -1);
	xml_mktag(xmlTextWriterWriteElement(writer, BAD_CAST "creator", BAD_CAST creator), -1);
	if (idx->commit_message && strlen(idx->commit_message)) {
		xml_mktag(xmlTextWriterWriteFormatElement(writer, BAD_CAST "comment",
			"%s", BAD_CAST (idx->commit_message)), -1);
	}
	xml_mktag(xmlTextWriterWriteElement(writer, BAD_CAST "volumeuuid", BAD_CAST idx->vol_uuid), -1);
	xml_mktag(xmlTextWriterWriteFormatElement(
		writer, BAD_CAST "generationnumber", "%u", idx->generation), -1);
	xml_mktag(xmlTextWriterWriteElement(writer, BAD_CAST "updatetime", BAD_CAST update_time), -1);
	xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "location"), -1);
	xml_mktag(xmlTextWriterWriteFormatElement(
		writer, BAD_CAST "partition", "%c", idx->selfptr.partition), -1);
	xml_mktag(xmlTextWriterWriteFormatElement(
		writer, BAD_CAST "startblock", "%"PRIu64, idx->selfptr.block), -1);
	xml_mktag(xmlTextWriterEndElement(writer), -1);
	if (idx->backptr.block) {
		xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "previousgenerationlocation"), -1);
		xml_mktag(xmlTextWriterWriteFormatElement(
			writer, BAD_CAST "partition", "%c", idx->backptr.partition), -1);
		xml_mktag(xmlTextWriterWriteFormatElement(
			writer, BAD_CAST "startblock", "%"PRIu64, idx->backptr.block), -1);
		xml_mktag(xmlTextWriterEndElement(writer), -1);
	}
	xml_mktag(xmlTextWriterWriteElement(writer, BAD_CAST "allowpolicyupdate",
		BAD_CAST (idx->criteria_allow_update ? "true" : "false")), -1);
	if (idx->original_criteria.have_criteria) {
		xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "dataplacementpolicy"), -1);
		xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "indexpartitioncriteria"), -1);
		xml_mktag(xmlTextWriterWriteFormatElement(writer, BAD_CAST "size", "%"PRIu64,
			idx->original_criteria.max_filesize_criteria), -1);
		if (idx->original_criteria.glob_patterns) {
			name_criteria = idx->original_criteria.glob_patterns;
			while (*name_criteria && **name_criteria) {
				xml_mktag(xmlTextWriterWriteElement(writer, BAD_CAST "name",
					BAD_CAST (*name_criteria)), -1);
				++name_criteria;
			}
		}
		xml_mktag(xmlTextWriterEndElement(writer), -1);
		xml_mktag(xmlTextWriterEndElement(writer), -1);
	}
	xml_mktag(xmlTextWriterWriteFormatElement(
		writer, BAD_CAST NEXTUID_TAGNAME, "%"PRIu64, idx->uid_number), -1);

	xml_mktag(_xml_write_dirtree(writer, idx->root, idx), -1);

	/* Save unrecognized tags */
	if (idx->tag_count > 0) {
		for (i=0; i<idx->tag_count; ++i) {
			if (xmlTextWriterWriteRaw(writer, idx->preserved_tags[i]) < 0) {
				ltfsmsg(LTFS_ERR, "17092E", __FUNCTION__);
				return -1;
			}
		}
	}

	xml_mktag(xmlTextWriterEndElement(writer), -1);
	ret = xmlTextWriterEndDocument(writer);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "17058E", ret);
		return -1;
	}

	free(update_time);
	return 0;
}

/**
 * Write XML tags representing the current directory tree to the given destination.
 * @param writer output pointer
 * @param dir directory to process
 * @param priv LTFS data
 * @return 0 on success or negative on failure
 */
int _xml_write_dirtree(xmlTextWriterPtr writer, struct dentry *dir,
	const struct ltfs_index *idx)
{
	size_t i;
	struct name_list *list_ptr, *list_tmp;

	if (!dir)
		return 0; /* nothing to do */

	/* write standard attributes */
	xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "directory"), -1);
	if (dir == idx->root) {
		if (idx->volume_name) {
			xml_mktag(
				xmlTextWriterWriteElement(writer, BAD_CAST "name", BAD_CAST idx->volume_name),
				-1
				);
		} else {
			xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "name"), -1);
			xml_mktag(xmlTextWriterEndElement(writer), -1);
		}
	} else
		xml_mktag(xmlTextWriterWriteElement(writer, BAD_CAST "name", BAD_CAST dir->name), -1);
	xml_mktag(xmlTextWriterWriteElement(
		writer, BAD_CAST "readonly", BAD_CAST (dir->readonly ? "true" : "false")), -1);
	xml_mktag(_xml_write_dentry_times(writer, dir), -1);
	xml_mktag(xmlTextWriterWriteFormatElement(
		writer, BAD_CAST UID_TAGNAME, "%"PRIu64, dir->uid), -1);

	/* write extended attributes */
	xml_mktag(_xml_write_xattr(writer, dir), -1);

	/* write children */
	xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "contents"), -1);
	/* Sort dentries by UID before generating xml */
	HASH_SORT(dir->child_list, fs_hash_sort_by_uid);

	HASH_ITER(hh, dir->child_list, list_ptr, list_tmp) {
		if (list_ptr->d->isdir)
			xml_mktag(_xml_write_dirtree(writer, list_ptr->d, idx), -1);
		else
			xml_mktag(_xml_write_file(writer, list_ptr->d), -1);
	}

	xml_mktag(xmlTextWriterEndElement(writer), -1);

	/* Save unrecognized tags */
	if (dir->tag_count > 0) {
		for (i=0; i<dir->tag_count; ++i) {
			if (xmlTextWriterWriteRaw(writer, dir->preserved_tags[i]) < 0) {
				ltfsmsg(LTFS_ERR, "17092E", __FUNCTION__);
				return -1;
			}
		}
	}

	xml_mktag(xmlTextWriterEndElement(writer), -1);
	return 0;
}

/**
 * Write file info to an XML stream.
 * @param writer output pointer
 * @param file the file to write
 * @return 0 on success or -1 on failure
 */
int _xml_write_file(xmlTextWriterPtr writer, const struct dentry *file)
{
	struct extent_info *extent;
	size_t i;

	if (file->isdir) {
		ltfsmsg(LTFS_ERR, "17062E");
		return -1;
	}

	/* write standard attributes */
	xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "file"), -1);
	xml_mktag(xmlTextWriterWriteElement(writer, BAD_CAST "name", BAD_CAST file->name), -1);
	xml_mktag(xmlTextWriterWriteFormatElement(
		writer, BAD_CAST "length", "%"PRIu64, file->size), -1);
	xml_mktag(xmlTextWriterWriteElement(
		writer, BAD_CAST "readonly", BAD_CAST (file->readonly ? "true" : "false")), -1);
	xml_mktag(_xml_write_dentry_times(writer, file), -1);
	xml_mktag(xmlTextWriterWriteFormatElement(
		writer, BAD_CAST UID_TAGNAME, "%"PRIu64, file->uid), -1);

	/* write extended attributes */
	xml_mktag(_xml_write_xattr(writer, file), -1);

	/* write extents */
    if (file->isslink) {
        xml_mktag(xmlTextWriterWriteElement(writer, BAD_CAST "symlink", BAD_CAST file->target), -1);
    }
	else if (! TAILQ_EMPTY(&file->extentlist)) {
		xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "extentinfo"), -1);
		TAILQ_FOREACH(extent, &file->extentlist, list) {
			xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "extent"), -1);
			xml_mktag(xmlTextWriterWriteFormatElement(
				writer, BAD_CAST "fileoffset", "%"PRIu64, extent->fileoffset), -1);
			xml_mktag(xmlTextWriterWriteFormatElement(
				writer, BAD_CAST "partition", "%c", extent->start.partition), -1);
			xml_mktag(xmlTextWriterWriteFormatElement(
				writer, BAD_CAST "startblock", "%"PRIu64, extent->start.block), -1);
			xml_mktag(xmlTextWriterWriteFormatElement(
				writer, BAD_CAST "byteoffset", "%"PRIu32, extent->byteoffset), -1);
			xml_mktag(xmlTextWriterWriteFormatElement(
				writer, BAD_CAST "bytecount", "%"PRIu64, extent->bytecount), -1);
			xml_mktag(xmlTextWriterEndElement(writer), -1);
		}
		xml_mktag(xmlTextWriterEndElement(writer), -1);
	}

	/* Save unrecognized tags */
	if (file->tag_count > 0) {
		for (i=0; i<file->tag_count; ++i) {
			if (xmlTextWriterWriteRaw(writer, file->preserved_tags[i]) < 0) {
				ltfsmsg(LTFS_ERR, "17092E", __FUNCTION__);
				return -1;
			}
		}
	}

	xml_mktag(xmlTextWriterEndElement(writer), -1);
	return 0;
}

/**
 * Write time info into an XML stream.
 * @param write output pointer
 * @param d dentry to get times from
 * @return 0 on success or a negative value on error.
 */
int _xml_write_dentry_times(xmlTextWriterPtr writer, const struct dentry *d)
{
	int ret;
	char *mtime;

	ret = xml_format_time(d->creation_time, &mtime);
	if (!mtime)
		return -1;
	else if (ret == LTFS_TIME_OUT_OF_RANGE)
		ltfsmsg(LTFS_WARN, "17225W", "creationtime", d->creation_time.tv_sec);
	xml_mktag(xmlTextWriterWriteElement(writer, BAD_CAST "creationtime", BAD_CAST mtime), -1);
	free(mtime);

	ret = xml_format_time(d->change_time, &mtime);
	if (!mtime)
		return -1;
	else if (ret == LTFS_TIME_OUT_OF_RANGE)
		ltfsmsg(LTFS_WARN, "17225W", "changetime", d->change_time.tv_sec);
	xml_mktag(xmlTextWriterWriteElement(writer, BAD_CAST "changetime", BAD_CAST mtime), -1);
	free(mtime);

	ret = xml_format_time(d->modify_time, &mtime);
	if (!mtime)
		return -1;
	else if (ret == LTFS_TIME_OUT_OF_RANGE)
		ltfsmsg(LTFS_WARN, "17225W", "modifytime", d->modify_time.tv_sec);
	xml_mktag(xmlTextWriterWriteElement(writer, BAD_CAST "modifytime", BAD_CAST mtime), -1);
	free(mtime);

	ret = xml_format_time(d->access_time, &mtime);
	if (!mtime)
		return -1;
	else if (ret == LTFS_TIME_OUT_OF_RANGE)
		ltfsmsg(LTFS_WARN, "17225W", "accesstime", d->access_time.tv_sec);
	xml_mktag(xmlTextWriterWriteElement(writer, BAD_CAST "accesstime", BAD_CAST mtime), -1);
	free(mtime);

	ret = xml_format_time(d->backup_time, &mtime);
	if (!mtime)
		return -1;
	else if (ret == LTFS_TIME_OUT_OF_RANGE)
		ltfsmsg(LTFS_WARN, "17225W", "backuptime", d->backup_time.tv_sec);
	xml_mktag(xmlTextWriterWriteElement(writer, BAD_CAST "backuptime", BAD_CAST mtime), -1);
	free(mtime);

	return 0;
}

/**
 * Write extended attributes from the given file or directory.
 * @param writer output pointer
 * @param file the dentry to take xattrs from
 * @return 0 on success or -1 on failure
 */
int _xml_write_xattr(xmlTextWriterPtr writer, const struct dentry *file)
{
	int ret;
	struct xattr_info *xattr;

	if (! TAILQ_EMPTY(&file->xattrlist)) {
		xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "extendedattributes"), -1);
		TAILQ_FOREACH(xattr, &file->xattrlist, list) {
			xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "xattr"), -1);
			xml_mktag(
				xmlTextWriterWriteElement(writer, BAD_CAST "key", BAD_CAST xattr->key), -1);
			if (xattr->value) {
				ret = pathname_validate_xattr_value(xattr->value, xattr->size);
				if (ret < 0) {
					ltfsmsg(LTFS_ERR, "17059E", ret);
					return -1;
				} else if (ret > 0) {
					xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "value"), -1);
					xml_mktag(
						xmlTextWriterWriteAttribute(writer, BAD_CAST "type", BAD_CAST "base64"),
						-1);
					xml_mktag(xmlTextWriterWriteBase64(writer, xattr->value, 0, xattr->size), -1);
					xml_mktag(xmlTextWriterEndElement(writer), -1);
				} else {
					xml_mktag(xmlTextWriterWriteFormatElement(
						writer, BAD_CAST "value", "%.*s", (int)xattr->size, xattr->value), -1);
				}
			} else { /* write empty value tag */
				xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "value"), -1);
				xml_mktag(xmlTextWriterEndElement(writer), -1);
			}
			xml_mktag(xmlTextWriterEndElement(writer), -1);
		}
		xml_mktag(xmlTextWriterEndElement(writer), -1);
	}

	return 0;
}
