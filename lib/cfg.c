
/*
 * Copyright (c) 2002-2005 MontaVista Software, Inc.
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

#include "../include/saAis.h"
#include "../include/openaisCfg.h"
#include "../include/ipc_gen.h"
#include "../include/ipc_cfg.h"
#include "util.h"

struct res_overlay {
	struct res_header header;
	char data[4096];
};

/*
 * Data structure for instance data
 */
struct cfgInstance {
	int response_fd;
	int dispatch_fd;
	OpenaisCfgCallbacksT callbacks;
	SaNameT compName;
	int compRegistered;
	int finalize;
	pthread_mutex_t response_mutex;
	pthread_mutex_t dispatch_mutex;
};

static void cfgHandleInstanceDestructor (void *);

/*
 * All instances in one database
 */
static struct saHandleDatabase cfgHandleDatabase = {
	.handleCount				= 0,
	.handles					= 0,
	.mutex						= PTHREAD_MUTEX_INITIALIZER,
	.handleInstanceDestructor	= cfgHandleInstanceDestructor
};

/*
 * Versions supported
 */
static SaVersionT amfVersionsSupported[] = {
	{ 'A', 1, 1 }
};

static struct saVersionDatabase amfVersionDatabase = {
	sizeof (amfVersionsSupported) / sizeof (SaVersionT),
	amfVersionsSupported
};
	
/*
 * Implementation
 */

void cfgHandleInstanceDestructor (void *instance)
{
}

SaAisErrorT
openaisCfgInitialize (
	OpenaisCfgHandleT *cfgHandle,
	const OpenaisCfgCallbacksT *amfCallbacks,
	SaVersionT *version)
{
	struct cfgInstance *cfgInstance;
	SaAisErrorT error = SA_OK;

	error = saVersionVerify (&amfVersionDatabase, (SaVersionT *)version);
	if (error != SA_OK) {
		goto error_no_destroy;
	}
	
	error = saHandleCreate (&cfgHandleDatabase, sizeof (struct cfgInstance), cfgHandle);
	if (error != SA_OK) {
		goto error_no_destroy;
	}

	error = saHandleInstanceGet (&cfgHandleDatabase, *cfgHandle, (void *)&cfgInstance);
	if (error != SA_OK) {
		goto error_destroy;
	}

	cfgInstance->response_fd = -1;

	cfgInstance->dispatch_fd = -1;
	
	error = saServiceConnectTwo (&cfgInstance->response_fd,
		&cfgInstance->dispatch_fd, AMF_SERVICE);
	if (error != SA_OK) {
		goto error_put_destroy;
	}

	memcpy (&cfgInstance->callbacks, amfCallbacks, sizeof (OpenaisCfgCallbacksT));

	pthread_mutex_init (&cfgInstance->response_mutex, NULL);

	pthread_mutex_init (&cfgInstance->dispatch_mutex, NULL);

	saHandleInstancePut (&cfgHandleDatabase, *cfgHandle);

	return (SA_OK);

error_put_destroy:
	saHandleInstancePut (&cfgHandleDatabase, *cfgHandle);
error_destroy:
	saHandleDestroy (&cfgHandleDatabase, *cfgHandle);
error_no_destroy:
	return (error);
}

SaAisErrorT
openaisCfgSelectionObjectGet (
	OpenaisCfgHandleT cfgHandle,
	SaSelectionObjectT *selectionObject)
{
	struct cfgInstance *cfgInstance;
	SaAisErrorT error;

	error = saHandleInstanceGet (&cfgHandleDatabase, cfgHandle, (void *)&cfgInstance);
	if (error != SA_OK) {
		return (error);
	}

	*selectionObject = cfgInstance->dispatch_fd;

	saHandleInstancePut (&cfgHandleDatabase, cfgHandle);
	return (SA_OK);
}

