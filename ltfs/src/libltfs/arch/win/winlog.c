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
** FILE NAME:       arch/win/winlog.c
**
** DESCRIPTION:     Implements MinGW (Windows) specific log 'functions
**
*************************************************************************************
**
** Copyright (C) 2012 OSR Open Systems Resources, Inc.
** 
************************************************************************************* 
*/

#include <stdio.h>
#include <windows.h>
#include <winbase.h>
#include "winlog.h"
#define OUTPUT_BUF_SIZE 4096  /* Output buffer size, should be big enough to hold any message. */

// For LOG
#define LOG_ERR     0
#define LOG_WARNING 1
#define LOG_INFO    2
#define LOG_DEBUG   3

/* 
 * OSR
 *
 * Include the header defining our Event Log message
 *    
 */
#ifdef HP_mingw_BUILD
#include "ltfs_msgs.h"
#endif

void vsyslog(int priority, const char *format, va_list ap)
{
    char output_buf[OUTPUT_BUF_SIZE];
    char *msg;

    /* 
     * OSR
     *
     * %z is not a valid format specifier in the MS C compiler. We
     * need to change those to %I
     *    
     */
#ifdef HP_mingw_BUILD
    char format_buf[OUTPUT_BUF_SIZE];
    char *buf_ptr;

    /* Copy the format string so we can modify it */
    strcpy(format_buf, format);

    /* Scan for %z */
    buf_ptr = strstr(&format_buf[0], "%z");
    while (buf_ptr != NULL) {

        /* Found it! buf_ptr[0] == %, so buf_ptr[1] is z and needs replaced */
        buf_ptr[1] = 'I';
        /* Scan for the next sequence */
        buf_ptr = strstr(buf_ptr, "%z");
    }
    
    vsprintf(output_buf, format_buf, ap);
    fprintf(stderr, "%s\n", output_buf);

    /* 
     * Flush the output so that the trace consumers see the messages
     * immediately
     */
    fflush(stderr);
#else
    vsprintf(output_buf, format, ap);
    fprintf(stderr, "%s\n", output_buf);
#endif

    WORD wType;
    switch (priority)
    {
    case LOG_ERR:
        wType = EVENTLOG_ERROR_TYPE;
        break;
    case LOG_WARNING:
        wType = EVENTLOG_WARNING_TYPE;
        break;
    case LOG_INFO:
    default:
        wType = EVENTLOG_INFORMATION_TYPE;
        break;
    }
    
    /* 
     * OSR
     *
     * For our environment we only log errors to the event log. We
     * also use the event ID LTFS_ERROR_EVENT, which allows us to
     * make the Event Viewer happy about the source of the messages
     * (we have a string for that message ID as a binary resource)
     *    
     */
#ifdef HP_mingw_BUILD
    if (wType == EVENTLOG_ERROR_TYPE) {
        HANDLE h = RegisterEventSource(NULL, "LTFS");
        msg = strchr(output_buf, ' ');
        *msg = '\0';
        msg ++;
        ReportEvent(h,                   //hEventLog
                    wType,               //EventType
                    0,                   //Category
                    LTFS_ERROR_EVENT,    //EventId
                    NULL,                //User SID
                    1,                   //String Num
                    0,                   //DataSize
                    (const CHAR **)&msg, //StringArray
                    NULL);               //Data
        DeregisterEventSource(h);
    }
#else
    HANDLE h = RegisterEventSource(NULL, "LTFS");
    char *id = output_buf + strlen("LTFS");
    msg = strchr(output_buf, ' ');
    *msg = '\0';
    msg ++;
    ReportEvent(h,      //hEventLog
            wType,      //EventType
            0,          //Category
            atoi(id),   //EventId
            NULL,       //User SID
            1,          //String Num
            0,          //DataSize
            &msg,       //StringArray
            NULL);      //Data
    DeregisterEventSource(h);
#endif
}

void syslog(int priority, const char *format, ...)
{
	va_list argp;
	va_start(argp, format);
	vsyslog(priority, format, argp);
	va_end(argp);
}
