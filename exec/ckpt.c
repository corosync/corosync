/*
 * Copyright (c) 2003-2004 MontaVista Software, Inc.
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
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>

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

DECLARE_LIST_INIT(checkpointListHead);

DECLARE_LIST_INIT(checkpointIteratorListHead);

static int ckptCheckpointApiFinalize (struct conn_info *conn_info);
static int ckptSectionIteratorApiFinalize (struct conn_info *conn_info);

static int message_handler_req_lib_activatepoll (struct conn_info *, void *message);

static int message_handler_req_exec_ckpt_checkpointopen (void *message, struct in_addr source_addr);

static int message_handler_req_exec_ckpt_checkpointclose (void *message, struct in_addr source_addr);

static int message_handler_req_exec_ckpt_checkpointunlink (void *message, struct in_addr source_addr);

static int message_handler_req_exec_ckpt_checkpointretentiondurationset (void *message, struct in_addr source_addr);

static int message_handler_req_exec_ckpt_sectioncreate (void *message, struct in_addr source_addr);

static int message_handler_req_exec_ckpt_sectiondelete (void *message, struct in_addr source_addr);

static int message_handler_req_exec_ckpt_sectionexpirationtimeset (void *message, struct in_addr source_addr);

static int message_handler_req_exec_ckpt_sectionwrite (void *message, struct in_addr source_addr);

static int message_handler_req_exec_ckpt_sectionoverwrite (void *message, struct in_addr source_addr);

static int message_handler_req_exec_ckpt_sectionread (void *message, struct in_addr source_addr);

static int message_handler_req_lib_ckpt_init (struct conn_info *conn_info, void *message);

static int message_handler_req_lib_ckpt_checkpoint_init (struct conn_info *conn_info, void *message);

static int message_handler_req_lib_ckpt_sectioniterator_init (struct conn_info *conn_info, void *message);

static int message_handler_req_lib_ckpt_checkpointopen (struct conn_info *conn_info, void *message);

static int message_handler_req_lib_ckpt_checkpointopenasync (struct conn_info *conn_info, void *message);

static int message_handler_req_lib_ckpt_checkpointunlink (struct conn_info *conn_info, void *message);

static int message_handler_req_lib_ckpt_checkpointretentiondurationset (struct conn_info *conn_info, void *message);

static int message_handler_req_lib_ckpt_activecheckpointset (struct conn_info *conn_info, void *message);

static int message_handler_req_lib_ckpt_checkpointstatusget (struct conn_info *conn_info, void *message);

static int message_handler_req_lib_ckpt_sectioncreate (struct conn_info *conn_info, void *message);

static int message_handler_req_lib_ckpt_sectiondelete (struct conn_info *conn_info, void *message);

static int message_handler_req_lib_ckpt_sectionexpirationtimeset (struct conn_info *conn_info, void *message);

static int message_handler_req_lib_ckpt_sectionwrite (struct conn_info *conn_info, void *message);

static int message_handler_req_lib_ckpt_sectionoverwrite (struct conn_info *conn_info, void *message);

static int message_handler_req_lib_ckpt_sectionread (struct conn_info *conn_info, void *message);

static int message_handler_req_lib_ckpt_checkpointsynchronize (struct conn_info *conn_info, void *message);

static int message_handler_req_lib_ckpt_checkpointsyncronizeasync (struct conn_info *conn_info, void *message);

static int message_handler_req_lib_ckpt_sectioniteratorinitialize (struct conn_info *conn_info, void *message);
static int message_handler_req_lib_ckpt_sectioniteratornext (struct conn_info *conn_info, void *message);

static int ckptConfChg (
	struct sockaddr_in *member_list, int member_list_entries,
	struct sockaddr_in *left_list, int left_list_entries,
	struct sockaddr_in *joined_list, int joined_list_entries) {

	return (0);
}

int (*ckpt_libais_handler_fns[]) (struct conn_info *conn_info, void *) = {
	message_handler_req_lib_activatepoll
};

/*
 * TODO
 */
int (*ckpt_aisexec_handler_fns[]) (void *, struct in_addr source_addr) = {
};

/*
 * exported service
 */
struct service_handler ckpt_service_handler = {
	.libais_handler_fns			= ckpt_libais_handler_fns,
	.libais_handler_fns_count	= sizeof (ckpt_libais_handler_fns) / sizeof (int (*)),
	.aisexec_handler_fns		= ckpt_aisexec_handler_fns,
	.aisexec_handler_fns_count	= sizeof (ckpt_aisexec_handler_fns) / sizeof (int (*)),
	.confchg_fn					= 0, /* ckpt service handler is not distributed */
	.libais_init_fn				= message_handler_req_lib_ckpt_init,
	.libais_exit_fn				= 0,
	.aisexec_init_fn			= 0
};

static int (*ckpt_checkpoint_libais_handler_fns[]) (struct conn_info *conn_info, void *) = {
	message_handler_req_lib_activatepoll,
	message_handler_req_lib_ckpt_checkpointopen,
	message_handler_req_lib_ckpt_checkpointopenasync,
	message_handler_req_lib_ckpt_checkpointunlink,
	message_handler_req_lib_ckpt_checkpointretentiondurationset,
	message_handler_req_lib_ckpt_activecheckpointset,
	message_handler_req_lib_ckpt_checkpointstatusget,
	message_handler_req_lib_ckpt_sectioncreate,
	message_handler_req_lib_ckpt_sectiondelete,
	message_handler_req_lib_ckpt_sectionexpirationtimeset,
	message_handler_req_lib_ckpt_sectionwrite,
	message_handler_req_lib_ckpt_sectionoverwrite,
	message_handler_req_lib_ckpt_sectionread,
	message_handler_req_lib_ckpt_checkpointsynchronize,
	message_handler_req_lib_ckpt_checkpointsyncronizeasync
};
 
static int (*ckpt_checkpoint_aisexec_handler_fns[]) (void *msg, struct in_addr source_addr) = {
	message_handler_req_exec_ckpt_checkpointopen,
	message_handler_req_exec_ckpt_checkpointclose,
	message_handler_req_exec_ckpt_checkpointunlink,
	message_handler_req_exec_ckpt_checkpointretentiondurationset,
	message_handler_req_exec_ckpt_sectioncreate,
	message_handler_req_exec_ckpt_sectiondelete,
	message_handler_req_exec_ckpt_sectionexpirationtimeset,
	message_handler_req_exec_ckpt_sectionwrite,
	message_handler_req_exec_ckpt_sectionoverwrite,
	message_handler_req_exec_ckpt_sectionread
};

struct service_handler ckpt_checkpoint_service_handler = {
	.libais_handler_fns			= ckpt_checkpoint_libais_handler_fns,
	.libais_handler_fns_count	= sizeof (ckpt_checkpoint_libais_handler_fns) / sizeof (int (*)),
	.aisexec_handler_fns		= ckpt_checkpoint_aisexec_handler_fns,
	.aisexec_handler_fns_count	= sizeof (ckpt_checkpoint_aisexec_handler_fns) / sizeof (int (*)),
	.confchg_fn					= ckptConfChg,
	.libais_init_fn				= message_handler_req_lib_ckpt_checkpoint_init,
	.libais_exit_fn				= ckptCheckpointApiFinalize,
	.aisexec_init_fn			= 0
};

static int (*ckpt_sectioniterator_libais_handler_fns[]) (struct conn_info *conn_info, void *) = {
	message_handler_req_lib_activatepoll,
    message_handler_req_lib_ckpt_sectioniteratorinitialize,
    message_handler_req_lib_ckpt_sectioniteratornext
};

static int (*ckpt_sectioniterator_aisexec_handler_fns[]) (void *msg, struct in_addr source_addr) = {
};

struct service_handler ckpt_sectioniterator_service_handler = {
	.libais_handler_fns			= ckpt_sectioniterator_libais_handler_fns,
	.libais_handler_fns_count	= sizeof (ckpt_sectioniterator_libais_handler_fns) / sizeof (int (*)),
	.aisexec_handler_fns		= ckpt_sectioniterator_aisexec_handler_fns ,
	.aisexec_handler_fns_count	= sizeof (ckpt_sectioniterator_aisexec_handler_fns) / sizeof (int (*)),
	.confchg_fn					= 0, /* Section Iterators are not distributed */
	.libais_init_fn				= message_handler_req_lib_ckpt_sectioniterator_init,
	.libais_exit_fn				= ckptSectionIteratorApiFinalize,
	.aisexec_init_fn			= 0
};

static struct saCkptCheckpoint *findCheckpoint (SaNameT *name)
{
	struct list_head *checkpointList;
	struct saCkptCheckpoint *checkpoint;

   for (checkpointList = checkpointListHead.next;
        checkpointList != &checkpointListHead;
        checkpointList = checkpointList->next) {

        checkpoint = list_entry (checkpointList,
            struct saCkptCheckpoint, list);

		if (SaNameTisNameT (name, &checkpoint->name)) {
			return (checkpoint);
		}
	}
	return (0);
}

static struct saCkptCheckpointSection *findCheckpointSection (
	struct saCkptCheckpoint *ckptCheckpoint,
	char *id,
	int idLen)
{
	struct list_head *checkpointSectionList;
	struct saCkptCheckpointSection *ckptCheckpointSection;

	log_printf (LOG_LEVEL_DEBUG, "Finding checkpoint section id %s %d\n", id, idLen);
	for (checkpointSectionList = ckptCheckpoint->checkpointSectionsListHead.next;
		checkpointSectionList != &ckptCheckpoint->checkpointSectionsListHead;
		checkpointSectionList = checkpointSectionList->next) {

		ckptCheckpointSection = list_entry (checkpointSectionList,
			struct saCkptCheckpointSection, list);
	
		log_printf (LOG_LEVEL_DEBUG, "Checking section id %s %d\n", 
			ckptCheckpointSection->sectionDescriptor.sectionId.id,
			ckptCheckpointSection->sectionDescriptor.sectionId.idLen);

		if (ckptCheckpointSection->sectionDescriptor.sectionId.idLen == idLen &&
			(memcmp (ckptCheckpointSection->sectionDescriptor.sectionId.id, 
			id, idLen) == 0)) {

			return (ckptCheckpointSection);
		}
	}
	return 0;
}

