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

#define DEBUG
#include <sys/types.h>
#include <malloc.h>
#include <errno.h>
#include "../include/ais_types.h"
#include "../include/ais_msg.h"
#include "../include/list.h"
#include "../include/queue.h"
#include "aispoll.h"
#include "mempool.h"
#include "parse.h"
#include "main.h"
#include "print.h"
#include "gmi.h"
#include "evt.h"

static int message_handler_req_lib_activatepoll (struct conn_info *conn_info, 
		void *message);
static int lib_evt_open_channel(struct conn_info *conn_info, void *message);
static int lib_evt_close_channel(struct conn_info *conn_info, void *message);
static int lib_evt_channel_subscribe(struct conn_info *conn_info, 
		void *message);
static int lib_evt_channel_unsubscribe(struct conn_info *conn_info, 
		void *message);
static int lib_evt_event_publish(struct conn_info *conn_info, void *message);
static int lib_evt_event_clear_retentiontime(struct conn_info *conn_info, 
		void *message);
static int evt_conf_change(
		struct sockaddr_in *member_list, int member_list_entries,
		struct sockaddr_in *left_list, int left_list_entries,
		struct sockaddr_in *joined_list, int joined_list_entries);
static int evt_init(struct conn_info *conn_info, void *msg);
static int evt_exit(struct conn_info *conn_info);
static int evt_exec_init(void);

static int (*evt_libais_handler_fns[]) (struct conn_info *ci, void *m) = {
	message_handler_req_lib_activatepoll,
	lib_evt_open_channel,
	lib_evt_close_channel,
	lib_evt_channel_subscribe,
	lib_evt_channel_unsubscribe,
	lib_evt_event_publish,
	lib_evt_event_clear_retentiontime
};

static int evt_remote_evt(void *msg, struct in_addr source_addr);
static int evt_remote_chan_op(void *msg, struct in_addr source_addr);

static int (*evt_exec_handler_fns[]) (void *m, struct in_addr s) = {
	evt_remote_evt,
	evt_remote_chan_op
};

struct service_handler evt_service_handler = {
	.libais_handler_fns			= evt_libais_handler_fns,
	.libais_handler_fns_count	= sizeof(evt_libais_handler_fns) /
									sizeof(int (*)),
	.aisexec_handler_fns		= evt_exec_handler_fns,
	.aisexec_handler_fns_count	= sizeof(evt_exec_handler_fns) /
									sizeof(int (*)),
	.confchg_fn					= evt_conf_change,
	.libais_init_fn				= evt_init,
	.libais_exit_fn				= evt_exit,
	.aisexec_init_fn			= evt_exec_init
};


/*
 * Take the filters we received from the application via the library and 
 * make them into a real SaEvtEventFilterArrayT
 */
static SaErrorT evtfilt_to_aisfilt(struct req_evt_channel_subscribe *req,
		SaEvtEventFilterArrayT **evtfilters)
{

	SaEvtEventFilterArrayT *filta = (SaEvtEventFilterArrayT *)req->ics_filter_data;
	SaEvtEventFilterArrayT *filters;
	SaEvtEventFilterT *filt = (void *)filta + sizeof(SaEvtEventFilterArrayT);
	SaUint8T *str = (void *)filta + sizeof(SaEvtEventFilterArrayT) + 
			(sizeof(SaEvtEventFilterT) * filta->filtersNumber);
	int i;

	filters = malloc(sizeof(SaEvtEventFilterArrayT));
	if (!filters) {
		return SA_ERR_NO_MEMORY;
	}

	filters->filtersNumber = filta->filtersNumber;
	filters->filters = malloc(sizeof(SaEvtEventFilterT) * 
				filta->filtersNumber);

	for (i = 0; i < filters->filtersNumber; i++) {
		filters->filters[i].filter.pattern = 
			malloc(filt[i].filter.patternSize);
		/*
		 * TODO: Back out of previous allocs if malloc fails
		 */
		filters->filters[i].filter.patternSize = 
			filt[i].filter.patternSize;
		memcpy(filters->filters[i].filter.pattern,
				str, filters->filters[i].filter.patternSize);
		str += filters->filters[i].filter.patternSize;
	}

	*evtfilters = filters;
	
	return SA_OK;
}


static int message_handler_req_lib_activatepoll(struct conn_info *conn_info, 
		void *message)
{
	struct res_lib_activatepoll res;

	res.header.magic = MESSAGE_MAGIC;
	res.header.size = sizeof (struct res_lib_activatepoll);
	res.header.id = MESSAGE_RES_LIB_ACTIVATEPOLL;
	libais_send_response(conn_info, &res, sizeof(res));

	return (0);
}

