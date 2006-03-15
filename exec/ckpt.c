/*
 * Copyright (c) 2003-2006 MontaVista Software, Inc.
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
#include "../include/saCkpt.h"
#include "../include/ipc_ckpt.h"
#include "../include/list.h"
#include "../include/queue.h"
#include "../include/hdb.h"
#include "../lcr/lcr_comp.h"
#include "aispoll.h"
#include "mempool.h"
#include "util.h"
#include "main.h"
#include "totempg.h"

#define LOG_SERVICE LOG_SERVICE_CKPT
#define CKPT_MAX_SECTION_DATA_SEND (1024*400)
#include "print.h"

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

struct saCkptCheckpointSection {
	struct list_head list;
	SaCkptSectionDescriptorT sectionDescriptor;
	void *sectionData;
	poll_timer_handle expiration_timer;
};

struct ckpt_refcnt {
	int count;
	struct totem_ip_address addr;
};

struct saCkptCheckpoint {
	struct list_head list;
	SaNameT name;
	SaCkptCheckpointCreationAttributesT checkpointCreationAttributes;
	struct list_head sections_list_head;
	int referenceCount;
	int unlinked;
	poll_timer_handle retention_timer;
	int expired;
	int active_replica_set;
	int sectionCount;
	struct ckpt_refcnt ckpt_refcount[32]; // SHOULD BE PROCESSOR COUNT MAX	
};

struct iteration_entry {
	int active;
	struct saCkptCheckpointSection *checkpoint_section;
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
	SaNameT ckpt_name;
	SaCkptSectionIdT ckpt_section_id;	
};

/* TODO static totempg_recovery_plug_handle ckpt_checkpoint_recovery_plug_handle; */

static int ckpt_exec_init_fn (struct openais_config *);

static int ckpt_lib_exit_fn (void *conn);

static int ckpt_lib_init_fn (void *conn);

static void message_handler_req_lib_ckpt_checkpointopen (
	void *conn,
	void *msg);

static void message_handler_req_lib_ckpt_checkpointopenasync (
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
	struct totem_ip_address *source_addr);

static void message_handler_req_exec_ckpt_synchronize_state (
	void *message,
	struct totem_ip_address *source_addr);

static void message_handler_req_exec_ckpt_synchronize_section (
	void *message,
	struct totem_ip_address *source_addr);

static void message_handler_req_exec_ckpt_checkpointclose (
	void *message,
	struct totem_ip_address *source_addr);

static void message_handler_req_exec_ckpt_checkpointunlink (
	void *message,
	struct totem_ip_address *source_addr);

static void message_handler_req_exec_ckpt_checkpointretentiondurationset (
	void *message,
	struct totem_ip_address *source_addr);

static void message_handler_req_exec_ckpt_checkpointretentiondurationexpire (
	void *message,
	struct totem_ip_address *source_addr);

static void message_handler_req_exec_ckpt_sectioncreate (
	void *message,
	struct totem_ip_address *source_addr);

static void message_handler_req_exec_ckpt_sectiondelete (
	void *message,
	struct totem_ip_address *source_addr);

static void message_handler_req_exec_ckpt_sectionexpirationtimeset (
	void *message,
	struct totem_ip_address *source_addr);

static void message_handler_req_exec_ckpt_sectionwrite (
	void *message,
	struct totem_ip_address *source_addr);

static void message_handler_req_exec_ckpt_sectionoverwrite (
	void *message,
	struct totem_ip_address *source_addr);

static void message_handler_req_exec_ckpt_sectionread (
	void *message,
	struct totem_ip_address *source_addr);

static void ckpt_recovery_activate (void);
static void ckpt_recovery_initialize (void);
static int  ckpt_recovery_process (void);
static void ckpt_recovery_finalize();
static void ckpt_recovery_abort(void);
static void ckpt_recovery_process_members_exit(
	struct totem_ip_address *left_list, 
	int left_list_entries); 
static void ckpt_replace_localhost_ip (struct totem_ip_address *joined_list);

void checkpoint_release (struct saCkptCheckpoint *checkpoint);
void timer_function_retention (void *data);
unsigned int abstime_to_msec (SaTimeT time);
void timer_function_section_expire (void *data);
void clean_checkpoint_list(struct list_head* head);

static int recovery_checkpoint_open(
	SaNameT *checkpointName,
	SaCkptCheckpointCreationAttributesT *ckptAttributes,
	struct ckpt_refcnt *ref_cnt);

static int recovery_section_create (
	SaCkptSectionDescriptorT *sectionDescriptor, 
	SaNameT *checkpointName,
	char* SectionId);

static int recovery_section_write(
	int sectionIdLen, char* sectionId , SaNameT *checkpointName,
	void *newData, SaUint32T dataOffSet, SaUint32T dataSize);
									
static int process_localhost_transition = 0;

DECLARE_LIST_INIT(checkpoint_list_head);

DECLARE_LIST_INIT(checkpoint_iteration_list_head);

DECLARE_LIST_INIT(checkpoint_recovery_list_head);

struct checkpoint_cleanup {
    struct list_head list;
    struct saCkptCheckpoint checkpoint;
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

static void ckpt_confchg_fn(
		enum totem_configuration_type configuration_type,
		struct totem_ip_address *member_list, int member_list_entries,
		struct totem_ip_address *left_list, int left_list_entries,
		struct totem_ip_address *joined_list, int joined_list_entries,
		struct memb_ring_id *ring_id);

/*
 * Executive Handler Definition
 */
static struct openais_lib_handler ckpt_lib_handlers[] =
{
	{ /* 0 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_checkpointopen,
		.response_size		= sizeof (struct res_lib_ckpt_checkpointopen),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTOPEN,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 1 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_checkpointopenasync,
		.response_size		= sizeof (struct res_lib_ckpt_checkpointopenasync),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTOPENASYNC,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 2 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_checkpointclose,
		.response_size		= sizeof (struct res_lib_ckpt_checkpointclose),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTCLOSE,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 3 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_checkpointunlink,
		.response_size		= sizeof (struct res_lib_ckpt_checkpointunlink),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTUNLINK,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 4 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_checkpointretentiondurationset,
		.response_size		= sizeof (struct res_lib_ckpt_checkpointretentiondurationset),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTRETENTIONDURATIONSET,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 5 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_activereplicaset,
		.response_size		= sizeof (struct res_lib_ckpt_activereplicaset),
		.response_id		= MESSAGE_RES_CKPT_ACTIVEREPLICASET,
		.flow_control		= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 6 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_checkpointstatusget,
		.response_size		= sizeof (struct res_lib_ckpt_checkpointstatusget),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTSTATUSGET,
		.flow_control		= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 7 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_sectioncreate,
		.response_size		= sizeof (struct res_lib_ckpt_sectioncreate),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_SECTIONCREATE,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 8 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_sectiondelete,
		.response_size		= sizeof (struct res_lib_ckpt_sectiondelete),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_SECTIONDELETE,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 9 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_sectionexpirationtimeset,
		.response_size		= sizeof (struct res_lib_ckpt_sectionexpirationtimeset),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_SECTIONEXPIRATIONTIMESET,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 10 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_sectionwrite,
		.response_size		= sizeof (struct res_lib_ckpt_sectionwrite),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_SECTIONWRITE,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 11 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_sectionoverwrite,
		.response_size		= sizeof (struct res_lib_ckpt_sectionoverwrite),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_SECTIONOVERWRITE,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 12 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_sectionread,
		.response_size		= sizeof (struct res_lib_ckpt_sectionread),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_SECTIONREAD,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 13 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_checkpointsynchronize,
		.response_size		= sizeof (struct res_lib_ckpt_checkpointsynchronize),
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTSYNCHRONIZE,
		.flow_control		= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 14 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_checkpointsynchronizeasync,
		.response_size		= sizeof (struct res_lib_ckpt_checkpointsynchronizeasync), /* TODO RESPONSE */
		.response_id		= MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTSYNCHRONIZEASYNC,
		.flow_control		= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 15 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_sectioniterationinitialize,
		.response_size		= sizeof (struct res_lib_ckpt_sectioniterationinitialize),
		.response_id		= MESSAGE_RES_CKPT_SECTIONITERATIONINITIALIZE,
		.flow_control		= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 16 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_sectioniterationfinalize,
		.response_size		= sizeof (struct res_lib_ckpt_sectioniterationfinalize),
		.response_id		= MESSAGE_RES_CKPT_SECTIONITERATIONFINALIZE,
		.flow_control		= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 17 */
		.lib_handler_fn		= message_handler_req_lib_ckpt_sectioniterationnext,
		.response_size		= sizeof (struct res_lib_ckpt_sectioniterationnext),
		.response_id		= MESSAGE_RES_CKPT_SECTIONITERATIONNEXT,
		.flow_control		= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	}
};


static struct openais_exec_handler ckpt_exec_handlers[] = {
	{
		.exec_handler_fn	= message_handler_req_exec_ckpt_checkpointopen,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_ckpt_checkpointclose,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_ckpt_checkpointunlink,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_ckpt_checkpointretentiondurationset,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_ckpt_checkpointretentiondurationexpire,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_ckpt_sectioncreate,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_ckpt_sectiondelete,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_ckpt_sectionexpirationtimeset,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_ckpt_sectionwrite,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_ckpt_sectionoverwrite,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_ckpt_sectionread,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_ckpt_synchronize_state,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_ckpt_synchronize_section
	}
};

struct openais_service_handler ckpt_service_handler = {
	.name				= (unsigned char *)"openais checkpoint service B.01.01",
	.id				= CKPT_SERVICE,
	.private_data_size		= sizeof (struct ckpt_pd),
	.lib_init_fn			= ckpt_lib_init_fn,
	.lib_exit_fn			= ckpt_lib_exit_fn,
	.lib_handlers			= ckpt_lib_handlers,
	.lib_handlers_count		= sizeof (ckpt_lib_handlers) / sizeof (struct openais_lib_handler),
	.exec_init_fn			= ckpt_exec_init_fn,
	.exec_dump_fn			= 0,
	.exec_handlers			= ckpt_exec_handlers,
	.exec_handlers_count		= sizeof (ckpt_exec_handlers) / sizeof (struct openais_exec_handler),
	.confchg_fn			= ckpt_confchg_fn,
	.sync_init			= ckpt_recovery_initialize,
	.sync_process			= ckpt_recovery_process,
	.sync_activate			= ckpt_recovery_activate,
	.sync_abort			= ckpt_recovery_abort,
};

/*
 * Dynamic loader definition
 */
struct openais_service_handler *ckpt_get_handler_ver0 (void);

struct openais_service_handler_iface_ver0 ckpt_service_handler_iface = {
	.openais_get_service_handler_ver0		= ckpt_get_handler_ver0
};

struct lcr_iface openais_ckpt_ver0[1] = {
	{
		.name				= "openais_ckpt",
		.version			= 0,
		.versions_replace		= 0,
		.versions_replace_count		= 0,
		.dependencies			= 0,
		.dependency_count		= 0,
		.constructor			= NULL,
		.destructor			= NULL,
		.interfaces			= (void **)&ckpt_service_handler_iface,
	}
};

struct lcr_comp ckpt_comp_ver0 = {
	.iface_count			= 1,
	.ifaces				= openais_ckpt_ver0
};

struct openais_service_handler *ckpt_get_handler_ver0 (void)
{
	return (&ckpt_service_handler);
}

__attribute__ ((constructor)) static void register_this_component (void) {
	lcr_component_register (&ckpt_comp_ver0);
}

/*
 * All data types used for executive messages
 */
struct req_exec_ckpt_checkpointclose {
	struct req_header header;
	struct message_source source;
	SaNameT checkpointName;
};

struct req_exec_ckpt_checkpointretentiondurationset {
	struct req_header header;
	struct message_source source;
	SaNameT checkpointName;
	SaTimeT retentionDuration;
};

struct req_exec_ckpt_checkpointretentiondurationexpire {
	struct req_header header;
	SaNameT checkpointName;
};

struct req_exec_ckpt_checkpointopen {
	struct req_header header;
	struct message_source source;
	struct req_lib_ckpt_checkpointopen req_lib_ckpt_checkpointopen;
	SaCkptCheckpointHandleT checkpointHandle;
	SaInvocationT invocation;
	int async_call;
	SaAisErrorT fail_with_error;
};

struct req_exec_ckpt_checkpointunlink {
	struct req_header header;
	struct message_source source;
	struct req_lib_ckpt_checkpointunlink req_lib_ckpt_checkpointunlink;
};

struct req_exec_ckpt_sectioncreate {
	struct req_header header;
	struct message_source source;
	SaNameT checkpointName;
	struct req_lib_ckpt_sectioncreate req_lib_ckpt_sectioncreate; /* this must be last */
};

struct req_exec_ckpt_sectiondelete {
	struct req_header header;
	struct message_source source;
	SaNameT checkpointName;
	struct req_lib_ckpt_sectiondelete req_lib_ckpt_sectiondelete; /* this must be last */
};

struct req_exec_ckpt_sectionexpirationtimeset {
	struct req_header header;
	struct message_source source;
	SaNameT checkpointName;
	struct req_lib_ckpt_sectionexpirationtimeset req_lib_ckpt_sectionexpirationtimeset;
};

struct req_exec_ckpt_sectionwrite {
	struct req_header header;
	struct message_source source;
	SaNameT checkpointName;
	struct req_lib_ckpt_sectionwrite req_lib_ckpt_sectionwrite;
};

struct req_exec_ckpt_sectionoverwrite {
	struct req_header header;
	struct message_source source;
	SaNameT checkpointName;
	struct req_lib_ckpt_sectionoverwrite req_lib_ckpt_sectionoverwrite;
};

struct req_exec_ckpt_sectionread {
	struct req_header header;
	struct message_source source;
	SaNameT checkpointName;
	struct req_lib_ckpt_sectionread req_lib_ckpt_sectionread;
};

struct req_exec_ckpt_synchronize_state {
	struct req_header header;
	struct memb_ring_id previous_ring_id;
	SaNameT checkpointName;
	SaCkptCheckpointCreationAttributesT checkpointCreationAttributes;
	SaCkptSectionDescriptorT sectionDescriptor;	
	struct totem_ip_address *source_addr;
	struct ckpt_refcnt ckpt_refcount[PROCESSOR_COUNT_MAX];
};

struct req_exec_ckpt_synchronize_section {
	struct req_header header;
	struct memb_ring_id previous_ring_id;
	SaNameT checkpointName;
	SaCkptSectionIdT sectionId;	
	SaUint32T dataOffSet;
	SaUint32T dataSize;	
};

/* 
 * Implementation
 */
static int processor_index_set(struct totem_ip_address *proc_addr, 
	struct ckpt_refcnt *ckpt_refcount) 
{
	int i;
	for (i = 0; i < PROCESSOR_COUNT_MAX; i ++) {
		if (ckpt_refcount[i].addr.family == 0) {
			/*
			 * If the source addresses do not match and this element
			 * has no stored value then store the new value and 
			 * return the Index.
		 	 */		
			totemip_copy(&ckpt_refcount[i].addr, proc_addr);
			return i;
		}
		/*
		* If the source addresses match then this processor index
		* has already been set
		*/
		else if (totemip_equal(&ckpt_refcount[i].addr, proc_addr)) {
			return -1;
		}

	}
	/* 
	 * Could not Find an empty slot 
	 * to store the new Processor.	
	 */
	for (i = 0; i < PROCESSOR_COUNT_MAX; i ++) {
		if (ckpt_refcount[i].addr.family == 0) {
			log_printf (LOG_LEVEL_ERROR,"Processor Set: Index %d has proc 0 and count 0\n", i);
		}
		else {
			log_printf (LOG_LEVEL_ERROR,"Processor Set: Index %d has proc %s and count %d\n",
                                i,
                                totemip_print(&ckpt_refcount[i].addr),
                                ckpt_refcount[i].count);
                }
        }