void sendCkptCheckpointClose (struct saCkptCheckpoint *checkpoint) {
	struct req_exec_ckpt_checkpointclose req_exec_ckpt_checkpointclose;
	struct iovec iovecs[2];
	int result;

	req_exec_ckpt_checkpointclose.header.magic = MESSAGE_MAGIC;
	req_exec_ckpt_checkpointclose.header.size =
		sizeof (struct req_exec_ckpt_checkpointclose);
	req_exec_ckpt_checkpointclose.header.id = MESSAGE_REQ_EXEC_CKPT_CHECKPOINTCLOSE;

	memcpy (&req_exec_ckpt_checkpointclose.checkpointName,
		&checkpoint->name,
		sizeof (SaNameT));

	iovecs[0].iov_base = (char *)&req_exec_ckpt_checkpointclose;
	iovecs[0].iov_len = sizeof (req_exec_ckpt_checkpointclose);

	result = gmi_mcast (&aisexec_groupname, iovecs, 1, GMI_PRIO_MED);
}

static int ckptCheckpointApiFinalize (struct conn_info *conn_info)
{
	/*
	 * close checkpoint opened from this fd
	 */
	if (conn_info->service == SOCKET_SERVICE_CKPT_CHECKPOINT &&
		conn_info->ais_ci.u.libckpt_ci.checkpoint) {
		log_printf (LOG_LEVEL_DEBUG, "APIFinalize fd is %d %s\n", conn_info,
			getSaNameT (&conn_info->ais_ci.u.libckpt_ci.checkpoint->name));

		sendCkptCheckpointClose (conn_info->ais_ci.u.libckpt_ci.checkpoint);
	}
	return (0);
}

static int ckptSectionIteratorApiFinalize (struct conn_info *conn_info) {
	/*
	 * If section iterator connection, unlink from list and free section iterator data
	 */
	if (conn_info->service == SOCKET_SERVICE_CKPT_SECTIONITERATOR) {
		log_printf (LOG_LEVEL_DEBUG, "freeing section iterator\n");
		if (conn_info->ais_ci.u.libckpt_ci.sectionIterator.sectionIteratorEntries) {
			free (conn_info->ais_ci.u.libckpt_ci.sectionIterator.sectionIteratorEntries);
		}
		list_del (&conn_info->ais_ci.u.libckpt_ci.sectionIterator.list);
	}

	return (0);
}

static int message_handler_req_lib_activatepoll (struct conn_info *conn_info, void *message)
{
	struct res_lib_activatepoll res_lib_activatepoll;

	res_lib_activatepoll.header.magic = MESSAGE_MAGIC;
	res_lib_activatepoll.header.size = sizeof (struct res_lib_activatepoll);
	res_lib_activatepoll.header.id = MESSAGE_RES_LIB_ACTIVATEPOLL;
	libais_send_response (conn_info, &res_lib_activatepoll,
		sizeof (struct res_lib_activatepoll));

	return (0);
}

static int message_handler_req_exec_ckpt_checkpointopen (void *message, struct in_addr source_addr)
{
	struct req_exec_ckpt_checkpointopen *req_exec_ckpt_checkpointopen = (struct req_exec_ckpt_checkpointopen *)message;
	struct req_lib_ckpt_checkpointopen *req_lib_ckpt_checkpointopen = (struct req_lib_ckpt_checkpointopen *)&req_exec_ckpt_checkpointopen->req_lib_ckpt_checkpointopen;
	struct res_lib_ckpt_checkpointopen res_lib_ckpt_checkpointopen;

	struct saCkptCheckpoint *ckptCheckpoint = 0;
	struct saCkptCheckpointSection *ckptCheckpointSection = 0;
	SaErrorT error = SA_OK;

	log_printf (LOG_LEVEL_DEBUG, "Executive request to open checkpoint %p\n", req_exec_ckpt_checkpointopen);
	
	ckptCheckpoint = findCheckpoint (&req_lib_ckpt_checkpointopen->checkpointName);

	/*
	 * If checkpoint doesn't exist, create one
	 */
	if (ckptCheckpoint == 0) {
		ckptCheckpoint = malloc (sizeof (struct saCkptCheckpoint));
		if (ckptCheckpoint == 0) {
			error = SA_ERR_NO_MEMORY;
			goto error_exit;
		}

		ckptCheckpointSection = malloc (sizeof (struct saCkptCheckpointSection));
		if (ckptCheckpointSection == 0) {
			free (ckptCheckpoint);
			error = SA_ERR_NO_MEMORY;
			goto error_exit;
		}

		memcpy (&ckptCheckpoint->name,
			&req_lib_ckpt_checkpointopen->checkpointName,
			sizeof (SaNameT));
		memcpy (&ckptCheckpoint->checkpointCreationAttributes,
			&req_lib_ckpt_checkpointopen->checkpointCreationAttributes,
			sizeof (SaCkptCheckpointCreationAttributesT));
		ckptCheckpoint->unlinked = 0;
		list_init (&ckptCheckpoint->list);
		list_init (&ckptCheckpoint->checkpointSectionsListHead);
		list_add (&ckptCheckpoint->list, &checkpointListHead);
		ckptCheckpoint->referenceCount = 0;

		/*
		 * Add in default checkpoint section
		 */
		list_init (&ckptCheckpointSection->list);
		list_add (&ckptCheckpointSection->list, &ckptCheckpoint->checkpointSectionsListHead);
		ckptCheckpointSection->sectionDescriptor.expirationTime = 0xFFFFFFFF; //SA_END_TIME;
		
		/*
		 * Default section id
		 */
		ckptCheckpointSection->sectionDescriptor.sectionId.id = 0;
		ckptCheckpointSection->sectionDescriptor.sectionId.idLen = 0;
		ckptCheckpointSection->sectionDescriptor.sectionSize = 0;
		ckptCheckpointSection->sectionDescriptor.expirationTime = 0xffffffff; /* SA_END_TIME */
		ckptCheckpointSection->sectionDescriptor.sectionState = SA_CKPT_SECTION_VALID;
		ckptCheckpointSection->sectionDescriptor.lastUpdate = 0; // current time
		ckptCheckpointSection->sectionData = 0;
	}

	/*
	 * If the checkpoint has been unlinked, it is an invalid name
	 */
	if (ckptCheckpoint->unlinked) {
		error = SA_ERR_INVALID_PARAM; /* Is this the correct return ? */
		goto error_exit;
	}

	/*
	 * Setup connection information and mark checkpoint as referenced
	 */
	log_printf (LOG_LEVEL_DEBUG, "CHECKPOINT opened is %p\n", ckptCheckpoint);
	ckptCheckpoint->referenceCount += 1;

	/*
	 * Send error result to CKPT library
	 */
error_exit:
	/*
	 * If this node was the source of the message, respond to this node
	 */
	if (req_exec_ckpt_checkpointopen->source.in_addr.s_addr == this_ip.sin_addr.s_addr) {
		req_exec_ckpt_checkpointopen->source.conn_info->ais_ci.u.libckpt_ci.checkpoint = ckptCheckpoint;
		req_exec_ckpt_checkpointopen->source.conn_info->ais_ci.u.libckpt_ci.checkpointOpenFlags = req_lib_ckpt_checkpointopen->checkpointOpenFlags;
		res_lib_ckpt_checkpointopen.header.magic = MESSAGE_MAGIC;
		res_lib_ckpt_checkpointopen.header.size = sizeof (struct res_lib_ckpt_checkpointopen);
		res_lib_ckpt_checkpointopen.header.id = MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTOPEN;
		res_lib_ckpt_checkpointopen.error = error;

		libais_send_response (req_exec_ckpt_checkpointopen->source.conn_info, &res_lib_ckpt_checkpointopen,
			sizeof (struct res_lib_ckpt_checkpointopen));
	}

//	return (error == SA_OK ? 0 : -1);
	return (0);
}

extern int message_handler_req_exec_ckpt_checkpointclose (void *message, struct in_addr source_addr)
{
	struct req_exec_ckpt_checkpointclose *req_exec_ckpt_checkpointclose = (struct req_exec_ckpt_checkpointclose *)message;
	struct saCkptCheckpoint *checkpoint = 0;

	log_printf (LOG_LEVEL_DEBUG, "Got EXEC request to close checkpoint %s\n", getSaNameT (&req_exec_ckpt_checkpointclose->checkpointName));

	checkpoint = findCheckpoint (&req_exec_ckpt_checkpointclose->checkpointName);
	if (checkpoint == 0) {
		return (0);
	}

	checkpoint->referenceCount--;
	log_printf (LOG_LEVEL_DEBUG, "disconnect called, new CKPT ref count is %d\n", 
		checkpoint->referenceCount);

	/*
	 * If checkpoint has been unlinked and this is the last reference, delete it
	 */
	if (checkpoint->unlinked && checkpoint->referenceCount == 0) {
		log_printf (LOG_LEVEL_DEBUG, "Unlinking checkpoint.\n");
		list_del (&checkpoint->list);
		free (checkpoint);
	} else
	if (checkpoint->referenceCount == 0) {
		// TODO Start retention duration timer if reference count is 0
		// and checkpoint has not been unlinked
	}
	
	return (0);
}

