/*
 * Copyright (c) 2004 Mark Haverkamp
 * Copyright (c) 2004 Open Source Development Lab
 *
 * All rights reserved.
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
 * - Neither the name of the Open Source Development Lab nor the names of its
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

#define DUMP_CHAN_INFO
#define RECOVERY_DEBUG LOG_LEVEL_DEBUG
#define CHAN_DEL_DEBUG LOG_LEVEL_DEBUG
#define CHAN_OPEN_DEBUG LOG_LEVEL_DEBUG
#define CHAN_UNLINK_DEBUG LOG_LEVEL_DEBUG
#define REMOTE_OP_DEBUG LOG_LEVEL_DEBUG

#include <sys/types.h>
#include <malloc.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "../include/ipc_evt.h"
#include "../include/list.h"
#include "../include/queue.h"
#include "util.h"
#include "aispoll.h"
#include "mempool.h"
#include "parse.h"
#include "main.h"
#include "totempg.h"
#include "hdb.h"
#include "clm.h"
#include "evt.h"
#include "swab.h"

#define LOG_SERVICE LOG_SERVICE_EVT
#include "print.h"

static int message_handler_req_lib_activatepoll (struct conn_info *conn_info, 
		void *message);
static int lib_evt_open_channel(struct conn_info *conn_info, void *message);
static int lib_evt_open_channel_async(struct conn_info *conn_info, 
		void *message);
static int lib_evt_close_channel(struct conn_info *conn_info, void *message);
static int lib_evt_unlink_channel(struct conn_info *conn_info, void *message);
static int lib_evt_event_subscribe(struct conn_info *conn_info, 
		void *message);
static int lib_evt_event_unsubscribe(struct conn_info *conn_info, 
		void *message);
static int lib_evt_event_publish(struct conn_info *conn_info, void *message);
static int lib_evt_event_clear_retentiontime(struct conn_info *conn_info, 
		void *message);
static int lib_evt_event_data_get(struct conn_info *conn_info, 
		void *message);

static int evt_conf_change(
		enum totempg_configuration_type configuration_type,
		struct in_addr *member_list, void *member_list_private,
			int member_list_entries,
		struct in_addr *left_list, void *left_list_private,
			int left_list_entries,
		struct in_addr *joined_list, void *joined_list_private,
			int joined_list_entries);

static int evt_initialize(struct conn_info *conn_info, void *msg);
static int evt_finalize(struct conn_info *conn_info);
static int evt_exec_init(void);

static struct libais_handler evt_libais_handlers[] = {
	{
	.libais_handler_fn = 	message_handler_req_lib_activatepoll,
	.response_size = 		sizeof(struct res_lib_activatepoll),
	.response_id = 			MESSAGE_RES_LIB_ACTIVATEPOLL,
	},
	{
	.libais_handler_fn = 	lib_evt_open_channel,
	.response_size = 		sizeof(struct res_evt_channel_open),
	.response_id = 			MESSAGE_RES_EVT_OPEN_CHANNEL,
	},
	{
	.libais_handler_fn = 	lib_evt_open_channel_async,
	.response_size = 		sizeof(struct res_evt_channel_open),
	.response_id = 			MESSAGE_RES_EVT_OPEN_CHANNEL,
	},
	{
	.libais_handler_fn = 	lib_evt_close_channel,
	.response_size = 		sizeof(struct res_evt_channel_close),
	.response_id = 			MESSAGE_RES_EVT_CLOSE_CHANNEL,
	},
	{
	.libais_handler_fn = 	lib_evt_unlink_channel,
	.response_size = 		sizeof(struct res_evt_channel_unlink),
	.response_id = 			MESSAGE_RES_EVT_UNLINK_CHANNEL,
	},
	{
	.libais_handler_fn = 	lib_evt_event_subscribe,
	.response_size = 		sizeof(struct res_evt_event_subscribe),
	.response_id = 			MESSAGE_RES_EVT_SUBSCRIBE,
	},
	{
	.libais_handler_fn = 	lib_evt_event_unsubscribe,
	.response_size = 		sizeof(struct res_evt_event_unsubscribe),
	.response_id = 			MESSAGE_RES_EVT_UNSUBSCRIBE,
	},
	{
	.libais_handler_fn = 	lib_evt_event_publish,
	.response_size = 		sizeof(struct res_evt_event_publish),
	.response_id = 			MESSAGE_RES_EVT_PUBLISH,
	},
	{
	.libais_handler_fn = 	lib_evt_event_clear_retentiontime,
	.response_size = 		sizeof(struct res_evt_event_clear_retentiontime),
	.response_id = 			MESSAGE_RES_EVT_CLEAR_RETENTIONTIME,
	},
	{
	.libais_handler_fn = 	lib_evt_event_data_get,
	.response_size = 		sizeof(struct lib_event_data),
	.response_id = 			MESSAGE_RES_EVT_EVENT_DATA,
	},
};

	
static int evt_remote_evt(void *msg, struct in_addr source_addr, 
		int endian_conversion_required);
static int evt_remote_recovery_evt(void *msg, struct in_addr source_addr, 
		int endian_conversion_required);
static int evt_remote_chan_op(void *msg, struct in_addr source_addr, 
		int endian_conversion_required);

static int (*evt_exec_handler_fns[]) (void *m, struct in_addr s, 
		int endian_conversion_required) = {
	evt_remote_evt,
	evt_remote_chan_op,
	evt_remote_recovery_evt
};

struct service_handler evt_service_handler = {
	.libais_handlers			= evt_libais_handlers,
	.libais_handlers_count		= sizeof(evt_libais_handlers) /
									sizeof(struct libais_handler),
	.aisexec_handler_fns		= evt_exec_handler_fns,
	.aisexec_handler_fns_count	= sizeof(evt_exec_handler_fns) /
									sizeof(int (*)),
	.confchg_fn					= evt_conf_change,
	.libais_init_fn				= evt_initialize,
	.libais_exit_fn				= evt_finalize,
	.exec_init_fn				= evt_exec_init,
	.exec_dump_fn				= 0
};

// TODO static totempg_recovery_plug_handle evt_recovery_plug_handle;

/* 
 * list of all retained events 
 * 		struct event_data
 */
static DECLARE_LIST_INIT(retained_list);

/*
 * list of all event channel information
 * 		struct event_svr_channel_instance
 */
static DECLARE_LIST_INIT(esc_head);

/*
 * list of all unlinked event channel information
 * 		struct event_svr_channel_instance
 */
static DECLARE_LIST_INIT(esc_unlinked_head);

/* 
 * list of all active event conn_info structs.
 * 		struct conn_info
 */
static DECLARE_LIST_INIT(ci_head);


/*
 * Global varaibles used by the event service
 *
 * base_id_top:		upper bits of next event ID to assign
 * base_id:			Lower bits of Next event ID to assign
 * my_node_id:		My cluster node id
 * in_cfg_change:	Config change occurred.  Figure out who sends retained evts.
 * 					cleared when retained events have been delivered.
 * total_members:	how many members in this cluster
 * checked_in:		keep track during config change.
 * any_joined:		did any nodes join on this change?
 * recovery_node:	True if we're the recovery node.
 * tok_call_handle:	totempg token callback handle for recovery.
 * next_retained:	pointer to next retained message to send during recovery.
 * next_chan:		pointer to next channel to send during recovery.
 *
 */
#define BASE_ID_MASK 0xffffffffLL
static SaEvtEventIdT base_id = 0;
static SaEvtEventIdT base_id_top = 0;
static SaClmNodeIdT  my_node_id = 0;
static int			 in_cfg_change = 0;
static int			 total_members = 0;
static int 			 checked_in = 0;
static int			 any_joined = 0;
static int			 recovery_node = 0;
static void 		 *tok_call_handle = 0;
static struct list_head *next_retained = 0;
static struct list_head *next_chan = 0;

/*
 * Structure to track pending channel open requests.
 * 	ocp_async:			1 for async open
 * 	ocp_invocation:		invocation for async open
 * 	ocp_chan_name:		requested channel
 * 	ocp_conn_info:		conn_info for returning to the library.
 * 	ocp_open_flags:		channel open flags
 * 	ocp_timer_handle:	timer handle for sync open
 * 	ocp_entry:			list entry for pending open list.
 */
struct open_chan_pending {
	int					ocp_async;
	SaInvocationT		ocp_invocation;
	SaNameT				ocp_chan_name;
	struct conn_info	*ocp_conn_info;
	SaEvtChannelOpenFlagsT	ocp_open_flag;
	poll_timer_handle	ocp_timer_handle;
	uint32_t			ocp_c_handle;
	struct list_head	ocp_entry;
};

/*
 * list of pending channel opens
 */
static DECLARE_LIST_INIT(open_pending);
static void chan_open_timeout(void *data);

/*
 * Structure to track pending channel unlink requests.
 * ucp_unlink_id:		unlink ID of unlinked channel.
 * ucp_conn_info:		conn_info for returning to the library.
 * ucp_entry:			list entry for pending unlink list.
 */
struct unlink_chan_pending {
	uint64_t	 		ucp_unlink_id;
	struct conn_info	*ucp_conn_info;
 	struct list_head 	ucp_entry;
};

/*
 * list of pending unlink requests
 */
static DECLARE_LIST_INIT(unlink_pending);

/*
 * Next unlink ID
 */
static uint64_t	base_unlink_id = 0;
inline uint64_t
next_chan_unlink_id()
{
	uint64_t uid = my_node_id;
	uid = (uid << 32ULL) | base_unlink_id;
	base_unlink_id = (base_unlink_id + 1ULL) & BASE_ID_MASK;
	return uid;
}

#define min(a,b) ((a) < (b) ? (a) : (b))

/*
 * Throttle event delivery to applications to keep
 * the exec from using too much memory if the app is 
 * slow to process its events.
 */
#define MAX_EVT_DELIVERY_QUEUE	1000
#define MIN_EVT_QUEUE_RESUME	(MAX_EVT_DELIVERY_QUEUE / 2)

#define LOST_PUB "EVENT_SERIVCE"
#define LOST_CHAN "LOST EVENT"
/*
 * Event to send when the delivery queue gets too full
 */
char lost_evt[] = SA_EVT_LOST_EVENT;
static int dropped_event_size;
static struct event_data *dropped_event;
struct evt_pattern {
	SaEvtEventPatternT	pat;
	char 	str[sizeof(lost_evt)];
};
static struct evt_pattern dropped_pattern = {
		.pat	= 	{&dropped_pattern.str[0], 
					sizeof(lost_evt)},
		.str = {SA_EVT_LOST_EVENT}
};

SaNameT lost_chan = {
	.value = LOST_CHAN,
	.length = sizeof(LOST_CHAN)
};

SaNameT dropped_publisher = {
	.value = LOST_PUB,
	.length = sizeof(LOST_PUB)
};

struct event_svr_channel_open;
struct event_svr_channel_subscr;

struct open_count {
	SaClmNodeIdT	oc_node_id;
	int32_t			oc_open_count;
};

/*
 * Structure to contain global channel releated information
 *
 * esc_channel_name:	The name of this channel.
 * esc_total_opens:		The total number of opens on this channel including
 * 						other nodes.
 * esc_local_opens:		The number of opens on this channel for this node.
 * esc_oc_size:			The total number of entries in esc_node_opens;
 * esc_node_opens:		list of node IDs and how many opens are associated.
 * esc_retained_count:	How many retained events for this channel
 * esc_open_chans:		list of opens of this channel.
 * 						(event_svr_channel_open.eco_entry)
 * esc_entry:			links to other channels. (used by esc_head)
 * esc_unlink_id:		If non-zero, then the channel has been marked
 * 						for unlink.  This unlink ID is used to
 * 						mark events still associated with current openings
 * 						so they get delivered to the proper recipients.
 */
struct event_svr_channel_instance {
	SaNameT				esc_channel_name;
	int32_t				esc_total_opens;
	int32_t				esc_local_opens;
	uint32_t			esc_oc_size;
	struct open_count	*esc_node_opens;
	uint32_t			esc_retained_count;
	struct list_head 	esc_open_chans;
	struct list_head 	esc_entry;
	uint64_t			esc_unlink_id;
};

/*
 * Has the event data in the correct format to send to the library API 
 * with aditional field for accounting.
 *
 * ed_ref_count:		how many other strutures are referencing.
 * ed_retained:			retained event list.
 * ed_timer_handle:		Timer handle for retained event expiration.
 * ed_delivered:		arrays of open channel pointers that this event
 * 						has been delivered to. (only used for events 
 * 						with a retention time).
 * ed_delivered_count:	Number of entries available in ed_delivered.
 * ed_delivered_next:	Next free spot in ed_delivered
 * ed_my_chan:			pointer to the global channel instance associated
 * 						with this event.
 * ed_event:			The event data formatted to be ready to send.
 */
struct event_data {
	uint32_t			    			ed_ref_count;
	struct list_head		    		ed_retained;
	poll_timer_handle 					ed_timer_handle;
	struct event_svr_channel_open 	    **ed_delivered;
	uint32_t			    			ed_delivered_count;
	uint32_t			    			ed_delivered_next;
	struct event_svr_channel_instance   *ed_my_chan;
	struct lib_event_data 		    	ed_event;
};

/*
 * Contains a list of pending events to be delivered to a subscribed 
 * application.
 *
 * cel_chan_handle:	associated library channel handle
 * cel_sub_id:		associated library subscription ID
 * cel_event:		event structure to deliver.
 * cel_entry:		list of pending events 
 * 					(struct event_server_instance.esi_events)
 */
struct chan_event_list {
	uint32_t			cel_chan_handle;
	uint32_t			cel_sub_id;
	struct event_data* 	cel_event;
	struct list_head 	cel_entry;
};

/*
 * Contains information about each open for a given channel
 *
 * eco_flags:			How the channel was opened.
 * eco_lib_handle:		channel handle in the app.  Used for event delivery.
 * eco_my_handle:		the handle used to access this data structure.
 * eco_channel:			Pointer to global channel info.
 * eco_entry:			links to other opeinings of this channel.
 * eco_instance_entry:	links to other channel opeinings for the 
 * 						associated server instance.
 * eco_subscr:			head of list of sbuscriptions for this channel open.
 * 						(event_svr_channel_subscr.ecs_entry)
 * eco_conn_info:		refrence to EvtInitialize who owns this open.
 */
struct event_svr_channel_open {
	uint8_t								eco_flags;
	uint32_t							eco_lib_handle;
	uint32_t							eco_my_handle;
	struct event_svr_channel_instance 	*eco_channel;
	struct list_head 					eco_entry;
	struct list_head 					eco_instance_entry;
	struct list_head 					eco_subscr;
	struct conn_info					*eco_conn_info;
};

/*
 * Contains information about each channel subscription
 *
 * ecs_open_chan:		Link to our open channel.
 * ecs_sub_id:			Subscription ID.
 * ecs_filter_count:	number of filters in ecs_filters
 * ecs_filters:			filters for determining event delivery.
 * ecs_entry:			Links to other subscriptions to this channel opening.
 */
struct event_svr_channel_subscr {
	struct event_svr_channel_open	*ecs_open_chan;
	uint32_t						ecs_sub_id;
	SaEvtEventFilterArrayT			*ecs_filters;
	struct list_head 				ecs_entry;
};


/*
 * Member node data
 * mn_node_info:		cluster node info from membership
 * mn_last_evt_id:		last seen event ID for this node
 * mn_started:			Indicates that event service has started
 * 						on this node.
 * mn_next:				pointer to the next node in the hash chain.
 * mn_entry:			List of all nodes.
 */
