/*
 * Copyright (c) 2003-2006 MontaVista Software, Inc.
 * Copyright (c) 2006 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Authors: Steven Dake (sdake@mvista.com)
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

#include "../include/saAis.h"
#include "../include/saCkpt.h"
#include "../include/mar_ckpt.h"
#include "../include/ipc_ckpt.h"
#include "../include/list.h"
#include "../include/queue.h"
#include "../include/hdb.h"
#include "../lcr/lcr_comp.h"
#include "service.h"
#include "mempool.h"
#include "tlist.h"
#include "timer.h"
#include "util.h"
#include "main.h"
#include "ipc.h"
#include "totempg.h"
#include "print.h"

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
	MESSAGE_REQ_EXEC_CKPT_SYNCHRONIZESTATE = 11,
	MESSAGE_REQ_EXEC_CKPT_SYNCHRONIZESECTION = 12
};

struct checkpoint_section {
	struct list_head list;
	mar_ckpt_section_descriptor_t section_descriptor;
	void *section_data;
	timer_handle expiration_timer;
};

struct ckpt_refcnt {
	int count;
	unsigned int nodeid;
};

typedef struct {
	int count __attribute__((aligned(8)));
	unsigned int nodeid __attribute__((aligned(8)));
} mar_ckpt_refcnt_t;

static inline void marshall_to_mar_ckpt_refcnt_t (
	mar_ckpt_refcnt_t *dest,
	struct ckpt_refcnt *src)
{
	dest->count = src->count;
	dest->nodeid = src->nodeid;
}

static inline void marshall_from_mar_ckpt_refcnt_t (
	struct ckpt_refcnt *dest,
	mar_ckpt_refcnt_t *src)
{
	dest->count = src->count;
	dest->nodeid = src->nodeid;
}

static inline void swab_mar_ckpt_refcnt_t (mar_ckpt_refcnt_t *to_swab)
{
	swab_mar_int32_t (&to_swab->count);
	swab_mar_uint32_t (&to_swab->nodeid);
}

struct checkpoint {
	struct list_head list;
	mar_name_t name;
	mar_ckpt_checkpoint_creation_attributes_t checkpoint_creation_attributes;
	struct list_head sections_list_head;
	int referenceCount;
	int unlinked;
	timer_handle retention_timer;
	int expired;
	int active_replica_set;
	int sectionCount;
	struct ckpt_refcnt ckpt_refcnt[PROCESSOR_COUNT_MAX];
};

struct iteration_entry {
	int active;
	struct checkpoint_section *checkpoint_section;
};

struct iteration_instance {
	struct iteration_entry *iteration_entries;
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
	mar_ckpt_section_id_t ckpt_section_id;	
};

/* TODO static totempg_recovery_plug_handle ckpt_checkpoint_recovery_plug_handle; */

static int ckpt_exec_init_fn (struct objdb_iface_ver0 *);

static int ckpt_lib_exit_fn (void *conn);

static int ckpt_lib_init_fn (void *conn);

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

