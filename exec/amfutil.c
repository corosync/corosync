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

#include "../include/saAis.h"
#include "../include/saAmf.h"
#include "../include/ipc_amf.h"
#include "../include/list.h"
#include "util.h"
#include "amf.h"
#include "totem.h"
#include "print.h"

typedef enum {
	AMF_HEAD,
	AMF_APPLICATION,
	AMF_CLUSTER,
	AMF_NODE,
	AMF_SG,
	AMF_SU,
	AMF_COMP,
	AMF_COMP_ENV_VAR,
	AMF_COMP_CS_TYPE,
	AMF_SI,
	AMF_SI_RANKED_SU,
	AMF_SI_DEPENDENCY,
	AMF_CSI,
	AMF_CSI_ATTRIBUTE,
	AMF_HEALTHCHECK,
	AMF_CSI_DEPENDENCIES,
	AMF_CS_TYPE,
} amf_parse_t;

#ifndef OPENAIS_CLUSTER_STARTUP_TIMEOUT
#define OPENAIS_CLUSTER_STARTUP_TIMEOUT 5000
#endif

static const char *presence_state_text[] = {
	"UNKNOWN",
	"UNINSTANTIATED",
	"INSTANTIATING",
	"INSTANTIATED",
	"TERMINATING",
	"RESTARTING",
	"INSTANTION_FAILED",
	"TERMINIATION-FAILED"
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

int amf_config_read (struct amf_cluster *cluster, char **error_string)
{
	char buf[1024];
	char *line;
	FILE *fp;
	char *filename;
	amf_parse_t current_parse = AMF_HEAD;
	int line_number = 0;
	char *loc;
	int i;
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
	int                       comp_env_var_cnt = 0;
	int                       comp_cs_type_cnt = 0;
	int                       csi_attr_cnt = 0;
	int                       csi_dependencies_cnt = 0;
	char                     *error_reason = NULL;
	char                     *value;

	filename = getenv("OPENAIS_AMF_CONFIG_FILE");
	if (!filename) {
		filename = "/etc/ais/amf.conf";
	}

	fp = fopen (filename, "r");
	if (fp == 0) {
		sprintf (buf, "Can't read %s file reason = (%s).\n",
				 filename, strerror (errno));
		*error_string = buf;
		return (-1);
	}

	cluster->saAmfClusterStartupTimeout = -1;
	cluster->saAmfClusterAdminState = SA_AMF_ADMIN_UNLOCKED;

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
		case AMF_HEAD:
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
				node = calloc (1, sizeof (struct amf_node));
				node->next = cluster->node_head;
				cluster->node_head = node;
				node->saAmfNodeAdminState = SA_AMF_ADMIN_UNLOCKED;
				node->saAmfNodeAutoRepair = SA_TRUE;
				node->cluster = cluster;
				node->saAmfNodeSuFailOverProb = -1;
				node->saAmfNodeSuFailoverMax = ~0;
				setSaNameT (&node->name, trim_str (loc));
				current_parse = AMF_NODE;
			} else if ((loc = strstr_rs (line, "safApp=")) != 0) {
				app = calloc (1, sizeof (struct amf_application));
				app->next = cluster->application_head;
				cluster->application_head = app;
				app->cluster = cluster;
				app->saAmfApplicationAdminState = SA_AMF_ADMIN_UNLOCKED;
				setSaNameT (&app->name, trim_str (loc));
				current_parse = AMF_APPLICATION;
			} else if (strstr_rs (line, "}")) {
				if (cluster->saAmfClusterStartupTimeout == -1) {
					error_reason = "saAmfClusterStartupTimeout missing";
					goto parse_error;
				}
				/* spec: set to default value if zero */
				if (cluster->saAmfClusterStartupTimeout == 0) {
					cluster->saAmfClusterStartupTimeout = OPENAIS_CLUSTER_STARTUP_TIMEOUT;
				}
				current_parse = AMF_HEAD;
			} else {
				goto parse_error;
			}
			break;

		case AMF_NODE:
			if ((loc = strstr_rs (line, "saAmfNodeSuFailOverProb")) != 0) {
				node->saAmfNodeSuFailOverProb = atol(loc);
			} else if ((loc = strstr_rs (line, "saAmfNodeSuFailoverMax")) != 0) {
				node->saAmfNodeSuFailoverMax = atol(loc);
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
				current_parse = AMF_CLUSTER;
			} else {
				goto parse_error;
			}
			break;

		case AMF_APPLICATION:
			if ((loc = strstr_rs (line, "clccli_path=")) != 0) {
				strcpy (app->clccli_path, loc);
			} else if ((loc = strstr_rs (line, "safSg=")) != 0) {
				sg = calloc (1, sizeof (struct amf_sg));
				sg->next = app->sg_head;
				app->sg_head = sg;
				sg->saAmfSGAdminState = SA_AMF_ADMIN_UNLOCKED;
				sg->saAmfSGNumPrefActiveSUs = 1;
				sg->saAmfSGNumPrefStandbySUs = 1;
				sg->saAmfSGCompRestartProb = -1;
				sg->saAmfSGCompRestartMax = ~0;
				sg->saAmfSGSuRestartProb = -1;
				sg->saAmfSGSuRestartMax = ~0;
				sg->saAmfSGAutoAdjustProb = -1;
				sg->saAmfSGAutoRepair = SA_TRUE;
				sg->application = app;
				current_parse = AMF_SG;
				setSaNameT (&sg->name, trim_str (loc));
			} else if ((loc = strstr_rs (line, "safSi=")) != 0) {
				si = calloc (1, sizeof (struct amf_si));
				si->next = app->si_head;
				app->si_head = si;
				si->application = app;
				si->saAmfSIPrefActiveAssignments = 1;
				si->saAmfSIPrefStandbyAssignments = 1;
				setSaNameT (&si->name, trim_str (loc));
				si->saAmfSIAdminState = SA_AMF_ADMIN_UNLOCKED;
				si->saAmfSIAssignmentState = SA_AMF_ASSIGNMENT_UNASSIGNED;
				current_parse = AMF_SI;
			} else if ((loc = strstr_rs (line, "safCSType=")) != 0) {
				current_parse = AMF_CS_TYPE;
			} else if (strstr_rs (line, "}")) {
				current_parse = AMF_CLUSTER;
			} else {
				goto parse_error;
			}
			break;

		case AMF_SG:
			if ((loc = strstr_rs (line, "clccli_path=")) != 0) {
				strcpy (sg->clccli_path, loc);
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
				su = calloc (1, sizeof (struct amf_su));
				su->next = sg->su_head;
				sg->su_head = su;
				su->sg = sg;
				su->saAmfSUAdminState = SA_AMF_ADMIN_UNLOCKED;
				su->saAmfSUOperState = SA_AMF_OPERATIONAL_DISABLED;
				su->saAmfSUPresenceState = SA_AMF_PRESENCE_UNINSTANTIATED;
				su->escalation_level = ESCALATION_LEVEL_NO_ESCALATION;
				su->saAmfSUFailover = 1;
				setSaNameT (&su->name, trim_str (loc));
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
				strcpy (su->clccli_path, loc);
			} else if ((loc = strstr_rs (line, "saAmfSUHostedByNode=")) != 0) {
				setSaNameT (&su->saAmfSUHostedByNode, loc);
			} else if ((loc = strstr_rs (line, "safComp=")) != 0) {
				comp = amf_comp_create (su);
				comp_env_var_cnt = 0;
				comp_cs_type_cnt = 0;
				setSaNameT (&comp->name, trim_str (loc));
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
				strcpy (comp->clccli_path, loc);
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
				comp->saAmfCompInstantiateCmdArgv = malloc (strlen(loc) + 1);
				strcpy (comp->saAmfCompInstantiateCmdArgv, loc);
			} else if ((loc = strstr_rs ( line, "saAmfCompInstantiateCmd=")) != 0) {
				comp->saAmfCompInstantiateCmd = malloc (strlen(loc) + 1);
				strcpy (comp->saAmfCompInstantiateCmd, loc);
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
				comp->saAmfCompTerminateCmdArgv = malloc (strlen(loc) + 1);
				strcpy (comp->saAmfCompTerminateCmdArgv, loc);
			} else if ((loc = strstr_rs (line, "saAmfCompTerminateCmd=")) != 0) {
				comp->saAmfCompTerminateCmd = malloc (strlen(loc) + 1);
				strcpy (comp->saAmfCompTerminateCmd, loc);
			} else if ((loc = strstr_rs(line, "saAmfCompTerminateTimeout=")) != 0) {
				comp->saAmfCompTerminateTimeout = atol (loc);
			} else if ((loc = strstr_rs (line, "saAmfCompCleanupCmdArgv=")) != 0) {
				comp->saAmfCompCleanupCmdArgv = malloc (strlen(loc) + 1);
				strcpy (comp->saAmfCompCleanupCmdArgv, loc);
			} else if ((loc = strstr_rs (line, "saAmfCompCleanupCmd=")) != 0) {
				comp->saAmfCompCleanupCmd = malloc (strlen(loc) + 1);
				strcpy (comp->saAmfCompCleanupCmd, loc);
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
			} else if ((loc = strstr_rs (line, "saAmfCompDisableRestart")) != 0) {
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
				comp->saAmfCompCsTypes[comp_cs_type_cnt - 1] = malloc (sizeof(SaNameT));
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
				env_var = comp->saAmfCompCmdEnv[comp_env_var_cnt - 1] = malloc (strlen (line) + 1);
				strcpy (env_var, line);
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
			} else if ((loc = strstr_rs (line, "saAmfSIProtectedbySG{")) != 0) {
				setSaNameT (&si->saAmfSIProtectedbySG, loc);
			} else if ((loc = strstr_rs (line, "saAmfSIRank{")) != 0) {
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
				attribute->name = malloc (strlen (loc) + 1);
				strcpy (attribute->name, trim_str (loc));
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
				csi->saAmfCSIDependencies[csi_dependencies_cnt - 1] = malloc (sizeof(SaNameT));
				setSaNameT (csi->saAmfCSIDependencies[csi_dependencies_cnt - 1], loc);
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
				attribute->value[csi_attr_cnt - 1] = malloc (strlen (value) + 1);
				strcpy (attribute->value[csi_attr_cnt - 1], value);
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
	return (0);

parse_error:
	sprintf (buf, "parse error at %s: %d: %s\n",
			 filename, line_number, error_reason);
	*error_string = buf;
	fclose (fp);
	return (-1);
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
	struct amf_si_assignment *si_assignment;
	struct amf_csi_assignment *csi_assignment;

	dprintf("AMF runtime attributes:");
	dprintf("===================================================");
	dprintf("safCluster=%s", getSaNameT(&cluster->name));
	dprintf("  admin state: %s\n", admin_state_text[cluster->saAmfClusterAdminState]);
	for (node = cluster->node_head; node != NULL; node = node->next) {
		dprintf("  safNode=%s\n", getSaNameT (&node->name));
		dprintf("    admin state: %s\n", admin_state_text[node->saAmfNodeAdminState]);
		dprintf("    oper state:  %s\n", oper_state_text[node->saAmfNodeOperState]);
	}
	for (app = cluster->application_head; app != NULL; app = app->next) {
		dprintf("  safApp=%s\n", getSaNameT(&app->name));
		dprintf("    admin state: %s\n", admin_state_text[app->saAmfApplicationAdminState]);
		dprintf("    num_sg:      %d\n", app->saAmfApplicationCurrNumSG);
		for (sg = app->sg_head;	sg != NULL; sg = sg->next) {
			dprintf("    safSG=%s\n", getSaNameT(&sg->name));
			dprintf("      admin state:        %s\n", admin_state_text[sg->saAmfSGAdminState]);
			dprintf("      assigned SUs        %d\n", sg->saAmfSGNumCurrAssignedSUs);
			dprintf("      non inst. spare SUs %d\n", sg->saAmfSGNumCurrNonInstantiatedSpareSUs);
			dprintf("      inst. spare SUs     %d\n", sg->saAmfSGNumCurrInstantiatedSpareSUs);
			for (su = sg->su_head; su != NULL; su = su->next) {
				dprintf("      safSU=%s\n", getSaNameT(&su->name));
				dprintf("        oper state:      %s\n", oper_state_text[su->saAmfSUOperState]);
				dprintf("        admin state:     %s\n", admin_state_text[su->saAmfSUAdminState]);
				dprintf("        readiness state: %s\n", readiness_state_text[su->saAmfSUReadinessState]);
				dprintf("        presence state:  %s\n", presence_state_text[su->saAmfSUPresenceState]);
				dprintf("        hosted by node   %s\n", su->saAmfSUHostedByNode.value);
				dprintf("        num active SIs   %d\n", su->saAmfSUNumCurrActiveSIs);
				dprintf("        num standby SIs  %d\n", su->saAmfSUNumCurrStandbySIs);
				dprintf("        restart count    %d\n", su->saAmfSURestartCount);
				dprintf("        escalation level %d\n", su->escalation_level);
				dprintf("        SU failover cnt  %d\n", su->su_failover_cnt);
				dprintf("        assigned SIs:");
				for (si_assignment = su->assigned_sis; si_assignment != NULL;
					si_assignment = si_assignment->next) {
					dprintf("          safSi=%s\n", si_assignment->si->name.value);
					dprintf("            HA state: %s\n",
						ha_state_text[si_assignment->saAmfSISUHAState]);
				}
				for (comp = su->comp_head; comp != NULL; comp = comp->next) {
					dprintf("        safComp=%s\n", getSaNameT(&comp->name));
					dprintf("          oper state:      %s\n",
						oper_state_text[comp->saAmfCompOperState]);
					dprintf("          readiness state: %s\n",
						readiness_state_text[comp->saAmfCompReadinessState]);
					dprintf("          presence state:  %s\n",
						presence_state_text[comp->saAmfCompPresenceState]);
					dprintf("          num active CSIs  %d\n",
						comp->saAmfCompNumCurrActiveCsi);
					dprintf("          num standby CSIs %d\n",
						comp->saAmfCompNumCurrStandbyCsi);
					dprintf("          restart count    %d\n", comp->saAmfCompRestartCount);
					dprintf("          assigned CSIs:");
					for (csi_assignment = comp->assigned_csis; csi_assignment != NULL;
						csi_assignment = csi_assignment->comp_next) {
						dprintf("            safCSI=%s\n", csi_assignment->csi->name.value);
						dprintf("              HA state: %s\n",
							ha_state_text[csi_assignment->saAmfCSICompHAState]);
					}
				}
			}
		}
		for (si = app->si_head; si != NULL; si = si->next) {
			dprintf("    safSi=%s\n", getSaNameT(&si->name));
			dprintf("      admin state:         %s\n", admin_state_text[si->saAmfSIAdminState]);
			dprintf("      assignm. state:      %s\n", assignment_state_text[si->saAmfSIAssignmentState]);
			dprintf("      active assignments:  %d\n", si->saAmfSINumCurrActiveAssignments);
			dprintf("      standby assignments: %d\n", si->saAmfSINumCurrStandbyAssignments);
			for (csi = si->csi_head; csi != NULL; csi = csi->next) {
				dprintf("      safCsi=%s\n", getSaNameT(&csi->name));
			}
		}
	}
	dprintf("===================================================");
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
		if ( !objdb->object_key_get (object_service_handle,
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

