/** @file amfsg.c
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

static void acsm_enter_activating_standby (struct amf_sg *sg);
static void delete_si_assignments_in_scope (struct amf_sg *sg);
static void acsm_enter_repairing_su (struct amf_sg *sg);
static void standby_su_activated_cbfn (
	struct amf_si_assignment *si_assignment, int result);

static void dependent_si_deactivated_cbfn (
	struct amf_si_assignment *si_assignment, int result);

static const char *sg_recovery_type_text[] = {
	"Unknown",
	"FailoverSU",
	"FailoverNode"
};

static void return_to_idle (struct amf_sg *sg)
{
	SaNameT dn;

	ENTER ("sg: %s state: %d", sg->name.value,sg->avail_state);

	sg->avail_state = SG_AC_Idle;
	if (sg->recovery_scope.recovery_type != 0) {
		switch (sg->recovery_scope.recovery_type) {
			case SG_RT_FailoverSU:
				assert (sg->recovery_scope.sus[0] != NULL);
				amf_su_dn_make (sg->recovery_scope.sus[0], &dn);
				log_printf (
					LOG_NOTICE, "'%s' %s recovery action finished",
					dn.value,
					sg_recovery_type_text[sg->recovery_scope.recovery_type]);

				break;
			case SG_RT_FailoverNode:
				log_printf (
					LOG_NOTICE, "'%s for %s' recovery action finished",
					sg_recovery_type_text[sg->recovery_scope.recovery_type],
					sg->name.value);
				break;
			default:
				log_printf (
					LOG_NOTICE, "'%s' recovery action finished",
					sg_recovery_type_text[0]);
		}
	}

	if (sg->recovery_scope.sus != NULL) {
		free ((void *)sg->recovery_scope.sus);
	}
	if (sg->recovery_scope.sis != NULL) {
		free ((void *)sg->recovery_scope.sis);
	}
	memset (&sg->recovery_scope, 0, sizeof (struct sg_recovery_scope));
	sg->node_to_start = NULL;
}

static int su_instantiated_count (struct amf_sg *sg)
{
	int cnt = 0;
	struct amf_su *su;

	for (su = sg->su_head; su != NULL; su = su->next) {
		if (su->saAmfSUPresenceState == SA_AMF_PRESENCE_INSTANTIATED)
			cnt++;
	}

	return cnt;
}

static int has_any_su_in_scope_active_workload (struct amf_sg *sg)
{
	struct amf_su **sus= sg->recovery_scope.sus;
	struct amf_si_assignment *si_assignment;

	while (*sus != NULL) {
		si_assignment = amf_su_get_next_si_assignment (*sus, NULL);
		while (si_assignment != NULL) {
			if (si_assignment->saAmfSISUHAState != SA_AMF_HA_ACTIVE) {
				break;
			}
			si_assignment = amf_su_get_next_si_assignment (
				*sus, si_assignment);
		}
		if (si_assignment != NULL) {
			break;
		}
		sus++;
	}
	return(*sus == NULL);
}

static int is_standby_for_non_active_si_in_scope (struct amf_sg *sg)
{
	struct amf_si **sis= sg->recovery_scope.sis;
	struct amf_si_assignment *si_assignment;

	/*
	 * Check if there is any si in the scope which has no active assignment
	 * and at least one standby assignment.
	 */
	while (*sis != NULL) {
		si_assignment = (*sis)->assigned_sis;
		while (si_assignment != NULL) {
			if (si_assignment->saAmfSISUHAState == SA_AMF_HA_ACTIVE) {
				break;
			}
			si_assignment = si_assignment->next;
		}
		if (si_assignment == NULL) {
			/* There is no ACTIVE assignment ..*/
			si_assignment = (*sis)->assigned_sis;
			while (si_assignment != NULL) {
				if (si_assignment->saAmfSISUHAState == SA_AMF_HA_STANDBY) {
					break;
				}
				si_assignment = si_assignment->next;
			}
			if (si_assignment != NULL) {
				/* .. and one STANDBY assignment*/
				break;
			}
		}
		sis++;
	}
	return(*sis != NULL);
}

static void acsm_enter_terminating_suspected (struct amf_sg *sg)
{
	struct amf_su **sus= sg->recovery_scope.sus;

	sg->avail_state = SG_AC_TerminatingSuspected;
					/*                                                              
	* Terminate suspected SU(s)
	*/
	while (*sus != 0) {
		amf_su_terminate (*sus);
		sus++;
	}
}


/**
 * Callback function used by SI when there is no dependent SI to
 * deactivate.
 * @param sg
 */
static void dependent_si_deactivated_cbfn2 (struct amf_sg *sg)
{
	struct amf_su **sus = sg->recovery_scope.sus;
	ENTER("'%s'", sg->name.value);

	/* Select next state depending on if some SU in the scope is
	 * needs to be terminated.
	 */
	while (*sus != NULL) {
		ENTER("SU %s pr_state='%d'",(*sus)->name.value,
			(*sus)->saAmfSUPresenceState);
		if (((*sus)->saAmfSUPresenceState == 
			SA_AMF_PRESENCE_UNINSTANTIATED) ||
			((*sus)->saAmfSUPresenceState == 
			SA_AMF_PRESENCE_TERMINATION_FAILED) ||
			((*sus)->saAmfSUPresenceState == 
			SA_AMF_PRESENCE_INSTANTIATION_FAILED)) {
			sus++;
			continue;
		}
		break;
	}

	if (*sus != NULL) {
		acsm_enter_terminating_suspected (sg);
	} else {
		delete_si_assignments_in_scope(sg);         
		acsm_enter_activating_standby (sg);
	}

}

static void timer_function_dependent_si_deactivated2 (void *sg)
{

	ENTER ("");
	dependent_si_deactivated_cbfn2 (sg);
}


static struct amf_si *si_get_dependent (struct amf_si *si) 
{

