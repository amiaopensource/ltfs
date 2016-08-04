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
** FILE NAME:       ltfs_types.h
**
** DESCRIPTION:     Type declarations used by several modules.
**
** AUTHORS:         Brian Biskeborn
**                  IBM Almaden Research Center
**                  bbiskebo@us.ibm.com
**
*************************************************************************************
*/

#ifndef __ltfs_types_h__
#define __ltfs_types_h__

#include "arch/time_internal.h"

typedef uint32_t tape_partition_t;
typedef uint64_t tape_block_t;

struct tc_coherency {
	uint64_t      volume_change_ref; /**< VWJ from the drive */
	uint64_t      count;             /**< Generation of Index */
	uint64_t      set_id;            /**< Position of Index   */
	char          uuid[37];          /**< Volume UUID */
	unsigned char version;           /**< Version field */
};

/* Structure of cartridge health */
#define UNSUPPORTED_CARTRIDGE_HEALTH ((int64_t)(-1))

typedef struct tc_cartridge_health {
	int64_t  mounts;           /* Total number of mount in the volume lifetime */
	uint64_t written_ds;       /* Total number of data sets written in the volume lifetime */
	int64_t  write_temps;      /* Total number of recoverd write error in the volume lifetime */
	int64_t  write_perms;      /* Total number of unrecoverd write error in the volume lifetime */
	uint64_t read_ds;          /* Total number of data sets read in the volume lifetime */
	int64_t  read_temps;       /* Total number of recoverd read error in the volume lifetime */
	int64_t  read_perms;       /* Total number of unrecoverd read error in the volume lifetime */
	int64_t  write_perms_prev; /* Unrecoverd write errors in previous mount */
	int64_t  read_perms_prev;  /* Unrecoverd read errors in previous mount */
	uint64_t written_mbytes;   /* Total number of mega bytes written in the volume lifetime */
	uint64_t read_mbytes;      /* Total number of mega bytes read in the volume lifetime */
	int64_t  passes_begin;     /* Count of the total number of times the beginning of medium position has passed */
	int64_t  passes_middle;    /* Count of the total number of times the middle of medium position has passed */
	int64_t  tape_efficiency;  /* Tape efficiency (0-255) */
} cartridge_health_info;

struct dentry_attr {
	uint64_t size;
	uint64_t alloc_size;
	uint64_t blocksize;
	uint64_t uid;
	uint32_t nlink;
	struct ltfs_timespec create_time;
	struct ltfs_timespec access_time;
	struct ltfs_timespec modify_time;
	struct ltfs_timespec change_time;
	struct ltfs_timespec backup_time;
	bool readonly;
	bool isdir;
	bool isslink;
};

struct tc_mam_attr {
	char *appl_vendor;		   /* The LTFS application vendor */
	char *appl_name;		   /* The LTFS application name */
	char *appl_ver;			   /* The LTFS application version */
	char *appl_format_ver;	   /* The LTFS application format version */
	char *volume_name;		   /* The User defined tape volume name */
	char *barcode;		   	   /* The User defined tape barcode name */
};

#endif /* __ltfs_types_h__ */
