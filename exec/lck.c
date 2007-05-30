/*
 * Copyright (c) 2005-2006 MontaVista Software, Inc.
 * Copyright (c) 2006 Red Hat, Inc.
 * Copyright (c) 2006 Sun Microsystems, Inc.
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

#include "swab.h"
#include "service.h"
#include "../include/saAis.h"
#include "../include/saLck.h"
#include "../include/ipc_lck.h"
#include "../include/list.h"
#include "../include/queue.h"
#include "../lcr/lcr_comp.h"
#include "mempool.h"
#include "util.h"
#include "main.h"
#include "ipc.h"
#include "totempg.h"
#include "print.h"


enum lck_message_req_types {
	MESSAGE_REQ_EXEC_LCK_RESOURCEOPEN = 0,
	MESSAGE_REQ_EXEC_LCK_RESOURCECLOSE = 1,
	MESSAGE_REQ_EXEC_LCK_RESOURCELOCK = 2,
	MESSAGE_REQ_EXEC_LCK_RESOURCEUNLOCK = 3,
	MESSAGE_REQ_EXEC_LCK_RESOURCELOCKORPHAN = 4,
	MESSAGE_REQ_EXEC_LCK_LOCKPURGE = 5
};

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
	mar_message_source_t callback_source;
	mar_message_source_t response_source;
	struct list_head list; /* locked resource lock list */
	struct list_head resource_list; /* resource locks on a resource */
	struct list_head resource_cleanup_list; /* cleanup data for resource locks */
};