	struct amf_si *tmp_si = NULL;
	ENTER("'%p'",si->depends_on);
	if (si->depends_on != NULL) {

		if (si->depends_on->name.length < SA_MAX_NAME_LENGTH) {
			si->depends_on->name.value[si->depends_on->name.length] = '\0';  
		}
		SaNameT res_arr[2];
		int is_match;


		is_match = sa_amf_grep ((char*)si->depends_on->name.value, 
			"safDepend=.*,safSi=(.*),safApp=.*", 
			2, res_arr);

		if (is_match) {
			tmp_si = amf_si_find (si->application, (char*)res_arr[1].value);
		} else {
			log_printf (LOG_LEVEL_ERROR, "distinguished name for "
				"amf_si_depedency failed\n");
			openais_exit_error (AIS_DONE_FATAL_ERR);
		}
	}
	return tmp_si;
}

struct amf_si *amf_dependent_get_next (struct amf_si *si, 
	struct amf_si *si_iter) 
{
	struct amf_si *tmp_si;
	struct amf_application *application;

	ENTER("");
	if (si_iter == NULL) {
		assert(amf_cluster != NULL);
		application = amf_cluster->application_head;
		assert(application != NULL);    
		tmp_si = application->si_head;
	} else {
		tmp_si = si_iter->next;
		if (tmp_si == NULL) {
			application = si->application->next;
			if (application == NULL) {
				goto out;
			}
		}
	}

	for (; tmp_si != NULL; tmp_si = tmp_si->next) {
		struct amf_si *depends_on_si = si_get_dependent (tmp_si);
		while (depends_on_si != NULL) {
			if (depends_on_si == si) {
				goto out;
			}
			depends_on_si = depends_on_si->next;
		}
	}
	out:
	return tmp_si;
}

static void acsm_enter_deactivating_dependent_workload (struct amf_sg *sg)
{
	struct amf_si **sis= sg->recovery_scope.sis;
	struct amf_si_assignment *si_assignment;
	int callback_pending = 0;

	sg->avail_state = SG_AC_DeactivatingDependantWorkload;

	ENTER("'%s'",sg->name.value);
	/*                                                              
	 * For each SI in the recovery scope, find all active assignments
	 * and request them to be deactivated.
	 */
	while (*sis != NULL) {
		struct amf_si *dependent_si;
		struct amf_si *si = *sis;
		si_assignment = si->assigned_sis;
		dependent_si = amf_dependent_get_next (si, NULL);   

		while (dependent_si != NULL) {
			si_assignment = dependent_si->assigned_sis;

			while (si_assignment != NULL) {

				if (si_assignment->saAmfSISUHAState == SA_AMF_HA_ACTIVE) {
					si_assignment->requested_ha_state = SA_AMF_HA_QUIESCED;
					callback_pending = 1;
					amf_si_ha_state_assume (
						si_assignment, dependent_si_deactivated_cbfn);
				}
				si_assignment = si_assignment->next;
			}
			dependent_si = amf_dependent_get_next (si, dependent_si);   
		}
		sis++;
	}

	if (callback_pending == 0) {
		poll_timer_handle handle;
		ENTER("");
		poll_timer_add (aisexec_poll_handle, 0, sg,
			timer_function_dependent_si_deactivated2, &handle);
	}
}
/**
 * Enter function for state SG_AC_ActivatingStandby. It activates
 * one STANDBY assignment for each SI in the recovery scope.
 * @param sg
 */
static void acsm_enter_activating_standby (struct amf_sg *sg)
{
	struct amf_si **sis= sg->recovery_scope.sis;
	struct amf_si_assignment *si_assignment;
	int is_no_standby_activated = 1;
	ENTER("'%s'",sg->name.value); 
	sg->avail_state = SG_AC_ActivatingStandby;

	/*                                                              
	 * For each SI in the recovery scope, find one standby
	 * SI assignment and activate it.
	 */
	while (*sis != NULL) {
		si_assignment = (*sis)->assigned_sis;
		while (si_assignment != NULL) {
			if (si_assignment->saAmfSISUHAState == SA_AMF_HA_STANDBY) {
				si_assignment->requested_ha_state = SA_AMF_HA_ACTIVE;
				amf_si_ha_state_assume (
					si_assignment, standby_su_activated_cbfn);
				is_no_standby_activated = 0;
				break;
			}
			si_assignment = si_assignment->next;
		}
		sis++;
	}

	if (is_no_standby_activated) {
		sg->avail_state = SG_AC_AssigningStandbyToSpare;
		acsm_enter_repairing_su (sg);	
	}
}

static void acsm_enter_repairing_su (struct amf_sg *sg)
{
	struct amf_su **sus= sg->recovery_scope.sus;
	ENTER("'%s'",sg->name.value);
	sg->avail_state = SG_AC_ReparingSu;
	int is_any_su_instantiated = 0;
	/*                                                              
	 * Instantiate SUs in current recovery scope until the configured
	 * preference is fulfiled.
	 */
	while (*sus != NULL) {
		if (su_instantiated_count ((*sus)->sg) <
			(*sus)->sg->saAmfSGNumPrefInserviceSUs) {
			struct amf_node *node = amf_node_find(&((*sus)->saAmfSUHostedByNode));
			if (node == NULL) {
				log_printf (LOG_LEVEL_ERROR, "no node to hosted on su found"
					"amf_si_depedency failed\n");
				openais_exit_error (AIS_DONE_FATAL_ERR);
			}
			if (node->saAmfNodeOperState == SA_AMF_OPERATIONAL_ENABLED) {
				is_any_su_instantiated = 1;	
				amf_su_instantiate ((*sus));
			} else {
				return_to_idle (sg);
			}

		}
		sus++;
	}
	if (is_any_su_instantiated == 0) {
		return_to_idle (sg);
	}
}

/**
 * Checks if the si pointed out is already in the scope.
 * @param sg
 * @param si
 */
static int is_si_in_scope(struct amf_sg *sg, struct amf_si *si)
{
	struct amf_si **tmp_sis= sg->recovery_scope.sis;

	while (*tmp_sis != NULL) {
		if (*tmp_sis == si) {
			break;
		}
		tmp_sis++;
	}
	return(*tmp_sis == si);
}

