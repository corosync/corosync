/** @file amfapp.c
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
 * AMF Application Class implementation
 * 
 * This file contains functions for handling the AMF applications. It can 
 * be viewed as the implementation of the AMF Application class
 * as described in SAI-Overview-B.02.01. The SA Forum specification 
 * SAI-AIS-AMF-B.02.01 has been used as specification of the behaviour
 * and is referred to as 'the spec' below.
 * 
 * The functions in this file are responsible for:
 *	- on request start the service groups it contains 
 *	- on request order the service groups to assign workload to all
 *    service units contained in the service group, level by level
 *	- to handle administrative operation support for the application (FUTURE)
 *
 * The cluster class contains the following state machines:
 *	- administrative state machine (ADSM)
 *	- availability control state machine (ACSM)
 *
 * The administrative state machine will be implemented in the future.
 *
 * ACSM handles initial start of an application. In the future it will also
 * handle administrative commands on the application as described in paragraph
 * 7.4 of the spec. ACSM includes two stable states (UNINSTANTIATED and
 * STARTED) and a number of states to control the transition between the
 * stable states.
 *
 * The application is in state UNINSTANTIATED when the application starts.
 * (In the future this state will also be assumed after the LOCK_INSTANTIATION
 * administrative command.)
 *
 * State WORKLOAD_ASSIGNED is assumed when the application has been initially
 * started and will in the future be re-assumed after the administrative
 * command RESTART have been executed.
 * 
 * 1. AMF Synchronization Control State Machine
 * =========================================
 * 
 * 1.1  State Transition Table
 * 
 * State:                  Event:                Action:  New state:
 * ===========================================================================
 * UNINSTANTIATED          start                 A6,A1    STARTING_SGS
 * STARTING_SGS            start [C4]            A7
 * STARTING_SGS            sg_started [C1]       A8,A9    STARTED
 * STARTED                 start                 A6,A1    STARTING_SGS
 * STARTED                 assign_workload       A3       ASSIGNING_WORKLOAD
 * ASSIGNING_WORKLOAD      assign_workload       A7
 * ASSIGNING_WORKLOAD      start                 A7
 * ASSIGNING_WORKLOAD      sg_assigned [C2]      A10,A9   WORKLOAD_ASSIGNED
 * WORKLOAD_ASSIGNED       start                 A6,A1    STARTING_SGS
 * WORKLOAD_ASSIGNED       assign_workload       A3       ASSIGNING_WORKLOAD
* 
*  1.2 State Description
*  =====================
* UNINSTANTIATED -  No SUs within the SGs contained in the application have been
*                   instantiated.
* STARTING_SGS - Waiting for the contained SGs to start.
* STARTED - No SUs within the SGs contained in the application are in the
*           process of beein instantiated. Either the SUs are instantiated or
*           instantiation was not possible or instantiation has failed.
* ASSIGNING_WORKLOAD - Waiting for the contained SGs to indicate they have
*                      assigned workload to its SUs.
* WORKLOAD_ASSIGNED - at least some workload has been assigned to the SUs that
*                     are in-service.
* 
*  1.3 Actions
*  ===========
*  A1 - [foreach sg in application] sg_start
*  A2 -
*  A3 - [foreach sg in application] sg_assign
*  A4 -
*  A5 -
*  A6 - save value of received node parameter
*  A7 - defer the event
*  A8 - [node == NULL] cluster_application_started else node_application_started
*  A9 - recall deferred events
*  A10 - [node == NULL] cluster_application_assigned else
*         node_application_assigned
* 
*  1.4 Guards
*  ==========
*  C1 - No sg has availability control state == INSTANTIATING_SERVICE_UNITS
*  C2 - All sgs have availability control state == IDLE
*  C3 -
*  C4 - saved node value != received node value
*/

#include <assert.h>
#include "amf.h"
#include "print.h"
#include "util.h"

/******************************************************************************
 * Internal (static) utility functions
 *****************************************************************************/

typedef struct application_event {
	amf_application_event_type_t event_type;  
	amf_application_t *app;
	amf_node_t	*node;
} application_event_t;

static void application_defer_event (
	amf_application_event_type_t event_type, amf_application_t *app, 
	amf_node_t *node) 
{
	application_event_t app_event = {event_type, app, node};
	amf_fifo_put (event_type, &app->deferred_events, 
		sizeof (application_event_t), &app_event);
}


