/*
 * Copyright (c) 2005 MontaVista Software, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
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
#include "aispoll.h"
#include "mempool.h"
#include "util.h"
#include "main.h"
#include "totempg.h"

#define LOG_SERVICE LOG_SERVICE_MSG
#include "print.h"

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

static int msg_exec_init_fn (struct openais_config *);

static int msg_exit_fn (struct conn_info *conn_info);

static int msg_init_two_fn (struct conn_info *conn_info);

static int message_handler_req_exec_msg_queueopen (
	void *message,
	struct totem_ip_address *source_addr,
	int endian_conversion_required);

static int message_handler_req_exec_msg_queueclose (
	void *message,
	struct totem_ip_address *source_addr,
	int endian_conversion_required);

static int message_handler_req_exec_msg_queuestatusget (
	void *message,
	struct totem_ip_address *source_addr,
	int endian_conversion_required);

static int message_handler_req_exec_msg_queueunlink (
	void *message,
	struct totem_ip_address *source_addr,
	int endian_conversion_required);

static int message_handler_req_exec_msg_queuegroupcreate (
	void *message,
	struct totem_ip_address *source_addr,
	int endian_conversion_required);

static int message_handler_req_exec_msg_queuegroupinsert (
	void *message,
	struct totem_ip_address *source_addr,
	int endian_conversion_required);

static int message_handler_req_exec_msg_queuegroupremove (
	void *message,
	struct totem_ip_address *source_addr,
	int endian_conversion_required);

static int message_handler_req_exec_msg_queuegroupdelete (
	void *message,
	struct totem_ip_address *source_addr,
	int endian_conversion_required);

static int message_handler_req_exec_msg_queuegrouptrack (
	void *message,
	struct totem_ip_address *source_addr,
	int endian_conversion_required);

static int message_handler_req_exec_msg_queuegrouptrackstop (
	void *message,
	struct totem_ip_address *source_addr,
	int endian_conversion_required);

static int message_handler_req_exec_msg_messagesend (
	void *message,
	struct totem_ip_address *source_addr,
	int endian_conversion_required);

static int message_handler_req_exec_msg_messageget (
	void *message,
	struct totem_ip_address *source_addr,
	int endian_conversion_required);

static int message_handler_req_exec_msg_messagecancel (
	void *message,
	struct totem_ip_address *source_addr,
	int endian_conversion_required);

static int message_handler_req_exec_msg_messagesendreceive (
	void *message,
	struct totem_ip_address *source_addr,
	int endian_conversion_required);

static int message_handler_req_exec_msg_messagereply (
	void *message,
	struct totem_ip_address *source_addr,
	int endian_conversion_required);

static int message_handler_req_lib_msg_queueopen (
	struct conn_info *conn_info,
	void *message);

static int message_handler_req_lib_msg_queueopenasync (
	struct conn_info *conn_info,
	void *message);

static int message_handler_req_lib_msg_queueclose (
	struct conn_info *conn_info,
	void *message);

static int message_handler_req_lib_msg_queuestatusget (
	struct conn_info *conn_info,
	void *message);

static int message_handler_req_lib_msg_queueunlink (
	struct conn_info *conn_info,
	void *message);

static int message_handler_req_lib_msg_queuegroupcreate (
	struct conn_info *conn_info,
	void *message);

static int message_handler_req_lib_msg_queuegroupinsert (
	struct conn_info *conn_info,
	void *message);

static int message_handler_req_lib_msg_queuegroupremove (
	struct conn_info *conn_info,
	void *message);

static int message_handler_req_lib_msg_queuegroupdelete (
	struct conn_info *conn_info,
	void *message);

static int message_handler_req_lib_msg_queuegrouptrack (
	struct conn_info *conn_info,
	void *message);

static int message_handler_req_lib_msg_queuegrouptrackstop (
	struct conn_info *conn_info,
	void *message);

static int message_handler_req_lib_msg_messagesend (
	struct conn_info *conn_info,
	void *message);

static int message_handler_req_lib_msg_messagesendasync (
	struct conn_info *conn_info,
	void *message);

static int message_handler_req_lib_msg_messageget (
	struct conn_info *conn_info,
	void *message);

static int message_handler_req_lib_msg_messagecancel (
	struct conn_info *conn_info,
	void *message);

static int message_handler_req_lib_msg_messagesendreceive (
	struct conn_info *conn_info,
	void *message);

static int message_handler_req_lib_msg_messagereply (
	struct conn_info *conn_info,
	void *message);

static int message_handler_req_lib_msg_messagereplyasync (
	struct conn_info *conn_info,
	void *message);

static void msg_recovery_activate (void);
static void msg_recovery_initialize (void);
static int  msg_recovery_process (void);
static void msg_recovery_finalize();
static void msg_recovery_abort(void);

void queue_release (struct message_queue *queue);

/*
static struct list_head *recovery_msg_next = 0;
static struct list_head *recovery_msg_section_next = 0;
static int recovery_section_data_offset = 0;
static int recovery_section_send_flag = 0;
static int recovery_abort = 0;
*/

static struct memb_ring_id saved_ring_id;

static int msg_confchg_fn (
		enum totem_configuration_type configuration_type,
		struct totem_ip_address *member_list, int member_list_entries,
		struct totem_ip_address *left_list, int left_list_entries,
		struct totem_ip_address *joined_list, int joined_list_entries,
		struct memb_ring_id *ring_id);

