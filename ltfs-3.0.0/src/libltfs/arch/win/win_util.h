/*
**  %Z% %I% %W% %G% %U%
**
**  ZZ_Copyright_BEGIN
**
**
**  Licensed Materials - Property of IBM
**
**  IBM Linear Tape File System Single Drive Edition Version 1.3.0.0 for Linux and Mac OS X
**
**  Copyright IBM Corp. 2010, 2012
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
** FILE NAME:       arch/win/win_util.h
**
** DESCRIPTION:     MinGW (Windows) specific definitions and prototypes
**
** AUTHOR:          Atsushi Abe
**                  IBM Yamato, Japan
**                  PISTE@jp.ibm.com
**
*************************************************************************************
**
** Copyright (C) 2012 OSR Open Systems Resources, Inc.
** 
************************************************************************************* 
*/

#ifndef __WIN_UTIL_H__
#define __WIN_UTIL_H__

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_INC_STAT) || defined(_STAT_H_) || defined(_TYPES_H_)  || defined(_INC_TYPES)
#error win_util.h must be included first!!!!!
#endif

#include <sys/types.h> /* Used in struct stat definition */
#ifndef HP_mingw_BUILD
#include <sys/stat.h>
#endif /* HP_mingw_BUILD */
#include <unistd.h>    /* Definitions of uid_t and gid_t */
#include <basetyps.h>  /* Used for UUID related functions */
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <errno.h>
#if defined(HP_mingw_BUILD)
#include <dirent.h>
#endif
#include <direct.h>    /* _mkdir() */
#include <sys/locking.h>

#include "libltfs/ltfslogging.h"

#define WIN32_LEAN_AND_MEAN

/* linux unique status defined in sys/stat.h */
#define S_IRGRP 0
#define S_IROTH 0
#define S_IRWXG 0
#define S_IRWXO 0
#define S_IWGRP 0
#define S_IWOTH 0

/* The error numbers not defined in MinGW */
#ifndef EIDRM
#define EIDRM      36
#endif

#ifndef EBADSLT
#define EBADSLT    55
#endif

#ifndef ENODATA
#define ENODATA    61
#endif
#ifndef EOPNOTSUPP
#define EOPNOTSUPP 95
#endif
#ifndef ENOBUFS
#define ENOBUFS    105
#endif

#ifndef ETIMEDOUT
#define ETIMEDOUT  110
#endif

#ifndef EUCLEAN
#define EUCLEAN    117
#endif

#ifndef EMSGSIZE
#define EMSGSIZE   122
#endif

#ifndef EPROTONOSUPPORT
#define EPROTONOSUPPORT 123
#endif

#ifndef ECANCELED
#define ECANCELED  125
#endif

#ifndef ENOMEDIUM
#define ENOMEDIUM  135
#endif

/* limits.h */
#ifndef LINE_MAX
#define LINE_MAX 2048
#endif

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 255
#endif

/* Definitions for xattr */
#define XATTR_REPLACE 0x0001
#define XATTR_CREATE  0x0002

/* Definitions for LOG */
#define LOG_ERR     0
#define LOG_WARNING 1
#define LOG_INFO    2
#define LOG_DEBUG   3
#define openlog(a, b, c)           win_ltfs_dummy()

/* Definitions for UUID */
#define UUID_TEXT_SIZE 37

/* IPC */
#define IPC_PRIVATE 0
#define IPC_RMID 0

#define pipe(fd) _pipe(fd, (LINE_MAX*2), O_BINARY)

/* Dynamic loading */
#define dlopen(filename,flag) (void*)LoadLibrary(filename)
#define dlclose(handle) ((int)!FreeLibrary((HMODULE)(handle)))
#define dlsym(handle,symbol) (void*)GetProcAddress((HMODULE)(handle),(symbol))
const char* dlerror(void);

/* mkdir */
#define mkdir(name, mode) _mkdir(name)

/* strerror_r */
#define strerror_r(errnum, buf, size) strerror_s(buf, size, errnum) ? NULL: buf

/* 
 * OSR
 *
 * In our implementation, we have a real FUSE layer so we don't
 * need all of this
 *
 */
#ifndef HP_mingw_BUILD
/* FUSE compatibility */
#undef fuse_opt_add_arg
#undef fuse_opt_free_args
#undef fuse_opt_parse
#undef fuse_main
#define fuse_opt_add_arg(a, b)     win_ltfs_dummy()
#define fuse_opt_free_args(a)      win_ltfs_dummy()
#define fuse_opt_parse(a, b, c, d) win_ltfs_dummy()
#define fuse_main(a, b, c, d)      win_ltfs_dummy()
#endif /* HP_mingw_BUILD */

#if defined(LTFS_MINGW_W64) || defined(LTFS_MINGW_W32)
#ifndef HAVE_STRUCT_TIMESPEC
#define HAVE_STRUCT_TIMESPEC
#endif /* HAVE_STRUCT_TIMESPEC */
#endif /* defined(LTFS_MINGW_W64) || defined(LTFS_MINGW_W32) */

