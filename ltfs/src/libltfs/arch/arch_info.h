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
** FILE NAME:       arch/arch_info.h
**
** DESCRIPTION:     Prototypes for platform information
**
** AUTHOR:          Atsushi Abe
**                  IBM Yamato, Japan
**                  PISTE@jp.ibm.com
**
*************************************************************************************
*/

#ifndef arch_info_h_
#define arch_info_h_

#if defined(__linux__)

#if defined(__i386__)
#define BUILD_SYS_FOR "This binary is built for Linux (i386)"
#define BUILD_SYS_GCC __VERSION__
#elif defined(__x86_64__)
#define BUILD_SYS_FOR "This binary is built for Linux (x86_64)"
#define BUILD_SYS_GCC __VERSION__
#elif defined(__ppc__)
#define BUILD_SYS_FOR "This binary is built for Linux (ppc)"
#define BUILD_SYS_GCC __VERSION__
#elif defined(__ppc64__)
#define BUILD_SYS_FOR "This binary is built for Linux (ppc64)"
#define BUILD_SYS_GCC __VERSION__
#else
#define BUILD_SYS_FOR "This binary is built for Linux (unknown)"
#define BUILD_SYS_GCC __VERSION__
#endif

#elif defined(__APPLE__)

#define BUILD_SYS_FOR "This binary is built for Mac OS X "
#define BUILD_SYS_GCC __VERSION__

#elif defined(mingw_PLATFORM)

#define BUILD_SYS_FOR "This binary is built for Windows"
#define BUILD_SYS_GCC __VERSION__

#else

#define BUILD_SYS_FOR "This binary is built on an unknown OS"
#define BUILD_SYS_GCC __VERSION__

#endif

void show_runtime_system_info(void);

#endif /* arch_info_h_ */