	return -1;
}

static int processor_add (struct totem_ip_address *proc_addr, int count, struct ckpt_refcnt *ckpt_refcount) 
{
	int i;
        for (i = 0; i < PROCESSOR_COUNT_MAX; i ++) {
                if (ckpt_refcount[i].addr.family == 0) {
			log_printf (LOG_LEVEL_DEBUG,"processor_add found empty slot to insert new item\n");
                        totemip_copy(&ckpt_refcount[i].addr, proc_addr);
			ckpt_refcount[i].count = count;
                        return i;
                }
		/*Dont know how we missed this in the processor find but update this*/
		else if (totemip_equal(&ckpt_refcount[i].addr, proc_addr)) {
			ckpt_refcount[i].count += count;
			log_printf (LOG_LEVEL_DEBUG,"processor_add for existent proc. ip %s, New count = %d\n",
				totemip_print(&ckpt_refcount[i].addr),
				ckpt_refcount[i].count);

			return i;
		}
	}
        /*
         * Could not Find an empty slot
         * to store the new Processor.
         */
	log_printf (LOG_LEVEL_ERROR,"Processor Add Failed. Dumping Refcount Array\n");
	for (i = 0; i < PROCESSOR_COUNT_MAX; i ++) {
		if (ckpt_refcount[i].addr.family == 0) {
			log_printf (LOG_LEVEL_ERROR,"Processor Add: Index %d has proc 0 and count 0\n", i);
		}
		else {
			log_printf (LOG_LEVEL_ERROR,"Processor Add: Index %d has proc %s and count %d\n",
				i,
				totemip_print(&ckpt_refcount[i].addr),
				ckpt_refcount[i].count);
		}
	}
        return -1;

}

static int processor_index_find(struct totem_ip_address *proc_addr,
	struct ckpt_refcnt *ckpt_refcount) 
{ 
	int i;
	for (i = 0; i < PROCESSOR_COUNT_MAX; i ++) {
		/*
		 * If the source addresses match then return the index
		 */
		
		if (totemip_equal(&ckpt_refcount[i].addr, proc_addr)) {
			return i;
		}				
	}
	/* 
	 * Could not Find the Processor 
	 */
	return -1;
}

static int ckpt_refcount_total(struct ckpt_refcnt *ckpt_refcount) 
{
	int i;
	int total = 0;
	for (i = 0; i < PROCESSOR_COUNT_MAX; i ++) {
		total += ckpt_refcount[i].count;
	}
	return total;
}

static void initialize_ckpt_refcount_array (struct ckpt_refcnt *ckpt_refcount) 
{
	memset((char*)ckpt_refcount, 0, PROCESSOR_COUNT_MAX * sizeof(struct ckpt_refcnt));
}

static void merge_ckpt_refcounts(struct ckpt_refcnt *local, struct ckpt_refcnt *network)
{
	int index,i;	

	for (i = 0; i < PROCESSOR_COUNT_MAX; i ++) {
		if (local[i].addr.family == 0) {
			continue;
		}
		index  = processor_index_find (&local[i].addr, network);
		if (index == -1) { /*Could Not Find the Local Entry in the remote.Add to it*/
			log_printf (LOG_LEVEL_DEBUG,"calling processor_add for ip %s, count %d\n",
				totemip_print(&local[i].addr),
				local[i].count);
			index = processor_add (&local[i].addr, local[i].count, network);
			if (index == -1) {
				log_printf(LOG_LEVEL_ERROR,
					"merge_ckpt_refcounts : could not add a new processor as the MAX limit of procs is reached.Exiting\n");
				assert(0);
			}
		}
		else {
			if (local[i].count == network[index].count) {
				/*Nothing to do here as the network is already up 2 date*/
				log_printf (LOG_LEVEL_DEBUG,"merge_ckpt_refcounts counts match, continue\n");
				continue;
			}
			else {
				/*Found a match for this proc in the Network choose the larger of the 2.*/
				network[index].count += local[i].count; 
				log_printf (LOG_LEVEL_DEBUG,"setting count for %s = %d\n",
					totemip_print(&network[index].addr),
					network[index].count);
			}
		}
	}
}


static void ckpt_recovery_initialize (void) 
{
	struct list_head *checkpoint_list;
	struct list_head *checkpoint_section_list;
	struct saCkptCheckpoint *checkpoint;
	struct saCkptCheckpointSection *section;
	struct saCkptCheckpoint *savedCheckpoint;	
	struct saCkptCheckpointSection *savedSection;
	
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
								struct saCkptCheckpoint, list);
            
		if (checkpoint->referenceCount <= 0) {
			log_printf (LOG_LEVEL_DEBUG, "CKPT: ckpt_recovery_initialize checkpoint %s has referenceCount 0 ignoring.\n", 
										(char*)&checkpoint->name.value);							
			continue;
		}
		savedCheckpoint = 
			(struct saCkptCheckpoint *) malloc (sizeof(struct saCkptCheckpoint));
		assert(savedCheckpoint);
		memcpy(savedCheckpoint, checkpoint, sizeof(struct saCkptCheckpoint));
		list_init(&savedCheckpoint->list);		
		list_add(&savedCheckpoint->list,&checkpoint_recovery_list_head);
		list_init(&savedCheckpoint->sections_list_head);
		for (checkpoint_section_list = checkpoint->sections_list_head.next;
			checkpoint_section_list != &checkpoint->sections_list_head;
			checkpoint_section_list = checkpoint_section_list->next) {
			section = list_entry (checkpoint_section_list,
								struct saCkptCheckpointSection, list);
			savedSection = 
				(struct saCkptCheckpointSection *) malloc (sizeof(struct saCkptCheckpointSection));
			assert(savedSection);
			poll_timer_delete_data (aisexec_poll_handle, section->expiration_timer);
			memcpy(savedSection, section, sizeof(struct saCkptCheckpointSection));
			list_init(&savedSection->list);		
			list_add(&savedSection->list,&savedCheckpoint->sections_list_head);
		}
	}
	
	if (list_empty (&checkpoint_recovery_list_head)) {
		return;
	}
	recovery_ckpt_next = checkpoint_recovery_list_head.next;
	savedCheckpoint = list_entry (recovery_ckpt_next,
								struct saCkptCheckpoint, list);
	recovery_ckpt_section_next = savedCheckpoint->sections_list_head.next;
}

