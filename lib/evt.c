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

#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#include "../include/ipc_evt.h"
#include "util.h"
#include "../exec/totem.h"
#include "../include/list.h"

static void evtHandleInstanceDestructor(void *instance);
static void chanHandleInstanceDestructor(void *instance);
static void eventHandleInstanceDestructor(void *instance);

/*
 * Versions of the SAF AIS specification supported by this library
 */
static SaVersionT supported_versions[] = {
	{'B', 0x01, 0x01}
};

static struct saVersionDatabase evt_version_database = {
	sizeof(supported_versions) / sizeof(SaVersionT),
	supported_versions
};

/*
 * Event instance data
 */
struct saHandleDatabase evt_instance_handle_db = {
	.handleCount	= 0,
	.handles 		= 0,
	.mutex			= PTHREAD_MUTEX_INITIALIZER,
	.handleInstanceDestructor	= evtHandleInstanceDestructor
};

/*
 * Channel instance data
 */
struct saHandleDatabase channel_handle_db = {
	.handleCount	= 0,
	.handles 		= 0,
	.mutex			= PTHREAD_MUTEX_INITIALIZER,
	.handleInstanceDestructor	= chanHandleInstanceDestructor
};

/*
 * Event instance data
 */
struct saHandleDatabase event_handle_db = {
	.handleCount	= 0,
	.handles 		= 0,
	.mutex			= PTHREAD_MUTEX_INITIALIZER,
	.handleInstanceDestructor	= eventHandleInstanceDestructor
};

struct res_overlay {
	mar_res_header_t header __attribute__((aligned(8)));
	char data[MESSAGE_SIZE_MAX];
};

struct handle_list {
	SaUint64T			hl_handle;
	struct list_head 	hl_entry;
};

/*
 * data required to support events for a given initialization
 *
 * ei_dispatch_fd:	fd used for getting callback data e.g. async event data
 * ei_response_fd:	fd used for everything else (i.e. evt sync api commands).
 * ei_callback:		callback function.
 * ei_version:		version sent to the evtInitialize call.
 * ei_node_id:		our node id.
 * ei_node_name:	our node name.
 * ei_finalize:		instance in finalize flag
 * ei_dispatch_mutex:	mutex for dispatch fd
 * ei_response_mutex:	mutex for response fd
 * ei_channel_list:		list of associated channels (struct handle_list)
 * ei_data_available:	Indicates that there is a pending event message though
 * 						there may not be a poll event.  This can happen
 * 						when we get a SA_AIS_ERR_TRY_AGAIN when asking for an
 * 						event.
 *
 */
struct event_instance {
	int 					ei_dispatch_fd;
	int 					ei_response_fd;
	SaEvtCallbacksT			ei_callback;
	SaVersionT				ei_version;
	SaClmNodeIdT			ei_node_id;
	SaNameT					ei_node_name;
	int						ei_finalize;
	pthread_mutex_t			ei_dispatch_mutex;
	pthread_mutex_t			ei_response_mutex;
	struct list_head 		ei_channel_list;
	int						ei_data_available;
};


/*
 * Data associated with an opened channel
 *
 * eci_channel_name:		name of channel
 * eci_open_flags:			channel open flags
 * eci_svr_channel_handle:	channel handle returned from server
 * eci_closing:				channel in process of being closed
 * eci_mutex:				channel mutex
 * eci_event_list:			events associated with this 
 * 							channel (struct handle_list)
 * eci_hl:					pointer to event instance handle struct
 * 						    for this channel.
 */
struct event_channel_instance {

	SaNameT					eci_channel_name;
	SaEvtChannelOpenFlagsT	eci_open_flags;
	uint32_t				eci_svr_channel_handle;
	SaEvtHandleT			eci_instance_handle;
	int						eci_closing;
	pthread_mutex_t			eci_mutex;
	struct list_head		eci_event_list;
	struct handle_list		*eci_hl;
	 
};

/*
 * Event data. 
 *
 * Store event data from saEvtEventAllocate function.
 * Store event data from received events.
 *
 * edi_channel_handle:	handle (local) of assocated channel
 * edi_patterns:		event patterns
 * edi_priority:		event priority
 * edi_retention_time:	event's retention time
 * edi_pub_name:		event's publisher name
 * edi_pub_node:		event's publisher node
 * edi_pub_time:		event's publish time
 * edi_event_id:		event's Id
 * edi_event_data:		event's data
 * edi_event_data_size:	size of edi_event_data
 * edi_freeing:			event is being freed
 * edi_mutex:			event data mutex
 * edi_hl:				pointer to channel's handle 
 * 						struct for this event.
 * edi_ro:				read only flag
 */
struct event_data_instance {
	SaEvtChannelHandleT		edi_channel_handle;
	SaEvtEventPatternArrayT	edi_patterns;
	SaUint8T				edi_priority;
	SaTimeT					edi_retention_time;
	SaNameT					edi_pub_name;
	SaClmNodeIdT			edi_pub_node;
	SaTimeT					edi_pub_time;
	SaEvtEventIdT			edi_event_id;
	void					*edi_event_data;
	SaSizeT					edi_event_data_size;
	int						edi_freeing;
	pthread_mutex_t			edi_mutex;
	struct handle_list		*edi_hl;
	int 					edi_ro;
};


#define min(a,b) ((a) < (b) ? (a) : (b))

static inline int is_valid_event_id(SaEvtEventIdT evt_id)
{
	if (evt_id > 1000) {
		return 1;
	} else {
		return 0;
	}
}

/*
 * Clean up function for an evt instance (saEvtInitialize) handle
 * Not to be confused with event data.
 */
static void evtHandleInstanceDestructor(void *instance)
{
	struct event_instance *evti = instance;
	struct event_channel_instance *eci;
	struct handle_list *hl;
	struct list_head *l, *nxt;
	uint64_t handle;
	SaAisErrorT error;

	/*
	 * Free up any channel data 
	 */
	for (l = evti->ei_channel_list.next; 
								l != &evti->ei_channel_list; l = nxt) {
		nxt = l->next;

		hl = list_entry(l, struct handle_list, hl_entry);
		handle = hl->hl_handle;
		error = saHandleInstanceGet(&channel_handle_db, hl->hl_handle,
			(void*)&eci);
		if (error != SA_AIS_OK) {
			/*
			 * already gone
			 */
			continue;
		}
		saHandleDestroy(&channel_handle_db, handle);
		saHandleInstancePut(&channel_handle_db, handle);
	}

	pthread_mutex_destroy(&evti->ei_dispatch_mutex);
	pthread_mutex_destroy(&evti->ei_response_mutex);
}

/*
 * Clean up function for an open channel handle
 */
static void chanHandleInstanceDestructor(void *instance)
{
	struct event_channel_instance *eci  = instance;
	struct list_head *l, *nxt;
	struct handle_list *hl;
	uint64_t handle;

	if (eci->eci_hl) {
		list_del(&eci->eci_hl->hl_entry);
		free(eci->eci_hl);
		eci->eci_hl = 0;
	}
	/*
	 * Free up any channel associated events
	 */
	for (l = eci->eci_event_list.next; l != &eci->eci_event_list; l = nxt) {
		nxt = l->next;
		hl = list_entry(l, struct handle_list, hl_entry);
		handle = hl->hl_handle;
		saEvtEventFree(handle);
	}

	pthread_mutex_destroy(&eci->eci_mutex);
}

/*
 * Clean up function for an event handle
 */
static void eventHandleInstanceDestructor(void *instance)
{
	struct event_data_instance *edi = instance;
	int i;

	if (edi->edi_hl) {
		list_del(&edi->edi_hl->hl_entry);
		free(edi->edi_hl);
		edi->edi_hl = 0;
	}
	if (edi->edi_patterns.patterns) {
		for (i = 0; i < edi->edi_patterns.patternsNumber; i++) {
			free(edi->edi_patterns.patterns[i].pattern);
		}
		free(edi->edi_patterns.patterns);
	}
	if (edi->edi_event_data) {
		free(edi->edi_event_data);
	}

	pthread_mutex_destroy(&edi->edi_mutex);
}

static SaAisErrorT evt_recv_event(int fd, struct lib_event_data **msg)
{
	SaAisErrorT error;
	mar_res_header_t hdr;
	void *data;

	error = saRecvRetry(fd, &hdr, sizeof(hdr));
	if (error != SA_AIS_OK) {
		goto msg_out;
	}
	*msg = malloc(hdr.size);
	if (!*msg) {
		error = SA_AIS_ERR_LIBRARY;
		goto msg_out;
	}
	data = (void *)((unsigned long)*msg) + sizeof(hdr);
	memcpy(*msg, &hdr, sizeof(hdr));
	if (hdr.size > sizeof(hdr)) {
		error = saRecvRetry(fd, data, hdr.size - sizeof(hdr));
		if (error != SA_AIS_OK) {
			goto msg_out;
		}
	}
msg_out:
	return error;
}

/* 
 * The saEvtInitialize() function initializes the Event Service for the 
 * invoking process. A user of the Event Service must invoke this function 
 * before it invokes any other function of the Event Service API. Each 
 * initialization returns a different callback handle that the process 
 * can use to communicate with that library instance.
 */

