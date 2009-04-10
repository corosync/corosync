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

#include <corosync/swab.h>
#include <corosync/corotypes.h>
#include <corosync/list.h>
#include <corosync/queue.h>
#include <corosync/lcr/lcr_ifact.h>
#include <corosync/totem/coropoll.h>
#include <corosync/totem/totempg.h>
#include <corosync/engine/objdb.h>
#include <corosync/engine/config.h>
#include <corosync/engine/logsys.h>

#include "quorum.h"
#include "totemsrp.h"
#include "mempool.h"
#include "mainconfig.h"
#include "totemconfig.h"
#include "main.h"
#include "sync.h"
#include "tlist.h"
#include "coroipcs.h"
#include "timer.h"
#include "util.h"
#include "apidef.h"
#include "service.h"
#include "version.h"

LOGSYS_DECLARE_SYSTEM ("corosync",
	LOG_MODE_OUTPUT_STDERR | LOG_MODE_THREADED | LOG_MODE_FORK,
	NULL,
	LOG_DAEMON,
	NULL,
	1000000);

LOGSYS_DECLARE_SUBSYS ("MAIN", LOG_INFO);

#define SERVER_BACKLOG 5

static int sched_priority = 0;

static unsigned int service_count = 32;

#if defined(HAVE_PTHREAD_SPIN_LOCK)
static pthread_spinlock_t serialize_spin;
#else
static pthread_mutex_t serialize_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static struct totem_logging_configuration totem_logging_configuration;

static char delivery_data[MESSAGE_SIZE_MAX];

static int num_config_modules;

static struct config_iface_ver0 *config_modules[MAX_DYNAMIC_SERVICES];

static struct objdb_iface_ver0 *objdb = NULL;

static struct corosync_api_v1 *api = NULL;

static struct main_config main_config;

unsigned long long *(*main_clm_get_by_nodeid) (unsigned int node_id);

hdb_handle_t corosync_poll_handle;

static void sigusr2_handler (int num)
{
	int i;

	for (i = 0; ais_service[i]; i++) {
		if (ais_service[i]->exec_dump_fn) {
			ais_service[i]->exec_dump_fn ();
		}
	}
}

static void *corosync_exit (void *arg) __attribute__((__noreturn__));
static void *corosync_exit (void *arg)
{
	if (api) {
		corosync_service_unlink_all (api);
	}

#ifdef DEBUG_MEMPOOL
	int stats_inuse[MEMPOOL_GROUP_SIZE];
	int stats_avail[MEMPOOL_GROUP_SIZE];
	int stats_memoryused[MEMPOOL_GROUP_SIZE];
	int i;

	mempool_getstats (stats_inuse, stats_avail, stats_memoryused);
	log_printf (LOG_LEVEL_DEBUG, "Memory pools:\n");
	for (i = 0; i < MEMPOOL_GROUP_SIZE; i++) {
	log_printf (LOG_LEVEL_DEBUG, "order %d size %d inuse %d avail %d memory used %d\n",
		i, 1<<i, stats_inuse[i], stats_avail[i], stats_memoryused[i]);
	}
#endif

	poll_stop (0);
	totempg_finalize ();
	coroipcs_ipc_exit ();
	corosync_exit_error (AIS_DONE_EXIT);
}

pthread_t corosync_exit_thread;
static void init_shutdown(void *data) 
{
	pthread_create (&corosync_exit_thread, NULL, corosync_exit, NULL);
}


static poll_timer_handle shutdown_handle;
static void sigquit_handler (int num)
{
	/* avoid creating threads from within the interrupt context */
	poll_timer_add (corosync_poll_handle, 500, NULL, init_shutdown, &shutdown_handle);
}


static void sigsegv_handler (int num)
{
	(void)signal (SIGSEGV, SIG_DFL);
	logsys_atsegv();
	logsys_log_rec_store (LOCALSTATEDIR "/lib/corosync/fdata");
	raise (SIGSEGV);
}

static void sigabrt_handler (int num)
{
	(void)signal (SIGABRT, SIG_DFL);
	logsys_atsegv();
	logsys_log_rec_store (LOCALSTATEDIR "/lib/corosync/fdata");
	raise (SIGABRT);
}

#define LOCALHOST_IP inet_addr("127.0.0.1")

hdb_handle_t corosync_group_handle;

struct totempg_group corosync_group = {
	.group		= "a",
	.group_len	= 1
};