/*
 * Executive Handler Definition
 */
struct libais_handler msg_libais_handlers[] =
{
	{ /* 0 */
		.libais_handler_fn	= message_handler_req_lib_msg_queueopen,
		.response_size		= sizeof (struct res_lib_msg_queueopen),
		.response_id		= MESSAGE_RES_MSG_QUEUEOPEN,
		.flow_control		= FLOW_CONTROL_REQUIRED
	},
	{ /* 1 */
		.libais_handler_fn	= message_handler_req_lib_msg_queueopenasync,
		.response_size		= sizeof (struct res_lib_msg_queueopenasync),
		.response_id		= MESSAGE_RES_MSG_QUEUEOPENASYNC,
		.flow_control		= FLOW_CONTROL_REQUIRED
	},
	{ /* 2 */
		.libais_handler_fn	= message_handler_req_lib_msg_queueclose,
		.response_size		= sizeof (struct res_lib_msg_queueclose),
		.response_id		= MESSAGE_RES_MSG_QUEUECLOSE,
		.flow_control		= FLOW_CONTROL_REQUIRED
	},
	{ /* 3 */
		.libais_handler_fn	= message_handler_req_lib_msg_queuestatusget,
		.response_size		= sizeof (struct res_lib_msg_queuestatusget),
		.response_id		= MESSAGE_RES_MSG_QUEUESTATUSGET,
		.flow_control		= FLOW_CONTROL_REQUIRED
	},
	{ /* 4 */
		.libais_handler_fn	= message_handler_req_lib_msg_queueunlink,
		.response_size		= sizeof (struct res_lib_msg_queueunlink),
		.response_id		= MESSAGE_RES_MSG_QUEUEUNLINK,
		.flow_control		= FLOW_CONTROL_REQUIRED
	},
	{ /* 5 */
		.libais_handler_fn	= message_handler_req_lib_msg_queuegroupcreate,
		.response_size		= sizeof (struct res_lib_msg_queuegroupcreate),
		.response_id		= MESSAGE_RES_MSG_QUEUEGROUPCREATE,
		.flow_control		= FLOW_CONTROL_REQUIRED
	},
	{ /* 6 */
		.libais_handler_fn	= message_handler_req_lib_msg_queuegroupinsert,
		.response_size		= sizeof (struct res_lib_msg_queuegroupinsert),
		.response_id		= MESSAGE_RES_MSG_QUEUEGROUPINSERT,
		.flow_control		= FLOW_CONTROL_REQUIRED
	},
	{ /* 7 */
		.libais_handler_fn	= message_handler_req_lib_msg_queuegroupremove,
		.response_size		= sizeof (struct res_lib_msg_queuegroupremove),
		.response_id		= MESSAGE_RES_MSG_QUEUEGROUPREMOVE,
		.flow_control		= FLOW_CONTROL_REQUIRED
	},
	{ /* 8 */
		.libais_handler_fn	= message_handler_req_lib_msg_queuegroupdelete,
		.response_size		= sizeof (struct res_lib_msg_queuegroupdelete),
		.response_id		= MESSAGE_RES_MSG_QUEUEGROUPDELETE,
		.flow_control		= FLOW_CONTROL_REQUIRED
	},
	{ /* 9 */
		.libais_handler_fn	= message_handler_req_lib_msg_queuegrouptrack,
		.response_size		= sizeof (struct res_lib_msg_queuegrouptrack),
		.response_id		= MESSAGE_RES_MSG_QUEUEGROUPTRACK,
		.flow_control		= FLOW_CONTROL_REQUIRED
	},
	{ /* 10 */
		.libais_handler_fn	= message_handler_req_lib_msg_queuegrouptrackstop,
		.response_size		= sizeof (struct res_lib_msg_queuegrouptrackstop),
		.response_id		= MESSAGE_RES_MSG_QUEUEGROUPTRACKSTOP,
		.flow_control		= FLOW_CONTROL_REQUIRED
	},
	{ /* 11 */
		.libais_handler_fn	= message_handler_req_lib_msg_messagesend,
		.response_size		= sizeof (struct res_lib_msg_messagesend),
		.response_id		= MESSAGE_RES_MSG_MESSAGESEND,
		.flow_control		= FLOW_CONTROL_REQUIRED
	},
	{ /* 12 */
		.libais_handler_fn	= message_handler_req_lib_msg_messagesendasync,
		.response_size		= sizeof (struct res_lib_msg_messagesendasync),
		.response_id		= MESSAGE_RES_MSG_MESSAGESENDASYNC,
		.flow_control		= FLOW_CONTROL_REQUIRED
	},
	{ /* 13 */
		.libais_handler_fn	= message_handler_req_lib_msg_messageget,
		.response_size		= sizeof (struct res_lib_msg_messageget),
		.response_id		= MESSAGE_RES_MSG_MESSAGEGET,
		.flow_control		= FLOW_CONTROL_REQUIRED
	},
	{ /* 14 */
		.libais_handler_fn	= message_handler_req_lib_msg_messagecancel,
		.response_size		= sizeof (struct res_lib_msg_messagecancel),
		.response_id		= MESSAGE_RES_MSG_MESSAGECANCEL,
		.flow_control		= FLOW_CONTROL_REQUIRED
	},
	{ /* 15 */
		.libais_handler_fn	= message_handler_req_lib_msg_messagesendreceive,
		.response_size		= sizeof (struct res_lib_msg_messagesendreceive),
		.response_id		= MESSAGE_RES_MSG_MESSAGESENDRECEIVE,
		.flow_control		= FLOW_CONTROL_REQUIRED
	},
	{ /* 16 */
		.libais_handler_fn	= message_handler_req_lib_msg_messagereply,
		.response_size		= sizeof (struct res_lib_msg_messagereply),
		.response_id		= MESSAGE_RES_MSG_MESSAGEREPLY,
		.flow_control		= FLOW_CONTROL_REQUIRED
	},
	{ /* 17 */
		.libais_handler_fn	= message_handler_req_lib_msg_messagereplyasync,
		.response_size		= sizeof (struct res_lib_msg_messagereplyasync),
		.response_id		= MESSAGE_RES_MSG_MESSAGEREPLYASYNC,
		.flow_control		= FLOW_CONTROL_REQUIRED
	},
};


