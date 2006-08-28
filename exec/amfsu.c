/** @file exec/amfsu.c
 * 
 * Copyright (c) 2002-2006 MontaVista Software, Inc.
 * Author: Steven Dake (sdake@mvista.com)
 *
 * Copyright (c) 2006 Ericsson AB.
 * Author: Hans Feldt, Anders Eriksson, Lars Holm
 * - Introduced AMF B.02 information model
 * - Use DN in API and multicast messages
 * - (Re-)Introduction of event based multicast messages
 * - Refactoring of code into several AMF files
 * - Component/SU restart, SU failover
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

int amf_su_presence_state_all_comps_in_su_are_set (struct amf_su *su,
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

/**
 * This function only logs since the readiness state is runtime
 * calculated.
 * @param su
 * @param amf_readiness_state
 */
static void su_readiness_state_set (struct amf_su *su,
	SaAmfReadinessStateT readiness_state)
{
	log_printf (LOG_NOTICE, "Setting SU '%s' readiness state: %s\n",
		su->name.value, amf_readiness_state (readiness_state));
}

static void clear_ha_state (
	struct amf_su *su, struct amf_si_assignment *si_assignment)
{
	ENTER ("");
	si_assignment->saAmfSISUHAState = 0;
}

static void su_presence_state_set (struct amf_su *su,
	SaAmfPresenceStateT presence_state)
{
	/*                                                              
	 * Set all SI's confirmed HA state to unknown if uninstantiated
	*/
	if (su->saAmfSUPresenceState == SA_AMF_PRESENCE_UNINSTANTIATED) {
		amf_su_foreach_si_assignment (su, clear_ha_state);
	}

	su->saAmfSUPresenceState = presence_state;
	log_printf (LOG_NOTICE, "Setting SU '%s' presence state: %s\n",
		su->name.value, amf_presence_state (presence_state));

	if (su->restart_control_state != SU_RC_RESTART_SU_SETTING) {
		amf_sg_su_state_changed (
			su->sg, su, SA_AMF_PRESENCE_STATE, presence_state);
	}
}

static void su_operational_state_set (struct amf_su *su,
	SaAmfOperationalStateT oper_state)
{
	struct amf_comp* comp;

	su->saAmfSUOperState = oper_state;
	log_printf (LOG_NOTICE, "Setting SU '%s' operational state: %s\n",
		su->name.value, amf_op_state (oper_state));

	if (oper_state == SA_AMF_OPERATIONAL_ENABLED) {
		su_readiness_state_set (su, SA_AMF_READINESS_IN_SERVICE);

		for (comp = su->comp_head; comp; comp = comp->next) {
			amf_comp_readiness_state_set (comp, SA_AMF_READINESS_IN_SERVICE);
		}
	} else if (oper_state == SA_AMF_OPERATIONAL_DISABLED) {
		su_readiness_state_set (su, SA_AMF_READINESS_OUT_OF_SERVICE);
		for (comp = su->comp_head; comp; comp = comp->next) {
			amf_comp_readiness_state_set (comp, SA_AMF_READINESS_OUT_OF_SERVICE);
		}
	}
}

static void comp_assign_csi (struct amf_comp *comp, struct amf_csi *csi,
	struct amf_si_assignment *si_assignment, SaAmfHAStateT ha_state)
{
	struct amf_csi_assignment *csi_assignment;

	dprintf ("  Creating CSI '%s' to comp '%s' with hastate %s\n",
		getSaNameT (&csi->name), getSaNameT (&comp->name),
		amf_ha_state (ha_state));

	csi_assignment = amf_malloc (sizeof (struct amf_csi_assignment));
	csi_assignment->next = csi->assigned_csis;
	csi->assigned_csis = csi_assignment;
	amf_comp_dn_make (comp, &csi_assignment->name);
	csi_assignment->comp = comp;
	csi_assignment->csi = csi;
	csi_assignment->saAmfCSICompHAState = 0; /* undefined confirmed HA state */
	csi_assignment->requested_ha_state = ha_state;
	csi_assignment->si_assignment = si_assignment;
}