SaAisErrorT 
saEvtInitialize(
	SaEvtHandleT *evtHandle,
	const SaEvtCallbacksT *callbacks,
	SaVersionT *version)
{
	SaAisErrorT error = SA_AIS_OK;
	struct event_instance *evti;

	if (!version || !evtHandle) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_nofree;
	}
	/*
	 * validate the requested version with what we support
	 */
	error = saVersionVerify(&evt_version_database, version);
	if (error != SA_AIS_OK) {
		goto error_nofree;
	}

	/*
	 * Allocate instance data, allocate unique handle for instance,
	 * assign instance data to unique handle
	 */
	error = saHandleCreate(&evt_instance_handle_db, sizeof(*evti), 
			evtHandle);
	if (error != SA_AIS_OK) {
		goto error_nofree;
	}
	error = saHandleInstanceGet(&evt_instance_handle_db, *evtHandle,
			(void*)&evti);
	if (error != SA_AIS_OK) {
		if (error == SA_AIS_ERR_BAD_HANDLE) {
			error = SA_AIS_ERR_LIBRARY;
		}
		goto error_handle_free;
	}
	memset(evti, 0, sizeof(*evti));
	
	list_init(&evti->ei_channel_list);

	/*
	 * Save the version so we can check with the event server
	 * and see if it supports this version.
	 */
	evti->ei_version = *version;

	/*
	 * Set up communication with the event server
	 */
	error = saServiceConnect(&evti->ei_response_fd,
		&evti->ei_dispatch_fd, EVT_SERVICE);
	if (error != SA_AIS_OK) {
		goto error_handle_put;
	}

	/*
	 * The callback function is saved in the event instance for 
	 * saEvtDispatch() to use.
	 */
	if (callbacks) {
		memcpy(&evti->ei_callback, callbacks, 
				sizeof(evti->ei_callback));
	}

 	pthread_mutex_init(&evti->ei_dispatch_mutex, NULL);
 	pthread_mutex_init(&evti->ei_response_mutex, NULL);
	saHandleInstancePut(&evt_instance_handle_db, *evtHandle);

	return SA_AIS_OK;

error_handle_put:
	saHandleInstancePut(&evt_instance_handle_db, *evtHandle);
error_handle_free:
	(void)saHandleDestroy(&evt_instance_handle_db, *evtHandle);
error_nofree:
	return error;
}

/*
 * The saEvtSelectionObjectGet() function returns the operating system 
 * handle selectionObject, associated with the handle evtHandle, allowing 
 * the invoking process to ascertain when callbacks are pending. This 
 * function allows a process to avoid repeated invoking saEvtDispatch() to 
 * see if there is a new event, thus, needlessly consuming CPU time. In a 
 * POSIX environment the system handle could be a file descriptor that is 
 * used with the poll() or select() system calls to detect incoming callbacks.
 */
SaAisErrorT
saEvtSelectionObjectGet(
	SaEvtHandleT evtHandle,
	SaSelectionObjectT *selectionObject)
{
	struct event_instance *evti;
	SaAisErrorT error;

	if (!selectionObject) {
		return SA_AIS_ERR_INVALID_PARAM;
	}

	error = saHandleInstanceGet(&evt_instance_handle_db, evtHandle, 
			(void *)&evti);

	if (error != SA_AIS_OK) {
		return error;
	}

	*selectionObject = evti->ei_dispatch_fd;

	saHandleInstancePut(&evt_instance_handle_db, evtHandle);

	return SA_AIS_OK;
}


/*
 * Alocate an event data structure and associated handle to be
 * used to supply event data to a call back function.
 */
static SaAisErrorT make_event(SaEvtEventHandleT *event_handle,
				struct lib_event_data *evt)
{
	struct event_data_instance *edi;
	struct event_channel_instance *eci;
	mar_evt_event_pattern_t *pat;
	SaUint8T *str;
	SaAisErrorT error;
	struct handle_list *hl;
	int i;

	error = saHandleCreate(&event_handle_db, sizeof(*edi), 
		event_handle);
	if (error != SA_AIS_OK) {
		if (error == SA_AIS_ERR_NO_MEMORY) {
			error = SA_AIS_ERR_LIBRARY;
		}
			goto make_evt_done;
	}

	error = saHandleInstanceGet(&event_handle_db, *event_handle,
				(void*)&edi);
	if (error != SA_AIS_OK) {
			saHandleDestroy(&event_handle_db, *event_handle);
			goto make_evt_done;
	}

	error = saHandleInstanceGet(&channel_handle_db, 
			evt->led_lib_channel_handle,
			(void*)&eci);
	if (error != SA_AIS_OK) {
		saHandleDestroy(&event_handle_db, *event_handle);
		goto make_evt_done_put;
	}

	pthread_mutex_init(&edi->edi_mutex, NULL);
	edi->edi_ro = 1;
	edi->edi_freeing = 0;
	edi->edi_channel_handle = evt->led_lib_channel_handle;
	edi->edi_priority = evt->led_priority;
	edi->edi_retention_time = evt->led_retention_time;
	edi->edi_pub_node = evt->led_publisher_node_id;
	edi->edi_pub_time = evt->led_publish_time;
	edi->edi_event_data_size = evt->led_user_data_size;
	edi->edi_event_id = evt->led_event_id;
	marshall_from_mar_name_t (&edi->edi_pub_name, &evt->led_publisher_name);

	if (edi->edi_event_data_size) {
		edi->edi_event_data = malloc(edi->edi_event_data_size);
		if (!edi->edi_event_data) {
			saHandleDestroy(&event_handle_db, *event_handle);

			/*
			 * saEvtDispatch doesn't return SA_AIS_ERR_NO_MEMORY
			 */
			error = SA_AIS_ERR_LIBRARY;
			goto make_evt_done_put2;
		}
		memcpy(edi->edi_event_data, 
				evt->led_body + evt->led_user_data_offset,
				edi->edi_event_data_size);
	}

	/*
	 * Move the pattern bits into the SaEvtEventPatternArrayT
	 */
	edi->edi_patterns.patternsNumber = evt->led_patterns_number;
	edi->edi_patterns.allocatedNumber = evt->led_patterns_number;
	edi->edi_patterns.patterns = malloc(sizeof(SaEvtEventPatternT) * 
					edi->edi_patterns.patternsNumber);
	if (!edi->edi_patterns.patterns) {
		/*
		 * The destructor will take care of freeing event data already
		 * allocated.
		 */
		edi->edi_patterns.patternsNumber = 0;
		saHandleDestroy(&event_handle_db, *event_handle);

		/*
		 * saEvtDispatch doesn't return SA_AIS_ERR_NO_MEMORY
		 */
		error = SA_AIS_ERR_LIBRARY;
		goto make_evt_done_put2;
	}
	memset(edi->edi_patterns.patterns, 0, sizeof(SaEvtEventPatternT) *
					edi->edi_patterns.patternsNumber);
	pat = (mar_evt_event_pattern_t *)evt->led_body;
	str = evt->led_body + sizeof(mar_evt_event_pattern_t) * 
						edi->edi_patterns.patternsNumber;
	for (i = 0; i < evt->led_patterns_number; i++) {
		edi->edi_patterns.patterns[i].patternSize = pat->pattern_size;
		edi->edi_patterns.patterns[i].allocatedSize = pat->pattern_size;
		edi->edi_patterns.patterns[i].pattern = malloc(pat->pattern_size);
		if (!edi->edi_patterns.patterns[i].pattern) {
            DPRINT (("make_event: couldn't alloc %llu bytes\n",
				(unsigned long long)pat->pattern_size));
			saHandleDestroy(&event_handle_db, *event_handle);
			error =  SA_AIS_ERR_LIBRARY;
			goto make_evt_done_put2;
		}
		memcpy(edi->edi_patterns.patterns[i].pattern,
				str, pat->pattern_size);
		str += pat->pattern_size;
		pat++; 
	}

	hl = malloc(sizeof(*hl));
	if (!hl) {
		saHandleDestroy(&event_handle_db, *event_handle);
		error = SA_AIS_ERR_LIBRARY;
	} else {
		edi->edi_hl = hl;
		hl->hl_handle = *event_handle;
		list_init(&hl->hl_entry);
		list_add(&hl->hl_entry, &eci->eci_event_list);
	}

make_evt_done_put2:
	saHandleInstancePut (&channel_handle_db, evt->led_lib_channel_handle);

make_evt_done_put:
	saHandleInstancePut (&event_handle_db, *event_handle);

make_evt_done:
	return error;
}

/*
 * The saEvtDispatch() function invokes, in the context of the calling 
 * thread, one or all of the pending callbacks for the handle evtHandle.
 */
