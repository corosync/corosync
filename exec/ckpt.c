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
#include <sys/time.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>

#include "../include/ais_types.h"
#include "../include/saCkpt.h"
#include "../include/ipc_ckpt.h"
#include "../include/list.h"
#include "../include/queue.h"
#include "aispoll.h"
#include "mempool.h"
#include "util.h"
#include "parse.h"
#include "main.h"
#include "totempg.h"

#define LOG_SERVICE LOG_SERVICE_CKPT
#include "print.h"

DECLARE_LIST_INIT(checkpointListHead);

DECLARE_LIST_INIT(checkpointIteratorListHead);

struct checkpoint_cleanup {
    struct list_head list;
    struct saCkptCheckpoint *checkpoint;
};

//TODO static totempg_recovery_plug_handle ckpt_checkpoint_recovery_plug_handle;

static int ckpt_exec_init_fn (void);

static int ckpt_exit_fn (struct conn_info *conn_info);

static int message_handler_req_lib_activatepoll (struct conn_info *, void *message);

static int message_handler_req_exec_ckpt_checkpointopen (void *message, struct in_addr source_addr, int endian_conversion_required);

static int message_handler_req_exec_ckpt_checkpointclose (void *message, struct in_addr source_addr, int endian_conversion_required);

static int message_handler_req_exec_ckpt_checkpointunlink (void *message, struct in_addr source_addr, int endian_conversion_required);

static int message_handler_req_exec_ckpt_checkpointretentiondurationset (void *message, struct in_addr source_addr, int endian_conversion_required);

static int message_handler_req_exec_ckpt_checkpointretentiondurationexpire (void *message, struct in_addr source_addr, int endian_conversion_required);

static int message_handler_req_exec_ckpt_sectioncreate (void *message, struct in_addr source_addr, int endian_conversion_required);

static int message_handler_req_exec_ckpt_sectiondelete (void *message, struct in_addr source_addr, int endian_conversion_required);

static int message_handler_req_exec_ckpt_sectionexpirationtimeset (void *message, struct in_addr source_addr, int endian_conversion_required);

static int message_handler_req_exec_ckpt_sectionwrite (void *message, struct in_addr source_addr, int endian_conversion_required);

static int message_handler_req_exec_ckpt_sectionoverwrite (void *message, struct in_addr source_addr, int endian_conversion_required);

static int message_handler_req_exec_ckpt_sectionread (void *message, struct in_addr source_addr, int endian_conversion_required);

static int message_handler_req_lib_ckpt_init (struct conn_info *conn_info, void *message);

static int message_handler_req_lib_ckpt_checkpointopen (struct conn_info *conn_info, void *message);

static int message_handler_req_lib_ckpt_checkpointopenasync (struct conn_info *conn_info, void *message);

static int message_handler_req_lib_ckpt_checkpointclose (struct conn_info *conn_info, void *message);

static int message_handler_req_lib_ckpt_checkpointunlink (struct conn_info *conn_info, void *message);

static int message_handler_req_lib_ckpt_checkpointretentiondurationset (struct conn_info *conn_info, void *message);

static int message_handler_req_lib_ckpt_activereplicaset (struct conn_info *conn_info, void *message);

static int message_handler_req_lib_ckpt_checkpointstatusget (struct conn_info *conn_info, void *message);

static int message_handler_req_lib_ckpt_sectioncreate (struct conn_info *conn_info, void *message);

static int message_handler_req_lib_ckpt_sectiondelete (struct conn_info *conn_info, void *message);

static int message_handler_req_lib_ckpt_sectionexpirationtimeset (struct conn_info *conn_info, void *message);

static int message_handler_req_lib_ckpt_sectionwrite (struct conn_info *conn_info, void *message);

static int message_handler_req_lib_ckpt_sectionoverwrite (struct conn_info *conn_info, void *message);

static int message_handler_req_lib_ckpt_sectionread (struct conn_info *conn_info, void *message);

static int message_handler_req_lib_ckpt_checkpointsynchronize (struct conn_info *conn_info, void *message);

static int message_handler_req_lib_ckpt_checkpointsynchronizeasync (struct conn_info *conn_info, void *message);

static int message_handler_req_lib_ckpt_sectioniteratorinitialize (struct conn_info *conn_info, void *message);
static int message_handler_req_lib_ckpt_sectioniteratornext (struct conn_info *conn_info, void *message);

static int ckpt_confchg_fn (
	enum totempg_configuration_type configuration_type,
	struct in_addr *member_list, void *member_list_private,
		int member_list_entries,
	struct in_addr *left_list, void *left_list_private,
		int left_list_entries,
	struct in_addr *joined_list, void *joined_list_private,
		int joined_list_entries) {

#ifdef TODO
	if (configuration_type == TOTEMPG_CONFIGURATION_REGULAR) {
		totempg_recovery_plug_unplug (ckpt_checkpoint_recovery_plug_handle);
	}
#endif

	return (0);
}

struct libais_handler ckpt_libais_handlers[] =
{
	{ /* 0 */
		.libais_handler_fn	= message_handler_req_lib_activatepoll,
		.response_size		= sizeof (struct res_lib_activatepoll),
		.response_id		= MESSAGE_RES_LIB_ACTIVATEPOLL,
	},
	{ /* 1 */
		.libais_handler_fn	= message_handler_req_lib_ckpt_checkpointopen,
		.response_size		= sizeof (struct res_lib_ckpt_checkpointopen),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTOPEN,
	},
	{ /* 2 */
		.libais_handler_fn	= message_handler_req_lib_ckpt_checkpointopenasync,
		.response_size		= sizeof (struct res_lib_ckpt_checkpointopenasync),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTOPENASYNC,
	},
	{ /* 3 */
		.libais_handler_fn	= message_handler_req_lib_ckpt_checkpointclose,
		.response_size		= sizeof (struct res_lib_ckpt_checkpointclose),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTCLOSE,
	},
	{ /* 4 */
		.libais_handler_fn	= message_handler_req_lib_ckpt_checkpointunlink,
		.response_size		= sizeof (struct res_lib_ckpt_checkpointunlink),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTUNLINK,
	},
	{ /* 5 */
		.libais_handler_fn	= message_handler_req_lib_ckpt_checkpointretentiondurationset,
		.response_size		= sizeof (struct res_lib_ckpt_checkpointretentiondurationset),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTRETENTIONDURATIONSET,
	},
	{ /* 6 */
		.libais_handler_fn	= message_handler_req_lib_ckpt_activereplicaset,
		.response_size		= sizeof (struct res_lib_ckpt_activereplicaset),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_ACTIVEREPLICASET,
	},
	{ /* 7 */
		.libais_handler_fn	= message_handler_req_lib_ckpt_checkpointstatusget,
		.response_size		= sizeof (struct res_lib_ckpt_checkpointstatusget),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTSTATUSGET,
	},
	{ /* 8 */
		.libais_handler_fn	= message_handler_req_lib_ckpt_sectioncreate,
		.response_size		= sizeof (struct res_lib_ckpt_sectioncreate),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_SECTIONCREATE,
	},
	{ /* 9 */
		.libais_handler_fn	= message_handler_req_lib_ckpt_sectiondelete,
		.response_size		= sizeof (struct res_lib_ckpt_sectiondelete),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_SECTIONDELETE,
	},
	{ /* 10 */
		.libais_handler_fn	= message_handler_req_lib_ckpt_sectionexpirationtimeset,
		.response_size		= sizeof (struct res_lib_ckpt_sectionexpirationtimeset),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_SECTIONEXPIRATIONTIMESET,
	},
	{ /* 11 */
		.libais_handler_fn	= message_handler_req_lib_ckpt_sectionwrite,
		.response_size		= sizeof (struct res_lib_ckpt_sectionwrite),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_SECTIONWRITE,
	},
	{ /* 12 */
		.libais_handler_fn	= message_handler_req_lib_ckpt_sectionoverwrite,
		.response_size		= sizeof (struct res_lib_ckpt_sectionoverwrite),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_SECTIONOVERWRITE,
	},
	{ /* 13 */
		.libais_handler_fn	= message_handler_req_lib_ckpt_sectionread,
		.response_size		= sizeof (struct res_lib_ckpt_sectionread),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_SECTIONREAD,
	},
	{ /* 14 */
		.libais_handler_fn	= message_handler_req_lib_ckpt_checkpointsynchronize,
		.response_size		= sizeof (struct res_lib_ckpt_checkpointsynchronize),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTSYNCHRONIZE,
	},
	{ /* 15 */
		.libais_handler_fn	= message_handler_req_lib_ckpt_checkpointsynchronizeasync,
		.response_size		= sizeof (struct res_lib_ckpt_checkpointsynchronizeasync), // TODO RESPONSE
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTSYNCHRONIZEASYNC,
	},
	{ /* 16 */
		.libais_handler_fn	= message_handler_req_lib_ckpt_sectioniteratorinitialize,
		.response_size		= sizeof (struct res_lib_ckpt_sectioniteratorinitialize),
		.response_id		= MESSAGE_RES_CKPT_SECTIONITERATOR_SECTIONITERATORINITIALIZE,
	},
	{ /* 17 */
		.libais_handler_fn	= message_handler_req_lib_ckpt_sectioniteratornext,
		.response_size		= sizeof (struct res_lib_ckpt_sectioniteratornext),
		.response_id		= MESSAGE_RES_CKPT_SECTIONITERATOR_SECTIONITERATORNEXT,
	}
};