static void application_recall_deferred_events (amf_application_t *app)
{
	application_event_t  application_event;

	if (amf_fifo_get (&app->deferred_events, &application_event)) {
		switch (application_event.event_type) {
			case APPLICATION_ASSIGN_WORKLOAD_EV: {
				log_printf (LOG_NOTICE,
					"Recall APPLICATION_ASSIGN_WORKLOAD_EV");
				amf_application_assign_workload (
					application_event.app,
					application_event.node);
					break;
			}
			case APPLICATION_START_EV: {

				log_printf (LOG_NOTICE, 
					"Recall APPLICATION_START_EV");
				amf_application_start (application_event.app,
						application_event.node);
				break;
			}
			default:
				assert (0);
				break;
		}
	}
}
static void timer_function_application_recall_deferred_events (void *data)
{
	amf_application_t *app = (amf_application_t*)data;

	ENTER ("");
	application_recall_deferred_events (app);
}

static int no_su_is_instantiating (struct amf_application *app) 
{
	struct amf_sg *sg;
	struct amf_su *su;
	int all_su_instantiated = 1;

	for (sg = app->sg_head; sg != NULL; sg = sg->next) {
		for (su = sg->su_head; su != NULL; su = su->next) {
			if (su->saAmfSUPresenceState == SA_AMF_PRESENCE_INSTANTIATING) {
				all_su_instantiated = 0;
				break;
			}
		}
	}
	return all_su_instantiated;

}

static int all_sg_assigned (struct amf_application *app)
{
	struct amf_sg *sg;
	int all_sg_assigned = 1;

	for (sg = app->sg_head; sg != NULL; sg = sg->next) {
		if (sg->avail_state != SG_AC_Idle) {
			all_sg_assigned = 0;
			break;
		}
	}
	return all_sg_assigned;
}

static void application_enter_starting_sgs (struct amf_application *app, 
	struct amf_node *node)
{
	amf_sg_t *sg = 0;
	app->node_to_start = node;
	app->acsm_state = APP_AC_STARTING_SGS;

	for (sg = app->sg_head; sg != NULL; sg = sg->next) {
		amf_sg_start (sg, node);
	}
}

static void application_enter_assigning_workload (amf_application_t *app) 
{
	amf_sg_t *sg = 0;
	int posible_to_assign_si = 0;
	app->acsm_state = APP_AC_ASSIGNING_WORKLOAD;
	for (sg = app->sg_head; sg != NULL; sg = sg->next) {
		if (amf_sg_assign_si_req (sg, 0)) {
			posible_to_assign_si = 1;
		}
	}
	if (posible_to_assign_si == 0) {
		app->acsm_state = APP_AC_WORKLOAD_ASSIGNED;
	}

}

static void application_enter_workload_assigned (amf_application_t *app)
{
	if (all_sg_assigned (app)){
		app->acsm_state = APP_AC_WORKLOAD_ASSIGNED;
		if (app->node_to_start == NULL){
			amf_cluster_application_workload_assigned (
				app->cluster, app);
		} else {
			amf_node_application_workload_assigned(
				app->node_to_start, app);
		}
		
		amf_call_function_asynchronous (
			timer_function_application_recall_deferred_events, app);
	}


}

/******************************************************************************
 * Event methods
 *****************************************************************************/


void amf_application_start (
	struct amf_application *app, struct amf_node *node)
{
	struct amf_sg *sg;

	ENTER ("'%s'", app->name.value);
	assert (app != NULL);
	switch (app->acsm_state) {
		case APP_AC_UNINSTANTIATED:
			application_enter_starting_sgs (app, node);
			break;
		case APP_AC_STARTING_SGS:
			if (app->node_to_start == node) {
				for (sg = app->sg_head; sg != NULL; sg = sg->next) {
					amf_sg_start (sg, node);
				}
			} else {
				application_defer_event (APPLICATION_START_EV, app , node);
			}
			break;
		case APP_AC_STARTED:
			/* TODO: Recall deferred events */
			app->node_to_start = node;
			app->acsm_state = APP_AC_STARTING_SGS;
			for (sg = app->sg_head; sg != NULL; sg = sg->next) {
				amf_sg_start (sg, node);
			}
			break;
		case APP_AC_ASSIGNING_WORKLOAD:
			log_printf (LOG_LEVEL_ERROR, "Request to start application"
				" =%s in state  APP_AC_ASSIGNING_WORKLOAD(should be deferred)",
				app->name.value);
			application_defer_event (APPLICATION_START_EV, app , node);
			break;
		case APP_AC_WORKLOAD_ASSIGNED:
			application_enter_starting_sgs (app, node);
			break;
		default:
			assert (0);
			break;
	}
}


void amf_application_assign_workload (struct amf_application *app, 
	struct amf_node *node)
{
	/*
	 * TODO: dependency level ignored. Each dependency level should
	 * be looped and amf_sg_assign_si called several times.
	 */

	assert (app != NULL);
	app->node_to_start = node;

