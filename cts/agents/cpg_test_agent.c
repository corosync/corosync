/*
 * Copyright (c) 2010 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Angus Salkeld (asalkeld@redhat.com)
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
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <syslog.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>

#include <corosync/totem/coropoll.h>
#include <corosync/list.h>
#include <corosync/cpg.h>


#define SERVER_PORT "9034"

typedef enum {
	MSG_OK,
	MSG_NODEID_ERR,
	MSG_PID_ERR,
	MSG_SEQ_ERR,
	MSG_SIZE_ERR,
	MSG_HASH_ERR,
} msg_status_t;

typedef struct {
	uint32_t nodeid;
	pid_t   pid;
	uint32_t hash;
	uint32_t seq;
	size_t size;
	char payload[1];
} msg_t;

#define LOG_STR_SIZE 128
typedef struct {
	char log[LOG_STR_SIZE];
	struct list_head list;
} log_entry_t;

#define HOW_BIG_AND_BUF 4096
static char big_and_buf[HOW_BIG_AND_BUF];
static char big_and_buf_rx[HOW_BIG_AND_BUF];
static int32_t parse_debug = 0;
static int32_t record_config_events_g = 0;
static int32_t record_messages_g = 0;
static cpg_handle_t cpg_handle = 0;
static int32_t cpg_fd = -1;
static struct list_head config_chg_log_head;
static struct list_head msg_log_head;
static pid_t my_pid;
static uint32_t my_nodeid;
static int32_t my_seq;
static int32_t my_msgs_to_send;
static int32_t total_stored_msgs = 0;
static hdb_handle_t poll_handle;


static void delivery_callback (
	cpg_handle_t handle,
	const struct cpg_name *groupName,
	uint32_t nodeid,
	uint32_t pid,
	void *msg,
	size_t msg_len)
{
	log_entry_t *log_pt;
	msg_t *msg_pt = (msg_t*)msg;
	msg_status_t status = MSG_OK;

	if (record_messages_g == 0) {
		return;
	}

	msg_pt->seq = my_seq;
	my_seq++;

	if (nodeid != msg_pt->nodeid) {
		status = MSG_NODEID_ERR;
	}
	if (pid != msg_pt->pid) {
		status = MSG_PID_ERR;
	}
	if (msg_len != msg_pt->size) {
		status = MSG_SIZE_ERR;
	}
	/* TODO: check hash here.
	*/

	log_pt = malloc (sizeof(log_entry_t));
	list_init (&log_pt->list);
	snprintf (log_pt->log, 128, "%d:%d:%d:%d;",
		msg_pt->nodeid, msg_pt->pid, msg_pt->seq, status);
	list_add_tail (&log_pt->list, &msg_log_head);
	total_stored_msgs++;
}

static void config_change_callback (
	cpg_handle_t handle,
	const struct cpg_name *groupName,
	const struct cpg_address *member_list, size_t member_list_entries,
	const struct cpg_address *left_list, size_t left_list_entries,
	const struct cpg_address *joined_list, size_t joined_list_entries)
{
	int i;
	log_entry_t *log_pt;

	/* group_name,ip,pid,join|leave */

	if (record_config_events_g == 0) {
		return;
	}
	for (i = 0; i < left_list_entries; i++) {
		syslog (LOG_DEBUG, "%s() inserting leave event into list", __func__);

		log_pt = malloc (sizeof(log_entry_t));
		list_init (&log_pt->list);
		snprintf (log_pt->log, 256, "%s,%d,%d,left",
			groupName->value, left_list[i].nodeid,left_list[i].pid);
		list_add_tail(&log_pt->list, &config_chg_log_head);
	}
	for (i = 0; i < joined_list_entries; i++) {
		syslog (LOG_DEBUG, "%s() inserting join event into list", __func__);

		log_pt = malloc (sizeof(log_entry_t));
		list_init (&log_pt->list);
		snprintf (log_pt->log, 256, "%s,%d,%d,join",
			groupName->value, joined_list[i].nodeid,joined_list[i].pid);
		list_add_tail (&log_pt->list, &config_chg_log_head);
	}
}

static cpg_callbacks_t callbacks = {
	.cpg_deliver_fn = delivery_callback,
	.cpg_confchg_fn = config_change_callback,
};

