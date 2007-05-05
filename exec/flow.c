/*
 * Copyright (c) 2006 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@mvista.com)
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
 */

/*
 * New messages are allowed from the library ONLY when the processor has not
 * received a OPENAIS_FLOW_CONTROL_STATE_ENABLED from any processor.  If a
 * OPENAIS_FLOW_CONTROL_STATE_ENABLED message is sent, it must later be
 *  cancelled by a OPENAIS_FLOW_CONTROL_STATE_DISABLED message.  A configuration
 * change with the flow controlled processor leaving the configuration will
 * also cancel flow control.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

#include "swab.h"
#include "flow.h"
#include "totem.h"
#include "totempg.h"
#include "print.h"
#include "hdb.h"
#include "../include/list.h"

struct flow_control_instance {
	struct list_head list_head;
	unsigned int service;
};

DECLARE_LIST_INIT (flow_control_service_list_head);

struct flow_control_message {
	unsigned int service __attribute__((aligned(8)));
	char id[1024] __attribute__((aligned(8)));
	unsigned int id_len __attribute__((aligned(8)));
	enum openais_flow_control_state flow_control_state __attribute__((aligned(8)));
};

struct flow_control_node_state {
	unsigned int nodeid;
	enum openais_flow_control_state flow_control_state;
};

struct flow_control_service {
	struct flow_control_node_state flow_control_node_state[PROCESSOR_COUNT_MAX];
	unsigned int service;
	char id[1024];
	unsigned int id_len;
	void (*flow_control_state_set_fn) (void *context, enum openais_flow_control_state flow_control_state);
	void *context;
	unsigned int processor_count;
	enum openais_flow_control_state flow_control_state;
	struct list_head list;
	struct list_head list_all;
};

static struct totempg_group flow_control_group = {
	.group      = "flowcontrol",
	.group_len  = 12
};

static totempg_groups_handle flow_control_handle;

static struct hdb_handle_database flow_control_hdb = {
	.handle_count	= 0,
	.handles	= NULL,
	.iterator	= 0,
	.mutex		= PTHREAD_MUTEX_INITIALIZER
};

static unsigned int flow_control_member_list[PROCESSOR_COUNT_MAX];
static unsigned int flow_control_member_list_entries;

static inline int flow_control_xmit (
	struct flow_control_service *flow_control_service,
	enum openais_flow_control_state flow_control_state)
{
	struct flow_control_message flow_control_message;
	struct iovec iovec;
	unsigned int res;

	flow_control_message.service = flow_control_service->service;
	flow_control_message.flow_control_state = flow_control_state;
	memcpy (&flow_control_message.id, flow_control_service->id,
		flow_control_service->id_len);
	flow_control_message.id_len = flow_control_service->id_len;

	iovec.iov_base = (char *)&flow_control_message;
	iovec.iov_len = sizeof (flow_control_message);

	res = totempg_groups_mcast_joined (flow_control_handle, &iovec, 1,
		TOTEMPG_AGREED);

	flow_control_service->flow_control_state_set_fn (
		flow_control_service->context,
		flow_control_service->flow_control_state);

	return (res);
}

static void flow_control_deliver_fn (
	unsigned int nodeid,
	struct iovec *iovec,
	int iov_len,
	int endian_conversion_required)
{
	struct flow_control_message *flow_control_message = (struct flow_control_message *)iovec[0].iov_base;
	struct flow_control_service *flow_control_service;
	struct list_head *list;
	unsigned int i;

	for (list = flow_control_service_list_head.next;
		list != &flow_control_service_list_head;
		list = list->next) {

		flow_control_service = list_entry (list, struct flow_control_service, list_all);
		/*
		 * Find this nodeid in the flow control service and set the message
		 * enabled or disabled flag
		 */
		for (i = 0; i < flow_control_service->processor_count; i++) {
			if (nodeid == flow_control_service->flow_control_node_state[i].nodeid) {
				flow_control_service->flow_control_node_state[i].flow_control_state =
					flow_control_message->flow_control_state;
				break;
			}
		}

		/*
		 * Determine if any flow control is enabled on any nodes and set
		 * the internal variable appropriately
		 */
		flow_control_service->flow_control_state = OPENAIS_FLOW_CONTROL_STATE_DISABLED;
		flow_control_service->flow_control_state_set_fn (flow_control_service->context, flow_control_service->flow_control_state);
		for (i = 0; i < flow_control_service->processor_count; i++) {
			if (flow_control_service->flow_control_node_state[i].flow_control_state == OPENAIS_FLOW_CONTROL_STATE_ENABLED) {
				flow_control_service->flow_control_state = OPENAIS_FLOW_CONTROL_STATE_ENABLED;
				flow_control_service->flow_control_state_set_fn (flow_control_service->context, flow_control_service->flow_control_state);
			}
		}
	} /* for list iteration */
}

