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
#include "../exec/totempg.h"

static void evtHandleInstanceDestructor(void *instance);
static void chanHandleInstanceDestructor(void *instance);
static void eventHandleInstanceDestructor(void *instance);

/*
 * Versions of the SAF AIS specification supported by this library
 */
static SaVersionT supported_versions[] = {
	{'B', 0x01, 0x01},
	{'b', 0x01, 0x01}
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

struct message_overlay {
	struct res_header header;
	char data[0];
};

/*
 * data required to support events for a given initialization
 *
 * ei_fd:			fd received from the evtInitialize call.
 * ei_callback:		callback function.
 * ei_version:		version sent to the evtInitialize call.
 * ei_node_id:		our node id.
 * ei_node_name:	our node name.
 * ei_finalize:		instance in finalize flag
 * ei_queue:		queue for async messages while doing sync commands
 * ei_mutex:		instance mutex
 *
 */
struct event_instance {
	int 					ei_fd;
	SaEvtCallbacksT			ei_callback;
	SaVersionT				ei_version;
	SaClmNodeIdT			ei_node_id;
	SaNameT					ei_node_name;
	int						ei_finalize;
	struct queue			ei_inq;
	char 					ei_message[MESSAGE_SIZE_MAX];
	pthread_mutex_t			ei_mutex;
};


/*
 * Data associated with an opened channel
 *
 * eci_channel_name:		name of channel
 * eci_open_flags:			channel open flags
 * eci_svr_channel_handle:	channel handle returned from server
 * eci_closing:				channel in process of being closed
 * eci_mutex:				channel mutex
 *
 */
struct event_channel_instance {

	SaNameT					eci_channel_name;
	SaEvtChannelOpenFlagsT	eci_open_flags;
	uint32_t				eci_svr_channel_handle;
	SaEvtHandleT			eci_instance_handle;
	int						eci_closing;
	pthread_mutex_t			eci_mutex;
	 
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
 *
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
};


#define min(a,b) ((a) < (b) ? (a) : (b))

/*
 * Clean up function for an evt instance (saEvtInitialize) handle
 */
static void evtHandleInstanceDestructor(void *instance)
{
	struct event_instance *evti = instance;
	void **msg;
	int empty;

	/*
	 * Empty out the queue if there are any pending messages
	 */
	while ((saQueueIsEmpty(&evti->ei_inq, &empty) == SA_AIS_OK) && !empty) {
		saQueueItemGet(&evti->ei_inq, (void *)&msg);
		saQueueItemRemove(&evti->ei_inq);
		free(*msg);
	}

	/*
	 * clean up the queue itself
	 */
	if (evti->ei_inq.items) {
			free(evti->ei_inq.items);
	}

	/*
	 * Disconnect from the server
	 */
	if (evti->ei_fd != -1) {
		shutdown(evti->ei_fd, 0);
		close(evti->ei_fd);
	}
}

/*
 * Clean up function for an open channel handle
 */
static void chanHandleInstanceDestructor(void *instance)
{
}

/*
 * Clean up function for an event handle
 */
static void eventHandleInstanceDestructor(void *instance)
{
	struct event_data_instance *edi = instance;
	int i;
	for (i = 0; i < edi->edi_patterns.patternsNumber; i++) {
		free(edi->edi_patterns.patterns[i].pattern);
	}
	if (edi->edi_patterns.patterns) {
		free(edi->edi_patterns.patterns);
	}
	if (edi->edi_event_data) {
		free(edi->edi_event_data);
	}
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
			(void*)evtHandle);
	if (error != SA_AIS_OK) {
		goto error_nofree;
	}
	error = saHandleInstanceGet(&evt_instance_handle_db, *evtHandle,
			(void*)&evti);
	if (error != SA_AIS_OK) {
		goto error_handle_free;
	}
	memset(evti, 0, sizeof(*evti));

	/*
	 * Save the version so we can check with the event server
	 * and see if it supports this version.
	 */
	evti->ei_version = *version;

	/*
	 * An inq is needed to store async messages while waiting for a 
	 * sync response
	 */
	error = saQueueInit(&evti->ei_inq, 1024, sizeof(void *));
	if (error != SA_AIS_OK) {
		goto error_handle_put;
	}

	/*
	 * Set up communication with the event server
	 */
	error = saServiceConnect(&evti->ei_fd, MESSAGE_REQ_EVT_INIT);
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

	pthread_mutex_init(&evti->ei_mutex, NULL);
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

	error = saHandleInstanceGet(&evt_instance_handle_db, evtHandle, 
			(void *)&evti);

	if (error != SA_AIS_OK) {
		return error;
	}

	*selectionObject = evti->ei_fd;

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
	SaEvtEventPatternT *pat;
	SaUint8T *str;
	SaAisErrorT error;
	int i;

	error = saHandleCreate(&event_handle_db, sizeof(*edi), 
		(void*)event_handle);
	if (error != SA_AIS_OK) {
			goto make_evt_done;
	}

	error = saHandleInstanceGet(&event_handle_db, *event_handle,
				(void*)&edi);
	if (error != SA_AIS_OK) {
			goto make_evt_done;
	}

	memset(edi, 0, sizeof(*edi));

	pthread_mutex_init(&edi->edi_mutex, NULL);
	edi->edi_freeing = 0;
	edi->edi_channel_handle = evt->led_lib_channel_handle;
	edi->edi_priority = evt->led_priority;
	edi->edi_retention_time = evt->led_retention_time;
	edi->edi_pub_node = evt->led_publisher_node_id;
	edi->edi_pub_time = evt->led_publish_time;
	edi->edi_event_data_size = evt->led_user_data_size;
	edi->edi_event_id = evt->led_event_id;
	edi->edi_pub_name = evt->led_publisher_name;
	if (edi->edi_event_data_size) {
		edi->edi_event_data = malloc(edi->edi_event_data_size);
		memcpy(edi->edi_event_data, 
				evt->led_body + evt->led_user_data_offset,
				edi->edi_event_data_size);
	}

	/*
	 * Move the pattern bits into the SaEvtEventPatternArrayT
	 */
	edi->edi_patterns.patternsNumber = evt->led_patterns_number;
	edi->edi_patterns.patterns = malloc(sizeof(SaEvtEventPatternT) * 
					edi->edi_patterns.patternsNumber);
	pat = (SaEvtEventPatternT *)evt->led_body;
	str = evt->led_body + sizeof(SaEvtEventPatternT) * 
						edi->edi_patterns.patternsNumber;
	for (i = 0; i < evt->led_patterns_number; i++) {
		edi->edi_patterns.patterns[i].patternSize = pat->patternSize;
		edi->edi_patterns.patterns[i].pattern = malloc(pat->patternSize);
		if (!edi->edi_patterns.patterns[i].pattern) {
			printf("make_event: couldn't alloc %d bytes\n", pat->patternSize);
			error =  SA_AIS_ERR_NO_MEMORY;
			break;
		}
		memcpy(edi->edi_patterns.patterns[i].pattern,
				str, pat->patternSize);
		str += pat->patternSize;
		pat++; 
	}
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
	struct res_header **queue_msg;
	struct res_header *msg = 0;
	int empty;
	int ignore_dispatch = 0;
	int cont = 1; /* always continue do loop except when set to 0 */
	int poll_fd;
	struct message_overlay *dispatch_data;
	struct lib_event_data *evt;
	struct res_evt_event_data res;

	error = saHandleInstanceGet(&evt_instance_handle_db, evtHandle,
		(void *)&evti);
	if (error != SA_AIS_OK) {
		return error;
	}

	/*
	 * Timeout instantly for SA_DISPATCH_ALL
	 */
	if (dispatchFlags == SA_DISPATCH_ALL) {
		timeout = 0;
	}

	do {
		poll_fd = evti->ei_fd;

		ufds.fd = poll_fd;
		ufds.events = POLLIN;
		ufds.revents = 0;

		pthread_mutex_lock(&evti->ei_mutex);
		saQueueIsEmpty(&evti->ei_inq, &empty);
		/*
		 * Read from the socket if there is nothing in
		 * our queue.
		 */
		if (empty == 1) {
			pthread_mutex_unlock(&evti->ei_mutex);
			error = saPollRetry(&ufds, 1, timeout);
			if (error != SA_AIS_OK) {
				goto error_nounlock;
			}
			pthread_mutex_lock(&evti->ei_mutex);
		}

		/*
		 * Handle has been finalized in another thread
		 */
		if (evti->ei_finalize == 1) {
			error = SA_AIS_OK;
			pthread_mutex_unlock(&evti->ei_mutex);
			goto error_unlock;
		}

		dispatch_avail = (ufds.revents & POLLIN) | (empty == 0);
		if (dispatch_avail == 0 && dispatchFlags == SA_DISPATCH_ALL) {
			pthread_mutex_unlock(&evti->ei_mutex);
			break; /* exit do while cont is 1 loop */
		} else
		if (dispatch_avail == 0) {
			pthread_mutex_unlock(&evti->ei_mutex);
			continue; /* next poll */
		}

		saQueueIsEmpty(&evti->ei_inq, &empty);
		if (empty == 0) {
			/*
			 * Queue is not empty, read data from queue
			 */
			saQueueItemGet(&evti->ei_inq, (void *)&queue_msg);
			msg = *queue_msg;
			dispatch_data = (struct message_overlay *)msg;
			saQueueItemRemove(&evti->ei_inq);
		} else {
			/*
			 * Queue empty, read response from socket
			 */
			dispatch_data = (struct message_overlay *)&evti->ei_message;
			error = saRecvRetry(evti->ei_fd, &dispatch_data->header,
				sizeof(struct res_header), MSG_WAITALL | MSG_NOSIGNAL);
			if (error != SA_AIS_OK) {
				pthread_mutex_unlock(&evti->ei_mutex);
				goto error_unlock;
			}
			if (dispatch_data->header.size > sizeof(struct res_header)) {
				error = saRecvRetry(evti->ei_fd, dispatch_data->data,
					dispatch_data->header.size - sizeof(struct res_header),
					MSG_WAITALL | MSG_NOSIGNAL);
				if (error != SA_AIS_OK) {
					pthread_mutex_unlock(&evti->ei_mutex);
					goto error_unlock;
				}
			}
		}
		/*
		 * Make copy of callbacks, message data, unlock instance, 
		 * and call callback. A risk of this dispatch method is that 
		 * the callback routines may operate at the same time that 
		 * EvtFinalize has been called in another thread.
		 */
		memcpy(&callbacks, &evti->ei_callback, sizeof(evti->ei_callback));
		pthread_mutex_unlock(&evti->ei_mutex);


		/*
		 * Dispatch incoming response
		 */
		switch (dispatch_data->header.id) {
		case MESSAGE_RES_LIB_ACTIVATEPOLL:
			/*
			 * This is a do nothing message which the node 
			 * executive sends to activate the file evtHandle 
			 * in poll when the library has queued a message into 
			 * evti->ei_inq. The dispatch is ignored for the 
			 * following two cases:
			 * 1) setting of timeout to zero for the 
			 *    DISPATCH_ALL case
			 * 2) expiration of the do loop for the 
			 *    DISPATCH_ONE case
			 */
			ignore_dispatch = 1;
			break;

		case MESSAGE_RES_EVT_AVAILABLE:
			/*
			 * There are events available.  Send a request for one and then
			 * dispatch it.
			 */
			evt = (struct lib_event_data *)&evti->ei_message;
			res.evd_head.id = MESSAGE_REQ_EVT_EVENT_DATA;
			res.evd_head.size = sizeof(res);
			error = saSendRetry(evti->ei_fd, &res, sizeof(res), MSG_NOSIGNAL);
			if (error != SA_AIS_OK) {
				printf("MESSAGE_RES_EVT_AVAILABLE: send failed: %d\n", error);
					break;
			}
			error = saRecvQueue(evti->ei_fd, evt, &evti->ei_inq, 
											MESSAGE_RES_EVT_EVENT_DATA);
			if (error != SA_AIS_OK) {
				printf("MESSAGE_RES_EVT_AVAILABLE: receive failed: %d\n", 
						error);
				break;
			}
			/*
			 * No data available.  This is OK.
			 */
			if (evt->led_head.error == SA_AIS_ERR_NOT_EXIST) {
				// printf("MESSAGE_RES_EVT_AVAILABLE: No event data\n");
				error = SA_AIS_OK;
				break;
			}

			if (evt->led_head.error != SA_AIS_OK) {
				error = evt->led_head.error;
				printf("MESSAGE_RES_EVT_AVAILABLE: Error returned: %d\n", 
						error);
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
				(struct res_evt_open_chan_async *)dispatch_data;
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
			printf("Dispatch: Bad message type 0x%x\n",
					dispatch_data->header.id);
			error = SA_AIS_ERR_LIBRARY;	
			goto error_nounlock;
			break;
		}

		/*
		 * If empty is zero it means the we got the 
		 * message from the queue and we are responsible
		 * for freeing it.
		 */
		if (empty == 0) {
			free(msg);
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

error_unlock:
	saHandleInstancePut(&evt_instance_handle_db, evtHandle);
error_nounlock:
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

       pthread_mutex_lock(&evti->ei_mutex);

	/*
	 * Another thread has already started finalizing
	 */
	if (evti->ei_finalize) {
		pthread_mutex_unlock(&evti->ei_mutex);
		saHandleInstancePut(&evt_instance_handle_db, evtHandle);
		return SA_AIS_ERR_BAD_HANDLE;
	}

	evti->ei_finalize = 1;

	saActivatePoll(evti->ei_fd);

	pthread_mutex_unlock(&evti->ei_mutex);

	saHandleDestroy(&evt_instance_handle_db, evtHandle);
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
	SaAisErrorT error;

	error = saHandleInstanceGet(&evt_instance_handle_db, evtHandle,
			(void*)&evti);
	
	if (error != SA_AIS_OK) {
		goto chan_open_done;
	}

	/*
	 * create a handle for this open channel
	 */
	error = saHandleCreate(&channel_handle_db, sizeof(*eci), 
			(void*)channelHandle);
	if (error != SA_AIS_OK) {
		goto chan_open_put;
	}

	error = saHandleInstanceGet(&channel_handle_db, *channelHandle,
					(void*)&eci);
	if (error != SA_AIS_OK) {
		saHandleDestroy(&channel_handle_db, *channelHandle);
		goto chan_open_put;
	}


	/*
	 * Send the request to the server and wait for a response
	 */
	req.ico_head.size = sizeof(req);
	req.ico_head.id = MESSAGE_REQ_EVT_OPEN_CHANNEL;
	req.ico_c_handle = *channelHandle;
	req.ico_timeout = timeout;
	req.ico_open_flag = channelOpenFlags;
	req.ico_channel_name = *channelName;


	pthread_mutex_lock(&evti->ei_mutex);

	error = saSendRetry(evti->ei_fd, &req, sizeof(req), MSG_NOSIGNAL);
	if (error != SA_AIS_OK) {
		pthread_mutex_unlock (&evti->ei_mutex);
		goto chan_open_free;
	}
	error = saRecvQueue(evti->ei_fd, &res, &evti->ei_inq, 
					MESSAGE_RES_EVT_OPEN_CHANNEL);

	pthread_mutex_unlock (&evti->ei_mutex);

	if (error != SA_AIS_OK) {
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

	saActivatePoll(evti->ei_fd);

	pthread_mutex_unlock(&eci->eci_mutex);
	

	/*
	 * Send the request to the server and wait for a response
	 */
	req.icc_head.size = sizeof(req);
	req.icc_head.id = MESSAGE_REQ_EVT_CLOSE_CHANNEL;
	req.icc_channel_handle = eci->eci_svr_channel_handle;

	pthread_mutex_lock(&evti->ei_mutex);

	error = saSendRetry(evti->ei_fd, &req, sizeof(req), MSG_NOSIGNAL);
	if (error != SA_AIS_OK) {
		pthread_mutex_unlock(&evti->ei_mutex);
		eci->eci_closing = 0;
		goto chan_close_put2;
	}
	error = saRecvQueue(evti->ei_fd, &res, &evti->ei_inq, 
					MESSAGE_RES_EVT_CLOSE_CHANNEL);
	pthread_mutex_unlock(&evti->ei_mutex);
	if (error != SA_AIS_OK) {
		eci->eci_closing = 0;
		goto chan_close_put2;
	}

	error = res.icc_head.error;

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
	req.ico_channel_name = *channelName;


	pthread_mutex_lock(&evti->ei_mutex);

	error = saSendRetry(evti->ei_fd, &req, sizeof(req), MSG_NOSIGNAL);
	if (error != SA_AIS_OK) {
		pthread_mutex_unlock (&evti->ei_mutex);
		goto chan_open_free;
	}
	error = saRecvQueue(evti->ei_fd, &res, &evti->ei_inq, 
					MESSAGE_RES_EVT_OPEN_CHANNEL);

	pthread_mutex_unlock (&evti->ei_mutex);

	if (error != SA_AIS_OK) {
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
	SaAisErrorT error;

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
	req.iuc_channel_name = *channelName;


	pthread_mutex_lock(&evti->ei_mutex);

	error = saSendRetry(evti->ei_fd, &req, sizeof(req), MSG_NOSIGNAL);
	if (error != SA_AIS_OK) {
		pthread_mutex_unlock (&evti->ei_mutex);
		goto chan_unlink_put;
	}
	error = saRecvQueue(evti->ei_fd, &res, &evti->ei_inq, 
					MESSAGE_RES_EVT_UNLINK_CHANNEL);

	pthread_mutex_unlock (&evti->ei_mutex);

	if (error != SA_AIS_OK) {
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
			(void*)eventHandle);
	if (error != SA_AIS_OK) {
		goto alloc_put2;
	}
	error = saHandleInstanceGet(&event_handle_db, *eventHandle,
					(void*)&edi);
	if (error != SA_AIS_OK) {
		goto alloc_put2;
	}

	memset(edi, 0, sizeof(*edi));

	pthread_mutex_init(&edi->edi_mutex, NULL);
	edi->edi_freeing = 0;
	edi->edi_channel_handle = channelHandle;
	edi->edi_pub_node = evti->ei_node_id;
	edi->edi_priority = SA_EVT_LOWEST_PRIORITY;
	edi->edi_event_id = SA_EVT_EVENTID_NONE;
	saHandleInstancePut (&event_handle_db, *eventHandle);

alloc_put2:
	saHandleInstancePut (&evt_instance_handle_db, eci->eci_instance_handle);
alloc_put1:
	saHandleInstancePut (&channel_handle_db, edi->edi_channel_handle);
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

	error = saHandleInstanceGet(&event_handle_db, eventHandle,
			(void*)&edi);
	if (error != SA_AIS_OK) {
		goto attr_set_done;
	}
	pthread_mutex_lock(&edi->edi_mutex);

	edi->edi_priority = priority;
	edi->edi_retention_time = retentionTime;

	/*
	 * publisherName or patternArray not allowed to be NULL
	 */
	if (!publisherName || !patternArray) {
			error = SA_AIS_ERR_INVALID_PARAM;
			goto attr_set_unlock;
	}

	edi->edi_pub_name = *publisherName;

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

	npats = min(patternArray->patternsNumber, 
				edi->edi_patterns.patternsNumber);
	/*
	 * We set the returned number of patterns to the actual
	 * pattern count of the event.  This way the caller can tell
	 * if it got all the possible patterns. If the returned number
	 * is more that the number supplied, then some available patterns
	 * were not returned.
	 *
	 * The same thing happens when copying the pattern strings.
	 */
	patternArray->patternsNumber = edi->edi_patterns.patternsNumber;

	for (i = 0; i < npats; i++) {
		memcpy(patternArray->patterns[i].pattern,
			edi->edi_patterns.patterns[i].pattern,
			min(patternArray->patterns[i].patternSize,
				edi->edi_patterns.patterns[i].patternSize));
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
	SaAisErrorT error = SA_AIS_OK;
	struct event_data_instance *edi;
	SaSizeT xfsize;

	if (!eventData || !eventDataSize) {
		goto data_get_done;
	}

	error = saHandleInstanceGet(&event_handle_db, eventHandle,
			(void*)&edi);
	if (error != SA_AIS_OK) {
		goto data_get_done;
	}
	pthread_mutex_lock(&edi->edi_mutex);

	if (edi->edi_event_data && edi->edi_event_data_size) {
		xfsize = min(*eventDataSize, edi->edi_event_data_size);
		*eventDataSize = edi->edi_event_data_size;
		memcpy(eventData, edi->edi_event_data, xfsize);
	} else {
		*eventDataSize = 0;
	}

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
	size_t size = sizeof(SaEvtEventPatternArrayT);
	for (i = 0; i < patterns->patternsNumber; i++) {
		size += sizeof(SaEvtEventPatternT);
		size += patterns->patterns[i].patternSize;
	}
	return size;
}

/*
 * copy patterns to a form for sending to the server
 */
static uint32_t aispatt_to_evt_patt(const SaEvtEventPatternArrayT *patterns, 
		void *data)
{
	int i;
	SaEvtEventPatternT *pats = data;
	SaUint8T *str  = (SaUint8T *)pats + 
				(patterns->patternsNumber * sizeof(*pats));

	/*
	 * Pointers are replaced with offsets into the data array.  These
	 * will be later converted back into pointers when received as events.
	 */
	for (i = 0; i < patterns->patternsNumber; i++) {
		memcpy(str, patterns->patterns[i].pattern, 
			 	patterns->patterns[i].patternSize);
		pats->patternSize = patterns->patterns[i].patternSize;
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
	size_t size = sizeof(SaEvtEventFilterArrayT);

	for (i = 0; i < filters->filtersNumber; i++) {
		size += sizeof(SaEvtEventFilterT);
		size += filters->filters[i].filter.patternSize;
	}
	return size;
}

/*
 * Convert the Sa filters to a form that can be sent over the network
 * i.e. replace pointers with offsets.  The pointers will be reconstituted
 * by the receiver.
 */
static uint32_t aisfilt_to_evt_filt(const SaEvtEventFilterArrayT *filters, 
		void *data)
{
	int i;
	SaEvtEventFilterArrayT *filta = data;
	SaEvtEventFilterT *filts = data + sizeof(SaEvtEventFilterArrayT);
	SaUint8T *str = (SaUint8T *)filts +
			(filters->filtersNumber * sizeof(*filts));

	/*
	 * Pointers are replaced with offsets into the data array.  These
	 * will be later converted back into pointers by the evt server.
	 */
	filta->filters = (SaEvtEventFilterT *)((void *)filts - data);
	filta->filtersNumber = filters->filtersNumber;

	for (i = 0; i < filters->filtersNumber; i++) {
		filts->filterType = filters->filters[i].filterType;
		filts->filter.patternSize = 
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

	if (eventDataSize > SA_EVT_DATA_MAX_LEN) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto pub_done;
	}

	error = saHandleInstanceGet(&event_handle_db, eventHandle,
			(void*)&edi);
	if (error != SA_AIS_OK) {
		goto pub_done;
	}
	pthread_mutex_lock(&edi->edi_mutex);

	/*
	 * See if patterns have been set for this event.  If not, we
	 * can't publish.
	 */
	if (!edi->edi_patterns.patterns) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto pub_put1;
	}

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

	patterns = (struct event_pattern *)req->led_body;
	data_start = (void *)req->led_body + pattern_size;

	if (!req) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto pub_put3;
	}

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
	req->led_publisher_name = edi->edi_pub_name;

	pthread_mutex_lock(&evti->ei_mutex);
	error = saSendRetry(evti->ei_fd, req, req->led_head.size, MSG_NOSIGNAL);
	free(req);
	if (error != SA_AIS_OK) {
		pthread_mutex_unlock (&evti->ei_mutex);
		goto pub_put3;
	}

	error = saRecvQueue(evti->ei_fd, &res, &evti->ei_inq, 
					MESSAGE_RES_EVT_PUBLISH);
	pthread_mutex_unlock (&evti->ei_mutex);
	if (error != SA_AIS_OK) {
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
	 * registered before  allowing the subscribe to continue.
	 */
	if (!evti->ei_callback.saEvtEventDeliverCallback) {
		error = SA_AIS_ERR_INIT;
		goto subscribe_put2;
	}

	/*
	 * See if we can subscribe to this channel
	 */
	if (!(eci->eci_open_flags & SA_EVT_CHANNEL_SUBSCRIBER)) {
		error = SA_AIS_ERR_INVALID_PARAM;
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

	pthread_mutex_lock(&evti->ei_mutex);
	error = saSendRetry(evti->ei_fd, req, req->ics_head.size, MSG_NOSIGNAL);
	free(req);
	if (error != SA_AIS_OK) {
		pthread_mutex_unlock (&evti->ei_mutex);
		goto subscribe_put2;
	}
	error = saRecvQueue(evti->ei_fd, &res, &evti->ei_inq, 
					MESSAGE_RES_EVT_SUBSCRIBE);

	pthread_mutex_unlock (&evti->ei_mutex);

	if (error != SA_AIS_OK) {
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

	pthread_mutex_lock(&evti->ei_mutex);
	error = saSendRetry(evti->ei_fd, &req, sizeof(req), MSG_NOSIGNAL);
	if (error != SA_AIS_OK) {
		pthread_mutex_unlock (&evti->ei_mutex);
		goto unsubscribe_put2;
	}
	error = saRecvQueue(evti->ei_fd, &res, &evti->ei_inq, 
					MESSAGE_RES_EVT_UNSUBSCRIBE);

	pthread_mutex_unlock (&evti->ei_mutex);

	if (error != SA_AIS_OK) {
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

	pthread_mutex_lock(&evti->ei_mutex);
	error = saSendRetry(evti->ei_fd, &req, sizeof(req), MSG_NOSIGNAL);
	if (error != SA_AIS_OK) {
		pthread_mutex_unlock (&evti->ei_mutex);
		goto ret_time_put2;
	}
	error = saRecvQueue(evti->ei_fd, &res, &evti->ei_inq, 
					MESSAGE_RES_EVT_CLEAR_RETENTIONTIME);

	pthread_mutex_unlock (&evti->ei_mutex);

	if (error != SA_AIS_OK) {
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
