/*
 * Copyright (c) 2002-2006 MontaVista Software, Inc.
 * Copyright (c) 2006-2021 Red Hat, Inc.
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

/**
 * \mainpage Corosync
 *
 * This is the doxygen generated developer documentation for the Corosync
 * project.  For more information about Corosync, please see the project
 * web site, <a href="http://www.corosync.org">corosync.org</a>.
 *
 * \section license License
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
#include <sys/file.h>
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
#include <semaphore.h>
#include <string.h>

#ifdef HAVE_LIBSYSTEMD
#include <systemd/sd-daemon.h>
#endif

#include <qb/qbdefs.h>
#include <qb/qblog.h>
#include <qb/qbloop.h>
#include <qb/qbutil.h>
#include <qb/qbipcs.h>

#include <corosync/swab.h>
#include <corosync/corotypes.h>
#include <corosync/corodefs.h>
#include <corosync/totem/totempg.h>
#include <corosync/logsys.h>
#include <corosync/icmap.h>

#include "quorum.h"
#include "totemsrp.h"
#include "logconfig.h"
#include "totemconfig.h"
#include "main.h"
#include "sync.h"
#include "timer.h"
#include "util.h"
#include "apidef.h"
#include "service.h"
#include "schedwrk.h"
#include "ipcs_stats.h"
#include "stats.h"

#ifdef HAVE_SMALL_MEMORY_FOOTPRINT
#define IPC_LOGSYS_SIZE			1024*64
#else
#define IPC_LOGSYS_SIZE			8192*128
#endif

/*
 * LibQB adds default "*" syslog filter so we have to set syslog_priority as low
 * as possible so filters applied later in _logsys_config_apply_per_file takes
 * effect.
 */
LOGSYS_DECLARE_SYSTEM ("corosync",
	LOGSYS_MODE_OUTPUT_STDERR | LOGSYS_MODE_OUTPUT_SYSLOG,
	LOG_DAEMON,
	LOG_EMERG);

LOGSYS_DECLARE_SUBSYS ("MAIN");

#define SERVER_BACKLOG 5

static int sched_priority = 0;

static unsigned int service_count = 32;

static struct totem_logging_configuration totem_logging_configuration;

static struct corosync_api_v1 *api = NULL;

static int sync_in_process = 1;

static qb_loop_t *corosync_poll_handle;

struct sched_param global_sched_param;

static corosync_timer_handle_t corosync_stats_timer_handle;

static char corosync_lock_file[PATH_MAX + 1] = LOCALSTATEDIR"/run/corosync.pid";

static char corosync_config_file[PATH_MAX + 1] = COROSYSCONFDIR "/corosync.conf";

static int lockfile_fd = -1;

enum move_to_root_cgroup_mode {
	MOVE_TO_ROOT_CGROUP_MODE_OFF = 0,
	MOVE_TO_ROOT_CGROUP_MODE_ON = 1,
	MOVE_TO_ROOT_CGROUP_MODE_AUTO = 2,
};

qb_loop_t *cs_poll_handle_get (void)
{
	return (corosync_poll_handle);
}

int cs_poll_dispatch_add (qb_loop_t * handle,
		int fd,
		int events,
		void *data,

		int (*dispatch_fn) (int fd,
			int revents,
			void *data))
{
	return qb_loop_poll_add(handle, QB_LOOP_MED, fd, events, data,
				dispatch_fn);
}

int cs_poll_dispatch_delete(qb_loop_t * handle, int fd)
{
	return qb_loop_poll_del(handle, fd);
}

void corosync_state_dump (void)
{
	int i;

	for (i = 0; i < SERVICES_COUNT_MAX; i++) {
		if (corosync_service[i] && corosync_service[i]->exec_dump_fn) {
			corosync_service[i]->exec_dump_fn ();
		}
	}
}

const char *corosync_get_config_file(void)
{

	return (corosync_config_file);
}

static void corosync_blackbox_write_to_file (void)
{
	char fname[PATH_MAX];
	char fdata_fname[PATH_MAX];
	char time_str[PATH_MAX];
	struct tm cur_time_tm;
	time_t cur_time_t;
	ssize_t res;

	cur_time_t = time(NULL);
	localtime_r(&cur_time_t, &cur_time_tm);

	strftime(time_str, PATH_MAX, "%Y-%m-%dT%H:%M:%S", &cur_time_tm);
	if (snprintf(fname, PATH_MAX, "%s/fdata-%s-%lld",
	    get_state_dir(),
	    time_str,
	    (long long int)getpid()) >= PATH_MAX) {
		log_printf(LOGSYS_LEVEL_ERROR, "Can't snprintf blackbox file name");
		return ;
	}

	if ((res = qb_log_blackbox_write_to_file(fname)) < 0) {
		LOGSYS_PERROR(-res, LOGSYS_LEVEL_ERROR, "Can't store blackbox file");
		return ;
	}
	snprintf(fdata_fname, sizeof(fdata_fname), "%s/fdata", get_state_dir());
	unlink(fdata_fname);
	if (symlink(fname, fdata_fname) == -1) {
		log_printf(LOGSYS_LEVEL_ERROR, "Can't create symlink to '%s' for corosync blackbox file '%s'",
		    fname, fdata_fname);
	}
}

static void unlink_all_completed (void)
{
	api->timer_delete (corosync_stats_timer_handle);
	qb_loop_stop (corosync_poll_handle);
	icmap_fini();
}

void corosync_shutdown_request (void)
{
	corosync_service_unlink_all (api, unlink_all_completed);
}

static int32_t sig_diag_handler (int num, void *data)
{
	corosync_state_dump ();
	return 0;
}

static int32_t sig_exit_handler (int num, void *data)
{
	log_printf(LOGSYS_LEVEL_NOTICE, "Node was shut down by a signal");
	corosync_service_unlink_all (api, unlink_all_completed);
	return 0;
}

static void sigsegv_handler (int num)
{
	(void)signal (num, SIG_DFL);
	corosync_blackbox_write_to_file ();
	qb_log_fini();
	raise (num);
}

#define LOCALHOST_IP inet_addr("127.0.0.1")

static void *corosync_group_handle;

static struct totempg_group corosync_group = {
	.group		= "a",
	.group_len	= 1
};

static void serialize_lock (void)
{
}

static void serialize_unlock (void)
{
}

static void corosync_sync_completed (void)
{
	log_printf (LOGSYS_LEVEL_NOTICE,
		"Completed service synchronization, ready to provide service.");
	sync_in_process = 0;

	cs_ipcs_sync_state_changed(sync_in_process);
	cs_ipc_allow_connections(1);
	/*
	 * Inform totem to start using new message queue again
	 */
	totempg_trans_ack();

#ifdef HAVE_LIBSYSTEMD
	sd_notify (0, "READY=1");
#endif
}