static int (*msg_aisexec_handler_fns[]) (void *msg, struct totem_ip_address *source_addr, int endian_conversion_required) = {
	message_handler_req_exec_msg_queueopen,
	message_handler_req_exec_msg_queueclose,
	message_handler_req_exec_msg_queuestatusget,
	message_handler_req_exec_msg_queueunlink,
	message_handler_req_exec_msg_queuegroupcreate,
	message_handler_req_exec_msg_queuegroupinsert,
	message_handler_req_exec_msg_queuegroupremove,
	message_handler_req_exec_msg_queuegroupdelete,
	message_handler_req_exec_msg_queuegrouptrack,
	message_handler_req_exec_msg_queuegrouptrackstop,
	message_handler_req_exec_msg_messagesend,
	message_handler_req_exec_msg_messageget,
	message_handler_req_exec_msg_messagecancel,
	message_handler_req_exec_msg_messagesendreceive,
	message_handler_req_exec_msg_messagereply
};

struct service_handler msg_service_handler = {
	.name				= "openais message service",
	.id				= MSG_SERVICE,
	.libais_handlers		= msg_libais_handlers,
	.libais_handlers_count		= sizeof (msg_libais_handlers) / sizeof (struct libais_handler),
	.aisexec_handler_fns		= msg_aisexec_handler_fns,
	.aisexec_handler_fns_count	= sizeof (msg_aisexec_handler_fns) / sizeof (int (*)),
	.confchg_fn			= msg_confchg_fn,
	.libais_init_two_fn		= msg_init_two_fn,
	.libais_exit_fn			= msg_exit_fn,
	.exec_init_fn			= msg_exec_init_fn,
	.exec_dump_fn			= 0,
	.sync_init			= msg_recovery_initialize,
	.sync_process			= msg_recovery_process,
	.sync_activate			= msg_recovery_activate,
	.sync_abort			= msg_recovery_abort,
};

#ifdef BUILD_DYNAMIC
struct service_handler *msg_get_handler_ver0 (void);

struct aisexec_iface_ver0 msg_service_handler_iface = {
	.test				= NULL,
	.get_handler_ver0		= msg_get_handler_ver0
};

struct lcr_iface openais_msg_ver0[1] = {
	{
		.name			= "openais_msg",
		.version		= 0,
		.versions_replace	= 0,
		.versions_replace_count = 0,
		.dependencies		= 0,
		.dependency_count	= 0,
		.constructor		= NULL,
		.destructor		= NULL,
		.interfaces		= (void **)&msg_service_handler_iface,
	}
};

struct lcr_comp msg_comp_ver0 = {
	.iface_count			= 1,
	.ifaces				= openais_msg_ver0
};

extern int lcr_comp_get (struct lcr_comp **component)
{
	*component = &msg_comp_ver0;
	return (0);
}

struct service_handler *msg_get_handler_ver0 (void)
{
	return (&msg_service_handler);
}

#endif /* BUILD_DYNAMIC */
/*
 * All data types used for executive messages
 */
struct req_exec_msg_queueopen {
	struct req_header header;
	struct message_source source;
	int async_call;
	SaNameT queue_name;
	SaInvocationT invocation;
	SaMsgQueueHandleT queue_handle;
	SaMsgQueueCreationAttributesT creation_attributes;
	SaMsgQueueOpenFlagsT openFlags;
	SaTimeT timeout;
};

struct req_exec_msg_queueclose {
	struct req_header header;
	struct message_source source;
	SaNameT queue_name;
};

struct req_exec_msg_queuestatusget {
	struct req_header header;
	struct message_source source;
	SaNameT queue_name;
};

struct req_exec_msg_queueunlink {
	struct req_header header;
	struct message_source source;
	SaNameT queue_name;
};