/**
 * Adds the si pointed out to the scope.
 * @param sg
 * @param si
 */
static void add_si_to_scope ( struct amf_sg *sg, struct amf_si *si)
{
	int number_of_si = 2; /* It shall be at least two */
	struct amf_si **tmp_sis= sg->recovery_scope.sis;

	ENTER ("'%s'", si->name.value);

	while (*tmp_sis != NULL) {
		number_of_si++;
		tmp_sis++;
	}

	sg->recovery_scope.sis = (struct amf_si **)
	realloc((void *)sg->recovery_scope.sis, 
		sizeof (struct amf_si *)*number_of_si);
	assert (sg->recovery_scope.sis != NULL);

	tmp_sis= sg->recovery_scope.sis;
	while (*tmp_sis != NULL) {
		tmp_sis++;
	}

	*tmp_sis = si;
	*(++tmp_sis) = NULL;
}
/**
 * Adds the ssu pointed out to the scope.
 * @param sg
 * @param su
 */
static void add_su_to_scope (struct amf_sg *sg, struct amf_su *su)
{
	int number_of_su = 2; /* It shall be at least two */
	struct amf_su **tmp_sus= sg->recovery_scope.sus;

	ENTER ("'%s'", su->name.value);
	while (*tmp_sus != NULL) {
		number_of_su++;
		tmp_sus++;
	}
	sg->recovery_scope.sus = (struct amf_su **)
	realloc((void *)sg->recovery_scope.sus, 
		sizeof (struct amf_su *)*number_of_su);
	assert (sg->recovery_scope.sus != NULL);

	tmp_sus= sg->recovery_scope.sus;
	while (*tmp_sus != NULL) {
		tmp_sus++;
	}

	*tmp_sus = su;
	*(++tmp_sus) = NULL; 
}

/**
 * Set recovery scope for failover SU.
 * @param sg
 * @param su
 */

static void set_scope_for_failover_su (struct amf_sg *sg, struct amf_su *su)
{
	struct amf_si_assignment *si_assignment;
	struct amf_si **sis;
	struct amf_su **sus;
	SaNameT dn;
	sg->recovery_scope.recovery_type = SG_RT_FailoverSU;


	sg->recovery_scope.comp = NULL;
	sg->recovery_scope.sus = (struct amf_su **)
	calloc (2, sizeof (struct amf_su *));
	sg->recovery_scope.sis = (struct amf_si **)
	calloc (1, sizeof (struct amf_si *));

	assert ((sg->recovery_scope.sus != NULL) &&
		(sg->recovery_scope.sis != NULL));
	sg->recovery_scope.sus[0] = su;

	amf_su_dn_make (sg->recovery_scope.sus[0], &dn);
	log_printf (
		LOG_NOTICE, "'%s' for %s recovery action started",
		sg_recovery_type_text[sg->recovery_scope.recovery_type],
		dn.value);

	si_assignment = amf_su_get_next_si_assignment (su, NULL);
	while (si_assignment != NULL) {
		if (is_si_in_scope(sg, si_assignment->si) == 0) {
			add_si_to_scope(sg,si_assignment->si );
		}
		si_assignment = amf_su_get_next_si_assignment (su, si_assignment);
	}

	sus = sg->recovery_scope.sus;
	dprintf("The following sus are within the scope:\n");
	while (*sus != NULL) {
		dprintf("%s\n", (*sus)->name.value);
		sus++;
	}
	sis= sg->recovery_scope.sis;
	dprintf("The following sis are within the scope:\n");
	while (*sis != NULL) {
		dprintf("%s\n", (*sis)->name.value);
		sis++;
	}
}
static void set_scope_for_failover_node (struct amf_sg *sg, struct amf_node *node)
{
	struct amf_si_assignment *si_assignment;
	struct amf_si **sis;
	struct amf_su **sus;
	struct amf_su *su;

	ENTER ("'%s'", node->name.value);
	sg->recovery_scope.recovery_type = SG_RT_FailoverNode;
	sg->recovery_scope.comp = NULL;
	sg->recovery_scope.sus = (struct amf_su **)
	calloc (1, sizeof (struct amf_su *));
	sg->recovery_scope.sis = (struct amf_si **)
	calloc (1, sizeof (struct amf_si *));

	log_printf (
		LOG_NOTICE, "'%s' for node %s recovery action started",
		sg_recovery_type_text[sg->recovery_scope.recovery_type],
		node->name.value);

	assert ((sg->recovery_scope.sus != NULL) &&
		(sg->recovery_scope.sis != NULL));
	for (su = sg->su_head; su != NULL; su = su->next) {
		if (name_match (&node->name, &su->saAmfSUHostedByNode)) {
			add_su_to_scope (sg, su);
		}
	}

	sus = sg->recovery_scope.sus;
	while (*sus != 0) {
		su  = *sus;
		si_assignment = amf_su_get_next_si_assignment (su, NULL);
		while (si_assignment != NULL) {
			if (is_si_in_scope(sg, si_assignment->si) == 0) {
				add_si_to_scope(sg, si_assignment->si );
			}
			si_assignment = amf_su_get_next_si_assignment (su, si_assignment);
		}
		sus++;
	}

	sus = sg->recovery_scope.sus;
	dprintf("The following sus are within the scope:\n");
	while (*sus != NULL) {
		dprintf("%s\n", (*sus)->name.value);
		sus++;
	}
	sis = sg->recovery_scope.sis;
	dprintf("The following sis are within the scope:\n");
	while (*sis != NULL) {
		dprintf("%s\n", (*sis)->name.value);
		sis++;
	}
}

/**
 * Delete all SI assignments and all CSI assignments
 * by requesting all contained components.
 * @param su
 */