static int evt_init(struct conn_info *conn_info, void *msg)
{
	struct res_lib_init res;

	
	res.header.magic = MESSAGE_MAGIC;
	res.header.size = sizeof (struct res_lib_init);
	res.header.id = MESSAGE_RES_INIT;
	res.error = SA_OK;

	log_printf(LOG_LEVEL_DEBUG, 
			"Got request to initalize cluster event service.\n");
	if (!conn_info->authenticated) {
		log_printf(LOG_LEVEL_DEBUG, 
				"event service: Not authenticated\n");
		res.error = SA_ERR_SECURITY;
		libais_send_response(conn_info, &res, sizeof(res));
		return -1;
	}

	conn_info->service = SOCKET_SERVICE_EVT;
	libais_send_response (conn_info, &res, sizeof(res));
	list_init (&conn_info->conn_list);

	return 0;
}

static int lib_evt_open_channel(struct conn_info *conn_info, void *message)
{
	struct req_evt_channel_open *req;
	struct res_evt_channel_open res;

	req = message;


	log_printf(LOG_LEVEL_DEBUG, "Open channel request\n");
	log_printf(LOG_LEVEL_DEBUG, 
			"magic %x, size %d, id %d, handle 0x%x, to 0x%llx\n",
			req->ico_head.magic,
			req->ico_head.size,
			req->ico_head.id,
			req->ico_c_handle,
			req->ico_timeout);
	log_printf(LOG_LEVEL_DEBUG, "flags %x, channel name(%d)  %s\n",
			req->ico_open_flag,
			req->ico_channel_name.length,
			req->ico_channel_name.value);

	/*
	 * TODO: Add open code here
	 */

	res.ico_head.magic = MESSAGE_MAGIC;
	res.ico_head.size = sizeof(res);
	res.ico_head.id = MESSAGE_RES_EVT_OPEN_CHANNEL;
	res.ico_error = SA_OK;
	res.ico_channel_handle = req->ico_c_handle; /* TODO: fix this */
	log_printf(LOG_LEVEL_DEBUG, 
			"magic %x, size %d, id %d, error 0x%x, handle 0x%x\n",
			res.ico_head.magic,
			res.ico_head.size,
			res.ico_head.id,
			res.ico_error,
			res.ico_channel_handle);
	libais_send_response (conn_info, &res, sizeof(res));

	return 0;
}

static int lib_evt_close_channel(struct conn_info *conn_info, void *message)
{
	struct req_evt_channel_close *req;
	struct res_evt_channel_close res;

	req = message;

	log_printf(LOG_LEVEL_DEBUG, "Close channel request\n");
	log_printf(LOG_LEVEL_DEBUG, "magic %x, size %d, id %d, handle 0x%x\n",
			req->icc_head.magic,
			req->icc_head.size,
			req->icc_head.id,
			req->icc_channel_handle);

	/*
	 * TODO: Add close code here
	 */

	res.icc_head.magic = MESSAGE_MAGIC;
	res.icc_head.size = sizeof(res);
	res.icc_head.id = MESSAGE_RES_EVT_CLOSE_CHANNEL;
	res.icc_error = SA_OK;
	libais_send_response (conn_info, &res, sizeof(res));

	return 0;
}