static int message_handler_req_exec_ckpt_checkpointunlink (void *message, struct in_addr source_addr)
{
	struct req_exec_ckpt_checkpointunlink *req_exec_ckpt_checkpointunlink = (struct req_exec_ckpt_checkpointunlink *)message;

	struct req_lib_ckpt_checkpointunlink *req_lib_ckpt_checkpointunlink = (struct req_lib_ckpt_checkpointunlink *)&req_exec_ckpt_checkpointunlink->req_lib_ckpt_checkpointunlink;
	struct res_lib_ckpt_checkpointunlink res_lib_ckpt_checkpointunlink;
	struct saCkptCheckpoint *ckptCheckpoint = 0;
	SaErrorT error = SA_OK;
	
	log_printf (LOG_LEVEL_DEBUG, "Got EXEC request to unlink checkpoint %p\n", req_exec_ckpt_checkpointunlink);
	ckptCheckpoint = findCheckpoint (&req_lib_ckpt_checkpointunlink->checkpointName);
	if (ckptCheckpoint == 0) {
printf ("invalid checkpoint name\n");
		error = SA_ERR_NOT_EXIST;
		goto error_exit;
	}
	if (ckptCheckpoint->unlinked) {
		error = SA_ERR_INVALID_PARAM;
		goto error_exit;
	}
	ckptCheckpoint->unlinked = 1;
	/*
	 * Immediately delete entry if reference count is zero
	 */
	if (ckptCheckpoint->referenceCount == 0) {
		list_del (&ckptCheckpoint->list);
		free (ckptCheckpoint);
	}

error_exit:
	/*
	 * If this node was the source of the message, respond to this node
	 */
	if (req_exec_ckpt_checkpointunlink->source.in_addr.s_addr == this_ip.sin_addr.s_addr) {
		res_lib_ckpt_checkpointunlink.header.magic = MESSAGE_MAGIC;
		res_lib_ckpt_checkpointunlink.header.size = sizeof (struct res_lib_ckpt_checkpointunlink);
		res_lib_ckpt_checkpointunlink.header.id = MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTUNLINK;
		res_lib_ckpt_checkpointunlink.error = error;
		libais_send_response (req_exec_ckpt_checkpointunlink->source.conn_info, &res_lib_ckpt_checkpointunlink,
			sizeof (struct res_lib_ckpt_checkpointunlink));
	}
	return (0);
}

static int message_handler_req_exec_ckpt_checkpointretentiondurationset (void *message, struct in_addr source_addr)
{
	struct req_exec_ckpt_checkpointretentiondurationset *req_exec_ckpt_checkpointretentiondurationset = (struct req_exec_ckpt_checkpointretentiondurationset *)message;
	struct saCkptCheckpoint *checkpoint;

	log_printf (LOG_LEVEL_DEBUG, "Got EXEC request to set retention duratione checkpoint %p\n", req_exec_ckpt_checkpointretentiondurationset);

	checkpoint = findCheckpoint (&req_exec_ckpt_checkpointretentiondurationset->checkpointName);
	if (checkpoint) {
		log_printf (LOG_LEVEL_DEBUG, "setting retention duration\n");
		checkpoint->checkpointCreationAttributes.retentionDuration = req_exec_ckpt_checkpointretentiondurationset->retentionDuration;
	}

	return (0);
}

static int message_handler_req_exec_ckpt_sectioncreate (void *message, struct in_addr source_addr) {
	struct req_exec_ckpt_sectioncreate *req_exec_ckpt_sectioncreate = (struct req_exec_ckpt_sectioncreate *)message;
	struct req_lib_ckpt_sectioncreate *req_lib_ckpt_sectioncreate = (struct req_lib_ckpt_sectioncreate *)&req_exec_ckpt_sectioncreate->req_lib_ckpt_sectioncreate;
	struct res_lib_ckpt_sectioncreate res_lib_ckpt_sectioncreate;
	struct saCkptCheckpoint *ckptCheckpoint;
	struct saCkptCheckpointSection *ckptCheckpointSection;
	void *initialData;
	void *sectionId;
	SaErrorT error = SA_OK;

	log_printf (LOG_LEVEL_DEBUG, "Executive request to create a checkpoint section.\n");
	ckptCheckpoint = findCheckpoint (&req_exec_ckpt_sectioncreate->checkpointName);
	if (ckptCheckpoint == 0) {
		error = SA_ERR_SYSTEM; // TODO find the right error for this
		goto error_exit;
	}

	/*
	 * Determine if user-specified checkpoint ID already exists
	 */
	ckptCheckpointSection = findCheckpointSection (ckptCheckpoint,
		((char *)req_lib_ckpt_sectioncreate) + sizeof (struct req_lib_ckpt_sectioncreate),
		req_lib_ckpt_sectioncreate->idLen);
	if (ckptCheckpointSection) {
		error = SA_ERR_EXIST;
		goto error_exit;
	}

	/*
	 * Allocate checkpoint section
	 */
	ckptCheckpointSection = malloc (sizeof (struct saCkptCheckpointSection));
	if (ckptCheckpointSection == 0) {
		error = SA_ERR_NO_MEMORY;
		goto error_exit;
	}
	/*
	 * Allocate checkpoint section data
	 */
	initialData = malloc (req_lib_ckpt_sectioncreate->initialDataSize);
	if (initialData == 0) {
		free (ckptCheckpointSection);
		error = SA_ERR_NO_MEMORY;
		goto error_exit;
	}
	/*
	 * Allocate checkpoint section id
	 */
	sectionId = malloc (req_lib_ckpt_sectioncreate->idLen);
	if (sectionId == 0) {
		free (ckptCheckpointSection);
		free (initialData);
		error = SA_ERR_NO_MEMORY;
		goto error_exit;
	}
		
	/*
	 * Copy checkpoint section and section ID
	 */
	memcpy (sectionId, ((char *)req_lib_ckpt_sectioncreate) + sizeof (struct req_lib_ckpt_sectioncreate),
		req_lib_ckpt_sectioncreate->idLen);
	
	memcpy (initialData,
		((char *)req_lib_ckpt_sectioncreate) +
			sizeof (struct req_lib_ckpt_sectioncreate) +
			req_lib_ckpt_sectioncreate->idLen,
		req_lib_ckpt_sectioncreate->initialDataSize);

	/*
	 * Configure checkpoint section
	 */
	ckptCheckpointSection->sectionDescriptor.expirationTime = req_lib_ckpt_sectioncreate->expirationTime;
	ckptCheckpointSection->sectionDescriptor.sectionId.id = sectionId;
	ckptCheckpointSection->sectionDescriptor.sectionId.idLen = req_lib_ckpt_sectioncreate->idLen;
	ckptCheckpointSection->sectionDescriptor.sectionSize = req_lib_ckpt_sectioncreate->initialDataSize;
	ckptCheckpointSection->sectionDescriptor.sectionState = SA_CKPT_SECTION_VALID;
	ckptCheckpointSection->sectionDescriptor.lastUpdate = 0; // TODO current time
	ckptCheckpointSection->sectionData = initialData;

	/*
	 * Add checkpoint section to checkpoint
	 */
	list_init (&ckptCheckpointSection->list);
	list_add (&ckptCheckpointSection->list, &ckptCheckpoint->checkpointSectionsListHead);

error_exit:
	if (req_exec_ckpt_sectioncreate->source.in_addr.s_addr == this_ip.sin_addr.s_addr) {
		res_lib_ckpt_sectioncreate.header.magic = MESSAGE_MAGIC;
		res_lib_ckpt_sectioncreate.header.size = sizeof (struct res_lib_ckpt_sectioncreate);
		res_lib_ckpt_sectioncreate.header.id = MESSAGE_RES_CKPT_CHECKPOINT_SECTIONCREATE;
		res_lib_ckpt_sectioncreate.error = error;

		libais_send_response (req_exec_ckpt_sectioncreate->source.conn_info,
			&res_lib_ckpt_sectioncreate,
			sizeof (struct res_lib_ckpt_sectioncreate));
	}
	return (0);
}

static int message_handler_req_exec_ckpt_sectiondelete (void *message, struct in_addr source_addr) {
	struct req_exec_ckpt_sectiondelete *req_exec_ckpt_sectiondelete = (struct req_exec_ckpt_sectiondelete *)message;
	struct req_lib_ckpt_sectiondelete *req_lib_ckpt_sectiondelete = (struct req_lib_ckpt_sectiondelete *)&req_exec_ckpt_sectiondelete->req_lib_ckpt_sectiondelete;
	struct res_lib_ckpt_sectiondelete res_lib_ckpt_sectiondelete;
	struct saCkptCheckpoint *ckptCheckpoint;
	struct saCkptCheckpointSection *ckptCheckpointSection;
	SaErrorT error = SA_OK;

	ckptCheckpoint = findCheckpoint (&req_exec_ckpt_sectiondelete->checkpointName);
	if (ckptCheckpoint == 0) {
		error = SA_ERR_NOT_EXIST;
		goto error_exit;
	}

	/*
	 * Determine if the user is trying to delete the default section
	 */
	if (req_lib_ckpt_sectiondelete->idLen == 0) {
		error = SA_ERR_INVALID_PARAM;
		goto error_exit;
	}

	/*
	 * Find checkpoint section to be deleted
	 */
	ckptCheckpointSection = findCheckpointSection (ckptCheckpoint,
		((char *)(req_lib_ckpt_sectiondelete) + sizeof (struct req_lib_ckpt_sectiondelete)),
		req_lib_ckpt_sectiondelete->idLen);
	if (ckptCheckpointSection == 0) {
printf ("section not found\n");
		error = SA_ERR_NOT_EXIST;
		goto error_exit;
	}

	/*
	 * Delete checkpoint section
	 */
	list_del (&ckptCheckpointSection->list);
	free (ckptCheckpointSection->sectionDescriptor.sectionId.id); 
	free (ckptCheckpointSection->sectionData);
	free (ckptCheckpointSection);

	/*
	 * return result to CKPT library
	 */
error_exit:
	if (req_exec_ckpt_sectiondelete->source.in_addr.s_addr == this_ip.sin_addr.s_addr) {
		res_lib_ckpt_sectiondelete.header.magic = MESSAGE_MAGIC;
		res_lib_ckpt_sectiondelete.header.size = sizeof (struct res_lib_ckpt_sectiondelete);
		res_lib_ckpt_sectiondelete.header.id = MESSAGE_RES_CKPT_CHECKPOINT_SECTIONDELETE;
		res_lib_ckpt_sectiondelete.error = error;

		libais_send_response (req_exec_ckpt_sectiondelete->source.conn_info,
			&res_lib_ckpt_sectiondelete,
			sizeof (struct res_lib_ckpt_sectiondelete));
	}
	return (0);
}