struct req_exec_msg_queuegroupcreate {
	struct req_header header;
	struct message_source source;
	SaNameT queue_group_name;
};
struct req_exec_msg_queuegroupinsert {
	struct req_header header;
	struct message_source source;
	SaNameT queue_name;
	SaNameT queue_group_name;
};
struct req_exec_msg_queuegroupremove {
	struct req_header header;
	struct message_source source;
	SaNameT queue_name;
	SaNameT queue_group_name;
};
struct req_exec_msg_queuegroupdelete {
	struct req_header header;
	struct message_source source;
	SaNameT queue_group_name;
};
struct req_exec_msg_queuegrouptrack {
	struct req_header header;
	struct message_source source;
	SaNameT queue_group_name;
};
struct req_exec_msg_queuegrouptrackstop {
	struct req_header header;
	struct message_source source;
	SaNameT queue_group_name;
};
struct req_exec_msg_messagesend {
	struct req_header header;
	struct message_source source;
	SaNameT destination;
	int async_call;
};
struct req_exec_msg_messageget {
	struct req_header header;
	struct message_source source;
	SaNameT queue_name;
};
struct req_exec_msg_messagecancel {
	struct req_header header;
	struct message_source source;
	SaNameT queue_name;
};
struct req_exec_msg_messagesendreceive {
	struct req_header header;
	struct message_source source;
	SaNameT queue_name;
};
struct req_exec_msg_messagereply {
	struct req_header header;
	struct message_source source;
	SaNameT queue_name;
	int async_call;
};

static void msg_recovery_initialize (void) 
{
	return;
}

static int msg_recovery_process (void) 
{
	return (0);
}

static void msg_recovery_finalize () 
{
	return;
	
}

static void msg_recovery_activate (void) 
{		
 	return;
}

static void msg_recovery_abort (void) 
{
	return;
}

static int msg_confchg_fn (
	enum totem_configuration_type configuration_type,
	struct totem_ip_address *member_list, int member_list_entries,
	struct totem_ip_address *left_list, int left_list_entries,
	struct totem_ip_address *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id) 
{
	return (0);
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

static int msg_exec_init_fn (struct openais_config *openais_config)
{
	/*
	 *  Initialize the saved ring ID.
	 */
//	saved_ring_id.seq = 0;
//	saved_ring_id.rep.s_addr = this_ip->sin_addr.s_addr;		
	
	return (0);
}

static int msg_exit_fn (struct conn_info *conn_info)
{
#ifdef COMPILE_OUT
	struct queue_cleanup *queue_cleanup;
	struct list_head *list;
	
printf ("exit_fn\n");
	if (conn_info->conn_info_partner->service != MSG_SERVICE) {
		return 0;
	}

	log_printf(LOG_LEVEL_NOTICE, "msg_exit_fn conn_info = %#x, with fd = %d\n", conn_info, conn_info->fd);
	
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

static int msg_init_two_fn (struct conn_info *conn_info)
{
	list_init (&conn_info->conn_info_partner->ais_ci.u.libmsg_ci.queue_list);
	list_init (&conn_info->conn_info_partner->ais_ci.u.libmsg_ci.queue_cleanup_list);

	return (0);

}

static int message_handler_req_exec_msg_queueopen (
	void *message,
	struct totem_ip_address *source_addr,
	int endian_conversion_required)
{
	struct req_exec_msg_queueopen *req_exec_msg_queueopen = (struct req_exec_msg_queueopen *)message;
	struct res_lib_msg_queueopen res_lib_msg_queueopen;
	struct res_lib_msg_queueopenasync res_lib_msg_queueopenasync;
	struct message_queue *queue;
//	struct queue_cleanup *queue_cleanup;
	SaErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_NOTICE, "EXEC request: saMsgQueueOpen %s\n",
		getSaNameT (&req_exec_msg_queueopen->queue_name));
	
	queue = queue_find (&req_exec_msg_queueopen->queue_name);

	printf ("queue %x\n", queue);
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
				sizeof (struct message_source));

			libais_send_response (
				req_exec_msg_queueopen->source.conn_info,
				&res_lib_msg_queueopenasync,
				sizeof (struct res_lib_msg_queueopenasync));
			libais_send_response (
				req_exec_msg_queueopen->source.conn_info->conn_info_partner,
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
				sizeof (struct message_source));

			libais_send_response (req_exec_msg_queueopen->source.conn_info, &res_lib_msg_queueopen,
				sizeof (struct res_lib_msg_queueopen));
		}
	}

	return (0);
}

static int message_handler_req_exec_msg_queueclose (
	void *message,
	struct totem_ip_address *source_addr,
	int endian_conversion_required)
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
		libais_send_response (req_exec_msg_queueclose->source.conn_info,
			&res_lib_msg_queueclose, sizeof (struct res_lib_msg_queueclose));
	}
	return (0);
}

static int message_handler_req_exec_msg_queuestatusget (
	void *message,
	struct totem_ip_address *source_addr,
	int endian_conversion_required)
{
	struct req_exec_msg_queuestatusget *req_exec_msg_queuestatusget =
		(struct req_exec_msg_queuestatusget *)message;
	struct res_lib_msg_queueclose res_lib_msg_queuestatusget;

	return (0);
}

static int message_handler_req_exec_msg_queueunlink (
	void *message,
	struct totem_ip_address *source_addr,
	int endian_conversion_required)
{
	struct req_exec_msg_queueunlink *req_exec_msg_queueunlink =
		(struct req_exec_msg_queueunlink *)message;
	struct res_lib_msg_queueclose res_lib_msg_queueunlink;

	return (0);
}

static int message_handler_req_exec_msg_queuegroupcreate (
	void *message,
	struct totem_ip_address *source_addr,
	int endian_conversion_required)
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

		libais_send_response (
			req_exec_msg_queuegroupcreate->source.conn_info,
			&res_lib_msg_queuegroupcreate,
			sizeof (struct res_lib_msg_queuegroupcreate));
	}
	return (0);
}