#ifdef _WIN64
/* 
 * OSR
 *
 * In our MinGW environment we need a correctly defined
 * __WORDSIZE for 64bit builds
 *
 */
#ifdef HP_mingw_BUILD
#define __WORDSIZE 64
#endif /* HP_mingw_BUILD */
#endif /* _WIN64 */

/* Date/Time compatibility*/
#if !defined(HAVE_STRUCT_TIMESPEC) && !defined(_TIMESPEC_DEFINED)
/* To avoid re-definition in pthread.h */
#define HAVE_STRUCT_TIMESPEC 1
struct timespec {
        time_t tv_sec;
        long   tv_nsec;
};
#endif /* !defined(HAVE_STRUCT_TIMESPEC) && !defined(_TIMESPEC_DEFINED) */

typedef int64_t	ltfs_time_t;
struct ltfs_timespec {
	ltfs_time_t	tv_sec;
	long		tv_nsec;
};

#ifndef _TM_DEFINED
#define _TM_DEFINED
struct tm
{
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
    char *tm_zone;
};/* End structure*/
#endif /* _TM_DEFINED */

/* time zone */
#define TIMEZONE_UTC "Coordinated Universal Time"

/* types for block size*/
#ifndef _BLKSIZE_T_
#define _BLKSIZE_T_
typedef unsigned long blksize_t;
typedef unsigned long blkcnt_t;
#endif /* Not _BLKSIZE_T */

/* Stat compatibility*/
/* Override the definitions in stat.h, wchar.h */
/*
 * The structure manipulated and returned by stat and fstat.
 *
 * NOTE: If called on a directory the values in the time fields are not only
 * invalid, they will cause localtime et. al. to return NULL. And calling
 * asctime with a NULL pointer causes an Invalid Page Fault. So watch it!
 */
struct _stat_libltfs
{
	_dev_t  st_dev;         /* Equivalent to drive number 0=A 1=B ... */
	_ino_t  st_ino;         /* Always zero ? */
	_mode_t st_mode;        /* See above constants */
	short   st_nlink;       /* Number of links. */
	short   st_uid;         /* User: Maybe significant on NT ? */
	short   st_gid;         /* Group: Ditto */
	_dev_t  st_rdev;        /* Seems useless (not even filled in) */
	_off64_t  st_size;        /* File size in bytes */
	__time64_t  st_atime;       /* Accessed date (always 00:00 hrs local
	 * on FAT) */
	__time64_t  st_mtime;       /* Modified time */
	__time64_t  st_ctime;       /* Creation time */
	struct timespec st_atim;       /* Accessed date (always 00:00 hrs local
	 * on FAT) */
	struct timespec  st_mtim;       /* Modified time */
	struct timespec  st_ctim;       /* Creation time */
	blksize_t st_blksize;
	blkcnt_t  st_blocks;
};

struct stat_libltfs
{
	dev_t   st_dev;         /* Equivalent to drive number 0=A 1=B ... */
	ino_t   st_ino;         /* Always zero ? */
	mode_t  st_mode;        /* See above constants */
	short   st_nlink;       /* Number of links. */
	short   st_uid;         /* User: Maybe significant on NT ? */
	short   st_gid;         /* Group: Ditto */
	dev_t   st_rdev;        /* Seems useless (not even filled in) */
	off64_t   st_size;        /* File size in bytes */
	__time64_t  st_atime;       /* Accessed date (always 00:00 hrs local
	 * on FAT) */
	__time64_t  st_mtime;       /* Modified time */
	__time64_t  st_ctime;       /* Creation time */
	struct timespec st_atim;       /* Accessed date (always 00:00 hrs local
	 * on FAT) */
	struct timespec  st_mtim;       /* Modified time */
	struct timespec  st_ctim;       /* Creation time */
	blksize_t st_blksize;
	blkcnt_t  st_blocks;
};

#ifndef _STAT_DEFINED
#define _STAT_DEFINED
/* 
 * OSR
 *
 * In our MinGW environment we always use a 64bit stat structure
 *
 */
#if defined(_WIN64) || defined(HP_mingw_BUILD)
struct _stat64i32 {
	_dev_t st_dev;
	_ino_t st_ino;
	unsigned short st_mode;
	short st_nlink;
	short st_uid;
	short st_gid;
	_dev_t st_rdev;
	_off_t st_size;
	__time64_t st_atime;
	__time64_t st_mtime;
	__time64_t st_ctime;
	struct timespec st_atim;       /* Accessed date (always 00:00 hrs local
	 * on FAT) */
	struct timespec  st_mtim;       /* Modified time */
	struct timespec  st_ctim;       /* Creation time */
	blksize_t st_blksize;
	blkcnt_t  st_blocks;
};

