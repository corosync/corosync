
/*
 * Copyright (c) 2002-2005 MontaVista Software, Inc.
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>

#include <saAis.h>
#include <cfg.h>
#include <mar_gen.h>
#include <ipc_gen.h>
#include <ipc_cfg.h>
#include "util.h"

struct res_overlay {
	mar_res_header_t header;
	char data[4096];
};

/*
 * Data structure for instance data
 */
struct cfg_instance {
	int response_fd;
	int dispatch_fd;
	OpenaisCfgCallbacksT callbacks;
	SaNameT compName;
	int compRegistered;
	int finalize;
	pthread_mutex_t response_mutex;
	pthread_mutex_t dispatch_mutex;
};

static void cfg_handleInstanceDestructor (void *);

/*
 * All instances in one database
 */
static struct saHandleDatabase cfg_hdb = {
	.handleCount				= 0,
	.handles					= 0,
	.mutex						= PTHREAD_MUTEX_INITIALIZER,
	.handleInstanceDestructor	= cfg_handleInstanceDestructor
};

/*
 * Implementation
 */
void cfg_handleInstanceDestructor (void *instance)
{
	struct cfg_instance *cfg_instance = instance;

	pthread_mutex_destroy (&cfg_instance->response_mutex);
	pthread_mutex_destroy (&cfg_instance->dispatch_mutex);
}

SaAisErrorT
openais_cfg_initialize (
	openais_cfg_handle_t *cfg_handle,
	const OpenaisCfgCallbacksT *cfgCallbacks)
{
	struct cfg_instance *cfg_instance;
	SaAisErrorT error = SA_AIS_OK;

	error = saHandleCreate (&cfg_hdb, sizeof (struct cfg_instance), cfg_handle);
	if (error != SA_AIS_OK) {
		goto error_no_destroy;
	}

	error = saHandleInstanceGet (&cfg_hdb, *cfg_handle, (void *)&cfg_instance);
	if (error != SA_AIS_OK) {
		goto error_destroy;
	}

	cfg_instance->response_fd = -1;

	cfg_instance->dispatch_fd = -1;
	
	error = saServiceConnect (&cfg_instance->response_fd,
		&cfg_instance->dispatch_fd, CFG_SERVICE);
	if (error != SA_AIS_OK) {
		goto error_put_destroy;
	}

	if (cfgCallbacks) {
	memcpy (&cfg_instance->callbacks, cfgCallbacks, sizeof (OpenaisCfgCallbacksT));
	}

	pthread_mutex_init (&cfg_instance->response_mutex, NULL);

	pthread_mutex_init (&cfg_instance->dispatch_mutex, NULL);

	saHandleInstancePut (&cfg_hdb, *cfg_handle);

	return (SA_AIS_OK);

error_put_destroy:
	saHandleInstancePut (&cfg_hdb, *cfg_handle);
error_destroy:
	saHandleDestroy (&cfg_hdb, *cfg_handle);
error_no_destroy:
	return (error);
}

