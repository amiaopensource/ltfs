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
** FILE NAME:       snmp/ltfssnmp.c
**
** DESCRIPTION:     Implements the snmp trap functions.
**
** AUTHORS:         Masahide Washizawa
**                  IBM Tokyo Lab., Japan
**                  washi@jp.ibm.com
**
*************************************************************************************
*/
#include <string.h>
#include <stdlib.h>

#include "ltfssnmp.h"

#define AGENT "ltfs"
#define TABLE_FILE_MODE "rb"

#if ((!defined (__APPLE__)) && (!defined (mingw_PLATFORM)))
#define DEFAULT_DEFFILE LTFS_BASE_DIR "/share/snmp/LtfsSnmpTrapDef.txt"
static const oid snmptrap_oid[] = { 1, 3, 6, 1, 6, 3, 1, 1, 4, 1, 0 };
#else
#define DEFAULT_DEFFILE LTFS_BASE_DIR "LtfsSnmpTrapDef.txt"
#endif

bool ltfs_snmp_enabled = false;

struct trap_entry {
	TAILQ_ENTRY(trap_entry) list;
	char *id;
};
TAILQ_HEAD(trap_struct, trap_entry) trap_entries;

bool is_snmp_enabled()
{
	return ltfs_snmp_enabled;
}

int read_trap_def_file(char *deffile)
{
	int ret = 0;
	char line[65536];
	char *trapfile=DEFAULT_DEFFILE;
	char *strip_pos, *tok, *saveptr;
	struct trap_entry *entry;
	FILE *fp;

	TAILQ_INIT(&trap_entries);

	if (deffile != NULL)
		trapfile = deffile;

	fp = fopen(trapfile, TABLE_FILE_MODE);
	if (! fp) {
		ret = -errno;
		ltfsmsg(LTFS_ERR, "11268E", trapfile, ret);
		return ret;
	}

	/* Parse the traf definition file */
	if (!ret) {
		while(fgets(line, 65536, fp) != NULL) {
			if (strlen(line) == 65535) {
				ltfsmsg(LTFS_ERR, "11269E");
				ret = -LTFS_CONFIG_INVALID;
				return ret;
			}
			/* Ignore comments and trailing whitespace */
			strip_pos = strstr(line, "#");
			if (! strip_pos)
				strip_pos = line + strlen(line);

			while (strip_pos > line &&
				(*(strip_pos - 1) == ' ' || *(strip_pos - 1) == '\t' ||
				 *(strip_pos - 1) == '\r' || *(strip_pos - 1) == '\n'))
				--strip_pos;
			*strip_pos = '\0';

			tok = strtok_r(line, " \t\r\n", &saveptr);
			if (tok) {
				entry = (struct trap_entry *) calloc(1, sizeof(struct trap_entry));
				if (! entry) {
					ltfsmsg(LTFS_ERR, "10001E", __FUNCTION__);
					return -LTFS_NO_MEMORY;
				}
				entry->id = strdup(tok);
				TAILQ_INSERT_TAIL(&trap_entries, entry, list);
			}
		}
		fclose(fp);
	}
	return ret;
}

bool is_snmp_trapid(const char *id)
{
	struct trap_entry *entry = NULL;
	if (id == NULL)
		return false;

	TAILQ_FOREACH(entry, &trap_entries, list) {
		if (! strcmp(entry->id, id))
			return true;
	}
	return false;
}

int ltfs_snmp_init(char *snmp_deffile)
{
#if ((!defined (__APPLE__)) && (!defined (mingw_PLATFORM)))
	ltfs_snmp_enabled = true;
	netsnmp_ds_set_boolean(NETSNMP_DS_APPLICATION_ID, NETSNMP_DS_AGENT_ROLE, 1);
	init_agent(AGENT);
	init_snmp(AGENT);
	read_trap_def_file(snmp_deffile);
#endif
	return 0;
}

int ltfs_snmp_finish()
{
	struct trap_entry *entry = NULL;
	TAILQ_FOREACH(entry, &trap_entries, list)
		free(entry->id);
#if ((!defined (__APPLE__)) && (!defined (mingw_PLATFORM)))
	send_ltfsStopTrap();
	snmp_shutdown(AGENT);
#endif
	return 0;
}