static int lib_evt_channel_subscribe(struct conn_info *conn_info, void *message)
{
	struct req_evt_channel_subscribe *req;
	struct res_evt_channel_subscribe res;
	SaEvtEventFilterArrayT *filters;
	SaErrorT error = SA_OK;
	int i;

	req = message;

	log_printf(LOG_LEVEL_DEBUG, "Subscribe channel request\n");
	log_printf(LOG_LEVEL_DEBUG, "magic %x, size %d, id %d\n",
			req->ics_head.magic,
			req->ics_head.size,
			req->ics_head.id);
	log_printf(LOG_LEVEL_DEBUG, "subscription Id: 0x%x\n", req->ics_sub_id);
	error = evtfilt_to_aisfilt(req, &filters);

	if (error == SA_OK) {
		log_printf(LOG_LEVEL_DEBUG, "Subscribe filters count %d\n", 
				filters->filtersNumber);
		for (i = 0; i < filters->filtersNumber; i++) {
			log_printf(LOG_LEVEL_DEBUG, "sz %d, type %d, <%s>\n", 
					filters->filters[i].filterType,
					filters->filters[i].filter.patternSize,
					filters->filters[i].filter.pattern);
		}
	}
	
	/*
	 * TODO: add subscribe code here. 
	 * TODO: remove filters for now to avoid a leak.
	 */
	for (i = 0; i < filters->filtersNumber; i++) {
		free(filters->filters[i].filter.pattern);
	}
	free(filters->filters);
	free(filters);

	res.ics_head.magic = MESSAGE_MAGIC;
	res.ics_head.size = sizeof(res);
	res.ics_head.id = MESSAGE_RES_EVT_SUBSCRIBE;
	res.ics_error = error;
	libais_send_response (conn_info, &res, sizeof(res));
	
	return 0;
}
static int lib_evt_channel_unsubscribe(struct conn_info *conn_info, 
		void *message)
{
	struct req_evt_channel_unsubscribe *req;
	struct res_evt_channel_unsubscribe res;
	SaErrorT error = SA_OK;

	req = message;

	log_printf(LOG_LEVEL_DEBUG, "Unsubscribe channel request\n");
	log_printf(LOG_LEVEL_DEBUG, "magic %x, size %d, id %d\n",
			req->icu_head.magic,
			req->icu_head.size,
			req->icu_head.id);
	log_printf(LOG_LEVEL_DEBUG, "subscription Id: 0x%x\n", req->icu_sub_id);

	/*
	 * TODO: Add unsubscribe code here
	 */


	res.icu_head.magic = MESSAGE_MAGIC;
	res.icu_head.size = sizeof(res);
	res.icu_head.id = MESSAGE_RES_EVT_UNSUBSCRIBE;
	res.icu_error = error;
	libais_send_response (conn_info, &res, sizeof(res));
	
	return 0;
}

static int lib_evt_event_publish(struct conn_info *conn_info, void *message)
{
	struct lib_event_data *req;
	struct res_evt_event_publish res;
	SaEvtEventIdT event_id = 0x5a5a5a5a5a5a5a5aull;
	SaErrorT error = SA_OK;

	req = message;

	log_printf(LOG_LEVEL_DEBUG, "Publish event request\n");
	log_printf(LOG_LEVEL_DEBUG, "magic %x, size %d, id %d\n",
			req->led_head.magic,
			req->led_head.size,
			req->led_head.id);

	/*
	 * TODO: Add publish code here
	 */

	res.iep_head.magic = MESSAGE_MAGIC;
	res.iep_head.size = sizeof(res);
	res.iep_head.id = MESSAGE_RES_EVT_PUBLISH;
	res.iep_error = error;
	res.iep_event_id = event_id;
	libais_send_response (conn_info, &res, sizeof(res));

	return 0;
}
static int lib_evt_event_clear_retentiontime(struct conn_info *conn_info, void *message)
{
	struct req_evt_event_clear_retentiontime *req;
	struct res_evt_event_clear_retentiontime res;
	SaErrorT error = SA_OK;

	req = message;

	log_printf(LOG_LEVEL_DEBUG, "Clear event retentiontime request\n");
	log_printf(LOG_LEVEL_DEBUG, 
		"magic %x, size %d, id %d, event ID 0x%llx, chan handle 0x%x\n",
			req->iec_head.magic,
			req->iec_head.size,
			req->iec_head.id,
			req->iec_event_id,
			req->iec_channel_handle);

	/*
	 * TODO: Add clear retention time code here
	 */

	res.iec_head.magic = MESSAGE_MAGIC;
	res.iec_head.size = sizeof(res);
	res.iec_head.id = MESSAGE_REQ_EVT_CLEAR_RETENTIONTIME;
	res.iec_error = error;
	libais_send_response (conn_info, &res, sizeof(res));

	return 0;
}
static int evt_conf_change(
		struct sockaddr_in *member_list, int member_list_entries,
		struct sockaddr_in *left_list, int left_list_entries,
		struct sockaddr_in *joined_list, int joined_list_entries)
{
	log_printf(LOG_LEVEL_DEBUG, "Evt conf change\n");
	
	return 0;
}
static int evt_exit(struct conn_info *conn_info)
{
	log_printf(LOG_LEVEL_DEBUG, "Evt exit request\n");

	/*
	 * Delete track entry if there is one
	 */
	list_del (&conn_info->conn_list);

	return 0;
}
static int evt_exec_init(void)
{
	log_printf(LOG_LEVEL_DEBUG, "Evt exec exit request\n");
	return 0;
}

static int evt_remote_evt(void *msg, struct in_addr source_addr)
{
	log_printf(LOG_LEVEL_DEBUG, "Remote event data received");
	return 0;
}
static int evt_remote_chan_op(void *msg, struct in_addr source_addr)
{
	log_printf(LOG_LEVEL_DEBUG, "Remote channel operation request\n");
	return 0;
}