SaAisErrorT
openais_cfg_fd_get (
	openais_cfg_handle_t cfg_handle,
	SaSelectionObjectT *selectionObject)
{
	struct cfg_instance *cfg_instance;
	SaAisErrorT error;

	error = saHandleInstanceGet (&cfg_hdb, cfg_handle, (void *)&cfg_instance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	*selectionObject = cfg_instance->dispatch_fd;

	saHandleInstancePut (&cfg_hdb, cfg_handle);
	return (SA_AIS_OK);
}

SaAisErrorT
openais_cfg_dispatch (
	openais_cfg_handle_t cfg_handle,
	SaDispatchFlagsT dispatchFlags)
{
	struct pollfd ufds;
	int timeout = -1;
	SaAisErrorT error;
	int cont = 1; /* always continue do loop except when set to 0 */
	int dispatch_avail;
	struct cfg_instance *cfg_instance;
#ifdef COMPILE_OUT
	struct res_lib_openais_healthcheckcallback *res_lib_openais_healthcheckcallback;
	struct res_lib_openais_readinessstatesetcallback *res_lib_openais_readinessstatesetcallback;
	struct res_lib_openais_csisetcallback *res_lib_openais_csisetcallback;
	struct res_lib_openais_csiremovecallback *res_lib_openais_csiremovecallback;
	struct res_lib_cfg_statetrackcallback *res_lib_cfg_statetrackcallback;
#endif
	OpenaisCfgCallbacksT callbacks;
	struct res_overlay dispatch_data;

	error = saHandleInstanceGet (&cfg_hdb, cfg_handle,
		(void *)&cfg_instance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	/*
	 * Timeout instantly for SA_DISPATCH_ALL
	 */
	if (dispatchFlags == SA_DISPATCH_ALL) {
		timeout = 0;
	}

	do {
		/*
		 * Read data directly from socket
		 */
		ufds.fd = cfg_instance->dispatch_fd;
		ufds.events = POLLIN;
		ufds.revents = 0;

		error = saPollRetry (&ufds, 1, timeout);
		if (error != SA_AIS_OK) {
			goto error_nounlock;
		}

		pthread_mutex_lock (&cfg_instance->dispatch_mutex);

		error = saPollRetry (&ufds, 1, 0);
		if (error != SA_AIS_OK) {
			goto error_nounlock;
		}

		/*
		 * Handle has been finalized in another thread
		 */
		if (cfg_instance->finalize == 1) {
			error = SA_AIS_OK;
			pthread_mutex_unlock (&cfg_instance->dispatch_mutex);
			goto error_unlock;
		}

		dispatch_avail = ufds.revents & POLLIN;
		if (dispatch_avail == 0 && dispatchFlags == SA_DISPATCH_ALL) {
			pthread_mutex_unlock (&cfg_instance->dispatch_mutex);
			break; /* exit do while cont is 1 loop */
		} else
		if (dispatch_avail == 0) {
			pthread_mutex_unlock (&cfg_instance->dispatch_mutex);
			continue; /* next poll */
		}

		if (ufds.revents & POLLIN) {
			/*
			 * Queue empty, read response from socket
			 */
			error = saRecvRetry (cfg_instance->dispatch_fd, &dispatch_data.header,
				sizeof (mar_res_header_t));
			if (error != SA_AIS_OK) {
				goto error_unlock;
			}
			if (dispatch_data.header.size > sizeof (mar_res_header_t)) {
				error = saRecvRetry (cfg_instance->dispatch_fd, &dispatch_data.data,
					dispatch_data.header.size - sizeof (mar_res_header_t));
				if (error != SA_AIS_OK) {
					goto error_unlock;
				}
			}
		} else {
			pthread_mutex_unlock (&cfg_instance->dispatch_mutex);
			continue;
		}

		/*
		 * Make copy of callbacks, message data, unlock instance, and call callback
		 * A risk of this dispatch method is that the callback routines may
		 * operate at the same time that cfgFinalize has been called in another thread.
		 */
		memcpy (&callbacks, &cfg_instance->callbacks, sizeof (OpenaisCfgCallbacksT));
		pthread_mutex_unlock (&cfg_instance->dispatch_mutex);

		/*
		 * Dispatch incoming response
		 */
		switch (dispatch_data.header.id) {

		default:
			error = SA_AIS_ERR_LIBRARY;	
			goto error_nounlock;
			break;
		}

		/*
		 * Determine if more messages should be processed
		 */
		switch (dispatchFlags) {
		case SA_DISPATCH_ONE:
			cont = 0;
			break;
		case SA_DISPATCH_ALL:
			break;
		case SA_DISPATCH_BLOCKING:
			break;
		}
	} while (cont);

error_unlock:
	saHandleInstancePut (&cfg_hdb, cfg_handle);
error_nounlock:
	return (error);
}

SaAisErrorT
openais_cfg_finalize (
	openais_cfg_handle_t cfg_handle)
{
	struct cfg_instance *cfg_instance;
	SaAisErrorT error;

	error = saHandleInstanceGet (&cfg_hdb, cfg_handle, (void *)&cfg_instance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	pthread_mutex_lock (&cfg_instance->dispatch_mutex);

	pthread_mutex_lock (&cfg_instance->response_mutex);

	/*
	 * Another thread has already started finalizing
	 */
	if (cfg_instance->finalize) {
		pthread_mutex_unlock (&cfg_instance->response_mutex);
		pthread_mutex_unlock (&cfg_instance->dispatch_mutex);
		saHandleInstancePut (&cfg_hdb, cfg_handle);
		return (SA_AIS_ERR_BAD_HANDLE);
	}

	cfg_instance->finalize = 1;

	pthread_mutex_unlock (&cfg_instance->response_mutex);

	pthread_mutex_unlock (&cfg_instance->dispatch_mutex);

	pthread_mutex_destroy (&cfg_instance->response_mutex);

	pthread_mutex_destroy (&cfg_instance->dispatch_mutex);

	saHandleDestroy (&cfg_hdb, cfg_handle);

	if (cfg_instance->response_fd != -1) {
		shutdown (cfg_instance->response_fd, 0);
		close (cfg_instance->response_fd);
	}
	if (cfg_instance->dispatch_fd != -1) {
		shutdown (cfg_instance->dispatch_fd, 0);
		close (cfg_instance->dispatch_fd);
	}

	saHandleInstancePut (&cfg_hdb, cfg_handle);

	return (error);
}

SaAisErrorT
openais_cfg_ring_status_get (
	openais_cfg_handle_t cfg_handle,
	char ***interface_names,
	char ***status,
	unsigned int *interface_count)
{
	struct cfg_instance *cfg_instance;
	struct req_lib_cfg_ringstatusget req_lib_cfg_ringstatusget;
	struct res_lib_cfg_ringstatusget res_lib_cfg_ringstatusget;
	unsigned int i;
	SaAisErrorT error;

	error = saHandleInstanceGet (&cfg_hdb, cfg_handle, (void *)&cfg_instance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_cfg_ringstatusget.header.size = sizeof (struct req_lib_cfg_ringstatusget);
	req_lib_cfg_ringstatusget.header.id = MESSAGE_REQ_CFG_RINGSTATUSGET;

	pthread_mutex_lock (&cfg_instance->response_mutex);

	error = saSendReceiveReply (cfg_instance->response_fd,
		&req_lib_cfg_ringstatusget,
		sizeof (struct req_lib_cfg_ringstatusget),
		&res_lib_cfg_ringstatusget,
		sizeof (struct res_lib_cfg_ringstatusget));

	pthread_mutex_unlock (&cfg_instance->response_mutex);

	*interface_count = res_lib_cfg_ringstatusget.interface_count;
	*interface_names = malloc (sizeof (char *) * *interface_count);
	if (*interface_names == NULL) {
		return (SA_AIS_ERR_NO_MEMORY);
	}
	memset (*interface_names, 0, sizeof (char *) * *interface_count);

	*status = malloc (sizeof (char *) * *interface_count);
	if (*status == NULL) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_free_interface_names;
	}
	memset (*status, 0, sizeof (char *) * *interface_count);

	for (i = 0; i < res_lib_cfg_ringstatusget.interface_count; i++) {
		(*(interface_names))[i] = strdup (res_lib_cfg_ringstatusget.interface_name[i]);
		if ((*(interface_names))[i] == NULL) {
			error = SA_AIS_ERR_NO_MEMORY;
			goto error_free_contents;
		}
		(*(status))[i] = strdup (res_lib_cfg_ringstatusget.interface_status[i]);
		if ((*(status))[i] == NULL) {
			error = SA_AIS_ERR_NO_MEMORY;
			goto error_free_contents;
		}
	}
	goto no_error;

error_free_contents:
	for (i = 0; i < res_lib_cfg_ringstatusget.interface_count; i++) {
		if ((*(interface_names))[i]) {
			free ((*(interface_names))[i]);
		}
		if ((*(status))[i]) {
			free ((*(status))[i]);
		}
	}

	free (*status);
	
error_free_interface_names:
	free (*interface_names);
	
no_error:
	saHandleInstancePut (&cfg_hdb, cfg_handle);

	return (error);
}

SaAisErrorT
openais_cfg_ring_reenable (
	openais_cfg_handle_t cfg_handle)
{
	struct cfg_instance *cfg_instance;
	struct req_lib_cfg_ringreenable req_lib_cfg_ringreenable;
	struct res_lib_cfg_ringreenable res_lib_cfg_ringreenable;
	SaAisErrorT error;

	error = saHandleInstanceGet (&cfg_hdb, cfg_handle, (void *)&cfg_instance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_cfg_ringreenable.header.size = sizeof (struct req_lib_cfg_ringreenable);
	req_lib_cfg_ringreenable.header.id = MESSAGE_REQ_CFG_RINGREENABLE;

	pthread_mutex_lock (&cfg_instance->response_mutex);

	error = saSendReceiveReply (cfg_instance->response_fd,
		&req_lib_cfg_ringreenable,
		sizeof (struct req_lib_cfg_ringreenable),
		&res_lib_cfg_ringreenable,
		sizeof (struct res_lib_cfg_ringreenable));

	pthread_mutex_unlock (&cfg_instance->response_mutex);
	saHandleInstancePut (&cfg_hdb, cfg_handle);

	return (error);
}

SaAisErrorT
openais_cfg_state_track (
	openais_cfg_handle_t cfg_handle,
	SaUint8T trackFlags,
	const OpenaisCfgStateNotificationT *notificationBuffer)
{
	struct cfg_instance *cfg_instance;
	struct req_lib_cfg_statetrack req_lib_cfg_statetrack;
	struct res_lib_cfg_statetrack res_lib_cfg_statetrack;
	SaAisErrorT error;

	req_lib_cfg_statetrack.header.size = sizeof (struct req_lib_cfg_statetrack);
	req_lib_cfg_statetrack.header.id = MESSAGE_REQ_CFG_STATETRACKSTART;
	req_lib_cfg_statetrack.trackFlags = trackFlags;
	req_lib_cfg_statetrack.notificationBufferAddress = (OpenaisCfgStateNotificationT *)notificationBuffer;

	error = saHandleInstanceGet (&cfg_hdb, cfg_handle,
		(void *)&cfg_instance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	pthread_mutex_lock (&cfg_instance->response_mutex);

	error = saSendReceiveReply (cfg_instance->response_fd,
		&req_lib_cfg_statetrack,
		sizeof (struct req_lib_cfg_statetrack),
		&res_lib_cfg_statetrack,
		sizeof (struct res_lib_cfg_statetrack));

	pthread_mutex_unlock (&cfg_instance->response_mutex);

	saHandleInstancePut (&cfg_hdb, cfg_handle);

        return (error == SA_AIS_OK ? res_lib_cfg_statetrack.header.error : error);
}

SaAisErrorT
openais_cfg_state_track_stop (
	openais_cfg_handle_t cfg_handle)
{
	struct cfg_instance *cfg_instance;
	struct req_lib_cfg_statetrackstop req_lib_cfg_statetrackstop;
	struct res_lib_cfg_statetrackstop res_lib_cfg_statetrackstop;
	SaAisErrorT error;

	error = saHandleInstanceGet (&cfg_hdb, cfg_handle,
		(void *)&cfg_instance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_cfg_statetrackstop.header.size = sizeof (struct req_lib_cfg_statetrackstop);
	req_lib_cfg_statetrackstop.header.id = MESSAGE_REQ_CFG_STATETRACKSTOP;

	pthread_mutex_lock (&cfg_instance->response_mutex);

	error = saSendReceiveReply (cfg_instance->response_fd,
		&req_lib_cfg_statetrackstop,
		sizeof (struct req_lib_cfg_statetrackstop),
		&res_lib_cfg_statetrackstop,
		sizeof (struct res_lib_cfg_statetrackstop));

	pthread_mutex_unlock (&cfg_instance->response_mutex);

	saHandleInstancePut (&cfg_hdb, cfg_handle);

        return (error == SA_AIS_OK ? res_lib_cfg_statetrackstop.header.error : error);
}

SaAisErrorT
openais_cfg_admin_state_get (
	openais_cfg_handle_t cfg_handle,
	OpenaisCfgAdministrativeTargetT administrativeTarget,
	OpenaisCfgAdministrativeStateT *administrativeState)
{
	struct cfg_instance *cfg_instance;
	struct req_lib_cfg_administrativestateget req_lib_cfg_administrativestateget;
	struct res_lib_cfg_administrativestateget res_lib_cfg_administrativestateget;
	SaAisErrorT error;

	error = saHandleInstanceGet (&cfg_hdb, cfg_handle,
		(void *)&cfg_instance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_cfg_administrativestateget.header.id = MESSAGE_REQ_CFG_ADMINISTRATIVESTATEGET;
	req_lib_cfg_administrativestateget.header.size = sizeof (struct req_lib_cfg_administrativestateget);
	req_lib_cfg_administrativestateget.administrativeTarget = administrativeTarget;

	error = saSendReceiveReply (cfg_instance->response_fd,
		&req_lib_cfg_administrativestateget,
		sizeof (struct req_lib_cfg_administrativestateget),
		&res_lib_cfg_administrativestateget,
		sizeof (struct res_lib_cfg_administrativestateget));

	error = res_lib_cfg_administrativestateget.header.error;

	pthread_mutex_unlock (&cfg_instance->response_mutex);

	saHandleInstancePut (&cfg_hdb, cfg_handle);

        return (error == SA_AIS_OK ? res_lib_cfg_administrativestateget.header.error : error);
}

SaAisErrorT
openais_cfg_admin_state_set (
	openais_cfg_handle_t cfg_handle,
	OpenaisCfgAdministrativeTargetT administrativeTarget,
	OpenaisCfgAdministrativeStateT administrativeState)
{
	struct cfg_instance *cfg_instance;
	struct req_lib_cfg_administrativestateset req_lib_cfg_administrativestateset;
	struct res_lib_cfg_administrativestateset res_lib_cfg_administrativestateset;
	SaAisErrorT error;

	error = saHandleInstanceGet (&cfg_hdb, cfg_handle,
		(void *)&cfg_instance);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_cfg_administrativestateset.header.id = MESSAGE_REQ_CFG_ADMINISTRATIVESTATEGET;
	req_lib_cfg_administrativestateset.header.size = sizeof (struct req_lib_cfg_administrativestateset);
	req_lib_cfg_administrativestateset.administrativeTarget = administrativeTarget;
	req_lib_cfg_administrativestateset.administrativeState = administrativeState;

	error = saSendReceiveReply (cfg_instance->response_fd,
		&req_lib_cfg_administrativestateset,
		sizeof (struct req_lib_cfg_administrativestateset),
		&res_lib_cfg_administrativestateset,
		sizeof (struct res_lib_cfg_administrativestateset));

	error = res_lib_cfg_administrativestateset.header.error;

	pthread_mutex_unlock (&cfg_instance->response_mutex);

	saHandleInstancePut (&cfg_hdb, cfg_handle);

        return (error == SA_AIS_OK ? res_lib_cfg_administrativestateset.header.error : error);
}
