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

#include <libxml/xmlstring.h>
#include <libxml/xmlwriter.h>

#include "ltfs.h"
#include "xml.h"
#include "fs.h"
#include "tape.h"
#include "pathname.h"
#include "arch/time_internal.h"

/* fsync emulation for MinGW */
#ifdef HP_mingw_BUILD
#include <windows.h>
#define HAVE_FSYNC 1
#define fsync(fd) (FlushFileBuffers ((HANDLE) _get_osfhandle(fd)) ? 0 : -1)
#endif /* HP_mingw_BUILD */

/**
 * Format a raw timespec structure for the XML file.
 */
int xml_format_time(struct ltfs_timespec t, char** out)
{
	char *timebuf;
	struct tm tm, *gmt;
	ltfs_time_t sec;
	int noramized;

	*out = NULL;
	noramized = normalize_ltfs_time(&t);
	sec = t.tv_sec;

	gmt = ltfs_gmtime(&sec, &tm);
	if (! gmt) {
		ltfsmsg(LTFS_ERR, "17056E");
		return -1;
	}

	timebuf = calloc(31, sizeof(char));
	if (!timebuf) {
		ltfsmsg(LTFS_ERR, "10001E", __FUNCTION__);
		return -1;
	}
	sprintf(timebuf, "%04d-%02d-%02dT%02d:%02d:%02d.%09ldZ", tm.tm_year+1900, tm.tm_mon+1,
			tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, t.tv_nsec);
	*out = timebuf;

	return noramized;
}

/**
 * Write callback for XML output using libxml2's I/O routines. It buffers the data it receives
 * into chunks of 1 tape block each and writes each chunk to the tape.
 */
int xml_output_tape_write_callback(void *context, const char *buffer, int len)
{
	ssize_t ret;
	struct xml_output_tape *ctx = context;
	uint32_t copy_count; /* number of bytes of "buffer" to write immediately */
	uint32_t bytes_remaining; /* number of input bytes waiting to be handled */

	if (len == 0)
		return 0;
	if (ctx->buf_used + len < ctx->buf_size) {
		memcpy(ctx->buf + ctx->buf_used, buffer, len);
		ctx->buf_used += len;
	} else {
		bytes_remaining = len;
		do {
			copy_count = ctx->buf_size - ctx->buf_used;
			memcpy(ctx->buf + ctx->buf_used, buffer + (len - bytes_remaining), copy_count);
			ret = tape_write(ctx->device, ctx->buf, ctx->buf_size, true, true);
			if (ret < 0) {
				ltfsmsg(LTFS_ERR, "17060E", (int)ret);
				return -1;
			}
			ctx->buf_used = 0;
			bytes_remaining -= copy_count;
		} while (bytes_remaining > ctx->buf_size);
		if (bytes_remaining > 0)
			memcpy(ctx->buf, buffer + (len - bytes_remaining), bytes_remaining);
		ctx->buf_used = bytes_remaining;
	}

	return len;
}

/**
 * Close callback for XML output using libxml2's I/O routines. It flushes any partial buffer
 * which might be left after the write callback has received all XML data.
 */
int xml_output_tape_close_callback(void *context)
{
	int ret;
	struct xml_output_tape *ctx = context;

	if (ctx->buf_used > 0)
		ret = tape_write(ctx->device, ctx->buf, ctx->buf_used, true, true);
	else
		ret = 0;
	if (ret < 0)
		ltfsmsg(LTFS_ERR, "17061E", (int)ret);

	free(ctx->buf);
	free(ctx);
	return (ret < 0) ? -1 : 0;
}

/**
 * Write callback for XML output using libxml2's I/O routines for file descriptor.
 */
int xml_output_fd_write_callback(void *context, const char *buffer, int len)
{
	ssize_t ret;
	struct xml_output_fd *ctx = context;

	if (len > 0) {
		ret = write(ctx->fd, buffer, len);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, "17206E", "write callback (write)", errno, len);
			return -1;
		}

		ret = fsync(ctx->fd);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, "17206E", "write callback (fsync)", errno, len);
			return -1;
		}
	}

	return len;
}

/**
 * Close callback for XML output using libxml2's I/O routines. It flushes any partial buffer
 * which might be left after the write callback has received all XML data.
 */
int xml_output_fd_close_callback(void *context)
{
	struct xml_output_fd *ctx = context;

	free(ctx);

	return 0;
}