static int (*ckpt_aisexec_handler_fns[]) (void *msg, struct in_addr source_addr, int endian_conversion_required) = {
	message_handler_req_exec_ckpt_checkpointopen,
	message_handler_req_exec_ckpt_checkpointclose,
	message_handler_req_exec_ckpt_checkpointunlink,
	message_handler_req_exec_ckpt_checkpointretentiondurationset,
	message_handler_req_exec_ckpt_checkpointretentiondurationexpire,
	message_handler_req_exec_ckpt_sectioncreate,
	message_handler_req_exec_ckpt_sectiondelete,
	message_handler_req_exec_ckpt_sectionexpirationtimeset,
	message_handler_req_exec_ckpt_sectionwrite,
	message_handler_req_exec_ckpt_sectionoverwrite,
	message_handler_req_exec_ckpt_sectionread
};

struct service_handler ckpt_service_handler = {
	.libais_handlers			= ckpt_libais_handlers,
	.libais_handlers_count		= sizeof (ckpt_libais_handlers) / sizeof (struct libais_handler),
	.aisexec_handler_fns		= ckpt_aisexec_handler_fns,
	.aisexec_handler_fns_count	= sizeof (ckpt_aisexec_handler_fns) / sizeof (int (*)),
	.confchg_fn					= ckpt_confchg_fn,
	.libais_init_fn				= message_handler_req_lib_ckpt_init,
	.libais_exit_fn				= ckpt_exit_fn,
	.exec_init_fn				= ckpt_exec_init_fn,
	.exec_dump_fn				= 0
};

static struct saCkptCheckpoint *ckpt_checkpoint_find_global (SaNameT *name)
{
	struct list_head *checkpointList;
	struct saCkptCheckpoint *checkpoint;

   for (checkpointList = checkpointListHead.next;
        checkpointList != &checkpointListHead;
        checkpointList = checkpointList->next) {

        checkpoint = list_entry (checkpointList,
            struct saCkptCheckpoint, list);

		if (name_match (name, &checkpoint->name)) {
			return (checkpoint);
		}
	}
	return (0);
}

static void ckpt_checkpoint_remove_cleanup (
	struct conn_info *conn_info,
	struct saCkptCheckpoint *checkpoint)
{
	struct list_head *list;
	struct checkpoint_cleanup *checkpoint_cleanup;

   for (list = conn_info->ais_ci.u.libckpt_ci.checkpoint_list.next;
		list != &conn_info->ais_ci.u.libckpt_ci.checkpoint_list;
        list = list->next) {

        checkpoint_cleanup = list_entry (list,
            struct checkpoint_cleanup, list);

		if (checkpoint_cleanup->checkpoint == checkpoint) {
			list_del (&checkpoint_cleanup->list);
			free (checkpoint_cleanup);
			return;
		}
	}
}
static struct saCkptCheckpointSection *ckpt_checkpoint_find_globalSection (
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
	
		log_printf (LOG_LEVEL_DEBUG, "Checking section id %*s\n", 
			    (int)ckptCheckpointSection->sectionDescriptor.sectionId.idLen,
			    ckptCheckpointSection->sectionDescriptor.sectionId.id);

		if (ckptCheckpointSection->sectionDescriptor.sectionId.idLen == idLen &&
			(memcmp (ckptCheckpointSection->sectionDescriptor.sectionId.id, 
			id, idLen) == 0)) {

			return (ckptCheckpointSection);
		}
	}
	return 0;
}

void checkpoint_section_release (struct saCkptCheckpointSection *section)
{
	list_del (&section->list);
	free (section->sectionDescriptor.sectionId.id); 
	free (section->sectionData);
	poll_timer_delete (aisexec_poll_handle, section->expiration_timer);
	free (section);
}

void checkpoint_release (struct saCkptCheckpoint *checkpoint)
{
	struct list_head *list;
	struct saCkptCheckpointSection *section;

	poll_timer_delete (aisexec_poll_handle, checkpoint->retention_timer);

	/*
	 * Release all checkpoint sections for this checkpoint
	 */
	for (list = checkpoint->checkpointSectionsListHead.next;
		list != &checkpoint->checkpointSectionsListHead;) {

		section = list_entry (list,
			struct saCkptCheckpointSection, list);
	
		list = list->next;
		checkpoint_section_release (section);
	}
	list_del (&checkpoint->list);
	free (checkpoint);
}

int ckpt_checkpoint_close (struct saCkptCheckpoint *checkpoint) {
	struct req_exec_ckpt_checkpointclose req_exec_ckpt_checkpointclose;
	struct iovec iovecs[2];

	if (checkpoint->expired == 1) {
		return (0);
	}
	req_exec_ckpt_checkpointclose.header.size =
		sizeof (struct req_exec_ckpt_checkpointclose);
	req_exec_ckpt_checkpointclose.header.id = MESSAGE_REQ_EXEC_CKPT_CHECKPOINTCLOSE;

	memcpy (&req_exec_ckpt_checkpointclose.checkpointName,
		&checkpoint->name, sizeof (SaNameT));

	iovecs[0].iov_base = (char *)&req_exec_ckpt_checkpointclose;
	iovecs[0].iov_len = sizeof (req_exec_ckpt_checkpointclose);

	if (totempg_send_ok (sizeof (struct req_exec_ckpt_checkpointclose))) {
		assert (totempg_mcast (iovecs, 1, TOTEMPG_AGREED) == 0);
		return (0);
	}

	return (-1);
}

static int ckpt_exec_init_fn (void)
{
#ifdef TODO
	int res;

	res = totempg_recovery_plug_create (&ckpt_checkpoint_recovery_plug_handle);
	if (res != 0) {
		log_printf(LOG_LEVEL_ERROR,
		"Could not create recovery plug for clm service.\n");
		return (-1);
	}
#endif 
	return (0);
}

static int ckpt_exit_fn (struct conn_info *conn_info)
{
	struct checkpoint_cleanup *checkpoint_cleanup;
	struct list_head *list;
	
	if (conn_info->service != SOCKET_SERVICE_CKPT) {
		return 0;
	}
	
	/*
	 * close all checkpoints opened on this fd
	 */
	for (list = conn_info->ais_ci.u.libckpt_ci.checkpoint_list.next;
		list != &conn_info->ais_ci.u.libckpt_ci.checkpoint_list;) {

		checkpoint_cleanup = list_entry (list,
			struct checkpoint_cleanup, list);

		ckpt_checkpoint_close (checkpoint_cleanup->checkpoint);

		list = list->next;

		free (checkpoint_cleanup);
	}

#ifdef TODO
/* todo close section iterators
 */
// TODO what about exit of open checkpoints

	if (conn_info->ais_ci.u.libckpt_ci.sectionIterator.sectionIteratorEntries) {
		free (conn_info->ais_ci.u.libckpt_ci.sectionIterator.sectionIteratorEntries);
	}
	list_del (&conn_info->ais_ci.u.libckpt_ci.sectionIterator.list);
#endif
	return (0);
}

static int message_handler_req_lib_activatepoll (struct conn_info *conn_info, void *message)
{
	struct res_lib_activatepoll res_lib_activatepoll;

	res_lib_activatepoll.header.size = sizeof (struct res_lib_activatepoll);
	res_lib_activatepoll.header.id = MESSAGE_RES_LIB_ACTIVATEPOLL;
	res_lib_activatepoll.header.error = SA_AIS_OK;
	libais_send_response (conn_info, &res_lib_activatepoll,
		sizeof (struct res_lib_activatepoll));

	return (0);
}