static void su_restart (struct amf_su *su)
{
	struct amf_comp *comp;
	SaNameT dn;

	ENTER ("'%s'", su->name.value);

	amf_su_dn_make (su, &dn);
	log_printf (LOG_NOTICE, "Error detected for '%s', recovery "
		"action:\n\t\tSU restart", dn.value);

	su->restart_control_state = SU_RC_RESTART_SU_DEACTIVATING;
	su->restart_control_state = SU_RC_RESTART_SU_INSTANTIATING;
	su->escalation_level_history_state =
		SU_RC_ESCALATION_LEVEL_2;

	su->saAmfSURestartCount += 1;

	for (comp = su->comp_head; comp != NULL; comp = comp->next) {
		amf_comp_restart (comp);
	}
}

static void comp_restart (struct amf_comp *comp)
{
	SaNameT dn;

	ENTER ("'%s'", comp->name.value);
	amf_comp_dn_make (comp, &dn);
	log_printf (LOG_NOTICE, "Error detected for '%s', recovery "
		"action:\n\t\tcomponent restart", dn.value);

	comp->su->restart_control_state = SU_RC_RESTART_COMP_DEACTIVATING;
	comp->su->restart_control_state = SU_RC_RESTART_COMP_RESTARTING;
	comp->su->escalation_level_history_state = SU_RC_ESCALATION_LEVEL_1;
	amf_comp_restart (comp);
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

	si_assignment = amf_malloc (sizeof (struct amf_si_assignment));
	amf_su_dn_make (su, &si_assignment->name);
	si_assignment->saAmfSISUHAState = 0; /* undefined confirmed HA state */
	si_assignment->requested_ha_state = ha_state;
	si_assignment->next = si->assigned_sis;
	si->assigned_sis = si_assignment;
	si_assignment->si = si;
	si_assignment->su = su;

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
					if (!memcmp(csi->saAmfCSTypeName.value, cs_type->value,
							cs_type->length)) {
						comp_assign_csi (comp, csi, si_assignment, ha_state);
						no_of_assignments++;
					}
				}
				if (no_of_assignments == 0) {
					log_printf (
						LOG_WARNING, "\t   No CSIs of type %s configured?!!\n",
						getSaNameT (cs_type));
				}
			}
			if (no_of_cs_types == 0) {
				log_printf (LOG_LEVEL_ERROR,
					"\t   No CS types configured for comp %s ?!!\n",
					getSaNameT (&comp->name));
			}
		}
	}
}

static void si_ha_state_assumed_cbfn (
	struct amf_si_assignment *si_assignment, int result)
{
	struct amf_si_assignment *tmp_si_assignment;
	struct amf_comp *comp;
	struct amf_csi_assignment *csi_assignment;
	int all_confirmed = 1;

	ENTER ("");

	tmp_si_assignment = amf_su_get_next_si_assignment(si_assignment->su, NULL);

	while (tmp_si_assignment != NULL) {
		for (comp = tmp_si_assignment->su->comp_head; comp != NULL;
			  comp = comp->next) {

			csi_assignment = amf_comp_get_next_csi_assignment(comp, NULL);
			while (csi_assignment != NULL) {

				if (csi_assignment->requested_ha_state != 
					csi_assignment->saAmfCSICompHAState) {
					all_confirmed = 0;
				}
				csi_assignment = amf_comp_get_next_csi_assignment(
					comp, csi_assignment);
			}
		}
		tmp_si_assignment = amf_su_get_next_si_assignment(
			si_assignment->su, tmp_si_assignment);
	}

	if (all_confirmed) {
		switch (si_assignment->su->restart_control_state) {
			case SU_RC_RESTART_COMP_SETTING:
				log_printf (LOG_NOTICE, "Component restart recovery finished");
				break;
			case SU_RC_RESTART_SU_SETTING:
				log_printf (LOG_NOTICE, "SU restart recovery finished");
				break;
			default:
				assert (0);
		}
		si_assignment->su->restart_control_state =
			si_assignment->su->escalation_level_history_state;
	}
}