SaAisErrorT
saEvtDispatch(
	SaEvtHandleT evtHandle,
	SaDispatchFlagsT dispatchFlags)
{
	struct pollfd ufds;
	int timeout = -1;
	SaAisErrorT error;
	int dispatch_avail;
	struct event_instance *evti;
	SaEvtEventHandleT event_handle;
	SaEvtCallbacksT callbacks;
	int ignore_dispatch = 0;
	int cont = 1; /* always continue do loop except when set to 0 */
	int poll_fd;
	struct res_overlay dispatch_data;
	struct lib_event_data *evt = 0;
	struct res_evt_event_data res;

	if (dispatchFlags < SA_DISPATCH_ONE || 
			dispatchFlags > SA_DISPATCH_BLOCKING) {
		return SA_AIS_ERR_INVALID_PARAM;
	}

	error = saHandleInstanceGet(&evt_instance_handle_db, evtHandle,
		(void *)&evti);
	if (error != SA_AIS_OK) {
		return error;
	}

	/*
	 * Timeout instantly for SA_DISPATCH_ALL
	 */
	if (dispatchFlags == SA_DISPATCH_ALL || dispatchFlags == SA_DISPATCH_ONE) {
		timeout = 0;
	}

	do {
		poll_fd = evti->ei_dispatch_fd;

		ufds.fd = poll_fd;
		ufds.events = POLLIN;
		ufds.revents = 0;

		error = saPollRetry(&ufds, 1, timeout);
		if (error != SA_AIS_OK) {
			goto dispatch_put;
		}

		pthread_mutex_lock(&evti->ei_dispatch_mutex);

		/*
		 * Handle has been finalized in another thread
		 */
		if (evti->ei_finalize == 1) {
			error = SA_AIS_OK;
			goto dispatch_unlock;
		}

		/*
		 * If we know that we have an event waiting, we can skip the
		 * polling and just ask for it.
		 */
		if (!evti->ei_data_available) {
			/*
			 * Check the poll data in case the fd status has changed
			 * since taking the lock
			 */
			error = saPollRetry(&ufds, 1, 0);
			if (error != SA_AIS_OK) {
				goto dispatch_unlock;
			}

			if ((ufds.revents & (POLLERR|POLLHUP|POLLNVAL)) != 0) {
				error = SA_AIS_ERR_BAD_HANDLE;
				goto dispatch_unlock;
			}

			dispatch_avail = ufds.revents & POLLIN;
			if (dispatch_avail == 0 &&
					(dispatchFlags == SA_DISPATCH_ALL ||
					 dispatchFlags == SA_DISPATCH_ONE)) {
				pthread_mutex_unlock(&evti->ei_dispatch_mutex);
				break; /* exit do while cont is 1 loop */
			} else if (dispatch_avail == 0) {
				pthread_mutex_unlock(&evti->ei_dispatch_mutex);
				continue; /* next poll */
			}

			if (ufds.revents & POLLIN) {
				error = saRecvRetry (evti->ei_dispatch_fd, &dispatch_data.header,
					sizeof (mar_res_header_t));

				if (error != SA_AIS_OK) {
					goto dispatch_unlock;
				}
				if (dispatch_data.header.size > sizeof (mar_res_header_t)) {
					error = saRecvRetry (evti->ei_dispatch_fd, &dispatch_data.data,
						dispatch_data.header.size - sizeof (mar_res_header_t));
					if (error != SA_AIS_OK) {
						goto dispatch_unlock;
					}
				}
			} else {
				pthread_mutex_unlock(&evti->ei_dispatch_mutex);
				continue;
			}
		} else {
			/*
			 * We know that we have an event available from before.
			 * Fake up a header message and the switch statement will
			 * take care of the rest.
			 */
			dispatch_data.header.id = MESSAGE_RES_EVT_AVAILABLE;
		}

		/*
		 * Make copy of callbacks, message data, unlock instance, 
		 * and call callback. A risk of this dispatch method is that 
		 * the callback routines may operate at the same time that 
		 * EvtFinalize has been called in another thread.
		 */
		memcpy(&callbacks, &evti->ei_callback, sizeof(evti->ei_callback));
		pthread_mutex_unlock(&evti->ei_dispatch_mutex);


		/*
		 * Dispatch incoming response
		 */
		switch (dispatch_data.header.id) {

		case MESSAGE_RES_EVT_AVAILABLE:
			evti->ei_data_available = 0;
			/*
			 * There are events available.  Send a request for one and then
			 * dispatch it.
			 */
			res.evd_head.id = MESSAGE_REQ_EVT_EVENT_DATA;
			res.evd_head.size = sizeof(res);
 
 			pthread_mutex_lock(&evti->ei_response_mutex);
 			error = saSendRetry(evti->ei_response_fd, &res, sizeof(res));
 
			if (error != SA_AIS_OK) {
				DPRINT (("MESSAGE_RES_EVT_AVAILABLE: send failed: %d\n", error));
 				pthread_mutex_unlock(&evti->ei_response_mutex);
					break;
			}
 			error = evt_recv_event(evti->ei_response_fd, &evt);
 			pthread_mutex_unlock(&evti->ei_response_mutex);

			if (error != SA_AIS_OK) {
				DPRINT (("MESSAGE_RES_EVT_AVAILABLE: receive failed: %d\n", error));
				break;
			}
			/*
 			 * No data available.  This is OK, another thread may have
 			 * grabbed it.
			 */
			if (evt->led_head.error == SA_AIS_ERR_NOT_EXIST) {
				error = SA_AIS_OK;
				break;
			}

			if (evt->led_head.error != SA_AIS_OK) {
				error = evt->led_head.error;

				/*
				 * If we get a try again response, we've lost the poll event
				 * so we have a data available flag so that we know that there
				 * really is an event waiting the next time dispatch gets
				 * called.
				 */
				if (error == SA_AIS_ERR_TRY_AGAIN) {
					evti->ei_data_available = 1;
				} else {
					DPRINT (("MESSAGE_RES_EVT_AVAILABLE: Error returned: %d\n", error));
				}
				break;
			}

			error = make_event(&event_handle, evt);
			if (error != SA_AIS_OK) {
					break;
			}

			/*
			 * Only call if there was a function registered
			 */
			if (callbacks.saEvtEventDeliverCallback) {
				callbacks.saEvtEventDeliverCallback(evt->led_sub_id, 
						event_handle, evt->led_user_data_size);
			}
			break;

		case MESSAGE_RES_EVT_CHAN_OPEN_CALLBACK:
		{
			struct res_evt_open_chan_async *resa = 
				(struct res_evt_open_chan_async *)&dispatch_data;
			struct event_channel_instance *eci;

			/*
			 * Check for errors.  If there are none, then
			 * look up the local channel via the handle that we
			 * got from the callback request.  All we need to do 
			 * is place in the handle from the server side and then 
			 * we can call the callback.
			 */
			error = resa->ica_head.error;
			if (error == SA_AIS_OK) {
				error = saHandleInstanceGet(&channel_handle_db, 
						resa->ica_c_handle, (void*)&eci);
				if (error == SA_AIS_OK) {
					eci->eci_svr_channel_handle = resa->ica_channel_handle;
					saHandleInstancePut (&channel_handle_db, 
							resa->ica_c_handle);
				}
			}

			/*
			 * Only call if there was a function registered
			 */
			if (callbacks.saEvtChannelOpenCallback) {
				callbacks.saEvtChannelOpenCallback(resa->ica_invocation,
					resa->ica_c_handle, error);
			}

		}
			break;

		default:
			DPRINT (("Dispatch: Bad message type 0x%x\n", dispatch_data.header.id));
			error = SA_AIS_ERR_LIBRARY;	
			goto dispatch_put;
		}

		/*
		 * If empty is zero it means the we got the 
		 * message from the queue and we are responsible
		 * for freeing it.
		 */
		if (evt) {
			free(evt);
			evt = 0;
		}

		/*
		 * Determine if more messages should be processed
		 */
		switch (dispatchFlags) {
		case SA_DISPATCH_ONE:
			if (ignore_dispatch) {
				ignore_dispatch = 0;
			} else {
				cont = 0;
			}
			break;
		case SA_DISPATCH_ALL:
			if (ignore_dispatch) {
				ignore_dispatch = 0;
			}
			break;
		case SA_DISPATCH_BLOCKING:
			break;
		}
	} while (cont);

	goto dispatch_put;

dispatch_unlock:
 			pthread_mutex_unlock(&evti->ei_dispatch_mutex);
dispatch_put:
	saHandleInstancePut(&evt_instance_handle_db, evtHandle);
	return error;
}

/*
 * The saEvtFinalize() function closes the association, represented by the 
 * evtHandle parameter, between the process and the Event Service. It may 
 * free up resources. 
 * This function cannot be invoked before the process has invoked the 
 * corresponding saEvtInitialize() function for the Event Service. After 
 * this function is invoked, the selection object is no longer valid. 
 * Moreover, the Event Service is unavailable for further use unless it is 
 * reinitialized using the saEvtInitialize() function.
 */
SaAisErrorT
saEvtFinalize(SaEvtHandleT evtHandle)
{
	struct event_instance *evti;
	SaAisErrorT error;

	error = saHandleInstanceGet(&evt_instance_handle_db, evtHandle, 
			(void *)&evti);
	if (error != SA_AIS_OK) {
		return error;
	}

       pthread_mutex_lock(&evti->ei_response_mutex);

	/*
	 * Another thread has already started finalizing
	 */
	if (evti->ei_finalize) {
		pthread_mutex_unlock(&evti->ei_response_mutex);
		saHandleInstancePut(&evt_instance_handle_db, evtHandle);
		return SA_AIS_ERR_BAD_HANDLE;
	}

	evti->ei_finalize = 1;

	pthread_mutex_unlock(&evti->ei_response_mutex);

	saHandleDestroy(&evt_instance_handle_db, evtHandle);
	/*
	 * Disconnect from the server
	 */
    if (evti->ei_response_fd != -1) {
		shutdown(evti->ei_response_fd, 0);
		close(evti->ei_response_fd);
	}

	if (evti->ei_dispatch_fd != -1) {
		shutdown(evti->ei_dispatch_fd, 0);
		close(evti->ei_dispatch_fd);
	}
	saHandleInstancePut(&evt_instance_handle_db, evtHandle);

	return error;
}

