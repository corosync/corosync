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
#include <arpa/inet.h>

#include "../include/saAis.h"
#include "../include/saLck.h"
#include "../include/ipc_lck.h"
#include "../include/list.h"
#include "../include/queue.h"
#include "aispoll.h"
#include "mempool.h"
#include "util.h"
#include "main.h"
#include "totempg.h"

#define LOG_SERVICE LOG_SERVICE_LCK
#include "print.h"

struct resource;
struct resource_lock {
	SaLckLockModeT lock_mode;
	SaLckLockIdT lock_id;
	SaLckLockFlagsT lock_flags;
	SaLckWaiterSignalT waiter_signal;
	SaLckLockStatusT lock_status;
	SaTimeT timeout;
	struct resource *resource;
	int async_call;
	SaInvocationT invocation;
	struct message_source callback_source;
	struct message_source response_source;
	struct list_head list; /* locked resource lock list */
	struct list_head resource_list; /* resource locks on a resource */
	struct list_head resource_cleanup_list; /* cleanup data for resource locks */
};

struct resource {
	SaNameT name;
	int refcount;
	struct list_head list;
	struct list_head resource_lock_list_head;
	struct list_head pr_granted_list_head;
	struct list_head pr_pending_list_head;
	struct list_head ex_pending_list_head;
	struct resource_lock *ex_granted;
};

struct resource_cleanup {
	struct resource *resource;
	SaLckResourceHandleT resource_handle;
	struct list_head resource_lock_list_head;
	struct list_head list;
};

DECLARE_LIST_INIT(resource_list_head);

static int lck_exec_init_fn (struct openais_config *);

static int lck_exit_fn (struct conn_info *conn_info);

static int lck_init_two_fn (struct conn_info *conn_info);

static int message_handler_req_exec_lck_resourceopen (
	void *message,
	struct in_addr source_addr,
	int endian_conversion_required);

static int message_handler_req_exec_lck_resourceclose (
	void *message,
	struct in_addr source_addr,
	int endian_conversion_required);

static int message_handler_req_exec_lck_resourcelock (
	void *message,
	struct in_addr source_addr,
	int endian_conversion_required);

static int message_handler_req_exec_lck_resourceunlock (
	void *message,
	struct in_addr source_addr,
	int endian_conversion_required);

static int message_handler_req_exec_lck_resourcelockorphan (
	void *message,
	struct in_addr source_addr,
	int endian_conversion_required);

static int message_handler_req_exec_lck_lockpurge (
	void *message,
	struct in_addr source_addr,
	int endian_conversion_required);

static int message_handler_req_lib_lck_resourceopen (
	struct conn_info *conn_info,
	void *message);

static int message_handler_req_lib_lck_resourceopenasync (
	struct conn_info *conn_info,
	void *message);

static int message_handler_req_lib_lck_resourceclose (
	struct conn_info *conn_info,
	void *message);

static int message_handler_req_lib_lck_resourcelock (
	struct conn_info *conn_info,
	void *message);

static int message_handler_req_lib_lck_resourcelockasync (
	struct conn_info *conn_info,
	void *message);

static int message_handler_req_lib_lck_resourceunlock (
	struct conn_info *conn_info,
	void *message);

static int message_handler_req_lib_lck_resourceunlockasync (
	struct conn_info *conn_info,
	void *message);

static int message_handler_req_lib_lck_lockpurge (
	struct conn_info *conn_info,
	void *message);

static void lck_recovery_activate (void);
static void lck_recovery_initialize (void);
static int  lck_recovery_process (void);
static void lck_recovery_finalize();
static void lck_recovery_abort(void);

void resource_release (struct resource *resource);

/*
static struct list_head *recovery_lck_next = 0;
static struct list_head *recovery_lck_section_next = 0;
static int recovery_section_data_offset = 0;
static int recovery_section_send_flag = 0;
static int recovery_abort = 0;
*/

static struct memb_ring_id saved_ring_id;

static int lck_confchg_fn (
		enum totem_configuration_type configuration_type,
		struct in_addr *member_list, int member_list_entries,
		struct in_addr *left_list, int left_list_entries,
		struct in_addr *joined_list, int joined_list_entries,
		struct memb_ring_id *ring_id);

/*
 * Executive Handler Definition
 */
struct libais_handler lck_libais_handlers[] =
{
	{ /* 0 */
		.libais_handler_fn	= message_handler_req_lib_lck_resourceopen,
		.response_size		= sizeof (struct res_lib_lck_resourceopen),
		.response_id		= MESSAGE_RES_LCK_RESOURCEOPEN,
		.flow_control		= FLOW_CONTROL_REQUIRED
	},
	{ /* 1 */
		.libais_handler_fn	= message_handler_req_lib_lck_resourceopenasync,
		.response_size		= sizeof (struct res_lib_lck_resourceopenasync),
		.response_id		= MESSAGE_RES_LCK_RESOURCEOPENASYNC,
		.flow_control		= FLOW_CONTROL_REQUIRED
	},
	{ /* 2 */
		.libais_handler_fn	= message_handler_req_lib_lck_resourceclose,
		.response_size		= sizeof (struct res_lib_lck_resourceclose),
		.response_id		= MESSAGE_RES_LCK_RESOURCECLOSE,
		.flow_control		= FLOW_CONTROL_REQUIRED
	},
	{ /* 3 */
		.libais_handler_fn	= message_handler_req_lib_lck_resourcelock,
		.response_size		= sizeof (struct res_lib_lck_resourcelock),
		.response_id		= MESSAGE_RES_LCK_RESOURCELOCK,
		.flow_control		= FLOW_CONTROL_REQUIRED
	},
	{ /* 4 */
		.libais_handler_fn	= message_handler_req_lib_lck_resourcelockasync,
		.response_size		= sizeof (struct res_lib_lck_resourcelockasync),
		.response_id		= MESSAGE_RES_LCK_RESOURCELOCKASYNC,
		.flow_control		= FLOW_CONTROL_REQUIRED
	},
	{ /* 5 */
		.libais_handler_fn	= message_handler_req_lib_lck_resourceunlock,
		.response_size		= sizeof (struct res_lib_lck_resourceunlock),
		.response_id		= MESSAGE_RES_LCK_RESOURCELOCK,
		.flow_control		= FLOW_CONTROL_REQUIRED
	},
	{ /* 6 */
		.libais_handler_fn	= message_handler_req_lib_lck_resourceunlockasync,
		.response_size		= sizeof (struct res_lib_lck_resourceunlock),
		.response_id		= MESSAGE_RES_LCK_RESOURCEUNLOCKASYNC,
		.flow_control		= FLOW_CONTROL_REQUIRED
	},
	{ /* 7 */
		.libais_handler_fn	= message_handler_req_lib_lck_lockpurge,
		.response_size		= sizeof (struct res_lib_lck_lockpurge),
		.response_id		= MESSAGE_RES_LCK_LOCKPURGE,
		.flow_control		= FLOW_CONTROL_REQUIRED
	}
};


