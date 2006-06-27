/** @file amfsg.c
 * 
 * Copyright (c) 2002-2006 MontaVista Software, Inc.
 * Author: Steven Dake (sdake@mvista.com)
 *
 * Copyright (c) 2006 Ericsson AB.
 *  Author: Hans Feldt
 * - Introduced AMF B.02 information model
 * - Use DN in API and multicast messages
 * - (Re-)Introduction of event based multicast messages
 * - Refactoring of code into several AMF files
 *  Author: Anders Eriksson
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
 * AMF Service Group Class Implementation
 * 
 * This file contains functions for handling AMF-service groups(SGs). It can be 
 * viewed as the implementation of the AMF Service Group class (called SG)
 * as described in SAI-Overview-B.02.01. The SA Forum specification 
 * SAI-AIS-AMF-B.02.01 has been used as specification of the behaviour
 * and is referred to as 'the spec' below.
 * 
 * The functions in this file are responsible for:
 *	-on request start the service group by instantiating the contained SUs
 *	-on request assign the service instances it protects to the in-service
 *   service units it contains respecting as many as possible of the configured
 *   requirements for the group
 *	-create and delete an SI-assignment object for each relation between
 *	 an SI and an SU
 *	-order each contained SU to create and delete CSI-assignments
 *	-request the Service Instance class (SI) to execute the transfer of the
 *	  HA-state set/remove requests to each component involved
 *	-fully control the execution of component failover and SU failover
 *	-on request control the execution of the initial steps of node switchover
 *	 and node failover
 *	-fully handle the auto adjust procedure
 *
 * Currently only the 'n+m' redundancy model is implemented. It is the 
 * ambition to identify n+m specific variables and functions and add the suffix
 * '_nplusm' to them so that they can be easily recognized.
 * 
 * When SG is requested to assign workload to all SUs or all SUs hosted on
 * a specific node, a procedure containing several steps is executed:
 *	<1> An algorithm is executed which assigns SIs to SUs respecting the rules
 *		that has been configured for SG. The algorithm also has to consider 
 *	    if assignments between som SIs and SUs already exist. The scope of this
 *	    algorithm is to create SI-assignments and set up requested HA-state for
 *	    each assignment but not to transfer those HA-states to the components.
 *	<2> All SI-assignments with a requested HA state == ACTIVE are transferred
 *	    to the components concerned before any STANDBY assignments are 
 *      transferred. All components have to acknowledge the setting of the 
 *      ACTIVE HA state before the transfer of any STANDBY assignment is 
 *      initiated.
 *	<3> All active assignments can not be transferred at the same time to the
 *      different components because the rules for dependencies between SI and
 *      SI application wide and CSI and CSI within one SI, has to be respected.
 *
 * SG is fully responsible for step <1> but not fully responsible for handling
 * step <2> and <3>. However, SG uses an attribute called 'dependency level'
 * when requsted to assign workload. This parameter refers to an integer that
 * has been calculated initially for each SI. The 'dependency level' indicates
 * to which extent an SI depends on other SIs such that an SI that depends on
 * no other SI is on dependecy_level == 1, an SI that depends only on an SI on
 * dependency_level == 1 is on dependency-level == 2. 
 * An SI that depends on several SIs gets a 
 * dependency_level that is one unit higher than the SI with the highest 
 * dependency_level it depends on. When SG is requested to assign the workload
 * on a certain dependency level, it requests all SI objects on that level to
 * activate (all) SI-assignments that during step <1> has been requested to
 * assume the active HA state.
 *
 * SG contains the following state machines:
 *	- administrative state machine (ADSM) (NOT IN THIS RELEASE)
 *	- availability control state machine (ACSM)
 *
 * The availability control state machine contains two states and one of them
 * is composite. Being a composite state means that it contains substates.
 * The states are:
 * - IDLE (non composite state)
 * - MANAGING_SG (composite state)
 * MANAGING_SG is entered at several different events which has in common
 * the need to set up or change the assignment of SIs to SUs. Only one such
 * event can be handled at the time. If new events occur while one event is
 * being handled then the new event is saved and will be handled after the
 * handling of the first event is ready (return to IDLE state has been done).
 * MANAGING_SG handles the following events:
 * - start (requests SG to order SU to instantiate all SUs in SG and waits
 *			for SU to indicate presence state change reports from the SUs and
 *			finally responds 'started' to the requester)
 * - assign (requests SG to assign SIs to SUs according to pre-configured 
 *			 rules (if not already done) and transfer the HA state of
 *			 the SIs on the requested SI dependency level. Then SG waits for 
 *			 confirmation that the HA state has been succesfully set and 
 *			 finally responds 'assigned' to the reqeuster)
 * - auto_adjust (this event indicates that the auto-adjust probation timer has
 *				  expired and that SG should evaluate current assignments of
 *				  SIs to SUs and if needed remove current assignments and 
 *				  create new according to what is specified in paragraph
 *				  3.7.1.2) 
 * - failover_comp (requests SG to failover a specific component according to
 *					the procedure described in paragraph 3.12.1.3)
 * - failover_su (requests SG to failover a specific SU according to the 
 *				  procedure described in paragraph 3.12.1.3 and 3.12.1.4)
 * - switchover_node (requests SG to execute the recovery actions described
 *					  in 3.12.1.3 and respond to the requester when recovery 
 *					  is completed)
 * - failover_node (requests SG to execute the recovery actions described
 *				   in 3.12.1.3 and respond to the requester when recovery is
 *                 completed)
 * 
 */

