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
** FILE NAME:       arch/win/win_util.c
**
** DESCRIPTION:     Implements MinGW (Windows) specific functions
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

#define WIN_UTIL_C
#include "win_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <windows.h>
#include <objbase.h>
#include <cguid.h>

#ifdef HP_mingw_BUILD
#include <stddef.h>
#include <dirent.h>
/* 
 * OSR
 * 
 * Coerce the build environment into including the correct 
 * versions of OLEAUT and RPC 
 *  
 */
#define _OLEAUT32_ 1
#define _RPCRT4_   1
#endif

#include <oleauto.h>
#include <rpc.h>

#include <time.h>
#include "ltfs_fuse_version.h"
#include <fuse.h>

const char* dlerror()
{
	static char szMsgBuf[256];
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			GetLastError(),
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			szMsgBuf,
			sizeof szMsgBuf,
			NULL);
	return szMsgBuf;
}


int _asprintf(char **strp, const char *fmt, ...)
{
#define BUF_SIZE_ASPRINTF (4098);

	va_list ap;
	char *buffer = NULL, *aux;
	size_t alloclen = 0;
	size_t len;

	while (1) {
		alloclen += BUF_SIZE_ASPRINTF;
		aux = realloc(buffer, alloclen);
		if (buffer) free(buffer);
		if (! aux) {
			return -1;
		}
		buffer = aux;
		memset(buffer, '\0', alloclen);
		va_start(ap, fmt);
		len = vsnprintf(buffer, alloclen, fmt, ap);
		va_end(ap);
		if ((int) len == -1) {
			free(buffer);
			return -1;
		} else if (len < alloclen)
			break;
	}

	aux = strdup(buffer);
	free(buffer);

	if(!aux) {
		return -1;
	}

	*strp = aux;
	return len;
}

int setenv(const char *name, const char *value, int overwrite )
{
	SetEnvironmentVariable( name, value );
	return 0;
}

int unsetenv( const char *name )
{
#ifdef HP_mingw_BUILD
	/* Fix compiler warning */
	FreeEnvironmentStrings( (LPCH)name );
#else
	FreeEnvironmentStrings( name );
#endif
	return 0;
}

void gen_uuid_win(char *uuid_str)
{
	UUID id;
	unsigned char *str;

	if( UuidCreate(&id) != RPC_S_OK )
		CoCreateGuid(&id);

	UuidToString(&id, &str);
	lstrcpyn(uuid_str, (char *)str, 38);
	RpcStringFree(&str);
}

int get_win32_current_timespec(struct ltfs_timespec* now)
{
#define DATES_FOR_70Y   (70*365+(70/4))

	/* GetSystemTime returns UTC.            */
	/* clock_gettime() returns Unixtime      */
	/* (seconds from 1970/01/01 00:00:00)    */
	/* VariantTime shows a date between 1900 */
	/* /01/01 and 9999/12/31                 */

	SYSTEMTIME time;
	double     vtime;

	GetSystemTime(&time);
	SystemTimeToVariantTime( &time, &vtime );

	now->tv_nsec = time.wMilliseconds * 1000;
	now->tv_sec = time.wSecond +
			time.wMinute * 60 +
			time.wHour * 60 * 60 +
			((int)vtime-2-DATES_FOR_70Y) * 24 * 60 * 60;

	return 0;
}

struct tm buffer_localtime;

struct tm *get_win32_localtime(const ltfs_time_t *timep)
{
	struct tm *t;
	time_t tTmp = *timep;

	t = localtime(&tTmp);

	buffer_localtime.tm_sec   = t->tm_sec;
	buffer_localtime.tm_min   = t->tm_min;
	buffer_localtime.tm_hour  = t->tm_hour;
	buffer_localtime.tm_mday  = t->tm_mday;
	buffer_localtime.tm_mon   = t->tm_mon;
	buffer_localtime.tm_year  = t->tm_year;
	buffer_localtime.tm_wday  = t->tm_wday;
	buffer_localtime.tm_yday  = t->tm_yday;
	buffer_localtime.tm_isdst = t->tm_isdst;
#if 1
	buffer_localtime.tm_zone  = "   ";
#else
	if (buffer_localtime.tm_isdst) {
		buffer_localtime.tm_zone  = _tzname[1];
	} else {
		buffer_localtime.tm_zone  = _tzname[0];
	}
#endif

	return &buffer_localtime;
}

struct tm buffer_gmtime;

struct tm *get_win32_gmtime(const ltfs_time_t *timep)
{
	struct tm *t;
	time_t tTmp = *timep;

	t = gmtime(&tTmp);

	buffer_gmtime.tm_sec   = t->tm_sec;
	buffer_gmtime.tm_min   = t->tm_min;
	buffer_gmtime.tm_hour  = t->tm_hour;
	buffer_gmtime.tm_mday  = t->tm_mday;
	buffer_gmtime.tm_mon   = t->tm_mon;
	buffer_gmtime.tm_year  = t->tm_year;
	buffer_gmtime.tm_wday  = t->tm_wday;
	buffer_gmtime.tm_yday  = t->tm_yday;
	buffer_gmtime.tm_isdst = t->tm_isdst;
#if 1
	buffer_gmtime.tm_zone  = "   ";
#else
	buffer_gmtime.tm_zone  = _tzname[0];
#endif

	return &buffer_gmtime;
}

