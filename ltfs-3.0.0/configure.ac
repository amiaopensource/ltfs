;;
;;  %Z% %I% %W% %G% %U%
;;
;;  ZZ_Copyright_BEGIN
;;
;;
;;  Licensed Materials - Property of IBM
;;
;;  IBM Linear Tape File System Single Drive Edition Version 2.2.0.2 for Linux and Mac OS X
;;
;;  Copyright IBM Corp. 2010, 2014
;;
;;  This file is part of the IBM Linear Tape File System Single Drive Edition for Linux and Mac OS X
;;  (formally known as IBM Linear Tape File System)
;;
;;  The IBM Linear Tape File System Single Drive Edition for Linux and Mac OS X is free software;
;;  you can redistribute it and/or modify it under the terms of the GNU Lesser
;;  General Public License as published by the Free Software Foundation,
;;  version 2.1 of the License.
;;
;;  The IBM Linear Tape File System Single Drive Edition for Linux and Mac OS X is distributed in the
;;  hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
;;  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
;;  See the GNU Lesser General Public License for more details.
;;
;;  You should have received a copy of the GNU Lesser General Public
;;  License along with this library; if not, write to the Free Software
;;  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
;;  or download the license from <http://www.gnu.org/licenses/>.
;;
;;
;;  ZZ_Copyright_END
;;
;;  (C) Copyright 2015 Hewlett Packard Enterprise Development LP.
;;  05/26/10 Added support for HP Tape Backend
;;  06/14/10 Changed version to HP nomenclature
;;
dnl
dnl LTFS configure.ac.
dnl
AC_INIT([LTFS],[3.0.0],[Hewlett-Packard Development Company L.P.])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_AUX_DIR([build-aux])
AC_CANONICAL_HOST
AC_CANONICAL_BUILD
AC_CANONICAL_TARGET
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_PROG_CC
AC_USE_SYSTEM_EXTENSIONS
AC_PROG_CC_C99
AC_PROG_LIBTOOL

dnl
dnl Check for debug
dnl
AC_MSG_CHECKING([whether to compile in debug mode])
AC_ARG_ENABLE([debug],
    [AS_HELP_STRING([--enable-debug],[compile with extra debugging output])],
    [use_debug=$enableval],
    [use_debug=no]
)
AC_MSG_RESULT([$use_debug])

dnl
dnl Check for max speed
dnl
AC_MSG_CHECKING([whether to enable optimizations])
AC_ARG_ENABLE([fast],
	[AS_HELP_STRING([--enable-fast],[compile with optimization enabled])],
	[use_fast=$enableval],
	[use_fast=no]
)
AC_MSG_RESULT([$use_fast])


dnl
dnl Check for livelink enable
dnl
AC_MSG_CHECKING([whether to enable livelink mode support [default=yes] ])
AC_ARG_ENABLE([livelink],
    [AS_HELP_STRING([--enable-livelink],[compile with livelink mode support [default=yes]])],
    [livelink=$enableval],
    [livelink=yes]
)
AC_MSG_RESULT([$livelink])


if test "x${use_fast}" != "xno"
then
	if test "x${use_debug}" != "xno"
	then
		AC_MSG_ERROR([Cannot specify --enable-fast and --enable-debug at the same time.])
	fi
fi

dnl
dnl Check for special environment variables
dnl
if test -z "$DEFAULT_DRIVER"
then
	DEFAULT_DRIVER=ltotape
fi
if test -z "$DEFAULT_IOSCHED" ; then
	DEFAULT_IOSCHED=unified
fi
if test -z "$DEFAULT_KMI" ; then
	DEFAULT_KMI=none
fi
AC_ARG_VAR([DEFAULT_DRIVER], [default tape device plugin, e.g. ltotape])
AC_ARG_VAR([DEFAULT_IOSCHED], [default I/O scheduler plugin, e.g. unified])
AC_ARG_VAR([DEFAULT_KMI], [default key manager interface plugin, e.g. none])

dnl
dnl Check for pkg-config
dnl
if test -z "$PKG_CONFIG"
then
    AC_PATH_PROG(PKG_CONFIG, pkg-config, [no])
fi
if test "x${PKG_CONFIG}" = "xno"
then
    AC_MSG_ERROR([pkg-config was not found])
fi

dnl
dnl Check for genrb
dnl
AC_PATH_PROG(GENRB, genrb, no)
if test "x${GENRB}" = "xno"
then
	AC_MSG_ERROR([genrb was not found])
fi

dnl
dnl Check for pkgdata
dnl
AC_PATH_PROG(PKGDATA, pkgdata, no)
if test "x${PKGDATA}" = "xno"
then
	AC_MSG_ERROR([pkgdata was not found])
fi

dnl
dnl Check for FUSE, libuuid, and libxml2
dnl
PKG_CHECK_MODULES([FUSE_MODULE], [fuse >= 2.6.0])
PKG_CHECK_MODULES([UUID_MODULE], [uuid >= 1.36])
PKG_CHECK_MODULES([LIBXML2_MODULE], [libxml-2.0 >= 2.6.16])

dnl
dnl Check for ICU
dnl
ICU_MODULE_CFLAGS="`icu-config --cppflags 2> /dev/null`";
ICU_MODULE_LIBS="`icu-config --ldflags 2> /dev/null`";
if test -z "$ICU_MODULE_LIBS"
then
    PKG_CHECK_MODULES([ICU_MODULE], [icu >= 0.21])
fi

