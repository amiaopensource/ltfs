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
** FILE NAME:       ltfs.h
**
** DESCRIPTION:     Defines LTFS data structures and prototypes for libltfs.
**
** AUTHORS:         Brian Biskeborn
**                  IBM Almaden Research Center
**                  bbiskebo@us.ibm.com
**
**                  Lucas C. Villa Real
**                  IBM Almaden Research Center
**                  lucasvr@us.ibm.com
**
*************************************************************************************
**
**  (C) Copyright 2015 Hewlett Packard Enterprise Development LP.
**  06/10/10 Define PACKAGE_OWNER and reference it in ltfs.c to make it configurable
**           Replace PACKAGE_VERSION with HP nomenclature
**  11/12/12 Define SOFTWARE_PRODUCT_NAME for use in xattr.c
**
*************************************************************************************
**
** Copyright (C) 2012 OSR Open Systems Resources, Inc.
** 
************************************************************************************* 
*/
#ifndef __ltfs_h__
#define __ltfs_h__

#ifdef __cplusplus
extern "C" {
#endif

#ifdef mingw_PLATFORM
#include "arch/win/win_util.h"
#endif


#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifndef mingw_PLATFORM
#include <sys/ipc.h>
#include <sys/shm.h>
#endif

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
#endif /* __APPLE__ */

#ifndef mingw_PLATFORM
#include <sys/xattr.h>
#endif

#include "libltfs/arch/signal_internal.h"
#include "libltfs/arch/arch_info.h"
#include "libltfs/ltfs_error.h"
#include "libltfs/ltfslogging.h"
#include "libltfs/ltfstrace.h"
#include "libltfs/ltfs_types.h"
#include "libltfs/ltfs_thread.h"
#include "libltfs/ltfs_locking.h"
#include "libltfs/queue.h"
#include "libltfs/uthash.h"
#include "libltfs/arch/time_internal.h"
#include "tape_ops.h"

/* forward declarations from tape.h, tape_ops.h */
struct tape_ops;
struct device_data;

#ifndef LTFS_DEFAULT_WORK_DIR
#ifdef mingw_PLATFORM
#define LTFS_DEFAULT_WORK_DIR         "c:/tmp/ltfs"
#else
#define LTFS_DEFAULT_WORK_DIR         "/tmp/ltfs"
#endif /* mingw_PLATFORM */
#endif /* LTFS_DEFAULT_WORK_DIR */

#define LTFS_BUILD_VERSION            47 /* Build version */
#define LTFS_MIN_CACHE_SIZE_DEFAULT   25 /* Default minimum cache size (MiB) */
#define LTFS_MAX_CACHE_SIZE_DEFAULT   50 /* Default maximum cache size (MiB) */
#define LTFS_SYNC_PERIOD_DEFAULT (5 * 60) /* default sync period (5 minutes) */

#define LTFS_NUM_PARTITIONS           2
#define LTFS_FILENAME_MAX             255
#define LTFS_MAX_XATTR_SIZE           4096

#define LTFS_SUPER_MAGIC              0x7af3
#define LTFS_DEFAULT_BLOCKSIZE        (512*1024)
#define LTFS_MIN_BLOCKSIZE            4096
#define LTFS_LABEL_MAX                4096

#define LTFS_CALC_VOLUME_SIZE(size,block_size,units)	(double)((double)(size) * (double)(block_size) \
																/(double) (units))
#define LTFS_CRC_SIZE                 (4)

#define MAKE_LTFS_VERSION(x,y,z)      (10000*(x) + 100*(y) + (z))

#define LTFS_FORMAT_MAJOR(v)          (v / 10000)
#define LTFS_FORMAT_MINOR(v)          ((v - ((v / 10000) * 10000))/100)
#define LTFS_FORMAT_REVISION(v)       (v % 100)

