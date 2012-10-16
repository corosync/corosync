/*
 * Copyright (c) 2002-2006 MontaVista Software, Inc.
 * Copyright (c) 2006-2009 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)
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
/*
 * Don't look, you will be disappointed
 */

#include <config.h>

#include <pthread.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#if defined(COROSYNC_BSD) || defined(COROSYNC_DARWIN)
#include <sys/sysctl.h>
#else
#include <sys/sysinfo.h>
#endif
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sched.h>
#include <time.h>

#include <corosync/totem/totempg.h>
#include <corosync/swab.h>
#include <corosync/corotypes.h>
#include <corosync/coroipc_types.h>
#include <corosync/corodefs.h>
#include <corosync/list.h>
#include <corosync/lcr/lcr_ifact.h>
#include <corosync/engine/objdb.h>
#include <corosync/engine/config.h>
#include <corosync/engine/coroapi.h>
#include <corosync/engine/logsys.h>
#include <corosync/coroipcs.h>
#include "sync.h"
#include "evil.h"

static unsigned int my_evt_checked_in = 0;

static unsigned int my_member_list_entries;

static struct corosync_api_v1 *api = NULL;

static void clm_sync_init (
	const unsigned int *member_list,
	size_t member_list_entries,
	const struct memb_ring_id *ring_id);

static int clm_sync_process (void);

static void clm_sync_activate (void);

static void clm_sync_abort (void);

static void evt_sync_init (
	const unsigned int *member_list,
	size_t member_list_entries,
	const struct memb_ring_id *ring_id);

static int evt_sync_process (void);

static void evt_sync_activate (void);

static void evt_sync_abort (void);

static int clm_hack_init (void);

static void deliver_fn_evt_compat (
	unsigned int nodeid,
	unsigned int service,
	unsigned int fn_id,
	const void *msg,
	unsigned int endian_conversion_required);

static struct sync_callbacks clm_sync_operations = {
	.api_version		= 1,
	.name			= "dummy CLM service",
	.sync_init_api.sync_init_v1 = clm_sync_init,
	.sync_process		= clm_sync_process,
	.sync_activate		= clm_sync_activate,
	.sync_abort		= clm_sync_abort,
};

static struct sync_callbacks evt_sync_operations = {
	.api_version		= 1,
	.name			= "dummy EVT service",
	.sync_init_api.sync_init_v1 = evt_sync_init,
	.sync_process		= evt_sync_process,
	.sync_activate		= evt_sync_activate,
	.sync_abort		= evt_sync_abort,
};


static void sync_dummy_init (
	const unsigned int *member_list,
	size_t member_list_entries,
	const struct memb_ring_id *ring_id)
{
}

static int sync_dummy_process (void)
{
	return (0);
}

static void sync_dummy_activate (void)
{
}

static void sync_dummy_abort (void)
{
}

void evil_init (struct corosync_api_v1 *api_in)
{
	api = api_in;
	clm_hack_init ();
}

extern int evil_callbacks_load (int sync_id,
	struct sync_callbacks *callbacks)
{
	int sync_dummy_found = 1;
	int callbacks_init = 1;

	if (callbacks != NULL) {
		memset (callbacks, 0, sizeof (struct sync_callbacks));
	}

	switch (sync_id) {
		case EVS_SERVICE:
			callbacks_init = 0;
			break;
		case CLM_SERVICE:
			/*
			 * ugh
			 */
			if (callbacks != NULL) {
				memcpy (callbacks, &clm_sync_operations, sizeof (struct sync_callbacks));
			}
			callbacks_init = 0;
			break;
		case AMF_SERVICE:
			if (callbacks != NULL) {
				callbacks->name = "dummy AMF service";
			}
			break;
		case CKPT_SERVICE:
			if (callbacks != NULL) {
				callbacks->name = "dummy CKPT service";
			}
			break;
		case EVT_SERVICE:
			/*
			 * double ugh
			 */
			if (callbacks != NULL) {
				memcpy (callbacks, &evt_sync_operations, sizeof (struct sync_callbacks));
			}
			callbacks_init = 0;
			break;
		case LCK_SERVICE:
			callbacks_init = 0;
			break;
		case MSG_SERVICE:
			callbacks_init = 0;
			break;
		case CFG_SERVICE:
			callbacks_init = 0;
			break;
		case CPG_SERVICE:
			if (callbacks != NULL) {
				callbacks->name = "dummy CPG service";
			} else {
				return (-1);
			}
			break;
		case CONFDB_SERVICE:
			callbacks_init = 0;
			break;
		default:
			callbacks_init = 0;
			sync_dummy_found = 0;
			break;

	}
	if (callbacks_init && callbacks != NULL) {
		callbacks->sync_init_api.sync_init_v1 = sync_dummy_init;
		callbacks->sync_process = sync_dummy_process;
		callbacks->sync_activate = sync_dummy_activate;
		callbacks->sync_abort = sync_dummy_abort;
	}

	if (sync_dummy_found == 0) {
		return (-1);
	}
	return (0);
}

