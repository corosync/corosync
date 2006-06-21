/*
 * Copyright (c) 2004-2005 Mark Haverkamp
 * Copyright (c) 2004-2005 Open Source Development Lab
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
 * - Neither the name of the Open Source Developement Lab nor the names of its
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

#ifndef IPC_EVT_H_DEFINED
#define IPC_EVT_H_DEFINED

#include <netinet/in.h>
#include "saAis.h"
#include "saEvt.h"
#include "saClm.h"
#include "ipc_gen.h"

enum req_evt_types {
	MESSAGE_REQ_EVT_OPEN_CHANNEL = 0,
	MESSAGE_REQ_EVT_OPEN_CHANNEL_ASYNC = 1,
	MESSAGE_REQ_EVT_CLOSE_CHANNEL = 2,
	MESSAGE_REQ_EVT_UNLINK_CHANNEL = 3,
	MESSAGE_REQ_EVT_SUBSCRIBE = 4,
	MESSAGE_REQ_EVT_UNSUBSCRIBE = 5,
	MESSAGE_REQ_EVT_PUBLISH = 6,
	MESSAGE_REQ_EVT_CLEAR_RETENTIONTIME = 7,
	MESSAGE_REQ_EVT_EVENT_DATA = 8
};

enum res_evt_types {
	MESSAGE_RES_EVT_OPEN_CHANNEL = 0,
	MESSAGE_RES_EVT_CLOSE_CHANNEL = 1,
	MESSAGE_RES_EVT_UNLINK_CHANNEL = 2,
	MESSAGE_RES_EVT_SUBSCRIBE = 3,
	MESSAGE_RES_EVT_UNSUBSCRIBE = 4,
	MESSAGE_RES_EVT_PUBLISH = 5,
	MESSAGE_RES_EVT_CLEAR_RETENTIONTIME = 6,
	MESSAGE_RES_EVT_CHAN_OPEN_CALLBACK = 7,
	MESSAGE_RES_EVT_EVENT_DATA = 8,
	MESSAGE_RES_EVT_AVAILABLE = 9
};

/* 
 * MESSAGE_REQ_EVT_OPEN_CHANNEL
 * MESSAGE_REQ_EVT_OPEN_CHANNEL_ASYNC
 *
 * ico_head				Request head
 * ico_open_flag:		Channel open flags
 * ico_channel_name:	Name of channel to open
 * ico_c_handle:		Local lib channel handle (used in returned event data)
 * ico_timeout:			Used only by open
 * ico_invocation:		Used only by async open
 *
 */
struct req_evt_channel_open {

	mar_req_header_t		ico_head;
	SaUint8T				ico_open_flag;
	SaNameT					ico_channel_name;
	SaEvtChannelHandleT		ico_c_handle;
	SaTimeT					ico_timeout;
	SaInvocationT			ico_invocation;
};

/*
 * MESSAGE_RES_EVT_OPEN_CHANNEL
 *
 * Used by both the blocing and non-blocking
 * versions.  Only the error code in the header is used by the async
 * open.  The channel handle will be returnd via the channel open
 * callback.
 * 
 *
 * ico_head:			Results head
 * ico_error:			Request results
 * ico_channel_handle:	Server side channel handle (used in channel ops)
 *
 */
struct res_evt_channel_open {

	mar_res_header_t	ico_head;
	uint32_t			ico_channel_handle;/* svr chan handle */

};

/*
 * MESSAGE_RES_EVT_CHAN_OPEN_CALLBACK
 *
 * ica_head:			Results head.
 * ica_c_handle:		Lib size channel handle.  So we can look
 * 						up the new open channel info from the callback.
 * ica_channel_handle:	Server side handle.
 * ica_invocation:		Supplied by the user in the open call.  Passed to
 * 						the callback so the user can assocate the callback
 * 						to the particular open.
 */
struct res_evt_open_chan_async {
	mar_res_header_t	ica_head;
	SaEvtChannelHandleT	ica_c_handle;
	uint32_t			ica_channel_handle;
	SaInvocationT		ica_invocation;

};


/*
 * MESSAGE_REQ_EVT_CLOSE_CHANNEL
 *
 * icc_head:			Request head
 * icc_channel_handle:	Server handle of channel to close
 *
 */