#include <stdlib.h>
#include <errno.h>

#include "amf.h"
#include "print.h"
#include "main.h"
#include "util.h"

static inline int div_round (int a, int b)
{
	int res;
	
	res = a / b;
	if ((a % b) != 0)
		res++;
	return res;
}

static int all_su_instantiated(struct amf_sg *sg)
{
	struct amf_su   *su;
	int all_instantiated = 1;

	for (su = sg->su_head; su != NULL; su = su->next) {
		if (su->saAmfSUPresenceState != SA_AMF_PRESENCE_INSTANTIATED) {
			all_instantiated = 0;
			break;
		}
	}

	return all_instantiated;
}

static int application_si_count_get (struct amf_application *app)
{
	struct amf_si *si;
	int answer = 0;

	for (si = app->si_head; si != NULL; si = si->next) {
		answer += 1;
	}
	return (answer);
}

static void sg_assign_nm_active (struct amf_sg *sg, int su_units_assign)
{
	struct amf_su *unit;
	struct amf_si *si;
	int assigned = 0;
	int assign_per_su = 0;
	int total_assigned = 0;

	assign_per_su = application_si_count_get (sg->application);
	assign_per_su = div_round (assign_per_su, su_units_assign);
	if (assign_per_su > sg->saAmfSGMaxActiveSIsperSUs) {
		assign_per_su = sg->saAmfSGMaxActiveSIsperSUs;
	}

	si = sg->application->si_head;
	unit = sg->su_head;
	while (unit != NULL) {
		if (unit->saAmfSUReadinessState != SA_AMF_READINESS_IN_SERVICE ||
			unit->saAmfSUNumCurrActiveSIs == sg->saAmfSGMaxActiveSIsperSUs ||
			unit->saAmfSUNumCurrStandbySIs > 0) {

			unit = unit->next;
			continue; /* Not in service */
		}

		assigned = 0;
		while (si != NULL &&
			assigned < assign_per_su &&
			total_assigned < application_si_count_get (sg->application)) {

			assigned += 1;
			total_assigned += 1;
			amf_su_assign_si (unit, si, SA_AMF_HA_ACTIVE);
			si = si->next;
		}
		unit = unit->next;
	}
	
	if (total_assigned == 0) {
		dprintf ("Error: No SIs assigned!");
	}
}

