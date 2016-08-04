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
** FILE NAME:       tape_drivers/osx/iokit/scsi_command_blocks.h
**
** DESCRIPTION:     Header file for raw SCSI command blocks.
**
** AUTHOR:          Michael A. Richmond
**                  IBM Almaden Research Center
**                  mar@almaden.ibm.com
**
*************************************************************************************
*/


#include <IOKit/IOTypes.h>

#ifndef scsi_command_blocks_h_
#define scsi_command_blocks_h_

#ifndef BYTE
    #define BYTE unsigned char
#endif

//#define four_bytes_to_int(data)		OSReadBigInt32(data, 0)
//#define three_bytes_to_int(data)		(OSReadBigInt32(data, -1) & 0x00FFFFFF)
//#define two_bytes_to_int(data)		OSReadBigInt16(data, 0)

#define eight_bytes_to_int(data)	   ( (((uint64_t)data[0] << 56) & 0xFF00000000000000LL) +		\
										 (((uint64_t)data[1] << 48) & 0x00FF000000000000LL) +		\
										 (((uint64_t)data[2] << 40) & 0x0000FF0000000000LL) +		\
										 (((uint64_t)data[3] << 32) & 0x000000FF00000000LL) +		\
										 (((uint64_t)data[4] << 24) & 0x00000000FF000000LL) +		\
										 (((uint64_t)data[5] << 16) & 0x0000000000FF0000LL) +		\
										 (((uint64_t)data[6] <<  8) & 0x000000000000FF00LL) +		\
										 (((uint64_t)data[7])       & 0x00000000000000FFLL) )

#define four_bytes_to_int(data)	       ( (((uint32_t)data[0] << 24) & 0xFF000000) +		\
										 (((uint32_t)data[1] << 16) & 0x00FF0000) +		\
										 (((uint32_t)data[2] <<  8) & 0x0000FF00) +		\
										 (((uint32_t)data[3])       & 0x000000FF) )

#define three_bytes_to_int(data)	   ( (((uint32_t)data[0] << 16) & 0x00FF0000) +		\
										 (((uint32_t)data[1] <<  8) & 0x0000FF00) +		\
										 (((uint32_t)data[2])       & 0x000000FF) )

#define two_bytes_to_int(data)	   		( (((uint16_t)data[0] << 8) & 0x0000FF00) +		\
										  (((uint16_t)data[1])      & 0x000000FF) )

/*
#define mask_block_limit_max_len(data)	  (data[0] << 16)		\
										| (data[1] << 8)		\
										| (data[2])

#define mask_block_limit_min_len(data)	  (data[0] << 8)		\
										| (data[1]);
*/


/******************************
 *
 * Command Block Descriptors
 *
 */
typedef struct _cdb_erase {
    unsigned char op_code;
#if defined(__LITTLE_ENDIAN__)
    unsigned char long_bit:1;
    unsigned char immediate:1;
    unsigned char reserved1:3;
    unsigned char lun:3;
#elif defined(__BIG_ENDIAN__)
    unsigned char lun:3;
    unsigned char reserved1:3;
    unsigned char immediate:1;
    unsigned char long_bit:1;
#else
    unsigned char unsupported;
#endif
    unsigned char reserved2[3];
    unsigned char ctrl_byte;
} cdb_erase;


typedef struct _cdb_inquiry {
        unsigned char op_code;
#if defined(__LITTLE_ENDIAN__)
        unsigned char evpd:1;
        unsigned char reserved1:4;
        unsigned char lun:3;
#elif defined(__BIG_ENDIAN__)
        unsigned char lun:3;
        unsigned char reserved1:4;
        unsigned char evpd:1;
#else
        unsigned char unsupported;
#endif
        unsigned char page_code;
        unsigned char reserved2;
        unsigned char alloc_length;
        unsigned char ctrl_byte;
} cdb_inquiry;


