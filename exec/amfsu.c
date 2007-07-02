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
 * the error escallation policy described in chapter 3.12.2 of the spec. It also
 * implements component restart and service unit restart as described in
 * paragraph 3.12.1.2 and 3.12.1.3.
 * RCSM contains three composite states.
 * Being a composite state means that the state contains substates.
 * RCSM composite states are:
 *  - IDLE (LEVEL_0, LEVEL_1 and LEVEL_2)
 *	- RESTARTING_COMPONENT (DEACTIVATING, RESTARTING, SETTING and ACTIVATING)
 *	- RESTARTING_SERVICE_UNIT (DEACTIVATING, TERMINATING, INSTANTIATING,
 *                             and ACTIVATING)
 * 
 * IDLE is a kind of state where no actions are performed and used only to
 * remember the escallation level. Substate LEVEL_0 indicates no escallation.
 * LEVEL_1 indicates that a component restart has been executed recently and the
 * escallation timer is still running. At this level component restart requests
 * will transition to RESTARTING_COMPONENT but if there are too many restart
 * requests before the probation timer expires then a transition will be made to
 * LEVEL_2 and the restart request will be forwarded to the node instance
 * hosting this component. State RESTARTING_SERVICE_UNIT will only be assumed if
 * the node explicitly requests the SU to execute a restart of itself (after
 * having evaluated its part of the error escallation policy).
 * 
