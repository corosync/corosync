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
#include <corosync/cfg.h>
#include "../../exec/crypto.h"
#include "common_test_agent.h"


typedef enum {
	MSG_OK,
	MSG_NODEID_ERR,
	MSG_PID_ERR,
	MSG_SEQ_ERR,
	MSG_SIZE_ERR,
	MSG_SHA1_ERR,
} msg_status_t;

typedef struct {
	uint32_t nodeid;
	pid_t   pid;
	unsigned char sha1[20];
	uint32_t seq;
	size_t size;
	unsigned char payload[0];
} msg_t;

#define LOG_STR_SIZE 256
typedef struct {
	char log[LOG_STR_SIZE];
	struct list_head list;
} log_entry_t;

static char big_and_buf[HOW_BIG_AND_BUF];
static int32_t record_config_events_g = 0;
static int32_t record_messages_g = 0;
static cpg_handle_t cpg_handle = 0;
static corosync_cfg_handle_t cfg_handle = 0;
static int32_t cpg_fd = -1;
static int32_t cfg_fd = -1;
static struct list_head config_chg_log_head;
static struct list_head msg_log_head;
static pid_t my_pid;
static uint32_t my_nodeid;
static int32_t my_seq;
static int32_t use_zcb = 0;
static int32_t my_msgs_to_send;
static int32_t total_stored_msgs = 0;
static int32_t in_cnchg = 0;
static int32_t pcmk_test = 0;

static void send_some_more_messages (void * unused);

static char* err_status_string (char * buf, size_t buf_len, msg_status_t status)
{
	switch (status) {
	case MSG_OK:
		strncpy (buf, "OK", buf_len);
		break;
	case MSG_NODEID_ERR:
		strncpy (buf, "NODEID_ERR", buf_len);
		break;
	case MSG_PID_ERR:
		strncpy (buf, "PID_ERR", buf_len);
		break;
	case MSG_SEQ_ERR:
		strncpy (buf, "SEQ_ERR", buf_len);
		break;
	case MSG_SIZE_ERR:
		strncpy (buf, "SIZE_ERR", buf_len);
		break;
	case MSG_SHA1_ERR:
		strncpy (buf, "SHA1_ERR", buf_len);
		break;
	default:
		strncpy (buf, "UNKNOWN_ERR", buf_len);
		break;
	}
	return buf;
}

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
	char status_buf[20];
	unsigned char sha1_compare[20];
	hash_state sha1_hash;

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
	sha1_init (&sha1_hash);
	sha1_process (&sha1_hash, msg_pt->payload, (msg_pt->size - sizeof (msg_t)));
	sha1_done (&sha1_hash, sha1_compare);
	if (memcmp (sha1_compare, msg_pt->sha1, 20) != 0) {
		syslog (LOG_ERR, "%s(); msg seq:%d; incorrect hash",
			__func__, msg_pt->seq);
		status = MSG_SHA1_ERR;
	}

	log_pt = malloc (sizeof(log_entry_t));
	list_init (&log_pt->list);

	snprintf (log_pt->log, LOG_STR_SIZE, "%d:%d:%d:%s;",
		msg_pt->nodeid, msg_pt->pid, msg_pt->seq,
		err_status_string (status_buf, 20, status));
	list_add_tail (&log_pt->list, &msg_log_head);
	total_stored_msgs++;

//	if ((total_stored_msgs % 100) == 0) {
//		syslog (LOG_INFO, "%s(); %d", __func__, total_stored_msgs);
//	}

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

	for (i = 0; i < left_list_entries; i++) {
		syslog (LOG_INFO, "%s(%d) %d:%s left", __func__, record_config_events_g,
			left_list[i].nodeid, groupName->value);

		if (record_config_events_g > 0) {
			log_pt = malloc (sizeof(log_entry_t));
			list_init (&log_pt->list);
			snprintf (log_pt->log, LOG_STR_SIZE, "%s,%d,%d,left",
				groupName->value, left_list[i].nodeid,left_list[i].pid);
			list_add_tail(&log_pt->list, &config_chg_log_head);
		}
	}
	for (i = 0; i < joined_list_entries; i++) {
		syslog (LOG_INFO, "%s(%d) %d:%s joined", __func__, record_config_events_g,
			left_list[i].nodeid, groupName->value);

		if (record_config_events_g > 0) {
			log_pt = malloc (sizeof(log_entry_t));
			list_init (&log_pt->list);
			snprintf (log_pt->log, LOG_STR_SIZE, "%s,%d,%d,join",
				groupName->value, joined_list[i].nodeid,joined_list[i].pid);
			list_add_tail (&log_pt->list, &config_chg_log_head);
		}
	}
	if (pcmk_test == 1) {
		in_cnchg = 1;
		send_some_more_messages (NULL);
		in_cnchg = 0;
	}
}