static int corosync_sync_callbacks_retrieve (
	int service_id,
	struct sync_callbacks *callbacks)
{
	if (corosync_service[service_id] == NULL) {
		return (-1);
	}

	if (callbacks == NULL) {
		return (0);
	}

	callbacks->name = corosync_service[service_id]->name;

	callbacks->sync_init = corosync_service[service_id]->sync_init;
	callbacks->sync_process = corosync_service[service_id]->sync_process;
	callbacks->sync_activate = corosync_service[service_id]->sync_activate;
	callbacks->sync_abort = corosync_service[service_id]->sync_abort;
	return (0);
}

static struct memb_ring_id corosync_ring_id;

static void member_object_joined (unsigned int nodeid)
{
	char member_ip[ICMAP_KEYNAME_MAXLEN];
	char member_join_count[ICMAP_KEYNAME_MAXLEN];
	char member_status[ICMAP_KEYNAME_MAXLEN];

	snprintf(member_ip, ICMAP_KEYNAME_MAXLEN,
		"runtime.members.%u.ip", nodeid);
	snprintf(member_join_count, ICMAP_KEYNAME_MAXLEN,
		"runtime.members.%u.join_count", nodeid);
	snprintf(member_status, ICMAP_KEYNAME_MAXLEN,
		"runtime.members.%u.status", nodeid);

	if (icmap_get(member_ip, NULL, NULL, NULL) == CS_OK) {
		icmap_inc(member_join_count);
		icmap_set_string(member_status, "joined");
	} else {
		icmap_set_string(member_ip, (char*)api->totem_ifaces_print (nodeid));
		icmap_set_uint32(member_join_count, 1);
		icmap_set_string(member_status, "joined");
	}

	log_printf (LOGSYS_LEVEL_DEBUG,
		"Member joined: %s", api->totem_ifaces_print (nodeid));
}

static void member_object_left (unsigned int nodeid)
{
	char member_status[ICMAP_KEYNAME_MAXLEN];

	snprintf(member_status, ICMAP_KEYNAME_MAXLEN,
		"runtime.members.%u.status", nodeid);
	icmap_set_string(member_status, "left");

	log_printf (LOGSYS_LEVEL_DEBUG,
		"Member left: %s", api->totem_ifaces_print (nodeid));
}

static void confchg_fn (
	enum totem_configuration_type configuration_type,
	const unsigned int *member_list, size_t member_list_entries,
	const unsigned int *left_list, size_t left_list_entries,
	const unsigned int *joined_list, size_t joined_list_entries,
	const struct memb_ring_id *ring_id)
{
	int i;
	int abort_activate = 0;

	if (sync_in_process == 1) {
		abort_activate = 1;
	}
	sync_in_process = 1;
	cs_ipcs_sync_state_changed(sync_in_process);
	memcpy (&corosync_ring_id, ring_id, sizeof (struct memb_ring_id));

	for (i = 0; i < left_list_entries; i++) {
		member_object_left (left_list[i]);
	}
	for (i = 0; i < joined_list_entries; i++) {
		member_object_joined (joined_list[i]);
	}
	/*
	 * Call configuration change for all services
	 */
	for (i = 0; i < service_count; i++) {
		if (corosync_service[i] && corosync_service[i]->confchg_fn) {
			corosync_service[i]->confchg_fn (configuration_type,
				member_list, member_list_entries,
				left_list, left_list_entries,
				joined_list, joined_list_entries, ring_id);
		}
	}

	if (abort_activate) {
		sync_abort ();
	}
	if (configuration_type == TOTEM_CONFIGURATION_TRANSITIONAL) {
		sync_save_transitional (member_list, member_list_entries, ring_id);
	}
	if (configuration_type == TOTEM_CONFIGURATION_REGULAR) {
		sync_start (member_list, member_list_entries, ring_id);
	}
}

static void priv_drop (void)
{
	return; /* TODO: we are still not dropping privs */
}

static void corosync_tty_detach (void)
{
	int devnull;

	/*
	 * Disconnect from TTY if this is not a debug run
	 */

	switch (fork ()) {
		case -1:
			corosync_exit_error (COROSYNC_DONE_FORK);
			break;
		case 0:
			/*
			 * child which is disconnected, run this process
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
	devnull = open("/dev/null", O_RDWR);
	if (devnull == -1) {
		corosync_exit_error (COROSYNC_DONE_STD_TO_NULL_REDIR);
	}

	if (dup2(devnull, 0) < 0 || dup2(devnull, 1) < 0
	    || dup2(devnull, 2) < 0) {
		close(devnull);
		corosync_exit_error (COROSYNC_DONE_STD_TO_NULL_REDIR);
	}
	close(devnull);
}

static void corosync_mlockall (void)
{
	int res;
	struct rlimit rlimit;

	rlimit.rlim_cur = RLIM_INFINITY;
	rlimit.rlim_max = RLIM_INFINITY;

#ifndef RLIMIT_MEMLOCK
#define RLIMIT_MEMLOCK RLIMIT_VMEM
#endif

	res = setrlimit (RLIMIT_MEMLOCK, &rlimit);
	if (res == -1) {
		LOGSYS_PERROR (errno, LOGSYS_LEVEL_WARNING,
			"Could not increase RLIMIT_MEMLOCK, not locking memory");
		return;
	}

	res = mlockall (MCL_CURRENT | MCL_FUTURE);
	if (res == -1) {
		LOGSYS_PERROR (errno, LOGSYS_LEVEL_WARNING,
			"Could not lock memory of service to avoid page faults");
	};
}


static void corosync_totem_stats_updater (void *data)
{
	totempg_stats_t * stats;
	uint32_t total_mtt_rx_token;
	uint32_t total_backlog_calc;
	uint32_t total_token_holdtime;
	int t, prev;
	int32_t token_count;
	const char *cstr;

	stats = api->totem_get_stats();


	stats->srp->firewall_enabled_or_nic_failure = stats->srp->continuous_gather > MAX_NO_CONT_GATHER ? 1 : 0;

	if (stats->srp->continuous_gather > MAX_NO_CONT_GATHER ||
	    stats->srp->continuous_sendmsg_failures > MAX_NO_CONT_SENDMSG_FAILURES) {
		cstr = "";

		if (stats->srp->continuous_sendmsg_failures > MAX_NO_CONT_SENDMSG_FAILURES) {
			cstr = "number of multicast sendmsg failures is above threshold";
		}

		if (stats->srp->continuous_gather > MAX_NO_CONT_GATHER) {
			cstr = "totem is continuously in gather state";
		}

		log_printf (LOGSYS_LEVEL_WARNING,
			"Totem is unable to form a cluster because of an "
			"operating system or network fault (reason: %s). The most common "
			"cause of this message is that the local firewall is "
			"configured improperly.", cstr);
		stats->srp->firewall_enabled_or_nic_failure = 1;
	} else {
		stats->srp->firewall_enabled_or_nic_failure = 0;
	}

	total_mtt_rx_token = 0;
	total_token_holdtime = 0;
	total_backlog_calc = 0;
	token_count = 0;
	t = stats->srp->latest_token;
	while (1) {
		if (t == 0)
			prev = TOTEM_TOKEN_STATS_MAX - 1;
		else
			prev = t - 1;
		if (prev == stats->srp->earliest_token)
			break;
		/* if tx == 0, then dropped token (not ours) */
		if (stats->srp->token[t].tx != 0 ||
			(stats->srp->token[t].rx - stats->srp->token[prev].rx) > 0 ) {
			total_mtt_rx_token += (stats->srp->token[t].rx - stats->srp->token[prev].rx);
			total_token_holdtime += (stats->srp->token[t].tx - stats->srp->token[t].rx);
			total_backlog_calc += stats->srp->token[t].backlog_calc;
			token_count++;
		}
		t = prev;
	}
	if (token_count) {
		stats->srp->mtt_rx_token = (total_mtt_rx_token / token_count);
		stats->srp->avg_token_workload = (total_token_holdtime / token_count);
		stats->srp->avg_backlog_calc = (total_backlog_calc / token_count);
	}

	stats->srp->time_since_token_last_received = qb_util_nano_current_get () / QB_TIME_NS_IN_MSEC -
		stats->srp->token[stats->srp->latest_token].rx;

	stats_trigger_trackers();

	api->timer_add_duration (1500 * MILLI_2_NANO_SECONDS, NULL,
		corosync_totem_stats_updater,
		&corosync_stats_timer_handle);
}