#define LTFS_LABEL_VERSION_MIN        MAKE_LTFS_VERSION(1,0,0)   /* Min supported label version */
#define LTFS_LABEL_VERSION_MAX        MAKE_LTFS_VERSION(2,99,99) /* Max supported label version */
#define LTFS_LABEL_VERSION            MAKE_LTFS_VERSION(2,2,0)   /* Written label version */
#define LTFS_LABEL_VERSION_STR        "2.2.0"    /* Label version string */

#define LTFS_INDEX_VERSION_MIN        MAKE_LTFS_VERSION(1,0,0)    /* Min supported index version */
#define LTFS_INDEX_VERSION_MAX        MAKE_LTFS_VERSION(2,99,99)  /* Max supported index version */
#define LTFS_INDEX_VERSION            MAKE_LTFS_VERSION(2,2,0)    /* Written index version */
#define LTFS_INDEX_VERSION_STR        "2.2.0"  /* Index version string */

#define INDEX_MAX_COMMENT_LEN         65536 /* Maximum comment field length (per LTFS Format) */
#define MAX_VOLUME_NAME_SIZE 		  159   /* Maximum size of the user medium text label is 160 with a NULL character termination */

#ifdef __APPLE__
#define PACKAGE_NAME                  "LTFS"
#define PACKAGE_VERSION               "3.0.0"
#else
#include "config.h"
#endif

#ifdef HP_BUILD
#define PACKAGE_OWNER                 "HP LTFS"
#define LTFS_VENDOR_NAME              "HP "
#define SOFTWARE_PRODUCT_NAME         "StoreOpen Standalone"
#elif defined QUANTUM_BUILD
#define PACKAGE_OWNER                 "QUANTUM LTFS"
#define LTFS_VENDOR_NAME              "QUANTUM "
#define SOFTWARE_PRODUCT_NAME         "LTFS Standalone"
#elif defined GENERIC_OEM_BUILD
#define PACKAGE_OWNER                 "LTFS"
#define LTFS_VENDOR_NAME              "LTFS"
#define SOFTWARE_PRODUCT_NAME         "LTFS"
#else
#define PACKAGE_OWNER                 "IBM LTFS"
#define LTFS_VENDOR_NAME              "IBM "
#define SOFTWARE_PRODUCT_NAME         "LTFS SDE"
#endif
#define LTFS_LOSTANDFOUND_DIR         "_ltfs_lostandfound"

#if 0
#ifdef GENERIC_OEM_BUILD
#undef PACKAGE_OWNER
#define PACKAGE_OWNWER               "LTFS"
#undef LTFS_VENDOR_NAME
#define LTFS_VENDOR_NAME             "LTFS"
#undef SOFTWARE_PRODUCT_NAME
#define SOFTWARE_PRODUCT_NAME        "LTFS Standalone"
#endif
#endif
#define LTFS_LIVELINK_EA_NAME         "ltfs.vendor.IBM.prefixLength"

#ifdef __APPLE__
#define BYTE_MULTIPLIER					(1000 * 1000 * 1000)
#else
#define BYTE_MULTIPLIER					(1024 * 1024 * 1024)
#endif /* __APPLE__ */

#define INTERRUPTED_GOTO(rc, label)				\
	do{											\
		if (ltfs_is_interrupted()) {			\
			ltfsmsg(LTFS_INFO, "17159I");		\
			rc = -LTFS_INTERRUPTED;				\
			goto label;							\
		}										\
	}while (0)

#define INTERRUPTED_RETURN()					\
	do{											\
		if (ltfs_is_interrupted()) {			\
			ltfsmsg(LTFS_INFO, "17159I");		\
			return -LTFS_INTERRUPTED;			\
		}										\
	}while (0)

/* Never returns from this macro. Send abort signal and wait to abort */
#define KILL_MYSELF()							\
	do{											\
		kill(getpid(), SIGABRT);				\
		do {sleep(1);}while(1);					\
	}while(0)

/* Callback prototype used to list directories. The function must return 0 on success
 * or a negative value on error. */