* 1. Service Unit Restart Control State Machine
*  ============================================
 * 
 * 1.1  State Transition Table
 * ===========================
 * 
 * State:                  Event:                 Action:  New state:
 * ===========================================================================
 * IDLE_ESCALATION_x       comp_restart           A9	   RS_COMP_RESTARTING
 * IDLE_ESCALATION_x       su_restart             A20      RS_SU_TERMINATING
 * IDLE_ESCALATION_0       error_suspected        A1,A3    IDLE_ESCALATION_1
 * IDLE_ESCALATION_1       error_suspected [!C3]  A1,A3    IDLE_ESCALATION_1
 * IDLE_ESCALATION_1       error_suspected [C3]   A2,A5    IDLE_ESCALATION_2
 * IDLE_ESCALATION_2       error_suspected        A2       IDLE_ESCALATION_2
 * RS_COMP_RESTARTING      comp_instantiated      A11      RS_COMP_SETTING
 * RS_COMP_RESTARTING      comp_inst_failed       A14,A15  RS_COMP_T-ING_2
 * RS_COMP_RESTARTING      comp_term_failed       A19      IDLE_ESCALATION_x
 * RS_COMP_RESTARTING      error_suspected        A18      RS_COMP_RESTARTING
 * RS_COMP_T-ING_2         comp_uninst..ed [C8]   A16,A15  RS_COMP_T-ING_2
 * RS_COMP_T-ING_2         comp_uninst..ed [C100]          IDLE_ESCALATION_x
 * RS_COMP_T-ING_2         comp_uninst..ed [C101] A25      IDLE_ESCALATION_x
 * RS_COMP_T-ING_2         comp_uninst..ed [C102] A26      IDLE_ESCALATION_x
 * RS_COMP_T-ING_2         comp_term_failed [C100]         RS_COMP_T-ING_2
 * RS_COMP_T-ING_2         error_suspected        A18      RS_COMP_T-ING_2      
 * RS_COMP_SETTING         ha_state_assumed [C7]  A19      IDLE_ESCALATION_x
 * RS_COMP_SETTING         error_suspected        A18      RS_COMP_SETTING
 * RS_SU_TERMINATING       comp_uninst..ed [C8]   A16,A15  RS_SU_TERMINATING
 * RS_SU_TERMINATING       comp_uninst..ed [C103] A17,A23  RS_SU_INSTANTIATING
 * RS_SU_TERMINATING       comp_uninst..ed [C104] A19      IDLE_ESCALATION_x
 * RS_SU_TERMINATING       comp_term_failed [C104]A19      IDLE_ESCALATION_X
 * RS_SU_TERMINATING       error_suspected        A18      RS_SU_TERMINATING
 * RS_SU_INSTANTIATING     comp_instantiated [C14]A21,A22  RS_SU_INSTANTIATING
 * RS_SU_INSTANTIATING     comp_instantiated [C105]A15     RS_SU_T-ING_2
 * RS_SU_INSTANTIATING     comp_instantiated [C106]A11     RS_SU_SETTING
 * RS_SU_INSTANTIATING     comp_inst_failed [C105]A15      RS_SU_T-ING_2
 * RS_SU_INSTANTIATING     error_suspected        A18      RS_SU_INSTANTIATING
 * RS_SU_T-ING_2           comp_uninst..ed [C8]   A16,A15  RS_SU_T-ING_2
 * RS_SU_T-ING_2           comp_uninst..ed [C100]          IDLE_ESCALATION_x
 * RS_SU_T-ING_2           comp_uninst..ed [C101] A25      IDLE_ESCALATION_x
 * RS_SU_T-ING_2           comp_uninst..ed [C102] A26      IDLE_ESCALATION_x
 * RS_SU_T-ING_2           comp_term_failed [C100]         RS_SU_T-ING_2
 * RS_SU_T-ING_2           error_suspected        A18      RS_SU_T-ING_2
 * RS_SU_SETTING           ha_state_assumed [C10] A19      IDLE_ESCALATION_X
 * RS_SU_SETTING           error_suspected        A18      RS_SU_SETTING
 * 
 * 1.2 State Description
 * =====================
 * IDLE_ESCALATION_x - This is just an abbreviated notation for 
 *                     IDLE_ESCALATION_0, IDLE_ESCALATION_1 or IDLE_ESCALATION_2
 *                     When leaving any of the idle states, a history state
 *                     is used to save the (exact) state value. When returning
 *                     to idle, the value of the history state is used to set
 *                     the correct idle state.
 *
 * IDLE_ESCALATION_0 - SU_RC_IDLE_ESCALATION_LEVEL_0
 *                    Service unit is idle and the restart probation timer is
 *					  off.
 *
 * IDLE_ESCALATION_1 - SU_RC_IDLE_ESCALATION_LEVEL_1
 *                    Service unit is idle and the restart probation timer is
 *                    on. This indicates there has recently been an error
 *                    detected on at least one of its components which has been
 *                    recovered by a component restart but we are still in the
 *                    probation period which follows every restart.
 *
 * IDLE_ESCALATION_2 - SU_RC_IDLE_ESCALATION_LEVEL_2
 *                    Service unit is idle and handling on potential new error
 *                    indications on any of its components has been delegated
 *                    to the node object where the service unit is hosted.
 *
 * RS_COMP_DEACTIVATING - SU_RC_RESTART_COMP_DEACTIVATING
 *                       Service unit is busy handling restart of one of its
 *                       components. In this sub-state, the service unit is
 *                       waiting for acknowledgements that all components which
 *                       had csi-assignments that were dependent of csi-
 *                       assignments associated to the restarting component
 *                       have been de-activated. This is a neccesary step to
 *                       take before the component to restart is terminated,
 *                       to avoid that the csi or si dependency rules are
 *                       violated.
 *
 * RS_COMP_RESTARTING - SU_RC_RESTART_COMP_RESTARTING
 *                     Service unit is busy handling restart of one of its
 *                     components. In this sub-state, the service unit has
 *                     ordered one of its components to restart and waits for
 *                     the component to indicate that the restart is done.
 * 
 * RS_COMP_T-ING_2 - SU_RC_RESTART_COMP_TERMINATING_AFTER_INST_FAILED
 *					Service unit is busy handling restart of one of its
 *					components. In this sub-state, the restart of the component
 *					has failed and the rest of the components in the service 
 *					unit has to be terminated.
 *
 * RS_COMP_SETTING - SU_RC_RESTART_COMP_SETTING
 *                  Service unit is busy handling restart of one of its
 *                  components. In this sub-state, the service unit has ordered
 *                  the component that just have been restarted to re-assume
 *                  the HA-states it had before, provided none of the states
 *                  were ACTIVE. It waits for an acknowledgement that the
 *                  setting of the HA-states are done.
 * 
 * RS_COMP_ACTIVATING - SU_RC_RESTART_COMP_ACTIVATING
 *                     Service unit is busy handling restart of one of its
 *                     components. In this sub-state, the service unit has
 *                     ordered the component that just have been restarted to
 *                     re-assume the active HA-states it had before and also
 *                     to activate the csi-assignments that possibly were
 *                     de-activated because of this restart. The service unit
 *                     waits in this state for an acknowledgement of the
 *                     activation.
 *
 * RS_SU_DEACTIVATING - SU_RC_RESTART_SU_DEACTIVATING
 *                     Service unit is busy handling restart of all of its
 *                     components. In this sub-state, the service unit is
 *                     waiting for acknowledgements that all components which
 *                     had csi-assignments that were dependent of si-
 *                     assignments associated to this service unit
 *                     have been de-activated. This is a neccesary step to
 *                     take before all components of the service unit are
 *                     terminated, to avoid that the csi or si dependency rules
 *                     are violated.
 *
 * RS_SU_TERMINATING - SU_RC_RESTART_SU_TERMINATING
 *                    Service unit is busy handling restart of all of its
 *                    components. In this sub-state, the service unit has
 *                    ordered all its components to terminate and is waiting
 *                    for an acknowledgement that all components are done with
 *                    the termination.
 * 
 * RS_SU_INSTANTIATING - SU_RC_RESTART_SU_INSTANTIATING
 *                    Service unit is busy handling restart of all of its
 *                    components. In this sub-state, the service unit has
 *                    ordered all components to instantiate and is waiting
 *                    for an acknowledgement that all components are done with
 *                    the instantiation.
 *
 * RS_SU_T-ING_2 - SU_RC_RESTART_SU_TERMINATING_AFTER_INST_FAILED
 *					Service unit is busy handling restart of all of its
 *					components. In this sub-state, the instantiation at least
 *					one component has failed and the rest of the components in
 *					the service unit has to be terminated.
 *
 * RS_SU_SETTING -  SU_RC_RESTART_SU_SETTING
 *                  Service unit is busy handling restart of all of its
 *                  components. In this sub-state, the service unit has ordered
 *                  all components that just have been restarted to re-assume
 *                  the HA-states they had before, provided none of the states
 *                  were ACTIVE. The service unit waits for an acknowledgement
 *                  that the setting of the HA-states are done.
 *
 * RS_SU_ACTIVATING - SU_RC_RESTART_SU_ACTIVATING
 *                   Service unit is busy handling restart of all of its
 *                   components. In this sub-state, the service unit has
 *                   ordered all components that just have been restarted to
 *                   re-assume the active HA-states they had before and also
 *                   to activate the csi-assignments that possibly were
 *                   de-activated because of this restart. The service unit
 *                   waits in this state for an acknowledgement of the
 *                   activation.
 *
 * 1.3 Actions
 * ===========
 * A1  - generate event comp_restart
 * A2  - forward component restart request to the node which hosts current su
 * A3  - start probation timer (SaAmfSGCompRestartProb)
 * A4  - [foreach component in su]/ cnt += SaAmfSGCompRestartCount
 * A5  - stop probation timer
 * A6  - restart ??
 * A7  - set restarting_comp = component
 * A8  - [foreach csi-assignment assigned to component] SI deactivate csi
 * A9  - order component to restart
 * A10 - set restarting_comp == ALL
 * A11 - initiate setting of the same HA-state as was set before the restart
 * A12 - SI activate
 * A13 - [foreach si-assignment assigned to su] SI deactivate
 * A14 - set current instantiation level = highest level 
 * A15 - [foreach component on current instantiation level]/terminate component
 * A16 - current instantiation level is decremented
 * A17 - request the presence state state machine to instantiate the su
 * A18 - defer the event
 * A19 - recall deferred event
 * A20 - restart all components contained in current su
 * A21 - current instantiation level is incremented
 * A22 - [foreach component on current instantiation level]/instantiate
 *       component
 * A23 - set current instantiation level = lowest level
 * A24 - order SG to do component failover
 * A25 - order Node to do node failover
 * A26 - order SG to do SU failover
 *
 * 1.4 Guards
 * ==========
 * C1  - disableRestart == False
 * C2  - the component has been restarted less than SaAmfSGCompRestartMax times
 * C3  - the component has been restarted SaAmfSGCompRestartMax number of times
 * C4  - all si-assignments have confirmed-ha-state == QUIESCED or the
 *       operation failed flag set.
 * C5  - for each si-assignment related to the restarting component where
 *       requested-ha-state != confirmed-ha-state and requested-ha-state !=
 *       ACTIVE
 * C6  - - for each si-assignment related to the restarting component where
 *       requested-ha-state != confirmed-ha-state and requested-ha-state ==
 *       ACTIVE
 * C7  - all si-assignments related to the restarting component have
 *       requested-ha-state == confirmed-ha-state or has the operation failed
 *       flag set
 * C8  - all components on current instantiation level == UNINSTANTIATED
 * C9  - current instantiation level < lowest instantiation level
 * C10 - all si-assignments related to current service unit have
 *       requested-ha-state == confirmed-ha-state or the operation failed
 *       flag set.
 * C11 - for each si-assignment related to current su where
 *       requested-ha-state != confirmed-ha-state and requested-ha-state ==
 *       ACTIVE
 * C12 - for each si-assignment related to current su where
 *       requested-ha-state != confirmed-ha-state and requested-ha-state ==
 *       STANDBY
 * C13 - at least one component has presence state == TERMINATION_FAILED
 * C14 - all components on current instantiation level == INSTANTIATED,
 *       INSTANTIATION_FAILED or INSTANTIATION_FAILED_REBOOT_NODE
 * C15 - current instantiation level is highest
 * C16 - all components has presence state == INSTANTIATED
 * C17 - at least one component has presence state == INSTANTIATION_FAILED and
 *       it is not allowed to reboot the node because of this problem
 * C18 - at least one component has presence state == INSTANTIATION_FAILED and
 *       it is allowed to reboot the node to recover from this problem
 * C19 - all components in the SU permit restart
 *
 * 1.4.2 Composed Guards
 * =====================
 *
 * C100 - C9 & C13
 * C101 - C9 & C18
 * C102 - C9 & C17
 * C103 - C8 & C9 & !C13
 * C104 - C8 & C9 & C13
 * C105 - C14 & (C17 | C18)
 * C106 - C14 & C15 & C16
 *
 * 1.5 Events
 * ==========
 *
 * E1  - component restart request
 * E2  - restart
 * E3  - probation timer expired
 * E4  - escalation reverted
 * E5  - operation failed
 * E6  - deactivated
 * E7  - comp_state(prsm, INSTANTIATED)
 * E8  - comp_state(prsm, INSTANTIATION_FAILED)
 * E9  - comp_state(prsm, TERMINATION_FAILED)
 * E10 - si_state(ha-state)
 * E11 - activated
 * E12 - comp_state(prsm, UNINSTANTIATED)
 * E13 - 
 * E14 - 
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <errno.h>