static int (*lck_aisexec_handler_fns[]) (void *msg, struct in_addr source_addr, int endian_conversion_required) = {
	message_handler_req_exec_lck_resourceopen,
	message_handler_req_exec_lck_resourceclose,
	message_handler_req_exec_lck_resourcelock,
	message_handler_req_exec_lck_resourceunlock,
	message_handler_req_exec_lck_resourcelockorphan,
	message_handler_req_exec_lck_lockpurge,
};

struct service_handler lck_service_handler = {
	.libais_handlers		= lck_libais_handlers,
	.libais_handlers_count		= sizeof (lck_libais_handlers) / sizeof (struct libais_handler),
	.aisexec_handler_fns		= lck_aisexec_handler_fns,
	.aisexec_handler_fns_count	= sizeof (lck_aisexec_handler_fns) / sizeof (int (*)),
	.confchg_fn			= lck_confchg_fn,
	.libais_init_two_fn		= lck_init_two_fn,
	.libais_exit_fn			= lck_exit_fn,
	.exec_init_fn			= lck_exec_init_fn,
	.exec_dump_fn			= 0,
	.sync_init			= lck_recovery_initialize,
	.sync_process			= lck_recovery_process,
	.sync_activate			= lck_recovery_activate,
	.sync_abort			= lck_recovery_abort,
};

/*
 * All data types used for executive messages
 */
struct req_exec_lck_resourceopen {
	struct req_header header;
	struct message_source source;
	SaNameT resource_name;
	SaLckResourceHandleT resource_handle;
	SaInvocationT invocation;
	SaTimeT timeout;
	SaLckResourceOpenFlagsT open_flags;
	int async_call;
	SaAisErrorT fail_with_error;
};

struct req_exec_lck_resourceclose {
	struct req_header header;
	struct message_source source;
	SaNameT lockResourceName;
	SaLckResourceHandleT resource_handle;
};

struct req_exec_lck_resourcelock {
	struct req_header header;
	SaLckResourceHandleT resource_handle;
	SaInvocationT invocation;
	int async_call;
	SaAisErrorT fail_with_error;
	struct message_source source;
	struct req_lib_lck_resourcelock req_lib_lck_resourcelock;
};

struct req_exec_lck_resourceunlock {
	struct req_header header;
	struct message_source source;
	SaNameT resource_name;
	SaLckLockIdT lock_id;
	SaInvocationT invocation;
	SaTimeT timeout;
	int async_call;
};

struct req_exec_lck_resourcelockorphan {
	struct req_header header;
	struct message_source source;
	SaNameT resource_name;
	SaLckLockIdT lock_id;
};

struct req_exec_lck_lockpurge {
	struct req_header header;
	struct message_source source;
	struct req_lib_lck_lockpurge req_lib_lck_lockpurge;
};

static void lck_recovery_initialize (void) 
{
	return;
}

static int lck_recovery_process (void) 
{
	return (0);
}

static void lck_recovery_finalize () 
{
	return;
	
}

static void lck_recovery_activate (void) 
{		
 	return;
}

static void lck_recovery_abort (void) 
{
	return;
}

static int lck_confchg_fn (
	enum totem_configuration_type configuration_type,
	struct in_addr *member_list, int member_list_entries,
	struct in_addr *left_list, int left_list_entries,
	struct in_addr *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id) 
{
	return (0);
}

static struct resource *resource_find (SaNameT *name)
{
	struct list_head *resource_list;
	struct resource *resource;

   for (resource_list = resource_list_head.next;
        resource_list != &resource_list_head;
        resource_list = resource_list->next) {

        resource = list_entry (resource_list,
            struct resource, list);

		if (name_match (name, &resource->name)) {
			return (resource);
		}
	}
	return (0);
}

static struct resource_lock *resource_lock_find (
	struct resource *resource,
	struct message_source *source,
	SaLckLockIdT lock_id)
{
	struct list_head *list;
	struct resource_lock *resource_lock;

	for (list = resource->resource_lock_list_head.next;
		list != &resource->resource_lock_list_head;
		list = list->next) {

		resource_lock = list_entry (list, struct resource_lock, resource_list);

		if ((memcmp (&resource_lock->callback_source,
			source, sizeof (struct message_source)) == 0) &&
			(lock_id == resource_lock->lock_id)) {

			return (resource_lock);
		}
	}
	return (0);
}

struct resource_cleanup *lck_resource_cleanup_find (
	struct conn_info *conn_info,
	SaLckResourceHandleT resource_handle)
{
	
	struct list_head *list;
	struct resource_cleanup *resource_cleanup;

   for (list = conn_info->ais_ci.u.liblck_ci.resource_cleanup_list.next;
		list != &conn_info->ais_ci.u.liblck_ci.resource_cleanup_list;
        list = list->next) {

		resource_cleanup = list_entry (list, struct resource_cleanup, list);
		if (resource_cleanup->resource_handle == resource_handle) {
			return (resource_cleanup);
		}
	}
	return (0);
}
	

int lck_resource_close (struct resource *resource)
{
	struct req_exec_lck_resourceclose req_exec_lck_resourceclose;
	struct iovec iovec;

	req_exec_lck_resourceclose.header.size =
		sizeof (struct req_exec_lck_resourceclose);
	req_exec_lck_resourceclose.header.id = MESSAGE_REQ_EXEC_LCK_RESOURCECLOSE;

	memcpy (&req_exec_lck_resourceclose.lockResourceName,
		&resource->name, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_lck_resourceclose;
	iovec.iov_len = sizeof (req_exec_lck_resourceclose);

	if (totempg_groups_send_ok_joined (openais_group_handle, &iovec, 1)) {
		assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1, TOTEMPG_AGREED) == 0);
		return (0);
	}

	return (-1);
}

void resource_lock_orphan (struct resource_lock *resource_lock)
{
	struct req_exec_lck_resourcelockorphan req_exec_lck_resourcelockorphan;
	struct iovec iovec;

	req_exec_lck_resourcelockorphan.header.size =
		sizeof (struct req_exec_lck_resourcelockorphan);
	req_exec_lck_resourcelockorphan.header.id = MESSAGE_REQ_EXEC_LCK_RESOURCELOCKORPHAN;

	memcpy (&req_exec_lck_resourcelockorphan.source,
		&resource_lock->callback_source,
		sizeof (struct message_source));

	memcpy (&req_exec_lck_resourcelockorphan.resource_name,
		&resource_lock->resource->name,
		sizeof (SaNameT));
		
	req_exec_lck_resourcelockorphan.lock_id = resource_lock->lock_id;
	
	iovec.iov_base = (char *)&req_exec_lck_resourcelockorphan;
	iovec.iov_len = sizeof (req_exec_lck_resourcelockorphan);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1, TOTEMPG_AGREED) == 0);

	// AAA
}