struct resource {
	mar_name_t name;
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

static int lck_exec_init_fn (struct objdb_iface_ver0 *objdb);

static int lck_lib_exit_fn (void *conn);

static int lck_lib_init_fn (void *conn);

static void message_handler_req_exec_lck_resourceopen (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_lck_resourceclose (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_lck_resourcelock (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_lck_resourceunlock (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_lck_resourcelockorphan (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_lck_lockpurge (
	void *message,
	unsigned int nodeid);

static void message_handler_req_lib_lck_resourceopen (
	void *conn,
	void *msg);

static void message_handler_req_lib_lck_resourceopenasync (
	void *conn,
	void *msg);

static void message_handler_req_lib_lck_resourceclose (
	void *conn,
	void *msg);

static void message_handler_req_lib_lck_resourcelock (
	void *conn,
	void *msg);

static void message_handler_req_lib_lck_resourcelockasync (
	void *conn,
	void *msg);

static void message_handler_req_lib_lck_resourceunlock (
	void *conn,
	void *msg);

static void message_handler_req_lib_lck_resourceunlockasync (
	void *conn,
	void *msg);

static void message_handler_req_lib_lck_lockpurge (
	void *conn,
	void *msg);

static void exec_lck_resourceopen_endian_convert (void *msg);

static void exec_lck_resourceclose_endian_convert (void *msg);

static void exec_lck_resourcelock_endian_convert (void *msg);

static void exec_lck_resourceunlock_endian_convert (void *msg);

static void exec_lck_resourcelockorphan_endian_convert (void *msg);

static void exec_lck_lockpurge_endian_convert (void *msg);

#ifdef TODO
static void lck_sync_init (void);
#endif
static int lck_sync_process (void);
static void lck_sync_activate (void);
static void lck_sync_abort (void);

void resource_release (struct resource *resource);

/*
static struct list_head *recovery_lck_next = 0;
static struct list_head *recovery_lck_section_next = 0;
static int recovery_section_data_offset = 0;
static int recovery_section_send_flag = 0;
static int recovery_abort = 0;
//static struct memb_ring_id saved_ring_id;
*/


static void lck_confchg_fn (
	enum totem_configuration_type configuration_type,
	unsigned int *member_list, int member_list_entries,
	unsigned int *left_list, int left_list_entries,
	unsigned int *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id);

struct lck_pd {
	struct list_head resource_list;
	struct list_head resource_cleanup_list;
};


/*
 * Executive Handler Definition
 */
static struct openais_lib_handler lck_lib_service[] =
{
	{ /* 0 */
		.lib_handler_fn		= message_handler_req_lib_lck_resourceopen,
		.response_size		= sizeof (struct res_lib_lck_resourceopen),
		.response_id		= MESSAGE_RES_LCK_RESOURCEOPEN,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 1 */
		.lib_handler_fn		= message_handler_req_lib_lck_resourceopenasync,
		.response_size		= sizeof (struct res_lib_lck_resourceopenasync),
		.response_id		= MESSAGE_RES_LCK_RESOURCEOPENASYNC,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 2 */
		.lib_handler_fn		= message_handler_req_lib_lck_resourceclose,
		.response_size		= sizeof (struct res_lib_lck_resourceclose),
		.response_id		= MESSAGE_RES_LCK_RESOURCECLOSE,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 3 */
		.lib_handler_fn		= message_handler_req_lib_lck_resourcelock,
		.response_size		= sizeof (struct res_lib_lck_resourcelock),
		.response_id		= MESSAGE_RES_LCK_RESOURCELOCK,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 4 */
		.lib_handler_fn		= message_handler_req_lib_lck_resourcelockasync,
		.response_size		= sizeof (struct res_lib_lck_resourcelockasync),
		.response_id		= MESSAGE_RES_LCK_RESOURCELOCKASYNC,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 5 */
		.lib_handler_fn		= message_handler_req_lib_lck_resourceunlock,
		.response_size		= sizeof (struct res_lib_lck_resourceunlock),
		.response_id		= MESSAGE_RES_LCK_RESOURCELOCK,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 6 */
		.lib_handler_fn		= message_handler_req_lib_lck_resourceunlockasync,
		.response_size		= sizeof (struct res_lib_lck_resourceunlock),
		.response_id		= MESSAGE_RES_LCK_RESOURCEUNLOCKASYNC,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 7 */
		.lib_handler_fn		= message_handler_req_lib_lck_lockpurge,
		.response_size		= sizeof (struct res_lib_lck_lockpurge),
		.response_id		= MESSAGE_RES_LCK_LOCKPURGE,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	}
};


static struct openais_exec_handler lck_exec_service[] = {
	{
		.exec_handler_fn	= message_handler_req_exec_lck_resourceopen,
		.exec_endian_convert_fn	= exec_lck_resourceopen_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_lck_resourceclose,
		.exec_endian_convert_fn	= exec_lck_resourceclose_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_lck_resourcelock,
		.exec_endian_convert_fn	= exec_lck_resourcelock_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_lck_resourceunlock,
		.exec_endian_convert_fn	= exec_lck_resourceunlock_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_lck_resourcelockorphan,
		.exec_endian_convert_fn	= exec_lck_resourcelockorphan_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_lck_lockpurge,
		.exec_endian_convert_fn	= exec_lck_lockpurge_endian_convert
	}
};

struct openais_service_handler lck_service_handler = {
	.name				= "openais distributed locking service B.01.01",
	.id				= LCK_SERVICE,
	.private_data_size		= sizeof (struct lck_pd),
	.flow_control			= OPENAIS_FLOW_CONTROL_NOT_REQUIRED, 
	.lib_init_fn			= lck_lib_init_fn,
	.lib_exit_fn			= lck_lib_exit_fn,
	.lib_service			= lck_lib_service,
	.lib_service_count		= sizeof (lck_lib_service) / sizeof (struct openais_lib_handler),
	.exec_init_fn			= lck_exec_init_fn,
	.exec_service			= lck_exec_service,
	.exec_service_count		= sizeof (lck_exec_service) / sizeof (struct openais_exec_handler),
	.exec_dump_fn			= NULL,
	.confchg_fn			= lck_confchg_fn,
	.sync_init			= NULL,
//	.sync_init			= lck_sync_init,
	.sync_process			= lck_sync_process,
	.sync_activate			= lck_sync_activate,
	.sync_abort			= lck_sync_abort,
};

/*
 * Dynamic loader definition
 */
static struct openais_service_handler *lck_get_handler_ver0 (void);

static struct openais_service_handler_iface_ver0 lck_service_handler_iface = {
	.openais_get_service_handler_ver0	= lck_get_handler_ver0
};

static struct lcr_iface openais_lck_ver0[1] = {
	{
		.name				= "openais_lck",
		.version			= 0,
		.versions_replace		= 0,
		.versions_replace_count		= 0,
		.dependencies			= 0,
		.dependency_count		= 0,
		.constructor			= NULL,
		.destructor			= NULL,
		.interfaces			= (void **)(void *)&lck_service_handler_iface,
	}
};

static struct lcr_comp lck_comp_ver0 = {
	.iface_count				= 1,
	.ifaces					= openais_lck_ver0
};

static struct openais_service_handler *lck_get_handler_ver0 (void)
{
	return (&lck_service_handler);
}

__attribute__ ((constructor)) static void register_this_component (void) {
	lcr_interfaces_set (&openais_lck_ver0[0], &lck_service_handler_iface);

	lcr_component_register (&lck_comp_ver0);
}

/*
 * All data types used for executive messages
 */
struct req_exec_lck_resourceopen {
	mar_req_header_t header;
	mar_message_source_t source;
	mar_name_t resource_name;
	SaLckResourceHandleT resource_handle;
	SaInvocationT invocation;
	SaTimeT timeout;
	SaLckResourceOpenFlagsT open_flags;
	int async_call;
	SaAisErrorT fail_with_error;
};

static void exec_lck_resourceopen_endian_convert (void *msg)
{
	struct req_exec_lck_resourceopen *to_swab =
		(struct req_exec_lck_resourceopen *)msg;

	swab_mar_req_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->resource_name);
	to_swab->resource_handle = swab64 (to_swab->resource_handle);
	to_swab->invocation = swab64 (to_swab->invocation);
	to_swab->timeout = swab64 (to_swab->timeout);
	to_swab->open_flags = swab32 (to_swab->open_flags);
	to_swab->async_call = swab32 (to_swab->async_call);
	to_swab->fail_with_error = swab32 (to_swab->fail_with_error);
}

struct req_exec_lck_resourceclose {
	mar_req_header_t header;
	mar_message_source_t source;
	mar_name_t lockResourceName;
	SaLckResourceHandleT resource_handle;
};

static void exec_lck_resourceclose_endian_convert (void *msg)
{
	struct req_exec_lck_resourceclose *to_swab =
		(struct req_exec_lck_resourceclose *)msg;

	swab_mar_req_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->lockResourceName);
	to_swab->resource_handle = swab64 (to_swab->resource_handle);
}

struct req_exec_lck_resourcelock {
	mar_req_header_t header;
	SaLckResourceHandleT resource_handle;
	SaInvocationT invocation;
	int async_call;
	SaAisErrorT fail_with_error;
	mar_message_source_t source;
	struct req_lib_lck_resourcelock req_lib_lck_resourcelock;
};

static void exec_lck_resourcelock_endian_convert (void *msg)
{
	struct req_exec_lck_resourcelock *to_swab =
		(struct req_exec_lck_resourcelock *)msg;

	swab_mar_req_header_t (&to_swab->header);
	to_swab->resource_handle = swab64 (to_swab->resource_handle);
	to_swab->invocation = swab64 (to_swab->invocation);
	to_swab->async_call = swab32 (to_swab->async_call);
	to_swab->fail_with_error = swab32 (to_swab->fail_with_error);
	swab_mar_message_source_t (&to_swab->source);
	swab_req_lib_lck_resourcelock (&to_swab->req_lib_lck_resourcelock);
}

struct req_exec_lck_resourceunlock {
	mar_req_header_t header;
	mar_message_source_t source;
	mar_name_t resource_name;
	SaLckLockIdT lock_id;
	SaInvocationT invocation;
	SaTimeT timeout;
	int async_call;
};

static void exec_lck_resourceunlock_endian_convert (void *msg)
{
	struct req_exec_lck_resourceunlock *to_swab =
		(struct req_exec_lck_resourceunlock *)msg;

	swab_mar_req_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->resource_name);
	to_swab->lock_id = swab64 (to_swab->lock_id);
	to_swab->invocation = swab64 (to_swab->invocation);
	to_swab->timeout = swab64 (to_swab->timeout);
	to_swab->async_call = swab32 (to_swab->async_call);
}

struct req_exec_lck_resourcelockorphan {
	mar_req_header_t header;
	mar_message_source_t source;
	mar_name_t resource_name;
	SaLckLockIdT lock_id;
};

static void exec_lck_resourcelockorphan_endian_convert (void *msg)
{
	struct req_exec_lck_resourcelockorphan *to_swab =
		(struct req_exec_lck_resourcelockorphan *)msg;

	swab_mar_req_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->resource_name);
	to_swab->lock_id = swab64 (to_swab->lock_id);
}

struct req_exec_lck_lockpurge {
	mar_req_header_t header;
	mar_message_source_t source;
	struct req_lib_lck_lockpurge req_lib_lck_lockpurge;
};

static void exec_lck_lockpurge_endian_convert (void *msg)
{
	struct req_exec_lck_lockpurge *to_swab =
		(struct req_exec_lck_lockpurge *)msg;

	swab_mar_req_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_req_lib_lck_lockpurge (&to_swab->req_lib_lck_lockpurge);
}

#ifdef TODO
static void lck_sync_init (void)
{
	return;
}
#endif

static int lck_sync_process (void)
{
	return (0);
}

static void lck_sync_activate (void)
{
	return;
}

static void lck_sync_abort (void)
{
	return;
}

static void lck_confchg_fn (
	enum totem_configuration_type configuration_type,
	unsigned int *member_list, int member_list_entries,
	unsigned int *left_list, int left_list_entries,
	unsigned int *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id)
{
}

static struct resource *resource_find (mar_name_t *name)
{
	struct list_head *resource_list;
	struct resource *resource;

   for (resource_list = resource_list_head.next;
        resource_list != &resource_list_head;
        resource_list = resource_list->next) {

        resource = list_entry (resource_list,
            struct resource, list);

		if (mar_name_match (name, &resource->name)) {
			return (resource);
		}
	}
	return (0);
}

static struct resource_lock *resource_lock_find (
	struct resource *resource,
	mar_message_source_t *source,
	SaLckLockIdT lock_id)
{
	struct list_head *list;
	struct resource_lock *resource_lock;

	for (list = resource->resource_lock_list_head.next;
		list != &resource->resource_lock_list_head;
		list = list->next) {

		resource_lock = list_entry (list, struct resource_lock, resource_list);

		if ((memcmp (&resource_lock->callback_source,
			source, sizeof (mar_message_source_t)) == 0) &&
			(lock_id == resource_lock->lock_id)) {

			return (resource_lock);
		}
	}
	return (0);
}

struct resource_cleanup *lck_resource_cleanup_find (
	void *conn,
	SaLckResourceHandleT resource_handle)
{
	struct list_head *list;
	struct resource_cleanup *resource_cleanup;
	struct lck_pd *lck_pd = (struct lck_pd *)openais_conn_private_data_get (conn);

	for (list = lck_pd->resource_cleanup_list.next;
		list != &lck_pd->resource_cleanup_list; list = list->next) {

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
	req_exec_lck_resourceclose.header.id =
		SERVICE_ID_MAKE (LCK_SERVICE, MESSAGE_REQ_EXEC_LCK_RESOURCECLOSE);

	memcpy (&req_exec_lck_resourceclose.lockResourceName,
		&resource->name, sizeof (mar_name_t));

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
	req_exec_lck_resourcelockorphan.header.id =
		SERVICE_ID_MAKE (LCK_SERVICE, MESSAGE_REQ_EXEC_LCK_RESOURCELOCKORPHAN);

	memcpy (&req_exec_lck_resourcelockorphan.source,
		&resource_lock->callback_source,
		sizeof (mar_message_source_t));

	memcpy (&req_exec_lck_resourcelockorphan.resource_name,
		&resource_lock->resource->name,
		sizeof (mar_name_t));
		
	req_exec_lck_resourcelockorphan.lock_id = resource_lock->lock_id;
	
	iovec.iov_base = (char *)&req_exec_lck_resourcelockorphan;
	iovec.iov_len = sizeof (req_exec_lck_resourcelockorphan);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1, TOTEMPG_AGREED) == 0);
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
	void *conn,
	SaLckResourceHandleT resource_handle)
{

	struct list_head *list;
	struct resource_cleanup *resource_cleanup;
	struct lck_pd *lck_pd = (struct lck_pd *)openais_conn_private_data_get (conn);

	for (list = lck_pd->resource_cleanup_list.next;
		list != &lck_pd->resource_cleanup_list;
		list = list->next) {

		resource_cleanup = list_entry (list, struct resource_cleanup, list);
		if (resource_cleanup->resource_handle == resource_handle) {
			list_del (&resource_cleanup->list);
			free (resource_cleanup);
			return;
		}
	}
}


static int lck_exec_init_fn (struct objdb_iface_ver0 *objdb)
{
	log_init ("LCK");

	/*
	 *  Initialize the saved ring ID.
	 */
	return (0);
}

static int lck_lib_exit_fn (void *conn)
{
	struct resource_cleanup *resource_cleanup;
	struct list_head *cleanup_list;
	struct lck_pd *lck_pd = (struct lck_pd *)openais_conn_private_data_get (conn);

	log_printf(LOG_LEVEL_NOTICE, "lck_exit_fn conn_info %p\n", conn);

	/*
	 * close all resources opened on this fd
	 */
	cleanup_list = lck_pd->resource_cleanup_list.next;
	while (!list_empty(cleanup_list)) {

		resource_cleanup = list_entry (cleanup_list, struct resource_cleanup, list);

		if (resource_cleanup->resource->name.length > 0) {
			lck_resource_cleanup_lock_remove (resource_cleanup);
			lck_resource_close (resource_cleanup->resource);
		}

		list_del (&resource_cleanup->list);
		free (resource_cleanup);

		cleanup_list = lck_pd->resource_cleanup_list.next;
	}

	return (0);
}

static int lck_lib_init_fn (void *conn)
{
    struct lck_pd *lck_pd = (struct lck_pd *)openais_conn_private_data_get (conn);

	list_init (&lck_pd->resource_list);
	list_init (&lck_pd->resource_cleanup_list);
	return (0);
}

static void message_handler_req_exec_lck_resourceopen (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_lck_resourceopen *req_exec_lck_resourceopen = (struct req_exec_lck_resourceopen *)message;
	struct res_lib_lck_resourceopen res_lib_lck_resourceopen;
	struct res_lib_lck_resourceopenasync res_lib_lck_resourceopenasync;
	struct resource *resource;
	struct resource_cleanup *resource_cleanup;
	SaAisErrorT error = SA_AIS_OK;
	struct lck_pd *lck_pd = (struct lck_pd *)openais_conn_private_data_get (req_exec_lck_resourceopen->source.conn);

	log_printf (LOG_LEVEL_NOTICE, "EXEC request: saLckResourceOpen %s\n",
		get_mar_name_t (&req_exec_lck_resourceopen->resource_name));
	
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
			sizeof (mar_name_t));
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
			log_printf (LOG_LEVEL_DEBUG, "resource is %p\n", resource);
			resource_cleanup->resource_handle = req_exec_lck_resourceopen->resource_handle;
			list_add (
				&resource_cleanup->list,
				&lck_pd->resource_cleanup_list);
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
				sizeof (mar_message_source_t));

			openais_conn_send_response (
				req_exec_lck_resourceopen->source.conn,
				&res_lib_lck_resourceopenasync,
				sizeof (struct res_lib_lck_resourceopenasync));
			openais_conn_send_response (
				openais_conn_partner_get (req_exec_lck_resourceopen->source.conn),
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
				sizeof (mar_message_source_t));

			openais_conn_send_response (req_exec_lck_resourceopen->source.conn,
				&res_lib_lck_resourceopen,
				sizeof (struct res_lib_lck_resourceopen));
		}
	}
}