#include "amf.h"
#include "util.h"
#include "print.h"
#include "main.h"

static int terminate_all_components_in_level (struct amf_su *su, 
	SaUint32T current_instantiation_level);
static int are_all_comps_in_level_uninst_or_term_failed (struct amf_su *su);
static int are_all_comps_in_level_instantiated (struct amf_su *su);
static int instantiate_all_components_in_level (struct amf_su *su, 
	SaUint32T current_instantiation_level);
static SaUint32T su_lowest_comp_instantiation_level_set (struct amf_su *su);
static void si_ha_state_assumed_cbfn (
	struct amf_si_assignment *si_assignment, int result);
static int is_any_component_instantiating (amf_su_t *su);

typedef struct su_event {
	amf_su_event_type_t event_type;
	amf_su_t *su;
	amf_comp_t *comp;
	SaAmfRecommendedRecoveryT recommended_recovery;
} su_event_t;

/******************************************************************************
 * Internal (static) utility functions
 *****************************************************************************/


static void su_event_set(struct amf_su *su, struct amf_comp *comp, 
	SaAmfRecommendedRecoveryT recommended_recovery,
	su_event_t *su_event, amf_su_event_type_t event_type)
{
	su_event->event_type = event_type;
	su_event->comp = comp;
	su_event->su = su;
	su_event->recommended_recovery = recommended_recovery;
}

static void su_defer_event (amf_su_t *su, amf_comp_t *comp, 
	SaAmfRecommendedRecoveryT recommended_recovery, 
	amf_su_event_type_t su_event_type)
{
	su_event_t event;
	su_event_set(su, comp, recommended_recovery,&event, su_event_type);

	ENTER("event_type = %d", event.event_type);
	amf_fifo_put (event.event_type, &event.su->deferred_events,
		sizeof (su_event_t), &event);
}

