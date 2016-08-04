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
** FILE NAME:       reed_solomon_crc.h
**
** DESCRIPTION:     Implements Reed-Solomon GF(256) CRC usd by LTO based drives
**
** AUTHOR:          Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  PISTE@jp.ibm.com
**
*************************************************************************************
*/

#ifndef __reed_solomon_crc_h
#define __reed_solomon_crc_h

#ifdef __cplusplus
extern "C" {
#endif

void *memcpy_rs_gf256_enc(void *dest, const void *src, size_t n);
int  memcpy_rs_gf256_check(void *dest, const void *src, size_t n);
void rs_gf256_enc(void *buf, size_t n);
int  rs_gf256_check(void *buf, size_t n);

#ifdef __cplusplus
}
#endif

#endif // __reed_solomon_crc_h