static void message_handler_req_exec_lck_resourceclose (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_lck_resourceclose *req_exec_lck_resourceclose = (struct req_exec_lck_resourceclose *)message;
	struct res_lib_lck_resourceclose res_lib_lck_resourceclose;
	struct resource *resource = 0;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_NOTICE, "EXEC request: saLckResourceClose %s\n",
		get_mar_name_t (&req_exec_lck_resourceclose->lockResourceName));

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
			req_exec_lck_resourceclose->source.conn,
			req_exec_lck_resourceclose->resource_handle);

		res_lib_lck_resourceclose.header.size = sizeof (struct res_lib_lck_resourceclose);
		res_lib_lck_resourceclose.header.id = MESSAGE_RES_LCK_RESOURCECLOSE;
		res_lib_lck_resourceclose.header.error = error;
		openais_conn_send_response (
			req_exec_lck_resourceclose->source.conn,
			&res_lib_lck_resourceclose, sizeof (struct res_lib_lck_resourceclose));
	}
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

	openais_conn_send_response (
		openais_conn_partner_get (resource_lock->callback_source.conn),
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
	mar_message_source_t *source,
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
			openais_conn_send_response (
				openais_conn_partner_get (source->conn),
				&res_lib_lck_resourcelockasync,
				sizeof (struct res_lib_lck_resourcelockasync));
		}
	}
}

