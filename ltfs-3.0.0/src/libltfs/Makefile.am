#
#  %Z% %I% %W% %G% %U%
#
#  ZZ_Copyright_BEGIN
#
#
#  Licensed Materials - Property of IBM
#
#  IBM Linear Tape File System Single Drive Edition Version 2.2.0.2 for Linux and Mac OS X
#
#  Copyright IBM Corp. 2010, 2014
#
#  This file is part of the IBM Linear Tape File System Single Drive Edition for Linux and Mac OS X
#  (formally known as IBM Linear Tape File System)
#
#  The IBM Linear Tape File System Single Drive Edition for Linux and Mac OS X is free software;
#  you can redistribute it and/or modify it under the terms of the GNU Lesser
#  General Public License as published by the Free Software Foundation,
#  version 2.1 of the License.
#
#  The IBM Linear Tape File System Single Drive Edition for Linux and Mac OS X is distributed in the
#  hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
#  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#  See the GNU Lesser General Public License for more details.
#
#  You should have received a copy of the GNU Lesser General Public
#  License along with this library; if not, write to the Free Software
#  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
#  or download the license from <http://www.gnu.org/licenses/>.
#
#
#  ZZ_Copyright_END
#

lib_LTLIBRARIES = libltfs.la

noinst_HEADERS =
nobase_pkginclude_HEADERS = ltfs.h tape_ops.h ltfs_types.h ltfslogging.h ltfstrace.h ltfs_thread.h ltfs_locking.h queue.h plugin.h config_file.h ltfs_error.h

libltfs_la_SOURCES = \
	ltfs.c \
	ltfs_internal.c \
	ltfs_fsops.c \
	ltfs_fsops_raw.c \
	fs.c \
	xml_common.c \
	xml_writer.c \
	xml_reader.c \
	xml_writer_libltfs.c \
	xml_reader_libltfs.c \
	label.c \
	base64.c \
	tape.c \
	iosched.c \
	dcache.c \
	kmi.c \
	pathname.c \
	index_criteria.c \
	xattr.c \
	ltfslogging.c \
	ltfstrace.c \
	config_file.c \
	plugin.c \
	periodic_sync.c \
	arch/uuid_internal.c \
	arch/filename_handling.c \
	arch/time_internal.c \
	arch/arch_info.c \
	arch/errormap.c

libltfs_la_DEPENDENCIES = ../../messages/libltfs_dat.o ../../messages/internal_error_dat.o
libltfs_la_LIBADD = ../../messages/libltfs_dat.o ../../messages/internal_error_dat.o
libltfs_la_CPPFLAGS = @AM_CPPFLAGS@ -I ..
libltfs_la_LDFLAGS = @AM_LDFLAGS@

install-data-local:
	if [ ! -d "$(prefix)/share/snmp" ]; then \
		mkdir -p "$(prefix)/share/snmp"; \
		cp LtfsSnmpTrapDef.txt LTFS-MIB.txt "$(prefix)/share/snmp/"; \
	elif [ ! -f "$(prefix)/share/snmp/LtfsSnmpTrapDef.txt" ]; then \
		cp LtfsSnmpTrapDef.txt "$(prefix)/share/snmp/LtfsSnmpTrapDef.txt"; \
	elif [ ! -f "$(prefix)/share/snmp/LTFS-MIB.txt" ]; then \
		cp LTFS-MIB.txt "$(prefix)/share/snmp/LTFS-MIB.txt"; \
	fi