struct req_evt_channel_close {

	mar_req_header_t		icc_head;
	uint32_t				icc_channel_handle;
};

/*
 * MESSAGE_RES_EVT_CLOSE_CHANNEL
 *
 * icc_head:		Results head
 *
 */
struct res_evt_channel_close {
	mar_res_header_t	icc_head;
};

/*
 * MESSAGE_REQ_EVT_UNLINK_CHANNEL
 *
 * iuc_head:			Request head
 * iuc_channel_name:	Name of channel to unlink
 *
 */
struct req_evt_channel_unlink {

	mar_req_header_t		iuc_head;
	SaNameT					iuc_channel_name;
};

/*
 * MESSAGE_RES_EVT_UNLINK_CHANNEL
 *
 * iuc_head:		Results head
 *
 */
struct res_evt_channel_unlink {
	mar_res_header_t	iuc_head;
};

/* 
 * MESSAGE_REQ_EVT_SUBSCRIBE
 *
 * ics_head:			Request head
 * ics_channel_handle:	Server handle of channel
 * ics_sub_id:			Subscription ID
 * ics_filter_size:		Size of supplied filter data
 * ics_filter_count:	Number of filters supplied
 * ics_filter_data:		Filter data
 *
 */
struct req_evt_event_subscribe {

	mar_req_header_t		ics_head;
	uint32_t				ics_channel_handle;
	SaEvtSubscriptionIdT	ics_sub_id;
	uint32_t				ics_filter_size;
	uint32_t				ics_filter_count;
	uint8_t					ics_filter_data[0];

};

/*
 * MESSAGE_RES_EVT_SUBSCRIBE
 *
 * ics_head:		Result head
 * ics_error:		Request results
 *
 */
struct res_evt_event_subscribe {
	mar_res_header_t	ics_head;
};

/*
 * MESSAGE_REQ_EVT_UNSUBSCRIBE
 *
 * icu_head:			Request head
 * icu_channel_handle:	Server handle of channel
 * icu_sub_id:			Subscription ID
 *
 */
struct req_evt_event_unsubscribe {

	mar_req_header_t		icu_head;
	uint32_t				icu_channel_handle;
	SaEvtSubscriptionIdT	icu_sub_id;
};


/*
 * MESSAGE_RES_EVT_UNSUBSCRIBE
 *
 * icu_head:		Results head
 * icu_error:		request result
 *
 */
struct res_evt_event_unsubscribe {
	mar_res_header_t	icu_head;

};

/*
 * MESSAGE_REQ_EVT_EVENT_DATA
 * MESSAGE_RES_EVT_AVAILABLE
 *
 * evd_head:		Request Head
 */
struct res_evt_event_data {
		mar_res_header_t	evd_head;
};

/*
 * MESSAGE_REQ_EVT_PUBLISH			(1)
 * MESSAGE_RES_EVT_EVENT_DATA		(2)
 * MESSAGE_REQ_EXEC_EVT_EVENTDATA	(3)
 * MESSAGE_REQ_EXEC_EVT_RECOVERY_EVENTDATA	(4)
 *
 * led_head:				Request/Results head
 * led_addr:				address of node (4 only)
 * led_receive_time:		Time that the message was received (4 only)
 * led_svr_channel_handle:	Server channel handle (1 only)
 * led_lib_channel_handle:	Lib channel handle (2 only)
 * led_chan_name:			Channel name (3 and 4 only)
 * led_chan_unlink_id:		directs delivery to unlinked channels.
 * led_event_id:			Event ID (2, 3 and 4 only)
 * led_msg_id:				message ID (2, 3 and 4 only)
 * led_sub_id:				Subscription ID (2 only)
 * led_publisher_node_id:	Node ID of event publisher
 * led_publisher_name:		Node name of event publisher
 * led_retention_time:		Event retention time
 * led_publish_time:		Publication time of the event
 * led_priority:			Event priority
 * led_user_data_offset:	Offset to user data
 * led_user_data_size:		Size of user data
 * led_patterns_number:		Number of patterns in the event
 * led_body:				Pattern and user data
 */