static void delete_si_assignments (struct amf_su *su)
{
	struct amf_csi *csi;
	struct amf_si *si;
	struct amf_si_assignment *si_assignment;
	struct amf_si_assignment **prev;	
	ENTER ("'%s'", su->name.value);

	for (si = su->sg->application->si_head; si != NULL; si = si->next) {

		prev = &si->assigned_sis;

		if (!name_match (&si->saAmfSIProtectedbySG, &su->sg->name)) {
			continue;
		}

		for (csi = si->csi_head; csi != NULL; csi = csi->next) {
			amf_csi_delete_assignments (csi, su);
		}


		for (si_assignment = si->assigned_sis; si_assignment != NULL;
			si_assignment = si_assignment->next) {
			if (si_assignment->su == su) {
				struct amf_si_assignment *tmp = si_assignment;
				*prev = si_assignment->next;
				dprintf ("SI assignment %s unlinked", tmp->name.value);
				free (tmp);
			} else {
				prev = &si_assignment->next;
			}
		}

	}
}

/**
 * Delete all SI assignments and all CSI assignments in current
 * recovery scope.
 * @param sg
 */
static void delete_si_assignments_in_scope (struct amf_sg *sg) 
{
	struct amf_su **sus= sg->recovery_scope.sus;

	while (*sus != NULL) {
		delete_si_assignments (*sus);
		sus++;
	}

}

/**
 * Callback function used by SI when an SI has been deactivated.
 * @param si_assignment
 * @param result
 */
static void dependent_si_deactivated_cbfn (
	struct amf_si_assignment *si_assignment, int result)
{
	struct amf_sg *sg = si_assignment->su->sg;
	struct amf_su **sus = sg->recovery_scope.sus;
	struct amf_su *su;

	ENTER ("'%s', %d", si_assignment->si->name.value, result);

	/*
	 * If all SI assignments for all SUs in the SG are not pending,
	 * goto next state (TerminatingSuspected).
	 */


	for (su = sg->su_head ; su != NULL; su = su->next) {
		struct amf_si_assignment *si_assignment;
		si_assignment = amf_su_get_next_si_assignment(su, NULL);

		while (si_assignment != NULL) {
			if (si_assignment->saAmfSISUHAState != 
				si_assignment->requested_ha_state) {
				goto still_wating;
			}
			si_assignment = amf_su_get_next_si_assignment(su, si_assignment);
		}


	}

	still_wating:

	if (su == NULL) {
		sus = si_assignment->su->sg->recovery_scope.sus;

		/* Select next state depending on if some SU in the scope is
		 * needs to be terminated.
		 */
		while (*sus != NULL) {
			if (((*sus)->saAmfSUPresenceState != 
				SA_AMF_PRESENCE_UNINSTANTIATED) &&
				((*sus)->saAmfSUPresenceState != 
				SA_AMF_PRESENCE_TERMINATION_FAILED) &&
				((*sus)->saAmfSUPresenceState != 
				SA_AMF_PRESENCE_INSTANTIATION_FAILED)) {
				break;
			}
			sus++;
		}
		if (*sus != NULL) {
			acsm_enter_terminating_suspected (sg);
		} else {
			delete_si_assignments_in_scope(sg);         
			acsm_enter_activating_standby (sg);
		}
	}
	LEAVE("");
}


static void standby_su_activated_cbfn (
	struct amf_si_assignment *si_assignment, int result)
{
	struct amf_su **sus= si_assignment->su->sg->recovery_scope.sus;
	struct amf_si **sis= si_assignment->su->sg->recovery_scope.sis;

	ENTER ("'%s', %d", si_assignment->si->name.value, result);

	/*
	 * If all SI assignments for all SIs in the scope are activated, goto next
	 * state.
	 */

	while (*sis != NULL) {
		if ((*sis)->assigned_sis != NULL &&
			(*sis)->assigned_sis->saAmfSISUHAState != SA_AMF_HA_ACTIVE) {
			break;
		}
		sis++;
	}

	if (*sis == NULL) {
		/*                                                              
		* TODO: create SI assignment to spare and assign them
		*/
		(*sus)->sg->avail_state = SG_AC_AssigningStandbyToSpare;

		acsm_enter_repairing_su ((*sus)->sg);
	}
}

static void assign_si_assumed_cbfn (
	struct amf_si_assignment *si_assignment, int result)
{
	struct amf_si_assignment *tmp_si_assignment;
	struct amf_si *si;
	struct amf_sg *sg = si_assignment->su->sg;
	int si_assignment_cnt = 0;
	int confirmed_assignments = 0;

	ENTER ("'%s', %d", si_assignment->si->name.value, result);

	/*                                                              
	 * Report to application when all SIs that this SG protects
	 * has been assigned or go back to idle state if not cluster
	 * start.
	 */
	for (si = sg->application->si_head; si != NULL; si = si->next) {
		if (name_match (&si->saAmfSIProtectedbySG, &sg->name)) {

			for (tmp_si_assignment = si->assigned_sis;
				tmp_si_assignment != NULL;
				tmp_si_assignment = tmp_si_assignment->next) {

				si_assignment_cnt++;
				if (tmp_si_assignment->requested_ha_state ==
					tmp_si_assignment->saAmfSISUHAState) {

					confirmed_assignments++;
				}
			}
		}
	}

	assert (confirmed_assignments != 0);

	switch (sg->avail_state) {
		case SG_AC_AssigningOnRequest:
			if (si_assignment_cnt == confirmed_assignments) {
				return_to_idle (sg);
				amf_application_sg_assigned (sg->application, sg);
			} else {
				dprintf ("%d, %d", si_assignment_cnt, confirmed_assignments);
			}
			break;
		case SG_AC_AssigningStandBy:
			{
				if (si_assignment_cnt == confirmed_assignments) {
					return_to_idle (sg);
				}
				break;
			}
		default:
			dprintf ("%d, %d, %d", sg->avail_state, si_assignment_cnt,
				confirmed_assignments);
			amf_runtime_attributes_print (amf_cluster);
			assert (0);
	}
}

static inline int div_round (int a, int b)
{
	int res;

	assert (b != 0);
	res = a / b;
	if ((a % b) != 0)
		res++;
	return res;
}