void lck_resource_cleanup_lock_remove (
	struct resource_cleanup *resource_cleanup)
{
	struct list_head *list;
	struct resource_lock *resource_lock;

	for (list = resource_cleanup->resource_lock_list_head.next;
		list != &resource_cleanup->resource_lock_list_head;
		list = list->next) {

		resource_lock = list_entry (list, struct resource_lock, resource_cleanup_list);
		resource_lock_orphan (resource_lock);
	}
}

void lck_resource_cleanup_remove (
	struct conn_info *conn_info,
	SaLckResourceHandleT resource_handle)
{
	
	struct list_head *list;
	struct resource_cleanup *resource_cleanup;

	for (list = conn_info->ais_ci.u.liblck_ci.resource_cleanup_list.next;
		list != &conn_info->ais_ci.u.liblck_ci.resource_cleanup_list;
		list = list->next) {

		resource_cleanup = list_entry (list, struct resource_cleanup, list);
		if (resource_cleanup->resource_handle == resource_handle) {
			list_del (&resource_cleanup->list);
			free (resource_cleanup);
			return;
		}
	}
}


static int lck_exec_init_fn (struct openais_config *openais_config)
{
	/*
	 *  Initialize the saved ring ID.
	 */
#ifdef TODO
	saved_ring_id.seq = 0;
	saved_ring_id.rep.s_addr = this_ip->sin_addr.s_addr;		
#endif
	
	return (0);
}

static int lck_exit_fn (struct conn_info *conn_info)
{
	struct resource_cleanup *resource_cleanup;
	struct list_head *list;
	
	if (conn_info->conn_info_partner->service != LCK_SERVICE) {
		return 0;
	}

	log_printf(LOG_LEVEL_NOTICE, "lck_exit_fn conn_info = %p, with fd = %d\n", conn_info, conn_info->fd);
	
	/*
	 * close all resources opened on this fd
	 */
	list = conn_info->conn_info_partner->ais_ci.u.liblck_ci.resource_cleanup_list.next;	
	while (!list_empty(&conn_info->conn_info_partner->ais_ci.u.liblck_ci.resource_cleanup_list)) {
		
		resource_cleanup = list_entry (list, struct resource_cleanup, list);
		
		if (resource_cleanup->resource->name.length > 0)	{
			lck_resource_cleanup_lock_remove (resource_cleanup);
			lck_resource_close (resource_cleanup->resource);
		}
		
		list_del (&resource_cleanup->list);	
		free (resource_cleanup);
                
		list = conn_info->conn_info_partner->ais_ci.u.liblck_ci.resource_cleanup_list.next;
	}

	return (0);
}

static int lck_init_two_fn (struct conn_info *conn_info)
{
	list_init (&conn_info->conn_info_partner->ais_ci.u.liblck_ci.resource_list);
	list_init (&conn_info->conn_info_partner->ais_ci.u.liblck_ci.resource_cleanup_list);

	return (0);

}

static int message_handler_req_exec_lck_resourceopen (
	void *message,
	struct in_addr source_addr,
	int endian_conversion_required)
{
	struct req_exec_lck_resourceopen *req_exec_lck_resourceopen = (struct req_exec_lck_resourceopen *)message;
	struct res_lib_lck_resourceopen res_lib_lck_resourceopen;
	struct res_lib_lck_resourceopenasync res_lib_lck_resourceopenasync;
	struct resource *resource;
	struct resource_cleanup *resource_cleanup;
	SaErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_NOTICE, "EXEC request: saLckResourceOpen %s\n",
		getSaNameT (&req_exec_lck_resourceopen->resource_name));
	
	if (req_exec_lck_resourceopen->fail_with_error != SA_AIS_OK) {
		error = req_exec_lck_resourceopen->fail_with_error;
		goto error_exit;
	}

	resource = resource_find (&req_exec_lck_resourceopen->resource_name);

	/*
	 * If resource doesn't exist, create one
	 */
	if (resource == 0) {
		if ((req_exec_lck_resourceopen->open_flags & SA_LCK_RESOURCE_CREATE) == 0) {
			error = SA_AIS_ERR_NOT_EXIST;
			goto error_exit;
		}
		resource = malloc (sizeof (struct resource));
		if (resource == 0) {
			error = SA_AIS_ERR_NO_MEMORY;
			goto error_exit;
		}
		memset (resource, 0, sizeof (struct resource));

		memcpy (&resource->name,
			&req_exec_lck_resourceopen->resource_name,
			sizeof (SaNameT));
		list_init (&resource->list);
		list_init (&resource->resource_lock_list_head);
		list_add (&resource->list, &resource_list_head);
		list_init (&resource->pr_granted_list_head);
		list_init (&resource->pr_pending_list_head);
		list_init (&resource->ex_pending_list_head);
		resource->refcount = 0;
		resource->ex_granted = NULL;
	}

	/*
	 * Setup connection information and mark resource as referenced
	 */
	if (message_source_is_local (&req_exec_lck_resourceopen->source)) {
		log_printf (LOG_LEVEL_DEBUG, "Lock resource opened is %p\n", resource);
		resource_cleanup = malloc (sizeof (struct resource_cleanup));
		if (resource_cleanup == 0) {
			free (resource);
			error = SA_AIS_ERR_NO_MEMORY;
		} else {
			list_init (&resource_cleanup->list);
			list_init (&resource_cleanup->resource_lock_list_head);
			resource_cleanup->resource = resource;
			resource_cleanup->resource_handle = req_exec_lck_resourceopen->resource_handle;
			list_add (
				&resource_cleanup->list,
				&req_exec_lck_resourceopen->source.conn_info->ais_ci.u.liblck_ci.resource_cleanup_list);
		}
		resource->refcount += 1;
	}
	
	
	/*
	 * Send error result to LCK library
	 */