char* get_local_timezone(void)
{
	_tzset();
	return _tzname[0];
}

bool get_local_daylight(void)
{
	_tzset();
	return _daylight != 0;
}

int geteuid(void)
{
	return 0;
}

int getegid(void)
{
	return 0;
}

char* strcasestr( const char* searchstr, const char* fromstr)
{
	return strstr( searchstr, fromstr );
}

char *strtok_r(char *str, const char *delim, char **saveptr)
{
	if(!delim || !saveptr || (!str && !*saveptr)){
		errno = EINVAL;
		return NULL;
	}

	char* start;
	if(!str) {
		if(strlen(*saveptr) == 0)
			return NULL;
		start = *saveptr;
	}
	else
		start = str;

	while(*start != '\0' && strchr(delim, *start)) {
		start++;
	}
	if (*start == '\0')
		return NULL;

	char* next = start;
	while(*next != '\0' && !strchr(delim, *next)) {
		next++;
	}

	while(*next != '\0' && strchr(delim, *next)) {
		*next = '\0';
		next++;
	}
	if (*next == '\0') {
		*saveptr = NULL;
	}
	else {
		*saveptr = next;
	}

	return start;
}

int win_ltfs_dummy(void)
{
	return 0;
}

struct tm *gmtime_libltfs(const time_t *timep, struct tm *result)
{
	FILETIME	  ft;
	LARGE_INTEGER l;
	SYSTEMTIME	st;
	struct tm	 *t;

	l.QuadPart = Int32x32To64(*timep, 10000000) + 116444736000000000ll;
	ft.dwLowDateTime = l.LowPart;
	ft.dwHighDateTime = l.HighPart;

	FileTimeToSystemTime(&ft, &st);
	result->tm_sec  = st.wSecond;
	result->tm_min  = st.wMinute;
	result->tm_hour = st.wHour;
	result->tm_mday = st.wDay;
	result->tm_mon  = st.wMonth - 1;
	result->tm_year = st.wYear - 1900;
	t = result;

	return t;
}

/* 
 * OSR
 * 
 * Our MinGW environment has no scandir, so create one
 *  
 */
#ifdef HP_mingw_BUILD
int scandir(const char *dirp,
		struct dirent ***namelist,
		int (*filter)(const struct dirent *entry),
		int (*compare)(const void* p1, const void* p2)) {

	DIR *dir;
	struct dirent* entry;
	int entry_count = 0;
	int i;
	struct dirent **dirent_array;
	struct dirent **dirent_array_old;
	struct dirent *dirent_copy;
	int max_items;

	dir = opendir(dirp);

	if (dir == NULL) {
		return -ENOENT;
	}

	/* make a guess on how many entries we'll end up with */
	max_items = 1024;
	dirent_array = (struct dirent **)malloc(max_items * sizeof(struct dirent *));
	if (dirent_array == NULL) {
		return -ENOMEM;
	}

	entry = readdir(dir);
	/* Keep reading until there are no more entries */
	while (entry != NULL) {
		/* Check with the filter if there is one */
		if (filter == NULL || (*filter)(entry) != 0) {
			/* A match! */
			if (entry_count == max_items) {
				/* Need to reallocate our return array */
				max_items *= 2;
				dirent_array_old = dirent_array;
				dirent_array =
						(struct dirent **)realloc(dirent_array,
								max_items * sizeof(struct dirent *));
				if (dirent_array == NULL) {
					for (i = 0; i < entry_count; i++) {
						free(dirent_array_old[i]);
					}
					free(dirent_array_old);
					return -ENOMEM;
				}
			}

			/* Get a copy of the dirent */
			dirent_copy = (struct dirent *)malloc(sizeof(struct dirent));
			if (dirent_copy == NULL) {
				for (i = 0; i < entry_count; i++) {
					free(dirent_array[i]);
				}
				free(dirent_array);
				return -ENOMEM;
			}

			/* Add it to the list */
			memcpy(dirent_copy, entry, offsetof(struct dirent, d_name));
			memcpy(dirent_copy->d_name, entry->d_name, entry->d_namlen + 1);
			dirent_array[entry_count++] = dirent_copy;
		}
		entry = readdir(dir);
	}
	closedir(dir);

	if (entry_count != 0) {
		/* Sort the entries if we have to */
		if (compare != NULL) {
			qsort(dirent_array, entry_count, sizeof(struct dirent *), compare);
		}
	}
	*namelist = dirent_array;
	return entry_count;
}
#endif

/**
 * 'strndup' implementation for mingw based builds. Since Mingw32 doesn't support strndup. We
 * make the calculations ourselves and use the standard strdup.
 * @param src The source string of which a duplicate has to be created.
 * @param size The number of characters that have to  be used from the 'src' to create a duplicate.
 * @return A duplicate string of the 'src' string or NULL for any failures.
 */
char *strndup(const char *src, size_t size)
{
	char	*ret_buf = NULL;

	/* If the requested size is less than the size of the source string, we have to allocate 'size'
	 * amount of memory and copy the string into the return buffer.
	 */
	if (strlen(src) > size) {
		if ((ret_buf = malloc(1 + size)) != NULL) {
			memcpy(ret_buf, src, size);
			ret_buf[size] = '\0';
		}
		return ret_buf;
	}

	/* Use the standard strdup for sizes equal to or greater than the size of the 'src' string. */
	return strdup(src);
}
