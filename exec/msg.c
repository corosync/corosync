/*
 * Copyright (c) 2005-2006 MontaVista Software, Inc.
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
#include <arpa/inet.h>

#include "../include/saAis.h"
#include "../include/saMsg.h"
#include "../include/ipc_msg.h"
#include "../include/list.h"
#include "../include/queue.h"
#include "../lcr/lcr_comp.h"
#include "objdb.h"
#include "totem.h"
#include "service.h"
#include "mempool.h"
#include "util.h"
#include "main.h"
#include "flow.h"
#include "tlist.h"
#include "ipc.h"
#include "totempg.h"
#include "logsys.h"

LOGSYS_DECLARE_SUBSYS ("MSG", LOG_INFO);

enum msg_exec_message_req_types {
	MESSAGE_REQ_EXEC_MSG_QUEUEOPEN = 0,
	MESSAGE_REQ_EXEC_MSG_QUEUECLOSE = 1,
	MESSAGE_REQ_EXEC_MSG_QUEUESTATUSGET = 2,
	MESSAGE_REQ_EXEC_MSG_QUEUEUNLINK = 3,
	MESSAGE_REQ_EXEC_MSG_QUEUEGROUPCREATE = 4,
	MESSAGE_REQ_EXEC_MSG_QUEUEGROUPINSERT = 5,
	MESSAGE_REQ_EXEC_MSG_QUEUEGROUPREMOVE = 6,
	MESSAGE_REQ_EXEC_MSG_QUEUEGROUPDELETE = 7,
	MESSAGE_REQ_EXEC_MSG_QUEUEGROUPTRACK = 8,
	MESSAGE_REQ_EXEC_MSG_QUEUEGROUPTRACKSTOP = 9,
	MESSAGE_REQ_EXEC_MSG_MESSAGESEND = 10,
	MESSAGE_REQ_EXEC_MSG_MESSAGEGET = 11,
	MESSAGE_REQ_EXEC_MSG_MESSAGECANCEL = 12,
	MESSAGE_REQ_EXEC_MSG_MESSAGESENDRECEIVE = 13,
	MESSAGE_REQ_EXEC_MSG_MESSAGEREPLY = 14
};

struct message_queue {
	SaNameT name;
	int refcount;
	struct list_head list;
};

struct queue_group {
	SaNameT name;
	struct list_head list;
	struct list_head message_queue_head;
};	

struct queue_group_entry {
	struct message_queue *message_queue;
	struct list_head list;
};


/*
struct queue_cleanup {
	struct message_queue *queue;
	SaMsgResourceHandleT queue_handle;
	struct list_head queue_lock_list_head;
	struct list_head list;
};
*/

DECLARE_LIST_INIT(queue_list_head);
DECLARE_LIST_INIT(queue_group_list_head);

static int msg_exec_init_fn (struct objdb_iface_ver0 *objdb);

static int msg_lib_exit_fn (void *conn);

static int msg_lib_init_fn (void *conn);

