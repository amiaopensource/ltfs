#!/bin/sh
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
#############################################################################
#
# FILE NAME:       build_package_osx.sh
#
# DESCRIPTION:     Automates build of LTFS for OS X.
#
# AUTHOR:          Michael A. Richmond
#                  IBM Almaden Research Center
#                  mar@almaden.ibm.com
#
#############################################################################
#
# (C) Copyright 2015 Hewlett Packard Enterprise Development LP.
#
# Changes to support building for HP environment 
#    - ltotape backend; clean ltfs.conf; add unltfs utility
#
#############################################################################

#DEBUG_FLAGS="-DDEBUG -DTRACE"
#DEBUG_FLAGS="-DDEBUG"
DEBUG_FLAGS=""

ICU4C_FRAMEWORK_DIR=/Library/Frameworks/ICU.framework
MACFUSE_FRAME_WORK_DIR=/Library/Frameworks/MacFUSE.framework
OSXFUSE_FRAME_WORK_DIR=/Library/Frameworks/OSXFUSE.framework
FUSE="NONE";

#
# LTFS version
#
LTFS_FRAMEWORK=LTFS.framework
LTFS_FRAMEWORK_EXECUTABLE=LTFS
LTFS_VERSION=3.0.0

OUTPUT_DIR_NAME=distribution

BASEDIR=`pwd`

#
# Final install path
#
LTFS_INSTALL_PREFIX=/Library/Frameworks/${LTFS_FRAMEWORK}/Versions/Current/usr
LTFS_CONF_PREFIX=/Library/Frameworks/${LTFS_FRAMEWORK}/Versions/Current/etc
LTFS_FRAMEWORK_DIR=${BASEDIR}/${OUTPUT_DIR_NAME}/${LTFS_FRAMEWORK}
LTFS_BUILD_OUTPUT_DIR=${LTFS_FRAMEWORK_DIR}/Versions/Current/usr
LTFS_SRC_DIR=${BASEDIR}/src
LTFS_CONF_DIR=${BASEDIR}/conf
TARBALL_VERSION=`echo ${LTFS_VERSION} | tr '.' '_'`
LTFS_TARBALL=ltfs-iokit-bin-v${TARBALL_VERSION}.tar.gz

if [ "x${GNUMAKE}" = "x" ]; then
	GNUMAKE=/usr/bin/gnumake
fi

###############################################################################
###############################################################################

check_prereqs()
{
    # ICU4C
    if [ ! -e ${ICU4C_FRAMEWORK_DIR} ]; then
            echo "error: prerequiste 'ICU' is not installed in '${ICU4C_FRAMEWORK_DIR}'."
            exit 1
	else
		for i in `ls -l ${ICU4C_FRAMEWORK_DIR}/Versions | grep Current`; do
			VERSION=$i
		done
        echo "info: prerequiste 'ICU ${VERSION}' is installed in '${ICU4C_FRAMEWORK_DIR}'."
    fi

    # MacFUSE
    if [ -e ${MACFUSE_FRAME_WORK_DIR} ]     &&
       [ -e /usr/local/include/fuse.h ]     &&
       [ -e /usr/local/lib/libfuse.dylib ]  &&
       [ -e /usr/local/lib/libfuse_ino64.dylib ]; then
        echo "info: prerequiste 'MacFUSE' is installed."
		FUSE="MACFUSE"
	fi

    # OSXFUSE
    if [ -e ${OSXFUSE_FRAME_WORK_DIR} ]         &&
       [ -e /usr/local/include/osxfuse/fuse.h ] &&
       [ -e /usr/local/lib/libosxfuse.dylib ]   &&
       [ -e /usr/local/lib/libosxfuse_i64.dylib ]; then
        echo "info: prerequiste 'OSXFUSE $i' is installed."
		FUSE="OSXFUSE"
	fi

	if [ "x$FUSE" = "xNONE" ]; then
		echo "error: prerequiste 'MacFUSE or OSXFUSE' is not installed in '/Library/Frameworks/'."
		exit 1
	fi
}

PWD=`pwd`