void lock_response_deliver (
	mar_message_source_t *source,
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
			openais_conn_send_response (source->conn,
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
 *	if lock in ex, set ex to null
 *	delete resource lock from list
 *
 *	 if ex lock not granted
 *		if ex pending list has locks
 *			grant first ex pending list lock to ex lock
 *	if ex lock not granted
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
	else {
		/*
		 * Delete resource lock from whichever list it is on
		 */
		list_del (&resource_lock->list);
	}
	list_del (&resource_lock->resource_list);

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
			list_p = resource->pr_pending_list_head.next;
			list_del (&resource->pr_pending_list_head);
			list_add_tail (list_p,
				&resource->pr_granted_list_head);
		}
	}
}

static void message_handler_req_exec_lck_resourcelock (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_lck_resourcelock *req_exec_lck_resourcelock = (struct req_exec_lck_resourcelock *)message;
	struct resource *resource = 0;
	struct resource_lock *resource_lock = 0;
	struct resource_cleanup *resource_cleanup = 0;

	log_printf (LOG_LEVEL_NOTICE, "EXEC request: saLckResourceLock %s\n",
		get_mar_name_t (&req_exec_lck_resourcelock->req_lib_lck_resourcelock.lockResourceName));

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
		sizeof (mar_message_source_t));

	lock_algorithm (resource, resource_lock);

	/*
	 * Add resource lock to cleanup handler for this api resource instance
	 */
	if (message_source_is_local (&req_exec_lck_resourcelock->source)) {
		resource_cleanup = lck_resource_cleanup_find (
			resource_lock->callback_source.conn,
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
				sizeof (mar_message_source_t));
		}

		/*
		 * Deliver async response to library
		 */
		req_exec_lck_resourcelock->source.conn =
			openais_conn_partner_get (req_exec_lck_resourcelock->source.conn);
		resource_lock_async_deliver (
			&req_exec_lck_resourcelock->source,
			resource_lock,
			SA_AIS_OK);
