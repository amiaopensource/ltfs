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
#
#   (C) Copyright 2015 Hewlett Packard Enterprise Development LP.
#   05/28/10 Add hptape backend to list of platform drivers
#   09/23/10 Rename hptape to more generic ltotape
#
#

bin_PROGRAMS = ltfs

ltfs_SOURCES = main.c ltfs_fuse.c
ltfs_DEPENDENCIES = libltfs/libltfs.la ../messages/bin_ltfs_dat.o
ltfs_CPPFLAGS = @AM_CPPFLAGS@ -I ../ltfs-sde/src
ltfs_LDADD = libltfs/libltfs.la ../messages/bin_ltfs_dat.o

# platform_drivers = tape_drivers/linux/ibmtape
# generic_drivers = tape_drivers/generic/file tape_drivers/generic/itdtimg
# SUBDIRS = $(generic_drivers) $(platform_drivers) iosched kmi libltfs utils
platform_drivers = tape_drivers/linux/ltotape
SUBDIRS = $(platform_drivers) iosched kmi libltfs utils
