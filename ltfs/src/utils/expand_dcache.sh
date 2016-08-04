#!/bin/sh
################################################################################
##
##  %Z% %I% %W% %G% %U%
##
##  ZZ_Copyright_BEGIN
##
##
##  Licensed Materials - Property of IBM
##
##  IBM Linear Tape File System Single Drive Edition Version 1.3.0.2 for Linux and Mac OS X
##
##  Copyright IBM Corp. 2010, 2013
##
##  This file is part of the IBM Linear Tape File System Single Drive Edition for Linux and Mac OS X
##  (formally known as IBM Linear Tape File System)
##
##  The IBM Linear Tape File System Single Drive Edition for Linux and Mac OS X is free software;
##  you can redistribute it and/or modify it under the terms of the GNU Lesser
##  General Public License as published by the Free Software Foundation,
##  version 2.1 of the License.
##
##  The IBM Linear Tape File System Single Drive Edition for Linux and Mac OS X is distributed in the
##  hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
##  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
##  See the GNU Lesser General Public License for more details.
##
##  You should have received a copy of the GNU Lesser General Public
##  License along with this library; if not, write to the Free Software
##  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
##  or download the license from <http://www.gnu.org/licenses/>.
##
##
##  ZZ_Copyright_END
##
## COMPONENT NAME:  IBM Linear Tape File System
##
## FILE NAME:       expand_dcache
##
## DESCRIPTION:     Expand the disk fulled dcache image
##
################################################################################

DEFAULT_CONFIG="__CONFDIR__/ltfs.conf"
DEFAULT_CONFIG_LOCAL="__CONFDIR__/ltfs.conf.local"
DEFAULT_WORK="/tmp/ltfs"

CONFIG=${DEFAULT_CONFIG}
CONFIG_LOCAL=${DEFAULT_CONFIG_LOCAL}
WORK_DIR=${DEFAULT_WORK}
INTERRUCTIVE="AUTO"

################################################################################
# Functions

function usage() {
	cat <<EOF
Usage: expand_dcache.sh [OPTION]... size [library]...

Increase the maxmum disk image size setting of LTFS LE and clear the disk full
flag of the disk images.

Options:
  -c config_file     Specify ltfs configuration file
                     (default __CONFDIR__/ltfs.conf)
  -w work_directory  Specify work directory to search disk image
                     (default /tmp/ltfs)
  -i                 Set interructive mode. This program ask user to clear
                     the disk full flag in each libraries
  -f                 Set force mode. This program attempts to clear the disk
                     full in all ribraries without any interruption

Arguments:
  size               Size to increase to the max size of the diskimage for
                     LTFS LE in GB
  library            Library serial to clear the disk full flag.
                     If this arguments are not specified and -i or -f is
                     not specified, This program enters interactive mode and
                     ask user to clear the disk full flag in each libraries.
EOF

	exit 2
}

################################################################################
# Parse options
while getopts c:w:if OPT; do
	case $OPT in
		c )
			CONFIG="$OPTARG"
			CONFIG_LOCAL="${OPTARG}.local" ;;
		w )
			WORK_DIR="$OPTARG" ;;
		i )
			INTERRUCTIVE="YES" ;;
		f )
			INTERRUCTIVE="NO" ;;
		? )
			echo "Unexpected option is detected"
			usage
	esac
done

shift $(( $OPTIND - 1))

################################################################################
# Check arguments
ADD_SIZE_GB=$1
shift

CHECK=`echo "$ADD_SIZE_GB" | sed 's/[^0-9]//g'`
if [ "$ADD_SIZE_GB" != "$CHECK" ]; then
	echo "Invalid option is detected ($ADD_SIZE_GB)"
	usage
fi

if [ "x$*" = "x" ]; then
	LIBRARIES="ALL";
else
	LIBRARIES="$*";
fi

echo ""
echo "The disk cahce expanding script for LTFS LE"
echo "  Config file:             ${CONFIG}"
echo "  Config file (local):     ${CONFIG_LOCAL}"
echo "  Work directory for LTFS: ${WORK_DIR}"
echo "  Expand size:             ${ADD_SIZE_GB}GB"
echo "  Target Libraries to clear the disk full flag:"
echo "                            $LIBRARIES"
echo ""

################################################################################
# Check the config files and detect the current max size in GB
if [ ! -f ${CONFIG} ]; then
	echo "${CONFIG} is not found"
	exit 2
fi

echo "Checking the max image size... "
CUR_SIZE_GB="NULL"
LOCAL_INCLUDED="NO"
while read opt type param value; do
	if [ "$opt $type $param" = "option dcache maxsize" ]; then
		CUR_SIZE_GB=$value
	fi
	if [ "$opt $type" = "include ${CONFIG_LOCAL}" ]; then
		LOCAL_INCLUDED="YES"
	fi
done < "${CONFIG}"

if [ "x${LOCAL_INCLUDED}" = "xNO" ]; then
	echo "${CONFIG_LOCAL} is not included into ${CONFIG}"
	exit 2
fi