static void message_handler_req_exec_msg_queueopen (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queueclose (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queuestatusget (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queueunlink (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queuegroupcreate (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queuegroupinsert (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queuegroupremove (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queuegroupdelete (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queuegrouptrack (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queuegrouptrackstop (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_messagesend (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_messageget (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_messagecancel (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_messagesendreceive (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_messagereply (
	void *message,
	unsigned int nodeid);

static void message_handler_req_lib_msg_queueopen (
	void *conn,
	void *msg);

static void message_handler_req_lib_msg_queueopenasync (
	void *conn,
	void *msg);

static void message_handler_req_lib_msg_queueclose (
	void *conn,
	void *msg);

static void message_handler_req_lib_msg_queuestatusget (
	void *conn,
	void *msg);

static void message_handler_req_lib_msg_queueunlink (
	void *conn,
	void *msg);

static void message_handler_req_lib_msg_queuegroupcreate (
	void *conn,
	void *msg);

static void message_handler_req_lib_msg_queuegroupinsert (
	void *conn,
	void *msg);

static void message_handler_req_lib_msg_queuegroupremove (
	void *conn,
	void *msg);

static void message_handler_req_lib_msg_queuegroupdelete (
	void *conn,
	void *msg);

static void message_handler_req_lib_msg_queuegrouptrack (
	void *conn,
	void *msg);

static void message_handler_req_lib_msg_queuegrouptrackstop (
	void *conn,
	void *msg);

static void message_handler_req_lib_msg_messagesend (
	void *conn,
	void *msg);

static void message_handler_req_lib_msg_messagesendasync (
	void *conn,
	void *msg);

static void message_handler_req_lib_msg_messageget (
	void *conn,
	void *msg);

static void message_handler_req_lib_msg_messagecancel (
	void *conn,
	void *msg);

static void message_handler_req_lib_msg_messagesendreceive (
	void *conn,
	void *msg);

static void message_handler_req_lib_msg_messagereply (
	void *conn,
	void *msg);

static void message_handler_req_lib_msg_messagereplyasync (
	void *conn,
	void *msg);

#ifdef TODO
static void msg_sync_init (void);
#endif
static void msg_sync_activate (void);
static int  msg_sync_process (void);
static void msg_sync_abort(void);

void queue_release (struct message_queue *queue);

static void msg_confchg_fn (
		enum totem_configuration_type configuration_type,
		unsigned int *member_list, int member_list_entries,
		unsigned int *left_list, int left_list_entries,
		unsigned int *joined_list, int joined_list_entries,
		struct memb_ring_id *ring_id);

struct msg_pd {
	struct list_head queue_list;
	struct list_head queue_cleanup_list;
};


/*
 * Executive Handler Definition
 */
struct openais_lib_handler msg_lib_service[] =
{
	{ /* 0 */
		.lib_handler_fn		= message_handler_req_lib_msg_queueopen,
		.response_size		= sizeof (struct res_lib_msg_queueopen),
		.response_id		= MESSAGE_RES_MSG_QUEUEOPEN,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 1 */
		.lib_handler_fn		= message_handler_req_lib_msg_queueopenasync,
		.response_size		= sizeof (struct res_lib_msg_queueopenasync),
		.response_id		= MESSAGE_RES_MSG_QUEUEOPENASYNC,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 2 */
		.lib_handler_fn		= message_handler_req_lib_msg_queueclose,
		.response_size		= sizeof (struct res_lib_msg_queueclose),
		.response_id		= MESSAGE_RES_MSG_QUEUECLOSE,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 3 */
		.lib_handler_fn		= message_handler_req_lib_msg_queuestatusget,
		.response_size		= sizeof (struct res_lib_msg_queuestatusget),
		.response_id		= MESSAGE_RES_MSG_QUEUESTATUSGET,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 4 */
		.lib_handler_fn		= message_handler_req_lib_msg_queueunlink,
		.response_size		= sizeof (struct res_lib_msg_queueunlink),
		.response_id		= MESSAGE_RES_MSG_QUEUEUNLINK,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 5 */
		.lib_handler_fn		= message_handler_req_lib_msg_queuegroupcreate,
		.response_size		= sizeof (struct res_lib_msg_queuegroupcreate),
		.response_id		= MESSAGE_RES_MSG_QUEUEGROUPCREATE,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 6 */
		.lib_handler_fn		= message_handler_req_lib_msg_queuegroupinsert,
		.response_size		= sizeof (struct res_lib_msg_queuegroupinsert),
		.response_id		= MESSAGE_RES_MSG_QUEUEGROUPINSERT,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 7 */
		.lib_handler_fn		= message_handler_req_lib_msg_queuegroupremove,
		.response_size		= sizeof (struct res_lib_msg_queuegroupremove),
		.response_id		= MESSAGE_RES_MSG_QUEUEGROUPREMOVE,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 8 */
		.lib_handler_fn		= message_handler_req_lib_msg_queuegroupdelete,
		.response_size		= sizeof (struct res_lib_msg_queuegroupdelete),
		.response_id		= MESSAGE_RES_MSG_QUEUEGROUPDELETE,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 9 */
		.lib_handler_fn		= message_handler_req_lib_msg_queuegrouptrack,
		.response_size		= sizeof (struct res_lib_msg_queuegrouptrack),
		.response_id		= MESSAGE_RES_MSG_QUEUEGROUPTRACK,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 10 */
		.lib_handler_fn		= message_handler_req_lib_msg_queuegrouptrackstop,
		.response_size		= sizeof (struct res_lib_msg_queuegrouptrackstop),
		.response_id		= MESSAGE_RES_MSG_QUEUEGROUPTRACKSTOP,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 11 */
		.lib_handler_fn		= message_handler_req_lib_msg_messagesend,
		.response_size		= sizeof (struct res_lib_msg_messagesend),
		.response_id		= MESSAGE_RES_MSG_MESSAGESEND,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 12 */
		.lib_handler_fn		= message_handler_req_lib_msg_messagesendasync,
		.response_size		= sizeof (struct res_lib_msg_messagesendasync),
		.response_id		= MESSAGE_RES_MSG_MESSAGESENDASYNC,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 13 */
		.lib_handler_fn		= message_handler_req_lib_msg_messageget,
		.response_size		= sizeof (struct res_lib_msg_messageget),
		.response_id		= MESSAGE_RES_MSG_MESSAGEGET,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 14 */
		.lib_handler_fn		= message_handler_req_lib_msg_messagecancel,
		.response_size		= sizeof (struct res_lib_msg_messagecancel),
		.response_id		= MESSAGE_RES_MSG_MESSAGECANCEL,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 15 */
		.lib_handler_fn		= message_handler_req_lib_msg_messagesendreceive,
		.response_size		= sizeof (struct res_lib_msg_messagesendreceive),
		.response_id		= MESSAGE_RES_MSG_MESSAGESENDRECEIVE,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 16 */
		.lib_handler_fn		= message_handler_req_lib_msg_messagereply,
		.response_size		= sizeof (struct res_lib_msg_messagereply),
		.response_id		= MESSAGE_RES_MSG_MESSAGEREPLY,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 17 */
		.lib_handler_fn		= message_handler_req_lib_msg_messagereplyasync,
		.response_size		= sizeof (struct res_lib_msg_messagereplyasync),
		.response_id		= MESSAGE_RES_MSG_MESSAGEREPLYASYNC,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
};


static struct openais_exec_handler msg_exec_service[] = {
	{
		.exec_handler_fn		= message_handler_req_exec_msg_queueopen,
	},
	{
		.exec_handler_fn		= message_handler_req_exec_msg_queueclose,
	},
	{
		.exec_handler_fn		= message_handler_req_exec_msg_queuestatusget,
	},
	{
		.exec_handler_fn		= message_handler_req_exec_msg_queueunlink,
	},
	{
		.exec_handler_fn		= message_handler_req_exec_msg_queuegroupcreate,
	},
	{
		.exec_handler_fn		= message_handler_req_exec_msg_queuegroupinsert,
	},
	{
		.exec_handler_fn		= message_handler_req_exec_msg_queuegroupremove,
	},
	{
		.exec_handler_fn		= message_handler_req_exec_msg_queuegroupdelete,
	},
	{
		.exec_handler_fn		= message_handler_req_exec_msg_queuegrouptrack,
	},
	{
		.exec_handler_fn		= message_handler_req_exec_msg_queuegrouptrackstop,
	},
	{
		.exec_handler_fn		= message_handler_req_exec_msg_messagesend,
	},
	{
		.exec_handler_fn		= message_handler_req_exec_msg_messageget,
	},
	{
		.exec_handler_fn		= message_handler_req_exec_msg_messagecancel,
	},
	{
		.exec_handler_fn		= message_handler_req_exec_msg_messagesendreceive,
	},
	{
		.exec_handler_fn		= message_handler_req_exec_msg_messagereply
	}
};

struct openais_service_handler msg_service_handler = {
	.name				= "openais message service B.01.01",
	.id				= MSG_SERVICE,
	.private_data_size		= sizeof (struct msg_pd),
	.flow_control			= OPENAIS_FLOW_CONTROL_NOT_REQUIRED, 
	.lib_init_fn			= msg_lib_init_fn,
	.lib_exit_fn			= msg_lib_exit_fn,
	.lib_service			= msg_lib_service,
	.lib_service_count		= sizeof (msg_lib_service) / sizeof (struct openais_lib_handler),
	.exec_init_fn			= msg_exec_init_fn,
	.exec_service			= msg_exec_service,
	.exec_service_count		= sizeof (msg_exec_service) / sizeof (struct openais_exec_handler),
	.confchg_fn			= msg_confchg_fn,
	.exec_dump_fn			= NULL,
	.sync_init			= NULL, // TODO msg_sync_init,
	.sync_process			= msg_sync_process,
	.sync_activate			= msg_sync_activate,
	.sync_abort			= msg_sync_abort
};

static struct openais_service_handler *msg_get_handler_ver0 (void);

static struct openais_service_handler_iface_ver0 msg_service_handler_iface = {
	.openais_get_service_handler_ver0	= msg_get_handler_ver0
};

static struct lcr_iface openais_msg_ver0[1] = {
	{
		.name			= "openais_msg",
		.version		= 0,
		.versions_replace	= 0,
		.versions_replace_count = 0,
		.dependencies		= 0,
		.dependency_count	= 0,
		.constructor		= NULL,
		.destructor		= NULL,
		.interfaces		= NULL
	}
};

static struct lcr_comp msg_comp_ver0 = {
	.iface_count			= 1,
	.ifaces				= openais_msg_ver0
};

static struct openais_service_handler *msg_get_handler_ver0 (void)
{
	return (&msg_service_handler);
}

__attribute__ ((constructor)) static void register_this_component (void) {
	lcr_interfaces_set (&openais_msg_ver0[0], &msg_service_handler_iface);

	lcr_component_register (&msg_comp_ver0);
}

/*
 * All data types used for executive messages
 */
struct req_exec_msg_queueopen {
	mar_req_header_t header;
	mar_message_source_t source;
	int async_call;
	SaNameT queue_name;
	SaInvocationT invocation;
	SaMsgQueueHandleT queue_handle;
	SaMsgQueueCreationAttributesT creation_attributes;
	SaMsgQueueOpenFlagsT openFlags;
	SaTimeT timeout;
};

struct req_exec_msg_queueclose {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_name;
};

struct req_exec_msg_queuestatusget {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_name;
};

struct req_exec_msg_queueunlink {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_name;
};

struct req_exec_msg_queuegroupcreate {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_group_name;
};
struct req_exec_msg_queuegroupinsert {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_name;
	SaNameT queue_group_name;
};
struct req_exec_msg_queuegroupremove {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_name;
	SaNameT queue_group_name;
};
struct req_exec_msg_queuegroupdelete {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_group_name;
};
struct req_exec_msg_queuegrouptrack {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_group_name;
};
struct req_exec_msg_queuegrouptrackstop {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_group_name;
};
struct req_exec_msg_messagesend {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT destination;
	int async_call;
};
struct req_exec_msg_messageget {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_name;
};
struct req_exec_msg_messagecancel {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_name;
};
struct req_exec_msg_messagesendreceive {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_name;
};
struct req_exec_msg_messagereply {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_name;
	int async_call;
};

#ifdef TODO
static void msg_sync_init (void) 
{
	return;
}
#endif

static int msg_sync_process (void) 
{
	return (0);
}

static void msg_sync_activate (void) 
{		
 	return;
}

static void msg_sync_abort (void) 
{
	return;
}

static void msg_confchg_fn (
	enum totem_configuration_type configuration_type,
	unsigned int *member_list, int member_list_entries,
	unsigned int *left_list, int left_list_entries,
	unsigned int *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id) 
{
}

static struct message_queue *queue_find (SaNameT *name)
{
	struct list_head *list;
	struct message_queue *queue;

	for (list = queue_list_head.next;
		list != &queue_list_head;
		list = list->next) {

	        queue = list_entry (list, struct message_queue, list);

		if (name_match (name, &queue->name)) {
			return (queue);
		}
	}
	return (0);
}

static struct queue_group *queue_group_find (SaNameT *name)
{
	struct list_head *list;
	struct queue_group *queue_group;

	for (list = queue_group_list_head.next;
		list != &queue_group_list_head;
		list = list->next) {

	        queue_group = list_entry (list, struct queue_group, list);

		if (name_match (name, &queue_group->name)) {
			return (queue_group);
		}
	}
	return (0);
}

static struct queue_group_entry *queue_group_entry_find (
	struct queue_group *queue_group,
	struct message_queue *queue)
{
	struct list_head *list;
	struct queue_group_entry *queue_group_entry;

	for (list = queue_group->message_queue_head.next;
		list != &queue_group->message_queue_head;
		list = list->next) {

	        queue_group_entry = list_entry (list, struct queue_group_entry, list);

		if (queue_group_entry->message_queue == queue) {
			return (queue_group_entry);
		}
	}
	return (0);
}

static int msg_exec_init_fn (struct objdb_iface_ver0 *objdb)
{
	/*
	 *  Initialize the saved ring ID.
	 */
//	saved_ring_id.seq = 0;
//	saved_ring_id.rep.s_addr = this_ip->sin_addr.s_addr;		
	
	return (0);
}

static int msg_lib_exit_fn (void *conn)
{
//	struct msg_pd *msg_pd = (struct msg_pd *)openais_conn_private_data_get (conn);
#ifdef COMPILE_OUT
	struct queue_cleanup *queue_cleanup;
	struct list_head *list;
	
printf ("exit_fn\n");

	log_printf(LOG_LEVEL_NOTICE, "msg_exit_fn conn_info = %p\n", conn);
	
	/*
	 * close all queues opened on this fd
	 */
	list = conn_info->conn_info_partner->ais_ci.u.libmsg_ci.queue_cleanup_list.next;	
	while (!list_empty(&conn_info->conn_info_partner->ais_ci.u.libmsg_ci.queue_cleanup_list)) {
		
		queue_cleanup = list_entry (list, struct queue_cleanup, list);
		
printf ("queue to cleanup\n");
		if (queue_cleanup->queue->name.length > 0)	{
			msg_queue_cleanup_lock_remove (queue_cleanup);
			msg_queue_close (queue_cleanup->queue);
		}
		
printf ("queue cleanup %x\n", queue_cleanup);
		list_del (&queue_cleanup->list);	
		free (queue_cleanup);
                
		list = conn_info->conn_info_partner->ais_ci.u.libmsg_ci.queue_cleanup_list.next;
	}
#endif

	return (0);
}

static int msg_lib_init_fn (void *conn)
{
	struct msg_pd *msg_pd = (struct msg_pd *)openais_conn_private_data_get (conn);

	list_init (&msg_pd->queue_list);
	list_init (&msg_pd->queue_cleanup_list);
	return (0);
}

static void message_handler_req_exec_msg_queueopen (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_msg_queueopen *req_exec_msg_queueopen = (struct req_exec_msg_queueopen *)message;
	struct res_lib_msg_queueopen res_lib_msg_queueopen;
	struct res_lib_msg_queueopenasync res_lib_msg_queueopenasync;
	struct message_queue *queue;
//	struct queue_cleanup *queue_cleanup;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_NOTICE, "EXEC request: saMsgQueueOpen %s\n",
		getSaNameT (&req_exec_msg_queueopen->queue_name));
	
	queue = queue_find (&req_exec_msg_queueopen->queue_name);

	printf ("queue %p\n", queue);
	/*
	 * If queue doesn't exist, create one
	 */
	if (queue == 0) {
		if ((req_exec_msg_queueopen->openFlags & SA_MSG_QUEUE_CREATE) == 0) {
			error = SA_AIS_ERR_NOT_EXIST;
			goto error_exit;
		}
		queue = malloc (sizeof (struct message_queue));
		if (queue == 0) {
			error = SA_AIS_ERR_NO_MEMORY;
			goto error_exit;
		}
		memset (queue, 0, sizeof (struct message_queue));

		memcpy (&queue->name,
			&req_exec_msg_queueopen->queue_name,
			sizeof (SaNameT));
		list_init (&queue->list);
		list_add (&queue->list, &queue_list_head);
		queue->refcount = 0;
	}
	queue->refcount += 1;
	printf ("Incrementing queue refcount to %d\n", queue->refcount);
#ifdef COMPILE_OUT
	/*
	 * Setup connection information and mark queue as referenced
	 */
	log_printf (LOG_LEVEL_DEBUG, "Lock queue opened is %p\n", queue);
	queue_cleanup = malloc (sizeof (struct queue_cleanup));
	if (queue_cleanup == 0) {
		free (queue);
		error = SA_AIS_ERR_NO_MEMORY;
	} else {
		list_init (&queue_cleanup->list);
		list_init (&queue_cleanup->queue_lock_list_head);
		queue_cleanup->queue = queue;
		queue_cleanup->queue_handle = req_exec_msg_queueopen->queue_handle;
		list_add (
			&queue_cleanup->list,
			&req_exec_msg_queueopen->source.conn_info->ais_ci.u.libmsg_ci.queue_cleanup_list);
	}
	queue->refcount += 1;
printf ("refcount == %d\n", queue->refcount);
#endif
	
	
	/*
	 * Send error result to MSG library
	 */
error_exit:
	/*
	 * If this node was the source of the message, respond to this node
	 */
	if (message_source_is_local (&req_exec_msg_queueopen->source)) {
		/*
		 * If its an async call respond with the invocation and handle
		 */
		if (req_exec_msg_queueopen->async_call) {
			res_lib_msg_queueopenasync.header.size = sizeof (struct res_lib_msg_queueopenasync);
			res_lib_msg_queueopenasync.header.id = MESSAGE_RES_MSG_QUEUEOPENASYNC;
			res_lib_msg_queueopenasync.header.error = error;
			res_lib_msg_queueopenasync.invocation = req_exec_msg_queueopen->invocation;
			memcpy (&res_lib_msg_queueopenasync.source,
				&req_exec_msg_queueopen->source,
				sizeof (mar_message_source_t));

			openais_conn_send_response (
				req_exec_msg_queueopen->source.conn,
				&res_lib_msg_queueopenasync,
				sizeof (struct res_lib_msg_queueopenasync));
			openais_conn_send_response (
				openais_conn_partner_get (req_exec_msg_queueopen->source.conn),
				&res_lib_msg_queueopenasync,
				sizeof (struct res_lib_msg_queueopenasync));
		} else {
			/*
			 * otherwise respond with the normal queueopen response
			 */
			res_lib_msg_queueopen.header.size = sizeof (struct res_lib_msg_queueopen);
			res_lib_msg_queueopen.header.id = MESSAGE_RES_MSG_QUEUEOPEN;
			res_lib_msg_queueopen.header.error = error;
			memcpy (&res_lib_msg_queueopen.source,
				&req_exec_msg_queueopen->source,
				sizeof (mar_message_source_t));

			openais_conn_send_response (
				req_exec_msg_queueopen->source.conn,
				&res_lib_msg_queueopen,
				sizeof (struct res_lib_msg_queueopen));
		}
	}
}

static void message_handler_req_exec_msg_queueclose (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_msg_queueclose *req_exec_msg_queueclose = (struct req_exec_msg_queueclose *)message;
	struct res_lib_msg_queueclose res_lib_msg_queueclose;
	struct message_queue *queue = 0;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_NOTICE, "EXEC request: saMsgQueueClose %s\n",
		getSaNameT (&req_exec_msg_queueclose->queue_name));

	queue = queue_find (&req_exec_msg_queueclose->queue_name);
	if (queue == 0) {
		goto error_exit;
	}
		
	queue->refcount -= 1;
	printf ("decrementing queue refcount to %d\n", queue->refcount);
	if (queue->refcount == 0) {
		printf ("should free queue\n");
	}
error_exit:
	if (message_source_is_local(&req_exec_msg_queueclose->source)) {
// TODO		msg_queue_cleanup_remove (
//			req_exec_msg_queueclose->source.conn_info,
//			req_exec_msg_queueclose->queue_handle);

		res_lib_msg_queueclose.header.size = sizeof (struct res_lib_msg_queueclose);
		res_lib_msg_queueclose.header.id = MESSAGE_RES_MSG_QUEUECLOSE;
		res_lib_msg_queueclose.header.error = error;
		openais_conn_send_response (req_exec_msg_queueclose->source.conn,
			&res_lib_msg_queueclose, sizeof (struct res_lib_msg_queueclose));
	}
}

static void message_handler_req_exec_msg_queuestatusget (
	void *message,
	unsigned int nodeid)
{
#if 0
	struct req_exec_msg_queuestatusget *req_exec_msg_queuestatusget =
		(struct req_exec_msg_queuestatusget *)message;
	struct res_lib_msg_queueclose res_lib_msg_queuestatusget;
#endif
}

static void message_handler_req_exec_msg_queueunlink (
	void *message,
	unsigned int nodeid)
{
#if 0
	struct req_exec_msg_queueunlink *req_exec_msg_queueunlink =
		(struct req_exec_msg_queueunlink *)message;
	struct res_lib_msg_queueclose res_lib_msg_queueunlink;
#endif
}

static void message_handler_req_exec_msg_queuegroupcreate (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_msg_queuegroupcreate *req_exec_msg_queuegroupcreate =
		(struct req_exec_msg_queuegroupcreate *)message;
	struct res_lib_msg_queuegroupcreate res_lib_msg_queuegroupcreate;
	struct queue_group *queue_group;
	SaAisErrorT error = SA_AIS_OK;

	queue_group = queue_group_find (&req_exec_msg_queuegroupcreate->queue_group_name);
	if (queue_group == 0) {
		queue_group = malloc (sizeof (struct queue_group));
		if (queue_group == 0) {
			error = SA_AIS_ERR_NO_MEMORY;
			goto error_exit;
		}
		memset (queue_group, 0, sizeof (struct queue_group));

		memcpy (&queue_group->name,
			&req_exec_msg_queuegroupcreate->queue_group_name,
			sizeof (SaNameT));
		list_init (&queue_group->list);
		list_init (&queue_group->message_queue_head);
		list_add (&queue_group->list, &queue_group_list_head);
	} else {
		error = SA_AIS_ERR_EXIST;
	}

error_exit:
	if (message_source_is_local(&req_exec_msg_queuegroupcreate->source)) {
		res_lib_msg_queuegroupcreate.header.size = sizeof (struct res_lib_msg_queuegroupcreate);
		res_lib_msg_queuegroupcreate.header.id = MESSAGE_RES_MSG_QUEUEGROUPCREATE;
		res_lib_msg_queuegroupcreate.header.error = error;

		openais_conn_send_response (
			req_exec_msg_queuegroupcreate->source.conn,
			&res_lib_msg_queuegroupcreate,
			sizeof (struct res_lib_msg_queuegroupcreate));
	}
}

static void message_handler_req_exec_msg_queuegroupinsert (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_msg_queuegroupinsert *req_exec_msg_queuegroupinsert =
		(struct req_exec_msg_queuegroupinsert *)message;
	struct res_lib_msg_queuegroupinsert res_lib_msg_queuegroupinsert;
	struct message_queue *queue;
	struct queue_group *queue_group;
	struct queue_group_entry *queue_group_entry;
	SaAisErrorT error = SA_AIS_OK;

	queue_group = queue_group_find (&req_exec_msg_queuegroupinsert->queue_group_name);
	if (queue_group == 0) {
printf ("a\n");
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}
	queue = queue_find (&req_exec_msg_queuegroupinsert->queue_name);
	if (queue == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}
	queue_group_entry = malloc (sizeof (struct queue_group_entry));
	if (queue_group_entry == 0) {
printf ("c\n");
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_exit;
	}	
	list_init (&queue_group_entry->list);
	list_add (&queue_group_entry->list, &queue_group->message_queue_head);
	list_add (&queue->list, &queue_list_head);
	queue_group_entry->message_queue = queue;

error_exit:
	if (message_source_is_local(&req_exec_msg_queuegroupinsert->source)) {
		res_lib_msg_queuegroupinsert.header.size = sizeof (struct res_lib_msg_queuegroupinsert);
		res_lib_msg_queuegroupinsert.header.id = MESSAGE_RES_MSG_QUEUEGROUPCREATE;
		res_lib_msg_queuegroupinsert.header.error = error;

		openais_conn_send_response (
			req_exec_msg_queuegroupinsert->source.conn,
			&res_lib_msg_queuegroupinsert,
			sizeof (struct res_lib_msg_queuegroupinsert));
	}
}

static void message_handler_req_exec_msg_queuegroupremove (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_msg_queuegroupremove *req_exec_msg_queuegroupremove =
		(struct req_exec_msg_queuegroupremove *)message;
	struct res_lib_msg_queuegroupremove res_lib_msg_queuegroupremove;
	struct queue_group *queue_group;
	struct message_queue *queue;
	struct queue_group_entry *queue_group_entry;
	SaAisErrorT error = SA_AIS_OK;

	queue_group = queue_group_find (&req_exec_msg_queuegroupremove->queue_group_name);
	if (queue_group == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	queue = queue_find (&req_exec_msg_queuegroupremove->queue_name);
	if (queue == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}
	queue_group_entry = queue_group_entry_find (queue_group, queue);
	if (queue_group_entry == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	list_del (&queue_group_entry->list);

error_exit:
	if (message_source_is_local(&req_exec_msg_queuegroupremove->source)) {
		res_lib_msg_queuegroupremove.header.size = sizeof (struct res_lib_msg_queuegroupremove);
		res_lib_msg_queuegroupremove.header.id = MESSAGE_RES_MSG_QUEUEGROUPCREATE;
		res_lib_msg_queuegroupremove.header.error = error;

		openais_conn_send_response (
			req_exec_msg_queuegroupremove->source.conn,
			&res_lib_msg_queuegroupremove,
			sizeof (struct res_lib_msg_queuegroupremove));
	}
}

static void message_handler_req_exec_msg_queuegroupdelete (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_msg_queuegroupdelete *req_exec_msg_queuegroupdelete =
		(struct req_exec_msg_queuegroupdelete *)message;
	struct res_lib_msg_queuegroupdelete res_lib_msg_queuegroupdelete;
	struct queue_group *queue_group;

	SaAisErrorT error = SA_AIS_OK;

	queue_group = queue_group_find (&req_exec_msg_queuegroupdelete->queue_group_name);
	if (queue_group) {
		list_del (&queue_group->list);
		free (queue_group);
	} else {
		error = SA_AIS_ERR_NOT_EXIST;
	}

	if (message_source_is_local(&req_exec_msg_queuegroupdelete->source)) {
		res_lib_msg_queuegroupdelete.header.size = sizeof (struct res_lib_msg_queuegroupdelete);
		res_lib_msg_queuegroupdelete.header.id = MESSAGE_RES_MSG_QUEUEGROUPCREATE;
		res_lib_msg_queuegroupdelete.header.error = error;

		openais_conn_send_response (
			req_exec_msg_queuegroupdelete->source.conn,
			&res_lib_msg_queuegroupdelete,
			sizeof (struct res_lib_msg_queuegroupdelete));
	}
}

static void message_handler_req_exec_msg_queuegrouptrack (
	void *message,
	unsigned int nodeid)
{
#if 0
	struct req_exec_msg_queuegrouptrack *req_exec_msg_queuegrouptrack =
		(struct req_exec_msg_queuegrouptrack *)message;
	struct res_lib_msg_queueclose res_lib_msg_queuegrouptrack;
#endif
}

static void message_handler_req_exec_msg_queuegrouptrackstop (
	void *message,
	unsigned int nodeid)
{
#if 0
	struct req_exec_msg_queuegrouptrackstop *req_exec_msg_queuegrouptrackstop =
		(struct req_exec_msg_queuegrouptrackstop *)message;
	struct res_lib_msg_queueclose res_lib_msg_queuegrouptrackstop;
#endif
}

static void message_handler_req_exec_msg_messagesend (
	void *message,
	unsigned int nodeid)
{
#if 0
	struct req_exec_msg_messagesend *req_exec_msg_messagesend =
		(struct req_exec_msg_messagesend *)message;
	struct res_lib_msg_queueclose res_lib_msg_messagesend;
#endif
}

static void message_handler_req_exec_msg_messageget (
	void *message,
	unsigned int nodeid)
{
#if 0
	struct req_exec_msg_messageget *req_exec_msg_messageget =
		(struct req_exec_msg_messageget *)message;
	struct res_lib_msg_queueclose res_lib_msg_messageget;
#endif
}

static void message_handler_req_exec_msg_messagecancel (
	void *message,
	unsigned int nodeid)
{
#if 0
	struct req_exec_msg_messagecancel *req_exec_msg_messagecancel =
		(struct req_exec_msg_messagecancel *)message;
	struct res_lib_msg_queueclose res_lib_msg_messagecancel;
#endif
}

static void message_handler_req_exec_msg_messagesendreceive (
	void *message,
	unsigned int nodeid)
{
#if 0
	struct req_exec_msg_messagesendreceive *req_exec_msg_messagesendreceive =
		(struct req_exec_msg_messagesendreceive *)message;
	struct res_lib_msg_queueclose res_lib_msg_messagesendreceive;
#endif
}

static void message_handler_req_exec_msg_messagereply (
	void *message,
	unsigned int nodeid)
{
#if 0
	struct req_exec_msg_messagereply *req_exec_msg_messagereply =
		(struct req_exec_msg_messagereply *)message;
	struct res_lib_msg_queueclose res_lib_msg_messagereply;
#endif
}


static void message_handler_req_lib_msg_queueopen (
	void *conn,
	void *msg)
{
	struct req_lib_msg_queueopen *req_lib_msg_queueopen = (struct req_lib_msg_queueopen *)msg;
	struct req_exec_msg_queueopen req_exec_msg_queueopen;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgQueueOpen %s\n",
		getSaNameT (&req_lib_msg_queueopen->queueName));

	req_exec_msg_queueopen.header.size =
		sizeof (struct req_exec_msg_queueopen);
	req_exec_msg_queueopen.header.id = 
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEOPEN);

	message_source_set (&req_exec_msg_queueopen.source, conn);

	memcpy (&req_exec_msg_queueopen.queue_name,
		&req_lib_msg_queueopen->queueName, sizeof (SaNameT));

	memcpy (&req_exec_msg_queueopen.creation_attributes,
		&req_lib_msg_queueopen->creationAttributes,
		sizeof (SaMsgQueueCreationAttributesT));

	req_exec_msg_queueopen.async_call = 0;
	req_exec_msg_queueopen.invocation = 0;
	req_exec_msg_queueopen.queue_handle = req_lib_msg_queueopen->queueHandle;
	req_exec_msg_queueopen.openFlags = req_lib_msg_queueopen->openFlags;
	req_exec_msg_queueopen.timeout = req_lib_msg_queueopen->timeout;
	iovec.iov_base = (char *)&req_exec_msg_queueopen;
	iovec.iov_len = sizeof (req_exec_msg_queueopen);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);
}

static void message_handler_req_lib_msg_queueopenasync (
	void *conn,
	void *msg)
{
	struct req_lib_msg_queueopen *req_lib_msg_queueopen = (struct req_lib_msg_queueopen *)msg;
	struct req_exec_msg_queueopen req_exec_msg_queueopen;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgQueueOpenAsync %s\n",
		getSaNameT (&req_lib_msg_queueopen->queueName));

	req_exec_msg_queueopen.header.size =
		sizeof (struct req_exec_msg_queueopen);
	req_exec_msg_queueopen.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEOPEN);

	message_source_set (&req_exec_msg_queueopen.source, conn);

	memcpy (&req_exec_msg_queueopen.queue_name,
		&req_lib_msg_queueopen->queueName, sizeof (SaNameT));

	memcpy (&req_exec_msg_queueopen.creation_attributes,
		&req_lib_msg_queueopen->creationAttributes,
		sizeof (SaMsgQueueCreationAttributesT));

	req_exec_msg_queueopen.async_call = 1;
	req_exec_msg_queueopen.invocation = req_lib_msg_queueopen->invocation;
	req_exec_msg_queueopen.queue_handle = req_lib_msg_queueopen->queueHandle;
	req_exec_msg_queueopen.openFlags = req_lib_msg_queueopen->openFlags;
	req_exec_msg_queueopen.timeout = SA_TIME_END;

	iovec.iov_base = (char *)&req_exec_msg_queueopen;
	iovec.iov_len = sizeof (req_exec_msg_queueopen);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);
}

static void message_handler_req_lib_msg_queueclose (
	void *conn,
	void *msg)
{
	struct req_lib_msg_queueclose *req_lib_msg_queueclose = (struct req_lib_msg_queueclose *)msg;
	struct req_exec_msg_queueclose req_exec_msg_queueclose;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgQueueClose %s\n",
		getSaNameT (&req_lib_msg_queueclose->queueName));

	req_exec_msg_queueclose.header.size =
		sizeof (struct req_exec_msg_queueclose);
	req_exec_msg_queueclose.header.id = 
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUECLOSE);

	message_source_set (&req_exec_msg_queueclose.source, conn);

	memcpy (&req_exec_msg_queueclose.queue_name,
		&req_lib_msg_queueclose->queueName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_queueclose;
	iovec.iov_len = sizeof (req_exec_msg_queueclose);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);
}

static void message_handler_req_lib_msg_queuestatusget (
	void *conn,
	void *msg)
{
	struct req_lib_msg_queuestatusget *req_lib_msg_queuestatusget =
		(struct req_lib_msg_queuestatusget *)msg;
	struct req_exec_msg_queuestatusget req_exec_msg_queuestatusget;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgQueueStatusGet %s\n",
		getSaNameT (&req_lib_msg_queuestatusget->queueName));

	req_exec_msg_queuestatusget.header.size =
		sizeof (struct req_exec_msg_queuestatusget);
	req_exec_msg_queuestatusget.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUESTATUSGET);

	message_source_set (&req_exec_msg_queuestatusget.source, conn);

	memcpy (&req_exec_msg_queuestatusget.queue_name,
		&req_lib_msg_queuestatusget->queueName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_queuestatusget;
	iovec.iov_len = sizeof (req_exec_msg_queuestatusget);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);
}

static void message_handler_req_lib_msg_queueunlink (
	void *conn,
	void *msg)
{
	struct req_lib_msg_queueunlink *req_lib_msg_queueunlink =
		(struct req_lib_msg_queueunlink *)msg;
	struct req_exec_msg_queueunlink req_exec_msg_queueunlink;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgQueueUnlink %s\n",
		getSaNameT (&req_lib_msg_queueunlink->queueName));

	req_exec_msg_queueunlink.header.size =
		sizeof (struct req_exec_msg_queueunlink);
	req_exec_msg_queueunlink.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEUNLINK);

	message_source_set (&req_exec_msg_queueunlink.source, conn);

	memcpy (&req_exec_msg_queueunlink.queue_name,
		&req_lib_msg_queueunlink->queueName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_queueunlink;
	iovec.iov_len = sizeof (req_exec_msg_queueunlink);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);
}

static void message_handler_req_lib_msg_queuegroupcreate (
	void *conn,
	void *msg)
{
	struct req_lib_msg_queuegroupcreate *req_lib_msg_queuegroupcreate =
		(struct req_lib_msg_queuegroupcreate *)msg;
	struct req_exec_msg_queuegroupcreate req_exec_msg_queuegroupcreate;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgQueueGroupCreate %s\n",
		getSaNameT (&req_lib_msg_queuegroupcreate->queueGroupName));

	req_exec_msg_queuegroupcreate.header.size =
		sizeof (struct req_exec_msg_queuegroupcreate);
	req_exec_msg_queuegroupcreate.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEGROUPCREATE);

	message_source_set (&req_exec_msg_queuegroupcreate.source, conn);

	memcpy (&req_exec_msg_queuegroupcreate.queue_group_name,
		&req_lib_msg_queuegroupcreate->queueGroupName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_queuegroupcreate;
	iovec.iov_len = sizeof (req_exec_msg_queuegroupcreate);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);
}

static void message_handler_req_lib_msg_queuegroupinsert (
	void *conn,
	void *msg)
{
	struct req_lib_msg_queuegroupinsert *req_lib_msg_queuegroupinsert =
		(struct req_lib_msg_queuegroupinsert *)msg;
	struct req_exec_msg_queuegroupinsert req_exec_msg_queuegroupinsert;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgQueueGroupInsert %s\n",
		getSaNameT (&req_lib_msg_queuegroupinsert->queueGroupName));

	req_exec_msg_queuegroupinsert.header.size =
		sizeof (struct req_exec_msg_queuegroupinsert);
	req_exec_msg_queuegroupinsert.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEGROUPINSERT);

	message_source_set (&req_exec_msg_queuegroupinsert.source, conn);

	memcpy (&req_exec_msg_queuegroupinsert.queue_name,
		&req_lib_msg_queuegroupinsert->queueName, sizeof (SaNameT));
	memcpy (&req_exec_msg_queuegroupinsert.queue_group_name,
		&req_lib_msg_queuegroupinsert->queueGroupName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_queuegroupinsert;
	iovec.iov_len = sizeof (req_exec_msg_queuegroupinsert);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);
}