typedef int (*ltfs_dir_filler) (void *buf, const char *name, void *priv);

/**
 * All capacities are relative to filesystem block size.
 */
struct device_capacity {
    unsigned long remaining_ip; /**< Remaining capacity of index partition */
    unsigned long remaining_dp; /**< Remaining capacity of data partition */
    unsigned long total_ip;     /**< Total capacity of index partition */
    unsigned long total_dp;     /**< Total capacity of data partition */
};

struct tape_offset {
	tape_block_t block;
	char partition;
};

/**
 * Extent information
 */
struct extent_info {
	TAILQ_ENTRY(extent_info) list;
	struct tape_offset start;
	uint32_t byteoffset;
	uint64_t bytecount;
	uint64_t fileoffset;
};

/**
 * Extended attributes
 */
struct xattr_info {
	TAILQ_ENTRY(xattr_info) list;
	char  *key;
	char  *value;
	size_t size;
};

struct name_list {
	struct dentry   *d;
	char            *name;
	uint64_t        uid;
	UT_hash_handle  hh;
};

struct dentry {
	/* When more than one of these locks is needed, take them in the order of
	 * iosched_lock, contents_lock, meta_lock. If the tape device lock is needed, take it
	 * before meta_lock. If locks are needed on a dentry's parent, take all parent locks before
	 * any dentry locks. */
	struct MultiReaderSingleWriter contents_lock;      /**< Lock for 'extentlist' and 'list' */
	struct MultiReaderSingleWriter meta_lock;          /**< Lock for metadata */
	ltfs_mutex_t iosched_lock;                      /**< Lock for use by the I/O scheduler */

	/* Immutable fields. No locks are needed to access these. */
	ino_t ino;               /**< Per-session inode number, unique across all LTFS volumes in this process */
	uint64_t uid;            /**< Persistent unique id. In single drive mode, this id is also used as inode number. */
	bool isdir;              /**< True if this is a directory, false if it's a file */
	bool isslink;            /**< True if this is a symlink, false if it's a file or directory */
	char *target;            /**< Target name of symbolic link */
	bool out_of_sync;        /**< This object was failed to sync */
	TAILQ_ENTRY(dentry) list; /**< To be tail q entry to manage out of sync dentry list */
	struct ltfs_volume *vol; /**< Volume to which this dentry belongs */
	size_t tag_count;        /**< Number of unknown tags */
	unsigned char **preserved_tags; /**< Unknown tags to be preserved on tape */

	/* Take the contents_lock before accessing these fields. */
	TAILQ_HEAD(extent_struct, extent_info) extentlist; /**< List of extents (file only) */

	/* Take the contents_lock and the meta_lock before writing to these fields. Take either of
	 * those locks before reading these fields. */
	uint64_t realsize;             /**< Size, not counting sparse tail */
	uint64_t size;                 /**< File size (logical EOF position) */
	bool     extents_dirty;        /**< Dirty flag of extents */
	uint64_t used_blocks;          /**< number of used block on tape */

	/* Take the meta_lock and parent's contents_lock before writing to these fields.
	 * Take either of those locks before reading these fields. */
	char    *name;                 /**< File or directory name */
	char    *platform_safe_name;   /**< File or directory name after file name mangling */
	struct dentry *parent;         /**< Pointer to parent dentry */

	/* Take the meta_lock before accessing these fields. */
	TAILQ_HEAD(xattr_struct, xattr_info) xattrlist;  /**< List of extended attributes */
	bool     readonly;             /**< True if file is marked read-only */
	struct ltfs_timespec creation_time; /**< Time of creation */
	struct ltfs_timespec modify_time;   /**< Time of last modification */
	struct ltfs_timespec access_time;   /**< Time of last access */
	struct ltfs_timespec change_time;   /**< Time of last status change */
	struct ltfs_timespec backup_time;   /**< Time of last backup */
	uint32_t numhandles;           /**< Reference count */
	uint32_t link_count;           /**< Number of file system links to this dentry */
	bool     deleted;              /**< True if dentry is unlinked from the file system */
	bool matches_name_criteria;    /**< True if file name matches the name criteria rules */
	void *dentry_proxy;            /**< dentry proxy corresponding to this dentry */
	bool need_update_time;         /**< True if write api has come from Windows side */