typedef struct _cdb_load_unload {
	unsigned char op_code;
#if defined(__LITTLE_ENDIAN__)
	unsigned char immediate:1;
	unsigned char reserved1:4;
	unsigned char lun:3;
	unsigned char reserved2[2];
	unsigned char load:1;
	unsigned char reten:1;
	unsigned char eot:1;
	unsigned char reserved3:5;
#elif defined(__BIG_ENDIAN__)
	unsigned char lun:3;
	unsigned char reserved1:4;
	unsigned char immediate:1;
	unsigned char reserved2[2];
	unsigned char reserved3:5;
	unsigned char eot:1;
	unsigned char reten:1;
	unsigned char load:1;
#else
	unsigned char unsupported[4];
#endif
	unsigned char ctrl_byte;
} cdb_load_unload;

typedef struct _cdb_locate {
    unsigned char op_code;
#if defined(__LITTLE_ENDIAN__)
    unsigned char immediate:1;
    unsigned char cp:1;
    unsigned char bt:1;
    unsigned char reserved1:2;
    unsigned char lun:3;
#elif defined(__BIG_ENDIAN__)
    unsigned char lun:3;
    unsigned char reserved1:2;
    unsigned char bt:1;
    unsigned char cp:1;
    unsigned char immediate:1;
#else
    unsigned char unsupported;
#endif
    unsigned char reserved3;
    unsigned char blk_addr[4];
    unsigned char reserved4;
    unsigned char partition;
    unsigned char ctrl_byte;
} cdb_locate;

typedef struct _cdb_logsense {
	unsigned char op_code;
#if defined(__LITTLE_ENDIAN__)
	unsigned char sp:1;
	unsigned char ppc:1;
	unsigned char reserved1:3;
	unsigned char lun:3;
	unsigned char page_code:6;
	unsigned char pc:2;
#elif defined(__BIG_ENDIAN__)
	unsigned char lun:3;
	unsigned char reserved1:3;
	unsigned char ppc:1;
	unsigned char sp:1;
	unsigned char pc:2;
	unsigned char page_code:6;
#else
	unsigned char unsupported[2];
#endif
	unsigned char subpage_code;
	unsigned char reserved2;
	unsigned char parm_ptr[2];
	unsigned char alloc_length[2];
	unsigned char ctrl_byte;
} cdb_logsense;

typedef struct _cdb_medium_removal {
        unsigned char op_code;
#if defined(__LITTLE_ENDIAN__)
        unsigned char reserved1:5;
        unsigned char lun:3;
#elif defined(__BIG_ENDIAN__)
        unsigned char lun:3;
        unsigned char reserved1:5;
#else
        unsigned char unsupported;
#endif
        unsigned char reserved2[2];
        unsigned char prevent;
        unsigned char ctrl_byte;
} cdb_medium_removal;


typedef struct _cdb_modeselect {
    unsigned char op_code;
#if defined(__LITTLE_ENDIAN__)
    unsigned char sp:1;
    unsigned char reserved1:3;
    unsigned char pf:1;
    unsigned char lun:3;
#elif defined(__BIG_ENDIAN__)
    unsigned char lun:3;
    unsigned char pf:1;
    unsigned char reserved1:3;
    unsigned char sp:1;
#else
    unsigned char unsupported;
#endif
    unsigned char reserved2[2];
    unsigned char parm_list_length;
    unsigned char ctrl_byte;
} cdb_modeselect;


typedef struct _cdb_modesense {
        unsigned char op_code;
#if defined(__LITTLE_ENDIAN__)
        unsigned char reserved1:3;
        unsigned char dbd:1;
        unsigned char reserved2:1;
        unsigned char lun:3;
        unsigned char page_code:6;
        unsigned char pc:2;
#elif defined(__BIG_ENDIAN__)
        unsigned char lun:3;
        unsigned char reserved2:1;
        unsigned char dbd:1;
        unsigned char reserved1:3;
        unsigned char pc:2;
        unsigned char page_code:6;
#else
        unsigned char unsupported[2];
#endif
        unsigned char reserved3;
        unsigned char alloc_length;
        unsigned char ctrl_byte;
} cdb_modesense;


