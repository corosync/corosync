/*
 * Copyright (c) 2010 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Angus Salkeld <asalkeld@redhat.com>
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

#include <config.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <sys/uio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>

#include <corosync/swab.h>
#include <corosync/corotypes.h>
#include <corosync/coroipc_types.h>
#include <corosync/corodefs.h>
#include <corosync/lcr/lcr_comp.h>
#include <corosync/mar_gen.h>
#include <corosync/engine/coroapi.h>
#include <corosync/list.h>
#include <corosync/engine/logsys.h>

#include "../exec/tlist.h"

LOGSYS_DECLARE_SUBSYS ("TST2");

/*
 * Service Interfaces required by service_message_handler struct
 */
static int tst_sv2_exec_init_fn (
	struct corosync_api_v1 *corosync_api);

static void tst_sv2_confchg_fn (
	enum totem_configuration_type configuration_type,
	const unsigned int *member_list, size_t member_list_entries,
	const unsigned int *left_list, size_t left_list_entries,
	const unsigned int *joined_list, size_t joined_list_entries,
	const struct memb_ring_id *ring_id);

static int tst_sv2_lib_init_fn (void *conn);
static int tst_sv2_lib_exit_fn (void *conn);
static struct corosync_api_v1 *api;

static void tst_sv2_sync_init_v2 (
	const unsigned int *member_list,
	size_t member_list_entries,
	const struct memb_ring_id *ring_id);

static int  tst_sv2_sync_process (void);

static void tst_sv2_sync_activate (void);

static void tst_sv2_sync_abort (void);

struct corosync_service_engine tst_sv2_service_engine = {
	.name			= "corosync test synv2 service",
	.id			= TST_SV2_SERVICE,
	.priority		= 1,
	.private_data_size	= 0,
	.flow_control		= CS_LIB_FLOW_CONTROL_NOT_REQUIRED,
	.lib_init_fn		= tst_sv2_lib_init_fn,
	.lib_exit_fn		= tst_sv2_lib_exit_fn,
	.lib_engine		= NULL,
	.lib_engine_count	= 0,
	.exec_engine		= NULL,
	.exec_engine_count	= 0,
	.confchg_fn		= tst_sv2_confchg_fn,
	.exec_init_fn		= tst_sv2_exec_init_fn,
	.exec_dump_fn		= NULL,
	.sync_mode		= CS_SYNC_V2,
	.sync_init              = tst_sv2_sync_init_v2,
	.sync_process           = tst_sv2_sync_process,
	.sync_activate          = tst_sv2_sync_activate,
	.sync_abort             = tst_sv2_sync_abort
};

static unsigned int my_member_list[PROCESSOR_COUNT_MAX];

static unsigned int my_member_list_entries;

static unsigned int my_old_member_list[PROCESSOR_COUNT_MAX];

static unsigned int my_old_member_list_entries = 0;
static int num_sync_processes = 0;

static DECLARE_LIST_INIT (confchg_notify);

/*
 * Dynamic loading descriptor
 */

static struct corosync_service_engine *tst_sv2_get_service_engine_ver0 (void);

static struct corosync_service_engine_iface_ver0 tst_sv2_service_engine_iface = {
	.corosync_get_service_engine_ver0	= tst_sv2_get_service_engine_ver0
};

static struct lcr_iface corosync_tst_sv2_ver0[1] = {
	{
		.name			= "corosync_tst_sv2",
		.version		= 0,
		.versions_replace	= 0,
		.versions_replace_count = 0,
		.dependencies		= 0,
		.dependency_count	= 0,
		.constructor		= NULL,
		.destructor		= NULL,
		.interfaces		= NULL,
	}
};

static struct lcr_comp tst_sv2_comp_ver0 = {
	.iface_count	= 1,
	.ifaces		= corosync_tst_sv2_ver0
};

static struct corosync_service_engine *tst_sv2_get_service_engine_ver0 (void)
{
	return (&tst_sv2_service_engine);
}

#ifdef COROSYNC_SOLARIS
void corosync_lcr_component_register (void);

