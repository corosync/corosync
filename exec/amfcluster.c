
/** @file amfcluster.c
 * 
 * Copyright (c) 2006 Ericsson AB.
 * Author: Hans Feldt, Anders Eriksson, Lars Holm
 *  - Refactoring of code into several AMF files
 *  - Constructors/destructors
 *  - Serializers/deserializers
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
 * AMF Cluster Class Implementation
 * 
 * This file contains functions for handling the AMF cluster. It can be 
 * viewed as the implementation of the AMF Cluster class
 * as described in SAI-Overview-B.02.01. The SA Forum specification 
 * SAI-AIS-AMF-B.02.01 has been used as specification of the behaviour
 * and is referred to as 'the spec' below.
 * 
 * The functions in this file are responsible for:
 *	- to start the cluster initially
 *	- to handle the administrative operation support for the cluster (FUTURE)
 *
 * The cluster class contains the following state machines:
 *	- administrative state machine (ADSM)
 *	- availability control state machine (ACSM)
 *
 * The administrative state machine will be implemented in the future.
 *
 * ACSM handles initial start of the cluster. In the future it will also handle
 * administrative commands on the cluster as described in paragraph 7.4 of the
 * spec. ACSM includes two stable states (UNINSTANTIATED and STARTED) and a
 * number of states to control the transition between the stable states.
 *
 * The cluster is in state UNINSTANTIATED when the cluster starts. (In the
 * future this state will also be assumed after the LOCK_INSTANTIATION
 * administrative command.)
 *
 * State STARTED is assumed when the cluster has been initially started and
 * will in the future be re-assumed after the administrative command RESTART
 * have been executed.
 * 
 * 1. Cluster Availability Control State Machine
 * =============================================
 * 
 * 1.1  State Transition Table
 * 
 * State:                  Event:                Action:  New state:
 * ===========================================================================
 * UNINSTANTIATED          sync_ready [C1]       A2,A1    STARTING_APPS
 * STARTING_APPS           sync_ready            A2,A1    STARTING_APPS
 * STARTING_APPS           app_started [C3]      A7,A3    ASSIGNING_WORKLOAD
 * STARTING_APPS           local_timer_expired   A8       STARTING_APPS
 * STARTING_APPS           time_out [C2]         A7,A3    ASSIGNING_WORKLOAD
 * STARTING_APPS           time_out              A7       WAITING_OVERTIME
 * WAITING_OVERTIME        sync_ready            A4       WAITING_OVERTIME
 * WAITING_OVERTIME        app_started           A3       ASSIGNING_WORKLOAD
 * ASSIGNING_WORKLOAD      sync_ready            A4       ASSIGNING_WORKLOAD
 * ASSIGNING_WORKLOAD      app_assigned [C4]     A6       STARTED
 * STARTED                 sync_ready            A8       STARTED
 * 
 * 1.2 State Description
 * =====================
 * UNINSTANTIATED -  No SUs within any SG in any Application is instantiated.
 * STARTING_APPLICATIONS - All applications have been requested to start
 *                         their contained SGs, which in its turn has requested
 *                         their contained SUs to instantiate all their 
 *                         components. The cluster startup timer is running.
 * WAITING_OVERTIME - The cluster startup timer has expired but all
 *                    applications have yet not responded that they have been
 *                    started. Cluster will wait infinitely for the
 *                    applications to respond. It is correct to do so even when
 *                    the startup timer has expired, because the applications
 *                    will report they are started as soon as there is no
 *                    attempt to instantiate any of its components pending,
 *                    because attempts to instantiate a component can not go on
 *                    forever, see saAmfCompInstantiateTimeout,
 *                    saAmfCompNumMaxInstantiateWithoutDelay and
 *                    saAmfCompNumMaxInstantiateWithDelay.
 * ASSIGNING_WORKLOAD - All applications have been requested to assign it's
 *                      specified workload to it's service units according to
 *                      the redundancy model specified by it's SGs.
 * STARTED - A best effort has been made to instatiate the components of all
 *           applications and assign the specified workload as close as possible
 *           to what is described in the configuration.
 *
 * 1.3 Actions
 * ===========
 * A1 - [foreach application in cluster]/start application
 * A2 - start cluster startup timer
 * A3 - [foreach application in cluster]/assign workload to application
 * A4 - defer sync_ready event
 * A5 - forward sync_ready to appropriate node object
 * A6 - recall deferred event
 * A7 - stop node local instance of cluster startup timer
 * A8 - multicast 'cluster startup timer time-out' event
 *
 * 1.4 Guards
 * ==========
 * C1 - No sg has availability control state == INSTANTIATING_SERVICE_UNITS
 * C2 - No application has Availability Control state == STARTING_SGS
 * C3 - All SGs are fully instantiated
 */


