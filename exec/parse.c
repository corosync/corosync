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
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../include/ais_types.h"
#include "../include/list.h"
#include "util.h"
#include "parse.h"
#include "mempool.h"
#include "print.h"

DECLARE_LIST_INIT (saAmfGroupHead);

static char error_string_response[512];

typedef enum {
	AMF_HEAD,
	AMF_GROUP,
	AMF_UNIT,
	AMF_PROTECTION,
	AMF_COMPONENT
} amf_parse_t;

typedef enum {
	MAIN_HEAD,
	MAIN_NETWORK,
	MAIN_LOGGING,
	MAIN_KEY,
	MAIN_TIMEOUT,
	MAIN_EVENT
} main_parse_t;

void setSaNameT (SaNameT *name, char *str) {
	strncpy ((char *)name->value, str, SA_MAX_NAME_LENGTH);
	if (strlen ((char *)name->value) > SA_MAX_NAME_LENGTH) {
		name->length = SA_MAX_NAME_LENGTH;
	} else {
		name->length = strlen (str);
	}
}

int SaNameTisEqual (SaNameT *str1, char *str2) {
	if (str1->length == strlen (str2)) {
		return ((strncmp ((char *)str1->value, (char *)str2,
			str1->length)) == 0);
	} else {
		return 0;
	}
}

struct saAmfComponent *findComponent (SaNameT *name)
{
	struct list_head *AmfGroupList = 0;
	struct list_head *AmfUnitList = 0;
	struct list_head *AmfComponentList = 0;

	struct saAmfGroup *saAmfGroup = 0;
	struct saAmfUnit *AmfUnit = 0;
	struct saAmfComponent *AmfComponent = 0;
	int found = 0;

	/*
	 * Search all groups
	 */
	for (AmfGroupList = saAmfGroupHead.next;
		AmfGroupList != &saAmfGroupHead && found == 0;
		AmfGroupList = AmfGroupList->next) {

		saAmfGroup = list_entry (AmfGroupList,
			struct saAmfGroup, saAmfGroupList);

		/*
		 * Search all units
		 */
		for (AmfUnitList = saAmfGroup->saAmfUnitHead.next;
			AmfUnitList != &saAmfGroup->saAmfUnitHead && found == 0;
			AmfUnitList = AmfUnitList->next) {

			AmfUnit = list_entry (AmfUnitList,
				struct saAmfUnit, saAmfUnitList);

			/*
			 * Search all components
			 */
			for (AmfComponentList = AmfUnit->saAmfComponentHead.next;
				AmfComponentList != &AmfUnit->saAmfComponentHead && found == 0;
				AmfComponentList = AmfComponentList->next) {

				AmfComponent = list_entry (AmfComponentList,
					struct saAmfComponent, saAmfComponentList);

				if (name_match (name, &AmfComponent->name)) {
					found = 1;
				}
			}
		}
	}

	if (found) {
		return (AmfComponent);
	} else {
		return (0);
	}
}

char *
strstr_rs (const char *haystack, const char *needle)
{
	char *end_address;
	char *new_needle;

	new_needle = (char *)mempool_strdup (needle);
	new_needle[strlen(new_needle) - 1] = '\0';

	end_address = strstr (haystack, new_needle);
	if (end_address) {
		end_address += strlen (new_needle);
		end_address = strstr (end_address, needle + strlen (new_needle));
	}
	if (end_address) {
		end_address += 1; /* skip past { or = */
		do {
			if (*end_address == '\t' || *end_address == ' ') {
				end_address++;
			} else {
				break;
			}
		} while (*end_address != '\0');
	}

	mempool_free (new_needle);
	return (end_address);
}

