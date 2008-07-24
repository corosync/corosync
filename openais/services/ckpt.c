/*
 * Copyright (c) 2003-2006 MontaVista Software, Inc.
 * Copyright (c) 2006-2007 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Authors: Steven Dake (sdake@redhat.com)
 *         Muni Bajpai (muni.osdl@gmail.com)
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

#include <corosync/ipc_gen.h>
#include <corosync/mar_gen.h>
#include <corosync/hdb.h>
#include <corosync/engine/swab.h>
#include <corosync/engine/list.h>
#include <corosync/engine/coroapi.h>
#include <corosync/engine/logsys.h>
#include <corosync/saAis.h>
#include <corosync/lcr/lcr_comp.h>
#include "../include/saEvt.h"
#include "../include/ipc_ckpt.h"
#include "../include/mar_ckpt.h"

LOGSYS_DECLARE_SUBSYS ("CKPT", LOG_INFO);

#define CKPT_MAX_SECTION_DATA_SEND (1024*400)

enum ckpt_message_req_types {
	MESSAGE_REQ_EXEC_CKPT_CHECKPOINTOPEN = 0,
	MESSAGE_REQ_EXEC_CKPT_CHECKPOINTCLOSE = 1,
	MESSAGE_REQ_EXEC_CKPT_CHECKPOINTUNLINK = 2,
	MESSAGE_REQ_EXEC_CKPT_CHECKPOINTRETENTIONDURATIONSET = 3,
	MESSAGE_REQ_EXEC_CKPT_CHECKPOINTRETENTIONDURATIONEXPIRE = 4,
	MESSAGE_REQ_EXEC_CKPT_SECTIONCREATE = 5,
	MESSAGE_REQ_EXEC_CKPT_SECTIONDELETE = 6,
	MESSAGE_REQ_EXEC_CKPT_SECTIONEXPIRATIONTIMESET = 7,
	MESSAGE_REQ_EXEC_CKPT_SECTIONWRITE = 8,
	MESSAGE_REQ_EXEC_CKPT_SECTIONOVERWRITE = 9,
	MESSAGE_REQ_EXEC_CKPT_SECTIONREAD = 10,
	MESSAGE_REQ_EXEC_CKPT_SYNCCHECKPOINT = 11,
	MESSAGE_REQ_EXEC_CKPT_SYNCCHECKPOINTSECTION = 12,
	MESSAGE_REQ_EXEC_CKPT_SYNCCHECKPOINTREFCOUNT = 13
};

struct checkpoint_section {
	struct list_head list;
	mar_ckpt_section_descriptor_t section_descriptor;
	void *section_data;
	corosync_timer_handle_t expiration_timer;
};

enum sync_state {
	SYNC_STATE_CHECKPOINT,
	SYNC_STATE_REFCOUNT
};

enum iteration_state {
	ITERATION_STATE_CHECKPOINT,
	ITERATION_STATE_SECTION
};

struct refcount_set {
	unsigned int refcount;
	unsigned int nodeid;
};

typedef struct {
	unsigned int refcount __attribute__((aligned(8)));
	unsigned int nodeid __attribute__((aligned(8)));
} mar_refcount_set_t;

static inline void marshall_to_mar_refcount_set_t (
	mar_refcount_set_t *dest,
	struct refcount_set *src)
{
	dest->refcount = src->refcount;
	dest->nodeid = src->nodeid;
}

static inline void marshall_to_mar_refcount_set_t_all (
	mar_refcount_set_t *dest,
	struct refcount_set *src)
{
	unsigned int i;
	for (i = 0; i < PROCESSOR_COUNT_MAX; i++) {
		marshall_to_mar_refcount_set_t (&dest[i], &src[i]);
	}
}

static inline void marshall_from_mar_refcount_set_t (
	struct refcount_set *dest,
	mar_refcount_set_t *src)
{
	dest->refcount = src->refcount;
	dest->nodeid = src->nodeid;
}

static inline void marshall_from_mar_refcount_set_t_all (
	struct refcount_set *dest,
	mar_refcount_set_t *src)
{
	unsigned int i;

	for (i = 0; i < PROCESSOR_COUNT_MAX; i++) {
		marshall_from_mar_refcount_set_t (&dest[i], &src[i]);
	}
}

static inline void swab_mar_refcount_set_t (mar_refcount_set_t *to_swab)
{
	swab_mar_uint32_t (&to_swab->refcount);
	swab_mar_uint32_t (&to_swab->nodeid);
}

struct checkpoint {
	struct list_head list;
	mar_name_t name;
	mar_uint32_t ckpt_id;
	mar_ckpt_checkpoint_creation_attributes_t checkpoint_creation_attributes;
	struct list_head sections_list_head;
	int reference_count;
	int unlinked;
	corosync_timer_handle_t retention_timer;
	int active_replica_set;
	int section_count;
	struct refcount_set refcount_set[PROCESSOR_COUNT_MAX];
};

struct iteration_entry {
	char *section_id;
	unsigned int section_id_len;
};

struct iteration_instance {
	struct iteration_entry *iteration_entries;
	mar_name_t checkpoint_name;
	mar_uint32_t ckpt_id;
	int iteration_entries_count;
	unsigned int iteration_pos;
};

struct ckpt_pd {
	struct list_head checkpoint_list;
	struct hdb_handle_database iteration_hdb;
	unsigned int iteration_pos;
};

struct ckpt_identifier {
	mar_name_t ckpt_name;
	mar_uint32_t ckpt_id;
	mar_ckpt_section_id_t ckpt_section_id;
};

static int ckpt_exec_init_fn (struct corosync_api_v1 *);

static int ckpt_lib_exit_fn (void *conn);

static int ckpt_lib_init_fn (void *conn);

static void ckpt_dump_fn (void);

static void message_handler_req_lib_ckpt_checkpointopen (
	void *conn,
	void *msg);

static void message_handler_req_lib_ckpt_checkpointclose (
	void *conn,
	void *msg);

static void message_handler_req_lib_ckpt_checkpointunlink (
	void *conn,
	void *msg);

static void message_handler_req_lib_ckpt_checkpointretentiondurationset (
	void *conn,
	void *msg);

static void message_handler_req_lib_ckpt_activereplicaset (
	void *conn,
	void *msg);

static void message_handler_req_lib_ckpt_checkpointstatusget (
	void *conn,
	void *msg);

static void message_handler_req_lib_ckpt_sectioncreate (
	void *conn,
	void *msg);

static void message_handler_req_lib_ckpt_sectiondelete (
	void *conn,
	void *msg);

static void message_handler_req_lib_ckpt_sectionexpirationtimeset (
	void *conn,
	void *msg);

static void message_handler_req_lib_ckpt_sectionwrite (
	void *conn,
	void *msg);

static void message_handler_req_lib_ckpt_sectionoverwrite (
	void *conn,
	void *msg);

static void message_handler_req_lib_ckpt_sectionread (
	void *conn,
	void *msg);

static void message_handler_req_lib_ckpt_checkpointsynchronize (
	void *conn,
	void *msg);

static void message_handler_req_lib_ckpt_checkpointsynchronizeasync (
	void *conn,
	void *msg);

static void message_handler_req_lib_ckpt_sectioniterationinitialize (
	void *conn,
	void *msg);

static void message_handler_req_lib_ckpt_sectioniterationfinalize (
	void *conn,
	void *msg);

static void message_handler_req_lib_ckpt_sectioniterationnext (
	void *conn,
	void *msg);

static void message_handler_req_exec_ckpt_checkpointopen (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_ckpt_sync_checkpoint (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_ckpt_sync_checkpoint_section (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_ckpt_sync_checkpoint_refcount (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_ckpt_checkpointclose (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_ckpt_checkpointunlink (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_ckpt_checkpointretentiondurationset (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_ckpt_checkpointretentiondurationexpire (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_ckpt_sectioncreate (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_ckpt_sectiondelete (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_ckpt_sectionexpirationtimeset (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_ckpt_sectionwrite (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_ckpt_sectionoverwrite (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_ckpt_sectionread (
	void *message,
	unsigned int nodeid);

static void exec_ckpt_checkpointopen_endian_convert (void *msg);
static void exec_ckpt_checkpointclose_endian_convert (void *msg);
static void exec_ckpt_checkpointunlink_endian_convert (void *msg);
static void exec_ckpt_checkpointretentiondurationset_endian_convert (void *msg);
static void exec_ckpt_checkpointretentiondurationexpire_endian_convert (void *msg);
static void exec_ckpt_sectioncreate_endian_convert (void *msg);
static void exec_ckpt_sectiondelete_endian_convert (void *msg);
static void exec_ckpt_sectrionexpirationtimeset_endian_convert (void *msg);
static void exec_ckpt_sectionwrite_endian_convert (void *msg);
static void exec_ckpt_sectionoverwrite_endian_convert (void *msg);
static void exec_ckpt_sectionread_endian_convert (void *msg);
static void exec_ckpt_sync_checkpoint_endian_convert (void *msg);
static void exec_ckpt_sync_checkpoint_section_endian_convert (void *msg);
static void exec_ckpt_sync_checkpoint_refcount_endian_convert (void *msg);


static void ckpt_sync_init (void);
static void ckpt_sync_activate (void);
static int  ckpt_sync_process (void);
static void ckpt_sync_abort(void);

static void sync_refcount_increment (
	struct checkpoint *checkpoint, unsigned int nodeid);

static void sync_refcount_decrement (
	struct checkpoint *checkpoint, unsigned int nodeid);

static void sync_refcount_calculate (
	struct checkpoint *checkpoint);

void checkpoint_release (struct checkpoint *checkpoint);
void timer_function_retention (void *data);
unsigned int abstime_to_msec (mar_time_t time);
void timer_function_section_expire (void *data);
void clean_checkpoint_list(struct list_head* head);

DECLARE_LIST_INIT(checkpoint_list_head);

DECLARE_LIST_INIT(sync_checkpoint_list_head);

DECLARE_LIST_INIT(checkpoint_iteration_list_head);

DECLARE_LIST_INIT(checkpoint_recovery_list_head);

static struct corosync_api_v1 *api;

static mar_uint32_t global_ckpt_id = 0;

static enum sync_state my_sync_state;

static enum iteration_state my_iteration_state;

static struct list_head *my_iteration_state_checkpoint;

static struct list_head *my_iteration_state_section;

static unsigned int my_member_list[PROCESSOR_COUNT_MAX];

static unsigned int my_member_list_entries = 0;

static unsigned int my_lowest_nodeid = 0;

struct checkpoint_cleanup {
	struct list_head list;
	mar_name_t checkpoint_name;
	mar_uint32_t ckpt_id;
};

static struct memb_ring_id my_saved_ring_id;

static void ckpt_confchg_fn (
	enum totem_configuration_type configuration_type,
	unsigned int *member_list, int member_list_entries,
	unsigned int *left_list, int left_list_entries,
	unsigned int *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id);

/*
 * Executive Handler Definition
 */
static struct corosync_lib_handler ckpt_lib_engine[] =
{
	{ /* 0 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_checkpointopen,
		.response_size		= sizeof (struct res_lib_ckpt_checkpointopen),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTOPEN,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 1 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_checkpointclose,
		.response_size		= sizeof (struct res_lib_ckpt_checkpointclose),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTCLOSE,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 2 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_checkpointunlink,
		.response_size		= sizeof (struct res_lib_ckpt_checkpointunlink),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTUNLINK,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 3 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_checkpointretentiondurationset,
		.response_size		= sizeof (struct res_lib_ckpt_checkpointretentiondurationset),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTRETENTIONDURATIONSET,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 4 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_activereplicaset,
		.response_size		= sizeof (struct res_lib_ckpt_activereplicaset),
		.response_id		= MESSAGE_RES_CKPT_ACTIVEREPLICASET,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 5 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_checkpointstatusget,
		.response_size		= sizeof (struct res_lib_ckpt_checkpointstatusget),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTSTATUSGET,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 6 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_sectioncreate,
		.response_size		= sizeof (struct res_lib_ckpt_sectioncreate),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_SECTIONCREATE,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 7 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_sectiondelete,
		.response_size		= sizeof (struct res_lib_ckpt_sectiondelete),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_SECTIONDELETE,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 8 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_sectionexpirationtimeset,
		.response_size		= sizeof (struct res_lib_ckpt_sectionexpirationtimeset),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_SECTIONEXPIRATIONTIMESET,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 9 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_sectionwrite,
		.response_size		= sizeof (struct res_lib_ckpt_sectionwrite),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_SECTIONWRITE,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 10 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_sectionoverwrite,
		.response_size		= sizeof (struct res_lib_ckpt_sectionoverwrite),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_SECTIONOVERWRITE,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 11 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_sectionread,
		.response_size		= sizeof (struct res_lib_ckpt_sectionread),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_SECTIONREAD,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 12 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_checkpointsynchronize,
		.response_size		= sizeof (struct res_lib_ckpt_checkpointsynchronize),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTSYNCHRONIZE,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 13 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_checkpointsynchronizeasync,
		.response_size		= sizeof (struct res_lib_ckpt_checkpointsynchronizeasync), /* TODO RESPONSE */
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTSYNCHRONIZEASYNC,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 14 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_sectioniterationinitialize,
		.response_size		= sizeof (struct res_lib_ckpt_sectioniterationinitialize),
		.response_id		= MESSAGE_RES_CKPT_SECTIONITERATIONINITIALIZE,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 15 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_sectioniterationfinalize,
		.response_size		= sizeof (struct res_lib_ckpt_sectioniterationfinalize),
		.response_id		= MESSAGE_RES_CKPT_SECTIONITERATIONFINALIZE,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 16 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_sectioniterationnext,
		.response_size		= sizeof (struct res_lib_ckpt_sectioniterationnext),
		.response_id		= MESSAGE_RES_CKPT_SECTIONITERATIONNEXT,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	}
};


static struct corosync_exec_handler ckpt_exec_engine[] = {
	{
		.exec_handler_fn	= message_handler_req_exec_ckpt_checkpointopen,
		.exec_endian_convert_fn = exec_ckpt_checkpointopen_endian_convert

	},
	{
		.exec_handler_fn	= message_handler_req_exec_ckpt_checkpointclose,
		.exec_endian_convert_fn = exec_ckpt_checkpointclose_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_ckpt_checkpointunlink,
		.exec_endian_convert_fn = exec_ckpt_checkpointunlink_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_ckpt_checkpointretentiondurationset,
		.exec_endian_convert_fn = exec_ckpt_checkpointretentiondurationset_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_ckpt_checkpointretentiondurationexpire,
		.exec_endian_convert_fn = exec_ckpt_checkpointretentiondurationexpire_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_ckpt_sectioncreate,
		.exec_endian_convert_fn = exec_ckpt_sectioncreate_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_ckpt_sectiondelete,
		.exec_endian_convert_fn = exec_ckpt_sectiondelete_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_ckpt_sectionexpirationtimeset,
		.exec_endian_convert_fn = exec_ckpt_sectrionexpirationtimeset_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_ckpt_sectionwrite,
		.exec_endian_convert_fn = exec_ckpt_sectionwrite_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_ckpt_sectionoverwrite,
		.exec_endian_convert_fn = exec_ckpt_sectionoverwrite_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_ckpt_sectionread,
		.exec_endian_convert_fn = exec_ckpt_sectionread_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_ckpt_sync_checkpoint,
		.exec_endian_convert_fn = exec_ckpt_sync_checkpoint_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_ckpt_sync_checkpoint_section,
		.exec_endian_convert_fn = exec_ckpt_sync_checkpoint_section_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_ckpt_sync_checkpoint_refcount,
		.exec_endian_convert_fn = exec_ckpt_sync_checkpoint_refcount_endian_convert
	}
};

struct corosync_service_engine ckpt_service_engine = {
	.name				= "openais checkpoint service B.01.01",
	.id				= CKPT_SERVICE,
	.private_data_size		= sizeof (struct ckpt_pd),
	.flow_control			= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED, 
	.lib_init_fn			= ckpt_lib_init_fn,
	.lib_exit_fn			= ckpt_lib_exit_fn,
	.lib_engine			= ckpt_lib_engine,
	.lib_engine_count		= sizeof (ckpt_lib_engine) / sizeof (struct corosync_lib_handler),
	.exec_init_fn			= ckpt_exec_init_fn,
	.exec_dump_fn			= ckpt_dump_fn,
	.exec_engine			= ckpt_exec_engine,
	.exec_engine_count		= sizeof (ckpt_exec_engine) / sizeof (struct corosync_exec_handler),
	.confchg_fn			= ckpt_confchg_fn,
	.sync_init			= ckpt_sync_init,
	.sync_process			= ckpt_sync_process,
	.sync_activate			= ckpt_sync_activate,
	.sync_abort			= ckpt_sync_abort,
};

/*
 * Dynamic loader definition
 */
static struct corosync_service_engine *ckpt_get_handler_ver0 (void);

static struct corosync_service_engine_iface_ver0 ckpt_service_engine_iface = {
	.corosync_get_service_engine_ver0	= ckpt_get_handler_ver0
};

static struct lcr_iface openais_ckpt_ver0[1] = {
	{
		.name				= "openais_ckpt",
		.version			= 0,
		.versions_replace		= 0,
		.versions_replace_count		= 0,
		.dependencies			= 0,
		.dependency_count		= 0,
		.constructor			= NULL,
		.destructor			= NULL,
		.interfaces			= NULL
	}
};

static struct lcr_comp ckpt_comp_ver0 = {
	.iface_count			= 1,
	.ifaces				= openais_ckpt_ver0
};

static struct corosync_service_engine *ckpt_get_handler_ver0 (void)
{
	return (&ckpt_service_engine);
}

__attribute__ ((constructor)) static void register_this_component (void) {
	lcr_interfaces_set (&openais_ckpt_ver0[0], &ckpt_service_engine_iface);

	lcr_component_register (&ckpt_comp_ver0);
}

/*
 * All data types used for executive messages
 */
struct req_exec_ckpt_checkpointopen {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_uint32_t ckpt_id __attribute__((aligned(8)));
	mar_ckpt_checkpoint_creation_attributes_t checkpoint_creation_attributes __attribute__((aligned(8)));
	mar_uint32_t checkpoint_creation_attributes_set __attribute__((aligned(8)));
	mar_ckpt_checkpoint_open_flags_t checkpoint_open_flags __attribute__((aligned(8)));
	mar_ckpt_checkpoint_handle_t checkpoint_handle __attribute__((aligned(8)));
	mar_invocation_t invocation __attribute__((aligned(8)));
	mar_uint32_t async_call __attribute__((aligned(8)));
	mar_uint32_t fail_with_error __attribute__((aligned(8)));
};

struct req_exec_ckpt_checkpointclose {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_uint32_t ckpt_id __attribute__((aligned(8)));
};

