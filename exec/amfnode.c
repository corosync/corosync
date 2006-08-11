/** @file amfnode.c
 * 
 * Copyright (c) 2006 Ericsson AB.
 * Author: Hans Feldt, Anders Eriksson, Lars Holm
 * - Constructors/destructors
 * - Serializers/deserializers
 *
 * All rights reserved.
 *
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
 * AMF Node Class Implementation
 * 
 * This file contains functions for handling AMF nodes. It can be 
 * viewed as the implementation of the AMF Node class (called NODE)
 * as described in SAI-Overview-B.02.01. The SA Forum specification 
 * SAI-AIS-AMF-B.02.01 has been used as specification of the behaviour
 * and is referred to as 'the spec' below.
 * 
 * The functions in this file are responsible for:
 *	- controlling the instantiation of the SUs hosted on current node and
 *	  controlling the assigning of workload to them when a node joins the
 *    cluster (cluster start is controlled by the Cluster Class)
 *	- controlling node level recovery and repair functions
 *	- implementing error escallation level 2 and 3 (paragraph 3.12.2.2 and
 *    3.12.2.3 in the spec)
 *	- handling run time attributes of the AMF NODE; cached
 *	  attributes are stored as variables and sent to the IMM service (future)
 *	  upon the changes described in the specification
 *
 * The node class contains the following state machines:
 *	- administrative state machine (ADSM)
 *	- operational state machine (OPSM)
 *	- availability control state machine (ACSM)
 *
 * The administrative state machine will be implemented in the future.
 *
 * The operational state machine is primarily used to report status of the
 * node.
 *
 * The availability control state machine is used for control purposes.
 * ACSM contains three states of which two are composite.
 * Being a composite state means that the state contains substates.
 * ACSM states are:
 *	- REPAIR_NEEDED
 *	- ESCALLATION_LEVEL (LEVEL_0, LEVEL_2 and LEVEL_3)
 *	- MANAGING_HOSTED_SERVICE_UNITS (
 *		. FAILING_FAST (REBOOTING_NODE and ACTIVATING_STANDBY_NODE)
 *		. FAILING_GRACEFULLY (SWITCHING_OVER, FAILING_OVER and REBOOTING_NODE)
 *		. LEAVING_SPONTANEOUSLY (DEACTIVATE_DEPENDENT and 
 *								 WAITING_FOR_NODE_TO_JOIN)
 *		. JOINING (STARTING_SERVICE_UNITS, ASSIGNING_ACTIVE_WORKLOAD and
 *				   ASSIGNING_STANDBY_WORKLOAD)
 * 
 * REPAIR_NEEDED indicates the node needs a manual repair and this state will
 * maintained until the administrative command REPAIRED is entered
 * (implemented in the future)
 *
 * ESCALLATION_LEVEL is a kind of idle state where no actions are performed
 * and used only to remember the escallation level. Substate LEVEL_0 indicates
 * no escallation. LEVEL_2 indicates that so many component restarts have been 
 * executed recently that a new component restart request will escalate 
 * to service unit restart action. Node will request a service unit restart
 * from SU.
 * LEVEL_3 will be entered if either there are too many service unit restarts
 * been made or a component failover recovery action is requested. On level 3
 * the recovery action performed is service unit failover (paragraph 3.12.1.3).
 * 
 * FAILING_FAST state executes a node re-boot and waits for the node to join
 * the cluster again.
 *
 * FAILING_GRACEFULLY state requests all SGs which have SUs hosted on current
 * node to switch or failover according to the procedures described in
 * paragraphs 3.12.1.3 before re-boot is executed. Then the confirmation is
 * awaited from all concerned SGs and finally a node re-boot is executed as
 * the repair action (see paragraph 2.12.1.4).
 *
 * LEAVING_SPONTANEOUSLY state handles the spontaneous leave of a node.
 *
 * JOINING state handles the start of a node in all cases except cluster start,
 * which is handled by the CLUSTER class. 
 * 
 */

#include <stdlib.h>
#include <assert.h>

#include "amf.h"
#include "util.h"
#include "print.h"

/**
 * Node constructor
 * @param loc
 * @param cluster
 * @param node
 */
struct amf_node *amf_node_new (struct amf_cluster *cluster, char *name)
{
	struct amf_node *node = calloc (1, sizeof (struct amf_node));

	if (node == NULL) {
		openais_exit_error(AIS_DONE_OUT_OF_MEMORY);
	}
	node->next = cluster->node_head;
	node->saAmfNodeAdminState = SA_AMF_ADMIN_UNLOCKED;
	node->saAmfNodeOperState = SA_AMF_OPERATIONAL_ENABLED;
	node->saAmfNodeAutoRepair = SA_TRUE;
	node->cluster = cluster;
	node->saAmfNodeSuFailOverProb = -1;
	node->saAmfNodeSuFailoverMax = ~0;
	setSaNameT (&node->name, name);

	return node;
}

void *amf_node_serialize (struct amf_node *node, int *len)
{
	int objsz = sizeof (struct amf_node);
	struct amf_node *copy;

	copy = amf_malloc (objsz);
	memcpy (copy, node, objsz);
	*len = objsz;
	TRACE8 ("%s", copy->name.value);

	return copy;
}

struct amf_node *amf_node_deserialize (
	struct amf_cluster *cluster, char *buf, int size)
{
	int objsz = sizeof (struct amf_node);

	if (objsz > size) {
		return NULL;
	} else {
		struct amf_node *obj = amf_node_new (cluster, "");
		if (obj == NULL) {
			return NULL;
		}
		memcpy (obj, buf, objsz);
		TRACE8 ("%s", obj->name.value);
		obj->cluster = cluster;
		obj->next = cluster->node_head;
		cluster->node_head = obj;
		return obj;
	}
}

void amf_node_sync_ready (struct amf_node *node)
{
	struct amf_application *app;

	assert (node != NULL);

	log_printf(LOG_NOTICE, "Node %s sync ready, starting hosted SUs.",
			   node->name.value);

	for (app = amf_cluster->application_head; app != NULL; app = app->next) {
		amf_application_start (app, node);
	}
}

void amf_node_init (void)
{
	log_init ("AMF");
}

struct amf_node *amf_node_find (SaNameT *name)
{
	struct amf_node *node;

	ENTER ("");

	for (node = amf_cluster->node_head; node != NULL; node = node->next) {
		if (name_match (&node->name, name)) {
			return node;
		}
	}

	return NULL;
}