static void su_recall_deferred_events (amf_su_t *su)
{
	su_event_t su_event;

	ENTER ("%s", su->name.value);
	if (amf_fifo_get (&su->deferred_events, &su_event)) {
		switch (su_event.event_type) {
			case SU_COMP_ERROR_SUSPECTED_EV:
				amf_su_comp_error_suspected (su_event.su,su_event.comp,
					su_event.recommended_recovery);
				break;
			default:
				dprintf("event_type = %d", su_event.event_type);
				break;
		}
	}
}

static int has_component_restarted_max_times (amf_comp_t *comp, amf_su_t *su)
{
 return comp->saAmfCompRestartCount >= su->sg->saAmfSGCompRestartMax;
}

#ifdef COMPILE_OUT
static int has_su_restarted_max_times (amf_su_t *su)
{
	return su->saAmfSURestartCount >= su->sg->saAmfSGSuRestartMax;
}
#endif

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

/**
 * This function sets presence state to the specified value. It also has the
 * following intentional side effects:
 * - sets HA-state to unknown when presence state is set to UNINSTANTIATED
 * - reports the change of presence state to the sg in which su is contained
 *   when the new state is 'stable'
 * @param su
 * @param presence_state - new value of presence state
 */
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


	if (su->restart_control_state != SU_RC_RESTART_SU_SETTING &&
		su->restart_control_state != SU_RC_RESTART_COMP_RESTARTING) {
		amf_sg_su_state_changed (su->sg, su, SA_AMF_PRESENCE_STATE, 
			presence_state);
	}
}
static void enter_idle (struct amf_su *su)
{
  su->restart_control_state = su->escalation_level_history_state;
}

static void enter_idle_with_recall (struct amf_su *su)
{
	su->restart_control_state = su->escalation_level_history_state;
	su_recall_deferred_events (su);
}

/**
 * This function sets operational state to the specified value. It also has the
 * following side effects:
 * - sets the readiness state for su
 * - sets the readiness state for all components contained in the su
 * @param su
 * @param oper_state - new value of operational state
 */