static void reassign_sis(struct amf_su *su)
{
	struct amf_si_assignment *si_assignment;

	ENTER ("");

	si_assignment = amf_su_get_next_si_assignment(su, NULL);

	while (si_assignment != NULL) {
		si_assignment->saAmfSISUHAState = 0; /* unknown */
		amf_si_ha_state_assume (si_assignment, si_ha_state_assumed_cbfn);
		si_assignment = amf_su_get_next_si_assignment(su, si_assignment);
	}
}

static void su_comp_presence_state_changed (
	struct amf_su *su, struct amf_comp *comp, int state)
{
	ENTER ("'%s', '%s'", su->name.value, comp->name.value);

	switch (state) {
		case SA_AMF_PRESENCE_INSTANTIATED:
			switch (su->restart_control_state) {
				case SU_RC_ESCALATION_LEVEL_2:
					/* 
					 * TODO: send to node
					*/
				case SU_RC_ESCALATION_LEVEL_0:
					if (amf_su_presence_state_all_comps_in_su_are_set (
						comp->su, SA_AMF_PRESENCE_INSTANTIATED)) {

						su_presence_state_set (
							comp->su, SA_AMF_PRESENCE_INSTANTIATED);
					}
					break;
				case SU_RC_RESTART_COMP_RESTARTING:
					su->restart_control_state = SU_RC_RESTART_COMP_SETTING;
					reassign_sis (comp->su);
					break;
				case SU_RC_RESTART_SU_INSTANTIATING:
					if (amf_su_presence_state_all_comps_in_su_are_set (
						comp->su, SA_AMF_PRESENCE_INSTANTIATED)) {

						su->restart_control_state = SU_RC_RESTART_SU_SETTING;
						su_presence_state_set (
							comp->su, SA_AMF_PRESENCE_INSTANTIATED);
						reassign_sis (comp->su);
					}
					break;
				default:
					dprintf ("state %d", su->restart_control_state);
					assert (0);
			}
			break;
		case SA_AMF_PRESENCE_UNINSTANTIATED:
			if (amf_su_presence_state_all_comps_in_su_are_set (
				su, SA_AMF_PRESENCE_UNINSTANTIATED)) {

				su_presence_state_set (comp->su,
					SA_AMF_PRESENCE_UNINSTANTIATED);
			}
			break;
		case SA_AMF_PRESENCE_INSTANTIATING:
			break;
		case SA_AMF_PRESENCE_RESTARTING:
			break;
		case SA_AMF_PRESENCE_TERMINATING:
			break;
		default:
			assert (0);
	}
}

static void su_comp_op_state_changed (
	struct amf_su *su, struct amf_comp *comp, int state)
{
	ENTER ("'%s', '%s'", su->name.value, comp->name.value);

