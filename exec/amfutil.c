/** @file exec/amfutil.c
 * 
 * Copyright (c) 2002-2005 MontaVista Software, Inc.
 * Author: Steven Dake (sdake@mvista.com)
 *
 * Copyright (c) 2006 Ericsson AB.
 * Author: Hans Feldt
 * Description:
 * - Reworked to match AMF B.02 information model Description:
 * - Refactoring of code into several AMF files
 * - Serializers/deserializers
 *
 * All rights reserved.
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
 * 
 * AMF utility functions
 * 
 * This file contains functions that provide different services used by other
 * AMF files. For example parsing the configuration file, printing state etc.
 * 
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <regex.h>

#include "../include/saAis.h"
#include "../include/saAmf.h"
#include "../include/ipc_amf.h"
#include "../include/list.h"
#include "util.h"
#include "amf.h"
#include "totem.h"
#include "logsys.h"
#include "aispoll.h"
#include "main.h"
#include "service.h"

LOGSYS_DECLARE_SUBSYS ("AMF", LOG_INFO);

#ifndef OPENAIS_CLUSTER_STARTUP_TIMEOUT
#define OPENAIS_CLUSTER_STARTUP_TIMEOUT 5000
#endif

struct req_exec_amf_msg {
	mar_req_header_t header;
};

static const char *presence_state_text[] = {
	"UNKNOWN",
	"UNINSTANTIATED",
	"INSTANTIATING",
	"INSTANTIATED",
	"TERMINATING",
	"RESTARTING",
	"INSTANTIATION_FAILED",
	"TERMINATION_FAILED"
};

static const char *oper_state_text[] = {
	"UNKNOWN",
	"ENABLED",
	"DISABLED"
};

static const char *admin_state_text[] = {
	"UNKNOWN",
	"UNLOCKED",
	"LOCKED",
	"LOCKED-INSTANTIATION",
	"SHUTTING-DOWN"
};

static const char *readiness_state_text[] = {
	"UNKNOWN",
	"OUT-OF-SERVICE",
	"IN-SERVICE",
};

static const char *ha_state_text[] = {
	"UNKNOWN",
	"ACTIVE",
	"STANDBY",
	"QUIESCED",
	"QUIESCING",
};

static const char *assignment_state_text[] = {
	"UNKNOWN",
	"UNASSIGNED",
	"FULLY-ASSIGNED",
	"PARTIALLY-ASSIGNED"
};

static int init_category (struct amf_comp *comp, char *loc)
{
	if (strcmp (loc, "sa_aware") == 0) {
		comp->saAmfCompCategory = SA_AMF_COMP_SA_AWARE;
	} else if (strcmp (loc, "proxy") == 0) {
		comp->saAmfCompCategory = SA_AMF_COMP_PROXY;
	} else if (strcmp (loc, "proxied") == 0) {
		comp->saAmfCompCategory = SA_AMF_COMP_PROXIED;
	} else if (strcmp (loc, "local") == 0) {
		comp->saAmfCompCategory = SA_AMF_COMP_LOCAL;
	} else {
		return -1;
	}

	return 0;
}   

static int init_capability (struct amf_comp *comp, char *loc)
{
	if (strcmp (loc, "x_active_and_y_standby") == 0) {
		comp->saAmfCompCapability = SA_AMF_COMP_X_ACTIVE_AND_Y_STANDBY;
	} else if (strcmp (loc, "x_active_or_y_standby") == 0) {
		comp->saAmfCompCapability = SA_AMF_COMP_X_ACTIVE_OR_Y_STANDBY;
	} else if (strcmp (loc, "one_active_or_y_standby") == 0) {
		comp->saAmfCompCapability = SA_AMF_COMP_ONE_ACTIVE_OR_Y_STANDBY;
	} else if (strcmp (loc, "one_active_or_one_standby") == 0) {
		comp->saAmfCompCapability = SA_AMF_COMP_ONE_ACTIVE_OR_ONE_STANDBY;
	} else if (strcmp (loc, "x_active") == 0) {
		comp->saAmfCompCapability = SA_AMF_COMP_X_ACTIVE;
	} else if (strcmp (loc, "1_active") == 0) {
		comp->saAmfCompCapability = SA_AMF_COMP_1_ACTIVE;
	} else if (strcmp (loc, "non_preinstantiable") == 0) {
		comp->saAmfCompCapability = SA_AMF_COMP_NON_PRE_INSTANTIABLE;
	} else {
		return -1;
	}

	return 0;
}

static int init_recovery_on_error (struct amf_comp *comp, char *loc)
{
	if (strcmp (loc, "component_restart") == 0) {
		comp->saAmfCompRecoveryOnError = SA_AMF_COMPONENT_RESTART;
	} else if (strcmp (loc, "component_failover") == 0) {
		comp->saAmfCompRecoveryOnError = SA_AMF_COMPONENT_FAILOVER;
	} else if (strcmp (loc, "node_switchover") == 0) {
		comp->saAmfCompRecoveryOnError = SA_AMF_NODE_SWITCHOVER;
	} else if (strcmp (loc, "node_failover") == 0) {
		comp->saAmfCompRecoveryOnError = SA_AMF_NODE_FAILOVER;
	} else if (strcmp (loc, "node_failfast") == 0) {
		comp->saAmfCompRecoveryOnError = SA_AMF_NODE_FAILFAST;
	} else if (strcmp (loc, "application_restart") == 0) {
		comp->saAmfCompRecoveryOnError = SA_AMF_APPLICATION_RESTART;
	} else if (strcmp (loc, "cluster_reset") == 0) {
		comp->saAmfCompRecoveryOnError = SA_AMF_CLUSTER_RESET;
	} else if (strcmp (loc, "no_recomondation") == 0) {
		comp->saAmfCompRecoveryOnError = SA_AMF_NO_RECOMMENDATION;
	} else {
		return -1;
	}

	return 0;
}

static void post_init_comp(struct amf_comp *comp)
{
	if (comp->saAmfCompInstantiateTimeout == 0) {
		comp->saAmfCompInstantiateTimeout = comp->saAmfCompDefaultClcCliTimeout;
	}
	if (comp->saAmfCompTerminateTimeout == 0) {
		comp->saAmfCompTerminateTimeout = comp->saAmfCompDefaultClcCliTimeout;
	}
	if (comp->saAmfCompCleanupTimeout == 0) {
		comp->saAmfCompCleanupTimeout = comp->saAmfCompDefaultClcCliTimeout;
	}
	if (comp->saAmfCompAmStartTimeout == 0) {
		comp->saAmfCompAmStartTimeout = comp->saAmfCompDefaultClcCliTimeout;
	}
	if (comp->saAmfCompAmStopTimeout == 0) {
		comp->saAmfCompAmStopTimeout = comp->saAmfCompDefaultClcCliTimeout;
	}
	if (comp->saAmfCompTerminateCallbackTimeout == 0) {
		comp->saAmfCompTerminateCallbackTimeout = comp->saAmfCompDefaultCallbackTimeOut;
	}
	if (comp->saAmfCompCSISetCallbackTimeout == 0) {
		comp->saAmfCompCSISetCallbackTimeout = comp->saAmfCompDefaultCallbackTimeOut;
	}
	if (comp->saAmfCompCSIRmvCallbackTimeout == 0) {
		comp->saAmfCompCSIRmvCallbackTimeout = comp->saAmfCompDefaultCallbackTimeOut;
	}
}

static char *trim_str(char *str)
{
	char *s = str + strlen (str) - 1;
	while (*s == '\t' || *s == ' ' || *s == '{') {
		*s = '\0';
		s--;
	}

	return str;
}

static char *rm_beginning_ws(char *str)
{
	char *s = str + strlen (str) - 1;
	while (*s == '\t' || *s == ' ') {
		*s = '\0';
		s--;
	}
	s = str;
	while (*s == '\t' || *s == ' ') {
		s++;
	}
	return s;
}

struct amf_cluster *amf_config_read (char **error_string)
{
	char buf[1024];
	char *line;
	FILE *fp;
	char *filename;
	amf_object_type_t current_parse = AMF_NONE;
	int line_number = 0;
	char *loc;
	int i;
	struct amf_cluster       *cluster;
	struct amf_application   *app = 0;
	struct amf_node          *node = 0;
	struct amf_sg            *sg = 0;
	struct amf_su            *su = 0;
	struct amf_comp          *comp = 0;
	struct amf_si            *si = 0;
	struct amf_si_ranked_su  *si_ranked_su = 0;
	struct amf_si_dependency *si_dependency = 0;
	struct amf_healthcheck   *healthcheck = 0;
	struct amf_csi           *csi = 0;
	struct amf_csi_attribute *attribute = 0;
	SaStringT                 env_var;
	int                       su_cnt = 0;
	int                       sg_cnt = 0;
	int                       comp_env_var_cnt = 0;
	int                       comp_cs_type_cnt = 0;
	int                       csi_attr_cnt = 0;
	int                       csi_dependencies_cnt = 0;
	char                     *error_reason = NULL;
	char                     *value;
	filename = getenv ("OPENAIS_AMF_CONFIG_FILE");
	if (!filename) {
		filename = "/etc/ais/amf.conf";
	}

	fp = fopen (filename, "r");
	if (fp == 0) {
		sprintf (buf, "Can't read %s file reason = (%s).\n",
			filename, strerror (errno));
		*error_string = buf;
		return NULL;
	}

	cluster = amf_cluster_new ();
	assert (cluster != NULL);

	while (fgets (buf, 255, fp)) {
		line_number += 1;
		line = buf;
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

		/* Trim whitespace from beginning of string */
		line = rm_beginning_ws(line);
		error_reason = line;
		error_reason = NULL;
		switch (current_parse) {
		case AMF_NONE:
			if ((loc = strstr_rs (line, "safAmfCluster=")) != 0) {
				setSaNameT (&cluster->name, trim_str (loc));
				current_parse = AMF_CLUSTER;
			} else {
				goto parse_error;
			}
			break;

		case AMF_CLUSTER:
			if ((loc = strstr_rs (line, "saAmfClusterClmCluster=")) != 0) {
				setSaNameT (&cluster->saAmfClusterClmCluster, loc);
			} else if ((loc = strstr_rs (line, "saAmfClusterStartupTimeout=")) != 0) {
				cluster->saAmfClusterStartupTimeout = atol(loc);
			} else if ((loc = strstr_rs (line, "safAmfNode=")) != 0) {
				node = amf_node_new (cluster, trim_str (loc));
				cluster->node_head = node;
				current_parse = AMF_NODE;
			} else if ((loc = strstr_rs (line, "safApp=")) != 0) {
				app = amf_application_new (cluster);
				setSaNameT (&app->name, trim_str (loc));
				current_parse = AMF_APPLICATION;
				sg_cnt = 0;
			} else if (strstr_rs (line, "}")) {
				if (cluster->saAmfClusterStartupTimeout == -1) {
					error_reason = "saAmfClusterStartupTimeout missing";
					goto parse_error;
				}
				/* spec: set to default value if zero */
				if (cluster->saAmfClusterStartupTimeout == 0) {
					cluster->saAmfClusterStartupTimeout = OPENAIS_CLUSTER_STARTUP_TIMEOUT;
				}
				current_parse = AMF_NONE;
			} else {
				goto parse_error;
			}
			break;

		case AMF_NODE:
			if ((loc = strstr_rs (line, "saAmfNodeSuFailOverProb=")) != 0) {
				node->saAmfNodeSuFailOverProb = atol(loc);
			} else if ((loc = strstr_rs (line, "saAmfNodeSuFailoverMax=")) != 0) {
				node->saAmfNodeSuFailoverMax = atol(loc);
			} else if ((loc = strstr_rs (line, "saAmfNodeClmNode=")) != 0) {
				setSaNameT (&node->saAmfNodeClmNode, trim_str (loc));
			} else if ((loc = strstr_rs (line, "saAmfNodeAutoRepair=")) != 0) {
				if (strcmp (loc, "true") == 0) {
					node->saAmfNodeAutoRepair = SA_TRUE;
				} else if (strcmp (loc, "false") == 0) {
					node->saAmfNodeAutoRepair = SA_FALSE;
				} else {
					goto parse_error;
				}
			} else if ((loc = strstr_rs (line, "saAmfNodeRebootOnTerminationFailure=")) != 0) {
				if (strcmp (loc, "true") == 0) {
					node->saAmfNodeRebootOnTerminationFailure = SA_TRUE;
				} else if (strcmp (loc, "false") == 0) {
					node->saAmfNodeRebootOnTerminationFailure = SA_FALSE;
				} else {
					goto parse_error;
				}
			} else if ((loc = strstr_rs (line, "saAmfNodeRebootOnInstantiationFailure=")) != 0) {
				if (strcmp (loc, "true") == 0) {
					node->saAmfNodeRebootOnInstantiationFailure = SA_TRUE;
				} else if (strcmp (loc, "false") == 0) {
					node->saAmfNodeRebootOnInstantiationFailure = SA_FALSE;
				} else {
					goto parse_error;
				}
			} else if (strstr_rs (line, "}")) {
				if (node->saAmfNodeSuFailOverProb == -1) {
					error_reason = "saAmfNodeSuFailOverProb missing";
					goto parse_error;
				}
				if (node->saAmfNodeSuFailoverMax == ~0) {
					error_reason = "saAmfNodeSuFailoverMax missing";
					goto parse_error;
				}
				if (node->saAmfNodeClmNode.length == 0) {
					error_reason = "saAmfNodeClmNode missing";
					goto parse_error;
				}
				current_parse = AMF_CLUSTER;
			} else {
				goto parse_error;
			}
			break;

		case AMF_APPLICATION:
			if ((loc = strstr_rs (line, "clccli_path=")) != 0) {
				app->clccli_path = amf_strdup(loc);
			} else if ((loc = strstr_rs (line, "safSg=")) != 0) {
				sg = amf_sg_new (app, trim_str (loc));
				sg_cnt++;
				sg->recovery_scope.comp = NULL;
				sg->recovery_scope.event_type = 0;
				sg->recovery_scope.node = NULL;
				sg->recovery_scope.sis = NULL;
				sg->recovery_scope.sus = NULL;
				current_parse = AMF_SG;
				su_cnt = 0;
			} else if ((loc = strstr_rs (line, "safSi=")) != 0) {
				si = amf_si_new (app, trim_str (loc));
				current_parse = AMF_SI;
			} else if ((loc = strstr_rs (line, "safCSType=")) != 0) {
				current_parse = AMF_CS_TYPE;
			} else if (strstr_rs (line, "}")) {
				if (sg_cnt == 1) {
					for (si = app->si_head; si != NULL; si = si->next) {
						memcpy (&si->saAmfSIProtectedbySG, &sg->name,
							sizeof (SaNameT));
					}
				} else {
					for (si = app->si_head; si != NULL; si = si->next) {
						if (si->saAmfSIProtectedbySG.length == 0) {
							error_reason = "saAmfSIProtectedbySG not set in SI"
								", needed when several SGs are specified.";
							goto parse_error;
						}
					}
				}
				current_parse = AMF_CLUSTER;
			} else {
				goto parse_error;
			}
			break;

		case AMF_SG:
			if ((loc = strstr_rs (line, "clccli_path=")) != 0) {
				sg->clccli_path = amf_strdup(loc);
			} else if ((loc = strstr_rs (line, "saAmfSGRedundancyModel=")) != 0) {
				if (strcmp (loc, "2n") == 0) {
					sg->saAmfSGRedundancyModel = SA_AMF_2N_REDUNDANCY_MODEL;
				} else if (strcmp (loc, "nplusm") == 0) {
					sg->saAmfSGRedundancyModel = SA_AMF_NPM_REDUNDANCY_MODEL;
				} else if (strcmp (loc, "nway") == 0) {
					error_reason = "nway redundancy model not supported";
					goto parse_error;
				} else if (strcmp (loc, "nwayactive") == 0) {
					error_reason = "nway active redundancy model not supported";
					goto parse_error;
				} else if (strcmp (loc, "noredundancy") == 0) {
					sg->saAmfSGRedundancyModel = SA_AMF_NO_REDUNDANCY_MODEL;
				} else {
					goto parse_error;
				}
			} else if ((loc = strstr_rs (line, "saAmfSGNumPrefActiveSUs=")) != 0) {
				sg->saAmfSGNumPrefActiveSUs = atoi (loc);
			} else if ((loc = strstr_rs (line, "saAmfSGNumPrefStandbySUs=")) != 0) {
				sg->saAmfSGNumPrefStandbySUs = atoi (loc);
			} else if ((loc = strstr_rs (line, "saAmfSGNumPrefInserviceSUs=")) != 0) {
				sg->saAmfSGNumPrefInserviceSUs = atoi (loc);
			} else if ((loc = strstr_rs (line, "saAmfSGNumPrefAssignedSUs=")) != 0) {
				sg->saAmfSGNumPrefAssignedSUs = atoi (loc);
			} else if ((loc = strstr_rs (line, "saAmfSGMaxActiveSIsperSUs=")) != 0) {
				sg->saAmfSGMaxActiveSIsperSUs = atoi (loc);
			} else if ((loc = strstr_rs (line, "saAmfSGMaxStandbySIsperSUs=")) != 0) {
				sg->saAmfSGMaxStandbySIsperSUs = atoi (loc);
			} else if ((loc = strstr_rs (line, "saAmfSGCompRestartProb=")) != 0) {
				sg->saAmfSGCompRestartProb = atoi (loc);
			} else if ((loc = strstr_rs (line, "saAmfSGCompRestartMax=")) != 0) {
				sg->saAmfSGCompRestartMax = atoi (loc);
			} else if ((loc = strstr_rs (line, "saAmfSGSuRestartProb=")) != 0) {
				sg->saAmfSGSuRestartProb = atoi (loc);
			} else if ((loc = strstr_rs (line, "saAmfSGSuRestartMax=")) != 0) {
				sg->saAmfSGSuRestartMax = atoi (loc);
			} else if ((loc = strstr_rs (line, "saAmfSGAutoAdjustProb=")) != 0) {
				sg->saAmfSGAutoAdjustProb = atoi (loc);
			} else if ((loc = strstr_rs (line, "saAmfSGAutoRepair=")) != 0) {
				sg->saAmfSGAutoRepair = atoi (loc);
			} else if ((loc = strstr_rs (line, "safSu=")) != 0) {
				su = amf_su_new (sg, trim_str (loc));
				su_cnt++;
				current_parse = AMF_SU;
			} else if (strstr_rs (line, "}")) {
				if (sg->saAmfSGRedundancyModel == 0) {
					error_reason = "saAmfSGRedundancyModel missing";
					goto parse_error;
				}
				if (sg->saAmfSGCompRestartProb == -1) {
					error_reason = "saAmfSGCompRestartProb missing";
					goto parse_error;
				}
				if (sg->saAmfSGCompRestartMax == ~0) {
					error_reason = "saAmfSGCompRestartMax missing";
					goto parse_error;
				}
				if (sg->saAmfSGSuRestartProb == -1) {
					error_reason = "saAmfSGSuRestartProb missing";
					goto parse_error;
				}
				if (sg->saAmfSGSuRestartMax == ~0) {
					error_reason = "saAmfSGSuRestartMax missing";
					goto parse_error;
				}
				if (sg->saAmfSGAutoAdjustProb == -1) {
					error_reason = "saAmfSGAutoAdjustProb missing";
					goto parse_error;
				}
				if (sg->saAmfSGAutoRepair > 1) {
					error_reason = "saAmfSGAutoRepair erroneous";
					goto parse_error;
				}
				if (sg->saAmfSGNumPrefInserviceSUs == ~0) {
					sg->saAmfSGNumPrefInserviceSUs = su_cnt;
				}
				if (sg->saAmfSGNumPrefAssignedSUs == ~0) {
					sg->saAmfSGNumPrefAssignedSUs =
						sg->saAmfSGNumPrefInserviceSUs;
				}
				current_parse = AMF_APPLICATION;
			} else {
				goto parse_error;
			}
			break;

		case AMF_SU:
			if ((loc = strstr_rs (line, "saAmfSUNumComponents=")) != 0) {
				su->saAmfSUNumComponents = atoi (loc);
			} else if ((loc = strstr_rs (line, "saAmfSUIsExternal=")) != 0) {
				su->saAmfSUIsExternal = atoi (loc);
			} else if ((loc = strstr_rs (line, "saAmfSUFailover=")) != 0) {
				su->saAmfSUFailover = atoi (loc);
			} else if ((loc = strstr_rs (line, "clccli_path=")) != 0) {
				su->clccli_path = amf_strdup(loc);
			} else if ((loc = strstr_rs (line, "saAmfSUHostedByNode=")) != 0) {
				setSaNameT (&su->saAmfSUHostedByNode, loc);
			} else if ((loc = strstr_rs (line, "safComp=")) != 0) {
				comp = amf_comp_new (su, trim_str (loc));
				comp_env_var_cnt = 0;
				comp_cs_type_cnt = 0;
				current_parse = AMF_COMP;
			} else if (strstr_rs (line, "}")) {
				if (su->saAmfSUNumComponents == 0) {
					error_reason = "saAmfSUNumComponents missing";
					goto parse_error;
				}
				if (su->saAmfSUIsExternal > 1) {
					error_reason = "saAmfSUIsExternal erroneous";
					goto parse_error;
				}
				if (su->saAmfSUFailover > 1) {
					error_reason = "saAmfSUFailover erroneous";
					goto parse_error;
				}
				if (strcmp ((char*)su->saAmfSUHostedByNode.value, "") == 0) {
					error_reason = "saAmfSUHostedByNode missing";
					goto parse_error;
				}
				current_parse = AMF_SG;
			} else {
				goto parse_error;
			}
			break;

		case AMF_COMP:
			if ((loc = strstr_rs (line, "clccli_path=")) != 0) {
				comp->clccli_path = amf_strdup(loc);
			} else if ((loc = strstr_rs (line, "saAmfCompCsTypes{")) != 0) {
				current_parse = AMF_COMP_CS_TYPE;
			} else if ((loc = strstr_rs(line, "saAmfCompCategory=")) != 0) {
				if (init_category(comp, loc) != 0) {
					error_reason = "unknown category";
					goto parse_error;
				}
			} else if ((loc = strstr_rs (line, "saAmfCompCapability=")) != 0) {
				if (init_capability(comp, loc) != 0) {
					error_reason = "unknown capability model";
					goto parse_error;
				}
			} else if ((loc = strstr_rs(line, "saAmfCompNumMaxActiveCsi=")) != 0) {
				comp->saAmfCompNumMaxActiveCsi = atol (loc);
			} else if ((loc = strstr_rs(line, "saAmfCompNumMaxStandbyCsi=")) != 0) {
				comp->saAmfCompNumMaxStandbyCsi = atol (loc);
			} else if ((loc = strstr_rs (line, "saAmfCompCmdEnv{")) != 0) {
				current_parse = AMF_COMP_ENV_VAR;
			} else if ((loc = strstr_rs(line, "saAmfCompDefaultClcCliTimeout=")) != 0) {
				comp->saAmfCompDefaultClcCliTimeout = atol (loc);
			} else if ((loc = strstr_rs(line, "saAmfCompDefaultCallbackTimeOut=")) != 0) {
				comp->saAmfCompDefaultCallbackTimeOut = atol (loc);
			} else if ((loc = strstr_rs (line, "saAmfCompInstantiateCmdArgv=")) != 0) {
				comp->saAmfCompInstantiateCmdArgv = amf_strdup(loc);
			} else if ((loc = strstr_rs ( line, "saAmfCompInstantiateCmd=")) != 0) {
				comp->saAmfCompInstantiateCmd = amf_strdup(loc);
			} else if ((loc = strstr_rs(line, "saAmfCompInstantiateTimeout=")) != 0) {
				comp->saAmfCompInstantiateTimeout = atol (loc);
			} else if ((loc = strstr_rs(line, "saAmfCompInstantiationLevel=")) != 0) {
				comp->saAmfCompInstantiationLevel = atol (loc);
			} else if ((loc = strstr_rs(line, "saAmfCompNumMaxInstantiateWithoutDelay=")) != 0) {
				comp->saAmfCompNumMaxInstantiateWithoutDelay = atol (loc);
			} else if ((loc = strstr_rs(line, "saAmfCompNumMaxInstantiateWithDelay=")) != 0) {
				comp->saAmfCompNumMaxInstantiateWithDelay = atol (loc);
			} else if ((loc = strstr_rs(line, "saAmfCompDelayBetweenInstantiateAttempts=")) != 0) {
				comp->saAmfCompDelayBetweenInstantiateAttempts = atol (loc);
			} else if ((loc = strstr_rs (line, "saAmfCompTerminateCmdArgv=")) != 0) {
				comp->saAmfCompTerminateCmdArgv = amf_strdup(loc);
			} else if ((loc = strstr_rs (line, "saAmfCompTerminateCmd=")) != 0) {
				comp->saAmfCompTerminateCmd = amf_strdup(loc);
			} else if ((loc = strstr_rs(line, "saAmfCompTerminateTimeout=")) != 0) {
				comp->saAmfCompTerminateTimeout = atol (loc);
			} else if ((loc = strstr_rs (line, "saAmfCompCleanupCmdArgv=")) != 0) {
				comp->saAmfCompCleanupCmdArgv = amf_strdup(loc);
			} else if ((loc = strstr_rs (line, "saAmfCompCleanupCmd=")) != 0) {
				comp->saAmfCompCleanupCmd = amf_strdup(loc);
			} else if ((loc = strstr_rs(line, "saAmfCompCleanupTimeout=")) != 0) {
				comp->saAmfCompCleanupTimeout = atol (loc);
			} else if ((loc = strstr_rs(line, "saAmfCompTerminateCallbackTimeout=")) != 0) {
				comp->saAmfCompTerminateCallbackTimeout = atol (loc);
			} else if ((loc = strstr_rs(line, "saAmfCompCSISetCallbackTimeout=")) != 0) {
				comp->saAmfCompCSISetCallbackTimeout = atol (loc);
			} else if ((loc = strstr_rs(line, "saAmfCompQuiescingCompleteTimeout=")) != 0) {
				comp->saAmfCompQuiescingCompleteTimeout = atol (loc);
			} else if ((loc = strstr_rs(line, "saAmfCompCSIRmvCallbackTimeout=")) != 0) {
				comp->saAmfCompCSIRmvCallbackTimeout = atol (loc);
			} else if ((loc = strstr_rs (line, "saAmfCompRecoveryOnError=")) != 0) {
				if (init_recovery_on_error (comp, loc) != 0) {
					error_reason = "bad value";
					goto parse_error;
				}
			} else if ((loc = strstr_rs (line, "saAmfCompDisableRestart=")) != 0) {
				if (strcmp (loc, "false") == 0) {
					comp->saAmfCompDisableRestart = SA_FALSE;
				} else if (strcmp (loc, "true") == 0) {
					comp->saAmfCompDisableRestart = SA_TRUE;
				} else {
					error_reason = "bad value";
					goto parse_error;
				}
			} else if ((loc = strstr_rs (line, "saAmfCompProxyCsi=")) != 0) {
				setSaNameT (&comp->saAmfCompProxyCsi, loc);
			} else if ((loc = strstr_rs (line, "safHealthcheckKey=")) != 0) {
				healthcheck = calloc (1, sizeof (struct amf_healthcheck));
				healthcheck->next = comp->healthcheck_head;
				comp->healthcheck_head = healthcheck;
				healthcheck->comp = comp;
				strcpy ((char *)healthcheck->safHealthcheckKey.key, trim_str (loc));
				healthcheck->safHealthcheckKey.keyLen = strlen (loc);
				current_parse = AMF_HEALTHCHECK;
			} else if (strstr_rs (line, "}")) {
				if (comp->saAmfCompCategory == 0) {
					error_reason = "category missing";
					goto parse_error;
				}
				if (comp->saAmfCompCapability == 0) {
					error_reason = "capability model missing";
					goto parse_error;
				}
				if (comp->saAmfCompCategory == SA_AMF_COMP_SA_AWARE) {
					comp->comptype = clc_component_sa_aware;
				} else if (comp->saAmfCompCategory == SA_AMF_COMP_PROXY) {
					if (comp->saAmfCompCapability == SA_AMF_COMP_NON_PRE_INSTANTIABLE) {
						comp->comptype = clc_component_proxied_non_pre;
					} else {
						comp->comptype = clc_component_proxied_pre;
					}
				} else if (comp->saAmfCompCategory == SA_AMF_COMP_LOCAL) {
					comp->comptype = clc_component_non_proxied_non_sa_aware;
				}
				if (comp->saAmfCompNumMaxActiveCsi == 0) {
					error_reason = "saAmfCompNumMaxActiveCsi missing";
					goto parse_error;
				}
				if (comp->saAmfCompNumMaxStandbyCsi == 0) {
					error_reason = "saAmfCompNumMaxStandbyCsi missing";
					goto parse_error;
				}
				if (comp->saAmfCompDefaultClcCliTimeout == 0) {
					error_reason = "saAmfCompDefaultClcCliTimeout missing or erroneous";
					goto parse_error;
				}
				if (comp->saAmfCompDefaultCallbackTimeOut == 0) {
					error_reason = "saAmfCompDefaultCallbackTimeOut missing or erroneous";
					goto parse_error;
				}
				if (comp->saAmfCompRecoveryOnError == 0) {
					error_reason = "saAmfCompRecoveryOnError missing";
					goto parse_error;
				}
				post_init_comp (comp);
				current_parse = AMF_SU;
			} else {
				error_reason = line;
				goto parse_error;
			}
			break;

		case AMF_COMP_CS_TYPE:
			if (strstr_rs (line, "}")) {
				current_parse = AMF_COMP;
			} else {
				comp_cs_type_cnt++;
				comp->saAmfCompCsTypes = realloc (comp->saAmfCompCsTypes,
					(comp_cs_type_cnt + 1) * sizeof(SaNameT));
				comp->saAmfCompCsTypes[comp_cs_type_cnt] = NULL;
				comp->saAmfCompCsTypes[comp_cs_type_cnt - 1] = amf_malloc (sizeof(SaNameT));
				setSaNameT (comp->saAmfCompCsTypes[comp_cs_type_cnt - 1], line);
			}
			break;

		case AMF_COMP_ENV_VAR:
			if (strstr_rs (line, "}")) {
				current_parse = AMF_COMP;
			} else if ((loc = strchr (line, '=')) != 0) {
				comp_env_var_cnt++;
				comp->saAmfCompCmdEnv = realloc (comp->saAmfCompCmdEnv,
					(comp_env_var_cnt + 1) * sizeof(SaStringT));
				comp->saAmfCompCmdEnv[comp_env_var_cnt] = NULL;
				env_var = comp->saAmfCompCmdEnv[comp_env_var_cnt - 1] = amf_strdup(line);
			} else {
				goto parse_error;
			}
			break;

		case AMF_HEALTHCHECK:
			if ((loc = strstr_rs (line, "saAmfHealthcheckPeriod=")) != 0) {
				healthcheck->saAmfHealthcheckPeriod = atoi (loc);
			} else if ((loc = strstr_rs (line, "saAmfHealthcheckMaxDuration=")) != 0) {
				healthcheck->saAmfHealthcheckMaxDuration = atoi (loc);
			} else if (strstr_rs (line, "}")) {
				current_parse = AMF_COMP;
			} else {
				goto parse_error;
			}
			break;

		case AMF_SI:
			if ((loc = strstr_rs (line, "safRankedSu=")) != 0) {
				si_ranked_su = calloc (1, sizeof(struct amf_si_ranked_su));
				si_ranked_su->si_next = si->ranked_sus;
				si->ranked_sus = si_ranked_su;
				si_ranked_su->si = si;
				setSaNameT (&si_ranked_su->name, trim_str (loc));
				current_parse = AMF_SI_RANKED_SU;
			} else if ((loc = strstr_rs (line, "safDepend=")) != 0) {
				si_dependency = calloc (1, sizeof(struct amf_si_dependency));
				si_dependency->next = si->depends_on;
				si->depends_on = si_dependency;
				setSaNameT (&si_dependency->name, trim_str (loc));
				current_parse = AMF_SI_DEPENDENCY;
			} else if ((loc = strstr_rs (line, "safCsi=")) != 0) {
				csi = calloc (1, sizeof(struct amf_csi));
				csi->next = si->csi_head;
				si->csi_head = csi;
				csi->si = si;
				setSaNameT (&csi->name, trim_str (loc));
				current_parse = AMF_CSI;
			} else if ((loc = strstr_rs (line, "saAmfSIProtectedbySG=")) != 0) {
				setSaNameT (&si->saAmfSIProtectedbySG, loc);
			} else if ((loc = strstr_rs (line, "saAmfSIRank=")) != 0) {
				si->saAmfSIRank = atoi (loc);
			} else if ((loc = strstr_rs (line, "saAmfSINumCSIs=")) != 0) {
				si->saAmfSINumCSIs = atoi (loc);
			} else if ((loc = strstr_rs (line, "saAmfSIPrefActiveAssignments=")) != 0) {
				si->saAmfSIPrefActiveAssignments = atoi (loc);
			} else if ((loc = strstr_rs (line, "saAmfSIPrefActiveAssignments=")) != 0) {
				si->saAmfSIPrefStandbyAssignments = atoi (loc);
			} else if (strstr_rs (line, "}")) {
				if (si->saAmfSINumCSIs == 0) {
					error_reason = "saAmfSINumCSIs missing";
					goto parse_error;
				}
				current_parse = AMF_APPLICATION;
			} else {
				goto parse_error;
			}
			break;

		case AMF_SI_RANKED_SU:
			if ((loc = strstr_rs (line, "saAmfRank=")) != 0) {
				si_ranked_su->saAmfRank = atoi (loc);
			} else if (strstr_rs (line, "}")) {
				current_parse = AMF_SI;
			} else {
				goto parse_error;
			}
			break;

		case AMF_SI_DEPENDENCY:
			if ((loc = strstr_rs (line, "saAmfToleranceTime=")) != 0) {
				si_dependency->saAmfToleranceTime = atoi (loc);
			} else if (strstr_rs (line, "}")) {
				current_parse = AMF_SI;
			} else {
				goto parse_error;
			}
			break;

		case AMF_CSI:
			if ((loc = strstr_rs (line, "saAmfCSTypeName=")) != 0) {
				setSaNameT (&csi->saAmfCSTypeName, loc);
			} else if ((loc = strstr_rs (line, "safCSIAttr=")) != 0) {
				attribute = calloc (1, sizeof(struct amf_csi_attribute));
				attribute->next = csi->attributes_head;
				csi->attributes_head = attribute;
				attribute->name = amf_strdup(loc);
				csi_attr_cnt = 1;
				current_parse = AMF_CSI_ATTRIBUTE;
			} else if ((loc = strstr_rs (line, "saAmfCsiDependencies{")) != 0) {
				csi_dependencies_cnt = 0;
				current_parse = AMF_CSI_DEPENDENCIES;
			} else if (strstr_rs (line, "}")) {
				if (strcmp(getSaNameT(&csi->saAmfCSTypeName), "") == 0) {
					error_reason = "saAmfCSTypeName missing";
					goto parse_error;
				}
				current_parse = AMF_SI;
			} else {
				goto parse_error;
			}
			break;

		case AMF_CSI_DEPENDENCIES:
			if (strstr_rs (line, "}")) {
				current_parse = AMF_CSI;
			} else if ((loc = strstr_rs (line, "saAmfCSIDependency=")) != 0) {
				csi_dependencies_cnt++;
				csi->saAmfCSIDependencies = realloc (csi->saAmfCSIDependencies,
					(csi_dependencies_cnt + 1) * sizeof(SaNameT));
				csi->saAmfCSIDependencies[csi_dependencies_cnt] = NULL;
				csi->saAmfCSIDependencies[csi_dependencies_cnt - 1] =
					amf_malloc (sizeof(SaNameT));
				setSaNameT (
					csi->saAmfCSIDependencies[csi_dependencies_cnt - 1], loc);
			} else {
				goto parse_error;
			}
			break;

		case AMF_CSI_ATTRIBUTE:
			if ((loc = strstr_rs (line, "}")) != 0) {
				current_parse = AMF_CSI;
			} else {
				value = rm_beginning_ws (line);
				attribute->value = realloc (attribute->value,
					sizeof (SaStringT) * (csi_attr_cnt + 1));
				attribute->value[csi_attr_cnt - 1] = amf_strdup(value);
				attribute->value[csi_attr_cnt] = NULL;
				csi_attr_cnt++;
			}
			break;

		case AMF_CS_TYPE:
			if ((loc = strstr_rs (line, "}")) != 0) {
				current_parse = AMF_APPLICATION;
			}
			break;

		default:
			error_reason = "Invalid state\n";
			goto parse_error;
			break;
		}
	}

	fclose (fp);

	sprintf (buf, "Successfully read AMF configuration file '%s'.\n", filename);
	*error_string = buf;

	return cluster;