void amf_su_operational_state_set (struct amf_su *su,
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

/**
 * This function creates a new csi-assignment object and initializes it. The
 * function also links the new csi-assignment object to the list of assignments
 * held by the specified csi object, sets a pointer to the specified component
 * and a pointer to the specified si-assignment.
 * @param comp
 * @param csi
 * @param si_assignment
 * @param ha_state - new value of ha-state
 */
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


static void comp_restart (struct amf_comp *comp)
{
	SaNameT dn;

	ENTER ("'%s'", comp->name.value);
	amf_comp_dn_make (comp, &dn);
	log_printf (LOG_NOTICE, "Error detected for '%s', recovery "
		"action: Component restart", dn.value);

	comp->su->restart_control_state = SU_RC_RESTART_COMP_DEACTIVATING;
	comp->su->restart_control_state = SU_RC_RESTART_COMP_RESTARTING;
	comp->su->escalation_level_history_state = SU_RC_IDLE_ESCALATION_LEVEL_1;
	amf_comp_restart (comp);
}

/**
 * Set the same HA-state as the before the restart to the SI-assignments
 * associated with current SU. As a side effect, this HA-state will also be set
 * to all components which are associated with the csi-assignments associated to
 * the specified su via its csi and si objects.
 * @param su
 * @param current_instantiation_level
 * 
 * @return - 1 if there were no components on the specified instantiation level
 */
static void reassume_ha_state(struct amf_su *su)
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

static int is_any_component_instantiating (amf_su_t *su)
{
	amf_comp_t *component;
	int any_component_instantiating = 0;
	for (component = su->comp_head; component != NULL; 
		  component = component->next) {
		if (component->saAmfCompPresenceState == 
			SA_AMF_PRESENCE_INSTANTIATING) {
			any_component_instantiating = 1;
			break;
		}
		
	}
	return any_component_instantiating;
}

static int is_any_component_terminating (amf_su_t *su)
{
	amf_comp_t *component;
	int any_component_terminating = 0;
	for (component = su->comp_head; component != NULL; 
		  component = component->next) {
		if (component->saAmfCompPresenceState == 
			SA_AMF_PRESENCE_TERMINATING) {
			any_component_terminating = 1;
			break;
		}
		
	}
	return any_component_terminating;
}


static int is_any_comp_instantiation_failed (amf_su_t *su)
{
	amf_comp_t *comp_;
	int comp_instantiation_failed = 0;

	for (comp_ = su->comp_head; comp_ != NULL; comp_ = comp_->next) {

		if (comp_->saAmfCompPresenceState == 
			SA_AMF_PRESENCE_INSTANTIATION_FAILED) {
			comp_instantiation_failed = 1;
			break;
		}
	}
	return comp_instantiation_failed;
}

static int is_any_comp_termination_failed (amf_su_t *su)
{
	amf_comp_t *comp_;
	int comp_instantiation_failed = 0;

	for (comp_ = su->comp_head; comp_ != NULL; comp_ = comp_->next) {

		if (comp_->saAmfCompPresenceState == 
			SA_AMF_PRESENCE_TERMINATION_FAILED) {
			comp_instantiation_failed = 1;
			break;
		}
	}
	return comp_instantiation_failed;
}

/**
 * Finds the component within the specified su that has the highest value of it
 * presence state. With current definition of values the highest value can also
 * be regarded as the 'worst' in the sence of capability to be assigned
 * workload. In the 'best' presence state (INSTANTIATED) the component is
 * immediately available to take workload while in the 'worst' state
 * (TERMINATION_FAILED) it can not take any workload before it has been manually
 * repaired.
 * @param su
 * 
 * @return - worst presence state
 */
static SaAmfPresenceStateT 	get_worst_comps_presence_state_in_su (amf_su_t *su)
{
	amf_comp_t *component;
	SaAmfPresenceStateT worst_presence_state = 0;

	for (component = su->comp_head; component != NULL; 
		  component = component->next) {
		if (component->saAmfCompPresenceState > worst_presence_state) {
			worst_presence_state = component->saAmfCompPresenceState;
		}
	}
	return worst_presence_state;
}

/**
 * 
 * @param su
 */
void su_history_state_set(struct amf_su *su, SaAmfPresenceStateT state)
{
	su->restart_control_state = su->escalation_level_history_state;
	su->saAmfSUPresenceState =  state;
}
/**
 * A component notifies its parent su that its presence state has changed.
 * @param su
 * @param comp - component which has changed its presence state
 * @param state - new value of presence state
 */
static void su_comp_presence_state_changed (struct amf_su *su, 
	struct amf_comp *comp, int state)
{
	ENTER ("'%s', '%s' %d %d", su->name.value, comp->name.value, state,
		su->restart_control_state);
	amf_node_t *node = amf_node_find (&comp->su->saAmfSUHostedByNode);
	switch (state) {
		case SA_AMF_PRESENCE_INSTANTIATED:
			switch (su->restart_control_state) {
				case SU_RC_IDLE_ESCALATION_LEVEL_1:
				case SU_RC_IDLE_ESCALATION_LEVEL_2:
				case SU_RC_IDLE_ESCALATION_LEVEL_0:
					if (!is_any_component_instantiating (su)) {
						if (are_all_comps_in_level_instantiated (su)) {
							if (instantiate_all_components_in_level (su, 
								++comp->su->current_comp_instantiation_level)) {
								/* All levels of instantiation is done */
								su_presence_state_set (comp->su, 
									SA_AMF_PRESENCE_INSTANTIATED);
							}
						} else {
							if (is_any_comp_instantiation_failed (su)) {
								su_presence_state_set (su, 
									SA_AMF_PRESENCE_INSTANTIATION_FAILED);

							} else {
								assert (0);
							}
						}
					}
					break;
				case SU_RC_RESTART_COMP_RESTARTING:
					su->restart_control_state = SU_RC_RESTART_COMP_SETTING;
					reassume_ha_state (comp->su);
					break;
				case SU_RC_RESTART_SU_INSTANTIATING:
					if (!is_any_component_instantiating(su)) {
						if (are_all_comps_in_level_instantiated (su)) {
							if (instantiate_all_components_in_level (su, 
								++comp->su->current_comp_instantiation_level)) {
								su->restart_control_state = SU_RC_RESTART_SU_SETTING;
								su_presence_state_set (comp->su, 
									SA_AMF_PRESENCE_INSTANTIATED);
								reassume_ha_state (comp->su);
							}
						} else if (is_any_comp_instantiation_failed (su)) {
							su->restart_control_state = 
								SU_RC_TERMINATING_AFTER_INSTANTIATION_FAILED;
							terminate_all_components_in_level (su,
								su->current_comp_instantiation_level);
						} else {
							assert (0);
						}
					}
					break;
				default:
					dprintf ("state %d", su->restart_control_state);
					assert (0);
					break;
			}
			break;
		case SA_AMF_PRESENCE_UNINSTANTIATED:
			switch (su->restart_control_state) {
				case SU_RC_IDLE_ESCALATION_LEVEL_0:
				case SU_RC_IDLE_ESCALATION_LEVEL_1:
				case SU_RC_IDLE_ESCALATION_LEVEL_2:
					if (!is_any_component_terminating (su)) {
						if (are_all_comps_in_level_uninst_or_term_failed (su)) {
							if (terminate_all_components_in_level (su,
								--su->current_comp_instantiation_level)) {
								su_presence_state_set (su,
									get_worst_comps_presence_state_in_su (su));
							} else {
								if (is_any_comp_termination_failed (su)) {
									su_presence_state_set (comp->su, 
										SA_AMF_PRESENCE_TERMINATION_FAILED);
								} else {
									assert (0);
								}
							}
						}
					}
					break;
				case SU_RC_RESTART_SU_INSTANTIATING:
					break;
				case SU_RC_RESTART_COMP_RESTARTING:
					break;
				case SU_RC_TERMINATING_AFTER_INSTANTIATION_FAILED:
					if (!is_any_component_terminating (su)) {
						if (terminate_all_components_in_level (su,
							--su->current_comp_instantiation_level)) {
							if (!is_any_comp_termination_failed (su)) {
								su_presence_state_set (su, 
									SA_AMF_PRESENCE_INSTANTIATION_FAILED);
								if (node->saAmfNodeRebootOnInstantiationFailure) {
									amf_node_failover(node);
								} else {
									amf_node_comp_failover_req(node, comp);
								}
								enter_idle (su);
							} else {
								if (!node->saAmfNodeRebootOnTerminationFailure) {
									su_presence_state_set (su,
										get_worst_comps_presence_state_in_su (su));
								} else {
									/* TODO Implement and request Node Failed Fast */
									;
								}
								enter_idle_with_recall (su);
							}
						}
					}
					break;
				case SU_RC_RESTART_SU_TERMINATING:
					if (!is_any_component_terminating (su)) {
						if (terminate_all_components_in_level (su,
							--su->current_comp_instantiation_level)) {
							if (!is_any_comp_termination_failed (su)) {
								su->restart_control_state = 
									SU_RC_RESTART_SU_INSTANTIATING;
								instantiate_all_components_in_level (su, 
									su_lowest_comp_instantiation_level_set (
									su));
							} else {
								if (!node->saAmfNodeRebootOnTerminationFailure) {
									su_presence_state_set (su,
										get_worst_comps_presence_state_in_su (su));
								} else {
									/* TODO Implement and request Node Failed Fast */
									;
								}
								enter_idle_with_recall (su);
							}
						}
					}
					break;
				default:
					dprintf ("state %d", su->restart_control_state);
					assert (0);
					break;
			}
			break;
		case SA_AMF_PRESENCE_INSTANTIATING:
			su_presence_state_set (comp->su,SA_AMF_PRESENCE_INSTANTIATING);
			break;
		case SA_AMF_PRESENCE_RESTARTING:
			if (amf_su_are_all_comps_in_su (su, SA_AMF_PRESENCE_RESTARTING)) {
				su_presence_state_set (comp->su, SA_AMF_PRESENCE_RESTARTING);
			}
			break;
		case SA_AMF_PRESENCE_TERMINATING:
			su_presence_state_set (comp->su, SA_AMF_PRESENCE_TERMINATING);
			break;
		case SA_AMF_PRESENCE_INSTANTIATION_FAILED:
			switch (su->restart_control_state) {
				case SU_RC_IDLE_ESCALATION_LEVEL_0:
				case SU_RC_IDLE_ESCALATION_LEVEL_1:
				case SU_RC_IDLE_ESCALATION_LEVEL_2:
					if (!is_any_component_instantiating (su)) {
						su_presence_state_set (su, 
							SA_AMF_PRESENCE_INSTANTIATION_FAILED);
					}
					break;
				case SU_RC_RESTART_COMP_RESTARTING:
					su->restart_control_state = 
						SU_RC_TERMINATING_AFTER_INSTANTIATION_FAILED;
					amf_su_terminate (su);
					break;
				case SU_RC_RESTART_SU_INSTANTIATING:
					if (!is_any_component_instantiating (su)) {
						su->restart_control_state = 
							SU_RC_TERMINATING_AFTER_INSTANTIATION_FAILED;
						su_presence_state_set (su, 
							SA_AMF_PRESENCE_INSTANTIATION_FAILED);
						terminate_all_components_in_level (su,
							su->current_comp_instantiation_level);
					}
					break;
				default:
					dprintf ("state %d", su->restart_control_state);
					assert (0);
					break;
			}
			break;
		case SA_AMF_PRESENCE_TERMINATION_FAILED:
			switch (su->restart_control_state) {
				case SU_RC_IDLE_ESCALATION_LEVEL_0:
				case SU_RC_IDLE_ESCALATION_LEVEL_1:
				case SU_RC_IDLE_ESCALATION_LEVEL_2:
					break;
				case SU_RC_RESTART_COMP_RESTARTING:
					if (!node->saAmfNodeRebootOnTerminationFailure) {
						su_presence_state_set (su,
							SA_AMF_PRESENCE_TERMINATION_FAILED);
						enter_idle_with_recall (su);
					} else {
						/* TODO Implement and request Node Failed Fast */
						;
					}
					break;
				case SU_RC_TERMINATING_AFTER_INSTANTIATION_FAILED:
				case SU_RC_RESTART_SU_TERMINATING:
					if (!is_any_component_terminating (su)) {
						if (terminate_all_components_in_level (su,
							--su->current_comp_instantiation_level)) {
							if (!node->saAmfNodeRebootOnTerminationFailure) {
								su_presence_state_set (su,
									get_worst_comps_presence_state_in_su (su));
								enter_idle_with_recall (su);
							} else {
								/* TODO Implement and request Node Failed Fast */
								;
							}
						}
					}
					break;
				default:
					log_printf (LOG_LEVEL_NOTICE,"%s %d",su->name.value, 
						su->restart_control_state);
					dprintf ("state %d", su->restart_control_state);
					assert (0);
					break;
			}
			break;
		default:
			assert (0);
			break;
	}
}

/**
 * A component notifies its parent su that its operational state has changed.
 * @param su
 * @param comp - component which has changed its operational state
 * @param state - new value of operational state
 */
static void su_comp_op_state_changed (
	struct amf_su *su, struct amf_comp *comp, int state)
{
	ENTER ("'%s', '%s' %d", su->name.value, comp->name.value, state);

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
					amf_su_operational_state_set (comp->su, 
						SA_AMF_OPERATIONAL_ENABLED);
				} else {
					amf_su_operational_state_set (comp->su, 
						SA_AMF_OPERATIONAL_DISABLED);
				}
				break;
			}
		case SA_AMF_OPERATIONAL_DISABLED:
			amf_su_operational_state_set (comp->su, SA_AMF_OPERATIONAL_DISABLED);
			break;
		default:
			assert (0);
			break;
	}
	return;
}