static int message_handler_req_exec_ckpt_sectionexpirationtimeset (void *message, struct in_addr source_addr) {
	struct req_exec_ckpt_sectionexpirationtimeset *req_exec_ckpt_sectionexpirationtimeset = (struct req_exec_ckpt_sectionexpirationtimeset *)message;
	struct req_lib_ckpt_sectionexpirationtimeset *req_lib_ckpt_sectionexpirationtimeset = (struct req_lib_ckpt_sectionexpirationtimeset *)&req_exec_ckpt_sectionexpirationtimeset->req_lib_ckpt_sectionexpirationtimeset;
	struct res_lib_ckpt_sectionexpirationtimeset res_lib_ckpt_sectionexpirationtimeset;
	struct saCkptCheckpoint *ckptCheckpoint;
	struct saCkptCheckpointSection *ckptCheckpointSection;
	SaErrorT error = SA_OK;

	log_printf (LOG_LEVEL_DEBUG, "Executive request to set section expiratoin time\n");
	ckptCheckpoint = findCheckpoint (&req_exec_ckpt_sectionexpirationtimeset->checkpointName);
	if (ckptCheckpoint == 0) {
		error = SA_ERR_NOT_EXIST;
		goto error_exit;
	}

	/*
	 * Determine if the user is trying to set expiration time for the default section
	 */
	if (req_lib_ckpt_sectionexpirationtimeset->idLen == 0) {
		error = SA_ERR_INVALID_PARAM;
		goto error_exit;
	}

	/*
	 * Find checkpoint section that expiration time should be set for
	 */
	ckptCheckpointSection = findCheckpointSection (ckptCheckpoint,
		((char *)req_lib_ckpt_sectionexpirationtimeset) +
			sizeof (struct req_lib_ckpt_sectionexpirationtimeset),
		req_lib_ckpt_sectionexpirationtimeset->idLen);

	if (ckptCheckpointSection == 0) {
		error = SA_ERR_NOT_EXIST;
		goto error_exit;
	}

	ckptCheckpointSection->sectionDescriptor.expirationTime = req_lib_ckpt_sectionexpirationtimeset->expirationTime;

error_exit:
	if (req_exec_ckpt_sectionexpirationtimeset->source.in_addr.s_addr == this_ip.sin_addr.s_addr) {
		res_lib_ckpt_sectionexpirationtimeset.header.magic = MESSAGE_MAGIC;
		res_lib_ckpt_sectionexpirationtimeset.header.size = sizeof (struct res_lib_ckpt_sectionexpirationtimeset);
		res_lib_ckpt_sectionexpirationtimeset.header.id = MESSAGE_RES_CKPT_CHECKPOINT_SECTIONEXPIRATIONTIMESET;
		res_lib_ckpt_sectionexpirationtimeset.error = error;

		libais_send_response (req_exec_ckpt_sectionexpirationtimeset->source.conn_info,
			&res_lib_ckpt_sectionexpirationtimeset,
			sizeof (struct res_lib_ckpt_sectionexpirationtimeset));
	}
	return (0);
}

static int message_handler_req_exec_ckpt_sectionwrite (void *message, struct in_addr source_addr) {
	struct req_exec_ckpt_sectionwrite *req_exec_ckpt_sectionwrite = (struct req_exec_ckpt_sectionwrite *)message;
	struct req_lib_ckpt_sectionwrite *req_lib_ckpt_sectionwrite = (struct req_lib_ckpt_sectionwrite *)&req_exec_ckpt_sectionwrite->req_lib_ckpt_sectionwrite;
	struct res_lib_ckpt_sectionwrite res_lib_ckpt_sectionwrite;
	struct saCkptCheckpoint *ckptCheckpoint;
	struct saCkptCheckpointSection *ckptCheckpointSection;
	int sizeRequired;
	void *sectionData;
	SaErrorT error = SA_OK;

	log_printf (LOG_LEVEL_DEBUG, "Executive request to section write.\n");
	ckptCheckpoint = findCheckpoint (&req_exec_ckpt_sectionwrite->checkpointName);
	if (ckptCheckpoint == 0) {
printf ("can't find checkpoint\n"); // TODO
		error = SA_ERR_NOT_EXIST;
		goto error_exit;
	}

//printf ("writing checkpoint section is %s\n", ((char *)req_lib_ckpt_sectionwrite) + sizeof (struct req_lib_ckpt_sectionwrite));
	/*
	 * Find checkpoint section to be written
	 */
	ckptCheckpointSection = findCheckpointSection (ckptCheckpoint,
		((char *)req_lib_ckpt_sectionwrite) + sizeof (struct req_lib_ckpt_sectionwrite),
		req_lib_ckpt_sectionwrite->idLen);
	if (ckptCheckpointSection == 0) {
printf ("CANT FIND SECTION '%s'\n",
		((char *)req_lib_ckpt_sectionwrite) + sizeof (struct req_lib_ckpt_sectionwrite));
		error = SA_ERR_NOT_EXIST;
		goto error_exit;
	}

	/*
	 * If write would extend past end of section data, enlarge section
	 */
	sizeRequired = req_lib_ckpt_sectionwrite->dataOffset + req_lib_ckpt_sectionwrite->dataSize;
	if (sizeRequired > ckptCheckpointSection->sectionDescriptor.sectionSize) {
printf ("reallocating data\n");
		sectionData = realloc (ckptCheckpointSection->sectionData, sizeRequired);
		if (sectionData == 0) {
			error = SA_ERR_NO_MEMORY;
			goto error_exit;
		}

		/*
		 * Install new section data
		 */
		ckptCheckpointSection->sectionData = sectionData;
		ckptCheckpointSection->sectionDescriptor.sectionSize = sizeRequired;
	}

	/*
	 * Write checkpoint section to section data
	 */
	if (req_lib_ckpt_sectionwrite->dataSize > 0) {
		char *sd;
		int *val;
		val = ckptCheckpointSection->sectionData;
		sd = (char *)ckptCheckpointSection->sectionData;
		memcpy (&sd[req_lib_ckpt_sectionwrite->dataOffset],
			((char *)req_exec_ckpt_sectionwrite) + sizeof (struct req_exec_ckpt_sectionwrite) + 
				req_lib_ckpt_sectionwrite->idLen,
			req_lib_ckpt_sectionwrite->dataSize);
	}
	/*
	 * Write write response to CKPT library
	 */
error_exit:
	if (req_exec_ckpt_sectionwrite->source.in_addr.s_addr == this_ip.sin_addr.s_addr) {
		res_lib_ckpt_sectionwrite.header.magic = MESSAGE_MAGIC;
		res_lib_ckpt_sectionwrite.header.size = sizeof (struct res_lib_ckpt_sectionwrite);
		res_lib_ckpt_sectionwrite.header.id = MESSAGE_RES_CKPT_CHECKPOINT_SECTIONWRITE;
		res_lib_ckpt_sectionwrite.error = error;

		libais_send_response (req_exec_ckpt_sectionwrite->source.conn_info,
			&res_lib_ckpt_sectionwrite,
			sizeof (struct res_lib_ckpt_sectionwrite));
	}

	return (0);
}