static void message_handler_req_lib_msg_queuegroupremove (
	void *conn,
	void *msg)
{
	struct req_lib_msg_queuegroupremove *req_lib_msg_queuegroupremove =
		(struct req_lib_msg_queuegroupremove *)msg;
	struct req_exec_msg_queuegroupremove req_exec_msg_queuegroupremove;
	struct iovec iovec;

	req_exec_msg_queuegroupremove.header.size =
		sizeof (struct req_exec_msg_queuegroupremove);
	req_exec_msg_queuegroupremove.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEGROUPREMOVE);

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgQueueGroupRemove %s\n",
		getSaNameT (&req_lib_msg_queuegroupremove->queueGroupName));

	message_source_set (&req_exec_msg_queuegroupremove.source, conn);

	memcpy (&req_exec_msg_queuegroupremove.queue_name,
		&req_lib_msg_queuegroupremove->queueName, sizeof (SaNameT));
	memcpy (&req_exec_msg_queuegroupremove.queue_group_name,
		&req_lib_msg_queuegroupremove->queueGroupName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_queuegroupremove;
	iovec.iov_len = sizeof (req_exec_msg_queuegroupremove);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);
}

static void message_handler_req_lib_msg_queuegroupdelete (
	void *conn,
	void *msg)
{
	struct req_lib_msg_queuegroupdelete *req_lib_msg_queuegroupdelete =
		(struct req_lib_msg_queuegroupdelete *)msg;
	struct req_exec_msg_queuegroupdelete req_exec_msg_queuegroupdelete;
	struct iovec iovec;

	req_exec_msg_queuegroupdelete.header.size =
		sizeof (struct req_exec_msg_queuegroupdelete);
	req_exec_msg_queuegroupdelete.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEGROUPDELETE);

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgQueueGroupDelete %s\n",
		getSaNameT (&req_lib_msg_queuegroupdelete->queueGroupName));

	message_source_set (&req_exec_msg_queuegroupdelete.source, conn);

	memcpy (&req_exec_msg_queuegroupdelete.queue_group_name,
		&req_lib_msg_queuegroupdelete->queueGroupName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_queuegroupdelete;
	iovec.iov_len = sizeof (req_exec_msg_queuegroupdelete);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);
}