#include <stdlib.h>
#include <errno.h>

#include "print.h"
#include "amf.h"
#include "util.h"
#include "main.h"
#include "service.h"

/******************************************************************************
 * Internal (static) utility functions
 *****************************************************************************/

 
typedef struct cluster_event {
	amf_cluster_event_type_t event_type;
	amf_cluster_t *cluster;
	amf_node_t *node;
} cluster_event_t;


static void cluster_defer_event (amf_cluster_event_type_t event_type, 
	struct amf_cluster *cluster, struct amf_node * node)
{
	cluster_event_t sync_ready_event = {event_type, cluster, node};
	amf_fifo_put (event_type, &cluster->deferred_events, 
		sizeof (cluster_event_t),
		&sync_ready_event);
}

static void cluster_recall_deferred_events (amf_cluster_t *cluster) 
{
	cluster_event_t cluster_event;
	
	if (amf_fifo_get (&cluster->deferred_events, &cluster_event)) {
		switch (cluster_event.event_type) {
			case CLUSTER_SYNC_READY_EV:
				log_printf (LOG_NOTICE, 
					"Recall CLUSTER_SYNC_READY_EV");

				amf_node_sync_ready (cluster_event.node);
				break;
			default:
				assert (0);
				break;
		}
	}
}

static void timer_function_cluster_recall_deferred_events (void *data)
{
	amf_cluster_t *cluster = (amf_cluster_t*)data;

	ENTER ("");
	cluster_recall_deferred_events (cluster);
}

/**
 * Determine if all applications are started so that all
 * SUs is in SA_AMF_PRESENCE_INSTANTIATED prsense state
 * @param cluster
 * 
 * @return int
 */
static int cluster_applications_started_instantiated (struct amf_cluster *cluster)
{
	int all_started = 1;
	struct amf_application *app;
	struct amf_sg *sg;
	struct amf_su *su;

	for (app = cluster->application_head; app != NULL; app = app->next) {
		for (sg = app->sg_head; sg != NULL; sg = sg->next) {
			for (su = sg->su_head; su != NULL; su = su->next) {
				if (su->saAmfSUPresenceState != SA_AMF_PRESENCE_INSTANTIATED) {
					all_started = 0;
					goto done;
				}
			}
		}
	}

	done:
	return all_started;
}

static int cluster_applications_are_starting_sgs(struct amf_cluster *cluster)
{
	struct amf_application *application = 0;
	int is_starting_sgs = 0;

	for (application = cluster->application_head; application != NULL;
		application = application->next) {
		if (application->acsm_state == APP_AC_STARTING_SGS) {
			is_starting_sgs = 1;
			break;
		}
	}
	return is_starting_sgs;
}



static void acsm_cluster_enter_assigning_workload (struct amf_cluster *cluster)
{
	log_printf(LOG_NOTICE,
		"Cluster: all applications started, assigning workload.");
	cluster->acsm_state = CLUSTER_AC_ASSIGNING_WORKLOAD;
	amf_cluster_assign_workload (cluster);
}

static void timer_function_cluster_assign_workload_tmo (void *cluster)
{
	struct req_exec_amf_cluster_start_tmo req;
	((struct amf_cluster*)cluster)->timeout_handle = 0;;

	ENTER ("");

	amf_msg_mcast (MESSAGE_REQ_EXEC_AMF_CLUSTER_START_TMO, &req, sizeof(req));
}

static inline void stop_cluster_startup_timer (struct amf_cluster *cluster)
{
	if (cluster->timeout_handle) {
		dprintf ("Stop cluster startup timer");
		poll_timer_delete (aisexec_poll_handle, 
			cluster->timeout_handle);
		cluster->timeout_handle = 0;
	}
}