parse_error:
	sprintf (buf, "parse error at %s: %d: %s\n",
		filename, line_number, error_reason);
	*error_string = buf;
	fclose (fp);
	return NULL;
}

static void print_csi_assignment (struct amf_comp *comp,
	struct amf_csi_assignment *csi_assignment)
{
	log_printf (LOG_INFO, "            safCSI=%s\n", csi_assignment->csi->name.value);
	log_printf (LOG_INFO, "              HA state: %s\n",
		ha_state_text[csi_assignment->saAmfCSICompHAState]);
}

static void print_si_assignment (struct amf_su *su,
	struct amf_si_assignment *si_assignment)
{
	log_printf (LOG_INFO, "          safSi=%s\n", si_assignment->si->name.value);
	log_printf (LOG_INFO, "            HA state: %s\n",
		ha_state_text[si_assignment->saAmfSISUHAState]);
}

void amf_runtime_attributes_print (struct amf_cluster *cluster)
{
	struct amf_node *node;
	struct amf_application *app;
	struct amf_sg *sg;
	struct amf_su *su;
	struct amf_comp *comp;
	struct amf_si *si;
	struct amf_csi *csi;

	log_printf (LOG_INFO, "AMF runtime attributes:");
	log_printf (LOG_INFO, "===================================================");
	log_printf (LOG_INFO, "safCluster=%s", getSaNameT(&cluster->name));
	log_printf (LOG_INFO, "  admin state: %s\n",
		admin_state_text[cluster->saAmfClusterAdminState]);
	log_printf (LOG_INFO, "  state:       %u\n", cluster->acsm_state);
	for (node = cluster->node_head; node != NULL; node = node->next) {
		log_printf (LOG_INFO, "  safNode=%s\n", getSaNameT (&node->name));
		log_printf (LOG_INFO, "    CLM Node:    %s\n", getSaNameT (&node->saAmfNodeClmNode));
		log_printf (LOG_INFO, "    node ID:     %u\n", node->nodeid);
		log_printf (LOG_INFO, "    admin state: %s\n",
			admin_state_text[node->saAmfNodeAdminState]);
		log_printf (LOG_INFO, "    oper state:  %s\n",
			oper_state_text[node->saAmfNodeOperState]);
		log_printf (LOG_INFO, "    acsm state:  %u\n", node->acsm_state);
	}
	for (app = cluster->application_head; app != NULL; app = app->next) {
		log_printf (LOG_INFO, "  safApp=%s\n", getSaNameT(&app->name));
		log_printf (LOG_INFO, "    admin state: %s\n",
			admin_state_text[app->saAmfApplicationAdminState]);
		log_printf (LOG_INFO, "    num_sg:      %d\n", app->saAmfApplicationCurrNumSG);
		for (sg = app->sg_head; sg != NULL; sg = sg->next) {
			log_printf (LOG_INFO, "    safSg=%s\n", getSaNameT(&sg->name));
			log_printf (LOG_INFO, "      avail_state:        %u\n",
				sg->avail_state);
			log_printf (LOG_INFO, "      admin state:        %s\n",
				admin_state_text[sg->saAmfSGAdminState]);
			log_printf (LOG_INFO, "      assigned SUs        %d\n",
				sg->saAmfSGNumCurrAssignedSUs);
			log_printf (LOG_INFO, "      non inst. spare SUs %d\n",
				sg->saAmfSGNumCurrNonInstantiatedSpareSUs);
			log_printf (LOG_INFO, "      inst. spare SUs     %d\n",
				sg->saAmfSGNumCurrInstantiatedSpareSUs);
			for (su = sg->su_head; su != NULL; su = su->next) {
				log_printf (LOG_INFO, "      safSU=%s\n", getSaNameT(&su->name));
				log_printf (LOG_INFO, "        oper state:      %s\n",
					oper_state_text[su->saAmfSUOperState]);
				log_printf (LOG_INFO, "        admin state:     %s\n",
					admin_state_text[su->saAmfSUAdminState]);
				log_printf (LOG_INFO, "        readiness state: %s\n",
					readiness_state_text[amf_su_get_saAmfSUReadinessState (su)]);
				log_printf (LOG_INFO, "        presence state:  %s\n",
					presence_state_text[su->saAmfSUPresenceState]);
				log_printf (LOG_INFO, "        hosted by node   %s\n",
					su->saAmfSUHostedByNode.value);
				log_printf (LOG_INFO, "        num active SIs   %d\n",
					amf_su_get_saAmfSUNumCurrActiveSIs (su));
				log_printf (LOG_INFO, "        num standby SIs  %d\n",
					amf_su_get_saAmfSUNumCurrStandbySIs (su));
				log_printf (LOG_INFO, "        restart count    %d\n",
					su->saAmfSURestartCount);
				log_printf (LOG_INFO, "        restart control state %d\n",
					su->restart_control_state);
				log_printf (LOG_INFO, "        SU failover cnt  %d\n", su->su_failover_cnt);
				log_printf (LOG_INFO, "        assigned SIs:");
				amf_su_foreach_si_assignment (su, print_si_assignment);

				for (comp = su->comp_head; comp != NULL; comp = comp->next) {
					log_printf (LOG_INFO, "        safComp=%s\n", getSaNameT(&comp->name));
					log_printf (LOG_INFO, "          oper state:      %s\n",
						oper_state_text[comp->saAmfCompOperState]);
					log_printf (LOG_INFO, "          readiness state: %s\n",
						readiness_state_text[amf_comp_get_saAmfCompReadinessState (comp)]);
					log_printf (LOG_INFO, "          presence state:  %s\n",
						presence_state_text[comp->saAmfCompPresenceState]);
					log_printf (LOG_INFO, "          num active CSIs  %d\n",
						amf_comp_get_saAmfCompNumCurrActiveCsi (comp));
					log_printf (LOG_INFO, "          num standby CSIs %d\n",
						amf_comp_get_saAmfCompNumCurrStandbyCsi (comp));
					log_printf (LOG_INFO, "          restart count    %d\n",
						comp->saAmfCompRestartCount);
					log_printf (LOG_INFO, "          assigned CSIs:");
					amf_comp_foreach_csi_assignment (
						comp, print_csi_assignment);
				}
			}
		}
		for (si = app->si_head; si != NULL; si = si->next) {
			log_printf (LOG_INFO, "    safSi=%s\n", getSaNameT(&si->name));
			log_printf (LOG_INFO, "      admin state:         %s\n",
				admin_state_text[si->saAmfSIAdminState]);
			log_printf (LOG_INFO, "      assignm. state:      %s\n",
				assignment_state_text[
				amf_si_get_saAmfSIAssignmentState (si)]);
			log_printf (LOG_INFO, "      active assignments:  %d\n",
				amf_si_get_saAmfSINumCurrActiveAssignments (si));
			log_printf (LOG_INFO, "      standby assignments: %d\n",
				amf_si_get_saAmfSINumCurrStandbyAssignments (si));
			for (csi = si->csi_head; csi != NULL; csi = csi->next) {
				log_printf (LOG_INFO, "      safCsi=%s\n", getSaNameT(&csi->name));
			}
		}
	}
	log_printf (LOG_INFO, "===================================================");
}