	/* Take the iosched_lock before accessing iosched_priv. */
	void *iosched_priv;            /**< I/O scheduler private data. */

	struct name_list *child_list;  /* for hash search */
};

/*
struct tape_attr {
	char vender[TC_MAM_APP_VENDER_SIZE + 1];
	char app_name[TC_MAM_APP_NAME_SIZE + 1];
	char app_ver[TC_MAM_APP_VERSION_SIZE + 1];
	char medium_label[TC_MAM_USER_MEDIUM_LABEL_SIZE + 1];
	unsigned char tli;
	char barcode[TC_MAM_BARCODE_SIZE + 1];
	char app_format_ver[TC_MAM_APP_FORMAT_VERSION_SIZE + 1];
};
*/

struct ltfs_volume {
	/* acquire this lock for read before using the volume in any way. acquire it for write before
	 * writing the index to tape or performing other exclusive operations. */
	struct MultiReaderSingleWriter lock; /**< Controls access to volume metadata */

	/* LTFS format data */
	struct tc_coherency ip_coh;    /**< Index partition coherency info */
	struct tc_coherency dp_coh;    /**< Data partition coherency info */
	struct ltfs_label *label;      /**< Information from the partition labels */
	struct ltfs_index *index;      /**< Current cartridge index */
	struct tc_mam_attr mam_attr;   /**< MAM attributes */

	/* Opaque handles to higher-level structures */
	void *iosched_handle;          /**< Handle to the I/O scheduler state */
	void *changer_handle;          /**< Handle to changer controller state */
	void *dcache_handle;           /**< Handle to the Dentry cache manager state */
	void *periodic_sync_handle;    /**< Handle to the periodic sync state */
	void *kmi_handle;              /**< Handle to the key manager interface state */

	/* Internal state variables */
	struct device_data *device;    /**< Device-specific data */
	bool ip_index_file_end;        /**< Does the index partition end in an index file? */
	bool dp_index_file_end;        /**< Does the data partition end in an index file? */
	bool rollback_mount;           /**< Is the volume mounted in rollback mount mode? */
	int  traverse_mode;            /**< Traverse strategy (rollback, list index, rollback mount) */
	bool skip_eod_check;           /**< Skip EOD existance check? */
	bool ignore_wrong_version;     /**< Ignore wrong index version while seeking index? */

	/* A 1-block read cache, used to prevent reading the same block from tape over and over.
	 * You MUST hold the tape device lock before accessing this buffer. */
	struct tape_offset last_pos;   /**< Position of last block read from the tape. */
	unsigned long last_size;       /**< Size of last block read from the tape. */
	char *last_block;              /**< Contents of last block read from the tape. */

	/* Caches of cartridge health and capacity data. Take the device lock before using these. */
	cartridge_health_info health_cache;
	uint64_t              tape_alert;
	struct device_capacity capacity_cache;

	/* User-controlled parameters */
	char *creator;                 /**< Creator string to use when writing labels, index files */
	void *opt_args;                /**< FUSE command-line arguments */
	size_t cache_size_min;         /**< Starting scheduler cache size in MiB */
	size_t cache_size_max;         /**< Maximum scheduler cache size in MiB */
	bool reset_capacity;           /**< Force to reset tape capacity when formatting tape */