	switch (app->acsm_state) {
		case APP_AC_WORKLOAD_ASSIGNED:
			application_enter_assigning_workload (app);
			break;
		case APP_AC_STARTED:
			application_enter_assigning_workload (app);
			break;
		case APP_AC_ASSIGNING_WORKLOAD:
			if (app->node_to_start == node) {
				/*
				 * Calling object has violated the contract !
				 */
				assert (0);
			} else {
				log_printf (LOG_LEVEL_ERROR, "Request to assign workload to"
					" application =%s in state APP_AC_ASSIGNING_WORKLOAD "
					"(should be deferred)", app->name.value);

				application_defer_event (APPLICATION_ASSIGN_WORKLOAD_EV, app, 
					node);
			}
			break;
		default:
			/*
			 * Calling object has violated the contract !
			 */
			assert (0);
			break;
	}
}

/******************************************************************************
 * Event response methods
 *****************************************************************************/
void amf_application_sg_started (struct amf_application *app, struct amf_sg *sg,
		struct amf_node *node)
{
	ENTER ("'%s'", app->name.value);
	
	assert (app != NULL);

	switch (app->acsm_state) {
		case APP_AC_STARTING_SGS:
			if (no_su_is_instantiating (app)) {
				app->acsm_state = APP_AC_STARTED;
				if (app->node_to_start == NULL) {
					amf_cluster_application_started (app->cluster, app);
				} else {
					amf_node_application_started (app->node_to_start, app);
				}   
			}
			break;
		default:
			log_printf (LOG_LEVEL_ERROR, "amf_application_sg_started()"
				" called in state = %d", app->acsm_state);
			openais_exit_error (AIS_DONE_FATAL_ERR);
			break;
	}
}

void amf_application_sg_assigned (
	struct amf_application *app, struct amf_sg *sg)
{
	ENTER ("'%s'", app->name.value);
	assert (app != NULL);

	switch (app->acsm_state) {
		case APP_AC_ASSIGNING_WORKLOAD:
			application_enter_workload_assigned (app);
			break;
		default:
			log_printf (LOG_LEVEL_ERROR, 
				"amf_application_sg_assigned()"
				" called in state = %d", app->acsm_state);
			openais_exit_error (AIS_DONE_FATAL_ERR);
			break;
	}
}

/******************************************************************************
 * General methods
 *****************************************************************************/
void amf_application_init (void)
{
	log_init ("AMF");
}

struct amf_application *amf_application_new (struct amf_cluster *cluster) {
	struct amf_application *app = amf_calloc (1, 
					sizeof (struct amf_application));

	app->cluster = cluster;
	app->next = cluster->application_head;
	cluster->application_head = app;
	app->acsm_state = APP_AC_UNINSTANTIATED;
	return app;
}

void amf_application_delete (struct amf_application *app)
{
	struct amf_sg *sg;
	struct amf_si *si;

	assert (app != NULL);
	for (sg = app->sg_head; sg != NULL;) {
		struct amf_sg *tmp = sg;
		sg = sg->next;
		amf_sg_delete (tmp);
	}

	for (si = app->si_head; si != NULL;) {
		struct amf_si *tmp = si;
		si = si->next;
		amf_si_delete (tmp);
	}
	free (app);
}

void *amf_application_serialize (
	struct amf_application *app, int *len)
{
	char *buf = NULL;
	int offset = 0, size = 0;

	assert (app != NULL);
	TRACE8 ("%s", app->name.value);

	buf = amf_serialize_SaNameT (buf, &size, &offset, &app->name);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, app->saAmfApplicationAdminState);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, app->saAmfApplicationCurrNumSG);
	buf = amf_serialize_SaStringT (
		buf, &size, &offset, app->clccli_path);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, app->acsm_state);

	*len = offset;
	return buf;
}

struct amf_application *amf_application_deserialize (
	struct amf_cluster *cluster, char *buf) 
{
	char *tmp = buf;
	struct amf_application *app = amf_application_new (cluster);

	tmp = amf_deserialize_SaNameT (tmp, &app->name);
	tmp = amf_deserialize_SaUint32T (tmp, &app->saAmfApplicationAdminState);
	tmp = amf_deserialize_SaUint32T (tmp, &app->saAmfApplicationCurrNumSG);
	tmp = amf_deserialize_SaStringT (tmp, &app->clccli_path);
	tmp = amf_deserialize_SaUint32T (tmp, &app->acsm_state);

	return app;
}

struct amf_application *amf_application_find (
	struct amf_cluster *cluster, char *name)
{
	struct amf_application *app;

	assert (cluster != NULL);
	for (app = cluster->application_head; app != NULL; app = app->next) {
		
		if (app->name.length == strlen(name) && 
			strncmp (name, (char*)app->name.value, app->name.length)
			 == 0) {
			break;
		}
	}

	if (app == NULL) {
		dprintf ("App %s not found!", name);
	}
	return app;
}