/* to be removed... */
int amf_enabled (struct objdb_iface_ver0 *objdb)
{
	unsigned int object_service_handle;
	char *value;
	int enabled = 0;

	objdb->object_find_reset (OBJECT_PARENT_HANDLE);
	if (objdb->object_find (
		OBJECT_PARENT_HANDLE,
		"amf",
		strlen ("amf"),
		&object_service_handle) == 0) {

		value = NULL;
		if (!objdb->object_key_get (object_service_handle,
			"mode",
			strlen ("mode"),
			(void *)&value,
			NULL) && value) {

			if (strcmp (value, "enabled") == 0) {
				enabled = 1;
			} else
				if (strcmp (value, "disabled") == 0) {
				enabled = 0;
			}
		}
	}

	return enabled;
}

const char *amf_admin_state (int state)
{
	return admin_state_text[state];
}

const char *amf_op_state (int state)
{
	return oper_state_text[state];
}

const char *amf_presence_state (int state)
{
	return presence_state_text[state];
}

const char *amf_ha_state (int state)
{
	return ha_state_text[state];
}

const char *amf_readiness_state (int state)
{
	return readiness_state_text[state];
}

const char *amf_assignment_state (int state)
{
	return assignment_state_text[state];
}

#define ALIGN_ADDR(addr) ((addr) + (4 - ((unsigned long)(addr) % 4)))