/**
 * Instantiates all components on specified instantiation level.
 * @param su
 * @param current_instantiation_level
 * 
 * @return - 1 if there were no components on the specified instantiation level
 */
static int instantiate_all_components_in_level (struct amf_su *su, 
	SaUint32T current_instantiation_level)
{
	amf_comp_t *comp;
	SaUint32T all_components_instantiated = 1;

	for (comp = su->comp_head; comp != NULL; comp = comp->next) {
		if (su->current_comp_instantiation_level == 
			comp->saAmfCompInstantiationLevel) {
			all_components_instantiated = 0;
			amf_comp_instantiate (comp);
		}
	}
	return all_components_instantiated;
}

static int are_all_comps_in_level_instantiated (struct amf_su *su)
{
	SaUint32T level = su->current_comp_instantiation_level;
	amf_comp_t *comp;
	int all = 1;

	for (comp = su->comp_head; comp != NULL; comp = comp->next) {
		if (level == comp->saAmfCompInstantiationLevel) {
			if (comp->saAmfCompPresenceState != SA_AMF_PRESENCE_INSTANTIATED) {
				all = 0;
				break;
			}
		}
	}

	return all;
}


static int are_all_comps_in_level_uninst_or_term_failed(
	struct amf_su *su)
{
	SaUint32T level = su->current_comp_instantiation_level;
	amf_comp_t *comp;
	int all = 1;