void corosync_lcr_component_register (void) {
#else
__attribute__ ((constructor)) static void corosync_lcr_component_register (void) {
#endif
	lcr_interfaces_set (&corosync_tst_sv2_ver0[0], &tst_sv2_service_engine_iface);

	lcr_component_register (&tst_sv2_comp_ver0);
}

static int tst_sv2_exec_init_fn (
	struct corosync_api_v1 *corosync_api)
{
#ifdef COROSYNC_SOLARIS
	logsys_subsys_init();
#endif
	api = corosync_api;

	return 0;
}

static void tst_sv2_confchg_fn (
	enum totem_configuration_type configuration_type,
	const unsigned int *member_list, size_t member_list_entries,
	const unsigned int *left_list, size_t left_list_entries,
	const unsigned int *joined_list, size_t joined_list_entries,
	const struct memb_ring_id *ring_id)
{
	int j;
	for (j = 0; j < left_list_entries; j++) {
		log_printf (LOGSYS_LEVEL_INFO,
			"Member left: %s", api->totem_ifaces_print (left_list[j]));
	}
	for (j = 0; j < joined_list_entries; j++) {
		log_printf (LOGSYS_LEVEL_INFO,
			"Member joined: %s", api->totem_ifaces_print (joined_list[j]));
	}
}

static int tst_sv2_lib_init_fn (void *conn)
{
	return (0);
}

static int tst_sv2_lib_exit_fn (void *conn)
{
	return (0);
}


static void tst_sv2_sync_init_v2 (
	const unsigned int *member_list,
	size_t member_list_entries,
	const struct memb_ring_id *ring_id)
{
	unsigned int lowest_nodeid = 0xffffffff;
	int i, j;
	int found;

	num_sync_processes = 0;

	memcpy (my_member_list, member_list, member_list_entries *
		sizeof (unsigned int));
	my_member_list_entries = member_list_entries;

	for (i = 0; i < my_member_list_entries; i++) {
		if (my_member_list[i] < lowest_nodeid) {
			lowest_nodeid = my_member_list[i];
		}
	}

	log_printf (LOGSYS_LEVEL_INFO,
	       "tst_sv2_sync_init_v2 %s",
	       api->totem_ifaces_print (lowest_nodeid));

	/* look for new (joined) nodes */
	for (j = 0; j < member_list_entries; j++) {
		found = 0;
		for (i = 0; i < my_old_member_list_entries; i++) {
			if (my_old_member_list[i] == member_list[j]) {
				found = 1;
				break;
			}
		}
		if (found == 0) {
			log_printf (LOGSYS_LEVEL_INFO,
				"sync: node joined %s",
				api->totem_ifaces_print (member_list[j]));
		}
	}
	/* look for old (left) nodes */
	for (i = 0; i < my_old_member_list_entries; i++) {
		found = 0;
		for (j = 0; j < member_list_entries; j++) {
			if (my_old_member_list[i] == member_list[j]) {
				found = 1;
				break;
			}
		}
		if (found == 0) {
			log_printf (LOGSYS_LEVEL_INFO,
			       "sync: node left %s",
			       api->totem_ifaces_print (my_old_member_list[i]));
		}
	}

}

static int tst_sv2_sync_process (void)
{
	num_sync_processes++;

	log_printf (LOGSYS_LEVEL_INFO, "sync: process %d", num_sync_processes);

	if (num_sync_processes > 3) {
		return 0;
	} else {
		return 1;
	}
}

static void tst_sv2_sync_activate (void)
{
	memcpy (my_old_member_list, my_member_list,
		my_member_list_entries * sizeof (unsigned int));
	my_old_member_list_entries = my_member_list_entries;

	if (num_sync_processes <= 3) {
		log_printf (LOGSYS_LEVEL_ERROR,
			"sync: activate called before process is done %d",
		       num_sync_processes);
	} else {
		log_printf (LOGSYS_LEVEL_INFO,
			"sync: activate correctly %d",
		       num_sync_processes);
	}

	num_sync_processes = 0;
}

static void tst_sv2_sync_abort (void)
{
	log_printf (LOGSYS_LEVEL_INFO, "sync: abort");
}