static void sigintr_handler (int signum)
{
	poll_timer_add (corosync_poll_handle, 500, NULL,
			init_shutdown, &shutdown_handle);
}


static int pool_sizes[] = { 0, 0, 0, 0, 0, 4096, 0, 1, 0, /* 256 */
					1024, 0, 1, 4096, 0, 0, 0, 0, /* 65536 */
					1, 1, 1, 1, 1, 1, 1, 1, 1 };

#if defined(HAVE_PTHREAD_SPIN_LOCK)
static void serialize_lock (void)
{
	pthread_spin_lock (&serialize_spin);
}

static void serialize_unlock (void)
{
	pthread_spin_unlock (&serialize_spin);
}
#else
static void serialize_lock (void)
{
	pthread_mutex_lock (&serialize_mutex);
}

static void serialize_unlock (void)
{
	pthread_mutex_unlock (&serialize_mutex);
}
#endif



static void corosync_sync_completed (void)
{
}

static int corosync_sync_callbacks_retrieve (int sync_id,
	struct sync_callbacks *callbacks)
{
	unsigned int ais_service_index;
	unsigned int ais_services_found = 0;
	
	for (ais_service_index = 0;
		ais_service_index < SERVICE_HANDLER_MAXIMUM_COUNT;
		ais_service_index++) {

		if (ais_service[ais_service_index] != NULL) {
			if (ais_services_found == sync_id) {
				break;
			}
			ais_services_found += 1;
		}
	}
	if (ais_service_index == SERVICE_HANDLER_MAXIMUM_COUNT) {
		memset (callbacks, 0, sizeof (struct sync_callbacks));
		return (-1);
	}
	callbacks->name = ais_service[ais_service_index]->name;
	callbacks->sync_init = ais_service[ais_service_index]->sync_init;
	callbacks->sync_process = ais_service[ais_service_index]->sync_process;
	callbacks->sync_activate = ais_service[ais_service_index]->sync_activate;
	callbacks->sync_abort = ais_service[ais_service_index]->sync_abort;
	return (0);
}

static struct memb_ring_id corosync_ring_id;

static void confchg_fn (
	enum totem_configuration_type configuration_type,
	const unsigned int *member_list, size_t member_list_entries,
	const unsigned int *left_list, size_t left_list_entries,
	const unsigned int *joined_list, size_t joined_list_entries,
	const struct memb_ring_id *ring_id)
{
	int i;

	serialize_lock ();
	memcpy (&corosync_ring_id, ring_id, sizeof (struct memb_ring_id));

	/*
	 * Call configuration change for all services
	 */
	for (i = 0; i < service_count; i++) {
		if (ais_service[i] && ais_service[i]->confchg_fn) {
			ais_service[i]->confchg_fn (configuration_type,
				member_list, member_list_entries,
				left_list, left_list_entries,
				joined_list, joined_list_entries, ring_id);
		}
	}
	serialize_unlock ();
}

static void priv_drop (void)
{
return; /* TODO: we are still not dropping privs */
	setuid (main_config.uid);
	setegid (main_config.gid);
}

static void corosync_mempool_init (void)
{
	int res;

	res = mempool_init (pool_sizes);
	if (res == ENOMEM) {
		log_printf (LOG_LEVEL_ERROR, "Couldn't allocate memory pools, not enough memory");
		corosync_exit_error (AIS_DONE_MEMPOOL_INIT);
	}
}

static void corosync_tty_detach (void)
{
	int fd;

	/*
	 * Disconnect from TTY if this is not a debug run
	 */

	switch (fork ()) {
		case -1:
			corosync_exit_error (AIS_DONE_FORK);
			break;
		case 0:
			/*
			 * child which is disconnected, run this process
			 */
/* 			setset();
			close (0);
			close (1);
			close (2);
*/
			break;
		default:
			exit (0);
			break;
	}

	/* Create new session */
	(void)setsid();

	/* 
	 * Map stdin/out/err to /dev/null.
	 */
	fd = open("/dev/null", O_RDWR);
	if (fd >= 0) {
		/* dup2 to 0 / 1 / 2 (stdin / stdout / stderr) */
		dup2(fd, STDIN_FILENO);  /* 0 */
		dup2(fd, STDOUT_FILENO); /* 1 */
		dup2(fd, STDERR_FILENO); /* 2 */

		/* Should be 0, but just in case it isn't... */
		if (fd > 2) 
			close(fd);
	}
}

