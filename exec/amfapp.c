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
 * State STARTED is assumed when the application has been initially started and
 * will in the future be re-assumed after the administrative command RESTART
 * have been executed.
 * 
 */
#include <assert.h>

#include "amf.h"
#include "print.h"

static int all_sg_started(struct amf_application *app)
{
	struct amf_sg *sg;
	struct amf_su *su;
	int all_su_instantiated = 1;

	/* TODO: spare SUs... */

	for (sg = app->sg_head; sg != NULL; sg = sg->next) {
		for (su = sg->su_head; su != NULL; su = su->next) {
			if (su->saAmfSUPresenceState != SA_AMF_PRESENCE_INSTANTIATED) {
				all_su_instantiated = 0;
				break;
			}
		}
	}

	return all_su_instantiated;
}

void amf_application_start (
	struct amf_application *app, struct amf_node *node)
{
	struct amf_sg *sg;

	ENTER ("'%s'", app->name.value);

	/* TODO: Calculate and set SI dependency levels  */

	for (sg = app->sg_head; sg != NULL; sg = sg->next) {
		amf_sg_start (sg, node);
	}
}

void amf_application_assign_workload (
	struct amf_application *app, struct amf_node *node)
{
	struct amf_sg *sg;

	/*                                                              
	 * TODO: dependency level ignored
	 * Each dependency level should be looped and amf_sg_assign_si
	 * called several times.
	*/
	for (sg = app->sg_head; sg != NULL; sg = sg->next) {
		amf_sg_assign_si (sg, 0);
	}
}

void amf_application_init (void)
{
	log_init ("AMF");
}

void amf_application_sg_started (
	struct amf_application *app, struct amf_sg *sg,	struct amf_node *node)
{
	ENTER ("'%s'", app->name.value);

	if (all_sg_started (app)) {
		amf_cluster_application_started (app->cluster, app);
	}
}

void amf_application_sg_assigned (
	struct amf_application *app, struct amf_sg *sg)
{
	ENTER ("'%s'", app->name.value);
	amf_cluster_application_workload_assigned (app->cluster, app);
}

struct amf_application *amf_application_new (struct amf_cluster *cluster)
{
	struct amf_application *app = amf_malloc (sizeof (struct amf_application));

	app->cluster = cluster;
	return app;
}

void amf_application_delete (struct amf_application *app)
{
	struct amf_sg *sg;
	struct amf_si *si;

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
	int objsz = sizeof (struct amf_application);
	struct amf_application *copy;

	copy = amf_malloc (objsz);
	memcpy (copy, app, objsz);
	*len = objsz;
	TRACE8 ("%s", copy->name.value);

	return copy;
}

struct amf_application *amf_application_deserialize (
	struct amf_cluster *cluster, char *buf, int size)
{
	int objsz = sizeof (struct amf_application);

	if (objsz > size) {
		return NULL;
	} else {
		struct amf_application *obj = amf_application_new (cluster);
		assert (obj);
		memcpy (obj, buf, objsz);
		TRACE8 ("%s", obj->name.value);
		obj->cluster = cluster;
		obj->sg_head = NULL;
		obj->si_head = NULL;
		obj->next = cluster->application_head;
		cluster->application_head = obj;
		return obj;
	}
}

struct amf_application *amf_application_find (
	struct amf_cluster *cluster, char *name)
{
	struct amf_application *app;

	ENTER ("%s", name);

	for (app = cluster->application_head; app != NULL; app = app->next) {
		if (strncmp (name, (char*)app->name.value, app->name.length) == 0) {
			break;
		}
	}

	return app;
}

