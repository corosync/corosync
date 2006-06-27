/** @file exec/amfsu.c
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
 * AMF Service Unit Class Implementation
 * 
 * This file contains functions for handling AMF-service units(SUs). It can be 
 * viewed as the implementation of the AMF Service Unit class (called SU)
 * as described in SAI-Overview-B.02.01. The SA Forum specification 
 * SAI-AIS-AMF-B.02.01 has been used as specification of the behaviour
 * and is referred to as 'the spec' below.
 * 
 * The functions in this file are responsible for:
 *	- instantiating and terminating service units on request
 *	  (considering the dependencies between components described in paragraph
 *	   3.9.2)
 *	- creating and deleting CSI-assignment objects between its components and
 *	  CSI-objects upon request
 *	- receiving error reports from its components and forwarding them to
 *	  appropriate handler (SU or SG or node or cluster)
 *	- implementing restart of itself and its components (paragraph 3.12.1.2)
 *	- implementing error escallation level 1 (paragraph 3.12.2.2 in the spec)
 *	- handling all run time attributes of the AMF SU; all cached
 *	  attributes are stored as variables and sent to the IMM service
 *	  upon the changes described in the specification.
 *
 * SU contains the following state machines:
 *	- presence state machine (PRSM)
 *	- administrative state machine (ADSM) (NOT IN THIS RELEASE)
 *	- operational state machine (OPSM)
 *	- readiness state machine (RESM)
 *	- ha state per service instance (SI)
 *	- restart control state machine (RCSM)
 *
 * The presence state machine orders intantiation of its components on request.
 * It fully respects the dependency rules between components at instantiation
 * such that it orders instantiation simultaneously only of components on the
 * same instantiation level. The presence state machine is implemented with
 * the states described in the spec and the state transitions are trigged by
 * reported state transitions from its contained components according to
 * paragraph 3.3.1.1.
 *
 * The operational state machine is not responsible for any control function. 
 * It assumes the DISABLED state if an incoming operational state change report
 * from a component indicates the component has assumed the DISABLED state.
 * Operational state changes are reported to IMM.
 *
 * The readiness state machine is not used for any control but is updated and
 * reported to IMM when it is changed.
 *
 * The restart control state machine (RCSM) is used to implement level 1 of
 * the error escallation polycy described in chapter 3.12.2 of the spec. It
 * also implements component restart and service unit restart as described in
 * paragraph 3.12.1.2 and 3.12.1.3.
 * RCSM contains three composite states.
 * Being a composite state means that the state contains substates.
 * RCSM composite states are:
 *	- ESCALLATION_LEVEL (LEVEL_0, LEVEL_1 and LEVEL_2)
 *	- RESTARTING_COMPONENT (DEACTIVATING, RESTARTING, SETTING and ACTIVATING)
 *	- RESTARTING_SERVICE_UNIT (DEACTIVATING, TERMINATING, INSTANTIATING,
 *                             and ACTIVATING)
 * 
 * ESCALLATION_LEVEL is a kind of idle state where no actions are performed
 * and used only to remember the escallation level. Substate LEVEL_0 indicates
 * no escallation. LEVEL_1 indicates that a component restart has been 
 * executed recently and the escallation timer is still running. At this level
 * component restart requests will transition to RESTARTING_COMPONENT but
 * if there are too many restart requests before the probation timer expires
 * then a transition will be made to LEVEL_2 and the restart request will
 * be forwarded to the node instance hosting this component.
 * State RESTARTING_SERVICE_UNIT will only be assumed if the node explicitly
 * requests the SU to execute a restart of itself (after having evaluated its
 * part of the error escallation policy).
 * 
 */

 /*
 *
 */

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <errno.h>

#include "amf.h"
#include "util.h"
#include "print.h"
#include "main.h"

static int presence_state_all_comps_in_su_are_set (struct amf_su *su,
	SaAmfPresenceStateT state)
{
	int all_set = 1;
	struct amf_comp *comp;

	for (comp = su->comp_head; comp != NULL; comp = comp->next) {
		if (comp->saAmfCompPresenceState != state) {
			all_set = 0;
		}
	}

	return all_set;
}

