/*
 * Copyright (c) 2002-2004 MontaVista Software, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@mvista.com)
 *
 * This software licensed under BSD license, the text of which follows:
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of the MontaVista Software, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/un.h>


#include "print.h"
#include "../include/saAis.h"

unsigned int logmode = LOG_MODE_STDERR | LOG_MODE_SYSLOG;
char *logfile = 0;

static char *log_levels[] = {
	"[ASSERT  ]",
	"[SECURITY]",
	"[ERROR   ]",
	"[WARNING ]",
	"[NOTICE  ]",
	"[DEBUG   ]"
};

static char *log_services[] = {
	"[ASSERT]",
	"[MAIN ]",
	"[GMI  ]",
	"[CLM  ]",
	"[AMF  ]",
	"[CKPT ]",
	"[EVT  ]",
	"[LCK  ]",
	"[EVS  ]",
	"[SYNC ]"
};

#define LOG_MODE_DEBUG      1
#define LOG_MODE_TIMESTAMP  2
#define LOG_MODE_FILE       4
#define LOG_MODE_SYSLOG     8
#define LOG_MODE_STDERR     16

int log_syslog_fd = -1;
FILE *log_file_fp = 0;

struct sockaddr_un syslog_sockaddr = {
	sun_family: AF_UNIX,
	sun_path: "/dev/log"
};

int log_setup (char **error_string, int log_mode, char *log_file)
{
	static char error_string_response[512];

	if (log_mode & LOG_MODE_SYSLOG && log_syslog_fd == -1) {
		log_syslog_fd = socket (AF_UNIX, SOCK_DGRAM, 0);
		if (log_syslog_fd == -1) {
			sprintf (error_string_response,
				"Can't create syslog socket for reason (%s).\n", strerror (errno));
			*error_string = error_string_response;
			return (-1);
		}
	}

	if (log_mode & LOG_MODE_FILE) {
		log_file_fp = fopen (log_file, "a+");
		if (log_file_fp == 0) {
			sprintf (error_string_response,
				"Can't open logfile '%s' for reason (%s).\n", logfile, strerror (errno));
			*error_string = error_string_response;
			return (-1);
		}
	}
			
	logmode = log_mode;
	logfile = log_file;

	return (0);
}

void log_syslog (char *log_string) {
	struct msghdr msg_log;
	struct iovec iov_log;
	int res;

	if (log_syslog_fd == -1) {
		log_syslog_fd = socket (AF_UNIX, SOCK_DGRAM, 0);
	}
	if (log_syslog_fd == -1) {
		return;
	}
	iov_log.iov_base = log_string;
	iov_log.iov_len = strlen (log_string) + 1;

	msg_log.msg_iov = &iov_log;
	msg_log.msg_iovlen = 1;
	msg_log.msg_name = &syslog_sockaddr;
	msg_log.msg_namelen = sizeof (syslog_sockaddr);
	msg_log.msg_control = 0;
	msg_log.msg_controllen = 0;
	msg_log.msg_flags = 0;

	res = sendmsg (log_syslog_fd, &msg_log, MSG_NOSIGNAL | MSG_DONTWAIT);
}

void internal_log_printf (int logclass, char *string, ...)
{
	va_list ap;
	char newstring[4096];
	char log_string[4096];
	char char_time[512];
	time_t curr_time;
	int level;
	int service;

	va_start (ap, string);
	
	assert (logmode != 0);
	level = logclass >> 16;
	service = logclass & 0xff;

	if (level == LOG_LEVEL_DEBUG && ((logmode & LOG_MODE_DEBUG) == 0)) {
		return;
	}

	if (((logmode & LOG_MODE_FILE) || (logmode & LOG_MODE_STDERR)) && 
		(logmode & LOG_MODE_TIMESTAMP)) {
		curr_time = time (NULL);
		strftime (char_time, sizeof (char_time), "%b %d %k:%M:%S", localtime (&curr_time));
		sprintf (newstring, "%s %s %s %s", char_time, log_levels[level], log_services[service], string);
	} else {
		sprintf (newstring, "%s %s %s", log_levels[level], log_services[service], string);
	}
	vsprintf (log_string, newstring, ap);

	/*
	 * Output the log data
	 */
	if (logmode & LOG_MODE_SYSLOG) {
		log_syslog (log_string);
	}
	if (logmode & LOG_MODE_FILE && log_file_fp != 0) {
		fprintf (log_file_fp, "%s", log_string);
	}
	if (logmode & LOG_MODE_STDERR) {
		fprintf (stderr, "%s", log_string);
	}
	fflush (stdout);

	va_end(ap);
}


char *getSaNameT (SaNameT *name)
{
	static char ret_name[300];

	memset (ret_name, 0, sizeof (ret_name));
	if (name->length > 299) {
		memcpy (ret_name, name->value, 299);
	} else {

		memcpy (ret_name, name->value, name->length);
	}
	return (ret_name);
}

#ifdef DEBUG
extern char *getSaClmNodeAddressT (SaClmNodeAddressT *nodeAddress) {
	int i;
	static char node_address[300];
	int pos;

	for (i = 0, pos = 0; i < nodeAddress->length; i++) {
		pos += sprintf (&node_address[pos], "%d.", nodeAddress->value[i]);
	}
	return (node_address);
}