static int message_handler_req_exec_ckpt_sectionoverwrite (void *message, struct in_addr source_addr) {
	struct req_exec_ckpt_sectionoverwrite *req_exec_ckpt_sectionoverwrite = (struct req_exec_ckpt_sectionoverwrite *)message;
	struct req_lib_ckpt_sectionoverwrite *req_lib_ckpt_sectionoverwrite = (struct req_lib_ckpt_sectionoverwrite *)&req_exec_ckpt_sectionoverwrite->req_lib_ckpt_sectionoverwrite;
	struct res_lib_ckpt_sectionoverwrite res_lib_ckpt_sectionoverwrite;
	struct saCkptCheckpoint *ckptCheckpoint;
	struct saCkptCheckpointSection *ckptCheckpointSection;
	void *sectionData;
	SaErrorT error = SA_OK;

	log_printf (LOG_LEVEL_DEBUG, "Executive request to section overwrite.\n");
	ckptCheckpoint = findCheckpoint (&req_exec_ckpt_sectionoverwrite->checkpointName);
	if (ckptCheckpoint == 0) {
		error = SA_ERR_NOT_EXIST;
		goto error_exit;
	}

	/*
	 * Find checkpoint section to be overwritten
	 */
	ckptCheckpointSection = findCheckpointSection (ckptCheckpoint,
		((char *)req_lib_ckpt_sectionoverwrite) +
			sizeof (struct req_lib_ckpt_sectionoverwrite),
		req_lib_ckpt_sectionoverwrite->idLen);
	if (ckptCheckpointSection == 0) {
		error = SA_ERR_NOT_EXIST;
		goto error_exit;
	}

	/*
	 * Allocate checkpoint section data
	 */
	sectionData = malloc (req_lib_ckpt_sectionoverwrite->dataSize);
	if (sectionData == 0) {
		error = SA_ERR_NO_MEMORY;
		goto error_exit;
	}

	memcpy (sectionData,
		((char *)req_lib_ckpt_sectionoverwrite) +
			sizeof (struct req_lib_ckpt_sectionoverwrite) +
			req_lib_ckpt_sectionoverwrite->idLen,
		req_lib_ckpt_sectionoverwrite->dataSize);

	/*
	 * release old checkpoint section data
	 */
	free (ckptCheckpointSection->sectionData);

	/*
	 * Install overwritten checkpoint section data
	 */
	ckptCheckpointSection->sectionDescriptor.sectionSize = req_lib_ckpt_sectionoverwrite->dataSize;
	ckptCheckpointSection->sectionDescriptor.sectionState = SA_CKPT_SECTION_VALID;
	ckptCheckpointSection->sectionDescriptor.lastUpdate = 0; // TODO current time
	ckptCheckpointSection->sectionData = sectionData;

	/*
	 * return result to CKPT library
	 */
error_exit:
	if (req_exec_ckpt_sectionoverwrite->source.in_addr.s_addr == this_ip.sin_addr.s_addr) {
		res_lib_ckpt_sectionoverwrite.header.magic = MESSAGE_MAGIC;
		res_lib_ckpt_sectionoverwrite.header.size = sizeof (struct res_lib_ckpt_sectionoverwrite);
		res_lib_ckpt_sectionoverwrite.header.id = MESSAGE_RES_CKPT_CHECKPOINT_SECTIONOVERWRITE;
		res_lib_ckpt_sectionoverwrite.error = error;

		libais_send_response (req_exec_ckpt_sectionoverwrite->source.conn_info,
			&res_lib_ckpt_sectionoverwrite,
			sizeof (struct res_lib_ckpt_sectionoverwrite));
	}
	return (0);
}
static int message_handler_req_exec_ckpt_sectionread (void *message, struct in_addr source_addr) {
	struct req_exec_ckpt_sectionread *req_exec_ckpt_sectionread = (struct req_exec_ckpt_sectionread *)message;
	struct req_lib_ckpt_sectionread *req_lib_ckpt_sectionread = (struct req_lib_ckpt_sectionread *)&req_exec_ckpt_sectionread->req_lib_ckpt_sectionread;
	struct res_lib_ckpt_sectionread res_lib_ckpt_sectionread;
	struct saCkptCheckpoint *ckptCheckpoint;
	struct saCkptCheckpointSection *ckptCheckpointSection = 0;
	int sectionSize = 0;
	SaErrorT error = SA_OK;

	log_printf (LOG_LEVEL_DEBUG, "Executive request for section read.\n");

	ckptCheckpoint = findCheckpoint (&req_exec_ckpt_sectionread->checkpointName);
	if (ckptCheckpoint == 0) {
		error = SA_ERR_SYSTEM; // TODO find the right error for this
		goto error_exit;
	}

	/*
	 * Find checkpoint section to be read
	 */
	ckptCheckpointSection = findCheckpointSection (ckptCheckpoint,
		((char *)req_lib_ckpt_sectionread) +
			sizeof (struct req_lib_ckpt_sectionread),
		req_lib_ckpt_sectionread->idLen);
	if (ckptCheckpointSection == 0) {
		error = SA_ERR_NOT_EXIST;
		goto error_exit;
	}

	/*
	 * Determine the section size
	 */
	sectionSize = ckptCheckpointSection->sectionDescriptor.sectionSize -
		req_lib_ckpt_sectionread->dataOffset;

	/*
	 * If the library has less space available then can be sent from the
	 * section, reduce bytes sent to library to max requested
	 */
	if (sectionSize > req_lib_ckpt_sectionread->dataSize) {
		sectionSize = req_lib_ckpt_sectionread->dataSize;
	}

	/*
	 * If dataOffset is past end of data, return INVALID PARAM
	 */
	if (req_lib_ckpt_sectionread->dataOffset > sectionSize) {
		sectionSize = 0;
		error = SA_ERR_INVALID_PARAM;
		goto error_exit;
	}

	/*
	 * Write read response to CKPT library
	 */
error_exit:
	if (req_exec_ckpt_sectionread->source.in_addr.s_addr == this_ip.sin_addr.s_addr) {
		res_lib_ckpt_sectionread.header.magic = MESSAGE_MAGIC;
		res_lib_ckpt_sectionread.header.size = sizeof (struct res_lib_ckpt_sectionread) + sectionSize;
		res_lib_ckpt_sectionread.header.id = MESSAGE_RES_CKPT_CHECKPOINT_SECTIONREAD;
		res_lib_ckpt_sectionread.error = error;
	
		libais_send_response (req_exec_ckpt_sectionread->source.conn_info,
			&res_lib_ckpt_sectionread,
			sizeof (struct res_lib_ckpt_sectionread));

		/*
		 * Write checkpoint to CKPT library section if section has data
		 */
		if (sectionSize) {
			char *sd;
			sd = (char *)ckptCheckpointSection->sectionData;
			libais_send_response (req_exec_ckpt_sectionread->source.conn_info,
				&sd[req_lib_ckpt_sectionread->dataOffset],
				sectionSize);
		}
	}
	return (0);
}

static int message_handler_req_lib_ckpt_init (struct conn_info *conn_info, void *message)
{
	struct res_lib_init res_lib_init;
	SaErrorT error = SA_ERR_SECURITY;

	log_printf (LOG_LEVEL_DEBUG, "Got request to initialize CKPT.\n");

	if (conn_info->authenticated) {
		conn_info->service = SOCKET_SERVICE_CKPT;
		error = SA_OK;
	}

	res_lib_init.header.magic = MESSAGE_MAGIC;
	res_lib_init.header.size = sizeof (struct res_lib_init);
	res_lib_init.header.id = MESSAGE_RES_INIT;
	res_lib_init.error = error;

	libais_send_response (conn_info, &res_lib_init, sizeof (res_lib_init));

	if (conn_info->authenticated) {
		return (0);
	}
	return (-1);
}

static int message_handler_req_lib_ckpt_checkpoint_init (struct conn_info *conn_info, void *message)
{
	struct res_lib_init res_lib_init;
	SaErrorT error = SA_ERR_SECURITY;

	log_printf (LOG_LEVEL_DEBUG, "Got request to initialize CKPT checkpoint.\n");

	if (conn_info->authenticated) {
    	conn_info->service = SOCKET_SERVICE_CKPT_CHECKPOINT;
		conn_info->ais_ci.u.libckpt_ci.checkpoint = 0;
		conn_info->ais_ci.u.libckpt_ci.checkpointOpenFlags = 0;
		error = SA_OK;
	}

	res_lib_init.header.magic = MESSAGE_MAGIC;
	res_lib_init.header.size = sizeof (struct res_lib_init);
	res_lib_init.header.id = MESSAGE_RES_INIT;
	res_lib_init.error = error;

	libais_send_response (conn_info, &res_lib_init, sizeof (res_lib_init));

	if (conn_info->authenticated) {
		return (0);
	}
	return (-1);
}

static int message_handler_req_lib_ckpt_sectioniterator_init (struct conn_info *conn_info, void *message)
{
	struct res_lib_init res_lib_init;
	SaErrorT error = SA_ERR_SECURITY;

	log_printf (LOG_LEVEL_DEBUG, "Got request to initialize CKPT section iterator.\n");

	if (conn_info->authenticated) {
		conn_info->service = SOCKET_SERVICE_CKPT_SECTIONITERATOR;
		list_init (&conn_info->ais_ci.u.libckpt_ci.sectionIterator.list);
		conn_info->ais_ci.u.libckpt_ci.sectionIterator.sectionIteratorEntries = 0;
		conn_info->ais_ci.u.libckpt_ci.sectionIterator.iteratorCount = 0;
		conn_info->ais_ci.u.libckpt_ci.sectionIterator.iteratorPos = 0;
		list_add (&conn_info->ais_ci.u.libckpt_ci.sectionIterator.list,
			&checkpointIteratorListHead);
		error = SA_OK;
	}

	res_lib_init.header.magic = MESSAGE_MAGIC;
	res_lib_init.header.size = sizeof (struct res_lib_init);
	res_lib_init.header.id = MESSAGE_RES_INIT;
	res_lib_init.error = error;

	libais_send_response (conn_info, &res_lib_init, sizeof (res_lib_init));

	if (conn_info->authenticated) {
		return (0);
	}
	return (-1);
}

static int message_handler_req_lib_ckpt_checkpointopen (struct conn_info *conn_info, void *message)
{
	struct req_lib_ckpt_checkpointopen *req_lib_ckpt_checkpointopen = (struct req_lib_ckpt_checkpointopen *)message;
	struct req_exec_ckpt_checkpointopen req_exec_ckpt_checkpointopen;
	struct iovec iovecs[2];
	int result;

	log_printf (LOG_LEVEL_DEBUG, "Library request to open checkpoint.\n");
	req_exec_ckpt_checkpointopen.header.magic = MESSAGE_MAGIC;
	req_exec_ckpt_checkpointopen.header.size =
		sizeof (struct req_exec_ckpt_checkpointopen);
	req_exec_ckpt_checkpointopen.header.id = MESSAGE_REQ_EXEC_CKPT_CHECKPOINTOPEN;

	req_exec_ckpt_checkpointopen.source.conn_info = conn_info;
	req_exec_ckpt_checkpointopen.source.in_addr.s_addr = this_ip.sin_addr.s_addr;

	memcpy (&req_exec_ckpt_checkpointopen.req_lib_ckpt_checkpointopen,
		req_lib_ckpt_checkpointopen,
		sizeof (struct req_lib_ckpt_checkpointopen));

	iovecs[0].iov_base = (char *)&req_exec_ckpt_checkpointopen;
	iovecs[0].iov_len = sizeof (req_exec_ckpt_checkpointopen);

	result = gmi_mcast (&aisexec_groupname, iovecs, 1, GMI_PRIO_MED);

	return (0);
}

static int message_handler_req_lib_ckpt_checkpointopenasync (struct conn_info *conn_info, void *message)
{
	return (0);
}


static int message_handler_req_lib_ckpt_checkpointunlink (struct conn_info *conn_info, void *message)
{
	struct req_lib_ckpt_checkpointunlink *req_lib_ckpt_checkpointunlink = (struct req_lib_ckpt_checkpointunlink *)message;
	struct req_exec_ckpt_checkpointunlink req_exec_ckpt_checkpointunlink;
	struct iovec iovecs[2];
	int result;

	req_exec_ckpt_checkpointunlink.header.magic = MESSAGE_MAGIC;
	req_exec_ckpt_checkpointunlink.header.size =
		sizeof (struct req_exec_ckpt_checkpointunlink);
	req_exec_ckpt_checkpointunlink.header.id = MESSAGE_REQ_EXEC_CKPT_CHECKPOINTUNLINK;

	req_exec_ckpt_checkpointunlink.source.conn_info = conn_info;
	req_exec_ckpt_checkpointunlink.source.in_addr.s_addr = this_ip.sin_addr.s_addr;

	memcpy (&req_exec_ckpt_checkpointunlink.req_lib_ckpt_checkpointunlink,
		req_lib_ckpt_checkpointunlink,
		sizeof (struct req_lib_ckpt_checkpointunlink));

	iovecs[0].iov_base = (char *)&req_exec_ckpt_checkpointunlink;
	iovecs[0].iov_len = sizeof (req_exec_ckpt_checkpointunlink);

	result = gmi_mcast (&aisexec_groupname, iovecs, 1, GMI_PRIO_MED);

	return (0);
}