static void su_readiness_state_set (struct amf_su *su,
	SaAmfReadinessStateT readiness_state)
{
	su->saAmfSUReadinessState = readiness_state;
	TRACE1 ("Setting SU '%s' readiness state: %s\n",
		&su->name.value, amf_readiness_state(readiness_state));
}

static void su_presence_state_set (struct amf_su *su,
	SaAmfPresenceStateT presence_state)
{
	su->saAmfSUPresenceState = presence_state;
	TRACE1 ("Setting SU '%s' presence state: %s\n",
		su->name.value, amf_presence_state (presence_state));
	amf_sg_su_state_changed (su->sg, su, SA_AMF_PRESENCE_STATE, presence_state);
}

static void su_operational_state_set (struct amf_su *su,
	SaAmfOperationalStateT oper_state)
{
	struct amf_comp* comp;

	if (oper_state == su->saAmfSUOperState) {
		log_printf (LOG_INFO,
			"Not assigning service unit new operational state - same state\n");
		return;
	}

	su->saAmfSUOperState = oper_state;
	TRACE1 ("Setting SU '%s' operational state: %s\n",
		su->name.value, amf_op_state (oper_state));

	if (oper_state == SA_AMF_OPERATIONAL_ENABLED) {
		su_readiness_state_set (su, SA_AMF_READINESS_IN_SERVICE);

		for (comp = su->comp_head; comp; comp = comp->next) {
			amf_comp_readiness_state_set (comp, SA_AMF_READINESS_IN_SERVICE);
		}

//		amf_sg_su_state_changed (su->sg, su, SA_AMF_OP_STATE, SA_AMF_OPERATIONAL_ENABLED);
	} else if (oper_state == SA_AMF_OPERATIONAL_DISABLED) {
		su_readiness_state_set (su, SA_AMF_READINESS_OUT_OF_SERVICE);
	}
}

static void comp_assign_csi (struct amf_comp *comp, struct amf_csi *csi,
	SaAmfHAStateT ha_state)
{
	struct amf_csi_assignment *csi_assignment;

	dprintf ("  Creating CSI '%s' to comp '%s' with hastate %s\n",
		getSaNameT (&csi->name), getSaNameT (&comp->name),
		amf_ha_state (ha_state));

	csi_assignment = malloc (sizeof (struct amf_csi_assignment));
	if (csi_assignment == NULL) {
		openais_exit_error (AIS_DONE_OUT_OF_MEMORY);
	}

	csi_assignment->comp_next = comp->assigned_csis;
	comp->assigned_csis = csi_assignment;
	csi_assignment->csi_next = csi->csi_assignments;
	csi->csi_assignments = csi_assignment;
	setSaNameT (&csi_assignment->name, (char*)comp->name.value);
	csi_assignment->saAmfCSICompHAState = ha_state;
	csi_assignment->csi = csi;
	csi_assignment->comp = comp;
	csi_assignment->saAmfCSICompHAState = 0; /* undefined confirmed HA state */
	csi_assignment->requested_ha_state = ha_state;

	if (ha_state == SA_AMF_HA_ACTIVE)
		comp->saAmfCompNumCurrActiveCsi++;
	else if (ha_state == SA_AMF_HA_STANDBY)
		comp->saAmfCompNumCurrStandbyCsi++;
	else
		assert (0);
}

static void su_cleanup (struct amf_su *su)
{
	struct amf_comp *comp;

	for (comp = su->comp_head; comp != NULL; comp = comp->next) {
		amf_comp_restart (comp);
	}
}