/*
 * The saEvtChannelOpen() function creates a new event channel or open an 
 * existing channel. The saEvtChannelOpen() function is a blocking operation 
 * and returns a new event channel handle. An event channel may be opened 
 * multiple times by the same or different processes for publishing, and 
 * subscribing to, events. If a process opens an event channel multiple 
 * times, it is possible to receive the same event multiple times. However, 
 * a process shall never receive an event more than once on a particular 
 * event channel handle. If a process opens a channel twice and an event is 
 * matched on both open channels, the Event Service performs two 
 * callbacks -- one for each opened channel.
 */
SaAisErrorT 
saEvtChannelOpen(
	SaEvtHandleT evtHandle, 
	const SaNameT *channelName, 
	SaEvtChannelOpenFlagsT channelOpenFlags, 
	SaTimeT timeout,
	SaEvtChannelHandleT *channelHandle)
{
	struct event_instance *evti;
	struct req_evt_channel_open req;
	struct res_evt_channel_open res;
	struct event_channel_instance *eci;
	struct handle_list *hl;
	SaAisErrorT error;
	struct iovec iov;

	if (!channelHandle || !channelName) {
		return SA_AIS_ERR_INVALID_PARAM;
	}

	if ((channelOpenFlags & ~(SA_EVT_CHANNEL_CREATE|SA_EVT_CHANNEL_PUBLISHER|
					SA_EVT_CHANNEL_SUBSCRIBER)) != 0) {
		return SA_AIS_ERR_BAD_FLAGS;
	}

	if (timeout == 0) {
		return (SA_AIS_ERR_TIMEOUT);
	}
	error = saHandleInstanceGet(&evt_instance_handle_db, evtHandle,
			(void*)&evti);
	
	if (error != SA_AIS_OK) {
		goto chan_open_done;
	}

	/*
	 * create a handle for this open channel
	 */
	error = saHandleCreate(&channel_handle_db, sizeof(*eci), 
			channelHandle);
	if (error != SA_AIS_OK) {
		goto chan_open_put;
	}


	error = saHandleInstanceGet(&channel_handle_db, *channelHandle,
					(void*)&eci);
	if (error != SA_AIS_OK) {
		saHandleDestroy(&channel_handle_db, *channelHandle);
		goto chan_open_put;
	}

	list_init(&eci->eci_event_list);

	/*
	 * Send the request to the server and wait for a response
	 */
	req.ico_head.size = sizeof(req);
	req.ico_head.id = MESSAGE_REQ_EVT_OPEN_CHANNEL;
	req.ico_c_handle = *channelHandle;
	req.ico_timeout = timeout;
	req.ico_open_flag = channelOpenFlags;
	marshall_to_mar_name_t (&req.ico_channel_name, (SaNameT *)channelName);

	iov.iov_base = (char *)&req;
	iov.iov_len = sizeof(req);

	pthread_mutex_lock(&evti->ei_response_mutex);

	error = saSendMsgReceiveReply(evti->ei_response_fd, &iov, 1,
		&res, sizeof(res));

	pthread_mutex_unlock (&evti->ei_response_mutex);

	if (error != SA_AIS_OK) {
		goto chan_open_free;
	}

	if (res.ico_head.id != MESSAGE_RES_EVT_OPEN_CHANNEL) {
		error = SA_AIS_ERR_LIBRARY;
		goto chan_open_free;
	}

	error = res.ico_head.error;
	if (error != SA_AIS_OK) {
		goto chan_open_free;
	}

	eci->eci_svr_channel_handle = res.ico_channel_handle;
	eci->eci_channel_name = *channelName;
	eci->eci_open_flags = channelOpenFlags;
	eci->eci_instance_handle = evtHandle;
	eci->eci_closing = 0;
	hl = malloc(sizeof(*hl));
	if (!hl) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto chan_open_free;
	}
	eci->eci_hl = hl;
	hl->hl_handle = *channelHandle;
	list_init(&hl->hl_entry);
	list_add(&hl->hl_entry, &evti->ei_channel_list);

	pthread_mutex_init(&eci->eci_mutex, NULL);
	saHandleInstancePut (&evt_instance_handle_db, evtHandle);
	saHandleInstancePut (&channel_handle_db, *channelHandle);

	return SA_AIS_OK;

chan_open_free:
	saHandleDestroy(&channel_handle_db, *channelHandle);
	saHandleInstancePut (&channel_handle_db, *channelHandle);
chan_open_put:
	saHandleInstancePut (&evt_instance_handle_db, evtHandle);
chan_open_done:
	return error;
}

/*
 * The saEvtChannelClose() function closes an event channel and frees 
 * resources allocated for that event channel in the invoking process. 
 */

SaAisErrorT 
saEvtChannelClose(SaEvtChannelHandleT channelHandle)
{
	SaAisErrorT error;
	struct event_instance *evti;
	struct event_channel_instance *eci;
	struct req_evt_channel_close req;
	struct res_evt_channel_close res;
	struct iovec iov;

	error = saHandleInstanceGet(&channel_handle_db, channelHandle,
			(void*)&eci);
	if (error != SA_AIS_OK) {
		goto chan_close_done;
	}

	/*
	 * get the evt handle for the fd
	 */
	error = saHandleInstanceGet(&evt_instance_handle_db, 
			eci->eci_instance_handle, (void*)&evti);
	if (error != SA_AIS_OK) {
		goto chan_close_put1;
	}

	/*
	 * Make sure that the channel isn't being closed elsewhere
	 */
	pthread_mutex_lock(&eci->eci_mutex);
	if (eci->eci_closing) {
		pthread_mutex_unlock(&eci->eci_mutex);
		saHandleInstancePut(&channel_handle_db, channelHandle);
		return SA_AIS_ERR_BAD_HANDLE;
	}
	eci->eci_closing = 1;
	pthread_mutex_unlock(&eci->eci_mutex);
	

	/*
	 * Send the request to the server and wait for a response
	 */
	req.icc_head.size = sizeof(req);
	req.icc_head.id = MESSAGE_REQ_EVT_CLOSE_CHANNEL;
	req.icc_channel_handle = eci->eci_svr_channel_handle;

	iov.iov_base = (char *)&req;
	iov.iov_len = sizeof (req);

	pthread_mutex_lock(&evti->ei_response_mutex);

	error = saSendMsgReceiveReply (evti->ei_response_fd, &iov, 1,
		&res, sizeof (res));

	pthread_mutex_unlock(&evti->ei_response_mutex);

	if (error != SA_AIS_OK) {
		eci->eci_closing = 0;
		goto chan_close_put2;
	}
	if (res.icc_head.id != MESSAGE_RES_EVT_CLOSE_CHANNEL) {
		error = SA_AIS_ERR_LIBRARY;
		eci->eci_closing = 0;
		goto chan_close_put2;
	}

	error = res.icc_head.error;
	if (error == SA_AIS_ERR_TRY_AGAIN) {
		pthread_mutex_lock(&eci->eci_mutex);
		eci->eci_closing = 0;
		pthread_mutex_unlock(&eci->eci_mutex);
		goto chan_close_put2;
	}

	saHandleInstancePut(&evt_instance_handle_db, 
					eci->eci_instance_handle);
	saHandleDestroy(&channel_handle_db, channelHandle);
	saHandleInstancePut(&channel_handle_db, channelHandle);

	return error;

chan_close_put2:
	saHandleInstancePut(&evt_instance_handle_db, 
					eci->eci_instance_handle);
chan_close_put1:
	saHandleInstancePut(&channel_handle_db, channelHandle);
chan_close_done:
	return error;
}

/*
 * The saEvtChannelOpenAsync() function creates a new event channel or open an 
 * existing channel. The saEvtChannelOpenAsync() function is a non-blocking 
 * operation. A new event channel handle is returned in the channel open
 * callback function (SaEvtChannelOpenCallbackT).
 */