static void corosync_setscheduler (void)
{
#if ! defined(TS_CLASS) && (defined(COROSYNC_BSD) || defined(COROSYNC_LINUX) || defined(COROSYNC_SOLARIS))
	struct sched_param sched_param;
	int res;

	sched_priority = sched_get_priority_max (SCHED_RR);
	if (sched_priority != -1) {
		sched_param.sched_priority = sched_priority;
		res = sched_setscheduler (0, SCHED_RR, &sched_param);
		if (res == -1) {
			log_printf (LOG_LEVEL_WARNING, "Could not set SCHED_RR at priority %d: %s\n",
				sched_param.sched_priority, strerror (errno));
		}
	} else {
		log_printf (LOG_LEVEL_WARNING, "Could not get maximum scheduler priority: %s\n", strerror (errno));
		sched_priority = 0;
	}
#else
	log_printf(LOG_LEVEL_WARNING, "Scheduler priority left to default value (no OS support)\n");
#endif
}

static void corosync_mlockall (void)
{
#if !defined(COROSYNC_BSD)
	int res;
#endif
	struct rlimit rlimit;

	rlimit.rlim_cur = RLIM_INFINITY;
	rlimit.rlim_max = RLIM_INFINITY;
#ifndef COROSYNC_SOLARIS
	setrlimit (RLIMIT_MEMLOCK, &rlimit);
#else
	setrlimit (RLIMIT_VMEM, &rlimit);
#endif

#if defined(COROSYNC_BSD)
	/* under FreeBSD a process with locked page cannot call dlopen
	 * code disabled until FreeBSD bug i386/93396 was solved
	 */
	log_printf (LOG_LEVEL_WARNING, "Could not lock memory of service to avoid page faults\n");
#else
	res = mlockall (MCL_CURRENT | MCL_FUTURE);
	if (res == -1) {
		log_printf (LOG_LEVEL_WARNING, "Could not lock memory of service to avoid page faults: %s\n", strerror (errno));
	};
#endif
}

static void deliver_fn (
	unsigned int nodeid,
	struct iovec *iovec,
	unsigned int iov_len,
	int endian_conversion_required)
{
	mar_req_header_t *header;
	int pos = 0;
	int i;
	int service;
	int fn_id;

	/*
	 * Build buffer without iovecs to make processing easier
	 * This is only used for messages which are multicast with iovecs
	 * and self-delivered.  All other mechanisms avoid the copy.
	 */
	if (iov_len > 1) {
		for (i = 0; i < iov_len; i++) {
			memcpy (&delivery_data[pos], iovec[i].iov_base, iovec[i].iov_len);
			pos += iovec[i].iov_len;
			assert (pos < MESSAGE_SIZE_MAX);
		}
		header = (mar_req_header_t *)delivery_data;
	} else {
		header = (mar_req_header_t *)iovec[0].iov_base;
	}
	if (endian_conversion_required) {
		header->id = swab32 (header->id);
		header->size = swab32 (header->size);
	}

//	assert(iovec->iov_len == header->size);

	/*
	 * Call the proper executive handler
	 */
	service = header->id >> 16;
	fn_id = header->id & 0xffff;
	if (!ais_service[service])
		return;

	serialize_lock();

	if (endian_conversion_required) {
		assert(ais_service[service]->exec_engine[fn_id].exec_endian_convert_fn != NULL);
		ais_service[service]->exec_engine[fn_id].exec_endian_convert_fn
			(header);
	}

	ais_service[service]->exec_engine[fn_id].exec_handler_fn
		(header, nodeid);

	serialize_unlock();
}

void main_get_config_modules(struct config_iface_ver0 ***modules, int *num)
{
	*modules = config_modules;
	*num = num_config_modules;
}

int main_mcast (
        const struct iovec *iovec,
        unsigned int iov_len,
        unsigned int guarantee)
{
	return (totempg_groups_mcast_joined (corosync_group_handle, iovec, iov_len, guarantee));
}

int message_source_is_local (const mar_message_source_t *source)
{
	int ret = 0;

	assert (source != NULL);
	if (source->nodeid == totempg_my_nodeid_get ()) {
		ret = 1;
	}
	return ret;
}

void message_source_set (
	mar_message_source_t *source,
	void *conn)
{
	assert ((source != NULL) && (conn != NULL));
	memset (source, 0, sizeof (mar_message_source_t));
	source->nodeid = totempg_my_nodeid_get ();
	source->conn = conn;
}

/*
 * Provides the glue from corosync to the IPC Service
 */