static void message_handler_req_lib_msg_queuegrouptrack (
	void *conn,
	void *msg)
{
	struct req_lib_msg_queuegrouptrack *req_lib_msg_queuegrouptrack =
		(struct req_lib_msg_queuegrouptrack *)msg;
	struct req_exec_msg_queuegrouptrack req_exec_msg_queuegrouptrack;
	struct iovec iovec;

	req_exec_msg_queuegrouptrack.header.size =
		sizeof (struct req_exec_msg_queuegrouptrack);
	req_exec_msg_queuegrouptrack.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEGROUPTRACK);

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgQueueGroupTrack %s\n",
		getSaNameT (&req_lib_msg_queuegrouptrack->queueGroupName));

	message_source_set (&req_exec_msg_queuegrouptrack.source, conn);

	memcpy (&req_exec_msg_queuegrouptrack.queue_group_name,
		&req_lib_msg_queuegrouptrack->queueGroupName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_queuegrouptrack;
	iovec.iov_len = sizeof (req_exec_msg_queuegrouptrack);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);
}

static void message_handler_req_lib_msg_queuegrouptrackstop (
	void *conn,
	void *msg)
{
	struct req_lib_msg_queuegrouptrackstop *req_lib_msg_queuegrouptrackstop =
		(struct req_lib_msg_queuegrouptrackstop *)msg;
	struct req_exec_msg_queuegrouptrackstop req_exec_msg_queuegrouptrackstop;
	struct iovec iovec;

	req_exec_msg_queuegrouptrackstop.header.size =
		sizeof (struct req_exec_msg_queuegrouptrackstop);
	req_exec_msg_queuegrouptrackstop.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEGROUPTRACKSTOP);

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgQueueGroupTrackStop %s\n",
		getSaNameT (&req_lib_msg_queuegrouptrackstop->queueGroupName));

	message_source_set (&req_exec_msg_queuegrouptrackstop.source, conn);

	memcpy (&req_exec_msg_queuegrouptrackstop.queue_group_name,
		&req_lib_msg_queuegrouptrackstop->queueGroupName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_queuegrouptrackstop;
	iovec.iov_len = sizeof (req_exec_msg_queuegrouptrackstop);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);
}