static void sg_assign_nm_standby (struct amf_sg *sg, int units_assign_standby)
{
	struct amf_su *unit;
	struct amf_si *si;
	int assigned = 0;
	int assign_per_su = 0;
	int total_assigned = 0;

	if (units_assign_standby == 0) {
		return;
	}
	assign_per_su = application_si_count_get (sg->application);
	assign_per_su = div_round (assign_per_su, units_assign_standby);
	if (assign_per_su > sg->saAmfSGMaxStandbySIsperSUs) {
		assign_per_su = sg->saAmfSGMaxStandbySIsperSUs;
	}

	si = sg->application->si_head;
	unit = sg->su_head;
	while (unit != NULL) {
		if (unit->saAmfSUReadinessState != SA_AMF_READINESS_IN_SERVICE ||
			unit->saAmfSUNumCurrActiveSIs > 0 ||
			unit->saAmfSUNumCurrStandbySIs == sg->saAmfSGMaxStandbySIsperSUs) {

			unit = unit->next;
			continue; /* Not available for assignment */
		}

		assigned = 0;
		while (si != NULL && assigned < assign_per_su) {
			assigned += 1;
			total_assigned += 1;
			amf_su_assign_si (unit, si, SA_AMF_HA_STANDBY);
			si = si->next;
		}
		unit = unit->next;
	}
	if (total_assigned == 0) {
		dprintf ("Error: No SIs assigned!");
	}
}
#if 0
static void assign_nm_spare (struct amf_sg *sg)
{
	struct amf_su *unit;

	for (unit = sg->su_head; unit != NULL; unit = unit->next) {
		if (unit->saAmfSUReadinessState == SA_AMF_READINESS_IN_SERVICE &&
			(unit->requested_ha_state != SA_AMF_HA_ACTIVE &&
			unit->requested_ha_state != SA_AMF_HA_STANDBY)) {

			dprintf ("Assigning to SU %s with SPARE\n",
				getSaNameT (&unit->name));
		}
	}
}
#endif

static int su_inservice_count_get (struct amf_sg *sg)
{
	struct amf_su *unit;
	int answer = 0;

	for (unit = sg->su_head; unit != NULL; unit = unit->next) {
		if (unit->saAmfSUReadinessState == SA_AMF_READINESS_IN_SERVICE) {
			answer += 1;
		}
	}
	return (answer);
}

static void si_activated_callback (struct amf_si *si, int result)
{
	/*                                                              
     * TODO: not implemented yet...
     */
}

/**
 * TODO: dependency_level not used, hard coded
 * @param sg
 * @param dependency_level
 */