void evil_deliver_fn (
	unsigned int nodeid,
	unsigned int service,
	unsigned int fn_id,
	const void *msg,
	unsigned int endian_conversion_required)
{
	if (service == EVT_SERVICE) {
		deliver_fn_evt_compat (
			nodeid,
			service,
			fn_id,
			msg,
			endian_conversion_required);
	}
}

/*
 * This sends the clm nodejoin message required by clm services
 * on whitetank as well as the event service
 */
enum clm_message_req_types {
	MESSAGE_REQ_EXEC_CLM_NODEJOIN = 0
};

#define MAR_CLM_MAX_ADDRESS_LENGTH 64

#define SA_MAX_NAME_LENGTH 256

typedef struct {
	int size __attribute__((aligned(8)));
	int id __attribute__((aligned(8)));
} mar_req_header_t __attribute__((aligned(8)));

typedef enum {
	MAR_CLM_AF_INET = 1,
	MAR_CLM_AF_INET6 = 2
} mar_clm_node_address_family_t;

typedef struct {
	unsigned short length __attribute__((aligned(8)));
	mar_clm_node_address_family_t family __attribute__((aligned(8)));
	unsigned char value[MAR_CLM_MAX_ADDRESS_LENGTH] __attribute__((aligned(8)));
} mar_clm_node_address_t;

typedef uint8_t mar_uint8_t;

typedef uint16_t mar_uint16_t;

typedef uint32_t mar_uint32_t;

typedef uint64_t mar_uint64_t;

typedef struct {
	mar_uint16_t length __attribute__((aligned(8)));
	mar_uint8_t value[SA_MAX_NAME_LENGTH] __attribute__((aligned(8)));
} mar_name_t;

typedef struct {
	mar_uint32_t node_id __attribute__((aligned(8)));
	mar_clm_node_address_t node_address __attribute__((aligned(8)));
	mar_name_t node_name __attribute__((aligned(8)));
	mar_uint32_t member __attribute__((aligned(8)));
	mar_uint64_t boot_timestamp __attribute__((aligned(8)));
	mar_uint64_t initial_view_number __attribute__((aligned(8)));
} mar_clm_cluster_node_t;

mar_clm_cluster_node_t my_cluster_node;

struct req_exec_clm_nodejoin {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_clm_cluster_node_t cluster_node __attribute__((aligned(8)));
};

static void my_cluster_node_load (void)
{
	struct totem_ip_address interfaces[INTERFACE_MAX];
	unsigned int iface_count;
	char **status;
	const char *iface_string;

	totempg_ifaces_get (
		totempg_my_nodeid_get (),
		interfaces,
		&status,
		&iface_count);

	iface_string = totemip_print (&interfaces[0]);

	sprintf ((char *)my_cluster_node.node_address.value, "%s",
		iface_string);
	my_cluster_node.node_address.length =
		strlen ((char *)my_cluster_node.node_address.value);
	if (totempg_my_family_get () == AF_INET) {
		my_cluster_node.node_address.family = MAR_CLM_AF_INET;
	} else
	if (totempg_my_family_get () == AF_INET6) {
		my_cluster_node.node_address.family = MAR_CLM_AF_INET6;
	} else {
		assert (0);
	}

	strcpy ((char *)my_cluster_node.node_name.value,
		(char *)my_cluster_node.node_address.value);
	my_cluster_node.node_name.length =
		my_cluster_node.node_address.length;
	my_cluster_node.node_id = totempg_my_nodeid_get ();
	my_cluster_node.member = 1;
}

static int clm_hack_init (void)
{

#if defined(COROSYNC_LINUX)
	struct sysinfo s_info;
	time_t current_time;
	sysinfo (&s_info);
	current_time = time (NULL);
	 /* (currenttime (s) - uptime (s)) * 1 billion (ns) / 1 (s) */
	my_cluster_node.boot_timestamp = ((uint64_t)(current_time - s_info.uptime)) * 1000000000ULL;
#elif defined(COROSYNC_BSD) || defined(COROSYNC_DARWIN)
	int mib[2] = { CTL_KERN, KERN_BOOTTIME };
	struct timeval boot_time;
	size_t size = sizeof(boot_time);

	if ( sysctl(mib, 2, &boot_time, &size, NULL, 0) == -1 )
		boot_time.tv_sec = time (NULL);
	 /* (currenttime (s) - uptime (s)) * 1 billion (ns) / 1 (s) */
	my_cluster_node.boot_timestamp = ((uint64_t)boot_time.tv_sec) * 1000000000ULL;
#else /* defined(CTL_KERN) && defined(KERN_BOOTTIME) */
	#warning "no bootime support"
#endif

	return (0);
}

static int clm_nodejoin_send (void)
{
	struct req_exec_clm_nodejoin req_exec_clm_nodejoin;
	struct iovec req_exec_clm_iovec;
	int result;

	my_cluster_node_load ();

	req_exec_clm_nodejoin.header.size = sizeof (struct req_exec_clm_nodejoin);
	req_exec_clm_nodejoin.header.id =
		SERVICE_ID_MAKE (CLM_SERVICE, MESSAGE_REQ_EXEC_CLM_NODEJOIN);

	my_cluster_node.initial_view_number = 0;

	memcpy (&req_exec_clm_nodejoin.cluster_node, &my_cluster_node,
		sizeof (mar_clm_cluster_node_t));

	req_exec_clm_iovec.iov_base = (char *)&req_exec_clm_nodejoin;
	req_exec_clm_iovec.iov_len = sizeof (req_exec_clm_nodejoin);

	result = api->totem_mcast (&req_exec_clm_iovec, 1, TOTEMPG_AGREED);

	return (result);
}