SaAisErrorT
saEvtChannelOpenAsync(SaEvtHandleT evtHandle,
                       SaInvocationT invocation,
                       const SaNameT *channelName,
                       SaEvtChannelOpenFlagsT channelOpenFlags)
{
	struct event_instance *evti;
	struct req_evt_channel_open req;
	struct res_evt_channel_open res;
	struct event_channel_instance *eci;
	SaEvtChannelHandleT channel_handle;
	SaAisErrorT error;
	struct handle_list *hl;
	struct iovec iov;

	if (!channelName) {
		return SA_AIS_ERR_INVALID_PARAM;
	}

	if ((channelOpenFlags & ~(SA_EVT_CHANNEL_CREATE|SA_EVT_CHANNEL_PUBLISHER|
					SA_EVT_CHANNEL_SUBSCRIBER)) != 0) {
		return SA_AIS_ERR_BAD_FLAGS;
	}

	error = saHandleInstanceGet(&evt_instance_handle_db, evtHandle,
			(void*)&evti);
	
	if (error != SA_AIS_OK) {
		goto chan_open_done;
	}

	/*
	 * Make sure that an open channel callback has been 
	 * registered before  allowing the open to continue.
	 */
	if (!evti->ei_callback.saEvtChannelOpenCallback) {
		error = SA_AIS_ERR_INIT;
		goto chan_open_put;
	}

	/*
	 * create a handle for this open channel
	 */
	error = saHandleCreate(&channel_handle_db, sizeof(*eci), 
			&channel_handle);
	if (error != SA_AIS_OK) {
		goto chan_open_put;
	}


	error = saHandleInstanceGet(&channel_handle_db, channel_handle,
					(void*)&eci);
	if (error != SA_AIS_OK) {
		saHandleDestroy(&channel_handle_db, channel_handle);
		goto chan_open_put;
	}

	list_init(&eci->eci_event_list);

	/*
	 * Send the request to the server.  The response isn't the open channel,
	 * just an ack.  The open channel will be returned when the channel open
	 * callback is called.
	 */
	req.ico_head.size = sizeof(req);
	req.ico_head.id = MESSAGE_REQ_EVT_OPEN_CHANNEL_ASYNC;
	req.ico_c_handle = channel_handle;
	req.ico_timeout = 0;
	req.ico_invocation = invocation;
	req.ico_open_flag = channelOpenFlags;
	marshall_to_mar_name_t (&req.ico_channel_name, (SaNameT *)channelName);
	iov.iov_base = (char *)&req;
	iov.iov_len = sizeof(req);


	pthread_mutex_lock(&evti->ei_response_mutex);

	error = saSendMsgReceiveReply (evti->ei_response_fd, &iov, 1,
		&res, sizeof (res));

	pthread_mutex_unlock(&evti->ei_response_mutex);

	if (error != SA_AIS_OK) {
		goto chan_open_free;
	}

	if (res.ico_head.id != MESSAGE_RES_EVT_OPEN_CHANNEL) {
		error = SA_AIS_ERR_LIBRARY;
		goto chan_open_free;
	}

	error = res.ico_head.error;
	if (error != SA_AIS_OK) {
		goto chan_open_free;
	}

	eci->eci_svr_channel_handle = 0; /* filled in by callback */
	eci->eci_channel_name = *channelName;
	eci->eci_open_flags = channelOpenFlags;
	eci->eci_instance_handle = evtHandle;
	eci->eci_closing = 0;
	list_init(&eci->eci_event_list);
	hl = malloc(sizeof(*hl));
	if (!hl) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto chan_open_free;
	}
	eci->eci_hl = hl;
	hl->hl_handle = channel_handle;
	list_init(&hl->hl_entry);
	list_add(&hl->hl_entry, &evti->ei_channel_list);

	pthread_mutex_init(&eci->eci_mutex, NULL);
	saHandleInstancePut (&evt_instance_handle_db, evtHandle);
	saHandleInstancePut (&channel_handle_db, channel_handle);

	return SA_AIS_OK;

chan_open_free:
	saHandleDestroy(&channel_handle_db, channel_handle);
	saHandleInstancePut (&channel_handle_db, channel_handle);
chan_open_put:
	saHandleInstancePut (&evt_instance_handle_db, evtHandle);
chan_open_done:
	return error;
}
/*
 *  The SaEvtChannelUnlink function deletes an existing event channel 
 *  from the cluster. 
 *
 *	After completion of the invocation: 
 *		- An open of the channel name without a create will fail.
 *		- The channel remains available to any already opened instances.
 *		- If an open/create is performed on this channel name a new instance
 *		  is created.
 *		- The ulinked channel's resources will be deleted when the last open
 *		  instance is closed.
 *		   
 *		Note that an event channel is only deleted from the cluster 
 *		namespace when saEvtChannelUnlink() is invoked on it. The deletion 
 *		of an event channel frees all resources allocated by the Event 
 *		Service for it, such as published events with non-zero retention 
 *		time.
 */
SaAisErrorT
saEvtChannelUnlink(
	SaEvtHandleT evtHandle,
	const SaNameT *channelName)
{
	struct event_instance *evti;
	struct req_evt_channel_unlink req;
	struct res_evt_channel_unlink res;
	struct iovec iov;
	SaAisErrorT error;

	if (!channelName) {
		return SA_AIS_ERR_INVALID_PARAM;
	}

	error = saHandleInstanceGet(&evt_instance_handle_db, evtHandle,
			(void*)&evti);
	
	if (error != SA_AIS_OK) {
		goto chan_unlink_done;
	}

	/*
	 * Send the request to the server and wait for a response
	 */
	req.iuc_head.size = sizeof(req);
	req.iuc_head.id = MESSAGE_REQ_EVT_UNLINK_CHANNEL;
	marshall_to_mar_name_t (&req.iuc_channel_name, (SaNameT *)channelName);
	iov.iov_base = (char *)&req;
	iov.iov_len = sizeof(req);


	pthread_mutex_lock(&evti->ei_response_mutex);

	error = saSendMsgReceiveReply (evti->ei_response_fd, &iov, 1,
		&res, sizeof (res));

	pthread_mutex_unlock(&evti->ei_response_mutex);

	if (error != SA_AIS_OK) {
		goto chan_unlink_put;
	}

	if (res.iuc_head.id != MESSAGE_RES_EVT_UNLINK_CHANNEL) {
		error = SA_AIS_ERR_LIBRARY;
		goto chan_unlink_put;
	}

	error = res.iuc_head.error;

chan_unlink_put:
	saHandleInstancePut (&evt_instance_handle_db, evtHandle);
chan_unlink_done:
	return error;
}

/* 
 * The saEvtEventAllocate() function allocates memory for the event, but not 
 * for the eventHandle, and initializes all event attributes to default values.
 * The event allocated by saEvtEventAllocate() is freed by invoking 
 * saEvtEventFree(). 
 * The memory associated with the eventHandle is not deallocated by the 
 * saEvtEventAllocate() function or the saEvtEventFree() function. It is the 
 * responsibility of the invoking process to deallocate the memory for the 
 * eventHandle when the process has published the event and has freed the 
 * event by invoking saEvtEventFree().
 */
SaAisErrorT 
saEvtEventAllocate(
	const SaEvtChannelHandleT channelHandle, 
	SaEvtEventHandleT *eventHandle)
{
	SaAisErrorT error;
	struct event_data_instance *edi;
	struct event_instance *evti;
	struct event_channel_instance *eci;
	struct handle_list *hl;

	if (!eventHandle) {
		return SA_AIS_ERR_INVALID_PARAM;
	}

	error = saHandleInstanceGet(&channel_handle_db, channelHandle,
			(void*)&eci);
	if (error != SA_AIS_OK) {
		goto alloc_done;
	}

	error = saHandleInstanceGet(&evt_instance_handle_db, 
			eci->eci_instance_handle, (void*)&evti);
	if (error != SA_AIS_OK) {
		goto alloc_put1;
	}

	error = saHandleCreate(&event_handle_db, sizeof(*edi), 
			eventHandle);
	if (error != SA_AIS_OK) {
		goto alloc_put2;
	}
	error = saHandleInstanceGet(&event_handle_db, *eventHandle,
					(void*)&edi);
	if (error != SA_AIS_OK) {
		saHandleDestroy(&event_handle_db, *eventHandle);
		goto alloc_put2;
	}

	pthread_mutex_init(&edi->edi_mutex, NULL);
	edi->edi_ro = 0;
	edi->edi_freeing = 0;
	edi->edi_channel_handle = channelHandle;
	edi->edi_pub_node = evti->ei_node_id;
	edi->edi_priority = SA_EVT_LOWEST_PRIORITY;
	edi->edi_event_id = SA_EVT_EVENTID_NONE;
	edi->edi_pub_time = SA_TIME_UNKNOWN;
	edi->edi_retention_time = 0;
	hl = malloc(sizeof(*hl));
	if (!hl) {
		saHandleDestroy(&event_handle_db, *eventHandle);
		error = SA_AIS_ERR_NO_MEMORY;
		goto alloc_put2;
	}
	edi->edi_hl = hl;
	hl->hl_handle = *eventHandle;
	list_init(&hl->hl_entry);
	list_add(&hl->hl_entry, &eci->eci_event_list);


	saHandleInstancePut (&event_handle_db, *eventHandle);

alloc_put2:
	saHandleInstancePut (&evt_instance_handle_db, eci->eci_instance_handle);
alloc_put1:
	saHandleInstancePut (&channel_handle_db, channelHandle);
alloc_done:
	return error;
}

/*
 * The saEvtEventFree() function gives the Event Service premission to 
 * deallocate the memory that contains the attributes of the event that is 
 * associated with eventHandle. The function is used to free events allocated 
 * by saEvtEventAllocate() and by saEvtEventDeliverCallback().
 */
SaAisErrorT 
saEvtEventFree(SaEvtEventHandleT eventHandle)
{
	SaAisErrorT error;
	struct event_data_instance *edi;

	error = saHandleInstanceGet(&event_handle_db, eventHandle,
			(void*)&edi);
	if (error != SA_AIS_OK) {
		goto evt_free_done;
	}

	/*
	 * Make sure that the event isn't being freed elsewhere
	 */
	pthread_mutex_lock(&edi->edi_mutex);
	if (edi->edi_freeing) {
		pthread_mutex_unlock(&edi->edi_mutex);
		saHandleInstancePut(&event_handle_db, eventHandle);
		return SA_AIS_ERR_BAD_HANDLE;
	}
	edi->edi_freeing = 1;

	pthread_mutex_unlock(&edi->edi_mutex);
	saHandleDestroy(&event_handle_db, eventHandle);
	saHandleInstancePut(&event_handle_db, eventHandle);

evt_free_done:
	return error;
}