static int message_handler_req_exec_ckpt_checkpointopen (void *message, struct in_addr source_addr, int endian_conversion_required)
{
	struct req_exec_ckpt_checkpointopen *req_exec_ckpt_checkpointopen = (struct req_exec_ckpt_checkpointopen *)message;
	struct req_lib_ckpt_checkpointopen *req_lib_ckpt_checkpointopen = (struct req_lib_ckpt_checkpointopen *)&req_exec_ckpt_checkpointopen->req_lib_ckpt_checkpointopen;
	struct res_lib_ckpt_checkpointopen res_lib_ckpt_checkpointopen;

	struct saCkptCheckpoint *ckptCheckpoint = 0;
	struct saCkptCheckpointSection *ckptCheckpointSection = 0;
	struct checkpoint_cleanup *checkpoint_cleanup;
	SaErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_DEBUG, "Executive request to open checkpoint %p\n", req_exec_ckpt_checkpointopen);
	
	ckptCheckpoint = ckpt_checkpoint_find_global (&req_lib_ckpt_checkpointopen->checkpointName);

	/*
	 * If checkpoint doesn't exist, create one
	 */
	if (ckptCheckpoint == 0) {
		ckptCheckpoint = malloc (sizeof (struct saCkptCheckpoint));
		if (ckptCheckpoint == 0) {
			error = SA_AIS_ERR_NO_MEMORY;
			goto error_exit;
		}

		ckptCheckpointSection = malloc (sizeof (struct saCkptCheckpointSection));
		if (ckptCheckpointSection == 0) {
			free (ckptCheckpoint);
			error = SA_AIS_ERR_NO_MEMORY;
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
		ckptCheckpoint->retention_timer = 0;
		ckptCheckpoint->expired = 0;

		/*
		 * Add in default checkpoint section
		 */
		list_init (&ckptCheckpointSection->list);
		list_add (&ckptCheckpointSection->list, &ckptCheckpoint->checkpointSectionsListHead);
		
		/*
		 * Default section id
		 */
		ckptCheckpointSection->sectionDescriptor.sectionId.id = 0;
		ckptCheckpointSection->sectionDescriptor.sectionId.idLen = 0;
		ckptCheckpointSection->sectionDescriptor.sectionSize = 0;
		ckptCheckpointSection->sectionDescriptor.expirationTime = SA_TIME_END;
		ckptCheckpointSection->sectionDescriptor.sectionState = SA_CKPT_SECTION_VALID;
		ckptCheckpointSection->sectionDescriptor.lastUpdate = 0; // current time
		ckptCheckpointSection->sectionData = 0;
		ckptCheckpointSection->expiration_timer = 0;
	}

	/*
	 * If the checkpoint has been unlinked, it is an invalid name
	 */
	if (ckptCheckpoint->unlinked) {
		error = SA_AIS_ERR_INVALID_PARAM; /* Is this the correct return ? */
		goto error_exit;
	}
	
	/*
	 * Setup connection information and mark checkpoint as referenced
	 */
	log_printf (LOG_LEVEL_DEBUG, "CHECKPOINT opened is %p\n", ckptCheckpoint);
	ckptCheckpoint->referenceCount += 1;

	/*
	 * Reset retention duration since this checkpoint was just opened
	 */
	poll_timer_delete (aisexec_poll_handle, ckptCheckpoint->retention_timer);
	ckptCheckpoint->retention_timer = 0;

	/*
	 * Send error result to CKPT library
	 */
error_exit:
	/*
	 * If this node was the source of the message, respond to this node
	 */
	if (req_exec_ckpt_checkpointopen->source.in_addr.s_addr == this_ip.sin_addr.s_addr) {
		res_lib_ckpt_checkpointopen.header.size = sizeof (struct res_lib_ckpt_checkpointopen);
		res_lib_ckpt_checkpointopen.header.id = MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTOPEN;
		res_lib_ckpt_checkpointopen.header.error = error;

		checkpoint_cleanup = malloc (sizeof (struct checkpoint_cleanup));
		if (checkpoint_cleanup == 0) {
			free (ckptCheckpoint);
			error = SA_AIS_ERR_NO_MEMORY;
		} else {
			checkpoint_cleanup->checkpoint = ckptCheckpoint;
			list_add (&checkpoint_cleanup->list,
				&req_exec_ckpt_checkpointopen->source.conn_info->ais_ci.u.libckpt_ci.checkpoint_list);
		}

		libais_send_response (req_exec_ckpt_checkpointopen->source.conn_info, &res_lib_ckpt_checkpointopen,
			sizeof (struct res_lib_ckpt_checkpointopen));
	}

//	return (error == SA_AIS_OK ? 0 : -1);
	return (0);
}

unsigned int abstime_to_msec (SaTimeT time)
{
	struct timeval tv;
	unsigned long long curr_time;
	unsigned long long msec_time;
	
	gettimeofday (&tv, NULL);
	curr_time = ((((unsigned long long)tv.tv_sec) * ((unsigned long)1000)) +
		(((unsigned long long)tv.tv_usec) / ((unsigned long long)1000)));
	msec_time = (((unsigned long long)time) / 1000000) -
		(unsigned long long)curr_time;
	return ((unsigned int)(msec_time));
}

void timer_function_section_expire (void *data)
{
	struct saCkptCheckpointSection *section = (struct saCkptCheckpointSection *)data;
	if (section->sectionDescriptor.sectionId.id) {
		log_printf (LOG_LEVEL_NOTICE, "CKPT: Expiring section %s\n", section->sectionDescriptor.sectionId.id);
	}
	checkpoint_section_release (section);
}

void timer_function_retention (void *data)
{
	struct saCkptCheckpoint *checkpoint = (struct saCkptCheckpoint *)data;
	struct req_exec_ckpt_checkpointretentiondurationexpire req_exec_ckpt_checkpointretentiondurationexpire;
	struct iovec iovec;

	checkpoint->retention_timer = 0;
	req_exec_ckpt_checkpointretentiondurationexpire.header.size =
		sizeof (struct req_exec_ckpt_checkpointretentiondurationexpire);
	req_exec_ckpt_checkpointretentiondurationexpire.header.id = MESSAGE_REQ_EXEC_CKPT_CHECKPOINTRETENTIONDURATIONEXPIRE;

	memcpy (&req_exec_ckpt_checkpointretentiondurationexpire.checkpointName,
		&checkpoint->name,
		sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_ckpt_checkpointretentiondurationexpire;
	iovec.iov_len = sizeof (req_exec_ckpt_checkpointretentiondurationexpire);

	assert (totempg_mcast (&iovec, 1, TOTEMPG_AGREED) == 0);
}

extern int message_handler_req_exec_ckpt_checkpointclose (void *message, struct in_addr source_addr, int endian_conversion_required)
{
	struct req_exec_ckpt_checkpointclose *req_exec_ckpt_checkpointclose = (struct req_exec_ckpt_checkpointclose *)message;
	struct res_lib_ckpt_checkpointclose res_lib_ckpt_checkpointclose;
	struct saCkptCheckpoint *checkpoint = 0;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_DEBUG, "Got EXEC request to close checkpoint %s\n", getSaNameT (&req_exec_ckpt_checkpointclose->checkpointName));

	checkpoint = ckpt_checkpoint_find_global (&req_exec_ckpt_checkpointclose->checkpointName);
	if (checkpoint == 0) {
		goto error_exit;
	}

	checkpoint->referenceCount--;
	assert (checkpoint->referenceCount >= 0);
	log_printf (LOG_LEVEL_DEBUG, "disconnect called, new CKPT ref count is %d\n", 
		checkpoint->referenceCount);

	/*
	 * If checkpoint has been unlinked and this is the last reference, delete it
	 */
	if (checkpoint->unlinked && checkpoint->referenceCount == 0) {
		log_printf (LOG_LEVEL_DEBUG, "Unlinking checkpoint.\n");
		checkpoint_release (checkpoint);
	} else
	if (checkpoint->referenceCount == 0) {
		poll_timer_add (aisexec_poll_handle,
			checkpoint->checkpointCreationAttributes.retentionDuration / 1000000,
			checkpoint,
			timer_function_retention,
			&checkpoint->retention_timer);
	}
	
error_exit:
	if (req_exec_ckpt_checkpointclose->source.in_addr.s_addr == this_ip.sin_addr.s_addr) {
		ckpt_checkpoint_remove_cleanup (req_exec_ckpt_checkpointclose->source.conn_info,
			checkpoint);

		res_lib_ckpt_checkpointclose.header.size = sizeof (struct res_lib_ckpt_checkpointclose);
		res_lib_ckpt_checkpointclose.header.id = MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTCLOSE;
		res_lib_ckpt_checkpointclose.header.error = error;
		libais_send_response (req_exec_ckpt_checkpointclose->source.conn_info,
			&res_lib_ckpt_checkpointclose, sizeof (struct res_lib_ckpt_checkpointclose));
	}
	return (0);
}