	/* Revalidation control. If the cartridge in the drive changes externally, e.g. after
	 * a drive power cycle, it needs to be revalidated. During the revalidation, operations
	 * need to be blocked, and if the revalidation fails, operations need to be permanently
	 * blocked (apart from unmount). */
	ltfs_thread_mutex_t reval_lock;
	ltfs_thread_cond_t  reval_cond;
	int reval;                     /**< One of 0, -LTFS_REVAL_RUNNING, -LTFS_REVAL_FAILED */
	bool append_only_mode;         /**< Use append-only mode */
	bool set_pew;                  /**< Set PEW value */

	bool livelink;                 /**< Live Link enabled? (SDE) */
	char *mountpoint;              /**< Store mount point for Live Link (SDE) */
	size_t mountpoint_len;         /**< Store mount point path length (SDE) */
	/*struct tape_attr *t_attr;*/  /**< Tape Attribute data */
};

struct ltfs_label {
	char *creator;                 /**< Program that wrote this label */
	int version;                   /**< Label format version, as formatted by MAKE_LTFS_VERSION */
	char barcode[7];               /**< Tape barcode number read from the ANSI label */
	char vol_uuid[37];             /**< LTFS volume UUID */
	struct ltfs_timespec format_time;   /**< time when this volume was formatted */
	unsigned long blocksize;       /**< Preferred tape blocksize */
	bool enable_compression;       /**< Enable data compression on tape */

	/* physical <-> logical partition mapping */
	char this_partition;           /**< Logical ID of this partition (used on read) */
	char partid_dp;                /**< Logical ID of data partition */
	char partid_ip;                /**< Logical ID of index partition */
	char part_num2id[LTFS_NUM_PARTITIONS]; /**< Mapping physical partition -> logical ID */
};

/**
 * Index partition criteria.
 *
 * The high and low water mark define how many objects of size @max_filesize_criteria
 * to allocate at initialization and at most for a given session. The filename_criteria
 * array defines glob patterns that are looked at by the file creation routines so that
 * the I/O scheduler can know if the new file is a candidate to go to the index partition
 * or not.
 *
 * Please note that when @max_filesize_criteria is 0 then no caching is performed by
 * LTFS and all files go straight to the data partition. The index partition only use
 * in this case is to store indices.
 */
struct index_criteria {
	bool have_criteria;                 /**< Does this struct actually specify criteria? */
	uint64_t max_filesize_criteria;     /**< Maximum file size that goes into the index partition */
	char **glob_patterns;               /**< NULL-terminated list of file name criteria */
	UChar **glob_cache;                 /**< Cache of glob patterns in comparison-ready form */
};

struct ltfs_index {
	char *creator;                      /**< Program that wrote this index */
	char vol_uuid[37];                  /**< LTFS volume UUID */
	char *volume_name;                  /**< human-readable volume name */
	unsigned int generation;            /**< last generation number written to tape */
	struct ltfs_timespec mod_time;      /**< time of last modification */
	struct tape_offset selfptr;         /**< self-pointer (where this index was recovered from tape) */
	struct tape_offset backptr;         /**< back pointer (to prior generation on data partition) */

	/* NOTE: index criteria are accessed without locking, so updates are not thread-safe */
	bool criteria_allow_update;              /**< Can the index criteria be changed? */
	struct index_criteria original_criteria; /**< Index partition criteria from the medium */
	struct index_criteria index_criteria;    /**< Active index criteria */

	struct dentry *root;                /**< The directory tree */
	ltfs_mutex_t rename_lock;        /**< Controls name tree access during renames */

	/* Update tracking */
	ltfs_mutex_t dirty_lock;         /**< Controls access to the update tracking bits */
	bool dirty;                         /**< Set on metadata update, cleared on write to tape */
	bool atime_dirty;                   /**< Set on atime update, cleared on write to tape */
	bool use_atime;                     /**< Set if atime updates should make the index dirty */
	uint64_t file_count;                /**< Number of files in the file system */
	uint64_t uid_number;                /**< File/directory's most recently reserved uid number */
	uint64_t valid_blocks;              /**< Numbert of valid blocks on tape */
	char *commit_message;               /**< Commit message specified by the "user.ltfs.sync" xattr */
	int version;                        /**< Index format version, as formatted by MAKE_LTFS_VERSION */