static int all_su_has_presence_state (
	struct amf_sg *sg, struct amf_node *node_to_start, 
	SaAmfPresenceStateT state)
{
	struct amf_su   *su;
	int all_set = 1;

	for (su = sg->su_head; su != NULL; su = su->next) {

		if (su->saAmfSUPresenceState != state) {
			if (node_to_start == NULL) {
				all_set = 0;
				break;
			} else {
				if (name_match(&node_to_start->name,
					&su->saAmfSUHostedByNode)) {
					all_set = 0;
					break;
				}
			}
		}
	}
	return all_set;
}


static int all_su_in_scope_has_presence_state (
	struct amf_sg *sg, SaAmfPresenceStateT state)
{
	struct amf_su **sus= sg->recovery_scope.sus;
	while (*sus != NULL) {
		if ((*sus)->saAmfSUPresenceState != state) {
			break;
		}
		sus++;
	}
	return(*sus == NULL);
}

/**
 * Get number of SIs protected by the specified SG.
 * @param sg
 * 
 * @return int
 */
static int sg_si_count_get (struct amf_sg *sg)
{
	struct amf_si *si;
	int cnt = 0;

	for (si = sg->application->si_head; si != NULL; si = si->next) {
		if (name_match (&si->saAmfSIProtectedbySG, &sg->name)) {
			cnt += 1;
		}
	}
	return(cnt);
}

int amf_si_get_saAmfSINumReqActiveAssignments(struct amf_si *si) 
{
	struct amf_si_assignment *si_assignment = si->assigned_sis;
	int number_of_req_active_assignments = 0;

	for (; si_assignment != NULL; si_assignment = si_assignment->next) {

		if (si_assignment->requested_ha_state == SA_AMF_HA_ACTIVE) {
			number_of_req_active_assignments++;
		}
	}
	return number_of_req_active_assignments;
}


int amf_si_get_saAmfSINumReqStandbyAssignments(struct amf_si *si) 
{
	struct amf_si_assignment *si_assignment = si->assigned_sis;
	int number_of_req_active_assignments = 0;

	for (; si_assignment != NULL; si_assignment = si_assignment->next) {
		if (si_assignment->requested_ha_state == SA_AMF_HA_STANDBY) {
			number_of_req_active_assignments++;
		}
	}
	return number_of_req_active_assignments;
}

static int sg_assign_nm_active (struct amf_sg *sg, int su_active_assign)
{
	struct amf_su *su;
	struct amf_si *si;
	int assigned = 0;
	int assign_to_su = 0;
	int total_assigned = 0;
	int si_left;
	int si_total;
	int su_left_to_assign = su_active_assign;

	si_total = sg_si_count_get (sg);
	si_left = si_total;
	assign_to_su = div_round (si_left, su_active_assign);
	if (assign_to_su > sg->saAmfSGMaxActiveSIsperSUs) {
		assign_to_su = sg->saAmfSGMaxActiveSIsperSUs;
	}

	su = sg->su_head;
	while (su != NULL && su_left_to_assign > 0) {
		if (amf_su_get_saAmfSUReadinessState (su) !=
			SA_AMF_READINESS_IN_SERVICE ||
			amf_su_get_saAmfSUNumCurrActiveSIs (su) == 
			assign_to_su ||
			amf_su_get_saAmfSUNumCurrStandbySIs (su) > 0) {

			su = su->next;
			continue; /* Not in service */
		}

		si = sg->application->si_head;
		assigned = 0;
		assign_to_su = div_round (si_left, su_left_to_assign);
		if (assign_to_su > sg->saAmfSGMaxActiveSIsperSUs) {
			assign_to_su = sg->saAmfSGMaxActiveSIsperSUs;
		}
		while (si != NULL) {

			if (name_match (&si->saAmfSIProtectedbySG, &sg->name) &&
				assigned < assign_to_su && 
				amf_si_get_saAmfSINumReqActiveAssignments(si) == 0) {
					assigned += 1;
					total_assigned += 1;
					amf_su_assign_si (su, si, SA_AMF_HA_ACTIVE);
		    	}
			
			si = si->next;
		}
		su = su->next;
		su_left_to_assign -= 1;
		si_left -= assigned;
		dprintf (" su_left_to_assign =%d, si_left=%d\n",
			su_left_to_assign, si_left);
	}

	assert (total_assigned <= si_total);
	if (total_assigned == 0) {
		dprintf ("Info: No SIs assigned");
	}
	LEAVE();

	return total_assigned;
}

static int sg_assign_nm_standby (struct amf_sg *sg, int su_standby_assign)
{
	struct amf_su *su;
	struct amf_si *si;
	int assigned = 0;
	int assign_to_su = 0;
	int total_assigned = 0;
	int si_left;
	int si_total;
	int su_left_to_assign = su_standby_assign;

	ENTER ("'%s'", sg->name.value);

	if (su_standby_assign == 0) {
		return 0;
	}
	si_total = sg_si_count_get (sg);
	si_left = si_total;
	assign_to_su = div_round (si_left, su_standby_assign);
	if (assign_to_su > sg->saAmfSGMaxStandbySIsperSUs) {
		assign_to_su = sg->saAmfSGMaxStandbySIsperSUs;
	}

	su = sg->su_head;
	while (su != NULL && su_left_to_assign > 0) {
		if (amf_su_get_saAmfSUReadinessState (su) !=
			SA_AMF_READINESS_IN_SERVICE ||
			amf_su_get_saAmfSUNumCurrActiveSIs (su) > 0 ||
			amf_su_get_saAmfSUNumCurrStandbySIs (su) ==
			assign_to_su) {

			su = su->next;
			continue; /* Not available for assignment */
		}

		si = sg->application->si_head;
		assigned = 0;
		assign_to_su = div_round (si_left, su_left_to_assign);
		if (assign_to_su > sg->saAmfSGMaxStandbySIsperSUs) {
			assign_to_su = sg->saAmfSGMaxStandbySIsperSUs;
		}
		while (si != NULL) {
			if (name_match (&si->saAmfSIProtectedbySG, &sg->name) &&
				assigned < assign_to_su && 
				amf_si_get_saAmfSINumReqStandbyAssignments (si) == 0) {
					assigned += 1;
					total_assigned += 1;
					amf_su_assign_si (su, si, SA_AMF_HA_STANDBY);
			}
			si = si->next;
		}
		su_left_to_assign -= 1;
		si_left -= assigned;
		dprintf (" su_left_to_assign =%d, si_left=%d\n",
			su_left_to_assign, si_left);

		su = su->next;
	}

	assert (total_assigned <= si_total);
	if (total_assigned == 0) {
		dprintf ("Info: No SIs assigned!");
	}

	return total_assigned;
}