	for (comp = su->comp_head; comp != NULL; comp = comp->next) {
		if (level == comp->saAmfCompInstantiationLevel) {
			if (comp->saAmfCompPresenceState != SA_AMF_PRESENCE_UNINSTANTIATED &&
				comp->saAmfCompPresenceState != SA_AMF_PRESENCE_TERMINATION_FAILED) {
				all = 0;
				break;
			}
		}
	}

	return all;
}
static void su_rc_enter_idle_escalation_level_1 (amf_comp_t *component,
	SaAmfRecommendedRecoveryT recommended_recovery)
{
	ENTER("");
	component->su->restart_control_state = SU_RC_IDLE_ESCALATION_LEVEL_1;
	if (has_component_restarted_max_times (component, component->su)) {
		component->su->restart_control_state = SU_RC_IDLE_ESCALATION_LEVEL_2;
		amf_su_comp_error_suspected (component->su, component, recommended_recovery);
	} else {
		comp_restart (component);
	}
}
static void su_rc_enter_idle_escalation_level_2 (amf_comp_t *component,
	SaAmfRecommendedRecoveryT recommended_recovery)
{
	ENTER("");
	component->su->restart_control_state = SU_RC_IDLE_ESCALATION_LEVEL_2;
	amf_node_t *node = amf_node_find (&component->su->saAmfSUHostedByNode);
	amf_node_comp_restart_req (node, component); 
}
static int get_instantiation_max_level (amf_su_t *su)
{
	amf_comp_t *comp;
	int instantiation_level = 0;
	for (comp = su->comp_head; comp != NULL; comp = comp->next) {
		if (comp->saAmfCompInstantiationLevel > instantiation_level) {
		   instantiation_level =  comp->saAmfCompInstantiationLevel;
		}
	}
	return instantiation_level;
}
/**
 * Initiates the termination of all components which have the specified
 * instantiation level.
 * @param su
 * @param current_instantiation_level
 * 
 * @return int -1 if no component has the specified instantiation level
 */
static int terminate_all_components_in_level (struct amf_su *su, 
	SaUint32T current_instantiation_level)
{
	amf_comp_t *comp;
	int all_components_in_level = 1;
	TRACE8("terminate comp->saAmfCompInstantiationLevel=%u", 
		current_instantiation_level);
	for (comp = su->comp_head; comp != NULL; comp = comp->next) {
		/* 
         * Terminate all components in instantiation level in SU
         * abruptly.
         */
		if (comp->saAmfCompInstantiationLevel == current_instantiation_level) {
			amf_comp_error_suspected_set (comp);
			amf_comp_terminate (comp);
			all_components_in_level = 0;
		}
	}
	return all_components_in_level;
}
/**
 * su_current_instantiation_level_init
 * @param su
 * 
 * @return SaUint32T - the value of the instantiation level which has been set
 */
static SaUint32T su_lowest_comp_instantiation_level_set (struct amf_su *su)
{
	amf_comp_t *component = su->comp_head;
	int comp_instantiation_level = component->saAmfCompInstantiationLevel;
	for (; component != NULL; component = component->next) {
		TRACE1("component->saAmfCompInstantiationLevel=%d",
			component->saAmfCompInstantiationLevel);

			if (component->saAmfCompInstantiationLevel < 
				comp_instantiation_level) {
				comp_instantiation_level = 
					component->saAmfCompInstantiationLevel;
			}
	}
	su->current_comp_instantiation_level = comp_instantiation_level;
	return comp_instantiation_level;
}


/**
 * An order to SU to instantiate its components.
 * @param su
 * 
 * @return int - 1 if its state allows it to request its contained components to
 *               instantiate or its state indicates that its components are in
 *               the process of instantiation.
 */
int amf_su_instantiate (struct amf_su *su)
{
	int is_instantiating = 1;

	ENTER ("'%s %d'", su->name.value, su->saAmfSUPresenceState);
	switch (su->saAmfSUPresenceState) {
		case SA_AMF_PRESENCE_UNINSTANTIATED:
			instantiate_all_components_in_level(su, 
				su_lowest_comp_instantiation_level_set (su));
			break;
		case SA_AMF_PRESENCE_RESTARTING:
		case SA_AMF_PRESENCE_INSTANTIATING:
			break;
		case SA_AMF_PRESENCE_INSTANTIATED:
		case SA_AMF_PRESENCE_TERMINATING:
		case SA_AMF_PRESENCE_INSTANTIATION_FAILED:
		case SA_AMF_PRESENCE_TERMINATION_FAILED:
			is_instantiating = 0;
			break;
		default:
			assert (0);
			break;
		
	}
	return is_instantiating;
}

/**
 * An order to SU to terminate its components.
 * @param su
 */