static void my_shutdown_callback (corosync_cfg_handle_t handle,
	corosync_cfg_shutdown_flags_t flags)
{
	syslog (LOG_CRIT, "%s flags:%d", __func__, flags);
	if (flags & COROSYNC_CFG_SHUTDOWN_FLAG_REQUEST) {
		corosync_cfg_replyto_shutdown (cfg_handle, COROSYNC_CFG_SHUTDOWN_FLAG_YES);
	}
}


static corosync_cfg_callbacks_t cfg_callbacks = {
	.corosync_cfg_shutdown_callback = my_shutdown_callback,
	.corosync_cfg_state_track_callback = NULL,
};
static cpg_callbacks_t callbacks = {
	.cpg_deliver_fn = delivery_callback,
	.cpg_confchg_fn = config_change_callback,
};

static void record_messages (void)
{
	record_messages_g = 1;
	syslog (LOG_DEBUG,"%s() record:%d", __func__, record_messages_g);
}

static void record_config_events (int sock)
{
	char response[100];

	record_config_events_g = 1;
	syslog (LOG_INFO, "%s() record:%d", __func__, record_config_events_g);

	snprintf (response, 100, "%s", OK_STR);
	send (sock, response, strlen (response), 0);
}

static void read_config_event (int sock)
{
	const char *empty = "None";
	struct list_head * list = config_chg_log_head.next;
	log_entry_t *entry;

	if (list != &config_chg_log_head) {
		entry = list_entry (list, log_entry_t, list);
		send (sock, entry->log,	strlen (entry->log), 0);
		list_del (&entry->list);
		free (entry);
	} else {
		syslog (LOG_DEBUG,"%s() no events in list", __func__);
		send (sock, empty, strlen (empty), 0);
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

static void send_some_more_messages_later (void)
{
	poll_timer_handle timer_handle;
	cpg_dispatch (cpg_handle, CS_DISPATCH_ALL);
	poll_timer_add (
		ta_poll_handle_get(),
		300, NULL,
		send_some_more_messages,
		&timer_handle);
}



static void send_some_more_messages_zcb (void)
{
	msg_t *my_msg;
	int i;
	int send_now;
	size_t payload_size;
	size_t total_size;
	hash_state sha1_hash;
	cs_error_t res;
	cpg_flow_control_state_t fc_state;
	void *zcb_buffer;

	if (cpg_fd < 0)
		return;

	send_now = my_msgs_to_send;
	payload_size = (rand() % 100000);
	total_size = payload_size + sizeof (msg_t);
	cpg_zcb_alloc (cpg_handle, total_size, &zcb_buffer);

	my_msg = (msg_t*)zcb_buffer;

	//syslog (LOG_DEBUG,"%s() send_now:%d", __func__, send_now);
	my_msg->pid = my_pid;
	my_msg->nodeid = my_nodeid;
	my_msg->size = sizeof (msg_t) + payload_size;
	my_msg->seq = 0;
	for (i = 0; i < payload_size; i++) {
		my_msg->payload[i] = i;
	}
	sha1_init (&sha1_hash);
	sha1_process (&sha1_hash, my_msg->payload, payload_size);
	sha1_done (&sha1_hash, my_msg->sha1);

	for (i = 0; i < send_now; i++) {

		res = cpg_flow_control_state_get (cpg_handle, &fc_state);
		if (res == CS_OK && fc_state == CPG_FLOW_CONTROL_ENABLED) {
			/* lets do this later */
			send_some_more_messages_later ();
			syslog (LOG_INFO, "%s() flow control enabled.", __func__);
			goto free_buffer;
		}

		res = cpg_zcb_mcast_joined (cpg_handle, CPG_TYPE_AGREED, zcb_buffer, total_size);
		if (res == CS_ERR_TRY_AGAIN) {
			/* lets do this later */
			send_some_more_messages_later ();
//			if (i > 0) {
//				syslog (LOG_INFO, "%s() TRY_AGAIN %d to send.",
//					__func__, my_msgs_to_send);
//			}
			goto free_buffer;
		} else if (res != CS_OK) {
			syslog (LOG_ERR, "%s() -> cpg_mcast_joined error:%d, exiting.",
				__func__, res);
			exit (-2);
		}

		my_msgs_to_send--;
	}
free_buffer:
	cpg_zcb_free (cpg_handle, zcb_buffer);
}


#define cs_repeat(counter, max, code) do {		\
	code;						\
	if(res == CS_ERR_TRY_AGAIN) {			\
	    counter++;					\
	    sleep(counter);				\
	}						\
    } while(res == CS_ERR_TRY_AGAIN && counter < max)

static unsigned char buffer[200000];
static void send_some_more_messages_normal (void)
{
	msg_t my_msg;
	struct iovec iov[2];
	int i;
	int send_now;
	size_t payload_size;
	hash_state sha1_hash;
	cs_error_t res;
	cpg_flow_control_state_t fc_state;
	int retries = 0;
	time_t before;

	if (cpg_fd < 0)
		return;

	send_now = my_msgs_to_send;

	//syslog (LOG_DEBUG,"%s() send_now:%d", __func__, send_now);
	my_msg.pid = my_pid;
	my_msg.nodeid = my_nodeid;
	payload_size = (rand() % 100000);
	my_msg.size = sizeof (msg_t) + payload_size;
	my_msg.seq = 0;
	for (i = 0; i < payload_size; i++) {
		buffer[i] = i;
	}
	sha1_init (&sha1_hash);
	sha1_process (&sha1_hash, buffer, payload_size);
	sha1_done (&sha1_hash, my_msg.sha1);

	iov[0].iov_len = sizeof (msg_t);
	iov[0].iov_base = &my_msg;
	iov[1].iov_len = payload_size;
	iov[1].iov_base = buffer;

	for (i = 0; i < send_now; i++) {
		if (in_cnchg && pcmk_test) {
			retries = 0;
			before = time(NULL);
			cs_repeat(retries, 30, res = cpg_mcast_joined(cpg_handle, CPG_TYPE_AGREED, iov, 1));
			if (retries > 20) {
				syslog (LOG_ERR, "%s() -> cs_repeat: blocked for :%lu secs.",
					__func__, (unsigned long)(time(NULL) - before));
			}
			if (res != CS_OK) {
				syslog (LOG_ERR, "%s() -> cpg_mcast_joined error:%d.",
					__func__, res);
				return;
			}
		} else {
			res = cpg_flow_control_state_get (cpg_handle, &fc_state);
			if (res == CS_OK && fc_state == CPG_FLOW_CONTROL_ENABLED) {
				/* lets do this later */
				send_some_more_messages_later ();
				syslog (LOG_INFO, "%s() flow control enabled.", __func__);
				return;
			}

			res = cpg_mcast_joined (cpg_handle, CPG_TYPE_AGREED, iov, 2);
			if (res == CS_ERR_TRY_AGAIN) {
				/* lets do this later */
				send_some_more_messages_later ();
				if (i > 0) {
					syslog (LOG_INFO, "%s() TRY_AGAIN %d to send.",
						__func__, my_msgs_to_send);
				}
				return;
			} else if (res != CS_OK) {
				syslog (LOG_ERR, "%s() -> cpg_mcast_joined error:%d, exiting.",
					__func__, res);
				exit (-2);
			}
		}
		my_msgs_to_send--;
	}
}

static void send_some_more_messages (void * unused)
{
	if (use_zcb) {
		send_some_more_messages_zcb ();
	} else {
		send_some_more_messages_normal ();
	}
}

static void msg_blaster (int sock, char* num_to_send_str)
{
	my_msgs_to_send = atoi (num_to_send_str);
	my_seq = 1;
	my_pid = getpid();

	use_zcb = 0;
	total_stored_msgs = 0;

	cpg_local_get (cpg_handle, &my_nodeid);

	/* control the limits */
	if (my_msgs_to_send <= 0)
		my_msgs_to_send = 1;
	if (my_msgs_to_send > 10000)
		my_msgs_to_send = 10000;

	send_some_more_messages_normal ();
}


static void context_test (int sock)
{
	char response[100];
	char *cmp;

	cpg_context_set (cpg_handle, response);
	cpg_context_get (cpg_handle, (void**)&cmp);
	if (response != cmp) {
		snprintf (response, 100, "%s", FAIL_STR);
	}
	else {
		snprintf (response, 100, "%s", OK_STR);
	}
	send (sock, response, strlen (response), 0);
}

static void msg_blaster_zcb (int sock, char* num_to_send_str)
{
	my_msgs_to_send = atoi (num_to_send_str);
	my_seq = 1;
	my_pid = getpid();

	use_zcb = 1;
	total_stored_msgs = 0;

	cpg_local_get (cpg_handle, &my_nodeid);

	/* control the limits */
	if (my_msgs_to_send <= 0)
		my_msgs_to_send = 1;
	if (my_msgs_to_send > 10000)
		my_msgs_to_send = 10000;

	send_some_more_messages_zcb ();
}

static corosync_cfg_state_notification_t notification_buffer;

static int cfg_dispatch_wrapper_fn (hdb_handle_t handle,
	int fd,
	int revents,
	void *data)
{
	cs_error_t error;
	if (revents & POLLHUP || revents & POLLERR) {
		syslog (LOG_ERR, "%s() got POLLHUP disconnecting from CFG", __func__);
		poll_dispatch_delete (ta_poll_handle_get(), cfg_fd);
		close (cfg_fd);
		cfg_fd = -1;
		return -1;
	}
	error = corosync_cfg_dispatch (cfg_handle, CS_DISPATCH_ALL);
	if (error == CS_ERR_LIBRARY) {
		syslog (LOG_ERR, "%s() got LIB error disconnecting from CFG.", __func__);
		poll_dispatch_delete (ta_poll_handle_get(), cfg_fd);
		close (cfg_fd);
		cfg_fd = -1;
		return -1;
	}
	return 0;
}

static int cpg_dispatch_wrapper_fn (hdb_handle_t handle,
	int fd,
	int revents,
	void *data)
{
	cs_error_t error;
	if (revents & POLLHUP || revents & POLLERR) {
		syslog (LOG_ERR, "%s() got POLLHUP disconnecting from CPG", __func__);
		poll_dispatch_delete (ta_poll_handle_get(), cpg_fd);
		close (cpg_fd);
		cpg_fd = -1;
		return -1;
	}
	error = cpg_dispatch (cpg_handle, CS_DISPATCH_ALL);
	if (error == CS_ERR_LIBRARY) {
		syslog (LOG_ERR, "%s() got LIB error disconnecting from CPG", __func__);
		poll_dispatch_delete (ta_poll_handle_get(), cpg_fd);
		close (cpg_fd);
		cpg_fd = -1;
		return -1;
	}
	return 0;
}

static void do_command (int sock, char* func, char*args[], int num_args)
{
	int result;
	char response[100];
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
		syslog (LOG_INFO, "called cpg_join()!");

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
			result = cpg_initialize (&cpg_handle, &callbacks);
		}

		cpg_fd_get (cpg_handle, &cpg_fd);
		poll_dispatch_add (ta_poll_handle_get(), cpg_fd, POLLIN|POLLNVAL, NULL, cpg_dispatch_wrapper_fn);

	} else if (strcmp ("cpg_local_get", func) == 0) {
		unsigned int local_nodeid;

		cpg_local_get (cpg_handle, &local_nodeid);
		snprintf (response, 100, "%u",local_nodeid);
		send (sock, response, strlen (response), 0);
	} else if (strcmp ("cpg_finalize", func) == 0) {

		cpg_finalize (cpg_handle);
		poll_dispatch_delete (ta_poll_handle_get(), cpg_fd);
		cpg_fd = -1;

	} else if (strcmp ("record_config_events", func) == 0) {
		record_config_events (sock);
	} else if (strcmp ("record_messages", func) == 0) {
		record_messages ();
	} else if (strcmp ("read_config_event", func) == 0) {
		read_config_event (sock);
	} else if (strcmp ("read_messages", func) == 0) {
		read_messages (sock, args[0]);
	} else if (strcmp ("msg_blaster_zcb", func) == 0) {
		msg_blaster_zcb (sock, args[0]);
	} else if (strcmp ("pcmk_test", func) == 0) {
		pcmk_test = 1;
	} else if (strcmp ("msg_blaster", func) == 0) {
		msg_blaster (sock, args[0]);
	} else if (strcmp ("context_test", func) == 0) {
		context_test (sock);
	} else if (strcmp ("are_you_ok_dude", func) == 0) {
		snprintf (response, 100, "%s", OK_STR);
		send (sock, response, strlen (response), 0);

	} else if (strcmp ("cfg_shutdown", func) == 0) {

		corosync_cfg_try_shutdown (cfg_handle, COROSYNC_CFG_SHUTDOWN_FLAG_REQUEST);

	} else if (strcmp ("cfg_initialize",func) == 0) {
		int retry_count = 0;

		syslog (LOG_INFO,"%s %s() called!", __func__, func);
		result = corosync_cfg_initialize (&cfg_handle, &cfg_callbacks);
		while (result != CS_OK) {
			syslog (LOG_ERR,
				"cfg_initialize error %d (attempt %d)\n",
				result, retry_count);
			if (retry_count >= 3) {
				exit (1);
			}
			sleep(1);
			retry_count++;
			result = corosync_cfg_initialize (&cfg_handle, &cfg_callbacks);
		}

		corosync_cfg_fd_get (cfg_handle, &cfg_fd);

		corosync_cfg_state_track (cfg_handle, 0, &notification_buffer);

		poll_dispatch_add (ta_poll_handle_get(), cfg_fd, POLLIN|POLLNVAL, NULL, cfg_dispatch_wrapper_fn);
	} else {
		syslog (LOG_ERR,"%s RPC:%s not supported!", __func__, func);
	}
}


int main (int argc, char *argv[])
{
	openlog (NULL, LOG_CONS|LOG_PID, LOG_DAEMON);

	list_init (&msg_log_head);
	list_init (&config_chg_log_head);

	return test_agent_run (9034, do_command);
}