struct member_node_data {
	struct in_addr		mn_node_addr;
	SaClmClusterNodeT	mn_node_info;
	SaEvtEventIdT		mn_last_evt_id;
	SaClmNodeIdT		mn_started;
	struct member_node_data	*mn_next;
	struct list_head	mn_entry;
};
DECLARE_LIST_INIT(mnd);
/*
 * Take the filters we received from the application via the library and 
 * make them into a real SaEvtEventFilterArrayT
 */
static SaErrorT evtfilt_to_aisfilt(struct req_evt_event_subscribe *req,
		SaEvtEventFilterArrayT **evtfilters)
{

	SaEvtEventFilterArrayT *filta = 
			(SaEvtEventFilterArrayT *)req->ics_filter_data;
	SaEvtEventFilterArrayT *filters;
	SaEvtEventFilterT *filt = (void *)filta + sizeof(SaEvtEventFilterArrayT);
	SaUint8T *str = (void *)filta + sizeof(SaEvtEventFilterArrayT) + 
			(sizeof(SaEvtEventFilterT) * filta->filtersNumber);
	int i;
	int j;

	filters = malloc(sizeof(SaEvtEventFilterArrayT));
	if (!filters) {
		return SA_AIS_ERR_NO_MEMORY;
	}

	filters->filtersNumber = filta->filtersNumber;
	filters->filters = malloc(sizeof(SaEvtEventFilterT) * 
				filta->filtersNumber);
	if (!filters->filters) {
			free(filters);
			return SA_AIS_ERR_NO_MEMORY;
	}

	for (i = 0; i < filters->filtersNumber; i++) {
		filters->filters[i].filter.pattern = 
			malloc(filt[i].filter.patternSize);

		if (!filters->filters[i].filter.pattern) {
				for (j = 0; j < i; j++) {
						free(filters->filters[j].filter.pattern);
				}
				free(filters->filters);
				free(filters);
				return SA_AIS_ERR_NO_MEMORY;
		}
		filters->filters[i].filter.patternSize = 
			filt[i].filter.patternSize;
		memcpy(filters->filters[i].filter.pattern,
				str, filters->filters[i].filter.patternSize);
		filters->filters[i].filterType = filt[i].filterType;
		str += filters->filters[i].filter.patternSize;
	}

	*evtfilters = filters;
	
	return SA_AIS_OK;
}

/*
 * Free up filter data
 */
static void free_filters(SaEvtEventFilterArrayT *fp)
{
	int i;

	for (i = 0; i < fp->filtersNumber; i++) {
		free(fp->filters[i].filter.pattern);
	}

	free(fp->filters);
	free(fp);
}

/*
 * Look up a channel in the global channel list
 */
static struct event_svr_channel_instance *
find_channel(SaNameT *chan_name, uint64_t unlink_id)
{
	struct list_head *l, *head;
	struct event_svr_channel_instance *eci;

	/*
	 * choose which list to look through
	 */
	if (unlink_id == EVT_CHAN_ACTIVE) {
		head = &esc_head;
	} else {
		head = &esc_unlinked_head;
	}

	for (l = head->next; l != head; l = l->next) {

		eci = list_entry(l, struct event_svr_channel_instance, esc_entry);
		if (!name_match(chan_name, &eci->esc_channel_name)) {
			continue;
		} else if (unlink_id != eci->esc_unlink_id) {
			continue;
		}
		return eci;
	}
	return 0;
}

/*
 * Find the last unlinked version of a channel.
 */
static struct event_svr_channel_instance *
find_last_unlinked_channel(SaNameT *chan_name)
{
	struct list_head *l;
	struct event_svr_channel_instance *eci;

	/*
	 * unlinked channels are added to the head of the list
	 * so the first one we see is the last one added.
	 */
	for (l = esc_unlinked_head.next; l != &esc_unlinked_head; l = l->next) {

		eci = list_entry(l, struct event_svr_channel_instance, esc_entry);
		if (!name_match(chan_name, &eci->esc_channel_name)) {
			continue;
		} 
	}
	return 0;
}

/*
 * Create and initialize a channel instance structure
 */
static struct event_svr_channel_instance *create_channel(SaNameT *cn)
{
	struct event_svr_channel_instance *eci;
	eci = (struct event_svr_channel_instance *) malloc(sizeof(*eci));
	if (!eci) {
		return (eci);
	}

	memset(eci, 0, sizeof(*eci));
	list_init(&eci->esc_entry);
	list_init(&eci->esc_open_chans);
	eci->esc_oc_size = total_members;
	eci->esc_node_opens = malloc(sizeof(struct open_count) * total_members);
	if (!eci->esc_node_opens) {
		free(eci);
		return 0;
	}
	memset(eci->esc_node_opens, 0, sizeof(struct open_count) * total_members);
	eci->esc_channel_name = *cn;
	eci->esc_channel_name.value[eci->esc_channel_name.length] = '\0';
	list_add(&eci->esc_entry, &esc_head);

	return eci;
}


/*
 * Make sure that the list of nodes is large enough to hold the whole
 * membership
 */
static int check_open_size(struct event_svr_channel_instance *eci)
{
	if (total_members > eci->esc_oc_size) {
		eci->esc_node_opens = realloc(eci->esc_node_opens, 
							sizeof(struct open_count) * total_members);
		if (!eci->esc_node_opens) {
			log_printf(LOG_LEVEL_WARNING, 
					"Memory error realloc of node list\n");
			return -1;
		}
		memset(&eci->esc_node_opens[eci->esc_oc_size], 0, 
			sizeof(struct open_count) * (total_members - eci->esc_oc_size));
		eci->esc_oc_size = total_members;
	}
	return 0;
}

/*
 * Find the specified node ID in the node list of the channel.
 * If it's not in the list, add it.
 */
static struct open_count* find_open_count(
		struct event_svr_channel_instance *eci,
		SaClmNodeIdT node_id)
{
	int i;

	for (i = 0; i < eci->esc_oc_size; i++) {
		if (eci->esc_node_opens[i].oc_node_id == 0) {
			eci->esc_node_opens[i].oc_node_id = node_id;
			eci->esc_node_opens[i].oc_open_count = 0;
		}
		if (eci->esc_node_opens[i].oc_node_id == node_id) {
			return &eci->esc_node_opens[i];
		}
	}
	log_printf(LOG_LEVEL_DEBUG, 
			"find_open_count: node id 0x%x not found\n",
			node_id);
	return 0;
}

static void dump_chan_opens(struct event_svr_channel_instance *eci)
{
	int i;
	log_printf(LOG_LEVEL_NOTICE,
			"Channel %s, total %d, local %d\n",
			eci->esc_channel_name.value,
			eci->esc_total_opens,
			eci->esc_local_opens);
	for (i = 0; i < eci->esc_oc_size; i++) {
		if (eci->esc_node_opens[i].oc_node_id == 0) {
			break;
		}
		log_printf(LOG_LEVEL_NOTICE, "Node 0x%x, count %d\n",
			eci->esc_node_opens[i].oc_node_id, 
			eci->esc_node_opens[i].oc_open_count);
		}
}

#ifdef DUMP_CHAN_INFO
/*
 * Scan the list of channels and dump the open count info.
 */
static void dump_all_chans()
{
	struct list_head *l;
	struct event_svr_channel_instance *eci;

	for (l = esc_head.next; l != &esc_head; l = l->next) {
		eci = list_entry(l, struct event_svr_channel_instance, esc_entry);
		dump_chan_opens(eci);

	}
}
#endif

/*
 * Replace the current open count for a node with the specified value.
 */
static int set_open_count(struct event_svr_channel_instance *eci,
		SaClmNodeIdT node_id, uint32_t open_count) 
{
	struct open_count *oc;
	int i;

	if ((i = check_open_size(eci)) != 0) {
		return i;
	}

	oc = find_open_count(eci, node_id);
	if (oc) {
		if (oc->oc_open_count) {
			/*
			 * If the open count wasn't zero, then we already
			 * knew about this node.  It should never be different than
			 * what we already had for an open count.
			 */
			if (oc->oc_open_count != open_count) {
				log_printf(LOG_LEVEL_ERROR, 
						"Channel open count error\n");
				dump_chan_opens(eci);
			}
			return 0;
		} 
		log_printf(LOG_LEVEL_DEBUG, 
			"Set count: Chan %s for node 0x%x, was %d, now %d\n",
			eci->esc_channel_name.value,
			node_id, eci->esc_node_opens[i].oc_open_count, open_count);
		eci->esc_total_opens += open_count;
		oc->oc_open_count = open_count;
		return 0;
	}
	return -1;
}

/*
 * Increment the open count for the specified node.
 */
static int inc_open_count(struct event_svr_channel_instance *eci,
		SaClmNodeIdT node_id) 
{

	struct open_count *oc;
	int i;

	if ((i = check_open_size(eci)) != 0) {
		return i;
	}

	if (node_id == my_node_id) {
		eci->esc_local_opens++;
	}
	oc = find_open_count(eci, node_id);
	if (oc) {
		eci->esc_total_opens++;
		oc->oc_open_count++;
		return 0;
	}
	return -1;
}

/*
 * Decrement the open count for the specified node in the 
 * specified channel.
 */
static int dec_open_count(struct event_svr_channel_instance *eci,
		SaClmNodeIdT node_id) 
{

	struct open_count *oc;
	int i;

	if ((i = check_open_size(eci)) != 0) {
		return i;
	}

	if (node_id == my_node_id) {
		eci->esc_local_opens--;
	}
	oc = find_open_count(eci, node_id);
	if (oc) {
		eci->esc_total_opens--;
		oc->oc_open_count--;
		if ((eci->esc_total_opens < 0) || (oc->oc_open_count < 0)) {
			log_printf(LOG_LEVEL_ERROR, "Channel open decrement error\n");
			dump_chan_opens(eci);
		}
		return 0;
	}
	return -1;
}


/*
 * Remove a channel and free its memory if it's not in use anymore.
 */
static void delete_channel(struct event_svr_channel_instance *eci)
{

	log_printf(CHAN_DEL_DEBUG, 
			"Called Delete channel %s t %d, l %d, r %d\n",
			eci->esc_channel_name.value,
			eci->esc_total_opens, eci->esc_local_opens,
			eci->esc_retained_count);
	/*
	 * If no one has the channel open anywhere and there are no unexpired
	 * retained events for this channel, and it has been marked for deletion
	 * by an unlink, then it is OK to delete the data structure.
	 */
	if ((eci->esc_retained_count == 0)  && (eci->esc_total_opens == 0) &&
			(eci->esc_unlink_id != EVT_CHAN_ACTIVE)) {
		log_printf(CHAN_DEL_DEBUG, "Delete channel %s\n",
			eci->esc_channel_name.value);
		log_printf(CHAN_UNLINK_DEBUG, "Delete channel %s, unlink_id %0llx\n",
			eci->esc_channel_name.value, eci->esc_unlink_id);

		if (!list_empty(&eci->esc_open_chans)) {
				log_printf(LOG_LEVEL_NOTICE, 
					"Last channel close request for %s (still open)\n",
					eci->esc_channel_name.value);
				dump_chan_opens(eci);
				return;
		}
		
		/*
		 * adjust if we're sending open counts on a config change.
		 */
		if (in_cfg_change && (&eci->esc_entry == next_chan)) {
			next_chan = eci->esc_entry.next;
		}

		list_del(&eci->esc_entry);
		if (eci->esc_node_opens) {
			free(eci->esc_node_opens);
		}
		free(eci);
	}
}

/*
 * Mark a channel for deletion.
 */
static void unlink_channel(struct event_svr_channel_instance *eci, 
		uint64_t unlink_id)
{
	struct event_data *edp;
	struct list_head *l;

	log_printf(CHAN_UNLINK_DEBUG, "Unlink request: %s, id 0x%llx\n",
			eci->esc_channel_name.value, unlink_id);
	/*
	 * The unlink ID is used to note that the channel has been marked 
	 * for deletion and is a way to distinguish between multiple 
	 * channels of the same name each marked for deletion.
	 */
	eci->esc_unlink_id = unlink_id;

	/*
	 * Move the unlinked channel to the unlinked list.  This way 
	 * we don't have to worry about filtering it out when we need to
	 * distribute retained events at recovery time.
	 */
	list_del(&eci->esc_entry);
	list_add(&eci->esc_entry, &esc_unlinked_head);

	/*
	 * Scan the retained event list and tag any associated with this channel
	 * with the unlink ID so that they get routed properly.
	 */
	for (l = retained_list.next; l != &retained_list; l = l->next) {
		edp = list_entry(l, struct event_data, ed_retained);
		if ((edp->ed_my_chan == eci) && 
				(edp->ed_event.led_chan_unlink_id == EVT_CHAN_ACTIVE)) {
			edp->ed_event.led_chan_unlink_id = unlink_id;
		}
	}

	delete_channel(eci);
}

/*
 * Remove the specified node from the node list in this channel.
 */
static int remove_open_count(
		struct event_svr_channel_instance *eci,
		SaClmNodeIdT node_id)
{
	int i;
	int j;

	/*
	 * Find the node, remove it and re-pack the array.
	 */
	for (i = 0; i < eci->esc_oc_size; i++) {
		if (eci->esc_node_opens[i].oc_node_id == 0) {
			break;
		}

		log_printf(RECOVERY_DEBUG, "roc: %x/%x, t %d, oc %d\n",
			node_id, eci->esc_node_opens[i].oc_node_id,
			eci->esc_total_opens, eci->esc_node_opens[i].oc_open_count);
		
		if (eci->esc_node_opens[i].oc_node_id == node_id) {

			eci->esc_total_opens -= eci->esc_node_opens[i].oc_open_count;

			for (j = i+1; j < eci->esc_oc_size; j++, i++) {
				eci->esc_node_opens[i].oc_node_id = 
				 eci->esc_node_opens[j].oc_node_id;
				eci->esc_node_opens[i].oc_open_count = 
				 eci->esc_node_opens[j].oc_open_count;
			}

			eci->esc_node_opens[eci->esc_oc_size-1].oc_node_id = 0;
			eci->esc_node_opens[eci->esc_oc_size-1].oc_open_count = 0;

			/*
			 * Remove the channel if it's not being used anymore
			 */
			delete_channel(eci);
			return 0;
		}
	}
	return -1;
}


/*
 * Send a request to open a channel to the rest of the cluster.
 */
static SaErrorT evt_open_channel(SaNameT *cn, SaUint8T flgs)
{
	struct req_evt_chan_command cpkt;
	struct event_svr_channel_instance *eci;
	struct iovec chn_iovec;
	int res;
	SaErrorT ret;

	ret = SA_AIS_OK;

	eci = find_channel(cn, EVT_CHAN_ACTIVE);

	/*
	 * If the create flag set, and it doesn't exist, we can make the channel.  
	 * Otherwise, it's an error since we're notified of channels being created 
	 * and opened.
	 */
	if (!eci && !(flgs & SA_EVT_CHANNEL_CREATE)) {
		ret = SA_AIS_ERR_NOT_EXIST;
		goto chan_open_end;
	}

	/*
	 * create the channel packet to send. Tell the the cluster
	 * to create the channel.
	 */
	memset(&cpkt, 0, sizeof(cpkt));
	cpkt.chc_head.id = MESSAGE_REQ_EXEC_EVT_CHANCMD;
	cpkt.chc_head.size = sizeof(cpkt);
	cpkt.chc_op = EVT_OPEN_CHAN_OP;
	cpkt.u.chc_chan = *cn;
	chn_iovec.iov_base = &cpkt;
	chn_iovec.iov_len = cpkt.chc_head.size;
	log_printf(CHAN_OPEN_DEBUG, "evt_open_channel: Send open mcast\n");
	res = totempg_mcast (&chn_iovec, 1, TOTEMPG_AGREED);
	log_printf(CHAN_OPEN_DEBUG, "evt_open_channel: Open mcast result: %d\n",
				res);
	if (res != 0) {
			ret = SA_AIS_ERR_LIBRARY;
	}

chan_open_end:
	return ret;

}