SaAisErrorT
openaisCfgDispatch (
	OpenaisCfgHandleT cfgHandle,
	SaDispatchFlagsT dispatchFlags)
{
	struct pollfd ufds;
	int timeout = -1;
	SaAisErrorT error;
	int cont = 1; /* always continue do loop except when set to 0 */
	int dispatch_avail;
	struct cfgInstance *cfgInstance;
#ifdef COMPILE_OUT
	struct res_lib_openais_healthcheckcallback *res_lib_openais_healthcheckcallback;
	struct res_lib_openais_readinessstatesetcallback *res_lib_openais_readinessstatesetcallback;
	struct res_lib_openais_csisetcallback *res_lib_openais_csisetcallback;
	struct res_lib_openais_csiremovecallback *res_lib_openais_csiremovecallback;
	struct res_lib_cfg_statetrackcallback *res_lib_cfg_statetrackcallback;
#endif
	OpenaisCfgCallbacksT callbacks;
	struct res_overlay dispatch_data;

	error = saHandleInstanceGet (&cfgHandleDatabase, cfgHandle,
		(void *)&cfgInstance);
	if (error != SA_OK) {
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
		ufds.fd = cfgInstance->dispatch_fd;
		ufds.events = POLLIN;
		ufds.revents = 0;

		error = saPollRetry (&ufds, 1, timeout);
		if (error != SA_OK) {
			goto error_nounlock;
		}

		pthread_mutex_lock (&cfgInstance->dispatch_mutex);

		error = saPollRetry (&ufds, 1, 0);
		if (error != SA_OK) {
			goto error_nounlock;
		}

		/*
		 * Handle has been finalized in another thread
		 */
		if (cfgInstance->finalize == 1) {
			error = SA_OK;
			pthread_mutex_unlock (&cfgInstance->dispatch_mutex);
			goto error_unlock;
		}

		dispatch_avail = ufds.revents & POLLIN;
		if (dispatch_avail == 0 && dispatchFlags == SA_DISPATCH_ALL) {
			pthread_mutex_unlock (&cfgInstance->dispatch_mutex);
			break; /* exit do while cont is 1 loop */
		} else
		if (dispatch_avail == 0) {
			pthread_mutex_unlock (&cfgInstance->dispatch_mutex);
			continue; /* next poll */
		}

		if (ufds.revents & POLLIN) {
			/*
			 * Queue empty, read response from socket
			 */
			error = saRecvRetry (cfgInstance->dispatch_fd, &dispatch_data.header,
				sizeof (struct res_header), MSG_WAITALL | MSG_NOSIGNAL);
			if (error != SA_OK) {
				goto error_unlock;
			}
			if (dispatch_data.header.size > sizeof (struct res_header)) {
				error = saRecvRetry (cfgInstance->dispatch_fd, &dispatch_data.data,
					dispatch_data.header.size - sizeof (struct res_header),
					MSG_WAITALL | MSG_NOSIGNAL);
				if (error != SA_OK) {
					goto error_unlock;
				}
			}
		} else {
			pthread_mutex_unlock (&cfgInstance->dispatch_mutex);
			continue;
		}

		/*
		 * Make copy of callbacks, message data, unlock instance, and call callback
		 * A risk of this dispatch method is that the callback routines may
		 * operate at the same time that amfFinalize has been called in another thread.
		 */
		memcpy (&callbacks, &cfgInstance->callbacks, sizeof (OpenaisCfgCallbacksT));
		pthread_mutex_unlock (&cfgInstance->dispatch_mutex);

		/*
		 * Dispatch incoming response
		 */
		switch (dispatch_data.header.id) {

#ifdef COMPILE_OUT
		case MESSAGE_RES_AMF_HEALTHCHECKCALLBACK:
			res_lib_openais_healthcheckcallback = (struct res_lib_openais_healthcheckcallback *)&dispatch_data;

			callbacks.openaisCfgHealthcheckCallback (
				res_lib_openais_healthcheckcallback->invocation,
				&res_lib_openais_healthcheckcallback->compName,
				res_lib_openais_healthcheckcallback->checkType);
			break;

		case MESSAGE_RES_AMF_READINESSSTATESETCALLBACK:
			res_lib_openais_readinessstatesetcallback = (struct res_lib_openais_readinessstatesetcallback *)&dispatch_data;
			callbacks.openaisCfgReadinessStateSetCallback (
				res_lib_openais_readinessstatesetcallback->invocation,
				&res_lib_openais_readinessstatesetcallback->compName,
				res_lib_openais_readinessstatesetcallback->readinessState);
			break;

		case MESSAGE_RES_AMF_CSISETCALLBACK:
			res_lib_openais_csisetcallback = (struct res_lib_openais_csisetcallback *)&dispatch_data;
			callbacks.openaisCfgCSISetCallback (
				res_lib_openais_csisetcallback->invocation,
				&res_lib_openais_csisetcallback->compName,
				&res_lib_openais_csisetcallback->csiName,
				res_lib_openais_csisetcallback->csiFlags,
				&res_lib_openais_csisetcallback->haState,
				&res_lib_openais_csisetcallback->activeCompName,
				res_lib_openais_csisetcallback->transitionDescriptor);
			break;

		case MESSAGE_RES_AMF_CSIREMOVECALLBACK:
			res_lib_openais_csiremovecallback = (struct res_lib_openais_csiremovecallback *)&dispatch_data;
			callbacks.openaisCfgCSIRemoveCallback (
				res_lib_openais_csiremovecallback->invocation,
				&res_lib_openais_csiremovecallback->compName,
				&res_lib_openais_csiremovecallback->csiName,
				&res_lib_openais_csiremovecallback->csiFlags);
			break;

		case MESSAGE_RES_AMF_PROTECTIONGROUPTRACKCALLBACK:
			res_lib_cfg_statetrackcallback = (struct res_lib_cfg_statetrackcallback *)&dispatch_data;
			memcpy (res_lib_cfg_statetrackcallback->notificationBufferAddress,
				res_lib_cfg_statetrackcallback->notificationBuffer,
				res_lib_cfg_statetrackcallback->numberOfItems * sizeof (OpenaisCfgProtectionGroupNotificationT));
			callbacks.openaisCfgProtectionGroupTrackCallback(
				&res_lib_cfg_statetrackcallback->csiName,
				res_lib_cfg_statetrackcallback->notificationBufferAddress,
				res_lib_cfg_statetrackcallback->numberOfItems,
				res_lib_cfg_statetrackcallback->numberOfMembers,
				res_lib_cfg_statetrackcallback->error);
			break;

#endif
		default:
			error = SA_ERR_LIBRARY;	
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
	saHandleInstancePut (&cfgHandleDatabase, cfgHandle);
error_nounlock:
	return (error);
}

SaAisErrorT
openaisCfgFinalize (
	OpenaisCfgHandleT cfgHandle)
{
	struct cfgInstance *cfgInstance;
	SaAisErrorT error;

	error = saHandleInstanceGet (&cfgHandleDatabase, cfgHandle, (void *)&cfgInstance);
	if (error != SA_OK) {
		return (error);
	}

	pthread_mutex_lock (&cfgInstance->dispatch_mutex);

	pthread_mutex_lock (&cfgInstance->response_mutex);

	/*
	 * Another thread has already started finalizing
	 */
	if (cfgInstance->finalize) {
		pthread_mutex_unlock (&cfgInstance->response_mutex);
		pthread_mutex_unlock (&cfgInstance->dispatch_mutex);
		saHandleInstancePut (&cfgHandleDatabase, cfgHandle);
		return (SA_ERR_BAD_HANDLE);
	}

	cfgInstance->finalize = 1;

	pthread_mutex_unlock (&cfgInstance->response_mutex);

	pthread_mutex_unlock (&cfgInstance->dispatch_mutex);

	saHandleDestroy (&cfgHandleDatabase, cfgHandle);

	if (cfgInstance->response_fd != -1) {
		shutdown (cfgInstance->response_fd, 0);
		close (cfgInstance->response_fd);
	}
	if (cfgInstance->dispatch_fd != -1) {
		shutdown (cfgInstance->dispatch_fd, 0);
		close (cfgInstance->dispatch_fd);
	}

	saHandleInstancePut (&cfgHandleDatabase, cfgHandle);

	return (error);
}

SaAisErrorT
openaisCfgStateTrackStart (
	OpenaisCfgHandleT cfgHandle,
	SaUint8T trackFlags,
	const OpenaisCfgStateNotificationT *notificationBuffer)
{
	struct cfgInstance *cfgInstance;
	struct req_lib_cfg_statetrackstart req_lib_cfg_statetrackstart;
	struct res_lib_cfg_statetrackstart res_lib_cfg_statetrackstart;
	SaAisErrorT error;

	req_lib_cfg_statetrackstart.header.size = sizeof (struct req_lib_cfg_statetrackstart);
	req_lib_cfg_statetrackstart.header.id = MESSAGE_REQ_CFG_STATETRACKSTART;
	req_lib_cfg_statetrackstart.trackFlags = trackFlags;
	req_lib_cfg_statetrackstart.notificationBufferAddress = (OpenaisCfgStateNotificationT *)notificationBuffer;

	error = saHandleInstanceGet (&cfgHandleDatabase, cfgHandle,
		(void *)&cfgInstance);
	if (error != SA_OK) {
		return (error);
	}

	pthread_mutex_lock (&cfgInstance->response_mutex);

	error = saSendReceiveReply (cfgInstance->response_fd,
		&req_lib_cfg_statetrackstart,
		sizeof (struct req_lib_cfg_statetrackstart),
		&res_lib_cfg_statetrackstart,
		sizeof (struct res_lib_cfg_statetrackstart));

	pthread_mutex_unlock (&cfgInstance->response_mutex);

	saHandleInstancePut (&cfgHandleDatabase, cfgHandle);

        return (error == SA_AIS_OK ? res_lib_cfg_statetrackstart.header.error : error);
}

SaAisErrorT
openaisCfgStateTrackStop (
	OpenaisCfgHandleT cfgHandle)
{
	struct cfgInstance *cfgInstance;
	struct req_lib_cfg_statetrackstop req_lib_cfg_statetrackstop;
	struct res_lib_cfg_statetrackstop res_lib_cfg_statetrackstop;
	SaAisErrorT error;

	error = saHandleInstanceGet (&cfgHandleDatabase, cfgHandle,
		(void *)&cfgInstance);
	if (error != SA_OK) {
		return (error);
	}

	req_lib_cfg_statetrackstop.header.size = sizeof (struct req_lib_cfg_statetrackstop);
	req_lib_cfg_statetrackstop.header.id = MESSAGE_REQ_CFG_STATETRACKSTOP;

	pthread_mutex_lock (&cfgInstance->response_mutex);

	error = saSendReceiveReply (cfgInstance->response_fd,
		&req_lib_cfg_statetrackstop,
		sizeof (struct req_lib_cfg_statetrackstop),
		&res_lib_cfg_statetrackstop,
		sizeof (struct res_lib_cfg_statetrackstop));

	pthread_mutex_unlock (&cfgInstance->response_mutex);

	saHandleInstancePut (&cfgHandleDatabase, cfgHandle);

        return (error == SA_AIS_OK ? res_lib_cfg_statetrackstop.header.error : error);
}

SaAisErrorT
openaisCfgAdministrativeStateGet (
	OpenaisCfgHandleT cfgHandle,
	OpenaisCfgAdministrativeTargetT administrativeTarget,
	OpenaisCfgAdministrativeStateT *administrativeState)
{
	struct cfgInstance *cfgInstance;
	struct req_lib_cfg_administrativestateget req_lib_cfg_administrativestateget;
	struct res_lib_cfg_administrativestateget res_lib_cfg_administrativestateget;
	SaAisErrorT error;

	error = saHandleInstanceGet (&cfgHandleDatabase, cfgHandle,
		(void *)&cfgInstance);
	if (error != SA_OK) {
		return (error);
	}

	req_lib_cfg_administrativestateget.header.id = MESSAGE_REQ_CFG_ADMINISTRATIVESTATEGET;
	req_lib_cfg_administrativestateget.header.size = sizeof (struct req_lib_cfg_administrativestateget);
	req_lib_cfg_administrativestateget.administrativeTarget = administrativeTarget;

	error = saSendReceiveReply (cfgInstance->response_fd,
		&req_lib_cfg_administrativestateget,
		sizeof (struct req_lib_cfg_administrativestateget),
		&res_lib_cfg_administrativestateget,
		sizeof (struct res_lib_cfg_administrativestateget));

	error = res_lib_cfg_administrativestateget.header.error;

	pthread_mutex_unlock (&cfgInstance->response_mutex);

	saHandleInstancePut (&cfgHandleDatabase, cfgHandle);

        return (error == SA_AIS_OK ? res_lib_cfg_administrativestateget.header.error : error);
}

SaAisErrorT
openaisCfgAdministrativeStateSet (
	OpenaisCfgHandleT cfgHandle,
	OpenaisCfgAdministrativeTargetT administrativeTarget,
	OpenaisCfgAdministrativeStateT administrativeState)
{
	struct cfgInstance *cfgInstance;
	struct req_lib_cfg_administrativestateset req_lib_cfg_administrativestateset;
	struct res_lib_cfg_administrativestateset res_lib_cfg_administrativestateset;
	SaAisErrorT error;

	error = saHandleInstanceGet (&cfgHandleDatabase, cfgHandle,
		(void *)&cfgInstance);
	if (error != SA_OK) {
		return (error);
	}

	req_lib_cfg_administrativestateset.header.id = MESSAGE_REQ_CFG_ADMINISTRATIVESTATEGET;
	req_lib_cfg_administrativestateset.header.size = sizeof (struct req_lib_cfg_administrativestateset);
	req_lib_cfg_administrativestateset.administrativeTarget = administrativeTarget;
	req_lib_cfg_administrativestateset.administrativeState = administrativeState;

	error = saSendReceiveReply (cfgInstance->response_fd,
		&req_lib_cfg_administrativestateset,
		sizeof (struct req_lib_cfg_administrativestateset),
		&res_lib_cfg_administrativestateset,
		sizeof (struct res_lib_cfg_administrativestateset));

	error = res_lib_cfg_administrativestateset.header.error;

	pthread_mutex_unlock (&cfgInstance->response_mutex);

	saHandleInstancePut (&cfgHandleDatabase, cfgHandle);

        return (error == SA_AIS_OK ? res_lib_cfg_administrativestateset.header.error : error);
}