int send_ltfsStartTrap(void)
{
#if ((!defined (__APPLE__)) && (!defined (mingw_PLATFORM)))
	netsnmp_variable_list *var_list = NULL;
	const oid ltfsStartTrap_oid[] = { 1, 3, 6, 1, 4, 1, 2, 6, 248, 2, 1 };

	/* Set the snmpTrapOid.0 value */
	snmp_varlist_add_variable(&var_list,
		snmptrap_oid, OID_LENGTH(snmptrap_oid),
		ASN_OBJECT_ID,
		(const u_char *)ltfsStartTrap_oid,
		sizeof(ltfsStartTrap_oid));

	/* Send the trap to the list of configured destinations and clean up */
	send_v2trap(var_list);
	snmp_free_varbind(var_list);
	return SNMP_ERR_NOERROR;
#else
	return 0;
#endif
}

int send_ltfsStopTrap(void)
{
#if ((!defined (__APPLE__)) && (!defined (mingw_PLATFORM)))
	netsnmp_variable_list *var_list = NULL;
	const oid ltfsStopTrap_oid[] = { 1, 3, 6, 1, 4, 1, 2, 6, 248, 2, 2 };

	/* Set the snmpTrapOid.0 value */
	snmp_varlist_add_variable(&var_list,
		snmptrap_oid, OID_LENGTH(snmptrap_oid),
		ASN_OBJECT_ID,
		(const u_char *)ltfsStopTrap_oid,
		sizeof(ltfsStopTrap_oid));

	/* Send the trap to the list of configured destinations and clean up */
	send_v2trap(var_list);
	snmp_free_varbind(var_list);
	return SNMP_ERR_NOERROR;
#else
	return 0;
#endif
}

int send_ltfsInfoTrap(char *str)
{
#if ((!defined (__APPLE__)) && (!defined (mingw_PLATFORM)))
	netsnmp_variable_list *var_list = NULL;
	const oid ltfsInfoTrap_oid[] = { 1, 3, 6, 1, 4, 1, 2, 6, 248, 2, 3 };
	const oid ltfsTrapInfo_oid[] = { 1, 3, 6, 1, 4, 1, 2, 6, 248, 1, 1, 0 };

	/* Set the snmpTrapOid.0 value */
	snmp_varlist_add_variable(&var_list,
		snmptrap_oid, OID_LENGTH(snmptrap_oid),
		ASN_OBJECT_ID,
		(const u_char *)ltfsInfoTrap_oid,
		sizeof(ltfsInfoTrap_oid));

	/* Add any objects from the trap definition */
	snmp_varlist_add_variable(&var_list,
		ltfsTrapInfo_oid,
		OID_LENGTH(ltfsTrapInfo_oid),
		ASN_OCTET_STR,
		(const u_char *)str,
		strlen(str));

	/* Send the trap to the list of configured destinations and clean up */
	send_v2trap(var_list);
	snmp_free_varbind(var_list);
	return SNMP_ERR_NOERROR;
#else
	return 0;
#endif
}

int send_ltfsErrorTrap(char *str)
{
#if ((!defined (__APPLE__)) && (!defined (mingw_PLATFORM)))
	netsnmp_variable_list *var_list = NULL;
	const oid ltfsErrorTrap_oid[] = { 1, 3, 6, 1, 4, 1, 2, 6, 248, 2, 4 };
	const oid ltfsTrapInfo_oid[] = { 1, 3, 6, 1, 4, 1, 2, 6, 248, 1, 1, 0 };

	/* Set the snmpTrapOid.0 value */
	snmp_varlist_add_variable(&var_list,
		snmptrap_oid, OID_LENGTH(snmptrap_oid),
		ASN_OBJECT_ID,
		(const u_char *)ltfsErrorTrap_oid,
		sizeof(ltfsErrorTrap_oid));

	/* Add any objects from the trap definition */
	snmp_varlist_add_variable(&var_list,
		ltfsTrapInfo_oid,
		OID_LENGTH(ltfsTrapInfo_oid),
		ASN_OCTET_STR,
		(const u_char *)str,
		strlen(str));

	/* Send the trap to the list of configured destinations and clean up */
	send_v2trap(var_list);
	snmp_free_varbind(var_list);
	return SNMP_ERR_NOERROR;
#else
	return 0;
#endif
}