void printSaClmClusterNodeT (char *description, SaClmClusterNodeT *clusterNode) {
	log_printf (LOG_LEVEL_NOTICE, "Node Information for %s:\n", description);

	log_printf (LOG_LEVEL_NOTICE, "\tnode id is %x\n", (int)clusterNode->nodeId);

	log_printf (LOG_LEVEL_NOTICE, "\tnode address is %s\n", getSaClmNodeAddressT (&clusterNode->nodeAddress));

	log_printf (LOG_LEVEL_NOTICE, "\tnode name is %s.\n", getSaNameT (&clusterNode->nodeName));

	log_printf (LOG_LEVEL_NOTICE, "\tcluster name is %s.\n", getSaNameT (&clusterNode->clusterName));

	log_printf (LOG_LEVEL_NOTICE, "\tMember is %d\n", clusterNode->member);

	log_printf (LOG_LEVEL_NOTICE, "\tTimestamp is %llx nanoseconds\n", clusterNode->bootTimestamp);
}
#endif /* DEBUG */


#ifdef CODE_COVERAGE_COMPILE_OUT
void saAmfPrintGroups (void)
{
	struct list_head *AmfGroupList;
	struct list_head *AmfUnitList;
	struct list_head *AmfComponentList;
	struct list_head *AmfProtectionGroupList;
	struct saAmfGroup *saAmfGroup;
	struct saAmfUnit *AmfUnit;
	struct saAmfComponent *AmfComponent;
	struct saAmfProtectionGroup *AmfProtectionGroup;

	for (AmfGroupList = saAmfGroupHead.next;
		AmfGroupList != &saAmfGroupHead;
		AmfGroupList = AmfGroupList->next) {

		saAmfGroup = list_entry (AmfGroupList, 
			struct saAmfGroup, saAmfGroupList);

		log_printf (LOG_LEVEL_DEBUG, "group {\n");
		log_printf (LOG_LEVEL_DEBUG, "\tname = ", getSaNameT (&saAmfGroup->name));
		log_printf (LOG_LEVEL_DEBUG, "\tmodel = %d\n", saAmfGroup->model);
		log_printf (LOG_LEVEL_DEBUG, "\tactive-units = %d\n", (int)saAmfGroup->saAmfActiveUnitsDesired);
		log_printf (LOG_LEVEL_DEBUG, "\tbackup-units = %d\n", (int)saAmfGroup->saAmfStandbyUnitsDesired);

		for (AmfUnitList = saAmfGroup->saAmfUnitHead.next;
			AmfUnitList != &saAmfGroup->saAmfUnitHead;
			AmfUnitList = AmfUnitList->next) {

			AmfUnit = list_entry (AmfUnitList, 
				struct saAmfUnit, saAmfUnitList);

			log_printf (LOG_LEVEL_DEBUG, "\tunit {\n");
			log_printf (LOG_LEVEL_DEBUG, "\t\tname = ", getSaNameT (&AmfUnit->name));

			for (AmfComponentList = AmfUnit->saAmfComponentHead.next;
				AmfComponentList != &AmfUnit->saAmfComponentHead;
				AmfComponentList = AmfComponentList->next) {

				AmfComponent = list_entry (AmfComponentList, 
					struct saAmfComponent, saAmfComponentList);
				log_printf (LOG_LEVEL_DEBUG, "\t\tcomponent {\n");
				log_printf (LOG_LEVEL_DEBUG, "\t\t\tname = ", getSaNameT (&AmfComponent->name));
				log_printf (LOG_LEVEL_DEBUG, "\t\t\tmodel = %d\n", AmfComponent->componentCapabilityModel);
				log_printf (LOG_LEVEL_DEBUG, "\t\t}\n");
			}
			log_printf (LOG_LEVEL_DEBUG, "\t}\n");
		}

		for (AmfProtectionGroupList = saAmfGroup->saAmfProtectionGroupHead.next;
			AmfProtectionGroupList != &saAmfGroup->saAmfProtectionGroupHead;
			AmfProtectionGroupList = AmfProtectionGroupList->next) {

			AmfProtectionGroup = list_entry (AmfProtectionGroupList, 
				struct saAmfProtectionGroup, saAmfProtectionGroupList);

			log_printf (LOG_LEVEL_DEBUG, "\tprotection {\n");
			log_printf (LOG_LEVEL_DEBUG, "\t\tname = ", getSaNameT (&AmfProtectionGroup->name));

			for (AmfComponentList = AmfProtectionGroup->saAmfMembersHead.next;
				AmfComponentList != &AmfProtectionGroup->saAmfMembersHead;
				AmfComponentList = AmfComponentList->next) {

				AmfComponent = list_entry (AmfComponentList, 
					struct saAmfComponent, saAmfProtectionGroupList);

				log_printf (LOG_LEVEL_DEBUG, "\t\tmember = ", getSaNameT (&AmfComponent->name));
			}
			log_printf (LOG_LEVEL_DEBUG, "\t}\n");
		}
		log_printf (LOG_LEVEL_DEBUG, "}\n");
	}
}
#endif /* CODE_COVERAGE_COMPILE_OUT */

