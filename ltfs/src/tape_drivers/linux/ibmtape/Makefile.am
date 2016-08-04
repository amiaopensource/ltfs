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

lib_LTLIBRARIES = libdriver-ibmtape.la
libdir = @libdir@/ltfs

AM_LIBTOOLFLAGS = --tag=disable-static

libdriver_ibmtape_la_SOURCES = ibmtape_cmn.c ibmtape_tc.c
libdriver_ibmtape_la_DEPENDENCIES = ../../../../messages/driver_linux_ibmtape_dat.o ./libdriver_ibmtape_la-reed_solomon_crc.lo ./libdriver_ibmtape_la-crc32c_crc.lo
libdriver_ibmtape_la_LIBADD = ../../../../messages/driver_linux_ibmtape_dat.o ./libdriver_ibmtape_la-reed_solomon_crc.lo ./libdriver_ibmtape_la-crc32c_crc.lo
libdriver_ibmtape_la_LDFLAGS = -avoid-version -module
libdriver_ibmtape_la_CPPFLAGS = @AM_CPPFLAGS@ -I ../../.. -I ../..

libdriver_ibmtape_la-reed_solomon_crc.lo: ../../reed_solomon_crc.c
	$(LIBTOOL)  --tag=CC $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --mode=compile $(CC) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(libdriver_ibmtape_la_CPPFLAGS) $(CPPFLAGS) $(AM_CFLAGS) $(CFLAGS) $(CRC_OPTIMIZE) -MT libdriver_ibmtape_la-reed_solomon_crc.lo -MD -MP -c -o libdriver_ibmtape_la-reed_solomon_crc.lo $<

libdriver_ibmtape_la-crc32c_crc.lo: ../../crc32c_crc.c
	$(LIBTOOL)  --tag=CC $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --mode=compile $(CC) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(libdriver_ibmtape_la_CPPFLAGS) $(CPPFLAGS) $(AM_CFLAGS) $(CFLAGS) $(CRC_OPTIMIZE) -MT libdriver_ibmtape_la-crc32c_crc.lo -MD -MP -c -o libdriver_ibmtape_la-crc32c_crc.lo $<

install-exec-hook:
	for f in $(lib_LTLIBRARIES); do rm -f $(libdir)/$$f; done