	switch (state) {
		case SA_AMF_OPERATIONAL_ENABLED:
			{
				struct amf_comp *comp_compare;
				int all_set = 1;
				for (comp_compare = comp->su->comp_head;
					comp_compare != NULL; comp_compare = comp_compare->next) {
					if (comp_compare->saAmfCompOperState !=
						SA_AMF_OPERATIONAL_ENABLED) {

						all_set = 0;
						break;
					}
				}
				if (all_set) {
					su_operational_state_set (comp->su, SA_AMF_OPERATIONAL_ENABLED);
				} else {
					su_operational_state_set (comp->su, SA_AMF_OPERATIONAL_DISABLED);
				}
				break;
			}
		case SA_AMF_OPERATIONAL_DISABLED:
			break;
		default:
			assert (0);
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
	switch (type) {
		case SA_AMF_PRESENCE_STATE:
			su_comp_presence_state_changed (su, comp, state);
			break;
		case SA_AMF_OP_STATE:
			su_comp_op_state_changed (su, comp, state);
			break;
		default:
			assert (0);
	}
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
	ENTER ("Comp '%s', SU '%s'", comp->name.value, su->name.value);

	switch (su->restart_control_state) {
		case SU_RC_ESCALATION_LEVEL_0:
 
			if (comp->saAmfCompRestartCount >= su->sg->saAmfSGCompRestartMax) {
				su->restart_control_state = SU_RC_ESCALATION_LEVEL_1;
				amf_su_comp_error_suspected (su, comp, recommended_recovery);
			} else {
				comp_restart (comp);
			}
			break;

		case SU_RC_ESCALATION_LEVEL_1:
			if (comp->saAmfCompRestartCount >= su->sg->saAmfSGCompRestartMax) {
				if (su->saAmfSURestartCount >= su->sg->saAmfSGSuRestartMax) {
					su->restart_control_state = SU_RC_ESCALATION_LEVEL_2;
					amf_su_comp_error_suspected (su, comp, recommended_recovery);
				} else {
					su_restart (comp->su);
				}
			} else {
				comp_restart (comp);
			}
			break;

		case SU_RC_ESCALATION_LEVEL_2:
			if (su->saAmfSURestartCount >= su->sg->saAmfSGSuRestartMax) {

				/*                                                              
				 * TODO: delegate to node
				*/
				SaNameT dn;

				su_operational_state_set (su, SA_AMF_OPERATIONAL_DISABLED);
				amf_comp_operational_state_set (
					comp, SA_AMF_OPERATIONAL_DISABLED);
				amf_comp_dn_make (comp, &dn);
				log_printf (LOG_NOTICE, "Error detected for '%s', recovery "
					"action:\n\t\tSU failover", dn.value);
				amf_sg_failover_su_req (comp->su->sg, comp->su, this_amf_node);
				return;
			} else {
				su_restart (comp->su);
			}
			break;

		default:
			break;
	}
}

void amf_su_init (void)
{
	log_init ("AMF");
}

void amf_su_terminate (struct amf_su *su)
{
	struct amf_comp *comp;

	ENTER ("'%s'", su->name.value);

	for (comp = su->comp_head; comp != NULL; comp = comp->next) {
		/* 
		 * Terminate all components in SU abruptly
		*/
		comp->error_suspected = 1;
		amf_comp_terminate (comp);
	}
}

char *amf_su_dn_make (struct amf_su *su, SaNameT *name)
{
	int i;

	assert (su != NULL);

	i = snprintf((char*) name->value, SA_MAX_NAME_LENGTH,
		"safSu=%s,safSg=%s,safApp=%s",
		su->name.value, su->sg->name.value, su->sg->application->name.value);
	assert (i <= SA_MAX_NAME_LENGTH);
	name->length = i;
	return (char *)name->value;
}

struct amf_si_assignment *amf_su_get_next_si_assignment (
	struct amf_su *su, const struct amf_si_assignment *si_assignment)
{
	struct amf_si *si;
	struct amf_si_assignment *tmp_si_assignment;
	SaNameT dn;

	amf_su_dn_make (su, &dn);

	if (si_assignment == NULL) {
		assert (su->sg);
		assert (su->sg->application);
		assert (su->sg->application->si_head);
		si = su->sg->application->si_head;
		tmp_si_assignment = si->assigned_sis;
	} else {
		tmp_si_assignment = si_assignment->next;
		if (tmp_si_assignment == NULL) {
			si = si_assignment->si->next;
			if (si == NULL) {
				return NULL;
			} else {
				tmp_si_assignment = si->assigned_sis;
			}
		} else {
			si = tmp_si_assignment->si;
		}
	}

	for (; si != NULL; si = si->next) {
		if (tmp_si_assignment == NULL && si != NULL) {
			tmp_si_assignment = si->assigned_sis;
		}
		for (; tmp_si_assignment != NULL;
			tmp_si_assignment = tmp_si_assignment->next) {

			if (name_match (&tmp_si_assignment->name, &dn)) {
				return tmp_si_assignment;
			}
		}
	}

	return NULL;
}

void amf_su_foreach_si_assignment (
	struct amf_su *su,
	void (*foreach_fn)(struct amf_su *su,
	struct amf_si_assignment *si_assignment))
{
	struct amf_si_assignment *si_assignment;