extern int openais_amf_config_read (char **error_string)
{
	char line[255];
	FILE *fp;
	amf_parse_t current_parse = AMF_HEAD;
	int line_number = 0;
	char *loc;
	int i;

	struct saAmfGroup *saAmfGroup = 0;
	struct saAmfUnit *saAmfUnit = 0;
	struct saAmfProtectionGroup *saAmfProtectionGroup = 0;
	struct saAmfComponent *saAmfComponent = 0;
	struct list_head *findAmfUnitList = 0;
	struct list_head *findAmfComponentList = 0;
	struct saAmfUnit *findAmfUnit = 0;
	struct saAmfComponent *findAmfComponent = 0;
	SaNameT componentName;

	fp = fopen ("/etc/ais/groups.conf", "r");
	if (fp == 0) {
		sprintf (error_string_response,
			"Can't read /etc/ais/groups.conf file reason = (%s).\n", strerror (errno));
		*error_string = error_string_response;
		return (-1);
	}

	while (fgets (line, 255, fp)) {
		line_number += 1;
		line[strlen(line) - 1] = '\0';
		/*
		 * Clear out white space and tabs
		 */
		for (i = strlen (line) - 1; i > -1; i--) {
			if (line[i] == '\t' || line[i] == ' ') {
				line[i] = '\0';
			} else {
				break;
			}
		}
		/*
		 * Clear out comments and empty lines
		 */
		if (line[0] == '#' || line[0] == '\0') {
			continue;
		}
			
		switch (current_parse) {
		case AMF_HEAD:
			if (strstr_rs (line, "group{")) {
				saAmfGroup = (struct saAmfGroup *)mempool_malloc (sizeof (struct saAmfGroup));
				memset (saAmfGroup, 0, sizeof (struct saAmfGroup));
				list_init (&saAmfGroup->saAmfGroupList);
				list_init (&saAmfGroup->saAmfUnitHead);
				list_init (&saAmfGroup->saAmfProtectionGroupHead);
				list_add (&saAmfGroup->saAmfGroupList, &saAmfGroupHead);
				current_parse = AMF_GROUP;
			} else
			if (strcmp (line, "") == 0) {
			} else {
				goto parse_error;
			}
			break;

		case AMF_GROUP:
			if ((loc = strstr_rs (line, "name=")) != 0) {
				setSaNameT (&saAmfGroup->name, loc);
			} else
			if ((loc = strstr_rs (line, "model=")) != 0) {
				if (strcmp (loc, "2n") == 0) {
					saAmfGroup->model = GROUPCAPABILITYMODEL_2N;
				} else
				if (strcmp (loc, "nplusm") == 0) {
					saAmfGroup->model = GROUPCAPABILITYMODEL_NPLUSM;
				} else
				if (strcmp (loc, "nway") == 0) {
					printf ("nway redundancy model not supported.\n");
					goto parse_error;
				} else
				if (strcmp (loc, "nwayactive") == 0) {
					printf ("nway active redundancy model not supported.\n");
					goto parse_error;
				} else
				if (strcmp (loc, "noredundancy") == 0) {
					saAmfGroup->model = GROUPCAPABILITYMODEL_NOREDUNDANCY;
				} else {
					goto parse_error;
				}
			} else
			if ((loc = strstr_rs (line, "active-units=")) != 0) {
				saAmfGroup->saAmfActiveUnitsDesired = atoi (loc);
			} else 
			if ((loc = strstr_rs (line, "backup-units=")) != 0) {
				saAmfGroup->saAmfStandbyUnitsDesired = atoi (loc);
			} else 
			if (strstr_rs (line, "unit{")) {
				saAmfUnit = (struct saAmfUnit *)mempool_malloc (sizeof (struct saAmfUnit));
				memset (saAmfUnit, 0, sizeof (struct saAmfUnit));
				saAmfUnit->saAmfGroup = saAmfGroup;

				list_init (&saAmfUnit->saAmfComponentHead);
				list_add (&saAmfUnit->saAmfUnitList, &saAmfGroup->saAmfUnitHead);
				current_parse = AMF_UNIT;
			} else
			if (strstr_rs (line, "protection{")) {
				saAmfProtectionGroup = (struct saAmfProtectionGroup *)mempool_malloc (sizeof (struct saAmfProtectionGroup));
				memset (saAmfProtectionGroup, 0, sizeof (struct saAmfProtectionGroup));
				list_init (&saAmfProtectionGroup->saAmfMembersHead);
				list_init (&saAmfProtectionGroup->saAmfProtectionGroupList);
				list_add (&saAmfProtectionGroup->saAmfProtectionGroupList, &saAmfGroup->saAmfProtectionGroupHead);

				current_parse = AMF_PROTECTION;
			} else
			if (strstr_rs (line, "}")) {
				current_parse = AMF_HEAD;
			} else {
				goto parse_error;
			}
			break;

		case AMF_UNIT:
			if ((loc = strstr_rs (line, "name=")) != 0) {
				setSaNameT (&saAmfUnit->name, loc);
			} else
			if ((loc = strstr_rs (line, "component{")) != 0) {
				saAmfComponent = (struct saAmfComponent *)mempool_malloc (sizeof (struct saAmfComponent));
				memset (saAmfComponent, 0, sizeof (struct saAmfComponent));
				saAmfComponent->saAmfUnit = saAmfUnit;
				saAmfComponent->currentReadinessState = SA_AMF_OUT_OF_SERVICE;
				saAmfComponent->newReadinessState = SA_AMF_OUT_OF_SERVICE;
				saAmfComponent->currentHAState = SA_AMF_QUIESCED;
				saAmfComponent->newHAState = SA_AMF_QUIESCED;
				saAmfComponent->healthcheckInterval = 100;
				list_init (&saAmfComponent->saAmfComponentList);
				list_init (&saAmfComponent->saAmfProtectionGroupList);
				list_add (&saAmfComponent->saAmfComponentList, &saAmfUnit->saAmfComponentHead);

				current_parse = AMF_COMPONENT;
			} else
			if (strstr_rs (line, "}")) {
				current_parse = AMF_GROUP;
			} else {
				goto parse_error;
			}
			break;

		case AMF_COMPONENT:
			if ((loc = strstr_rs (line, "name=")) != 0) {
				setSaNameT (&saAmfComponent->name, loc);
			} else
			if ((loc = strstr_rs (line, "model=")) != 0) {
				if (strcmp (loc, "x_active_and_y_standby") == 0) {
					saAmfComponent->componentCapabilityModel = SA_AMF_COMPONENT_CAPABILITY_X_ACTIVE_AND_Y_STANDBY;
				} else
				if (strcmp (loc, "x_active_or_y_standby") == 0) {
					saAmfComponent->componentCapabilityModel = SA_AMF_COMPONENT_CAPABILITY_X_ACTIVE_OR_Y_STANDBY;
				} else
				if (strcmp (loc, "1_active_or_y_standby") == 0) {
					saAmfComponent->componentCapabilityModel = SA_AMF_COMPONENT_CAPABILITY_1_ACTIVE_OR_Y_STANDBY;
				} else
				if (strcmp (loc, "1_active_or_1_standby") == 0) {
					saAmfComponent->componentCapabilityModel = SA_AMF_COMPONENT_CAPABILITY_1_ACTIVE_OR_1_STANDBY;
				} else
				if (strcmp (loc, "x_active") == 0) {
					saAmfComponent->componentCapabilityModel = SA_AMF_COMPONENT_CAPABILITY_X_ACTIVE;
				} else
				if (strcmp (loc, "1_active") == 0) {
					saAmfComponent->componentCapabilityModel = SA_AMF_COMPONENT_CAPABILITY_1_ACTIVE;
				} else
				if (strcmp (loc, "no_active") == 0) {
					saAmfComponent->componentCapabilityModel = SA_AMF_COMPONENT_CAPABILITY_NO_ACTIVE;
				} else {
					goto parse_error;
				}
			} else
			if (strstr_rs (line, "}")) {
				current_parse = AMF_UNIT;
			} else {
				goto parse_error;
			}
			break;

		case AMF_PROTECTION:
			if ((loc = strstr_rs (line, "name=")) != 0) {
				setSaNameT (&saAmfProtectionGroup->name, loc);
			} else
			if ((loc = strstr_rs (line, "member=")) != 0) {
				for (findAmfUnitList = saAmfGroup->saAmfUnitHead.next;
					findAmfUnitList != &saAmfGroup->saAmfUnitHead;
					findAmfUnitList = findAmfUnitList->next) {

					findAmfUnit = list_entry (findAmfUnitList, 
						struct saAmfUnit, saAmfUnitList);
					for (findAmfComponentList = findAmfUnit->saAmfComponentHead.next;
						findAmfComponentList != &findAmfUnit->saAmfComponentHead;
						findAmfComponentList = findAmfComponentList->next) {

						findAmfComponent = list_entry (findAmfComponentList, 
							struct saAmfComponent, saAmfComponentList);

						if (SaNameTisEqual (&findAmfComponent->name, loc)) {
							list_add (&findAmfComponent->saAmfProtectionGroupList,
								&saAmfProtectionGroup->saAmfMembersHead);
						}
					}
					/*
					 * Connect component to protection group
					 */
					setSaNameT (&componentName, loc);
					saAmfComponent = findComponent (&componentName);
					saAmfComponent->saAmfProtectionGroup = saAmfProtectionGroup;
				}
			} else
			if (strstr_rs (line, "}")) {
				current_parse = AMF_GROUP;
			} else {
				goto parse_error;
			}
			break;

		default:
			printf ("Invalid state\n");
			goto parse_error;
			break;
		}
	}

	fclose (fp);
	return (0);

parse_error:
	sprintf (error_string_response,
		"parse error at /etc/groups.conf:%d.\n", line_number);
	*error_string = error_string_response;
	fclose (fp);
	return (-1);
}

