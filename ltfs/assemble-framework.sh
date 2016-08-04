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
# FILE NAME:       assemble-framework.sh
#
# DESCRIPTION:     Automates assembly of OS X framework
#                  skeleton for LTFS.
#
# AUTHOR:          Michael A. Richmond
#                  IBM Almaden Research Center
#                  mar@almaden.ibm.com
#
#############################################################################

BASEDIR=`pwd`

DIRNAME=`dirname $0`

PROJECT_NAME=LTFS
PROJECT_VERSION=3.0.0

FRAMEWORK_NAME=${PROJECT_NAME}.framework

OUTPUT_DIR=distribution

BUNDLE_IDENTIFIER=com.hp.ltfs
BUNDLE_REGION=English
BUNDLE_EXECUTABLE=ltfs

##########################################################################
##########################################################################

# Create folders for framework in current directory
create_framework_structure()
{
	umask 00
    mkdir Versions
    mkdir Versions/${PROJECT_VERSION}

    ln -s ${PROJECT_VERSION} Versions/Current

    mkdir Versions/${PROJECT_VERSION}/Headers
    ln -s Versions/Current/Headers Headers

    mkdir Versions/Current/Resources
    ln -s Versions/Current/Resources Resources

    # Prefix for project install...
    mkdir Versions/Current/usr

    mkdir Versions/Current/usr/lib
    ln -s Versions/Current/usr/lib Libraries
}


create_infoplist()
{
    packageName=$1
    packageVersion=$2
    bundleName=$3
    bundleIdentifier=$4
    bundleDevelopmentRegion=$5
    bundleExecutable=$6

    cat > Resources/Info.plist <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key>
	<string>${bundleDevelopmentRegion}</string>
	<key>CFBundleExecutable</key>
	<string>${bundleExecutable}</string>
	<key>CFBundleGetInfoString</key>
	<string>${packageName} ${packageVersion}</string>
	<key>CFBundleIdentifier</key>
	<string>${bundleIdentifier}</string>
	<key>CFBundleInfoDictionaryVersion</key>
	<string>6.0</string>
	<key>CFBundleName</key>
	<string>${bundleName}</string>
	<key>CFBundlePackageType</key>
	<string>FMWK</string>
	<key>CFBundleShortVersionString</key>
	<string>${packageVersion}</string>
	<key>CFBundleSignature</key>
	<string>????</string>
	<key>CFBundleVersion</key>
	<string>${packageVersion}</string>
</dict>
</plist>
EOF
}


print_usage()
{
    echo "usage: "`basename $0`" [clean|distclean]"
    echo ""
    echo "     This script assembles the skeleton for an OS X framework."
    echo ""
    echo "     The 'clean' option cleans up any previously built"
    echo "     framework."
    echo ""
}


##########################################################################
##########################################################################

if [ $# -ne 0 ]; then
    case ${1} in
        clean)
            cd ${BASEDIR}
            \rm -rf ${OUTPUT_DIR}

            exit 0
            ;;
        *)
            print_usage
            cd ${BASEDIR}

            exit 1
            ;;
    esac
fi


##
##  create working directory...
##
cd ${BASEDIR}
mkdir ${OUTPUT_DIR}


##
## create framework structure...
##
cd ${BASEDIR}/${OUTPUT_DIR}
mkdir ${FRAMEWORK_NAME}
cd ${FRAMEWORK_NAME}

create_framework_structure


##
## Create Info.plist...
##
cd ${BASEDIR}/${OUTPUT_DIR}/${FRAMEWORK_NAME}
create_infoplist "${PROJECT_NAME}" "${PROJECT_VERSION}" "${PROJECT_NAME}" "${BUNDLE_IDENTIFIER}" "${BUNDLE_REGION}" "${BUNDLE_EXECUTABLE}"


##
## Set permissions...
##
cd ${BASEDIR}/${OUTPUT_DIR}/${FRAMEWORK_NAME}
find . -type d |xargs chmod a+rx

cd ${BASEDIR}
