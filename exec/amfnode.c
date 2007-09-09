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
 *	- IDLE (ESCALATION_LEVEL_0, ESCALATION_LEVEL_2 and ESCALATION_LEVEL_3)
 *	- MANAGING_HOSTED_SERVICE_UNITS (
 *		. FAILING_FAST (REBOOTING_NODE and ACTIVATING_STANDBY_NODE)
 *		. FAILING_GRACEFULLY (SWITCHING_OVER, FAILING_OVER and REBOOTING_NODE)
 *      . LEAVING_SPONTANEOUSLY (SWITCHING_OVER, FAILING_OVER and
 *								 WAITING_FOR_NODE_TO_JOIN)
 *      . JOINING (STARTING_APPLICATIONS and ASSIGNING_WORKLOAD)
 * 
 * REPAIR_NEEDED indicates the node needs a manual repair and this state will be
 * maintained until the administrative command REPAIRED is entered (implemented
 * in the future)
 *
 * IDLE is a composite state where no actions are actually performed
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
 * the cluster again. (not implemented)
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
 * 1. Node Availability Control State Machine
 * ==========================================
 * 
 * 1.1  State Transition Table
 * 
 * State:                  Event:              Action:  New state:
 * ============================================================================
 * ESCALATION_LEVEL_X      node_sync_ready     A6       JOINING_STARTING_APPLS
 * ESCALATION_LEVEL_X      node_leave          A9,A8    LEAVING_SP_FAILING_OVER
 * ESCALATION_LEVEL_X	   failover            A11		GRACEFULLY_FAILING_OVER
 * ESCALATION_LEVEL_2      comp_restart_req [!C6]A13	ESCALATION_LEVEL_2
 * ESCALATION_LEVEL_2      comp_restart_req [C6]A14		ESCALATION_LEVEL_3
 * ESCALATION_LEVEL_3	   comp_restart_req [!C7]A14    ESCALATION_LEVEL_3
 * ESCALATION_LEVEL_3      comp_failover_req [!C7]A14   ESCALATION_LEVEL_3
 * ESCALATION_LEVEL_3	   comp_restart_req [C7]A15     ESCALATION_LEVEL_3
 * ESCALATION_LEVEL_3      comp_failover_req [C7]A15    ESCALATION_LEVEL_3
 * JOINING_STARTING_APPLS  appl_started [C4]   A7       JOINING_ASSIGNING_WL
 * JOINING_ASSIGNING_WL    appl_assigned [C5]           ESCALATION_LEVEL_X
 * LEAVING_SP_FAILING_OVER sg_failed_over [C1]          LEAVING_SP_WAIT_FOR_JOIN
 * LEAVING_SP_WAIT_FOR_JOIN node_sync_ready    A6       JOINING_STARTING_APPLS
 * GRACEFULLY_FAILING_OVER sg_failed_over [C1] A12      GRACEFULLY_REBOOTING
 * GRACEFULLY_REBOOTING    node_leave					ESCALATION_LEVEL_X
 * 
 *  1.2 State Description
 *  =====================
 * ESCALATION_LEVEL_X -  Node is synchronized and idle (X = 0,2 or 3).
 * JOINING_STARTING_APPLS - JOINING_STARTING_APPLICATIONS
 *                          Node has ordered all applications to start its SUs
 *                          hosted on current node and is now waiting for them
 *                          to acknowledge that they have started.
 * GRACEFULLY_FAILING_OVER - FAILING_GRACEFULLY_FAILING_OVER
 *							 Node has ordered all SGs in the cluster to
 *							 failover all SUs that are hosted on a specific
 *							 node and waits for the SGs to confirm the
 *							 failover is completed.
 * GRACEFULLY_REBOOTING - FAILING_GRACEFULLY_REBOOTING_NODE
 *						  Node has ordered reboot and waits for the rebooted
 *						  node to join the cluster again.
 * JOINING_ASSIGNING_WL - JOINING_ASSIGNING_WORKLOAD
 *                        Node has ordered all applications to assign workload
 *                        to all its SUs which currently have no workload and
 *                        is now waiting for the applications to acknowledge.
 *
 * LEAVING_SP_FAILING_OVER - LEAVING_SPONTANEOUSLY_FAILING_OVER
 *                           Node has received an event telling that this node
 *                           has left the cluster and has ordered all service
 *                           groups to failover those of its SUs that were
 *							 hosted on current node.
 * 
 * LEAVING_SP_WAIT_FOR_JOIN - LEAVING_SPONTANEOUSLY_WAITING_FOR_NODE_TO_JOIN
 *                            Node is waiting for current node to join again.
 *
 * 1.3 Actions
 * ===========
 * A1 - 
 * A2 -
 * A3 -
 * A4 -
 * A5 -
 * A6 - [foreach application in cluster]start application
 * A7 - [foreach application in cluster]assign workload to application
 * A8 - [foreach application in cluster]
 *      [foreach SG in application ]failover node
 * A9 - [foreach application in cluster]
 *      [foreach SG in application ]
 *      [foreach SU in SG where the SU is hosted on current node]
 *      [foreach comp in such an SU]indicate that the node has left the cluster
 * A10- 
 * A11- [foreach SG in cluster]failover node
 * A12- reboot node
 * A13- restart SU
 * A14- failover SU
 * A15- failover node
 * 
 * 1.4 Guards
 * ==========
 * C1 - All SG availability control state machines (ACSM) == IDLE
 * C2 -
 * C3 -
 * C4 - No applications are in ACSM state == STARTING_SGS
 * C5 - All applications have ACSM state == WORKLOAD_ASSIGNED
 * C6 - Specified number of SU restarts have been done.
 * C7 - Specified number of SU failover actions have been done.
 */ 

#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include "amf.h"
#include "util.h"
#include "logsys.h"
#include "main.h"

LOGSYS_DECLARE_SUBSYS ("AMF", LOG_INFO)

/******************************************************************************
 * Internal (static) utility functions
 *****************************************************************************/

static void node_acsm_enter_leaving_spontaneously(struct amf_node *node)
{
	ENTER("'%s'", node->name.value);
	node->saAmfNodeOperState = SA_AMF_OPERATIONAL_DISABLED;
	node->nodeid = 0;
}

static void node_acsm_enter_failing_over (struct amf_node *node)
{ 
	struct amf_application *app;
	struct amf_sg *sg;
	struct amf_su *su;
	struct amf_comp *component = NULL;

	ENTER("'%s'", node->name.value);
	node->acsm_state = NODE_ACSM_LEAVING_SPONTANEOUSLY_FAILING_OVER;
	/*
	 * Indicate to each component object in the model that current
	 * node has left the cluster
	 */
	for (app = amf_cluster->application_head; app != NULL; app = app->next) {
		for (sg = app->sg_head; sg != NULL; sg = sg->next) {
			for (su = sg->su_head; su != NULL; su = su->next) {
				if (name_match(&node->name, &su->saAmfSUHostedByNode)) {
					for (component = su->comp_head; component != NULL;
						component = component->next) {
						amf_comp_node_left(component);
					}
				}
			}
		}
	}

	/*
	 * Let all service groups with service units hosted on current node failover
	 * its workload
	 */
	for (app = amf_cluster->application_head; app != NULL; app =
		app->next) {
		for (sg = app->sg_head; sg != NULL; sg =
			sg->next) {
			amf_sg_failover_node_req(sg, node);
		}
	}
}

static void failover_all_sg_on_node (amf_node_t *node)
{
	amf_application_t *app;
	amf_sg_t *sg;
	amf_su_t *su;
	for (app = amf_cluster->application_head; app != NULL; app = app->next) {
		for (sg = app->sg_head; sg != NULL; sg = sg->next) {
			for (su = sg->su_head; su != NULL; su = su->next) {
				if (name_match(&su->saAmfSUHostedByNode, &node->name)) {
					amf_sg_failover_node_req (sg, node);
					break;
				}
			}

		}
	}
}

static void node_acsm_enter_failing_gracefully_failing_over (amf_node_t *node)
{
	ENTER("");
	node->acsm_state = NODE_ACSM_FAILING_GRACEFULLY_FAILING_OVER;
	failover_all_sg_on_node (node);
}

static int has_all_sg_on_node_failed_over (amf_node_t *node) 
{
	amf_application_t *app;
	amf_sg_t *sg;
	amf_su_t *su;
	int has_all_sg_on_node_failed_over = 1;

	for (app = amf_cluster->application_head; app != NULL; app = app->next) {
		for (sg = app->sg_head; sg != NULL; sg = sg->next) {
			for (su = sg->su_head; su != NULL; su = su->next) {
				if (name_match(&su->saAmfSUHostedByNode, &node->name)) {

					if (sg->avail_state != SG_AC_Idle) {
						TRACE1("%s %s",sg->name.value, su->name.value);
						has_all_sg_on_node_failed_over = 0;
						goto out;
					}
					break;
				}
			}

		}
	}
out:
	return has_all_sg_on_node_failed_over;
}

static void repair_node (amf_node_t *node)
{
	ENTER("");
	char hostname[256];
	gethostname (hostname, 256);
	if (!strcmp (hostname, (const char*)node->saAmfNodeClmNode.value)) {
        /* TODO if(saAmfAutoRepair == SA_TRUE) */
#ifdef DEBUG
			exit (0);
#else
			system ("reboot");
#endif	
	}
}

static void enter_failing_gracefully_rebooting_node (amf_node_t *node)
{
	ENTER("");
	node->acsm_state = NODE_ACSM_FAILING_GRACEFULLY_REBOOTING_NODE;
	repair_node (node);
}

static void node_acsm_enter_idle (amf_node_t *node)
{
	ENTER ("history_state=%d",node->history_state);
	node->acsm_state =  node->history_state;
}

static void node_acsm_enter_joining_assigning_workload (struct amf_node *node, 
	struct amf_application *app)
{
	log_printf(LOG_NOTICE,
		"Node=%s: all applications started, assigning workload.",
		node->name.value);

	ENTER("");
	node->acsm_state = NODE_ACSM_JOINING_ASSIGNING_WORKLOAD;
	for (app = app->cluster->application_head; app != NULL; 
		app = app->next) {
		amf_application_assign_workload (app, node);
	}
}

/******************************************************************************
 * Event methods
 *****************************************************************************/

/**
 * This event indicates that a node has unexpectedly left the cluster. Node
 * leave event is obtained from amf_confchg_fn.
 * 
 * @param node
 */
void amf_node_leave (struct amf_node *node)
{
	assert (node != NULL);
	ENTER("'%s', CLM node '%s'", node->name.value,
		node->saAmfNodeClmNode.value);


	switch (node->acsm_state) {
		case NODE_ACSM_IDLE_ESCALLATION_LEVEL_0:
		case NODE_ACSM_IDLE_ESCALLATION_LEVEL_2:
		case NODE_ACSM_IDLE_ESCALLATION_LEVEL_3:
			node_acsm_enter_leaving_spontaneously(node);    
			node_acsm_enter_failing_over (node);
			break;
		case NODE_ACSM_REPAIR_NEEDED:
			break;
		case NODE_ACSM_FAILING_GRACEFULLY_REBOOTING_NODE:
			node->saAmfNodeOperState = SA_AMF_OPERATIONAL_ENABLED; 
			node_acsm_enter_idle (node);
			break;
		default:
			log_printf (LOG_LEVEL_ERROR, "amf_node_leave called in state = %d"
				" (should have been deferred)", node->acsm_state);
			openais_exit_error (AIS_DONE_FATAL_ERR);
			break;

	}
}

/**
 * This function handles a detected error that by a pre-analysis executed
 * elsewhere has been decided to be recovered by a node fail over.
 * @param node
 */
void amf_node_failover (struct amf_node *node)
{
	assert (node != NULL);
	ENTER("'%s', CLM node '%s'", node->name.value,
		node->saAmfNodeClmNode.value);

	switch (node->acsm_state) {
		case NODE_ACSM_IDLE_ESCALLATION_LEVEL_0:
		case NODE_ACSM_IDLE_ESCALLATION_LEVEL_2:
		case NODE_ACSM_IDLE_ESCALLATION_LEVEL_3:
			node_acsm_enter_failing_gracefully_failing_over (node);
			break;
		case NODE_ACSM_REPAIR_NEEDED:
			break;
		default:
			log_printf (LOG_LEVEL_ERROR, "amf_node_leave()called in state = %d"
				" (should have been deferred)", node->acsm_state);
			openais_exit_error (AIS_DONE_FATAL_ERR);
			break;
	}
}

/**
 * 
 * @param node
 */
void amf_node_switchover (struct amf_node *node)
{

}

/**
 * 
 * @param node
 */
void amf_node_failfast (struct amf_node *node)
{

}

/**
 * This event is a request to restart a component which has been escalated,
 * because the component has already been restarted the number of times
 * specified by the configuration. 
 * This function evaluates which recovery measure shall now be
 * taken and initiates the action which result from the evaluation.
 * @param node
 * @param comp
 */
void amf_node_comp_restart_req (struct amf_node *node, struct amf_comp *comp)
{
	amf_su_t *su = comp->su;
	ENTER("");
	switch (node->acsm_state) {
		case NODE_ACSM_IDLE_ESCALLATION_LEVEL_0:
			node->acsm_state = NODE_ACSM_IDLE_ESCALLATION_LEVEL_2;
			amf_node_comp_restart_req (node, comp);
			break;
		case NODE_ACSM_IDLE_ESCALLATION_LEVEL_2:
			if (su->saAmfSURestartCount >= su->sg->saAmfSGSuRestartMax) {
				SaNameT dn;
				node->acsm_state = NODE_ACSM_IDLE_ESCALLATION_LEVEL_3;
				amf_comp_operational_state_set (comp, SA_AMF_OPERATIONAL_DISABLED);
				amf_su_operational_state_set (su, SA_AMF_OPERATIONAL_DISABLED);
				amf_comp_dn_make (comp, &dn);

				log_printf (LOG_NOTICE, "Error detected for '%s', recovery "
					"action:\n\t\tSU failover", dn.value);

				amf_sg_failover_su_req (su->sg, su, node);
			} else {
				amf_su_restart (su);
			}
			break;
		case NODE_ACSM_IDLE_ESCALLATION_LEVEL_3:
			if (su->su_failover_cnt <  node->saAmfNodeSuFailoverMax) {
				SaNameT dn;
				amf_comp_operational_state_set (comp, SA_AMF_OPERATIONAL_DISABLED);
				amf_su_operational_state_set (su, SA_AMF_OPERATIONAL_DISABLED);
				amf_comp_dn_make (comp, &dn);

				log_printf (LOG_NOTICE, "Error detected for '%s', recovery "
					"action:\n\t\tSU failover", dn.value);

				amf_sg_failover_su_req (su->sg, su, node);
				return;
			} else {
				node->history_state = NODE_ACSM_IDLE_ESCALLATION_LEVEL_0;
				amf_node_failover (node);
			}
			break;
		default:
			dprintf("%d",node->acsm_state);
			assert (0);
			break;
	}                       	
}

/**
 * This event is a request to failover the specified component. 
 * This function evaluates which recovery measure shall actually be
 * taken considering the escalation policy and initiates the action
 * which result from the evaluation.
 * @param node
 * @param comp
 */
void amf_node_comp_failover_req (amf_node_t *node, amf_comp_t *comp)
{
	ENTER("");
	switch (node->acsm_state) {
		case NODE_ACSM_IDLE_ESCALLATION_LEVEL_0:
		case NODE_ACSM_IDLE_ESCALLATION_LEVEL_2:
			if (comp->su->saAmfSUFailover) {
				/* SU failover */
				amf_sg_failover_su_req (comp->su->sg,comp->su, node);
			} 
			break;
		case NODE_ACSM_IDLE_ESCALLATION_LEVEL_3:
			if (comp->su->su_failover_cnt < node->saAmfNodeSuFailoverMax) {
				if (comp->su->saAmfSUFailover) {
					/* SU failover */
					amf_sg_failover_su_req (comp->su->sg,comp->su, node);
					
				} 
			} else {
				node->history_state = NODE_ACSM_IDLE_ESCALLATION_LEVEL_0;
					amf_node_failover (node);
			}
			break;
		default:
			dprintf("%d",node->acsm_state);
			assert (0);
			break;
	}
}

/**
 * This event indicates that current node has joined and its cluster model has
 * been synchronized with the other nodes cluster models.
 * 
 * @param node
 */
void amf_node_sync_ready (struct amf_node *node)
{
	struct amf_application *app;

	assert (node != NULL);

	log_printf(LOG_NOTICE, "Node=%s: sync ready, starting hosted SUs.",
		node->name.value);
	node->saAmfNodeOperState = SA_AMF_OPERATIONAL_ENABLED;

	switch (node->acsm_state) {
		case NODE_ACSM_IDLE_ESCALLATION_LEVEL_0:
		case NODE_ACSM_IDLE_ESCALLATION_LEVEL_2:
		case NODE_ACSM_IDLE_ESCALLATION_LEVEL_3:
		case NODE_ACSM_LEAVING_SPONTANEOUSLY_WAITING_FOR_NODE_TO_JOIN:
			node->acsm_state = NODE_ACSM_JOINING_STARTING_APPLICATIONS;
			for (app = amf_cluster->application_head; app != NULL; app = app->next) {
				amf_application_start (app, node);
			}
			break;
		case NODE_ACSM_REPAIR_NEEDED:
			break;
		default:
			log_printf (LOG_LEVEL_ERROR, "amf_node_sync_ready() was called in "
										 "state = %d (should have been deferred)",
				node->acsm_state);
			openais_exit_error (AIS_DONE_FATAL_ERR);
			break;

	}
}

/******************************************************************************
 * Event response methods
 *****************************************************************************/

/**
 * This event indicates that an application has started. Started in this context
 * means that none of its contained service units is in an -ING state with other
 * words successfully instantiated, instantiation has failed or instantiation
 * was not possible (due to the node on which the SU was to be hosted is not
 * operational).
 * 
 * @param node
 * @param application which has been started
 */
void amf_node_application_started (struct amf_node *node, 
	struct amf_application *app)
{
	assert (node != NULL && app != NULL );
	ENTER ("Node=%s: application '%s' started", node->name.value,
		app->name.value);

	switch (node->acsm_state) {
		case NODE_ACSM_JOINING_STARTING_APPLICATIONS:
			if (amf_cluster_applications_started_with_no_starting_sgs(
				app->cluster)) {

				node_acsm_enter_joining_assigning_workload(node, app);
			}
			break;
		default:
			log_printf (LOG_LEVEL_ERROR, "amf_node_application_started()"
				"called in state = %d (unexpected !!)", node->acsm_state);
			openais_exit_error (AIS_DONE_FATAL_ERR);
			break;

	}
}

/**
 * This event indicates that an application has been assigned workload.
 * 
 * @param node
 * @param app - Application which has been assigned workload
 */
void amf_node_application_workload_assigned (struct amf_node *node, 
	struct amf_application *app)
{
	assert (node != NULL && app != NULL );
	ENTER ("Node=%s: application '%s' started", node->name.value,
		app->name.value);

	switch (node->acsm_state) {
		case NODE_ACSM_JOINING_ASSIGNING_WORKLOAD:

			if (amf_cluster_applications_assigned (amf_cluster)) {
				log_printf(LOG_NOTICE, "Node=%s: all workload assigned", 
					node->name.value);
				node_acsm_enter_idle (node);
			}
			break;
		default:
			log_printf (LOG_LEVEL_ERROR, "amf_node_application_workload_assigned()"
				"called in state = %d (unexpected !!)", node->acsm_state);
			openais_exit_error (AIS_DONE_FATAL_ERR);
			break;
	}
}

/**
 * This event indicates that an SG has failed over its workload after a node
 * failure.
 * 
 * @param node
 * @param sg_in SG which is now ready with its failover
 */
void amf_node_sg_failed_over (struct amf_node *node, struct amf_sg *sg_in)
{
	assert (node != NULL);
	ENTER ("Node=%s: SG '%s' started %d", node->name.value,
		sg_in->name.value,node->acsm_state);

	switch (node->acsm_state) {
		case NODE_ACSM_LEAVING_SPONTANEOUSLY_FAILING_OVER:
			if (has_all_sg_on_node_failed_over (node)) { /*C2*/
				node->acsm_state = 
					NODE_ACSM_LEAVING_SPONTANEOUSLY_WAITING_FOR_NODE_TO_JOIN;
			}
			break;
		case NODE_ACSM_LEAVING_SPONTANEOUSLY_WAITING_FOR_NODE_TO_JOIN:
			/* Accept reports of failed over sg that has completed. */
			break;
		case NODE_ACSM_FAILING_GRACEFULLY_FAILING_OVER:
			if (has_all_sg_on_node_failed_over (node)) { /*C2*/
				enter_failing_gracefully_rebooting_node (node);
			}
			break;
		default:
			log_printf (LOG_LEVEL_ERROR, "amf_node_sg_failed_over()"
				"called in state = %d (unexpected !!)", node->acsm_state);
			openais_exit_error (AIS_DONE_FATAL_ERR);
			break;
	}
}

/******************************************************************************
 * General methods
 *****************************************************************************/

/**
 * Node constructor
 * @param cluster
 * @param name - RDN of node
 */
struct amf_node *amf_node_new (struct amf_cluster *cluster, char *name) {
	struct amf_node *node = amf_calloc (1, sizeof (struct amf_node));

	setSaNameT (&node->name, name);
	node->saAmfNodeAdminState = SA_AMF_ADMIN_UNLOCKED;
	node->saAmfNodeOperState = SA_AMF_OPERATIONAL_ENABLED;
	node->saAmfNodeAutoRepair = SA_TRUE;
	node->saAmfNodeSuFailOverProb = -1;
	node->saAmfNodeSuFailoverMax = ~0;
	node->cluster = cluster;
	node->next = cluster->node_head;
	cluster->node_head = node;
	node->acsm_state = NODE_ACSM_IDLE_ESCALLATION_LEVEL_0; 
	node->history_state = NODE_ACSM_IDLE_ESCALLATION_LEVEL_0;
	return node;
}

void *amf_node_serialize (struct amf_node *node, int *len)
{
	char *buf = NULL;
	int offset = 0, size = 0;

	TRACE8 ("%s", node->name.value);

	buf = amf_serialize_SaNameT (buf, &size, &offset, &node->name);
	buf = amf_serialize_SaNameT (buf, &size, &offset, &node->saAmfNodeClmNode);
	buf = amf_serialize_SaUint32T (buf, &size, &offset,
		node->saAmfNodeSuFailOverProb);
	buf = amf_serialize_SaUint32T (buf, &size, &offset,
		node->saAmfNodeSuFailoverMax);
	buf = amf_serialize_SaUint32T (buf, &size, &offset,
		node->saAmfNodeAutoRepair);
	buf = amf_serialize_SaUint32T (buf, &size, &offset,
		node->saAmfNodeRebootOnInstantiationFailure);
	buf = amf_serialize_SaUint32T (buf, &size, &offset,
		node->saAmfNodeRebootOnTerminationFailure);
	buf = amf_serialize_SaUint32T (buf, &size, &offset,
		node->saAmfNodeAdminState);
	buf = amf_serialize_SaUint32T (buf, &size, &offset,
		node->saAmfNodeOperState);
	buf = amf_serialize_SaUint32T (buf, &size, &offset,
		node->nodeid);
	buf = amf_serialize_SaUint32T (buf, &size, &offset,
		node->acsm_state);
	buf = amf_serialize_SaUint32T (buf, &size, &offset,
		node->history_state);

	*len = offset;

	return buf;
}

struct amf_node *amf_node_deserialize (struct amf_cluster *cluster, char *buf) {
	char *tmp = buf;
	struct amf_node *node = amf_node_new (cluster, "");

	tmp = amf_deserialize_SaNameT (tmp, &node->name);
	tmp = amf_deserialize_SaNameT (tmp, &node->saAmfNodeClmNode);
	tmp = amf_deserialize_SaUint32T (tmp, &node->saAmfNodeSuFailOverProb);
	tmp = amf_deserialize_SaUint32T (tmp, &node->saAmfNodeSuFailoverMax);
	tmp = amf_deserialize_SaUint32T (tmp, &node->saAmfNodeAutoRepair);
	tmp = amf_deserialize_SaUint32T (tmp, &node->saAmfNodeRebootOnInstantiationFailure);
	tmp = amf_deserialize_SaUint32T (tmp, &node->saAmfNodeRebootOnTerminationFailure);
	tmp = amf_deserialize_SaUint32T (tmp, &node->saAmfNodeAdminState);
	tmp = amf_deserialize_SaUint32T (tmp, &node->saAmfNodeOperState);
	tmp = amf_deserialize_SaUint32T (tmp, &node->nodeid);
	tmp = amf_deserialize_SaUint32T (tmp, &node->acsm_state);
	tmp = amf_deserialize_SaUint32T (tmp, &node->history_state);

	return node;
}

struct amf_node *amf_node_find (SaNameT *name) {
	struct amf_node *node;

	assert (name != NULL && amf_cluster != NULL);

	for (node = amf_cluster->node_head; node != NULL; node = node->next) {
		if (name_match (&node->name, name)) {
			return node;
		}
	}

	dprintf ("node %s not found in configuration!", name->value);

	return NULL;
}

struct amf_node *amf_node_find_by_nodeid (unsigned int nodeid) {
	struct amf_node *node;

	assert (amf_cluster != NULL);

	for (node = amf_cluster->node_head; node != NULL; node = node->next) {
		if (node->nodeid == nodeid) {
			return node;
		}
	}

	dprintf ("node %u not found in configuration!", nodeid);

	return NULL;
}

struct amf_node *amf_node_find_by_hostname (const char *hostname) {
	struct amf_node *node;

	assert (hostname != NULL && amf_cluster != NULL);

	for (node = amf_cluster->node_head; node != NULL; node = node->next) {
		if (strcmp ((char*)node->saAmfNodeClmNode.value, hostname) == 0) {
			return node;
		}
	}

	dprintf ("node %s not found in configuration!", hostname);

	return NULL;
}