static void message_handler_req_lib_msg_messagesend (
	void *conn,
	void *msg)
{
	struct req_lib_msg_messagesend *req_lib_msg_messagesend =
		(struct req_lib_msg_messagesend *)msg;
	struct req_exec_msg_messagesend req_exec_msg_messagesend;
	struct iovec iovec;

	req_exec_msg_messagesend.header.size =
		sizeof (struct req_exec_msg_messagesend);
	req_exec_msg_messagesend.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGESEND);
	req_exec_msg_messagesend.async_call = 0;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgMessageSend %s\n",
		getSaNameT (&req_lib_msg_messagesend->destination));

	message_source_set (&req_exec_msg_messagesend.source, conn);

	memcpy (&req_exec_msg_messagesend.destination,
		&req_lib_msg_messagesend->destination, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_messagesend;
	iovec.iov_len = sizeof (req_exec_msg_messagesend);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);
}

static void message_handler_req_lib_msg_messagesendasync (
	void *conn,
	void *msg)
{
	struct req_lib_msg_messagesend *req_lib_msg_messagesend =
		(struct req_lib_msg_messagesend *)msg;
	struct req_exec_msg_messagesend req_exec_msg_messagesend;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgMessageSendAsync %s\n",
		getSaNameT (&req_lib_msg_messagesend->destination));

	req_exec_msg_messagesend.header.size =
		sizeof (struct req_exec_msg_messagesend);
	req_exec_msg_messagesend.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGESEND);
	req_exec_msg_messagesend.async_call = 1;

	message_source_set (&req_exec_msg_messagesend.source, conn);

	memcpy (&req_exec_msg_messagesend.destination,
		&req_lib_msg_messagesend->destination, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_messagesend;
	iovec.iov_len = sizeof (req_exec_msg_messagesend);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);
}