/*
 * Send a request to close a channel with the rest of the cluster.
 */
static SaErrorT evt_close_channel(SaNameT *cn, uint64_t unlink_id)
{
	struct req_evt_chan_command cpkt;
	struct iovec chn_iovec;
	int res;
	SaErrorT ret;

	ret = SA_AIS_OK;

	/*
	 * create the channel packet to send. Tell the the cluster
	 * to close the channel.
	 */
	memset(&cpkt, 0, sizeof(cpkt));
	cpkt.chc_head.id = MESSAGE_REQ_EXEC_EVT_CHANCMD;
	cpkt.chc_head.size = sizeof(cpkt);
	cpkt.chc_op = EVT_CLOSE_CHAN_OP;
	cpkt.u.chcu.chcu_name = *cn;
	cpkt.u.chcu.chcu_unlink_id = unlink_id;
	chn_iovec.iov_base = &cpkt;
	chn_iovec.iov_len = cpkt.chc_head.size;
	res = totempg_mcast (&chn_iovec, 1, TOTEMPG_AGREED);
	if (res != 0) {
			ret = SA_AIS_ERR_LIBRARY;
	}
	return ret;

}

/*
 * Node data access functions.  Used to keep track of event IDs
 * delivery of messages.
 *
 * add_node: 	Add a new member node to our list.
 * remove_node:	Remove a node that left membership.
 * find_node:	Given the node ID return a pointer to node information.
 *
 */
#define NODE_HASH_SIZE 256
static struct member_node_data *nl[NODE_HASH_SIZE] = {0};
inline int 
hash_sock_addr(struct in_addr addr)
{
	return addr.s_addr & (NODE_HASH_SIZE - 1);
}

static struct member_node_data **lookup_node(struct in_addr addr)
{
	int index = hash_sock_addr(addr);
	struct member_node_data **nlp;

	nlp = &nl[index];
	for (nlp = &nl[index]; *nlp; nlp = &((*nlp)->mn_next)) {
		if ((*nlp)->mn_node_addr.s_addr == addr.s_addr) {
			break;
		}
	}

	return nlp;
}

static struct member_node_data *
evt_find_node(struct in_addr addr)
{
	struct member_node_data **nlp;

	nlp = lookup_node(addr);

	if (!nlp) {
		log_printf(LOG_LEVEL_DEBUG, "find_node: Got NULL nlp?\n");
		return 0;
	}

	return *nlp;
}

static SaErrorT
evt_add_node(struct in_addr addr, SaClmClusterNodeT *cn) 
{
	struct member_node_data **nlp;
	struct member_node_data *nl;
	SaErrorT err = SA_AIS_ERR_EXIST;

	nlp = lookup_node(addr);

	if (!nlp) {
		log_printf(LOG_LEVEL_DEBUG, "add_node: Got NULL nlp?\n");
		goto an_out;
	}

	if (*nlp) {
		goto an_out;
	}

	*nlp = malloc(sizeof(struct member_node_data));
	if (!nlp) {
			return SA_AIS_ERR_NO_MEMORY;
	}
	nl = *nlp;
	if (nl) {
		memset(nl, 0, sizeof(*nl));
		err = SA_AIS_OK;
		nl->mn_node_addr = addr;
		nl->mn_started = 1;
	}
	list_init(&nl->mn_entry);
	list_add(&nl->mn_entry, &mnd);
	nl->mn_node_info = *cn;

an_out:
	return err;
}

#ifdef REMOVE_NODE
static SaErrorT
evt_remove_node(struct in_addr addr) 
{
	struct member_node_data **nlp;
	struct member_node_data *nl;
	SaErrorT err = SA_AIS_ERR_NOT_EXIST;

	nlp = lookup_node(addr);

	if (!nlp) {
		log_printf(LOG_LEVEL_DEBUG, "remove_node: Got NULL nlp?\n");
		goto an_out;
	}

	if (!(*nlp)) {
		goto an_out;
	}

	nl = *nlp;

	list_del(&nl->mn_entry);
	*nlp = nl->mn_next;
	free(*nlp);
	err = SA_AIS_OK;

an_out:
	return err;
}
#endif /* REMOVE_NODE */

/*
 * Find the oldest node in the membership.  This is the one we choose to 
 * perform some cluster-wide functions like distributing retained events.
 */
static struct member_node_data* oldest_node()
{
	struct list_head *l;
	struct member_node_data *mn = 0;
	struct member_node_data *oldest = 0;

	for (l = mnd.next; l != &mnd; l = l->next) {
		mn = list_entry(l, struct member_node_data, mn_entry);
		if (mn->mn_started == 0) {
			continue;
		}
		if ((oldest == NULL) || 
				(mn->mn_node_info.bootTimestamp <
					 oldest->mn_node_info.bootTimestamp )) {
			oldest = mn;
		} else if (mn->mn_node_info.bootTimestamp ==
					 oldest->mn_node_info.bootTimestamp) {
			if (mn->mn_node_info.nodeId < oldest->mn_node_info.nodeId) {
				oldest = mn;
			}
		}
	}
	return oldest;
}


/*
 * Token callback routine.  Send as many mcasts as possible to distribute
 * retained events on a config change.
 */
static int send_next_retained(void *data)
{
	struct req_evt_chan_command cpkt;
	struct iovec chn_iovec;
	struct event_data *evt;
	int res;

	if (in_cfg_change && recovery_node) {
		/*
		 * Process messages.  When we're done, send the done message
		 * to the nodes.
		 */
		for (;next_retained != &retained_list; 
								next_retained = next_retained->next) {
			log_printf(LOG_LEVEL_DEBUG, "Sending next retained event\n");
			evt = list_entry(next_retained, struct event_data, ed_retained);
			evt->ed_event.led_head.id = MESSAGE_REQ_EXEC_EVT_RECOVERY_EVENTDATA;
			chn_iovec.iov_base = &evt->ed_event;
			chn_iovec.iov_len = evt->ed_event.led_head.size;
			res = totempg_mcast(&chn_iovec, 1, TOTEMPG_AGREED);

			if (res != 0) {
			/*
			 * Try again later.
			 */
				return -1;
			}
		}
		log_printf(RECOVERY_DEBUG, "DONE Sending retained events\n");
		memset(&cpkt, 0, sizeof(cpkt));
		cpkt.chc_head.id = MESSAGE_REQ_EXEC_EVT_CHANCMD;
		cpkt.chc_head.size = sizeof(cpkt);
		cpkt.chc_op = EVT_CONF_DONE;
		chn_iovec.iov_base = &cpkt;
		chn_iovec.iov_len = cpkt.chc_head.size;
		res = totempg_mcast (&chn_iovec, 1, TOTEMPG_AGREED);
	}
	tok_call_handle = 0;
	return 0;
}

/*
 * Send our retained events. If we've been chosen as the recovery node, kick
 * kick off the process of sending retained events.
 */
static void send_retained()
{
	struct req_evt_chan_command cpkt;
	struct iovec chn_iovec;
	int res = 0;

	if (list_empty(&retained_list) || !any_joined) {
		memset(&cpkt, 0, sizeof(cpkt));
		cpkt.chc_head.id = MESSAGE_REQ_EXEC_EVT_CHANCMD;
		cpkt.chc_head.size = sizeof(cpkt);
		cpkt.chc_op = EVT_CONF_DONE;
		chn_iovec.iov_base = &cpkt;
		chn_iovec.iov_len = cpkt.chc_head.size;
		log_printf(RECOVERY_DEBUG, "No messages to send\n");
		res = totempg_mcast (&chn_iovec, 1, TOTEMPG_AGREED);
	} else {
		log_printf(RECOVERY_DEBUG, 
					"Start sending retained messages\n");
		recovery_node = 1;
		next_retained = retained_list.next;
// TODO		res = totempg_token_callback_create(&tok_call_handle, send_next_retained,
//				NULL);
	}
	if (res != 0) {
		log_printf(LOG_LEVEL_ERROR, "ERROR sending evt recovery data\n");
	}
}

/*
 * 	Token callback routine.  Send as many mcasts as possible to distribute
 *  open counts on a config change.
 */
static int send_next_open_count(void *data)
{
	struct req_evt_chan_command cpkt;
	struct iovec chn_iovec;
	struct event_svr_channel_instance *eci;
	int res;

	if (in_cfg_change) {
		/*
		 * Process messages.  When we're done, send the done message
		 * to the nodes.
		 */
		memset(&cpkt, 0, sizeof(cpkt));
		for (;next_chan != &esc_head; 
								next_chan = next_chan->next) {
			log_printf(RECOVERY_DEBUG, "Sending next open count\n");
			eci = list_entry(next_chan, struct event_svr_channel_instance, 
					esc_entry);
			cpkt.chc_head.id = MESSAGE_REQ_EXEC_EVT_CHANCMD;
			cpkt.chc_head.size = sizeof(cpkt);
			cpkt.chc_op = EVT_OPEN_COUNT;
			cpkt.u.chc_set_opens.chc_chan_name = eci->esc_channel_name;
			cpkt.u.chc_set_opens.chc_open_count = eci->esc_local_opens;
			chn_iovec.iov_base = &cpkt;
			chn_iovec.iov_len = cpkt.chc_head.size;
			res = totempg_mcast(&chn_iovec, 1,TOTEMPG_AGREED);

			if (res != 0) {
			/*
			 * Try again later.
			 */
				return -1;
			}
		}
		log_printf(RECOVERY_DEBUG, "DONE Sending open counts\n");
		memset(&cpkt, 0, sizeof(cpkt));
		cpkt.chc_head.id = MESSAGE_REQ_EXEC_EVT_CHANCMD;
		cpkt.chc_head.size = sizeof(cpkt);
		cpkt.chc_op = EVT_OPEN_COUNT_DONE;
		chn_iovec.iov_base = &cpkt;
		chn_iovec.iov_len = cpkt.chc_head.size;
		res = totempg_mcast (&chn_iovec, 1,TOTEMPG_AGREED);
	}
	tok_call_handle = 0;
	return 0;
}

/*
 * kick off the process of sending open channel counts during recovery.
 * Every node does this.
 */
static void send_open_count()
{
	struct req_evt_chan_command cpkt;
	struct iovec chn_iovec;
	int res;

	if (list_empty(&esc_head)) {
		memset(&cpkt, 0, sizeof(cpkt));
		cpkt.chc_head.id = MESSAGE_REQ_EXEC_EVT_CHANCMD;
		cpkt.chc_head.size = sizeof(cpkt);
		cpkt.chc_op = EVT_OPEN_COUNT_DONE;
		chn_iovec.iov_base = &cpkt;
		chn_iovec.iov_len = cpkt.chc_head.size;
		log_printf(RECOVERY_DEBUG, "No channels to send\n");
		res = totempg_mcast (&chn_iovec, 1,TOTEMPG_AGREED);
	} else {
		log_printf(RECOVERY_DEBUG, 
					"Start sending open channel count\n");
		next_chan = esc_head.next;
// TODO		res = totempg_token_callback_create(&tok_call_handle, send_next_open_count,
//				NULL);
	}
	if (res != 0) {
		log_printf(LOG_LEVEL_ERROR, "ERROR sending evt recovery data\n");
	}
}

/*
 * keep track of the last event ID from a node.
 * If we get an event ID less than our last, we've already
 * seen it.  It's probably a retained event being sent to 
 * a new node.
 */
static int check_last_event(struct lib_event_data *evtpkt, 
				struct in_addr addr)
{
	struct member_node_data *nd;
	SaClmClusterNodeT *cn;


	nd = evt_find_node(addr);
	if (!nd) {
		log_printf(LOG_LEVEL_DEBUG, 
				"Node ID 0x%x not found for event %llx\n",
				evtpkt->led_publisher_node_id, evtpkt->led_event_id);
		cn = clm_get_by_nodeid(addr);
		if (!cn) {
			log_printf(LOG_LEVEL_DEBUG, 
					"Cluster Node 0x%x not found for event %llx\n",
				evtpkt->led_publisher_node_id, evtpkt->led_event_id);
		} else {
			evt_add_node(addr, cn);
			nd = evt_find_node(addr);
		}
	}

	if (!nd) {
		return 0;
	}

	if ((nd->mn_last_evt_id < evtpkt->led_event_id)) {
		nd->mn_last_evt_id = evtpkt->led_event_id;
		return 0;
	}
	return 1;
}

/*
 * Send a message to the app to wake it up if it is polling
 */
static int message_handler_req_lib_activatepoll(struct conn_info *conn_info, 
		void *message)
{
	struct res_lib_activatepoll res;

	res.header.error = SA_AIS_OK;
	res.header.size = sizeof (struct res_lib_activatepoll);
	res.header.id = MESSAGE_RES_LIB_ACTIVATEPOLL;
	libais_send_response(conn_info, &res, sizeof(res));

	return (0);
}

/*
 * event id generating code.  We use the node ID for this node for the
 * upper 32 bits of the event ID to make sure that we can generate a cluster
 * wide unique event ID for a given event.
 */
SaErrorT set_event_id(SaClmNodeIdT node_id)
{
	SaErrorT err = SA_AIS_OK;
	if (base_id_top) {
		err =  SA_AIS_ERR_EXIST;
	}
	base_id_top = (SaEvtEventIdT)node_id << 32;
	return err;
}

static SaErrorT get_event_id(uint64_t *event_id)
{
	*event_id = base_id_top | base_id ;
	base_id = (base_id + 1) & BASE_ID_MASK;
	return SA_AIS_OK;
}



/*
 * Free up an event structure if it isn't being used anymore.
 */
static void
free_event_data(struct event_data *edp)
{
	if (--edp->ed_ref_count) {
		return;
	}
	log_printf(LOG_LEVEL_DEBUG, "Freeing event ID: 0x%llx\n", 
			edp->ed_event.led_event_id);
	if (edp->ed_delivered) {
		free(edp->ed_delivered);
	}

	free(edp);
}

/*
 * Timer handler to delete expired events.
 *
 */
static void
event_retention_timeout(void *data)
{
	struct event_data *edp = data;
	log_printf(LOG_LEVEL_DEBUG, "Event ID %llx expired\n", 
					edp->ed_event.led_event_id);
	/*
	 * adjust next_retained if we're in recovery and 
	 * were in charge of sending retained events.
	 */
	if (in_cfg_change && recovery_node) {
		if (next_retained == &edp->ed_retained) {
			next_retained = edp->ed_retained.next;
		}
	}
	list_del(&edp->ed_retained);
	list_init(&edp->ed_retained);
	/*
	 * Check to see if the channel isn't in use anymore.
	 */
	edp->ed_my_chan->esc_retained_count--;
	if (edp->ed_my_chan->esc_retained_count == 0) {
		delete_channel(edp->ed_my_chan);
	}
	free_event_data(edp);
}

/*
 * clear a particular event's retention time.
 * This will free the event as long as it isn't being
 * currently used.
 *
 */
