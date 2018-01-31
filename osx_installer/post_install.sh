#!/bin/sh

LOG_FILE=/tmp/LTFS_postinstall.log

FRAMEWORK_NAME=LTFS.framework
VERSION=1.0

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

#   Create symbolic links to /usr/local/bin
if [ ! -d /usr/local/bin ]; then
	mkdir -p /usr/local/bin
fi

if [ -e /usr/local/bin/ltfs ]; then
	echo "Warning: overwriting existing ltfs executable" >> ${LOG_FILE}
fi
ln -s -f ${INSTALL_DEST}/${FRAMEWORK_NAME}/Versions/Current/usr/bin/ltfs /usr/local/bin/ltfs

if [ -e /usr/local/bin/mkltfs ]; then
	echo "Warning: overwriting existing mkltfs executable" >> ${LOG_FILE}
fi
ln -s -f ${INSTALL_DEST}/${FRAMEWORK_NAME}/Versions/Current/usr/bin/mkltfs /usr/local/bin/mkltfs

if [ -e /usr/local/bin/ltfsck ]; then
	echo "Warning: overwriting existing ltfsck executable" >> ${LOG_FILE}
fi
ln -s -f ${INSTALL_DEST}/${FRAMEWORK_NAME}/Versions/Current/usr/bin/ltfsck /usr/local/bin/ltfsck

echo "Finished post-install for ${FRAMEWORK_NAME} at `date`" >> ${LOG_FILE}