struct _stat64 {
	_dev_t st_dev;
	_ino_t st_ino;
	unsigned short st_mode;
	short st_nlink;
	short st_uid;
	short st_gid;
	_dev_t st_rdev;
	__MINGW_EXTENSION __int64 st_size;
	__time64_t st_atime;
	__time64_t st_mtime;
	__time64_t st_ctime;
	struct timespec st_atim;       /* Accessed date (always 00:00 hrs local
	 * on FAT) */
	struct timespec  st_mtim;       /* Modified time */
	struct timespec  st_ctim;       /* Creation time */
	blksize_t st_blksize;
	blkcnt_t  st_blocks;
};
#else
#undef _STAT_DEFINED
#endif /* defined(_WIN64) || defined(HP_mingw_BUILD) */
#endif /* _STAT_DEFINED */

int stat_libltfs(const char *_Filename,struct stat_libltfs *_Stat);
int fstat_libltfs(int _Desc,struct stat_libltfs *_Stat);
int wstat_libltfs(const wchar_t *_Filename,struct stat_libltfs *_Stat);
struct tm *gmtime_libltfs(const time_t *timep, struct tm *result);

/* NOTE: MinGW does not support _FILE_OFFSET_BITS mechanism
 * but fuse.h requires _FILE_OFFSET_BITS set to 64.
 */
#define _FILE_OFFSET_BITS 64
#ifndef HP_mingw_BUILD
#ifndef WIN_UTIL_C
#define _stat _stat_libltfs
#define stat stat_libltfs
#define fstat fstat_libltfs
#define wstat wstat_libltfs
#define _off_t off64_t
#define off_t off64_t
#endif /* WIN_UTIL_C */
#endif /* HP_mingw_BUILD */

#ifndef HP_mingw_BUILD
/* We move this structure definition into our statvfs.h to
   share with the FUSE headers */
struct statvfs
{
	blksize_t st_blksize;
	blkcnt_t  st_blocks;
	unsigned long f_fsid;
	unsigned long f_namemax;
	unsigned long f_bsize;
	unsigned long f_frsize;
	unsigned long f_blocks;
	unsigned long f_bfree;
	unsigned long f_favail;
	unsigned long f_bavail;
	/*unsigned long f_bfree;*/
	unsigned long f_files;
	unsigned long f_ffree;
	unsigned long f_flag;
};
#endif /* HP_mingw_BUILD */

/* Misc utilities */
//#define strcasestr(s1, s2) strstr((s1), (s2))  // Need to change!!
#define MAX(a,b)    ( (a)>=(b)?(a):(b) )
/* 
 * OSR
 *
 * The Sleep API takes milliseconds, not seconds
 *
 */
#define sleep(a) Sleep((a) * 1000)
/* #define fuse_opt_parse(args, data, opts, proc) windows_opt_parse(args, data, opts, proc) */

/* signal */
typedef void(*sighandler_t)(int);

/* 
 * OSR
 *
 * Pick a more Windows friendly location for the config file
 *
 */
#if defined(HP_mingw_BUILD) && defined(LTFS_CONFIG_FILE)
#undef LTFS_CONFIG_FILE
#define LTFS_CONFIG_FILE "C:/ProgramData/Hewlett-Packard/LTFS/ltfs.conf"
#endif

int _asprintf(char **strp, const char *fmt, ...);
#define asprintf _asprintf

int setenv(const char *name, const char *value, int overwrite);
int unsetenv( const char *name );
void gen_uuid_win(char *uuid_str);
int get_win32_current_timespec(struct ltfs_timespec* now);
struct tm *get_win32_localtime(const ltfs_time_t *timep);
struct tm *get_win32_gmtime(const ltfs_time_t *timep);
char *get_local_timezone();
bool get_local_daylight();
int geteuid(void);
int getegid(void);
char *strcasestr( const char* searchstr, const char* fromstr);

/* 
 * OSR
 *
 * This causes a collision in our MinGW environment
 *
 */
struct tm *gmtime_libltfs(const time_t *timep, struct tm *result);
#if defined(HP_mingw_BUILD) && !defined(strtok_r)
char *strtok_r(char *str, const char *delim, char **saveptr);
#endif /* defined(HP_mingw_BUILD) && !defined(strtok_r) */

int win_ltfs_dummy(void);

#if defined(HP_mingw_BUILD)
int scandir(const char *dirp, 
            struct dirent ***namelist,
            int (*filter)(const struct dirent *entry),
            int (*compare)(const void* p1, const void* p2));
#endif /* defined(HP_mingw_BUILD) */

HANDLE shmget(int key, size_t size, int flag);
void *shmat(HANDLE handle, void* addr, int flag);
int shmdt(const void* addr);
int shmctl(HANDLE handle, int cmd, void* buf);
char *strndup(const char *src, size_t size);
int runcommand(char *command, char **command_output, int *ret);

#ifdef __cplusplus
}
#endif

#endif /* Not for __WIN_UTIL_H__ */