typedef struct _cdb_read_pos {
    unsigned char op_code;
#if defined(__LITTLE_ENDIAN__)
    unsigned char service_action:5;
    unsigned char lun:3;
#elif defined(__BIG_ENDIAN__)
    unsigned char lun:3;
    unsigned char service_action:5;
#else
    unsigned char unsupported;
#endif
    unsigned char reserved1[7];
    unsigned char ctrl_byte;
} cdb_read_pos;


typedef struct _cdb_read_write {
    unsigned char op_code;
#if defined(__LITTLE_ENDIAN__)
    unsigned char fixed:1;
    unsigned char sili:1;
    unsigned char reserved:3;
    unsigned char lun:3;
#elif defined(__BIG_ENDIAN__)
    unsigned char lun:3;
    unsigned char reserved:3;
    unsigned char sili:1;
    unsigned char fixed:1;
#else
    unsigned char unsupported;
#endif
    unsigned char xfer_length[3];
    unsigned char ctrl_byte;
} cdb_read_write;


typedef struct _cdb_read_block_limits {
        unsigned char op_code;
#if defined(__LITTLE_ENDIAN__)
        unsigned char reserved1:5;
        unsigned char lun:3;
#elif defined(__BIG_ENDIAN__)
        unsigned char lun:3;
        unsigned char reserved1:5;
#else
        unsigned char unsupported;
#endif
        unsigned char reserved2[3];
        unsigned char ctrl_byte;
} cdb_read_block_limits;


typedef struct _cdb_release {
    unsigned char op_code;
#if defined(__LITTLE_ENDIAN__)
    unsigned char element:1;
    unsigned char third_party_dev_id:3;
    unsigned char third_party:1;
    unsigned char lun:3;
#elif defined(__BIG_ENDIAN__)
    unsigned char lun:3;
    unsigned char third_party:1;
    unsigned char third_party_dev_id:3;
    unsigned char element:1;
#else
    unsigned char unsupported;
#endif
    unsigned char reserve_id;
    unsigned char reserved;
    unsigned char alloc_length;
    unsigned char ctrl_byte;
} cdb_release;


typedef struct _cdb_report_density {
	unsigned char op_code;
#if defined(__LITTLE_ENDIAN__)
	unsigned char media:1;
	unsigned char reserved1:4;
	unsigned char lun:3;
#elif defined(__BIG_ENDIAN__)
	unsigned char lun:3;
	unsigned char reserved1:4;
	unsigned char media:1;
#else
	unsigned char unsupported;
#endif
	unsigned char reserved2[5];
	unsigned char alloc_length[2];
	unsigned char ctrl_byte;
} cdb_report_density;


typedef struct _cdb_reserve {
    unsigned char op_code;
#if defined(__LITTLE_ENDIAN__)
    unsigned char reserved1:1;
    unsigned char third_party_dev_id:3;
    unsigned char third_party:1;
    unsigned char lun:3;
#elif defined(__BIG_ENDIAN__)
    unsigned char lun:3;
    unsigned char third_party:1;
    unsigned char third_party_dev_id:3;
    unsigned char reserved1:1;
#else
    unsigned char unsupported;
#endif
    unsigned char reserved[3];
    unsigned char ctrl_byte;
} cdb_reserve;


typedef struct _cdb_rewind {
        unsigned char op_code;
#if defined(__LITTLE_ENDIAN__)
        unsigned char immediate:1;
        unsigned char reserved1:4;
        unsigned char lun:3;
#elif defined(__BIG_ENDIAN__)
        unsigned char lun:3;
        unsigned char reserved1:4;
        unsigned char immediate:1;
#else
        unsigned char unsupported;
#endif
        unsigned char reserved2[3];
        unsigned char ctrl_byte;
} cdb_rewind;


typedef struct _cdb_space {
    unsigned char op_code;
#if defined(__LITTLE_ENDIAN__)
    unsigned char code:3;
    unsigned char reserved1:2;
    unsigned char lun:3;
#elif defined(__BIG_ENDIAN__)
    unsigned char lun:3;
    unsigned char reserved1:2;
    unsigned char code:3;
#else
    unsigned char unsupported;
#endif
    unsigned char count[3];
    unsigned char ctrl_byte;
} cdb_space;