static int message_handler_req_exec_ckpt_checkpointunlink (void *message, struct in_addr source_addr, int endian_conversion_required)
{
	struct req_exec_ckpt_checkpointunlink *req_exec_ckpt_checkpointunlink = (struct req_exec_ckpt_checkpointunlink *)message;

	struct req_lib_ckpt_checkpointunlink *req_lib_ckpt_checkpointunlink = (struct req_lib_ckpt_checkpointunlink *)&req_exec_ckpt_checkpointunlink->req_lib_ckpt_checkpointunlink;
	struct res_lib_ckpt_checkpointunlink res_lib_ckpt_checkpointunlink;
	struct saCkptCheckpoint *ckptCheckpoint = 0;
	SaErrorT error = SA_AIS_OK;
	
	log_printf (LOG_LEVEL_DEBUG, "Got EXEC request to unlink checkpoint %p\n", req_exec_ckpt_checkpointunlink);
	ckptCheckpoint = ckpt_checkpoint_find_global (&req_lib_ckpt_checkpointunlink->checkpointName);
	if (ckptCheckpoint == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}
	if (ckptCheckpoint->unlinked) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}
	ckptCheckpoint->unlinked = 1;
	/*
	 * Immediately delete entry if reference count is zero
	 */
	if (ckptCheckpoint->referenceCount == 0) {
		/*
		 * Remove retention timer since this checkpoint was unlinked and is no
		 * longer referenced
		 */
		checkpoint_release (ckptCheckpoint);
	}

error_exit:
	/*
	 * If this node was the source of the message, respond to this node
	 */
	if (req_exec_ckpt_checkpointunlink->source.in_addr.s_addr == this_ip.sin_addr.s_addr) {
		res_lib_ckpt_checkpointunlink.header.size = sizeof (struct res_lib_ckpt_checkpointunlink);
		res_lib_ckpt_checkpointunlink.header.id = MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTUNLINK;
		res_lib_ckpt_checkpointunlink.header.error = error;
		libais_send_response (req_exec_ckpt_checkpointunlink->source.conn_info, &res_lib_ckpt_checkpointunlink,
			sizeof (struct res_lib_ckpt_checkpointunlink));
	}
	return (0);
}

static int message_handler_req_exec_ckpt_checkpointretentiondurationset (void *message, struct in_addr source_addr, int endian_conversion_required)
{
	struct req_exec_ckpt_checkpointretentiondurationset *req_exec_ckpt_checkpointretentiondurationset = (struct req_exec_ckpt_checkpointretentiondurationset *)message;
	struct res_lib_ckpt_checkpointretentiondurationset res_lib_ckpt_checkpointretentiondurationset;
	struct saCkptCheckpoint *checkpoint;

	checkpoint = ckpt_checkpoint_find_global (&req_exec_ckpt_checkpointretentiondurationset->checkpointName);
	if (checkpoint) {
		log_printf (LOG_LEVEL_NOTICE, "CKPT: Setting retention duration for checkpoint %s\n",
			getSaNameT (&req_exec_ckpt_checkpointretentiondurationset->checkpointName));
		checkpoint->checkpointCreationAttributes.retentionDuration = req_exec_ckpt_checkpointretentiondurationset->retentionDuration;

		if (checkpoint->expired == 0 && checkpoint->referenceCount == 0) {
			poll_timer_delete (aisexec_poll_handle, checkpoint->retention_timer);

			poll_timer_add (aisexec_poll_handle,
				checkpoint->checkpointCreationAttributes.retentionDuration / 1000000,
				checkpoint,
				timer_function_retention,
				&checkpoint->retention_timer);
		}
	}

	/*
	 * Respond to library if this processor sent the duration set request
	 */
	if (req_exec_ckpt_checkpointretentiondurationset->source.in_addr.s_addr == this_ip.sin_addr.s_addr) {
		res_lib_ckpt_checkpointretentiondurationset.header.size = sizeof (struct res_lib_ckpt_checkpointretentiondurationset);
		res_lib_ckpt_checkpointretentiondurationset.header.id = MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTRETENTIONDURATIONSET;
		res_lib_ckpt_checkpointretentiondurationset.header.error = SA_AIS_OK;

		libais_send_response (req_exec_ckpt_checkpointretentiondurationset->source.conn_info,
			&res_lib_ckpt_checkpointretentiondurationset,
			sizeof (struct res_lib_ckpt_checkpointretentiondurationset));
	}
	return (0);
}

static int message_handler_req_exec_ckpt_checkpointretentiondurationexpire (void *message, struct in_addr source_addr, int endian_conversion_required)
{
	struct req_exec_ckpt_checkpointretentiondurationexpire *req_exec_ckpt_checkpointretentiondurationexpire = (struct req_exec_ckpt_checkpointretentiondurationexpire *)message;
	struct req_exec_ckpt_checkpointunlink req_exec_ckpt_checkpointunlink;
	struct saCkptCheckpoint *checkpoint;
	struct iovec iovecs[2];

	checkpoint = ckpt_checkpoint_find_global (&req_exec_ckpt_checkpointretentiondurationexpire->checkpointName);
	if (checkpoint && checkpoint->expired == 0) {
		log_printf (LOG_LEVEL_NOTICE, "CKPT: Expiring checkpoint %s\n", getSaNameT (&req_exec_ckpt_checkpointretentiondurationexpire->checkpointName));
		checkpoint->expired = 1;

		req_exec_ckpt_checkpointunlink.header.size =
			sizeof (struct req_exec_ckpt_checkpointunlink);
		req_exec_ckpt_checkpointunlink.header.id = MESSAGE_REQ_EXEC_CKPT_CHECKPOINTUNLINK;

		req_exec_ckpt_checkpointunlink.source.conn_info = 0;
		req_exec_ckpt_checkpointunlink.source.in_addr.s_addr = 0;

		memcpy (&req_exec_ckpt_checkpointunlink.req_lib_ckpt_checkpointunlink.checkpointName,
			&req_exec_ckpt_checkpointretentiondurationexpire->checkpointName,
			sizeof (SaNameT));

		iovecs[0].iov_base = (char *)&req_exec_ckpt_checkpointunlink;
		iovecs[0].iov_len = sizeof (req_exec_ckpt_checkpointunlink);

		assert (totempg_mcast (iovecs, 1, TOTEMPG_AGREED) == 0);
	}
	return (0);
}