error_exit:
	/*
	 * If this node was the source of the message, respond to this node
	 */
	if (message_source_is_local (&req_exec_lck_resourceopen->source)) {
		/*
		 * If its an async call respond with the invocation and handle
		 */
		if (req_exec_lck_resourceopen->async_call) {
			res_lib_lck_resourceopenasync.header.size = sizeof (struct res_lib_lck_resourceopenasync);
			res_lib_lck_resourceopenasync.header.id = MESSAGE_RES_LCK_RESOURCEOPENASYNC;
			res_lib_lck_resourceopenasync.header.error = error;
			res_lib_lck_resourceopenasync.resourceHandle = req_exec_lck_resourceopen->resource_handle;
			res_lib_lck_resourceopenasync.invocation = req_exec_lck_resourceopen->invocation;
			memcpy (&res_lib_lck_resourceopenasync.source,
				&req_exec_lck_resourceopen->source,
				sizeof (struct message_source));

			libais_send_response (
				req_exec_lck_resourceopen->source.conn_info,
				&res_lib_lck_resourceopenasync,
				sizeof (struct res_lib_lck_resourceopenasync));
			libais_send_response (
				req_exec_lck_resourceopen->source.conn_info->conn_info_partner,
				&res_lib_lck_resourceopenasync,
				sizeof (struct res_lib_lck_resourceopenasync));
		} else {
			/*
			 * otherwise respond with the normal resourceopen response
			 */
			res_lib_lck_resourceopen.header.size = sizeof (struct res_lib_lck_resourceopen);
			res_lib_lck_resourceopen.header.id = MESSAGE_RES_LCK_RESOURCEOPEN;
			res_lib_lck_resourceopen.header.error = error;
			memcpy (&res_lib_lck_resourceopen.source,
				&req_exec_lck_resourceopen->source,
				sizeof (struct message_source));

			libais_send_response (req_exec_lck_resourceopen->source.conn_info, &res_lib_lck_resourceopen,
				sizeof (struct res_lib_lck_resourceopen));
		}
	}

	return (0);
}

static int message_handler_req_exec_lck_resourceclose (
	void *message,
	struct in_addr source_addr,
	int endian_conversion_required)
{
	struct req_exec_lck_resourceclose *req_exec_lck_resourceclose = (struct req_exec_lck_resourceclose *)message;
	struct res_lib_lck_resourceclose res_lib_lck_resourceclose;
	struct resource *resource = 0;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_NOTICE, "EXEC request: saLckResourceClose %s\n",
		getSaNameT (&req_exec_lck_resourceclose->lockResourceName));

	resource = resource_find (&req_exec_lck_resourceclose->lockResourceName);
	if (resource == 0) {
		goto error_exit;
	}
		
	resource->refcount -= 1;
	if (resource->refcount == 0) {
	}
error_exit:
	if (message_source_is_local(&req_exec_lck_resourceclose->source)) {
		lck_resource_cleanup_remove (
			req_exec_lck_resourceclose->source.conn_info,
			req_exec_lck_resourceclose->resource_handle);

		res_lib_lck_resourceclose.header.size = sizeof (struct res_lib_lck_resourceclose);
		res_lib_lck_resourceclose.header.id = MESSAGE_RES_LCK_RESOURCECLOSE;
		res_lib_lck_resourceclose.header.error = error;
		libais_send_response (req_exec_lck_resourceclose->source.conn_info,
			&res_lib_lck_resourceclose, sizeof (struct res_lib_lck_resourceclose));
	}
	return (0);
}

void waiter_notification_send (struct resource_lock *resource_lock)
{
	struct res_lib_lck_lockwaitercallback res_lib_lck_lockwaitercallback;

	if (message_source_is_local (&resource_lock->callback_source) == 0) {
		return;
	}

	res_lib_lck_lockwaitercallback.header.size = sizeof (struct res_lib_lck_lockwaitercallback);
	res_lib_lck_lockwaitercallback.header.id = MESSAGE_RES_LCK_LOCKWAITERCALLBACK;
	res_lib_lck_lockwaitercallback.header.error = SA_AIS_OK;
	res_lib_lck_lockwaitercallback.waiter_signal = resource_lock->waiter_signal;
	res_lib_lck_lockwaitercallback.lock_id = resource_lock->lock_id;
	res_lib_lck_lockwaitercallback.mode_requested = resource_lock->lock_mode;

	if (resource_lock->resource->ex_granted) {
		res_lib_lck_lockwaitercallback.mode_held = SA_LCK_EX_LOCK_MODE;
	} else {
		res_lib_lck_lockwaitercallback.mode_held = SA_LCK_PR_LOCK_MODE;
	}

	libais_send_response (
		resource_lock->callback_source.conn_info->conn_info_partner,
		&res_lib_lck_lockwaitercallback,
		sizeof (struct res_lib_lck_lockwaitercallback));
}

void waiter_notification_list_send (struct list_head *list_notify_head)
{
	struct list_head *list;
	struct resource_lock *resource_lock;

	for (list = list_notify_head->next;
		list != list_notify_head;
		list = list->next) {

		resource_lock = list_entry (list, struct resource_lock, list);
		waiter_notification_send (resource_lock);
	}
}

void resource_lock_async_deliver (
	struct message_source *source,
	struct resource_lock *resource_lock,
	SaAisErrorT error)
{
	struct res_lib_lck_resourcelockasync res_lib_lck_resourcelockasync;

	if (source && message_source_is_local(source)) {
		if (resource_lock->async_call) {
			res_lib_lck_resourcelockasync.header.size = sizeof (struct res_lib_lck_resourcelockasync);
			res_lib_lck_resourcelockasync.header.id = MESSAGE_RES_LCK_RESOURCELOCKASYNC;
			res_lib_lck_resourcelockasync.header.error = error;
			res_lib_lck_resourcelockasync.resource_lock = (void *)resource_lock;
			res_lib_lck_resourcelockasync.lockStatus = resource_lock->lock_status;
			res_lib_lck_resourcelockasync.invocation = resource_lock->invocation;
			res_lib_lck_resourcelockasync.lockId = resource_lock->lock_id;
			libais_send_response (source->conn_info->conn_info_partner,
				&res_lib_lck_resourcelockasync,
				sizeof (struct res_lib_lck_resourcelockasync));
		}
	}
}

void lock_response_deliver (
	struct message_source *source,
	struct resource_lock *resource_lock,
	SaAisErrorT error)
{
	struct res_lib_lck_resourcelock res_lib_lck_resourcelock;

	if (source && message_source_is_local(source)) {
		if (resource_lock->async_call) {
			resource_lock_async_deliver (&resource_lock->callback_source, resource_lock, error);
		} else {
			res_lib_lck_resourcelock.header.size = sizeof (struct res_lib_lck_resourcelock);
			res_lib_lck_resourcelock.header.id = MESSAGE_RES_LCK_RESOURCELOCK;
			res_lib_lck_resourcelock.header.error = error;
			res_lib_lck_resourcelock.resource_lock = (void *)resource_lock;
			res_lib_lck_resourcelock.lockStatus = resource_lock->lock_status;
			libais_send_response (source->conn_info,
				&res_lib_lck_resourcelock,
				sizeof (struct res_lib_lck_resourcelock));
		}
	}
}