static void
clear_retention_time(SaEvtEventIdT event_id)
{
	struct event_data *edp;
	struct list_head *l, *nxt;
	int ret;

	log_printf(LOG_LEVEL_DEBUG, "Search for Event ID %llx\n", event_id);
	for (l = retained_list.next; l != &retained_list; l = nxt) {
		nxt = l->next;
		edp = list_entry(l, struct event_data, ed_retained);
		if (edp->ed_event.led_event_id != event_id) {
				continue;
		}

		log_printf(LOG_LEVEL_DEBUG, 
							"Clear retention time for Event ID %llx\n", 
				edp->ed_event.led_event_id);
		ret = poll_timer_delete(aisexec_poll_handle, edp->ed_timer_handle);
		if (ret != 0 ) {
			log_printf(LOG_LEVEL_ERROR, "Error expiring event ID %llx\n",
							edp->ed_event.led_event_id);
			return;
		}
		edp->ed_event.led_retention_time = 0;
		list_del(&edp->ed_retained);
		list_init(&edp->ed_retained);

		/*
		 * Check to see if the channel isn't in use anymore.
		 */
		edp->ed_my_chan->esc_retained_count--;
		if (edp->ed_my_chan->esc_retained_count == 0) {
			delete_channel(edp->ed_my_chan);
		}
		free_event_data(edp);
		break;
	}
}

/*
 * Remove specified channel from event delivery list
 */
static void
remove_delivered_channel(struct event_svr_channel_open *eco)
{
	int i;
	struct list_head *l;
	struct event_data *edp;

	for (l = retained_list.next; l != &retained_list; l = l->next) {
		edp = list_entry(l, struct event_data, ed_retained);

		for (i = 0; i < edp->ed_delivered_next; i++) {
			if (edp->ed_delivered[i] == eco) {
				edp->ed_delivered_next--;
				if (edp->ed_delivered_next == i) {
					break;
				}
				memmove(&edp->ed_delivered[i],
					&edp->ed_delivered[i+1],
					&edp->ed_delivered[edp->ed_delivered_next] - 
					   &edp->ed_delivered[i]);
				break;
			}
		}
	}
	return;
}

/*
 * If there is a retention time, add this open channel to the event so 
 * we can check if we've already delivered this message later if a new
 * subscription matches.
 */
#define DELIVER_SIZE 8
static void
evt_delivered(struct event_data *evt, struct event_svr_channel_open *eco)
{
	if (!evt->ed_event.led_retention_time) {
		return;
	}

	log_printf(LOG_LEVEL_DEBUG, "delivered ID %llx to eco %p\n", 
			evt->ed_event.led_event_id, eco);
	if (evt->ed_delivered_count == evt->ed_delivered_next) {
		evt->ed_delivered = realloc(evt->ed_delivered,
			DELIVER_SIZE * sizeof(struct event_svr_channel_open *));
		memset(evt->ed_delivered + evt->ed_delivered_next, 0, 
			DELIVER_SIZE * sizeof(struct event_svr_channel_open *));
		evt->ed_delivered_next = evt->ed_delivered_count;
		evt->ed_delivered_count += DELIVER_SIZE;
	}

	evt->ed_delivered[evt->ed_delivered_next++] = eco;
}

/*
 * Check to see if an event has already been delivered to this open channel
 */
static int
evt_already_delivered(struct event_data *evt, 
		struct event_svr_channel_open *eco)
{
	int i;

	if (!evt->ed_event.led_retention_time) {
		return 0;
	}

	log_printf(LOG_LEVEL_DEBUG, "Deliver count: %d deliver_next %d\n", 
		evt->ed_delivered_count, evt->ed_delivered_next);
	for (i = 0; i < evt->ed_delivered_next; i++) {
		log_printf(LOG_LEVEL_DEBUG, "Checking ID %llx delivered %p eco %p\n", 
			evt->ed_event.led_event_id, evt->ed_delivered[i], eco);
		if (evt->ed_delivered[i] == eco) {
			return 1;
		}
	}
	return 0;
}

/*
 * Compare a filter to a given pattern.
 * return SA_AIS_OK if the pattern matches a filter
 */
static SaErrorT
filter_match(SaEvtEventPatternT *ep, SaEvtEventFilterT *ef)
{
	int ret;
	ret = SA_AIS_ERR_FAILED_OPERATION;

	switch (ef->filterType) {
	case SA_EVT_PREFIX_FILTER:
		if (ef->filter.patternSize > ep->patternSize) {
			break;
		}
		if (strncmp(ef->filter.pattern, ep->pattern,
					ef->filter.patternSize) == 0) {
			ret = SA_AIS_OK;
		}
		break;
	case SA_EVT_SUFFIX_FILTER:
		if (ef->filter.patternSize > ep->patternSize) {
			break;
		}
		if (strncmp(ef->filter.pattern, 
			&ep->pattern[ep->patternSize - ef->filter.patternSize],
					ef->filter.patternSize) == 0) {
			ret = SA_AIS_OK;
		}
		
		break;
	case SA_EVT_EXACT_FILTER:
		if (ef->filter.patternSize != ep->patternSize) {
			break;
		}
		if (strncmp(ef->filter.pattern, ep->pattern,
					ef->filter.patternSize) == 0) {
			ret = SA_AIS_OK;
		}
		break;
	case SA_EVT_PASS_ALL_FILTER:
		ret = SA_AIS_OK;
		break;
	default:
		break;
	}
	return ret;
}

/*
 * compare the event's patterns with the subscription's filter rules.
 * SA_AIS_OK is returned if the event matches the filter rules.
 */
static SaErrorT
event_match(struct event_data *evt, 
			struct event_svr_channel_subscr *ecs)
{
	SaEvtEventFilterT *ef;
	SaEvtEventPatternT *ep;
	uint32_t filt_count;
	SaErrorT ret =  SA_AIS_OK;
	int i;

	ep = (SaEvtEventPatternT *)(&evt->ed_event.led_body[0]);
	ef = ecs->ecs_filters->filters;
	filt_count = min(ecs->ecs_filters->filtersNumber, 
			evt->ed_event.led_patterns_number);

	for (i = 0; i < filt_count; i++) {
		ret = filter_match(ep, ef);
		if (ret != SA_AIS_OK) {
			break;
		}
		ep++;
		ef++;
	}
	return ret;
}

/*
 * Scan undelivered pending events and either remove them if no subscription
 * filters match anymore or re-assign them to another matching subscription
 */
static void
filter_undelivered_events(struct event_svr_channel_open *op_chan)
{
	struct event_svr_channel_open *eco;
	struct event_svr_channel_instance *eci;
	struct event_svr_channel_subscr *ecs;
	struct chan_event_list *cel;
	struct libevt_ci *esip = &op_chan->eco_conn_info->ais_ci.u.libevt_ci;
	struct list_head *l, *nxt;
	struct list_head *l1, *l2;
	int i;

	eci = op_chan->eco_channel;

	/*
	 * Scan each of the priority queues for messages
	 */
	for (i = SA_EVT_HIGHEST_PRIORITY; i <= SA_EVT_LOWEST_PRIORITY; i++) {
		/*
		 * examine each message queued for delivery
		 */
		for (l = esip->esi_events[i].next; l != &esip->esi_events[i]; l = nxt) {
			nxt = l->next;
			cel = list_entry(l, struct chan_event_list, cel_entry);
			/*
		 	 * Check open channels
		 	 */
			 for (l1 = eci->esc_open_chans.next; 
								l1 != &eci->esc_open_chans; l1 = l1->next) {
				 eco = list_entry(l1, struct event_svr_channel_open, eco_entry);

				 /*
				  * See if this channel open instance belongs
				  * to this evtinitialize instance
				  */
				 if (eco->eco_conn_info != op_chan->eco_conn_info) {
					 continue;
				 }

				 /*
				  * See if enabled to receive
				  */
				 if (!(eco->eco_flags & SA_EVT_CHANNEL_SUBSCRIBER)) {
					continue;
				 }

				 /*
				  * Check subscriptions
				  */
				 for (l2 = eco->eco_subscr.next; 
									l2 != &eco->eco_subscr; l2 = l2->next) {
					 ecs = list_entry(l2, 
								struct event_svr_channel_subscr, ecs_entry);
					 if (event_match(cel->cel_event, ecs) == SA_AIS_OK) {
						 /*
						  * Something still matches.  
						  * We'll assign it to
						  * the new subscription.
						  */
						 cel->cel_sub_id = ecs->ecs_sub_id;
						 cel->cel_chan_handle = eco->eco_lib_handle;
						 goto next_event;
					 }
				 }
			 }
			 /*
			  * No subscription filter matches anymore.  We
			  * can delete this event.
			  */
			 list_del(&cel->cel_entry);
			 list_init(&cel->cel_entry);
			 esip->esi_nevents--;
		
			 free_event_data(cel->cel_event);
			 free(cel);
next_event:
			 continue;
		 }
	}
}

/*
 * Notify the library of a pending event
 */
static void __notify_event(struct conn_info *conn_info)
{
	struct res_evt_event_data res;
	struct libevt_ci *esip = &conn_info->ais_ci.u.libevt_ci;

	log_printf(LOG_LEVEL_DEBUG, "DELIVER: notify\n");
	if (esip->esi_nevents != 0) {
		res.evd_head.size = sizeof(res);
		res.evd_head.id = MESSAGE_RES_EVT_AVAILABLE;
		res.evd_head.error = SA_AIS_OK;
		libais_send_response(conn_info, &res, sizeof(res));
	}

}
inline void notify_event(struct conn_info *conn_info)
{
	struct libevt_ci *esip = &conn_info->ais_ci.u.libevt_ci;

	/*
	 * Give the library a kick if there aren't already
	 * events queued for delivery.
	 */
	if (esip->esi_nevents++ == 0) {
		__notify_event(conn_info);
	}
}

/*
 * sends/queues up an event for a subscribed channel.
 */
static void
deliver_event(struct event_data *evt, 
		struct event_svr_channel_open *eco,
		struct event_svr_channel_subscr *ecs)
{
	struct chan_event_list *ep;
	struct libevt_ci *esip = &eco->eco_conn_info->ais_ci.u.libevt_ci;
	SaEvtEventPriorityT evt_prio = evt->ed_event.led_priority;
	struct chan_event_list *cel;
	int do_deliver_event = 0;
	int do_deliver_warning = 0;
	int i;

	if (evt_prio > SA_EVT_LOWEST_PRIORITY) {
		evt_prio = SA_EVT_LOWEST_PRIORITY;
	}

	/*
	 * Delivery queue check.
	 * - If the queue is blocked, see if we've sent enough messages to
	 *   unblock it.
	 * - If it isn't blocked, see if this message will put us over the top.
	 * - If we can't deliver this message, see if we can toss some lower
	 *   priority message to make room for this one.
	 * - If we toss any messages, queue up an event of SA_EVT_LOST_EVENT_PATTERN
	 *   to let the application know that we dropped some messages.
	 */
	if (esip->esi_queue_blocked) {
		if (esip->esi_nevents < MIN_EVT_QUEUE_RESUME) {
			esip->esi_queue_blocked = 0;
			log_printf(LOG_LEVEL_DEBUG, "unblock\n");
		}
	}

	if (!esip->esi_queue_blocked && 
							(esip->esi_nevents >= MAX_EVT_DELIVERY_QUEUE)) {
		log_printf(LOG_LEVEL_DEBUG, "block\n");
		esip->esi_queue_blocked = 1;
		do_deliver_warning = 1;
	}

	if (esip->esi_queue_blocked) {
		do_deliver_event = 0;
		for (i = SA_EVT_LOWEST_PRIORITY; i > evt_prio; i--) {
			if (!list_empty(&esip->esi_events[i])) {
				/*
				 * Get the last item on the list, so we drop the most 
				 * recent lowest priority event.
				 */
				cel = list_entry(esip->esi_events[i].prev, 
											struct chan_event_list, cel_entry);
				log_printf(LOG_LEVEL_DEBUG, "Drop 0x%0llx\n",
					cel->cel_event->ed_event.led_event_id);
				list_del(&cel->cel_entry);
				free_event_data(cel->cel_event);
				free(cel);
				esip->esi_nevents--;
				do_deliver_event = 1;
				break;
			}
		}
	} else {
		do_deliver_event = 1;
	}

	/*
	 * Queue the event for delivery
	 */
	if (do_deliver_event) {
		ep = malloc(sizeof(*ep));
		if (!ep) {
			log_printf(LOG_LEVEL_WARNING, 
						"3Memory allocation error, can't deliver event\n");
			return;
		}
		evt->ed_ref_count++;
		ep->cel_chan_handle = eco->eco_lib_handle;
		ep->cel_sub_id = ecs->ecs_sub_id;
		list_init(&ep->cel_entry);
		ep->cel_event = evt;
		list_add_tail(&ep->cel_entry, &esip->esi_events[evt_prio]);
		evt_delivered(evt, eco);
		notify_event(eco->eco_conn_info);
	} 

	/*
	 * If we dropped an event, queue this so that the application knows
	 * what has happened.
	 */
	if (do_deliver_warning) {
		struct event_data *ed;
		ed = malloc(dropped_event_size);
		if (!ed) {
			log_printf(LOG_LEVEL_WARNING, 
						"4Memory allocation error, can't deliver event\n");
			return;
		}
		log_printf(LOG_LEVEL_DEBUG, "Warn 0x%0llx\n", 
								evt->ed_event.led_event_id);
		memcpy(ed, dropped_event, dropped_event_size);
		ed->ed_event.led_publish_time = clust_time_now();
		ed->ed_event.led_event_id = SA_EVT_EVENTID_LOST;
		list_init(&ed->ed_retained);

		ep = malloc(sizeof(*ep));
		if (!ep) {
			log_printf(LOG_LEVEL_WARNING, 
						"5Memory allocation error, can't deliver event\n");
			return;
		}
		ep->cel_chan_handle = eco->eco_lib_handle;
		ep->cel_sub_id = ecs->ecs_sub_id;
		list_init(&ep->cel_entry);
		ep->cel_event = ed;
		list_add_tail(&ep->cel_entry, &esip->esi_events[SA_EVT_HIGHEST_PRIORITY]);
		notify_event(eco->eco_conn_info);
	}
}

/*
 * Take the event data and swap the elements so they match our architectures
 * word order.
 */
static void
convert_event(struct lib_event_data *evt)
{
	SaEvtEventPatternT *eps;
	int i;

	/*
	 * The following elements don't require processing:
	 *
	 * converted in the main deliver_fn:
	 * led_head.id, led_head.size.
	 *
	 * Supplied localy:
	 * source_addr, publisher_node_id, receive_time.
	 *
	 * Used internaly only:
	 * led_svr_channel_handle and led_lib_channel_handle.
	 */

	evt->led_chan_name.length = swab16(evt->led_chan_name.length);
	evt->led_chan_unlink_id = swab64(evt->led_chan_unlink_id);
	evt->led_event_id = swab64(evt->led_event_id);
	evt->led_sub_id = swab32(evt->led_sub_id);
	evt->led_publisher_name.length = swab32(evt->led_publisher_name.length);
	evt->led_retention_time = swab64(evt->led_retention_time);
	evt->led_publish_time = swab64(evt->led_publish_time);
	evt->led_user_data_offset = swab32(evt->led_user_data_offset);
	evt->led_user_data_size = swab32(evt->led_user_data_size);
	evt->led_patterns_number = swab32(evt->led_patterns_number);

	/*
	 * Now we need to go through the led_body and swizzle pattern counts.
	 * We can't do anything about user data since it doesn't have a specified
	 * format.  The application is on its own here.
	 */
	eps = (SaEvtEventPatternT *)evt->led_body;  
	for (i = 0; i < evt->led_patterns_number; i++) {
		eps->patternSize = swab32(eps->patternSize);
		eps++;
	}
	
}