static void start_cluster_startup_timer (struct amf_cluster *cluster)
{
	poll_timer_add (aisexec_poll_handle, 
		cluster->saAmfClusterStartupTimeout,
		cluster,
		timer_function_cluster_assign_workload_tmo,
		&cluster->timeout_handle);
}

static inline void cluster_enter_starting_applications (
	struct amf_cluster *cluster)
{
	ENTER ("");
	start_cluster_startup_timer (cluster);
	amf_cluster->acsm_state = CLUSTER_AC_STARTING_APPLICATIONS;
	amf_cluster_start_applications (cluster);
}

static void acsm_cluster_enter_started (amf_cluster_t *cluster)
{
	ENTER ("");
	amf_cluster->acsm_state = CLUSTER_AC_STARTED;
	amf_call_function_asynchronous (
		timer_function_cluster_recall_deferred_events, cluster);
}

/******************************************************************************
 * Event methods
 *****************************************************************************/


int amf_cluster_applications_started_with_no_starting_sgs (
struct amf_cluster *cluster)
{
	return !cluster_applications_are_starting_sgs (cluster);
}

void amf_cluster_start_tmo_event (int is_sync_masterm, 
	struct amf_cluster *cluster)
{
	ENTER ("acsm_state = %d", amf_cluster->acsm_state);

	stop_cluster_startup_timer (cluster);

	switch (cluster->acsm_state) {
		case CLUSTER_AC_STARTING_APPLICATIONS:
			if (cluster_applications_are_starting_sgs (cluster)) {
				dprintf ("Cluster startup timeout," 
					"start waiting over time");
				amf_cluster->acsm_state = 
					CLUSTER_AC_WAITING_OVER_TIME; 
			} else {
				dprintf ("Cluster startup timeout,"
					" assigning workload");
				acsm_cluster_enter_assigning_workload (cluster);
			}
			break;
		case CLUSTER_AC_ASSIGNING_WORKLOAD:
			/* ignore cluster startup timer expiration */
		case CLUSTER_AC_STARTED:
			/* ignore cluster startup timer expiration */
		case CLUSTER_AC_WAITING_OVER_TIME:
			/* ignore cluster startup timer expiration */
			break;
		default:
			log_printf(LOG_LEVEL_ERROR, "Cluster timout expired"
				" in wrong cluster"
				" state = %d", cluster->acsm_state);
			assert(0);
			break;
	}
}


/**
 * Start all applications in the cluster and start
 * the cluster startup timeout.
 * @param cluster
 * @param app
 */
void amf_cluster_start_applications(struct amf_cluster *cluster)
{
	struct amf_application *app;
	for (app = cluster->application_head; app != NULL; app = app->next) {
		amf_application_start (app, NULL);
	}
}



void amf_cluster_sync_ready (struct amf_cluster *cluster, struct amf_node *node)
{
	log_printf(LOG_NOTICE, "Cluster: starting applications.");
	switch (amf_cluster->acsm_state) {
		case CLUSTER_AC_UNINSTANTIATED:
			if (amf_cluster->saAmfClusterAdminState == 
				SA_AMF_ADMIN_UNLOCKED) {
				cluster_enter_starting_applications (cluster);
			}
			break;
		case CLUSTER_AC_STARTING_APPLICATIONS:
			cluster_enter_starting_applications(cluster);
			break;
		case CLUSTER_AC_ASSIGNING_WORKLOAD:
			/*
			 * Defer assigning workload to those syncronized nodes to
			 * CLUSTER_AC_STARTED state.
			 */
			cluster_defer_event (CLUSTER_SYNC_READY_EV, cluster,
				node);
			log_printf (LOG_LEVEL_ERROR, 
				"Sync ready not implemented in "
				"cluster state: %u\n", amf_cluster->acsm_state);
			assert (0);
			break;
		case CLUSTER_AC_WAITING_OVER_TIME:
			/*
			 * Defer assigning workload to those syncronized nodes to
			 * CLUSTER_AC_STARTED state.
			 */
			cluster_defer_event (CLUSTER_SYNC_READY_EV, cluster,
				node);
			break;
		case CLUSTER_AC_STARTED:
			TRACE1 ("Node sync ready sent from cluster in "
				"CLUSTER_AC_STARTED state");
			amf_node_sync_ready (node);
			break;

		default:
			assert (0);
			break;
	}
}