static int message_handler_req_lib_ckpt_checkpointretentiondurationset (struct conn_info *conn_info, void *message)
{
	struct req_lib_ckpt_checkpointretentiondurationset *req_lib_ckpt_checkpointretentiondurationset = (struct req_lib_ckpt_checkpointretentiondurationset *)message;
	struct req_exec_ckpt_checkpointretentiondurationset req_exec_ckpt_checkpointretentiondurationset;
	struct iovec iovecs[2];

	log_printf (LOG_LEVEL_DEBUG, "DURATION SET FROM API fd %d\n", conn_info);
	req_exec_ckpt_checkpointretentiondurationset.header.magic = MESSAGE_MAGIC;
	req_exec_ckpt_checkpointretentiondurationset.header.id = MESSAGE_REQ_EXEC_CKPT_CHECKPOINTRETENTIONDURATIONSET;
	req_exec_ckpt_checkpointretentiondurationset.header.size = sizeof (struct req_exec_ckpt_checkpointretentiondurationset);
	memcpy (&req_exec_ckpt_checkpointretentiondurationset.checkpointName,
		&conn_info->ais_ci.u.libckpt_ci.checkpoint->name,
		sizeof (SaNameT));
	req_exec_ckpt_checkpointretentiondurationset.retentionDuration = req_lib_ckpt_checkpointretentiondurationset->retentionDuration;

	iovecs[0].iov_base = (char *)&req_exec_ckpt_checkpointretentiondurationset;
	iovecs[0].iov_len = sizeof (req_exec_ckpt_checkpointretentiondurationset);

	gmi_mcast (&aisexec_groupname, iovecs, 1, GMI_PRIO_MED);

	return (0);
}

static int message_handler_req_lib_ckpt_activecheckpointset (struct conn_info *conn_info, void *message)
{
	return (0);
}

static int message_handler_req_lib_ckpt_checkpointstatusget (struct conn_info *conn_info, void *message)
{
	struct req_lib_ckpt_checkpointstatusget *req_lib_ckpt_checkpointstatusget = (struct req_lib_ckpt_checkpointstatusget *)message;
	struct res_lib_ckpt_checkpointstatusget res_lib_ckpt_checkpointstatusget;
	struct saCkptCheckpoint *checkpoint;
	int memoryUsed = 0;
	int numberOfSections = 0;
	struct list_head *checkpointSectionList;
	struct saCkptCheckpointSection *checkpointSection;

	req_lib_ckpt_checkpointstatusget = 0; /* The request info isn't used */
	log_printf (LOG_LEVEL_DEBUG, "in status get\n");

	/*
	 * Count memory used by checkpoint sections
	 */
	checkpoint = conn_info->ais_ci.u.libckpt_ci.checkpoint;
	for (checkpointSectionList = checkpoint->checkpointSectionsListHead.next;
		checkpointSectionList != &checkpoint->checkpointSectionsListHead;
		checkpointSectionList = checkpointSectionList->next) {

		checkpointSection = list_entry (checkpointSectionList,
			struct saCkptCheckpointSection, list);

		memoryUsed += checkpointSection->sectionDescriptor.sectionSize;
		numberOfSections += 1;
	}

	/*
	 * Build checkpoint status get response
	 */
	res_lib_ckpt_checkpointstatusget.header.magic = MESSAGE_MAGIC;
	res_lib_ckpt_checkpointstatusget.header.size = sizeof (struct res_lib_ckpt_checkpointstatusget);
	res_lib_ckpt_checkpointstatusget.header.id = MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTSTATUSGET;

	memcpy (&res_lib_ckpt_checkpointstatusget.checkpointStatus.checkpointCreationAttributes,
		&checkpoint->checkpointCreationAttributes,
		sizeof (SaCkptCheckpointCreationAttributesT));
	res_lib_ckpt_checkpointstatusget.checkpointStatus.numberOfSections = numberOfSections;
	res_lib_ckpt_checkpointstatusget.checkpointStatus.memoryUsed = memoryUsed;

	log_printf (LOG_LEVEL_DEBUG, "before sending message\n");
	libais_send_response (conn_info, &res_lib_ckpt_checkpointstatusget,
		sizeof (struct res_lib_ckpt_checkpointstatusget));
	return (0);
}

static int message_handler_req_lib_ckpt_sectioncreate (struct conn_info *conn_info, void *message)
{
	struct req_lib_ckpt_sectioncreate *req_lib_ckpt_sectioncreate = (struct req_lib_ckpt_sectioncreate *)message;
	struct req_exec_ckpt_sectioncreate req_exec_ckpt_sectioncreate;
	struct res_lib_ckpt_sectioncreate res_lib_ckpt_sectioncreate;
	struct iovec iovecs[2];

	log_printf (LOG_LEVEL_DEBUG, "Section create from API fd %d\n", conn_info);
	/*
	 * Determine if checkpoint is opened in write mode If not, send error to api
	 */
	if ((conn_info->ais_ci.u.libckpt_ci.checkpointOpenFlags & SA_CKPT_CHECKPOINT_WRITE) == 0) {
		res_lib_ckpt_sectioncreate.header.magic = MESSAGE_MAGIC;
		res_lib_ckpt_sectioncreate.header.size = sizeof (struct res_lib_ckpt_sectioncreate);
		res_lib_ckpt_sectioncreate.header.id = MESSAGE_RES_CKPT_CHECKPOINT_SECTIONCREATE;
		res_lib_ckpt_sectioncreate.error = SA_ERR_ACCESS;

		libais_send_response (conn_info, &res_lib_ckpt_sectioncreate,
			sizeof (struct res_lib_ckpt_sectioncreate));
		return (0);
	}

	/*
	 * checkpoint opened is writeable mode so send message to cluster
	 */
	req_exec_ckpt_sectioncreate.header.magic = MESSAGE_MAGIC;
	req_exec_ckpt_sectioncreate.header.id = MESSAGE_REQ_EXEC_CKPT_SECTIONCREATE;
	req_exec_ckpt_sectioncreate.header.size = sizeof (struct req_exec_ckpt_sectioncreate);

	memcpy (&req_exec_ckpt_sectioncreate.req_lib_ckpt_sectioncreate,
		req_lib_ckpt_sectioncreate,
		sizeof (struct req_lib_ckpt_sectioncreate));

	memcpy (&req_exec_ckpt_sectioncreate.checkpointName,
		&conn_info->ais_ci.u.libckpt_ci.checkpoint->name,
		sizeof (SaNameT));

	req_exec_ckpt_sectioncreate.source.conn_info = conn_info;
	req_exec_ckpt_sectioncreate.source.in_addr.s_addr = this_ip.sin_addr.s_addr;

	iovecs[0].iov_base = (char *)&req_exec_ckpt_sectioncreate;
	iovecs[0].iov_len = sizeof (req_exec_ckpt_sectioncreate);
	/*
	 * Send section name and initial data in message
	 */
	iovecs[1].iov_base = ((char *)req_lib_ckpt_sectioncreate) + sizeof (struct req_lib_ckpt_sectioncreate);
	iovecs[1].iov_len = req_lib_ckpt_sectioncreate->header.size - sizeof (struct req_lib_ckpt_sectioncreate);

#ifdef DEBUG
printf ("LIBRARY SECTIONCREATE string is %s len is %d\n", (unsigned char *)iovecs[1].iov_base,
	iovecs[1].iov_len);
printf ("|\n");
{ int i;
	char *abc = iovecs[1].iov_base;
for (i = 0; i < 14;i++) {

	printf ("%c ", abc[i]);
}
}
printf ("|\n");
#endif
	if (iovecs[1].iov_len > 0) {
		log_printf (LOG_LEVEL_DEBUG, "IOV_BASE is %s\n", iovecs[1].iov_base);
		gmi_mcast (&aisexec_groupname, iovecs, 2, GMI_PRIO_MED);
	} else {
		gmi_mcast (&aisexec_groupname, iovecs, 1, GMI_PRIO_MED);
	}

	return (0);
}

static int message_handler_req_lib_ckpt_sectiondelete (struct conn_info *conn_info, void *message)
{
	struct req_lib_ckpt_sectiondelete *req_lib_ckpt_sectiondelete = (struct req_lib_ckpt_sectiondelete *)message;
	struct req_exec_ckpt_sectiondelete req_exec_ckpt_sectiondelete;
	struct iovec iovecs[2];

	log_printf (LOG_LEVEL_DEBUG, "section delete from API fd %d\n", conn_info);

	req_exec_ckpt_sectiondelete.header.magic = MESSAGE_MAGIC;
	req_exec_ckpt_sectiondelete.header.id = MESSAGE_REQ_EXEC_CKPT_SECTIONDELETE;
	req_exec_ckpt_sectiondelete.header.size = sizeof (struct req_exec_ckpt_sectiondelete);

	memcpy (&req_exec_ckpt_sectiondelete.checkpointName,
		&conn_info->ais_ci.u.libckpt_ci.checkpoint->name,
		sizeof (SaNameT));

	memcpy (&req_exec_ckpt_sectiondelete.req_lib_ckpt_sectiondelete,
		req_lib_ckpt_sectiondelete,
		sizeof (struct req_lib_ckpt_sectiondelete));

	req_exec_ckpt_sectiondelete.source.conn_info = conn_info;
	req_exec_ckpt_sectiondelete.source.in_addr.s_addr = this_ip.sin_addr.s_addr;

	iovecs[0].iov_base = (char *)&req_exec_ckpt_sectiondelete;
	iovecs[0].iov_len = sizeof (req_exec_ckpt_sectiondelete);

	/*
	 * Send section name
	 */
	iovecs[1].iov_base = ((char *)req_lib_ckpt_sectiondelete) + sizeof (struct req_lib_ckpt_sectiondelete);
	iovecs[1].iov_len = req_lib_ckpt_sectiondelete->header.size - sizeof (struct req_lib_ckpt_sectiondelete);

	if (iovecs[1].iov_len > 0) {
		log_printf (LOG_LEVEL_DEBUG, "IOV_BASE is %s\n", iovecs[1].iov_base);
		gmi_mcast (&aisexec_groupname, iovecs, 2, GMI_PRIO_MED);
	} else {
		gmi_mcast (&aisexec_groupname, iovecs, 1, GMI_PRIO_MED);
	}

	return (0);
}