/*
 * Take an event received from the network and fix it up to be usable.
 * - fix up pointers for pattern list.
 * - fill in some channel info
 */
static struct event_data *
make_local_event(struct lib_event_data *p, 
			struct event_svr_channel_instance *eci)
{
	struct event_data *ed;
	SaEvtEventPatternT *eps;
	SaUint8T *str;
	uint32_t ed_size;
	int i;

	ed_size = sizeof(*ed) + p->led_user_data_offset + p->led_user_data_size;
	ed = malloc(ed_size);
	if (!ed) {
			return 0;
	}
	memset(ed, 0, ed_size);
	list_init(&ed->ed_retained);
	ed->ed_my_chan = eci;

	/*
	 * Fill in lib_event_data and make the pattern pointers valid
	 */
	memcpy(&ed->ed_event, p, sizeof(*p) + 
					p->led_user_data_offset + p->led_user_data_size);

	eps = (SaEvtEventPatternT *)ed->ed_event.led_body;  
	str = ed->ed_event.led_body + 
			(ed->ed_event.led_patterns_number * sizeof(SaEvtEventPatternT));
	for (i = 0; i < ed->ed_event.led_patterns_number; i++) {
		eps->pattern = str;
		str += eps->patternSize;
		eps++;
	}

	ed->ed_ref_count++;
	return ed;
}

/*
 * Set an event to be retained.
 */
static void retain_event(struct event_data *evt)
{
	uint32_t ret;
	int msec_in_future;

	evt->ed_ref_count++;
	evt->ed_my_chan->esc_retained_count++;
	list_add_tail(&evt->ed_retained, &retained_list);
	/*
	 * Time in nanoseconds - convert to miliseconds
	 */
	msec_in_future = (uint32_t)((evt->ed_event.led_retention_time) / 1000000ULL);
	ret = poll_timer_add(aisexec_poll_handle,
					msec_in_future,
					evt,
					event_retention_timeout,
					&evt->ed_timer_handle);
	if (ret != 0) {
		log_printf(LOG_LEVEL_ERROR, 
				"retention of event id 0x%llx failed\n",
				evt->ed_event.led_event_id);
	} else {
		log_printf(LOG_LEVEL_DEBUG, "Retain event ID 0x%llx\n", 
					evt->ed_event.led_event_id);
	}
}

/*
 * Scan the subscription list and look for the specified subsctiption ID.
 * Only look for the ID in subscriptions that are associated with the 
 * saEvtInitialize associated with the specified open channel.
 */
static struct event_svr_channel_subscr *find_subscr(
		struct event_svr_channel_open *open_chan, SaEvtSubscriptionIdT sub_id)
{
	struct event_svr_channel_instance *eci;
	struct event_svr_channel_subscr *ecs;
	struct event_svr_channel_open	*eco;
	struct list_head *l, *l1;
	struct conn_info* conn_info = open_chan->eco_conn_info;

	eci = open_chan->eco_channel;

	/*
	 * Check for subscription id already in use.
	 * Subscriptions are unique within saEvtInitialize (Callback scope).
	 */
    for (l = eci->esc_open_chans.next; l != &eci->esc_open_chans; l = l->next) {
		eco = list_entry(l, struct event_svr_channel_open, eco_entry);
		/*
		 * Don't bother with open channels associated with another 
		 * EvtInitialize
		 */
		if (eco->eco_conn_info != conn_info) {
			continue;
		}

		for (l1 = eco->eco_subscr.next; l1 != &eco->eco_subscr; l1 = l1->next) {
			ecs = list_entry(l1, struct event_svr_channel_subscr, ecs_entry);
			if (ecs->ecs_sub_id == sub_id) {
				return ecs;
			}
		}
	}
	return 0;
}

/*
 * Handler for saEvtInitialize
 */
static int evt_initialize(struct conn_info *conn_info, void *msg)
{
	struct res_lib_init res;
	struct libevt_ci *libevt_ci = &conn_info->ais_ci.u.libevt_ci;
	int i;

	
	res.header.size = sizeof (struct res_lib_init);
	res.header.id = MESSAGE_RES_INIT;
	res.header.error = SA_AIS_OK;

	log_printf(LOG_LEVEL_DEBUG, "saEvtInitialize request.\n");
	if (!conn_info->authenticated) {
		log_printf(LOG_LEVEL_ERROR, "event service: Not authenticated\n");
		res.header.error = SA_AIS_ERR_LIBRARY;
		libais_send_response(conn_info, &res, sizeof(res));
		return -1;
	}

	memset(libevt_ci, 0, sizeof(*libevt_ci));
	list_init(&libevt_ci->esi_open_chans);
	for (i = SA_EVT_HIGHEST_PRIORITY; i <= SA_EVT_LOWEST_PRIORITY; i++) {
		list_init(&libevt_ci->esi_events[i]);
	}
	conn_info->service = SOCKET_SERVICE_EVT;
	list_init (&conn_info->conn_list);
	list_add_tail(&conn_info->conn_list, &ci_head);
	libais_send_response (conn_info, &res, sizeof(res));

	return 0;
}

/*
 * Handler for saEvtChannelOpen
 */
static int lib_evt_open_channel(struct conn_info *conn_info, void *message)
{
	SaErrorT error;
	struct req_evt_channel_open *req;
	struct res_evt_channel_open res;
	struct open_chan_pending *ocp;
	int msec_in_future;
	int ret;

	req = message;


	log_printf(CHAN_OPEN_DEBUG, 
			"saEvtChannelOpen (Open channel request)\n");
	log_printf(CHAN_OPEN_DEBUG, 
			"handle 0x%x, to 0x%llx\n",
			req->ico_c_handle,
			req->ico_timeout);
	log_printf(CHAN_OPEN_DEBUG, "flags %x, channel name(%d)  %s\n",
			req->ico_open_flag,
			req->ico_channel_name.length,
			req->ico_channel_name.value);
	/*
	 * Open the channel.
	 *
	 */
	error = evt_open_channel(&req->ico_channel_name, req->ico_open_flag);

	if (error != SA_AIS_OK) {
		goto open_return;
	}

	ocp = malloc(sizeof(struct open_chan_pending));
	if (!ocp) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto open_return;
	}

	ocp->ocp_async = 0;
	ocp->ocp_invocation = 0;
	ocp->ocp_chan_name = req->ico_channel_name;
	ocp->ocp_open_flag = req->ico_open_flag;
	ocp->ocp_conn_info = conn_info;
	ocp->ocp_c_handle = req->ico_c_handle;
	ocp->ocp_timer_handle = 0;
	list_init(&ocp->ocp_entry);
	list_add_tail(&ocp->ocp_entry, &open_pending);
	if (req->ico_timeout != 0) {
		/*
		 * Time in nanoseconds - convert to miliseconds
		 */
		msec_in_future = (uint32_t)(req->ico_timeout / 1000000ULL);
		ret = poll_timer_add(aisexec_poll_handle,
				msec_in_future,
				ocp,
				chan_open_timeout,
				&ocp->ocp_timer_handle);
		if (ret != 0) {
			log_printf(LOG_LEVEL_WARNING, 
					"Error setting timeout for open channel %s\n",
					req->ico_channel_name.value);
		}
	}
	return 0;


open_return:
	res.ico_head.size = sizeof(res);
	res.ico_head.id = MESSAGE_RES_EVT_OPEN_CHANNEL;
	res.ico_head.error = error;
	libais_send_response (conn_info, &res, sizeof(res));

	return 0;
}

/*
 * Handler for saEvtChannelOpen
 */
static int lib_evt_open_channel_async(struct conn_info *conn_info, 
		void *message)
{
	SaErrorT error;
	struct req_evt_channel_open *req;
	struct res_evt_channel_open res;
	struct open_chan_pending *ocp;
	int msec_in_future;
	int ret;

	req = message;


	log_printf(CHAN_OPEN_DEBUG, 
			"saEvtChannelOpenAsync (Async Open channel request)\n");
	log_printf(CHAN_OPEN_DEBUG, 
			"handle 0x%x, to 0x%x\n",
			req->ico_c_handle,
			req->ico_invocation);
	log_printf(CHAN_OPEN_DEBUG, "flags %x, channel name(%d)  %s\n",
			req->ico_open_flag,
			req->ico_channel_name.length,
			req->ico_channel_name.value);
	/*
	 * Open the channel.
	 *
	 */
	error = evt_open_channel(&req->ico_channel_name, req->ico_open_flag);

	if (error != SA_AIS_OK) {
		goto open_return;
	}

	ocp = malloc(sizeof(struct open_chan_pending));
	if (!ocp) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto open_return;
	}

	ocp->ocp_async = 1;
	ocp->ocp_invocation = req->ico_invocation;
	ocp->ocp_c_handle = req->ico_c_handle;
	ocp->ocp_chan_name = req->ico_channel_name;
	ocp->ocp_open_flag = req->ico_open_flag;
	ocp->ocp_conn_info = conn_info;
	ocp->ocp_timer_handle = 0;
	list_init(&ocp->ocp_entry);
	list_add_tail(&ocp->ocp_entry, &open_pending);
	if (req->ico_timeout != 0) {
		/*
		 * Time in nanoseconds - convert to miliseconds
		 */
		msec_in_future = (uint32_t)(req->ico_timeout / 1000000ULL);
		ret = poll_timer_add(aisexec_poll_handle,
				msec_in_future,
				ocp,
				chan_open_timeout,
				&ocp->ocp_timer_handle);
		if (ret != 0) {
			log_printf(LOG_LEVEL_WARNING, 
					"Error setting timeout for open channel %s\n",
					req->ico_channel_name.value);
		}
	}

open_return:
	res.ico_head.size = sizeof(res);
	res.ico_head.id = MESSAGE_RES_EVT_OPEN_CHANNEL;
	res.ico_head.error = error;
	libais_send_response (conn_info, &res, sizeof(res));

	return 0;
}



/*
 * Used by the channel close code and by the implicit close
 * when saEvtFinalize is called with channels open.
 */
static SaErrorT
common_chan_close(struct event_svr_channel_open	*eco, struct libevt_ci *esip)
{
	struct event_svr_channel_subscr *ecs;
	struct list_head *l, *nxt;

	log_printf(LOG_LEVEL_DEBUG, "Close channel %s flags 0x%02x\n", 
			eco->eco_channel->esc_channel_name.value,
			eco->eco_flags);

	/*
	 * Disconnect the channel open structure.
	 *
	 * Check for subscriptions and deal with them.  In this case
	 * if there are any, we just implicitly unsubscribe.
	 *
	 * When We're done with the channel open data then we can 
	 * remove it's handle (this frees the memory too).
	 *
	 */
	list_del(&eco->eco_entry);
	list_del(&eco->eco_instance_entry);

	for (l = eco->eco_subscr.next; l != &eco->eco_subscr; l = nxt) {
		nxt = l->next;
		ecs = list_entry(l, struct event_svr_channel_subscr, ecs_entry);
		log_printf(LOG_LEVEL_DEBUG, "Unsubscribe ID: %x\n", 
				ecs->ecs_sub_id);
		list_del(&ecs->ecs_entry);
		free(ecs);
		/*
		 * Purge any pending events associated with this subscription
		 * that don't match another subscription.
		 */
		filter_undelivered_events(eco);
	}

	/*
	 * Remove this channel from the retained event's notion 
	 * of who they have been delivered to.
	 */
	remove_delivered_channel(eco);
	return evt_close_channel(&eco->eco_channel->esc_channel_name,
			eco->eco_channel->esc_unlink_id);
}

/*
 * Handler for saEvtChannelClose
 */
static int lib_evt_close_channel(struct conn_info *conn_info, void *message)
{
	struct req_evt_channel_close *req;
	struct res_evt_channel_close res;
	struct event_svr_channel_open	*eco;
	struct libevt_ci *esip = &conn_info->ais_ci.u.libevt_ci;
	SaErrorT error;
	void *ptr;

	req = message;

	log_printf(LOG_LEVEL_DEBUG, 
			"saEvtChannelClose (Close channel request)\n");
	log_printf(LOG_LEVEL_DEBUG, "handle 0x%x\n", req->icc_channel_handle);

	/*
	 * look up the channel handle
	 */
	error = saHandleInstanceGet(&esip->esi_hdb, 
					req->icc_channel_handle, &ptr);
	if (error != SA_AIS_OK) {
		goto chan_close_done;
	}
	eco = ptr;

	common_chan_close(eco, esip);
	saHandleDestroy(&esip->esi_hdb, req->icc_channel_handle);
	saHandleInstancePut(&esip->esi_hdb, req->icc_channel_handle);

chan_close_done:
	res.icc_head.size = sizeof(res);
	res.icc_head.id = MESSAGE_RES_EVT_CLOSE_CHANNEL;
	res.icc_head.error = error;
	libais_send_response (conn_info, &res, sizeof(res));

	return 0;
}

/*
 * Handler for saEvtChannelUnlink
 */
static int lib_evt_unlink_channel(struct conn_info *conn_info, void *message)
{
	struct req_evt_channel_unlink *req;
	struct res_evt_channel_unlink res;
	struct iovec chn_iovec;
	struct unlink_chan_pending *ucp;
	struct req_evt_chan_command cpkt;
	SaAisErrorT error = SA_AIS_ERR_LIBRARY;

	req = message;

	log_printf(CHAN_UNLINK_DEBUG, 
			"saEvtChannelUnlink (Unlink channel request)\n");
	log_printf(CHAN_UNLINK_DEBUG, "Channel Name %s\n", 
			req->iuc_channel_name.value);

	if (!find_channel(&req->iuc_channel_name, EVT_CHAN_ACTIVE)) {
		log_printf(CHAN_UNLINK_DEBUG, "Channel Name doesn't exist\n");
		error = SA_AIS_ERR_NOT_EXIST;
		goto evt_unlink_err;
	}

	/*
	 * Set up the data structure so that the channel op
	 * mcast handler can complete the unlink comamnd back to the 
	 * requestor.
	 */
	ucp = malloc(sizeof(*ucp));
	if (!ucp) {
		log_printf(LOG_LEVEL_ERROR,
				"saEvtChannelUnlink: Memory allocation failure\n");
		error = SA_AIS_ERR_NO_MEMORY;
		goto evt_unlink_err;
	}

	ucp->ucp_unlink_id = next_chan_unlink_id();
	ucp->ucp_conn_info = conn_info;
	list_add_tail(&ucp->ucp_entry, &unlink_pending);

	/*
	 * Put together a mcast packet to notify everyone
	 * of the channel unlink.
	 */
	memset(&cpkt, 0, sizeof(cpkt));
	cpkt.chc_head.id = MESSAGE_REQ_EXEC_EVT_CHANCMD;
	cpkt.chc_head.size = sizeof(cpkt);
	cpkt.chc_op = EVT_UNLINK_CHAN_OP;
	cpkt.u.chcu.chcu_name = req->iuc_channel_name;
	cpkt.u.chcu.chcu_unlink_id = ucp->ucp_unlink_id;
	chn_iovec.iov_base = &cpkt;
	chn_iovec.iov_len = cpkt.chc_head.size;
	if (totempg_mcast (&chn_iovec, 1, TOTEMPG_AGREED) == 0) {
		return 0;
	}

evt_unlink_err:
	res.iuc_head.size = sizeof(res);
	res.iuc_head.id = MESSAGE_RES_EVT_UNLINK_CHANNEL;
	res.iuc_head.error = error;
	libais_send_response (conn_info, &res, sizeof(res));
	return 0;
}