/*
 * Queue a lock if resource flags allow it
 */
void lock_queue (
	struct resource *resource,
	struct resource_lock *resource_lock)
{
	if ((resource_lock->lock_flags & SA_LCK_LOCK_NO_QUEUE) == 0) {
		/*
		 * Add lock to the list
		 */
		if (resource_lock->lock_mode == SA_LCK_PR_LOCK_MODE) {
			list_add_tail (&resource_lock->list, 
				&resource->pr_pending_list_head);
				waiter_notification_send (resource->ex_granted);
		} else
		if (resource_lock->lock_mode == SA_LCK_EX_LOCK_MODE) {
			list_add_tail (&resource_lock->list, 
				&resource->ex_pending_list_head);
				waiter_notification_list_send (&resource->pr_granted_list_head);
		}
	} else {
		resource_lock->lock_status = SA_LCK_LOCK_NOT_QUEUED;
	}
}

/*
The algorithm:

if ex lock granted
	if ex pending list has locks
		send waiter notification to ex lock granted 
else
	if ex pending list has locks
		if pr granted list has locks
			send waiter notification to all pr granted locks
		else
			grant ex lock from pending to granted
	else
		grant all pr pending locks to pr granted list
*/
#define SA_LCK_LOCK_NO_STATUS 0
void lock_algorithm (
	struct resource *resource,
	struct resource_lock *resource_lock)
{
	resource_lock->lock_status = SA_LCK_LOCK_NO_STATUS; /* no status */
	if (resource->ex_granted) {
		/*
		 * Exclusive lock granted
		 */
		if (resource_lock->lock_mode == SA_LCK_PR_LOCK_MODE) {
			lock_queue (resource, resource_lock);
		}
	} else {
		/*
		 * Exclusive lock not granted
		 */
		if (resource_lock->lock_mode == SA_LCK_EX_LOCK_MODE) {
			if (list_empty (&resource->pr_granted_list_head) == 0) {
				lock_queue (resource, resource_lock);
			} else {
				/*
				 * grant ex lock from pending to granted
				 */
				resource->ex_granted = resource_lock;
				resource_lock->lock_status = SA_LCK_LOCK_GRANTED;
			}
		} else {
			/*
			 * grant all pr pending locks to pr granted list
			 */
			list_add (&resource_lock->list,
				&resource->pr_granted_list_head);
			resource_lock->lock_status = SA_LCK_LOCK_GRANTED;
		}
	}
}

/*
 * 	if lock in ex, set ex to null
 *	delete resource lock from list
 * 
 *	 if ex lock not granted
 * 		if ex pending list has locks
 *			grant first ex pending list lock to ex lock
 * 	if ex lock not granted
 *		if pr pending list has locks
 *			assign all pr pending locks to pr granted lock list
 */
void unlock_algorithm (
	struct resource *resource,
	struct resource_lock *resource_lock)
{
	struct resource_lock *resource_lock_grant;
	struct list_head *list;
	struct list_head *list_p;

	/*
	 * If unlocking the ex lock, reset ex granted
	 */
	if (resource_lock == resource->ex_granted) {
		resource->ex_granted = 0;
	}

	/*
	 * Delete resource lock from whichever list it is on
	 */
	list_del (&resource_lock->list);

	/*
	 * Check if EX locks are available, if so assign one
	 */
	if (resource->ex_granted == 0) {
		if (list_empty (&resource->ex_pending_list_head) == 0) {
			/*
			 * grant first ex pending list lock to ex lock
			 */
			resource_lock_grant = list_entry (
				resource->ex_pending_list_head.next,
				struct resource_lock, list);

			list_del (&resource_lock_grant->list);
			resource->ex_granted = resource_lock_grant;

			resource_lock_grant->lock_status = SA_LCK_LOCK_GRANTED;
			lock_response_deliver (
				&resource_lock_grant->response_source,
				resource_lock_grant,
				SA_AIS_OK);
		}
	}

	/*
	 * Couldn't assign EX lock, so assign any pending PR locks
	 */
	if (resource->ex_granted == 0) {
		if (list_empty (&resource->pr_pending_list_head) == 0) {
 			/*
			 * assign all pr pending locks to pr granted lock list
			 */

		   for (list = resource->pr_pending_list_head.next;
				list != &resource->pr_pending_list_head;
				list = list->next) {

				resource_lock_grant = list_entry (list, struct resource_lock, list);
				resource_lock_grant->lock_status = SA_LCK_LOCK_GRANTED;

				lock_response_deliver (
					&resource_lock_grant->response_source,
					resource_lock_grant,
					SA_AIS_OK);
			}

			/*
			 * Add pending locks to granted list
			 */
		   	list_p = &resource->pr_pending_list_head.next;
			list_del (&resource->pr_pending_list_head);
			list_add_tail (list_p,
				&resource->pr_granted_list_head);
		}
	}
}