static void record_messages (void)
{
	record_messages_g = 1;
	syslog (LOG_DEBUG,"%s() record:%d", __func__, record_messages_g);
}

static void record_config_events (void)
{
	record_config_events_g = 1;
	syslog (LOG_DEBUG,"%s() record:%d", __func__, record_config_events_g);
}

static void read_config_event (int sock)
{
	const char *empty = "None";
	struct list_head * list = config_chg_log_head.next;
	log_entry_t *entry;

	if (list != &config_chg_log_head) {
		entry = list_entry (list, log_entry_t, list);
		send (sock, entry->log,	strlen (entry->log) + 1, 0);
		list_del (&entry->list);
		free (entry);
	} else {
		syslog (LOG_DEBUG,"%s() no events in list", __func__);
		send (sock, empty, strlen (empty) + 1, 0);
	}
}

static void read_messages (int sock, char* atmost_str)
{
	struct list_head * list;
	log_entry_t *entry;
	int atmost = atoi (atmost_str);
	int packed = 0;

	if (atmost == 0)
		atmost = 1;
	if (atmost > (HOW_BIG_AND_BUF / LOG_STR_SIZE))
		atmost = (HOW_BIG_AND_BUF / LOG_STR_SIZE);

	syslog (LOG_DEBUG, "%s() atmost %d; total_stored_msgs:%d",
		__func__, atmost, total_stored_msgs);
	big_and_buf[0] = '\0';

	for (list = msg_log_head.next;
		(!list_empty (&msg_log_head) && packed < atmost); ) {

		entry = list_entry (list, log_entry_t, list);

		strcat (big_and_buf, entry->log);
		packed++;

		list = list->next;
		list_del (&entry->list);
		free (entry);

		total_stored_msgs--;
	}
	syslog (LOG_DEBUG, "%s() sending %d; total_stored_msgs:%d; len:%d",
		__func__, packed, total_stored_msgs, (int)strlen (big_and_buf));
	if (packed == 0) {
		strcpy (big_and_buf, "None");
	}
	send (sock, big_and_buf, strlen (big_and_buf), 0);
}

static void send_some_more_messages (void)
{
	msg_t my_msg;
	struct iovec iov[1];
	int i;
	int send_now;
	cs_error_t res;
	cpg_flow_control_state_t fc_state;

	if (cpg_fd < 0)
		return;

	send_now = my_msgs_to_send;

	syslog (LOG_DEBUG,"%s() send_now:%d", __func__, send_now);
	my_msg.pid = my_pid;
	my_msg.nodeid = my_nodeid;
	my_msg.hash = 0;
	my_msg.size = sizeof (msg_t);
	my_msg.seq = 0;

	iov[0].iov_len = my_msg.size;
	iov[0].iov_base = &my_msg;

	for (i = 0; i < send_now; i++) {

		res = cpg_flow_control_state_get (cpg_handle, &fc_state);
		if (res == CS_OK && fc_state == CPG_FLOW_CONTROL_ENABLED) {
			/* lets do this later */
			syslog (LOG_DEBUG, "%s() flow control enabled.", __func__);
			return;
		}

		res = cpg_mcast_joined (cpg_handle, CPG_TYPE_AGREED, iov, 1);
		if (res == CS_ERR_TRY_AGAIN) {
			/* lets do this later */
			syslog (LOG_DEBUG, "%s() cpg_mcast_joined() says try again.",
				__func__);
			return;
		} else
			if (res != CS_OK) {
				syslog (LOG_ERR, "%s() -> cpg_mcast_joined error:%d, exiting.",
					__func__, res);
				exit (-2);
			}

		my_msgs_to_send--;
	}
}

static void msg_blaster (int sock, char* num_to_send_str)
{
	my_msgs_to_send = atoi (num_to_send_str);
	my_seq = 1;
	my_pid = getpid();

	cpg_local_get (cpg_handle, &my_nodeid);

	/* control the limits */
	if (my_msgs_to_send <= 0)
		my_msgs_to_send = 1;
	if (my_msgs_to_send > 1000)
		my_msgs_to_send = 1000;

	send_some_more_messages ();
}