static int message_handler_req_exec_msg_queuegroupinsert (
	void *message,
	struct totem_ip_address *source_addr,
	int endian_conversion_required)
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

		libais_send_response (
			req_exec_msg_queuegroupinsert->source.conn_info,
			&res_lib_msg_queuegroupinsert,
			sizeof (struct res_lib_msg_queuegroupinsert));
	}
	return (0);
}

static int message_handler_req_exec_msg_queuegroupremove (
	void *message,
	struct totem_ip_address *source_addr,
	int endian_conversion_required)
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

		libais_send_response (
			req_exec_msg_queuegroupremove->source.conn_info,
			&res_lib_msg_queuegroupremove,
			sizeof (struct res_lib_msg_queuegroupremove));
	}
	return (0);
}

static int message_handler_req_exec_msg_queuegroupdelete (
	void *message,
	struct totem_ip_address *source_addr,
	int endian_conversion_required)
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

		libais_send_response (
			req_exec_msg_queuegroupdelete->source.conn_info,
			&res_lib_msg_queuegroupdelete,
			sizeof (struct res_lib_msg_queuegroupdelete));
	}
	return (0);
}

static int message_handler_req_exec_msg_queuegrouptrack (
	void *message,
	struct totem_ip_address *source_addr,
	int endian_conversion_required)
{
	struct req_exec_msg_queuegrouptrack *req_exec_msg_queuegrouptrack =
		(struct req_exec_msg_queuegrouptrack *)message;
	struct res_lib_msg_queueclose res_lib_msg_queuegrouptrack;

	return (0);
}

static int message_handler_req_exec_msg_queuegrouptrackstop (
	void *message,
	struct totem_ip_address *source_addr,
	int endian_conversion_required)
{
	struct req_exec_msg_queuegrouptrackstop *req_exec_msg_queuegrouptrackstop =
		(struct req_exec_msg_queuegrouptrackstop *)message;
	struct res_lib_msg_queueclose res_lib_msg_queuegrouptrackstop;

	return (0);
}

static int message_handler_req_exec_msg_messagesend (
	void *message,
	struct totem_ip_address *source_addr,
	int endian_conversion_required)
{
	struct req_exec_msg_messagesend *req_exec_msg_messagesend =
		(struct req_exec_msg_messagesend *)message;
	struct res_lib_msg_queueclose res_lib_msg_messagesend;

	return (0);
}

static int message_handler_req_exec_msg_messageget (
	void *message,
	struct totem_ip_address *source_addr,
	int endian_conversion_required)
{
	struct req_exec_msg_messageget *req_exec_msg_messageget =
		(struct req_exec_msg_messageget *)message;
	struct res_lib_msg_queueclose res_lib_msg_messageget;

	return (0);
}

static int message_handler_req_exec_msg_messagecancel (
	void *message,
	struct totem_ip_address *source_addr,
	int endian_conversion_required)
{
	struct req_exec_msg_messagecancel *req_exec_msg_messagecancel =
		(struct req_exec_msg_messagecancel *)message;
	struct res_lib_msg_queueclose res_lib_msg_messagecancel;

	return (0);
}

static int message_handler_req_exec_msg_messagesendreceive (
	void *message,
	struct totem_ip_address *source_addr,
	int endian_conversion_required)
{
	struct req_exec_msg_messagesendreceive *req_exec_msg_messagesendreceive =
		(struct req_exec_msg_messagesendreceive *)message;
	struct res_lib_msg_queueclose res_lib_msg_messagesendreceive;

	return (0);
}

static int message_handler_req_exec_msg_messagereply (
	void *message,
	struct totem_ip_address *source_addr,
	int endian_conversion_required)
{
	struct req_exec_msg_messagereply *req_exec_msg_messagereply =
		(struct req_exec_msg_messagereply *)message;
	struct res_lib_msg_queueclose res_lib_msg_messagereply;

	return (0);
}


static int message_handler_req_lib_msg_queueopen (struct conn_info *conn_info, void *message)
{
	struct req_lib_msg_queueopen *req_lib_msg_queueopen = (struct req_lib_msg_queueopen *)message;
	struct req_exec_msg_queueopen req_exec_msg_queueopen;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgQueueOpen %s\n",
		getSaNameT (&req_lib_msg_queueopen->queueName));

	req_exec_msg_queueopen.header.size =
		sizeof (struct req_exec_msg_queueopen);
	req_exec_msg_queueopen.header.id = 
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEOPEN);

	message_source_set (&req_exec_msg_queueopen.source, conn_info);

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

	return (0);
}

static int message_handler_req_lib_msg_queueopenasync (struct conn_info *conn_info, void *message)
{
	struct req_lib_msg_queueopen *req_lib_msg_queueopen = (struct req_lib_msg_queueopen *)message;
	struct req_exec_msg_queueopen req_exec_msg_queueopen;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgQueueOpenAsync %s\n",
		getSaNameT (&req_lib_msg_queueopen->queueName));

	req_exec_msg_queueopen.header.size =
		sizeof (struct req_exec_msg_queueopen);
	req_exec_msg_queueopen.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEOPEN);

	message_source_set (&req_exec_msg_queueopen.source, conn_info);

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

	return (0);
}