static void flow_control_confchg_fn (
	enum totem_configuration_type configuration_type,
	unsigned int *member_list, int member_list_entries,
	unsigned int *left_list, int left_list_entries,
	unsigned int *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id)
{
	unsigned int i;
	unsigned int j;
	struct flow_control_service *flow_control_service;
	struct list_head *list;
	struct flow_control_node_state flow_control_node_state_temp[PROCESSOR_COUNT_MAX];

	memcpy (flow_control_member_list, member_list,
		sizeof (unsigned int) * member_list_entries);
	flow_control_member_list_entries = member_list_entries;

	for (list = flow_control_service_list_head.next;
		list != &flow_control_service_list_head;
		list = list->next) {

		flow_control_service = list_entry (list, struct flow_control_service, list_all);

		/*
		 * Generate temporary flow control node state information
		 */
		for (i = 0; i < member_list_entries; i++) {
			flow_control_node_state_temp[i].nodeid = member_list[i];
			flow_control_node_state_temp[i].flow_control_state = OPENAIS_FLOW_CONTROL_STATE_DISABLED;

			/*
			 * Determine if previous state was set for this processor
			 * if so keep that setting
			 */
			for (j = 0; j < flow_control_service->processor_count; j++) {
				if (flow_control_service->flow_control_node_state[j].nodeid == member_list[i]) {
					flow_control_node_state_temp[i].flow_control_state =
						flow_control_service->flow_control_node_state[j].flow_control_state;
					break; /* from for */
				}
			}
		}

		/*
		 * Copy temporary node state information to node state information
		 */
		memcpy (flow_control_service->flow_control_node_state,
			flow_control_node_state_temp,
			sizeof (struct flow_control_node_state) * member_list_entries);

		/*
		 * Set all of the node ids after a configuration change
		 * Turn on all flow control after a configuration change
		 */
		flow_control_service->processor_count = flow_control_member_list_entries;
		flow_control_service->flow_control_state = OPENAIS_FLOW_CONTROL_STATE_DISABLED;
		for (i = 0; i < member_list_entries; i++) {
			if (flow_control_service->flow_control_node_state[i].flow_control_state == OPENAIS_FLOW_CONTROL_STATE_ENABLED) {
				flow_control_service->flow_control_state = OPENAIS_FLOW_CONTROL_STATE_ENABLED;
				flow_control_service->flow_control_state_set_fn (flow_control_service->context, flow_control_service->flow_control_state);
			}
		}

	}
} 
/*
 * External API
 */
unsigned int openais_flow_control_initialize (void)
{
	unsigned int res;

	log_init ("FLOW");

	res = totempg_groups_initialize (
		&flow_control_handle,
		flow_control_deliver_fn,
		flow_control_confchg_fn);

	if (res == -1) {
		log_printf (LOG_LEVEL_ERROR,
			"Couldn't initialize flow control interface.\n");
		return (-1);
	}
	res = totempg_groups_join (
		flow_control_handle,
		&flow_control_group,
		1);

	if (res == -1) {
		log_printf (LOG_LEVEL_ERROR, "Couldn't join flow control group.\n");
		return (-1);
	}

	return (0);
}

unsigned int openais_flow_control_ipc_init (
	unsigned int *flow_control_handle,
	unsigned int service)
{
	struct flow_control_instance *instance;
	unsigned int res;

	res = hdb_handle_create (&flow_control_hdb,
		sizeof (struct flow_control_instance), flow_control_handle);
	if (res != 0) {
		goto error_exit;
	}
	res = hdb_handle_get (&flow_control_hdb, *flow_control_handle,
		(void *)&instance);
	if (res != 0) {
		goto error_destroy;
	}
	instance->service = service;

	list_init (&instance->list_head);

	return (0);

error_destroy:
	hdb_handle_destroy (&flow_control_hdb, *flow_control_handle);
error_exit:
	return (-1);

}

unsigned int openais_flow_control_ipc_exit (
	unsigned int flow_control_handle)
{
	hdb_handle_destroy (&flow_control_hdb, flow_control_handle);
	return (0);
}

