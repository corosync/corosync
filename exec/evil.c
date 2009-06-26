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
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
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

static void clm_sync_init (void);

static int clm_sync_process (void);

static void clm_sync_activate (void);

static void clm_sync_abort (void);

static int clm_nodejoin_send (void);

static int clm_hack_init (void);

static struct sync_callbacks clm_sync_operations = {
	.name			= "dummy CLM service",
	.sync_init		= clm_sync_init,
	.sync_process		= clm_sync_process,
	.sync_activate		= clm_sync_activate,
	.sync_abort		= clm_sync_abort,
};

static struct corosync_api_v1 *api = NULL;

static void sync_dummy_init (void)
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

	memset (callbacks, 0, sizeof (struct sync_callbacks));
	switch (sync_id) {
		case EVS_SERVICE:
			callbacks_init = 0;
			break;
		case CLM_SERVICE:
			/*
			 * ugh
			 */
			memcpy (callbacks, &clm_sync_operations, sizeof (struct sync_callbacks));
			callbacks_init = 0;
			break;
		case AMF_SERVICE:
			callbacks->name = "dummy AMF service";
			break;
		case CKPT_SERVICE:
			callbacks->name = "dummy CKPT service";
			break;
		case EVT_SERVICE:
			callbacks->name = "dummy EVT service";
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
			callbacks->name = "dummy CPG service";
			break;
		case CONFDB_SERVICE:
			callbacks_init = 0;
			break;
		default:
			callbacks_init = 0;
			sync_dummy_found = 0;
			break;

	}
	if (callbacks_init) {
		callbacks->sync_init = sync_dummy_init;
		callbacks->sync_process = sync_dummy_process;
		callbacks->sync_activate = sync_dummy_activate;
		callbacks->sync_abort = sync_dummy_abort;
	}

	if (sync_dummy_found == 0) {
		return (-1);
	}
	return (0);
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
	my_cluster_node.boot_timestamp = ((SaTimeT)(current_time - s_info.uptime)) * 1000000000;
#elif defined(COROSYNC_BSD) || defined(COROSYNC_DARWIN)
	int mib[2] = { CTL_KERN, KERN_BOOTTIME };
	struct timeval boot_time;
	size_t size = sizeof(boot_time);
	
	if ( sysctl(mib, 2, &boot_time, &size, NULL, 0) == -1 )
		boot_time.tv_sec = time (NULL);
	 /* (currenttime (s) - uptime (s)) * 1 billion (ns) / 1 (s) */
	my_cluster_node.boot_timestamp = ((SaTimeT)boot_time.tv_sec) * 1000000000;
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
static void clm_sync_init (void)
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