typedef struct _cdb_test_unit_ready {
        unsigned char op_code;
#if defined(__LITTLE_ENDIAN__)
        unsigned char reserved1:5;
        unsigned char lun:3;
#elif defined(__BIG_ENDIAN__)
        unsigned char lun:3;
        unsigned char reserved1:5;
#else
        unsigned char unsupported;
#endif
        unsigned char reserved2[3];
        unsigned char ctrl_byte;
} cdb_test_unit_ready;


typedef struct _cdb_write_file_mark {
    unsigned char op_code;
#if defined(__LITTLE_ENDIAN__)
    unsigned char immediate:1;
    unsigned char write_setmarks:1;
    unsigned char reserved:3;
    unsigned char lun:3;
#elif defined(__BIG_ENDIAN__)
    unsigned char lun:3;
    unsigned char reserved:3;
    unsigned char write_setmarks:1;
    unsigned char immediate:1;
#else
    unsigned char unsupported;
#endif
    unsigned char count[3];
    unsigned char ctrl_byte;
} cdb_write_file_mark;

typedef struct _cdb_request_sense {
	unsigned char op_code;
	unsigned char reserved[3];
	unsigned char allocation_length;
	unsigned char ctrl_byte;
} cdb_request_sense;


/******************************
 *
 * Command Result Structures
 *
 */

/* read block limits data buffer */
typedef struct _block_limits {
        unsigned char reserved;
        unsigned char max_blk_size[3];
        unsigned char min_blk_size[2];
} block_limits;



typedef struct _read_pos {
#if defined(__LITTLE_ENDIAN__)
    unsigned char reserved1:2;
    unsigned char bpu:1;
    unsigned char reserved2:1;
    unsigned char bycu:1;
    unsigned char bcu:1;
    unsigned char eop:1;
    unsigned char bop:1;
#elif defined(__BIG_ENDIAN__)
    unsigned char bop:1;
    unsigned char eop:1;
    unsigned char bcu:1;
    unsigned char bycu:1;
    unsigned char reserved2:1;
    unsigned char bpu:1;
    unsigned char reserved1:2;
#else
    unsigned char unsupported;
#endif
    unsigned char partition;
    unsigned char reserved3[2];
    unsigned char first_blk_pos[4];
    unsigned char last_blk_pos[4];
    unsigned char reserved4;
    unsigned char blks_in_buf[3];
    unsigned char bytes_in_buf[4];
} read_pos;


typedef struct _pos_info {
	unsigned char bop;
	unsigned char eop;
	unsigned long first_blk_pos;
	unsigned long last_blk_pos;
	unsigned long blks_in_buf;
	unsigned long bytes_in_buf;
} pos_info;


typedef struct _medium_sense_param {
#if defined(__LITTLE_ENDIAN__)
    unsigned char page_code:6;
    unsigned char reserved1:1;
    unsigned char page_same:1;
    unsigned char page_length;
    unsigned char reserved2[2];
    unsigned char medium_id[2];
    unsigned char format_id;
    unsigned char reserved3[2];
    unsigned char worm_mode:4;
    unsigned char worm_locked:1;
    unsigned char reserved4:3;
    unsigned char permanent_wp:1;
    unsigned char reserved5:2;
    unsigned char reset_persistent_wp:1;
    unsigned char persistent_wp:1;
    unsigned char reset_associate_wp:1;
    unsigned char associated_wp:1;
    unsigned char physical_wp:1;
    unsigned char capacity_scaling_valid:1;
    unsigned char seg_scaling_valid:1;
    unsigned char reserved6:6;
    unsigned char capacity_scaling;
    unsigned char medium_capacity[4];
    unsigned char partition_num;
    unsigned char reserved7[2];
    unsigned char edge_track:1;
    unsigned char reserved:7;
    unsigned char internal_vid_source:3;
    unsigned char reserved8:3;
    unsigned char dbm_valid:1;
    unsigned char internal_vid_valid:1;
#elif defined(__BIG_ENDIAN__)
        unsigned char page_save:1;
        unsigned char reserved1:1;
        unsigned char page_code:6;
        unsigned char page_length;
        unsigned char reserved2[2];
        unsigned char medium_id[2];
        unsigned char format_id;
        unsigned char reserved3[2];
        unsigned char reserved4:3;
        unsigned char worm_locked:1;
        unsigned char worm_mode:4;
        unsigned char physical_wp:1;
        unsigned char associated_wp:1;
        unsigned char reset_associate_wp:1;
        unsigned char persistent_wp:1;
        unsigned char reset_persistent_wp:1;
        unsigned char reserved5:2;
        unsigned char permanent_wp:1;
        unsigned char reserved6:7;
        unsigned char capacity_scaling_valid:1;
        unsigned char capacity_scaling;
        unsigned char medium_capacity[4];
        unsigned char partition_num;
        unsigned char reserved7[2];
        unsigned char reserved:7;
        unsigned char edge_track:1;
        unsigned char internal_vid_valid:1;
        unsigned char dbm_valid:1;
        unsigned char reserved8:3;
        unsigned char internal_vid_source:3;
#else
        unsigned char unsupported[22];
#endif
        unsigned char internal_vid[6];
        unsigned char partition_capacity[4];
        unsigned char kb_transfered[4];
        unsigned char blks_written[4];
        unsigned char curr_file[4];
        unsigned char files_written[4];
        unsigned char ww_cartridge_id[12];
} medium_sense_param;