static int su_inservice_count_get (struct amf_sg *sg)
{
	struct amf_su *su;
	int answer = 0;

	for (su = sg->su_head; su != NULL; su = su->next) {
		if (amf_su_get_saAmfSUReadinessState (su) ==
			SA_AMF_READINESS_IN_SERVICE) {

			answer += 1;
		}
	}
	return(answer);
}


/**
 * TODO: dependency_level not used, hard coded
 * @param sg
 * @param dependency_level
 */
static int assign_si (struct amf_sg *sg, int dependency_level)
{
	int active_sus_needed = 0;
	int standby_sus_needed = 0;
	int inservice_count;
	int su_active_assign;
	int su_standby_assign;
	int su_spare_assign;
	int assigned = 0;

	ENTER ("'%s'", sg->name.value);

	/**
	 * Phase 1: Calculate assignments and create all runtime objects in
	 * information model. Do not do the actual assignment, done in
	 * phase 2.
	 */

	/**
	 * Calculate number of SUs to assign to active or standby state
	 */
	inservice_count = su_inservice_count_get (sg);

	if (sg->saAmfSGNumPrefActiveSUs > 0) {
		active_sus_needed = div_round (
			sg_si_count_get (sg),
			sg->saAmfSGMaxActiveSIsperSUs);
	} else {
		log_printf (LOG_LEVEL_ERROR, "ERROR: saAmfSGNumPrefActiveSUs == 0 !!");
		openais_exit_error (AIS_DONE_FATAL_ERR);
	}

	if (sg->saAmfSGNumPrefStandbySUs > 0) {
		standby_sus_needed = div_round (
			sg_si_count_get (sg),
			sg->saAmfSGMaxStandbySIsperSUs);
	} else {
		log_printf (LOG_LEVEL_ERROR, "ERROR: saAmfSGNumPrefStandbySUs == 0 !!");
		openais_exit_error (AIS_DONE_FATAL_ERR);

	}

	dprintf ("(inservice=%d) (active_sus_needed=%d) (standby_sus_needed=%d)"
		"\n",
		inservice_count, active_sus_needed, standby_sus_needed);

	/* Determine number of active and standby service units
	 * to assign based upon reduction procedure
	 */
	if ((inservice_count < active_sus_needed)) {
		dprintf ("assignment VI - partial assignment with SIs drop outs\n");

		su_active_assign = inservice_count;
		su_standby_assign = 0;
		su_spare_assign = 0;
	} else
		if ((inservice_count < active_sus_needed + standby_sus_needed)) {
		dprintf ("assignment V - partial assignment with reduction of"
			" standby units\n");

		su_active_assign = active_sus_needed;
		su_standby_assign = inservice_count - active_sus_needed;
		su_spare_assign = 0;
	} else
		if ((inservice_count < sg->saAmfSGNumPrefActiveSUs + standby_sus_needed)) {
		dprintf ("IV: full assignment with reduction of active service"
			" units\n");
		su_active_assign = inservice_count - standby_sus_needed;
		su_standby_assign = standby_sus_needed;
		su_spare_assign = 0;
	} else
		if ((inservice_count < 
		sg->saAmfSGNumPrefActiveSUs + sg->saAmfSGNumPrefStandbySUs)) {
		dprintf ("III: full assignment with reduction of standby service"
			" units\n");
		su_active_assign = sg->saAmfSGNumPrefActiveSUs;
		su_standby_assign = inservice_count - sg->saAmfSGNumPrefActiveSUs;
		su_spare_assign = 0;
	} else
		if ((inservice_count == 
		sg->saAmfSGNumPrefActiveSUs + sg->saAmfSGNumPrefStandbySUs)) {
		if (sg->saAmfSGNumPrefInserviceSUs > inservice_count) {
			dprintf ("II: full assignment with spare reduction\n");
		} else {
			dprintf ("II: full assignment without spares\n");
		}

		su_active_assign = sg->saAmfSGNumPrefActiveSUs;
		su_standby_assign = sg->saAmfSGNumPrefStandbySUs;
		su_spare_assign = 0;
	} else {
		dprintf ("I: full assignment with spares\n");
		su_active_assign = sg->saAmfSGNumPrefActiveSUs;
		su_standby_assign = sg->saAmfSGNumPrefStandbySUs;
		su_spare_assign = inservice_count - 
			sg->saAmfSGNumPrefActiveSUs - sg->saAmfSGNumPrefStandbySUs;
	}

	dprintf ("(inservice=%d) (assigning active=%d) (assigning standby=%d)"
		" (assigning spares=%d)\n",
		inservice_count, su_active_assign, su_standby_assign, su_spare_assign);

	if (inservice_count > 0) {
		assigned = sg_assign_nm_active (sg, su_active_assign);
		assigned += sg_assign_nm_standby (sg, su_standby_assign);

#if 0
		assert (assigned > 0);
#endif
		sg->saAmfSGNumCurrAssignedSUs = inservice_count;

	/**
	 * Phase 2: do the actual assignment to the component
	 * TODO: first do active, then standby
	 */
		{
			struct amf_si *si;
			struct amf_si_assignment *si_assignment;

			for (si = sg->application->si_head; si != NULL; si = si->next) {
				if (name_match (&si->saAmfSIProtectedbySG, &sg->name)) {
					for (si_assignment = si->assigned_sis; 
						si_assignment != NULL;
						si_assignment = si_assignment->next) {

						if (si_assignment->requested_ha_state !=
							si_assignment->saAmfSISUHAState) {
							amf_si_ha_state_assume (
								si_assignment, assign_si_assumed_cbfn);
						}
					}
				}
			}
		}
	}

	LEAVE ("'%s'", sg->name.value);
	return assigned;
}