static void message_handler_req_lib_msg_messageget (
	void *conn,
	void *msg)
{
	struct req_lib_msg_messageget *req_lib_msg_messageget =
		(struct req_lib_msg_messageget *)msg;
	struct req_exec_msg_messageget req_exec_msg_messageget;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgMessageGet %s\n",
		getSaNameT (&req_lib_msg_messageget->queueName));

	req_exec_msg_messageget.header.size =
		sizeof (struct req_exec_msg_messageget);
	req_exec_msg_messageget.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGEGET);

	message_source_set (&req_exec_msg_messageget.source, conn);

	memcpy (&req_exec_msg_messageget.queue_name,
		&req_lib_msg_messageget->queueName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_messageget;
	iovec.iov_len = sizeof (req_exec_msg_messageget);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);
}

static void message_handler_req_lib_msg_messagecancel (
	void *conn,
	void *msg)
{
	struct req_lib_msg_messagecancel *req_lib_msg_messagecancel =
		(struct req_lib_msg_messagecancel *)msg;
	struct req_exec_msg_messagecancel req_exec_msg_messagecancel;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgMessageCancel %s\n",
		getSaNameT (&req_lib_msg_messagecancel->queueName));

	req_exec_msg_messagecancel.header.size =
		sizeof (struct req_exec_msg_messagecancel);
	req_exec_msg_messagecancel.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGECANCEL);

	message_source_set (&req_exec_msg_messagecancel.source, conn);

	memcpy (&req_exec_msg_messagecancel.queue_name,
		&req_lib_msg_messagecancel->queueName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_messagecancel;
	iovec.iov_len = sizeof (req_exec_msg_messagecancel);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);
}

