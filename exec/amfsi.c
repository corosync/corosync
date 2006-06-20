/** @file amfsi.c
 * 
 * Copyright (c) 2006 Ericsson AB.
 *  Author: Hans Feldt
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
 * AMF Workload related classes Implementation
 * 
 * This file contains functions for handling :
 *	- AMF service instances(SI)
 *	- AMF SI Dependency
 *	- AMF SI Ranked SU
 *	- AMF SI Assignment
 *	- AMF component service instances (CSI)
 *	- AMF CSI Assignment
 *	- AMF CSI Type
 *	- AMF CSI Attribute
 * The file can be viewed as the implementation of the classes listed above
 * as described in SAI-Overview-B.02.01. The SA Forum specification 
 * SAI-AIS-AMF-B.02.01 has been used as specification of the behaviour
 * and is referred to as 'the spec' below.
 * 
 * The functions in this file are responsible for:
 * - calculating and storing an SI_dependency_level integer per SI
 * - calculating and storing a csi_dependency_level integer per CSI
 * - on request change HA state of an SI or CSI in such a way that the
 *   requirements regarding SI -> SI dependencies (paragraphs 3.9.1.1 and
 *   3.9.1.2) and CSI -> CSI dependencies (paragraph 3.9.1.3) are fully
 *   respected
 *
 * The si_dependency_level is an attribute calculated at init (in the future
 * also at reconfiguration) which indicates dependencies between SIs as
 * an integer. The si_dependency level indicates to which extent an SI depends
 * on other SIs such that an SI that depends on no other SI is on 
 * si_dependecy_level == 1, an SI that depends only on an SI on
 * si_dependency_level == 1 is on si_dependency-level == 2. 
 * An SI that depends on several SIs gets a si_dependency_level that is one
 * unit higher than the SI with the highest si_dependency_level it depends on.
 *
 * The csi_dependency_level attribute works the same way.
 *
 * According to paragraph 3.9.1 of the spec, a change to or from the ACTIVE
 * HA state is not always allowed without first deactivate dependent SI and CSI
 * assignments. Dependencies between CSIs are absolute while an SI that depends
 * on another SI may tolerate that the SI on which it depends is inactive for a
 * configurable time (the tolerance time). The consequence of this is that a
 * request to change the SI state may require a sequence of requests to
 * components to assume a new HA state for a CSI-assignment and to guarantee
 * the dependency rules, the active response from the component has to be
 * awaited before next HA state can be set.
 * 
 * This file implements an SI state machine that fully implements these rules.
 * This state machine is called SI Dependency Control State Machine (dcsm)
 * and has the following states:
 *	- DEACTIVATED (there is no SI-assignment with active HA state)
 *	- ACTIVATING (a request to set the ACTIVE HA state has been received and
 *				  setting ACTIVE HA states to the appropriate components are
 *				  in progress)
 *	- ACTIVATED	(there is at least one SI-assignment with the ACTIVE HA-state)
 *	- DEACTIVATING (a request to de-activate an SI or only a specific CSI
 *					within an SI has been received and setting the QUISCED
 *				    HA states to the appropriate components are in progress)
 *	- DEPENDENCY_DEACTIVATING (the SI-SI dependency tolerance timer has expired
 *                             and setting the QUISCED HA states to the
 *							   appropriate components are in progress)
 *	- DEPENDENCY_DEACTIVATED (as state DEACTIVATED but will automatically
 *							  transition to state ACTIVATING when the
 *							  dependency problem is solved, i.e. the SI on
 *							  which it depends has re-assumed the ACTIVE HA
 *							  state)
 *	- SETTING (a request to change the HA state when neither the existing
 *			   nor the requested state is ACTIVE)
 * 
 * This file also implements:
 *	- SI:             Assignment state (for report purposes)
 *	- SI Assignment:  HA state
 *	- CSI Assignment: HA state
 * 
 */

#include <assert.h>
#include <stdio.h>
#include "amf.h"
#include "print.h"

char *amf_csi_dn_make (struct amf_csi *csi, SaNameT *name)
{
	int	i = snprintf((char*) name->value, SA_MAX_NAME_LENGTH,
		"safCsi=%s,safSi=%s,safApp=%s",
		csi->name.value, csi->si->name.value,
		csi->si->application->name.value);
	assert (i <= SA_MAX_NAME_LENGTH);
	name->length = i;

	return (char *)name->value;
}

void amf_si_init (void)
{
	log_init ("AMF");
}