	/* Reference counts */
	ltfs_mutex_t refcount_lock;      /**< Controls access to the refcount */
	uint64_t refcount;                  /**< Reference count */

	size_t tag_count;                   /**< Number of unrecognized tags */
	unsigned char **preserved_tags;     /**< Unrecognized tags, will be preserved when writing tape */
	size_t symerr_count;                /**< Number of conflicted symlink dentries */
	struct dentry **symlink_conflict;   /**< symlink/extent conflicted dentries */
};

struct ltfs_direntry {
	struct ltfs_timespec creation_time; /**< Time of creation */
	struct ltfs_timespec access_time;   /**< Time of last access */
	struct ltfs_timespec modify_time;   /**< Time of last modification */
	struct ltfs_timespec change_time;   /**< Time of last status change */
	bool     isdir;                     /**< True if this is a directory, false if it's a file */
	bool     readonly;                  /**< True if file is marked read-only */
	bool     isslink;                   /**< True if file is symbolic link */
	uint64_t realsize;                  /**< Size, not counting sparse tail */
	uint64_t size;                      /**< File size (logical EOF position) */
	char    *name;                      /**< File or directory name */
	char    *platform_safe_name;        /**< File or directory name after file name mangling */
};

/* Definitions for sync */
typedef enum {
	LTFS_SYNC_NONE,
	LTFS_SYNC_TIME,
	LTFS_SYNC_CLOSE,
	LTFS_SYNC_UNMOUNT
} ltfs_sync_type_t;

/* Reasons of index write */
#define SYNC_EXPLICIT        "Explicit Sync"
#define SYNC_PERIODIC        "Periodic Sync"
#define SYNC_EA              "Sync by EA"
#define SYNC_CLOSE           "Sync on close"
#define SYNC_DNO_SPACE       "Dcache no space"
#define SYNC_UNMOUNT         "Unmount"
#define SYNC_UNMOUNT_NOMEM   "Unmount - no memory"
#define SYNC_MOVE            "Unmount - %"PRIu64
#define SYNC_CHECK           "Check"
#define SYNC_ROLLBACK        "Rollback"
#define SYNC_FORMAT          "Format"
#define SYNC_RECOVERY        "Recovery"
#define SYNC_CASCHE_PRESSURE "Cache Pressure"
#define SYNC_SCAN_TAPE       "Scan Tape"

struct ltfs_device_info {
	char *device_name;
	char *vender_id;
	char *product_id;
	char *serial_number;
};

/* Traverse strategy for index */
enum {
	TRAVERSE_UNKNOWN,
	TRAVERSE_FORWARD,
	TRAVERSE_BACKWARD,
};

/* Call back function when index is valid */
typedef int (*f_index_found)(struct ltfs_volume *vol, unsigned int gen, void **list, void *priv);

const char *ltfs_version();
const char *ltfs_format_version();
int ltfs_init(int log_level, bool use_syslog, bool print_thread_id);
int ltfs_fs_init(void);
void ltfs_set_log_level(int log_level);
void ltfs_set_syslog_level(int syslog_level);
bool ltfs_is_interrupted(void);
int ltfs_set_signal_handlers(void);
int ltfs_unset_signal_handlers(void);
int ltfs_finish();