FOUND_IN_LOCAL="NO"
while read opt type param value; do
	if [ "$opt $type $param" = "option dcache maxsize" ]; then
		CUR_SIZE_GB=$value
		FOUND_IN_LOCAL="YES"
	fi
done < "${CONFIG_LOCAL}"

if [ "x${CUR_SIZE_GB}" = "xNULL" ]; then
	echo "option dcache maxsize is not into configuration files"
fi

NEW_SIZE_GB=`expr ${CUR_SIZE_GB} + ${ADD_SIZE_GB}`
echo "  Current size:   ${CUR_SIZE_GB}GB"
echo "  New size:       ${NEW_SIZE_GB}GB"
echo ""

################################################################################
# Update the max size of the disk image in config.local
echo "Updating the max image size... "
if [ "x${FOUND_IN_LOCAL}" == "xYES" ]; then
	BASENAME=`basename ${CONFIG_LOCAL}`
	TMP_FILE=`mktemp /tmp/${BASENAME}.XXXXXX`
	cp ${CONFIG_LOCAL} ${TMP_FILE}
	if [ "$?" != "0" ]; then
		echo "failed to copy ${CONFIG_LOCAL}"
		rm -f ${TMP_FILE}
		exit 2
	fi
	cat ${TMP_FILE} | sed "s/^option\s\+dcache\s\+maxsize\s\+.*/option dcache maxsize ${NEW_SIZE_GB}/" > ${CONFIG_LOCAL}
	rm -f ${TMP_FILE}
else
	echo "option dcache maxsize ${NEW_SIZE_GB}" >> "${CONFIG_LOCAL}"
fi
echo "Updated max image size to ${NEW_SIZE_GB}"
echo ""

################################################################################
# Searching libraries into work directory
echo "Searching libraries into ${WORK_DIR}"
EXISTING_LIBS=`cd ${WORK_DIR}; find -name "*.img" -exec dirname {} \; | sed 's/\.\///'`
MOUNTING_DIRS=`mount | grep LTFSDCACHE | grep ${WORK_DIR} | (while read a b c d e f; do echo $f; done) | sed -e 's/(LTFSDCACHE,//' | sed -e 's/)//'`

if [ "x${EXISTING_LIBS}" != "x" ]; then
	echo "  LTFS LE mouted following libraries in the past"
	for lib in $EXISTING_LIBS; do
		echo "    * $lib"
	done
else
	echo "  No libraries LTFS LE mounted in the past are found"
fi

if [ "x${MOUNTING_DIRS}" != "x" ]; then
	echo "  Following directories are mounted at this time"
	for dir in $MOUNTING_DIRS; do
		echo "    * $dir"
	done
else
	echo "  No disk image is mounted at this time"
fi
echo "Done"
echo ""

################################################################################
# unset the disk full flag
if [ "x${INTERRUCTIVE}" = "xAUTO" ]; then
	if [ "x${LIBRARIES}" = "xALL" ]; then
		INTERRUCTIVE="YES"
	else
		INTERRUCTIVE="NO"
	fi
fi

if [ "x${LIBRARIES}" = "xALL" ]; then
	LIBRARIES=${EXISTING_LIBS}
fi

for target in $LIBRARIES; do
	if [ ! -f "${WORK_DIR}/${target}/dentry_cache.img" ]; then
		echo "Library ${target} is not found in this system. Skip"
	else
		MOUNTED="NO"
		for dir in $MOUNTING_DIRS; do
			if [ "x$dir" = "x${WORK_DIR}/${target}/dentry_cache.img" ]; then
				MOUNTED="YES"
			fi
		done
		if [ "x${MOUNTED}" = "xYES" ]; then
			echo "Library ${target} is already mounted. Skip"
		else
			if [ "x${INTERRUCTIVE}" = "xYES" ]; then
				echo -n "Clear the disk full flag of library ${target} ? (Y/N)"
				read ans
				case "$ans" in
					[Yy][Ee][Ss] ) GO="YES" ;;
					[Nn][Oo]     ) GO="NO" ;;
					[Yy]         ) GO="YES" ;;
					[Nn]         ) GO="NO" ;;
					* ) GO="NO" ;;
				esac
			else
				GO="YES"
			fi
			if [ "x${GO}" = "xYES" ]; then
				if [ "x${INTERRUCTIVE}" = "xYES" ]; then
					echo -n " --> "
				fi
				echo "Clearing the disk full flag of library ${target}"
				mount -o loop -o rw -o user_xattr ${WORK_DIR}/${target}/dentry_cache.img ${WORK_DIR}/${target}/dentry_cache
				if [ "$?" != "0" ]; then
					echo " --> failed to mount ${WORK_DIR}/${target}/dentry_cache.img"
				else
					attr -s ltfs.DiskFull -V 0 ${WORK_DIR}/${target}/dentry_cache > /dev/null 2>&1
					if [ "$?" != "0" ]; then
						echo " --> failed to unset disk full flag ${WORK_DIR}/${target}/dentry_cache.img"
					else
						echo " --> Unset disk full flag successfully"
					fi
				fi
				umount -f ${WORK_DIR}/${target}/dentry_cache
			else
				echo " --> Skip to clear the disk full flag of library ${target}"
			fi
		fi
	fi
done