	assert (foreach_fn != NULL);
	si_assignment = amf_su_get_next_si_assignment (su, NULL);
	while (si_assignment != NULL) {
		foreach_fn (su, si_assignment);
		si_assignment = amf_su_get_next_si_assignment (su, si_assignment);
	}
}


int amf_su_get_saAmfSUNumCurrActiveSIs(struct amf_su *su)
{
	int cnt = 0;
	struct amf_si_assignment *si_assignment;

	si_assignment = amf_su_get_next_si_assignment (su, NULL); 
	while (si_assignment != NULL) {
		if (su->sg->avail_state == SG_AC_AssigningOnRequest &&
			si_assignment->requested_ha_state == SA_AMF_HA_ACTIVE) {
			cnt++;
		} else {
			if (si_assignment->saAmfSISUHAState == SA_AMF_HA_ACTIVE) {
				cnt++;
			}
		}
		si_assignment = amf_su_get_next_si_assignment (su, si_assignment);
	}

	return cnt;
}


int amf_su_get_saAmfSUNumCurrStandbySIs(struct amf_su *su)
{
	int cnt = 0;
	struct amf_si_assignment *si_assignment;

	si_assignment = amf_su_get_next_si_assignment (su, NULL); 
	while (si_assignment != NULL) {
		if (su->sg->avail_state == SG_AC_AssigningOnRequest &&
			si_assignment->requested_ha_state == SA_AMF_HA_STANDBY) {
			cnt++;
		} else {
			if (si_assignment->saAmfSISUHAState == SA_AMF_HA_STANDBY) {
				cnt++;
			}
		}
		si_assignment = amf_su_get_next_si_assignment (su, si_assignment);
	}

	return cnt;
}

SaAmfReadinessStateT amf_su_get_saAmfSUReadinessState (struct amf_su *su)
{
	if ((su->saAmfSUOperState == SA_AMF_OPERATIONAL_ENABLED) &&
		((su->saAmfSUPresenceState == SA_AMF_PRESENCE_INSTANTIATED) ||
		(su->saAmfSUPresenceState == SA_AMF_PRESENCE_RESTARTING))) {

		return SA_AMF_READINESS_IN_SERVICE;
	} else if (su->saAmfSUOperState == SA_AMF_OPERATIONAL_ENABLED) {
		return SA_AMF_READINESS_STOPPING;
	} else {
		return SA_AMF_READINESS_OUT_OF_SERVICE;
	}
}

/**
 * Constructor for SU objects. Adds SU last in the ordered
 * list owned by the specified SG. Always returns a
 * valid SU object, out-of-memory problems are handled here.
 * Default values are initialized.
 * @param sg
 * @param name
 * 
 * @return struct amf_su*
 */
struct amf_su *amf_su_new (struct amf_sg *sg, char *name)
{
	struct amf_su *tail = sg->su_head;
	struct amf_su *su = calloc (1, sizeof (struct amf_su));

	if (su == NULL) {
		openais_exit_error (AIS_DONE_OUT_OF_MEMORY);
	}

	while (tail != NULL) {
		if (tail->next == NULL) {
			break;
		}
		tail = tail->next;
	}

	if (tail == NULL) {
		sg->su_head = su;
	} else {
		tail->next = su;
	}
	su->sg = sg;

	/* setup default values from spec. */
	su->saAmfSURank = 0;
	su->saAmfSUIsExternal = 0;
	su->saAmfSUFailover = 1;
	su->saAmfSUAdminState = SA_AMF_ADMIN_UNLOCKED;
	su->saAmfSUOperState = SA_AMF_OPERATIONAL_DISABLED;
	su->saAmfSUPresenceState = SA_AMF_PRESENCE_UNINSTANTIATED;
	su->restart_control_state = SU_RC_ESCALATION_LEVEL_0;

	setSaNameT (&su->name, name);