/*
 * Subscribe to an event channel.
 *
 * - First look up the channel to subscribe.
 * - Make sure that the subscription ID is not already in use.
 * - Fill in the subscription data structures and add them to the channels
 *      subscription list.
 * - See if there are any events with retetion times that need to be delivered
 *      because of the new subscription.
 */
static char *filter_types[] = {
	"INVALID FILTER TYPE",
	"SA_EVT_PREFIX_FILTER",
	"SA_EVT_SUFFIX_FILTER",
	"SA_EVT_EXACT_FILTER",
	"SA_EVT_PASS_ALL_FILTER",
};

/*
 * saEvtEventSubscribe Handler
 */
static int lib_evt_event_subscribe(struct conn_info *conn_info, void *message)
{
	struct req_evt_event_subscribe *req;
	struct res_evt_event_subscribe res;
	SaEvtEventFilterArrayT *filters;
	SaErrorT error = SA_AIS_OK;
	struct event_svr_channel_open	*eco;
	struct event_svr_channel_instance *eci;
	struct event_svr_channel_subscr *ecs;
	struct event_data *evt;
	struct libevt_ci *esip = &conn_info->ais_ci.u.libevt_ci;
	struct list_head *l;
	void *ptr;
	int i;

	req = message;

	log_printf(LOG_LEVEL_DEBUG, 
			"saEvtEventSubscribe (Subscribe request)\n");
	log_printf(LOG_LEVEL_DEBUG, "subscription Id: 0x%x\n", 
			req->ics_sub_id);

	error = evtfilt_to_aisfilt(req, &filters);

	if (error == SA_AIS_OK) {
		log_printf(LOG_LEVEL_DEBUG, "Subscribe filters count %d\n", 
				filters->filtersNumber);
		for (i = 0; i < filters->filtersNumber; i++) {
			log_printf(LOG_LEVEL_DEBUG, "type %s(%d) sz %d, <%s>\n", 
					filter_types[filters->filters[i].filterType],
					filters->filters[i].filterType,
					filters->filters[i].filter.patternSize,
					(filters->filters[i].filter.patternSize) 
						? (char *)filters->filters[i].filter.pattern
						: "");
		}
	}

	if (error != SA_AIS_OK) {
		goto subr_done;
	}

	/*
	 * look up the channel handle
	 */
	error = saHandleInstanceGet(&esip->esi_hdb, 
						req->ics_channel_handle, &ptr);
	if (error != SA_AIS_OK) {
		goto subr_done;
	}
	eco = ptr;

	eci = eco->eco_channel;

	/*
	 * See if the id is already being used
	 */
	ecs = find_subscr(eco, req->ics_sub_id); 
	if (ecs) {
		error = SA_AIS_ERR_EXIST;
		goto subr_put;
	}

	ecs = (struct event_svr_channel_subscr *)malloc(sizeof(*ecs));
	if (!ecs) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto subr_put;
	}
	ecs->ecs_filters = filters;
	ecs->ecs_sub_id = req->ics_sub_id;
	list_init(&ecs->ecs_entry);
	list_add(&ecs->ecs_entry, &eco->eco_subscr);


	res.ics_head.size = sizeof(res);
	res.ics_head.id = MESSAGE_RES_EVT_SUBSCRIBE;
	res.ics_head.error = error;
	libais_send_response (conn_info, &res, sizeof(res));

	/*
	 * See if an existing event with a retention time
	 * needs to be delivered based on this subscription
	 */
	for (l = retained_list.next; l != &retained_list; l = l->next) {
		evt = list_entry(l, struct event_data, ed_retained);
		log_printf(LOG_LEVEL_DEBUG,
			"Checking event ID %llx chanp %p -- sub chanp %p\n",
			evt->ed_event.led_event_id, evt->ed_my_chan, eci);
		if (evt->ed_my_chan == eci) {
			if (evt_already_delivered(evt, eco)) {
				continue;
			}
			if (event_match(evt, ecs) == SA_AIS_OK) {
				log_printf(LOG_LEVEL_DEBUG,
					"deliver event ID: 0x%llx\n", 
						evt->ed_event.led_event_id);
				deliver_event(evt, eco, ecs);
			}
		}
	}
	saHandleInstancePut(&esip->esi_hdb, req->ics_channel_handle);
	return 0;

subr_put:
	saHandleInstancePut(&esip->esi_hdb, req->ics_channel_handle);
subr_done:
	res.ics_head.size = sizeof(res);
	res.ics_head.id = MESSAGE_RES_EVT_SUBSCRIBE;
	res.ics_head.error = error;
	libais_send_response (conn_info, &res, sizeof(res));
	
	return 0;
}

/*
 * saEvtEventUnsubscribe Handler
 */
static int lib_evt_event_unsubscribe(struct conn_info *conn_info, 
		void *message)
{
	struct req_evt_event_unsubscribe *req;
	struct res_evt_event_unsubscribe res;
	struct event_svr_channel_open	*eco;
	struct event_svr_channel_instance *eci;
	struct event_svr_channel_subscr *ecs;
	struct libevt_ci *esip = &conn_info->ais_ci.u.libevt_ci;
	SaErrorT error = SA_AIS_OK;
	void *ptr;

	req = message;

	log_printf(LOG_LEVEL_DEBUG, 
					"saEvtEventUnsubscribe (Unsubscribe request)\n");
	log_printf(LOG_LEVEL_DEBUG, "subscription Id: 0x%x\n", 
			req->icu_sub_id);

	/*
	 * look up the channel handle, get the open channel
	 * data.
	 */
	error = saHandleInstanceGet(&esip->esi_hdb, 
						req->icu_channel_handle, &ptr);
	if (error != SA_AIS_OK) {
		goto unsubr_done;
	}
	eco = ptr;

	eci = eco->eco_channel;

	/*
	 * Make sure that the id exists.
	 */
	ecs = find_subscr(eco, req->icu_sub_id); 
	if (!ecs) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto unsubr_put;
	}

	list_del(&ecs->ecs_entry);

	log_printf(LOG_LEVEL_DEBUG, 
			"unsubscribe from channel %s subscription ID 0x%x "
			"with %d filters\n", 
			eci->esc_channel_name.value,
			ecs->ecs_sub_id, ecs->ecs_filters->filtersNumber);

	free_filters(ecs->ecs_filters);
	free(ecs);

unsubr_put:
	saHandleInstancePut(&esip->esi_hdb, req->icu_channel_handle);
unsubr_done:
	res.icu_head.size = sizeof(res);
	res.icu_head.id = MESSAGE_RES_EVT_UNSUBSCRIBE;
	res.icu_head.error = error;
	libais_send_response (conn_info, &res, sizeof(res));
	
	return 0;
}

/*
 * saEvtEventPublish Handler
 */
static int lib_evt_event_publish(struct conn_info *conn_info, void *message)
{
	struct lib_event_data *req;
	struct res_evt_event_publish res;
	struct libevt_ci *esip = &conn_info->ais_ci.u.libevt_ci;
	struct event_svr_channel_open	*eco;
	struct event_svr_channel_instance *eci;
	SaEvtEventIdT event_id = 0;
	SaErrorT error = SA_AIS_OK;
	struct iovec pub_iovec;
	void *ptr;
	int result;


	req = message;

	log_printf(LOG_LEVEL_DEBUG, 
			"saEvtEventPublish (Publish event request)\n");


	/*
	 * look up and validate open channel info
	 */
	error = saHandleInstanceGet(&esip->esi_hdb, 
				    req->led_svr_channel_handle, &ptr);
	if (error != SA_AIS_OK) {
		goto pub_done;
	}
	eco = ptr;

	eci = eco->eco_channel;

	/*
	 * modify the request structure for sending event data to subscribed
	 * processes.
	 */
	get_event_id(&event_id);
	req->led_head.id = MESSAGE_REQ_EXEC_EVT_EVENTDATA;
	req->led_chan_name = eci->esc_channel_name;
	req->led_event_id = event_id;
	req->led_chan_unlink_id = eci->esc_unlink_id;

	/*
	 * Distribute the event.
	 * The multicasted event will be picked up and delivered
	 * locally by the local network event receiver.
	 */
	pub_iovec.iov_base = req;
	pub_iovec.iov_len = req->led_head.size;
	result = totempg_mcast (&pub_iovec, 1, TOTEMPG_AGREED);
	if (result != 0) {
			error = SA_AIS_ERR_LIBRARY;
	}

	saHandleInstancePut(&esip->esi_hdb, req->led_svr_channel_handle);
pub_done:
	res.iep_head.size = sizeof(res);
	res.iep_head.id = MESSAGE_RES_EVT_PUBLISH;
	res.iep_head.error = error;
	res.iep_event_id = event_id;
	libais_send_response (conn_info, &res, sizeof(res));

	return 0;
}

/*
 * saEvtEventRetentionTimeClear handler
 */
static int lib_evt_event_clear_retentiontime(struct conn_info *conn_info, 
				void *message)
{
	struct req_evt_event_clear_retentiontime *req;
	struct res_evt_event_clear_retentiontime res;
	struct req_evt_chan_command cpkt;
	struct iovec rtn_iovec;
	SaErrorT error = SA_AIS_OK;
	int ret;

	req = message;

	log_printf(LOG_LEVEL_DEBUG, 
		"saEvtEventRetentionTimeClear (Clear event retentiontime request)\n");
	log_printf(LOG_LEVEL_DEBUG, 
		"event ID 0x%llx, chan handle 0x%x\n",
			req->iec_event_id,
			req->iec_channel_handle);

	memset(&cpkt, 0, sizeof(cpkt));
	cpkt.chc_head.id = MESSAGE_REQ_EXEC_EVT_CHANCMD;
	cpkt.chc_head.size = sizeof(cpkt);
	cpkt.chc_op = EVT_CLEAR_RET_OP;
	cpkt.u.chc_event_id = req->iec_event_id;
	rtn_iovec.iov_base = &cpkt;
	rtn_iovec.iov_len = cpkt.chc_head.size;
	ret = totempg_mcast (&rtn_iovec, 1, TOTEMPG_AGREED);
	if (ret != 0) {
			error = SA_AIS_ERR_LIBRARY;
	}

	res.iec_head.size = sizeof(res);
	res.iec_head.id = MESSAGE_RES_EVT_CLEAR_RETENTIONTIME;
	res.iec_head.error = error;
	libais_send_response (conn_info, &res, sizeof(res));

	return 0;
}

/*
 * Send requested event data to the application
 */
static int lib_evt_event_data_get(struct conn_info *conn_info, void *message)
{
	struct lib_event_data res;
	struct libevt_ci *esip = &conn_info->ais_ci.u.libevt_ci;
	struct chan_event_list *cel;
	struct event_data *edp;
	int i;


	/*
	 * Deliver events in publish order within priority
	 */
	for (i = SA_EVT_HIGHEST_PRIORITY; i <= SA_EVT_LOWEST_PRIORITY; i++) {
		if (!list_empty(&esip->esi_events[i])) {
			cel = list_entry(esip->esi_events[i].next, struct chan_event_list, 
						cel_entry);
			list_del(&cel->cel_entry);
			list_init(&cel->cel_entry);
			esip->esi_nevents--;
			if (esip->esi_queue_blocked && 
					(esip->esi_nevents < MIN_EVT_QUEUE_RESUME)) {
				esip->esi_queue_blocked = 0;
				log_printf(LOG_LEVEL_DEBUG, "unblock\n");
			}
			edp = cel->cel_event;
			edp->ed_event.led_lib_channel_handle = cel->cel_chan_handle;
			edp->ed_event.led_sub_id = cel->cel_sub_id;
			edp->ed_event.led_head.id = MESSAGE_RES_EVT_EVENT_DATA;
			edp->ed_event.led_head.error = SA_AIS_OK;
			free(cel);
			libais_send_response(conn_info, &edp->ed_event, 
											edp->ed_event.led_head.size);
			free_event_data(edp);
			goto data_get_done;
		} 
	}

	res.led_head.size = sizeof(res.led_head);
	res.led_head.id = MESSAGE_RES_EVT_EVENT_DATA;
	res.led_head.error = SA_AIS_ERR_NOT_EXIST;
	libais_send_response(conn_info, &res, res.led_head.size);

	/*
	 * See if there are any events that the app doesn't know about
	 * because the notify pipe was full.
	 */
data_get_done:
	if (esip->esi_nevents) {
		__notify_event(conn_info);
	}
	return 0;
}

/*
 * Scan the list of channels and remove the specified node.
 */
static void remove_chan_open_info(SaClmNodeIdT node_id)
{
	struct list_head *l, *nxt;
	struct event_svr_channel_instance *eci;

	for (l = esc_head.next; l != &esc_head; l = nxt) {
		nxt = l->next;
		eci = list_entry(l, struct event_svr_channel_instance, esc_entry);
		remove_open_count(eci, node_id);

	}
}


/*
 * Called when there is a configuration change in the cluster.
 * This function looks at any joiners and leavers and updates the evt
 * node list.  The node list is used to keep track of event IDs
 * received for each node for the detection of duplicate events.
 */