extern int openais_main_config_read (char **error_string,
    struct openais_config *openais_config,
	int interface_max)
{
	FILE *fp;
	int line_number = 0;
	main_parse_t parse = MAIN_HEAD;
	int network_parsed = 0;
	int logging_parsed = 0;
	int key_parsed = 0;
	int timeout_parsed = 0;
	int event_parsed = 0;
	char *loc;
	int i;
	int parse_done = 0;
	char line[512];
	char *error_reason = error_string_response;

	memset (openais_config, 0, sizeof (struct openais_config));
	openais_config->interfaces = malloc (sizeof (struct totem_interface) * interface_max);
	if (openais_config->interfaces == 0) {
		parse_done = 1;
		*error_string = "Out of memory trying to allocate ethernet interface storage area";
		return -1;
	}

	memset (openais_config->interfaces, 0,
		sizeof (struct totem_interface) * interface_max);

	openais_config->mcast_addr.sin_family = AF_INET;
	fp = fopen ("/etc/ais/openais.conf", "r");
	if (fp == 0) {
		parse_done = 1;
		sprintf (error_reason, "Can't read file reason = (%s)\n", strerror (errno));
		*error_string = error_reason;
		return -1;
	}

	while (fgets (line, 255, fp)) {
		line_number += 1;
		line[strlen(line) - 1] = '\0';
		/*
		 * Clear out white space and tabs
		 */
		for (i = strlen (line) - 1; i > -1; i--) {
			if (line[i] == '\t' || line[i] == ' ') {
				line[i] = '\0';
			} else {
				break;
			}
		}
		/*
		 * Clear out comments and empty lines
		 */
		if (line[0] == '#' || line[0] == '\0') {
			continue;
		}
			
		line_number += 1;

		switch (parse) {
		case MAIN_HEAD:
			if (network_parsed == 0 && strstr_rs (line, "network{")) {
				network_parsed = 1;
				parse = MAIN_NETWORK;
			} else
			if (logging_parsed == 0 && strstr_rs (line, "logging{")) {
				logging_parsed = 1;
				parse = MAIN_LOGGING;
			} else
			if (key_parsed == 0 && strstr_rs (line, "key{")) {
				key_parsed = 1;
				parse = MAIN_KEY;
			} else
			if (timeout_parsed == 0 && strstr_rs (line, "timeout{")) {
				timeout_parsed = 1;
				parse = MAIN_TIMEOUT;
			} else 
			if (event_parsed == 0 && strstr_rs (line, "event{")) {
				event_parsed = 1;
				parse = MAIN_EVENT;
			} else {
				goto parse_error;
			}
			break;

		case MAIN_NETWORK:
			if ((loc = strstr_rs (line, "mcastaddr:"))) {
				inet_aton (loc, &openais_config->mcast_addr.sin_addr);
			} else
			if ((loc = strstr_rs (line, "mcastport:"))) {
				openais_config->mcast_addr.sin_port = htons (atoi (loc));
			} else
			if ((loc = strstr_rs (line, "bindnetaddr:"))) {
				if (interface_max == openais_config->interface_count) {
					sprintf (error_reason,
						"%d is too many interfaces in /etc/ais/network.conf",
					openais_config->interface_count);
					goto parse_error;
				}
				inet_aton (loc,
					&openais_config->interfaces[openais_config->interface_count].bindnet.sin_addr);
				openais_config->interface_count += 1;
			} else
			if ((loc = strstr_rs (line, "}"))) {
				parse = MAIN_HEAD;
			} else {
				goto parse_error;
			}
			break;

		case MAIN_LOGGING:
			if ((loc = strstr_rs (line, "logoutput:"))) {
				if (strcmp (loc, "file") == 0) {
					openais_config->logmode |= LOG_MODE_FILE;
				} else
				if (strcmp (loc, "syslog") == 0) {
					openais_config->logmode |= LOG_MODE_SYSLOG;
				} else
				if (strcmp (loc, "stderr") == 0) {
					openais_config->logmode |= LOG_MODE_STDERR;
				} else {
					goto parse_error;
				}
			} else
			if ((loc = strstr_rs (line, "debug:"))) {
				if (strcmp (loc, "on") == 0) {
					openais_config->logmode |= LOG_MODE_DEBUG;
				} else
				if (strcmp (loc, "off") == 0) {
					openais_config->logmode &= ~LOG_MODE_DEBUG;
				} else {
					goto parse_error;
				}
			} else
			if ((loc = strstr_rs (line, "timestamp:"))) {
				if (strcmp (loc, "on") == 0) {
					openais_config->logmode |= LOG_MODE_TIMESTAMP;
				} else
				if (strcmp (loc, "off") == 0) {
					openais_config->logmode &= ~LOG_MODE_TIMESTAMP;
				} else {
					goto parse_error;
				}
			} else 
			if ((loc = strstr_rs (line, "logfile:"))) {
				openais_config->logfile = strdup (loc);
			} else
			if ((loc = strstr_rs (line, "}"))) {
				parse = MAIN_HEAD;
			} else {
				goto parse_error;
			}
			break;
		case MAIN_TIMEOUT:
			if ((loc = strstr_rs (line, "token:"))) {
				openais_config->timeouts[TOTEM_TOKEN]= atoi(loc);
			} else if ((loc = strstr_rs (line, "token_retransmit:"))) {
				openais_config->timeouts[TOTEM_RETRANSMIT_TOKEN] = atoi(loc);
			} else if ((loc = strstr_rs (line, "hold:"))) {
				openais_config->timeouts[TOTEM_HOLD_TOKEN] = atoi(loc);
			} else if ((loc = strstr_rs (line, "retransmits_before_loss:"))) {
				openais_config->timeouts[TOTEM_RETRANSMITS_BEFORE_LOSS] = atoi(loc);
		
			} else if ((loc = strstr_rs (line, "join:"))) {
				openais_config->timeouts[TOTEM_JOIN] = atoi(loc);
			} else if ((loc = strstr_rs (line, "consensus:"))) {
				openais_config->timeouts[TOTEM_CONSENSUS] = atoi(loc);
			} else if ((loc = strstr_rs (line, "merge:"))) {
				openais_config->timeouts[TOTEM_MERGE] = atoi(loc);
			} else if ((loc = strstr_rs (line, "downcheck:"))) {
				openais_config->timeouts[TOTEM_DOWNCHECK] = atoi(loc);
			} else if ((loc = strstr_rs (line, "fail_recv_const:"))) {
				openais_config->timeouts[TOTEM_FAIL_RECV_CONST] = atoi(loc);
			} else if ((loc = strstr_rs (line, "}"))) {
				parse = MAIN_HEAD;
			} else {
				goto parse_error;
			}
			break;

		case MAIN_EVENT:
			if ((loc = strstr_rs (line, "delivery_queue_size:"))) {
					openais_config->evt_delivery_queue_size = atoi(loc);
			} else if ((loc = strstr_rs (line, "delivery_queue_resume:"))) {
					openais_config->evt_delivery_queue_resume = atoi(loc);
			} else if ((loc = strstr_rs (line, "}"))) {
				parse = MAIN_HEAD;
			} else {
				goto parse_error;
			}
			break;

		default:
			assert (0 == 1); /* SHOULDN'T HAPPEN */
			break;	
		}
	}

	/*
	 * Some error checking of parsed data to make sure its valid
	 */
	parse_done = 1;
	if (openais_config->mcast_addr.sin_addr.s_addr == 0) {
		error_reason = "No multicast address specified";
		goto parse_error;
	}

	if (openais_config->mcast_addr.sin_port == 0) {
		error_reason = "No multicast port specified";
		goto parse_error;
	}

	if (openais_config->interface_count == 0) {
		error_reason = "No bindnet specified";
		goto parse_error;
	}

	if ((openais_config->logmode & LOG_MODE_FILE) && openais_config->logfile == 0) {
		error_reason = "logmode set to 'file' but no logfile specified";
		goto parse_error;
	}

	if (parse == MAIN_HEAD) {
		fclose (fp);
		return (0);
	} else {
		error_reason = "Missing closing brace";
		goto parse_error;
	}

parse_error:
	if (parse_done) {
		sprintf (error_string_response,
			"parse error in /etc/ais/openais.conf: %s.\n", error_reason);
	} else {
		sprintf (error_string_response,
			"parse error in /etc/ais/openais.conf: %s (line %d).\n",
			error_reason, line_number);
	}
	*error_string = error_string_response;
	fclose (fp);
	return (-1);
}