char *amf_serialize_SaNameT (char *buf, int *size, int *offset, SaNameT *name)
{
	char *tmp = buf;

	if ((*size - *offset ) < sizeof (SaNameT)) {
		*size += sizeof (SaNameT);
		tmp = realloc (buf, *size);
		if (tmp == NULL) {
			openais_exit_error (AIS_DONE_OUT_OF_MEMORY);
		}
	}

	memcpy (&tmp[*offset], name, sizeof (SaNameT));

	(*offset) += sizeof (SaNameT);

	return tmp;
}

char *amf_serialize_SaStringT (char *buf, int *size, int *offset, SaStringT str)
{
	unsigned int len;

	if (str != NULL) {
		len = strlen ((char*)str);
	} else {
		len = 0;
	}

	return amf_serialize_opaque (buf, size, offset, str, len);
}

char *amf_serialize_SaUint16T (char *buf, int *size, int *offset, SaUint16T num)
{
	char *tmp = buf;

	if ((*size - *offset ) < sizeof (SaUint16T)) {
		*size += sizeof (SaUint16T);
		tmp = realloc (buf, *size);
		if (tmp == NULL) {
			openais_exit_error (AIS_DONE_OUT_OF_MEMORY);
		}
	}

	*((SaUint16T *)&tmp[*offset]) = num;
	(*offset) += sizeof (SaUint16T);

	return tmp;
}