struct lib_event_data {
	mar_res_header_t		led_head;
	unsigned int			led_nodeid;
	SaTimeT					led_receive_time;
	uint32_t				led_svr_channel_handle;
	SaEvtChannelHandleT		led_lib_channel_handle;
	SaNameT					led_chan_name;
	uint64_t				led_chan_unlink_id;
	SaEvtEventIdT			led_event_id;
	uint64_t				led_msg_id;
	SaEvtSubscriptionIdT	led_sub_id;
	SaClmNodeIdT			led_publisher_node_id;
	SaNameT					led_publisher_name;
	SaTimeT					led_retention_time;
	SaTimeT					led_publish_time;
	SaEvtEventPriorityT		led_priority;
	uint32_t				led_user_data_offset;
	uint32_t				led_user_data_size;
	uint32_t				led_patterns_number;
	uint8_t					led_body[0];
};

/*
 * MESSAGE_RES_EVT_PUBLISH
 *
 * iep_head:		Result head
 * iep_error:		Request results
 * iep_event_id:	Event ID of published message
 *
 */
struct res_evt_event_publish {

	mar_res_header_t	iep_head;
	SaEvtEventIdT		iep_event_id;
};

/*
 * MESSAGE_REQ_EVT_CLEAR_RETENTIONTIME
 *
 * Request message:
 *
 * iec_head:			Request head
 * iec_event_id:		ID of event to clear
 * iec_channel_handle:	Server handle of associate channel
 *
 */
struct req_evt_event_clear_retentiontime {

	mar_req_header_t	iec_head;
	SaEvtEventIdT		iec_event_id;
	uint32_t			iec_channel_handle;

};

/*
 * MESSAGE_RES_EVT_CLEAR_RETENTIONTIME
 *
 * iec_head:		Results head
 * iec_error:		Request result
 *
 */
struct res_evt_event_clear_retentiontime {
	mar_res_header_t	iec_head;
};


/*
 * MESSAGE_REQ_EXEC_EVT_CHANCMD
 *
 * Used for various event related operations.
 *
 */
enum evt_chan_ops {
	EVT_OPEN_CHAN_OP,		/* chc_chan */
	EVT_CLOSE_CHAN_OP,  	/* chc_close_unlink_chan */
	EVT_UNLINK_CHAN_OP,  	/* chc_close_unlink_chan */
	EVT_CLEAR_RET_OP,		/* chc_event_id */
	EVT_SET_ID_OP,			/* chc_set_id */
	EVT_CONF_DONE,			/* no data used */
	EVT_OPEN_COUNT,			/* chc_set_opens */
	EVT_OPEN_COUNT_DONE		/* no data used */
};
	
/*
 * Used during recovery to set the next issued event ID
 * based on the highest ID seen by any of the members
 */
struct evt_set_id {
	unsigned int 	chc_nodeid;
	uint64_t		chc_last_id;
};

/*
 * For set open count used during recovery to syncronize all nodes
 *
 * 	chc_chan_name:		Channel name.
 * 	chc_open_count:		number of local opens of this channel.
 */
struct evt_set_opens {
	SaNameT		chc_chan_name;
	uint32_t	chc_open_count;
};

/*
 * Used to communicate channel to close or unlink.
 */
#define EVT_CHAN_ACTIVE 0
struct evt_close_unlink_chan {
	SaNameT		chcu_name;
	uint64_t	chcu_unlink_id;
};

struct open_chan_req {
	SaNameT		ocr_name;
	uint64_t	ocr_serial_no;
};

/*
 * Sent via MESSAGE_REQ_EXEC_EVT_CHANCMD
 *
 * chc_head:	Request head
 * chc_op:		Channel operation (open, close, clear retentiontime)
 * u:			union of operation options.
 */
struct req_evt_chan_command {
	mar_req_header_t 	chc_head;
	int 				chc_op;
	union {
		struct open_chan_req	chc_chan;
		SaEvtEventIdT			chc_event_id;
		struct evt_set_id		chc_set_id;
		struct evt_set_opens 	chc_set_opens;
		struct evt_close_unlink_chan	chcu;
	} u;
};

#endif  /* AIS_EVT_H_DEFINED */
/*
 *	vi: set autoindent tabstop=4 shiftwidth=4 :
 */