static int message_handler_req_lib_msg_queueclose (struct conn_info *conn_info, void *message) {
	struct req_lib_msg_queueclose *req_lib_msg_queueclose = (struct req_lib_msg_queueclose *)message;
	struct req_exec_msg_queueclose req_exec_msg_queueclose;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgQueueClose %s\n",
		getSaNameT (&req_lib_msg_queueclose->queueName));

	req_exec_msg_queueclose.header.size =
		sizeof (struct req_exec_msg_queueclose);
	req_exec_msg_queueclose.header.id = 
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUECLOSE);

	message_source_set (&req_exec_msg_queueclose.source, conn_info);

	memcpy (&req_exec_msg_queueclose.queue_name,
		&req_lib_msg_queueclose->queueName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_queueclose;
	iovec.iov_len = sizeof (req_exec_msg_queueclose);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);
	return (0);
}

static int message_handler_req_lib_msg_queuestatusget (
	struct conn_info *conn_info,
	void *message)
{
	struct req_lib_msg_queuestatusget *req_lib_msg_queuestatusget =
		(struct req_lib_msg_queuestatusget *)message;
	struct req_exec_msg_queuestatusget req_exec_msg_queuestatusget;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgQueueStatusGet %s\n",
		getSaNameT (&req_lib_msg_queuestatusget->queueName));

	req_exec_msg_queuestatusget.header.size =
		sizeof (struct req_exec_msg_queuestatusget);
	req_exec_msg_queuestatusget.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUESTATUSGET);

	message_source_set (&req_exec_msg_queuestatusget.source, conn_info);

	memcpy (&req_exec_msg_queuestatusget.queue_name,
		&req_lib_msg_queuestatusget->queueName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_queuestatusget;
	iovec.iov_len = sizeof (req_exec_msg_queuestatusget);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);

	return (0);
}

static int message_handler_req_lib_msg_queueunlink (
	struct conn_info *conn_info,
	void *message)
{
	struct req_lib_msg_queueunlink *req_lib_msg_queueunlink =
		(struct req_lib_msg_queueunlink *)message;
	struct req_exec_msg_queueunlink req_exec_msg_queueunlink;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgQueueUnlink %s\n",
		getSaNameT (&req_lib_msg_queueunlink->queueName));

	req_exec_msg_queueunlink.header.size =
		sizeof (struct req_exec_msg_queueunlink);
	req_exec_msg_queueunlink.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEUNLINK);

	message_source_set (&req_exec_msg_queueunlink.source, conn_info);

	memcpy (&req_exec_msg_queueunlink.queue_name,
		&req_lib_msg_queueunlink->queueName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_queueunlink;
	iovec.iov_len = sizeof (req_exec_msg_queueunlink);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);

	return (0);
}

static int message_handler_req_lib_msg_queuegroupcreate (
	struct conn_info *conn_info,
	void *message)
{
	struct req_lib_msg_queuegroupcreate *req_lib_msg_queuegroupcreate =
		(struct req_lib_msg_queuegroupcreate *)message;
	struct req_exec_msg_queuegroupcreate req_exec_msg_queuegroupcreate;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgQueueGroupCreate %s\n",
		getSaNameT (&req_lib_msg_queuegroupcreate->queueGroupName));

	req_exec_msg_queuegroupcreate.header.size =
		sizeof (struct req_exec_msg_queuegroupcreate);
	req_exec_msg_queuegroupcreate.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEGROUPCREATE);

	message_source_set (&req_exec_msg_queuegroupcreate.source, conn_info);

	memcpy (&req_exec_msg_queuegroupcreate.queue_group_name,
		&req_lib_msg_queuegroupcreate->queueGroupName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_queuegroupcreate;
	iovec.iov_len = sizeof (req_exec_msg_queuegroupcreate);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);

	return (0);
}

static int message_handler_req_lib_msg_queuegroupinsert (
	struct conn_info *conn_info,
	void *message)
{
	struct req_lib_msg_queuegroupinsert *req_lib_msg_queuegroupinsert =
		(struct req_lib_msg_queuegroupinsert *)message;
	struct req_exec_msg_queuegroupinsert req_exec_msg_queuegroupinsert;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgQueueGroupInsert %s\n",
		getSaNameT (&req_lib_msg_queuegroupinsert->queueGroupName));

	req_exec_msg_queuegroupinsert.header.size =
		sizeof (struct req_exec_msg_queuegroupinsert);
	req_exec_msg_queuegroupinsert.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEGROUPINSERT);

	message_source_set (&req_exec_msg_queuegroupinsert.source, conn_info);

	memcpy (&req_exec_msg_queuegroupinsert.queue_name,
		&req_lib_msg_queuegroupinsert->queueName, sizeof (SaNameT));
	memcpy (&req_exec_msg_queuegroupinsert.queue_group_name,
		&req_lib_msg_queuegroupinsert->queueGroupName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_queuegroupinsert;
	iovec.iov_len = sizeof (req_exec_msg_queuegroupinsert);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);

	return (0);
}