/*
 * This is a noop for this service
 */
static void clm_sync_init (
	const unsigned int *member_list,
	size_t member_list_entries,
	const struct memb_ring_id *ring_id)
{
	return;
}

static int clm_sync_process (void)
{
	/*
	 * Send node information to other nodes
	 */
	return (clm_nodejoin_send ());
}

/*
 * This is a noop for this service
 */
static void clm_sync_activate (void)
{
	return;
}

/*
 * This is a noop for this service
 */
static void clm_sync_abort (void)
{
	return;
}

enum evt_sync_states {
	EVT_SYNC_PART_ONE,
	EVT_SYNC_PART_TWO,
	EVT_SYNC_PART_THREE,
	EVT_SYNC_PART_FOUR,
	EVT_SYNC_DONE
};

static enum evt_sync_states evt_sync_state;

enum evt_chan_ops {
	EVT_OPEN_CHAN_OP,               /* chc_chan */
	EVT_CLOSE_CHAN_OP,              /* chc_close_unlink_chan */
	EVT_UNLINK_CHAN_OP,             /* chc_close_unlink_chan */
	EVT_CLEAR_RET_OP,               /* chc_event_id */
	EVT_SET_ID_OP,                  /* chc_set_id */
	EVT_CONF_DONE,                  /* no data used */
	EVT_OPEN_COUNT,                 /* chc_set_opens */
	EVT_OPEN_COUNT_DONE             /* no data used */
};

enum evt_message_req_types {
	MESSAGE_REQ_EXEC_EVT_EVENTDATA = 0,
	MESSAGE_REQ_EXEC_EVT_CHANCMD = 1,
	MESSAGE_REQ_EXEC_EVT_RECOVERY_EVENTDATA = 2
};

struct req_evt_chan_command {
	mar_req_header_t chc_head __attribute__((aligned(8)));
	mar_uint32_t chc_op __attribute__((aligned(8)));
};

static void evt_sync_init (
	const unsigned int *member_list,
	size_t member_list_entries,
	const struct memb_ring_id *ring_id)
{
	my_member_list_entries = member_list_entries;
	my_evt_checked_in = 0;

	evt_sync_state = EVT_SYNC_PART_ONE;
	return;
}

static int evt_sync_process (void)
{
	int res;
	struct req_evt_chan_command cpkt;
	struct iovec chn_iovec;

	memset(&cpkt, 0, sizeof(cpkt));
	cpkt.chc_head.id =
		SERVICE_ID_MAKE(EVT_SERVICE, MESSAGE_REQ_EXEC_EVT_CHANCMD);
	cpkt.chc_head.size = sizeof(cpkt);
	chn_iovec.iov_base = &cpkt;
	chn_iovec.iov_len = cpkt.chc_head.size;

	if (evt_sync_state == EVT_SYNC_PART_ONE) {
		cpkt.chc_op = EVT_OPEN_COUNT_DONE;

		res = api->totem_mcast(&chn_iovec, 1,TOTEMPG_AGREED);
		if (res == -1) {
			return (res);
		}
		evt_sync_state = EVT_SYNC_PART_TWO;
	}
	if (evt_sync_state == EVT_SYNC_PART_THREE) {
		cpkt.chc_op = EVT_CONF_DONE;
		res = api->totem_mcast(&chn_iovec, 1,TOTEMPG_AGREED);
		if (res == -1) {
			return (res);
		}
		evt_sync_state = EVT_SYNC_PART_FOUR;
	}
	if (evt_sync_state == EVT_SYNC_DONE) {
		return (0);
	}
	return (-1);
}

static void evt_sync_activate (void)
{
	return;
}

static void evt_sync_abort (void)
{
	return;
}

static void deliver_fn_evt_compat (
	unsigned int nodeid,
	unsigned int service,
	unsigned int fn_id,
	const void *msg,
	unsigned int endian_conversion_required)
{
	const struct req_evt_chan_command *cpkt = msg;
	unsigned int operation;

	if (fn_id != MESSAGE_REQ_EXEC_EVT_CHANCMD) {
		return;
	}
	operation = cpkt->chc_op;

	if (endian_conversion_required) {
		operation = swab32 (operation);
	}

	switch (operation) {
	case EVT_OPEN_COUNT_DONE:
		if (++my_evt_checked_in == my_member_list_entries) {
			evt_sync_state = EVT_SYNC_PART_THREE;
			my_evt_checked_in = 0;
		}
		break;
	case EVT_CONF_DONE:
		if (++my_evt_checked_in == my_member_list_entries) {
			evt_sync_state = EVT_SYNC_DONE;
			my_evt_checked_in = 0;
		}
		break;
	}
}