void amf_sg_assign_si (struct amf_sg *sg, int dependency_level)
{
	int active_sus_needed;
	int standby_sus_needed;
	int inservice_count;
	int units_for_standby;
	int units_for_active;
	int ii_spare;
	int su_active_assign;
	int su_standby_assign;
	int su_spare_assign;

	ENTER ("'%s'", sg->name.value);

	/**
     * Phase 1: Calculate assignments and create all runtime objects in
     * information model. Do not do the actual assignment, done in
     * phase 2.
     */

	/**
	 * Calculate number of SUs to assign to active or standby state
	 */
	inservice_count = (float)su_inservice_count_get (sg);

	active_sus_needed = div_round (application_si_count_get (sg->application),
		sg->saAmfSGMaxActiveSIsperSUs);

	standby_sus_needed = div_round (application_si_count_get (sg->application),
		sg->saAmfSGMaxStandbySIsperSUs);

	units_for_active = inservice_count - sg->saAmfSGNumPrefStandbySUs;
	if (units_for_active < 0) {
		units_for_active = 0;
	}

	units_for_standby = inservice_count - sg->saAmfSGNumPrefActiveSUs;
	if (units_for_standby < 0) {
		units_for_standby = 0;
	}

	ii_spare = inservice_count - sg->saAmfSGNumPrefActiveSUs - sg->saAmfSGNumPrefStandbySUs;
	if (ii_spare < 0) {
		ii_spare = 0;
	}

    /**
	 * Determine number of active and standby service units
	 * to assign based upon reduction procedure
	 */
	if ((inservice_count - active_sus_needed) < 0) {
		dprintf ("assignment VI - partial assignment with SIs drop outs\n");

		su_active_assign = active_sus_needed;
		su_standby_assign = 0;
		su_spare_assign = 0;
	} else
	if ((inservice_count - active_sus_needed - standby_sus_needed) < 0) {
		dprintf ("assignment V - partial assignment with reduction of standby units\n");

		su_active_assign = active_sus_needed;
		if (standby_sus_needed > units_for_standby) {
			su_standby_assign = units_for_standby;
		} else {
			su_standby_assign = standby_sus_needed;
		}
		su_spare_assign = 0;
	} else
	if ((sg->saAmfSGMaxStandbySIsperSUs * units_for_standby) <= application_si_count_get (sg->application)) {
		dprintf ("IV: full assignment with reduction of active service units\n");
		su_active_assign = inservice_count - standby_sus_needed;
		su_standby_assign = standby_sus_needed;
		su_spare_assign = 0;
	} else 
	if ((sg->saAmfSGMaxActiveSIsperSUs * units_for_active) <= application_si_count_get (sg->application)) {

		dprintf ("III: full assignment with reduction of standby service units\n");
		su_active_assign = sg->saAmfSGNumPrefActiveSUs;
		su_standby_assign = units_for_standby;
		su_spare_assign = 0;
	} else
	if (ii_spare == 0) {
		dprintf ("II: full assignment with spare reduction\n");

		su_active_assign = sg->saAmfSGNumPrefActiveSUs;
		su_standby_assign = sg->saAmfSGNumPrefStandbySUs;
		su_spare_assign = 0;
	} else {
		dprintf ("I: full assignment with spares\n");

		su_active_assign = sg->saAmfSGNumPrefActiveSUs;
		su_standby_assign = sg->saAmfSGNumPrefStandbySUs;
		su_spare_assign = ii_spare;
	}

	dprintf ("(inservice=%d) (assigning active=%d) (assigning standby=%d) (assigning spares=%d)\n",
		inservice_count, su_active_assign, su_standby_assign, su_spare_assign);
	sg_assign_nm_active (sg, su_active_assign);
	sg_assign_nm_standby (sg, su_standby_assign);

	/**
     * Phase 2: do the actual assignment to the component
     */
	{
		struct amf_si *si;

		for (si = sg->application->si_head; si != NULL; si = si->next) {
			if (name_match (&si->saAmfSIProtectedbySG, &sg->name)) {
				amf_si_activate (si, si_activated_callback);
			}
		}
	}

	LEAVE ("'%s'", sg->name.value);
}

void amf_sg_start (struct amf_sg *sg, struct amf_node *node)
{
	struct amf_su *su;

	ENTER ("'%s'", sg->name.value);

	for (su = sg->su_head; su != NULL; su = su->next) {
		amf_su_instantiate (su);
	}
}

void amf_sg_su_state_changed (
	struct amf_sg *sg, struct amf_su *su, SaAmfStateT type, int state)
{
	ENTER ("'%s' SU '%s' state %d", sg->name.value, su->name.value, state);

	if (type == SA_AMF_PRESENCE_STATE) {
		if (state == SA_AMF_PRESENCE_INSTANTIATED) {
			/*
			 * If all SU presence states are INSTANTIATED, report to SG.
			 */
			if (all_su_instantiated(su->sg)) {
				amf_application_sg_started (sg->application, sg, this_amf_node);
			}
		} else {
			assert (0);
		}
	} else {
		assert (0);
	}
}

void amf_sg_init (void)
{
	log_init ("AMF");
}

void amf_sg_si_activated (struct amf_sg *sg, struct amf_si *si)
{
	ENTER ("");
	amf_application_sg_assigned (sg->application, sg);
}

