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

lib_LTLIBRARIES = libiosched-fcfs.la libiosched-unified.la
libdir = @libdir@/ltfs

AM_LIBTOOLFLAGS = --tag=disable-static

libiosched_fcfs_la_SOURCES = fcfs.c
libiosched_fcfs_la_LDFLAGS = -avoid-version -module @AM_LDFLAGS@
libiosched_fcfs_la_DEPENDENCIES = ../../messages/iosched_fcfs_dat.o
libiosched_fcfs_la_LIBADD = ../../messages/iosched_fcfs_dat.o
libiosched_fcfs_la_CPPFLAGS = @AM_CPPFLAGS@ -I ..

libiosched_unified_la_SOURCES = unified.c cache_manager.c
libiosched_unified_la_LDFLAGS = -avoid-version -module @AM_LDFLAGS@
libiosched_unified_la_DEPENDENCIES = ../../messages/iosched_unified_dat.o
libiosched_unified_la_LIBADD = ../../messages/iosched_unified_dat.o
libiosched_unified_la_CPPFLAGS = @AM_CPPFLAGS@ -I ..

install-exec-hook:
	for f in $(lib_LTLIBRARIES); do rm -f $(libdir)/$$f; done