/* Public wrappers for tape_* functions */
const char *ltfs_default_device_name(struct tape_ops *ops);
int ltfs_device_open(const char *devname, struct tape_ops *ops, struct ltfs_volume *vol);
int ltfs_device_reopen(const char *devname, struct ltfs_volume *vol);
void ltfs_device_close(struct ltfs_volume *vol);
void ltfs_device_close_skip_append_only_mode(struct ltfs_volume *vol);
int ltfs_setup_device(struct ltfs_volume *vol);
int ltfs_test_unit_ready(struct ltfs_volume *vol);
int ltfs_get_tape_readonly(struct ltfs_volume *vol);
int ltfs_get_logically_tape_readonly(struct ltfs_volume *vol);
int ltfs_get_partition_readonly(char partition, struct ltfs_volume *vol);
int ltfs_get_cartridge_health(cartridge_health_info *h, struct ltfs_volume *vol);
int ltfs_get_tape_alert(uint64_t *tape_alert, struct ltfs_volume *vol);
int ltfs_get_tape_alert_unlocked(uint64_t *tape_alert, struct ltfs_volume *vol);
int ltfs_clear_tape_alert(uint64_t tape_alert, struct ltfs_volume *vol);
int ltfs_get_append_position(uint64_t *pos, struct ltfs_volume *vol);
int ltfs_get_vendorunique_xattr(const char *name, char **buf, struct ltfs_volume *vol);
int ltfs_set_vendorunique_xattr(const char *name, const char *value, size_t size, struct ltfs_volume *vol);
tape_partition_t ltfs_part_id2num(char id, struct ltfs_volume *vol);

int ltfs_volume_alloc(const char *execname, struct ltfs_volume **volume);
void _ltfs_volume_free(bool force, struct ltfs_volume **volume);
#define ltfs_volume_free(vol)					\
	_ltfs_volume_free(false, vol)
#define ltfs_volume_free_force(vol)				\
	_ltfs_volume_free(true, vol)

int ltfs_capacity_data(struct device_capacity *cap, struct ltfs_volume *vol);
int ltfs_capacity_data_unlocked(struct device_capacity *cap, struct ltfs_volume *vol);
unsigned long ltfs_get_blocksize(struct ltfs_volume *vol);
bool ltfs_get_compression(struct ltfs_volume *vol);
struct ltfs_timespec ltfs_get_format_time(struct ltfs_volume *vol);
uint64_t ltfs_get_file_count(struct ltfs_volume *vol);
uint64_t ltfs_get_valid_block_count(struct ltfs_volume *vol);
uint64_t ltfs_get_valid_block_count_unlocked(struct ltfs_volume *vol);
int ltfs_update_valid_block_count(struct ltfs_volume *vol, int64_t c);
int ltfs_update_valid_block_count_unlocked(struct ltfs_volume *vol, int64_t c);
unsigned int ltfs_get_index_generation(struct ltfs_volume *vol);
struct ltfs_timespec ltfs_get_index_time(struct ltfs_volume *vol);
struct tape_offset ltfs_get_index_selfpointer(struct ltfs_volume *vol);
struct tape_offset ltfs_get_index_backpointer(struct ltfs_volume *vol);
int ltfs_get_index_commit_message(char **msg, struct ltfs_volume *vol);
int ltfs_get_index_creator(char **msg, struct ltfs_volume *vol);
int ltfs_get_volume_name(char **msg, struct ltfs_volume *vol);
int ltfs_get_index_version(struct ltfs_volume *vol);
const char *ltfs_get_barcode(struct ltfs_volume *vol);
const struct index_criteria *ltfs_get_index_criteria(struct ltfs_volume *vol);
bool ltfs_get_criteria_allow_update(struct ltfs_volume *vol);

int ltfs_start_mount(bool trial, struct ltfs_volume *vol);
int ltfs_mount(bool force_full, bool deep_recovery, bool recover_extra, bool recover_symlink,
			   unsigned short gen, struct ltfs_volume *vol);