static int evt_conf_change(
		enum totempg_configuration_type configuration_type,
		struct in_addr *member_list, void *member_list_private,
			int member_list_entries,
		struct in_addr *left_list, void *left_list_private,
			int left_list_entries,
		struct in_addr *joined_list, void *joined_list_private,
			int joined_list_entries)
{
	struct in_addr my_node = {SA_CLM_LOCAL_NODE_ID};
	SaClmClusterNodeT *cn;
	static int first = 1;
	struct sockaddr_in *add_list;
	struct member_node_data *md;
	int add_count;
	struct req_evt_chan_command cpkt;
	struct iovec chn_iovec;
	int res;


	/*  
	 *  TODO required for open count accounting 
	 *  until the recovery code is re-enabled.
	 */
	total_members = member_list_entries;

	/*
	 * Set the base event id
	 */
	cn = clm_get_by_nodeid(my_node);
	if (!base_id_top) {
		log_printf(RECOVERY_DEBUG, "My node ID 0x%x\n", cn->nodeId);
		my_node_id = cn->nodeId;
		set_event_id(my_node_id);
	}

	return (0); // TODO 
	log_printf(LOG_LEVEL_DEBUG, "Evt conf change %d\n", 
			configuration_type);
	log_printf(LOG_LEVEL_DEBUG, "m %d, j %d, l %d\n", 
					member_list_entries,
					joined_list_entries,
					left_list_entries);
	/*
	 * Stop any recovery callbacks in progress.
	 */
	if (tok_call_handle) {
// TODO		totempg_token_callback_destroy(tok_call_handle);
		tok_call_handle = 0;
	}

	/*
	 * Don't seem to be able to tell who joined if we're just coming up. Not all
	 * nodes show up in the join list.  If this is the first time through,
	 * choose the members list to use to add nodes, after that use the join
	 * list.  Always use the left list for removing nodes.
	 */
	if (first) {
//j			add_list = member_list;
//			add_count = member_list_entries;
			first = 0;
	} else {
//			add_list = joined_list;
//			add_count = joined_list_entries;
	}

	while (add_count--) {
		/*
		 * If we've seen this node before, send out the last event ID 
		 * that we've seen from him.  He will set his base event ID to
		 * the highest one seen.
		 */
		md = evt_find_node(add_list->sin_addr);
		if (md != NULL) {
			if (!md->mn_started) {
				log_printf(RECOVERY_DEBUG, 
					"end set evt ID %llx to %s\n",
					md->mn_last_evt_id, inet_ntoa(add_list->sin_addr));
				md->mn_started = 1;
				memset(&cpkt, 0, sizeof(cpkt));
				cpkt.chc_head.id = MESSAGE_REQ_EXEC_EVT_CHANCMD;
				cpkt.chc_head.size = sizeof(cpkt);
				cpkt.chc_op = EVT_SET_ID_OP;
				cpkt.u.chc_set_id.chc_addr = add_list->sin_addr;
				cpkt.u.chc_set_id.chc_last_id = 
										md->mn_last_evt_id & BASE_ID_MASK;
				chn_iovec.iov_base = &cpkt;
				chn_iovec.iov_len = cpkt.chc_head.size;
				res = totempg_mcast (&chn_iovec, 1,TOTEMPG_AGREED);
				if (res != 0) {
					log_printf(LOG_LEVEL_WARNING, 
						"Unable to send event id to %s\n", 
						inet_ntoa(add_list->sin_addr));
				}
			}
		}
		add_list++;
	}

	while (left_list_entries--) {
// TODO		md = evt_find_node(left_list);
		if (md == 0) {
			log_printf(LOG_LEVEL_WARNING, 
					"Can't find cluster node at %s\n",
							inet_ntoa(left_list[0]));
		/*
		 * Mark this one as down.
		 */
		} else {
			log_printf(RECOVERY_DEBUG, "cluster node at %s down\n",
							inet_ntoa(left_list[0]));
			md->mn_started = 0;
			remove_chan_open_info(md->mn_node_info.nodeId);
		}
		left_list++;
	}


	/*
	 * Notify that a config change happened.  The exec handler will
	 * then determine what to do.
	 */
	if (configuration_type == TOTEMPG_CONFIGURATION_REGULAR) {
		if (in_cfg_change) {
			log_printf(LOG_LEVEL_NOTICE, 
				"Already in config change, Starting over, m %d, c %d\n",
					total_members, checked_in);
		}

		in_cfg_change = 1;
		total_members = member_list_entries;
		checked_in = 0;
		any_joined = joined_list_entries;

		/*
	   	 * Start by updating all the nodes on our
	 	 * open channel count. Once that is done, proceed to determining who
	 	 * sends ratained events.  Then we can start normal operation again.
	 	 */
		send_open_count();
	}

	return 0;
}

/*
 * saEvtFinalize Handler
 */
static int evt_finalize(struct conn_info *conn_info)
{

	struct libevt_ci *esip = &conn_info->ais_ci.u.libevt_ci;
	struct event_svr_channel_open	*eco;
	struct list_head *l, *nxt;

	log_printf(LOG_LEVEL_DEBUG, "saEvtFinalize (Event exit request)\n");
	log_printf(LOG_LEVEL_DEBUG, "saEvtFinalize %d evts on list\n",
			esip->esi_nevents);
	
	/*
	 * Clean up any open channels and associated subscriptions.
	 */
	for (l = esip->esi_open_chans.next; l != &esip->esi_open_chans; l = nxt) {
		nxt = l->next;
		eco = list_entry(l, struct event_svr_channel_open, eco_instance_entry);
		common_chan_close(eco, esip);
		saHandleDestroy(&esip->esi_hdb, eco->eco_my_handle);
	}


	/*
	 * Delete track entry if there is one
	 */
	list_del (&conn_info->conn_list);

	return 0;
}

/*
 * Called at service start time.
 */
static int evt_exec_init(void)
{
	log_printf(LOG_LEVEL_DEBUG, "Evt exec init request\n");

#ifdef TODO
	int res;
	res = totempg_recovery_plug_create (&evt_recovery_plug_handle);
	if (res != 0) {
		log_printf(LOG_LEVEL_ERROR,
			"Could not create recovery plug for event service.\n");
		return (-1);
	}
#endif

	/*
	 * Create an event to be sent when we have to drop messages
	 * for an application.
	 */
	dropped_event_size = sizeof(*dropped_event) + sizeof(dropped_pattern);
	dropped_event = malloc(dropped_event_size);
	if (dropped_event == 0) {
		log_printf(LOG_LEVEL_ERROR, 
				"Memory Allocation Failure, event service not started\n");
// TODO		res = totempg_recovery_plug_destroy (evt_recovery_plug_handle);
		errno = ENOMEM;
		return -1;
	}
	memset(dropped_event, 0, sizeof(*dropped_event) + sizeof(dropped_pattern));
	dropped_event->ed_ref_count = 1;
	list_init(&dropped_event->ed_retained);
	dropped_event->ed_event.led_head.size = 
			sizeof(*dropped_event) + sizeof(dropped_pattern);
	dropped_event->ed_event.led_head.error = SA_AIS_OK;
	dropped_event->ed_event.led_priority = SA_EVT_HIGHEST_PRIORITY;
	dropped_event->ed_event.led_chan_name = lost_chan;
	dropped_event->ed_event.led_publisher_name = dropped_publisher;
	dropped_event->ed_event.led_patterns_number = 1;
	memcpy(&dropped_event->ed_event.led_body[0], 
					&dropped_pattern, sizeof(dropped_pattern));
	return 0;
}


/*
 * Receive the network event message and distribute it to local subscribers
 */
static int evt_remote_evt(void *msg, struct in_addr source_addr, 
		int endian_conversion_required)
{
	/*
	 * - retain events that have a retention time
	 * - Find assocated channel
	 * - Scan list of subscribers
	 * - Apply filters
	 * - Deliver events that pass the filter test
	 */
	struct lib_event_data *evtpkt = msg;
	struct event_svr_channel_instance *eci;
	struct event_svr_channel_open *eco;
	struct event_svr_channel_subscr *ecs;
	struct event_data *evt;
	struct list_head *l, *l1;
	SaClmClusterNodeT *cn;

	log_printf(LOG_LEVEL_DEBUG, "Remote event data received from %s\n",
					inet_ntoa(source_addr));

	/*
	 * See where the message came from so that we can set the 
	 * publishing node id in the message before delivery.
	 */
	cn = clm_get_by_nodeid (source_addr);
	if (!cn) {
			/*
			 * Not sure how this can happen...
			 */
			log_printf(LOG_LEVEL_NOTICE, "No cluster node data for %s\n",
							inet_ntoa(source_addr));
			errno = ENXIO;
			return -1;
	}
	log_printf(LOG_LEVEL_DEBUG, "Cluster node ID 0x%x name %s\n",
					cn->nodeId, cn->nodeName.value);

	if (endian_conversion_required) {
		convert_event(evtpkt);
	}

	evtpkt->led_publisher_node_id = cn->nodeId;
	evtpkt->led_in_addr = source_addr;
	evtpkt->led_receive_time = clust_time_now();

	log_printf(CHAN_UNLINK_DEBUG, 
				"evt_remote_evt(0): chan %s, id 0x%llx\n",
					evtpkt->led_chan_name.value, evtpkt->led_chan_unlink_id);
	eci = find_channel(&evtpkt->led_chan_name, evtpkt->led_chan_unlink_id);
	/*
	 * We may have had some events that were already queued when an
	 * unlink happened, if we don't find the channel in the active list
	 * look for the last unlinked channel of the same name.  If this channel
	 * is re-opened the messages will still be routed correctly because new
	 * active channel messages will be ordered behind the open.
	 */
	if (!eci && (evtpkt->led_chan_unlink_id == EVT_CHAN_ACTIVE)) {
		log_printf(CHAN_UNLINK_DEBUG, 
				"evt_remote_evt(1): chan %s, id 0x%llx\n",
					evtpkt->led_chan_name.value, evtpkt->led_chan_unlink_id);
		eci = find_last_unlinked_channel(&evtpkt->led_chan_name);
	}

	/*
	 * We shouldn't normally see an event for a channel that we 
	 * don't know about.
	 */
	if (!eci) {
		log_printf(LOG_LEVEL_DEBUG, "Channel %s doesn't exist\n",
				evtpkt->led_chan_name.value);
		return 0;
	}

	if (check_last_event(evtpkt, source_addr)) {
		return 0;
	}

	evt = make_local_event(evtpkt, eci);
	if (!evt) {
		log_printf(LOG_LEVEL_WARNING, 
						"1Memory allocation error, can't deliver event\n");
		errno = ENOMEM;
		return -1;
	}
		
	if (evt->ed_event.led_retention_time) {
		retain_event(evt);
	}

	/*
	 * Check open channels
	 */
	for (l = eci->esc_open_chans.next; l != &eci->esc_open_chans; l = l->next) {
		eco = list_entry(l, struct event_svr_channel_open, eco_entry);
		/*
		 * See if enabled to receive
		 */
		if (!(eco->eco_flags & SA_EVT_CHANNEL_SUBSCRIBER)) {
				continue;
		}

		/*
		 * Check subscriptions
		 */
		for (l1 = eco->eco_subscr.next; l1 != &eco->eco_subscr; l1 = l1->next) {
			ecs = list_entry(l1, struct event_svr_channel_subscr, ecs_entry);
			/*
			 * Apply filter rules and deliver if patterns
			 * match filters.
			 * Only deliver one event per open channel
			 */
			if (event_match(evt, ecs) == SA_AIS_OK) {
				deliver_event(evt, eco, ecs);
				break;
			}
		}
	}
	free_event_data(evt);


	return 0;
}

/*
 * Calculate the remaining retention time of a received event during recovery
 */
inline SaTimeT calc_retention_time(SaTimeT retention, 
								SaTimeT received, SaTimeT now)
{
	if ((received < now) && ((now - received) < retention)) {
		return retention - (now - received);
	} else {
		return 0;
	}
}

/*
 * Receive a recovery network event message and save it in the retained list
 */
static int evt_remote_recovery_evt(void *msg, struct in_addr source_addr, 
		int endian_conversion_required)
{
	/*
	 * - retain events that have a retention time
	 * - Find assocated channel
	 */
	struct lib_event_data *evtpkt = msg;
	struct event_svr_channel_instance *eci;
	struct event_data *evt;
	struct member_node_data *md;
	SaTimeT now;

	now = clust_time_now();

	log_printf(LOG_LEVEL_DEBUG, 
			"Remote recovery event data received from %s\n",
					inet_ntoa(source_addr));

	if (!in_cfg_change) {
		log_printf(LOG_LEVEL_NOTICE, 
				"Received recovery data, not in recovery mode\n");
		return 0;
	}

	if (endian_conversion_required) {
		convert_event(evtpkt);
	}

	log_printf(LOG_LEVEL_DEBUG, 
			"Processing recovery of retained events\n");
	if (recovery_node) {
		log_printf(LOG_LEVEL_DEBUG, "This node is the recovery node\n");
	}

	log_printf(LOG_LEVEL_DEBUG, "(1)EVT ID: %llx, Time: %llx\n",
			evtpkt->led_event_id, evtpkt->led_retention_time);
	/*
	 * Calculate remaining retention time
	 */
	evtpkt->led_retention_time = calc_retention_time(
				evtpkt->led_retention_time, 
				evtpkt->led_receive_time, 
				now);

	log_printf(LOG_LEVEL_DEBUG, 
			"(2)EVT ID: %llx, ret: %llx, rec: %llx, now: %llx\n",
			evtpkt->led_event_id, 
			evtpkt->led_retention_time, evtpkt->led_receive_time, now);

	/*
	 * If we haven't seen this event yet and it has remaining time, process
	 * the event.
	 */
	if (!check_last_event(evtpkt, evtpkt->led_in_addr) && 
												evtpkt->led_retention_time) {
		/*
		 * See where the message came from so that we can set the 
		 * publishing node id in the message before delivery.
		 */
		md = evt_find_node(evtpkt->led_in_addr);
		if (!md) {
				/*
				 * Not sure how this can happen
				 */
				log_printf(LOG_LEVEL_NOTICE, "No node for %s\n",
								inet_ntoa(evtpkt->led_in_addr));
				errno = ENXIO;
				return -1;
		}
		log_printf(LOG_LEVEL_DEBUG, "Cluster node ID 0x%x name %s\n",
						md->mn_node_info.nodeId, 
						md->mn_node_info.nodeName.value);

		log_printf(CHAN_UNLINK_DEBUG, 
				"evt_recovery_event: chan %s, id 0x%llx\n",
					evtpkt->led_chan_name.value, evtpkt->led_chan_unlink_id);
		eci = find_channel(&evtpkt->led_chan_name, evtpkt->led_chan_unlink_id);

		/*
		 * We shouldn't normally see an event for a channel that we don't 
		 * know about.
		 */
		if (!eci) {
			log_printf(LOG_LEVEL_DEBUG, "Channel %s doesn't exist\n",
				evtpkt->led_chan_name.value);
			return 0;
		}

		evt = make_local_event(evtpkt, eci);
		if (!evt) {
			log_printf(LOG_LEVEL_WARNING, 
				"2Memory allocation error, can't deliver event\n");
			errno = ENOMEM;
			return -1;
		}
			
		retain_event(evt);
		free_event_data(evt);
	}

	return 0;
}


/*
 * Timeout handler for event channel open.
 */
static void chan_open_timeout(void *data)
{
	struct open_chan_pending *ocp = (struct open_chan_pending *)data;
	struct res_evt_channel_open res;
	
	res.ico_head.size = sizeof(res);
	res.ico_head.id = MESSAGE_RES_EVT_OPEN_CHANNEL;
	res.ico_head.error = SA_AIS_ERR_TIMEOUT;
	libais_send_response (ocp->ocp_conn_info, &res, sizeof(res));
	list_del(&ocp->ocp_entry);
	free(ocp);
}

/*
 * Called by the channel open exec handler to finish the open and 
 * respond to the application
 */
static void evt_chan_open_finish(struct open_chan_pending *ocp, 
		struct event_svr_channel_instance *eci)
{
	uint32_t handle;
	struct event_svr_channel_open *eco;
	SaErrorT error;
	struct libevt_ci *esip = &ocp->ocp_conn_info->ais_ci.u.libevt_ci;
	int ret = 0;
	void *ptr;

	log_printf(CHAN_OPEN_DEBUG, "Open channel finish %s\n", 
											getSaNameT(&ocp->ocp_chan_name));
	if (ocp->ocp_timer_handle) {
		ret = poll_timer_delete(aisexec_poll_handle, ocp->ocp_timer_handle);
		if (ret != 0 ) {
			log_printf(LOG_LEVEL_WARNING, 
				"Error clearing timeout for open channel of %s\n",
				   getSaNameT(&ocp->ocp_chan_name));
		}
	}

	/*
	 * Create a handle to give back to the caller to associate
	 * with this channel open instance.
	 */
	error = saHandleCreate(&esip->esi_hdb, sizeof(*eco), &handle);
	if (error != SA_AIS_OK) {
		goto open_return;
	}
	error = saHandleInstanceGet(&esip->esi_hdb, handle, &ptr);
	if (error != SA_AIS_OK) {
		goto open_return;
	}
	eco = ptr;