static int message_handler_req_exec_lck_resourcelock (
	void *message,
	struct in_addr source_addr,
	int endian_conversion_required)
{
	struct req_exec_lck_resourcelock *req_exec_lck_resourcelock = (struct req_exec_lck_resourcelock *)message;
	struct resource *resource = 0;
	struct resource_lock *resource_lock = 0;
	struct resource_cleanup *resource_cleanup = 0;

	log_printf (LOG_LEVEL_NOTICE, "EXEC request: saLckResourceLock %s\n",
		getSaNameT (&req_exec_lck_resourcelock->req_lib_lck_resourcelock.lockResourceName));

	resource = resource_find (&req_exec_lck_resourcelock->req_lib_lck_resourcelock.lockResourceName);
	if (resource == 0) {
		goto error_exit;
	}
	resource->refcount += 1;

	resource_lock = malloc (sizeof (struct resource_lock));
	if (resource_lock == 0) {
		lock_response_deliver (&req_exec_lck_resourcelock->source,
			resource_lock,
			SA_AIS_ERR_NO_MEMORY);
		goto error_exit;
	}

	/*
	 * Build resource lock structure
	 */
	memset (resource_lock, 0, sizeof (struct resource_lock));

	list_init (&resource_lock->list);
	list_init (&resource_lock->resource_list);
	list_init (&resource_lock->resource_cleanup_list);

	list_add (&resource_lock->resource_list, &resource->resource_lock_list_head);

	resource_lock->resource = resource;

	resource_lock->lock_mode =
		req_exec_lck_resourcelock->req_lib_lck_resourcelock.lockMode;
	resource_lock->lock_flags =
		req_exec_lck_resourcelock->req_lib_lck_resourcelock.lockFlags;
	resource_lock->waiter_signal =
		req_exec_lck_resourcelock->req_lib_lck_resourcelock.waiterSignal;
	resource_lock->timeout =
		req_exec_lck_resourcelock->req_lib_lck_resourcelock.timeout;
	resource_lock->lock_id =
		req_exec_lck_resourcelock->req_lib_lck_resourcelock.lockId;
	resource_lock->async_call =
		req_exec_lck_resourcelock->req_lib_lck_resourcelock.async_call;
	resource_lock->invocation =
		req_exec_lck_resourcelock->req_lib_lck_resourcelock.invocation;
	
	/*
	 * Waiter callback source
	 */
	memcpy (&resource_lock->callback_source,
		&req_exec_lck_resourcelock->req_lib_lck_resourcelock.source,
		sizeof (struct message_source));

	lock_algorithm (resource, resource_lock);

	/*
	 * Add resource lock to cleanup handler for this api resource instance
	 */
	if (message_source_is_local (&req_exec_lck_resourcelock->source)) {
		resource_cleanup = lck_resource_cleanup_find (
			resource_lock->callback_source.conn_info,
			req_exec_lck_resourcelock->resource_handle);

		assert (resource_cleanup);
			
		list_add (&resource_lock->resource_cleanup_list,
			&resource_cleanup->resource_lock_list_head);

		/*
		 * If lock queued by lock algorithm, dont send response to library now
		 */
		if (resource_lock->lock_status != SA_LCK_LOCK_NO_STATUS) {
			/*
			 * If lock granted or denied, deliver callback or 
			 * response to library for non-async calls
			 */
			lock_response_deliver (
				&req_exec_lck_resourcelock->source,
				resource_lock,
				SA_AIS_OK);
		} else {
			memcpy (&resource_lock->response_source,
				&req_exec_lck_resourcelock->source,
				sizeof (struct message_source));
		}

		/*
		 * Deliver async response to library
		 */
		req_exec_lck_resourcelock->source.conn_info =
			req_exec_lck_resourcelock->source.conn_info->conn_info_partner;
		resource_lock_async_deliver (
			&req_exec_lck_resourcelock->source,
			resource_lock,
			SA_AIS_OK);
		req_exec_lck_resourcelock->source.conn_info =
			req_exec_lck_resourcelock->source.conn_info->conn_info_partner;
	}

error_exit:
	return (0);
}

static int message_handler_req_exec_lck_resourceunlock (
	void *message,
	struct in_addr source_addr,
	int endian_conversion_required)
{
	struct req_exec_lck_resourceunlock *req_exec_lck_resourceunlock = (struct req_exec_lck_resourceunlock *)message;
	struct res_lib_lck_resourceunlock res_lib_lck_resourceunlock;
	struct res_lib_lck_resourceunlockasync res_lib_lck_resourceunlockasync;
	struct resource *resource = 0;
	struct resource_lock *resource_lock = 0;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_NOTICE, "EXEC request: saLckResourceUnlock %s\n",
		getSaNameT (&req_exec_lck_resourceunlock->resource_name));

	resource = resource_find (&req_exec_lck_resourceunlock->resource_name);
	if (resource == 0) {
		goto error_exit;
	}
	resource->refcount -= 1;

	resource_lock = resource_lock_find (resource,
		&req_exec_lck_resourceunlock->source,
		req_exec_lck_resourceunlock->lock_id);
	assert (resource_lock);

	list_del (&resource_lock->resource_cleanup_list);
	unlock_algorithm (resource, resource_lock);

error_exit:
	if (message_source_is_local(&req_exec_lck_resourceunlock->source)) {
		if (req_exec_lck_resourceunlock->async_call) {
			res_lib_lck_resourceunlockasync.header.size = sizeof (struct res_lib_lck_resourceunlockasync);
			res_lib_lck_resourceunlockasync.header.id = MESSAGE_RES_LCK_RESOURCEUNLOCKASYNC;
			res_lib_lck_resourceunlockasync.header.error = error;
			res_lib_lck_resourceunlockasync.invocation =
				req_exec_lck_resourceunlock->invocation;

			libais_send_response (
				req_exec_lck_resourceunlock->source.conn_info,
				&res_lib_lck_resourceunlockasync,
				sizeof (struct res_lib_lck_resourceunlockasync));
			libais_send_response (
				resource_lock->callback_source.conn_info,
				&res_lib_lck_resourceunlockasync,
				sizeof (struct res_lib_lck_resourceunlockasync));
		} else {
			res_lib_lck_resourceunlock.header.size = sizeof (struct res_lib_lck_resourceunlock);
			res_lib_lck_resourceunlock.header.id = MESSAGE_RES_LCK_RESOURCEUNLOCK;
			res_lib_lck_resourceunlock.header.error = error;
			libais_send_response (req_exec_lck_resourceunlock->source.conn_info,
				&res_lib_lck_resourceunlock, sizeof (struct res_lib_lck_resourceunlock));
		}
	}
	return (0);
}

static int message_handler_req_exec_lck_resourcelockorphan (
	void *message,
	struct in_addr source_addr,
	int endian_conversion_required)
{
	struct req_exec_lck_resourcelockorphan *req_exec_lck_resourcelockorphan = (struct req_exec_lck_resourcelockorphan *)message;
	struct resource *resource = 0;
	struct resource_lock *resource_lock = 0;

	log_printf (LOG_LEVEL_NOTICE, "EXEC request: Orphan resource locks for resource %s\n",
		getSaNameT (&req_exec_lck_resourcelockorphan->resource_name));

	resource = resource_find (&req_exec_lck_resourcelockorphan->resource_name);
	if (resource == 0) {
		assert (0);
	}
	resource->refcount -= 1;

	resource_lock = resource_lock_find (resource,
		&req_exec_lck_resourcelockorphan->source,
		req_exec_lck_resourcelockorphan->lock_id);
	assert (resource_lock);

	list_del (&resource_lock->resource_cleanup_list);
	unlock_algorithm (resource, resource_lock);
	return (0);
}

static int message_handler_req_exec_lck_lockpurge (
	void *message,
	struct in_addr source_addr,
	int endian_conversion_required)
{
	struct req_exec_lck_lockpurge *req_exec_lck_lockpurge = (struct req_exec_lck_lockpurge *)message;
	struct res_lib_lck_lockpurge res_lib_lck_lockpurge;
	struct resource *resource = 0;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_DEBUG, "EXEC request: saLckLockPurge %s\n",
		getSaNameT (&req_exec_lck_lockpurge->req_lib_lck_lockpurge.lockResourceName));

	resource = resource_find (&req_exec_lck_lockpurge->req_lib_lck_lockpurge.lockResourceName);
	if (resource == 0) {
		goto error_exit;
	}
		