// TODO why is this twice ?
		req_exec_lck_resourcelock->source.conn =
			openais_conn_partner_get (req_exec_lck_resourcelock->source.conn);
	}

error_exit:
	return;
}

static void message_handler_req_exec_lck_resourceunlock (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_lck_resourceunlock *req_exec_lck_resourceunlock = (struct req_exec_lck_resourceunlock *)message;
	struct res_lib_lck_resourceunlock res_lib_lck_resourceunlock;
	struct res_lib_lck_resourceunlockasync res_lib_lck_resourceunlockasync;
	struct resource *resource = NULL;
	struct resource_lock *resource_lock = NULL;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_NOTICE, "EXEC request: saLckResourceUnlock %s\n",
		get_mar_name_t (&req_exec_lck_resourceunlock->resource_name));

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

			openais_conn_send_response (
				req_exec_lck_resourceunlock->source.conn,
				&res_lib_lck_resourceunlockasync,
				sizeof (struct res_lib_lck_resourceunlockasync));
			openais_conn_send_response (
				openais_conn_partner_get(resource_lock->callback_source.conn),
				&res_lib_lck_resourceunlockasync,
				sizeof (struct res_lib_lck_resourceunlockasync));
		} else {
			res_lib_lck_resourceunlock.header.size = sizeof (struct res_lib_lck_resourceunlock);
			res_lib_lck_resourceunlock.header.id = MESSAGE_RES_LCK_RESOURCEUNLOCK;
			res_lib_lck_resourceunlock.header.error = error;
			openais_conn_send_response (req_exec_lck_resourceunlock->source.conn,
				&res_lib_lck_resourceunlock, sizeof (struct res_lib_lck_resourceunlock));
		}
	}
}