unsigned int openais_flow_control_create (
	unsigned int flow_control_handle,
	unsigned int service,
	void *id,
	unsigned int id_len,
	void (*flow_control_state_set_fn) (void *context, enum openais_flow_control_state flow_control_state),
	void *context)
{
	struct flow_control_service *flow_control_service;
	struct flow_control_instance *instance;
	unsigned int res;
	unsigned int i;

	res = hdb_handle_get (&flow_control_hdb, flow_control_handle,
		(void *)&instance);
	if (res != 0) {
		goto error_exit;
	}

	flow_control_service = malloc (sizeof (struct flow_control_service));
	if (flow_control_service == NULL) {
		goto error_put;
	}

	/*
	 * Add new service to flow control system
	 */
	memset (flow_control_service, 0, sizeof (struct flow_control_service));

	flow_control_service->flow_control_state = OPENAIS_FLOW_CONTROL_STATE_DISABLED;
	flow_control_service->service = service;
	memcpy (flow_control_service->id, id, id_len);
	flow_control_service->id_len = id_len;
	flow_control_service->flow_control_state_set_fn = flow_control_state_set_fn;
	flow_control_service->context = context;

	list_init (&flow_control_service->list);
	list_add_tail (&instance->list_head,
		&flow_control_service->list);

	list_init (&flow_control_service->list_all);
	list_add_tail (&flow_control_service_list_head,
		&flow_control_service->list_all);

	for (i = 0; i < flow_control_member_list_entries; i++) {
		flow_control_service->flow_control_node_state[i].nodeid = flow_control_member_list[i];
		flow_control_service->processor_count = flow_control_member_list_entries;
	}
error_put:
	hdb_handle_put (&flow_control_hdb, flow_control_handle);

error_exit:
	return (res);
}

unsigned int openais_flow_control_destroy (
	unsigned int flow_control_identifier,
	unsigned int service,
	unsigned char *id,
	unsigned int id_len)
{
	struct flow_control_service *flow_control_service;
	struct flow_control_instance *instance;
	struct list_head *list;
	unsigned int res;

	res = hdb_handle_get (&flow_control_hdb, flow_control_handle,
		(void *)&instance);
	if (res != 0) {
		goto error_exit;
	}

	for (list = flow_control_service_list_head.next;
		list != &flow_control_service_list_head;
		list = list->next) {

		flow_control_service = list_entry (list, struct flow_control_service, list_all);

		if ((flow_control_service->id_len == id_len) &&
			(memcmp (flow_control_service->id, id, id_len) == 0)) {
			flow_control_xmit (flow_control_service,
				OPENAIS_FLOW_CONTROL_STATE_DISABLED);
			list_del (&flow_control_service->list);
			list_del (&flow_control_service->list_all);
			free (flow_control_service);
			break; /* done - no delete-safe for loop needed */
		}
	}
	hdb_handle_put (&flow_control_hdb, flow_control_handle);

error_exit:
	return (res);
}

/*
 * Disable the ability for new messages to be sent for this service
 * with the handle id of length id_len
 */
unsigned int openais_flow_control_disable (
	unsigned int flow_control_handle)
{
	struct flow_control_instance *instance;
	struct flow_control_service *flow_control_service;
	struct list_head *list;
	unsigned int res;

	res = hdb_handle_get (&flow_control_hdb, flow_control_handle,
		(void *)&instance);
	if (res != 0) {
		goto error_exit;
	}

	for (list = instance->list_head.next;
		list != &instance->list_head;
		list = list->next) {

		flow_control_service = list_entry (list, struct flow_control_service, list);
		flow_control_service->flow_control_state = OPENAIS_FLOW_CONTROL_STATE_DISABLED;
		flow_control_xmit (flow_control_service, OPENAIS_FLOW_CONTROL_STATE_DISABLED);
	}
	hdb_handle_put (&flow_control_hdb, flow_control_handle);

error_exit:
	return (res);
}

/*
 * Enable the ability for new messagess to be sent for this service
 * with the handle id of length id_len
 */
unsigned int openais_flow_control_enable (
	unsigned int flow_control_handle)
{
	struct flow_control_instance *instance;
	struct flow_control_service *flow_control_service;
	struct list_head *list;
	unsigned int res;

	res = hdb_handle_get (&flow_control_hdb, flow_control_handle,
		(void *)&instance);
	if (res != 0) {
		goto error_exit;
	}

	for (list = instance->list_head.next;
		list != &instance->list_head;
		list = list->next) {


		flow_control_service = list_entry (list, struct flow_control_service, list);
		flow_control_service->flow_control_state = OPENAIS_FLOW_CONTROL_STATE_ENABLED;
		flow_control_xmit (flow_control_service, OPENAIS_FLOW_CONTROL_STATE_ENABLED);
	}
	hdb_handle_put (&flow_control_hdb, flow_control_handle);

error_exit:
	return (res);
}