static int corosync_private_data_size_get (unsigned int service)
{
	return (ais_service[service]->private_data_size);
}

static coroipcs_init_fn_lvalue corosync_init_fn_get (unsigned int service)
{
	return (ais_service[service]->lib_init_fn);
}

static coroipcs_exit_fn_lvalue corosync_exit_fn_get (unsigned int service)
{
	return (ais_service[service]->lib_exit_fn);
}

static coroipcs_handler_fn_lvalue corosync_handler_fn_get (unsigned int service, unsigned int id)
{
	return (ais_service[service]->lib_engine[id].lib_handler_fn);
}


static int corosync_security_valid (int euid, int egid)
{
	if (euid == 0 || egid == 0) {
		return (1);
	}
	if (euid == main_config.uid || egid == main_config.gid) {
		return (1);
	}
	return (0);
}

static int corosync_service_available (unsigned int service)
{
	return (ais_service[service] != NULL);
}

static int corosync_response_size_get (unsigned int service, unsigned int id)
{
	return (ais_service[service]->lib_engine[id].response_size);

}

static int corosync_response_id_get (unsigned int service, unsigned int id)
{
	return (ais_service[service]->lib_engine[id].response_id);
}


struct sending_allowed_private_data_struct {
	int reserved_msgs;
};

static int corosync_sending_allowed (
	unsigned int service,
	unsigned int id, 
	void *msg,
	void *sending_allowed_private_data)
{
	struct sending_allowed_private_data_struct *pd =
		(struct sending_allowed_private_data_struct *)sending_allowed_private_data;
	struct iovec reserve_iovec;
	mar_req_header_t *header = (mar_req_header_t *)msg;
	int sending_allowed;

	reserve_iovec.iov_base = (char *)header;
	reserve_iovec.iov_len = header->size;

	pd->reserved_msgs = totempg_groups_joined_reserve (
		corosync_group_handle,
		&reserve_iovec, 1);

	sending_allowed =
		(corosync_quorum_is_quorate() == 1 ||
		ais_service[service]->allow_inquorate == CS_LIB_ALLOW_INQUORATE) &&
		((ais_service[service]->lib_engine[id].flow_control == CS_LIB_FLOW_CONTROL_NOT_REQUIRED) ||
		((ais_service[service]->lib_engine[id].flow_control == CS_LIB_FLOW_CONTROL_REQUIRED) &&
		(pd->reserved_msgs) &&
		(sync_in_process() == 0)));

	return (sending_allowed);
}

static void corosync_sending_allowed_release (void *sending_allowed_private_data)
{
	struct sending_allowed_private_data_struct *pd =
		(struct sending_allowed_private_data_struct *)sending_allowed_private_data;

	totempg_groups_joined_release (pd->reserved_msgs);
}

static int ipc_subsys_id = -1;

static void ipc_log_printf (const char *format, ...) {
        va_list ap;

        va_start (ap, format);
	
       _logsys_log_printf (ipc_subsys_id, __FUNCTION__,	
                __FILE__, __LINE__, LOG_LEVEL_ERROR, format, ap);

        va_end (ap);
}

static int corosync_poll_handler_accept (
	hdb_handle_t handle,	
	int fd,
	int revent,
	void *context)
{
	return (coroipcs_handler_accept (fd, revent, context));
}

static int corosync_poll_handler_dispatch (
	hdb_handle_t handle,	
	int fd,
	int revent,
	void *context)
{
	return (coroipcs_handler_dispatch (fd, revent, context));
}


static void corosync_poll_accept_add (
	int fd)
{
	poll_dispatch_add (corosync_poll_handle, fd, POLLIN|POLLNVAL, 0,
		corosync_poll_handler_accept);
}

static void corosync_poll_dispatch_add (
	int fd,
	void *context)
{
	poll_dispatch_add (corosync_poll_handle, fd, POLLIN|POLLNVAL, context,
		corosync_poll_handler_dispatch);
}

static void corosync_poll_dispatch_modify (
	int fd,
	int events)
{
	poll_dispatch_modify (corosync_poll_handle, fd, events,
		corosync_poll_handler_dispatch);
}