char *amf_serialize_SaUint32T (char *buf, int *size, int *offset, SaUint32T num)
{
	char *tmp = buf;

	if ((*size - *offset ) < sizeof (SaUint32T)) {
		*size += sizeof (SaUint32T);
		tmp = realloc (buf, *size);
		if (tmp == NULL) {
			openais_exit_error (AIS_DONE_OUT_OF_MEMORY);
		}
	}

	*((SaUint32T *)&tmp[*offset]) = num;
	(*offset) += sizeof (SaUint32T);

	return tmp;
}

char *amf_serialize_opaque (
	char *buf, int *size, int *offset, void *src, int cnt)
{
	unsigned int required_size;
	char *tmp = buf;

	required_size = cnt + sizeof (SaUint32T);

	if ((*size - *offset ) < required_size) {
		*size += required_size;
		tmp = realloc (buf, *size);
		if (tmp == NULL) {
			openais_exit_error (AIS_DONE_OUT_OF_MEMORY);
		}
	}

	*((SaUint32T *)&tmp[*offset]) = cnt;
	(*offset) += sizeof (SaUint32T);
	memcpy (&tmp[*offset], src, cnt);
	(*offset) += cnt;

	return tmp;
}

char *amf_deserialize_SaNameT (char *buf, SaNameT *name)
{
	memcpy (name, buf, sizeof (SaNameT));
	return (buf + sizeof (SaNameT));
}