static int cpg_dispatch_wrapper_fn (hdb_handle_t handle,
	int fd,
	int revents,
	void *data)
{
	cs_error_t error = cpg_dispatch (cpg_handle, CS_DISPATCH_ALL);
	if (error == CS_ERR_LIBRARY) {
		syslog (LOG_ERR, "%s() got LIB error disconnecting from corosync.", __func__);
		poll_dispatch_delete (poll_handle, cpg_fd);
		close (cpg_fd);
		cpg_fd = -1;
	}
	return 0;
}

static void do_command (int sock, char* func, char*args[], int num_args)
{
	int result;
	struct cpg_name group_name;

	if (parse_debug)
		syslog (LOG_DEBUG,"RPC:%s() called.", func);

	if (strcmp ("cpg_mcast_joined",func) == 0) {
		struct iovec iov[5];
		int a;

		for (a = 0; a < num_args; a++) {
			iov[a].iov_base = args[a];
			iov[a].iov_len = strlen(args[a])+1;
		}
		cpg_mcast_joined (cpg_handle, CPG_TYPE_AGREED, iov, num_args);

	} else if (strcmp ("cpg_join",func) == 0) {

		strcpy (group_name.value, args[0]);
		group_name.length = strlen(args[0]);

		result = cpg_join (cpg_handle, &group_name);
		if (result != CS_OK) {
			syslog (LOG_ERR,
				"Could not join process group, error %d\n", result);
			exit (1);
		}

	} else if (strcmp ("cpg_leave",func) == 0) {

		strcpy (group_name.value, args[0]);
		group_name.length = strlen(args[0]);

		result = cpg_leave (cpg_handle, &group_name);
		if (result != CS_OK) {
			syslog (LOG_ERR,
				"Could not leave process group, error %d\n", result);
			exit (1);
		}
		syslog (LOG_INFO, "called cpg_leave()!");

	} else if (strcmp ("cpg_initialize",func) == 0) {
		int retry_count = 0;

		result = cpg_initialize (&cpg_handle, &callbacks);
		while (result != CS_OK) {
			syslog (LOG_ERR,
				"cpg_initialize error %d (attempt %d)\n",
				result, retry_count);
			if (retry_count >= 3) {
				exit (1);
			}
			sleep(1);
			retry_count++;
		}

		cpg_fd_get (cpg_handle, &cpg_fd);
		poll_dispatch_add (poll_handle, cpg_fd, POLLIN|POLLNVAL, NULL, cpg_dispatch_wrapper_fn);

	} else if (strcmp ("cpg_local_get", func) == 0) {
		unsigned int local_nodeid;
		char response[100];

		cpg_local_get (cpg_handle, &local_nodeid);
		snprintf (response, 100, "%u",local_nodeid);
		send (sock, response, strlen (response) + 1, 0);

	} else if (strcmp ("cpg_finalize",func) == 0) {

		cpg_finalize (cpg_handle);
		poll_dispatch_delete (poll_handle, cpg_fd);
		cpg_fd = -1;

	} else if (strcmp ("record_config_events",func) == 0) {

		record_config_events ();

	} else if (strcmp ("record_messages",func) == 0) {

		record_messages ();

	} else if (strcmp ("read_config_event",func) == 0) {

		read_config_event (sock);

	} else if (strcmp ("read_messages",func) == 0) {

		read_messages (sock, args[0]);

	} else if (strcmp ("msg_blaster",func) == 0) {

		msg_blaster (sock, args[0]);

	} else {
		syslog (LOG_ERR,"%s RPC:%s not supported!", __func__, func);
	}
}

static void handle_command (int sock, char* msg)
{
	int num_args;
	char *saveptr = NULL;
	char *str = strdup (msg);
	char *str_len;
	char *str_arg;
	char *args[5];
	int i = 0;
	int a = 0;
	char* func = NULL;

	if (parse_debug)
		syslog (LOG_DEBUG,"%s (MSG:%s)\n", __func__, msg);

	str_len = strtok_r (str, ":", &saveptr);
	assert (str_len);

	num_args = atoi (str_len) * 2;
	for (i = 0; i < num_args / 2; i++) {
		str_len = strtok_r (NULL, ":", &saveptr);
		str_arg = strtok_r (NULL, ":", &saveptr);
		if (func == NULL) {
			/* first "arg" is the function */
			if (parse_debug)
				syslog (LOG_DEBUG, "(LEN:%s, FUNC:%s)", str_len, str_arg);
			func = str_arg;
			a = 0;
		} else {
			args[a] = str_arg;
			a++;
			if (parse_debug)
				syslog (LOG_DEBUG, "(LEN:%s, ARG:%s)", str_len, str_arg);
		}
	}
	do_command (sock, func, args, a+1);

	free (str);
}