static void message_handler_req_exec_lck_resourcelockorphan (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_lck_resourcelockorphan *req_exec_lck_resourcelockorphan = (struct req_exec_lck_resourcelockorphan *)message;
	struct resource *resource = 0;
	struct resource_lock *resource_lock = 0;

	log_printf (LOG_LEVEL_NOTICE, "EXEC request: Orphan resource locks for resource %s\n",
		get_mar_name_t (&req_exec_lck_resourcelockorphan->resource_name));

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
}

static void message_handler_req_exec_lck_lockpurge (
	void *msg,
	unsigned int nodeid)
{
	struct req_exec_lck_lockpurge *req_exec_lck_lockpurge = (struct req_exec_lck_lockpurge *)msg;
	struct res_lib_lck_lockpurge res_lib_lck_lockpurge;
	struct resource *resource = 0;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_DEBUG, "EXEC request: saLckLockPurge %s\n",
		get_mar_name_t (&req_exec_lck_lockpurge->req_lib_lck_lockpurge.lockResourceName));

	resource = resource_find (&req_exec_lck_lockpurge->req_lib_lck_lockpurge.lockResourceName);
	if (resource == 0) {
		goto error_exit;
	}

error_exit:
	if (message_source_is_local(&req_exec_lck_lockpurge->source)) {
//		lck_resource_cleanup_remove (req_exec_lck_lockpurge->source.conn,
//			resource);

		res_lib_lck_lockpurge.header.size = sizeof (struct res_lib_lck_lockpurge);
		res_lib_lck_lockpurge.header.id = MESSAGE_RES_LCK_LOCKPURGE;
		res_lib_lck_lockpurge.header.error = error;
		openais_conn_send_response (req_exec_lck_lockpurge->source.conn,
			&res_lib_lck_lockpurge, sizeof (struct res_lib_lck_lockpurge));
	}
}

static void message_handler_req_lib_lck_resourceopen (
	void *conn,
	void *msg)
{
	struct req_lib_lck_resourceopen *req_lib_lck_resourceopen = (struct req_lib_lck_resourceopen *)msg;
	struct req_exec_lck_resourceopen req_exec_lck_resourceopen;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saLckResourceOpen %s\n",
		get_mar_name_t (&req_lib_lck_resourceopen->lockResourceName));

	req_exec_lck_resourceopen.header.size =
		sizeof (struct req_exec_lck_resourceopen);
	req_exec_lck_resourceopen.header.id =
		SERVICE_ID_MAKE (LCK_SERVICE, MESSAGE_REQ_EXEC_LCK_RESOURCEOPEN);

	message_source_set (&req_exec_lck_resourceopen.source, conn);

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
}