static int message_handler_req_exec_ckpt_sectioncreate (void *message, struct in_addr source_addr, int endian_conversion_required) {
	struct req_exec_ckpt_sectioncreate *req_exec_ckpt_sectioncreate = (struct req_exec_ckpt_sectioncreate *)message;
	struct req_lib_ckpt_sectioncreate *req_lib_ckpt_sectioncreate = (struct req_lib_ckpt_sectioncreate *)&req_exec_ckpt_sectioncreate->req_lib_ckpt_sectioncreate;
	struct res_lib_ckpt_sectioncreate res_lib_ckpt_sectioncreate;
	struct saCkptCheckpoint *ckptCheckpoint;
	struct saCkptCheckpointSection *ckptCheckpointSection;
	void *initialData;
	void *sectionId;
	SaErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_DEBUG, "Executive request to create a checkpoint section.\n");
	ckptCheckpoint = ckpt_checkpoint_find_global (&req_exec_ckpt_sectioncreate->checkpointName);
	if (ckptCheckpoint == 0) {
		error = SA_AIS_ERR_LIBRARY; // TODO find the right error for this
		goto error_exit;
	}

	/*
	 * Determine if user-specified checkpoint ID already exists
	 */
	ckptCheckpointSection = ckpt_checkpoint_find_globalSection (ckptCheckpoint,
		((char *)req_lib_ckpt_sectioncreate) + sizeof (struct req_lib_ckpt_sectioncreate),
		req_lib_ckpt_sectioncreate->idLen);
	if (ckptCheckpointSection) {
		error = SA_AIS_ERR_EXIST;
		goto error_exit;
	}

	/*
	 * Allocate checkpoint section
	 */
	ckptCheckpointSection = malloc (sizeof (struct saCkptCheckpointSection));
	if (ckptCheckpointSection == 0) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_exit;
	}
	/*
	 * Allocate checkpoint section data
	 */
	initialData = malloc (req_lib_ckpt_sectioncreate->initialDataSize);
	if (initialData == 0) {
		free (ckptCheckpointSection);
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_exit;
	}
	/*
	 * Allocate checkpoint section id
	 */
	sectionId = malloc (req_lib_ckpt_sectioncreate->idLen);
	if (sectionId == 0) {
		free (ckptCheckpointSection);
		free (initialData);
		error = SA_AIS_ERR_NO_MEMORY;
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
	ckptCheckpointSection->sectionDescriptor.sectionId.id = sectionId;
	ckptCheckpointSection->sectionDescriptor.sectionId.idLen = req_lib_ckpt_sectioncreate->idLen;
	ckptCheckpointSection->sectionDescriptor.sectionSize = req_lib_ckpt_sectioncreate->initialDataSize;
	ckptCheckpointSection->sectionDescriptor.expirationTime = req_lib_ckpt_sectioncreate->expirationTime;
	ckptCheckpointSection->sectionDescriptor.sectionState = SA_CKPT_SECTION_VALID;
	ckptCheckpointSection->sectionDescriptor.lastUpdate = 0; // TODO current time
	ckptCheckpointSection->sectionData = initialData;
	ckptCheckpointSection->expiration_timer = 0;

	if (req_lib_ckpt_sectioncreate->expirationTime != SA_TIME_END) {
		poll_timer_add (aisexec_poll_handle,
			abstime_to_msec (ckptCheckpointSection->sectionDescriptor.expirationTime),
			ckptCheckpointSection,
			timer_function_section_expire,
			&ckptCheckpointSection->expiration_timer);
	}

	/*
	 * Add checkpoint section to checkpoint
	 */
	list_init (&ckptCheckpointSection->list);
	list_add (&ckptCheckpointSection->list,
		&ckptCheckpoint->checkpointSectionsListHead);

error_exit:
	if (req_exec_ckpt_sectioncreate->source.in_addr.s_addr == this_ip.sin_addr.s_addr) {
		res_lib_ckpt_sectioncreate.header.size = sizeof (struct res_lib_ckpt_sectioncreate);
		res_lib_ckpt_sectioncreate.header.id = MESSAGE_RES_CKPT_CHECKPOINT_SECTIONCREATE;
		res_lib_ckpt_sectioncreate.header.error = error;

		libais_send_response (req_exec_ckpt_sectioncreate->source.conn_info,
			&res_lib_ckpt_sectioncreate,
			sizeof (struct res_lib_ckpt_sectioncreate));
	}
	return (0);
}