static void message_handler_req_lib_msg_messagesendreceive (
	void *conn,
	void *msg)
{
	struct req_lib_msg_messagesendreceive *req_lib_msg_messagesendreceive =
		(struct req_lib_msg_messagesendreceive *)msg;
	struct req_exec_msg_messagesendreceive req_exec_msg_messagesendreceive;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgMessageSendReceive %s\n",
		getSaNameT (&req_lib_msg_messagesendreceive->queueName));

	req_exec_msg_messagesendreceive.header.size =
		sizeof (struct req_exec_msg_messagesendreceive);
	req_exec_msg_messagesendreceive.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGESENDRECEIVE);

	message_source_set (&req_exec_msg_messagesendreceive.source, conn);

	memcpy (&req_exec_msg_messagesendreceive.queue_name,
		&req_lib_msg_messagesendreceive->queueName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_messagesendreceive;
	iovec.iov_len = sizeof (req_exec_msg_messagesendreceive);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);
}

static void message_handler_req_lib_msg_messagereply (
	void *conn,
	void *msg)
{
	struct req_lib_msg_messagereply *req_lib_msg_messagereply =
		(struct req_lib_msg_messagereply *)msg;
	struct req_exec_msg_messagereply req_exec_msg_messagereply;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgMessageReply %s\n",
		getSaNameT (&req_lib_msg_messagereply->queueName));

	req_exec_msg_messagereply.header.size =
		sizeof (struct req_exec_msg_messagereply);
	req_exec_msg_messagereply.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGEREPLY);
	req_exec_msg_messagereply.async_call = 0;

	message_source_set (&req_exec_msg_messagereply.source, conn);

	memcpy (&req_exec_msg_messagereply.queue_name,
		&req_lib_msg_messagereply->queueName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_messagereply;
	iovec.iov_len = sizeof (req_exec_msg_messagereply);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);
}

static void message_handler_req_lib_msg_messagereplyasync (
	void *conn,
	void *msg)
{
	struct req_lib_msg_messagereply *req_lib_msg_messagereply =
		(struct req_lib_msg_messagereply *)msg;
	struct req_exec_msg_messagereply req_exec_msg_messagereply;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgMessageReplyAsync %s\n",
		getSaNameT (&req_lib_msg_messagereply->queueName));

	req_exec_msg_messagereply.header.size =
		sizeof (struct req_exec_msg_messagereply);
	req_exec_msg_messagereply.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGEREPLY);
	req_exec_msg_messagereply.async_call = 1;

	message_source_set (&req_exec_msg_messagereply.source, conn);

	memcpy (&req_exec_msg_messagereply.queue_name,
		&req_lib_msg_messagereply->queueName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_messagereply;
	iovec.iov_len = sizeof (req_exec_msg_messagereply);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);
}