/*
 * This function may be used to assign writeable event attributes. It takes 
 * as arguments an event handle eventHandle and the attribute to be set in 
 * the event structure of the event with that handle. Note: The only 
 * attributes that a process can set are the patternArray, priority, 
 * retentionTime and publisherName attributes.
 */
SaAisErrorT 
saEvtEventAttributesSet(
	const SaEvtEventHandleT eventHandle, 
	const SaEvtEventPatternArrayT *patternArray, 
	SaEvtEventPriorityT priority, 
	SaTimeT retentionTime, 
	const SaNameT *publisherName)
{
	SaEvtEventPatternT *oldpatterns;
	SaSizeT		    oldnumber;
	SaAisErrorT error;
	struct event_data_instance *edi;
	int i;

	if (priority > SA_EVT_LOWEST_PRIORITY) {
		return SA_AIS_ERR_INVALID_PARAM;
	}

	error = saHandleInstanceGet(&event_handle_db, eventHandle,
			(void*)&edi);
	if (error != SA_AIS_OK) {
		goto attr_set_done;
	}
	pthread_mutex_lock(&edi->edi_mutex);

	/*
	 * Cannot modify an event returned via callback.
	 */
	if (edi->edi_ro) {
		error = SA_AIS_ERR_ACCESS;
		goto attr_set_unlock;
	}

	edi->edi_priority = priority;
	edi->edi_retention_time = retentionTime;

	if (publisherName) {
		edi->edi_pub_name = *publisherName;
	}

	if (!patternArray) {
		goto attr_set_unlock;
	}

	oldpatterns = edi->edi_patterns.patterns;
	oldnumber = edi->edi_patterns.patternsNumber;
	edi->edi_patterns.patterns = 0;
	edi->edi_patterns.patterns = malloc(sizeof(SaEvtEventPatternT) * 
					patternArray->patternsNumber);
	if (!edi->edi_patterns.patterns) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto attr_set_done_reset;
	}
	edi->edi_patterns.patternsNumber = patternArray->patternsNumber;
	edi->edi_patterns.allocatedNumber = patternArray->patternsNumber;

	/*
	 * copy the patterns from the caller. allocating memory for
	 * of all the strings.
	 */
	for (i = 0; i < patternArray->patternsNumber; i++) {
		edi->edi_patterns.patterns[i].pattern = 
			malloc(patternArray->patterns[i].patternSize);
		if (!edi->edi_patterns.patterns[i].pattern) {
			int j;
			for (j = 0; j < i; j++) {
				free(edi->edi_patterns.patterns[j].pattern);
			}
			free(edi->edi_patterns.patterns);
			error = SA_AIS_ERR_NO_MEMORY;
			goto attr_set_done_reset;
		}
		memcpy(edi->edi_patterns.patterns[i].pattern,
			patternArray->patterns[i].pattern,
				patternArray->patterns[i].patternSize);
		edi->edi_patterns.patterns[i].patternSize = 
			patternArray->patterns[i].patternSize;
		edi->edi_patterns.patterns[i].allocatedSize = 
			patternArray->patterns[i].patternSize;
	}

	/*
	 * free up the old pattern memory 
	 */
	if (oldpatterns) {
		for (i = 0; i < oldnumber; i++) {
			if (oldpatterns[i].pattern) {
				free(oldpatterns[i].pattern);
			}
		}
		free (oldpatterns);
	}
	goto attr_set_unlock;

attr_set_done_reset:
	edi->edi_patterns.patterns = oldpatterns;
	edi->edi_patterns.patternsNumber = oldnumber;
attr_set_unlock:
	pthread_mutex_unlock(&edi->edi_mutex);
	saHandleInstancePut(&event_handle_db, eventHandle);
attr_set_done:
	return error;
}

/* 
 * This function takes as parameters an event handle eventHandle and the 
 * attributes of the event with that handle. The function retrieves the 
 * value of the attributes for the event and stores them at the address 
 * provided for the out parameters. 
 * It is the responsibility of the invoking process to allocate memory for 
 * the out parameters before it invokes this function. The Event Service 
 * assigns the out values into that memory. For each of the out, or in/out, 
 * parameters, if the invoking process provides a NULL reference, the 
 * Availability Management Framework does not return the out value. 
 * Similarly, it is the responsibility of the invoking process to allocate 
 * memory for the patternArray.
 */
SaAisErrorT 
saEvtEventAttributesGet(
	SaEvtEventHandleT eventHandle, 
	SaEvtEventPatternArrayT *patternArray, 
	SaEvtEventPriorityT *priority, 
	SaTimeT *retentionTime, 
	SaNameT *publisherName, 
	SaTimeT *publishTime, 
	SaEvtEventIdT *eventId)
{
	SaAisErrorT error;
	struct event_data_instance *edi;
	SaSizeT npats;
	int i;

	error = saHandleInstanceGet(&event_handle_db, eventHandle,
			(void*)&edi);
	if (error != SA_AIS_OK) {
		goto attr_get_done;
	}
	pthread_mutex_lock(&edi->edi_mutex);

	/*
	 * Go through the args and send back information if the pointer
	 * isn't NULL
	 */
	if (eventId) {
		*eventId = edi->edi_event_id;
	}

	if (publishTime) {
		*publishTime = edi->edi_pub_time;
	}

	if (publisherName) {
		*publisherName = edi->edi_pub_name;
	}

	if (retentionTime) {
		*retentionTime = edi->edi_retention_time;
	}

	if (priority) {
		*priority = edi->edi_priority;
	}

	if (!patternArray) {
		goto attr_get_unlock;
	}

	/*
	 * The spec says that if the called passes in a NULL patterns array,
	 * then we allocate the required data and the caller is responsible
	 * for dealocating later. Otherwise, we copy to pre-allocated space.
	 * If there are more patterns than allcated space, we set the return
	 * code to SA_AIS_ERR_NO_SPACE and copy as much as will fit. We will
	 * return the total number of patterns available in the patternsNumber
	 * regardless of how much was allocated.
	 *
	 */
	if (patternArray->patterns == NULL) {
		npats = edi->edi_patterns.patternsNumber;
		patternArray->allocatedNumber = edi->edi_patterns.patternsNumber;
		patternArray->patternsNumber = edi->edi_patterns.patternsNumber;
		patternArray->patterns = malloc(sizeof(*patternArray->patterns) * 
				edi->edi_patterns.patternsNumber);
		if (!patternArray->patterns) {
			error = SA_AIS_ERR_LIBRARY;
			goto attr_get_unlock;
		}
		for (i = 0; i < edi->edi_patterns.patternsNumber; i++) {
			patternArray->patterns[i].allocatedSize = 
				edi->edi_patterns.patterns[i].allocatedSize;
			patternArray->patterns[i].patternSize = 
				edi->edi_patterns.patterns[i].patternSize;
			patternArray->patterns[i].pattern = 
				malloc(edi->edi_patterns.patterns[i].patternSize);
			if (!patternArray->patterns[i].pattern) {
				int j;
				/*
				 * back out previous mallocs
				 */
				for (j = 0; j < i; j++) {
					free(patternArray->patterns[j].pattern);
				}
				free(patternArray->patterns);

				/*
				 * saEvtEventAttributesGet doesn't return
				 * SA_AIS_ERR_NO_MEMORY
				 */
				error = SA_AIS_ERR_LIBRARY;
				goto attr_get_unlock;
			}
		}
	} else {
		if (patternArray->allocatedNumber < edi->edi_patterns.allocatedNumber) {
			error = SA_AIS_ERR_NO_SPACE;
			npats = patternArray->allocatedNumber;
		} else {
			npats = edi->edi_patterns.patternsNumber;
		}
	}
	patternArray->patternsNumber = edi->edi_patterns.patternsNumber;

	/*
	 * copy the patterns to the callers structure.  If we have pre-allocated
	 * data, the patterns may not fit in the supplied space. In that case we
	 * return NO_SPACE.
	 */
	for (i = 0; i < npats; i++) {

		memcpy(patternArray->patterns[i].pattern,
			edi->edi_patterns.patterns[i].pattern,
			min(patternArray->patterns[i].allocatedSize,
				edi->edi_patterns.patterns[i].patternSize));

		if (patternArray->patterns[i].allocatedSize < 
								edi->edi_patterns.patterns[i].patternSize) {
			error = SA_AIS_ERR_NO_SPACE;
		}

		patternArray->patterns[i].patternSize = 
			edi->edi_patterns.patterns[i].patternSize;
	}

attr_get_unlock:
	pthread_mutex_unlock(&edi->edi_mutex);
	saHandleInstancePut(&event_handle_db, eventHandle);
attr_get_done:
	return error;
}

/*
 * The saEvtEventDataGet() function allows a process to retrieve the data 
 * associated with an event previously delivered by 
 * saEvtEventDeliverCallback().
 */