int ltfs_unmount(char *reason, struct ltfs_volume *vol);
void ltfs_dump_tree_unlocked(struct ltfs_index *index);
void ltfs_dump_tree(struct ltfs_volume *vol);
int ltfs_eject_tape(struct ltfs_volume *vol);
int ltfs_set_blocksize(unsigned long blocksize, struct ltfs_volume *vol);
int ltfs_set_compression(bool enable_compression, struct ltfs_volume *vol);
int ltfs_set_barcode(const char *barcode, struct ltfs_volume *vol);
int ltfs_validate_barcode(const char *barcode);
int ltfs_set_volume_name(const char *volname, struct ltfs_volume *vol);
int ltfs_set_partition_map(char dp, char ip, int dp_num, int ip_num, struct ltfs_volume *vol);
int ltfs_reset_capacity(bool reset, struct ltfs_volume *vol);
int ltfs_write_label(tape_partition_t partition, struct ltfs_volume *vol);
int ltfs_format_tape(struct ltfs_volume *vol);
int ltfs_unformat_tape(struct ltfs_volume *vol, bool long_erase);
bool ltfs_is_dirty(struct ltfs_volume *vol);
/*int ltfs_load_all_attributes(struct ltfs_volume *vol);*/

int ltfs_wait_revalidation(struct ltfs_volume *vol);
int ltfs_get_volume_lock(bool exclusive, struct ltfs_volume *vol);
int ltfs_revalidate(bool reacquire, struct ltfs_volume *vol);

void ltfs_use_atime(bool use_atime, struct ltfs_volume *vol);
void ltfs_set_eod_check(bool use, struct ltfs_volume *vol);
void ltfs_set_traverse_mode(int mode, struct ltfs_volume *vol);
int ltfs_override_policy(const char *rules, bool permanent, struct ltfs_volume *vol);
int ltfs_set_scheduler_cache(size_t min_size, size_t max_size, struct ltfs_volume *vol);
size_t ltfs_min_cache_size(struct ltfs_volume *vol);
size_t ltfs_max_cache_size(struct ltfs_volume *vol);

int ltfs_parse_tape_backend_opts(void *opt_args, struct ltfs_volume *vol);
int ltfs_parse_kmi_backend_opts(void *opt_args, struct ltfs_volume *vol);
int ltfs_parse_library_backend_opts(void *opt_args, void *opts);

void ltfs_set_index_dirty(bool locking, bool atime, struct ltfs_index *idx);
void ltfs_unset_index_dirty(bool update_version, struct ltfs_index *idx);
int ltfs_write_index(char partition, char *reason, struct ltfs_volume *vol);
int ltfs_save_index_to_disk(const char *work_dir, char * reason, bool need_gen, struct ltfs_volume *vol);


char ltfs_dp_id(struct ltfs_volume *vol);
char ltfs_ip_id(struct ltfs_volume *vol);
const char *ltfs_get_volume_uuid(struct ltfs_volume *vol);

int ltfs_sync_index(char *reason, bool index_locking, struct ltfs_volume *vol);

int ltfs_traverse_index_forward(struct ltfs_volume *vol, char partition, unsigned int gen,
								f_index_found func, void **list, void *priv);
int ltfs_traverse_index_backward(struct ltfs_volume *vol, char partition, unsigned int gen,
								 f_index_found func, void **list, void *priv);
int ltfs_traverse_index_no_eod(struct ltfs_volume *vol, char partition, unsigned int gen,
								f_index_found func, void **list, void *priv);
int ltfs_check_eod_status(struct ltfs_volume *vol);
int ltfs_recover_eod(struct ltfs_volume *vol);
int ltfs_release_medium(struct ltfs_volume *vol);
int ltfs_wait_device_ready(struct ltfs_volume *vol);
void ltfs_recover_eod_simple(struct ltfs_volume *vol);

int ltfs_print_device_list(struct tape_ops *ops);
void ltfs_enable_livelink_mode(struct ltfs_volume *vol);

int ltfs_update_mam_attributes(struct ltfs_volume *vol);
int mkdir_p(const char *path, mode_t mode);

int ltfs_string_toupper(char *barcode);
int ltfs_get_tape_logically_readonly(struct ltfs_volume *vol);

#ifdef __cplusplus
}
#endif

#endif /* __ltfs_h__ */