static int message_handler_req_lib_msg_queuegroupremove (
	struct conn_info *conn_info,
	void *message)
{
	struct req_lib_msg_queuegroupremove *req_lib_msg_queuegroupremove =
		(struct req_lib_msg_queuegroupremove *)message;
	struct req_exec_msg_queuegroupremove req_exec_msg_queuegroupremove;
	struct iovec iovec;

	req_exec_msg_queuegroupremove.header.size =
		sizeof (struct req_exec_msg_queuegroupremove);
	req_exec_msg_queuegroupremove.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEGROUPREMOVE);

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgQueueGroupRemove %s\n",
		getSaNameT (&req_lib_msg_queuegroupremove->queueGroupName));

	message_source_set (&req_exec_msg_queuegroupremove.source, conn_info);

	memcpy (&req_exec_msg_queuegroupremove.queue_name,
		&req_lib_msg_queuegroupremove->queueName, sizeof (SaNameT));
	memcpy (&req_exec_msg_queuegroupremove.queue_group_name,
		&req_lib_msg_queuegroupremove->queueGroupName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_queuegroupremove;
	iovec.iov_len = sizeof (req_exec_msg_queuegroupremove);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);

	return (0);
}

static int message_handler_req_lib_msg_queuegroupdelete (
	struct conn_info *conn_info,
	void *message)
{
	struct req_lib_msg_queuegroupdelete *req_lib_msg_queuegroupdelete =
		(struct req_lib_msg_queuegroupdelete *)message;
	struct req_exec_msg_queuegroupdelete req_exec_msg_queuegroupdelete;
	struct iovec iovec;

	req_exec_msg_queuegroupdelete.header.size =
		sizeof (struct req_exec_msg_queuegroupdelete);
	req_exec_msg_queuegroupdelete.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEGROUPDELETE);

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgQueueGroupDelete %s\n",
		getSaNameT (&req_lib_msg_queuegroupdelete->queueGroupName));

	message_source_set (&req_exec_msg_queuegroupdelete.source, conn_info);

	memcpy (&req_exec_msg_queuegroupdelete.queue_group_name,
		&req_lib_msg_queuegroupdelete->queueGroupName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_queuegroupdelete;
	iovec.iov_len = sizeof (req_exec_msg_queuegroupdelete);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);

	return (0);
}

static int message_handler_req_lib_msg_queuegrouptrack (
	struct conn_info *conn_info,
	void *message)
{
	struct req_lib_msg_queuegrouptrack *req_lib_msg_queuegrouptrack =
		(struct req_lib_msg_queuegrouptrack *)message;
	struct req_exec_msg_queuegrouptrack req_exec_msg_queuegrouptrack;
	struct iovec iovec;

	req_exec_msg_queuegrouptrack.header.size =
		sizeof (struct req_exec_msg_queuegrouptrack);
	req_exec_msg_queuegrouptrack.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEGROUPTRACK);

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgQueueGroupTrack %s\n",
		getSaNameT (&req_lib_msg_queuegrouptrack->queueGroupName));

	message_source_set (&req_exec_msg_queuegrouptrack.source, conn_info);

	memcpy (&req_exec_msg_queuegrouptrack.queue_group_name,
		&req_lib_msg_queuegrouptrack->queueGroupName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_queuegrouptrack;
	iovec.iov_len = sizeof (req_exec_msg_queuegrouptrack);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);

	return (0);
}

static int message_handler_req_lib_msg_queuegrouptrackstop (
	struct conn_info *conn_info,
	void *message)
{
	struct req_lib_msg_queuegrouptrackstop *req_lib_msg_queuegrouptrackstop =
		(struct req_lib_msg_queuegrouptrackstop *)message;
	struct req_exec_msg_queuegrouptrackstop req_exec_msg_queuegrouptrackstop;
	struct iovec iovec;

	req_exec_msg_queuegrouptrackstop.header.size =
		sizeof (struct req_exec_msg_queuegrouptrackstop);
	req_exec_msg_queuegrouptrackstop.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEGROUPTRACKSTOP);

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgQueueGroupTrackStop %s\n",
		getSaNameT (&req_lib_msg_queuegrouptrackstop->queueGroupName));

	message_source_set (&req_exec_msg_queuegrouptrackstop.source, conn_info);

	memcpy (&req_exec_msg_queuegrouptrackstop.queue_group_name,
		&req_lib_msg_queuegrouptrackstop->queueGroupName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_queuegrouptrackstop;
	iovec.iov_len = sizeof (req_exec_msg_queuegrouptrackstop);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);

	return (0);
}

static int message_handler_req_lib_msg_messagesend (
	struct conn_info *conn_info,
	void *message)
{
	struct req_lib_msg_messagesend *req_lib_msg_messagesend =
		(struct req_lib_msg_messagesend *)message;
	struct req_exec_msg_messagesend req_exec_msg_messagesend;
	struct iovec iovec;

	req_exec_msg_messagesend.header.size =
		sizeof (struct req_exec_msg_messagesend);
	req_exec_msg_messagesend.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGESEND);
	req_exec_msg_messagesend.async_call = 0;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgMessageSend %s\n",
		getSaNameT (&req_lib_msg_messagesend->destination));

	message_source_set (&req_exec_msg_messagesend.source, conn_info);

	memcpy (&req_exec_msg_messagesend.destination,
		&req_lib_msg_messagesend->destination, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_messagesend;
	iovec.iov_len = sizeof (req_exec_msg_messagesend);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);

	return (0);
}

