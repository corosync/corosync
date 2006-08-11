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
 */

#include <stdlib.h>
#include <errno.h>

#include "print.h"
#include "amf.h"
#include "util.h"
#include "main.h"

/**
 * Determine if all applications are started
 * @param cluster
 * 
 * @return int
 */
static int all_applications_started (struct amf_cluster *cluster)
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

static void timer_function_cluster_assign_workload_tmo (void *_cluster)
{
	struct amf_application *app;
	struct amf_cluster *cluster = _cluster;

	dprintf("Cluster start timer expired, assigning workload to application\n");

	for (app = cluster->application_head; app != NULL; app = app->next) {
		amf_application_assign_workload (app, this_amf_node);
	}
}

void amf_cluster_start (struct amf_cluster *cluster)
{
	struct amf_application *app;

	log_printf(LOG_NOTICE, "Cluster: starting applications.");

	amf_cluster->state = CLUSTER_STARTING;

	for (app = cluster->application_head; app != NULL; app = app->next) {
		amf_application_start (app, NULL);
	}

	/* wait a while before assigning workload */
	poll_timer_add (aisexec_poll_handle, cluster->saAmfClusterStartupTimeout,
		cluster,
		timer_function_cluster_assign_workload_tmo,
		&cluster->timeout_handle);
}

void amf_cluster_init (void)
{
	log_init ("AMF");
}

void amf_cluster_application_started (
	struct amf_cluster *cluster, struct amf_application *application)
{
	ENTER ("application '%s' started", application->name.value);

	if (all_applications_started (cluster)) {
		struct amf_application *app;

		log_printf(LOG_NOTICE,
				   "Cluster: all applications started, assigning workload.");

		if (cluster->timeout_handle) {
			poll_timer_delete (aisexec_poll_handle, cluster->timeout_handle);
		}

		for (app = cluster->application_head; app != NULL; app = app->next) {
			amf_application_assign_workload (app, this_amf_node);
		}
	}
}

struct amf_cluster *amf_cluster_new (void)
{
	struct amf_cluster *cluster = calloc (1, sizeof (struct amf_cluster));

	if (cluster == NULL) {
		openais_exit_error (AIS_DONE_OUT_OF_MEMORY);
	}

	cluster->saAmfClusterStartupTimeout = -1;
	cluster->saAmfClusterAdminState = SA_AMF_ADMIN_UNLOCKED;

	return cluster;
}

void amf_cluster_application_workload_assigned (
	struct amf_cluster *cluster, struct amf_application *app)
{
	log_printf(LOG_NOTICE, "Cluster: all workload assigned.");
	amf_cluster->state = CLUSTER_STARTED;
}

void *amf_cluster_serialize (struct amf_cluster *cluster, int *len)
{
	int objsz = sizeof (struct amf_cluster);
	struct amf_cluster *copy;

	copy = amf_malloc (objsz);
	memcpy (copy, cluster, objsz);
	*len = objsz;
	TRACE8 ("%s", copy->name.value);

	return copy;
}

struct amf_cluster *amf_cluster_deserialize (char *buf, int size)
{
	int objsz = sizeof (struct amf_cluster);

	if (objsz > size) {
		return NULL;
	} else {
		struct amf_cluster *obj = amf_cluster_new ();
		if (obj == NULL) {
			return NULL;
		}
		memcpy (obj, buf, objsz);
		TRACE8 ("%s", obj->name.value);
		obj->node_head = NULL;
		obj->application_head = NULL;
		return obj;
	}
}