static void corosync_totem_stats_init (void)
{
	/* start stats timer */
	api->timer_add_duration (1500 * MILLI_2_NANO_SECONDS, NULL,
		corosync_totem_stats_updater,
		&corosync_stats_timer_handle);
}

static void deliver_fn (
	unsigned int nodeid,
	const void *msg,
	unsigned int msg_len,
	int endian_conversion_required)
{
	const struct qb_ipc_request_header *header;
	int32_t service;
	int32_t fn_id;
	uint32_t id;

	header = msg;
	if (endian_conversion_required) {
		id = swab32 (header->id);
	} else {
		id = header->id;
	}

	/*
	 * Call the proper executive handler
	 */
	service = id >> 16;
	fn_id = id & 0xffff;

	if (!corosync_service[service]) {
		return;
	}
	if (fn_id >= corosync_service[service]->exec_engine_count) {
		log_printf(LOGSYS_LEVEL_WARNING, "discarded unknown message %d for service %d (max id %d)",
			fn_id, service, corosync_service[service]->exec_engine_count);
		return;
	}

	icmap_fast_inc(service_stats_rx[service][fn_id]);

	if (endian_conversion_required) {
		assert(corosync_service[service]->exec_engine[fn_id].exec_endian_convert_fn != NULL);
		corosync_service[service]->exec_engine[fn_id].exec_endian_convert_fn
			((void *)msg);
	}

	corosync_service[service]->exec_engine[fn_id].exec_handler_fn
		(msg, nodeid);
}

int main_mcast (
        const struct iovec *iovec,
        unsigned int iov_len,
        unsigned int guarantee)
{
	const struct qb_ipc_request_header *req = iovec->iov_base;
	int32_t service;
	int32_t fn_id;

	service = req->id >> 16;
	fn_id = req->id & 0xffff;

	if (corosync_service[service]) {
		icmap_fast_inc(service_stats_tx[service][fn_id]);
	}

	return (totempg_groups_mcast_joined (corosync_group_handle, iovec, iov_len, guarantee));
}

static void corosync_ring_id_create_or_load (
	struct memb_ring_id *memb_ring_id,
	unsigned int nodeid)
{
	int fd;
	int res = 0;
	char filename[PATH_MAX];

	snprintf (filename, sizeof(filename), "%s/ringid_%u",
		get_state_dir(), nodeid);
	fd = open (filename, O_RDONLY);
	/*
	 * If file can be opened and read, read the ring id
	 */
	if (fd != -1) {
		res = read (fd, &memb_ring_id->seq, sizeof (uint64_t));
		close (fd);
	}
	/*
	 * If file could not be opened or read, create a new ring id
	 */
	if ((fd == -1) || (res != sizeof (uint64_t))) {
		memb_ring_id->seq = 0;
		fd = creat (filename, 0600);
		if (fd != -1) {
			res = write (fd, &memb_ring_id->seq, sizeof (uint64_t));
			close (fd);
			if (res == -1) {
				LOGSYS_PERROR (errno, LOGSYS_LEVEL_ERROR,
					"Couldn't write ringid file '%s'", filename);

				corosync_exit_error (COROSYNC_DONE_STORE_RINGID);
			}
		} else {
			LOGSYS_PERROR (errno, LOGSYS_LEVEL_ERROR,
				"Couldn't create ringid file '%s'", filename);

			corosync_exit_error (COROSYNC_DONE_STORE_RINGID);
		}
	}

	memb_ring_id->rep = nodeid;
}

static void corosync_ring_id_store (
	const struct memb_ring_id *memb_ring_id,
	unsigned int nodeid)
{
	char filename[PATH_MAX];
	int fd;
	int res;

	snprintf (filename, sizeof(filename), "%s/ringid_%u",
		get_state_dir(), nodeid);

	fd = creat (filename, 0600);
	if (fd == -1) {
		LOGSYS_PERROR(errno, LOGSYS_LEVEL_ERROR,
			"Couldn't store new ring id " CS_PRI_RING_ID_SEQ " to stable storage",
			    memb_ring_id->seq);

		corosync_exit_error (COROSYNC_DONE_STORE_RINGID);
	}
	log_printf (LOGSYS_LEVEL_DEBUG,
		"Storing new sequence id for ring " CS_PRI_RING_ID_SEQ, memb_ring_id->seq);
	res = write (fd, &memb_ring_id->seq, sizeof(memb_ring_id->seq));
	close (fd);
	if (res != sizeof(memb_ring_id->seq)) {
		LOGSYS_PERROR(errno, LOGSYS_LEVEL_ERROR,
			"Couldn't store new ring id " CS_PRI_RING_ID_SEQ " to stable storage",
			memb_ring_id->seq);

		corosync_exit_error (COROSYNC_DONE_STORE_RINGID);
	}
}