static void message_handler_req_lib_lck_resourceopenasync (
	void *conn,
	void *msg)
{
	struct req_lib_lck_resourceopen *req_lib_lck_resourceopen = (struct req_lib_lck_resourceopen *)msg;
	struct req_exec_lck_resourceopen req_exec_lck_resourceopen;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saLckResourceOpenAsync %s\n",
		get_mar_name_t (&req_lib_lck_resourceopen->lockResourceName));

	req_exec_lck_resourceopen.header.size =
		sizeof (struct req_exec_lck_resourceopen);
	req_exec_lck_resourceopen.header.id =
		SERVICE_ID_MAKE (LCK_SERVICE, MESSAGE_REQ_EXEC_LCK_RESOURCEOPEN);

	message_source_set (&req_exec_lck_resourceopen.source, conn);

	memcpy (&req_exec_lck_resourceopen.resource_name,
		&req_lib_lck_resourceopen->lockResourceName,
		sizeof (mar_name_t));
	
	req_exec_lck_resourceopen.resource_handle = req_lib_lck_resourceopen->resourceHandle;
	req_exec_lck_resourceopen.invocation = req_lib_lck_resourceopen->invocation;
	req_exec_lck_resourceopen.open_flags = req_lib_lck_resourceopen->resourceOpenFlags;
	req_exec_lck_resourceopen.timeout = 0;
	req_exec_lck_resourceopen.async_call = 1;

	iovec.iov_base = (char *)&req_exec_lck_resourceopen;
	iovec.iov_len = sizeof (req_exec_lck_resourceopen);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1, TOTEMPG_AGREED) == 0);
}

static void message_handler_req_lib_lck_resourceclose (
	void *conn,
	void *msg)
{
	struct req_lib_lck_resourceclose *req_lib_lck_resourceclose = (struct req_lib_lck_resourceclose *)msg;
	struct req_exec_lck_resourceclose req_exec_lck_resourceclose;
	struct iovec iovecs[2];
	struct resource *resource;
	struct res_lib_lck_resourceclose res_lib_lck_resourceclose;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saLckResourceClose %s\n",
		get_mar_name_t (&req_lib_lck_resourceclose->lockResourceName));

	resource = resource_find (&req_lib_lck_resourceclose->lockResourceName);
	if (resource) {
		req_exec_lck_resourceclose.header.size =
			sizeof (struct req_exec_lck_resourceclose);
		req_exec_lck_resourceclose.header.id =
			SERVICE_ID_MAKE (LCK_SERVICE, MESSAGE_REQ_EXEC_LCK_RESOURCECLOSE);

		message_source_set (&req_exec_lck_resourceclose.source, conn);

		memcpy (&req_exec_lck_resourceclose.lockResourceName,
			&req_lib_lck_resourceclose->lockResourceName, sizeof (mar_name_t));

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

		openais_conn_send_response (conn,
			&res_lib_lck_resourceclose,
			sizeof (struct res_lib_lck_resourceclose));
	}
}

static void message_handler_req_lib_lck_resourcelock (
	void *conn,
	void *msg)
{
	struct req_lib_lck_resourcelock *req_lib_lck_resourcelock = (struct req_lib_lck_resourcelock *)msg;
	struct req_exec_lck_resourcelock req_exec_lck_resourcelock;
	struct iovec iovecs[2];

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saLckResourceLock %s\n",
		get_mar_name_t (&req_lib_lck_resourcelock->lockResourceName));

	req_exec_lck_resourcelock.header.size =
		sizeof (struct req_exec_lck_resourcelock);
	req_exec_lck_resourcelock.header.id =
		SERVICE_ID_MAKE (LCK_SERVICE, MESSAGE_REQ_EXEC_LCK_RESOURCELOCK);

	message_source_set (&req_exec_lck_resourcelock.source, conn);

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
}

static void message_handler_req_lib_lck_resourcelockasync (
	void *conn,
	void *msg)
{
	struct req_lib_lck_resourcelock *req_lib_lck_resourcelock = (struct req_lib_lck_resourcelock *)msg;
	struct req_exec_lck_resourcelock req_exec_lck_resourcelock;
	struct iovec iovecs[2];

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saLckResourceLockAsync %s\n",
		get_mar_name_t (&req_lib_lck_resourcelock->lockResourceName));

	req_exec_lck_resourcelock.header.size =
		sizeof (struct req_exec_lck_resourcelock);
	req_exec_lck_resourcelock.header.id =
		SERVICE_ID_MAKE (LCK_SERVICE, MESSAGE_REQ_EXEC_LCK_RESOURCELOCK);

	message_source_set (&req_exec_lck_resourcelock.source, conn);

	memcpy (&req_exec_lck_resourcelock.req_lib_lck_resourcelock,
		req_lib_lck_resourcelock,
		sizeof (struct req_lib_lck_resourcelock));

	req_exec_lck_resourcelock.resource_handle = req_lib_lck_resourcelock->resourceHandle;
	req_exec_lck_resourcelock.async_call = 1;
	req_exec_lck_resourcelock.invocation = req_lib_lck_resourcelock->invocation;

	iovecs[0].iov_base = (char *)&req_exec_lck_resourcelock;
	iovecs[0].iov_len = sizeof (req_exec_lck_resourcelock);

	assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 1, TOTEMPG_AGREED) == 0);
}