static int message_handler_req_lib_ckpt_sectionexpirationtimeset (struct conn_info *conn_info, void *message)
{
	struct req_lib_ckpt_sectionexpirationtimeset *req_lib_ckpt_sectionexpirationtimeset = (struct req_lib_ckpt_sectionexpirationtimeset *)message;
	struct req_exec_ckpt_sectionexpirationtimeset req_exec_ckpt_sectionexpirationtimeset;
	struct iovec iovecs[2];

	log_printf (LOG_LEVEL_DEBUG, "section expiration time set fd=%d\n", conn_info);
	req_exec_ckpt_sectionexpirationtimeset.header.magic = MESSAGE_MAGIC;
	req_exec_ckpt_sectionexpirationtimeset.header.id = MESSAGE_REQ_EXEC_CKPT_SECTIONEXPIRATIONTIMESET;
	req_exec_ckpt_sectionexpirationtimeset.header.size = sizeof (struct req_exec_ckpt_sectionexpirationtimeset);

	memcpy (&req_exec_ckpt_sectionexpirationtimeset.checkpointName,
		&conn_info->ais_ci.u.libckpt_ci.checkpoint->name,
		sizeof (SaNameT));

	memcpy (&req_exec_ckpt_sectionexpirationtimeset.req_lib_ckpt_sectionexpirationtimeset,
		req_lib_ckpt_sectionexpirationtimeset,
		sizeof (struct req_lib_ckpt_sectionexpirationtimeset));

	req_exec_ckpt_sectionexpirationtimeset.source.conn_info = conn_info;
	req_exec_ckpt_sectionexpirationtimeset.source.in_addr.s_addr = this_ip.sin_addr.s_addr;

	iovecs[0].iov_base = (char *)&req_exec_ckpt_sectionexpirationtimeset;
	iovecs[0].iov_len = sizeof (req_exec_ckpt_sectionexpirationtimeset);

	/*
	 * Send section name
	 */
	iovecs[1].iov_base = ((char *)req_lib_ckpt_sectionexpirationtimeset) + sizeof (struct req_lib_ckpt_sectionexpirationtimeset);
	iovecs[1].iov_len = req_lib_ckpt_sectionexpirationtimeset->header.size - sizeof (struct req_lib_ckpt_sectionexpirationtimeset);

	if (iovecs[1].iov_len > 0) {
		log_printf (LOG_LEVEL_DEBUG, "IOV_BASE is %s\n", iovecs[1].iov_base);
		gmi_mcast (&aisexec_groupname, iovecs, 2, GMI_PRIO_MED);
	} else {
		gmi_mcast (&aisexec_groupname, iovecs, 1, GMI_PRIO_MED);
	}

	return (0);
}

int write_inv = 0;
static int message_handler_req_lib_ckpt_sectionwrite (struct conn_info *conn_info, void *message)
{
	struct req_lib_ckpt_sectionwrite *req_lib_ckpt_sectionwrite = (struct req_lib_ckpt_sectionwrite *)message;
	struct req_exec_ckpt_sectionwrite req_exec_ckpt_sectionwrite;
	struct res_lib_ckpt_sectionwrite res_lib_ckpt_sectionwrite;
	struct iovec iovecs[2];

	log_printf (LOG_LEVEL_DEBUG, "Section write from API fd %d\n", conn_info);
// UNDO printf ("section write %d\n", write_inv++);
	/*
	 * Determine if checkpoint is opened in write mode If not, send error to api
	 */
	if ((conn_info->ais_ci.u.libckpt_ci.checkpointOpenFlags & SA_CKPT_CHECKPOINT_WRITE) == 0) {
		res_lib_ckpt_sectionwrite.header.magic = MESSAGE_MAGIC;
		res_lib_ckpt_sectionwrite.header.size = sizeof (struct res_lib_ckpt_sectionwrite);
		res_lib_ckpt_sectionwrite.header.id = MESSAGE_RES_CKPT_CHECKPOINT_SECTIONWRITE;
		res_lib_ckpt_sectionwrite.error = SA_ERR_ACCESS;

		libais_send_response (conn_info, &res_lib_ckpt_sectionwrite,
			sizeof (struct res_lib_ckpt_sectionwrite));
		return (0);
	}

	/*
	 * checkpoint opened is writeable mode so send message to cluster
	 */
	req_exec_ckpt_sectionwrite.header.magic = MESSAGE_MAGIC;
	req_exec_ckpt_sectionwrite.header.id = MESSAGE_REQ_EXEC_CKPT_SECTIONWRITE;
	req_exec_ckpt_sectionwrite.header.size = sizeof (struct req_exec_ckpt_sectionwrite);

	memcpy (&req_exec_ckpt_sectionwrite.req_lib_ckpt_sectionwrite,
		req_lib_ckpt_sectionwrite,
		sizeof (struct req_lib_ckpt_sectionwrite));

	memcpy (&req_exec_ckpt_sectionwrite.checkpointName,
		&conn_info->ais_ci.u.libckpt_ci.checkpoint->name,
		sizeof (SaNameT));

	req_exec_ckpt_sectionwrite.source.conn_info = conn_info;
	req_exec_ckpt_sectionwrite.source.in_addr.s_addr = this_ip.sin_addr.s_addr;

	iovecs[0].iov_base = (char *)&req_exec_ckpt_sectionwrite;
	iovecs[0].iov_len = sizeof (req_exec_ckpt_sectionwrite);
	/*
	 * Send section name and data to write in message
	 */
	iovecs[1].iov_base = ((char *)req_lib_ckpt_sectionwrite) + sizeof (struct req_lib_ckpt_sectionwrite);
	iovecs[1].iov_len = req_lib_ckpt_sectionwrite->header.size - sizeof (struct req_lib_ckpt_sectionwrite);

//printf ("LIB writing checkpoint section is %s\n", ((char *)req_lib_ckpt_sectionwrite) + sizeof (struct req_lib_ckpt_sectionwrite));
	if (iovecs[1].iov_len > 0) {
		gmi_mcast (&aisexec_groupname, iovecs, 2, GMI_PRIO_MED);
	} else {
		gmi_mcast (&aisexec_groupname, iovecs, 1, GMI_PRIO_MED);
	}

	return (0);
}

static int message_handler_req_lib_ckpt_sectionoverwrite (struct conn_info *conn_info, void *message)
{
	struct req_lib_ckpt_sectionoverwrite *req_lib_ckpt_sectionoverwrite = (struct req_lib_ckpt_sectionoverwrite *)message;
	struct req_exec_ckpt_sectionoverwrite req_exec_ckpt_sectionoverwrite;
	struct res_lib_ckpt_sectionoverwrite res_lib_ckpt_sectionoverwrite;
	struct iovec iovecs[2];

	log_printf (LOG_LEVEL_DEBUG, "Section overwrite from API fd %d\n", conn_info);
	/*
	 * Determine if checkpoint is opened in write mode If not, send error to api
	 */
	if ((conn_info->ais_ci.u.libckpt_ci.checkpointOpenFlags & SA_CKPT_CHECKPOINT_WRITE) == 0) {
		res_lib_ckpt_sectionoverwrite.header.magic = MESSAGE_MAGIC;
		res_lib_ckpt_sectionoverwrite.header.size = sizeof (struct res_lib_ckpt_sectionoverwrite);
		res_lib_ckpt_sectionoverwrite.header.id = MESSAGE_RES_CKPT_CHECKPOINT_SECTIONOVERWRITE;
		res_lib_ckpt_sectionoverwrite.error = SA_ERR_ACCESS;

		libais_send_response (conn_info, &res_lib_ckpt_sectionoverwrite,
			sizeof (struct res_lib_ckpt_sectionoverwrite));
		return (0);
	}

	/*
	 * checkpoint opened is writeable mode so send message to cluster
	 */
	req_exec_ckpt_sectionoverwrite.header.magic = MESSAGE_MAGIC;
	req_exec_ckpt_sectionoverwrite.header.id = MESSAGE_REQ_EXEC_CKPT_SECTIONOVERWRITE;
	req_exec_ckpt_sectionoverwrite.header.size = sizeof (struct req_exec_ckpt_sectionoverwrite);

	memcpy (&req_exec_ckpt_sectionoverwrite.req_lib_ckpt_sectionoverwrite,
		req_lib_ckpt_sectionoverwrite,
		sizeof (struct req_lib_ckpt_sectionoverwrite));

	memcpy (&req_exec_ckpt_sectionoverwrite.checkpointName,
		&conn_info->ais_ci.u.libckpt_ci.checkpoint->name,
		sizeof (SaNameT));

	req_exec_ckpt_sectionoverwrite.source.conn_info = conn_info;
	req_exec_ckpt_sectionoverwrite.source.in_addr.s_addr = this_ip.sin_addr.s_addr;

	iovecs[0].iov_base = (char *)&req_exec_ckpt_sectionoverwrite;
	iovecs[0].iov_len = sizeof (req_exec_ckpt_sectionoverwrite);
	/*
	 * Send section name and data to overwrite in message
	 */
	iovecs[1].iov_base = ((char *)req_lib_ckpt_sectionoverwrite) + sizeof (struct req_lib_ckpt_sectionoverwrite);
	iovecs[1].iov_len = req_lib_ckpt_sectionoverwrite->header.size - sizeof (struct req_lib_ckpt_sectionoverwrite);

	if (iovecs[1].iov_len > 0) {
		gmi_mcast (&aisexec_groupname, iovecs, 2, GMI_PRIO_MED);
	} else {
		gmi_mcast (&aisexec_groupname, iovecs, 1, GMI_PRIO_MED);
	}

	return (0);
}