SaAisErrorT 
saEvtEventDataGet(
	const SaEvtEventHandleT eventHandle, 
	void *eventData, 
	SaSizeT *eventDataSize)
{
	SaAisErrorT error = SA_AIS_ERR_INVALID_PARAM;
	struct event_data_instance *edi;
	SaSizeT xfsize;

	if (!eventDataSize) {
		goto data_get_done;
	}

	error = saHandleInstanceGet(&event_handle_db, eventHandle,
			(void*)&edi);
	if (error != SA_AIS_OK) {
		goto data_get_done;
	}
	pthread_mutex_lock(&edi->edi_mutex);

	/*
	 * If no buffer was supplied, then just tell the caller
	 * how large a buffer is needed.  
	 */
	if (!eventData) {
		error = SA_AIS_ERR_NO_SPACE;
		*eventDataSize = edi->edi_event_data_size;
		goto unlock_put;
	}

	/*
	 * Can't get data from an event that wasn't 
	 * a delivered event.
	 */
	if (!edi->edi_ro) {
		error = SA_AIS_ERR_BAD_HANDLE;
		goto unlock_put;
	}

	if (edi->edi_event_data && edi->edi_event_data_size) {
		xfsize = min(*eventDataSize, edi->edi_event_data_size);
		if (*eventDataSize < edi->edi_event_data_size) {
			error = SA_AIS_ERR_NO_SPACE;
		}
		*eventDataSize = edi->edi_event_data_size;
		memcpy(eventData, edi->edi_event_data, xfsize);
	} else {
		*eventDataSize = 0;
	}

unlock_put:
	pthread_mutex_unlock(&edi->edi_mutex);
	saHandleInstancePut(&event_handle_db, eventHandle);
data_get_done:
	return error;
}

/*
 * Calculate the size in bytes for patterns
 */
static size_t patt_size(const SaEvtEventPatternArrayT *patterns)
{
	int i;
	size_t size = sizeof(mar_evt_event_pattern_array_t);
	for (i = 0; i < patterns->patternsNumber; i++) {
		size += sizeof(mar_evt_event_pattern_t);
		size += patterns->patterns[i].patternSize;
	}
	return size;
}

/*
 * copy patterns to a form for sending to the server
 */
static uint32_t aispatt_to_evt_patt(
	const SaEvtEventPatternArrayT *patterns, 
		void *data)
{
	int i;
	mar_evt_event_pattern_t *pats = data;
	SaUint8T *str  = (SaUint8T *)pats + 
				(patterns->patternsNumber * sizeof(*pats));

	/*
	 * Pointers are replaced with offsets into the data array.  These
	 * will be later converted back into pointers when received as events.
	 */
	for (i = 0; i < patterns->patternsNumber; i++) {
		memcpy(str, patterns->patterns[i].pattern, 
			 	patterns->patterns[i].patternSize);
		pats->pattern_size = patterns->patterns[i].patternSize;
		pats->pattern = (SaUint8T *)((void *)str - data);
		str += patterns->patterns[i].patternSize;
		pats++;
	}
	return patterns->patternsNumber;
}

/*
 * Calculate the size in bytes for filters
 */
static size_t filt_size(const SaEvtEventFilterArrayT *filters)
{
	int i;
	size_t size = sizeof(mar_evt_event_filter_array_t);

	for (i = 0; i < filters->filtersNumber; i++) {
		size += sizeof(mar_evt_event_filter_t);
		size += filters->filters[i].filter.patternSize;
	}
	return size;
}

/*
 * Convert the Sa filters to a form that can be sent over the network
 * i.e. replace pointers with offsets.  The pointers will be reconstituted
 * by the receiver.
 */
static uint32_t aisfilt_to_evt_filt(
	const SaEvtEventFilterArrayT *filters, 
	void *data)
{
	int i;
	mar_evt_event_filter_array_t *filtd = data;
	mar_evt_event_filter_t *filts = data + sizeof(mar_evt_event_filter_array_t);
	SaUint8T *str = (SaUint8T *)filts +
			(filters->filtersNumber * sizeof(*filts));

	/*
	 * Pointers are replaced with offsets into the data array.  These
	 * will be later converted back into pointers by the evt server.
	 */
	filtd->filters = (mar_evt_event_filter_t *)((void *)filts - data);
	filtd->filters_number = filters->filtersNumber;

	for (i = 0; i < filters->filtersNumber; i++) {
		filts->filter_type = filters->filters[i].filterType;
		filts->filter.pattern_size = 
			filters->filters[i].filter.patternSize;
		memcpy(str,
			 filters->filters[i].filter.pattern, 
			 filters->filters[i].filter.patternSize);
		filts->filter.pattern = (SaUint8T *)((void *)str - data);
		str += filters->filters[i].filter.patternSize;
		filts++;
	}
	return filters->filtersNumber;
}

/*
 * The saEvtEventPublish() function publishes an event on the associated 
 * channel. The event to be published consists of a 
 * standard set of attributes (the event header) and an optional data part. 
 * Before an event can be published, the publisher process must invoke the 
 * saEvtEventPatternArraySet() function to set the event patterns. The event 
 * is delivered to subscribers whose subscription filter matches the event 
 * patterns. 
 * When the Event Service publishes an event, it automatically sets 
 * the following readonly event attributes: 
 * 	- Event attribute time 
 * 	- Event publisher identifier 
 * 	- Event publisher node identifier
 * 	- Event identifier 
 * In addition to the event attributes, a process can supply values for the 
 * eventData and eventDataSize parameters for publication as part of the 
 * event. The data portion of the event may be at most SA_EVT_DATA_MAX_LEN 
 * bytes in length. 
 * The process may assume that the invocation of saEvtEventPublish() copies 
 * all pertinent parameters, including the memory associated with the 
 * eventHandle and eventData parameters, to its own local memory. However, 
 * the invocation does not automatically deallocate memory associated with 
 * the eventHandle and eventData parameters. It is the responsibility of the 
 * invoking process to deallocate the memory for those parameters after 
 * saEvtEventPublish() returns.
 */
SaAisErrorT 
saEvtEventPublish(
	const SaEvtEventHandleT eventHandle, 
	const void *eventData, 
	SaSizeT eventDataSize,
	SaEvtEventIdT *eventId)
{
	SaAisErrorT error;
	struct event_data_instance *edi;
	struct event_instance *evti;
	struct event_channel_instance *eci;
	struct lib_event_data *req;
	struct res_evt_event_publish res;
	size_t pattern_size;
	struct event_pattern *patterns;
	void   *data_start;
	struct iovec iov;

	if (!eventId) {
		return SA_AIS_ERR_INVALID_PARAM;
	}

	if (eventDataSize > SA_EVT_DATA_MAX_LEN) {
		error = SA_AIS_ERR_TOO_BIG;
		goto pub_done;
	}

	error = saHandleInstanceGet(&event_handle_db, eventHandle,
			(void*)&edi);
	if (error != SA_AIS_OK) {
		goto pub_done;
	}
	pthread_mutex_lock(&edi->edi_mutex);

	error = saHandleInstanceGet(&channel_handle_db, edi->edi_channel_handle,
			(void*)&eci);
	if (error != SA_AIS_OK) {
		goto pub_put1;
	}

	/*
	 * See if we can publish to this channel
	 */
	if (!(eci->eci_open_flags & SA_EVT_CHANNEL_PUBLISHER)) {
		error = SA_AIS_ERR_ACCESS;
		goto pub_put2;
	}

	error = saHandleInstanceGet(&evt_instance_handle_db, 
			eci->eci_instance_handle, (void*)&evti);
	if (error != SA_AIS_OK) {
		goto pub_put2;
	}

	/*
	 * Figure out how much memory we need for the patterns and data
	 */
	pattern_size = patt_size(&edi->edi_patterns);

	req = malloc(sizeof(*req) + eventDataSize + pattern_size);

	if (!req) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto pub_put3;
	}

	patterns = (struct event_pattern *)req->led_body;
	data_start = (void *)req->led_body + pattern_size;

	/*
	 * copy everything to the request structure
	 */
	aispatt_to_evt_patt(&edi->edi_patterns, patterns);

	req->led_patterns_number = edi->edi_patterns.patternsNumber;

	req->led_user_data_offset = pattern_size;
	if (eventData && eventDataSize) {
		memcpy(data_start, eventData, eventDataSize);
		req->led_user_data_size = eventDataSize;
	} else {
		req->led_user_data_size = 0;
	}

	req->led_head.id = MESSAGE_REQ_EVT_PUBLISH;
	req->led_head.size = sizeof(*req) + pattern_size + eventDataSize;
	req->led_svr_channel_handle = eci->eci_svr_channel_handle;
	req->led_retention_time = edi->edi_retention_time;
	req->led_publish_time = clustTimeNow();
	req->led_priority = edi->edi_priority;
	marshall_to_mar_name_t (&req->led_publisher_name, &edi->edi_pub_name);

	iov.iov_base = (char *)req;
	iov.iov_len = req->led_head.size;

	pthread_mutex_lock(&evti->ei_response_mutex);

	error = saSendMsgReceiveReply(evti->ei_response_fd, &iov, 1, &res,
		sizeof(res));

	pthread_mutex_unlock (&evti->ei_response_mutex);
	free(req);
	if (error != SA_AIS_OK) {
		pthread_mutex_unlock (&evti->ei_response_mutex);
		goto pub_put3;
	}

	error = res.iep_head.error;
	if (error == SA_AIS_OK) {
		*eventId = res.iep_event_id;
	}