static int server_process_data_fn (hdb_handle_t handle,
	int fd,
	int revents,
	void *data)
{
	char *saveptr;
	char *msg;
	char *cmd;
	int32_t nbytes;

	if ((nbytes = recv (fd, big_and_buf_rx, sizeof (big_and_buf_rx), 0)) <= 0) {
		/* got error or connection closed by client */
		if (nbytes == 0) {
			/* connection closed */
			syslog (LOG_WARNING, "socket %d hung up: exiting...\n", fd);
		} else {
			syslog (LOG_ERR,"recv() failed: %s", strerror(errno));
		}
		close (fd);
		exit (0);
	} else {
		if (my_msgs_to_send > 0)
			send_some_more_messages ();

		big_and_buf_rx[nbytes] = '\0';

		msg = strtok_r (big_and_buf_rx, ";", &saveptr);
		assert (msg);
		while (msg) {
			cmd = strdup (msg);
			handle_command (fd, cmd);
			free (cmd);
			msg = strtok_r (NULL, ";", &saveptr);
		}
	}

	return 0;
}

static int server_accept_fn (hdb_handle_t handle,
	int fd,
	int revents,
	void *data)
{
	socklen_t addrlen;
	struct sockaddr_in in_addr;
	int new_fd;
	int res;

	addrlen = sizeof (struct sockaddr_in);

retry_accept:
	new_fd = accept (fd, (struct sockaddr *)&in_addr, &addrlen);
	if (new_fd == -1 && errno == EINTR) {
		goto retry_accept;
	}

	if (new_fd == -1) {
		syslog (LOG_ERR,
			"Could not accept connection: %s\n", strerror (errno));
		return (0); /* This is an error, but -1 would indicate disconnect from poll loop */
	}

	res = fcntl (new_fd, F_SETFL, O_NONBLOCK);
	if (res == -1) {
		syslog (LOG_ERR,
			"Could not set non-blocking operation on connection: %s\n",
			strerror (errno));
		close (new_fd);
		return (0); /* This is an error, but -1 would indicate disconnect from poll loop */
	}

	poll_dispatch_add (poll_handle, new_fd, POLLIN|POLLNVAL, NULL, server_process_data_fn);
	return 0;
}

static int create_server_sockect (void)
{
	int listener;
	int yes = 1;
	int rv;
	struct addrinfo hints, *ai, *p;

	/* get a socket and bind it
	 */
	memset (&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	if ((rv = getaddrinfo (NULL, SERVER_PORT, &hints, &ai)) != 0) {
		syslog (LOG_ERR, "%s\n", gai_strerror (rv));
		exit (1);
	}

	for (p = ai; p != NULL; p = p->ai_next) {
		listener = socket (p->ai_family, p->ai_socktype, p->ai_protocol);
		if (listener < 0) {
			continue;
		}

		/* lose the pesky "address already in use" error message
		 */
		if (setsockopt (listener, SOL_SOCKET, SO_REUSEADDR,
				&yes, sizeof(int)) < 0) {
			syslog (LOG_ERR, "setsockopt() failed: %s\n", strerror (errno));
		}

		if (bind (listener, p->ai_addr, p->ai_addrlen) < 0) {
			syslog (LOG_ERR, "bind() failed: %s\n", strerror (errno));
			close (listener);
			continue;
		}

		break;
	}

	if (p == NULL) {
		syslog (LOG_ERR, "failed to bind\n");
		exit (2);
	}

	freeaddrinfo (ai);

	if (listen (listener, 10) == -1) {
		syslog (LOG_ERR, "listen() failed: %s", strerror(errno));
		exit (3);
	}

	return listener;
}

int main (int argc, char *argv[])
{
	int listener;

	openlog (NULL, LOG_CONS|LOG_PID, LOG_DAEMON);

	list_init (&msg_log_head);
	list_init (&config_chg_log_head);

	poll_handle = poll_create ();

	listener = create_server_sockect ();
	poll_dispatch_add (poll_handle, listener, POLLIN|POLLNVAL, NULL, server_accept_fn);

	poll_run (poll_handle);
	return -1;
}