static int message_handler_req_lib_msg_messagesendasync (
	struct conn_info *conn_info,
	void *message)
{
	struct req_lib_msg_messagesend *req_lib_msg_messagesend =
		(struct req_lib_msg_messagesend *)message;
	struct req_exec_msg_messagesend req_exec_msg_messagesend;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgMessageSendAsync %s\n",
		getSaNameT (&req_lib_msg_messagesend->destination));

	req_exec_msg_messagesend.header.size =
		sizeof (struct req_exec_msg_messagesend);
	req_exec_msg_messagesend.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGESEND);
	req_exec_msg_messagesend.async_call = 1;

	message_source_set (&req_exec_msg_messagesend.source, conn_info);

	memcpy (&req_exec_msg_messagesend.destination,
		&req_lib_msg_messagesend->destination, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_messagesend;
	iovec.iov_len = sizeof (req_exec_msg_messagesend);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);

	return (0);
}

static int message_handler_req_lib_msg_messageget (
	struct conn_info *conn_info,
	void *message)
{
	struct req_lib_msg_messageget *req_lib_msg_messageget =
		(struct req_lib_msg_messageget *)message;
	struct req_exec_msg_messageget req_exec_msg_messageget;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgMessageGet %s\n",
		getSaNameT (&req_lib_msg_messageget->queueName));

	req_exec_msg_messageget.header.size =
		sizeof (struct req_exec_msg_messageget);
	req_exec_msg_messageget.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGEGET);

	message_source_set (&req_exec_msg_messageget.source, conn_info);

	memcpy (&req_exec_msg_messageget.queue_name,
		&req_lib_msg_messageget->queueName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_messageget;
	iovec.iov_len = sizeof (req_exec_msg_messageget);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);

	return (0);
}

static int message_handler_req_lib_msg_messagecancel (
	struct conn_info *conn_info,
	void *message)
{
	struct req_lib_msg_messagecancel *req_lib_msg_messagecancel =
		(struct req_lib_msg_messagecancel *)message;
	struct req_exec_msg_messagecancel req_exec_msg_messagecancel;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgMessageCancel %s\n",
		getSaNameT (&req_lib_msg_messagecancel->queueName));

	req_exec_msg_messagecancel.header.size =
		sizeof (struct req_exec_msg_messagecancel);
	req_exec_msg_messagecancel.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGECANCEL);

	message_source_set (&req_exec_msg_messagecancel.source, conn_info);

	memcpy (&req_exec_msg_messagecancel.queue_name,
		&req_lib_msg_messagecancel->queueName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_messagecancel;
	iovec.iov_len = sizeof (req_exec_msg_messagecancel);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);

	return (0);
}

static int message_handler_req_lib_msg_messagesendreceive (
	struct conn_info *conn_info,
	void *message)
{
	struct req_lib_msg_messagesendreceive *req_lib_msg_messagesendreceive =
		(struct req_lib_msg_messagesendreceive *)message;
	struct req_exec_msg_messagesendreceive req_exec_msg_messagesendreceive;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgMessageSendReceive %s\n",
		getSaNameT (&req_lib_msg_messagesendreceive->queueName));

	req_exec_msg_messagesendreceive.header.size =
		sizeof (struct req_exec_msg_messagesendreceive);
	req_exec_msg_messagesendreceive.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGESENDRECEIVE);

	message_source_set (&req_exec_msg_messagesendreceive.source, conn_info);

	memcpy (&req_exec_msg_messagesendreceive.queue_name,
		&req_lib_msg_messagesendreceive->queueName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_messagesendreceive;
	iovec.iov_len = sizeof (req_exec_msg_messagesendreceive);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);

	return (0);
}

static int message_handler_req_lib_msg_messagereply (
	struct conn_info *conn_info,
	void *message)
{
	struct req_lib_msg_messagereply *req_lib_msg_messagereply =
		(struct req_lib_msg_messagereply *)message;
	struct req_exec_msg_messagereply req_exec_msg_messagereply;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgMessageReply %s\n",
		getSaNameT (&req_lib_msg_messagereply->queueName));

	req_exec_msg_messagereply.header.size =
		sizeof (struct req_exec_msg_messagereply);
	req_exec_msg_messagereply.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGEREPLY);
	req_exec_msg_messagereply.async_call = 0;

	message_source_set (&req_exec_msg_messagereply.source, conn_info);

	memcpy (&req_exec_msg_messagereply.queue_name,
		&req_lib_msg_messagereply->queueName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_messagereply;
	iovec.iov_len = sizeof (req_exec_msg_messagereply);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);

	return (0);
}

static int message_handler_req_lib_msg_messagereplyasync (
	struct conn_info *conn_info,
	void *message)
{
	struct req_lib_msg_messagereply *req_lib_msg_messagereply =
		(struct req_lib_msg_messagereply *)message;
	struct req_exec_msg_messagereply req_exec_msg_messagereply;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgMessageReplyAsync %s\n",
		getSaNameT (&req_lib_msg_messagereply->queueName));

	req_exec_msg_messagereply.header.size =
		sizeof (struct req_exec_msg_messagereply);
	req_exec_msg_messagereply.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGEREPLY);
	req_exec_msg_messagereply.async_call = 1;

	message_source_set (&req_exec_msg_messagereply.source, conn_info);

	memcpy (&req_exec_msg_messagereply.queue_name,
		&req_lib_msg_messagereply->queueName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_messagereply;
	iovec.iov_len = sizeof (req_exec_msg_messagereply);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);

	return (0);
}