static void escalation_policy_cleanup (struct amf_comp *comp)
{
//	escalation_timer_start (comp);

	switch (comp->su->escalation_level) {
	case ESCALATION_LEVEL_NO_ESCALATION:
		comp->saAmfCompRestartCount += 1;
		if (comp->saAmfCompRestartCount >= comp->su->sg->saAmfSGCompRestartMax) {
			comp->su->escalation_level = ESCALATION_LEVEL_ONE;
			escalation_policy_cleanup (comp);
			comp->saAmfCompRestartCount = 0;
			return;
		}
		dprintf ("Escalation level 0 - restart component\n");
		dprintf ("Cleaning up and restarting component.\n");
		amf_comp_restart (comp);
		break;

	case ESCALATION_LEVEL_ONE:
		comp->su->saAmfSURestartCount += 1;
		if (comp->su->saAmfSURestartCount >= comp->su->sg->saAmfSGSuRestartMax) {
			comp->su->escalation_level = ESCALATION_LEVEL_TWO;
			escalation_policy_cleanup (comp);
			comp->saAmfCompRestartCount = 0;
			comp->su->saAmfSURestartCount = 0;
			return;
		}
		dprintf ("Escalation level 1 - restart unit\n");
		dprintf ("Cleaning up and restarting unit.\n");
		su_cleanup (comp->su);
		break;

	case ESCALATION_LEVEL_TWO:
		dprintf ("Escalation level TWO\n");
		su_cleanup (comp->su);
//		unit_terminate_failover (comp);
		break;

	case ESCALATION_LEVEL_THREE:
//TODO
		break;
	}
}

void amf_su_instantiate (struct amf_su *su)
{
	struct amf_comp *comp;

	ENTER ("'%s'", su->name.value);

	for (comp = su->comp_head; comp != NULL; comp = comp->next) {
		amf_comp_instantiate (comp);
	}
}

void amf_su_assign_si (struct amf_su *su, struct amf_si *si,
	SaAmfHAStateT ha_state)
{
	struct amf_si_assignment *si_assignment;

	dprintf ("Creating SI '%s' to SU '%s' with hastate %s\n",
		getSaNameT (&si->name), getSaNameT (&su->name),
		amf_ha_state (ha_state));

	si_assignment = malloc (sizeof (struct amf_si_assignment));
	if (si_assignment == NULL) {
		openais_exit_error (AIS_DONE_OUT_OF_MEMORY);
	}
	setSaNameT (&si_assignment->name, (char*)su->name.value);
	si_assignment->saAmfSISUHAState = 0; /* undefined confirmed HA state */
	si_assignment->requested_ha_state = ha_state;
	si_assignment->next = su->assigned_sis;
	su->assigned_sis = si_assignment;
	si_assignment->si = si;
	memcpy (&si_assignment->si->saAmfSIProtectedbySG,
		&su->sg->name, sizeof (SaNameT));

	if (ha_state == SA_AMF_HA_ACTIVE) {
		si->saAmfSINumCurrActiveAssignments++;
		su->saAmfSUNumCurrActiveSIs++;
	} else if (ha_state == SA_AMF_HA_STANDBY) {
		su->saAmfSUNumCurrStandbySIs++;
		si->saAmfSINumCurrStandbyAssignments++;
	} else
		assert(0);

	if ((si->saAmfSINumCurrActiveAssignments == si->saAmfSIPrefActiveAssignments) &&
		(si->saAmfSINumCurrStandbyAssignments == si->saAmfSIPrefStandbyAssignments)) {
		si->saAmfSIAssignmentState = SA_AMF_ASSIGNMENT_FULLY_ASSIGNED;
	} else if ((si->saAmfSINumCurrActiveAssignments < si->saAmfSIPrefActiveAssignments) ||
		(si->saAmfSINumCurrStandbyAssignments < si->saAmfSIPrefStandbyAssignments)) {
		si->saAmfSIAssignmentState = SA_AMF_ASSIGNMENT_PARTIALLY_ASSIGNED;
	}

	{
		struct amf_csi *csi;
		struct amf_comp *comp;
		SaNameT *cs_type;
		int i;

		/*
		** for each component in SU, find a CSI in the SI with the same type
		*/
		for (comp = su->comp_head; comp != NULL; comp = comp->next) {
			int no_of_cs_types = 0;
			for (i = 0; comp->saAmfCompCsTypes[i]; i++) {
				cs_type = comp->saAmfCompCsTypes[i];
				no_of_cs_types++;
				int no_of_assignments = 0;

				for (csi = si->csi_head; csi != NULL; csi = csi->next) {
					if (!memcmp(csi->saAmfCSTypeName.value, cs_type->value, cs_type->length)) {
						comp_assign_csi (comp, csi, ha_state);
						no_of_assignments++;
					}
				}
				if (no_of_assignments == 0) {
					log_printf (LOG_WARNING, "\t   No CSIs of type %s configured?!!\n",
						getSaNameT (cs_type));
				}
			}
			if (no_of_cs_types == 0) {
				log_printf (LOG_LEVEL_ERROR, "\t   No CS types configured for comp %s ?!!\n",
					getSaNameT (&comp->name));
			}
		}
	}
}