struct coroipcs_init_state ipc_init_state = {
	.socket_name			= IPC_SOCKET_NAME,
	.malloc				= malloc,
	.free				= free,
	.log_printf			= ipc_log_printf,
	.security_valid			= corosync_security_valid,
	.service_available		= corosync_service_available,
	.private_data_size_get		= corosync_private_data_size_get,
	.serialize_lock			= serialize_lock,
	.serialize_unlock		= serialize_unlock,
	.sending_allowed		= corosync_sending_allowed,
	.sending_allowed_release	= corosync_sending_allowed_release,
	.response_size_get		= corosync_response_size_get,
	.response_id_get		= corosync_response_id_get,
	.poll_accept_add		= corosync_poll_accept_add,
	.poll_dispatch_add		= corosync_poll_dispatch_add,
	.poll_dispatch_modify		= corosync_poll_dispatch_modify,
	.init_fn_get			= corosync_init_fn_get,
	.exit_fn_get			= corosync_exit_fn_get,
	.handler_fn_get			= corosync_handler_fn_get
};

int main (int argc, char **argv)
{
	const char *error_string;
	struct totem_config totem_config;
	hdb_handle_t objdb_handle;
	hdb_handle_t config_handle;
	unsigned int config_version = 0;
	void *objdb_p;
	struct config_iface_ver0 *config;
	void *config_p;
	const char *config_iface_init;
	char *config_iface;
	char *iface;
	int res, ch;
	int background, setprio;

#if defined(HAVE_PTHREAD_SPIN_LOCK)
	pthread_spin_init (&serialize_spin, 0);
#endif

 	/* default configuration
	 */
	background = 1;
	setprio = 1;
 	
 	while ((ch = getopt (argc, argv, "fp")) != EOF) {
 	
		switch (ch) {
			case 'f':
				background = 0;
				logsys_config_mode_set (LOG_MODE_OUTPUT_STDERR|LOG_MODE_THREADED|LOG_MODE_FORK);
				break;
			case 'p':
				setprio = 0;
				break;
			default:
				fprintf(stderr, \
					"usage:\n"\
					"        -f     : Start application in foreground.\n"\
					"        -p     : Do not set process priority.    \n");
				return EXIT_FAILURE;
		}
	}	

	if (background)
		corosync_tty_detach ();

	log_printf (LOG_LEVEL_NOTICE, "Corosync Executive Service RELEASE '%s'\n", RELEASE_VERSION);
	log_printf (LOG_LEVEL_NOTICE, "Copyright (C) 2002-2006 MontaVista Software, Inc and contributors.\n");
	log_printf (LOG_LEVEL_NOTICE, "Copyright (C) 2006-2008 Red Hat, Inc.\n");

	(void)signal (SIGINT, sigintr_handler);
	(void)signal (SIGUSR2, sigusr2_handler);
	(void)signal (SIGSEGV, sigsegv_handler);
	(void)signal (SIGABRT, sigabrt_handler);
	(void)signal (SIGQUIT, sigquit_handler);
#if MSG_NOSIGNAL == 0
	(void)signal (SIGPIPE, SIG_IGN);
#endif
	
	corosync_timer_init (
		serialize_lock,
		serialize_unlock,
		sched_priority);

	log_printf (LOG_LEVEL_NOTICE, "Corosync Executive Service: started and ready to provide service.\n");

	corosync_poll_handle = poll_create ();

	/*
	 * Load the object database interface
	 */
	res = lcr_ifact_reference (
		&objdb_handle,
		"objdb",
		0,
		&objdb_p,
		0);
	if (res == -1) {
		log_printf (LOG_LEVEL_ERROR, "Corosync Executive couldn't open configuration object database component.\n");
		corosync_exit_error (AIS_DONE_OBJDB);
	}

	objdb = (struct objdb_iface_ver0 *)objdb_p;

	objdb->objdb_init ();

	/*
	 * Initialize the corosync_api_v1 definition
	 */
	apidef_init (objdb);
	api = apidef_get ();

	num_config_modules = 0;

	/*
	 * Bootstrap in the default configuration parser or use
	 * the corosync default built in parser if the configuration parser
	 * isn't overridden
	 */
	config_iface_init = getenv("COROSYNC_DEFAULT_CONFIG_IFACE");
	if (!config_iface_init) {
		config_iface_init = "corosync_parser";
	}

	/* Make a copy so we can deface it with strtok */
	config_iface = strdup(config_iface_init);

	iface = strtok(config_iface, ":");
	while (iface)
	{
		res = lcr_ifact_reference (
			&config_handle,
			iface,
			config_version,
			&config_p,
			0);

		config = (struct config_iface_ver0 *)config_p;
		if (res == -1) {
			log_printf (LOG_LEVEL_ERROR, "Corosync Executive couldn't open configuration component '%s'\n", iface);
			corosync_exit_error (AIS_DONE_MAINCONFIGREAD);
		}

		res = config->config_readconfig(objdb, &error_string);
		if (res == -1) {
			log_printf (LOG_LEVEL_ERROR, "%s", error_string);
			corosync_exit_error (AIS_DONE_MAINCONFIGREAD);
		}
		log_printf (LOG_LEVEL_NOTICE, "%s", error_string);
		config_modules[num_config_modules++] = config;

		iface = strtok(NULL, ":");
	}
	if (config_iface)
		free(config_iface);

	res = corosync_main_config_read (objdb, &error_string, &main_config);
	if (res == -1) {
		log_printf (LOG_LEVEL_ERROR, "%s", error_string);
		corosync_exit_error (AIS_DONE_MAINCONFIGREAD);
	}

	res = totem_config_read (objdb, &totem_config, &error_string);
	if (res == -1) {
		log_printf (LOG_LEVEL_ERROR, "%s", error_string);
		corosync_exit_error (AIS_DONE_MAINCONFIGREAD);
	}

	res = totem_config_keyread (objdb, &totem_config, &error_string);
	if (res == -1) {
		log_printf (LOG_LEVEL_ERROR, "%s", error_string);
		corosync_exit_error (AIS_DONE_MAINCONFIGREAD);
	}

	res = totem_config_validate (&totem_config, &error_string);
	if (res == -1) {
		log_printf (LOG_LEVEL_ERROR, "%s", error_string);
		corosync_exit_error (AIS_DONE_MAINCONFIGREAD);
	}

	/*
	 * Set round robin realtime scheduling with priority 99
	 * Lock all memory to avoid page faults which may interrupt
	 * application healthchecking
	 */
	if (setprio)
		corosync_setscheduler ();

	corosync_mlockall ();

	totem_config.totem_logging_configuration = totem_logging_configuration;
	totem_config.totem_logging_configuration.log_subsys_id =
		_logsys_subsys_create ("TOTEM", LOG_INFO);
  	totem_config.totem_logging_configuration.log_level_security = LOG_LEVEL_SECURITY;
	totem_config.totem_logging_configuration.log_level_error = LOG_LEVEL_ERROR;
	totem_config.totem_logging_configuration.log_level_warning = LOG_LEVEL_WARNING;
	totem_config.totem_logging_configuration.log_level_notice = LOG_LEVEL_NOTICE;
	totem_config.totem_logging_configuration.log_level_debug = LOG_LEVEL_DEBUG;
	totem_config.totem_logging_configuration.log_printf = _logsys_log_printf;

	/*
	 * Sleep for a while to let other nodes in the cluster
	 * understand that this node has been away (if it was
	 * an corosync restart).
	 */

// TODO what is this hack for?	usleep(totem_config.token_timeout * 2000);

	/*
	 * if totempg_initialize doesn't have root priveleges, it cannot
	 * bind to a specific interface.  This only matters if
	 * there is more then one interface in a system, so
	 * in this case, only a warning is printed
	 */
	/*
	 * Join multicast group and setup delivery
	 *  and configuration change functions
	 */
	totempg_initialize (
		corosync_poll_handle,
		&totem_config);

	totempg_groups_initialize (
		&corosync_group_handle,
		deliver_fn,
		confchg_fn);

	totempg_groups_join (
		corosync_group_handle,
		&corosync_group,
		1);

	/*
	 * This must occur after totempg is initialized because "this_ip" must be set
	 */
	res = corosync_service_defaults_link_and_init (api);
	if (res == -1) {
		log_printf (LOG_LEVEL_ERROR, "Could not initialize default services\n");
		corosync_exit_error (AIS_DONE_INIT_SERVICES);
	}


	sync_register (corosync_sync_callbacks_retrieve, corosync_sync_completed);

	/*
	 * Drop root privleges to user 'ais'
	 * TODO: Don't really need full root capabilities;
	 *       needed capabilities are:
	 * CAP_NET_RAW (bindtodevice)
	 * CAP_SYS_NICE (setscheduler)
	 * CAP_IPC_LOCK (mlockall)
	 */
	priv_drop ();

	corosync_mempool_init ();

	ipc_subsys_id = _logsys_subsys_create ("IPC", LOG_INFO);

	ipc_init_state.sched_priority = sched_priority;

	coroipcs_ipc_init (&ipc_init_state);

	/*
	 * Start main processing loop
	 */
	poll_run (corosync_poll_handle);

	return EXIT_SUCCESS;
}