pub_put3:
	saHandleInstancePut (&evt_instance_handle_db, eci->eci_instance_handle);
pub_put2:
	saHandleInstancePut (&channel_handle_db, edi->edi_channel_handle);
pub_put1:
	pthread_mutex_unlock(&edi->edi_mutex);
	saHandleInstancePut(&event_handle_db, eventHandle);
pub_done:
	return error;
}

/*
 * The saEvtEventSubscribe() function enables a process to subscribe for 
 * events on an event channel by registering one or more filters on that 
 * event channel. The process must have opened the event channel, designated 
 * by channelHandle, with the SA_EVT_CHANNEL_SUBSCRIBER flag set for an 
 * invocation of this function to be successful. 
 * The memory associated with the filters is not deallocated by the 
 * saEvtEventSubscribe() function. It is the responsibility of the invoking 
 * process to deallocate the memory when the saEvtEventSubscribe() function 
 * returns. 
 * For a given subscription, the filters parameter cannot be modified. To 
 * change the filters parameter without losing events, a process must 
 * establish a new subscription with the new filters parameter. After the new 
 * subscription is established, the old subscription can be removed by 
 * invoking the saEvtEventUnsubscribe() function.
 */
SaAisErrorT 
saEvtEventSubscribe(
	const SaEvtChannelHandleT channelHandle, 
	const SaEvtEventFilterArrayT *filters, 
	SaEvtSubscriptionIdT subscriptionId)
{
	SaAisErrorT error;
	struct event_instance *evti;
	struct event_channel_instance *eci;
	struct req_evt_event_subscribe *req;
	struct res_evt_event_subscribe res;
	int	sz;
	struct iovec iov;

	if (!filters) {
		return SA_AIS_ERR_INVALID_PARAM;
	}
	error = saHandleInstanceGet(&channel_handle_db, channelHandle,
			(void*)&eci);
	if (error != SA_AIS_OK) {
		goto subscribe_done;
	}

	/*
	 * get the evt handle for the fd
	 */
	error = saHandleInstanceGet(&evt_instance_handle_db, 
			eci->eci_instance_handle, (void*)&evti);
	if (error != SA_AIS_OK) {
		goto subscribe_put1;
	}

	/*
	 * Make sure that a deliver callback has been 
	 * registered before allowing the subscribe to continue.
	 */
	if (!evti->ei_callback.saEvtEventDeliverCallback) {
		error = SA_AIS_ERR_INIT;
		goto subscribe_put2;
	}

	/*
	 * See if we can subscribe to this channel
	 */
	if (!(eci->eci_open_flags & SA_EVT_CHANNEL_SUBSCRIBER)) {
		error = SA_AIS_ERR_ACCESS;
		goto subscribe_put2;
	}

	/*
	 * calculate size needed to store the filters
	 */
	sz = filt_size(filters);

	req = malloc(sizeof(*req) + sz);
	
	if (!req) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto subscribe_put2;
	}

	/*
	 * Copy the supplied filters to the request
	 */
	req->ics_filter_count = aisfilt_to_evt_filt(filters, 
		req->ics_filter_data);
	req->ics_head.id = MESSAGE_REQ_EVT_SUBSCRIBE;
	req->ics_head.size = sizeof(*req) + sz;
	req->ics_channel_handle = eci->eci_svr_channel_handle;
	req->ics_sub_id = subscriptionId;
	req->ics_filter_size = sz;
	iov.iov_base = (char *)req;
	iov.iov_len = req->ics_head.size;

	pthread_mutex_lock(&evti->ei_response_mutex);
	error = saSendMsgReceiveReply(evti->ei_response_fd, &iov, 1,
		&res, sizeof(res));
	pthread_mutex_unlock (&evti->ei_response_mutex);
	free(req);

	if (res.ics_head.id != MESSAGE_RES_EVT_SUBSCRIBE) {
		goto subscribe_put2;
	}

	error = res.ics_head.error;

subscribe_put2:
	saHandleInstancePut(&evt_instance_handle_db, 
					eci->eci_instance_handle);
subscribe_put1:
	saHandleInstancePut(&channel_handle_db, channelHandle);
subscribe_done:
	return error;
}

/*
 * The saEvtEventUnsubscribe() function allows a process to stop receiving 
 * events for a particular subscription on an event channel by removing the 
 * subscription. The saEvtEventUnsubscribe() operation is successful if the 
 * subscriptionId parameter matches a previously registered subscription. 
 * Pending events that no longer match any subscription, because the 
 * saEvtEventUnsubscribe() operation had been invoked, are purged. a process 
 * that wishes to modify a subscription without losing any events must 
 * establish the new subscription before removing the existing subscription.
 */
SaAisErrorT 
saEvtEventUnsubscribe(
	const SaEvtChannelHandleT channelHandle, 
	SaEvtSubscriptionIdT subscriptionId)
{
	SaAisErrorT error;
	struct event_instance *evti;
	struct event_channel_instance *eci;
	struct req_evt_event_unsubscribe req;
	struct res_evt_event_unsubscribe res;
	struct iovec iov;

	error = saHandleInstanceGet(&channel_handle_db, channelHandle,
			(void*)&eci);
	if (error != SA_AIS_OK) {
		goto unsubscribe_done;
	}

	error = saHandleInstanceGet(&evt_instance_handle_db, 
			eci->eci_instance_handle, (void*)&evti);
	if (error != SA_AIS_OK) {
		goto unsubscribe_put1;
	}

	req.icu_head.id = MESSAGE_REQ_EVT_UNSUBSCRIBE;
	req.icu_head.size = sizeof(req);

	req.icu_channel_handle = eci->eci_svr_channel_handle;
	req.icu_sub_id = subscriptionId;
	iov.iov_base = (char *)&req;
	iov.iov_len = sizeof(req);

	pthread_mutex_lock(&evti->ei_response_mutex);
 	error = saSendMsgReceiveReply(evti->ei_response_fd, &iov, 1,
 		&res, sizeof(res));
 	pthread_mutex_unlock (&evti->ei_response_mutex);

	if (error != SA_AIS_OK) {
		goto unsubscribe_put2;
	}

	if (res.icu_head.id != MESSAGE_RES_EVT_UNSUBSCRIBE) {
		error = SA_AIS_ERR_LIBRARY;
		goto unsubscribe_put2;
	}

	error = res.icu_head.error;

unsubscribe_put2:
	saHandleInstancePut(&evt_instance_handle_db, 
					eci->eci_instance_handle);
unsubscribe_put1:
	saHandleInstancePut(&channel_handle_db, channelHandle);
unsubscribe_done:
	return error;
}


/*
 * The saEvtEventRetentionTimeClear() function is used to clear the retention 
 * time of a published event. It indicates to the Event Service that it does 
 * not need to keep the event any longer for potential new subscribers. Once 
 * the value of the retention time is reset to 0, the event is no longer 
 * available for new subscribers. The event is held until all old subscribers 
 * in the system process the event and free the event using saEvtEventFree().
 */
SaAisErrorT 
saEvtEventRetentionTimeClear(
	const SaEvtChannelHandleT channelHandle, 
	const SaEvtEventIdT eventId)
{
	SaAisErrorT error;
	struct event_instance *evti;
	struct event_channel_instance *eci;
	struct req_evt_event_clear_retentiontime req;
	struct res_evt_event_clear_retentiontime res;
	struct iovec iov;

	if (!is_valid_event_id(eventId)) {
		return SA_AIS_ERR_INVALID_PARAM;
	}

	error = saHandleInstanceGet(&channel_handle_db, channelHandle,
			(void*)&eci);
	if (error != SA_AIS_OK) {
		goto ret_time_done;
	}

	error = saHandleInstanceGet(&evt_instance_handle_db, 
			eci->eci_instance_handle, (void*)&evti);
	if (error != SA_AIS_OK) {
		goto ret_time_put1;
	}

	req.iec_head.id = MESSAGE_REQ_EVT_CLEAR_RETENTIONTIME;
	req.iec_head.size = sizeof(req);

	req.iec_channel_handle = eci->eci_svr_channel_handle;
	req.iec_event_id = eventId;

	iov.iov_base = (char *)&req;
	iov.iov_len = sizeof(req);

	pthread_mutex_lock(&evti->ei_response_mutex);
	error = saSendMsgReceiveReply(evti->ei_response_fd, &iov, 1,
		&res, sizeof(res));
	pthread_mutex_unlock (&evti->ei_response_mutex);

	if (error != SA_AIS_OK) {
		goto ret_time_put2;
	}
	if (res.iec_head.id != MESSAGE_RES_EVT_CLEAR_RETENTIONTIME) {
		error = SA_AIS_ERR_LIBRARY;
		goto ret_time_put2;
	}

	error = res.iec_head.error;

ret_time_put2:
	saHandleInstancePut(&evt_instance_handle_db, 
					eci->eci_instance_handle);
ret_time_put1:
	saHandleInstancePut(&channel_handle_db, channelHandle);
ret_time_done:
	return error;
}

/*
 *	vi: set autoindent tabstop=4 shiftwidth=4 :
 */