if [ $# -ne 0 ]; then
	if [ "${1}" == "clean" ]; then
		echo "Cleaning up the ltfs build folder in"
		echo "${OUTPUT_DIR_NAME}"

		cd ${BASEDIR}
		/bin/rm -rf ${OUTPUT_DIR_NAME}

##
## Added for HP-SOS
##
		cd ${BASEDIR}/conf
		${GNUMAKE} -f Makefile.osx clean-local
        
		cd ${BASEDIR}/messages
		${GNUMAKE} -f Makefile.osx clean-local

		cd ${LTFS_SRC_DIR}
		${GNUMAKE} -f Makefile.osx clean

		exit 0
	else
		echo "usage: build_osx_package.sh [clean]"
		echo ""
		echo "      This script builds the LTFS system from source and produces"
		echo "      a tar.gz file in the directory '${OUTPUT_DIR_NAME}'."
		echo ""
		echo "      The 'clean' option cleans up the source and object trees."
		exit 1
	fi
fi

##
## create working directory...
##
cd ${BASEDIR}
if [ -e ${OUTPUT_DIR_NAME} ]; then
	echo "error: Output directory '${OUTPUT_DIR_NAME}' already exists."
	echo "       Please use '"`basename $0`" clean' to remove previous build output."
	echo ""
	exit 1
fi

mkdir ${OUTPUT_DIR_NAME}

echo ""
echo "Checking pre-requisites..."
check_prereqs


#
# Create framework skeleton
#
echo ""
echo "Creating framework skeleton..."
cd ${BASEDIR}
./assemble-framework.sh
if [ $? != "0" ]; then
	echo "Encountered problem assembling framework skeleton."
	exit 1
fi


#
# Create unix directory tree in framework
#
echo ""
echo "Creating output folders in '${OUTPUT_DIR_NAME}..."
cd ${LTFS_BUILD_OUTPUT_DIR}
mkdir -p bin
mkdir -p lib/ltfs
mkdir -p share/ltfs


#
# Compile code
#
echo ""
echo "Building message library..."

cd ${BASEDIR}/messages
${GNUMAKE} -f Makefile.osx clean-local &&   \
${GNUMAKE} -f Makefile.osx
if [ $? != "0" ]; then
	echo "Encountered problem building message library."
	exit 1
fi

echo ""
echo "Building binaries..."

cd ${LTFS_SRC_DIR}

${GNUMAKE} -f Makefile.osx clean && 	\

if [ "x${FUSE}" = "MACFUSE" ]; then
	${GNUMAKE} -f Makefile.osx CONFDIR="${LTFS_CONF_PREFIX}" FUSE=MACFUSE PREFIX="${LTFS_INSTALL_PREFIX}" DEBUG="${DEBUG_FLAGS}"
else
	${GNUMAKE} -f Makefile.osx CONFDIR="${LTFS_CONF_PREFIX}" FUSE=OSXFUSE PREFIX="${LTFS_INSTALL_PREFIX}" DEBUG="${DEBUG_FLAGS}"
fi

if [ $? != "0" ]; then
	echo "Encountered problem during build."
	exit 1
fi


#
# Copy binaries to output folder
#
echo ""
echo "Copying binaries to output folders..."

cd ${LTFS_SRC_DIR}

BIN_FILES="ltfs mkltfs ltfsck unltfs"
for f in ${BIN_FILES}
do
	echo "Copying ${f} to ${BASEDIR}/${OUTPUT_DIR_NAME}${LTFS_INSTALL_PREFIX}/bin/"
	cp ${LTFS_SRC_DIR}/${f} ${LTFS_BUILD_OUTPUT_DIR}/bin
	if [ $? != "0" ]; then
		echo "Encountered problem copying files."
		exit 1
	fi
done

cp ${LTFS_SRC_DIR}/libltfs.${LTFS_VERSION}.dylib ${LTFS_BUILD_OUTPUT_DIR}/lib
if [ $? != "0" ]; then
	echo "Encountered problem copying files."
	exit 1
fi

cp -R ${LTFS_SRC_DIR}/libiosched-unified.${LTFS_VERSION}.dylib ${LTFS_BUILD_OUTPUT_DIR}/lib/ltfs
if [ $? != "0" ]; then
	echo "Encountered problem copying files."
	exit 1
fi
cp -R ${LTFS_SRC_DIR}/libiosched-fcfs.${LTFS_VERSION}.dylib ${LTFS_BUILD_OUTPUT_DIR}/lib/ltfs
if [ $? != "0" ]; then
	echo "Encountered problem copying files."
	exit 1
fi
cp -R ${LTFS_SRC_DIR}/libkmi-flatfile.${LTFS_VERSION}.dylib ${LTFS_BUILD_OUTPUT_DIR}/lib/ltfs
if [ $? != "0" ]; then
	echo "Encountered problem copying files."
	exit 1
fi
cp -R ${LTFS_SRC_DIR}/libkmi-simple.${LTFS_VERSION}.dylib ${LTFS_BUILD_OUTPUT_DIR}/lib/ltfs
if [ $? != "0" ]; then
	echo "Encountered problem copying files."
	exit 1
fi
#cp -R ${LTFS_SRC_DIR}/libdriver-iokit.${LTFS_VERSION}.dylib ${LTFS_BUILD_OUTPUT_DIR}/lib/ltfs
#if [ $? != "0" ]; then
#	echo "Encountered problem copying files."
#	exit 1
#fi
#cp -R ${LTFS_SRC_DIR}/libdriver-file.${LTFS_VERSION}.dylib ${LTFS_BUILD_OUTPUT_DIR}/lib/ltfs
#if [ $? != "0" ]; then
#	echo "Encountered problem copying files."
#	exit 1
#fi
#cp -R ${LTFS_SRC_DIR}/libdriver-itdtimg.${LTFS_VERSION}.dylib ${LTFS_BUILD_OUTPUT_DIR}/lib/ltfs
#if [ $? != "0" ]; then
#	echo "Encountered problem copying files."
#	exit 1
#fi
cp -R ${LTFS_SRC_DIR}/libdriver-ltotape.${LTFS_VERSION}.dylib ${LTFS_BUILD_OUTPUT_DIR}/lib/ltfs
if [ $? != "0" ]; then
	echo "Encountered problem copying files."
	exit 1
fi



umask 00
cd ${LTFS_BUILD_OUTPUT_DIR}/lib
ln -fs libltfs.${LTFS_VERSION}.dylib libltfs.dylib

cd ${LTFS_BUILD_OUTPUT_DIR}/lib/ltfs
ln -fs libdriver-iokit.${LTFS_VERSION}.dylib libdriver-iokit.dylib
ln -fs libdriver-file.${LTFS_VERSION}.dylib libdriver-file.dylib
ln -fs libdriver-ltotape.${LTFS_VERSION}.dylib libdriver-ltotape.dylib
#ln -fs libdriver-itdtimg.${LTFS_VERSION}.dylib libdriver-itdtimg.dylib

cd ${LTFS_BUILD_OUTPUT_DIR}/lib/ltfs
ln -fs libiosched-unified.${LTFS_VERSION}.dylib libiosched-unified.dylib
ln -fs libiosched-fcfs.${LTFS_VERSION}.dylib libiosched-fcfs.dylib
ln -fs libkmi-flatfile.${LTFS_VERSION}.dylib libkmi-flatfile.dylib
ln -fs libkmi-simple.${LTFS_VERSION}.dylib libkmi-simple.dylib


# Set up framework executatble
cd ${LTFS_FRAMEWORK_DIR}
ln -fs Versions/Current/usr/lib/libltfs.${LTFS_VERSION}.dylib ${LTFS_FRAMEWORK_EXECUTABLE}


#
# Copy over documentation
#
echo ""
echo "Copying documentation..."
cd ${LTFS_BUILD_OUTPUT_DIR}/share/ltfs
mkdir doc
cp -R ${BASEDIR}/doc/[A-z]* ./doc/


#
# Copy over header files
#
echo ""
echo "Copying header files..."
cd ${LTFS_BUILD_OUTPUT_DIR}
mkdir -p include/ltfs/arch
HEADER_FILES="ltfsprintf.h"
for f in ${HEADER_FILES}
do
    cp ${BASEDIR}/src/${f} ${LTFS_BUILD_OUTPUT_DIR}/include/ltfs/${f}
done
HEADER_FILES="ltfs.h ltfs_types.h queue.h tape_ops.h plugin.h config_file.h ltfs_error.h"
for f in ${HEADER_FILES}
do
    cp ${BASEDIR}/src/libltfs/${f} ${LTFS_BUILD_OUTPUT_DIR}/INCLUDE/LTFS/${f}
done
cd ${LTFS_FRAMEWORK_DIR}/Versions/Current/Headers
ln -s ../usr/include/ltfs ltfs

#
# Create and copy over configuration file
#
echo ""
echo "Copying configuration file..."
cd ${LTFS_CONF_DIR}
${GNUMAKE} -f Makefile.osx clean-local &&   \
${GNUMAKE} LIBDIR=${LTFS_INSTALL_PREFIX}/lib DEFAULT_DRIVER=ltotape DEFAULT_IOSCHED=unified DEFAULT_KMI=none CONFDIR="${LTFS_CONF_PREFIX}" -f Makefile.osx
mkdir -p ${LTFS_FRAMEWORK_DIR}/Versions/Current/etc
cp ${LTFS_CONF_DIR}/ltfs.conf ${LTFS_FRAMEWORK_DIR}/Versions/Current/etc/ltfs.conf
cp ${LTFS_CONF_DIR}/ltfs.conf.local ${LTFS_FRAMEWORK_DIR}/Versions/Current/etc/ltfs.conf.local

##
## Set permissions...
##
echo ""
echo "Setting permissions..."
cd ${LTFS_BUILD_OUTPUT_DIR}
find bin -type f      | xargs chmod a+rx
find lib -type f      | xargs chmod a+rx
find lib -type d      | xargs chmod a+rx
find share -type d    | xargs chmod a+rx
find share -type f    | xargs chmod a+r
find include -type d  | xargs chmod a+rx
find include -type f  | xargs chmod a+r


##
## Building OS X .pkg install package
##
cd ${BASEDIR}/${OUTPUT_DIR_NAME}
PACKAGE_MAKER=/Applications/Utilities/PackageMaker.app/Contents/MacOS/PackageMaker
${PACKAGE_MAKER} -d ${BASEDIR}/osx_installer/LTFS.pmdoc -v -i com.ltfs -o ${BASEDIR}/${OUTPUT_DIR_NAME}/LTFS_Mac.pkg

cd ${BASEDIR}
echo "Build complete"