static int message_handler_req_exec_ckpt_sectiondelete (void *message, struct in_addr source_addr, int endian_conversion_required) {
	struct req_exec_ckpt_sectiondelete *req_exec_ckpt_sectiondelete = (struct req_exec_ckpt_sectiondelete *)message;
	struct req_lib_ckpt_sectiondelete *req_lib_ckpt_sectiondelete = (struct req_lib_ckpt_sectiondelete *)&req_exec_ckpt_sectiondelete->req_lib_ckpt_sectiondelete;
	struct res_lib_ckpt_sectiondelete res_lib_ckpt_sectiondelete;
	struct saCkptCheckpoint *ckptCheckpoint;
	struct saCkptCheckpointSection *ckptCheckpointSection;
	SaErrorT error = SA_AIS_OK;

	ckptCheckpoint = ckpt_checkpoint_find_global (&req_exec_ckpt_sectiondelete->checkpointName);
	if (ckptCheckpoint == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	/*
	 * Determine if the user is trying to delete the default section
	 */
	if (req_lib_ckpt_sectiondelete->idLen == 0) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	/*
	 * Find checkpoint section to be deleted
	 */
	ckptCheckpointSection = ckpt_checkpoint_find_globalSection (ckptCheckpoint,
		((char *)(req_lib_ckpt_sectiondelete) + sizeof (struct req_lib_ckpt_sectiondelete)),
		req_lib_ckpt_sectiondelete->idLen);
	if (ckptCheckpointSection == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	/*
	 * Delete checkpoint section
	 */
	checkpoint_section_release (ckptCheckpointSection);

	/*
	 * return result to CKPT library
	 */
error_exit:
	if (req_exec_ckpt_sectiondelete->source.in_addr.s_addr == this_ip.sin_addr.s_addr) {
		res_lib_ckpt_sectiondelete.header.size = sizeof (struct res_lib_ckpt_sectiondelete);
		res_lib_ckpt_sectiondelete.header.id = MESSAGE_RES_CKPT_CHECKPOINT_SECTIONDELETE;
		res_lib_ckpt_sectiondelete.header.error = error;

		libais_send_response (req_exec_ckpt_sectiondelete->source.conn_info,
			&res_lib_ckpt_sectiondelete,
			sizeof (struct res_lib_ckpt_sectiondelete));
	}
	return (0);
}

static int message_handler_req_exec_ckpt_sectionexpirationtimeset (void *message, struct in_addr source_addr, int endian_conversion_required) {
	struct req_exec_ckpt_sectionexpirationtimeset *req_exec_ckpt_sectionexpirationtimeset = (struct req_exec_ckpt_sectionexpirationtimeset *)message;
	struct req_lib_ckpt_sectionexpirationtimeset *req_lib_ckpt_sectionexpirationtimeset = (struct req_lib_ckpt_sectionexpirationtimeset *)&req_exec_ckpt_sectionexpirationtimeset->req_lib_ckpt_sectionexpirationtimeset;
	struct res_lib_ckpt_sectionexpirationtimeset res_lib_ckpt_sectionexpirationtimeset;
	struct saCkptCheckpoint *ckptCheckpoint;
	struct saCkptCheckpointSection *ckptCheckpointSection;
	SaErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_DEBUG, "Executive request to set section expiratoin time\n");
	ckptCheckpoint = ckpt_checkpoint_find_global (&req_exec_ckpt_sectionexpirationtimeset->checkpointName);
	if (ckptCheckpoint == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	/*
	 * Determine if the user is trying to set expiration time for the default section
	 */
	if (req_lib_ckpt_sectionexpirationtimeset->idLen == 0) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	/*
	 * Find checkpoint section that expiration time should be set for
	 */
	ckptCheckpointSection = ckpt_checkpoint_find_globalSection (ckptCheckpoint,
		((char *)req_lib_ckpt_sectionexpirationtimeset) +
			sizeof (struct req_lib_ckpt_sectionexpirationtimeset),
		req_lib_ckpt_sectionexpirationtimeset->idLen);

	if (ckptCheckpointSection == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	ckptCheckpointSection->sectionDescriptor.expirationTime = req_lib_ckpt_sectionexpirationtimeset->expirationTime;

	poll_timer_delete (aisexec_poll_handle, ckptCheckpointSection->expiration_timer);
	ckptCheckpointSection->expiration_timer = 0;

	if (req_lib_ckpt_sectionexpirationtimeset->expirationTime != SA_TIME_END) {
		poll_timer_add (aisexec_poll_handle,
			abstime_to_msec (ckptCheckpointSection->sectionDescriptor.expirationTime),
			ckptCheckpointSection,
			timer_function_section_expire,
			&ckptCheckpointSection->expiration_timer);
	}

error_exit:
	if (req_exec_ckpt_sectionexpirationtimeset->source.in_addr.s_addr == this_ip.sin_addr.s_addr) {
		res_lib_ckpt_sectionexpirationtimeset.header.size = sizeof (struct res_lib_ckpt_sectionexpirationtimeset);
		res_lib_ckpt_sectionexpirationtimeset.header.id = MESSAGE_RES_CKPT_CHECKPOINT_SECTIONEXPIRATIONTIMESET;
		res_lib_ckpt_sectionexpirationtimeset.header.error = error;

		libais_send_response (req_exec_ckpt_sectionexpirationtimeset->source.conn_info,
			&res_lib_ckpt_sectionexpirationtimeset,
			sizeof (struct res_lib_ckpt_sectionexpirationtimeset));
	}
	return (0);
}

static int message_handler_req_exec_ckpt_sectionwrite (void *message, struct in_addr source_addr, int endian_conversion_required) {
	struct req_exec_ckpt_sectionwrite *req_exec_ckpt_sectionwrite = (struct req_exec_ckpt_sectionwrite *)message;
	struct req_lib_ckpt_sectionwrite *req_lib_ckpt_sectionwrite = (struct req_lib_ckpt_sectionwrite *)&req_exec_ckpt_sectionwrite->req_lib_ckpt_sectionwrite;
	struct res_lib_ckpt_sectionwrite res_lib_ckpt_sectionwrite;
	struct saCkptCheckpoint *ckptCheckpoint;
	struct saCkptCheckpointSection *ckptCheckpointSection;
	int sizeRequired;
	void *sectionData;
	SaErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_DEBUG, "Executive request to section write.\n");
	ckptCheckpoint = ckpt_checkpoint_find_global (&req_exec_ckpt_sectionwrite->checkpointName);
	if (ckptCheckpoint == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

//printf ("writing checkpoint section is %s\n", ((char *)req_lib_ckpt_sectionwrite) + sizeof (struct req_lib_ckpt_sectionwrite));
	/*
	 * Find checkpoint section to be written
	 */
	ckptCheckpointSection = ckpt_checkpoint_find_globalSection (ckptCheckpoint,
		((char *)req_lib_ckpt_sectionwrite) + sizeof (struct req_lib_ckpt_sectionwrite),
		req_lib_ckpt_sectionwrite->idLen);
	if (ckptCheckpointSection == 0) {
printf ("CANT FIND SECTION '%s'\n",
		((char *)req_lib_ckpt_sectionwrite) + sizeof (struct req_lib_ckpt_sectionwrite));
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	/*
	 * If write would extend past end of section data, enlarge section
	 */
	sizeRequired = req_lib_ckpt_sectionwrite->dataOffset + req_lib_ckpt_sectionwrite->dataSize;
	if (sizeRequired > ckptCheckpointSection->sectionDescriptor.sectionSize) {
		sectionData = realloc (ckptCheckpointSection->sectionData, sizeRequired);
		if (sectionData == 0) {
			error = SA_AIS_ERR_NO_MEMORY;
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
		res_lib_ckpt_sectionwrite.header.size = sizeof (struct res_lib_ckpt_sectionwrite);
		res_lib_ckpt_sectionwrite.header.id = MESSAGE_RES_CKPT_CHECKPOINT_SECTIONWRITE;
		res_lib_ckpt_sectionwrite.header.error = error;

		libais_send_response (req_exec_ckpt_sectionwrite->source.conn_info,
			&res_lib_ckpt_sectionwrite,
			sizeof (struct res_lib_ckpt_sectionwrite));
	}

	return (0);
}

static int message_handler_req_exec_ckpt_sectionoverwrite (void *message, struct in_addr source_addr, int endian_conversion_required) {
	struct req_exec_ckpt_sectionoverwrite *req_exec_ckpt_sectionoverwrite = (struct req_exec_ckpt_sectionoverwrite *)message;
	struct req_lib_ckpt_sectionoverwrite *req_lib_ckpt_sectionoverwrite = (struct req_lib_ckpt_sectionoverwrite *)&req_exec_ckpt_sectionoverwrite->req_lib_ckpt_sectionoverwrite;
	struct res_lib_ckpt_sectionoverwrite res_lib_ckpt_sectionoverwrite;
	struct saCkptCheckpoint *ckptCheckpoint;
	struct saCkptCheckpointSection *ckptCheckpointSection;
	void *sectionData;
	SaErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_DEBUG, "Executive request to section overwrite.\n");
	ckptCheckpoint = ckpt_checkpoint_find_global (&req_exec_ckpt_sectionoverwrite->checkpointName);
	if (ckptCheckpoint == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	/*
	 * Find checkpoint section to be overwritten
	 */
	ckptCheckpointSection = ckpt_checkpoint_find_globalSection (ckptCheckpoint,
		((char *)req_lib_ckpt_sectionoverwrite) +
			sizeof (struct req_lib_ckpt_sectionoverwrite),
		req_lib_ckpt_sectionoverwrite->idLen);
	if (ckptCheckpointSection == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	/*
	 * Allocate checkpoint section data
	 */
	sectionData = malloc (req_lib_ckpt_sectionoverwrite->dataSize);
	if (sectionData == 0) {
		error = SA_AIS_ERR_NO_MEMORY;
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
		res_lib_ckpt_sectionoverwrite.header.size = sizeof (struct res_lib_ckpt_sectionoverwrite);
		res_lib_ckpt_sectionoverwrite.header.id = MESSAGE_RES_CKPT_CHECKPOINT_SECTIONOVERWRITE;
		res_lib_ckpt_sectionoverwrite.header.error = error;

		libais_send_response (req_exec_ckpt_sectionoverwrite->source.conn_info,
			&res_lib_ckpt_sectionoverwrite,
			sizeof (struct res_lib_ckpt_sectionoverwrite));
	}
	return (0);
}
static int message_handler_req_exec_ckpt_sectionread (void *message, struct in_addr source_addr, int endian_conversion_required) {
	struct req_exec_ckpt_sectionread *req_exec_ckpt_sectionread = (struct req_exec_ckpt_sectionread *)message;
	struct req_lib_ckpt_sectionread *req_lib_ckpt_sectionread = (struct req_lib_ckpt_sectionread *)&req_exec_ckpt_sectionread->req_lib_ckpt_sectionread;
	struct res_lib_ckpt_sectionread res_lib_ckpt_sectionread;
	struct saCkptCheckpoint *ckptCheckpoint;
	struct saCkptCheckpointSection *ckptCheckpointSection = 0;
	int sectionSize = 0;
	SaErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_DEBUG, "Executive request for section read.\n");

	ckptCheckpoint = ckpt_checkpoint_find_global (&req_exec_ckpt_sectionread->checkpointName);
	if (ckptCheckpoint == 0) {
		error = SA_AIS_ERR_LIBRARY; // TODO find the right error for this
		goto error_exit;
	}

	/*
	 * Find checkpoint section to be read
	 */
	ckptCheckpointSection = ckpt_checkpoint_find_globalSection (ckptCheckpoint,
		((char *)req_lib_ckpt_sectionread) +
			sizeof (struct req_lib_ckpt_sectionread),
		req_lib_ckpt_sectionread->idLen);
	if (ckptCheckpointSection == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
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
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	/*
	 * Write read response to CKPT library
	 */
error_exit:
	if (req_exec_ckpt_sectionread->source.in_addr.s_addr == this_ip.sin_addr.s_addr) {
		res_lib_ckpt_sectionread.header.size = sizeof (struct res_lib_ckpt_sectionread) + sectionSize;
		res_lib_ckpt_sectionread.header.id = MESSAGE_RES_CKPT_CHECKPOINT_SECTIONREAD;
		res_lib_ckpt_sectionread.header.error = error;
	
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
	SaErrorT error = SA_AIS_ERR_ACCESS;

	log_printf (LOG_LEVEL_DEBUG, "Got request to initialize CKPT checkpoint.\n");

	if (conn_info->authenticated) {
    	conn_info->service = SOCKET_SERVICE_CKPT;
		list_init (&conn_info->ais_ci.u.libckpt_ci.sectionIterator.list);
		conn_info->ais_ci.u.libckpt_ci.sectionIterator.sectionIteratorEntries = 0;
		conn_info->ais_ci.u.libckpt_ci.sectionIterator.iteratorCount = 0;
		conn_info->ais_ci.u.libckpt_ci.sectionIterator.iteratorPos = 0;
		list_add (&conn_info->ais_ci.u.libckpt_ci.sectionIterator.list,
			&checkpointIteratorListHead);
		list_init (&conn_info->ais_ci.u.libckpt_ci.checkpoint_list);
		error = SA_AIS_OK;
	}

	res_lib_init.header.size = sizeof (struct res_lib_init);
	res_lib_init.header.id = MESSAGE_RES_INIT;
	res_lib_init.header.error = error;

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

	log_printf (LOG_LEVEL_DEBUG, "Library request to open checkpoint.\n");
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

	assert (totempg_mcast (iovecs, 1, TOTEMPG_AGREED) == 0);

	return (0);
}

static int message_handler_req_lib_ckpt_checkpointopenasync (struct conn_info *conn_info, void *message)
{
	return (0);
}


static int message_handler_req_lib_ckpt_checkpointclose (struct conn_info *conn_info, void *message) {
	struct req_lib_ckpt_checkpointclose *req_lib_ckpt_checkpointclose = (struct req_lib_ckpt_checkpointclose *)message;
	struct req_exec_ckpt_checkpointclose req_exec_ckpt_checkpointclose;
	struct saCkptCheckpoint *checkpoint;
	struct iovec iovecs[2];

	checkpoint = ckpt_checkpoint_find_global (&req_lib_ckpt_checkpointclose->checkpointName);
	if (checkpoint->expired == 1) {
		return (0);
	}
	req_exec_ckpt_checkpointclose.header.size =
		sizeof (struct req_exec_ckpt_checkpointclose);
	req_exec_ckpt_checkpointclose.header.id = MESSAGE_REQ_EXEC_CKPT_CHECKPOINTCLOSE;

	req_exec_ckpt_checkpointclose.source.conn_info = conn_info;
	req_exec_ckpt_checkpointclose.source.in_addr.s_addr = this_ip.sin_addr.s_addr;

	memcpy (&req_exec_ckpt_checkpointclose.checkpointName,
		&checkpoint->name, sizeof (SaNameT));

	iovecs[0].iov_base = (char *)&req_exec_ckpt_checkpointclose;
	iovecs[0].iov_len = sizeof (req_exec_ckpt_checkpointclose);

	if (totempg_send_ok (sizeof (struct req_exec_ckpt_checkpointclose))) {
		assert (totempg_mcast (iovecs, 1, TOTEMPG_AGREED) == 0);
	}

	return (0);
}

static int message_handler_req_lib_ckpt_checkpointunlink (struct conn_info *conn_info, void *message)
{
	struct req_lib_ckpt_checkpointunlink *req_lib_ckpt_checkpointunlink = (struct req_lib_ckpt_checkpointunlink *)message;
	struct req_exec_ckpt_checkpointunlink req_exec_ckpt_checkpointunlink;
	struct iovec iovecs[2];

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

	assert (totempg_mcast (iovecs, 1, TOTEMPG_AGREED) == 0);

	return (0);
}

static int message_handler_req_lib_ckpt_checkpointretentiondurationset (struct conn_info *conn_info, void *message)
{
	struct req_lib_ckpt_checkpointretentiondurationset *req_lib_ckpt_checkpointretentiondurationset = (struct req_lib_ckpt_checkpointretentiondurationset *)message;
	struct req_exec_ckpt_checkpointretentiondurationset req_exec_ckpt_checkpointretentiondurationset;
	struct iovec iovecs[2];

	log_printf (LOG_LEVEL_DEBUG, "DURATION SET FROM API fd %d\n", conn_info->fd);
	req_exec_ckpt_checkpointretentiondurationset.header.id = MESSAGE_REQ_EXEC_CKPT_CHECKPOINTRETENTIONDURATIONSET;
	req_exec_ckpt_checkpointretentiondurationset.header.size = sizeof (struct req_exec_ckpt_checkpointretentiondurationset);

	req_exec_ckpt_checkpointretentiondurationset.source.conn_info = conn_info;
	req_exec_ckpt_checkpointretentiondurationset.source.in_addr.s_addr = this_ip.sin_addr.s_addr;

	memcpy (&req_exec_ckpt_checkpointretentiondurationset.checkpointName,
		&req_lib_ckpt_checkpointretentiondurationset->checkpointName,
		sizeof (SaNameT));
	req_exec_ckpt_checkpointretentiondurationset.retentionDuration = req_lib_ckpt_checkpointretentiondurationset->retentionDuration;

	iovecs[0].iov_base = (char *)&req_exec_ckpt_checkpointretentiondurationset;
	iovecs[0].iov_len = sizeof (req_exec_ckpt_checkpointretentiondurationset);

	assert (totempg_mcast (iovecs, 1, TOTEMPG_AGREED) == 0);

	return (0);
}

static int message_handler_req_lib_ckpt_activereplicaset (struct conn_info *conn_info, void *message)
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

	log_printf (LOG_LEVEL_DEBUG, "in status get\n");

	/*
	 * Count memory used by checkpoint sections
	 */
	checkpoint = ckpt_checkpoint_find_global (&req_lib_ckpt_checkpointstatusget->checkpointName);

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
	res_lib_ckpt_checkpointstatusget.header.size = sizeof (struct res_lib_ckpt_checkpointstatusget);
	res_lib_ckpt_checkpointstatusget.header.id = MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTSTATUSGET;
	res_lib_ckpt_checkpointstatusget.header.error = SA_AIS_OK;

	memcpy (&res_lib_ckpt_checkpointstatusget.checkpointDescriptor.checkpointCreationAttributes,
		&checkpoint->checkpointCreationAttributes,
		sizeof (SaCkptCheckpointCreationAttributesT));
	res_lib_ckpt_checkpointstatusget.checkpointDescriptor.numberOfSections = numberOfSections;
	res_lib_ckpt_checkpointstatusget.checkpointDescriptor.memoryUsed = memoryUsed;

	log_printf (LOG_LEVEL_DEBUG, "before sending message\n");
	libais_send_response (conn_info, &res_lib_ckpt_checkpointstatusget,
		sizeof (struct res_lib_ckpt_checkpointstatusget));
	return (0);
}

static int message_handler_req_lib_ckpt_sectioncreate (struct conn_info *conn_info, void *message)
{
	struct req_lib_ckpt_sectioncreate *req_lib_ckpt_sectioncreate = (struct req_lib_ckpt_sectioncreate *)message;
	struct req_exec_ckpt_sectioncreate req_exec_ckpt_sectioncreate;
	struct iovec iovecs[2];
	struct saCkptCheckpoint *checkpoint;

	log_printf (LOG_LEVEL_DEBUG, "Section create from API fd %d\n", conn_info->fd);
	checkpoint = ckpt_checkpoint_find_global (&req_lib_ckpt_sectioncreate->checkpointName);

	/*
	 * checkpoint opened is writeable mode so send message to cluster
	 */
	req_exec_ckpt_sectioncreate.header.id = MESSAGE_REQ_EXEC_CKPT_SECTIONCREATE;
	req_exec_ckpt_sectioncreate.header.size = sizeof (struct req_exec_ckpt_sectioncreate);

	memcpy (&req_exec_ckpt_sectioncreate.req_lib_ckpt_sectioncreate,
		req_lib_ckpt_sectioncreate,
		sizeof (struct req_lib_ckpt_sectioncreate));

	memcpy (&req_exec_ckpt_sectioncreate.checkpointName,
		&req_lib_ckpt_sectioncreate->checkpointName,
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
		log_printf (LOG_LEVEL_DEBUG, "IOV_BASE is %p\n", iovecs[1].iov_base);
		assert (totempg_mcast (iovecs, 2, TOTEMPG_AGREED) == 0);
	} else {
		assert (totempg_mcast (iovecs, 1, TOTEMPG_AGREED) == 0);
	}

	return (0);
}

static int message_handler_req_lib_ckpt_sectiondelete (struct conn_info *conn_info, void *message)
{
	struct req_lib_ckpt_sectiondelete *req_lib_ckpt_sectiondelete = (struct req_lib_ckpt_sectiondelete *)message;
	struct req_exec_ckpt_sectiondelete req_exec_ckpt_sectiondelete;
	struct iovec iovecs[2];

	log_printf (LOG_LEVEL_DEBUG, "section delete from API fd %d\n", conn_info->fd);

	req_exec_ckpt_sectiondelete.header.id = MESSAGE_REQ_EXEC_CKPT_SECTIONDELETE;
	req_exec_ckpt_sectiondelete.header.size = sizeof (struct req_exec_ckpt_sectiondelete);

	memcpy (&req_exec_ckpt_sectiondelete.checkpointName,
		&req_lib_ckpt_sectiondelete->checkpointName,
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
		assert (totempg_mcast (iovecs, 2, TOTEMPG_AGREED) == 0);
	} else {
		assert (totempg_mcast (iovecs, 1, TOTEMPG_AGREED) == 0);
	}

	return (0);
}

static int message_handler_req_lib_ckpt_sectionexpirationtimeset (struct conn_info *conn_info, void *message)
{
	struct req_lib_ckpt_sectionexpirationtimeset *req_lib_ckpt_sectionexpirationtimeset = (struct req_lib_ckpt_sectionexpirationtimeset *)message;
	struct req_exec_ckpt_sectionexpirationtimeset req_exec_ckpt_sectionexpirationtimeset;
	struct iovec iovecs[2];

	log_printf (LOG_LEVEL_DEBUG, "section expiration time set fd=%d\n", conn_info->fd);
	req_exec_ckpt_sectionexpirationtimeset.header.id = MESSAGE_REQ_EXEC_CKPT_SECTIONEXPIRATIONTIMESET;
	req_exec_ckpt_sectionexpirationtimeset.header.size = sizeof (struct req_exec_ckpt_sectionexpirationtimeset);

	memcpy (&req_exec_ckpt_sectionexpirationtimeset.checkpointName,
		&req_lib_ckpt_sectionexpirationtimeset->checkpointName,
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
		log_printf (LOG_LEVEL_DEBUG, "IOV_BASE is %p\n", iovecs[1].iov_base);
		assert (totempg_mcast (iovecs, 2, TOTEMPG_AGREED) == 0);
	} else {
		assert (totempg_mcast (iovecs, 1, TOTEMPG_AGREED) == 0);
	}

	return (0);
}

int write_inv = 0;
static int message_handler_req_lib_ckpt_sectionwrite (struct conn_info *conn_info, void *message)
{
	struct req_lib_ckpt_sectionwrite *req_lib_ckpt_sectionwrite = (struct req_lib_ckpt_sectionwrite *)message;
	struct req_exec_ckpt_sectionwrite req_exec_ckpt_sectionwrite;
	struct iovec iovecs[2];
	struct saCkptCheckpoint *checkpoint;

	log_printf (LOG_LEVEL_DEBUG, "Section write from API fd %d\n", conn_info->fd);
	checkpoint = ckpt_checkpoint_find_global (&req_lib_ckpt_sectionwrite->checkpointName);

	/*
	 * checkpoint opened is writeable mode so send message to cluster
	 */
	req_exec_ckpt_sectionwrite.header.id = MESSAGE_REQ_EXEC_CKPT_SECTIONWRITE;
	req_exec_ckpt_sectionwrite.header.size = sizeof (struct req_exec_ckpt_sectionwrite);

	memcpy (&req_exec_ckpt_sectionwrite.req_lib_ckpt_sectionwrite,
		req_lib_ckpt_sectionwrite,
		sizeof (struct req_lib_ckpt_sectionwrite));

	memcpy (&req_exec_ckpt_sectionwrite.checkpointName,
		&req_lib_ckpt_sectionwrite->checkpointName,
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
		assert (totempg_mcast (iovecs, 2, TOTEMPG_AGREED) == 0);
	} else {
		assert (totempg_mcast (iovecs, 1, TOTEMPG_AGREED) == 0);
	}

	return (0);
}

static int message_handler_req_lib_ckpt_sectionoverwrite (struct conn_info *conn_info, void *message)
{
	struct req_lib_ckpt_sectionoverwrite *req_lib_ckpt_sectionoverwrite = (struct req_lib_ckpt_sectionoverwrite *)message;
	struct req_exec_ckpt_sectionoverwrite req_exec_ckpt_sectionoverwrite;
	struct iovec iovecs[2];
	struct saCkptCheckpoint *checkpoint;

	log_printf (LOG_LEVEL_DEBUG, "Section overwrite from API fd %d\n", conn_info->fd);
	checkpoint = ckpt_checkpoint_find_global (&req_lib_ckpt_sectionoverwrite->checkpointName);

	/*
	 * checkpoint opened is writeable mode so send message to cluster
	 */
	req_exec_ckpt_sectionoverwrite.header.id = MESSAGE_REQ_EXEC_CKPT_SECTIONOVERWRITE;
	req_exec_ckpt_sectionoverwrite.header.size = sizeof (struct req_exec_ckpt_sectionoverwrite);

	memcpy (&req_exec_ckpt_sectionoverwrite.req_lib_ckpt_sectionoverwrite,
		req_lib_ckpt_sectionoverwrite,
		sizeof (struct req_lib_ckpt_sectionoverwrite));

	memcpy (&req_exec_ckpt_sectionoverwrite.checkpointName,
		&req_lib_ckpt_sectionoverwrite->checkpointName,
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
		assert (totempg_mcast (iovecs, 2, TOTEMPG_AGREED) == 0);
	} else {
		assert (totempg_mcast (iovecs, 1, TOTEMPG_AGREED) == 0);
	}

	return (0);
}

static int message_handler_req_lib_ckpt_sectionread (struct conn_info *conn_info, void *message)
{
	struct req_lib_ckpt_sectionread *req_lib_ckpt_sectionread = (struct req_lib_ckpt_sectionread *)message;
	struct req_exec_ckpt_sectionread req_exec_ckpt_sectionread;
	struct iovec iovecs[2];
	struct saCkptCheckpoint *checkpoint;

	log_printf (LOG_LEVEL_DEBUG, "Section overwrite from API fd %d\n", conn_info->fd);
	checkpoint = ckpt_checkpoint_find_global (&req_lib_ckpt_sectionread->checkpointName);

	/*
	 * checkpoint opened is writeable mode so send message to cluster
	 */
	req_exec_ckpt_sectionread.header.id = MESSAGE_REQ_EXEC_CKPT_SECTIONREAD;
	req_exec_ckpt_sectionread.header.size = sizeof (struct req_exec_ckpt_sectionread);

	memcpy (&req_exec_ckpt_sectionread.req_lib_ckpt_sectionread,
		req_lib_ckpt_sectionread,
		sizeof (struct req_lib_ckpt_sectionread));

	memcpy (&req_exec_ckpt_sectionread.checkpointName,
		&req_lib_ckpt_sectionread->checkpointName,
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
		assert (totempg_mcast (iovecs, 2, TOTEMPG_AGREED) == 0);
	} else {
		assert (totempg_mcast (iovecs, 1, TOTEMPG_AGREED) == 0);
	}

	return (0);
}

static int message_handler_req_lib_ckpt_checkpointsynchronize (struct conn_info *conn_info, void *message)
{
	return (0);
}

static int message_handler_req_lib_ckpt_checkpointsynchronizeasync (struct conn_info *conn_info, void *message)
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
	SaErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_DEBUG, "section iterator initialize\n");
	ckptSectionIterator = &conn_info->ais_ci.u.libckpt_ci.sectionIterator;

	ckptCheckpoint = ckpt_checkpoint_find_global (&req_lib_ckpt_sectioniteratorinitialize->checkpointName);
	if (ckptCheckpoint == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
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
				error = SA_AIS_ERR_NO_MEMORY;
				goto error_exit;
			}
			ckptSectionIteratorEntries[iteratorEntries - 1].active = 1;
			ckptSectionIteratorEntries[iteratorEntries - 1].checkpointSection = ckptCheckpointSection;
			ckptSectionIterator->sectionIteratorEntries = ckptSectionIteratorEntries;
		}
	}
	ckptSectionIterator->iteratorCount = iteratorEntries;

error_exit:
	res_lib_ckpt_sectioniteratorinitialize.header.size = sizeof (struct res_lib_ckpt_sectioniteratorinitialize);
	res_lib_ckpt_sectioniteratorinitialize.header.id = MESSAGE_RES_CKPT_SECTIONITERATOR_SECTIONITERATORINITIALIZE;
	res_lib_ckpt_sectioniteratorinitialize.header.error = error;

	libais_send_response (conn_info, &res_lib_ckpt_sectioniteratorinitialize,
		sizeof (struct res_lib_ckpt_sectioniteratorinitialize));

	return (0);
}

static int message_handler_req_lib_ckpt_sectioniteratornext (struct conn_info *conn_info, void *message)
{
	struct req_lib_ckpt_sectioniteratornext *req_lib_ckpt_sectioniteratornext = (struct req_lib_ckpt_sectioniteratornext *)message;
	struct res_lib_ckpt_sectioniteratornext res_lib_ckpt_sectioniteratornext;
	struct saCkptSectionIterator *ckptSectionIterator;
	SaErrorT error = SA_AIS_OK;
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
			error = SA_AIS_ERR_NOT_EXIST;
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
	res_lib_ckpt_sectioniteratornext.header.size = sizeof (struct res_lib_ckpt_sectioniteratornext) + sectionIdSize;
	res_lib_ckpt_sectioniteratornext.header.id = MESSAGE_RES_CKPT_SECTIONITERATOR_SECTIONITERATORNEXT;
	res_lib_ckpt_sectioniteratornext.header.error = error;

	libais_send_response (conn_info, &res_lib_ckpt_sectioniteratornext,
		sizeof (struct res_lib_ckpt_sectioniteratornext));

	libais_send_response (conn_info,
		ckptSectionIterator->sectionIteratorEntries[iteratorPos].checkpointSection->sectionDescriptor.sectionId.id,
		sectionIdSize);
	return (0);
}