void amf_sg_assign_si (struct amf_sg *sg, int dependency_level)
{   

	sg->avail_state = SG_AC_AssigningOnRequest;
	if (assign_si (sg, dependency_level) == 0) {
		return_to_idle (sg);	
		amf_application_sg_assigned (sg->application, sg);	
	}
}

void amf_sg_failover_node_req (
	struct amf_sg *sg, struct amf_node *node) 
{

	ENTER("'%s, %s'",node->name.value, sg->name.value);

   /*                                                              
	 * TODO: Defer all new events. Workaround is to exit.
	*/
	if (sg->avail_state != SG_AC_Idle) {
		log_printf (LOG_LEVEL_ERROR, "To handle multiple simultaneous SG"
			" recovery actions is not implemented yet:"
			" SG '%s', NODE '%s', avail_state %d",
			sg->name.value, node->name.value, sg->avail_state);
		openais_exit_error (AIS_DONE_FATAL_ERR);
		return;
	}

	set_scope_for_failover_node(sg, node);

	if (has_any_su_in_scope_active_workload (sg)) {
		acsm_enter_deactivating_dependent_workload (sg);
	} else {
		struct amf_su **sus = sg->recovery_scope.sus;

		/* Select next state depending on if some SU in the scope is
		 * needs to be terminated.
		 */
		while (*sus != NULL) {
			ENTER("SU %s pr_state='%d'",(*sus)->name.value,
				(*sus)->saAmfSUPresenceState);
			if (((*sus)->saAmfSUPresenceState == 
				SA_AMF_PRESENCE_UNINSTANTIATED) ||
				((*sus)->saAmfSUPresenceState == 
				SA_AMF_PRESENCE_TERMINATION_FAILED) ||
				((*sus)->saAmfSUPresenceState == 
				SA_AMF_PRESENCE_INSTANTIATION_FAILED)) {
				sus++;
				continue;
			}
			break;
		}

		if (*sus != NULL) {
			acsm_enter_terminating_suspected (sg);
		} else {
			delete_si_assignments_in_scope (sg);
			return_to_idle (sg);         
		}

	} 

}
void amf_sg_start (struct amf_sg *sg, struct amf_node *node)
{
	struct amf_su *su;
	sg_avail_control_state_t old_avail_state = sg->avail_state;
	int instantiated_sus = 0;

	ENTER ("'%s'", sg->name.value);

	sg->node_to_start = node;

	sg->avail_state = SG_AC_InstantiatingServiceUnits;

	for (su = sg->su_head; su != NULL; su = su->next) {
		if (node == NULL) {
			/* Cluster start */
			amf_su_instantiate (su);
			instantiated_sus++;
		} else {
			/* Node start, match if SU is hosted on the specified node*/
			if (name_match (&node->name, &su->saAmfSUHostedByNode)) {
				amf_su_instantiate (su);
				instantiated_sus++;
			}
		}
	}

	if (instantiated_sus == 0) {
		sg->avail_state = old_avail_state;
	}
}

void amf_sg_su_state_changed (struct amf_sg *sg, 
	struct amf_su *su, SaAmfStateT type, int state)
{
	ENTER ("'%s' SU '%s' state %s",
		sg->name.value, su->name.value, amf_presence_state(state));