	/*
	 * Initailize and link into the global channel structure.
	 */
	list_init(&eco->eco_subscr);
	list_init(&eco->eco_entry);
	list_init(&eco->eco_instance_entry);
	eco->eco_flags = ocp->ocp_open_flag;
	eco->eco_channel = eci;
	eco->eco_lib_handle = ocp->ocp_c_handle;
	eco->eco_my_handle = handle;
	eco->eco_conn_info = ocp->ocp_conn_info;
	list_add_tail(&eco->eco_entry, &eci->esc_open_chans);
	list_add_tail(&eco->eco_instance_entry, &esip->esi_open_chans);

	/*
	 * respond back with a handle to access this channel
	 * open instance for later subscriptions, etc.
	 */
	saHandleInstancePut(&esip->esi_hdb, handle);

open_return:
	log_printf(CHAN_OPEN_DEBUG, "Open channel finish %s send response %d\n", 
											getSaNameT(&ocp->ocp_chan_name),
											error);
	if (ocp->ocp_async) {
		struct res_evt_open_chan_async resa;
		resa.ica_head.size = sizeof(resa);
		resa.ica_head.id = MESSAGE_RES_EVT_CHAN_OPEN_CALLBACK;
		resa.ica_head.error = error;
		resa.ica_channel_handle = handle;
		resa.ica_c_handle = ocp->ocp_c_handle;
		resa.ica_invocation = ocp->ocp_invocation;
		libais_send_response (ocp->ocp_conn_info, &resa, sizeof(resa));
	} else {
		struct res_evt_channel_open res;
		res.ico_head.size = sizeof(res);
		res.ico_head.id = MESSAGE_RES_EVT_OPEN_CHANNEL;
		res.ico_head.error = error;
		res.ico_channel_handle = handle;
		libais_send_response (ocp->ocp_conn_info, &res, sizeof(res));
	}

	if (ret == 0) {
		list_del(&ocp->ocp_entry);
		free(ocp);
	}
}

/*
 * Called by the channel unlink exec handler to
 * respond to the application.
 */
static void evt_chan_unlink_finish(struct unlink_chan_pending *ucp)
{
	struct res_evt_channel_unlink res;

	log_printf(CHAN_UNLINK_DEBUG, "Unlink channel finish ID 0x%llx\n", 
											ucp->ucp_unlink_id);

	res.iuc_head.size = sizeof(res);
	res.iuc_head.id = MESSAGE_RES_EVT_UNLINK_CHANNEL;
	res.iuc_head.error = SA_AIS_OK;
	libais_send_response (ucp->ucp_conn_info, &res, sizeof(res));

	list_del(&ucp->ucp_entry);
	free(ucp);
}

/*
 * Take the channel command data and swap the elements so they match 
 * our architectures word order.
 */
static void
convert_chan_packet(struct req_evt_chan_command *cpkt)
{
	/*
	 * converted in the main deliver_fn:
	 * led_head.id, led_head.size.
	 *
	 */

	cpkt->chc_op = swab32(cpkt->chc_op);

	/*
	 * Which elements of the packet that are converted depend
	 * on the operation.
	 */
	switch (cpkt->chc_op) {
	
	case EVT_OPEN_CHAN_OP:
		cpkt->u.chc_chan.length = swab16(cpkt->u.chc_chan.length);
		break;

	case EVT_UNLINK_CHAN_OP:
	case EVT_CLOSE_CHAN_OP:
		cpkt->u.chcu.chcu_name.length = swab16(cpkt->u.chcu.chcu_name.length);
		cpkt->u.chcu.chcu_unlink_id = swab64(cpkt->u.chcu.chcu_unlink_id);
		break;

	case EVT_CLEAR_RET_OP:
		cpkt->u.chc_event_id = swab64(cpkt->u.chc_event_id);
		break;

	case EVT_SET_ID_OP:
		cpkt->u.chc_set_id.chc_addr.s_addr = 
			swab32(cpkt->u.chc_set_id.chc_addr.s_addr);
		cpkt->u.chc_set_id.chc_last_id = swab64(cpkt->u.chc_set_id.chc_last_id);
		break;

	case EVT_OPEN_COUNT:
		cpkt->u.chc_set_opens.chc_chan_name.length = 
			swab16(cpkt->u.chc_set_opens.chc_chan_name.length);
		cpkt->u.chc_set_opens.chc_open_count = 
			swab32(cpkt->u.chc_set_opens.chc_open_count);
		break;

	/* 
	 * No data assocaited with these ops.
	 */
	case EVT_CONF_DONE:
	case EVT_OPEN_COUNT_DONE:
		break;

	/*
	 * Make sure that this function is updated when new ops are added.
	 */
	default:
		assert(0);
	}
}


/*
 * Receive and process remote event operations.
 * Used to communicate channel opens/closes, clear retention time,
 * config change updates...
 */
static int evt_remote_chan_op(void *msg, struct in_addr source_addr, 
		int endian_conversion_required)
{
	struct req_evt_chan_command *cpkt = msg;
	struct in_addr local_node = {SA_CLM_LOCAL_NODE_ID};
	SaClmClusterNodeT *cn, *my_node;
	struct member_node_data *mn;
	struct event_svr_channel_instance *eci;

	if (endian_conversion_required) {
		convert_chan_packet(cpkt);
	}

	log_printf(REMOTE_OP_DEBUG, "Remote channel operation request\n");
	my_node = clm_get_by_nodeid(local_node);
	log_printf(REMOTE_OP_DEBUG, "my node ID: 0x%x\n", my_node->nodeId);

	mn = evt_find_node(source_addr);
	if (mn == NULL) {
		cn = clm_get_by_nodeid(source_addr);
		if (cn == NULL) {
			log_printf(LOG_LEVEL_WARNING, 
				"Evt remote channel op: Node data for addr %s is NULL\n",
					inet_ntoa(source_addr));
		} else {
			evt_add_node(source_addr, cn);
			mn = evt_find_node(source_addr);
		}
	}

	switch (cpkt->chc_op) {
		/*
		 * Open channel remote command.  The open channel request is done
		 * in two steps.  When an pplication tries to open, we send an open 
		 * channel message to the other nodes. When we receive an open channel 
		 * message, we may create the channel structure if it doesn't exist 
		 * and also complete applicaiton open requests for the specified 
		 * channel.  We keep a counter of total opens for the given channel and
		 * later when it has been completely closed (everywhere in the cluster)
		 * we will free up the assocated channel data.
		 */
	case EVT_OPEN_CHAN_OP: {
		struct open_chan_pending *ocp;
		struct list_head *l, *nxt;

		log_printf(CHAN_OPEN_DEBUG, "Opening channel %s for node 0x%x\n",
						cpkt->u.chc_chan.value, mn->mn_node_info.nodeId);
		eci = find_channel(&cpkt->u.chc_chan, EVT_CHAN_ACTIVE);

		if (!eci) {
			eci = create_channel(&cpkt->u.chc_chan);
		}
		if (!eci) {
			log_printf(LOG_LEVEL_WARNING, "Could not create channel %s\n",
				   getSaNameT(&cpkt->u.chc_chan));
			break;
		}

		inc_open_count(eci, mn->mn_node_info.nodeId);

		if (mn->mn_node_info.nodeId == my_node->nodeId) {
			/*
			 * Complete one of our pending open requests
			 */
			for (l = open_pending.next; l != &open_pending; l = nxt) {
				nxt = l->next;
				ocp = list_entry(l, struct open_chan_pending, ocp_entry);
				log_printf(CHAN_OPEN_DEBUG, 
				"Compare channel %s %s\n", ocp->ocp_chan_name.value,
						eci->esc_channel_name.value);
				if (name_match(&ocp->ocp_chan_name, &eci->esc_channel_name)) {
					evt_chan_open_finish(ocp, eci);
					break;
				}
			}
		}
		log_printf(CHAN_OPEN_DEBUG, 
				"Open channel %s t %d, l %d, r %d\n",
				getSaNameT(&eci->esc_channel_name),
				eci->esc_total_opens, eci->esc_local_opens,
				eci->esc_retained_count);
		break;
	}

	/*
	 * Handle a channel close. We'll decrement the global open counter and 
	 * free up channel associated data when all instances are closed.
	 */
	case EVT_CLOSE_CHAN_OP:
		log_printf(LOG_LEVEL_DEBUG, "Closing channel %s for node 0x%x\n",
						cpkt->u.chcu.chcu_name.value, mn->mn_node_info.nodeId);
		eci = find_channel(&cpkt->u.chcu.chcu_name, cpkt->u.chcu.chcu_unlink_id);
		if (!eci) {
			log_printf(LOG_LEVEL_NOTICE, 
					"Channel close request for %s not found\n",
				cpkt->u.chcu.chcu_name.value);	
			break;
		}

		/*
		 * if last instance, we can free up assocated data.
		 */
		dec_open_count(eci, mn->mn_node_info.nodeId);
		log_printf(LOG_LEVEL_DEBUG, 
				"Close channel %s t %d, l %d, r %d\n",
				eci->esc_channel_name.value,
				eci->esc_total_opens, eci->esc_local_opens,
				eci->esc_retained_count);
		delete_channel(eci);
		break;

	/*
	 * Handle a request for channel unlink saEvtChannelUnlink().  
	 * We'll look up the channel and mark it as unlinked.  Respond to 
	 * local library unlink commands.
	 */
	case EVT_UNLINK_CHAN_OP: {
		struct unlink_chan_pending *ucp;
		struct list_head *l, *nxt;

		log_printf(CHAN_UNLINK_DEBUG, 
				"Unlink request channel %s unlink ID 0x%llx from node 0x%x\n",
				cpkt->u.chcu.chcu_name.value,
				cpkt->u.chcu.chcu_unlink_id,
				mn->mn_node_info.nodeId);


		/*
		 * look up the channel name and get its assoicated data.
		 */
		eci = find_channel(&cpkt->u.chcu.chcu_name, 
				EVT_CHAN_ACTIVE);
		if (!eci) {
			log_printf(LOG_LEVEL_NOTICE, 
					"Channel unlink request for %s not found\n",
				cpkt->u.chcu.chcu_name);
			break;
		}

		/*
		 * Mark channel as unlinked.
		 */
		unlink_channel(eci, cpkt->u.chcu.chcu_unlink_id);

		/*
		 * respond only to local library requests.
		 */
		if (mn->mn_node_info.nodeId == my_node->nodeId) {
			/*
			 * Complete one of our pending unlink requests
			 */
			for (l = unlink_pending.next; l != &unlink_pending; l = nxt) {
				nxt = l->next;
				ucp = list_entry(l, struct unlink_chan_pending, ucp_entry);
				log_printf(CHAN_UNLINK_DEBUG, 
				"Compare channel id 0x%llx 0x%llx\n", 
					ucp->ucp_unlink_id, eci->esc_unlink_id);
				if (ucp->ucp_unlink_id == eci->esc_unlink_id) {
					evt_chan_unlink_finish(ucp);
					break;
				}
			}
		}
		break;
 	}


	/*
	 * saEvtClearRetentiotime handler.
	 */
	case EVT_CLEAR_RET_OP:
		log_printf(LOG_LEVEL_DEBUG, "Clear retention time request %llx\n",
				cpkt->u.chc_event_id);	
		clear_retention_time(cpkt->u.chc_event_id);
		break;
	
	/*
	 * Set our next event ID based on the largest event ID seen
	 * by others in the cluster.  This way, if we've left and re-joined, we'll
	 * start using an event ID that is unique.
	 */
	case EVT_SET_ID_OP: {
		struct in_addr my_addr;
		my_addr.s_addr = my_node->nodeId;
		log_printf(RECOVERY_DEBUG, 
			"Received Set event ID OP from %x to %llx for %x my addr %x base %llx\n",
					inet_ntoa(source_addr), 
					cpkt->u.chc_set_id.chc_last_id,
					cpkt->u.chc_set_id.chc_addr.s_addr,
					my_addr.s_addr,
					base_id);	
		if (cpkt->u.chc_set_id.chc_addr.s_addr == my_addr.s_addr) {
			if (cpkt->u.chc_set_id.chc_last_id > base_id) {
				log_printf(RECOVERY_DEBUG, 
					"Set event ID from %s to %llx\n",
					inet_ntoa(source_addr), cpkt->u.chc_set_id.chc_last_id);	
				base_id = cpkt->u.chc_set_id.chc_last_id + 1;
			}
		}
		break;
	}

	/*
	 * Receive the open count for a particular channel during recovery.
	 * This insures that everyone has the same notion of who has a channel
	 * open so that it can be removed when no one else has it open anymore.
	 */
	case EVT_OPEN_COUNT:
		if (!in_cfg_change) {
			log_printf(LOG_LEVEL_ERROR, 
				"Evt open count msg from %s, but not in membership change\n",
				inet_ntoa(source_addr));
		}
		log_printf(RECOVERY_DEBUG, 
				"Open channel count %s is %d for node 0x%x\n",
				cpkt->u.chc_set_opens.chc_chan_name.value, 
				cpkt->u.chc_set_opens.chc_open_count,
				mn->mn_node_info.nodeId);

		eci = find_channel(&cpkt->u.chc_set_opens.chc_chan_name, 
					EVT_CHAN_ACTIVE);
		if (!eci) {
			eci = create_channel(&cpkt->u.chc_set_opens.chc_chan_name);
		}
		if (!eci) {
			log_printf(LOG_LEVEL_WARNING, "Could not create channel %s\n",
				   getSaNameT(&cpkt->u.chc_set_opens.chc_chan_name));
			break;
		}
		if (set_open_count(eci, mn->mn_node_info.nodeId, 
									cpkt->u.chc_set_opens.chc_open_count)) {
			log_printf(LOG_LEVEL_ERROR, 
				"Error setting Open channel count %s for node 0x%x\n",
				cpkt->u.chc_set_opens.chc_chan_name.value, 
				mn->mn_node_info.nodeId);
		}
		break;

	/*
	 * Once we get all the messages from
	 * the current membership, determine who delivers any retained events.
	 */
	case EVT_OPEN_COUNT_DONE: {
		if (!in_cfg_change) {
			log_printf(LOG_LEVEL_ERROR, 
				"Evt config msg from %s, but not in membership change\n",
				inet_ntoa(source_addr));
		}
		log_printf(RECOVERY_DEBUG, 
			"Receive EVT_CONF_CHANGE_DONE from %s members %d checked in %d\n",
				inet_ntoa(source_addr), total_members, checked_in+1);
		if (!mn) {
			log_printf(RECOVERY_DEBUG, 
				"NO NODE DATA AVAILABLE FOR %s\n",
					inet_ntoa(source_addr));
		}

		if (++checked_in == total_members) {
			/*
			 * We're all here, now figure out who should send the
			 * retained events, if any.
			 */
			mn = oldest_node();
			if (mn->mn_node_info.nodeId == my_node_id) {
				log_printf(RECOVERY_DEBUG, "I am oldest\n");
				send_retained();
			}
			
		}
		break;
	}

	/*
	 * OK, We're done with recovery, continue operations.
	 */
	case EVT_CONF_DONE: {
		log_printf(RECOVERY_DEBUG, 
				"Receive EVT_CONF_DONE from %s\n", 
				inet_ntoa(source_addr));
		in_cfg_change = 0;
// TODO		totempg_recovery_plug_unplug (evt_recovery_plug_handle);
#ifdef DUMP_CHAN_INFO
		dump_all_chans();
#endif
		break;
	}

	default:
		log_printf(LOG_LEVEL_NOTICE, "Invalid channel operation %d\n",
						cpkt->chc_op);
		break;
	}

	return 0;
}
/*
 *	vi: set autoindent tabstop=4 shiftwidth=4 :
 */