static int message_handler_req_lib_ckpt_sectionread (struct conn_info *conn_info, void *message)
{
	struct req_lib_ckpt_sectionread *req_lib_ckpt_sectionread = (struct req_lib_ckpt_sectionread *)message;
	struct req_exec_ckpt_sectionread req_exec_ckpt_sectionread;
	struct res_lib_ckpt_sectionread res_lib_ckpt_sectionread;
	struct iovec iovecs[2];

	log_printf (LOG_LEVEL_DEBUG, "Section overwrite from API fd %d\n", conn_info);
	/*
	 * Determine if checkpoint is opened in write mode If not, send error to api
	 */
	if ((conn_info->ais_ci.u.libckpt_ci.checkpointOpenFlags & SA_CKPT_CHECKPOINT_READ) == 0) {
		res_lib_ckpt_sectionread.header.magic = MESSAGE_MAGIC;
		res_lib_ckpt_sectionread.header.size = sizeof (struct res_lib_ckpt_sectionread);
		res_lib_ckpt_sectionread.header.id = MESSAGE_RES_CKPT_CHECKPOINT_SECTIONREAD;
		res_lib_ckpt_sectionread.error = SA_ERR_ACCESS;

		libais_send_response (conn_info, &res_lib_ckpt_sectionread,
			sizeof (struct res_lib_ckpt_sectionread));
		return (0);
	}

	/*
	 * checkpoint opened is writeable mode so send message to cluster
	 */
	req_exec_ckpt_sectionread.header.magic = MESSAGE_MAGIC;
	req_exec_ckpt_sectionread.header.id = MESSAGE_REQ_EXEC_CKPT_SECTIONREAD;
	req_exec_ckpt_sectionread.header.size = sizeof (struct req_exec_ckpt_sectionread);

	memcpy (&req_exec_ckpt_sectionread.req_lib_ckpt_sectionread,
		req_lib_ckpt_sectionread,
		sizeof (struct req_lib_ckpt_sectionread));

	memcpy (&req_exec_ckpt_sectionread.checkpointName,
		&conn_info->ais_ci.u.libckpt_ci.checkpoint->name,
		sizeof (SaNameT));

	req_exec_ckpt_sectionread.source.conn_info = conn_info;
	req_exec_ckpt_sectionread.source.in_addr.s_addr = this_ip.sin_addr.s_addr;

	iovecs[0].iov_base = (char *)&req_exec_ckpt_sectionread;
	iovecs[0].iov_len = sizeof (req_exec_ckpt_sectionread);
	/*
	 * Send section name and data to overwrite in message
	 */
	iovecs[1].iov_base = ((char *)req_lib_ckpt_sectionread) + sizeof (struct req_lib_ckpt_sectionread);
	iovecs[1].iov_len = req_lib_ckpt_sectionread->header.size - sizeof (struct req_lib_ckpt_sectionread);

	if (iovecs[1].iov_len > 0) {
		gmi_mcast (&aisexec_groupname, iovecs, 2, GMI_PRIO_MED);
	} else {
		gmi_mcast (&aisexec_groupname, iovecs, 1, GMI_PRIO_MED);
	}

	return (0);
}

static int message_handler_req_lib_ckpt_checkpointsynchronize (struct conn_info *conn_info, void *message)
{
	return (0);
}

static int message_handler_req_lib_ckpt_checkpointsyncronizeasync (struct conn_info *conn_info, void *message)
{
	return (0);
}

static int message_handler_req_lib_ckpt_sectioniteratorinitialize (struct conn_info *conn_info, void *message)
{
	struct req_lib_ckpt_sectioniteratorinitialize *req_lib_ckpt_sectioniteratorinitialize = (struct req_lib_ckpt_sectioniteratorinitialize *)message;
	struct res_lib_ckpt_sectioniteratorinitialize res_lib_ckpt_sectioniteratorinitialize;
	struct saCkptCheckpoint *ckptCheckpoint;
	struct saCkptCheckpointSection *ckptCheckpointSection;
	struct saCkptSectionIteratorEntry *ckptSectionIteratorEntries;
	struct saCkptSectionIterator *ckptSectionIterator;
	struct list_head *checkpointSectionList;
	int addEntry = 0;
	int iteratorEntries = 0;
	SaErrorT error = SA_OK;

	log_printf (LOG_LEVEL_DEBUG, "section iterator initialize\n");
	ckptSectionIterator = &conn_info->ais_ci.u.libckpt_ci.sectionIterator;

	ckptCheckpoint = findCheckpoint (&req_lib_ckpt_sectioniteratorinitialize->checkpointName);
	if (ckptCheckpoint == 0) {
		error = SA_ERR_NOT_EXIST;
		goto error_exit;
	}

	/*
	 * Iterate list of checkpoint sections
	 */
	for (checkpointSectionList = ckptCheckpoint->checkpointSectionsListHead.next;
		checkpointSectionList != &ckptCheckpoint->checkpointSectionsListHead;
		checkpointSectionList = checkpointSectionList->next) {

		ckptCheckpointSection = list_entry (checkpointSectionList,
			struct saCkptCheckpointSection, list);

		addEntry = 1;

		/*
		 * Item should be added to iterator list
		 */
		if (addEntry) {
			iteratorEntries += 1;
			ckptSectionIteratorEntries =
				realloc (ckptSectionIterator->sectionIteratorEntries,
				sizeof (struct saCkptSectionIteratorEntry) * iteratorEntries);
			if (ckptSectionIteratorEntries == 0) {
				if (ckptSectionIterator->sectionIteratorEntries) {
					free (ckptSectionIterator->sectionIteratorEntries);
				}
				error = SA_ERR_NO_MEMORY;
				goto error_exit;
			}
			ckptSectionIteratorEntries[iteratorEntries - 1].active = 1;
			ckptSectionIteratorEntries[iteratorEntries - 1].checkpointSection = ckptCheckpointSection;
			ckptSectionIterator->sectionIteratorEntries = ckptSectionIteratorEntries;
		}
	}
	ckptSectionIterator->iteratorCount = iteratorEntries;

error_exit:
	res_lib_ckpt_sectioniteratorinitialize.header.magic = MESSAGE_MAGIC;
	res_lib_ckpt_sectioniteratorinitialize.header.size = sizeof (struct res_lib_ckpt_sectioniteratorinitialize);
	res_lib_ckpt_sectioniteratorinitialize.header.id = MESSAGE_RES_CKPT_SECTIONITERATOR_SECTIONITERATORINITIALIZE;
	res_lib_ckpt_sectioniteratorinitialize.error = error;

	libais_send_response (conn_info, &res_lib_ckpt_sectioniteratorinitialize,
		sizeof (struct res_lib_ckpt_sectioniteratorinitialize));

	return (0);
}

static int message_handler_req_lib_ckpt_sectioniteratornext (struct conn_info *conn_info, void *message)
{
	struct req_lib_ckpt_sectioniteratornext *req_lib_ckpt_sectioniteratornext = (struct req_lib_ckpt_sectioniteratornext *)message;
	struct res_lib_ckpt_sectioniteratornext res_lib_ckpt_sectioniteratornext;
	struct saCkptSectionIterator *ckptSectionIterator;
	SaErrorT error = SA_OK;
	int sectionIdSize = 0;
	int iteratorPos = 0;

	req_lib_ckpt_sectioniteratornext = 0; /* this variable not used */

	log_printf (LOG_LEVEL_DEBUG, "section iterator next\n");
	ckptSectionIterator = &conn_info->ais_ci.u.libckpt_ci.sectionIterator;

	/*
	 * Find active iterator entry
	 */
	for (;;) {
		/*
		 * No more sections in iterator
		 */
		if (ckptSectionIterator->iteratorPos + 1 >= ckptSectionIterator->iteratorCount) {
			error = SA_ERR_NOT_EXIST;
			goto error_exit;
		}

		/*
		 * active iterator entry
		 */
		if (ckptSectionIterator->sectionIteratorEntries[ckptSectionIterator->iteratorPos].active == 1) {
			break;
		}

		ckptSectionIterator->iteratorPos += 1;
	}

	/*
	 * Prepare response to API
	 */
	iteratorPos = ckptSectionIterator->iteratorPos;

	sectionIdSize = ckptSectionIterator->sectionIteratorEntries[iteratorPos].checkpointSection->sectionDescriptor.sectionId.idLen;

	memcpy (&res_lib_ckpt_sectioniteratornext.sectionDescriptor, 
		&ckptSectionIterator->sectionIteratorEntries[iteratorPos].checkpointSection->sectionDescriptor,
		sizeof (SaCkptSectionDescriptorT));

	/*
	 * Get to next iterator entry
	 */
	ckptSectionIterator->iteratorPos += 1;

error_exit:
	res_lib_ckpt_sectioniteratornext.header.magic = MESSAGE_MAGIC;
	res_lib_ckpt_sectioniteratornext.header.size = sizeof (struct res_lib_ckpt_sectioniteratornext) + sectionIdSize;
	res_lib_ckpt_sectioniteratornext.header.id = MESSAGE_RES_CKPT_SECTIONITERATOR_SECTIONITERATORNEXT;
	res_lib_ckpt_sectioniteratornext.error = error;

	libais_send_response (conn_info, &res_lib_ckpt_sectioniteratornext,
		sizeof (struct res_lib_ckpt_sectioniteratornext));

	libais_send_response (conn_info,
		ckptSectionIterator->sectionIteratorEntries[iteratorPos].checkpointSection->sectionDescriptor.sectionId.id,
		sectionIdSize);
	return (0);
}