error_exit:
	if (message_source_is_local(&req_exec_lck_lockpurge->source)) {
//		lck_resource_cleanup_remove (req_exec_lck_lockpurge->source.conn_info,
//			resource);

		res_lib_lck_lockpurge.header.size = sizeof (struct res_lib_lck_lockpurge);
		res_lib_lck_lockpurge.header.id = MESSAGE_RES_LCK_LOCKPURGE;
		res_lib_lck_lockpurge.header.error = error;
		libais_send_response (req_exec_lck_lockpurge->source.conn_info,
			&res_lib_lck_lockpurge, sizeof (struct res_lib_lck_lockpurge));
	}
	return (0);
}

static int message_handler_req_lib_lck_resourceopen (struct conn_info *conn_info, void *message)
{
	struct req_lib_lck_resourceopen *req_lib_lck_resourceopen = (struct req_lib_lck_resourceopen *)message;
	struct req_exec_lck_resourceopen req_exec_lck_resourceopen;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saLckResourceOpen %s\n",
		getSaNameT (&req_lib_lck_resourceopen->lockResourceName));

	req_exec_lck_resourceopen.header.size =
		sizeof (struct req_exec_lck_resourceopen);
	req_exec_lck_resourceopen.header.id = MESSAGE_REQ_EXEC_LCK_RESOURCEOPEN;

	message_source_set (&req_exec_lck_resourceopen.source, conn_info);

	memcpy (&req_exec_lck_resourceopen.resource_name,
		&req_lib_lck_resourceopen->lockResourceName,
		sizeof (SaNameT));
	
	req_exec_lck_resourceopen.open_flags = req_lib_lck_resourceopen->resourceOpenFlags;
	req_exec_lck_resourceopen.async_call = 0;
	req_exec_lck_resourceopen.invocation = 0;
	req_exec_lck_resourceopen.resource_handle = req_lib_lck_resourceopen->resourceHandle;
	req_exec_lck_resourceopen.fail_with_error = SA_AIS_OK;

	iovec.iov_base = (char *)&req_exec_lck_resourceopen;
	iovec.iov_len = sizeof (req_exec_lck_resourceopen);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1, TOTEMPG_AGREED) == 0);

	return (0);
}

static int message_handler_req_lib_lck_resourceopenasync (struct conn_info *conn_info, void *message)
{
	struct req_lib_lck_resourceopen *req_lib_lck_resourceopen = (struct req_lib_lck_resourceopen *)message;
	struct req_exec_lck_resourceopen req_exec_lck_resourceopen;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saLckResourceOpenAsync %s\n",
		getSaNameT (&req_lib_lck_resourceopen->lockResourceName));

	req_exec_lck_resourceopen.header.size =
		sizeof (struct req_exec_lck_resourceopen);
	req_exec_lck_resourceopen.header.id = MESSAGE_REQ_EXEC_LCK_RESOURCEOPEN;

	message_source_set (&req_exec_lck_resourceopen.source, conn_info);

	memcpy (&req_exec_lck_resourceopen.resource_name,
		&req_lib_lck_resourceopen->lockResourceName,
		sizeof (SaNameT));
	
	req_exec_lck_resourceopen.resource_handle = req_lib_lck_resourceopen->resourceHandle;
	req_exec_lck_resourceopen.invocation = req_lib_lck_resourceopen->invocation;
	req_exec_lck_resourceopen.open_flags = req_lib_lck_resourceopen->resourceOpenFlags;
	req_exec_lck_resourceopen.timeout = 0;
	req_exec_lck_resourceopen.async_call = 1;

	iovec.iov_base = (char *)&req_exec_lck_resourceopen;
	iovec.iov_len = sizeof (req_exec_lck_resourceopen);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1, TOTEMPG_AGREED) == 0);

	return (0);
}

static int message_handler_req_lib_lck_resourceclose (struct conn_info *conn_info, void *message) {
	struct req_lib_lck_resourceclose *req_lib_lck_resourceclose = (struct req_lib_lck_resourceclose *)message;
	struct req_exec_lck_resourceclose req_exec_lck_resourceclose;
	struct iovec iovecs[2];
	struct resource *resource;
	struct res_lib_lck_resourceclose res_lib_lck_resourceclose;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saLckResourceClose %s\n",
		getSaNameT (&req_lib_lck_resourceclose->lockResourceName));

	resource = resource_find (&req_lib_lck_resourceclose->lockResourceName);
	if (resource) {
		req_exec_lck_resourceclose.header.size =
			sizeof (struct req_exec_lck_resourceclose);
		req_exec_lck_resourceclose.header.id = MESSAGE_REQ_EXEC_LCK_RESOURCECLOSE;

		message_source_set (&req_exec_lck_resourceclose.source, conn_info);

		memcpy (&req_exec_lck_resourceclose.lockResourceName,
			&req_lib_lck_resourceclose->lockResourceName, sizeof (SaNameT));

		req_exec_lck_resourceclose.resource_handle = req_lib_lck_resourceclose->resourceHandle;
		iovecs[0].iov_base = (char *)&req_exec_lck_resourceclose;
		iovecs[0].iov_len = sizeof (req_exec_lck_resourceclose);

		if (totempg_groups_send_ok_joined (openais_group_handle, iovecs, 1)) {
			assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 1, TOTEMPG_AGREED) == 0);
		}
	}
	else {
		log_printf (LOG_LEVEL_ERROR, "#### LCK: Could Not Find the Checkpoint to close so Returning Error. ####\n");

		res_lib_lck_resourceclose.header.size = sizeof (struct res_lib_lck_resourceclose);
		res_lib_lck_resourceclose.header.id = MESSAGE_RES_LCK_RESOURCECLOSE;
		res_lib_lck_resourceclose.header.error = SA_AIS_ERR_NOT_EXIST;

		libais_send_response (conn_info,
			&res_lib_lck_resourceclose,
			sizeof (struct res_lib_lck_resourceclose));
	}
	return (0);
}