/* 4 byte mode parameter header for mode sense/mode select 6*/
typedef struct _mode_parm_header {
    unsigned char mode_data_length;
    unsigned char medium_type;
#if defined(__LITTLE_ENDIAN__)
    unsigned char speed:4;
    unsigned char buffered_mode:3;
    unsigned char wp:1;
#elif defined(__BIG_ENDIAN__)
    unsigned char wp:1;
    unsigned char buffered_mode:3;
    unsigned char speed:4;
#else
    unsigned char unsupported;
#endif
    unsigned char blk_descriptor_length;
} mode_parm_header;

/* mode block descriptor for mode sense / mode select 6 and 10*/
typedef struct _mode_block_descriptor {
        unsigned char density_code;
        unsigned char blks_num[3];
        unsigned char reserved;
        unsigned char blk_length[3];
} mode_block_descriptor;

typedef struct _media_param {
        struct _mode_parm_header header;
        struct _mode_block_descriptor block;
} media_param;


typedef struct _medium_sense_page {
	struct _mode_parm_header header;
	struct _medium_sense_param medium;
} medium_sense_page;


typedef struct _compression_parm {
	mode_parm_header header;
	unsigned char page[16];
} compression_parm;


/* report density support */
#define MaxDensityReports 8
typedef struct _report_density_header {
	unsigned char avail_length[2];
	unsigned char reserved0[2];
} report_density_header;

typedef struct _report_density_descriptor {
	unsigned char primary_density;
	unsigned char second_density;
#if defined(__LITTLE_ENDIAN__)
	unsigned char reserved1:5;
	unsigned char deflt:1;
	unsigned char dup:1;
	unsigned char wrtok:1;
#elif defined(__BIG_ENDIAN__)
	unsigned char wrtok:1;
	unsigned char dup:1;
	unsigned char deflt:1;
	unsigned char reserved1:5;
#else
	unsigned char unsupported;
#endif
	unsigned char reserved2;
	unsigned char reserved3;
	unsigned char bits_per_mm[3];
	unsigned char media_width[2];
	unsigned char tracks[2];
	unsigned char capacity[4];
	unsigned char assign_org[8];
	unsigned char density_name[8];
	unsigned char description[20];
} report_density_descriptor;

typedef struct _report_density {
	struct _report_density_header header;
	struct _report_density_descriptor descriptor[MaxDensityReports];
} report_density;


/***************************************************************************
 * Report Density Support ioctl data structures and defines                *
 ***************************************************************************/
#define ALL_MEDIA_DENSITY      0
#define CURRENT_MEDIA_DENSITY  1

#define MAX_DENSITY_REPORTS    8