dnl
dnl Check for SNMP
dnl
dnl SNMP_MODULE_CFLAGS="`net-snmp-config --cflags`";
dnl SNMP_MODULE_LIBS="`net-snmp-config --agent-libs` `net-snmp-config --libs`";
dnl if test -z "$SNMP_MODULE_LIBS"
dnl then
dnl     PKG_CHECK_MODULES([SNMP_MODULE], [net-snmp >= 5.3])
dnl fi

dnl
dnl Check for headers, types, structures, compiler characteristics
dnl
AC_CHECK_HEADERS([fcntl.h limits.h stddef.h stdint.h stdlib.h string.h sys/ioctl.h sys/mount.h sys/time.h termios.h unistd.h])
AC_HEADER_STDBOOL
AC_TYPE_MODE_T
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT8_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_CHECK_SIZEOF([time_t])
AC_STRUCT_ST_BLOCKS
AC_CHECK_MEMBERS([struct stat.st_blksize])
AC_CHECK_MEMBERS([struct stat.st_rdev])
AC_C_INLINE

dnl
dnl Check for library functions
dnl
AC_FUNC_MALLOC
#AC_FUNC_MKTIME
AC_FUNC_STRNLEN
AC_CHECK_FUNCS([bzero memchr memset mkdir rmdir strcasecmp strdup strerror strndup strrchr strstr])

dnl
dnl Update flags
dnl Sets CFLAGS to force optimization and debugging options, which isn't quite kosher
dnl
dnl AM_CPPFLAGS="-D_GNU_SOURCE -I\$(top_srcdir)/src -DLTFS_CONFIG_FILE='\"${sysconfdir}/ltfs.conf\"' -DLTFS_BASE_DIR='\"${prefix}\"'"
dnl AM_CFLAGS="-Wall -Wsign-compare ${FUSE_MODULE_CFLAGS} ${UUID_MODULE_CFLAGS} ${LIBXML2_MODULE_CFLAGS} ${ICU_MODULE_CFLAGS} ${SNMP_MODULE_CFLAGS}"
AM_CPPFLAGS="-D_GNU_SOURCE -DGENERIC_OEM_BUILD -I\$(top_srcdir)/src -DLTFS_CONFIG_FILE='\"${sysconfdir}/ltfs.conf\"'"
AM_CFLAGS="-Wall -Wsign-compare ${FUSE_MODULE_CFLAGS} ${UUID_MODULE_CFLAGS} ${LIBXML2_MODULE_CFLAGS} ${ICU_MODULE_CFLAGS}"

if test "x$use_fast" = "xyes"
then
	OPT_FLAGS="-O2 -g -fno-strict-aliasing"
else
	OPT_FLAGS="-O0 -ggdb"
	AM_CFLAGS="${AM_CFLAGS} -fkeep-inline-functions -rdynamic"
	if test "x$use_debug" = "xyes"
	then
		AM_CPPFLAGS="${AM_CPPFLAGS} -DDEBUG -DTRACE"
	fi
fi

if test "x$livelink" = "xno"
then
	AM_CPPFLAGS="${AM_CPPFLAGS} -DPOSIXLINK_ONLY"
fi

dnl
dnl Specify CPU specific optimizer options for CRC calculation
dnl
AC_MSG_CHECKING([SSE4.2])
CRC_OPTIMIZE="-O2"

if test "x$GCC" = 'xyes'
then
	GCC_VERSION=`$CC -dumpversion`
	GCC_VERSION_MAJOR=$(echo $GCC_VERSION | cut -d'.' -f1)
	GCC_VERSION_MINOR=$(echo $GCC_VERSION | cut -d'.' -f2)
	SSE42=no

	if test ${GCC_VERSION_MAJOR} -ge 4 -a ${GCC_VERSION_MINOR} -ge 3
	then
		SSE42=yes
	fi

	if test "x${SSE42}" = "xyes"
	then
		case x"$target_cpu" in
			xx86_64)
				AC_MSG_RESULT([yes, x86_64])
				CRC_OPTIMIZE="-msse4.2 -O2 -D__SSE42__"
				;;
			xi*86)
				AC_MSG_RESULT([yes, x86])
				CRC_OPTIMIZE="-msse4.2 -O2 -D__SSE42__"
				;;
			*)
				AC_MSG_RESULT([no, unsupported cpu])
				;;
		esac
	else
		AC_MSG_RESULT([no, gcc version])
	fi
else
	AC_MSG_RESULT([no, non-gcc])
fi

dnl
dnl Configure standard options
dnl
dnl AM_LDFLAGS="${AM_LDFLAGS} ${FUSE_MODULE_LIBS} ${UUID_MODULE_LIBS} ${LIBXML2_MODULE_LIBS} ${ICU_MODULE_LIBS} ${SNMP_MODULE_LIBS}"
AM_LDFLAGS="${AM_LDFLAGS} ${FUSE_MODULE_LIBS} ${UUID_MODULE_LIBS} ${LIBXML2_MODULE_LIBS} ${ICU_MODULE_LIBS}"
CFLAGS="${CFLAGS} ${OPT_FLAGS}"

dnl
dnl Define options
dnl
AC_SUBST(CFLAGS)
AC_SUBST(CRC_OPTIMIZE)
AC_SUBST(AM_CPPFLAGS)
AC_SUBST(AM_CFLAGS)
AC_SUBST(AM_LDFLAGS)

dnl
dnl Output files
dnl
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
    Makefile
    messages/Makefile
    conf/Makefile
    init.d/Makefile
    src/Makefile
    src/libltfs/Makefile
    src/tape_drivers/linux/ltotape/Makefile
    src/iosched/Makefile
    src/kmi/Makefile
    src/utils/Makefile
])
AC_OUTPUT
