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
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "print.h"
#include "parse.h"
#include "../include/ais_types.h"

/*
 * logging printf
 */
void
internal_log_printf (int level, char *string, ...)
{
	va_list ap;
	char newstring[1024];

	va_start(ap, string);
	
	sprintf (newstring, "L(%x): %s", level, string);
	vfprintf(stderr, newstring, ap);

	va_end(ap);
}

void
internal_log_printf_checkdebug (int level, char *string, ...)
{
	va_list ap;
	char newstring[1024];

	va_start(ap, string);
	
#ifdef DEBUG
	sprintf (newstring, "L(%x): %s", level, string);
	vfprintf(stderr, newstring, ap);
#else
	if (level != LOG_LEVEL_DEBUG) {
		sprintf (newstring, "L(%x): %s", level, string);
		vfprintf(stderr, newstring, ap);
	}
#endif

	va_end(ap);
}

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
