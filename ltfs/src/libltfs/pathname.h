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
** FILE NAME:       pathname.h
**
** DESCRIPTION:     Header file for Unicode text analysis and processing routines.
**
** AUTHORS:         Brian Biskeborn
**                  IBM Almaden Research Center
**                  bbiskebo@us.ibm.com
**
*************************************************************************************
**
** Copyright (C) 2012 OSR Open Systems Resources, Inc.
** 
************************************************************************************* 
*/

/** \file
 * Functions for converting and manipulating file, directory, and extended attribute names.
 */

#ifndef __PATHNAME_H__
#define __PATHNAME_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdbool.h>

#ifdef __APPLE__
#include <ICU/unicode/utypes.h>
#else

/* 
 * OSR
 * 
 * Some ICU header files are ill formed and do not include the
 * right files for the MinGW environment if __MINGW32__ is 
 * defined. Strange, yes, but true 
 *  
 */
#if defined(HP_mingw_BUILD) && defined(__MINGW32__)

#undef __MINGW32__
#include <unicode/utypes.h>
#define __MINGW32__

#else 
#include <unicode/utypes.h>
#endif /* #if defined(HP_mingw_BUILD) && defined(__MINGW32__) */


#endif

int pathname_format(const char *name, char **new_name, bool validate, bool path);
int pathname_unformat(const char *name, char **new_name);
int pathname_caseless_match(const char *name1, const char *name2, int *result);
int pathname_prepare_caseless(const char *name, UChar **new_name, bool use_nfc);
int pathname_normalize(const char *name, char **new_name);
int pathname_validate_file(const char *name);
int pathname_validate_xattr_name(const char *name);
int pathname_validate_xattr_value(const char *name, size_t size);
int pathname_strlen(const char *name);
int pathname_truncate(char *name, size_t size);
int pathname_nfd_normaize(const char *name, char **new_name);
int _pathname_utf16_to_utf8_icu(const UChar *src, char **dest);
#ifdef __cplusplus
}
#endif

#endif /* __PATHNAME_H__ */
