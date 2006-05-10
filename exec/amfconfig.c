/*
 * Copyright (c) 2002-2005 MontaVista Software, Inc.
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

#include "../include/saAis.h"
#include "../include/saAmf.h"
#include "../include/ipc_amf.h"
#include "../include/list.h"
#include "util.h"
#include "amfconfig.h"
#include "mempool.h"
#include "totem.h"

DECLARE_LIST_INIT (amf_groupHead);
DECLARE_LIST_INIT (amf_healthcheck_head);

static char error_string_response[512];

typedef enum {
	AMF_HEAD,
	AMF_GROUP,
	AMF_UNIT,
	AMF_COMPONENT,
        AMF_COMPONENT_CSI_TYPE_NAMES,
	AMF_SERVICEINSTANCE,
        AMF_SERVICEINSTANCE_CSIDESCRIPTOR,
        AMF_SERVICEINSTANCE_CSIDESCRIPTOR_NAMEVALUE,
	AMF_HEALTHCHECK
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

struct amf_healthcheck *find_healthcheck (SaAmfHealthcheckKeyT *key)
{
	struct amf_healthcheck *healthcheck;
	struct amf_healthcheck *ret_healthcheck = 0;
	struct list_head *list;

	for (list = amf_healthcheck_head.next;
		list != &amf_healthcheck_head;
		list = list->next) {

		healthcheck = list_entry (list,
			struct amf_healthcheck, list);

		if (memcmp (key, &healthcheck->key, sizeof (SaAmfHealthcheckKeyT)) == 0) {
			ret_healthcheck = healthcheck;
			break;
		}
	}
	return (ret_healthcheck);
}

struct amf_comp *find_comp (SaNameT *name)
{
	struct list_head *list_group = 0;
	struct list_head *list_unit = 0;
	struct list_head *AmfComponentList = 0;

	struct amf_group *amf_group = 0;
	struct amf_unit *amf_unit = 0;
	struct amf_comp *AmfComponent = 0;
	int found = 0;

	/*
	 * Search all groups
	 */
	for (list_group = amf_groupHead.next;
		list_group != &amf_groupHead && found == 0;
		list_group = list_group->next) {

		amf_group = list_entry (list_group,
			struct amf_group, group_list);

		/*
		 * Search all units
		 */
		for (list_unit = amf_group->unit_head.next;
			list_unit != &amf_group->unit_head && found == 0;
			list_unit = list_unit->next) {

			amf_unit = list_entry (list_unit,
				struct amf_unit, unit_list);

			/*
			 * Search all components
			 */
			for (AmfComponentList = amf_unit->comp_head.next;
				AmfComponentList != &amf_unit->comp_head && found == 0;
				AmfComponentList = AmfComponentList->next) {

				AmfComponent = list_entry (AmfComponentList,
					struct amf_comp, comp_list);

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

struct amf_unit *find_unit (SaNameT *name)
{
	struct list_head *list_group = 0;
	struct list_head *list_unit = 0;

	struct amf_group *amf_group = 0;
	struct amf_unit *amf_unit = 0;
	int found = 0;

	/*
	 * Search all groups
	 */
	for (list_group = amf_groupHead.next;
		list_group != &amf_groupHead && found == 0;
		list_group = list_group->next) {

		amf_group = list_entry (list_group,
			struct amf_group, group_list);

		/*
		 * Search all units
		 */
		for (list_unit = amf_group->unit_head.next;
			list_unit != &amf_group->unit_head && found == 0;
			list_unit = list_unit->next) {

			amf_unit = list_entry (list_unit,
				struct amf_unit, unit_list);

			if (name_match (name, &amf_unit->name)) {
				found = 1;
			}
		}
	}

	if (found) {
		return (amf_unit);
	} else {
		return (0);
	}
}

static char *strstr_rs (const char *haystack, const char *needle)
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
	char *filename;
	amf_parse_t current_parse = AMF_HEAD;
	int line_number = 0;
	char *loc;
	int i;

	struct amf_group *amf_group = 0;
	struct amf_unit *amf_unit = 0;
	struct amf_comp *amf_comp = 0;
	struct amf_si *amf_si = 0;
	struct amf_healthcheck *amf_healthcheck = 0;
	struct amf_comp_csi_type_name *csi_type_name = 0;
	struct amf_csi *amf_csi = 0;
	struct amf_csi_name_value *csi_name_value = NULL;

	filename = getenv("OPENAIS_AMF_CONFIG_FILE");
	if (!filename)
		filename = "etc/ais/groups.conf";

	fp = fopen (filename, "r");

	if (fp == 0) {
		sprintf (error_string_response,
			"Can't read %s file reason = (%s).\n",
			 filename, strerror (errno));
		*error_string = error_string_response;
		return (-1);
	}

	while (fgets (line, 255, fp)) {
		line_number += 1;
		line[strlen(line) - 1] = '\0';
		/*
		 * Clear out comments and empty lines
		 */
		if (line[0] == '#' || line[0] == '\0' || line[0] == '\n') {
			continue;
		}

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

		switch (current_parse) {
		case AMF_HEAD:
			if (strstr_rs (line, "group{")) {
				amf_group = (struct amf_group *)mempool_malloc (sizeof (struct amf_group));
				memset (amf_group, 0, sizeof (struct amf_group));
				list_init (&amf_group->group_list);
				list_init (&amf_group->unit_head);
				list_init (&amf_group->si_head);
				list_add (&amf_group->group_list, &amf_groupHead);
				memset (amf_group->clccli_path, 0, sizeof (&amf_unit->clccli_path));
				memset (amf_group->binary_path, 0, sizeof (&amf_unit->binary_path));
				current_parse = AMF_GROUP;
			} else
			if (strstr_rs (line, "healthcheck{")) {
				amf_healthcheck = (struct amf_healthcheck *)mempool_malloc (sizeof (struct amf_healthcheck));
				memset (amf_healthcheck, 0, sizeof (struct amf_healthcheck));
				list_init (&amf_healthcheck->list);
				list_add_tail (&amf_healthcheck->list,
					&amf_healthcheck_head);
				current_parse = AMF_HEALTHCHECK;
			} else {
				goto parse_error;
			}
			break;

		case AMF_GROUP:
			if ((loc = strstr_rs (line, "name=")) != 0) {
				setSaNameT (&amf_group->name, loc);
			} else
			if ((loc = strstr_rs (line, "model=")) != 0) {
				if (strcmp (loc, "2n") == 0) {
					amf_group->model = SA_AMF_2N_REDUNDANCY_MODEL;
				} else
				if (strcmp (loc, "nplusm") == 0) {
					amf_group->model = SA_AMF_NPM_REDUNDANCY_MODEL;
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
					amf_group->model = SA_AMF_NO_REDUNDANCY_MODEL;
				} else {
					goto parse_error;
				}
			} else
			if ((loc = strstr_rs (line, "preferred-active-units=")) != 0) {
				amf_group->preferred_active_units = atoi (loc);
			} else 
			if ((loc = strstr_rs (line, "preferred-standby-units=")) != 0) {
				amf_group->preferred_standby_units = atoi (loc);
			} else 
			if ((loc = strstr_rs (line, "maximum-active-instances=")) != 0) {
				amf_group->maximum_active_instances = atoi (loc);
			} else 
			if ((loc = strstr_rs (line, "maximum-standby-instances=")) != 0) {
				amf_group->maximum_standby_instances = atoi (loc);
			} else 
			if ((loc = strstr_rs (line, "clccli_path=")) != 0) {
				strcpy (amf_group->clccli_path, loc);
			} else
			if ((loc = strstr_rs (line, "binary_path=")) != 0) {
				strcpy (amf_group->binary_path, loc);
			} else
			if ((loc = strstr_rs (line, "component_restart_probation=")) != 0) {
				amf_group->component_restart_probation = atoi (loc);
				printf ("restart probation %d\n", amf_group->component_restart_probation);
			} else
			if ((loc = strstr_rs (line, "component_restart_max=")) != 0) {
				amf_group->component_restart_max = atoi (loc);
				printf ("restart max %d\n", amf_group->component_restart_max);
			} else
			if ((loc = strstr_rs (line, "unit_restart_probation=")) != 0) {
				amf_group->unit_restart_probation = atoi (loc);
				printf ("unit restart probation %d\n", amf_group->unit_restart_probation);
			} else
			if ((loc = strstr_rs (line, "unit_restart_max=")) != 0) {
				amf_group->unit_restart_max = atoi (loc);
				printf ("unit restart max %d\n", amf_group->unit_restart_max);
			} else

			if (strstr_rs (line, "unit{")) {
				amf_unit = (struct amf_unit *)mempool_malloc (sizeof (struct amf_unit));
				memset (amf_unit, 0, sizeof (struct amf_unit));
				amf_unit->amf_group = amf_group;

				amf_unit->operational_state = SA_AMF_OPERATIONAL_DISABLED;
				amf_unit->presence_state = SA_AMF_PRESENCE_UNINSTANTIATED;
				list_init (&amf_unit->comp_head);
				list_init (&amf_unit->si_head);
				amf_unit->escalation_level = ESCALATION_LEVEL_NO_ESCALATION;
				amf_unit->restart_count = 0;
				list_add_tail (&amf_unit->unit_list, &amf_group->unit_head);
				memset (amf_unit->clccli_path, 0, sizeof (&amf_unit->clccli_path));
				memset (amf_unit->binary_path, 0, sizeof (&amf_unit->binary_path));
				current_parse = AMF_UNIT;
			} else
			if (strstr_rs (line, "serviceinstance{")) {
				amf_si = (struct amf_si *)mempool_malloc (sizeof (struct amf_si));
				memset (amf_si, 0, sizeof (struct amf_si));
				list_init (&amf_si->csi_head);
				list_init (&amf_si->unit_list);
				list_init (&amf_si->pg_head);
				list_add_tail (&amf_si->si_list, &amf_group->si_head);
				amf_si->group = amf_group;
				current_parse = AMF_SERVICEINSTANCE;
			} else
			if (strstr_rs (line, "}")) {
				current_parse = AMF_HEAD;
			} else {
				goto parse_error;
			}
			break;

		case AMF_UNIT:
			if ((loc = strstr_rs (line, "name=")) != 0) {
				setSaNameT (&amf_unit->name, loc);
			} else
			if ((loc = strstr_rs (line, "component{")) != 0) {
				amf_comp = (struct amf_comp *)mempool_malloc (sizeof (struct amf_comp));
				memset (amf_comp, 0, sizeof (struct amf_comp));
				amf_comp->unit = amf_unit;
				amf_comp->operational_state = SA_AMF_OPERATIONAL_DISABLED;
				amf_comp->presence_state = SA_AMF_PRESENCE_UNINSTANTIATED;

				list_init (&amf_comp->comp_list);
				list_init (&amf_comp->healthcheck_list);
                                list_init (&amf_comp->csi_type_name_head);
				list_add_tail (&amf_comp->comp_list, &amf_unit->comp_head);

				memset (amf_comp->clccli_path, 0, sizeof (&amf_comp->clccli_path));
				memset (amf_comp->binary_path, 0, sizeof (&amf_unit->binary_path));
				memset (amf_comp->binary_name, 0, sizeof (&amf_comp->binary_name));

				current_parse = AMF_COMPONENT;
			} else
			if ((loc = strstr_rs (line, "clccli_path=")) != 0) {
				strcpy (amf_unit->clccli_path, loc);
			} else
			if ((loc = strstr_rs (line, "binary_path=")) != 0) {
				strcpy (amf_unit->binary_path, loc);
			} else
			if (strstr_rs (line, "}")) {
				current_parse = AMF_GROUP;
			} else {
				goto parse_error;
			}
			break;

		case AMF_COMPONENT:
			if ((loc = strstr_rs (line, "name=")) != 0) {
				setSaNameT (&amf_comp->name, loc);
			} else
#ifdef COMPILE_OUT
			if ((loc = strstr_rs (line, "model=")) != 0) {
				if (strcmp (loc, "x_active_and_y_standby") == 0) {
					amf_comp->componentCapabilityModel = SA_AMF_COMPONENT_CAPABILITY_X_ACTIVE_AND_Y_STANDBY;
				} else
				if (strcmp (loc, "x_active_or_y_standby") == 0) {
					amf_comp->componentCapabilityModel = SA_AMF_COMPONENT_CAPABILITY_X_ACTIVE_OR_Y_STANDBY;
				} else
				if (strcmp (loc, "1_active_or_y_standby") == 0) {
					amf_comp->componentCapabilityModel = SA_AMF_COMPONENT_CAPABILITY_1_ACTIVE_OR_Y_STANDBY;
				} else
				if (strcmp (loc, "1_active_or_1_standby") == 0) {
					amf_comp->componentCapabilityModel = SA_AMF_COMPONENT_CAPABILITY_1_ACTIVE_OR_1_STANDBY;
				} else
				if (strcmp (loc, "x_active") == 0) {
					amf_comp->componentCapabilityModel = SA_AMF_COMPONENT_CAPABILITY_X_ACTIVE;
				} else
				if (strcmp (loc, "1_active") == 0) {
					amf_comp->componentCapabilityModel = SA_AMF_COMPONENT_CAPABILITY_1_ACTIVE;
				} else
				if (strcmp (loc, "no_active") == 0) {
					amf_comp->componentCapabilityModel = SA_AMF_COMPONENT_CAPABILITY_NO_ACTIVE;
				} else {
					goto parse_error;
				}
			} else
#endif
			if ((loc = strstr_rs(line, "comptype=")) != 0) {
				if (strstr (line, "sa_aware")) {
					amf_comp->comptype = clc_component_sa_aware;
				} else
				if (strstr (line, "proxied_pre")) {
					amf_comp->comptype = clc_component_proxied_pre;
				} else
				if (strstr (line, "proxied_non_pre")) {
					amf_comp->comptype = clc_component_proxied_non_pre;
				} else
				if (strstr (line, "non_proxied_non_sa_aware")) {
					amf_comp->comptype = clc_component_proxied_non_pre;
				} else {
					goto parse_error;
				}
			} else
			if ((loc = strstr_rs(line, "instantiate=")) != 0) {
				strcpy (amf_comp->instantiate_cmd, loc);
			} else
			if ((loc = strstr_rs(line, "terminate=")) != 0) {
				strcpy (amf_comp->terminate_cmd, loc);
			} else
			if ((loc = strstr_rs(line, "cleanup=")) != 0) {
				strcpy (amf_comp->cleanup_cmd, loc);
			} else
			if ((loc = strstr_rs(line, "am_start=")) != 0) {
				strcpy (amf_comp->am_start_cmd, loc);
			} else
			if ((loc = strstr_rs(line, "am_stop=")) != 0) {
				strcpy (amf_comp->am_stop_cmd, loc);
			} else
			if ((loc = strstr_rs (line, "clccli_path=")) != 0) {
				strcpy (amf_comp->clccli_path, loc);
			} else
			if ((loc = strstr_rs (line, "binary_path=")) != 0) {
				strcpy (amf_comp->binary_path, loc);
			} else
			if ((loc = strstr_rs (line, "bn=")) != 0) {
				strcpy (amf_comp->binary_name, loc);
			} else
			if ((loc = strstr_rs (line, "csi_type_name{")) != 0) {
                                csi_type_name =
                                        (struct amf_comp_csi_type_name*)mempool_malloc (sizeof(struct amf_comp_csi_type_name));

                                list_init(&csi_type_name->list);
                                list_add_tail (&csi_type_name->list, &amf_comp->csi_type_name_head);

                                current_parse = AMF_COMPONENT_CSI_TYPE_NAMES;
                        } else
			if (strstr_rs (line, "}")) {
				current_parse = AMF_UNIT;
			} else {
				goto parse_error;
			}
			break;

                case AMF_COMPONENT_CSI_TYPE_NAMES:
                        if ((loc = strstr_rs (line, "name=")) != 0) {
                                setSaNameT(&csi_type_name->name, loc);
                        } else
                        if ((loc = strstr_rs (line, "csi_type_name{")) != 0) {
                                csi_type_name =
                                (struct amf_comp_csi_type_name*)mempool_malloc (sizeof(struct amf_comp_csi_type_name));

                                list_init(&csi_type_name->list);
                                list_add_tail (&csi_type_name->list, &amf_comp->csi_type_name_head);

                                current_parse = AMF_COMPONENT_CSI_TYPE_NAMES;
                        } else
                        if (strstr_rs (line, "}")) {
                                current_parse = AMF_COMPONENT;
                        } else {
                                goto parse_error;
                        }
                        break;
		case AMF_SERVICEINSTANCE:
			if ((loc = strstr_rs (line, "name=")) != 0) {
				setSaNameT (&amf_si->name, loc);
			} else
                        if ((loc = strstr_rs (line, "csi_descriptor{")) != 0) {
                                amf_csi = (struct amf_csi*)mempool_malloc (sizeof(struct amf_csi));

                                list_init(&amf_csi->csi_list);
                                list_init(&amf_csi->name_value_head);
                                list_add_tail (&amf_csi->csi_list, &amf_si->csi_head);

                                current_parse = AMF_SERVICEINSTANCE_CSIDESCRIPTOR;
                        } else
			if (strstr_rs (line, "}")) {
				current_parse = AMF_GROUP;
			} else {
				goto parse_error;
			}
			break;
                case AMF_SERVICEINSTANCE_CSIDESCRIPTOR:
                        if ((loc = strstr_rs (line, "csi_name=")) != 0) {
                                setSaNameT (&amf_csi->name, loc);
                        } else
                        if ((loc = strstr_rs (line, "type_name=")) != 0) {
                                setSaNameT (&amf_csi->type_name, loc);
                        } else
                        if ((loc = strstr_rs (line, "name_value{")) != 0) {
                                 csi_name_value = (struct amf_csi_name_value*)mempool_malloc (sizeof(struct amf_csi_name_value));

                                 list_init(&csi_name_value->csi_name_list);
                                 list_add_tail (&csi_name_value->csi_name_list, &amf_csi->name_value_head);

                                 current_parse = AMF_SERVICEINSTANCE_CSIDESCRIPTOR_NAMEVALUE;
                        } else
                        if (strstr_rs (line, "}")) {
                                current_parse = AMF_SERVICEINSTANCE;
                        } else {
                                goto parse_error;
                        }
                        break;
                case AMF_SERVICEINSTANCE_CSIDESCRIPTOR_NAMEVALUE:
                        if ((loc = strstr_rs (line, "name=")) != 0) {
                                strcpy(csi_name_value->name, loc);
                        } else
                        if ((loc = strstr_rs (line, "value=")) != 0) {
                                strcpy(csi_name_value->value, loc);
                        } else
                        if (strstr_rs (line, "}")) {
                                current_parse = AMF_SERVICEINSTANCE_CSIDESCRIPTOR;
                        } else {
                                goto parse_error;
                        }
                        break;

		case AMF_HEALTHCHECK:
			if ((loc = strstr_rs (line, "key=")) != 0) {
				strcpy ((char *)amf_healthcheck->key.key, loc);
				amf_healthcheck->key.keyLen = strlen (loc);
			} else 
			if ((loc = strstr_rs (line, "period=")) != 0) {
				amf_healthcheck->period = atoi (loc);
			} else
			if ((loc = strstr_rs (line, "maximum_duration=")) != 0) {
				amf_healthcheck->maximum_duration = atoi (loc);
			} else
			if (strstr_rs (line, "}")) {
				current_parse = AMF_HEAD;
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
		"parse error at %s: %d.\n", filename, line_number);
	*error_string = error_string_response;
	fclose (fp);
	return (-1);
}