struct req_exec_ckpt_checkpointretentiondurationset {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_uint32_t ckpt_id __attribute__((aligned(8)));
	mar_time_t retention_duration __attribute__((aligned(8)));
};

struct req_exec_ckpt_checkpointretentiondurationexpire {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_uint32_t ckpt_id __attribute__((aligned(8)));
};

struct req_exec_ckpt_checkpointunlink {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
};

struct req_exec_ckpt_sectioncreate {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_uint32_t ckpt_id __attribute__((aligned(8)));
	mar_uint32_t id_len __attribute__((aligned(8)));
	mar_time_t expiration_time __attribute__((aligned(8)));
	mar_uint32_t initial_data_size __attribute__((aligned(8)));
};

struct req_exec_ckpt_sectiondelete {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_uint32_t ckpt_id __attribute__((aligned(8)));
	mar_uint32_t id_len __attribute__((aligned(8)));
};

struct req_exec_ckpt_sectionexpirationtimeset {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_uint32_t ckpt_id __attribute__((aligned(8)));
	mar_uint32_t id_len __attribute__((aligned(8)));
	mar_time_t expiration_time __attribute__((aligned(8)));
};

struct req_exec_ckpt_sectionwrite {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_uint32_t ckpt_id __attribute__((aligned(8)));
	mar_uint32_t id_len __attribute__((aligned(8)));
	mar_offset_t data_offset __attribute__((aligned(8)));
	mar_offset_t data_size __attribute__((aligned(8)));
};

struct req_exec_ckpt_sectionoverwrite {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_uint32_t ckpt_id __attribute__((aligned(8)));
	mar_uint32_t id_len __attribute__((aligned(8)));
	mar_offset_t data_size __attribute__((aligned(8)));
};

struct req_exec_ckpt_sectionread {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_uint32_t ckpt_id __attribute__((aligned(8)));
	mar_uint32_t id_len __attribute__((aligned(8)));
	mar_offset_t data_offset __attribute__((aligned(8)));
	mar_offset_t data_size __attribute__((aligned(8)));
};

struct req_exec_ckpt_sync_checkpoint {
	mar_req_header_t header __attribute__((aligned(8)));
	struct memb_ring_id ring_id __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_uint32_t ckpt_id __attribute__((aligned(8)));
	mar_ckpt_checkpoint_creation_attributes_t checkpoint_creation_attributes __attribute__((aligned(8)));
	mar_uint32_t checkpoint_creation_attributes_set __attribute__((aligned(8)));
	mar_uint32_t active_replica_set __attribute__((aligned(8)));
	mar_uint32_t unlinked __attribute__((aligned(8)));
};

struct req_exec_ckpt_sync_checkpoint_section {
	mar_req_header_t header __attribute__((aligned(8)));
	struct memb_ring_id ring_id __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_uint32_t ckpt_id __attribute__((aligned(8)));
	mar_uint32_t id_len __attribute__((aligned(8)));
	mar_time_t expiration_time __attribute__((aligned(8)));
	mar_uint32_t section_size __attribute__((aligned(8)));
};

struct req_exec_ckpt_sync_checkpoint_refcount {
	mar_req_header_t header __attribute__((aligned(8)));
	struct memb_ring_id ring_id __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_uint32_t ckpt_id __attribute__((aligned(8)));
	mar_refcount_set_t refcount_set[PROCESSOR_COUNT_MAX] __attribute__((aligned(8)));
};

/*
 * Implementation
 */

void clean_checkpoint_list(struct list_head *head)
{
	struct list_head *checkpoint_list;
	struct checkpoint *checkpoint;

	if (list_empty(head)) {
		log_printf (LOG_LEVEL_DEBUG, "clean_checkpoint_list: List is empty \n");
		return;
	}

	checkpoint_list = head->next;
        while (checkpoint_list != head) {
		checkpoint = list_entry (checkpoint_list,
                                struct checkpoint, list);
                assert (checkpoint > 0);

		/*
		* If checkpoint has been unlinked and this is the last reference, delete it
		*/
		 if (checkpoint->unlinked && checkpoint->reference_count == 0) {
			log_printf (LOG_LEVEL_DEBUG,"clean_checkpoint_list: deallocating checkpoint %s.\n",
                                                                                                checkpoint->name.value);
			checkpoint_list = checkpoint_list->next;
			checkpoint_release (checkpoint);
			continue;

		}
		else if (checkpoint->reference_count == 0) {
			log_printf (LOG_LEVEL_DEBUG, "clean_checkpoint_list: Starting timer to release checkpoint %s.\n",
				checkpoint->name.value);
			api->timer_delete (checkpoint->retention_timer);
			api->timer_add_duration (
				checkpoint->checkpoint_creation_attributes.retention_duration,
				checkpoint,
				timer_function_retention,
				&checkpoint->retention_timer);
		}
		checkpoint_list = checkpoint_list->next;
        }
}

static void ckpt_confchg_fn (
	enum totem_configuration_type configuration_type,
	unsigned int *member_list, int member_list_entries,
	unsigned int *left_list, int left_list_entries,
	unsigned int *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id)
{
	unsigned int i, j;

	/*
	 * Determine lowest nodeid in old regular configuration for the
	 * purpose of executing the synchronization algorithm
	 */
	if (configuration_type == TOTEM_CONFIGURATION_TRANSITIONAL) {
		for (i = 0; i < left_list_entries; i++) {
			for (j = 0; j < my_member_list_entries; j++) {
				if (left_list[i] == my_member_list[j]) {
					my_member_list[j] = 0;
				}
			}
		}	
	}
	
	my_lowest_nodeid = 0xffffffff;

	/*
	 * Handle regular configuration
	 */
	if (configuration_type == TOTEM_CONFIGURATION_REGULAR) {
		memcpy (my_member_list, member_list,
			sizeof (unsigned int) * member_list_entries);
		my_member_list_entries = member_list_entries;
		memcpy (&my_saved_ring_id, ring_id,
			sizeof (struct memb_ring_id));
		for (i = 0; i < my_member_list_entries; i++) {
			if ((my_member_list[i] != 0) &&
				(my_member_list[i] < my_lowest_nodeid)) {

				my_lowest_nodeid = my_member_list[i];
			}
		}
	}
}

static struct checkpoint *checkpoint_find (
	struct list_head *ckpt_list_head,
	mar_name_t *name,
	mar_uint32_t ckpt_id)
{
	struct list_head *list;
	struct checkpoint *checkpoint;

	for (list = ckpt_list_head->next;
		list != ckpt_list_head;
		list = list->next) {

		checkpoint = list_entry (list,
			struct checkpoint, list);

		if (mar_name_match (name, &checkpoint->name) &&
			ckpt_id == checkpoint->ckpt_id) {
			return (checkpoint);
		}
	}
	return (0);
}

static struct checkpoint *checkpoint_find_linked (
	struct list_head *ckpt_list_head,
	mar_name_t *name)
{
	struct list_head *list;
	struct checkpoint *checkpoint;

	for (list = ckpt_list_head->next;
		list != ckpt_list_head;
		list = list->next) {

		checkpoint = list_entry (list, struct checkpoint, list);

		if (mar_name_match (name, &checkpoint->name) &&
			checkpoint->unlinked == 0) {
			return (checkpoint);
		}
	}
	return (0);
}

static struct checkpoint *checkpoint_find_specific (
	struct list_head *ckpt_list_head,
	mar_name_t *name,
	mar_uint32_t ckpt_id)
{
	struct list_head *list;
	struct checkpoint *checkpoint;

	for (list = ckpt_list_head->next;
		list != ckpt_list_head;
		list = list->next) {

		checkpoint = list_entry (list, struct checkpoint, list);

		if (mar_name_match (name, &checkpoint->name) &&
			(ckpt_id == checkpoint->ckpt_id)) {
			return (checkpoint);
		}
	}
	return (0);
}

static void ckpt_checkpoint_remove_cleanup (
	void *conn,
	mar_name_t checkpoint_name,
	mar_uint32_t ckpt_id)
{
	struct list_head *list;
	struct checkpoint_cleanup *checkpoint_cleanup;
	struct ckpt_pd *ckpt_pd = (struct ckpt_pd *)api->ipc_private_data_get (conn);

	for (list = ckpt_pd->checkpoint_list.next;
		list != &ckpt_pd->checkpoint_list;
		list = list->next) {

		checkpoint_cleanup = list_entry (list, struct checkpoint_cleanup, list);
		if (mar_name_match (&checkpoint_cleanup->checkpoint_name,
			&checkpoint_name) &&
			(checkpoint_cleanup->ckpt_id == ckpt_id)) {

			list_del (&checkpoint_cleanup->list);
			free (checkpoint_cleanup);
			return;
		}
	}
}

static struct checkpoint_section *checkpoint_section_find (
	struct checkpoint *checkpoint,
	char *id,
	int id_len)
{
	struct list_head *checkpoint_section_list;
	struct checkpoint_section *checkpoint_section;

	if (id_len != 0) {
		log_printf (LOG_LEVEL_DEBUG, "Finding checkpoint section id %s %d\n", (char*)id, id_len);
	}
	else {
		log_printf (LOG_LEVEL_DEBUG, "Finding default checkpoint section\n");
	}

	for (checkpoint_section_list = checkpoint->sections_list_head.next;
		checkpoint_section_list != &checkpoint->sections_list_head;
		checkpoint_section_list = checkpoint_section_list->next) {

		checkpoint_section = list_entry (checkpoint_section_list,
			struct checkpoint_section, list);
		if (checkpoint_section->section_descriptor.section_id.id_len) {
			log_printf (LOG_LEVEL_DEBUG, "Checking section id %d %*s\n",
				checkpoint_section->section_descriptor.section_id.id_len,
				checkpoint_section->section_descriptor.section_id.id_len,
				checkpoint_section->section_descriptor.section_id.id);
		}
		else {
			log_printf (LOG_LEVEL_DEBUG, "Checking default section id\n");
		}

		/*
		  All 3 of these values being checked MUST be = 0 to return
		  The default section. If even one of them is NON zero follow
		  the normal route
		*/
		if ((id_len ||
				checkpoint_section->section_descriptor.section_id.id ||
				checkpoint_section->section_descriptor.section_id.id_len) == 0) {
			 log_printf (LOG_LEVEL_DEBUG, "Returning default section\n");
			 return (checkpoint_section);
		}

		if (checkpoint_section->section_descriptor.section_id.id_len == id_len &&
			(checkpoint_section->section_descriptor.section_id.id)&&
			(id)&&
			(memcmp (checkpoint_section->section_descriptor.section_id.id,
			id, id_len) == 0)) {

			log_printf (LOG_LEVEL_DEBUG, "Returning section %s(0x%p)\n", checkpoint_section->section_descriptor.section_id.id,
				checkpoint_section);

			return (checkpoint_section);
		}
	}
	return 0;
}

void checkpoint_section_release (struct checkpoint_section *section)
{
	log_printf (LOG_LEVEL_DEBUG, "checkpoint_section_release expiration timer = 0x%p\n", section->expiration_timer);
	list_del (&section->list);

	api->timer_delete (section->expiration_timer);
	if (section->section_descriptor.section_id.id) {
		free (section->section_descriptor.section_id.id);
	}
	if (section->section_data) {
		free (section->section_data);
	}
	free (section);
}


void checkpoint_release (struct checkpoint *checkpoint)
{
	struct list_head *list;
	struct checkpoint_section *section;

	api->timer_delete (checkpoint->retention_timer);

	/*
	 * Release all checkpoint sections for this checkpoint
	 */
	for (list = checkpoint->sections_list_head.next;
		list != &checkpoint->sections_list_head;) {

		section = list_entry (list,
			struct checkpoint_section, list);

		list = list->next;
		checkpoint->section_count -= 1;
		api->timer_delete (section->expiration_timer);
		checkpoint_section_release (section);
	}
	list_del (&checkpoint->list);
	free (checkpoint);
}

int ckpt_checkpoint_close (
	mar_name_t *checkpoint_name,
	mar_uint32_t ckpt_id)
{
	struct req_exec_ckpt_checkpointclose req_exec_ckpt_checkpointclose;
	struct iovec iovec;

	req_exec_ckpt_checkpointclose.header.size =
		sizeof (struct req_exec_ckpt_checkpointclose);
	req_exec_ckpt_checkpointclose.header.id =
		SERVICE_ID_MAKE (CKPT_SERVICE,
			MESSAGE_REQ_EXEC_CKPT_CHECKPOINTCLOSE);

	memcpy (&req_exec_ckpt_checkpointclose.checkpoint_name,
		checkpoint_name, sizeof (mar_name_t));
	req_exec_ckpt_checkpointclose.ckpt_id = ckpt_id;
	memset (&req_exec_ckpt_checkpointclose.source, 0,
		sizeof (mar_message_source_t));

	iovec.iov_base = (char *)&req_exec_ckpt_checkpointclose;
	iovec.iov_len = sizeof (req_exec_ckpt_checkpointclose);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);

	return (-1);
}

static int ckpt_exec_init_fn (struct corosync_api_v1 *corosync_api)
{
	api = corosync_api;

	return (0);
}


/*
 * Endian conversion routines for executive message handlers
 */
static void exec_ckpt_checkpointopen_endian_convert (void *msg)
{
	struct req_exec_ckpt_checkpointopen *req_exec_ckpt_checkpointopen = (struct req_exec_ckpt_checkpointopen *)msg;

	swab_mar_req_header_t (&req_exec_ckpt_checkpointopen->header);
	swab_mar_message_source_t (&req_exec_ckpt_checkpointopen->source);
	swab_mar_name_t (&req_exec_ckpt_checkpointopen->checkpoint_name);
	swab_mar_uint32_t (&req_exec_ckpt_checkpointopen->ckpt_id);
	swab_mar_ckpt_checkpoint_creation_attributes_t (&req_exec_ckpt_checkpointopen->checkpoint_creation_attributes);
	swab_mar_uint32_t (&req_exec_ckpt_checkpointopen->checkpoint_creation_attributes_set);
	swab_mar_ckpt_checkpoint_open_flags_t (&req_exec_ckpt_checkpointopen->checkpoint_open_flags);
	swab_mar_ckpt_checkpoint_handle_t (&req_exec_ckpt_checkpointopen->checkpoint_handle);
	swab_mar_invocation_t (&req_exec_ckpt_checkpointopen->invocation);
	swab_mar_uint32_t (&req_exec_ckpt_checkpointopen->async_call);
	swab_mar_uint32_t (&req_exec_ckpt_checkpointopen->fail_with_error);
}

static void exec_ckpt_checkpointclose_endian_convert (void *msg)
{
	struct req_exec_ckpt_checkpointclose *req_exec_ckpt_checkpointclose = (struct req_exec_ckpt_checkpointclose *)msg;

	swab_mar_req_header_t (&req_exec_ckpt_checkpointclose->header);
	swab_mar_message_source_t (&req_exec_ckpt_checkpointclose->source);
	swab_mar_name_t (&req_exec_ckpt_checkpointclose->checkpoint_name);
	swab_mar_uint32_t (&req_exec_ckpt_checkpointclose->ckpt_id);
}
static void exec_ckpt_checkpointunlink_endian_convert (void *msg)
{
	struct req_exec_ckpt_checkpointunlink *req_exec_ckpt_checkpointunlink = (struct req_exec_ckpt_checkpointunlink *)msg;

	swab_mar_req_header_t (&req_exec_ckpt_checkpointunlink->header);
	swab_mar_message_source_t (&req_exec_ckpt_checkpointunlink->source);
	swab_mar_name_t (&req_exec_ckpt_checkpointunlink->checkpoint_name);
}

static void exec_ckpt_checkpointretentiondurationset_endian_convert (void *msg)
{
	struct req_exec_ckpt_checkpointretentiondurationset *req_exec_ckpt_checkpointretentiondurationset = (struct req_exec_ckpt_checkpointretentiondurationset *)msg;

	swab_mar_req_header_t (&req_exec_ckpt_checkpointretentiondurationset->header);
	swab_mar_message_source_t (&req_exec_ckpt_checkpointretentiondurationset->source);
	swab_mar_name_t (&req_exec_ckpt_checkpointretentiondurationset->checkpoint_name);
	swab_mar_uint32_t (&req_exec_ckpt_checkpointretentiondurationset->ckpt_id);
	swab_mar_time_t (&req_exec_ckpt_checkpointretentiondurationset->retention_duration);
}

static void exec_ckpt_checkpointretentiondurationexpire_endian_convert (void *msg)
{
	struct req_exec_ckpt_checkpointretentiondurationexpire *req_exec_ckpt_checkpointretentiondurationexpire = (struct req_exec_ckpt_checkpointretentiondurationexpire *)msg;

	swab_mar_req_header_t (&req_exec_ckpt_checkpointretentiondurationexpire->header);
	swab_mar_name_t (&req_exec_ckpt_checkpointretentiondurationexpire->checkpoint_name);
	swab_mar_uint32_t (&req_exec_ckpt_checkpointretentiondurationexpire->ckpt_id);
}

static void exec_ckpt_sectioncreate_endian_convert (void *msg)
{
	struct req_exec_ckpt_sectioncreate *req_exec_ckpt_sectioncreate = (struct req_exec_ckpt_sectioncreate *)msg;

	swab_mar_req_header_t (&req_exec_ckpt_sectioncreate->header);
	swab_mar_message_source_t (&req_exec_ckpt_sectioncreate->source);
	swab_mar_name_t (&req_exec_ckpt_sectioncreate->checkpoint_name);
	swab_mar_uint32_t (&req_exec_ckpt_sectioncreate->ckpt_id);
	swab_mar_uint32_t (&req_exec_ckpt_sectioncreate->id_len);
	swab_mar_time_t (&req_exec_ckpt_sectioncreate->expiration_time);
	swab_mar_uint32_t (&req_exec_ckpt_sectioncreate->initial_data_size);
}

static void exec_ckpt_sectiondelete_endian_convert (void *msg)
{
	struct req_exec_ckpt_sectiondelete *req_exec_ckpt_sectiondelete = (struct req_exec_ckpt_sectiondelete *)msg;

	swab_mar_req_header_t (&req_exec_ckpt_sectiondelete->header);
	swab_mar_message_source_t (&req_exec_ckpt_sectiondelete->source);
	swab_mar_name_t (&req_exec_ckpt_sectiondelete->checkpoint_name);
	swab_mar_uint32_t (&req_exec_ckpt_sectiondelete->ckpt_id);
	swab_mar_uint32_t (&req_exec_ckpt_sectiondelete->id_len);
}