static qb_loop_timer_handle recheck_the_q_level_timer;
void corosync_recheck_the_q_level(void *data)
{
	totempg_check_q_level(corosync_group_handle);
	if (cs_ipcs_q_level_get() == TOTEM_Q_LEVEL_CRITICAL) {
		qb_loop_timer_add(cs_poll_handle_get(), QB_LOOP_MED, 1*QB_TIME_NS_IN_MSEC,
			NULL, corosync_recheck_the_q_level, &recheck_the_q_level_timer);
	}
}

struct sending_allowed_private_data_struct {
	int reserved_msgs;
};


int corosync_sending_allowed (
	unsigned int service,
	unsigned int id,
	const void *msg,
	void *sending_allowed_private_data)
{
	struct sending_allowed_private_data_struct *pd =
		(struct sending_allowed_private_data_struct *)sending_allowed_private_data;
	struct iovec reserve_iovec;
	struct qb_ipc_request_header *header = (struct qb_ipc_request_header *)msg;
	int sending_allowed;

	reserve_iovec.iov_base = (char *)header;
	reserve_iovec.iov_len = header->size;

	pd->reserved_msgs = totempg_groups_joined_reserve (
		corosync_group_handle,
		&reserve_iovec, 1);
	if (pd->reserved_msgs == -1) {
		return -EINVAL;
	}

	/* Message ID out of range */
	if (id >= corosync_service[service]->lib_engine_count) {
		return -EINVAL;
	}

	sending_allowed = QB_FALSE;
	if (corosync_quorum_is_quorate() == 1 ||
	    corosync_service[service]->allow_inquorate == CS_LIB_ALLOW_INQUORATE) {
		// we are quorate
		// now check flow control
		if (corosync_service[service]->lib_engine[id].flow_control == CS_LIB_FLOW_CONTROL_NOT_REQUIRED) {
			sending_allowed = QB_TRUE;
		} else if (pd->reserved_msgs && sync_in_process == 0) {
			sending_allowed = QB_TRUE;
		} else if (pd->reserved_msgs == 0) {
			return -ENOBUFS;
		} else /* (sync_in_process) */ {
			return -EINPROGRESS;
		}
	} else {
		return -EHOSTUNREACH;
	}

	return (sending_allowed);
}

