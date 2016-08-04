/*
**  %Z% %I% %W% %G% %U%
**
**  ZZ_Copyright_BEGIN
**
**
**  Licensed Materials - Property of IBM
**
**  IBM Linear Tape File System Single Drive Edition Version 2.2.0.2 for Linux and Mac OS X
**
**  Copyright IBM Corp. 2010, 2014
**
**  This file is part of the IBM Linear Tape File System Single Drive Edition for Linux and Mac OS X
**  (formally known as IBM Linear Tape File System)
**
**  The IBM Linear Tape File System Single Drive Edition for Linux and Mac OS X is free software;
**  you can redistribute it and/or modify it under the terms of the GNU Lesser
**  General Public License as published by the Free Software Foundation,
**  version 2.1 of the License.
**
**  The IBM Linear Tape File System Single Drive Edition for Linux and Mac OS X is distributed in the
**  hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
**  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
**  See the GNU Lesser General Public License for more details.
**
**  You should have received a copy of the GNU Lesser General Public
**  License along with this library; if not, write to the Free Software
**  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
**  or download the license from <http://www.gnu.org/licenses/>.
**
**
**  ZZ_Copyright_END
**
*************************************************************************************
**
** COMPONENT NAME:  IBM Linear Tape File System
**
** FILE NAME:       arch/arch_info.c
**
** DESCRIPTION:     Show platform information
**
** AUTHOR:          Atsushi Abe
**                  IBM Yamato, Japan
**                  PISTE@jp.ibm.com
**
*************************************************************************************
*/

#include "libltfs/ltfs.h"
#ifndef mingw_PLATFORM
#include <sys/sysctl.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

void show_runtime_system_info(void)
#if defined(__linux__)
{
	int fd;
	char kernel_version[512];
	char destribution[256];
	DIR *dir;
	struct dirent *dent;
	struct stat stat_vm64, stat_rel;
	char *path, *tmp;

	fd = open("/proc/version", O_RDONLY);
	if( fd == -1) {
		ltfsmsg(LTFS_WARN, "17086W");
	} else {
		memset(kernel_version, 0, sizeof(kernel_version));
		read(fd, kernel_version, sizeof(kernel_version));
		if((tmp = strchr(kernel_version, '\n')) != NULL)
			*tmp = '\0';

		if(stat("/proc/sys/kernel/vsyscall64", &stat_vm64) != -1 && S_ISREG(stat_vm64.st_mode)) {
#if defined(__i386__) || defined(__x86_64__)
			strcat(kernel_version, " x86_64");
#elif defined(__ppc__) || defined(__ppc64__)
			strcat(kernel_version, " ppc64");
#else
			strcat(kernel_version, " unknown");
#endif
		}
		else {
#if defined(__i386__) || defined(__x86_64__)
			strcat(kernel_version, " i386");
#elif defined(__ppc__) || defined(__ppc64__)
			strcat(kernel_version, " ppc");
#else
			strcat(kernel_version, " unknown");
#endif
		}
		ltfsmsg(LTFS_INFO, "17087I", kernel_version);
		close(fd);
	}

	dir = opendir("/etc");
	if (dir) {
		while( (dent = readdir(dir)) != NULL) {
			if(strlen(dent->d_name) > strlen("-release") &&
			   !strcmp(&(dent->d_name[strlen(dent->d_name) - strlen("-release")]), "-release")) {
				path = calloc(1, strlen(dent->d_name) + strlen("/etc/") + 1);
				if (!path) {
					/* Memory allocation failed */
					ltfsmsg(LTFS_ERR, "10001E", __FUNCTION__);
					closedir(dir);
					return;
				}
				strcat(path, "/etc/");
				strcat(path, dent->d_name);
				if(stat(path, &stat_rel) != -1 && S_ISREG(stat_rel.st_mode)) {
					fd = open(path, O_RDONLY);
					if( fd == -1) {
						ltfsmsg(LTFS_WARN, "17088W");
					} else {
						memset(destribution, 0, sizeof(destribution));
						read(fd, destribution, sizeof(destribution));
						if((tmp = strchr(destribution, '\n')) != NULL)
							*tmp = '\0';
						ltfsmsg(LTFS_INFO, "17089I", destribution);
						close(fd);
					}
				}
				free(path);
			}
		}
		closedir(dir);
	}

	return;
}
#elif defined(__APPLE__)
{
	int mib[2];
	size_t len;
	char *kernel_version;

	mib[0] = CTL_KERN;
	mib[1] = KERN_VERSION;

	if (sysctl(mib, 2, NULL, &len, NULL, 0) == -1) {
		ltfsmsg(LTFS_WARN, "17090W", "Length check");
		return;
	}

	kernel_version = malloc(len);
	if (!kernel_version) {
		/* Memory allocation failed */
		ltfsmsg(LTFS_ERR, "10001E", __FUNCTION__);
		return;
	}

	if (sysctl(mib, 2, kernel_version, &len, NULL, 0) == -1)
		ltfsmsg(LTFS_WARN, "17090W", "Getting kernel version");
	else if (len > 0)
		ltfsmsg(LTFS_INFO, "17087I", kernel_version);

	free(kernel_version);

	return;
}
#elif defined(mingw_PLATFORM)
{
	/* Windows kernel detection is not supported yet*/
	ltfsmsg(LTFS_INFO, "17087I", "Windows");
	return;
}
#else
{
	ltfsmsg(LTFS_INFO, "17087I", "Unknown kernel");
	return;
}
#endif