typedef struct _density_report {
	unsigned char	primary_density_code;  	/* primary density code                   */
	unsigned char	secondary_density_code;	/* secondary densuty code                 */
	unsigned int	wrtok:1,                /* write ok, device can write this format */
					dup:1,                  /* zero if density only reported once     */
					deflt:1,                /* current density is default format      */
					:5;                     /* reserved                               */
  char   			reserved[2];            /* reserved                               */
  unsigned int		bits_per_mm:24;         /* bits per mm                            */
  unsigned short	media_width;            /* media width in millimeters             */
  unsigned short	tracks;                 /* tracks                                 */
  unsigned int		capacity;               /* capacity in megabytes                  */
  char   			assigning_org[8];       /* assigning organization in ASCII        */
  char				density_name[8];        /* density name in ASCII                  */
  char				description[20];        /* description in ASCII                   */
} density_report;

typedef struct _report_density_support {
	unsigned char			media;						/* report all or current media as defined above */
	unsigned short			number_reports;				/* number of density reports returned in array  */
	struct _density_report	reports[MAX_DENSITY_REPORTS];
} report_density_support;


/* log sense buffer */
#define MaxLogSense   4096	/* for full log sense */
#define SimMimLogPage 0x31
#define TapeAlertLogPage 0xE2
#define LogPageHeaderSize 4

#define LOGSENSEPAGE 1024        /* max data xfer for log sense page ioctl */
typedef struct _log_sense_page {
    unsigned char  page_code;
    unsigned short len;           /* log_page_header size + parameter length */
    unsigned short parm_pointer;
    unsigned char  data[LOGSENSEPAGE];
} log_sense_page;


typedef struct _all_device_params {
		int blksize;                        	/* new block size    */
		boolean_t trace;                    	/* TRUE = message trace on           */
		unsigned int hkwrd;						/* trace hook word                   */
		int sync_count;                   		/* obsolete - not used               */
		boolean_t autoload;                		/* on/off autoload feature           */
		boolean_t buffered_mode;           		/* on/off buffered mode              */
		boolean_t compression;             		/* on/off compression                */
		boolean_t trailer_labels;           	/* on/off allow writing after EOM    */
		boolean_t rewind_immediate;         	/* on/off immediate rewinds          */
		boolean_t bus_domination;           	/* obsolete - not used               */
		boolean_t logging;						/* enable or disable volume logging  */
		boolean_t write_protect;				/* write_protected media             */
		unsigned int min_blksize;				/* minimum block size                */
		unsigned int max_blksize;				/* maximum block size                */
		unsigned int max_scsi_xfer;				/* maximum scsi tranfer len          */
		char volid[16];							/* volume id                         */
		unsigned char acf_mode;					/* automatic cartridge facility mode */
			#define ACF_NONE             0
			#define ACF_MANUAL           1
			#define ACF_SYSTEM           2
			#define ACF_AUTOMATIC        3
			#define ACF_ACCUMULATE       4
			#define ACF_RANDOM           5
		unsigned char record_space_mode;		/* fsr/bsr space mode    */
			#define SCSI_SPACE_MODE      1
			#define AIX_SPACE_MODE       2
		unsigned char logical_write_protect;	/* logical write protect */
			#define NO_PROTECT           0
			#define ASSOCIATED_PROTECT   1
			#define PERSISTENT_PROTECT   2
			#define WORM_PROTECT         3
		unsigned char capacity_scaling;			/* capacity scaling      */
			#define SCALE_100            0
			#define SCALE_75             1
			#define SCALE_50             2
			#define SCALE_25             3
			#define SCALE_VALUE          4
		unsigned char retain_reservation;       /* retain reservation                 */
		unsigned char alt_pathing;              /* alternate pathing active           */
		boolean_t emulate_autoloader;      		/* emulate autoloader in random mode  */
		unsigned char medium_type;              /* tape medium type                   */
		unsigned char density_code;             /* tape density code                  */
		boolean_t disable_sim_logging;  		/* disable sim/mim error logging      */
		boolean_t read_sili_bit;          		/* SILI bit setting for read commands */
		unsigned char read_past_filemark;       /* fixed block read pass the filemark */
		boolean_t disable_auto_drive_dump; 		/* disable auto drive dump logging    */
		unsigned char capacity_scaling_value;   /* hex value of capacity scaling      */
		boolean_t wfm_immediate;       		    /* buffer write file mark             */
		boolean_t limit_read_recov;        		/* limit read recovery to 5 seconds   */
		boolean_t limit_write_recov;       		/* limit write recovery to 5 seconds  */
		unsigned char reserved[16];
} all_device_params;