void corosync_sending_allowed_release (void *sending_allowed_private_data)
{
	struct sending_allowed_private_data_struct *pd =
		(struct sending_allowed_private_data_struct *)sending_allowed_private_data;

	if (pd->reserved_msgs == -1) {
		return;
	}
	totempg_groups_joined_release (pd->reserved_msgs);
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

struct scheduler_pause_timeout_data {
	struct totem_config *totem_config;
	qb_loop_timer_handle handle;
	unsigned long long tv_prev;
	unsigned long long max_tv_diff;
};

static void timer_function_scheduler_timeout (void *data)
{
	struct scheduler_pause_timeout_data *timeout_data = (struct scheduler_pause_timeout_data *)data;
	unsigned long long tv_current;
	unsigned long long tv_diff;
	uint64_t schedmiss_event_tstamp;

	tv_current = qb_util_nano_current_get ();

	if (timeout_data->tv_prev == 0) {
		/*
		 * Initial call -> just pretent everything is ok
		 */
		timeout_data->tv_prev = tv_current;
		timeout_data->max_tv_diff = 0;
	}

	tv_diff = tv_current - timeout_data->tv_prev;
	timeout_data->tv_prev = tv_current;

	if (tv_diff > timeout_data->max_tv_diff) {
		schedmiss_event_tstamp = qb_util_nano_from_epoch_get() / QB_TIME_NS_IN_MSEC;

		log_printf (LOGSYS_LEVEL_WARNING, "Corosync main process was not scheduled (@%" PRIu64 ") for %0.4f ms "
		    "(threshold is %0.4f ms). Consider token timeout increase.",
		    schedmiss_event_tstamp,
		    (float)tv_diff / QB_TIME_NS_IN_MSEC, (float)timeout_data->max_tv_diff / QB_TIME_NS_IN_MSEC);

		stats_add_schedmiss_event(schedmiss_event_tstamp, (float)tv_diff / QB_TIME_NS_IN_MSEC);
	}

	/*
	 * Set next threshold, because token_timeout can change
	 */
	timeout_data->max_tv_diff = timeout_data->totem_config->token_timeout * QB_TIME_NS_IN_MSEC * 0.8;
	qb_loop_timer_add (corosync_poll_handle,
		QB_LOOP_MED,
		timeout_data->totem_config->token_timeout * QB_TIME_NS_IN_MSEC / 3,
		timeout_data,
		timer_function_scheduler_timeout,
		&timeout_data->handle);
}


/*
 * Set main pid RR scheduler.
 * silent: don't log sched_get_priority_max and sched_setscheduler errors
 * Returns: 0 - success, -1 failure, -2 platform doesn't support SCHED_RR
 */
static int corosync_set_rr_scheduler (int silent)
{
	int ret_val = 0;

#if defined(HAVE_PTHREAD_SETSCHEDPARAM) && defined(HAVE_SCHED_GET_PRIORITY_MAX) && defined(HAVE_SCHED_SETSCHEDULER)
	int res;

	sched_priority = sched_get_priority_max (SCHED_RR);
	if (sched_priority != -1) {
		global_sched_param.sched_priority = sched_priority;
		res = sched_setscheduler (0, SCHED_RR, &global_sched_param);
		if (res == -1) {
			if (!silent) {
				LOGSYS_PERROR(errno, LOGSYS_LEVEL_WARNING,
					"Could not set SCHED_RR at priority %d",
					global_sched_param.sched_priority);
			}

			global_sched_param.sched_priority = 0;
#ifdef HAVE_QB_LOG_THREAD_PRIORITY_SET
			qb_log_thread_priority_set (SCHED_OTHER, 0);
#endif
			ret_val = -1;
		} else {

			/*
			 * Turn on SCHED_RR in logsys system
			 */
#ifdef HAVE_QB_LOG_THREAD_PRIORITY_SET
			res = qb_log_thread_priority_set (SCHED_RR, sched_priority);
#else
			res = -1;
#endif
			if (res == -1) {
				log_printf (LOGSYS_LEVEL_ERROR,
					    "Could not set logsys thread priority."
					    " Can't continue because of priority inversions.");
				corosync_exit_error (COROSYNC_DONE_LOGSETUP);
			}
		}
	} else {
		if (!silent) {
			LOGSYS_PERROR (errno, LOGSYS_LEVEL_WARNING,
				"Could not get maximum scheduler priority");
		}
		sched_priority = 0;
		ret_val = -1;
	}
#else
	log_printf(LOGSYS_LEVEL_WARNING,
		"The Platform is missing process priority setting features.  Leaving at default.");
	ret_val = -2;
#endif

	return (ret_val);
}


/* The basename man page contains scary warnings about
   thread-safety and portability, hence this */
static const char *corosync_basename(const char *file_name)
{
	char *base;
	base = strrchr (file_name, '/');
	if (base) {
		return base + 1;
	}

	return file_name;
}

static void
_logsys_log_printf(int level, int subsys,
		const char *function_name,
		const char *file_name,
		int file_line,
		const char *format,
		...) __attribute__((format(printf, 6, 7)));

static void
_logsys_log_printf(int level, int subsys,
		const char *function_name,
		const char *file_name,
		int file_line,
		const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	qb_log_from_external_source_va(function_name, corosync_basename(file_name),
				    format, level, file_line,
				    subsys, ap);
	va_end(ap);
}

static void fplay_key_change_notify_fn (
	int32_t event,
	const char *key_name,
	struct icmap_notify_value new_val,
	struct icmap_notify_value old_val,
	void *user_data)
{
	if (strcmp(key_name, "runtime.blackbox.dump_flight_data") == 0) {
		fprintf(stderr,"Writetofile\n");
		corosync_blackbox_write_to_file ();
	}
	if (strcmp(key_name, "runtime.blackbox.dump_state") == 0) {
		fprintf(stderr,"statefump\n");
		corosync_state_dump ();
	}
}

static void corosync_fplay_control_init (void)
{
	icmap_track_t track = NULL;

	icmap_set_string("runtime.blackbox.dump_flight_data", "no");
	icmap_set_string("runtime.blackbox.dump_state", "no");

	icmap_track_add("runtime.blackbox.dump_flight_data",
			ICMAP_TRACK_ADD | ICMAP_TRACK_DELETE | ICMAP_TRACK_MODIFY,
			fplay_key_change_notify_fn,
			NULL, &track);
	icmap_track_add("runtime.blackbox.dump_state",
			ICMAP_TRACK_ADD | ICMAP_TRACK_DELETE | ICMAP_TRACK_MODIFY,
			fplay_key_change_notify_fn,
			NULL, &track);
}

static void force_gather_notify_fn(
	int32_t event,
	const char *key_name,
	struct icmap_notify_value new_val,
	struct icmap_notify_value old_val,
	void *user_data)
{
	char *key_val;

	if (icmap_get_string(key_name, &key_val) == CS_OK && strcmp(key_val, "no") == 0)
		goto out;

	icmap_set_string("runtime.force_gather", "no");

	if (strcmp(key_name, "runtime.force_gather") == 0) {
		log_printf(LOGSYS_LEVEL_ERROR, "Forcing into GATHER state\n");
		totempg_force_gather();
	}

out:
	free(key_val);
}

static void corosync_force_gather_init (void)
{
	icmap_track_t track = NULL;

	icmap_set_string("runtime.force_gather", "no");

	icmap_track_add("runtime.force_gather",
			ICMAP_TRACK_ADD | ICMAP_TRACK_DELETE | ICMAP_TRACK_MODIFY,
			force_gather_notify_fn,
			NULL, &track);
}

/*
 * Set RO flag for keys, which ether doesn't make sense to change by user (statistic)
 * or which when changed are not reflected by runtime (totem.crypto_cipher, ...).
 *
 * Also some RO keys cannot be determined in this stage, so they are set later in
 * other functions (like nodelist.local_node_pos, ...)
 */
static void set_icmap_ro_keys_flag (void)
{
	/*
	 * Set RO flag for all keys of internal configuration and runtime statistics
	 */
	icmap_set_ro_access("internal_configuration.", CS_TRUE, CS_TRUE);
	icmap_set_ro_access("runtime.services.", CS_TRUE, CS_TRUE);
	icmap_set_ro_access("runtime.config.", CS_TRUE, CS_TRUE);
	icmap_set_ro_access("runtime.totem.", CS_TRUE, CS_TRUE);
	icmap_set_ro_access("uidgid.config.", CS_TRUE, CS_TRUE);
	icmap_set_ro_access("system.", CS_TRUE, CS_TRUE);
	icmap_set_ro_access("nodelist.", CS_TRUE, CS_TRUE);

	/*
	 * Set RO flag for constrete keys of configuration which can't be changed
	 * during runtime
	 */
	icmap_set_ro_access("totem.crypto_cipher", CS_FALSE, CS_TRUE);
	icmap_set_ro_access("totem.crypto_hash", CS_FALSE, CS_TRUE);
	icmap_set_ro_access("totem.crypto_model", CS_FALSE, CS_TRUE);
	icmap_set_ro_access("totem.keyfile", CS_FALSE, CS_TRUE);
	icmap_set_ro_access("totem.key", CS_FALSE, CS_TRUE);
	icmap_set_ro_access("totem.secauth", CS_FALSE, CS_TRUE);
	icmap_set_ro_access("totem.ip_version", CS_FALSE, CS_TRUE);
	icmap_set_ro_access("totem.transport", CS_FALSE, CS_TRUE);
	icmap_set_ro_access("totem.cluster_name", CS_FALSE, CS_TRUE);
	icmap_set_ro_access("totem.netmtu", CS_FALSE, CS_TRUE);
	icmap_set_ro_access("totem.threads", CS_FALSE, CS_TRUE);
	icmap_set_ro_access("totem.version", CS_FALSE, CS_TRUE);
	icmap_set_ro_access("totem.nodeid", CS_FALSE, CS_TRUE);
	icmap_set_ro_access("totem.clear_node_high_bit", CS_FALSE, CS_TRUE);
	icmap_set_ro_access("config.reload_in_progress", CS_FALSE, CS_TRUE);
	icmap_set_ro_access("config.totemconfig_reload_in_progress", CS_FALSE, CS_TRUE);
}

static void main_service_ready (void)
{
	int res;

	/*
	 * This must occur after totempg is initialized because "this_ip" must be set
	 */
	res = corosync_service_defaults_link_and_init (api);
	if (res == -1) {
		log_printf (LOGSYS_LEVEL_ERROR, "Could not initialize default services");
		corosync_exit_error (COROSYNC_DONE_INIT_SERVICES);
	}
	cs_ipcs_init();
	corosync_totem_stats_init ();
	corosync_fplay_control_init ();
	corosync_force_gather_init ();

	sync_init (
		corosync_sync_callbacks_retrieve,
		corosync_sync_completed);
}

static enum e_corosync_done corosync_flock (const char *lockfile, pid_t pid)
{
	struct flock lock;
	enum e_corosync_done err;
	char pid_s[17];
	int fd_flag;

	err = COROSYNC_DONE_EXIT;

	lockfile_fd = open (lockfile, O_WRONLY | O_CREAT, 0640);
	if (lockfile_fd == -1) {
		log_printf (LOGSYS_LEVEL_ERROR, "Corosync Executive couldn't create lock file.");
		return (COROSYNC_DONE_ACQUIRE_LOCK);
	}

retry_fcntl:
	lock.l_type = F_WRLCK;
	lock.l_start = 0;
	lock.l_whence = SEEK_SET;
	lock.l_len = 0;
	if (fcntl (lockfile_fd, F_SETLK, &lock) == -1) {
		switch (errno) {
		case EINTR:
			goto retry_fcntl;
			break;
		case EAGAIN:
		case EACCES:
			log_printf (LOGSYS_LEVEL_ERROR, "Another Corosync instance is already running.");
			err = COROSYNC_DONE_ALREADY_RUNNING;
			goto error_close;
			break;
		default:
			log_printf (LOGSYS_LEVEL_ERROR, "Corosync Executive couldn't acquire lock. Error was %s",
			    strerror(errno));
			err = COROSYNC_DONE_ACQUIRE_LOCK;
			goto error_close;
			break;
		}
	}

	if (ftruncate (lockfile_fd, 0) == -1) {
		log_printf (LOGSYS_LEVEL_ERROR, "Corosync Executive couldn't truncate lock file. Error was %s",
		    strerror (errno));
		err = COROSYNC_DONE_ACQUIRE_LOCK;
		goto error_close_unlink;
	}

	memset (pid_s, 0, sizeof (pid_s));
	snprintf (pid_s, sizeof (pid_s) - 1, "%u\n", pid);

retry_write:
	if (write (lockfile_fd, pid_s, strlen (pid_s)) != strlen (pid_s)) {
		if (errno == EINTR) {
			goto retry_write;
		} else {
			log_printf (LOGSYS_LEVEL_ERROR, "Corosync Executive couldn't write pid to lock file. "
				"Error was %s", strerror (errno));
			err = COROSYNC_DONE_ACQUIRE_LOCK;
			goto error_close_unlink;
		}
	}

	if ((fd_flag = fcntl (lockfile_fd, F_GETFD, 0)) == -1) {
		log_printf (LOGSYS_LEVEL_ERROR, "Corosync Executive couldn't get close-on-exec flag from lock file. "
			"Error was %s", strerror (errno));
		err = COROSYNC_DONE_ACQUIRE_LOCK;
		goto error_close_unlink;
	}
	fd_flag |= FD_CLOEXEC;
	if (fcntl (lockfile_fd, F_SETFD, fd_flag) == -1) {
		log_printf (LOGSYS_LEVEL_ERROR, "Corosync Executive couldn't set close-on-exec flag to lock file. "
			"Error was %s", strerror (errno));
		err = COROSYNC_DONE_ACQUIRE_LOCK;
		goto error_close_unlink;
	}

	return (err);

error_close_unlink:
	unlink (lockfile);
error_close:
	close (lockfile_fd);

	return (err);
}

static int corosync_move_to_root_cgroup(void) {
	FILE *f;
	int res = -1;
	const char *cgroup_task_fname = NULL;

	/*
	 * /sys/fs/cgroup is hardcoded, because most of Linux distributions are now
	 * using systemd and systemd uses hardcoded path of cgroup mount point.
	 *
	 * This feature is expected to be removed as soon as systemd gets support
	 * for managing RT configuration.
	 */
	f = fopen("/sys/fs/cgroup/cpu/cpu.rt_runtime_us", "rt");
	if (f == NULL) {
		/*
		 * Try cgroup v2
		 */
		f = fopen("/sys/fs/cgroup/cgroup.procs", "rt");
		if (f == NULL) {
			log_printf(LOG_DEBUG, "cpu.rt_runtime_us or cgroup.procs doesn't exist -> "
			    "system without cgroup or with disabled CONFIG_RT_GROUP_SCHED");

			res = 0;
			goto exit_res;
		} else {
			log_printf(LOGSYS_LEVEL_DEBUG, "Moving main pid to cgroup v2 root cgroup");

			cgroup_task_fname = "/sys/fs/cgroup/cgroup.procs";
		}
	} else {
		log_printf(LOGSYS_LEVEL_DEBUG, "Moving main pid to cgroup v1 root cgroup");

		cgroup_task_fname = "/sys/fs/cgroup/cpu/tasks";
	}
	(void)fclose(f);

	f = fopen(cgroup_task_fname, "w");
	if (f == NULL) {
		log_printf(LOGSYS_LEVEL_WARNING, "Can't open cgroups tasks file for writing");

		goto exit_res;
	}

	if (fprintf(f, "%jd\n", (intmax_t)getpid()) <= 0) {
		log_printf(LOGSYS_LEVEL_WARNING, "Can't write corosync pid into cgroups tasks file");

		goto close_and_exit_res;
	}

close_and_exit_res:
	if (fclose(f) != 0) {
		log_printf(LOGSYS_LEVEL_WARNING, "Can't close cgroups tasks file");

		goto exit_res;
	}

exit_res:
	 return (res);
}

static void show_version_info_crypto(void)
{
	const char *error_string;
	const char *list_str;

	if (util_is_valid_knet_crypto_model(NULL, &list_str, 1, "", &error_string) != -1) {
		printf("Available crypto models: %s\n", list_str);
	} else {
		perror(error_string);
	}
}

static void show_version_info_compress(void)
{
	const char *error_string;
	const char *list_str;

	if (util_is_valid_knet_compress_model(NULL, &list_str, 1, "", &error_string) != -1) {
		printf("Available compression models: %s\n", list_str);
	} else {
		perror(error_string);
	}
}

static void show_version_info(void)
{

	printf ("Corosync Cluster Engine, version '%s'\n", VERSION);
	printf ("Copyright (c) 2006-2021 Red Hat, Inc.\n");

	printf ("\nBuilt-in features:" PACKAGE_FEATURES "\n");

	show_version_info_crypto();
	show_version_info_compress();
}

int main (int argc, char **argv, char **envp)
{
	const char *error_string;
	struct totem_config totem_config;
	int res, ch;
	int background, sched_rr, prio, testonly;
	enum move_to_root_cgroup_mode move_to_root_cgroup;
	enum e_corosync_done flock_err;
	uint64_t totem_config_warnings;
	struct scheduler_pause_timeout_data scheduler_pause_timeout_data;
	long int tmpli;
	char *ep;
	char *tmp_str;
	int log_subsys_id_totem;
	int silent;

	/* default configuration
	 */
	background = 1;
	testonly = 0;

	while ((ch = getopt (argc, argv, "c:l:ftv")) != EOF) {

		switch (ch) {
			case 'c':
				res = snprintf(corosync_config_file, sizeof(corosync_config_file), "%s", optarg);
				if (res >= sizeof(corosync_config_file)) {
					fprintf (stderr, "Config file path too long.\n");
					syslog (LOGSYS_LEVEL_ERROR, "Config file path too long.");

					logsys_system_fini();
					return EXIT_FAILURE;
				}
				break;
			case 'l':
				res = snprintf(corosync_lock_file, sizeof(corosync_lock_file), "%s", optarg);
				if (res >= sizeof(corosync_lock_file)) {
					fprintf (stderr, "PID lock file path too long.\n");
					syslog (LOGSYS_LEVEL_ERROR, "PID lock file path too long.");

					logsys_system_fini();
					return EXIT_FAILURE;
				}
				break;
			case 'f':
				background = 0;
				break;
			case 't':
				testonly = 1;
				break;
			case 'v':
				show_version_info();
				logsys_system_fini();
				return EXIT_SUCCESS;

				break;
			default:
				fprintf(stderr, \
					"usage:\n"\
					"        -c     : Corosync config file path.\n"\
					"        -l     : Corosync pid lock file path.\n"\
					"        -f     : Start application in foreground.\n"\
					"        -t     : Test configuration and exit.\n"\
					"        -v     : Display version, git revision and some useful information about Corosync and exit.\n");
				logsys_system_fini();
				return EXIT_FAILURE;
		}
	}


	/*
	 * Other signals are registered later via qb_loop_signal_add
	 */
	(void)signal (SIGSEGV, sigsegv_handler);
	(void)signal (SIGABRT, sigsegv_handler);
#if MSG_NOSIGNAL != 0
	(void)signal (SIGPIPE, SIG_IGN);
#endif

	if (icmap_init() != CS_OK) {
		fprintf (stderr, "Corosync Executive couldn't initialize configuration component.\n");
		syslog (LOGSYS_LEVEL_ERROR, "Corosync Executive couldn't initialize configuration component.");
		corosync_exit_error (COROSYNC_DONE_ICMAP);
	}
	set_icmap_ro_keys_flag();

	/*
	 * Initialize the corosync_api_v1 definition
	 */
	api = apidef_get ();

	res = coroparse_configparse(icmap_get_global_map(), &error_string);
	if (res == -1) {
		/*
		 * Logsys can't log properly at this early stage, and we need to get this message out
		 *
		 */
		fprintf (stderr, "%s\n", error_string);
		syslog (LOGSYS_LEVEL_ERROR, "%s", error_string);
		corosync_exit_error (COROSYNC_DONE_MAINCONFIGREAD);
	}

	if (stats_map_init(api) != CS_OK) {
		fprintf (stderr, "Corosync Executive couldn't initialize statistics component.\n");
		syslog (LOGSYS_LEVEL_ERROR, "Corosync Executive couldn't initialize statistics component.");
		corosync_exit_error (COROSYNC_DONE_STATS);
	}

	res = corosync_log_config_read (&error_string);
	if (res == -1) {
		/*
		 * if we are here, we _must_ flush the logsys queue
		 * and try to inform that we couldn't read the config.
		 * this is a desperate attempt before certain death
		 * and there is no guarantee that we can print to stderr
		 * nor that logsys is sending the messages where we expect.
		 */
		log_printf (LOGSYS_LEVEL_ERROR, "%s", error_string);
		fprintf(stderr, "%s", error_string);
		syslog (LOGSYS_LEVEL_ERROR, "%s", error_string);
		corosync_exit_error (COROSYNC_DONE_LOGCONFIGREAD);
	}

	if (!testonly) {
		log_printf (LOGSYS_LEVEL_NOTICE, "Corosync Cluster Engine %s starting up", VERSION);
		log_printf (LOGSYS_LEVEL_INFO, "Corosync built-in features:" PACKAGE_FEATURES "");
	}

	/*
	 * Create totem logsys subsys before totem_config_read so log functions can be used
	 */
	log_subsys_id_totem = _logsys_subsys_create("TOTEM", "totem,"
			"totemip.c,totemconfig.c,totemcrypto.c,totemsrp.c,"
			"totempg.c,totemudp.c,totemudpu.c,totemnet.c,totemknet.c");

	res = chdir(get_state_dir());
	if (res == -1) {
		log_printf (LOGSYS_LEVEL_ERROR, "Cannot chdir to state directory %s. %s", get_state_dir(), strerror(errno));
		corosync_exit_error (COROSYNC_DONE_DIR_NOT_PRESENT);
	}

	res = totem_config_read (&totem_config, &error_string, &totem_config_warnings);
	if (res == -1) {
		log_printf (LOGSYS_LEVEL_ERROR, "%s", error_string);
		corosync_exit_error (COROSYNC_DONE_MAINCONFIGREAD);
	}

	if (totem_config_warnings & TOTEM_CONFIG_WARNING_MEMBERS_IGNORED) {
		log_printf (LOGSYS_LEVEL_WARNING, "member section is used together with nodelist. Members ignored.");
	}

	if (totem_config_warnings & TOTEM_CONFIG_WARNING_MEMBERS_DEPRECATED) {
		log_printf (LOGSYS_LEVEL_WARNING, "member section is deprecated.");
	}

	if (totem_config_warnings & TOTEM_CONFIG_WARNING_TOTEM_NODEID_SET) {
		log_printf (LOGSYS_LEVEL_WARNING, "nodeid in totem section is deprecated and ignored. "
		    "Nodelist (or autogenerated) nodeid is going to be used.");
	}

	if (totem_config_warnings & TOTEM_CONFIG_BINDNETADDR_NODELIST_SET) {
		log_printf (LOGSYS_LEVEL_WARNING, "interface section bindnetaddr is used together with nodelist. "
		    "Nodelist one is going to be used.");
	}

	if (totem_config_warnings != 0) {
		log_printf (LOGSYS_LEVEL_WARNING, "Please migrate config file to nodelist.");
	}

	res = totem_config_validate (&totem_config, &error_string);
	if (res == -1) {
		log_printf (LOGSYS_LEVEL_ERROR, "%s", error_string);
		corosync_exit_error (COROSYNC_DONE_MAINCONFIGREAD);
	}

	if (testonly) {
		corosync_exit_error (COROSYNC_DONE_EXIT);
	}


	move_to_root_cgroup = MOVE_TO_ROOT_CGROUP_MODE_AUTO;
	if (icmap_get_string("system.move_to_root_cgroup", &tmp_str) == CS_OK) {
		/*
		 * Validity of move_to_root_cgroup values checked in coroparse.c
		 */
		if (strcmp(tmp_str, "yes") == 0) {
			move_to_root_cgroup = MOVE_TO_ROOT_CGROUP_MODE_ON;
		} else if (strcmp(tmp_str, "no") == 0) {
			move_to_root_cgroup = MOVE_TO_ROOT_CGROUP_MODE_OFF;
		}
		free(tmp_str);
	}


	sched_rr = 1;
	if (icmap_get_string("system.sched_rr", &tmp_str) == CS_OK) {
		if (strcmp(tmp_str, "yes") != 0) {
			sched_rr = 0;
		}
		free(tmp_str);
	}

	prio = 0;
	if (icmap_get_string("system.priority", &tmp_str) == CS_OK) {
		if (strcmp(tmp_str, "max") == 0) {
			prio = INT_MIN;
		} else if (strcmp(tmp_str, "min") == 0) {
			prio = INT_MAX;
		} else {
			errno = 0;

			tmpli = strtol(tmp_str, &ep, 10);
			if (errno != 0 || *ep != '\0' || tmpli > INT_MAX || tmpli < INT_MIN) {
				log_printf (LOGSYS_LEVEL_ERROR, "Priority value %s is invalid", tmp_str);
				corosync_exit_error (COROSYNC_DONE_MAINCONFIGREAD);
			}

			prio = tmpli;
		}

		free(tmp_str);
	}

	if (move_to_root_cgroup == MOVE_TO_ROOT_CGROUP_MODE_ON) {
		/*
		 * Try to move corosync into root cpu cgroup. Failure is not fatal and
		 * error is deliberately ignored.
		 */
		(void)corosync_move_to_root_cgroup();
	}

	/*
	 * Set round robin realtime scheduling with priority 99
	 */
	if (sched_rr) {
		silent = (move_to_root_cgroup == MOVE_TO_ROOT_CGROUP_MODE_AUTO);
		res = corosync_set_rr_scheduler (silent);

		if (res == -1 && move_to_root_cgroup == MOVE_TO_ROOT_CGROUP_MODE_AUTO) {
			/*
			 * Try to move process to root cgroup and try set priority again
			 */
			(void)corosync_move_to_root_cgroup();

			res = corosync_set_rr_scheduler (0);
		}

		if (res != 0) {
			prio = INT_MIN;
		} else {
			prio = 0;
		}
	}

	if (prio != 0) {
		if (setpriority(PRIO_PGRP, 0, prio) != 0) {
			LOGSYS_PERROR(errno, LOGSYS_LEVEL_WARNING,
				"Could not set priority %d", prio);
		}
	}

	totem_config.totem_memb_ring_id_create_or_load = corosync_ring_id_create_or_load;
	totem_config.totem_memb_ring_id_store = corosync_ring_id_store;

	totem_config.totem_logging_configuration = totem_logging_configuration;
	totem_config.totem_logging_configuration.log_subsys_id = log_subsys_id_totem;

	totem_config.totem_logging_configuration.log_level_security = LOGSYS_LEVEL_WARNING;
	totem_config.totem_logging_configuration.log_level_error = LOGSYS_LEVEL_ERROR;
	totem_config.totem_logging_configuration.log_level_warning = LOGSYS_LEVEL_WARNING;
	totem_config.totem_logging_configuration.log_level_notice = LOGSYS_LEVEL_NOTICE;
	totem_config.totem_logging_configuration.log_level_debug = LOGSYS_LEVEL_DEBUG;
	totem_config.totem_logging_configuration.log_level_trace = LOGSYS_LEVEL_TRACE;
	totem_config.totem_logging_configuration.log_printf = _logsys_log_printf;

	logsys_config_apply();

	/*
	 * Now we are fully initialized.
	 */
	if (background) {
		logsys_blackbox_prefork();

		corosync_tty_detach ();

		logsys_blackbox_postfork();

		log_printf (LOGSYS_LEVEL_DEBUG, "Corosync TTY detached");
	}

	/*
	 * Lock all memory to avoid page faults which may interrupt
	 * application healthchecking
	 */
	corosync_mlockall ();

	corosync_poll_handle = qb_loop_create ();

	memset(&scheduler_pause_timeout_data, 0, sizeof(scheduler_pause_timeout_data));
	scheduler_pause_timeout_data.totem_config = &totem_config;
	timer_function_scheduler_timeout (&scheduler_pause_timeout_data);

	qb_loop_signal_add(corosync_poll_handle, QB_LOOP_LOW,
		SIGUSR2, NULL, sig_diag_handler, NULL);
	qb_loop_signal_add(corosync_poll_handle, QB_LOOP_HIGH,
		SIGINT, NULL, sig_exit_handler, NULL);
	qb_loop_signal_add(corosync_poll_handle, QB_LOOP_HIGH,
		SIGQUIT, NULL, sig_exit_handler, NULL);
	qb_loop_signal_add(corosync_poll_handle, QB_LOOP_HIGH,
		SIGTERM, NULL, sig_exit_handler, NULL);

	if (logsys_thread_start() != 0) {
		log_printf (LOGSYS_LEVEL_ERROR, "Can't initialize log thread");
		corosync_exit_error (COROSYNC_DONE_LOGCONFIGREAD);
	}

	if ((flock_err = corosync_flock (corosync_lock_file, getpid ())) != COROSYNC_DONE_EXIT) {
		corosync_exit_error (flock_err);
	}

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
	if (totempg_initialize (
		corosync_poll_handle,
		&totem_config) != 0) {

		log_printf (LOGSYS_LEVEL_ERROR, "Can't initialize TOTEM layer");
		corosync_exit_error (COROSYNC_DONE_FATAL_ERR);
	}

	totempg_service_ready_register (
		main_service_ready);

	totempg_groups_initialize (
		&corosync_group_handle,
		deliver_fn,
		confchg_fn);

	totempg_groups_join (
		corosync_group_handle,
		&corosync_group,
		1);

	/*
	 * Drop root privleges to user 'corosync'
	 * TODO: Don't really need full root capabilities;
	 *       needed capabilities are:
	 * CAP_NET_RAW (bindtodevice)
	 * CAP_SYS_NICE (setscheduler)
	 * CAP_IPC_LOCK (mlockall)
	 */
	priv_drop ();

	schedwrk_init (
		serialize_lock,
		serialize_unlock);

	/*
	 * Start main processing loop
	 */
	qb_loop_run (corosync_poll_handle);

	/*
	 * Exit was requested
	 */
	totempg_finalize ();

	/*
	 * free the loop resources
	 */
	qb_loop_destroy (corosync_poll_handle);

	/*
	 * free up the icmap 
	 */

	/*
	 * Remove pid lock file
	 */
	close (lockfile_fd);
	unlink (corosync_lock_file);

	corosync_exit_error (COROSYNC_DONE_EXIT);

	return EXIT_SUCCESS;
}