/**
 * Used by a component to report a state change event
 * @param su
 * @param comp
 * @param type type of state
 * @param state new state
 */
void amf_su_comp_state_changed (
	struct amf_su *su, struct amf_comp *comp, SaAmfStateT type, int state)
{
	if (type == SA_AMF_PRESENCE_STATE) {
		/*
		 * If all comp presence states are INSTANTIATED, then SU should
		 * be instantated.
		 */
		if (state == SA_AMF_PRESENCE_INSTANTIATED) {
			if (presence_state_all_comps_in_su_are_set (
					comp->su, SA_AMF_PRESENCE_INSTANTIATED)) {

				su_presence_state_set (comp->su, SA_AMF_PRESENCE_INSTANTIATED);
			} else {
				/*
				 * This state occurs when there is more then
				 * one SU
				 */
				return;
			}
		} else if (state == SA_AMF_PRESENCE_INSTANTIATING) {
		} else if (state == SA_AMF_PRESENCE_RESTARTING) {
		} else {
			assert (0);
		}
	} else if (type == SA_AMF_OP_STATE) {
		/*
		 * If all component op states are ENABLED, then SU op 
		 * state should be ENABLED.
		 */
		if (state == SA_AMF_OPERATIONAL_ENABLED) {
			struct amf_comp *comp_compare;
			int all_set = 1;
			for (comp_compare = comp->su->comp_head; comp_compare != NULL; comp_compare = comp_compare->next) {
				if (comp_compare->saAmfCompOperState != SA_AMF_OPERATIONAL_ENABLED) {
					all_set = 0;
					break;
				}
			}
			if (all_set) {
				su_operational_state_set (comp->su, SA_AMF_OPERATIONAL_ENABLED);
			} else {
				su_operational_state_set (comp->su, SA_AMF_OPERATIONAL_DISABLED);
			}
		} else {
			assert (0);
		}
	} else {
		assert (0);
	}
}

/**
 * Used by a component to report a change in HA state
 * @param su
 * @param comp
 * @param csi_assignment
 */
void amf_su_comp_hastate_changed (
	struct amf_su *su, struct amf_comp *comp,
	struct amf_csi_assignment *csi_assignment)
{
	ENTER("'%s' '%s'", comp->name.value, csi_assignment->csi->name.value);
}

/**
 * Determine if the SU is hosted on the local node.
 * @param su
 * 
 * @return int
 */
int amf_su_is_local (struct amf_su *su)
{
	if (name_match (&this_amf_node->name, &su->saAmfSUHostedByNode)) {
		return 1;
	} else {
		return 0;
	}
}

/**
 * Called by a component to report a suspected error on a component
 * @param su
 * @param comp
 * @param recommended_recovery
 */
void amf_su_comp_error_suspected (
	struct amf_su *su,
	struct amf_comp *comp,
	SaAmfRecommendedRecoveryT recommended_recovery)
{
	escalation_policy_cleanup (comp);
}

void amf_su_init (void)
{
	log_init ("AMF");
}