	if (type == SA_AMF_PRESENCE_STATE) {
		if (state == SA_AMF_PRESENCE_INSTANTIATED) {
			if (sg->avail_state == SG_AC_InstantiatingServiceUnits) {
				if (all_su_has_presence_state(sg, sg->node_to_start, 
					SA_AMF_PRESENCE_INSTANTIATED)) {
					su->sg->avail_state = SG_AC_Idle;
					amf_application_sg_started (
						sg->application, sg, this_amf_node);
				}
			} else if (sg->avail_state == SG_AC_ReparingSu) {
				if (all_su_in_scope_has_presence_state(su->sg,
					SA_AMF_PRESENCE_INSTANTIATED)) {
					su->sg->avail_state = SG_AC_AssigningStandBy;
					if (assign_si (sg, 0) == 0) {
						return_to_idle (sg);	
					}

				} else {
					dprintf ("avail-state: %u", sg->avail_state);
					assert (0);
				}
			} else {
				assert (0);
			}
		} else if (state == SA_AMF_PRESENCE_UNINSTANTIATED) {
			if (sg->avail_state == SG_AC_TerminatingSuspected) {
				if (all_su_in_scope_has_presence_state (sg, state)) {
					delete_si_assignments_in_scope (sg);
					if (is_standby_for_non_active_si_in_scope (sg)) {
						acsm_enter_activating_standby (sg);
					} else {
						/*                                                              
						* TODO: create SI assignment to spare and assign them
						*/
						sg->avail_state = SG_AC_AssigningStandbyToSpare;
						acsm_enter_repairing_su (sg);
					}
				}
			} else {
				assert (0);
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

void amf_sg_failover_su_req (
	struct amf_sg *sg, struct amf_su *su, struct amf_node *node)
{
	ENTER ("");
   /*                                                              
	 * TODO: Defer all new events. Workaround is to exit.
	*/
	if (sg->avail_state != SG_AC_Idle) {
		log_printf (LOG_LEVEL_ERROR, "To handle multiple simultaneous SG"
			" recovery actions is not implemented yet:"
			" SG '%s', SU '%s', avail_state %d",
			sg->name.value, su->name.value, sg->avail_state);
		openais_exit_error (AIS_DONE_FATAL_ERR);
		return;
	}
	set_scope_for_failover_su (sg, su);
	if (has_any_su_in_scope_active_workload (sg)) {
		acsm_enter_deactivating_dependent_workload (sg);
	} else {
		acsm_enter_terminating_suspected (sg);
	}
}

/**
 * Constructor for SG objects. Adds SG to the list owned by
 * the specified application. Always returns a valid SG
 * object, out-of-memory problems are handled here. Default
 * values are initialized.
 * @param sg
 * @param name
 * 
 * @return struct amf_sg*
 */

struct amf_sg *amf_sg_new (struct amf_application *app, char *name) 
{
	struct amf_sg *sg = calloc (1, sizeof (struct amf_sg));

	if (sg == NULL) {
		openais_exit_error (AIS_DONE_OUT_OF_MEMORY);
	}

	sg->next = app->sg_head;
	app->sg_head = sg;
	sg->saAmfSGAdminState = SA_AMF_ADMIN_UNLOCKED;
	sg->saAmfSGNumPrefActiveSUs = 1;
	sg->saAmfSGNumPrefStandbySUs = 1;
	sg->saAmfSGNumPrefInserviceSUs = ~0;
	sg->saAmfSGNumPrefAssignedSUs = ~0;
	sg->saAmfSGCompRestartProb = -1;
	sg->saAmfSGCompRestartMax = ~0;
	sg->saAmfSGSuRestartProb = -1;
	sg->saAmfSGSuRestartMax = ~0;
	sg->saAmfSGAutoAdjustProb = -1;
	sg->saAmfSGAutoRepair = SA_TRUE;
	sg->application = app;
	setSaNameT (&sg->name, name);
	sg->node_to_start = NULL;

	return sg;
}

void amf_sg_delete (struct amf_sg *sg)
{
	struct amf_su *su;

	for (su = sg->su_head; su != NULL;) {
		struct amf_su *tmp = su;
		su = su->next;
		amf_su_delete (tmp);
	}

	free (sg);
}

void *amf_sg_serialize (struct amf_sg *sg, int *len)
{
	char *buf = NULL;
	int offset = 0, size = 0;

	TRACE8 ("%s", sg->name.value);

	buf = amf_serialize_SaNameT (buf, &size, &offset, &sg->name);
	buf = amf_serialize_SaUint32T (buf, &size, &offset, sg->saAmfSGRedundancyModel);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, sg->saAmfSGAutoAdjust);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, sg->saAmfSGNumPrefActiveSUs);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, sg->saAmfSGNumPrefStandbySUs);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, sg->saAmfSGNumPrefInserviceSUs);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, sg->saAmfSGNumPrefAssignedSUs);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, sg->saAmfSGMaxActiveSIsperSUs);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, sg->saAmfSGMaxStandbySIsperSUs);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, sg->saAmfSGCompRestartProb);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, sg->saAmfSGCompRestartMax);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, sg->saAmfSGSuRestartProb);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, sg->saAmfSGSuRestartMax);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, sg->saAmfSGAutoAdjustProb);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, sg->saAmfSGAutoRepair);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, sg->saAmfSGAdminState);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, sg->saAmfSGNumCurrAssignedSUs);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, sg->saAmfSGNumCurrNonInstantiatedSpareSUs);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, sg->saAmfSGNumCurrInstantiatedSpareSUs);
	buf = amf_serialize_SaStringT (
		buf, &size, &offset, sg->clccli_path);
	buf = amf_serialize_SaUint32T (
		buf, &size, &offset, sg->avail_state);

	*len = offset;

	return buf;
}

struct amf_sg *amf_sg_deserialize (
	struct amf_application *app, char *buf, int size) 
{
	char *tmp = buf;
	struct amf_sg *sg;

	sg = amf_sg_new (app, "");

	tmp = amf_deserialize_SaNameT (tmp, &sg->name);
	tmp = amf_deserialize_SaUint32T (tmp, &sg->saAmfSGRedundancyModel);
	tmp = amf_deserialize_SaUint32T (tmp, &sg->saAmfSGAutoAdjust);
	tmp = amf_deserialize_SaUint32T (tmp, &sg->saAmfSGNumPrefActiveSUs);
	tmp = amf_deserialize_SaUint32T (tmp, &sg->saAmfSGNumPrefStandbySUs);
	tmp = amf_deserialize_SaUint32T (tmp, &sg->saAmfSGNumPrefInserviceSUs);
	tmp = amf_deserialize_SaUint32T (tmp, &sg->saAmfSGNumPrefAssignedSUs);
	tmp = amf_deserialize_SaUint32T (tmp, &sg->saAmfSGMaxActiveSIsperSUs);
	tmp = amf_deserialize_SaUint32T (tmp, &sg->saAmfSGMaxStandbySIsperSUs);
	tmp = amf_deserialize_SaUint32T (tmp, &sg->saAmfSGCompRestartProb);
	tmp = amf_deserialize_SaUint32T (tmp, &sg->saAmfSGCompRestartMax);
	tmp = amf_deserialize_SaUint32T (tmp, &sg->saAmfSGSuRestartProb);
	tmp = amf_deserialize_SaUint32T (tmp, &sg->saAmfSGSuRestartMax);
	tmp = amf_deserialize_SaUint32T (tmp, &sg->saAmfSGAutoAdjustProb);
	tmp = amf_deserialize_SaUint32T (tmp, &sg->saAmfSGAutoRepair);
	tmp = amf_deserialize_SaUint32T (tmp, &sg->saAmfSGAdminState);
	tmp = amf_deserialize_SaUint32T (tmp, &sg->saAmfSGNumCurrAssignedSUs);
	tmp = amf_deserialize_SaUint32T (tmp, &sg->saAmfSGNumCurrNonInstantiatedSpareSUs);
	tmp = amf_deserialize_SaUint32T (tmp, &sg->saAmfSGNumCurrInstantiatedSpareSUs);
	tmp = amf_deserialize_SaStringT (tmp, &sg->clccli_path);
	tmp = amf_deserialize_SaUint32T (tmp, &sg->avail_state);

	return sg;
}

struct amf_sg *amf_sg_find (struct amf_application *app, char *name) 
{
	struct amf_sg *sg;

	for (sg = app->sg_head; sg != NULL; sg = sg->next) {
		if (sg->name.length == strlen(name) && 
			strncmp (name, (char*)sg->name.value, sg->name.length) == 0) {
			break;
		}
	}

	return sg;
}

