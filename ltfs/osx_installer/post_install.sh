#!/bin/sh

LOG_FILE=/tmp/LTFS_postinstall.log

FRAMEWORK_NAME=LTFS.framework
VERSION=3.0.0

echo "" >> ${LOG_FILE}
echo "Starting post-install for ${FRAMEWORK_NAME} at `date`" >> ${LOG_FILE}

echo "User: "`who am i` >> ${LOG_FILE}
echo "PWD:  "`pwd`      >> ${LOG_FILE}
echo "args: "$@         >> ${LOG_FILE}

PACKAGE_LOCATION=${1}
INSTALL_DEST=${2}
DEST_VOL=${3}
INSTALL_TYPE=${4}

echo "PACKAGE_LOC:  ${PACKAGE_LOCATION}" >> ${LOG_FILE}
echo "INSTALL_DEST: ${INSTALL_DEST}" >> ${LOG_FILE}
echo "DEST_VOL:     ${DEST_VOL}" >> ${LOG_FILE}
echo "INSTALL_TYPE: ${INSTALL_TYPE}" >> ${LOG_FILE}

echo "PWD: "`pwd` >> ${LOG_FILE}


# Change owner...
echo "Changing owner and group..." >> ${LOG_FILE}
cd ${INSTALL_DEST}/${FRAMEWORK_NAME}
chown -R root:admin .
echo "    owner and group changed" >> ${LOG_FILE}


# Change permissions...
echo "Changing Permissions..." >> ${LOG_FILE}
#   set directory permissions
cd ${INSTALL_DEST}/${FRAMEWORK_NAME}
find . -type d | xargs chmod u=rwx,go=rx
echo "    set directory permissions" >> ${LOG_FILE}

#   set read file permissions
#cd ${INSTALL_DEST}/${FRAMEWORK_NAME}/Versions/Current/usr/include
#find . -type f | xargs chmod u=rw,go=r
#cd ${INSTALL_DEST}/${FRAMEWORK_NAME}/Versions/Current/usr/man
#find . -type f | xargs chmod u=rw,go=r
cd ${INSTALL_DEST}/${FRAMEWORK_NAME}/Versions/Current/usr/share
find . -type f | xargs chmod u=rw,go=r
echo "    set read file permissions" >> ${LOG_FILE}

#   set executable file permissions
cd ${INSTALL_DEST}/${FRAMEWORK_NAME}/Versions/Current/usr/bin
find . -type f | xargs chmod u+rwx,go=rx
cd ${INSTALL_DEST}/${FRAMEWORK_NAME}/Versions/Current/usr/lib
find . -type f | xargs chmod u+rwx,go=rx
echo "    set executable file permissions" >> ${LOG_FILE}

# Create symlinks to save cumbersome path manipulation for users
echo "Linking executables..." >> ${LOG_FILE}
if [ ! -d /usr/local/bin/ ]
then
  mkdir /usr/local/bin/
  echo "    created directory /usr/local/bin/" >> ${LOG_FILE}
fi
proglist=`find ${INSTALL_DEST}/${FRAMEWORK_NAME}/Versions/Current/usr/bin -type f`
for prog in $proglist 
do
  ln -s $prog /usr/local/bin/`basename $prog`
done
echo "    symlinked executables to /usr/local/bin/" >> ${LOG_FILE}

# Create a log directory if it doesn't already exist
DUMPDIR=${HOME}/Library/Logs/LTFS
if [ ! -d $DUMPDIR ]
then
  echo "Creating log directory...">> ${LOG_FILE}
  mkdir $DUMPDIR
  echo "    created directory $DUMPDIR for drive log snapshot dumps" >> ${LOG_FILE}
  ls -ld ${HOME}/Library/Logs | awk '{ printf "%s:%s %s/LTFS",$3,$4,$9 }' | xargs chown
  echo "    set directory permissions" >> ${LOG_FILE}
fi

echo "Finished post-install for ${FRAMEWORK_NAME} at `date`" >> ${LOG_FILE}