char *amf_deserialize_SaStringT (char *buf, SaStringT *str)
{
	int len;
	char *tmp, *tmp_str;

	len = *((SaUint32T *)buf);
	tmp = buf + sizeof (SaUint32T);

	if (len > 0) {
		tmp_str = amf_malloc (len + 1);
		memcpy (tmp_str, tmp, len);
		tmp_str[len] = '\0';
		*str = tmp_str;
	} else {
		*str = NULL;
	}

	tmp += len;

	return tmp;
}

char *amf_deserialize_SaUint16T (char *buf, SaUint16T *num)
{
	*num = *((SaUint16T *)buf);
	return buf + sizeof (SaUint16T);
}

char *amf_deserialize_SaUint32T (char *buf, SaUint32T *num)
{
	*num = *((SaUint32T *)buf);
	return buf + sizeof (SaUint32T);
}

char *amf_deserialize_opaque (char *buf, void *dst, int *cnt)
{
	*cnt = *((SaUint32T *)buf);
	memcpy (dst, buf + sizeof (SaUint32T), *cnt);
	return buf + *cnt + sizeof (SaUint32T);
}

void *_amf_malloc (size_t size, const char *file, unsigned int line)
{
	void *tmp = malloc (size);

	if (tmp == NULL) {
		log_printf (LOG_LEVEL_ERROR, "AMF out-of-memory at %s:%u", file, line);
		openais_exit_error (AIS_DONE_OUT_OF_MEMORY);
	}

	return tmp;
}