void amf_su_terminate (struct amf_su *su)
{
	ENTER ("'%s'", su->name.value);
	su->current_comp_instantiation_level = get_instantiation_max_level (su);

	terminate_all_components_in_level (su, su->current_comp_instantiation_level);
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
	ENTER ("Comp '%s', SU '%s' %d", comp->name.value, su->name.value,
		su->restart_control_state);

	switch (su->restart_control_state) {
		case SU_RC_IDLE_ESCALATION_LEVEL_0:
				su_rc_enter_idle_escalation_level_1 (comp,
					recommended_recovery);
			break;

		case SU_RC_IDLE_ESCALATION_LEVEL_1:
			if (has_component_restarted_max_times (comp, su)) {
				su_rc_enter_idle_escalation_level_2 (comp,
					recommended_recovery);
			} else {
				comp_restart (comp);
			}
			break;
		case SU_RC_IDLE_ESCALATION_LEVEL_2: {
				amf_node_t *node = amf_node_find (&comp->su->saAmfSUHostedByNode);
				amf_node_comp_restart_req (node, comp); 

#ifdef COMPILE_OUT
				if (su->saAmfSURestartCount >= su->sg->saAmfSGSuRestartMax) {

					/*
					 * TODO: delegate to node
					*/
					SaNameT dn;
					amf_comp_operational_state_set (comp, 
						SA_AMF_OPERATIONAL_DISABLED);
					amf_su_operational_state_set (su, 
						SA_AMF_OPERATIONAL_DISABLED);
					
					amf_comp_dn_make (comp, &dn);
					log_printf (LOG_NOTICE, "Error detected for '%s', recovery "
						"action:\n\t\tSU failover", dn.value);
					amf_sg_failover_su_req (comp->su->sg, comp->su, this_amf_node);
					return;
				} else {
					su_restart (comp->su);
				}
#endif
				break;
			}
		case SU_RC_RESTART_SU_SETTING:
		case SU_RC_RESTART_COMP_RESTARTING:
		case SU_RC_RESTART_COMP_SETTING:
			/* TODO: Complete the implementation of SU defer event */
			su_defer_event (su, comp, recommended_recovery,
				SU_COMP_ERROR_SUSPECTED_EV); 
			break;
		default:
			dprintf ("restart_control_state = %d",su->restart_control_state);
			break;
	}
}

/**
 * An order to SU to unconditionally restart itself.
 * @param su
 */
void amf_su_restart (struct amf_su *su)
{
	SaNameT dn;

	ENTER ("'%s'", su->name.value);

	amf_su_dn_make (su, &dn);
	log_printf (LOG_NOTICE, "Error detected for '%s', recovery "
		"action: SU restart", dn.value);

	su->restart_control_state = SU_RC_RESTART_SU_DEACTIVATING;
	su->restart_control_state = SU_RC_RESTART_SU_TERMINATING;
	su->escalation_level_history_state = SU_RC_IDLE_ESCALATION_LEVEL_2;
	su->current_comp_instantiation_level = get_instantiation_max_level (su);
	su->saAmfSURestartCount += 1;
	terminate_all_components_in_level(su, su->current_comp_instantiation_level);
}

/******************************************************************************
 * Event response methods
 *****************************************************************************/

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
			break;
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
				break;
		}
		enter_idle_with_recall (si_assignment->su);
	}
}

/******************************************************************************
 * General methods
 *****************************************************************************/

void amf_su_init (void)
{
	log_init ("AMF");
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
	struct amf_su *su = amf_calloc (1, sizeof (struct amf_su));

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
	su->restart_control_state = SU_RC_IDLE_ESCALATION_LEVEL_0;
	su->current_comp_instantiation_level = 0;
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
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, su->current_comp_instantiation_level);

	*len = offset;

	return buf;
}

struct amf_su *amf_su_deserialize (struct amf_sg *sg, char *buf)
{
	char *tmp = buf;
	struct amf_su *su = amf_su_new (sg, "");

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
	tmp = amf_deserialize_SaUint32T (tmp, &su->current_comp_instantiation_level);

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
				if (su->name.length == strlen(su_name) && 
					strncmp (su_name, (char*)su->name.value,
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

/**
 * This function makes a distinguished name for specified su object.
 * @param su
 * @param name -[out] pointer to where the distinguished name shall be stored
 * 
 * @return SaNameT* - distinguished name
 */
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

/**
 * An order to SU to create an si-assignment object with a specified HA-state
 * between it self and a specified si. The created si-assignment is initialized
 * and linked to list of assignments held by the specified si.
 * This function also orders creation of all csi-assignments required
 * considering the cs-types specified for the components and csi objects
 * respectively.
 * @param su
 * @param si
 * @param ha_state
 * 
 * @return amf_si_assignment_t*
 */
amf_si_assignment_t *amf_su_assign_si (struct amf_su *su, struct amf_si *si,
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
	return si_assignment;
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

/**
 * This function calculates the number of si-assignments with active HA-state
 * which currently are associated with the specified su.
 * TODO: Split into two functions and remove dependency to sg's avail_state
 * @param su
 * 
 * @return int 
 */
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

/**
 * This function calculates the number of si-assignments with standby HA-state
 * which currently are associated with the specified su.
 * TODO: Split into two functions and remove dependency to sg's avail_state
 * @param su
 * 
 * @return int 
 */
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

/**
 * This function calculates the readiness state for specified su
 * @param su
 * 
 * @return SaAmfReadinessStateT 
 */
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
 * Determine if all components have the specified HA-state.
 * @param su
 * @param state -specified HA-state
 * 
 * @return int - return 0 if not all components have the specified HA-state
 */
int amf_su_are_all_comps_in_su (struct amf_su *su,
	SaAmfPresenceStateT state)
{
	int all_comps_in_su_are_set = 1;
	amf_comp_t *component;
	for (component = su->comp_head; component != NULL; 
		  component = component->next) {

		if (component->saAmfCompPresenceState != state) {
			all_comps_in_su_are_set = 0;
		}
	}
	return all_comps_in_su_are_set;
}