static void exec_ckpt_sectrionexpirationtimeset_endian_convert (void *msg)
{
	struct req_exec_ckpt_sectionexpirationtimeset *req_exec_ckpt_sectionexpirationtimeset = (struct req_exec_ckpt_sectionexpirationtimeset *)msg;

	swab_mar_req_header_t (&req_exec_ckpt_sectionexpirationtimeset->header);
	swab_mar_message_source_t (&req_exec_ckpt_sectionexpirationtimeset->source);
	swab_mar_name_t (&req_exec_ckpt_sectionexpirationtimeset->checkpoint_name);
	swab_mar_uint32_t (&req_exec_ckpt_sectionexpirationtimeset->ckpt_id);
	swab_mar_uint32_t (&req_exec_ckpt_sectionexpirationtimeset->id_len);
	swab_mar_time_t (&req_exec_ckpt_sectionexpirationtimeset->expiration_time);
}

static void exec_ckpt_sectionwrite_endian_convert (void *msg)
{
	struct req_exec_ckpt_sectionwrite *req_exec_ckpt_sectionwrite = (struct req_exec_ckpt_sectionwrite *)msg;

	swab_mar_req_header_t (&req_exec_ckpt_sectionwrite->header);
	swab_mar_message_source_t (&req_exec_ckpt_sectionwrite->source);
	swab_mar_name_t (&req_exec_ckpt_sectionwrite->checkpoint_name);
	swab_mar_uint32_t (&req_exec_ckpt_sectionwrite->ckpt_id);
	swab_mar_uint32_t (&req_exec_ckpt_sectionwrite->id_len);
	swab_mar_offset_t (&req_exec_ckpt_sectionwrite->data_size);
}

static void exec_ckpt_sectionoverwrite_endian_convert (void *msg)
{
	struct req_exec_ckpt_sectionoverwrite *req_exec_ckpt_sectionoverwrite = (struct req_exec_ckpt_sectionoverwrite *)msg;

	swab_mar_req_header_t (&req_exec_ckpt_sectionoverwrite->header);
	swab_mar_message_source_t (&req_exec_ckpt_sectionoverwrite->source);
	swab_mar_name_t (&req_exec_ckpt_sectionoverwrite->checkpoint_name);
	swab_mar_uint32_t (&req_exec_ckpt_sectionoverwrite->ckpt_id);
	swab_mar_uint32_t (&req_exec_ckpt_sectionoverwrite->id_len);
	swab_mar_offset_t (&req_exec_ckpt_sectionoverwrite->data_size);
}

static void exec_ckpt_sectionread_endian_convert (void *msg)
{
	struct req_exec_ckpt_sectionread *req_exec_ckpt_sectionread = (struct req_exec_ckpt_sectionread *)msg;

	swab_mar_req_header_t (&req_exec_ckpt_sectionread->header);
	swab_mar_message_source_t (&req_exec_ckpt_sectionread->source);
	swab_mar_name_t (&req_exec_ckpt_sectionread->checkpoint_name);
	swab_mar_uint32_t (&req_exec_ckpt_sectionread->ckpt_id);
	swab_mar_uint32_t (&req_exec_ckpt_sectionread->id_len);
	swab_mar_offset_t (&req_exec_ckpt_sectionread->data_offset);
	swab_mar_offset_t (&req_exec_ckpt_sectionread->data_size);
}

static void exec_ckpt_sync_checkpoint_endian_convert (void *msg)
{
}
static void exec_ckpt_sync_checkpoint_section_endian_convert (void *msg)
{
}
static void exec_ckpt_sync_checkpoint_refcount_endian_convert (void *msg)
{
}

#ifdef ABC
static void exec_ckpt_sync_state_endian_convert (void *msg)
{
	struct req_exec_ckpt_sync_state *req_exec_ckpt_sync_state = (struct req_exec_ckpt_sync_state *)msg;
	unsigned int i;

	swab_mar_req_header_t (&req_exec_ckpt_sync_state->header);
//	swab_mar_memb_ring_id_t (&req_exec_ckpt_sync_state->memb_ring_id);
	swab_mar_name_t (&req_exec_ckpt_sync_state->checkpoint_name);
	swab_mar_uint32_t (&req_exec_ckpt_sync_state->ckpt_id);
	swab_mar_ckpt_checkpoint_creation_attributes_t (&req_exec_ckpt_sync_state->checkpoint_creation_attributes);
//	swab_mar_ckpt_section_descriptor_t (&req_exec_ckpt_sync_state->section_descriptor);
	swab_mar_uint32_t (&req_exec_ckpt_sync_state->nodeid);
	for (i = 0; i < PROCESSOR_COUNT_MAX; i++) {
		swab_mar_refcount_set_t (&req_exec_ckpt_sync_state->refcount_set[i]);
	}
}

static void exec_ckpt_sync_section_endian_convert (void *msg)
{
	struct req_exec_ckpt_sync_section *req_exec_ckpt_sync_section = (struct req_exec_ckpt_sync_section *)msg;
	swab_mar_req_header_t (&req_exec_ckpt_sync_section->header);
//	swab_mar_memb_ring_id_t (&req_exec_ckpt_sync_section->memb_ring_id);
	swab_mar_name_t (&req_exec_ckpt_sync_section->checkpoint_name);
	swab_mar_uint32_t (&req_exec_ckpt_sync_section->ckpt_id);
	swab_mar_uint32_t (&req_exec_ckpt_sync_section->id_len);
	swab_mar_offset_t (&req_exec_ckpt_sync_section->data_offset);
	swab_mar_offset_t (&req_exec_ckpt_sync_section->data_size);
}
#endif

/*
 * Executive message handlers
 */
static void message_handler_req_exec_ckpt_checkpointopen (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_ckpt_checkpointopen *req_exec_ckpt_checkpointopen = (struct req_exec_ckpt_checkpointopen *)message;
	struct res_lib_ckpt_checkpointopen res_lib_ckpt_checkpointopen;
	struct res_lib_ckpt_checkpointopenasync res_lib_ckpt_checkpointopenasync;

	struct checkpoint *checkpoint = 0;
	struct checkpoint_section *checkpoint_section = 0;
	struct checkpoint_cleanup *checkpoint_cleanup = 0;
	struct ckpt_pd *ckpt_pd;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_DEBUG, "Executive request to open checkpoint %p\n", req_exec_ckpt_checkpointopen);

	if (req_exec_ckpt_checkpointopen->fail_with_error != SA_AIS_OK) {
		error = req_exec_ckpt_checkpointopen->fail_with_error;
		goto error_exit;
	}

	if (api->ipc_source_is_local(&req_exec_ckpt_checkpointopen->source)) {
		checkpoint_cleanup = malloc (sizeof (struct checkpoint_cleanup));
		if (checkpoint_cleanup == 0) {
			error = SA_AIS_ERR_NO_MEMORY;
			goto error_exit;
		}
	}

	checkpoint = checkpoint_find_linked (
		&checkpoint_list_head,
		&req_exec_ckpt_checkpointopen->checkpoint_name);

	/*
	 * If checkpoint doesn't exist, create one
	 */
	if (checkpoint == 0) {
		if ((req_exec_ckpt_checkpointopen->checkpoint_open_flags & SA_CKPT_CHECKPOINT_CREATE) == 0) {
			error = SA_AIS_ERR_NOT_EXIST;
			goto error_exit;
		}
		checkpoint = malloc (sizeof (struct checkpoint));
		if (checkpoint == 0) {
			error = SA_AIS_ERR_NO_MEMORY;
			goto error_exit;
		}

		memcpy (&checkpoint->name,
			&req_exec_ckpt_checkpointopen->checkpoint_name,
			sizeof (mar_name_t));
		memcpy (&checkpoint->checkpoint_creation_attributes,
			&req_exec_ckpt_checkpointopen->checkpoint_creation_attributes,
			sizeof (mar_ckpt_checkpoint_creation_attributes_t));
		checkpoint->unlinked = 0;
		list_init (&checkpoint->list);
		list_init (&checkpoint->sections_list_head);
		list_add (&checkpoint->list, &checkpoint_list_head);
		checkpoint->reference_count = 1;
		checkpoint->retention_timer = 0;
		checkpoint->section_count = 0;
		checkpoint->ckpt_id = global_ckpt_id++;

		if ((checkpoint->checkpoint_creation_attributes.creation_flags & (SA_CKPT_WR_ACTIVE_REPLICA | SA_CKPT_WR_ACTIVE_REPLICA_WEAK)) &&
			(checkpoint->checkpoint_creation_attributes.creation_flags & SA_CKPT_CHECKPOINT_COLLOCATED) == 0) {
			checkpoint->active_replica_set = 1;
		} else
		if ((checkpoint->checkpoint_creation_attributes.creation_flags & SA_CKPT_WR_ALL_REPLICAS) == 1) {
			checkpoint->active_replica_set = 1;
		} else {
			checkpoint->active_replica_set = 0;
		}

		memset (&checkpoint->refcount_set, 0,
			sizeof (struct refcount_set) * PROCESSOR_COUNT_MAX);

		/*
		 * Create default section id if max_sections is 1
		 */
		if (checkpoint->checkpoint_creation_attributes.max_sections == 1) {
			/*
			 * Add in default checkpoint section
			 */
			checkpoint_section = malloc (sizeof (struct checkpoint_section));
			if (checkpoint_section == 0) {
				free (checkpoint);
				error = SA_AIS_ERR_NO_MEMORY;
				goto error_exit;
			}

			list_init (&checkpoint_section->list);
			list_add (&checkpoint_section->list, &checkpoint->sections_list_head);


			checkpoint_section->section_descriptor.section_id.id = 0;
			checkpoint_section->section_descriptor.section_id.id_len = 0;
			checkpoint_section->section_descriptor.expiration_time = SA_TIME_END;
			checkpoint_section->section_descriptor.section_state = SA_CKPT_SECTION_VALID;
			checkpoint_section->section_descriptor.last_update = 0; /*current time*/
			checkpoint_section->section_descriptor.section_size = 0;
			checkpoint_section->section_data = NULL;
			checkpoint_section->expiration_timer = 0;
		}
	} else {
		if (req_exec_ckpt_checkpointopen->checkpoint_creation_attributes_set &&
			memcmp (&checkpoint->checkpoint_creation_attributes,
				&req_exec_ckpt_checkpointopen->checkpoint_creation_attributes,
				sizeof (mar_ckpt_checkpoint_creation_attributes_t)) != 0) {

			error = SA_AIS_ERR_EXIST;
			goto error_exit;
		}
	}

	assert (checkpoint->unlinked == 0);

	/*
	 * Setup connection information and mark checkpoint as referenced
	 */
	log_printf (LOG_LEVEL_DEBUG, "CHECKPOINT opened is %p\n", checkpoint);

	sync_refcount_increment (checkpoint, nodeid);
	sync_refcount_calculate (checkpoint);

	/*
	 * Reset retention duration since this checkpoint was just opened
	 */
	api->timer_delete (checkpoint->retention_timer);
	checkpoint->retention_timer = 0;

	/*
	 * Send error result to CKPT library
	 */
error_exit:
	/*
	 * If this node was the source of the message, respond to this node
	 */
	if (api->ipc_source_is_local(&req_exec_ckpt_checkpointopen->source)) {
		/*
		 * If its an async call respond with the invocation and handle
		 */
		if (req_exec_ckpt_checkpointopen->async_call) {
			res_lib_ckpt_checkpointopenasync.header.size = sizeof (struct res_lib_ckpt_checkpointopenasync);
			res_lib_ckpt_checkpointopenasync.header.id = MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTOPENASYNC;
			res_lib_ckpt_checkpointopenasync.header.error = error;
			res_lib_ckpt_checkpointopenasync.checkpoint_handle = req_exec_ckpt_checkpointopen->checkpoint_handle;
			res_lib_ckpt_checkpointopenasync.invocation = req_exec_ckpt_checkpointopen->invocation;
			if (error == SA_AIS_OK) {
				res_lib_ckpt_checkpointopenasync.ckpt_id = checkpoint->ckpt_id;
			}

			api->ipc_conn_send_response (
				req_exec_ckpt_checkpointopen->source.conn,
				&res_lib_ckpt_checkpointopenasync,
				sizeof (struct res_lib_ckpt_checkpointopenasync));
			api->ipc_conn_send_response (
				api->ipc_conn_partner_get (req_exec_ckpt_checkpointopen->source.conn),
				&res_lib_ckpt_checkpointopenasync,
				sizeof (struct res_lib_ckpt_checkpointopenasync));
		} else {
			/*
			 * otherwise respond with the normal checkpointopen response
			 */
			res_lib_ckpt_checkpointopen.header.size = sizeof (struct res_lib_ckpt_checkpointopen);
			res_lib_ckpt_checkpointopen.header.id = MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTOPEN;
			if (error == SA_AIS_OK) {
				res_lib_ckpt_checkpointopen.ckpt_id = checkpoint->ckpt_id;
			}
			res_lib_ckpt_checkpointopen.header.error = error;

			api->ipc_conn_send_response (
				req_exec_ckpt_checkpointopen->source.conn,
				&res_lib_ckpt_checkpointopen,
				sizeof (struct res_lib_ckpt_checkpointopen));
		}

		/*
		 * This is the path taken when all goes well and this call was local
		 */
		if (error == SA_AIS_OK) {
			ckpt_pd = api->ipc_private_data_get (req_exec_ckpt_checkpointopen->source.conn);

			memcpy(&checkpoint_cleanup->checkpoint_name,
				&checkpoint->name, sizeof (mar_name_t));
			checkpoint_cleanup->ckpt_id = checkpoint->ckpt_id;

			list_add (&checkpoint_cleanup->list,
				&ckpt_pd->checkpoint_list);
		} else {
				/*
				 * We allocated this in the hope of using it but an error occured
				 * so deallocate it.
				 */
				free (checkpoint_cleanup);
		}
	}
}

unsigned int abstime_to_msec (mar_time_t time)
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
	struct checkpoint *checkpoint = 0;
	struct checkpoint_section *checkpoint_section = 0;
	struct ckpt_identifier *ckpt_id = 0;

	ckpt_id = (struct ckpt_identifier *)data;
	log_printf (LOG_LEVEL_DEBUG, "timer_function_section_expire data = 0x%p\n",data);
	if (ckpt_id->ckpt_section_id.id_len && ckpt_id->ckpt_section_id.id) {
		log_printf (LOG_LEVEL_DEBUG, "Attempting to expire section %s in ckpt %s\n",
			ckpt_id->ckpt_section_id.id,
			ckpt_id->ckpt_name.value);
	}
	else {
		log_printf (LOG_LEVEL_ERROR, "timer_function_section_expire data incorect\n");
		goto free_mem;
	}

	checkpoint = checkpoint_find (
		&checkpoint_list_head,
		&ckpt_id->ckpt_name,
		ckpt_id->ckpt_id);
        if (checkpoint == 0) {
		log_printf (LOG_LEVEL_ERROR, "timer_function_section_expire could not find ckpt %s\n",
                        ckpt_id->ckpt_name.value);
		goto free_mem;
        }

        checkpoint_section = checkpoint_section_find (checkpoint,
		(char *)ckpt_id->ckpt_section_id.id,
		(int)ckpt_id->ckpt_section_id.id_len);
        if (checkpoint_section == 0) {
		log_printf (LOG_LEVEL_ERROR, "timer_function_section_expire could not find section %s in ckpt %s\n",
                        ckpt_id->ckpt_section_id.id,
                        ckpt_id->ckpt_name.value);
		goto free_mem;
        }

	log_printf (LOG_LEVEL_DEBUG, "Expiring section %s in ckpt %s\n",
                        ckpt_id->ckpt_section_id.id,
                        ckpt_id->ckpt_name.value);

	checkpoint->section_count -= 1;
	checkpoint_section_release (checkpoint_section);

free_mem :
	free (ckpt_id);

}