void *_amf_calloc (size_t nmemb, size_t size, const char *file, unsigned int line)
{
	void *tmp = calloc (nmemb, size);

	if (tmp == NULL) {
		log_printf (LOG_LEVEL_ERROR, "AMF out-of-memory at %s:%u", file, line);
		openais_exit_error (AIS_DONE_OUT_OF_MEMORY);
	}

	return tmp;
}

void *_amf_realloc (void* ptr, size_t size, const char *file, unsigned int line)
{
	void *tmp = realloc (ptr, size);

	if (tmp == NULL) {
		log_printf (LOG_LEVEL_ERROR, "AMF out-of-memory at %s:%u", file, line);
		openais_exit_error (AIS_DONE_OUT_OF_MEMORY);
	}

	return tmp;
}

char *_amf_strdup (const char *in_str, const char *file, unsigned int line)
{
	char *out_str = strdup (in_str);

	if (out_str == NULL) {
		log_printf (LOG_LEVEL_ERROR, "AMF out-of-memory at %s:%u", file, line);
		openais_exit_error (AIS_DONE_OUT_OF_MEMORY);
	}

	return out_str;
}

int sa_amf_grep_one_sub_match(const char *string, char *pattern, 
	SaNameT *matches_arr)
{
	int             status;
	regex_t         re;
	size_t          nmatch = 2;
	regmatch_t      pmatch[nmatch];

	int i;

	ENTER("'%s %s'",string, pattern);

	if (regcomp(&re, pattern, REG_EXTENDED) != 0) {
		status = 0;
		goto out;
	}
	status = regexec(&re, string, nmatch, pmatch, 0);
	if (status != 0) {
		regfree(&re);
		status = 0;
		goto out;
	} else {


		for (i = 0; i < nmatch; i++) {
			int sub_string_len;
			sub_string_len = pmatch[i].rm_eo - pmatch[i].rm_so;
			if (i==1) {

				memcpy(matches_arr[i].value, string + pmatch[i].rm_so, 
					sub_string_len);
				matches_arr[i].value[sub_string_len] = '\0';
			}
		}
		status = 1;
		regfree(&re);
	}
	out:
	return status;

}