static int message_handler_req_lib_lck_resourcelock (struct conn_info *conn_info, void *message)
{
	struct req_lib_lck_resourcelock *req_lib_lck_resourcelock = (struct req_lib_lck_resourcelock *)message;
	struct req_exec_lck_resourcelock req_exec_lck_resourcelock;
	struct iovec iovecs[2];

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saLckResourceLock %s\n",
		getSaNameT (&req_lib_lck_resourcelock->lockResourceName));

	req_exec_lck_resourcelock.header.size =
		sizeof (struct req_exec_lck_resourcelock);
	req_exec_lck_resourcelock.header.id = MESSAGE_REQ_EXEC_LCK_RESOURCELOCK;

	message_source_set (&req_exec_lck_resourcelock.source, conn_info);

	memcpy (&req_exec_lck_resourcelock.req_lib_lck_resourcelock,
		req_lib_lck_resourcelock,
		sizeof (struct req_lib_lck_resourcelock));
	
	req_exec_lck_resourcelock.resource_handle = req_lib_lck_resourcelock->resourceHandle;
	req_exec_lck_resourcelock.async_call = 0;
	req_exec_lck_resourcelock.invocation = 0;
	req_exec_lck_resourcelock.fail_with_error = SA_AIS_OK;

	iovecs[0].iov_base = (char *)&req_exec_lck_resourcelock;
	iovecs[0].iov_len = sizeof (req_exec_lck_resourcelock);

	assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 1, TOTEMPG_AGREED) == 0);

	return (0);
}

static int message_handler_req_lib_lck_resourcelockasync (struct conn_info *conn_info, void *message)
{
	struct req_lib_lck_resourcelock *req_lib_lck_resourcelock = (struct req_lib_lck_resourcelock *)message;
	struct req_exec_lck_resourcelock req_exec_lck_resourcelock;
	struct iovec iovecs[2];

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saLckResourceLockAsync %s\n",
		getSaNameT (&req_lib_lck_resourcelock->lockResourceName));

	req_exec_lck_resourcelock.header.size =
		sizeof (struct req_exec_lck_resourcelock);
	req_exec_lck_resourcelock.header.id = MESSAGE_REQ_EXEC_LCK_RESOURCELOCK;

	message_source_set (&req_exec_lck_resourcelock.source, conn_info);

	memcpy (&req_exec_lck_resourcelock.req_lib_lck_resourcelock,
		req_lib_lck_resourcelock,
		sizeof (struct req_lib_lck_resourcelock));
	
	req_exec_lck_resourcelock.resource_handle = req_lib_lck_resourcelock->resourceHandle;
	req_exec_lck_resourcelock.async_call = 1;
	req_exec_lck_resourcelock.invocation = req_lib_lck_resourcelock->invocation;

	iovecs[0].iov_base = (char *)&req_exec_lck_resourcelock;
	iovecs[0].iov_len = sizeof (req_exec_lck_resourcelock);

	assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 1, TOTEMPG_AGREED) == 0);

	return (0);
}

static int message_handler_req_lib_lck_resourceunlock (struct conn_info *conn_info, void *message)
{
	struct req_lib_lck_resourceunlock *req_lib_lck_resourceunlock = (struct req_lib_lck_resourceunlock *)message;
	struct req_exec_lck_resourceunlock req_exec_lck_resourceunlock;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saLckResourceUnlock %s\n",
		getSaNameT (&req_lib_lck_resourceunlock->lockResourceName));

	req_exec_lck_resourceunlock.header.size =
		sizeof (struct req_exec_lck_resourceunlock);
	req_exec_lck_resourceunlock.header.id = MESSAGE_REQ_EXEC_LCK_RESOURCEUNLOCK;

	message_source_set (&req_exec_lck_resourceunlock.source, conn_info);

	memcpy (&req_exec_lck_resourceunlock.resource_name,
		&req_lib_lck_resourceunlock->lockResourceName,
		sizeof (SaNameT));
		
	req_exec_lck_resourceunlock.lock_id = req_lib_lck_resourceunlock->lockId;
	req_exec_lck_resourceunlock.async_call = 0;
	req_exec_lck_resourceunlock.invocation = 0;
	
	iovec.iov_base = (char *)&req_exec_lck_resourceunlock;
	iovec.iov_len = sizeof (req_exec_lck_resourceunlock);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1, TOTEMPG_AGREED) == 0);

	return (0);
}

static int message_handler_req_lib_lck_resourceunlockasync (struct conn_info *conn_info, void *message)
{
	struct req_lib_lck_resourceunlock *req_lib_lck_resourceunlock = (struct req_lib_lck_resourceunlock *)message;
	struct req_exec_lck_resourceunlock req_exec_lck_resourceunlock;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saLckResourceUnlockAsync %s\n",
		getSaNameT (&req_lib_lck_resourceunlock->lockResourceName));

	req_exec_lck_resourceunlock.header.size =
		sizeof (struct req_exec_lck_resourceunlock);
	req_exec_lck_resourceunlock.header.id = MESSAGE_REQ_EXEC_LCK_RESOURCEUNLOCK;

	message_source_set (&req_exec_lck_resourceunlock.source, conn_info);

	memcpy (&req_exec_lck_resourceunlock.resource_name,
		&req_lib_lck_resourceunlock->lockResourceName,
		sizeof (SaNameT));
		
	req_exec_lck_resourceunlock.lock_id = req_lib_lck_resourceunlock->lockId;
	req_exec_lck_resourceunlock.invocation = req_lib_lck_resourceunlock->invocation;
	req_exec_lck_resourceunlock.async_call = 1;
	
	iovec.iov_base = (char *)&req_exec_lck_resourceunlock;
	iovec.iov_len = sizeof (req_exec_lck_resourceunlock);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1, TOTEMPG_AGREED) == 0);

	return (0);
}

static int message_handler_req_lib_lck_lockpurge (struct conn_info *conn_info, void *message)
{
	struct req_lib_lck_lockpurge *req_lib_lck_lockpurge = (struct req_lib_lck_lockpurge *)message;
	struct req_exec_lck_lockpurge req_exec_lck_lockpurge;
	struct iovec iovecs[2];

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saLckResourceLockPurge %s\n",
		getSaNameT (&req_lib_lck_lockpurge->lockResourceName));

	req_exec_lck_lockpurge.header.size =
		sizeof (struct req_exec_lck_lockpurge);
	req_exec_lck_lockpurge.header.id = MESSAGE_REQ_EXEC_LCK_LOCKPURGE;

	message_source_set (&req_exec_lck_lockpurge.source, conn_info);

	memcpy (&req_exec_lck_lockpurge.req_lib_lck_lockpurge,
		req_lib_lck_lockpurge,
		sizeof (struct req_lib_lck_lockpurge));
	
	iovecs[0].iov_base = (char *)&req_exec_lck_lockpurge;
	iovecs[0].iov_len = sizeof (req_exec_lck_lockpurge);

	assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 1, TOTEMPG_AGREED) == 0);

	return (0);
}