#define MAXSENSE        			255

#define MAX_CDB_SIZE         			12


#define MAX_INQ_LEN 255            /* max data xfer for inquiry page ioctl */

typedef struct _standard_inquiry_data {
		BYTE b0;
		/*macros for accessing fields of byte 1*/
		#define PERIPHERAL_QUALIFIER(x)   ((x->b0 & 0xE0)>>5)
			#define PERIPHERAL_CONNECTED          0x00
			#define PERIPHERAL_NOT_CONNECTED      0x01
			#define LUN_NOT_SUPPORTED             0x03
		#define PERIPHERAL_DEVICE_TYPE(x) (x->b0 & 0x1F)
			#define DIRECT_ACCESS                 0x00
			#define SEQUENTIAL_DEVICE             0x01
			#define PRINTER_DEVICE                0x02
			#define PROCESSOR_DEVICE              0x03
			#define CD_ROM_DEVICE                 0x05
			#define OPTICAL_MEMORY_DEVICE         0x07
			#define MEDIUM_CHANGER_DEVICE         0x08
			#define UNKNOWN                       0x1F

		BYTE b1;
		/*macros for accessing fields of byte 2*/
		#define RMB(x) ((x->b1 & 0x80)>>7)                /*removable media bit  */
		#define FIXED     0
		#define REMOVABLE 1
		#define device_type_qualifier(x) (x->b1 & 0x7F)   /* vendor specific     */

		BYTE b2;
		/*macros for accessing fields of byte 3*/
		#define ISO_Version(x)  ((x->b2 & 0xC0)>>6)
		#define ECMA_Version(x) ((x->b2 & 0x38)>>3)
		#define ANSI_Version(x) (x->b2 & 0x07)
			#define NONSTANDARD     0
			#define SCSI1           1
			#define SCSI2           2
			#define SCSI3           3

		BYTE b3;
		/*macros for accessing fields of byte 4*/
		#define AENC(x)    ((x->b3 & 0x80)>>7) /*asynchronous event notification */
		#ifndef TRUE
			#define TRUE 1
		#endif
		#ifndef FALSE
			#define FALSE 0
		#endif
		#define TrmIOP(x)  ((x->b3 & 0x40)>>6) /* support terminate I/O process? */
		#define Response_Data_Format(x)  (x->b3 & 0x0F)
			#define SCSI1INQ      0          /* scsi1 standard inquiry data format */
			#define CCSINQ        1          /* CCS standard inquiry data format   */
			#define SCSI2INQ      2          /* scsi2 standard inquiry data format */

		BYTE additional_length;  /* number of bytes following this field minus 4 */
		BYTE res5;

		BYTE b6;
		#define MChngr(x)    ((x->b6 & 0x08)>>3)

		BYTE b7;
		/* macros for accessing fields of byte 7  */
		/* the following fields are true or false */
		#define RelAdr(x)    ((x->b7 & 0x80)>>7)
		#define WBus32(x)    ((x->b7 & 0x40)>>6)
		#define WBus16(x)    ((x->b7 & 0x20)>>5)
		#define Sync(x)      ((x->b7 & 0x10)>>4)
		#define Linked(x)    ((x->b7 & 0x08)>>3)
		#define CmdQue(x)    ((x->b7 & 0x02)>>1)
		#define SftRe(x)     (x->b7 & 0x01)

		char vendor_identification[8];
		char product_identification[16];
		char product_revision_level[4];
} standard_inquiry_data;


typedef struct _inquiry_data {
  struct _standard_inquiry_data standard;
  BYTE vendor_specific[MAX_INQ_LEN - sizeof(struct _standard_inquiry_data)];
} inquiry_data;

#endif /*scsi_command_blocks_h_*/