int sa_amf_grep(const char *string, char *pattern, size_t nmatch,
	SaNameT *matches_arr)
{
	int             status;
	regex_t         re;
	regmatch_t      pmatch[nmatch];

	int i;

	ENTER("'%s %s'",string, pattern);
	if (regcomp(&re, pattern, REG_EXTENDED) != 0) {
		status = 0;
		goto out;
	}
	status = regexec(&re, string, nmatch, pmatch, 0);
	if (status != 0) {
		regfree(&re);
		status = 0;
		goto out;
	} else {


		for (i = 0; i < nmatch; i++) {
			int sub_string_len;
			sub_string_len = pmatch[i].rm_eo - pmatch[i].rm_so; 
			memcpy(matches_arr[i].value, string + pmatch[i].rm_so, 
				sub_string_len);
			matches_arr[i].value[sub_string_len] = '\0';
			matches_arr[i].length = sub_string_len;
		}
		status = 1;
		regfree(&re);
	}
out:
	return status;

}

/**
 * Multicast a message to the cluster. Errors are treated as
 * fatal and will exit the program.
 * @param msg_id
 * @param buf
 * @param len
 * 
 * @return int
 */
int amf_msg_mcast (int msg_id, void *buf, size_t len)
{
	struct req_exec_amf_msg msg;
	struct iovec iov[2];
	int iov_cnt;
	int res;

//	ENTER ("%u, %p, %u", msg_id, buf, len);

	msg.header.size = sizeof (msg);
	msg.header.id = SERVICE_ID_MAKE (AMF_SERVICE, msg_id);
	iov[0].iov_base = (char *)&msg;
	iov[0].iov_len  = sizeof (msg);

	if (buf == NULL) {
		msg.header.size = sizeof (msg);
		iov_cnt = 1;
	} else {
		msg.header.size = sizeof (msg) + len;
		iov[1].iov_base = buf;
		iov[1].iov_len  = len;
		iov_cnt = 2;
	}

	res = totempg_groups_mcast_joined (
		openais_group_handle, iov, iov_cnt, TOTEMPG_AGREED);

	if (res != 0) {
		dprintf("Unable to send %d bytes\n", msg.header.size);
		openais_exit_error (AIS_DONE_FATAL_ERR);
	}

	return res;
}

void amf_fifo_put (int entry_type, amf_fifo_t **root, int size_of_data, 
	void *data)
{
	amf_fifo_t *fifo;
	amf_fifo_t **new_item = root;

	/* Insert newest entry at the end of the single linked list */
	for (fifo = *root; fifo != NULL; fifo = fifo->next) {
		if (fifo->next == NULL) {
			new_item = &fifo->next;
		}		
	}
	*new_item = amf_malloc (size_of_data + sizeof (amf_fifo_t));
	fifo = *new_item;
	
	/* Set data of this entry*/
	fifo->entry_type = entry_type;
	fifo->next = NULL;
	fifo->size_of_data = size_of_data;
	memcpy (fifo->data, data, size_of_data);
}

int amf_fifo_get (amf_fifo_t **root, void *data)
{
	amf_fifo_t *fifo;
	int result = 0;

	fifo = *root;
	if (fifo != NULL) {
		/* Unlink oldest entry*/
		*root = fifo->next;
		memcpy (data, fifo->data, fifo->size_of_data);
		free (fifo);
		result = 1;
	}
	return result;
}


/**
 *
 * Use timer to call function f (void *data) after that current
 * execution in this thread has been re-assumed because of a
 * time-out. Time-out time is 0 msec so f will be called as soon
 * as possible. *
 * 
 * @param async_func
 * @param func_param
 */

void amf_call_function_asynchronous (async_func_t async_func, void *func_param)
{
	
	static poll_timer_handle async_func_timer_handle;
	poll_timer_add (aisexec_poll_handle, 0, func_param, async_func, 
		&async_func_timer_handle);
}