	return su;
}

void amf_su_delete (struct amf_su *su)
{
	struct amf_comp *comp;

	for (comp = su->comp_head; comp != NULL;) {
		struct amf_comp *tmp = comp;
		comp = comp->next;
		amf_comp_delete (tmp);
	}

	free (su);
}

void *amf_su_serialize (struct amf_su *su, int *len)
{
	char *buf = NULL;
	int offset = 0, size = 0;

	TRACE8 ("%s", su->name.value);

	buf = amf_serialize_SaNameT (buf, &size, &offset, &su->name);
	buf = amf_serialize_SaUint32T (buf, &size, &offset, su->saAmfSURank);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, su->saAmfSUNumComponents);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, su->saAmfSUIsExternal);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, su->saAmfSUFailover);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, su->saAmfSUPreInstantiable);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, su->saAmfSUOperState);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, su->saAmfSUAdminState);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, su->saAmfSUPresenceState);
	buf = amf_serialize_SaNameT (buf, &size, &offset, &su->saAmfSUHostedByNode);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, su->saAmfSURestartCount);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, su->restart_control_state);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, su->escalation_level_history_state);
	buf = amf_serialize_SaStringT (
		buf, &size, &offset, su->clccli_path);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, su->su_failover_cnt);

	*len = offset;

	return buf;
}

struct amf_su *amf_su_deserialize (struct amf_sg *sg, char *buf, int size)
{
	char *tmp = buf;
	struct amf_su *su;

	su = amf_su_new (sg, "");

	tmp = amf_deserialize_SaNameT (tmp, &su->name);
	tmp = amf_deserialize_SaUint32T (tmp, &su->saAmfSURank);
	tmp = amf_deserialize_SaUint32T (tmp, &su->saAmfSUNumComponents);
	tmp = amf_deserialize_SaUint32T (tmp, &su->saAmfSUIsExternal);
	tmp = amf_deserialize_SaUint32T (tmp, &su->saAmfSUFailover);
	tmp = amf_deserialize_SaUint32T (tmp, &su->saAmfSUPreInstantiable);
	tmp = amf_deserialize_SaUint32T (tmp, &su->saAmfSUOperState);
	tmp = amf_deserialize_SaUint32T (tmp, &su->saAmfSUAdminState);
	tmp = amf_deserialize_SaUint32T (tmp, &su->saAmfSUPresenceState);
	tmp = amf_deserialize_SaNameT (tmp, &su->saAmfSUHostedByNode);
	tmp = amf_deserialize_SaUint32T (tmp, &su->saAmfSURestartCount);
	tmp = amf_deserialize_SaUint32T (tmp, &su->restart_control_state);
	tmp = amf_deserialize_SaUint32T (tmp, &su->escalation_level_history_state);
	tmp = amf_deserialize_SaStringT (tmp, &su->clccli_path);
	tmp = amf_deserialize_SaUint32T (tmp, &su->su_failover_cnt);

	return su;
}

struct amf_su *amf_su_find (struct amf_cluster *cluster, SaNameT *name)
{
	struct amf_application *app;
	struct amf_sg *sg;
	struct amf_su *su = NULL;
	char *app_name;
	char *sg_name;
	char *su_name;
	char *ptrptr;
	char *buf;

	assert (cluster != NULL && name != NULL);

	/* malloc new buffer since strtok_r writes to its first argument */
	buf = amf_malloc (name->length + 1);
	memcpy (buf, name->value, name->length + 1);

	su_name = strtok_r(buf, ",", &ptrptr);
	sg_name = strtok_r(NULL, ",", &ptrptr);
	app_name = strtok_r(NULL, ",", &ptrptr);

	if (su_name == NULL || sg_name == NULL || app_name == NULL) {
		goto end;
	}

	su_name += 6;
	sg_name += 6;
	app_name += 7;

	app = amf_application_find (cluster, app_name);
	if (app == NULL) {
		goto end;
	}

	for (sg = app->sg_head; sg != NULL; sg = sg->next) {
		if (strncmp (sg_name, (char*)sg->name.value,
			sg->name.length) == 0) {
			for (su = sg->su_head; su != NULL; su = su->next) {
				if (strncmp (su_name, (char*)su->name.value,
					su->name.length) == 0) {
					goto end;
				}
			}
		}
	}

end:
	free (buf);
	return su;
}