void amf_cluster_init (void)
{
	log_init ("AMF");
}



void amf_cluster_application_started (
	struct amf_cluster *cluster, struct amf_application *application)
{
	ENTER ("application '%s' started", application->name.value);

	switch (cluster->acsm_state) {
		case CLUSTER_AC_STARTING_APPLICATIONS:
			if (cluster_applications_started_instantiated (cluster)) {
				stop_cluster_startup_timer (cluster);
				acsm_cluster_enter_assigning_workload (cluster);
			}
			break;
		case CLUSTER_AC_WAITING_OVER_TIME:
			if (amf_cluster_applications_started_with_no_starting_sgs (cluster)) {
				acsm_cluster_enter_assigning_workload (cluster);
			}
			break;
		default: {
			log_printf (LOG_ERR,"Error invalid cluster availability state %d",
				cluster->acsm_state);
			openais_exit_error(cluster->acsm_state);
			break;
		}
	}
}

struct amf_cluster *amf_cluster_new (void) 
{
	struct amf_cluster *cluster = amf_calloc (1, 
					sizeof (struct amf_cluster));

	cluster->saAmfClusterStartupTimeout = -1;
	cluster->saAmfClusterAdminState = SA_AMF_ADMIN_UNLOCKED;
	cluster->deferred_events = 0;
	cluster->acsm_state = CLUSTER_AC_UNINSTANTIATED; 
	return cluster;
}

int amf_cluster_applications_assigned (struct amf_cluster *cluster)
{
	struct amf_application *app = 0;
	int is_all_application_assigned = 1;

	for (app = cluster->application_head; app != NULL; app = app->next) {
		if (app->acsm_state !=  APP_AC_WORKLOAD_ASSIGNED) {
			is_all_application_assigned = 0;
			break;
		}
	}
	return is_all_application_assigned;
}

void amf_cluster_application_workload_assigned (
	struct amf_cluster *cluster, struct amf_application *app)
{
	ENTER ("");
	switch (cluster->acsm_state) {
		case CLUSTER_AC_ASSIGNING_WORKLOAD:
			log_printf (LOG_NOTICE, "Cluster: application %s assigned.",
				app->name.value);
			if (amf_cluster_applications_assigned (cluster)) {
				acsm_cluster_enter_started (cluster);
			}
			break;
		default:
			assert(0);
			break;
	}
}

void *amf_cluster_serialize (struct amf_cluster *cluster, int *len)
{
	char *buf = NULL;
	int offset = 0, size = 0;

	TRACE8 ("%s", cluster->name.value);

	buf = amf_serialize_SaNameT (buf, &size, &offset, &cluster->name);
	buf = amf_serialize_SaUint32T (buf, &size, &offset,
		cluster->saAmfClusterStartupTimeout);
	buf = amf_serialize_SaNameT (buf, &size, &offset,
		&cluster->saAmfClusterClmCluster);
	buf = amf_serialize_SaUint32T (buf, &size, &offset,
		cluster->saAmfClusterAdminState);
	buf = amf_serialize_SaUint32T (buf, &size, &offset, cluster->acsm_state);

	*len = offset;

	return buf;
}

struct amf_cluster *amf_cluster_deserialize (char *buf) 
{
	char *tmp = buf;
	struct amf_cluster *cluster = amf_cluster_new ();

	tmp = amf_deserialize_SaNameT (tmp, &cluster->name);
	tmp = amf_deserialize_SaUint32T (tmp, &cluster->saAmfClusterStartupTimeout);
	tmp = amf_deserialize_SaNameT (tmp, &cluster->saAmfClusterClmCluster);
	tmp = amf_deserialize_SaUint32T (tmp, &cluster->saAmfClusterAdminState);
	tmp = amf_deserialize_SaUint32T (tmp, &cluster->acsm_state);

	return cluster;
}

void amf_cluster_assign_workload (struct amf_cluster *cluster)
{
	struct amf_application *app;
	ENTER ("");

	for (app = cluster->application_head; app != NULL; app = app->next) {
		amf_application_assign_workload (app, NULL);
	}
}