void timer_function_retention (void *data)
{
	struct checkpoint *checkpoint = (struct checkpoint *)data;
	struct req_exec_ckpt_checkpointretentiondurationexpire req_exec_ckpt_checkpointretentiondurationexpire;
	struct iovec iovec;

	checkpoint->retention_timer = 0;
	req_exec_ckpt_checkpointretentiondurationexpire.header.size =
		sizeof (struct req_exec_ckpt_checkpointretentiondurationexpire);
	req_exec_ckpt_checkpointretentiondurationexpire.header.id =
		SERVICE_ID_MAKE (CKPT_SERVICE,
			MESSAGE_REQ_EXEC_CKPT_CHECKPOINTRETENTIONDURATIONEXPIRE);

	memcpy (&req_exec_ckpt_checkpointretentiondurationexpire.checkpoint_name,
		&checkpoint->name,
		sizeof (mar_name_t));
	req_exec_ckpt_checkpointretentiondurationexpire.ckpt_id =
		checkpoint->ckpt_id;

	iovec.iov_base = (char *)&req_exec_ckpt_checkpointretentiondurationexpire;
	iovec.iov_len = sizeof (req_exec_ckpt_checkpointretentiondurationexpire);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_exec_ckpt_checkpointclose (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_ckpt_checkpointclose *req_exec_ckpt_checkpointclose = (struct req_exec_ckpt_checkpointclose *)message;
	struct res_lib_ckpt_checkpointclose res_lib_ckpt_checkpointclose;
	struct checkpoint *checkpoint = 0;
	SaAisErrorT error = SA_AIS_OK;
	int release_checkpoint = 0;

	log_printf (LOG_LEVEL_DEBUG, "Got EXEC request to close checkpoint %s\n",
		get_mar_name_t (&req_exec_ckpt_checkpointclose->checkpoint_name));

	checkpoint = checkpoint_find (
		&checkpoint_list_head,
		&req_exec_ckpt_checkpointclose->checkpoint_name,
		req_exec_ckpt_checkpointclose->ckpt_id);
	if (checkpoint == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	sync_refcount_decrement (checkpoint, nodeid);
	sync_refcount_calculate (checkpoint);

	log_printf (LOG_LEVEL_DEBUG, "Close checkpoint->reference_count %d\n",
		checkpoint->reference_count);
	assert (checkpoint->reference_count >= 0);

	/*
	 * If checkpoint has been unlinked and this is the last reference, delete it
	 */
	if (checkpoint->unlinked && checkpoint->reference_count == 0) {
		log_printf (LOG_LEVEL_DEBUG, "Unlinking checkpoint.\n");
		release_checkpoint = 1;
	} else
	if (checkpoint->reference_count == 0) {
		if (checkpoint->checkpoint_creation_attributes.retention_duration != SA_TIME_END) {
			api->timer_add_duration (
				checkpoint->checkpoint_creation_attributes.retention_duration,
				checkpoint,
				timer_function_retention,
				&checkpoint->retention_timer);
		}
	}

error_exit:
	/*
	 * Remove the checkpoint from my connections checkpoint list
	 */
	if (api->ipc_source_is_local(&req_exec_ckpt_checkpointclose->source)) {

		res_lib_ckpt_checkpointclose.header.size = sizeof (struct res_lib_ckpt_checkpointclose);
		res_lib_ckpt_checkpointclose.header.id = MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTCLOSE;
		res_lib_ckpt_checkpointclose.header.error = error;
		api->ipc_conn_send_response (req_exec_ckpt_checkpointclose->source.conn,
			&res_lib_ckpt_checkpointclose, sizeof (struct res_lib_ckpt_checkpointclose));
	}

	/*
	 * Release the checkpoint if instructed to do so.
	 */
	if (error == SA_AIS_OK && release_checkpoint) {
		checkpoint_release(checkpoint);
	}
}

static void message_handler_req_exec_ckpt_checkpointunlink (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_ckpt_checkpointunlink *req_exec_ckpt_checkpointunlink = (struct req_exec_ckpt_checkpointunlink *)message;

	struct res_lib_ckpt_checkpointunlink res_lib_ckpt_checkpointunlink;
	struct checkpoint *checkpoint = 0;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_DEBUG, "Got EXEC request to unlink checkpoint %p\n", req_exec_ckpt_checkpointunlink);
	checkpoint = checkpoint_find_linked (
		&checkpoint_list_head,
		&req_exec_ckpt_checkpointunlink->checkpoint_name);
	if (checkpoint == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	assert (checkpoint->unlinked == 0);

	checkpoint->unlinked = 1;
	/*
	 * Immediately delete entry if reference count is zero
	 */
	if (checkpoint->reference_count == 0) {
		/*
		 * Remove retention timer since this checkpoint was unlinked and is no
		 * longer referenced
		 */
		checkpoint_release (checkpoint);
	}

error_exit:
	/*
	 * If this node was the source of the message, respond to this node
	 */
	if (api->ipc_source_is_local(&req_exec_ckpt_checkpointunlink->source)) {
		res_lib_ckpt_checkpointunlink.header.size = sizeof (struct res_lib_ckpt_checkpointunlink);
		res_lib_ckpt_checkpointunlink.header.id = MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTUNLINK;
		res_lib_ckpt_checkpointunlink.header.error = error;
		api->ipc_conn_send_response (
			req_exec_ckpt_checkpointunlink->source.conn,
			&res_lib_ckpt_checkpointunlink,
			sizeof (struct res_lib_ckpt_checkpointunlink));
	}
}

static void message_handler_req_exec_ckpt_checkpointretentiondurationset (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_ckpt_checkpointretentiondurationset *req_exec_ckpt_checkpointretentiondurationset = (struct req_exec_ckpt_checkpointretentiondurationset *)message;
	struct res_lib_ckpt_checkpointretentiondurationset res_lib_ckpt_checkpointretentiondurationset;
	struct checkpoint *checkpoint;
	SaAisErrorT error = SA_AIS_ERR_BAD_OPERATION;

	checkpoint = checkpoint_find (
		&checkpoint_list_head,
		&req_exec_ckpt_checkpointretentiondurationset->checkpoint_name,
		req_exec_ckpt_checkpointretentiondurationset->ckpt_id);
	if (checkpoint) {
		log_printf (LOG_LEVEL_DEBUG, "Setting retention duration for checkpoint %s\n",
			get_mar_name_t (&req_exec_ckpt_checkpointretentiondurationset->checkpoint_name));
		if (checkpoint->unlinked == 0) {
			checkpoint->checkpoint_creation_attributes.retention_duration =
				req_exec_ckpt_checkpointretentiondurationset->retention_duration;

			if (checkpoint->reference_count == 0) {
				api->timer_delete (checkpoint->retention_timer);

				api->timer_add_duration (
					checkpoint->checkpoint_creation_attributes.retention_duration,
					checkpoint,
					timer_function_retention,
					&checkpoint->retention_timer);
			}
			error = SA_AIS_OK;
		}
	}

	/*
	 * Respond to library if this processor sent the duration set request
	 */
	if (api->ipc_source_is_local(&req_exec_ckpt_checkpointretentiondurationset->source)) {
		res_lib_ckpt_checkpointretentiondurationset.header.size = sizeof (struct res_lib_ckpt_checkpointretentiondurationset);
		res_lib_ckpt_checkpointretentiondurationset.header.id = MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTRETENTIONDURATIONSET;
		res_lib_ckpt_checkpointretentiondurationset.header.error = error;

		api->ipc_conn_send_response (
			req_exec_ckpt_checkpointretentiondurationset->source.conn,
			&res_lib_ckpt_checkpointretentiondurationset,
			sizeof (struct res_lib_ckpt_checkpointretentiondurationset));
	}
}

static void message_handler_req_exec_ckpt_checkpointretentiondurationexpire (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_ckpt_checkpointretentiondurationexpire *req_exec_ckpt_checkpointretentiondurationexpire = (struct req_exec_ckpt_checkpointretentiondurationexpire *)message;
	struct req_exec_ckpt_checkpointunlink req_exec_ckpt_checkpointunlink;
	struct checkpoint *checkpoint;
	struct iovec iovec;

	checkpoint = checkpoint_find (
		&checkpoint_list_head,
		&req_exec_ckpt_checkpointretentiondurationexpire->checkpoint_name,
		req_exec_ckpt_checkpointretentiondurationexpire->ckpt_id);
		log_printf (LOG_LEVEL_NOTICE, "Expiring checkpoint %s\n",
			get_mar_name_t (&req_exec_ckpt_checkpointretentiondurationexpire->checkpoint_name));
	if (checkpoint && (checkpoint->reference_count == 0)) {
		log_printf (LOG_LEVEL_NOTICE, "Expiring checkpoint %s\n",
			get_mar_name_t (&req_exec_ckpt_checkpointretentiondurationexpire->checkpoint_name));

		req_exec_ckpt_checkpointunlink.header.size =
			sizeof (struct req_exec_ckpt_checkpointunlink);
		req_exec_ckpt_checkpointunlink.header.id =
			SERVICE_ID_MAKE (CKPT_SERVICE,
				MESSAGE_REQ_EXEC_CKPT_CHECKPOINTUNLINK);

		req_exec_ckpt_checkpointunlink.source.conn = 0;
		req_exec_ckpt_checkpointunlink.source.nodeid = 0;

		memcpy (&req_exec_ckpt_checkpointunlink.checkpoint_name,
			&req_exec_ckpt_checkpointretentiondurationexpire->checkpoint_name,
			sizeof (mar_name_t));

		iovec.iov_base = (char *)&req_exec_ckpt_checkpointunlink;
		iovec.iov_len = sizeof (req_exec_ckpt_checkpointunlink);

		assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
	}
}


static void message_handler_req_exec_ckpt_sectioncreate (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_ckpt_sectioncreate *req_exec_ckpt_sectioncreate = (struct req_exec_ckpt_sectioncreate *)message;
	struct res_lib_ckpt_sectioncreate res_lib_ckpt_sectioncreate;
	struct checkpoint *checkpoint;
	struct checkpoint_section *checkpoint_section;
	void *initial_data;
	void *section_id;
	struct ckpt_identifier *ckpt_id = 0;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_DEBUG, "Executive request to create a checkpoint section.\n");
	checkpoint = checkpoint_find (
		&checkpoint_list_head,
		&req_exec_ckpt_sectioncreate->checkpoint_name,
		req_exec_ckpt_sectioncreate->ckpt_id);
	if (checkpoint == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	if (checkpoint->section_count == checkpoint->checkpoint_creation_attributes.max_sections) {
		error = SA_AIS_ERR_NO_SPACE;
		goto error_exit;
	}

	if (checkpoint->checkpoint_creation_attributes.max_sections == 1) {
		error = SA_AIS_ERR_EXIST;
		goto error_exit;
	}

	if (checkpoint->checkpoint_creation_attributes.max_section_size <
		req_exec_ckpt_sectioncreate->initial_data_size) {

		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}
	
	if (checkpoint->checkpoint_creation_attributes.max_section_id_size <
		req_exec_ckpt_sectioncreate->id_len) {
		
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}	

	/*
	 * Determine if user-specified checkpoint section already exists
	 */
	checkpoint_section = checkpoint_section_find (checkpoint,
		((char *)req_exec_ckpt_sectioncreate) +
			sizeof (struct req_exec_ckpt_sectioncreate),
		req_exec_ckpt_sectioncreate->id_len);
	if (checkpoint_section) {
		error = SA_AIS_ERR_EXIST;
		goto error_exit;
	}

	/*
	 * Allocate checkpoint section
	 */
	checkpoint_section = malloc (sizeof (struct checkpoint_section));
	if (checkpoint_section == 0) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_exit;
	}
	/*
	 * Allocate checkpoint section data
	 */
	initial_data = malloc (req_exec_ckpt_sectioncreate->initial_data_size);
	if (initial_data == 0) {
		free (checkpoint_section);
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_exit;
	}
	/*
	 * Allocate checkpoint section id
	 */
	section_id = malloc (req_exec_ckpt_sectioncreate->id_len + 1);
	if (section_id == 0) {
		free (checkpoint_section);
		free (initial_data);
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_exit;
	}

	/*
	 * Copy checkpoint section and section ID
	 */
	memcpy (section_id,
		((char *)req_exec_ckpt_sectioncreate) +
			sizeof (struct req_exec_ckpt_sectioncreate),
		req_exec_ckpt_sectioncreate->id_len);

	/*Must be null terminated if it already isn't*/
	((char*)(section_id))[req_exec_ckpt_sectioncreate->id_len] = '\0';

	memcpy (initial_data,
		((char *)req_exec_ckpt_sectioncreate) +
			sizeof (struct req_exec_ckpt_sectioncreate) +
			req_exec_ckpt_sectioncreate->id_len,
		req_exec_ckpt_sectioncreate->initial_data_size);

	/*
	 * Configure checkpoint section
	 */
	checkpoint_section->section_descriptor.section_id.id = section_id;
	checkpoint_section->section_descriptor.section_id.id_len =
		req_exec_ckpt_sectioncreate->id_len;
	checkpoint_section->section_descriptor.section_size =
		req_exec_ckpt_sectioncreate->initial_data_size;
	checkpoint_section->section_descriptor.expiration_time =
		req_exec_ckpt_sectioncreate->expiration_time;
	checkpoint_section->section_descriptor.section_state =
		SA_CKPT_SECTION_VALID;
	checkpoint_section->section_descriptor.last_update = 0; /* TODO current time */
	checkpoint_section->section_data = initial_data;
	checkpoint_section->expiration_timer = 0;

	if (req_exec_ckpt_sectioncreate->expiration_time != SA_TIME_END) {
		ckpt_id = malloc (sizeof(struct ckpt_identifier));
		assert(ckpt_id);
		memcpy(&ckpt_id->ckpt_name,
			&req_exec_ckpt_sectioncreate->checkpoint_name,
			sizeof(mar_name_t));
		ckpt_id->ckpt_id = req_exec_ckpt_sectioncreate->ckpt_id;
		memcpy(&ckpt_id->ckpt_section_id,
			&checkpoint_section->section_descriptor.section_id,
			sizeof(mar_ckpt_section_id_t));
		log_printf (LOG_LEVEL_DEBUG, "req_exec_ckpt_sectioncreate Enqueuing Timer to Expire section %s in ckpt %s\n",
			ckpt_id->ckpt_section_id.id,
			ckpt_id->ckpt_name.value);
		api->timer_add_absolute (
			checkpoint_section->section_descriptor.expiration_time,
			ckpt_id,
			timer_function_section_expire,
			&checkpoint_section->expiration_timer);
		log_printf (LOG_LEVEL_DEBUG,
			"req_exec_ckpt_sectionicreate expiration timer = 0x%p\n",
			checkpoint_section->expiration_timer);
	}

	log_printf (LOG_LEVEL_DEBUG,
		"message_handler_req_exec_ckpt_sectioncreate created section with id = %s, id_len = %d\n",
		checkpoint_section->section_descriptor.section_id.id,
		checkpoint_section->section_descriptor.section_id.id_len);
	/*
	 * Add checkpoint section to checkpoint
	 */
	list_init (&checkpoint_section->list);
	list_add (&checkpoint_section->list,
		&checkpoint->sections_list_head);
	checkpoint->section_count += 1;

error_exit:
	if (api->ipc_source_is_local(&req_exec_ckpt_sectioncreate->source)) {
		res_lib_ckpt_sectioncreate.header.size = sizeof (struct res_lib_ckpt_sectioncreate);
		res_lib_ckpt_sectioncreate.header.id = MESSAGE_RES_CKPT_CHECKPOINT_SECTIONCREATE;
		res_lib_ckpt_sectioncreate.header.error = error;

		api->ipc_conn_send_response (req_exec_ckpt_sectioncreate->source.conn,
			&res_lib_ckpt_sectioncreate,
			sizeof (struct res_lib_ckpt_sectioncreate));
	}
}

static void message_handler_req_exec_ckpt_sectiondelete (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_ckpt_sectiondelete *req_exec_ckpt_sectiondelete = (struct req_exec_ckpt_sectiondelete *)message;
	struct res_lib_ckpt_sectiondelete res_lib_ckpt_sectiondelete;
	struct checkpoint *checkpoint;
	struct checkpoint_section *checkpoint_section;
	SaAisErrorT error = SA_AIS_OK;

	checkpoint = checkpoint_find (
		&checkpoint_list_head,
		&req_exec_ckpt_sectiondelete->checkpoint_name,
		req_exec_ckpt_sectiondelete->ckpt_id);
	if (checkpoint == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	if (checkpoint->active_replica_set == 0) {
		log_printf (LOG_LEVEL_DEBUG, "sectiondelete: no active replica, returning error.\n");
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	/*
	 * Determine if the user is trying to delete the default section
	 */
	if (req_exec_ckpt_sectiondelete->id_len == 0) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	/*
	 * Find checkpoint section to be deleted
	 */
	checkpoint_section = checkpoint_section_find (checkpoint,
		((char *)(req_exec_ckpt_sectiondelete) + sizeof (struct req_exec_ckpt_sectiondelete)),
		req_exec_ckpt_sectiondelete->id_len);
	if (checkpoint_section == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	/*
	 * Delete checkpoint section
	 */
	checkpoint->section_count -= 1;
	checkpoint_section_release (checkpoint_section);

	/*
	 * return result to CKPT library
	 */
error_exit:
	if (api->ipc_source_is_local(&req_exec_ckpt_sectiondelete->source)) {
		res_lib_ckpt_sectiondelete.header.size = sizeof (struct res_lib_ckpt_sectiondelete);
		res_lib_ckpt_sectiondelete.header.id = MESSAGE_RES_CKPT_CHECKPOINT_SECTIONDELETE;
		res_lib_ckpt_sectiondelete.header.error = error;

		api->ipc_conn_send_response (
			req_exec_ckpt_sectiondelete->source.conn,
			&res_lib_ckpt_sectiondelete,
			sizeof (struct res_lib_ckpt_sectiondelete));
	}
}

static void message_handler_req_exec_ckpt_sectionexpirationtimeset (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_ckpt_sectionexpirationtimeset *req_exec_ckpt_sectionexpirationtimeset = (struct req_exec_ckpt_sectionexpirationtimeset *)message;
	struct res_lib_ckpt_sectionexpirationtimeset res_lib_ckpt_sectionexpirationtimeset;
	struct checkpoint *checkpoint;
	struct checkpoint_section *checkpoint_section;
	struct ckpt_identifier *ckpt_id = 0;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_DEBUG, "Executive request to set section expiration time\n");
	checkpoint = checkpoint_find (
		&checkpoint_list_head,
		&req_exec_ckpt_sectionexpirationtimeset->checkpoint_name,
		req_exec_ckpt_sectionexpirationtimeset->ckpt_id);
	if (checkpoint == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	if (checkpoint->active_replica_set == 0) {
		log_printf (LOG_LEVEL_DEBUG, "expirationset: no active replica, returning error.\n");
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	/*
	 * Determine if the user is trying to set expiration time for the default section
	 */
	if (req_exec_ckpt_sectionexpirationtimeset->id_len == 0) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	/*
	 * Find checkpoint section that expiration time should be set for
	 */
	checkpoint_section = checkpoint_section_find (checkpoint,
		((char *)req_exec_ckpt_sectionexpirationtimeset) +
			sizeof (struct req_exec_ckpt_sectionexpirationtimeset),
		req_exec_ckpt_sectionexpirationtimeset->id_len);

	if (checkpoint_section == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	checkpoint_section->section_descriptor.expiration_time =
		req_exec_ckpt_sectionexpirationtimeset->expiration_time;

	api->timer_delete (checkpoint_section->expiration_timer);
	checkpoint_section->expiration_timer = 0;

	if (req_exec_ckpt_sectionexpirationtimeset->expiration_time != SA_TIME_END) {
		ckpt_id = malloc (sizeof(struct ckpt_identifier));
		assert(ckpt_id);
		memcpy(&ckpt_id->ckpt_name,
			&req_exec_ckpt_sectionexpirationtimeset->checkpoint_name,
			sizeof(mar_name_t));
		ckpt_id->ckpt_id =
			req_exec_ckpt_sectionexpirationtimeset->ckpt_id;
		memcpy(&ckpt_id->ckpt_section_id,
			&checkpoint_section->section_descriptor.section_id,
			sizeof(mar_ckpt_section_id_t));
		log_printf (LOG_LEVEL_DEBUG, "req_exec_ckpt_sectionexpirationtimeset Enqueuing Timer to Expire section %s in ckpt %s, ref = 0x%p\n",
			ckpt_id->ckpt_section_id.id,
			ckpt_id->ckpt_name.value,
			ckpt_id);
		api->timer_add_absolute (
			checkpoint_section->section_descriptor.expiration_time,
			ckpt_id,
			timer_function_section_expire,
			&checkpoint_section->expiration_timer);
		log_printf (LOG_LEVEL_DEBUG, "req_exec_ckpt_sectionexpirationtimeset expiration timer = 0x%p\n",
			checkpoint_section->expiration_timer);
	}

error_exit:
	if (api->ipc_source_is_local (&req_exec_ckpt_sectionexpirationtimeset->source)) {
		res_lib_ckpt_sectionexpirationtimeset.header.size =
			sizeof (struct res_lib_ckpt_sectionexpirationtimeset);
		res_lib_ckpt_sectionexpirationtimeset.header.id =
			 MESSAGE_RES_CKPT_CHECKPOINT_SECTIONEXPIRATIONTIMESET;
		res_lib_ckpt_sectionexpirationtimeset.header.error = error;

		api->ipc_conn_send_response (
			req_exec_ckpt_sectionexpirationtimeset->source.conn,
			&res_lib_ckpt_sectionexpirationtimeset,
			sizeof (struct res_lib_ckpt_sectionexpirationtimeset));
	}
}

static void message_handler_req_exec_ckpt_sectionwrite (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_ckpt_sectionwrite *req_exec_ckpt_sectionwrite = (struct req_exec_ckpt_sectionwrite *)message;
	struct res_lib_ckpt_sectionwrite res_lib_ckpt_sectionwrite;
	struct checkpoint *checkpoint;
	struct checkpoint_section *checkpoint_section = 0;
	int size_required;
	void *section_data;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_DEBUG, "Executive request to section write.\n");
	checkpoint = checkpoint_find (
		&checkpoint_list_head,
		&req_exec_ckpt_sectionwrite->checkpoint_name,
		req_exec_ckpt_sectionwrite->ckpt_id);
	if (checkpoint == 0) {
		log_printf (LOG_LEVEL_ERROR, "checkpoint_find returned 0 Calling error_exit.\n");
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	if (checkpoint->active_replica_set == 0) {
		log_printf (LOG_LEVEL_DEBUG, "checkpointwrite: no active replica, returning error.\n");
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	if (checkpoint->checkpoint_creation_attributes.max_section_size < req_exec_ckpt_sectionwrite->data_size) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	log_printf (LOG_LEVEL_DEBUG, "writing checkpoint section is %s\n",
		((char *)req_exec_ckpt_sectionwrite) +
			sizeof (struct req_exec_ckpt_sectionwrite));

	/*
	 * Find checkpoint section to be written
	 */
	checkpoint_section = checkpoint_section_find (checkpoint,
		((char *)req_exec_ckpt_sectionwrite) +
			sizeof (struct req_exec_ckpt_sectionwrite),
		req_exec_ckpt_sectionwrite->id_len);
	if (checkpoint_section == 0) {
		if (req_exec_ckpt_sectionwrite->id_len == 0) {
			log_printf (LOG_LEVEL_DEBUG, "CANT FIND DEFAULT SECTION.\n");
		}
		else {
			log_printf (LOG_LEVEL_DEBUG, "CANT FIND SECTION '%s'\n",
				((char *)req_exec_ckpt_sectionwrite) +
				sizeof (struct req_exec_ckpt_sectionwrite));
		}
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	/*
	 * If write would extend past end of section data, enlarge section
	 */
	size_required = req_exec_ckpt_sectionwrite->data_offset +
		req_exec_ckpt_sectionwrite->data_size;
	if (size_required > checkpoint_section->section_descriptor.section_size) {
		section_data = realloc (checkpoint_section->section_data, size_required);
		if (section_data == 0) {
			log_printf (LOG_LEVEL_ERROR, "section_data realloc returned 0 Calling error_exit.\n");
			error = SA_AIS_ERR_NO_MEMORY;
			goto error_exit;
		}

		/*
		 * Install new section data
		 */
		checkpoint_section->section_data = section_data;
		checkpoint_section->section_descriptor.section_size = size_required;
	}

	/*
	 * Write checkpoint section to section data
	 */
	if (req_exec_ckpt_sectionwrite->data_size > 0) {
		char *sd;
		int *val;
		val = checkpoint_section->section_data;
		sd = (char *)checkpoint_section->section_data;
		memcpy (&sd[req_exec_ckpt_sectionwrite->data_offset],
			((char *)req_exec_ckpt_sectionwrite) +
				sizeof (struct req_exec_ckpt_sectionwrite) +
				req_exec_ckpt_sectionwrite->id_len,
			req_exec_ckpt_sectionwrite->data_size);
	}
	/*
	 * Write sectionwrite response to CKPT library
	 */
error_exit:
	if (api->ipc_source_is_local(&req_exec_ckpt_sectionwrite->source)) {
		res_lib_ckpt_sectionwrite.header.size =
			sizeof (struct res_lib_ckpt_sectionwrite);
		res_lib_ckpt_sectionwrite.header.id =
			MESSAGE_RES_CKPT_CHECKPOINT_SECTIONWRITE;
		res_lib_ckpt_sectionwrite.header.error = error;

		api->ipc_conn_send_response (
			req_exec_ckpt_sectionwrite->source.conn,
			&res_lib_ckpt_sectionwrite,
			sizeof (struct res_lib_ckpt_sectionwrite));
	}
}

static void message_handler_req_exec_ckpt_sectionoverwrite (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_ckpt_sectionoverwrite *req_exec_ckpt_sectionoverwrite = (struct req_exec_ckpt_sectionoverwrite *)message;
	struct res_lib_ckpt_sectionoverwrite res_lib_ckpt_sectionoverwrite;
	struct checkpoint *checkpoint;
	struct checkpoint_section *checkpoint_section;
	void *section_data;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_DEBUG, "Executive request to section overwrite.\n");
	checkpoint = checkpoint_find (
		&checkpoint_list_head,
		&req_exec_ckpt_sectionoverwrite->checkpoint_name,
		req_exec_ckpt_sectionoverwrite->ckpt_id);
	if (checkpoint == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	if (checkpoint->active_replica_set == 0) {
		log_printf (LOG_LEVEL_DEBUG, "sectionoverwrite: no active replica, returning error.\n");
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	if (checkpoint->checkpoint_creation_attributes.max_section_size <
		req_exec_ckpt_sectionoverwrite->data_size) {

		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	/*
	 * Find checkpoint section to be overwritten
	 */
	checkpoint_section = checkpoint_section_find (checkpoint,
		((char *)req_exec_ckpt_sectionoverwrite) +
			sizeof (struct req_exec_ckpt_sectionoverwrite),
		req_exec_ckpt_sectionoverwrite->id_len);
	if (checkpoint_section == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	/*
	 * Allocate checkpoint section data
	 */
	section_data = malloc (req_exec_ckpt_sectionoverwrite->data_size);
	if (section_data == 0) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_exit;
	}

	memcpy (section_data,
		((char *)req_exec_ckpt_sectionoverwrite) +
			sizeof (struct req_exec_ckpt_sectionoverwrite) +
			req_exec_ckpt_sectionoverwrite->id_len,
		req_exec_ckpt_sectionoverwrite->data_size);

	/*
	 * release old checkpoint section data
	 */
	free (checkpoint_section->section_data);

	/*
	 * Install overwritten checkpoint section data
	 */
	checkpoint_section->section_descriptor.section_size =
		req_exec_ckpt_sectionoverwrite->data_size;
	checkpoint_section->section_descriptor.section_state =
		SA_CKPT_SECTION_VALID;
	/*
	 * TODO current time
	 */
	checkpoint_section->section_descriptor.last_update = 0;
	checkpoint_section->section_data = section_data;

	/*
	 * return result to CKPT library
	 */
error_exit:
	if (api->ipc_source_is_local(&req_exec_ckpt_sectionoverwrite->source)) {
		res_lib_ckpt_sectionoverwrite.header.size =
			sizeof (struct res_lib_ckpt_sectionoverwrite);
		res_lib_ckpt_sectionoverwrite.header.id =
			MESSAGE_RES_CKPT_CHECKPOINT_SECTIONOVERWRITE;
		res_lib_ckpt_sectionoverwrite.header.error = error;

		api->ipc_conn_send_response (
			req_exec_ckpt_sectionoverwrite->source.conn,
			&res_lib_ckpt_sectionoverwrite,
			sizeof (struct res_lib_ckpt_sectionoverwrite));
	}
}

static void message_handler_req_exec_ckpt_sectionread (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_ckpt_sectionread *req_exec_ckpt_sectionread = (struct req_exec_ckpt_sectionread *)message;
	struct res_lib_ckpt_sectionread res_lib_ckpt_sectionread;
	struct checkpoint *checkpoint;
	struct checkpoint_section *checkpoint_section = 0;
	int section_size = 0;
	SaAisErrorT error = SA_AIS_OK;

	res_lib_ckpt_sectionread.data_read = 0;

	log_printf (LOG_LEVEL_DEBUG, "Executive request for section read.\n");

	checkpoint = checkpoint_find (
		&checkpoint_list_head,
		&req_exec_ckpt_sectionread->checkpoint_name,
		req_exec_ckpt_sectionread->ckpt_id);
	if (checkpoint == 0) {
		error = SA_AIS_ERR_LIBRARY;
		goto error_exit;
	}

	if (checkpoint->active_replica_set == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	/*
	 * Find checkpoint section to be read
	 */
	checkpoint_section = checkpoint_section_find (checkpoint,
		((char *)req_exec_ckpt_sectionread) +
			sizeof (struct req_exec_ckpt_sectionread),
		req_exec_ckpt_sectionread->id_len);
	if (checkpoint_section == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	/*
	 * If data size is greater then max section size, return INVALID_PARAM
	 */
	if (checkpoint->checkpoint_creation_attributes.max_section_size <
		req_exec_ckpt_sectionread->data_size) {

		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	/*
	 * If data_offset is past end of data, return INVALID_PARAM
	 */
	if (req_exec_ckpt_sectionread->data_offset > checkpoint_section->section_descriptor.section_size) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	/*
	 * Determine the section size
	 */
	section_size = checkpoint_section->section_descriptor.section_size -
		req_exec_ckpt_sectionread->data_offset;

	/*
	 * If the library has less space available then can be sent from the
	 * section, reduce bytes sent to library to max requested
	 */
	if (section_size > req_exec_ckpt_sectionread->data_size) {
		section_size = req_exec_ckpt_sectionread->data_size;
	}

	/*
	 * Write read response to CKPT library
	 */
error_exit:
	if (api->ipc_source_is_local(&req_exec_ckpt_sectionread->source)) {
		res_lib_ckpt_sectionread.header.size = sizeof (struct res_lib_ckpt_sectionread) + section_size;
		res_lib_ckpt_sectionread.header.id = MESSAGE_RES_CKPT_CHECKPOINT_SECTIONREAD;
		res_lib_ckpt_sectionread.header.error = error;

		if (section_size != 0) {
			res_lib_ckpt_sectionread.data_read = section_size;
		}

		api->ipc_conn_send_response (
			req_exec_ckpt_sectionread->source.conn,
			&res_lib_ckpt_sectionread,
			sizeof (struct res_lib_ckpt_sectionread));

		/*
		 * Write checkpoint to CKPT library section if section has data
		 */
		if (error == SA_AIS_OK) {
			char *sd;
			sd = (char *)checkpoint_section->section_data;
			api->ipc_conn_send_response (
				req_exec_ckpt_sectionread->source.conn,
				&sd[req_exec_ckpt_sectionread->data_offset],
				section_size);
		}
	}
}

static int ckpt_lib_init_fn (void *conn)
{
	struct ckpt_pd *ckpt_pd = (struct ckpt_pd *)api->ipc_private_data_get (conn);

	hdb_create (&ckpt_pd->iteration_hdb);

	list_init (&ckpt_pd->checkpoint_list);

       return (0);

}

static int ckpt_lib_exit_fn (void *conn)
{
	struct checkpoint_cleanup *checkpoint_cleanup;
	struct list_head *list;
	struct ckpt_pd *ckpt_pd = (struct ckpt_pd *)api->ipc_private_data_get (conn);

	log_printf (LOG_LEVEL_DEBUG, "checkpoint exit conn %p\n", conn);

	/*
	 * close all checkpoints opened on this connection
	 */
	list = ckpt_pd->checkpoint_list.next;
	while (!list_empty(&ckpt_pd->checkpoint_list)) {

		checkpoint_cleanup = list_entry (list,
			struct checkpoint_cleanup, list);

		assert (checkpoint_cleanup->checkpoint_name.length != 0);
		ckpt_checkpoint_close (
			&checkpoint_cleanup->checkpoint_name,
			checkpoint_cleanup->ckpt_id);

		list_del (&checkpoint_cleanup->list);
		free (checkpoint_cleanup);

		list = ckpt_pd->checkpoint_list.next;
	}

	hdb_destroy (&ckpt_pd->iteration_hdb);

	return (0);
}


static void message_handler_req_lib_ckpt_checkpointopen (
	void *conn,
	void *msg)
{
	struct req_lib_ckpt_checkpointopen *req_lib_ckpt_checkpointopen = (struct req_lib_ckpt_checkpointopen *)msg;
	struct req_exec_ckpt_checkpointopen req_exec_ckpt_checkpointopen;
	struct iovec iovec;

	log_printf (LOG_LEVEL_DEBUG, "Library request to open checkpoint.\n");
	req_exec_ckpt_checkpointopen.header.size =
		sizeof (struct req_exec_ckpt_checkpointopen);
	req_exec_ckpt_checkpointopen.header.id =
		SERVICE_ID_MAKE (CKPT_SERVICE, MESSAGE_REQ_EXEC_CKPT_CHECKPOINTOPEN);

	api->ipc_source_set (&req_exec_ckpt_checkpointopen.source, conn);
	memcpy (&req_exec_ckpt_checkpointopen.checkpoint_name,
		&req_lib_ckpt_checkpointopen->checkpoint_name,
		sizeof (mar_name_t));
	req_exec_ckpt_checkpointopen.ckpt_id =
		req_lib_ckpt_checkpointopen->ckpt_id;
	memcpy (&req_exec_ckpt_checkpointopen.checkpoint_creation_attributes,
		&req_lib_ckpt_checkpointopen->checkpoint_creation_attributes,
		sizeof (mar_ckpt_checkpoint_creation_attributes_t));
	req_exec_ckpt_checkpointopen.checkpoint_creation_attributes_set =
		req_lib_ckpt_checkpointopen->checkpoint_creation_attributes_set;
	req_exec_ckpt_checkpointopen.checkpoint_open_flags =
		req_lib_ckpt_checkpointopen->checkpoint_open_flags;
	req_exec_ckpt_checkpointopen.invocation =
		req_lib_ckpt_checkpointopen->invocation;
	req_exec_ckpt_checkpointopen.checkpoint_handle =
		req_lib_ckpt_checkpointopen->checkpoint_handle;
	req_exec_ckpt_checkpointopen.fail_with_error =
		req_lib_ckpt_checkpointopen->fail_with_error;
	req_exec_ckpt_checkpointopen.async_call =
		req_lib_ckpt_checkpointopen->async_call;

	iovec.iov_base = (char *)&req_exec_ckpt_checkpointopen;
	iovec.iov_len = sizeof (req_exec_ckpt_checkpointopen);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_ckpt_checkpointclose (
	void *conn,
	void *msg)
{
	struct req_lib_ckpt_checkpointclose *req_lib_ckpt_checkpointclose = (struct req_lib_ckpt_checkpointclose *)msg;
	struct req_exec_ckpt_checkpointclose req_exec_ckpt_checkpointclose;
	struct iovec iovec;

	req_exec_ckpt_checkpointclose.header.size =
		sizeof (struct req_exec_ckpt_checkpointclose);
	req_exec_ckpt_checkpointclose.header.id =
		SERVICE_ID_MAKE (CKPT_SERVICE,
			MESSAGE_REQ_EXEC_CKPT_CHECKPOINTCLOSE);

	api->ipc_source_set (&req_exec_ckpt_checkpointclose.source, conn);

	memcpy (&req_exec_ckpt_checkpointclose.checkpoint_name,
		&req_lib_ckpt_checkpointclose->checkpoint_name, sizeof (mar_name_t));
	req_exec_ckpt_checkpointclose.ckpt_id =
		req_lib_ckpt_checkpointclose->ckpt_id;

	iovec.iov_base = (char *)&req_exec_ckpt_checkpointclose;
	iovec.iov_len = sizeof (req_exec_ckpt_checkpointclose);

	ckpt_checkpoint_remove_cleanup (
		conn,
		req_lib_ckpt_checkpointclose->checkpoint_name,
		req_lib_ckpt_checkpointclose->ckpt_id);
	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_ckpt_checkpointunlink (
	void *conn,
	void *msg)
{
	struct req_lib_ckpt_checkpointunlink *req_lib_ckpt_checkpointunlink = (struct req_lib_ckpt_checkpointunlink *)msg;
	struct req_exec_ckpt_checkpointunlink req_exec_ckpt_checkpointunlink;
	struct iovec iovec;

	req_exec_ckpt_checkpointunlink.header.size =
		sizeof (struct req_exec_ckpt_checkpointunlink);
	req_exec_ckpt_checkpointunlink.header.id =
		SERVICE_ID_MAKE (CKPT_SERVICE, MESSAGE_REQ_EXEC_CKPT_CHECKPOINTUNLINK);

	api->ipc_source_set (&req_exec_ckpt_checkpointunlink.source, conn);

	memcpy (&req_exec_ckpt_checkpointunlink.checkpoint_name,
		&req_lib_ckpt_checkpointunlink->checkpoint_name,
		sizeof (mar_name_t));

	iovec.iov_base = (char *)&req_exec_ckpt_checkpointunlink;
	iovec.iov_len = sizeof (req_exec_ckpt_checkpointunlink);

	assert (api->totem_mcast (&iovec, 1,
		TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_ckpt_checkpointretentiondurationset (
	void *conn,
	void *msg)
{
	struct req_lib_ckpt_checkpointretentiondurationset *req_lib_ckpt_checkpointretentiondurationset = (struct req_lib_ckpt_checkpointretentiondurationset *)msg;
	struct req_exec_ckpt_checkpointretentiondurationset req_exec_ckpt_checkpointretentiondurationset;
	struct iovec iovec;

	log_printf (LOG_LEVEL_DEBUG, "DURATION SET FROM API conn %p\n", conn);
	req_exec_ckpt_checkpointretentiondurationset.header.id =
		SERVICE_ID_MAKE (CKPT_SERVICE,
			MESSAGE_REQ_EXEC_CKPT_CHECKPOINTRETENTIONDURATIONSET);
	req_exec_ckpt_checkpointretentiondurationset.header.size = sizeof (struct req_exec_ckpt_checkpointretentiondurationset);

	api->ipc_source_set (&req_exec_ckpt_checkpointretentiondurationset.source, conn);
	memcpy (&req_exec_ckpt_checkpointretentiondurationset.checkpoint_name,
		&req_lib_ckpt_checkpointretentiondurationset->checkpoint_name,
		sizeof (mar_name_t));
	req_exec_ckpt_checkpointretentiondurationset.ckpt_id =
		req_lib_ckpt_checkpointretentiondurationset->ckpt_id;
	req_exec_ckpt_checkpointretentiondurationset.retention_duration =
		req_lib_ckpt_checkpointretentiondurationset->retention_duration;

	iovec.iov_base = (char *)&req_exec_ckpt_checkpointretentiondurationset;
	iovec.iov_len = sizeof (req_exec_ckpt_checkpointretentiondurationset);

	assert (api->totem_mcast (&iovec, 1,
		TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_ckpt_activereplicaset (
	void *conn,
	void *msg)
{
	struct req_lib_ckpt_activereplicaset *req_lib_ckpt_activereplicaset = (struct req_lib_ckpt_activereplicaset *)msg;
	struct res_lib_ckpt_activereplicaset res_lib_ckpt_activereplicaset;
	struct checkpoint *checkpoint;
	SaAisErrorT error = SA_AIS_OK;

	checkpoint = checkpoint_find (
		&checkpoint_list_head,
		&req_lib_ckpt_activereplicaset->checkpoint_name,
		req_lib_ckpt_activereplicaset->ckpt_id);

	/*
	 * Make sure checkpoint is collocated and async update option
	 */
	if (((checkpoint->checkpoint_creation_attributes.creation_flags & SA_CKPT_CHECKPOINT_COLLOCATED) == 0) ||
		(checkpoint->checkpoint_creation_attributes.creation_flags & (SA_CKPT_WR_ACTIVE_REPLICA | SA_CKPT_WR_ACTIVE_REPLICA_WEAK)) == 0) {
		error = SA_AIS_ERR_BAD_OPERATION;
	}
	checkpoint->active_replica_set = 1;
	res_lib_ckpt_activereplicaset.header.size = sizeof (struct res_lib_ckpt_activereplicaset);
	res_lib_ckpt_activereplicaset.header.id = MESSAGE_RES_CKPT_ACTIVEREPLICASET;
	res_lib_ckpt_activereplicaset.header.error = error;

	api->ipc_conn_send_response (
		conn,
		&res_lib_ckpt_activereplicaset,
		sizeof (struct res_lib_ckpt_activereplicaset));
}

static void message_handler_req_lib_ckpt_checkpointstatusget (
	void *conn,
	void *msg)
{
	struct req_lib_ckpt_checkpointstatusget *req_lib_ckpt_checkpointstatusget = (struct req_lib_ckpt_checkpointstatusget *)msg;
	struct res_lib_ckpt_checkpointstatusget res_lib_ckpt_checkpointstatusget;
	struct checkpoint *checkpoint;
	int memory_used = 0;
	int number_of_sections = 0;
	struct list_head *checkpoint_section_list;
	struct checkpoint_section *checkpointSection;

	/*
	 * Count memory used by checkpoint sections
	 */
	checkpoint = checkpoint_find (
		&checkpoint_list_head,
		&req_lib_ckpt_checkpointstatusget->checkpoint_name,
		req_lib_ckpt_checkpointstatusget->ckpt_id);

	if (checkpoint) {

		for (checkpoint_section_list = checkpoint->sections_list_head.next;
			checkpoint_section_list != &checkpoint->sections_list_head;
			checkpoint_section_list = checkpoint_section_list->next) {

			checkpointSection = list_entry (checkpoint_section_list,
				struct checkpoint_section, list);

			memory_used += checkpointSection->section_descriptor.section_size;
			number_of_sections += 1;
		}

		/*
		 * Build checkpoint status get response
		 */
		res_lib_ckpt_checkpointstatusget.header.size = sizeof (struct res_lib_ckpt_checkpointstatusget);
		res_lib_ckpt_checkpointstatusget.header.id = MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTSTATUSGET;
		if (checkpoint->active_replica_set == 1) {
			res_lib_ckpt_checkpointstatusget.header.error = SA_AIS_OK;
		} else {
			res_lib_ckpt_checkpointstatusget.header.error = SA_AIS_ERR_NOT_EXIST;
		}

		memcpy (&res_lib_ckpt_checkpointstatusget.checkpoint_descriptor.checkpoint_creation_attributes,
			&checkpoint->checkpoint_creation_attributes,
			sizeof (mar_ckpt_checkpoint_creation_attributes_t));
		res_lib_ckpt_checkpointstatusget.checkpoint_descriptor.number_of_sections = number_of_sections;
		res_lib_ckpt_checkpointstatusget.checkpoint_descriptor.memory_used = memory_used;
	}
	else {
		log_printf (LOG_LEVEL_ERROR, "#### Could Not Find the Checkpoint's status so Returning Error. ####\n");

		res_lib_ckpt_checkpointstatusget.header.size = sizeof (struct res_lib_ckpt_checkpointstatusget);
		res_lib_ckpt_checkpointstatusget.header.id = MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTSTATUSGET;
		res_lib_ckpt_checkpointstatusget.header.error = SA_AIS_ERR_NOT_EXIST;
	}
	api->ipc_conn_send_response (
		conn,
		&res_lib_ckpt_checkpointstatusget,
		sizeof (struct res_lib_ckpt_checkpointstatusget));
}

static void message_handler_req_lib_ckpt_sectioncreate (
	void *conn,
	void *msg)
{
	struct req_lib_ckpt_sectioncreate *req_lib_ckpt_sectioncreate = (struct req_lib_ckpt_sectioncreate *)msg;
	struct req_exec_ckpt_sectioncreate req_exec_ckpt_sectioncreate;
	struct iovec iovecs[2];

	log_printf (LOG_LEVEL_DEBUG, "Section create from conn %p\n", conn);

	req_exec_ckpt_sectioncreate.header.id =
		SERVICE_ID_MAKE (CKPT_SERVICE,
			MESSAGE_REQ_EXEC_CKPT_SECTIONCREATE);
	req_exec_ckpt_sectioncreate.header.size = sizeof (struct req_exec_ckpt_sectioncreate);

	api->ipc_source_set (&req_exec_ckpt_sectioncreate.source, conn);

	memcpy (&req_exec_ckpt_sectioncreate.checkpoint_name,
		&req_lib_ckpt_sectioncreate->checkpoint_name,
		sizeof (mar_name_t));
	req_exec_ckpt_sectioncreate.ckpt_id =
		req_lib_ckpt_sectioncreate->ckpt_id;
	req_exec_ckpt_sectioncreate.id_len = req_lib_ckpt_sectioncreate->id_len;
	req_exec_ckpt_sectioncreate.expiration_time =
		req_lib_ckpt_sectioncreate->expiration_time;
	req_exec_ckpt_sectioncreate.initial_data_size =
		req_lib_ckpt_sectioncreate->initial_data_size;

	iovecs[0].iov_base = (char *)&req_exec_ckpt_sectioncreate;
	iovecs[0].iov_len = sizeof (req_exec_ckpt_sectioncreate);

	/*
	 * Send section name and initial data in message
	 */
	iovecs[1].iov_base = ((char *)req_lib_ckpt_sectioncreate) + sizeof (struct req_lib_ckpt_sectioncreate);
	iovecs[1].iov_len = req_lib_ckpt_sectioncreate->header.size - sizeof (struct req_lib_ckpt_sectioncreate);
	req_exec_ckpt_sectioncreate.header.size += iovecs[1].iov_len;

	if (iovecs[1].iov_len) {
		log_printf (LOG_LEVEL_DEBUG, "message_handler_req_lib_ckpt_sectioncreate Section = %p, id_len = %d\n",
			iovecs[1].iov_base,
			(int)iovecs[1].iov_len);
	}

	if (iovecs[1].iov_len > 0) {
		log_printf (LOG_LEVEL_DEBUG, "IOV_BASE is %p\n", iovecs[1].iov_base);
		assert (api->totem_mcast (iovecs, 2, TOTEM_AGREED) == 0);
	} else {
		assert (api->totem_mcast (iovecs, 1, TOTEM_AGREED) == 0);
	}
}

static void message_handler_req_lib_ckpt_sectiondelete (
	void *conn,
	void *msg)
{
	struct req_lib_ckpt_sectiondelete *req_lib_ckpt_sectiondelete = (struct req_lib_ckpt_sectiondelete *)msg;
	struct req_exec_ckpt_sectiondelete req_exec_ckpt_sectiondelete;
	struct iovec iovecs[2];

	log_printf (LOG_LEVEL_DEBUG, "section delete from conn %p\n", conn);

	req_exec_ckpt_sectiondelete.header.id =
		SERVICE_ID_MAKE (CKPT_SERVICE,
			MESSAGE_REQ_EXEC_CKPT_SECTIONDELETE);
	req_exec_ckpt_sectiondelete.header.size = sizeof (struct req_exec_ckpt_sectiondelete);

	api->ipc_source_set (&req_exec_ckpt_sectiondelete.source, conn);

	memcpy (&req_exec_ckpt_sectiondelete.checkpoint_name,
		&req_lib_ckpt_sectiondelete->checkpoint_name,
		sizeof (mar_name_t));
	req_exec_ckpt_sectiondelete.ckpt_id =
		req_lib_ckpt_sectiondelete->ckpt_id;
	req_exec_ckpt_sectiondelete.id_len = req_lib_ckpt_sectiondelete->id_len;

	iovecs[0].iov_base = (char *)&req_exec_ckpt_sectiondelete;
	iovecs[0].iov_len = sizeof (req_exec_ckpt_sectiondelete);

	/*
	 * Send section name
	 */
	iovecs[1].iov_base = ((char *)req_lib_ckpt_sectiondelete) +
		sizeof (struct req_lib_ckpt_sectiondelete);
	iovecs[1].iov_len = req_lib_ckpt_sectiondelete->header.size -
		sizeof (struct req_lib_ckpt_sectiondelete);
	req_exec_ckpt_sectiondelete.header.size += iovecs[1].iov_len;

	if (iovecs[1].iov_len > 0) {
		assert (api->totem_mcast (iovecs, 2, TOTEM_AGREED) == 0);
	} else {
		assert (api->totem_mcast (iovecs, 1, TOTEM_AGREED) == 0);
	}
}

static void message_handler_req_lib_ckpt_sectionexpirationtimeset (
	void *conn,
	void *msg)
{
	struct req_lib_ckpt_sectionexpirationtimeset *req_lib_ckpt_sectionexpirationtimeset = (struct req_lib_ckpt_sectionexpirationtimeset *)msg;
	struct req_exec_ckpt_sectionexpirationtimeset req_exec_ckpt_sectionexpirationtimeset;
	struct iovec iovecs[2];

	log_printf (LOG_LEVEL_DEBUG, "section expiration time set from conn %p\n", conn);
	req_exec_ckpt_sectionexpirationtimeset.header.id =
		SERVICE_ID_MAKE (CKPT_SERVICE,
			MESSAGE_REQ_EXEC_CKPT_SECTIONEXPIRATIONTIMESET);
	req_exec_ckpt_sectionexpirationtimeset.header.size = sizeof (struct req_exec_ckpt_sectionexpirationtimeset);

	api->ipc_source_set (&req_exec_ckpt_sectionexpirationtimeset.source, conn);

	memcpy (&req_exec_ckpt_sectionexpirationtimeset.checkpoint_name,
		&req_lib_ckpt_sectionexpirationtimeset->checkpoint_name,
		sizeof (mar_name_t));
	req_exec_ckpt_sectionexpirationtimeset.ckpt_id =
		req_lib_ckpt_sectionexpirationtimeset->ckpt_id;
	req_exec_ckpt_sectionexpirationtimeset.id_len =
		req_lib_ckpt_sectionexpirationtimeset->id_len;
	req_exec_ckpt_sectionexpirationtimeset.expiration_time =
		req_lib_ckpt_sectionexpirationtimeset->expiration_time;

	iovecs[0].iov_base = (char *)&req_exec_ckpt_sectionexpirationtimeset;
	iovecs[0].iov_len = sizeof (req_exec_ckpt_sectionexpirationtimeset);

	/*
	 * Send section name
	 */
	iovecs[1].iov_base = ((char *)req_lib_ckpt_sectionexpirationtimeset) +
		sizeof (struct req_lib_ckpt_sectionexpirationtimeset);
	iovecs[1].iov_len = req_lib_ckpt_sectionexpirationtimeset->header.size -
		sizeof (struct req_lib_ckpt_sectionexpirationtimeset);
	req_exec_ckpt_sectionexpirationtimeset.header.size += iovecs[1].iov_len;

	if (iovecs[1].iov_len > 0) {
		assert (api->totem_mcast (iovecs, 2, TOTEM_AGREED) == 0);
	} else {
		assert (api->totem_mcast (iovecs, 1, TOTEM_AGREED) == 0);
	}
}

static void message_handler_req_lib_ckpt_sectionwrite (
	void *conn,
	void *msg)
{
	struct req_lib_ckpt_sectionwrite *req_lib_ckpt_sectionwrite = (struct req_lib_ckpt_sectionwrite *)msg;
	struct req_exec_ckpt_sectionwrite req_exec_ckpt_sectionwrite;
	struct iovec iovecs[2];

	log_printf (LOG_LEVEL_DEBUG, "Received data from lib with len = %d and ref = 0x%lx\n",
		(int)req_lib_ckpt_sectionwrite->data_size,
		(long)req_lib_ckpt_sectionwrite->data_offset);

	log_printf (LOG_LEVEL_DEBUG, "Checkpoint section being written to is %s, id_len = %d\n",
		((char *)req_lib_ckpt_sectionwrite) +
			sizeof (struct req_lib_ckpt_sectionwrite),
		req_lib_ckpt_sectionwrite->id_len);

	log_printf (LOG_LEVEL_DEBUG, "Section write from conn %p\n", conn);

	/*
	 * checkpoint opened is writeable mode so send message to cluster
	 */
	req_exec_ckpt_sectionwrite.header.id =
		SERVICE_ID_MAKE (CKPT_SERVICE,
			MESSAGE_REQ_EXEC_CKPT_SECTIONWRITE);
	req_exec_ckpt_sectionwrite.header.size =
		sizeof (struct req_exec_ckpt_sectionwrite);

	api->ipc_source_set (&req_exec_ckpt_sectionwrite.source, conn);

	memcpy (&req_exec_ckpt_sectionwrite.checkpoint_name,
		&req_lib_ckpt_sectionwrite->checkpoint_name,
		sizeof (mar_name_t));
	req_exec_ckpt_sectionwrite.ckpt_id =
		req_lib_ckpt_sectionwrite->ckpt_id;
	req_exec_ckpt_sectionwrite.id_len =
		req_lib_ckpt_sectionwrite->id_len;
	req_exec_ckpt_sectionwrite.data_offset =
		req_lib_ckpt_sectionwrite->data_offset;
	req_exec_ckpt_sectionwrite.data_size =
		req_lib_ckpt_sectionwrite->data_size;

	iovecs[0].iov_base = (char *)&req_exec_ckpt_sectionwrite;
	iovecs[0].iov_len = sizeof (req_exec_ckpt_sectionwrite);

	/*
	 * Send section name and data to write in message
	 */
	iovecs[1].iov_base = ((char *)req_lib_ckpt_sectionwrite) +
		sizeof (struct req_lib_ckpt_sectionwrite);
	iovecs[1].iov_len = req_lib_ckpt_sectionwrite->header.size -
		sizeof (struct req_lib_ckpt_sectionwrite);
	req_exec_ckpt_sectionwrite.header.size += iovecs[1].iov_len;

	if (iovecs[1].iov_len > 0) {
		assert (api->totem_mcast (iovecs, 2, TOTEM_AGREED) == 0);
	} else {
		assert (api->totem_mcast (iovecs, 1, TOTEM_AGREED) == 0);
	}
}

static void message_handler_req_lib_ckpt_sectionoverwrite (
	void *conn,
	void *msg)
{
	struct req_lib_ckpt_sectionoverwrite *req_lib_ckpt_sectionoverwrite = (struct req_lib_ckpt_sectionoverwrite *)msg;
	struct req_exec_ckpt_sectionoverwrite req_exec_ckpt_sectionoverwrite;
	struct iovec iovecs[2];

	log_printf (LOG_LEVEL_DEBUG, "Section overwrite from conn %p\n", conn);

	req_exec_ckpt_sectionoverwrite.header.id =
		SERVICE_ID_MAKE (CKPT_SERVICE,
			MESSAGE_REQ_EXEC_CKPT_SECTIONOVERWRITE);
	req_exec_ckpt_sectionoverwrite.header.size =
		sizeof (struct req_exec_ckpt_sectionoverwrite);
	api->ipc_source_set (&req_exec_ckpt_sectionoverwrite.source, conn);
	memcpy (&req_exec_ckpt_sectionoverwrite.checkpoint_name,
		&req_lib_ckpt_sectionoverwrite->checkpoint_name,
		sizeof (mar_name_t));
	req_exec_ckpt_sectionoverwrite.ckpt_id =
		req_lib_ckpt_sectionoverwrite->ckpt_id;
	req_exec_ckpt_sectionoverwrite.id_len =
		req_lib_ckpt_sectionoverwrite->id_len;
	req_exec_ckpt_sectionoverwrite.data_size =
		req_lib_ckpt_sectionoverwrite->data_size;

	iovecs[0].iov_base = (char *)&req_exec_ckpt_sectionoverwrite;
	iovecs[0].iov_len = sizeof (req_exec_ckpt_sectionoverwrite);

	/*
	 * Send section name and data to overwrite in message
	 */
	iovecs[1].iov_base = ((char *)req_lib_ckpt_sectionoverwrite) +
		sizeof (struct req_lib_ckpt_sectionoverwrite);
	iovecs[1].iov_len = req_lib_ckpt_sectionoverwrite->header.size -
		sizeof (struct req_lib_ckpt_sectionoverwrite);
	req_exec_ckpt_sectionoverwrite.header.size += iovecs[1].iov_len;

	if (iovecs[1].iov_len > 0) {
		assert (api->totem_mcast (iovecs, 2, TOTEM_AGREED) == 0);
	} else {
		assert (api->totem_mcast (iovecs, 1, TOTEM_AGREED) == 0);
	}
}

static void message_handler_req_lib_ckpt_sectionread (
	void *conn,
	void *msg)
{
	struct req_lib_ckpt_sectionread *req_lib_ckpt_sectionread = (struct req_lib_ckpt_sectionread *)msg;
	struct req_exec_ckpt_sectionread req_exec_ckpt_sectionread;
	struct iovec iovecs[2];

	log_printf (LOG_LEVEL_DEBUG, "Section read from conn %p\n", conn);

	/*
	 * checkpoint opened is writeable mode so send message to cluster
	 */
	req_exec_ckpt_sectionread.header.id =
		SERVICE_ID_MAKE (CKPT_SERVICE,
			MESSAGE_REQ_EXEC_CKPT_SECTIONREAD);
	req_exec_ckpt_sectionread.header.size =
		sizeof (struct req_exec_ckpt_sectionread);
	api->ipc_source_set (&req_exec_ckpt_sectionread.source, conn);
	memcpy (&req_exec_ckpt_sectionread.checkpoint_name,
		&req_lib_ckpt_sectionread->checkpoint_name,
		sizeof (mar_name_t));
	req_exec_ckpt_sectionread.ckpt_id =
		req_lib_ckpt_sectionread->ckpt_id;
	req_exec_ckpt_sectionread.id_len =
		req_lib_ckpt_sectionread->id_len;
	req_exec_ckpt_sectionread.data_offset =
		req_lib_ckpt_sectionread->data_offset;
	req_exec_ckpt_sectionread.data_size =
		req_lib_ckpt_sectionread->data_size;

	iovecs[0].iov_base = (char *)&req_exec_ckpt_sectionread;
	iovecs[0].iov_len = sizeof (req_exec_ckpt_sectionread);

		/*
		 * Send section name and data to overwrite in message
		 */
	iovecs[1].iov_base = ((char *)req_lib_ckpt_sectionread) +
		sizeof (struct req_lib_ckpt_sectionread);
	iovecs[1].iov_len = req_lib_ckpt_sectionread->header.size -
		sizeof (struct req_lib_ckpt_sectionread);
	req_exec_ckpt_sectionread.header.size += iovecs[1].iov_len;

	if (iovecs[1].iov_len > 0) {
		assert (api->totem_mcast (iovecs, 2, TOTEM_AGREED) == 0);
	} else {
		assert (api->totem_mcast (iovecs, 1, TOTEM_AGREED) == 0);
	}
}

static void message_handler_req_lib_ckpt_checkpointsynchronize (
	void *conn,
	void *msg)
{
	struct req_lib_ckpt_checkpointsynchronize *req_lib_ckpt_checkpointsynchronize = (struct req_lib_ckpt_checkpointsynchronize *)msg;
	struct res_lib_ckpt_checkpointsynchronize res_lib_ckpt_checkpointsynchronize;
	struct checkpoint *checkpoint;

	checkpoint = checkpoint_find (
		&checkpoint_list_head,
		&req_lib_ckpt_checkpointsynchronize->checkpoint_name,
		req_lib_ckpt_checkpointsynchronize->ckpt_id);
	if ((checkpoint->checkpoint_creation_attributes.creation_flags & (SA_CKPT_WR_ACTIVE_REPLICA | SA_CKPT_WR_ACTIVE_REPLICA_WEAK)) == 0) {
		res_lib_ckpt_checkpointsynchronize.header.error = SA_AIS_ERR_BAD_OPERATION;
	} else
	if (checkpoint->active_replica_set == 1) {
		res_lib_ckpt_checkpointsynchronize.header.error = SA_AIS_OK;
	} else {
		res_lib_ckpt_checkpointsynchronize.header.error = SA_AIS_ERR_NOT_EXIST;
	}

	res_lib_ckpt_checkpointsynchronize.header.size = sizeof (struct res_lib_ckpt_checkpointsynchronize);
	res_lib_ckpt_checkpointsynchronize.header.id = MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTSYNCHRONIZE;

	api->ipc_conn_send_response (
		conn,
		&res_lib_ckpt_checkpointsynchronize,
		sizeof (struct res_lib_ckpt_checkpointsynchronize));
}

static void message_handler_req_lib_ckpt_checkpointsynchronizeasync (
	void *conn,
	void *msg)
{
	struct req_lib_ckpt_checkpointsynchronizeasync *req_lib_ckpt_checkpointsynchronizeasync = (struct req_lib_ckpt_checkpointsynchronizeasync *)msg;
	struct res_lib_ckpt_checkpointsynchronizeasync res_lib_ckpt_checkpointsynchronizeasync;
	struct checkpoint *checkpoint;

	checkpoint = checkpoint_find (
		&checkpoint_list_head,
		&req_lib_ckpt_checkpointsynchronizeasync->checkpoint_name,
		req_lib_ckpt_checkpointsynchronizeasync->ckpt_id);
	if ((checkpoint->checkpoint_creation_attributes.creation_flags & (SA_CKPT_WR_ACTIVE_REPLICA | SA_CKPT_WR_ACTIVE_REPLICA_WEAK)) == 0) {
		res_lib_ckpt_checkpointsynchronizeasync.header.error = SA_AIS_ERR_BAD_OPERATION;
	} else
	if (checkpoint->active_replica_set == 1) {
		res_lib_ckpt_checkpointsynchronizeasync.header.error = SA_AIS_OK;
	} else {
		res_lib_ckpt_checkpointsynchronizeasync.header.error = SA_AIS_ERR_NOT_EXIST;
	}

	res_lib_ckpt_checkpointsynchronizeasync.header.size = sizeof (struct res_lib_ckpt_checkpointsynchronizeasync);
	res_lib_ckpt_checkpointsynchronizeasync.header.id = MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTSYNCHRONIZEASYNC;
	res_lib_ckpt_checkpointsynchronizeasync.invocation = req_lib_ckpt_checkpointsynchronizeasync->invocation;

	api->ipc_conn_send_response (
		conn,
		&res_lib_ckpt_checkpointsynchronizeasync,
		sizeof (struct res_lib_ckpt_checkpointsynchronizeasync));

	api->ipc_conn_send_response (
		api->ipc_conn_partner_get (conn),
		&res_lib_ckpt_checkpointsynchronizeasync,
		sizeof (struct res_lib_ckpt_checkpointsynchronizeasync));
}

static void message_handler_req_lib_ckpt_sectioniterationinitialize (
	void *conn,
	void *msg)
{
	struct req_lib_ckpt_sectioniterationinitialize *req_lib_ckpt_sectioniterationinitialize = (struct req_lib_ckpt_sectioniterationinitialize *)msg;
	struct res_lib_ckpt_sectioniterationinitialize res_lib_ckpt_sectioniterationinitialize;
	struct checkpoint *checkpoint;
	struct checkpoint_section *checkpoint_section;
	struct iteration_entry *iteration_entries;
	struct list_head *section_list;
	struct iteration_instance *iteration_instance;
	void *iteration_instance_p;
	unsigned int iteration_handle = 0;
	int res;
	SaAisErrorT error = SA_AIS_OK;

	struct ckpt_pd *ckpt_pd = (struct ckpt_pd *)api->ipc_private_data_get (conn);

	log_printf (LOG_LEVEL_DEBUG, "section iteration initialize\n");

	checkpoint = checkpoint_find (
		&checkpoint_list_head,
		&req_lib_ckpt_sectioniterationinitialize->checkpoint_name,
		req_lib_ckpt_sectioniterationinitialize->ckpt_id);
	if (checkpoint == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	if (checkpoint->active_replica_set == 0) {
		log_printf (LOG_LEVEL_DEBUG, "iterationinitialize: no active replica, returning error.\n");
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	res = hdb_handle_create (&ckpt_pd->iteration_hdb, sizeof(struct iteration_instance),
		&iteration_handle);
	if (res != 0) {
		goto error_exit;
	}

	res = hdb_handle_get (&ckpt_pd->iteration_hdb, iteration_handle,
		&iteration_instance_p);
	if (res != 0) {
		hdb_handle_destroy (&ckpt_pd->iteration_hdb, iteration_handle);
		goto error_exit;
	}
	iteration_instance = (struct iteration_instance *)iteration_instance_p;

	iteration_instance->iteration_entries = NULL;
	iteration_instance->iteration_entries_count = 0;
	iteration_instance->iteration_pos = 0;

	memcpy (&iteration_instance->checkpoint_name,
		&req_lib_ckpt_sectioniterationinitialize->checkpoint_name,
		sizeof (mar_name_t));
	iteration_instance->ckpt_id = 
		req_lib_ckpt_sectioniterationinitialize->ckpt_id;

	/*
	 * Iterate list of checkpoint sections
	 */
	for (section_list = checkpoint->sections_list_head.next;
		section_list != &checkpoint->sections_list_head;
		section_list = section_list->next) {

		checkpoint_section = list_entry (section_list,
			struct checkpoint_section, list);

		switch (req_lib_ckpt_sectioniterationinitialize->sections_chosen) {
		case SA_CKPT_SECTIONS_FOREVER:
			if (checkpoint_section->section_descriptor.expiration_time != SA_TIME_END) {
				continue;
			}
			break;
		case SA_CKPT_SECTIONS_LEQ_EXPIRATION_TIME:
			if (checkpoint_section->section_descriptor.expiration_time > req_lib_ckpt_sectioniterationinitialize->expiration_time) {
				continue;
			}
			break;
		case SA_CKPT_SECTIONS_GEQ_EXPIRATION_TIME:
			if (checkpoint_section->section_descriptor.expiration_time < req_lib_ckpt_sectioniterationinitialize->expiration_time) {
				continue;
			}
			break;
		case SA_CKPT_SECTIONS_CORRUPTED:
			/* there can be no corrupted sections - do nothing */
			break;
		case SA_CKPT_SECTIONS_ANY:
			/* iterate all sections - do nothing */
			break;
		}
		iteration_entries = realloc (
			iteration_instance->iteration_entries,
			sizeof (struct iteration_entry) *
				(iteration_instance->iteration_entries_count + 1));
		if (iteration_entries == NULL) {
			error = SA_AIS_ERR_NO_MEMORY;
			goto error_put;
		}
		iteration_instance->iteration_entries = iteration_entries;

		iteration_entries[iteration_instance->iteration_entries_count].section_id =
			malloc (checkpoint_section->section_descriptor.section_id.id_len);
		assert (iteration_entries[iteration_instance->iteration_entries_count].section_id);
		memcpy (iteration_entries[iteration_instance->iteration_entries_count].section_id,
			checkpoint_section->section_descriptor.section_id.id,
			checkpoint_section->section_descriptor.section_id.id_len);
		iteration_entries[iteration_instance->iteration_entries_count].section_id_len = checkpoint_section->section_descriptor.section_id.id_len;
		iteration_instance->iteration_entries_count += 1;
			
	}

error_put:
	hdb_handle_put (&ckpt_pd->iteration_hdb, iteration_handle);

error_exit:
	res_lib_ckpt_sectioniterationinitialize.header.size = sizeof (struct res_lib_ckpt_sectioniterationinitialize);
	res_lib_ckpt_sectioniterationinitialize.header.id = MESSAGE_RES_CKPT_SECTIONITERATIONINITIALIZE;
	res_lib_ckpt_sectioniterationinitialize.header.error = error;
	res_lib_ckpt_sectioniterationinitialize.iteration_handle = iteration_handle;
	res_lib_ckpt_sectioniterationinitialize.max_section_id_size =
		checkpoint->checkpoint_creation_attributes.max_section_id_size;

	api->ipc_conn_send_response (
		conn,
		&res_lib_ckpt_sectioniterationinitialize,
		sizeof (struct res_lib_ckpt_sectioniterationinitialize));
}

static void message_handler_req_lib_ckpt_sectioniterationfinalize (
	void *conn,
	void *msg)
{
	struct req_lib_ckpt_sectioniterationfinalize *req_lib_ckpt_sectioniterationfinalize = (struct req_lib_ckpt_sectioniterationfinalize *)msg;
	struct res_lib_ckpt_sectioniterationfinalize res_lib_ckpt_sectioniterationfinalize;
	SaAisErrorT error = SA_AIS_OK;
	struct iteration_instance *iteration_instance;
	void *iteration_instance_p;
	unsigned int res;

	struct ckpt_pd *ckpt_pd = (struct ckpt_pd *)api->ipc_private_data_get (conn);

	res = hdb_handle_get (&ckpt_pd->iteration_hdb,
		req_lib_ckpt_sectioniterationfinalize->iteration_handle,
		&iteration_instance_p);
	if (res != 0) {
		error = SA_AIS_ERR_LIBRARY;
		goto error_exit;
	}
	iteration_instance = (struct iteration_instance *)iteration_instance_p;

	free (iteration_instance->iteration_entries);

	hdb_handle_put (&ckpt_pd->iteration_hdb,
		req_lib_ckpt_sectioniterationfinalize->iteration_handle);

	hdb_handle_destroy (&ckpt_pd->iteration_hdb,
		req_lib_ckpt_sectioniterationfinalize->iteration_handle);

error_exit:
	res_lib_ckpt_sectioniterationfinalize.header.size = sizeof (struct res_lib_ckpt_sectioniterationfinalize);
	res_lib_ckpt_sectioniterationfinalize.header.id = MESSAGE_RES_CKPT_SECTIONITERATIONFINALIZE;
	res_lib_ckpt_sectioniterationfinalize.header.error = error;

	api->ipc_conn_send_response (
		conn,
		&res_lib_ckpt_sectioniterationfinalize,
		sizeof (struct res_lib_ckpt_sectioniterationfinalize));
}

static void message_handler_req_lib_ckpt_sectioniterationnext (
	void *conn,
	void *msg)
{
	struct req_lib_ckpt_sectioniterationnext *req_lib_ckpt_sectioniterationnext = (struct req_lib_ckpt_sectioniterationnext *)msg;
	struct res_lib_ckpt_sectioniterationnext res_lib_ckpt_sectioniterationnext;
	SaAisErrorT error = SA_AIS_OK;
	int section_id_size = 0;
	unsigned int res;
	struct iteration_instance *iteration_instance = NULL;
	void *iteration_instance_p;
	struct checkpoint *checkpoint;
	struct checkpoint_section *checkpoint_section = NULL;

	struct ckpt_pd *ckpt_pd = (struct ckpt_pd *)api->ipc_private_data_get (conn);

	log_printf (LOG_LEVEL_DEBUG, "section iteration next\n");
	res = hdb_handle_get (&ckpt_pd->iteration_hdb,
		req_lib_ckpt_sectioniterationnext->iteration_handle,
		&iteration_instance_p);
	if (res != 0) {
		error = SA_AIS_ERR_LIBRARY;
		goto error_exit;
	}

	iteration_instance = (struct iteration_instance *)iteration_instance_p;
	assert (iteration_instance);
	/*
	 * Find active iteration entry
	 */
	for (;;) {
		/*
		 * No more sections in iteration
		 */
		if (iteration_instance->iteration_pos == iteration_instance->iteration_entries_count) {
			error = SA_AIS_ERR_NO_SECTIONS;
			goto error_put;
		}

		/*
		 * Find the checkpoint section to respond to library
	 	 */
		checkpoint = checkpoint_find_specific (
			&checkpoint_list_head,
			&iteration_instance->checkpoint_name,
			iteration_instance->ckpt_id);

		assert (checkpoint);

		checkpoint_section = checkpoint_section_find (
			checkpoint,
			iteration_instance->iteration_entries[iteration_instance->iteration_pos].section_id,
			iteration_instance->iteration_entries[iteration_instance->iteration_pos].section_id_len);


		iteration_instance->iteration_pos += 1;
		/*
		 * If checkpoint section found, then return it in iteration
		 */
		if (checkpoint_section) {
			section_id_size = checkpoint_section->section_descriptor.section_id.id_len;

			memcpy (&res_lib_ckpt_sectioniterationnext.section_descriptor,
				&checkpoint_section->section_descriptor,
				sizeof (mar_ckpt_section_descriptor_t));

			/*
			 * This drops out of for loop
			 */
			break;
		}

	}

error_put:
	hdb_handle_put (&ckpt_pd->iteration_hdb, req_lib_ckpt_sectioniterationnext->iteration_handle);

error_exit:
	res_lib_ckpt_sectioniterationnext.header.size = sizeof (struct res_lib_ckpt_sectioniterationnext) + section_id_size;
	res_lib_ckpt_sectioniterationnext.header.id = MESSAGE_RES_CKPT_SECTIONITERATIONNEXT;
	res_lib_ckpt_sectioniterationnext.header.error = error;

	api->ipc_conn_send_response (
		conn,
		&res_lib_ckpt_sectioniterationnext,
		sizeof (struct res_lib_ckpt_sectioniterationnext));

	if (error == SA_AIS_OK) {
		api->ipc_conn_send_response (
			conn,
			checkpoint_section->section_descriptor.section_id.id,
			checkpoint_section->section_descriptor.section_id.id_len);
	}
}

/*
 * Recovery after network partition or merge
 */
void sync_refcount_increment (
	struct checkpoint *checkpoint,
	unsigned int nodeid)
{
	unsigned int i;

	for (i = 0; i < PROCESSOR_COUNT_MAX; i++) {
		if (checkpoint->refcount_set[i].nodeid == 0) {
			checkpoint->refcount_set[i].nodeid = nodeid;
			checkpoint->refcount_set[i].refcount = 1;
			break;
		}
		if (checkpoint->refcount_set[i].nodeid == nodeid) {
			checkpoint->refcount_set[i].refcount += 1;
			break;
		}
	}
}

void sync_refcount_add (
	struct checkpoint *checkpoint,
	unsigned int nodeid,
	unsigned int count)
{
	unsigned int i;

	for (i = 0; i < PROCESSOR_COUNT_MAX; i++) {
		if (checkpoint->refcount_set[i].nodeid == 0) {
			checkpoint->refcount_set[i].nodeid = nodeid;
			checkpoint->refcount_set[i].refcount = count;
			break;
		}
		if (checkpoint->refcount_set[i].nodeid == nodeid) {
			checkpoint->refcount_set[i].refcount += count;
			break;
		}
	}
}

void sync_refcount_decrement (
	struct checkpoint *checkpoint,
	unsigned int nodeid)
{
	unsigned int i;

	for (i = 0; i < PROCESSOR_COUNT_MAX; i++) {
		if (checkpoint->refcount_set[i].nodeid == 0) {
			break;
		}
		if (checkpoint->refcount_set[i].nodeid == nodeid) {
			checkpoint->refcount_set[i].refcount -= 1;
			break;
		}
	}
}

/*
 * Sum all reference counts for the checkpoint
 */
void sync_refcount_calculate (
	struct checkpoint *checkpoint)
{
	checkpoint->reference_count = 0;
	unsigned int i;

	for (i = 0; i < PROCESSOR_COUNT_MAX; i++) {
		if (checkpoint->refcount_set[i].nodeid == 0) {
			break;
		}
		
		checkpoint->reference_count += checkpoint->refcount_set[i].refcount;
	}
}

void sync_checkpoints_free (struct list_head *ckpt_list_head)
{
	struct checkpoint *checkpoint;
	struct list_head *list;

	list = ckpt_list_head->next;
        while (list != ckpt_list_head) {
		checkpoint = list_entry (list, struct checkpoint, list);
		list = list->next;
		checkpoint_release (checkpoint);
	}
	list_init (ckpt_list_head);
}

static inline void sync_checkpoints_enter (void)
{
	struct checkpoint *checkpoint;

	ENTER();

	my_sync_state = SYNC_STATE_CHECKPOINT;
	my_iteration_state = ITERATION_STATE_CHECKPOINT;
	my_iteration_state_checkpoint = checkpoint_list_head.next;

	checkpoint = list_entry (checkpoint_list_head.next, struct checkpoint,
		list);
	my_iteration_state_section = checkpoint->sections_list_head.next;

	LEAVE();
}

static inline void sync_refcounts_enter (void)
{
	my_sync_state = SYNC_STATE_REFCOUNT;
}

static void ckpt_sync_init (void)
{
	ENTER();

	sync_checkpoints_enter();

	LEAVE();
}

static int sync_checkpoint_transmit (struct checkpoint *checkpoint)
{
	struct req_exec_ckpt_sync_checkpoint req_exec_ckpt_sync_checkpoint;
	struct iovec iovec;

	req_exec_ckpt_sync_checkpoint.header.size =
		sizeof (struct req_exec_ckpt_sync_checkpoint);
	req_exec_ckpt_sync_checkpoint.header.id =
		SERVICE_ID_MAKE (CKPT_SERVICE,
			MESSAGE_REQ_EXEC_CKPT_SYNCCHECKPOINT);

	memcpy (&req_exec_ckpt_sync_checkpoint.ring_id,
		&my_saved_ring_id, sizeof (struct memb_ring_id));
		
	memcpy (&req_exec_ckpt_sync_checkpoint.checkpoint_name,
		&checkpoint->name, sizeof (mar_name_t));

	req_exec_ckpt_sync_checkpoint.ckpt_id = checkpoint->ckpt_id;

	memcpy (&req_exec_ckpt_sync_checkpoint.checkpoint_creation_attributes,
		&checkpoint->checkpoint_creation_attributes,
		sizeof (mar_ckpt_checkpoint_creation_attributes_t));

	req_exec_ckpt_sync_checkpoint.active_replica_set =
		checkpoint->active_replica_set;

	req_exec_ckpt_sync_checkpoint.unlinked =
		checkpoint->unlinked;

	iovec.iov_base = (char *)&req_exec_ckpt_sync_checkpoint;
	iovec.iov_len = sizeof (req_exec_ckpt_sync_checkpoint);

	return (api->totem_mcast (&iovec, 1, TOTEM_AGREED));
}

static int sync_checkpoint_section_transmit (
	struct checkpoint *checkpoint,
	struct checkpoint_section *checkpoint_section)
{
	struct req_exec_ckpt_sync_checkpoint_section req_exec_ckpt_sync_checkpoint_section;
	struct iovec iovecs[3];

	ENTER();

	TRACE1 ("transmitting section\n");
	req_exec_ckpt_sync_checkpoint_section.header.size =
		sizeof (struct req_exec_ckpt_sync_checkpoint_section);
	req_exec_ckpt_sync_checkpoint_section.header.id =
		SERVICE_ID_MAKE (CKPT_SERVICE,
			MESSAGE_REQ_EXEC_CKPT_SYNCCHECKPOINTSECTION);

	memcpy (&req_exec_ckpt_sync_checkpoint_section.ring_id,
		&my_saved_ring_id, sizeof (struct memb_ring_id));

	memcpy (&req_exec_ckpt_sync_checkpoint_section.checkpoint_name,
		&checkpoint->name, sizeof (mar_name_t));

	req_exec_ckpt_sync_checkpoint_section.ckpt_id = checkpoint->ckpt_id;

	req_exec_ckpt_sync_checkpoint_section.id_len =
		checkpoint_section->section_descriptor.section_id.id_len;

	req_exec_ckpt_sync_checkpoint_section.section_size = 
		 checkpoint_section->section_descriptor.section_size;

	req_exec_ckpt_sync_checkpoint_section.section_size =
		checkpoint_section->section_descriptor.section_size;

	req_exec_ckpt_sync_checkpoint_section.expiration_time =
		checkpoint_section->section_descriptor.expiration_time;

	iovecs[0].iov_base = (char *)&req_exec_ckpt_sync_checkpoint_section;
	iovecs[0].iov_len = sizeof (req_exec_ckpt_sync_checkpoint_section);
	iovecs[1].iov_base = (char *)checkpoint_section->section_descriptor.section_id.id;
	iovecs[1].iov_len = checkpoint_section->section_descriptor.section_id.id_len;
	iovecs[2].iov_base = checkpoint_section->section_data;
	iovecs[2].iov_len = checkpoint_section->section_descriptor.section_size;

	LEAVE();
	return (api->totem_mcast (iovecs, 3, TOTEM_AGREED));
}

static int sync_checkpoint_refcount_transmit (
	struct checkpoint *checkpoint)
{
	struct req_exec_ckpt_sync_checkpoint_refcount req_exec_ckpt_sync_checkpoint_refcount;
	struct iovec iovec;

	ENTER();

	TRACE1 ("transmitting refcounts for checkpoints\n");
	req_exec_ckpt_sync_checkpoint_refcount.header.size =
		sizeof (struct req_exec_ckpt_sync_checkpoint_refcount);
	req_exec_ckpt_sync_checkpoint_refcount.header.id =
		SERVICE_ID_MAKE (CKPT_SERVICE,
			MESSAGE_REQ_EXEC_CKPT_SYNCCHECKPOINTREFCOUNT);

	memcpy (&req_exec_ckpt_sync_checkpoint_refcount.ring_id,
		&my_saved_ring_id, sizeof (struct memb_ring_id));

	memcpy (&req_exec_ckpt_sync_checkpoint_refcount.checkpoint_name,
		&checkpoint->name, sizeof (mar_name_t));

	req_exec_ckpt_sync_checkpoint_refcount.ckpt_id = checkpoint->ckpt_id;

	marshall_to_mar_refcount_set_t_all (
		req_exec_ckpt_sync_checkpoint_refcount.refcount_set,
		checkpoint->refcount_set);
	
	iovec.iov_base = (char *)&req_exec_ckpt_sync_checkpoint_refcount;
	iovec.iov_len = sizeof (struct req_exec_ckpt_sync_checkpoint_refcount);

	LEAVE();
	return (api->totem_mcast (&iovec, 1, TOTEM_AGREED));
}

unsigned int sync_checkpoints_iterate (void)
{
	struct checkpoint *checkpoint;
	struct checkpoint_section *checkpoint_section;
	struct list_head *checkpoint_list;
	struct list_head *section_list;
	unsigned int res = 0;

	for (checkpoint_list = checkpoint_list_head.next;
		checkpoint_list != &checkpoint_list_head;
		checkpoint_list = checkpoint_list->next) {

		checkpoint = list_entry (checkpoint_list, struct checkpoint, list);

		res = sync_checkpoint_transmit (checkpoint);
		if (res != 0) {
			break;
		}
		for (section_list = checkpoint->sections_list_head.next;
			section_list != &checkpoint->sections_list_head;
			section_list = section_list->next) {

			checkpoint_section = list_entry (section_list, struct checkpoint_section, list);
			res = sync_checkpoint_section_transmit (checkpoint, checkpoint_section);
		}
	}
	return (res);
}

unsigned int sync_refcounts_iterate (void)
{
	struct checkpoint *checkpoint;
	struct list_head *list;
	unsigned int res = 0;

	for (list = checkpoint_list_head.next;
		list != &checkpoint_list_head;
		list = list->next) {

		checkpoint = list_entry (list, struct checkpoint, list);

		res = sync_checkpoint_refcount_transmit (checkpoint);
		if (res != 0) {
			break;
		}
	}
	return (res);
}

static int ckpt_sync_process (void)
{
	unsigned int done_queueing = 1;
	unsigned int continue_processing = 0;
	unsigned int res;

	ENTER();

	switch (my_sync_state) {
	case SYNC_STATE_CHECKPOINT:
		if (my_lowest_nodeid == api->totem_nodeid_get ()) {
			TRACE1 ("should transmit checkpoints because lowest member in old configuration.\n");
			res = sync_checkpoints_iterate ();

			if (res == 0) { 
				done_queueing = 1;
			}
		}
		if (done_queueing) {
			sync_refcounts_enter ();
		}

		/*
		 * TODO recover current iteration state
		 */
		continue_processing = 1;
		break;

	case SYNC_STATE_REFCOUNT:
		done_queueing = 1;
		if (my_lowest_nodeid == api->totem_nodeid_get()) {
			TRACE1 ("transmit refcounts because this processor is the lowest member in old configuration.\n");
			res = sync_refcounts_iterate ();
		}
		if (done_queueing) {
			continue_processing = 0;
		}
		break;
	}

	LEAVE();
	return (continue_processing);
}

static void ckpt_sync_activate (void)
{
	ENTER();

	sync_checkpoints_free (&checkpoint_list_head);

	list_init (&checkpoint_list_head);

	if (!list_empty (&sync_checkpoint_list_head)) {
		list_splice (&sync_checkpoint_list_head, &checkpoint_list_head);
	}

	list_init (&sync_checkpoint_list_head);

	my_sync_state = SYNC_STATE_CHECKPOINT;

	LEAVE();
}

static void ckpt_sync_abort (void)
{
	sync_checkpoints_free (&sync_checkpoint_list_head);
}

static void message_handler_req_exec_ckpt_sync_checkpoint (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_ckpt_sync_checkpoint *req_exec_ckpt_sync_checkpoint =
		(struct req_exec_ckpt_sync_checkpoint *)message;
	struct checkpoint *checkpoint = 0;

	ENTER();

	/*
	 * Ignore messages from previous ring ids
	 */
	if (memcmp (&req_exec_ckpt_sync_checkpoint->ring_id,
		&my_saved_ring_id, sizeof (struct memb_ring_id)) != 0) {
		return;
	}

	checkpoint = checkpoint_find_specific (
		&sync_checkpoint_list_head,
		&req_exec_ckpt_sync_checkpoint->checkpoint_name,
		req_exec_ckpt_sync_checkpoint->ckpt_id);

	/*
	 * If checkpoint doesn't exist, create one
	 */
	if (checkpoint == 0) {
		checkpoint = malloc (sizeof (struct checkpoint));
		if (checkpoint == 0) {
			LEAVE();
#ifdef TODO
			openais_exit_error (AIS_DONE_OUT_OF_MEMORY);
#endif
		}
		memset (checkpoint, 0, sizeof (struct checkpoint));

		memcpy (&checkpoint->name,
			&req_exec_ckpt_sync_checkpoint->checkpoint_name,
			sizeof (mar_name_t));

		memcpy (&checkpoint->checkpoint_creation_attributes,
			&req_exec_ckpt_sync_checkpoint->checkpoint_creation_attributes,
			sizeof (mar_ckpt_checkpoint_creation_attributes_t));

		memset (&checkpoint->refcount_set, 0, sizeof (struct refcount_set) * PROCESSOR_COUNT_MAX);
		checkpoint->ckpt_id = req_exec_ckpt_sync_checkpoint->ckpt_id;

		checkpoint->active_replica_set = req_exec_ckpt_sync_checkpoint->active_replica_set;

		checkpoint->unlinked = req_exec_ckpt_sync_checkpoint->unlinked;
		checkpoint->reference_count = 0;
		checkpoint->retention_timer = 0;
		checkpoint->section_count = 0;

		list_init (&checkpoint->list);
		list_init (&checkpoint->sections_list_head);
		list_add (&checkpoint->list, &sync_checkpoint_list_head);

		memset (checkpoint->refcount_set, 0,
			sizeof (struct refcount_set) * PROCESSOR_COUNT_MAX);
	}

	if (checkpoint->ckpt_id >= global_ckpt_id) {
		global_ckpt_id = checkpoint->ckpt_id + 1;
	}

	LEAVE();
}

static void message_handler_req_exec_ckpt_sync_checkpoint_section (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_ckpt_sync_checkpoint_section *req_exec_ckpt_sync_checkpoint_section =
		(struct req_exec_ckpt_sync_checkpoint_section *)message;
	struct checkpoint *checkpoint;
	struct checkpoint_section *checkpoint_section;
	char *section_contents;
	char *section_id;

	ENTER();

	/*
	 * Ignore messages from previous ring ids
	 */
	if (memcmp (&req_exec_ckpt_sync_checkpoint_section->ring_id,
		&my_saved_ring_id, sizeof (struct memb_ring_id)) != 0) {
		LEAVE();
		return;
	}

	checkpoint = checkpoint_find_specific (
		&sync_checkpoint_list_head,
		&req_exec_ckpt_sync_checkpoint_section->checkpoint_name,
		req_exec_ckpt_sync_checkpoint_section->ckpt_id);

	assert (checkpoint != NULL);

	/*
	 * Determine if user-specified checkpoint section already exists
	 */
	checkpoint_section = checkpoint_section_find (
		checkpoint,
		((char *)req_exec_ckpt_sync_checkpoint_section) +
			sizeof (struct req_exec_ckpt_sync_checkpoint_section),
		req_exec_ckpt_sync_checkpoint_section->id_len);
	if (checkpoint_section == NULL) {
		/*
		 * Allocate checkpoint section
		 */
		checkpoint_section = malloc (sizeof (struct checkpoint_section));
		if (checkpoint_section == 0) {
			LEAVE();
#ifdef TODO
			openais_exit_error (AIS_DONE_OUT_OF_MEMORY);
#endif
		}
		section_contents = malloc (req_exec_ckpt_sync_checkpoint_section->section_size);
		if (section_contents == 0) {
			free (checkpoint_section);
			LEAVE();
#ifdef TODO
			openais_exit_error (AIS_DONE_OUT_OF_MEMORY);
#endif
		}
		if (req_exec_ckpt_sync_checkpoint_section->id_len) {
			
			section_id = malloc (req_exec_ckpt_sync_checkpoint_section->id_len + 1);
			if (section_id == 0) {
				free (checkpoint_section);
				free (section_contents);
				LEAVE();
#ifdef TODO
				openais_exit_error (AIS_DONE_OUT_OF_MEMORY);
#endif
			}

			/*
			 * Copy checkpoint section and section ID
			 */
			memcpy (section_id,
				((char *)req_exec_ckpt_sync_checkpoint_section) +
				sizeof (struct req_exec_ckpt_sync_checkpoint_section),
			req_exec_ckpt_sync_checkpoint_section->id_len);

			/*
			 * Null terminate the section id for printing purposes
			 */
			((char*)(section_id))[req_exec_ckpt_sync_checkpoint_section->id_len] = '\0';

		} else {
			/*
			 * Default section
			 */
			section_id = NULL;
		}
		
		memcpy (section_contents,
		((char *)req_exec_ckpt_sync_checkpoint_section) +
			sizeof (struct req_exec_ckpt_sync_checkpoint_section) +
			req_exec_ckpt_sync_checkpoint_section->id_len,
		req_exec_ckpt_sync_checkpoint_section->section_size);

		/*
		 * Configure checkpoint section
		 */
		checkpoint_section->section_descriptor.section_id.id = (unsigned char *)section_id;
		checkpoint_section->section_descriptor.section_id.id_len =
			req_exec_ckpt_sync_checkpoint_section->id_len;
		checkpoint_section->section_descriptor.section_size =
			req_exec_ckpt_sync_checkpoint_section->section_size;
		checkpoint_section->section_descriptor.expiration_time =
			req_exec_ckpt_sync_checkpoint_section->expiration_time;
		checkpoint_section->section_descriptor.section_state =
			SA_CKPT_SECTION_VALID;
		checkpoint_section->section_descriptor.last_update = 0; /* TODO current time */
		checkpoint_section->section_data = section_contents;
		checkpoint_section->expiration_timer = 0;

		/*
		 * Add checkpoint section to checkpoint
		 */
		list_init (&checkpoint_section->list);
		list_add (&checkpoint_section->list,
			&checkpoint->sections_list_head);
		checkpoint->section_count += 1;
	}

	LEAVE();
}

static void message_handler_req_exec_ckpt_sync_checkpoint_refcount (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_ckpt_sync_checkpoint_refcount *req_exec_ckpt_sync_checkpoint_refcount
		= (struct req_exec_ckpt_sync_checkpoint_refcount *)message;
	struct checkpoint *checkpoint;
	unsigned int i, j;

	ENTER();

	/*
	 * Ignore messages from previous ring ids
	 */
	if (memcmp (&req_exec_ckpt_sync_checkpoint_refcount->ring_id,
		&my_saved_ring_id, sizeof (struct memb_ring_id)) != 0) {
		LEAVE();
		return;
	}

	checkpoint = checkpoint_find_specific (
		&sync_checkpoint_list_head,
		&req_exec_ckpt_sync_checkpoint_refcount->checkpoint_name,
		req_exec_ckpt_sync_checkpoint_refcount->ckpt_id);

	assert (checkpoint != NULL);

	for (i = 0; i < PROCESSOR_COUNT_MAX; i++) {
		if (req_exec_ckpt_sync_checkpoint_refcount->refcount_set[i].nodeid == 0) {
			break;
		}
		for (j = 0; j < PROCESSOR_COUNT_MAX; j++) {
			if (checkpoint->refcount_set[j].nodeid == 0) {
				checkpoint->refcount_set[j].nodeid =
					req_exec_ckpt_sync_checkpoint_refcount->refcount_set[i].nodeid;
				checkpoint->refcount_set[j].refcount =
					req_exec_ckpt_sync_checkpoint_refcount->refcount_set[i].refcount;
				/*
				 * No match found, added processor reference count
				 */
				break;
			}

			if (req_exec_ckpt_sync_checkpoint_refcount->refcount_set[i].nodeid == checkpoint->refcount_set[j].nodeid) {
				checkpoint->refcount_set[j].refcount +=
					req_exec_ckpt_sync_checkpoint_refcount->refcount_set[i].refcount;
				/*
				 * Found match, so look at next processor ref count
				 */
				break;
			}
		}
	}

	sync_refcount_calculate (checkpoint);

	LEAVE();
}


static void ckpt_dump_fn (void)
{
	struct list_head *checkpoint_list;
	struct checkpoint *checkpoint;
	struct list_head *checkpoint_section_list;
	struct checkpoint_section *section;

	log_printf (LOG_LEVEL_NOTICE,
		"========== Checkpoint Information ===========");
	log_printf (LOG_LEVEL_NOTICE, "global_ckpt_id: %u", global_ckpt_id);

	for (checkpoint_list = checkpoint_list_head.next;
		checkpoint_list != &checkpoint_list_head;
		checkpoint_list = checkpoint_list->next) {

		checkpoint = list_entry (checkpoint_list, struct checkpoint, list);

		if (checkpoint == NULL) {
			return;
		}

		log_printf (LOG_LEVEL_NOTICE, "Checkpoint %s (%d):",
			checkpoint->name.value, checkpoint->name.length);
		log_printf (LOG_LEVEL_NOTICE, "   id:       %u", checkpoint->ckpt_id);
		log_printf (LOG_LEVEL_NOTICE, "   sec cnt:  %u", checkpoint->section_count);
		log_printf (LOG_LEVEL_NOTICE, "   ref cnt:  %u", checkpoint->reference_count);
		log_printf (LOG_LEVEL_NOTICE, "   unlinked: %u", checkpoint->unlinked);

		for (checkpoint_section_list = checkpoint->sections_list_head.next;
			checkpoint_section_list != &checkpoint->sections_list_head;
			checkpoint_section_list = checkpoint_section_list->next) {

			section = list_entry (checkpoint_section_list,
				struct checkpoint_section, list);

			log_printf (LOG_LEVEL_NOTICE, "   Section %s (%d)",
				section->section_descriptor.section_id.id,
				section->section_descriptor.section_id.id_len);
			log_printf (LOG_LEVEL_NOTICE, "      size:     %llu",
				section->section_descriptor.section_size);
			log_printf (LOG_LEVEL_NOTICE, "      state:    %u",
				section->section_descriptor.section_state);
			log_printf (LOG_LEVEL_NOTICE, "      exp time: %llu",
				section->section_descriptor.expiration_time);
		}
	}
}