static void message_handler_req_exec_ckpt_synchronize_state (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_ckpt_synchronize_section (
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
static void exec_ckpt_synchronize_state_endian_convert (void *msg);
static void exec_ckpt_synchronize_section_endian_convert (void *msg);

static void ckpt_recovery_activate (void);
static void ckpt_recovery_initialize (void);
static int  ckpt_recovery_process (void);
static void ckpt_recovery_finalize (void);
static void ckpt_recovery_abort(void);
static void ckpt_recovery_process_members_exit (
	unsigned int *left_list,
	int left_list_entries); 
static void ckpt_replace_localhost_ip (unsigned int *joined_list);

void checkpoint_release (struct checkpoint *checkpoint);
void timer_function_retention (void *data);
unsigned int abstime_to_msec (mar_time_t time);
void timer_function_section_expire (void *data);
void clean_checkpoint_list(struct list_head* head);

static int recovery_checkpoint_open(
	mar_name_t *checkpoint_name,
	mar_ckpt_checkpoint_creation_attributes_t *ckptAttributes,
	struct ckpt_refcnt *ref_cnt);

static int recovery_section_create (
	mar_ckpt_section_descriptor_t *section_descriptor, 
	mar_name_t *checkpoint_name,
	char* section_id);

static int recovery_section_write(
	int section_id_len, char *section_id, mar_name_t *checkpoint_name,
	void *new_data, mar_uint32_t data_offset, mar_uint32_t data_size);
									
static int process_localhost_transition = 0;

DECLARE_LIST_INIT(checkpoint_list_head);

DECLARE_LIST_INIT(checkpoint_iteration_list_head);

DECLARE_LIST_INIT(checkpoint_recovery_list_head);

struct checkpoint_cleanup {
    struct list_head list;
    struct checkpoint checkpoint;
};

typedef enum {
	SYNCHRONY_STATE_STARTED,
	SYNCHRONY_STATE_ENDED
}synchrony_state;


static synchrony_state recovery_state = SYNCHRONY_STATE_ENDED;

static struct list_head *recovery_ckpt_next = 0;
static struct list_head *recovery_ckpt_section_next = 0;
static int recovery_section_data_offset = 0;
static int recovery_section_send_flag = 0;
static int recovery_abort = 0;

static struct memb_ring_id saved_ring_id;

static void ckpt_confchg_fn (
	enum totem_configuration_type configuration_type,
	unsigned int *member_list, int member_list_entries,
	unsigned int *left_list, int left_list_entries,
	unsigned int *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id);

/*
 * Executive Handler Definition
 */
static struct openais_lib_handler ckpt_lib_service[] =
{
	{ /* 0 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_checkpointopen,
		.response_size		= sizeof (struct res_lib_ckpt_checkpointopen),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTOPEN,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 1 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_checkpointclose,
		.response_size		= sizeof (struct res_lib_ckpt_checkpointclose),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTCLOSE,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 2 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_checkpointunlink,
		.response_size		= sizeof (struct res_lib_ckpt_checkpointunlink),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTUNLINK,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 3 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_checkpointretentiondurationset,
		.response_size		= sizeof (struct res_lib_ckpt_checkpointretentiondurationset),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTRETENTIONDURATIONSET,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 4 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_activereplicaset,
		.response_size		= sizeof (struct res_lib_ckpt_activereplicaset),
		.response_id		= MESSAGE_RES_CKPT_ACTIVEREPLICASET,
		.flow_control		= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 5 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_checkpointstatusget,
		.response_size		= sizeof (struct res_lib_ckpt_checkpointstatusget),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTSTATUSGET,
		.flow_control		= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 6 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_sectioncreate,
		.response_size		= sizeof (struct res_lib_ckpt_sectioncreate),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_SECTIONCREATE,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 7 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_sectiondelete,
		.response_size		= sizeof (struct res_lib_ckpt_sectiondelete),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_SECTIONDELETE,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 8 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_sectionexpirationtimeset,
		.response_size		= sizeof (struct res_lib_ckpt_sectionexpirationtimeset),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_SECTIONEXPIRATIONTIMESET,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 9 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_sectionwrite,
		.response_size		= sizeof (struct res_lib_ckpt_sectionwrite),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_SECTIONWRITE,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 10 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_sectionoverwrite,
		.response_size		= sizeof (struct res_lib_ckpt_sectionoverwrite),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_SECTIONOVERWRITE,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 11 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_sectionread,
		.response_size		= sizeof (struct res_lib_ckpt_sectionread),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_SECTIONREAD,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 12 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_checkpointsynchronize,
		.response_size		= sizeof (struct res_lib_ckpt_checkpointsynchronize),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTSYNCHRONIZE,
		.flow_control		= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 13 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_checkpointsynchronizeasync,
		.response_size		= sizeof (struct res_lib_ckpt_checkpointsynchronizeasync), /* TODO RESPONSE */
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTSYNCHRONIZEASYNC,
		.flow_control		= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 14 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_sectioniterationinitialize,
		.response_size		= sizeof (struct res_lib_ckpt_sectioniterationinitialize),
		.response_id		= MESSAGE_RES_CKPT_SECTIONITERATIONINITIALIZE,
		.flow_control		= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 15 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_sectioniterationfinalize,
		.response_size		= sizeof (struct res_lib_ckpt_sectioniterationfinalize),
		.response_id		= MESSAGE_RES_CKPT_SECTIONITERATIONFINALIZE,
		.flow_control		= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 16 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_sectioniterationnext,
		.response_size		= sizeof (struct res_lib_ckpt_sectioniterationnext),
		.response_id		= MESSAGE_RES_CKPT_SECTIONITERATIONNEXT,
		.flow_control		= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	}
};


static struct openais_exec_handler ckpt_exec_service[] = {
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
		.exec_handler_fn	= message_handler_req_exec_ckpt_synchronize_state,
		.exec_endian_convert_fn = exec_ckpt_synchronize_state_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_ckpt_synchronize_section,
		.exec_endian_convert_fn = exec_ckpt_synchronize_section_endian_convert
	}
};

struct openais_service_handler ckpt_service_handler = {
	.name				= (unsigned char *)"openais checkpoint service B.01.01",
	.id				= CKPT_SERVICE,
	.private_data_size		= sizeof (struct ckpt_pd),
	.lib_init_fn			= ckpt_lib_init_fn,
	.lib_exit_fn			= ckpt_lib_exit_fn,
	.lib_service			= ckpt_lib_service,
	.lib_service_count		= sizeof (ckpt_lib_service) / sizeof (struct openais_lib_handler),
	.exec_init_fn			= ckpt_exec_init_fn,
	.exec_dump_fn			= 0,
	.exec_service			= ckpt_exec_service,
	.exec_service_count		= sizeof (ckpt_exec_service) / sizeof (struct openais_exec_handler),
	.confchg_fn			= ckpt_confchg_fn,
	.sync_init			= ckpt_recovery_initialize,
	.sync_process			= ckpt_recovery_process,
	.sync_activate			= ckpt_recovery_activate,
	.sync_abort			= ckpt_recovery_abort,
};

/*
 * Dynamic loader definition
 */
static struct openais_service_handler *ckpt_get_handler_ver0 (void);

static struct openais_service_handler_iface_ver0 ckpt_service_handler_iface = {
	.openais_get_service_handler_ver0	= ckpt_get_handler_ver0
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

static struct openais_service_handler *ckpt_get_handler_ver0 (void)
{
	return (&ckpt_service_handler);
}

__attribute__ ((constructor)) static void register_this_component (void) {
	lcr_interfaces_set (&openais_ckpt_ver0[0], &ckpt_service_handler_iface);

	lcr_component_register (&ckpt_comp_ver0);
}

/*
 * All data types used for executive messages
 */
struct req_exec_ckpt_checkpointopen {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
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
};

struct req_exec_ckpt_checkpointretentiondurationset {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_time_t retention_duration __attribute__((aligned(8)));
};

struct req_exec_ckpt_checkpointretentiondurationexpire {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
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
	mar_uint32_t id_len __attribute__((aligned(8)));
	mar_time_t expiration_time __attribute__((aligned(8)));
	mar_uint32_t initial_data_size __attribute__((aligned(8)));
};

struct req_exec_ckpt_sectiondelete {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_uint32_t id_len __attribute__((aligned(8)));
};

struct req_exec_ckpt_sectionexpirationtimeset {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_uint32_t id_len __attribute__((aligned(8)));
	mar_time_t expiration_time __attribute__((aligned(8)));
};

struct req_exec_ckpt_sectionwrite {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_uint32_t id_len __attribute__((aligned(8)));
	mar_offset_t data_offset __attribute__((aligned(8)));
	mar_offset_t data_size __attribute__((aligned(8)));
};

struct req_exec_ckpt_sectionoverwrite {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_uint32_t id_len __attribute__((aligned(8)));
	mar_offset_t data_size __attribute__((aligned(8)));
};

struct req_exec_ckpt_sectionread {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_uint32_t id_len __attribute__((aligned(8)));
	mar_offset_t data_offset __attribute__((aligned(8)));
	mar_offset_t data_size __attribute__((aligned(8)));
};

struct req_exec_ckpt_synchronize_state {
	mar_req_header_t header __attribute__((aligned(8)));
	struct memb_ring_id previous_ring_id __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_ckpt_checkpoint_creation_attributes_t checkpoint_creation_attributes __attribute__((aligned(8)));
	mar_ckpt_section_descriptor_t section_descriptor __attribute__((aligned(8)));
	mar_uint32_t nodeid __attribute__((aligned(8)));
	mar_ckpt_refcnt_t ckpt_refcnt[PROCESSOR_COUNT_MAX] __attribute__((aligned(8)));
};

struct req_exec_ckpt_synchronize_section {
	mar_req_header_t header __attribute__((aligned(8)));
	struct memb_ring_id previous_ring_id __attribute__((aligned(8)));
	mar_name_t checkpoint_name __attribute__((aligned(8)));
	mar_uint32_t id_len __attribute__((aligned(8)));
	mar_offset_t data_offset __attribute__((aligned(8)));
	mar_offset_t data_size __attribute__((aligned(8)));	
};

/* 
 * Implementation
 */
static int processor_index_set(
	unsigned int nodeid,
	struct ckpt_refcnt *ckpt_refcnt) 
{
	int i;
	for (i = 0; i < PROCESSOR_COUNT_MAX; i ++) {
		if (ckpt_refcnt[i].nodeid == 0) {
			/*
			 * If the source addresses do not match and this element
			 * has no stored value then store the new value and 
			 * return the Index.
		 	 */		
			ckpt_refcnt[i].nodeid = nodeid;
			return i;
		}
		/*
		* If the source addresses match then this processor index
		* has already been set
		*/
		else
		if (ckpt_refcnt[i].nodeid == nodeid) {
			return -1;
		}

	}
	/* 
	 * Could not Find an empty slot 
	 * to store the new Processor.	
	 */
	for (i = 0; i < PROCESSOR_COUNT_MAX; i ++) {
		log_printf (LOG_LEVEL_ERROR,"Processor Set: Index %d has proc %s and count %d\n",
			i,
			totempg_ifaces_print (ckpt_refcnt[i].nodeid),
			ckpt_refcnt[i].count);
        }

	return -1;
}

static int processor_add (
	unsigned int nodeid,
	int count,
	struct ckpt_refcnt *ckpt_refcnt) 
{
	int i;
        for (i = 0; i < PROCESSOR_COUNT_MAX; i ++) {
                if (ckpt_refcnt[i].nodeid == 0) {
			log_printf (LOG_LEVEL_DEBUG,"processor_add found empty slot to insert new item\n");
			ckpt_refcnt[i].nodeid = nodeid;
			ckpt_refcnt[i].count = count;
                        return i;
                }
		/*Dont know how we missed this in the processor find but update this*/
		else
		if (ckpt_refcnt[i].nodeid == nodeid) {
			ckpt_refcnt[i].count += count;
			log_printf (LOG_LEVEL_DEBUG,"processor_add for existent proc. nodeid %s, New count = %d\n",
				totempg_ifaces_print (ckpt_refcnt[i].nodeid),
				ckpt_refcnt[i].count);

			return i;
		}
	}
        /*
         * Could not Find an empty slot
         * to store the new Processor.
         */
	log_printf (LOG_LEVEL_ERROR,"Processor Add Failed. Dumping Refcount Array\n");
	for (i = 0; i < PROCESSOR_COUNT_MAX; i ++) {
		log_printf (LOG_LEVEL_ERROR,"Processor Add: Index %d has proc %s and count %d\n",
			i,
			totempg_ifaces_print (ckpt_refcnt[i].nodeid),
			ckpt_refcnt[i].count);
	}
        return -1;

}

static int processor_index_find(
	unsigned int nodeid,
	struct ckpt_refcnt *ckpt_refcnt) 
{ 
	int i;
	for (i = 0; i < PROCESSOR_COUNT_MAX; i ++) {
		/*
		 * If the source addresses match then return the index
		 */
		
		if (ckpt_refcnt[i].nodeid ==  nodeid) {
			return i;
		}				
	}
	/* 
	 * Could not Find the Processor 
	 */
	return -1;
}

static int ckpt_refcnt_total(struct ckpt_refcnt *ckpt_refcnt) 
{
	int i;
	int total = 0;
	for (i = 0; i < PROCESSOR_COUNT_MAX; i ++) {
		total += ckpt_refcnt[i].count;
	}
	return total;
}

static void initialize_ckpt_refcnt_array (
	struct ckpt_refcnt *ckpt_refcnt) 
{
	memset((char*)ckpt_refcnt, 0,
		PROCESSOR_COUNT_MAX * sizeof(struct ckpt_refcnt));
}

static void merge_ckpt_refcnts (
	struct ckpt_refcnt *local,
	struct ckpt_refcnt *network)
{
	int index,i;	

	for (i = 0; i < PROCESSOR_COUNT_MAX; i ++) {
		index  = processor_index_find (local[i].nodeid, network);
		if (index == -1) { /*Could Not Find the Local Entry in the remote.Add to it*/
			log_printf (LOG_LEVEL_DEBUG,"calling processor_add for nodeid %s, count %d\n",
				totempg_ifaces_print (local[i].nodeid),
				local[i].count);
			index = processor_add (local[i].nodeid, local[i].count, network);
			if (index == -1) {
				log_printf(LOG_LEVEL_ERROR,
					"merge_ckpt_refcnts : could not add a new processor as the MAX limit of procs is reached.Exiting\n");
				assert(0);
			}
		}
		else {
			if (local[i].count == network[index].count) {
				/*Nothing to do here as the network is already up 2 date*/
				log_printf (LOG_LEVEL_DEBUG,"merge_ckpt_refcnts counts match, continue\n");
				continue;
			}
			else {
				/*Found a match for this proc in the Network choose the larger of the 2.*/
				network[index].count += local[i].count; 
				log_printf (LOG_LEVEL_DEBUG,"setting count for nodeid %s = %d\n",
					totempg_ifaces_print (network[index].nodeid),
					network[index].count);
			}
		}
	}
}


static void ckpt_recovery_initialize (void) 
{
	struct list_head *checkpoint_list;
	struct list_head *checkpoint_section_list;
	struct checkpoint *checkpoint;
	struct checkpoint_section *section;
	struct checkpoint *savedCheckpoint;	
	struct checkpoint_section *savedSection;
	
	if (recovery_abort) { /*Abort was called.*/
		return;
	}	

	/*
	* Save off the existing Checkpoints to be used by ckpt_recovery_process 
	*/
	for (checkpoint_list = checkpoint_list_head.next;
		checkpoint_list != &checkpoint_list_head;
		checkpoint_list = checkpoint_list->next) {
					
		checkpoint = list_entry (checkpoint_list,
								struct checkpoint, list);
            
		if (checkpoint->referenceCount < 1) { /*defect 1192*/
			log_printf (LOG_LEVEL_DEBUG, "ckpt_recovery_initialize checkpoint %s has referenceCount < 1 ignoring.\n", 
										(char*)&checkpoint->name.value);							
			continue;
		}
		savedCheckpoint = 
			(struct checkpoint *) malloc (sizeof(struct checkpoint));
		assert(savedCheckpoint);
		memcpy(savedCheckpoint, checkpoint, sizeof(struct checkpoint));
		list_init(&savedCheckpoint->list);		
		list_add_tail(&savedCheckpoint->list,&checkpoint_recovery_list_head);
		list_init(&savedCheckpoint->sections_list_head);
		for (checkpoint_section_list = checkpoint->sections_list_head.next;
			checkpoint_section_list != &checkpoint->sections_list_head;
			checkpoint_section_list = checkpoint_section_list->next) {
			section = list_entry (checkpoint_section_list,
								struct checkpoint_section, list);
			savedSection = 
				(struct checkpoint_section *) malloc (sizeof(struct checkpoint_section));
			assert(savedSection);
			openais_timer_delete_data (section->expiration_timer);
			memcpy(savedSection, section, sizeof(struct checkpoint_section));
			list_init(&savedSection->list);		
			list_add_tail(&savedSection->list,&savedCheckpoint->sections_list_head);
		}
	}
	
	if (list_empty (&checkpoint_recovery_list_head)) {
		return;
	}
	recovery_ckpt_next = checkpoint_recovery_list_head.next;
	savedCheckpoint = list_entry (recovery_ckpt_next,
								struct checkpoint, list);
	recovery_ckpt_section_next = savedCheckpoint->sections_list_head.next;
}

static int ckpt_recovery_process (void) 
{
	struct req_exec_ckpt_synchronize_state request_exec_sync_state;
	struct req_exec_ckpt_synchronize_section request_exec_sync_section;
	struct iovec iovecs[3];
	struct checkpoint *checkpoint;	
	struct checkpoint_section *checkpoint_section;
	mar_size_t origSectionSize;  
	mar_size_t newSectionSize;
	int res;
	unsigned int i;

	if (recovery_abort) { /*Abort was called.*/
		goto recovery_exit_clean;
	}
	/*So Initialize did not have any checkpoints to Synchronize*/
	if ((recovery_ckpt_next == 0) && (recovery_ckpt_section_next == 0)) {
		log_printf (LOG_LEVEL_DEBUG, "ckpt_recovery_process Nothing to Process ...\n");
		goto recovery_exit_clean; 
	}
	
	/*
	 * ALGORITHM :
	 * 1.) extract the checkpoint if there.
	 * 2.) If there is a checkpoint then there has to be a section
	 * 3.) If the recovery_section_send_flag was not set in the previous
	 * 		invocation that means we have to send out a sync_msg before
	 * 		we send out the sections
	 * 4.) Set the recovery_section_send_flag and send the sections.
	 */
	
	while (1) { /*Go for as long as the oubound queue is not full*/
		
		if(recovery_ckpt_next != &checkpoint_recovery_list_head) {				
			checkpoint = list_entry (recovery_ckpt_next,
							struct checkpoint, list);
			if (recovery_ckpt_section_next == 0) {
				recovery_ckpt_section_next = checkpoint->sections_list_head.next;
			}
			if (recovery_ckpt_section_next != &checkpoint->sections_list_head) {
				checkpoint_section = list_entry (recovery_ckpt_section_next,
					struct checkpoint_section, list);

				/*
		 		* None of the section data msgs have been sent
		 		* so lets start with sending the sync_msg
		 		*/	
				if (recovery_section_send_flag == 0) {
					if (checkpoint_section->section_descriptor.section_id.id) {
						log_printf (LOG_LEVEL_DEBUG, "New Sync State Message for ckpt = %s, section = %s.\n", 
							(char*)&checkpoint->name.value,
							((char*)checkpoint_section->section_descriptor.section_id.id));							
					} else {
						log_printf (LOG_LEVEL_DEBUG, "New Sync State Message for ckpt = %s, section = default section.\n",
							(char*)&checkpoint->name.value);
					} 
					request_exec_sync_state.header.size =	sizeof (struct req_exec_ckpt_synchronize_state);
					request_exec_sync_state.header.id =
						SERVICE_ID_MAKE (CKPT_SERVICE,
							MESSAGE_REQ_EXEC_CKPT_SYNCHRONIZESTATE);
					memcpy(&request_exec_sync_state.previous_ring_id, &saved_ring_id, sizeof(struct memb_ring_id));
					memcpy(&request_exec_sync_state.checkpoint_name,
						&checkpoint->name,
						sizeof(mar_name_t));
					memcpy(&request_exec_sync_state.checkpoint_creation_attributes, 
							&checkpoint->checkpoint_creation_attributes, 
							sizeof(mar_ckpt_checkpoint_creation_attributes_t));
					memcpy(&request_exec_sync_state.section_descriptor,
							&checkpoint_section->section_descriptor,
							sizeof(mar_ckpt_section_descriptor_t));						
						
					request_exec_sync_state.nodeid = this_ip->nodeid;
				 			
					for (i = 0; i < PROCESSOR_COUNT_MAX; i++) {

						marshall_to_mar_ckpt_refcnt_t (
							&request_exec_sync_state.ckpt_refcnt[i],
							&checkpoint->ckpt_refcnt[i]);
						}
					request_exec_sync_state.section_descriptor.section_id.id = 0;

					log_printf (LOG_LEVEL_DEBUG, "New Sync State Message Values\n");
					for (i = 0; i < PROCESSOR_COUNT_MAX; i ++) {
						log_printf (LOG_LEVEL_DEBUG,"Index %d has proc %s and count %d\n",
						i,
						totempg_ifaces_print (request_exec_sync_state.ckpt_refcnt[i].nodeid),
						request_exec_sync_state.ckpt_refcnt[i].count);
					}
	
					iovecs[0].iov_base = (char *)&request_exec_sync_state;
					iovecs[0].iov_len = sizeof (struct req_exec_ckpt_synchronize_state);
					
					/*
					 * Populate the Section ID
					 */
					iovecs[1].iov_base = ((char*)checkpoint_section->section_descriptor.section_id.id);
					iovecs[1].iov_len = checkpoint_section->section_descriptor.section_id.id_len;
					request_exec_sync_state.header.size += iovecs[1].iov_len;	
					 
					/*
					 * Check to see if we can queue the new message and if you can
					 * then mcast the message else break and create callback.
					 */
					res = totempg_groups_mcast_joined (openais_group_handle, iovecs, 2, TOTEMPG_AGREED);
					if (res == 0) {
						log_printf (LOG_LEVEL_DEBUG, "Multicasted Sync State Message.\n");
					}			
					else {
						log_printf (LOG_LEVEL_DEBUG, "Sync State Message Outbound Queue full need to Wait for Callback.\n");
						return (1);
					}				
					recovery_section_send_flag = 1;				
				}
				
				origSectionSize = checkpoint_section->section_descriptor.section_size;  
				newSectionSize = 0;
		            
				/*
				* Now Create SyncSection messsages in chunks of CKPT_MAX_SECTION_DATA_SEND or less
				*/
				while (recovery_section_data_offset < origSectionSize) {
				/*
				* Send a Max of CKPT_MAX_SECTION_DATA_SEND of section data
				*/
					if ((origSectionSize - recovery_section_data_offset) > CKPT_MAX_SECTION_DATA_SEND) {             	
						newSectionSize = CKPT_MAX_SECTION_DATA_SEND;
					}      
					else {
						newSectionSize = (origSectionSize - recovery_section_data_offset);
					}        
		            
					/*
					* Create and save a new Sync Section message.
					*/			
								
					request_exec_sync_section.header.size =	sizeof (struct req_exec_ckpt_synchronize_section); 
					request_exec_sync_section.header.id =
						SERVICE_ID_MAKE (CKPT_SERVICE,
							MESSAGE_REQ_EXEC_CKPT_SYNCHRONIZESECTION);
					memcpy (&request_exec_sync_section.previous_ring_id, &saved_ring_id, sizeof(struct memb_ring_id));
					memcpy (&request_exec_sync_section.checkpoint_name, &checkpoint->name, sizeof(mar_name_t));
					request_exec_sync_section.id_len =
						checkpoint_section->section_descriptor.section_id.id_len;
					memcpy (&request_exec_sync_section.data_offset, &recovery_section_data_offset, sizeof(mar_uint32_t));
					memcpy (&request_exec_sync_section.data_size, &newSectionSize, sizeof(mar_uint32_t));			
					if (checkpoint_section->section_descriptor.section_id.id) {
						log_printf (LOG_LEVEL_DEBUG, "New Sync Section Message for ckpt = %s, section = %s, Data size = %d.\n", 
												(char*)&checkpoint->name.value,
												((char*)checkpoint_section->section_descriptor.section_id.id),
												newSectionSize);
					} else {
						log_printf (LOG_LEVEL_DEBUG, "New Sync Section Message for ckpt = %s, default section, Data size = %d.\n",
										(char*)&checkpoint->name.value,newSectionSize);
					}
					/*
					* Populate the Sync Section Request
					*/
					iovecs[0].iov_base = (char *)&request_exec_sync_section;
					iovecs[0].iov_len = sizeof (struct req_exec_ckpt_synchronize_section);					
					
					/*
					 * Populate the Section ID
					 */
					iovecs[1].iov_base = ((char*)checkpoint_section->section_descriptor.section_id.id);
					iovecs[1].iov_len = checkpoint_section->section_descriptor.section_id.id_len;
					request_exec_sync_section.header.size += iovecs[1].iov_len;
					
					/*
					 * Populate the Section Data.
					 */
					iovecs[2].iov_base = ((char*)checkpoint_section->section_data + recovery_section_data_offset);
					iovecs[2].iov_len = newSectionSize;
					request_exec_sync_section.header.size += iovecs[2].iov_len;
					/*
					 * Check to see if we can queue the new message and if you can
					 * then mcast the message else break and create callback.
					 */

					res = totempg_groups_mcast_joined (openais_group_handle, iovecs, 3, TOTEMPG_AGREED);
					if (res == 0) {
						log_printf (LOG_LEVEL_DEBUG, "Multicasted Sync Section Message.\n");
					} else {
						log_printf (LOG_LEVEL_DEBUG, "Sync Section Message Outbound Queue full need to Wait for Callback.\n");
						return (1);
					}									
						
		        	recovery_section_data_offset += newSectionSize;
				}
				recovery_section_send_flag = 0;
				recovery_section_data_offset = 0;
				recovery_ckpt_section_next = recovery_ckpt_section_next->next;
				continue;
			}
			else {
				/*
				 * We have reached the end of a section List. 
				 * Move to the next element in the ckpt list.
				 * Init the section ptr to 0 so it is re evaled
				 */
				recovery_ckpt_next = recovery_ckpt_next->next;							
				recovery_ckpt_section_next = 0;				
				continue;
			}
		}
		/*Should only be here at the end of the traversal of the ckpt list*/
		ckpt_recovery_finalize();
recovery_exit_clean:		
		/*Re - Initialize the static's*/
		recovery_ckpt_next = 0;
		recovery_ckpt_section_next = 0;
		recovery_section_data_offset = 0;
		recovery_section_send_flag = 0;
		recovery_abort = 0;
		
		return (0);
	}
}

static void ckpt_recovery_finalize (void) 
{
	struct list_head *checkpoint_list;
	struct list_head *checkpoint_section_list;
	struct checkpoint *checkpoint;
	struct checkpoint_section *section;
	struct ckpt_identifier *ckpt_id;

	/*
	 * Remove All elements from old checkpoint
	 * list
	 */
	checkpoint_list = checkpoint_list_head.next;	
	while (!list_empty(&checkpoint_list_head)) {
		checkpoint = list_entry (checkpoint_list,
            			struct checkpoint, list);
            			
		checkpoint_section_list = checkpoint->sections_list_head.next;	
		while (!list_empty(&checkpoint->sections_list_head)) {
			section = list_entry (checkpoint_section_list,
				struct checkpoint_section, list);
			
			list_del (&section->list);			
			log_printf (LOG_LEVEL_DEBUG, "ckpt_recovery_finalize removed 0x%x.\n", section);
			free (section);
			checkpoint_section_list = checkpoint->sections_list_head.next;
		}
		list_del(&checkpoint->list);
		free(checkpoint);
		checkpoint_list = checkpoint_list_head.next;
	}
	
	/*
	 * Initialize the old list again.
	 */
	list_init(&checkpoint_list_head);
	
	/*
	 * Copy the contents of the new list_head into the old list head
	 */
	checkpoint_recovery_list_head.prev->next = &checkpoint_list_head;
	checkpoint_recovery_list_head.next->prev = &checkpoint_list_head;		
	memcpy(&checkpoint_list_head, &checkpoint_recovery_list_head, sizeof(struct list_head));

	/*Timers might have been started before recovery happened .. restart them ..*/
	for (checkpoint_list = checkpoint_list_head.next;
                checkpoint_list != &checkpoint_list_head;
                checkpoint_list = checkpoint_list->next) {

		checkpoint = list_entry (checkpoint_list,
			struct checkpoint, list);

		for (checkpoint_section_list = checkpoint->sections_list_head.next;
                        checkpoint_section_list != &checkpoint->sections_list_head;
                        checkpoint_section_list = checkpoint_section_list->next) {
                        section = list_entry (checkpoint_section_list,
                                                                struct checkpoint_section, list);

			if (section->section_descriptor.expiration_time != SA_TIME_END) {
				ckpt_id = malloc (sizeof(struct ckpt_identifier));
				assert(ckpt_id);
				memcpy(&ckpt_id->ckpt_name,&checkpoint->name,sizeof(mar_name_t));
				memcpy(&ckpt_id->ckpt_section_id, &section->section_descriptor.section_id,sizeof(mar_ckpt_section_id_t));

				openais_timer_add (
					abstime_to_msec (section->section_descriptor.expiration_time),
					ckpt_id,
					timer_function_section_expire,
					&section->expiration_timer);
				log_printf (LOG_LEVEL_DEBUG, "ckpt_recovery_initialize expiration timer = 0x%x\n",
					section->expiration_timer);	
			}
                }
        }


	/*
	 * Initialize the new list head for reuse.
	 */
	list_init(&checkpoint_recovery_list_head);
	log_printf (LOG_LEVEL_DEBUG, "ckpt_recovery_finalize Done.\n");
	
}

static void ckpt_recovery_activate (void) 
{		
 	recovery_state = SYNCHRONY_STATE_ENDED;
 	return;
}

static void ckpt_recovery_abort (void) 
{
	recovery_abort = 1;
	return;
}

static void ckpt_replace_localhost_ip (unsigned int *joined_list) {
	struct list_head *checkpoint_list;
        struct checkpoint *checkpoint;
        unsigned int localhost_nodeid = 0;
        int index;

	assert(joined_list);

	for (checkpoint_list = checkpoint_list_head.next;
		checkpoint_list != &checkpoint_list_head;
		checkpoint_list = checkpoint_list->next) {

		checkpoint = list_entry (checkpoint_list,
			struct checkpoint, list);
		index = processor_index_find(localhost_nodeid, checkpoint->ckpt_refcnt);
		if (index == -1) {
			continue;
		}		
		checkpoint->ckpt_refcnt[index].nodeid = joined_list[0];
		log_printf (LOG_LEVEL_DEBUG, "Transitioning From Local Host replacing 127.0.0.1 with %x ...\n",
			joined_list[0]);

	}
	process_localhost_transition = 0;
}


static void ckpt_recovery_process_members_exit (
	unsigned int *left_list, 
	int left_list_entries)
{
	struct list_head *checkpoint_list;
	struct checkpoint *checkpoint;
	unsigned int *member_nodeid;
	unsigned int localhost_nodeid;
	int index;
	int i;

	localhost_nodeid = 0; // TODO
	
	if (left_list_entries == 0) {
		return;
	}
// TODO this is wrong
	if ((left_list_entries == 1) && 
	    *left_list == localhost_nodeid) {
		process_localhost_transition = 1;
		return; 
	}
	
	/*
	 *  Iterate left_list_entries. 
	 */
	member_nodeid = left_list;
	for (i = 0; i < left_list_entries; i++) {		
		checkpoint_list = checkpoint_list_head.next;

iterate_while_loop:
		while (checkpoint_list != &checkpoint_list_head) {
			checkpoint = list_entry (checkpoint_list,
				struct checkpoint, list);			
			assert (checkpoint > 0);
			index = processor_index_find(*member_nodeid,
				checkpoint->ckpt_refcnt);
			assert (-1 <= index);
			assert (index < PROCESSOR_COUNT_MAX);			
			if (index < 0) {
				checkpoint_list = checkpoint_list->next;
				goto iterate_while_loop;
			}		
			/*
			 * Decrement
			 * 
			 */
			if (checkpoint->referenceCount > 0) { /*defect 1192*/
				checkpoint->referenceCount -= checkpoint->ckpt_refcnt[index].count;
				log_printf (LOG_LEVEL_DEBUG, "ckpt_recovery_process_members_exit: refCount for %s = %d.\n",
												&checkpoint->name.value,checkpoint->referenceCount);
				assert (checkpoint->referenceCount > 0);/*defect 1192*/
			} else {
				log_printf (LOG_LEVEL_ERROR, "ckpt_recovery_process_members_exit: refCount for %s = %d.\n",
												&checkpoint->name.value,checkpoint->referenceCount);			
				assert(0);
			}		
			checkpoint->ckpt_refcnt[index].count = 0;
			checkpoint->ckpt_refcnt[index].nodeid = 0;
			checkpoint_list = checkpoint_list->next;
		}
		member_nodeid++;
	}

	clean_checkpoint_list(&checkpoint_list_head);
	
	return;
}

void clean_checkpoint_list(struct list_head *head) 
{
	struct list_head *checkpoint_list;
	struct checkpoint *checkpoint;

	if (list_empty(head)) {
		log_printf (LOG_LEVEL_NOTICE, "clean_checkpoint_list: List is empty \n");
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
		 if (checkpoint->unlinked && checkpoint->referenceCount == 1) { /*defect 1129*/
			log_printf (LOG_LEVEL_NOTICE,"clean_checkpoint_list: deallocating checkpoint %s.\n",
                                                                                                &checkpoint->name.value);
			checkpoint_list = checkpoint_list->next;
			checkpoint_release (checkpoint);
			continue;
			
		} 
		else if ((checkpoint->expired == 0) && (checkpoint->referenceCount == 1)) { /*defect 1192*/
			log_printf (LOG_LEVEL_NOTICE, "clean_checkpoint_list: Starting timer to release checkpoint %s.\n",
				&checkpoint->name.value);
			openais_timer_delete (checkpoint->retention_timer);
			openais_timer_add (
				checkpoint->checkpoint_creation_attributes.retention_duration / 1000000,
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
	if (configuration_type == TOTEM_CONFIGURATION_REGULAR) {
		if (recovery_state == SYNCHRONY_STATE_ENDED) {
			memcpy (&saved_ring_id, ring_id, sizeof(struct memb_ring_id));
		}
		if (process_localhost_transition) {
			ckpt_replace_localhost_ip (joined_list);
		}
	}	

	else if (configuration_type == TOTEM_CONFIGURATION_TRANSITIONAL) {
		ckpt_recovery_process_members_exit(left_list, left_list_entries);
		recovery_state = SYNCHRONY_STATE_STARTED;
		recovery_abort = 0;
	}
}

static struct checkpoint *checkpoint_find (mar_name_t *name)
{
	struct list_head *checkpoint_list;
	struct checkpoint *checkpoint;

	for (checkpoint_list = checkpoint_list_head.next;
		checkpoint_list != &checkpoint_list_head;
		checkpoint_list = checkpoint_list->next) {

		checkpoint = list_entry (checkpoint_list,
			struct checkpoint, list);

		if (mar_name_match (name, &checkpoint->name)) {
			return (checkpoint);
		}
	}
	return (0);
}

static void ckpt_checkpoint_remove_cleanup (
	void *conn,
	struct checkpoint *checkpoint)
{
	struct list_head *list;
	struct checkpoint_cleanup *checkpoint_cleanup;
	struct ckpt_pd *ckpt_pd = (struct ckpt_pd *)openais_conn_private_data_get (conn);

	for (list = ckpt_pd->checkpoint_list.next;
		list != &ckpt_pd->checkpoint_list;
		list = list->next) {

		checkpoint_cleanup = list_entry (list, struct checkpoint_cleanup, list);
		if (mar_name_match (&checkpoint_cleanup->checkpoint.name, &checkpoint->name)
				|| (checkpoint_cleanup->checkpoint.name.length == 0)) {											
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
			log_printf (LOG_LEVEL_DEBUG, "Checking section id %*s\n", 
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

			log_printf (LOG_LEVEL_DEBUG, "Returning section %s(0x%x)\n", checkpoint_section->section_descriptor.section_id.id,
				checkpoint_section);

			return (checkpoint_section);
		}
	}
	return 0;
}

/*
 * defect 1112: We need to be able to call section release without
 * having to to delete the timer as in the case of release being called
 * from timer_function_section_expire where the expiry already takes care
 * of the timer and its data
 */
void checkpoint_section_and_associate_timer_cleanup (struct checkpoint_section *section,int deleteTimer)
{
	list_del (&section->list);
	if (section->section_descriptor.section_id.id) {
		free (section->section_descriptor.section_id.id); 
	}
	if (section->section_data) {
		free (section->section_data);
	}
	/*
	 * defect 1112 on a section release we need to delete the timer AND its data or memory leaks
	 */
	if (deleteTimer) {	
		openais_timer_delete_data (section->expiration_timer);
	}
	free (section);
}

void checkpoint_section_release (struct checkpoint_section *section)
{
	log_printf (LOG_LEVEL_DEBUG, "checkpoint_section_release expiration timer = 0x%x\n", section->expiration_timer);
	checkpoint_section_and_associate_timer_cleanup (section, 1);
}


void checkpoint_release (struct checkpoint *checkpoint)
{
	struct list_head *list;
	struct checkpoint_section *section;

	openais_timer_delete (checkpoint->retention_timer);

	/*
	 * Release all checkpoint sections for this checkpoint
	 */
	for (list = checkpoint->sections_list_head.next;
		list != &checkpoint->sections_list_head;) {

		section = list_entry (list,
			struct checkpoint_section, list);
	
		list = list->next;
		checkpoint->sectionCount -= 1;
		checkpoint_section_release (section);
	}
	list_del (&checkpoint->list);
	free (checkpoint);
}

int ckpt_checkpoint_close (struct checkpoint *checkpoint) {
	struct req_exec_ckpt_checkpointclose req_exec_ckpt_checkpointclose;
	struct iovec iovec;

	if (checkpoint->expired == 1) {
		return (0);
	}
	req_exec_ckpt_checkpointclose.header.size =
		sizeof (struct req_exec_ckpt_checkpointclose);
	req_exec_ckpt_checkpointclose.header.id =
		SERVICE_ID_MAKE (CKPT_SERVICE,
			MESSAGE_REQ_EXEC_CKPT_CHECKPOINTCLOSE);

	memcpy (&req_exec_ckpt_checkpointclose.checkpoint_name,
		&checkpoint->name, sizeof (mar_name_t));
	memset (&req_exec_ckpt_checkpointclose.source, 0,
		sizeof (mar_message_source_t));

	iovec.iov_base = (char *)&req_exec_ckpt_checkpointclose;
	iovec.iov_len = sizeof (req_exec_ckpt_checkpointclose);

	if (totempg_groups_send_ok_joined (openais_group_handle, &iovec, 1)) {
		assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1, TOTEMPG_AGREED) == 0);
		return (0);
	}

	return (-1);
}

static int ckpt_exec_init_fn (struct objdb_iface_ver0 *objdb)
{
	log_init ("CKPT");

	/*
	 *  Initialize the saved ring ID.
	 */
	saved_ring_id.seq = 0;
	totemip_copy(&saved_ring_id.rep, this_ip);
	
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
	swab_mar_time_t (&req_exec_ckpt_checkpointretentiondurationset->retention_duration);
}

static void exec_ckpt_checkpointretentiondurationexpire_endian_convert (void *msg)
{
	struct req_exec_ckpt_checkpointretentiondurationexpire *req_exec_ckpt_checkpointretentiondurationexpire = (struct req_exec_ckpt_checkpointretentiondurationexpire *)msg;

	swab_mar_req_header_t (&req_exec_ckpt_checkpointretentiondurationexpire->header);
	swab_mar_name_t (&req_exec_ckpt_checkpointretentiondurationexpire->checkpoint_name);
}

static void exec_ckpt_sectioncreate_endian_convert (void *msg)
{
	struct req_exec_ckpt_sectioncreate *req_exec_ckpt_sectioncreate = (struct req_exec_ckpt_sectioncreate *)msg;

	swab_mar_req_header_t (&req_exec_ckpt_sectioncreate->header);
	swab_mar_message_source_t (&req_exec_ckpt_sectioncreate->source);
	swab_mar_name_t (&req_exec_ckpt_sectioncreate->checkpoint_name);
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
	swab_mar_uint32_t (&req_exec_ckpt_sectiondelete->id_len);
}

static void exec_ckpt_sectrionexpirationtimeset_endian_convert (void *msg)
{
	struct req_exec_ckpt_sectionexpirationtimeset *req_exec_ckpt_sectionexpirationtimeset = (struct req_exec_ckpt_sectionexpirationtimeset *)msg;

	swab_mar_req_header_t (&req_exec_ckpt_sectionexpirationtimeset->header);
	swab_mar_message_source_t (&req_exec_ckpt_sectionexpirationtimeset->source);
	swab_mar_name_t (&req_exec_ckpt_sectionexpirationtimeset->checkpoint_name);
	swab_mar_uint32_t (&req_exec_ckpt_sectionexpirationtimeset->id_len);
	swab_mar_time_t (&req_exec_ckpt_sectionexpirationtimeset->expiration_time);
}

static void exec_ckpt_sectionwrite_endian_convert (void *msg)
{
	struct req_exec_ckpt_sectionwrite *req_exec_ckpt_sectionwrite = (struct req_exec_ckpt_sectionwrite *)msg;

	swab_mar_req_header_t (&req_exec_ckpt_sectionwrite->header);
	swab_mar_message_source_t (&req_exec_ckpt_sectionwrite->source);
	swab_mar_name_t (&req_exec_ckpt_sectionwrite->checkpoint_name);
	swab_mar_uint32_t (&req_exec_ckpt_sectionwrite->id_len);
	swab_mar_offset_t (&req_exec_ckpt_sectionwrite->data_size);
}

static void exec_ckpt_sectionoverwrite_endian_convert (void *msg)
{
	struct req_exec_ckpt_sectionoverwrite *req_exec_ckpt_sectionoverwrite = (struct req_exec_ckpt_sectionoverwrite *)msg;

	swab_mar_req_header_t (&req_exec_ckpt_sectionoverwrite->header);
	swab_mar_message_source_t (&req_exec_ckpt_sectionoverwrite->source);
	swab_mar_name_t (&req_exec_ckpt_sectionoverwrite->checkpoint_name);
	swab_mar_uint32_t (&req_exec_ckpt_sectionoverwrite->id_len);
	swab_mar_offset_t (&req_exec_ckpt_sectionoverwrite->data_size);
}

static void exec_ckpt_sectionread_endian_convert (void *msg)
{
	struct req_exec_ckpt_sectionread *req_exec_ckpt_sectionread = (struct req_exec_ckpt_sectionread *)msg;

	swab_mar_req_header_t (&req_exec_ckpt_sectionread->header);
	swab_mar_message_source_t (&req_exec_ckpt_sectionread->source);
	swab_mar_name_t (&req_exec_ckpt_sectionread->checkpoint_name);
	swab_mar_uint32_t (&req_exec_ckpt_sectionread->id_len);
	swab_mar_offset_t (&req_exec_ckpt_sectionread->data_offset);
	swab_mar_offset_t (&req_exec_ckpt_sectionread->data_size);
}

static void exec_ckpt_synchronize_state_endian_convert (void *msg)
{
	struct req_exec_ckpt_synchronize_state *req_exec_ckpt_synchronize_state = (struct req_exec_ckpt_synchronize_state *)msg;
	unsigned int i;

	swab_mar_req_header_t (&req_exec_ckpt_synchronize_state->header);
//	swab_mar_memb_ring_id_t (&req_exec_ckpt_synchronize_state->memb_ring_id);
	swab_mar_name_t (&req_exec_ckpt_synchronize_state->checkpoint_name);
	swab_mar_ckpt_checkpoint_creation_attributes_t (&req_exec_ckpt_synchronize_state->checkpoint_creation_attributes);
	swab_mar_ckpt_section_descriptor_t (&req_exec_ckpt_synchronize_state->section_descriptor);
	swab_mar_uint32_t (&req_exec_ckpt_synchronize_state->nodeid);
	for (i = 0; i < PROCESSOR_COUNT_MAX; i++) {
		swab_mar_ckpt_refcnt_t (&req_exec_ckpt_synchronize_state->ckpt_refcnt[i]);
	}
}

static void exec_ckpt_synchronize_section_endian_convert (void *msg)
{
	struct req_exec_ckpt_synchronize_section *req_exec_ckpt_synchronize_section = (struct req_exec_ckpt_synchronize_section *)msg;
	swab_mar_req_header_t (&req_exec_ckpt_synchronize_section->header);
//	swab_mar_memb_ring_id_t (&req_exec_ckpt_synchronize_section->memb_ring_id);
	swab_mar_name_t (&req_exec_ckpt_synchronize_section->checkpoint_name);
	swab_mar_uint32_t (&req_exec_ckpt_synchronize_section->id_len);
	swab_mar_offset_t (&req_exec_ckpt_synchronize_section->data_offset);
	swab_mar_offset_t (&req_exec_ckpt_synchronize_section->data_size);
}

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
	int proc_index;

	log_printf (LOG_LEVEL_DEBUG, "Executive request to open checkpoint %p\n", req_exec_ckpt_checkpointopen);
	
	if (req_exec_ckpt_checkpointopen->fail_with_error != SA_AIS_OK) {
		error = req_exec_ckpt_checkpointopen->fail_with_error;
		goto error_exit;
	}

	if (message_source_is_local(&req_exec_ckpt_checkpointopen->source)) {
		checkpoint_cleanup = malloc (sizeof (struct checkpoint_cleanup));
		if (checkpoint_cleanup == 0) {
			error = SA_AIS_ERR_NO_MEMORY;
			goto error_exit;
		}		
	}	

	checkpoint = checkpoint_find (&req_exec_ckpt_checkpointopen->checkpoint_name);

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

		checkpoint_section = malloc (sizeof (struct checkpoint_section));
		if (checkpoint_section == 0) {
			free (checkpoint);
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
		checkpoint->referenceCount = 0;
		checkpoint->retention_timer = 0;
		checkpoint->expired = 0;
		checkpoint->sectionCount = 0;
		
		if ((checkpoint->checkpoint_creation_attributes.creation_flags & (SA_CKPT_WR_ACTIVE_REPLICA | SA_CKPT_WR_ACTIVE_REPLICA_WEAK)) &&
			(checkpoint->checkpoint_creation_attributes.creation_flags & SA_CKPT_CHECKPOINT_COLLOCATED) == 0) {
			checkpoint->active_replica_set = 1;
		} else
		if ((checkpoint->checkpoint_creation_attributes.creation_flags & SA_CKPT_WR_ALL_REPLICAS) == 1) {
			checkpoint->active_replica_set = 1;
		} else {
			checkpoint->active_replica_set = 0;
		}
		
		initialize_ckpt_refcnt_array(checkpoint->ckpt_refcnt);

		/*
		 * Add in default checkpoint section
		 */
		list_init (&checkpoint_section->list);
		list_add (&checkpoint_section->list, &checkpoint->sections_list_head);
		
		/*
		 * Default section id
		 */
		checkpoint_section->section_descriptor.section_id.id = 0;
		checkpoint_section->section_descriptor.section_id.id_len = 0;
		checkpoint_section->section_descriptor.expiration_time = SA_TIME_END;
		checkpoint_section->section_descriptor.section_state = SA_CKPT_SECTION_VALID;
		checkpoint_section->section_descriptor.last_update = 0; /*current time*/
		checkpoint_section->section_descriptor.section_size = strlen("Factory installed data\0")+1;		
		checkpoint_section->section_data = malloc(strlen("Factory installed data\0")+1);
		assert(checkpoint_section->section_data);
		memcpy(checkpoint_section->section_data, "Factory installed data\0", strlen("Factory installed data\0")+1);
		checkpoint_section->expiration_timer = 0;
		
		checkpoint->referenceCount += 1;
	} else {
		if (req_exec_ckpt_checkpointopen->checkpoint_creation_attributes_set &&
			memcmp (&checkpoint->checkpoint_creation_attributes,
				&req_exec_ckpt_checkpointopen->checkpoint_creation_attributes,
				sizeof (mar_ckpt_checkpoint_creation_attributes_t)) != 0) {

			error = SA_AIS_ERR_EXIST;
			goto error_exit;
		}
	}

	/*
	 * If the checkpoint has been unlinked, it is an invalid name
	 */
	if (checkpoint->unlinked) {
		error = SA_AIS_ERR_NOT_EXIST; /* TODO this is wrong */
		goto error_exit;
	}
	
	/*
	 * Setup connection information and mark checkpoint as referenced
	 */
	log_printf (LOG_LEVEL_DEBUG, "CHECKPOINT opened is %p\n", checkpoint);
	checkpoint->referenceCount += 1;
	
	/*
	 * Add the connection reference information to the Checkpoint to be
	 * sent out later as a part of the sync process.
	 * 
	 */
	 
	 proc_index = processor_index_find(nodeid,checkpoint->ckpt_refcnt);
	 if (proc_index == -1) {/* Could not find, lets set the processor to an index.*/
	 	proc_index = processor_index_set(nodeid,checkpoint->ckpt_refcnt);
	 }
	 if (proc_index != -1 ) {	 
		checkpoint->ckpt_refcnt[proc_index].nodeid = nodeid;
	 	checkpoint->ckpt_refcnt[proc_index].count++;
	 }
	 else {
	 	log_printf (LOG_LEVEL_ERROR, 
	 		"MAX LIMIT OF PROCESSORS reached. Cannot store new proc %p info.\n", 
	 		checkpoint);
	 }
	/*
	 * Reset retention duration since this checkpoint was just opened
	 */
	openais_timer_delete (checkpoint->retention_timer);
	checkpoint->retention_timer = 0;

	/*
	 * Send error result to CKPT library
	 */
error_exit:
	/*
	 * If this node was the source of the message, respond to this node
	 */
	if (message_source_is_local(&req_exec_ckpt_checkpointopen->source)) {
		/*
		 * If its an async call respond with the invocation and handle
		 */
		if (req_exec_ckpt_checkpointopen->async_call) {
			res_lib_ckpt_checkpointopenasync.header.size = sizeof (struct res_lib_ckpt_checkpointopenasync);
			res_lib_ckpt_checkpointopenasync.header.id = MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTOPENASYNC;
			res_lib_ckpt_checkpointopenasync.header.error = error;
			res_lib_ckpt_checkpointopenasync.checkpoint_handle = req_exec_ckpt_checkpointopen->checkpoint_handle;
			res_lib_ckpt_checkpointopenasync.invocation = req_exec_ckpt_checkpointopen->invocation;

			openais_conn_send_response (
				req_exec_ckpt_checkpointopen->source.conn,
				&res_lib_ckpt_checkpointopenasync,
				sizeof (struct res_lib_ckpt_checkpointopenasync));
			openais_conn_send_response (
				openais_conn_partner_get (req_exec_ckpt_checkpointopen->source.conn),
				&res_lib_ckpt_checkpointopenasync,
				sizeof (struct res_lib_ckpt_checkpointopenasync));
		} else {
			/*
			 * otherwise respond with the normal checkpointopen response
			 */
			res_lib_ckpt_checkpointopen.header.size = sizeof (struct res_lib_ckpt_checkpointopen);
			res_lib_ckpt_checkpointopen.header.id = MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTOPEN;
			res_lib_ckpt_checkpointopen.header.error = error;

			openais_conn_send_response (
				req_exec_ckpt_checkpointopen->source.conn,
				&res_lib_ckpt_checkpointopen,
				sizeof (struct res_lib_ckpt_checkpointopen));
		}

		/*
		 * This is the path taken when all goes well and this call was local
		 */
		if (error == SA_AIS_OK) {
			memcpy(&checkpoint_cleanup->checkpoint,checkpoint,sizeof(struct checkpoint));
			ckpt_pd = openais_conn_private_data_get (req_exec_ckpt_checkpointopen->source.conn);

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

static int recovery_checkpoint_open(
	mar_name_t *checkpoint_name,
	mar_ckpt_checkpoint_creation_attributes_t *ckptAttributes,
	struct ckpt_refcnt *ref_cnt) 
{
	int i;	
	struct checkpoint *checkpoint = 0;
	struct checkpoint_section *checkpoint_section = 0;
	SaAisErrorT error = SA_AIS_OK;	

	log_printf (LOG_LEVEL_DEBUG, "recovery_checkpoint_open %s\n", &checkpoint_name->value);
	log_printf (LOG_LEVEL_DEBUG, "recovery_checkpoint_open refcnt Values\n");
	for (i = 0; i < PROCESSOR_COUNT_MAX; i ++) {
		log_printf (LOG_LEVEL_DEBUG,"Index %d has proc %s and count %d\n",
			i,
			totempg_ifaces_print (ref_cnt[i].nodeid),
			ref_cnt[i].count);
	}

	
	checkpoint = checkpoint_find (checkpoint_name);

	/*
	 * If checkpoint doesn't exist, create one
	 */
	if (checkpoint == 0) {
		log_printf (LOG_LEVEL_DEBUG, "recovery_checkpoint_open Allocating new Checkpoint %s\n", &checkpoint_name->value);
		checkpoint = malloc (sizeof (struct checkpoint));
		if (checkpoint == 0) {
			error = SA_AIS_ERR_NO_MEMORY;
			goto error_exit;
		}

		checkpoint_section = malloc (sizeof (struct checkpoint_section));
		if (checkpoint_section == 0) {
			free (checkpoint);
			error = SA_AIS_ERR_NO_MEMORY;
			goto error_exit;
		}

		memcpy (&checkpoint->name,
			checkpoint_name,
			sizeof (mar_name_t));
		memcpy (&checkpoint->checkpoint_creation_attributes,
			ckptAttributes,
			sizeof (mar_ckpt_checkpoint_creation_attributes_t));
		checkpoint->unlinked = 0;
		list_init (&checkpoint->list);
		list_init (&checkpoint->sections_list_head);
		list_add (&checkpoint->list, &checkpoint_list_head);
		checkpoint->retention_timer = 0;
		checkpoint->expired = 0;
		
		/*
		 * Add in default checkpoint section
		 */
		list_init (&checkpoint_section->list);
		list_add (&checkpoint_section->list, &checkpoint->sections_list_head);
		
		/*
		 * Default section id
		 */
		checkpoint_section->section_descriptor.section_id.id = 0;
		checkpoint_section->section_descriptor.section_id.id_len = 0;
		checkpoint_section->section_descriptor.expiration_time = SA_TIME_END;
		checkpoint_section->section_descriptor.section_state = SA_CKPT_SECTION_VALID;
		checkpoint_section->section_descriptor.last_update = 0; /*current time*/
		checkpoint_section->section_descriptor.section_size = strlen("Factory installed data\0")+1;
		checkpoint_section->section_data = malloc(strlen("Factory installed data\0")+1);
		assert(checkpoint_section->section_data);
		memcpy(checkpoint_section->section_data, "Factory installed data\0", strlen("Factory installed data\0")+1);
		checkpoint_section->expiration_timer = 0;

		if ((checkpoint->checkpoint_creation_attributes.creation_flags & (SA_CKPT_WR_ACTIVE_REPLICA | SA_CKPT_WR_ACTIVE_REPLICA_WEAK)) &&
			(checkpoint->checkpoint_creation_attributes.creation_flags & SA_CKPT_CHECKPOINT_COLLOCATED) == 0) {
			checkpoint->active_replica_set = 1;
		} else
		if ((checkpoint->checkpoint_creation_attributes.creation_flags & SA_CKPT_WR_ALL_REPLICAS) == 1) {
			checkpoint->active_replica_set = 1;
		} else {
			checkpoint->active_replica_set = 0;
		}


		initialize_ckpt_refcnt_array(checkpoint->ckpt_refcnt);
	}
	else {
		/*
		* Setup connection information and mark checkpoint as referenced
		*/
		log_printf (LOG_LEVEL_DEBUG, "recovery CHECKPOINT reopened is %p\n", checkpoint);
	}

	/*
	 * If the checkpoint has been unlinked, it is an invalid name
	 */
	if (checkpoint->unlinked) {
		error = SA_AIS_ERR_BAD_OPERATION; /* Is this the correct return ? */
		goto error_exit;
	}

	/*CHECK to see if there are any existing ckpts*/
	if ((checkpoint->ckpt_refcnt) &&  (ckpt_refcnt_total(checkpoint->ckpt_refcnt) > 0)) {
		log_printf (LOG_LEVEL_DEBUG,"calling merge_ckpt_refcnts\n");
		merge_ckpt_refcnts(checkpoint->ckpt_refcnt, ref_cnt);
	}
	else {
		initialize_ckpt_refcnt_array(checkpoint->ckpt_refcnt);
	}

	/*No Existing ckpts. Lets assign what we got over the network or the merged with network values*/
	/*
	 * The reason why we are adding 1 is because there is an assignment vis-a-via an increment in the 
	 * the next line. Whether the ckpt was opened earlier or just now, the referenceCount is getting
	 * obliterated in the next line.
	 */
	checkpoint->referenceCount = ckpt_refcnt_total(ref_cnt) + 1; /*defect 1192*/
	log_printf (LOG_LEVEL_DEBUG, "OPEN checkpoint->referenceCount %d\n",checkpoint->referenceCount);
	memcpy(checkpoint->ckpt_refcnt,ref_cnt,sizeof(struct ckpt_refcnt)*PROCESSOR_COUNT_MAX);
	
	/*
	 * Reset retention duration since this checkpoint was just opened
	 */
	openais_timer_delete (checkpoint->retention_timer);
	checkpoint->retention_timer = 0;

	/*
	 * Send error result to CKPT library
	 */
error_exit:
	return (error);
}

static void message_handler_req_exec_ckpt_synchronize_state (
	void *message,
	unsigned int nodeid)
{
	int retcode;
	struct req_exec_ckpt_synchronize_state *req_exec_ckpt_sync_state 
		= (struct req_exec_ckpt_synchronize_state *)message;
	struct ckpt_refcnt local_ckpt_refcnt[PROCESSOR_COUNT_MAX];
	unsigned int i;
					
	/*
	 * If the Incoming message's previous ring id == saved_ring_id
	 * Ignore because we have seen this message before.
	 */
	if (memcmp (&req_exec_ckpt_sync_state->previous_ring_id, &saved_ring_id,sizeof (struct memb_ring_id)) == 0) {
			log_printf(LOG_LEVEL_DEBUG, "message_handler_req_exec_ckpt_synchronize_state ignoring ...\n");
			return;
	}

	for (i = 0; i < PROCESSOR_COUNT_MAX; i++) {
		marshall_from_mar_ckpt_refcnt_t (&local_ckpt_refcnt[i],
			&req_exec_ckpt_sync_state->ckpt_refcnt[i]);
	}
	retcode = recovery_checkpoint_open(&req_exec_ckpt_sync_state->checkpoint_name,
		&req_exec_ckpt_sync_state->checkpoint_creation_attributes,
		local_ckpt_refcnt);
	if (retcode != SA_AIS_OK) {
		log_printf(LOG_LEVEL_DEBUG, "message_handler_req_exec_ckpt_synchronize_state\n");
		log_printf(LOG_LEVEL_DEBUG, "recovery_checkpoint_open returned %d\n",retcode);		
	}
	
	retcode = recovery_section_create (&req_exec_ckpt_sync_state->section_descriptor,
		&req_exec_ckpt_sync_state->checkpoint_name,
		(char*)req_exec_ckpt_sync_state
			+ sizeof (struct req_exec_ckpt_synchronize_state));
	if (retcode != SA_AIS_OK) {
		log_printf(LOG_LEVEL_DEBUG, "message_handler_req_exec_ckpt_synchronize_state\n");
		log_printf(LOG_LEVEL_DEBUG, "recovery_section_create returned %d\n",retcode);		
	}	
}

static void message_handler_req_exec_ckpt_synchronize_section (
	void *message,
	unsigned int nodeid)
{
	int retcode;
	struct req_exec_ckpt_synchronize_section *req_exec_ckpt_sync_section 
					= (struct req_exec_ckpt_synchronize_section *)message;
					
	/*
	 * If the Incoming message's previous ring id == saved_ring_id
	 * Ignore because we have seen this message before.
	 */
	if (memcmp (&req_exec_ckpt_sync_section->previous_ring_id, &saved_ring_id,sizeof (struct memb_ring_id)) == 0) {
			log_printf(LOG_LEVEL_DEBUG, "message_handler_req_exec_ckpt_synchronize_section ignoring ...\n");
			return;
	}
	/*
	 * Write the contents of the section to the checkpoint section.
	 */
	retcode = recovery_section_write(req_exec_ckpt_sync_section->id_len,
		(char*)req_exec_ckpt_sync_section +
			sizeof (struct req_exec_ckpt_synchronize_section),
		&req_exec_ckpt_sync_section->checkpoint_name,
		(char*)req_exec_ckpt_sync_section
			+ sizeof (struct req_exec_ckpt_synchronize_section)
			+ req_exec_ckpt_sync_section->id_len, 
		req_exec_ckpt_sync_section->data_offset, 
		req_exec_ckpt_sync_section->data_size);
	if (retcode != SA_AIS_OK) {
		log_printf(LOG_LEVEL_ERROR, "message_handler_req_exec_ckpt_synchronize_section\n");
		log_printf(LOG_LEVEL_ERROR, "recovery_section_write returned %d\n",retcode);		
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
	log_printf (LOG_LEVEL_DEBUG, "timer_function_section_expire data = 0x%x \n",data);
	if (ckpt_id->ckpt_section_id.id_len && ckpt_id->ckpt_section_id.id) {
		log_printf (LOG_LEVEL_DEBUG, "Attempting to Expire section %s in ckpt %s\n", 
			ckpt_id->ckpt_section_id.id,
			(char *)&ckpt_id->ckpt_name.value);
	}
	else {
		log_printf (LOG_LEVEL_ERROR, "timer_function_section_expire data incorect\n");
		goto free_mem;
	}
	
	checkpoint = checkpoint_find (&ckpt_id->ckpt_name);
        if (checkpoint == 0) {
		log_printf (LOG_LEVEL_ERROR, "timer_function_section_expire could not find ckpt %s\n",
                        (char *)&ckpt_id->ckpt_name.value);
		goto free_mem;	
        }

        checkpoint_section = checkpoint_section_find (checkpoint,
                                                                (char *)ckpt_id->ckpt_section_id.id,
                                                                (int)ckpt_id->ckpt_section_id.id_len);
        if (checkpoint_section == 0) {
		log_printf (LOG_LEVEL_ERROR, "timer_function_section_expire could not find section %s in ckpt %s\n",
                        ckpt_id->ckpt_section_id.id,
                        (char *)&ckpt_id->ckpt_name.value);
		goto free_mem;
        }

	log_printf (LOG_LEVEL_DEBUG, "Expiring section %s in ckpt %s\n",
                        ckpt_id->ckpt_section_id.id,
                        (char *)&ckpt_id->ckpt_name.value);

	checkpoint->sectionCount -= 1;
	/*
	 * defect id 1112 "memory leak in checkpoint service" Dont try to delete the timer as the timer
	 * mechanism takes care of that. Just delete the data after this call
	 */
	checkpoint_section_and_associate_timer_cleanup (checkpoint_section, 0);
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

	iovec.iov_base = (char *)&req_exec_ckpt_checkpointretentiondurationexpire;
	iovec.iov_len = sizeof (req_exec_ckpt_checkpointretentiondurationexpire);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1, TOTEMPG_AGREED) == 0);
}

static void message_handler_req_exec_ckpt_checkpointclose (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_ckpt_checkpointclose *req_exec_ckpt_checkpointclose = (struct req_exec_ckpt_checkpointclose *)message;
	struct res_lib_ckpt_checkpointclose res_lib_ckpt_checkpointclose;
	struct checkpoint *checkpoint = 0;
	SaAisErrorT error = SA_AIS_OK;
	int proc_index;
	int release_checkpoint = 0;

	log_printf (LOG_LEVEL_DEBUG, "Got EXEC request to close checkpoint %s\n", get_mar_name_t (&req_exec_ckpt_checkpointclose->checkpoint_name));

	checkpoint = checkpoint_find (&req_exec_ckpt_checkpointclose->checkpoint_name);
	if (checkpoint == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}
		
	log_printf (LOG_LEVEL_DEBUG, "CKPT:CLOSE checkpoint->referenceCount %d\n",checkpoint->referenceCount);
	checkpoint->referenceCount--;
	/*
	 * Modify the connection reference information to the Checkpoint to be
	 * sent out later as a part of the sync process.	 
	 */
	
	proc_index = processor_index_find(nodeid, checkpoint->ckpt_refcnt);
	if (proc_index != -1 ) {	 		
	 	checkpoint->ckpt_refcnt[proc_index].count--;
	}
	else {
		log_printf (LOG_LEVEL_ERROR, 
	 				"Could Not find Processor Info %p info.\n", 
	 				checkpoint);
	}
	assert (checkpoint->referenceCount > 0); /*defect 1192*/
	log_printf (LOG_LEVEL_DEBUG, "disconnect called, new CKPT ref count is %d\n", 
		checkpoint->referenceCount);

	/*
	 * If checkpoint has been unlinked and this is the last reference, delete it
	 */
	if (checkpoint->unlinked && checkpoint->referenceCount == 1 ) { /*defect 1192*/
		log_printf (LOG_LEVEL_DEBUG, "Unlinking checkpoint.\n");		
		release_checkpoint = 1;		
	} else
	if (checkpoint->referenceCount == 1 ) { /*defect 1192*/		
		openais_timer_add (
			checkpoint->checkpoint_creation_attributes.retention_duration / 1000000,
			checkpoint,
			timer_function_retention,
			&checkpoint->retention_timer);
	}
	
error_exit:
	/*Remove the checkpoint from my connections checkpoint list*/
	if (message_source_is_local(&req_exec_ckpt_checkpointclose->source)) {

		res_lib_ckpt_checkpointclose.header.size = sizeof (struct res_lib_ckpt_checkpointclose);
		res_lib_ckpt_checkpointclose.header.id = MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTCLOSE;
		res_lib_ckpt_checkpointclose.header.error = error;
		openais_conn_send_response (req_exec_ckpt_checkpointclose->source.conn,
			&res_lib_ckpt_checkpointclose, sizeof (struct res_lib_ckpt_checkpointclose));
		if (error == SA_AIS_OK) {
			ckpt_checkpoint_remove_cleanup (
				req_exec_ckpt_checkpointclose->source.conn,
				checkpoint);
		}
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
	checkpoint = checkpoint_find (&req_exec_ckpt_checkpointunlink->checkpoint_name);
	if (checkpoint == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}
	if (checkpoint->unlinked) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}
	checkpoint->unlinked = 1;
	/*
	 * Immediately delete entry if reference count is zero
	 */
	if (checkpoint->referenceCount == 1) { /*defect 1192*/
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
	if (message_source_is_local(&req_exec_ckpt_checkpointunlink->source)) {
		res_lib_ckpt_checkpointunlink.header.size = sizeof (struct res_lib_ckpt_checkpointunlink);
		res_lib_ckpt_checkpointunlink.header.id = MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTUNLINK;
		res_lib_ckpt_checkpointunlink.header.error = error;
		openais_conn_send_response (
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

	checkpoint = checkpoint_find (&req_exec_ckpt_checkpointretentiondurationset->checkpoint_name);
	if (checkpoint) {
		log_printf (LOG_LEVEL_DEBUG, "Setting retention duration for checkpoint %s\n",
			get_mar_name_t (&req_exec_ckpt_checkpointretentiondurationset->checkpoint_name));
		if (checkpoint->unlinked == 0) {
			checkpoint->checkpoint_creation_attributes.retention_duration =
				req_exec_ckpt_checkpointretentiondurationset->retention_duration;
	
			if (checkpoint->expired == 0 && checkpoint->referenceCount == 1) { /*defect 1192*/
				openais_timer_delete (checkpoint->retention_timer);
	
				openais_timer_add (
					checkpoint->checkpoint_creation_attributes.retention_duration / 1000000,
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
	if (message_source_is_local(&req_exec_ckpt_checkpointretentiondurationset->source)) {
		res_lib_ckpt_checkpointretentiondurationset.header.size = sizeof (struct res_lib_ckpt_checkpointretentiondurationset);
		res_lib_ckpt_checkpointretentiondurationset.header.id = MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTRETENTIONDURATIONSET;
		res_lib_ckpt_checkpointretentiondurationset.header.error = error;

		openais_conn_send_response (
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

	checkpoint = checkpoint_find (&req_exec_ckpt_checkpointretentiondurationexpire->checkpoint_name);
	if (checkpoint && (checkpoint->expired == 0) && (checkpoint->referenceCount == 1)) { /*defect 1192*/
		log_printf (LOG_LEVEL_NOTICE, "Expiring checkpoint %s\n", get_mar_name_t (&req_exec_ckpt_checkpointretentiondurationexpire->checkpoint_name));
		checkpoint->expired = 1;

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

		assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1, TOTEMPG_AGREED) == 0);
	}
}

static int recovery_section_create (
	mar_ckpt_section_descriptor_t *section_descriptor,
	mar_name_t *checkpoint_name,
	char *section_id) 
{
	struct checkpoint *checkpoint;
	struct checkpoint_section *checkpoint_section;
	void *initial_data;
	struct ckpt_identifier *ckpt_id = 0;
	SaAisErrorT error = SA_AIS_OK;		
	void *section_id_new;
	
	if ((int)section_descriptor->section_id.id_len) {
		log_printf (LOG_LEVEL_DEBUG, "recovery_section_create for checkpoint %s, section %s.\n",
			&checkpoint_name->value, section_id);
	} else {
		log_printf (LOG_LEVEL_DEBUG, "recovery_section_create for checkpoint %s, default section.\n",
			&checkpoint_name->value);
	}
	
	checkpoint = checkpoint_find (checkpoint_name);
	if (checkpoint == 0) {		
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	/*
	 * Determine if user-specified checkpoint ID already exists
	 */	
	checkpoint_section = checkpoint_section_find (
		checkpoint,
		section_id,
		(int)section_descriptor->section_id.id_len);
	if (checkpoint_section) {
		/*
		 * This use case is mostly for the default section and is not probable for any other
		 * sections.
		 */
		if (section_descriptor->section_size
			> checkpoint_section->section_descriptor.section_size) {
			
			log_printf (LOG_LEVEL_NOTICE, 
				"recovery_section_create reallocating data. Present Size: %d, New Size: %d\n",
				checkpoint_section->section_descriptor.section_size,section_descriptor->section_size);

			checkpoint_section->section_data =
				realloc (checkpoint_section->section_data, section_descriptor->section_size);

			if (checkpoint_section->section_data == 0) {
				log_printf (LOG_LEVEL_ERROR,
					"recovery_section_create section_data realloc returned 0 Calling error_exit.\n");
				error = SA_AIS_ERR_NO_MEMORY;
				checkpoint_section_release(checkpoint_section);
				goto error_exit;
			}

			checkpoint_section->section_descriptor.section_size = section_descriptor->section_size;
			error = SA_AIS_OK;
			
		}
		else {
			error = SA_AIS_ERR_EXIST;
		}
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
	initial_data = malloc (section_descriptor->section_size);
	if (initial_data == 0) {
		free (checkpoint_section);
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_exit;
	}
	/*
	 * Allocate checkpoint section id
	 */
	section_id_new = NULL;
	if (section_descriptor->section_id.id_len) {
		section_id_new = malloc ((int)section_descriptor->section_id.id_len);
		if (section_id_new == 0) {
			free (checkpoint_section);
			free (initial_data);
			error = SA_AIS_ERR_NO_MEMORY;
			goto error_exit;
		}
	}

	/*
	 * Copy checkpoint section ID and initialize data.
	 */
	if (section_id) {
		memcpy ((char*)section_id_new, (char*)section_id,
			(int)section_descriptor->section_id.id_len);
	}
	memset (initial_data, 0, section_descriptor->section_size);
	
	/*
	 * Configure checkpoint section
	 */
	memcpy(&checkpoint_section->section_descriptor, 
			section_descriptor,
			sizeof(mar_ckpt_section_descriptor_t));	
	checkpoint_section->section_descriptor.section_state = SA_CKPT_SECTION_VALID;	
	checkpoint_section->section_data = initial_data;
	checkpoint_section->expiration_timer = 0;
	checkpoint_section->section_descriptor.section_id.id = section_id_new;

	if (section_descriptor->expiration_time != SA_TIME_END) {		
		ckpt_id = malloc (sizeof(struct ckpt_identifier));
		assert(ckpt_id);
		memcpy(&ckpt_id->ckpt_name, checkpoint_name,
			sizeof(mar_name_t));
		memcpy(&ckpt_id->ckpt_section_id,
			&checkpoint_section->section_descriptor.section_id,
			sizeof(mar_ckpt_section_id_t));
		log_printf (LOG_LEVEL_DEBUG, "recovery_section_create Enqueuing Timer to Expire section %s in ckpt %s\n",
			ckpt_id->ckpt_section_id.id,
			(char *)&ckpt_id->ckpt_name.value);
		openais_timer_add (
			abstime_to_msec (checkpoint_section->section_descriptor.expiration_time),
			ckpt_id,
			timer_function_section_expire,
			&checkpoint_section->expiration_timer);
		log_printf (LOG_LEVEL_DEBUG, "recovery_section_create expiration timer = 0x%x\n",
			checkpoint_section->expiration_timer);
	}

	/*
	 * Add checkpoint section to checkpoint
	 */
	list_init (&checkpoint_section->list);
	list_add (&checkpoint_section->list,
		&checkpoint->sections_list_head);

error_exit:
	return (error);				
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
	checkpoint = checkpoint_find (&req_exec_ckpt_sectioncreate->checkpoint_name);
	if (checkpoint == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	if (checkpoint->sectionCount == checkpoint->checkpoint_creation_attributes.max_sections) {
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
		memcpy(&ckpt_id->ckpt_section_id,
			&checkpoint_section->section_descriptor.section_id,
			sizeof(mar_ckpt_section_id_t));
		log_printf (LOG_LEVEL_DEBUG, "req_exec_ckpt_sectioncreate Enqueuing Timer to Expire section %s in ckpt %s\n",
			ckpt_id->ckpt_section_id.id,
			(char *)&ckpt_id->ckpt_name.value);
		openais_timer_add (
			abstime_to_msec (checkpoint_section->section_descriptor.expiration_time),
			ckpt_id,
			timer_function_section_expire,
			&checkpoint_section->expiration_timer);
		log_printf (LOG_LEVEL_DEBUG, "req_exec_ckpt_sectionicreate expiration timer = 0x%x\n",
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
	checkpoint->sectionCount += 1;

error_exit:
	if (message_source_is_local(&req_exec_ckpt_sectioncreate->source)) {
		res_lib_ckpt_sectioncreate.header.size = sizeof (struct res_lib_ckpt_sectioncreate);
		res_lib_ckpt_sectioncreate.header.id = MESSAGE_RES_CKPT_CHECKPOINT_SECTIONCREATE;
		res_lib_ckpt_sectioncreate.header.error = error;

		openais_conn_send_response (req_exec_ckpt_sectioncreate->source.conn,
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

	checkpoint = checkpoint_find (&req_exec_ckpt_sectiondelete->checkpoint_name);
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
	checkpoint->sectionCount -= 1;
	checkpoint_section_release (checkpoint_section);

	/*
	 * return result to CKPT library
	 */
error_exit:
	if (message_source_is_local(&req_exec_ckpt_sectiondelete->source)) {
		res_lib_ckpt_sectiondelete.header.size = sizeof (struct res_lib_ckpt_sectiondelete);
		res_lib_ckpt_sectiondelete.header.id = MESSAGE_RES_CKPT_CHECKPOINT_SECTIONDELETE;
		res_lib_ckpt_sectiondelete.header.error = error;

		openais_conn_send_response (
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

	log_printf (LOG_LEVEL_DEBUG, "Executive request to set section expiratoin time\n");
	checkpoint = checkpoint_find (&req_exec_ckpt_sectionexpirationtimeset->checkpoint_name);
	if (checkpoint == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	if (checkpoint->active_replica_set == 0) {
		log_printf (LOG_LEVEL_NOTICE, "expirationset: no active replica, returning error.\n");
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

	openais_timer_delete (checkpoint_section->expiration_timer);
	checkpoint_section->expiration_timer = 0;

	if (req_exec_ckpt_sectionexpirationtimeset->expiration_time != SA_TIME_END) {
		ckpt_id = malloc (sizeof(struct ckpt_identifier));
		assert(ckpt_id);
		memcpy(&ckpt_id->ckpt_name,
			&req_exec_ckpt_sectionexpirationtimeset->checkpoint_name,
			sizeof(mar_name_t));
		memcpy(&ckpt_id->ckpt_section_id,
			&checkpoint_section->section_descriptor.section_id,
			sizeof(mar_ckpt_section_id_t));
		log_printf (LOG_LEVEL_DEBUG, "req_exec_ckpt_sectionexpirationtimeset Enqueuing Timer to Expire section %s in ckpt %s, ref = 0x%x\n",
			ckpt_id->ckpt_section_id.id,
			(char *)&ckpt_id->ckpt_name.value,
			ckpt_id);
		openais_timer_add (
			abstime_to_msec (checkpoint_section->section_descriptor.expiration_time),
			ckpt_id,
			timer_function_section_expire,
			&checkpoint_section->expiration_timer);
		log_printf (LOG_LEVEL_DEBUG, "req_exec_ckpt_sectionexpirationtimeset expiration timer = 0x%x\n", 
			checkpoint_section->expiration_timer);
	}

error_exit:
	if (message_source_is_local (&req_exec_ckpt_sectionexpirationtimeset->source)) {
		res_lib_ckpt_sectionexpirationtimeset.header.size =
			sizeof (struct res_lib_ckpt_sectionexpirationtimeset);
		res_lib_ckpt_sectionexpirationtimeset.header.id =
			 MESSAGE_RES_CKPT_CHECKPOINT_SECTIONEXPIRATIONTIMESET;
		res_lib_ckpt_sectionexpirationtimeset.header.error = error;

		openais_conn_send_response (
			req_exec_ckpt_sectionexpirationtimeset->source.conn,
			&res_lib_ckpt_sectionexpirationtimeset,
			sizeof (struct res_lib_ckpt_sectionexpirationtimeset));
	}
}

static int recovery_section_write(int section_id_len, 
	char* section_id,
	mar_name_t *checkpoint_name,
	void *new_data,
	mar_uint32_t data_offset,
	mar_uint32_t data_size) 
{
	struct checkpoint *checkpoint;
	struct checkpoint_section *checkpoint_section;
	int size_required;	
	SaAisErrorT error = SA_AIS_OK;
	char *sd;	
	
	log_printf (LOG_LEVEL_DEBUG, "recovery_section_write.\n");
	checkpoint = checkpoint_find (checkpoint_name);
	if (checkpoint == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	/*
	 * Find checkpoint section to be written
	 */
	checkpoint_section = checkpoint_section_find (checkpoint,
		section_id, section_id_len);
	if (checkpoint_section == 0) {		
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	/*
	 * If write would extend past end of section data, return error;
	 */
	size_required = data_offset + data_size;
	if (size_required > checkpoint_section->section_descriptor.section_size) {
		log_printf (LOG_LEVEL_ERROR,
			"recovery_section_write. write-past-end size_required:(%d), data_offset:(%d), data_size:(%d), sync-section-size:(%d)\n",
			size_required, data_offset, data_size,
			(int)checkpoint_section->section_descriptor.section_size);
		error = SA_AIS_ERR_ACCESS;
		goto error_exit;		
	}
	
	/*
	 * Write checkpoint section to section data
	 */
	if (data_size > 0) {			
		sd = (char *)checkpoint_section->section_data;
		memcpy (&sd[data_offset], new_data, data_size);
	}	
error_exit:	
	return (error);	
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
	checkpoint = checkpoint_find (&req_exec_ckpt_sectionwrite->checkpoint_name);
	if (checkpoint == 0) {
		log_printf (LOG_LEVEL_ERROR, "checkpoint_find returned 0 Calling error_exit.\n");
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	if (checkpoint->active_replica_set == 0) {
		log_printf (LOG_LEVEL_NOTICE, "checkpointwrite: no active replica, returning error.\n");
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
	if (message_source_is_local(&req_exec_ckpt_sectionwrite->source)) {
		res_lib_ckpt_sectionwrite.header.size =
			sizeof (struct res_lib_ckpt_sectionwrite);
		res_lib_ckpt_sectionwrite.header.id =
			MESSAGE_RES_CKPT_CHECKPOINT_SECTIONWRITE;
		res_lib_ckpt_sectionwrite.header.error = error;

		openais_conn_send_response (
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
	checkpoint = checkpoint_find (&req_exec_ckpt_sectionoverwrite->checkpoint_name);
	if (checkpoint == 0) {
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
	if (message_source_is_local(&req_exec_ckpt_sectionoverwrite->source)) {
		res_lib_ckpt_sectionoverwrite.header.size =
			sizeof (struct res_lib_ckpt_sectionoverwrite);
		res_lib_ckpt_sectionoverwrite.header.id =
			MESSAGE_RES_CKPT_CHECKPOINT_SECTIONOVERWRITE;
		res_lib_ckpt_sectionoverwrite.header.error = error;

		openais_conn_send_response (
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

	checkpoint = checkpoint_find (&req_exec_ckpt_sectionread->checkpoint_name);
	if (checkpoint == 0) {
		error = SA_AIS_ERR_LIBRARY; /* TODO find the right error for this */
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
	if (message_source_is_local(&req_exec_ckpt_sectionread->source)) {
		res_lib_ckpt_sectionread.header.size = sizeof (struct res_lib_ckpt_sectionread) + section_size;
		res_lib_ckpt_sectionread.header.id = MESSAGE_RES_CKPT_CHECKPOINT_SECTIONREAD;
		res_lib_ckpt_sectionread.header.error = error;

		if (section_size != 0) {
			res_lib_ckpt_sectionread.data_read = section_size;
		}
	
		openais_conn_send_response (
			req_exec_ckpt_sectionread->source.conn,
			&res_lib_ckpt_sectionread,
			sizeof (struct res_lib_ckpt_sectionread));

		/*
		 * Write checkpoint to CKPT library section if section has data
		 */
		if (error == SA_AIS_OK) {
			char *sd;
			sd = (char *)checkpoint_section->section_data;
			openais_conn_send_response (
				req_exec_ckpt_sectionread->source.conn,
				&sd[req_exec_ckpt_sectionread->data_offset],
				section_size);
		}
	}
}

static int ckpt_lib_init_fn (void *conn)
{
	struct ckpt_pd *ckpt_pd = (struct ckpt_pd *)openais_conn_private_data_get (conn);

	hdb_create (&ckpt_pd->iteration_hdb);

	/* TODO
	list_add (&ckpt_pd->sectionIterator.list,
		&checkpoint_iteration_list_head);
		*/
	list_init (&ckpt_pd->checkpoint_list);

       return (0);

}

static int ckpt_lib_exit_fn (void *conn)
{
	struct checkpoint_cleanup *checkpoint_cleanup;
	struct list_head *cleanup_list;
	struct ckpt_pd *ckpt_pd = (struct ckpt_pd *)openais_conn_private_data_get (conn);
	
	log_printf (LOG_LEVEL_DEBUG, "checkpoint exit conn %p\n", conn);
	
	/*
	 * close all checkpoints opened on this connection
	 */
	cleanup_list = ckpt_pd->checkpoint_list.next;	
	while (!list_empty(&ckpt_pd->checkpoint_list)) {
		
		checkpoint_cleanup = list_entry (cleanup_list,
			struct checkpoint_cleanup, list);
		
		if (checkpoint_cleanup->checkpoint.name.length > 0) {
			ckpt_checkpoint_close (&checkpoint_cleanup->checkpoint);
		}
		
		list_del (&checkpoint_cleanup->list);		
		free (checkpoint_cleanup);

		cleanup_list = ckpt_pd->checkpoint_list.next;
	}

	/* TODO
	if (ckpt_pd->sectionIterator.sectionIteratorEntries) {
		free (ckpt_pd->sectionIterator.sectionIteratorEntries);
	}
	list_del (&ckpt_pd->sectionIterator.list);
	*/
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

	message_source_set (&req_exec_ckpt_checkpointopen.source, conn);
	memcpy (&req_exec_ckpt_checkpointopen.checkpoint_name,
		&req_lib_ckpt_checkpointopen->checkpoint_name,
		sizeof (mar_name_t));
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

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1, TOTEMPG_AGREED) == 0);
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

	message_source_set (&req_exec_ckpt_checkpointclose.source, conn);

	memcpy (&req_exec_ckpt_checkpointclose.checkpoint_name,
		&req_lib_ckpt_checkpointclose->checkpoint_name, sizeof (mar_name_t));

	iovec.iov_base = (char *)&req_exec_ckpt_checkpointclose;
	iovec.iov_len = sizeof (req_exec_ckpt_checkpointclose);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1, TOTEMPG_AGREED) == 0);
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

	message_source_set (&req_exec_ckpt_checkpointunlink.source, conn);

	memcpy (&req_exec_ckpt_checkpointunlink.checkpoint_name,
		&req_lib_ckpt_checkpointunlink->checkpoint_name,
		sizeof (mar_name_t));

	iovec.iov_base = (char *)&req_exec_ckpt_checkpointunlink;
	iovec.iov_len = sizeof (req_exec_ckpt_checkpointunlink);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);
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

	message_source_set (&req_exec_ckpt_checkpointretentiondurationset.source, conn);
	memcpy (&req_exec_ckpt_checkpointretentiondurationset.checkpoint_name,
		&req_lib_ckpt_checkpointretentiondurationset->checkpoint_name,
		sizeof (mar_name_t));
	req_exec_ckpt_checkpointretentiondurationset.retention_duration =
		req_lib_ckpt_checkpointretentiondurationset->retention_duration;

	iovec.iov_base = (char *)&req_exec_ckpt_checkpointretentiondurationset;
	iovec.iov_len = sizeof (req_exec_ckpt_checkpointretentiondurationset);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1,
		TOTEMPG_AGREED) == 0);

}

static void message_handler_req_lib_ckpt_activereplicaset (
	void *conn,
	void *msg)
{
	struct req_lib_ckpt_activereplicaset *req_lib_ckpt_activereplicaset = (struct req_lib_ckpt_activereplicaset *)msg;
	struct res_lib_ckpt_activereplicaset res_lib_ckpt_activereplicaset;
	struct checkpoint *checkpoint;
	SaAisErrorT error = SA_AIS_OK;

	checkpoint = checkpoint_find (&req_lib_ckpt_activereplicaset->checkpoint_name);

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

	openais_conn_send_response (
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
	checkpoint = checkpoint_find (&req_lib_ckpt_checkpointstatusget->checkpoint_name);
	
	if (checkpoint && (checkpoint->expired == 0)) {

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
	openais_conn_send_response (
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

	message_source_set (&req_exec_ckpt_sectioncreate.source, conn);

	memcpy (&req_exec_ckpt_sectioncreate.checkpoint_name,
		&req_lib_ckpt_sectioncreate->checkpoint_name,
		sizeof (mar_name_t));
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
		log_printf (LOG_LEVEL_DEBUG, "message_handler_req_lib_ckpt_sectioncreate Section = %s, id_len = %d\n",
			iovecs[1].iov_base,
			iovecs[1].iov_len);
	}
	
	if (iovecs[1].iov_len > 0) {
		log_printf (LOG_LEVEL_DEBUG, "IOV_BASE is %p\n", iovecs[1].iov_base);
		assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 2, TOTEMPG_AGREED) == 0);
	} else {
		assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 1, TOTEMPG_AGREED) == 0);
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

	message_source_set (&req_exec_ckpt_sectiondelete.source, conn);

	memcpy (&req_exec_ckpt_sectiondelete.checkpoint_name,
		&req_lib_ckpt_sectiondelete->checkpoint_name,
		sizeof (mar_name_t));
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
		assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 2, TOTEMPG_AGREED) == 0);
	} else {
		assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 1, TOTEMPG_AGREED) == 0);
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

	message_source_set (&req_exec_ckpt_sectionexpirationtimeset.source, conn);

	memcpy (&req_exec_ckpt_sectionexpirationtimeset.checkpoint_name,
		&req_lib_ckpt_sectionexpirationtimeset->checkpoint_name,
		sizeof (mar_name_t));
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
		assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 2, TOTEMPG_AGREED) == 0);
	} else {
		assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 1, TOTEMPG_AGREED) == 0);
	}
}

static void message_handler_req_lib_ckpt_sectionwrite (
	void *conn,
	void *msg)
{
	struct req_lib_ckpt_sectionwrite *req_lib_ckpt_sectionwrite = (struct req_lib_ckpt_sectionwrite *)msg;
	struct req_exec_ckpt_sectionwrite req_exec_ckpt_sectionwrite;
	struct iovec iovecs[2];
	
	log_printf (LOG_LEVEL_DEBUG, "Received data from lib with len = %d and ref = 0x%x\n",
		req_lib_ckpt_sectionwrite->data_size,
	 	req_lib_ckpt_sectionwrite->data_offset);

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

	message_source_set (&req_exec_ckpt_sectionwrite.source, conn);

	memcpy (&req_exec_ckpt_sectionwrite.checkpoint_name,
		&req_lib_ckpt_sectionwrite->checkpoint_name,
		sizeof (mar_name_t));

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
		assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 2, TOTEMPG_AGREED) == 0);
	} else {
		assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 1, TOTEMPG_AGREED) == 0);
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
	message_source_set (&req_exec_ckpt_sectionoverwrite.source, conn);
	memcpy (&req_exec_ckpt_sectionoverwrite.checkpoint_name,
		&req_lib_ckpt_sectionoverwrite->checkpoint_name,
		sizeof (mar_name_t));
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
		assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 2, TOTEMPG_AGREED) == 0);
	} else {
		assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 1, TOTEMPG_AGREED) == 0);
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
	message_source_set (&req_exec_ckpt_sectionread.source, conn);
	memcpy (&req_exec_ckpt_sectionread.checkpoint_name,
		&req_lib_ckpt_sectionread->checkpoint_name,
		sizeof (mar_name_t));
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
		assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 2, TOTEMPG_AGREED) == 0);
	} else {
		assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 1, TOTEMPG_AGREED) == 0);
	}
}

static void message_handler_req_lib_ckpt_checkpointsynchronize (
	void *conn,
	void *msg)
{
	struct req_lib_ckpt_checkpointsynchronize *req_lib_ckpt_checkpointsynchronize = (struct req_lib_ckpt_checkpointsynchronize *)msg;
	struct res_lib_ckpt_checkpointsynchronize res_lib_ckpt_checkpointsynchronize;
	struct checkpoint *checkpoint;

	checkpoint = checkpoint_find (&req_lib_ckpt_checkpointsynchronize->checkpoint_name);
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

	openais_conn_send_response (
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

	checkpoint = checkpoint_find (&req_lib_ckpt_checkpointsynchronizeasync->checkpoint_name);
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

	openais_conn_send_response (
		conn,
		&res_lib_ckpt_checkpointsynchronizeasync,
		sizeof (struct res_lib_ckpt_checkpointsynchronizeasync));

	openais_conn_send_response (
		openais_conn_partner_get (conn),
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

	struct ckpt_pd *ckpt_pd = (struct ckpt_pd *)openais_conn_private_data_get (conn);

	log_printf (LOG_LEVEL_DEBUG, "section iteration initialize\n");

	checkpoint = checkpoint_find (&req_lib_ckpt_sectioniterationinitialize->checkpoint_name);
	if (checkpoint == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	if (checkpoint->active_replica_set == 0) {
		log_printf (LOG_LEVEL_NOTICE, "iterationinitialize: no active replica, returning error.\n");
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
		iteration_instance->iteration_entries_count += 1;
		iteration_entries = realloc (iteration_instance->iteration_entries,
			sizeof (struct iteration_entry) * iteration_instance->iteration_entries_count);
		if (iteration_entries == NULL) {
			error = SA_AIS_ERR_NO_MEMORY;
			goto error_put;
		}
		iteration_instance->iteration_entries = iteration_entries;
		iteration_entries[iteration_instance->iteration_entries_count - 1].active = 1;
		iteration_entries[iteration_instance->iteration_entries_count - 1].checkpoint_section = checkpoint_section;
	}

error_put:
	hdb_handle_put (&ckpt_pd->iteration_hdb, iteration_handle);

error_exit:
	res_lib_ckpt_sectioniterationinitialize.header.size = sizeof (struct res_lib_ckpt_sectioniterationinitialize);
	res_lib_ckpt_sectioniterationinitialize.header.id = MESSAGE_RES_CKPT_SECTIONITERATIONINITIALIZE;
	res_lib_ckpt_sectioniterationinitialize.header.error = error;
	res_lib_ckpt_sectioniterationinitialize.iteration_handle = iteration_handle;

	openais_conn_send_response (
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

	struct ckpt_pd *ckpt_pd = (struct ckpt_pd *)openais_conn_private_data_get (conn);

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

	hdb_destroy (&ckpt_pd->iteration_hdb);

error_exit:
	res_lib_ckpt_sectioniterationfinalize.header.size = sizeof (struct res_lib_ckpt_sectioniterationfinalize);
	res_lib_ckpt_sectioniterationfinalize.header.id = MESSAGE_RES_CKPT_SECTIONITERATIONFINALIZE;
	res_lib_ckpt_sectioniterationfinalize.header.error = error;

	openais_conn_send_response (
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
	struct iteration_instance *iteration_instance = NULL; // this assignment to null makes no sense and is only needed with -O2 or greater TODO
	void *iteration_instance_p;

	struct ckpt_pd *ckpt_pd = (struct ckpt_pd *)openais_conn_private_data_get (conn);

	log_printf (LOG_LEVEL_DEBUG, "section iteration next\n");
	res = hdb_handle_get (&ckpt_pd->iteration_hdb,
		req_lib_ckpt_sectioniterationnext->iteration_handle,
		&iteration_instance_p);
	if (res != 0) {
		error = SA_AIS_ERR_LIBRARY;
		goto error_exit;
	}

	iteration_instance = (struct iteration_instance *)iteration_instance_p;
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
		 * active iteration entry
		 */
		if (iteration_instance->iteration_entries[iteration_instance->iteration_pos].active == 1) {
			break;
		}

		iteration_instance->iteration_pos += 1;
	}

	/*
	 * Prepare response to API
	 */
	section_id_size = iteration_instance->iteration_entries[iteration_instance->iteration_pos].checkpoint_section->section_descriptor.section_id.id_len;

	memcpy (&res_lib_ckpt_sectioniterationnext.section_descriptor, 
		&iteration_instance->iteration_entries[iteration_instance->iteration_pos].checkpoint_section->section_descriptor,
		sizeof (mar_ckpt_section_descriptor_t));

	/*
	 * Get to next iteration entry
	 */
	iteration_instance->iteration_pos += 1;

error_put:
	hdb_handle_put (&ckpt_pd->iteration_hdb, req_lib_ckpt_sectioniterationnext->iteration_handle);

error_exit:
	res_lib_ckpt_sectioniterationnext.header.size = sizeof (struct res_lib_ckpt_sectioniterationnext) + section_id_size;
	res_lib_ckpt_sectioniterationnext.header.id = MESSAGE_RES_CKPT_SECTIONITERATIONNEXT;
	res_lib_ckpt_sectioniterationnext.header.error = error;

	openais_conn_send_response (
		conn,
		&res_lib_ckpt_sectioniterationnext,
		sizeof (struct res_lib_ckpt_sectioniterationnext));

	if (error == SA_AIS_OK) {
		openais_conn_send_response (
			conn,
			iteration_instance->iteration_entries[
				iteration_instance->iteration_pos - 1].
					checkpoint_section->section_descriptor.section_id.id,
				section_id_size);
	}
}