static int ckpt_recovery_process (void) 
{
	int i;
	struct req_exec_ckpt_synchronize_state request_exec_sync_state;
	struct req_exec_ckpt_synchronize_section request_exec_sync_section;
	struct iovec iovecs[3];
	struct saCkptCheckpoint *checkpoint;	
	struct saCkptCheckpointSection *ckptCheckpointSection;
	SaSizeT origSectionSize;  
	SaSizeT newSectionSize;

	if (recovery_abort) { /*Abort was called.*/
		goto recovery_exit_clean;
	}
	/*So Initialize did not have any checkpoints to Synchronize*/
	if ((recovery_ckpt_next == 0) && (recovery_ckpt_section_next == 0)) {
		log_printf (LOG_LEVEL_DEBUG, "CKPT: ckpt_recovery_process Nothing to Process ...\n");
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
							struct saCkptCheckpoint, list);
			if (recovery_ckpt_section_next == 0) {
				recovery_ckpt_section_next = checkpoint->sections_list_head.next;
			}
			if (recovery_ckpt_section_next != &checkpoint->sections_list_head) {
				ckptCheckpointSection = list_entry (recovery_ckpt_section_next,
											struct saCkptCheckpointSection, list);
				/*
		 		* None of the section data msgs have been sent
		 		* so lets start with sending the sync_msg
		 		*/	
				if (recovery_section_send_flag == 0) {
					if (ckptCheckpointSection->sectionDescriptor.sectionId.id) {
						log_printf (LOG_LEVEL_DEBUG, "CKPT: New Sync State Message for ckpt = %s, section = %s.\n", 
							(char*)&checkpoint->name.value,
							((char*)ckptCheckpointSection->sectionDescriptor.sectionId.id));							
					} else {
						log_printf (LOG_LEVEL_DEBUG, "CKPT: New Sync State Message for ckpt = %s, section = default section.\n",
							(char*)&checkpoint->name.value);
					} 
					request_exec_sync_state.header.size =	sizeof (struct req_exec_ckpt_synchronize_state);
					request_exec_sync_state.header.id =
						SERVICE_ID_MAKE (CKPT_SERVICE,
							MESSAGE_REQ_EXEC_CKPT_SYNCHRONIZESTATE);
					memcpy(&request_exec_sync_state.previous_ring_id, &saved_ring_id, sizeof(struct memb_ring_id));
					memcpy(&request_exec_sync_state.checkpointName, &checkpoint->name, sizeof(SaNameT));
					memcpy(&request_exec_sync_state.checkpointCreationAttributes, 
							&checkpoint->checkpointCreationAttributes, 
							sizeof(SaCkptCheckpointCreationAttributesT));
					memcpy(&request_exec_sync_state.sectionDescriptor,
							&ckptCheckpointSection->sectionDescriptor,
							sizeof(SaCkptSectionDescriptorT));						
					memcpy(&request_exec_sync_state.source_addr, &this_ip, sizeof(struct totem_ip_address));
				 			
					memcpy(request_exec_sync_state.ckpt_refcount,
							checkpoint->ckpt_refcount,
							sizeof(struct ckpt_refcnt)*PROCESSOR_COUNT_MAX);
					request_exec_sync_state.sectionDescriptor.sectionId.id = 0;

					log_printf (LOG_LEVEL_DEBUG, "CKPT: New Sync State Message Values\n");
					for (i = 0; i < PROCESSOR_COUNT_MAX; i ++) {
						if (request_exec_sync_state.ckpt_refcount[i].addr.family == 0) {
							log_printf (LOG_LEVEL_DEBUG,"Index %d has proc 0 and count %d\n", i,
								request_exec_sync_state.ckpt_refcount[i].count);
						}
						else {
							log_printf (LOG_LEVEL_DEBUG,"Index %d has proc %s and count %d\n",
							i,
							totemip_print(&request_exec_sync_state.ckpt_refcount[i].addr),
							request_exec_sync_state.ckpt_refcount[i].count);
						}
					}
	
					iovecs[0].iov_base = (char *)&request_exec_sync_state;
					iovecs[0].iov_len = sizeof (struct req_exec_ckpt_synchronize_state);
					
					/*
					 * Populate the Section ID
					 */
					iovecs[1].iov_base = ((char*)ckptCheckpointSection->sectionDescriptor.sectionId.id);
					iovecs[1].iov_len = ckptCheckpointSection->sectionDescriptor.sectionId.idLen;
					request_exec_sync_state.header.size += iovecs[1].iov_len;	
					 
					/*
					 * Check to see if we can queue the new message and if you can
					 * then mcast the message else break and create callback.
					 */
					if (totempg_groups_send_ok_joined (openais_group_handle, iovecs, 2)) {
						assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 2, TOTEMPG_AGREED) == 0);
						log_printf (LOG_LEVEL_DEBUG, "CKPT: Multicasted Sync State Message.\n");
					}			
					else {
						log_printf (LOG_LEVEL_DEBUG, "CKPT: Sync State Message Outbound Queue full need to Wait for Callback.\n");
						return (1);
					}				
					recovery_section_send_flag = 1;				
				}
				
				origSectionSize = ckptCheckpointSection->sectionDescriptor.sectionSize;  
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
					memcpy (&request_exec_sync_section.checkpointName, &checkpoint->name, sizeof(SaNameT));
					memcpy (&request_exec_sync_section.sectionId,
							&ckptCheckpointSection->sectionDescriptor.sectionId,
							sizeof(SaCkptSectionIdT));	    	    
					request_exec_sync_section.sectionId.id = 0;
					memcpy (&request_exec_sync_section.dataOffSet, &recovery_section_data_offset, sizeof(SaUint32T));
					memcpy (&request_exec_sync_section.dataSize, &newSectionSize, sizeof(SaUint32T));			
					if (ckptCheckpointSection->sectionDescriptor.sectionId.id) {
						log_printf (LOG_LEVEL_DEBUG, "CKPT: New Sync Section Message for ckpt = %s, section = %s, Data size = %d.\n", 
												(char*)&checkpoint->name.value,
												((char*)ckptCheckpointSection->sectionDescriptor.sectionId.id),
												newSectionSize);
					} else {
						log_printf (LOG_LEVEL_DEBUG, "CKPT: New Sync Section Message for ckpt = %s, default section, Data size = %d.\n",
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
					iovecs[1].iov_base = ((char*)ckptCheckpointSection->sectionDescriptor.sectionId.id);
					iovecs[1].iov_len = ckptCheckpointSection->sectionDescriptor.sectionId.idLen;
					request_exec_sync_section.header.size += iovecs[1].iov_len;
					
					/*
					 * Populate the Section Data.
					 */
					iovecs[2].iov_base = ((char*)ckptCheckpointSection->sectionData + recovery_section_data_offset);
					iovecs[2].iov_len = newSectionSize;
					request_exec_sync_section.header.size += iovecs[2].iov_len;
					/*
					 * Check to see if we can queue the new message and if you can
					 * then mcast the message else break and create callback.
					 */
					if (totempg_groups_send_ok_joined (openais_group_handle, iovecs, 2)) {
						assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 3, TOTEMPG_AGREED) == 0);
						log_printf (LOG_LEVEL_DEBUG, "CKPT: Multicasted Sync Section Message.\n");
					}			
					else {
						log_printf (LOG_LEVEL_DEBUG, "CKPT: Sync Section Message Outbound Queue full need to Wait for Callback.\n");
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
	struct saCkptCheckpoint *checkpoint;
	struct saCkptCheckpointSection *section;
	struct ckpt_identifier *ckpt_id;

	/*
	 * Remove All elements from old checkpoint
	 * list
	 */
	checkpoint_list = checkpoint_list_head.next;	
	while (!list_empty(&checkpoint_list_head)) {
		checkpoint = list_entry (checkpoint_list,
            			struct saCkptCheckpoint, list);
            			
		checkpoint_section_list = checkpoint->sections_list_head.next;	
		while (!list_empty(&checkpoint->sections_list_head)) {
			section = list_entry (checkpoint_section_list,
				struct saCkptCheckpointSection, list);
			
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
			struct saCkptCheckpoint, list);

		for (checkpoint_section_list = checkpoint->sections_list_head.next;
                        checkpoint_section_list != &checkpoint->sections_list_head;
                        checkpoint_section_list = checkpoint_section_list->next) {
                        section = list_entry (checkpoint_section_list,
                                                                struct saCkptCheckpointSection, list);

			if (section->sectionDescriptor.expirationTime != SA_TIME_END) {
				ckpt_id = malloc (sizeof(struct ckpt_identifier));
				assert(ckpt_id);
				memcpy(&ckpt_id->ckpt_name,&checkpoint->name,sizeof(SaNameT));
				memcpy(&ckpt_id->ckpt_section_id, &section->sectionDescriptor.sectionId,sizeof(SaCkptSectionIdT));

				poll_timer_add (aisexec_poll_handle,
					abstime_to_msec (section->sectionDescriptor.expirationTime),
					ckpt_id,
					timer_function_section_expire,
					&section->expiration_timer);
				log_printf (LOG_LEVEL_DEBUG, "CKPT: ckpt_recovery_initialize expiration timer = 0x%x\n",
					section->expiration_timer);	
			}
                }
        }


	/*
	 * Initialize the new list head for reuse.
	 */
	list_init(&checkpoint_recovery_list_head);
	log_printf (LOG_LEVEL_DEBUG, "CKPT: ckpt_recovery_finalize Done.\n");
	
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

static void ckpt_replace_localhost_ip (struct totem_ip_address *joined_list) {
	struct list_head *checkpoint_list;
        struct saCkptCheckpoint *checkpoint;
        struct totem_ip_address local_ip;
        int index;

	assert(joined_list);

	totemip_localhost(AF_INET, &local_ip);

	for (checkpoint_list = checkpoint_list_head.next;
		checkpoint_list != &checkpoint_list_head;
		checkpoint_list = checkpoint_list->next) {

		checkpoint = list_entry (checkpoint_list,
			struct saCkptCheckpoint, list);
		index = processor_index_find(&local_ip, checkpoint->ckpt_refcount);
		if (index == -1) {
			continue;
		}		
		memcpy(&checkpoint->ckpt_refcount[index].addr, joined_list, sizeof(struct in_addr));
		log_printf (LOG_LEVEL_DEBUG, "Transitioning From Local Host replacing 127.0.0.1 with %s ...\n",
			totemip_print(joined_list));

	}
	process_localhost_transition = 0;
}


static void ckpt_recovery_process_members_exit(struct totem_ip_address *left_list, 
	int left_list_entries)
{
	struct list_head *checkpoint_list;
	struct saCkptCheckpoint *checkpoint;
	struct totem_ip_address *member;
	struct totem_ip_address local_ip;
	int index;
	int i;

	totemip_localhost(AF_INET, &local_ip);
	
	if (left_list_entries == 0) {
		return;
	}

	if ((left_list_entries == 1) && 
	    (totemip_equal(left_list, &local_ip))) {
		process_localhost_transition = 1;
		return; 
	}
	
	/*
	 *  Iterate left_list_entries. 
	 */
	member = left_list;
	for (i = 0; i < left_list_entries; i++) {		
		checkpoint_list = checkpoint_list_head.next;

iterate_while_loop:
		while (checkpoint_list != &checkpoint_list_head) {
			checkpoint = list_entry (checkpoint_list,
				struct saCkptCheckpoint, list);			
			assert (checkpoint > 0);
			index = processor_index_find(member, checkpoint->ckpt_refcount);
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
			if (checkpoint->referenceCount >= 0) {
				checkpoint->referenceCount -= checkpoint->ckpt_refcount[index].count;
				log_printf (LOG_LEVEL_DEBUG, "ckpt_recovery_process_members_exit: refCount for %s = %d.\n",
												&checkpoint->name.value,checkpoint->referenceCount);
				assert (checkpoint->referenceCount >= 0);
			} else {
				log_printf (LOG_LEVEL_ERROR, "ckpt_recovery_process_members_exit: refCount for %s = %d.\n",
												&checkpoint->name.value,checkpoint->referenceCount);			
				assert(0);
			}		
			checkpoint->ckpt_refcount[index].count = 0;
			memset((char*)&checkpoint->ckpt_refcount[index].addr, 0, sizeof(struct in_addr));			
			checkpoint_list = checkpoint_list->next;
		}
		member++;
	}

	clean_checkpoint_list(&checkpoint_list_head);
	
	return;
}

void clean_checkpoint_list(struct list_head *head) 
{
	struct list_head *checkpoint_list;
	struct saCkptCheckpoint *checkpoint;

	if (list_empty(head)) {
		log_printf (LOG_LEVEL_NOTICE, "clean_checkpoint_list: List is empty \n");
		return;
	}

	checkpoint_list = head->next;
        while (checkpoint_list != head) {
		checkpoint = list_entry (checkpoint_list,
                                struct saCkptCheckpoint, list);
                assert (checkpoint > 0);

		/*
		* If checkpoint has been unlinked and this is the last reference, delete it
		*/
		 if (checkpoint->unlinked && checkpoint->referenceCount == 0) {
			log_printf (LOG_LEVEL_NOTICE,"clean_checkpoint_list: deallocating checkpoint %s.\n",
                                                                                                &checkpoint->name.value);
			checkpoint_list = checkpoint_list->next;
			checkpoint_release (checkpoint);
			continue;
			
		} 
		else if ((checkpoint->expired == 0) && (checkpoint->referenceCount == 0)) {
			log_printf (LOG_LEVEL_NOTICE, "clean_checkpoint_list: Starting timer to release checkpoint %s.\n",
				&checkpoint->name.value);
			poll_timer_delete (aisexec_poll_handle, checkpoint->retention_timer);
			poll_timer_add (aisexec_poll_handle,
				checkpoint->checkpointCreationAttributes.retentionDuration / 1000000,
				checkpoint,
				timer_function_retention,
				&checkpoint->retention_timer);
		}
		checkpoint_list = checkpoint_list->next;
        }
}

static void ckpt_confchg_fn (
	enum totem_configuration_type configuration_type,
	struct totem_ip_address *member_list, int member_list_entries,
	struct totem_ip_address *left_list, int left_list_entries,
	struct totem_ip_address *joined_list, int joined_list_entries,
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

static struct saCkptCheckpoint *ckpt_checkpoint_find_global (SaNameT *name)
{
	struct list_head *checkpoint_list;
	struct saCkptCheckpoint *checkpoint;

	for (checkpoint_list = checkpoint_list_head.next;
		checkpoint_list != &checkpoint_list_head;
		checkpoint_list = checkpoint_list->next) {

		checkpoint = list_entry (checkpoint_list,
			struct saCkptCheckpoint, list);

		if (name_match (name, &checkpoint->name)) {
			return (checkpoint);
		}
	}
	return (0);
}

static void ckpt_checkpoint_remove_cleanup (
	void *conn,
	struct saCkptCheckpoint *checkpoint)
{
	struct list_head *list;
	struct checkpoint_cleanup *checkpoint_cleanup;
	struct ckpt_pd *ckpt_pd = (struct ckpt_pd *)openais_conn_private_data_get (conn);

	for (list = ckpt_pd->checkpoint_list.next;
		list != &ckpt_pd->checkpoint_list;
		list = list->next) {

		checkpoint_cleanup = list_entry (list, struct checkpoint_cleanup, list);
		if (name_match (&checkpoint_cleanup->checkpoint.name, &checkpoint->name)
				|| (checkpoint_cleanup->checkpoint.name.length == 0)) {											
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
	struct list_head *checkpoint_section_list;
	struct saCkptCheckpointSection *ckptCheckpointSection;

	if (idLen != 0) {
		log_printf (LOG_LEVEL_DEBUG, "Finding checkpoint section id %s %d\n", (char*)id, idLen);
	}
	else {
		log_printf (LOG_LEVEL_DEBUG, "Finding default checkpoint section\n");
	}
	for (checkpoint_section_list = ckptCheckpoint->sections_list_head.next;
		checkpoint_section_list != &ckptCheckpoint->sections_list_head;
		checkpoint_section_list = checkpoint_section_list->next) {

		ckptCheckpointSection = list_entry (checkpoint_section_list,
			struct saCkptCheckpointSection, list);
		if (ckptCheckpointSection->sectionDescriptor.sectionId.idLen) {
			log_printf (LOG_LEVEL_DEBUG, "Checking section id %*s\n", 
				ckptCheckpointSection->sectionDescriptor.sectionId.idLen,
				ckptCheckpointSection->sectionDescriptor.sectionId.id);
		}
		else {
			log_printf (LOG_LEVEL_DEBUG, "Checking default section id\n");
		}

		/*
		  All 3 of these values being checked MUST be = 0 to return
		  The default section. If even one of them is NON zero follow
		  the normal route
		*/
		if ((idLen ||
				ckptCheckpointSection->sectionDescriptor.sectionId.id ||
				ckptCheckpointSection->sectionDescriptor.sectionId.idLen) == 0) {
			 log_printf (LOG_LEVEL_DEBUG, "Returning default section\n");
			 return (ckptCheckpointSection);
		}
		
		if (ckptCheckpointSection->sectionDescriptor.sectionId.idLen == idLen &&
			(ckptCheckpointSection->sectionDescriptor.sectionId.id)&&
			(id)&&
			(memcmp (ckptCheckpointSection->sectionDescriptor.sectionId.id, 
			id, idLen) == 0)) {

			log_printf (LOG_LEVEL_DEBUG, "Returning section %s(0x%x)\n", ckptCheckpointSection->sectionDescriptor.sectionId.id,
				ckptCheckpointSection);

			return (ckptCheckpointSection);
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
void checkpoint_section_and_associate_timer_cleanup (struct saCkptCheckpointSection *section,int deleteTimer)
{
	list_del (&section->list);
	if (section->sectionDescriptor.sectionId.id) {
		free (section->sectionDescriptor.sectionId.id); 
	}
	if (section->sectionData) {
		free (section->sectionData);
	}
	/*
	 * defect 1112 on a section release we need to delete the timer AND its data or memory leaks
	 */
	if (deleteTimer) {	
		poll_timer_delete_data (aisexec_poll_handle, section->expiration_timer);
	}
	free (section);
}

void checkpoint_section_release (struct saCkptCheckpointSection *section)
{
	log_printf (LOG_LEVEL_DEBUG, "checkpoint_section_release expiration timer = 0x%x\n", section->expiration_timer);
	checkpoint_section_and_associate_timer_cleanup (section, 1);
}


void checkpoint_release (struct saCkptCheckpoint *checkpoint)
{
	struct list_head *list;
	struct saCkptCheckpointSection *section;

	poll_timer_delete (aisexec_poll_handle, checkpoint->retention_timer);

	/*
	 * Release all checkpoint sections for this checkpoint
	 */
	for (list = checkpoint->sections_list_head.next;
		list != &checkpoint->sections_list_head;) {

		section = list_entry (list,
			struct saCkptCheckpointSection, list);
	
		list = list->next;
		checkpoint->sectionCount -= 1;
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
	req_exec_ckpt_checkpointclose.header.id =
		SERVICE_ID_MAKE (CKPT_SERVICE,
			MESSAGE_REQ_EXEC_CKPT_CHECKPOINTCLOSE);

	memcpy (&req_exec_ckpt_checkpointclose.checkpointName,
		&checkpoint->name, sizeof (SaNameT));
	memset (&req_exec_ckpt_checkpointclose.source, 0,
		sizeof (struct message_source));

	iovecs[0].iov_base = (char *)&req_exec_ckpt_checkpointclose;
	iovecs[0].iov_len = sizeof (req_exec_ckpt_checkpointclose);

	if (totempg_groups_send_ok_joined (openais_group_handle, iovecs, 1)) {
		assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 1, TOTEMPG_AGREED) == 0);
		return (0);
	}

	return (-1);
}

static int ckpt_exec_init_fn (struct openais_config *openais_config)
{
	/*
	 *  Initialize the saved ring ID.
	 */
	saved_ring_id.seq = 0;
	totemip_copy(&saved_ring_id.rep, this_ip);
	
	return (0);
}

static void message_handler_req_exec_ckpt_checkpointopen (
	void *message,
	struct totem_ip_address *source_addr)
{
	struct req_exec_ckpt_checkpointopen *req_exec_ckpt_checkpointopen = (struct req_exec_ckpt_checkpointopen *)message;
	struct req_lib_ckpt_checkpointopen *req_lib_ckpt_checkpointopen = (struct req_lib_ckpt_checkpointopen *)&req_exec_ckpt_checkpointopen->req_lib_ckpt_checkpointopen;
	struct res_lib_ckpt_checkpointopen res_lib_ckpt_checkpointopen;
	struct res_lib_ckpt_checkpointopenasync res_lib_ckpt_checkpointopenasync;

	struct saCkptCheckpoint *ckptCheckpoint = 0;
	struct saCkptCheckpointSection *ckptCheckpointSection = 0;
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

	ckptCheckpoint = ckpt_checkpoint_find_global (&req_lib_ckpt_checkpointopen->checkpointName);

	/*
	 * If checkpoint doesn't exist, create one
	 */
	if (ckptCheckpoint == 0) {
		if ((req_lib_ckpt_checkpointopen->checkpointOpenFlags & SA_CKPT_CHECKPOINT_CREATE) == 0) {
			error = SA_AIS_ERR_NOT_EXIST;
			goto error_exit;
		}
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
		list_init (&ckptCheckpoint->sections_list_head);
		list_add (&ckptCheckpoint->list, &checkpoint_list_head);
		ckptCheckpoint->referenceCount = 0;
		ckptCheckpoint->retention_timer = 0;
		ckptCheckpoint->expired = 0;
		ckptCheckpoint->sectionCount = 0;
		
		if ((ckptCheckpoint->checkpointCreationAttributes.creationFlags & (SA_CKPT_WR_ACTIVE_REPLICA | SA_CKPT_WR_ACTIVE_REPLICA_WEAK)) &&
			(ckptCheckpoint->checkpointCreationAttributes.creationFlags & SA_CKPT_CHECKPOINT_COLLOCATED) == 0) {
			ckptCheckpoint->active_replica_set = 1;
		} else
		if ((ckptCheckpoint->checkpointCreationAttributes.creationFlags & SA_CKPT_WR_ALL_REPLICAS) == 1) {
			ckptCheckpoint->active_replica_set = 1;
		} else {
			ckptCheckpoint->active_replica_set = 0;
		}
		
		initialize_ckpt_refcount_array(ckptCheckpoint->ckpt_refcount);

		/*
		 * Add in default checkpoint section
		 */
		list_init (&ckptCheckpointSection->list);
		list_add (&ckptCheckpointSection->list, &ckptCheckpoint->sections_list_head);
		
		/*
		 * Default section id
		 */
		ckptCheckpointSection->sectionDescriptor.sectionId.id = 0;
		ckptCheckpointSection->sectionDescriptor.sectionId.idLen = 0;
		ckptCheckpointSection->sectionDescriptor.expirationTime = SA_TIME_END;
		ckptCheckpointSection->sectionDescriptor.sectionState = SA_CKPT_SECTION_VALID;
		ckptCheckpointSection->sectionDescriptor.lastUpdate = 0; /*current time*/
		ckptCheckpointSection->sectionDescriptor.sectionSize = strlen("Factory installed data\0")+1;		
		ckptCheckpointSection->sectionData = malloc(strlen("Factory installed data\0")+1);
		assert(ckptCheckpointSection->sectionData);
		memcpy(ckptCheckpointSection->sectionData, "Factory installed data\0", strlen("Factory installed data\0")+1);
		ckptCheckpointSection->expiration_timer = 0;
	} else {
		if (req_lib_ckpt_checkpointopen->checkpointCreationAttributesSet &&
			memcmp (&ckptCheckpoint->checkpointCreationAttributes,
				&req_lib_ckpt_checkpointopen->checkpointCreationAttributes,
				sizeof (SaCkptCheckpointCreationAttributesT)) != 0) {

			error = SA_AIS_ERR_EXIST;
			goto error_exit;
		}
	}

	/*
	 * If the checkpoint has been unlinked, it is an invalid name
	 */
	if (ckptCheckpoint->unlinked) {
		error = SA_AIS_ERR_NOT_EXIST; /* Is this the correct return ? */
		goto error_exit;
	}
	
	/*
	 * Setup connection information and mark checkpoint as referenced
	 */
	log_printf (LOG_LEVEL_DEBUG, "CHECKPOINT opened is %p\n", ckptCheckpoint);
	ckptCheckpoint->referenceCount += 1;
	
	/*
	 * Add the connection reference information to the Checkpoint to be
	 * sent out later as a part of the sync process.
	 * 
	 */
	 
	 proc_index = processor_index_find(source_addr,ckptCheckpoint->ckpt_refcount);
	 if (proc_index == -1) {/* Could not find, lets set the processor to an index.*/
	 	proc_index = processor_index_set(source_addr,ckptCheckpoint->ckpt_refcount);
	 }
	 if (proc_index != -1 ) {	 
		 totemip_copy(&ckptCheckpoint->ckpt_refcount[proc_index].addr, source_addr);
	 	ckptCheckpoint->ckpt_refcount[proc_index].count++;
	 }
	 else {
	 	log_printf (LOG_LEVEL_ERROR, 
	 		"CKPT: MAX LIMIT OF PROCESSORS reached. Cannot store new proc %p info.\n", 
	 		ckptCheckpoint);
	 }
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
	if (message_source_is_local(&req_exec_ckpt_checkpointopen->source)) {
		/*
		 * If its an async call respond with the invocation and handle
		 */
		if (req_exec_ckpt_checkpointopen->async_call) {
			res_lib_ckpt_checkpointopenasync.header.size = sizeof (struct res_lib_ckpt_checkpointopenasync);
			res_lib_ckpt_checkpointopenasync.header.id = MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTOPENASYNC;
			res_lib_ckpt_checkpointopenasync.header.error = error;
			res_lib_ckpt_checkpointopenasync.checkpointHandle = req_exec_ckpt_checkpointopen->checkpointHandle;
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
			memcpy(&checkpoint_cleanup->checkpoint,ckptCheckpoint,sizeof(struct saCkptCheckpoint));
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
	SaNameT *checkpointName,
	SaCkptCheckpointCreationAttributesT *ckptAttributes,
	struct ckpt_refcnt *ref_cnt) 
{
	int i;	
	struct saCkptCheckpoint *ckptCheckpoint = 0;
	struct saCkptCheckpointSection *ckptCheckpointSection = 0;
	SaAisErrorT error = SA_AIS_OK;	

	log_printf (LOG_LEVEL_DEBUG, "CKPT: recovery_checkpoint_open %s\n", &checkpointName->value);
	log_printf (LOG_LEVEL_DEBUG, "CKPT: recovery_checkpoint_open refcount Values\n");
	for (i = 0; i < PROCESSOR_COUNT_MAX; i ++) {
        	if (ref_cnt[i].addr.family == 0) {
			log_printf (LOG_LEVEL_DEBUG,"Index %d has proc 0 and count %d\n", i,
				ref_cnt[i].count);
		}
		else {
			log_printf (LOG_LEVEL_DEBUG,"Index %d has proc %s and count %d\n",
				i,
				totemip_print(&ref_cnt[i].addr),
				ref_cnt[i].count);
		}
	}

	
	ckptCheckpoint = ckpt_checkpoint_find_global (checkpointName);

	/*
	 * If checkpoint doesn't exist, create one
	 */
	if (ckptCheckpoint == 0) {
		log_printf (LOG_LEVEL_DEBUG, "CKPT: recovery_checkpoint_open Allocating new Checkpoint %s\n", &checkpointName->value);
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
			checkpointName,
			sizeof (SaNameT));
		memcpy (&ckptCheckpoint->checkpointCreationAttributes,
			ckptAttributes,
			sizeof (SaCkptCheckpointCreationAttributesT));
		ckptCheckpoint->unlinked = 0;
		list_init (&ckptCheckpoint->list);
		list_init (&ckptCheckpoint->sections_list_head);
		list_add (&ckptCheckpoint->list, &checkpoint_list_head);
		ckptCheckpoint->retention_timer = 0;
		ckptCheckpoint->expired = 0;
		
		/*
		 * Add in default checkpoint section
		 */
		list_init (&ckptCheckpointSection->list);
		list_add (&ckptCheckpointSection->list, &ckptCheckpoint->sections_list_head);
		
		/*
		 * Default section id
		 */
		ckptCheckpointSection->sectionDescriptor.sectionId.id = 0;
		ckptCheckpointSection->sectionDescriptor.sectionId.idLen = 0;
		ckptCheckpointSection->sectionDescriptor.expirationTime = SA_TIME_END;
		ckptCheckpointSection->sectionDescriptor.sectionState = SA_CKPT_SECTION_VALID;
		ckptCheckpointSection->sectionDescriptor.lastUpdate = 0; /*current time*/
		ckptCheckpointSection->sectionDescriptor.sectionSize = strlen("Factory installed data\0")+1;
		ckptCheckpointSection->sectionData = malloc(strlen("Factory installed data\0")+1);
		assert(ckptCheckpointSection->sectionData);
		memcpy(ckptCheckpointSection->sectionData, "Factory installed data\0", strlen("Factory installed data\0")+1);
		ckptCheckpointSection->expiration_timer = 0;

		if ((ckptCheckpoint->checkpointCreationAttributes.creationFlags & (SA_CKPT_WR_ACTIVE_REPLICA | SA_CKPT_WR_ACTIVE_REPLICA_WEAK)) &&
			(ckptCheckpoint->checkpointCreationAttributes.creationFlags & SA_CKPT_CHECKPOINT_COLLOCATED) == 0) {
			ckptCheckpoint->active_replica_set = 1;
		} else
		if ((ckptCheckpoint->checkpointCreationAttributes.creationFlags & SA_CKPT_WR_ALL_REPLICAS) == 1) {
			ckptCheckpoint->active_replica_set = 1;
		} else {
			ckptCheckpoint->active_replica_set = 0;
		}


		initialize_ckpt_refcount_array(ckptCheckpoint->ckpt_refcount);
	}
	else {
		/*
		* Setup connection information and mark checkpoint as referenced
		*/
		log_printf (LOG_LEVEL_DEBUG, "CKPT: recovery CHECKPOINT reopened is %p\n", ckptCheckpoint);
	}

	/*
	 * If the checkpoint has been unlinked, it is an invalid name
	 */
	if (ckptCheckpoint->unlinked) {
		error = SA_AIS_ERR_BAD_OPERATION; /* Is this the correct return ? */
		goto error_exit;
	}

	/*CHECK to see if there are any existing ckpts*/
	if ((ckptCheckpoint->ckpt_refcount) &&  (ckpt_refcount_total(ckptCheckpoint->ckpt_refcount) > 0)) {
		log_printf (LOG_LEVEL_DEBUG,"calling merge_ckpt_refcounts\n");
		merge_ckpt_refcounts(ckptCheckpoint->ckpt_refcount, ref_cnt);
	}
	else {
		initialize_ckpt_refcount_array(ckptCheckpoint->ckpt_refcount);
	}

	/*No Existing ckpts. Lets assign what we got over the network or the merged with network values*/
	ckptCheckpoint->referenceCount = ckpt_refcount_total(ref_cnt);
	log_printf (LOG_LEVEL_DEBUG, "CKPT: OPEN ckptCheckpoint->referenceCount %d\n",ckptCheckpoint->referenceCount);
	memcpy(ckptCheckpoint->ckpt_refcount,ref_cnt,sizeof(struct ckpt_refcnt)*PROCESSOR_COUNT_MAX);
	
	/*
	 * Reset retention duration since this checkpoint was just opened
	 */
	poll_timer_delete (aisexec_poll_handle, ckptCheckpoint->retention_timer);
	ckptCheckpoint->retention_timer = 0;

	/*
	 * Send error result to CKPT library
	 */
error_exit:
	return (error);
}

static void message_handler_req_exec_ckpt_synchronize_state (
	void *message,
	struct totem_ip_address *source_addr)
{
	int retcode;
	struct req_exec_ckpt_synchronize_state *req_exec_ckpt_sync_state 
					= (struct req_exec_ckpt_synchronize_state *)message;
					
	/*
	 * If the Incoming message's previous ring id == saved_ring_id
	 * Ignore because we have seen this message before.
	 */
	if (memcmp (&req_exec_ckpt_sync_state->previous_ring_id, &saved_ring_id,sizeof (struct memb_ring_id)) == 0) {
			log_printf(LOG_LEVEL_DEBUG, "CKPT: message_handler_req_exec_ckpt_synchronize_state ignoring ...\n");
			return;
	}
	retcode = recovery_checkpoint_open(&req_exec_ckpt_sync_state->checkpointName,
										&req_exec_ckpt_sync_state->checkpointCreationAttributes,
										req_exec_ckpt_sync_state->ckpt_refcount);
	if (retcode != SA_AIS_OK) {
		log_printf(LOG_LEVEL_DEBUG, "CKPT: message_handler_req_exec_ckpt_synchronize_state\n");
		log_printf(LOG_LEVEL_DEBUG, "CKPT: recovery_checkpoint_open returned %d\n",retcode);		
	}
	
	retcode = recovery_section_create (&req_exec_ckpt_sync_state->sectionDescriptor,
										&req_exec_ckpt_sync_state->checkpointName,
										(char*)req_exec_ckpt_sync_state
										+ sizeof (struct req_exec_ckpt_synchronize_state));
	if (retcode != SA_AIS_OK) {
		log_printf(LOG_LEVEL_DEBUG, "CKPT: message_handler_req_exec_ckpt_synchronize_state\n");
		log_printf(LOG_LEVEL_DEBUG, "CKPT: recovery_section_create returned %d\n",retcode);		
	}	
}

static void message_handler_req_exec_ckpt_synchronize_section (
	void *message,
	struct totem_ip_address *source_addr) 
{
	int retcode;
	struct req_exec_ckpt_synchronize_section *req_exec_ckpt_sync_section 
					= (struct req_exec_ckpt_synchronize_section *)message;
					
	/*
	 * If the Incoming message's previous ring id == saved_ring_id
	 * Ignore because we have seen this message before.
	 */
	if (memcmp (&req_exec_ckpt_sync_section->previous_ring_id, &saved_ring_id,sizeof (struct memb_ring_id)) == 0) {
			log_printf(LOG_LEVEL_DEBUG, "CKPT: message_handler_req_exec_ckpt_synchronize_section ignoring ...\n");
			return;
	}
	/*
	 * Write the contents of the section to the checkpoint section.
	 */
	retcode = recovery_section_write(req_exec_ckpt_sync_section->sectionId.idLen,
							(char*)req_exec_ckpt_sync_section
								+ sizeof (struct req_exec_ckpt_synchronize_section),
							&req_exec_ckpt_sync_section->checkpointName,
							(char*)req_exec_ckpt_sync_section
								+ sizeof (struct req_exec_ckpt_synchronize_section)
								+ req_exec_ckpt_sync_section->sectionId.idLen, 
							req_exec_ckpt_sync_section->dataOffSet, 
							req_exec_ckpt_sync_section->dataSize);
	if (retcode != SA_AIS_OK) {
		log_printf(LOG_LEVEL_ERROR, "CKPT: message_handler_req_exec_ckpt_synchronize_section\n");
		log_printf(LOG_LEVEL_ERROR, "CKPT: recovery_section_write returned %d\n",retcode);		
	}
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
	struct saCkptCheckpoint *ckptCheckpoint = 0;
	struct saCkptCheckpointSection *ckptCheckpointSection = 0;
	struct ckpt_identifier *ckpt_id = 0;

	ckpt_id = (struct ckpt_identifier *)data;
	log_printf (LOG_LEVEL_DEBUG, "CKPT: timer_function_section_expire data = 0x%x \n",data);
	if (ckpt_id->ckpt_section_id.idLen && ckpt_id->ckpt_section_id.id) {
		log_printf (LOG_LEVEL_DEBUG, "CKPT: Attempting to Expire section %s in ckpt %s\n", 
			ckpt_id->ckpt_section_id.id,
			(char *)&ckpt_id->ckpt_name.value);
	}
	else {
		log_printf (LOG_LEVEL_ERROR, "CKPT: timer_function_section_expire data incorect\n");
		goto free_mem;
	}
	
	ckptCheckpoint = ckpt_checkpoint_find_global (&ckpt_id->ckpt_name);
        if (ckptCheckpoint == 0) {
		log_printf (LOG_LEVEL_ERROR, "CKPT: timer_function_section_expire could not find ckpt %s\n",
                        (char *)&ckpt_id->ckpt_name.value);
		goto free_mem;	
        }

        ckptCheckpointSection = ckpt_checkpoint_find_globalSection (ckptCheckpoint,
                                                                (char *)ckpt_id->ckpt_section_id.id,
                                                                (int)ckpt_id->ckpt_section_id.idLen);
        if (ckptCheckpointSection == 0) {
		log_printf (LOG_LEVEL_ERROR, "CKPT: timer_function_section_expire could not find section %s in ckpt %s\n",
                        ckpt_id->ckpt_section_id.id,
                        (char *)&ckpt_id->ckpt_name.value);
		goto free_mem;
        }

	log_printf (LOG_LEVEL_DEBUG, "CKPT: Expiring section %s in ckpt %s\n",
                        ckpt_id->ckpt_section_id.id,
                        (char *)&ckpt_id->ckpt_name.value);

	ckptCheckpoint->sectionCount -= 1;
	/*
	 * defect id 1112 "memory leak in checkpoint service" Dont try to delete the timer as the timer
	 * mechanism takes care of that. Just delete the data after this call
	 */
	checkpoint_section_and_associate_timer_cleanup (ckptCheckpointSection, 0);
free_mem :
	free (ckpt_id);
	
}

void timer_function_retention (void *data)
{
	struct saCkptCheckpoint *checkpoint = (struct saCkptCheckpoint *)data;
	struct req_exec_ckpt_checkpointretentiondurationexpire req_exec_ckpt_checkpointretentiondurationexpire;
	struct iovec iovec;

	checkpoint->retention_timer = 0;
	req_exec_ckpt_checkpointretentiondurationexpire.header.size =
		sizeof (struct req_exec_ckpt_checkpointretentiondurationexpire);
	req_exec_ckpt_checkpointretentiondurationexpire.header.id =
		SERVICE_ID_MAKE (CKPT_SERVICE,
			MESSAGE_REQ_EXEC_CKPT_CHECKPOINTRETENTIONDURATIONEXPIRE);

	memcpy (&req_exec_ckpt_checkpointretentiondurationexpire.checkpointName,
		&checkpoint->name,
		sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_ckpt_checkpointretentiondurationexpire;
	iovec.iov_len = sizeof (req_exec_ckpt_checkpointretentiondurationexpire);

	assert (totempg_groups_mcast_joined (openais_group_handle, &iovec, 1, TOTEMPG_AGREED) == 0);
}

static void message_handler_req_exec_ckpt_checkpointclose (
	void *message,
	struct totem_ip_address *source_addr)
{
	struct req_exec_ckpt_checkpointclose *req_exec_ckpt_checkpointclose = (struct req_exec_ckpt_checkpointclose *)message;
	struct res_lib_ckpt_checkpointclose res_lib_ckpt_checkpointclose;
	struct saCkptCheckpoint *checkpoint = 0;
	SaAisErrorT error = SA_AIS_OK;
	int proc_index;
	int release_checkpoint = 0;

	log_printf (LOG_LEVEL_DEBUG, "Got EXEC request to close checkpoint %s\n", getSaNameT (&req_exec_ckpt_checkpointclose->checkpointName));

	checkpoint = ckpt_checkpoint_find_global (&req_exec_ckpt_checkpointclose->checkpointName);
	if (checkpoint == 0) {
		goto error_exit;
	}
		
	log_printf (LOG_LEVEL_DEBUG, "CKPT:CLOSE checkpoint->referenceCount %d\n",checkpoint->referenceCount);
	checkpoint->referenceCount--;
	/*
	 * Modify the connection reference information to the Checkpoint to be
	 * sent out later as a part of the sync process.	 
	 */
	
	proc_index = processor_index_find(source_addr, checkpoint->ckpt_refcount);
	if (proc_index != -1 ) {	 		
	 	checkpoint->ckpt_refcount[proc_index].count--;
	}
	else {
		log_printf (LOG_LEVEL_ERROR, 
	 				"CKPT: Could Not find Processor Info %p info.\n", 
	 				checkpoint);
	}
	assert (checkpoint->referenceCount >= 0);
	log_printf (LOG_LEVEL_DEBUG, "disconnect called, new CKPT ref count is %d\n", 
		checkpoint->referenceCount);

	/*
	 * If checkpoint has been unlinked and this is the last reference, delete it
	 */
	if (checkpoint->unlinked && checkpoint->referenceCount == 0) {
		log_printf (LOG_LEVEL_DEBUG, "Unlinking checkpoint.\n");		
		release_checkpoint = 1;		
	} else
	if (checkpoint->referenceCount == 0) {		
		poll_timer_add (aisexec_poll_handle,
			checkpoint->checkpointCreationAttributes.retentionDuration / 1000000,
			checkpoint,
			timer_function_retention,
			&checkpoint->retention_timer);
	}
	
error_exit:
	/*Remove the checkpoint from my connections checkpoint list*/
	if (message_source_is_local(&req_exec_ckpt_checkpointclose->source)) {
		ckpt_checkpoint_remove_cleanup (
			req_exec_ckpt_checkpointclose->source.conn,
			checkpoint);

		res_lib_ckpt_checkpointclose.header.size = sizeof (struct res_lib_ckpt_checkpointclose);
		res_lib_ckpt_checkpointclose.header.id = MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTCLOSE;
		res_lib_ckpt_checkpointclose.header.error = error;
		openais_conn_send_response (req_exec_ckpt_checkpointclose->source.conn,
			&res_lib_ckpt_checkpointclose, sizeof (struct res_lib_ckpt_checkpointclose));
	}
	/*Release the checkpoint if instructed to do so.*/
	if (release_checkpoint) {
		checkpoint_release(checkpoint);
	}
}

static void message_handler_req_exec_ckpt_checkpointunlink (
	void *message,
	struct totem_ip_address *source_addr)
{
	struct req_exec_ckpt_checkpointunlink *req_exec_ckpt_checkpointunlink = (struct req_exec_ckpt_checkpointunlink *)message;

	struct req_lib_ckpt_checkpointunlink *req_lib_ckpt_checkpointunlink = (struct req_lib_ckpt_checkpointunlink *)&req_exec_ckpt_checkpointunlink->req_lib_ckpt_checkpointunlink;
	struct res_lib_ckpt_checkpointunlink res_lib_ckpt_checkpointunlink;
	struct saCkptCheckpoint *ckptCheckpoint = 0;
	SaAisErrorT error = SA_AIS_OK;
	
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
	struct totem_ip_address *source_addr)
{
	struct req_exec_ckpt_checkpointretentiondurationset *req_exec_ckpt_checkpointretentiondurationset = (struct req_exec_ckpt_checkpointretentiondurationset *)message;
	struct res_lib_ckpt_checkpointretentiondurationset res_lib_ckpt_checkpointretentiondurationset;
	struct saCkptCheckpoint *checkpoint;
	SaAisErrorT error = SA_AIS_ERR_BAD_OPERATION;

	checkpoint = ckpt_checkpoint_find_global (&req_exec_ckpt_checkpointretentiondurationset->checkpointName);
	if (checkpoint) {
		log_printf (LOG_LEVEL_DEBUG, "CKPT: Setting retention duration for checkpoint %s\n",
			getSaNameT (&req_exec_ckpt_checkpointretentiondurationset->checkpointName));
		if (checkpoint->unlinked == 0) {
			checkpoint->checkpointCreationAttributes.retentionDuration =
				req_exec_ckpt_checkpointretentiondurationset->retentionDuration;
	
			if (checkpoint->expired == 0 && checkpoint->referenceCount == 0) {
				poll_timer_delete (aisexec_poll_handle, checkpoint->retention_timer);
	
				poll_timer_add (aisexec_poll_handle,
					checkpoint->checkpointCreationAttributes.retentionDuration / 1000000,
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
	struct totem_ip_address *source_addr)
{
	struct req_exec_ckpt_checkpointretentiondurationexpire *req_exec_ckpt_checkpointretentiondurationexpire = (struct req_exec_ckpt_checkpointretentiondurationexpire *)message;
	struct req_exec_ckpt_checkpointunlink req_exec_ckpt_checkpointunlink;
	struct saCkptCheckpoint *checkpoint;
	struct iovec iovecs[2];

	checkpoint = ckpt_checkpoint_find_global (&req_exec_ckpt_checkpointretentiondurationexpire->checkpointName);
	if (checkpoint && (checkpoint->expired == 0) && (checkpoint->referenceCount < 1)) {
		log_printf (LOG_LEVEL_NOTICE, "CKPT: Expiring checkpoint %s\n", getSaNameT (&req_exec_ckpt_checkpointretentiondurationexpire->checkpointName));
		checkpoint->expired = 1;

		req_exec_ckpt_checkpointunlink.header.size =
			sizeof (struct req_exec_ckpt_checkpointunlink);
		req_exec_ckpt_checkpointunlink.header.id =
			SERVICE_ID_MAKE (CKPT_SERVICE,
				MESSAGE_REQ_EXEC_CKPT_CHECKPOINTUNLINK);

		req_exec_ckpt_checkpointunlink.source.conn = 0;
		req_exec_ckpt_checkpointunlink.source.addr.family = 0;

		memcpy (&req_exec_ckpt_checkpointunlink.req_lib_ckpt_checkpointunlink.checkpointName,
			&req_exec_ckpt_checkpointretentiondurationexpire->checkpointName,
			sizeof (SaNameT));

		iovecs[0].iov_base = (char *)&req_exec_ckpt_checkpointunlink;
		iovecs[0].iov_len = sizeof (req_exec_ckpt_checkpointunlink);

		assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 1, TOTEMPG_AGREED) == 0);
	}
}

static int recovery_section_create (SaCkptSectionDescriptorT *sectionDescriptor,
	SaNameT *checkpointName,
	char* SectionId) 
{
	struct saCkptCheckpoint *ckptCheckpoint;
	struct saCkptCheckpointSection *ckptCheckpointSection;
	void *initialData;
	void *sectionId;
	struct ckpt_identifier *ckpt_id = 0;
	SaAisErrorT error = SA_AIS_OK;		
	
	if ((int)sectionDescriptor->sectionId.idLen) {
		log_printf (LOG_LEVEL_DEBUG, "CKPT: recovery_section_create for checkpoint %s, section %s.\n",
			&checkpointName->value, SectionId);
	} else {
		log_printf (LOG_LEVEL_DEBUG, "CKPT: recovery_section_create for checkpoint %s, default section.\n",
			&checkpointName->value);
	}
	
	ckptCheckpoint = ckpt_checkpoint_find_global (checkpointName);
	if (ckptCheckpoint == 0) {		
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	/*
	 * Determine if user-specified checkpoint ID already exists
	 */	
	ckptCheckpointSection = ckpt_checkpoint_find_globalSection (ckptCheckpoint,
								 SectionId,
								(int)sectionDescriptor->sectionId.idLen);
	if (ckptCheckpointSection) {
		/*
		 * This use case is mostly for the default section and is not probable for any other
		 * sections.
		 */
		if (sectionDescriptor->sectionSize
			> ckptCheckpointSection->sectionDescriptor.sectionSize) {
			
			log_printf (LOG_LEVEL_NOTICE, 
				"CKPT: recovery_section_create reallocating data. Present Size: %d, New Size: %d\n",
				ckptCheckpointSection->sectionDescriptor.sectionSize,sectionDescriptor->sectionSize);

			ckptCheckpointSection->sectionData =
				realloc (ckptCheckpointSection->sectionData, sectionDescriptor->sectionSize);

			if (ckptCheckpointSection->sectionData == 0) {
				log_printf (LOG_LEVEL_ERROR,
					"CKPT: recovery_section_create sectionData realloc returned 0 Calling error_exit.\n");
				error = SA_AIS_ERR_NO_MEMORY;
				checkpoint_section_release(ckptCheckpointSection);
				goto error_exit;
			}

			ckptCheckpointSection->sectionDescriptor.sectionSize = sectionDescriptor->sectionSize;
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
	ckptCheckpointSection = malloc (sizeof (struct saCkptCheckpointSection));
	if (ckptCheckpointSection == 0) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_exit;
	}
	/*
	 * Allocate checkpoint section data
	 */
	initialData = malloc (sectionDescriptor->sectionSize);
	if (initialData == 0) {
		free (ckptCheckpointSection);
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_exit;
	}
	/*
	 * Allocate checkpoint section id
	 */
	if (sectionDescriptor->sectionId.idLen) {
		sectionId = malloc ((int)sectionDescriptor->sectionId.idLen);
		if (sectionId == 0) {
			free (ckptCheckpointSection);
			free (initialData);
			error = SA_AIS_ERR_NO_MEMORY;
			goto error_exit;
		}
	} else {
		sectionId = 0;
	}
	/*
	 * Copy checkpoint section ID and initialize data.
	 */
	if (SectionId) {
		memcpy ((char*)sectionId, (char*)SectionId, (int)sectionDescriptor->sectionId.idLen);
	} else {
		sectionId = 0;		
	}
	memset (initialData, 0, sectionDescriptor->sectionSize);
	
	/*
	 * Configure checkpoint section
	 */
	memcpy(&ckptCheckpointSection->sectionDescriptor, 
			sectionDescriptor,
			sizeof(SaCkptSectionDescriptorT));	
	ckptCheckpointSection->sectionDescriptor.sectionState = SA_CKPT_SECTION_VALID;	
	ckptCheckpointSection->sectionData = initialData;
	ckptCheckpointSection->expiration_timer = 0;
	ckptCheckpointSection->sectionDescriptor.sectionId.id = sectionId;

	if (sectionDescriptor->expirationTime != SA_TIME_END) {		
		ckpt_id = malloc (sizeof(struct ckpt_identifier));
		assert(ckpt_id);
		memcpy(&ckpt_id->ckpt_name,checkpointName,sizeof(SaNameT));
		memcpy(&ckpt_id->ckpt_section_id, &ckptCheckpointSection->sectionDescriptor.sectionId,sizeof(SaCkptSectionIdT));
		log_printf (LOG_LEVEL_DEBUG, "CKPT: recovery_section_create Enqueuing Timer to Expire section %s in ckpt %s\n",
			ckpt_id->ckpt_section_id.id,
			(char *)&ckpt_id->ckpt_name.value);
		poll_timer_add (aisexec_poll_handle,
			abstime_to_msec (ckptCheckpointSection->sectionDescriptor.expirationTime),
			ckpt_id,
			timer_function_section_expire,
			&ckptCheckpointSection->expiration_timer);
		log_printf (LOG_LEVEL_DEBUG, "CKPT: recovery_section_create expiration timer = 0x%x\n",
			ckptCheckpointSection->expiration_timer);
	}

	/*
	 * Add checkpoint section to checkpoint
	 */
	list_init (&ckptCheckpointSection->list);
	list_add (&ckptCheckpointSection->list,
		&ckptCheckpoint->sections_list_head);

error_exit:
	return (error);				

}

static void message_handler_req_exec_ckpt_sectioncreate (
	void *message,
	struct totem_ip_address *source_addr)
{
	struct req_exec_ckpt_sectioncreate *req_exec_ckpt_sectioncreate = (struct req_exec_ckpt_sectioncreate *)message;
	struct req_lib_ckpt_sectioncreate *req_lib_ckpt_sectioncreate = (struct req_lib_ckpt_sectioncreate *)&req_exec_ckpt_sectioncreate->req_lib_ckpt_sectioncreate;
	struct res_lib_ckpt_sectioncreate res_lib_ckpt_sectioncreate;
	struct saCkptCheckpoint *ckptCheckpoint;
	struct saCkptCheckpointSection *ckptCheckpointSection;
	void *initialData;
	void *sectionId;
	struct ckpt_identifier *ckpt_id = 0;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_DEBUG, "Executive request to create a checkpoint section.\n");
	ckptCheckpoint = ckpt_checkpoint_find_global (&req_exec_ckpt_sectioncreate->checkpointName);
	if (ckptCheckpoint == 0) {
		error = SA_AIS_ERR_LIBRARY; /* TODO find the right error for this*/
		goto error_exit;
	}

	if (ckptCheckpoint->sectionCount == ckptCheckpoint->checkpointCreationAttributes.maxSections) {
		error = SA_AIS_ERR_NO_SPACE;
		goto error_exit;
	}

	if (ckptCheckpoint->checkpointCreationAttributes.maxSections == 1) {
		error = SA_AIS_ERR_EXIST;
		goto error_exit;
	}

	if (ckptCheckpoint->checkpointCreationAttributes.maxSectionSize < 
		req_lib_ckpt_sectioncreate->initialDataSize) {

		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	/*
	 * Determine if user-specified checkpoint section already exists
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
	ckptCheckpointSection->sectionDescriptor.lastUpdate = 0; /* TODO current time */
	ckptCheckpointSection->sectionData = initialData;
	ckptCheckpointSection->expiration_timer = 0;

	if (req_lib_ckpt_sectioncreate->expirationTime != SA_TIME_END) {
		ckpt_id = malloc (sizeof(struct ckpt_identifier));
		assert(ckpt_id);
		memcpy(&ckpt_id->ckpt_name,&req_exec_ckpt_sectioncreate->checkpointName,sizeof(SaNameT));
		memcpy(&ckpt_id->ckpt_section_id, &ckptCheckpointSection->sectionDescriptor.sectionId,sizeof(SaCkptSectionIdT));
		log_printf (LOG_LEVEL_DEBUG, "CKPT: req_exec_ckpt_sectioncreate Enqueuing Timer to Expire section %s in ckpt %s\n",
			ckpt_id->ckpt_section_id.id,
			(char *)&ckpt_id->ckpt_name.value);
		poll_timer_add (aisexec_poll_handle,
			abstime_to_msec (ckptCheckpointSection->sectionDescriptor.expirationTime),
			ckpt_id,
			timer_function_section_expire,
			&ckptCheckpointSection->expiration_timer);
		log_printf (LOG_LEVEL_DEBUG, "CKPT: req_exec_ckpt_sectionicreate expiration timer = 0x%x\n",
			ckptCheckpointSection->expiration_timer);
	}

	log_printf (LOG_LEVEL_DEBUG, "CKPT: message_handler_req_exec_ckpt_sectioncreate created section with id = %s, idLen = %d\n",
					ckptCheckpointSection->sectionDescriptor.sectionId.id,
					ckptCheckpointSection->sectionDescriptor.sectionId.idLen); 
	/*
	 * Add checkpoint section to checkpoint
	 */
	list_init (&ckptCheckpointSection->list);
	list_add (&ckptCheckpointSection->list,
		&ckptCheckpoint->sections_list_head);
	ckptCheckpoint->sectionCount += 1;

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
	struct totem_ip_address *source_addr)
{
	struct req_exec_ckpt_sectiondelete *req_exec_ckpt_sectiondelete = (struct req_exec_ckpt_sectiondelete *)message;
	struct req_lib_ckpt_sectiondelete *req_lib_ckpt_sectiondelete = (struct req_lib_ckpt_sectiondelete *)&req_exec_ckpt_sectiondelete->req_lib_ckpt_sectiondelete;
	struct res_lib_ckpt_sectiondelete res_lib_ckpt_sectiondelete;
	struct saCkptCheckpoint *ckptCheckpoint;
	struct saCkptCheckpointSection *ckptCheckpointSection;
	SaAisErrorT error = SA_AIS_OK;

	ckptCheckpoint = ckpt_checkpoint_find_global (&req_exec_ckpt_sectiondelete->checkpointName);
	if (ckptCheckpoint == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	if (ckptCheckpoint->active_replica_set == 0) {
		log_printf (LOG_LEVEL_NOTICE, "sectiondelete: no active replica, returning error.\n");
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
	ckptCheckpoint->sectionCount -= 1;
	checkpoint_section_release (ckptCheckpointSection);

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
	struct totem_ip_address *source_addr)
{
	struct req_exec_ckpt_sectionexpirationtimeset *req_exec_ckpt_sectionexpirationtimeset = (struct req_exec_ckpt_sectionexpirationtimeset *)message;
	struct req_lib_ckpt_sectionexpirationtimeset *req_lib_ckpt_sectionexpirationtimeset = (struct req_lib_ckpt_sectionexpirationtimeset *)&req_exec_ckpt_sectionexpirationtimeset->req_lib_ckpt_sectionexpirationtimeset;
	struct res_lib_ckpt_sectionexpirationtimeset res_lib_ckpt_sectionexpirationtimeset;
	struct saCkptCheckpoint *ckptCheckpoint;
	struct saCkptCheckpointSection *ckptCheckpointSection;
	struct ckpt_identifier *ckpt_id = 0;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_DEBUG, "Executive request to set section expiratoin time\n");
	ckptCheckpoint = ckpt_checkpoint_find_global (&req_exec_ckpt_sectionexpirationtimeset->checkpointName);
	if (ckptCheckpoint == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	if (ckptCheckpoint->active_replica_set == 0) {
		log_printf (LOG_LEVEL_NOTICE, "expirationset: no active replica, returning error.\n");
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
		ckpt_id = malloc (sizeof(struct ckpt_identifier));
		assert(ckpt_id);
		memcpy(&ckpt_id->ckpt_name,&req_exec_ckpt_sectionexpirationtimeset->checkpointName,sizeof(SaNameT));
		memcpy(&ckpt_id->ckpt_section_id, &ckptCheckpointSection->sectionDescriptor.sectionId,sizeof(SaCkptSectionIdT));
		log_printf (LOG_LEVEL_DEBUG, "CKPT: req_exec_ckpt_sectionexpirationtimeset Enqueuing Timer to Expire section %s in ckpt %s, ref = 0x%x\n",
			ckpt_id->ckpt_section_id.id,
			(char *)&ckpt_id->ckpt_name.value,
			ckpt_id);
		poll_timer_add (aisexec_poll_handle,
			abstime_to_msec (ckptCheckpointSection->sectionDescriptor.expirationTime),
			ckpt_id,
			timer_function_section_expire,
			&ckptCheckpointSection->expiration_timer);
		log_printf (LOG_LEVEL_DEBUG, "CKPT: req_exec_ckpt_sectionexpirationtimeset expiration timer = 0x%x\n", 
			ckptCheckpointSection->expiration_timer);
	}

error_exit:
	if (message_source_is_local(&req_exec_ckpt_sectionexpirationtimeset->source)) {
		res_lib_ckpt_sectionexpirationtimeset.header.size = sizeof (struct res_lib_ckpt_sectionexpirationtimeset);
		res_lib_ckpt_sectionexpirationtimeset.header.id = MESSAGE_RES_CKPT_CHECKPOINT_SECTIONEXPIRATIONTIMESET;
		res_lib_ckpt_sectionexpirationtimeset.header.error = error;

		openais_conn_send_response (
			req_exec_ckpt_sectionexpirationtimeset->source.conn,
			&res_lib_ckpt_sectionexpirationtimeset,
			sizeof (struct res_lib_ckpt_sectionexpirationtimeset));
	}
}

static int recovery_section_write(int sectionIdLen, 
	char* sectionId,
	SaNameT *checkpointName,
	void *newData,
	SaUint32T dataOffSet,
	SaUint32T dataSize) 
{
	struct saCkptCheckpoint *ckptCheckpoint;
	struct saCkptCheckpointSection *ckptCheckpointSection;
	int sizeRequired;	
	SaAisErrorT error = SA_AIS_OK;
	char *sd;	
	
	log_printf (LOG_LEVEL_DEBUG, "CKPT: recovery_section_write.\n");
	ckptCheckpoint = ckpt_checkpoint_find_global (checkpointName);
	if (ckptCheckpoint == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	/*
	 * Find checkpoint section to be written
	 */
	ckptCheckpointSection = ckpt_checkpoint_find_globalSection (ckptCheckpoint,
								sectionId,
								sectionIdLen);
	if (ckptCheckpointSection == 0) {		
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	/*
	 * If write would extend past end of section data, return error;
	 */
	sizeRequired = dataOffSet + dataSize;
	if (sizeRequired > ckptCheckpointSection->sectionDescriptor.sectionSize) {
		log_printf (LOG_LEVEL_ERROR,
			"recovery_section_write. write-past-end sizeRequired:(%d), dataOffSet:(%d), dataSize:(%d), sync-section-size:(%d)\n",
			sizeRequired, dataOffSet, dataSize,
			(int)ckptCheckpointSection->sectionDescriptor.sectionSize);
		error = SA_AIS_ERR_ACCESS;
		goto error_exit;		
	}
	
	/*
	 * Write checkpoint section to section data
	 */
	if (dataSize > 0) {			
		sd = (char *)ckptCheckpointSection->sectionData;
		memcpy (&sd[dataOffSet],
			newData,
			dataSize);
	}	
error_exit:	
	return (error);	
}


static void message_handler_req_exec_ckpt_sectionwrite (
	void *message,
	struct totem_ip_address *source_addr)
{
	struct req_exec_ckpt_sectionwrite *req_exec_ckpt_sectionwrite = (struct req_exec_ckpt_sectionwrite *)message;
	struct req_lib_ckpt_sectionwrite *req_lib_ckpt_sectionwrite = (struct req_lib_ckpt_sectionwrite *)&req_exec_ckpt_sectionwrite->req_lib_ckpt_sectionwrite;
	struct res_lib_ckpt_sectionwrite res_lib_ckpt_sectionwrite;
	struct saCkptCheckpoint *ckptCheckpoint;
	struct saCkptCheckpointSection *ckptCheckpointSection = 0;
	int sizeRequired;
	void *sectionData;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_DEBUG, "Executive request to section write.\n");
	ckptCheckpoint = ckpt_checkpoint_find_global (&req_exec_ckpt_sectionwrite->checkpointName);
	if (ckptCheckpoint == 0) {
		log_printf (LOG_LEVEL_ERROR, "CKPT: ckpt_checkpoint_find_global returned 0 Calling error_exit.\n");
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}
	if (ckptCheckpoint->active_replica_set == 0) {
		log_printf (LOG_LEVEL_NOTICE, "checkpointwrite: no active replica, returning error.\n");
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	if (ckptCheckpoint->checkpointCreationAttributes.maxSectionSize < req_lib_ckpt_sectionwrite->dataSize) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

/*
	printf ("writing checkpoint section is %s\n", ((char *)req_lib_ckpt_sectionwrite) + sizeof (struct req_lib_ckpt_sectionwrite));
*/
	/*
	 * Find checkpoint section to be written
	 */
	ckptCheckpointSection = ckpt_checkpoint_find_globalSection (ckptCheckpoint,
		((char *)req_lib_ckpt_sectionwrite) + sizeof (struct req_lib_ckpt_sectionwrite),
		req_lib_ckpt_sectionwrite->idLen);
	if (ckptCheckpointSection == 0) {
		if (req_lib_ckpt_sectionwrite->idLen == 0) {
			log_printf (LOG_LEVEL_DEBUG, "CANT FIND DEFAULT SECTION.\n");
		}
		else {
			log_printf (LOG_LEVEL_DEBUG, "CANT FIND SECTION '%s'\n",
				((char *)req_lib_ckpt_sectionwrite) + sizeof (struct req_lib_ckpt_sectionwrite));
		}
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
			log_printf (LOG_LEVEL_ERROR, "CKPT: sectionData realloc returned 0 Calling error_exit.\n");
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
	if (message_source_is_local(&req_exec_ckpt_sectionwrite->source)) {
		res_lib_ckpt_sectionwrite.header.size = sizeof (struct res_lib_ckpt_sectionwrite);
		res_lib_ckpt_sectionwrite.header.id = MESSAGE_RES_CKPT_CHECKPOINT_SECTIONWRITE;
		res_lib_ckpt_sectionwrite.header.error = error;

		openais_conn_send_response (
			req_exec_ckpt_sectionwrite->source.conn,
			&res_lib_ckpt_sectionwrite,
			sizeof (struct res_lib_ckpt_sectionwrite));
	}
}

static void message_handler_req_exec_ckpt_sectionoverwrite (
	void *message,
	struct totem_ip_address *source_addr)
{
	struct req_exec_ckpt_sectionoverwrite *req_exec_ckpt_sectionoverwrite = (struct req_exec_ckpt_sectionoverwrite *)message;
	struct req_lib_ckpt_sectionoverwrite *req_lib_ckpt_sectionoverwrite = (struct req_lib_ckpt_sectionoverwrite *)&req_exec_ckpt_sectionoverwrite->req_lib_ckpt_sectionoverwrite;
	struct res_lib_ckpt_sectionoverwrite res_lib_ckpt_sectionoverwrite;
	struct saCkptCheckpoint *ckptCheckpoint;
	struct saCkptCheckpointSection *ckptCheckpointSection;
	void *sectionData;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_DEBUG, "Executive request to section overwrite.\n");
	ckptCheckpoint = ckpt_checkpoint_find_global (&req_exec_ckpt_sectionoverwrite->checkpointName);
	if (ckptCheckpoint == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	if (ckptCheckpoint->checkpointCreationAttributes.maxSectionSize < req_lib_ckpt_sectionoverwrite->dataSize) {
		error = SA_AIS_ERR_INVALID_PARAM;
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
	ckptCheckpointSection->sectionDescriptor.lastUpdate = 0; /* TODO current time */
	ckptCheckpointSection->sectionData = sectionData;

	/*
	 * return result to CKPT library
	 */
error_exit:
	if (message_source_is_local(&req_exec_ckpt_sectionoverwrite->source)) {
		res_lib_ckpt_sectionoverwrite.header.size = sizeof (struct res_lib_ckpt_sectionoverwrite);
		res_lib_ckpt_sectionoverwrite.header.id = MESSAGE_RES_CKPT_CHECKPOINT_SECTIONOVERWRITE;
		res_lib_ckpt_sectionoverwrite.header.error = error;

		openais_conn_send_response (
			req_exec_ckpt_sectionoverwrite->source.conn,
			&res_lib_ckpt_sectionoverwrite,
			sizeof (struct res_lib_ckpt_sectionoverwrite));
	}
}

static void message_handler_req_exec_ckpt_sectionread (
	void *message,
	struct totem_ip_address *source_addr)
{
	struct req_exec_ckpt_sectionread *req_exec_ckpt_sectionread = (struct req_exec_ckpt_sectionread *)message;
	struct req_lib_ckpt_sectionread *req_lib_ckpt_sectionread = (struct req_lib_ckpt_sectionread *)&req_exec_ckpt_sectionread->req_lib_ckpt_sectionread;
	struct res_lib_ckpt_sectionread res_lib_ckpt_sectionread;
	struct saCkptCheckpoint *ckptCheckpoint;
	struct saCkptCheckpointSection *ckptCheckpointSection = 0;
	int sectionSize = 0;
	SaAisErrorT error = SA_AIS_OK;
	res_lib_ckpt_sectionread.dataRead = 0;

	log_printf (LOG_LEVEL_DEBUG, "Executive request for section read.\n");

	ckptCheckpoint = ckpt_checkpoint_find_global (&req_exec_ckpt_sectionread->checkpointName);
	if (ckptCheckpoint == 0) {
		error = SA_AIS_ERR_LIBRARY; /* TODO find the right error for this */
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
	if (message_source_is_local(&req_exec_ckpt_sectionread->source)) {
		res_lib_ckpt_sectionread.header.size = sizeof (struct res_lib_ckpt_sectionread) + sectionSize;
		res_lib_ckpt_sectionread.header.id = MESSAGE_RES_CKPT_CHECKPOINT_SECTIONREAD;
		res_lib_ckpt_sectionread.header.error = error;

		if (sectionSize != 0) {
			res_lib_ckpt_sectionread.dataRead = sectionSize;
		}
	
		openais_conn_send_response (
			req_exec_ckpt_sectionread->source.conn,
			&res_lib_ckpt_sectionread,
			sizeof (struct res_lib_ckpt_sectionread));

		/*
		 * Write checkpoint to CKPT library section if section has data
		 */
		if (sectionSize) {
			char *sd;
			sd = (char *)ckptCheckpointSection->sectionData;
			openais_conn_send_response (
				req_exec_ckpt_sectionread->source.conn,
				&sd[req_lib_ckpt_sectionread->dataOffset],
				sectionSize);
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
	struct iovec iovecs[2];

	log_printf (LOG_LEVEL_DEBUG, "Library request to open checkpoint.\n");
	req_exec_ckpt_checkpointopen.header.size =
		sizeof (struct req_exec_ckpt_checkpointopen);
	req_exec_ckpt_checkpointopen.header.id =
		SERVICE_ID_MAKE (CKPT_SERVICE, MESSAGE_REQ_EXEC_CKPT_CHECKPOINTOPEN);

	message_source_set (&req_exec_ckpt_checkpointopen.source, conn);

	memcpy (&req_exec_ckpt_checkpointopen.req_lib_ckpt_checkpointopen,
		req_lib_ckpt_checkpointopen,
		sizeof (struct req_lib_ckpt_checkpointopen));
	
	req_exec_ckpt_checkpointopen.async_call = 0;
	req_exec_ckpt_checkpointopen.invocation = 0;
	req_exec_ckpt_checkpointopen.checkpointHandle = 0;
	req_exec_ckpt_checkpointopen.fail_with_error = SA_AIS_OK;

	iovecs[0].iov_base = (char *)&req_exec_ckpt_checkpointopen;
	iovecs[0].iov_len = sizeof (req_exec_ckpt_checkpointopen);

	assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 1, TOTEMPG_AGREED) == 0);
}

static void message_handler_req_lib_ckpt_checkpointopenasync (
	void *conn,
	void *msg)
{
	struct req_lib_ckpt_checkpointopenasync *req_lib_ckpt_checkpointopenasync = (struct req_lib_ckpt_checkpointopenasync *)msg;
	struct req_exec_ckpt_checkpointopen req_exec_ckpt_checkpointopen;
	struct iovec iovecs[2];

	log_printf (LOG_LEVEL_DEBUG, "Library request to open checkpoint async.\n");
	req_exec_ckpt_checkpointopen.header.size =
		sizeof (struct req_exec_ckpt_checkpointopen);
	req_exec_ckpt_checkpointopen.header.id =
		SERVICE_ID_MAKE (CKPT_SERVICE, MESSAGE_REQ_EXEC_CKPT_CHECKPOINTOPEN);

	message_source_set (&req_exec_ckpt_checkpointopen.source, conn);

	memcpy (&req_exec_ckpt_checkpointopen.req_lib_ckpt_checkpointopen,
		req_lib_ckpt_checkpointopenasync,
		sizeof (struct req_lib_ckpt_checkpointopen));
	
	req_exec_ckpt_checkpointopen.async_call = 1;
	req_exec_ckpt_checkpointopen.invocation = req_lib_ckpt_checkpointopenasync->invocation;
	req_exec_ckpt_checkpointopen.checkpointHandle = req_lib_ckpt_checkpointopenasync->checkpointHandle;
	req_exec_ckpt_checkpointopen.fail_with_error = req_lib_ckpt_checkpointopenasync->fail_with_error;

	iovecs[0].iov_base = (char *)&req_exec_ckpt_checkpointopen;
	iovecs[0].iov_len = sizeof (req_exec_ckpt_checkpointopen);

	assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 1, TOTEMPG_AGREED) == 0);
}

static void message_handler_req_lib_ckpt_checkpointclose (
	void *conn,
	void *msg)
{
	struct req_lib_ckpt_checkpointclose *req_lib_ckpt_checkpointclose = (struct req_lib_ckpt_checkpointclose *)msg;
	struct req_exec_ckpt_checkpointclose req_exec_ckpt_checkpointclose;
	struct iovec iovecs[2];
	struct saCkptCheckpoint *checkpoint;
	struct res_lib_ckpt_checkpointclose res_lib_ckpt_checkpointclose;

	checkpoint = ckpt_checkpoint_find_global (&req_lib_ckpt_checkpointclose->checkpointName);
	if (checkpoint && (checkpoint->expired == 0)) {
		req_exec_ckpt_checkpointclose.header.size =
			sizeof (struct req_exec_ckpt_checkpointclose);
		req_exec_ckpt_checkpointclose.header.id =
			SERVICE_ID_MAKE (CKPT_SERVICE,
				 MESSAGE_REQ_EXEC_CKPT_CHECKPOINTCLOSE);

		message_source_set (&req_exec_ckpt_checkpointclose.source, conn);

		memcpy (&req_exec_ckpt_checkpointclose.checkpointName,
			&req_lib_ckpt_checkpointclose->checkpointName, sizeof (SaNameT));

		iovecs[0].iov_base = (char *)&req_exec_ckpt_checkpointclose;
		iovecs[0].iov_len = sizeof (req_exec_ckpt_checkpointclose);

		if (totempg_groups_send_ok_joined (openais_group_handle, iovecs, 1)) {
			assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 1, TOTEMPG_AGREED) == 0);
		}
	}
	else {
		log_printf (LOG_LEVEL_ERROR, "#### CKPT: Could Not Find the Checkpoint to close so Returning Error. ####\n");

		res_lib_ckpt_checkpointclose.header.size = sizeof (struct res_lib_ckpt_checkpointclose);
		res_lib_ckpt_checkpointclose.header.id = MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTCLOSE;
		res_lib_ckpt_checkpointclose.header.error = SA_AIS_ERR_NOT_EXIST;

		openais_conn_send_response (
			conn,
			&res_lib_ckpt_checkpointclose,
			sizeof (struct res_lib_ckpt_checkpointclose));
	}
}

static void message_handler_req_lib_ckpt_checkpointunlink (
	void *conn,
	void *msg)
{
	struct req_lib_ckpt_checkpointunlink *req_lib_ckpt_checkpointunlink = (struct req_lib_ckpt_checkpointunlink *)msg;
	struct req_exec_ckpt_checkpointunlink req_exec_ckpt_checkpointunlink;
	struct iovec iovecs[2];

	req_exec_ckpt_checkpointunlink.header.size =
		sizeof (struct req_exec_ckpt_checkpointunlink);
	req_exec_ckpt_checkpointunlink.header.id =
		SERVICE_ID_MAKE (CKPT_SERVICE, MESSAGE_REQ_EXEC_CKPT_CHECKPOINTUNLINK);

	message_source_set (&req_exec_ckpt_checkpointunlink.source, conn);

	memcpy (&req_exec_ckpt_checkpointunlink.req_lib_ckpt_checkpointunlink,
		req_lib_ckpt_checkpointunlink,
		sizeof (struct req_lib_ckpt_checkpointunlink));

	iovecs[0].iov_base = (char *)&req_exec_ckpt_checkpointunlink;
	iovecs[0].iov_len = sizeof (req_exec_ckpt_checkpointunlink);

	assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 1, TOTEMPG_AGREED) == 0);
}

static void message_handler_req_lib_ckpt_checkpointretentiondurationset (
	void *conn,
	void *msg)
{
	struct req_lib_ckpt_checkpointretentiondurationset *req_lib_ckpt_checkpointretentiondurationset = (struct req_lib_ckpt_checkpointretentiondurationset *)msg;
	struct req_exec_ckpt_checkpointretentiondurationset req_exec_ckpt_checkpointretentiondurationset;
	struct iovec iovecs[2];

	log_printf (LOG_LEVEL_DEBUG, "DURATION SET FROM API conn %p\n", conn);
	req_exec_ckpt_checkpointretentiondurationset.header.id =
		SERVICE_ID_MAKE (CKPT_SERVICE,
			MESSAGE_REQ_EXEC_CKPT_CHECKPOINTRETENTIONDURATIONSET);
	req_exec_ckpt_checkpointretentiondurationset.header.size = sizeof (struct req_exec_ckpt_checkpointretentiondurationset);

	message_source_set (&req_exec_ckpt_checkpointretentiondurationset.source, conn);

	memcpy (&req_exec_ckpt_checkpointretentiondurationset.checkpointName,
		&req_lib_ckpt_checkpointretentiondurationset->checkpointName,
		sizeof (SaNameT));
	req_exec_ckpt_checkpointretentiondurationset.retentionDuration = req_lib_ckpt_checkpointretentiondurationset->retentionDuration;

	iovecs[0].iov_base = (char *)&req_exec_ckpt_checkpointretentiondurationset;
	iovecs[0].iov_len = sizeof (req_exec_ckpt_checkpointretentiondurationset);

	assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 1, TOTEMPG_AGREED) == 0);

}

static void message_handler_req_lib_ckpt_activereplicaset (
	void *conn,
	void *msg)
{
	struct req_lib_ckpt_activereplicaset *req_lib_ckpt_activereplicaset = (struct req_lib_ckpt_activereplicaset *)msg;
	struct res_lib_ckpt_activereplicaset res_lib_ckpt_activereplicaset;
	struct saCkptCheckpoint *checkpoint;
	SaAisErrorT error = SA_AIS_OK;

	checkpoint = ckpt_checkpoint_find_global (&req_lib_ckpt_activereplicaset->checkpointName);

	/*
	 * Make sure checkpoint is collocated and async update option
	 */
	if (((checkpoint->checkpointCreationAttributes.creationFlags & SA_CKPT_CHECKPOINT_COLLOCATED) == 0) ||
		(checkpoint->checkpointCreationAttributes.creationFlags & (SA_CKPT_WR_ACTIVE_REPLICA | SA_CKPT_WR_ACTIVE_REPLICA_WEAK)) == 0) {
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
	struct saCkptCheckpoint *checkpoint;
	int memoryUsed = 0;
	int numberOfSections = 0;
	struct list_head *checkpoint_section_list;
	struct saCkptCheckpointSection *checkpointSection;

	log_printf (LOG_LEVEL_DEBUG, "in status get\n");

	/*
	 * Count memory used by checkpoint sections
	 */
	checkpoint = ckpt_checkpoint_find_global (&req_lib_ckpt_checkpointstatusget->checkpointName);
	
	if (checkpoint && (checkpoint->expired == 0)) {

		for (checkpoint_section_list = checkpoint->sections_list_head.next;
			checkpoint_section_list != &checkpoint->sections_list_head;
			checkpoint_section_list = checkpoint_section_list->next) {

			checkpointSection = list_entry (checkpoint_section_list,
				struct saCkptCheckpointSection, list);

			memoryUsed += checkpointSection->sectionDescriptor.sectionSize;
			numberOfSections += 1;
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

		memcpy (&res_lib_ckpt_checkpointstatusget.checkpointDescriptor.checkpointCreationAttributes,
			&checkpoint->checkpointCreationAttributes,
			sizeof (SaCkptCheckpointCreationAttributesT));
		res_lib_ckpt_checkpointstatusget.checkpointDescriptor.numberOfSections = numberOfSections;
		res_lib_ckpt_checkpointstatusget.checkpointDescriptor.memoryUsed = memoryUsed;
	}
	else {
		log_printf (LOG_LEVEL_ERROR, "#### CKPT: Could Not Find the Checkpoint's status so Returning Error. ####\n");

		res_lib_ckpt_checkpointstatusget.header.size = sizeof (struct res_lib_ckpt_checkpointstatusget);
		res_lib_ckpt_checkpointstatusget.header.id = MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTSTATUSGET;
		res_lib_ckpt_checkpointstatusget.header.error = SA_AIS_ERR_NOT_EXIST;
	}
	log_printf (LOG_LEVEL_DEBUG, "before sending message\n");
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
	struct saCkptCheckpoint *checkpoint;
	struct res_lib_ckpt_sectioncreate res_lib_ckpt_sectioncreate;

	log_printf (LOG_LEVEL_DEBUG, "Section create from conn %p\n", conn);

	checkpoint = ckpt_checkpoint_find_global (&req_lib_ckpt_sectioncreate->checkpointName);

	if (checkpoint && (checkpoint->expired == 0)) {
		/*
		 * checkpoint opened is writeable mode so send message to cluster
		 */
		req_exec_ckpt_sectioncreate.header.id =
			SERVICE_ID_MAKE (CKPT_SERVICE,
				MESSAGE_REQ_EXEC_CKPT_SECTIONCREATE);
		req_exec_ckpt_sectioncreate.header.size = sizeof (struct req_exec_ckpt_sectioncreate);

		memcpy (&req_exec_ckpt_sectioncreate.req_lib_ckpt_sectioncreate,
			req_lib_ckpt_sectioncreate,
			sizeof (struct req_lib_ckpt_sectioncreate));
	
		memcpy (&req_exec_ckpt_sectioncreate.checkpointName,
			&req_lib_ckpt_sectioncreate->checkpointName,
			sizeof (SaNameT));

		message_source_set (&req_exec_ckpt_sectioncreate.source, conn);

		iovecs[0].iov_base = (char *)&req_exec_ckpt_sectioncreate;
		iovecs[0].iov_len = sizeof (req_exec_ckpt_sectioncreate);
		/*
		 * Send section name and initial data in message
		 */
		iovecs[1].iov_base = ((char *)req_lib_ckpt_sectioncreate) + sizeof (struct req_lib_ckpt_sectioncreate);
		iovecs[1].iov_len = req_lib_ckpt_sectioncreate->header.size - sizeof (struct req_lib_ckpt_sectioncreate);
		req_exec_ckpt_sectioncreate.header.size += iovecs[1].iov_len;
	
		if (iovecs[1].iov_len) {
			log_printf (LOG_LEVEL_DEBUG, "CKPT: message_handler_req_lib_ckpt_sectioncreate Section = %s, idLen = %d\n",
				iovecs[1].iov_base,
				iovecs[1].iov_len);
		}
	
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
			assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 2, TOTEMPG_AGREED) == 0);
		} else {
			assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 1, TOTEMPG_AGREED) == 0);
		}

	}
	else {
		log_printf (LOG_LEVEL_ERROR, "#### CKPT: Could Not Find the Checkpoint to create a section in so Returning Error. ####\n");

		res_lib_ckpt_sectioncreate.header.size = sizeof (struct res_lib_ckpt_sectioncreate);
		res_lib_ckpt_sectioncreate.header.id = MESSAGE_RES_CKPT_CHECKPOINT_SECTIONCREATE;
		res_lib_ckpt_sectioncreate.header.error = SA_AIS_ERR_NOT_EXIST;

		openais_conn_send_response (
			conn,
			&res_lib_ckpt_sectioncreate,
			sizeof (struct res_lib_ckpt_sectioncreate));
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

	memcpy (&req_exec_ckpt_sectiondelete.checkpointName,
		&req_lib_ckpt_sectiondelete->checkpointName,
		sizeof (SaNameT));

	memcpy (&req_exec_ckpt_sectiondelete.req_lib_ckpt_sectiondelete,
		req_lib_ckpt_sectiondelete,
		sizeof (struct req_lib_ckpt_sectiondelete));

	message_source_set (&req_exec_ckpt_sectiondelete.source, conn);

	iovecs[0].iov_base = (char *)&req_exec_ckpt_sectiondelete;
	iovecs[0].iov_len = sizeof (req_exec_ckpt_sectiondelete);

	/*
	 * Send section name
	 */
	iovecs[1].iov_base = ((char *)req_lib_ckpt_sectiondelete) + sizeof (struct req_lib_ckpt_sectiondelete);
	iovecs[1].iov_len = req_lib_ckpt_sectiondelete->header.size - sizeof (struct req_lib_ckpt_sectiondelete);
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

	memcpy (&req_exec_ckpt_sectionexpirationtimeset.checkpointName,
		&req_lib_ckpt_sectionexpirationtimeset->checkpointName,
		sizeof (SaNameT));

	memcpy (&req_exec_ckpt_sectionexpirationtimeset.req_lib_ckpt_sectionexpirationtimeset,
		req_lib_ckpt_sectionexpirationtimeset,
		sizeof (struct req_lib_ckpt_sectionexpirationtimeset));

	message_source_set (&req_exec_ckpt_sectionexpirationtimeset.source, conn);

	iovecs[0].iov_base = (char *)&req_exec_ckpt_sectionexpirationtimeset;
	iovecs[0].iov_len = sizeof (req_exec_ckpt_sectionexpirationtimeset);

	/*
	 * Send section name
	 */
	iovecs[1].iov_base = ((char *)req_lib_ckpt_sectionexpirationtimeset) + sizeof (struct req_lib_ckpt_sectionexpirationtimeset);
	iovecs[1].iov_len = req_lib_ckpt_sectionexpirationtimeset->header.size - sizeof (struct req_lib_ckpt_sectionexpirationtimeset);
	req_exec_ckpt_sectionexpirationtimeset.header.size += iovecs[1].iov_len;

	if (iovecs[1].iov_len > 0) {
		log_printf (LOG_LEVEL_DEBUG, "IOV_BASE is %p\n", iovecs[1].iov_base);
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
	struct saCkptCheckpoint *checkpoint;
	struct res_lib_ckpt_sectionwrite res_lib_ckpt_sectionwrite;
	
	log_printf (LOG_LEVEL_DEBUG, "CKPT: Received data from lib with len = %d and ref = 0x%x\n",
			req_lib_ckpt_sectionwrite->dataSize,
		 	req_lib_ckpt_sectionwrite->dataOffset);

	log_printf (LOG_LEVEL_DEBUG, "CKPT: Checkpoint section being written to is %s, idLen = %d\n",
			((char *)req_lib_ckpt_sectionwrite) + sizeof (struct req_lib_ckpt_sectionwrite),
			req_lib_ckpt_sectionwrite->idLen); 

	log_printf (LOG_LEVEL_DEBUG, "Section write from conn %p\n", conn);
	checkpoint = ckpt_checkpoint_find_global (&req_lib_ckpt_sectionwrite->checkpointName);

	if (checkpoint && (checkpoint->expired == 0)) {
		/*
		 * checkpoint opened is writeable mode so send message to cluster
		 */
		req_exec_ckpt_sectionwrite.header.id =
			SERVICE_ID_MAKE (CKPT_SERVICE,
				MESSAGE_REQ_EXEC_CKPT_SECTIONWRITE);
		req_exec_ckpt_sectionwrite.header.size = sizeof (struct req_exec_ckpt_sectionwrite); 

		memcpy (&req_exec_ckpt_sectionwrite.req_lib_ckpt_sectionwrite,
			req_lib_ckpt_sectionwrite,
			sizeof (struct req_lib_ckpt_sectionwrite));
	
		memcpy (&req_exec_ckpt_sectionwrite.checkpointName,
			&req_lib_ckpt_sectionwrite->checkpointName,
			sizeof (SaNameT));
	
		message_source_set (&req_exec_ckpt_sectionwrite.source, conn);
	
		iovecs[0].iov_base = (char *)&req_exec_ckpt_sectionwrite;
		iovecs[0].iov_len = sizeof (req_exec_ckpt_sectionwrite);
		/*
		 * Send section name and data to write in message
		 */
		iovecs[1].iov_base = ((char *)req_lib_ckpt_sectionwrite) + sizeof (struct req_lib_ckpt_sectionwrite);
		iovecs[1].iov_len = req_lib_ckpt_sectionwrite->header.size - sizeof (struct req_lib_ckpt_sectionwrite);
		req_exec_ckpt_sectionwrite.header.size += iovecs[1].iov_len;
	
		if (iovecs[1].iov_len > 0) {
			assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 2, TOTEMPG_AGREED) == 0);
		} else {
			assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 1, TOTEMPG_AGREED) == 0);
		}
	}	
	else {
		log_printf (LOG_LEVEL_ERROR, "#### CKPT: Could Not Find the Checkpoint to write to Returning Error. ####\n");

		res_lib_ckpt_sectionwrite.header.size = sizeof (struct res_lib_ckpt_sectionwrite);
		res_lib_ckpt_sectionwrite.header.id = MESSAGE_RES_CKPT_CHECKPOINT_SECTIONWRITE;
		res_lib_ckpt_sectionwrite.header.error = SA_AIS_ERR_NOT_EXIST;

		openais_conn_send_response (
			conn,
			&res_lib_ckpt_sectionwrite,
			sizeof (struct res_lib_ckpt_sectionwrite));	
	}
}

static void message_handler_req_lib_ckpt_sectionoverwrite (
	void *conn,
	void *msg)
{
	struct req_lib_ckpt_sectionoverwrite *req_lib_ckpt_sectionoverwrite = (struct req_lib_ckpt_sectionoverwrite *)msg;
	struct req_exec_ckpt_sectionoverwrite req_exec_ckpt_sectionoverwrite;
	struct iovec iovecs[2];
	struct saCkptCheckpoint *checkpoint;
	struct res_lib_ckpt_sectionoverwrite res_lib_ckpt_sectionoverwrite;

	log_printf (LOG_LEVEL_DEBUG, "Section overwrite from conn %p\n", conn);
	checkpoint = ckpt_checkpoint_find_global (&req_lib_ckpt_sectionoverwrite->checkpointName);

	if (checkpoint && (checkpoint->expired == 0) && (checkpoint->active_replica_set == 1)) {
		/*
		 * checkpoint opened is writeable mode so send message to cluster
		 */
		req_exec_ckpt_sectionoverwrite.header.id =
			SERVICE_ID_MAKE (CKPT_SERVICE,
				MESSAGE_REQ_EXEC_CKPT_SECTIONOVERWRITE);
		req_exec_ckpt_sectionoverwrite.header.size = sizeof (struct req_exec_ckpt_sectionoverwrite); 
	
		memcpy (&req_exec_ckpt_sectionoverwrite.req_lib_ckpt_sectionoverwrite,
			req_lib_ckpt_sectionoverwrite,
			sizeof (struct req_lib_ckpt_sectionoverwrite));
	
		memcpy (&req_exec_ckpt_sectionoverwrite.checkpointName,
			&req_lib_ckpt_sectionoverwrite->checkpointName,
			sizeof (SaNameT));

		message_source_set (&req_exec_ckpt_sectionoverwrite.source, conn);
	
		iovecs[0].iov_base = (char *)&req_exec_ckpt_sectionoverwrite;
		iovecs[0].iov_len = sizeof (req_exec_ckpt_sectionoverwrite);
		/*
		 * Send section name and data to overwrite in message
		 */
		iovecs[1].iov_base = ((char *)req_lib_ckpt_sectionoverwrite) + sizeof (struct req_lib_ckpt_sectionoverwrite);
		iovecs[1].iov_len = req_lib_ckpt_sectionoverwrite->header.size - sizeof (struct req_lib_ckpt_sectionoverwrite);
		req_exec_ckpt_sectionoverwrite.header.size += iovecs[1].iov_len;
	
		if (iovecs[1].iov_len > 0) {
			assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 2, TOTEMPG_AGREED) == 0);
		} else {
			assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 1, TOTEMPG_AGREED) == 0);
		}
	}
	else {
		log_printf (LOG_LEVEL_ERROR, "#### CKPT: Could Not Find the Checkpoint to over write so Returning Error. ####\n");

		res_lib_ckpt_sectionoverwrite.header.size = sizeof (struct res_lib_ckpt_sectionwrite);
		res_lib_ckpt_sectionoverwrite.header.id = MESSAGE_RES_CKPT_CHECKPOINT_SECTIONOVERWRITE;
                res_lib_ckpt_sectionoverwrite.header.error = SA_AIS_ERR_NOT_EXIST;

                openais_conn_send_response (
			conn,
                        &res_lib_ckpt_sectionoverwrite,
                        sizeof (struct res_lib_ckpt_sectionoverwrite));
        }
}

static void message_handler_req_lib_ckpt_sectionread (
	void *conn,
	void *msg)
{
	struct req_lib_ckpt_sectionread *req_lib_ckpt_sectionread = (struct req_lib_ckpt_sectionread *)msg;
	struct req_exec_ckpt_sectionread req_exec_ckpt_sectionread;
	struct iovec iovecs[2];
	struct saCkptCheckpoint *checkpoint;
	struct res_lib_ckpt_sectionread res_lib_ckpt_sectionread;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_DEBUG, "Section read from conn %p\n", conn);
	checkpoint = ckpt_checkpoint_find_global (&req_lib_ckpt_sectionread->checkpointName);
	
	if (checkpoint && (checkpoint->expired == 0) &&
		checkpoint->active_replica_set && 

		req_lib_ckpt_sectionread->dataSize <=
			checkpoint->checkpointCreationAttributes.maxSectionSize) {

		/*
		 * checkpoint opened is writeable mode so send message to cluster
		 */
		req_exec_ckpt_sectionread.header.id =
			SERVICE_ID_MAKE (CKPT_SERVICE,
				MESSAGE_REQ_EXEC_CKPT_SECTIONREAD);
		req_exec_ckpt_sectionread.header.size = sizeof (struct req_exec_ckpt_sectionread);

		memcpy (&req_exec_ckpt_sectionread.req_lib_ckpt_sectionread,
			req_lib_ckpt_sectionread,
			sizeof (struct req_lib_ckpt_sectionread));
	
		memcpy (&req_exec_ckpt_sectionread.checkpointName,
			&req_lib_ckpt_sectionread->checkpointName,
			sizeof (SaNameT));
	
		message_source_set (&req_exec_ckpt_sectionread.source, conn);
	
		iovecs[0].iov_base = (char *)&req_exec_ckpt_sectionread;
		iovecs[0].iov_len = sizeof (req_exec_ckpt_sectionread);
		/*
		 * Send section name and data to overwrite in message
		 */
		iovecs[1].iov_base = ((char *)req_lib_ckpt_sectionread) + sizeof (struct req_lib_ckpt_sectionread);
		iovecs[1].iov_len = req_lib_ckpt_sectionread->header.size - sizeof (struct req_lib_ckpt_sectionread);
		req_exec_ckpt_sectionread.header.size += iovecs[1].iov_len;
	
		if (iovecs[1].iov_len > 0) {
			assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 2, TOTEMPG_AGREED) == 0);
		} else {
			assert (totempg_groups_mcast_joined (openais_group_handle, iovecs, 1, TOTEMPG_AGREED) == 0);
		}
		return;
	}

	if (checkpoint) {
		if (checkpoint->active_replica_set == 0) {
			log_printf (LOG_LEVEL_DEBUG,
				"sectionread - active replica not set.\n");
			error = SA_AIS_ERR_NOT_EXIST;
		}

		if (req_lib_ckpt_sectionread->dataSize >
			checkpoint->checkpointCreationAttributes.maxSectionSize) {

			log_printf (LOG_LEVEL_DEBUG,
				"sectionread - data size greater then max section size.\n");
			error = SA_AIS_ERR_INVALID_PARAM;
		}
	} else {
		log_printf (LOG_LEVEL_ERROR,
			"sectionread - could not find checkpoint.\n");
	}
		
	res_lib_ckpt_sectionread.header.size = sizeof (struct res_lib_ckpt_sectionread);
	res_lib_ckpt_sectionread.header.id = MESSAGE_RES_CKPT_CHECKPOINT_SECTIONREAD;
	res_lib_ckpt_sectionread.header.error = error;

	openais_conn_send_response (
		conn,
		&res_lib_ckpt_sectionread,
		sizeof (struct res_lib_ckpt_sectionread));
}