static void message_handler_req_lib_lck_resourceunlock (
	void *conn,
	void *msg)
{
	struct req_lib_lck_resourceunlock *req_lib_lck_resourceunlock = (struct req_lib_lck_resourceunlock *)msg;
	struct req_exec_lck_resourceunlock req_exec_lck_resourceunlock;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saLckResourceUnlock %s\n",
		get_mar_name_t (&req_lib_lck_resourceunlock->lockResourceName));

	req_exec_lck_resourceunlock.header.size =
		sizeof (struct req_exec_lck_resourceunlock);
	req_exec_lck_resourceunlock.header.id =
		SERVICE_ID_MAKE (LCK_SERVICE, MESSAGE_REQ_EXEC_LCK_RESOURCEUNLOCK);

	message_source_set (&req_exec_lck_resourceunlock.source, conn);

	memcpy (&req_exec_lck_resourceunlock.resource_name,
		&req_lib_lck_resourceunlock->lockResourceName,
		sizeof (mar_name_t));
		
	req_exec_lck_resourceunlock.lock_id = req_lib_lck_resourceunlock->lockId;
	req_exec_lck_resourceunlock.async_call = 0;
	req_exec_lck_resourceunlock.invocation = 0;
	
	iovec.iov_base = (char *)&req_exec_lck_resourceunlock;
	iovec.iov_len = sizeof (req_exec_lck_resourceunlock);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1, TOTEMPG_AGREED) == 0);
}

static void message_handler_req_lib_lck_resourceunlockasync (
	void *conn,
	void *msg)
{
	struct req_lib_lck_resourceunlock *req_lib_lck_resourceunlock = (struct req_lib_lck_resourceunlock *)msg;
	struct req_exec_lck_resourceunlock req_exec_lck_resourceunlock;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saLckResourceUnlockAsync %s\n",
		get_mar_name_t (&req_lib_lck_resourceunlock->lockResourceName));

	req_exec_lck_resourceunlock.header.size =
		sizeof (struct req_exec_lck_resourceunlock);
	req_exec_lck_resourceunlock.header.id =
		SERVICE_ID_MAKE (LCK_SERVICE, MESSAGE_REQ_EXEC_LCK_RESOURCEUNLOCK);

	message_source_set (&req_exec_lck_resourceunlock.source, conn);

	memcpy (&req_exec_lck_resourceunlock.resource_name,
		&req_lib_lck_resourceunlock->lockResourceName,
		sizeof (mar_name_t));
		
	req_exec_lck_resourceunlock.lock_id = req_lib_lck_resourceunlock->lockId;
	req_exec_lck_resourceunlock.invocation = req_lib_lck_resourceunlock->invocation;
	req_exec_lck_resourceunlock.async_call = 1;
	
	iovec.iov_base = (char *)&req_exec_lck_resourceunlock;
	iovec.iov_len = sizeof (req_exec_lck_resourceunlock);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1, TOTEMPG_AGREED) == 0);
}

static void message_handler_req_lib_lck_lockpurge (
	void *conn,
	void *msg)
{
	struct req_lib_lck_lockpurge *req_lib_lck_lockpurge = (struct req_lib_lck_lockpurge *)msg;
	struct req_exec_lck_lockpurge req_exec_lck_lockpurge;
	struct iovec iovecs[2];

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saLckResourceLockPurge %s\n",
		get_mar_name_t (&req_lib_lck_lockpurge->lockResourceName));

	req_exec_lck_lockpurge.header.size =
		sizeof (struct req_exec_lck_lockpurge);
	req_exec_lck_lockpurge.header.id =
		SERVICE_ID_MAKE (LCK_SERVICE, MESSAGE_REQ_EXEC_LCK_LOCKPURGE);

	message_source_set (&req_exec_lck_lockpurge.source, conn);

	memcpy (&req_exec_lck_lockpurge.req_lib_lck_lockpurge,
		req_lib_lck_lockpurge,
		sizeof (struct req_lib_lck_lockpurge));

	iovecs[0].iov_base = (char *)&req_exec_lck_lockpurge;
	iovecs[0].iov_len = sizeof (req_exec_lck_lockpurge);

	assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 1, TOTEMPG_AGREED) == 0);
}