static void message_handler_req_lib_ckpt_checkpointsynchronize (
	void *conn,
	void *msg)
{
	struct req_lib_ckpt_checkpointsynchronize *req_lib_ckpt_checkpointsynchronize = (struct req_lib_ckpt_checkpointsynchronize *)msg;
	struct res_lib_ckpt_checkpointsynchronize res_lib_ckpt_checkpointsynchronize;
	struct saCkptCheckpoint *checkpoint;

	checkpoint = ckpt_checkpoint_find_global (&req_lib_ckpt_checkpointsynchronize->checkpointName);
	if ((checkpoint->checkpointCreationAttributes.creationFlags & (SA_CKPT_WR_ACTIVE_REPLICA | SA_CKPT_WR_ACTIVE_REPLICA_WEAK)) == 0) {
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
	struct saCkptCheckpoint *checkpoint;

	checkpoint = ckpt_checkpoint_find_global (&req_lib_ckpt_checkpointsynchronizeasync->checkpointName);
	if ((checkpoint->checkpointCreationAttributes.creationFlags & (SA_CKPT_WR_ACTIVE_REPLICA | SA_CKPT_WR_ACTIVE_REPLICA_WEAK)) == 0) {
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
	struct saCkptCheckpoint *checkpoint;
	struct saCkptCheckpointSection *checkpoint_section;
	struct iteration_entry *iteration_entries;
	struct list_head *section_list;
	struct iteration_instance *iteration_instance;
	unsigned int iteration_handle = 0;
	int res;
	SaAisErrorT error = SA_AIS_OK;

	struct ckpt_pd *ckpt_pd = (struct ckpt_pd *)openais_conn_private_data_get (conn);

	log_printf (LOG_LEVEL_DEBUG, "section iteration initialize\n");

	checkpoint = ckpt_checkpoint_find_global (&req_lib_ckpt_sectioniterationinitialize->checkpointName);
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
		(void **)&iteration_instance);
	if (res != 0) {
		hdb_handle_destroy (&ckpt_pd->iteration_hdb, iteration_handle);
		goto error_exit;
	}

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
			struct saCkptCheckpointSection, list);

		switch (req_lib_ckpt_sectioniterationinitialize->sectionsChosen) {
		case SA_CKPT_SECTIONS_FOREVER:
			if (checkpoint_section->sectionDescriptor.expirationTime != SA_TIME_END) {
				continue;
			}
			break;
		case SA_CKPT_SECTIONS_LEQ_EXPIRATION_TIME:
			if (checkpoint_section->sectionDescriptor.expirationTime > req_lib_ckpt_sectioniterationinitialize->expirationTime) {
				continue;
			}
			break;
		case SA_CKPT_SECTIONS_GEQ_EXPIRATION_TIME:
			if (checkpoint_section->sectionDescriptor.expirationTime < req_lib_ckpt_sectioniterationinitialize->expirationTime) {
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
	unsigned int res;

	struct ckpt_pd *ckpt_pd = (struct ckpt_pd *)openais_conn_private_data_get (conn);

	res = hdb_handle_get (&ckpt_pd->iteration_hdb,
		req_lib_ckpt_sectioniterationfinalize->iteration_handle,
		(void **)&iteration_instance);
	if (res != 0) {
		error = SA_AIS_ERR_LIBRARY;
		goto error_exit;
	}

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
	int sectionIdSize = 0;
	unsigned int res;
	struct iteration_instance *iteration_instance;

	struct ckpt_pd *ckpt_pd = (struct ckpt_pd *)openais_conn_private_data_get (conn);

	log_printf (LOG_LEVEL_DEBUG, "section iteration next\n");
	res = hdb_handle_get (&ckpt_pd->iteration_hdb,
		req_lib_ckpt_sectioniterationnext->iteration_handle,
		(void **)&iteration_instance);
	if (res != 0) {
		error = SA_AIS_ERR_LIBRARY;
		goto error_exit;
	}

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
	sectionIdSize = iteration_instance->iteration_entries[iteration_instance->iteration_pos].checkpoint_section->sectionDescriptor.sectionId.idLen;

	memcpy (&res_lib_ckpt_sectioniterationnext.sectionDescriptor, 
		&iteration_instance->iteration_entries[iteration_instance->iteration_pos].checkpoint_section->sectionDescriptor,
		sizeof (SaCkptSectionDescriptorT));

	/*
	 * Get to next iteration entry
	 */
	iteration_instance->iteration_pos += 1;

error_put:
	hdb_handle_put (&ckpt_pd->iteration_hdb, req_lib_ckpt_sectioniterationnext->iteration_handle);

error_exit:
	res_lib_ckpt_sectioniterationnext.header.size = sizeof (struct res_lib_ckpt_sectioniterationnext) + sectionIdSize;
	res_lib_ckpt_sectioniterationnext.header.id = MESSAGE_RES_CKPT_SECTIONITERATIONNEXT;
	res_lib_ckpt_sectioniterationnext.header.error = error;

	openais_conn_send_response (
		conn,
		&res_lib_ckpt_sectioniterationnext,
		sizeof (struct res_lib_ckpt_sectioniterationnext));

	openais_conn_send_response (
		conn,
		iteration_instance->iteration_entries[
			iteration_instance->iteration_pos - 1].
				checkpoint_section->sectionDescriptor.sectionId.id,
			sectionIdSize);
}
