/*
 * Copyright (c) 2010-2012 Red Hat, Inc.
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
 * - Neither the name of Red Hat, Inc. nor the names of its
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

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <sys/uio.h>
#include <string.h>

#include <qb/qbdefs.h>
#include <qb/qblist.h>
#include <qb/qbutil.h>
#include <qb/qbloop.h>
#include <qb/qbipcs.h>

#include <corosync/swab.h>
#include <corosync/corotypes.h>
#include <corosync/corodefs.h>
#include <corosync/totem/totempg.h>
#include <corosync/logsys.h>
#include <corosync/icmap.h>

#include "sync.h"
#include "timer.h"
#include "main.h"
#include "util.h"
#include "apidef.h"
#include "service.h"

LOGSYS_DECLARE_SUBSYS ("MAIN");

static struct corosync_api_v1 *api = NULL;
static int32_t ipc_not_enough_fds_left = 0;
static int32_t ipc_fc_is_quorate; /* boolean */
static int32_t ipc_fc_totem_queue_level; /* percentage used */
static int32_t ipc_fc_sync_in_process; /* boolean */
static int32_t ipc_allow_connections = 0; /* boolean */

#define CS_IPCS_MAPPER_SERV_NAME		256

struct cs_ipcs_mapper {
	int32_t id;
	qb_ipcs_service_t *inst;
	char name[CS_IPCS_MAPPER_SERV_NAME];
};

struct outq_item {
	void *msg;
	size_t mlen;
	struct list_head list;
};

static struct cs_ipcs_mapper ipcs_mapper[SERVICES_COUNT_MAX];

static int32_t cs_ipcs_job_add(enum qb_loop_priority p,	void *data, qb_loop_job_dispatch_fn fn);
static int32_t cs_ipcs_dispatch_add(enum qb_loop_priority p, int32_t fd, int32_t events,
	void *data, qb_ipcs_dispatch_fn_t fn);
static int32_t cs_ipcs_dispatch_mod(enum qb_loop_priority p, int32_t fd, int32_t events,
	void *data, qb_ipcs_dispatch_fn_t fn);
static int32_t cs_ipcs_dispatch_del(int32_t fd);
static void outq_flush (void *data);


static struct qb_ipcs_poll_handlers corosync_poll_funcs = {
	.job_add = cs_ipcs_job_add,
	.dispatch_add = cs_ipcs_dispatch_add,
	.dispatch_mod = cs_ipcs_dispatch_mod,
	.dispatch_del = cs_ipcs_dispatch_del,
};

static int32_t cs_ipcs_connection_accept (qb_ipcs_connection_t *c, uid_t euid, gid_t egid);
static void cs_ipcs_connection_created(qb_ipcs_connection_t *c);
static int32_t cs_ipcs_msg_process(qb_ipcs_connection_t *c,
		void *data, size_t size);
static int32_t cs_ipcs_connection_closed (qb_ipcs_connection_t *c);
static void cs_ipcs_connection_destroyed (qb_ipcs_connection_t *c);

static struct qb_ipcs_service_handlers corosync_service_funcs = {
	.connection_accept	= cs_ipcs_connection_accept,
	.connection_created	= cs_ipcs_connection_created,
	.msg_process		= cs_ipcs_msg_process,
	.connection_closed	= cs_ipcs_connection_closed,
	.connection_destroyed	= cs_ipcs_connection_destroyed,
};

static const char* cs_ipcs_serv_short_name(int32_t service_id)
{
	const char *name;
	switch (service_id) {
	case CFG_SERVICE:
		name = "cfg";
		break;
	case CPG_SERVICE:
		name = "cpg";
		break;
	case QUORUM_SERVICE:
		name = "quorum";
		break;
	case PLOAD_SERVICE:
		name = "pload";
		break;
	case VOTEQUORUM_SERVICE:
		name = "votequorum";
		break;
	case MON_SERVICE:
		name = "mon";
		break;
	case WD_SERVICE:
		name = "wd";
		break;
	case CMAP_SERVICE:
		name = "cmap";
		break;
	default:
		name = NULL;
		break;
	}
	return name;
}

void cs_ipc_allow_connections(int32_t allow)
{
	ipc_allow_connections = allow;
}

int32_t cs_ipcs_service_destroy(int32_t service_id)
{
	if (ipcs_mapper[service_id].inst) {
		qb_ipcs_destroy(ipcs_mapper[service_id].inst);
		ipcs_mapper[service_id].inst = NULL;
	}
	return 0;
}

static int32_t cs_ipcs_connection_accept (qb_ipcs_connection_t *c, uid_t euid, gid_t egid)
{
	int32_t service = qb_ipcs_service_id_get(c);
	uint8_t u8;
	char key_name[ICMAP_KEYNAME_MAXLEN];

	if (!ipc_allow_connections) {
		log_printf(LOGSYS_LEVEL_DEBUG, "Denied connection, corosync is not ready");
		return -EAGAIN;
	}

	if (corosync_service[service] == NULL ||
		ipcs_mapper[service].inst == NULL) {
		return -ENOSYS;
	}

	if (ipc_not_enough_fds_left) {
		return -EMFILE;
	}

	if (euid == 0 || egid == 0) {
		return 0;
	}

	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "uidgid.uid.%u", euid);
	if (icmap_get_uint8(key_name, &u8) == CS_OK && u8 == 1)
		return 0;

	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "uidgid.config.uid.%u", euid);
	if (icmap_get_uint8(key_name, &u8) == CS_OK && u8 == 1)
		return 0;

	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "uidgid.gid.%u", egid);
	if (icmap_get_uint8(key_name, &u8) == CS_OK && u8 == 1)
		return 0;

	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "uidgid.config.gid.%u", egid);
	if (icmap_get_uint8(key_name, &u8) == CS_OK && u8 == 1)
		return 0;

	log_printf(LOGSYS_LEVEL_ERROR, "Denied connection attempt from %d:%d", euid, egid);

	return -EACCES;
}

static char * pid_to_name (pid_t pid, char *out_name, size_t name_len)
{
	char *name;
	char *rest;
	FILE *fp;
	char fname[32];
	char buf[256];

	snprintf (fname, 32, "/proc/%d/stat", pid);
	fp = fopen (fname, "r");
	if (!fp) {
		return NULL;
	}

	if (fgets (buf, sizeof (buf), fp) == NULL) {
		fclose (fp);
		return NULL;
	}
	fclose (fp);

	name = strrchr (buf, '(');
	if (!name) {
		return NULL;
	}

	/* move past the bracket */
	name++;

	rest = strrchr (buf, ')');

	if (rest == NULL || rest[1] != ' ') {
		return NULL;
	}

	*rest = '\0';
	/* move past the NULL and space */
	rest += 2;

	/* copy the name */
	strncpy (out_name, name, name_len);
	out_name[name_len - 1] = '\0';
	return out_name;
}

struct cs_ipcs_conn_context {
	char *icmap_path;
	struct list_head outq_head;
	int32_t queuing;
	uint32_t queued;
	uint64_t invalid_request;
	uint64_t overload;
	uint32_t sent;
	char data[1];
};

static void cs_ipcs_connection_created(qb_ipcs_connection_t *c)
{
	int32_t service = 0;
	struct cs_ipcs_conn_context *context;
	char proc_name[32];
	struct qb_ipcs_connection_stats stats;
	int32_t size = sizeof(struct cs_ipcs_conn_context);
	char key_name[ICMAP_KEYNAME_MAXLEN];
	int set_client_pid = 0;
	int set_proc_name = 0;

	log_printf(LOG_DEBUG, "connection created");

	service = qb_ipcs_service_id_get(c);

	size += corosync_service[service]->private_data_size;
	context = calloc(1, size);
	if (context == NULL) {
		qb_ipcs_disconnect(c);
		return;
	}

	list_init(&context->outq_head);
	context->queuing = QB_FALSE;
	context->queued = 0;
	context->sent = 0;

	qb_ipcs_context_set(c, context);

	if (corosync_service[service]->lib_init_fn(c) != 0) {
		log_printf(LOG_ERR, "lib_init_fn failed, disconnecting");
		qb_ipcs_disconnect(c);
		return;
	}
	icmap_inc("runtime.connections.active");

	qb_ipcs_connection_stats_get(c, &stats, QB_FALSE);

	if (stats.client_pid > 0) {
		if (pid_to_name (stats.client_pid, proc_name, sizeof(proc_name))) {
			snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "runtime.connections.%s:%u:%p",
					proc_name, stats.client_pid, c);
			set_proc_name = 1;
		} else {
			snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "runtime.connections.%u:%p",
					stats.client_pid, c);
		}
		set_client_pid = 1;
	} else {
		snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "runtime.connections.%p", c);
	}

	icmap_convert_name_to_valid_name(key_name);

	context->icmap_path = strdup(key_name);
	if (context->icmap_path == NULL) {
		qb_ipcs_disconnect(c);
		return;
	}

	if (set_proc_name) {
		snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s.name", context->icmap_path);
		icmap_set_string(key_name, proc_name);
	}

	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s.client_pid", context->icmap_path);
	if (set_client_pid) {
		icmap_set_uint32(key_name, stats.client_pid);
	} else {
		icmap_set_uint32(key_name, 0);
	}

	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s.service_id", context->icmap_path);
	icmap_set_uint32(key_name, service);

	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s.responses", context->icmap_path);
	icmap_set_uint64(key_name, 0);

	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s.dispatched", context->icmap_path);
	icmap_set_uint64(key_name, 0);

	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s.requests", context->icmap_path);
	icmap_set_uint64(key_name, 0);

	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s.send_retries", context->icmap_path);
	icmap_set_uint64(key_name, 0);

	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s.recv_retries", context->icmap_path);
	icmap_set_uint64(key_name, 0);

	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s.flow_control", context->icmap_path);
	icmap_set_uint32(key_name, 0);

	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s.flow_control_count", context->icmap_path);
	icmap_set_uint64(key_name, 0);

	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s.queue_size", context->icmap_path);
	icmap_set_uint32(key_name, 0);

	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s.invalid_request", context->icmap_path);
	icmap_set_uint64(key_name, 0);

	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s.overload", context->icmap_path);
	icmap_set_uint64(key_name, 0);
}

void cs_ipc_refcnt_inc(void *conn)
{
	qb_ipcs_connection_ref(conn);
}

void cs_ipc_refcnt_dec(void *conn)
{
	qb_ipcs_connection_unref(conn);
}

void *cs_ipcs_private_data_get(void *conn)
{
	struct cs_ipcs_conn_context *cnx;
	cnx = qb_ipcs_context_get(conn);
	return &cnx->data[0];
}

static void cs_ipcs_connection_destroyed (qb_ipcs_connection_t *c)
{
	struct cs_ipcs_conn_context *context;
	struct list_head *list, *list_next;
	struct outq_item *outq_item;

	log_printf(LOG_DEBUG, "%s() ", __func__);

	context = qb_ipcs_context_get(c);
	if (context) {
		for (list = context->outq_head.next;
			list != &context->outq_head; list = list_next) {

			list_next = list->next;
			outq_item = list_entry (list, struct outq_item, list);

			list_del (list);
			free (outq_item->msg);
			free (outq_item);
		}
		free(context);
	}
}

static int32_t cs_ipcs_connection_closed (qb_ipcs_connection_t *c)
{
	int32_t res = 0;
	int32_t service = qb_ipcs_service_id_get(c);
	icmap_iter_t iter;
	char prefix[ICMAP_KEYNAME_MAXLEN];
	const char *key_name;
	struct cs_ipcs_conn_context *cnx;

	log_printf(LOG_DEBUG, "%s() ", __func__);
	res = corosync_service[service]->lib_exit_fn(c);
	if (res != 0) {
		return res;
	}

	qb_loop_job_del(cs_poll_handle_get(), QB_LOOP_HIGH, c, outq_flush);

	cnx = qb_ipcs_context_get(c);

	snprintf(prefix, ICMAP_KEYNAME_MAXLEN, "%s.", cnx->icmap_path);
	iter = icmap_iter_init(prefix);
	while ((key_name = icmap_iter_next(iter, NULL, NULL)) != NULL) {
		icmap_delete(key_name);
	}
	icmap_iter_finalize(iter);
	free(cnx->icmap_path);

	icmap_inc("runtime.connections.closed");
	icmap_dec("runtime.connections.active");

	return 0;
}

int cs_ipcs_response_iov_send (void *conn,
	const struct iovec *iov,
	unsigned int iov_len)
{
	int32_t rc = qb_ipcs_response_sendv(conn, iov, iov_len);
	if (rc >= 0) {
		return 0;
	}
	return rc;
}

int cs_ipcs_response_send(void *conn, const void *msg, size_t mlen)
{
	int32_t rc = qb_ipcs_response_send(conn, msg, mlen);
	if (rc >= 0) {
		return 0;
	}
	return rc;
}

static void outq_flush (void *data)
{
	qb_ipcs_connection_t *conn = data;
	struct list_head *list, *list_next;
	struct outq_item *outq_item;
	int32_t rc;
	struct cs_ipcs_conn_context *context = qb_ipcs_context_get(conn);

	for (list = context->outq_head.next;
		list != &context->outq_head; list = list_next) {

		list_next = list->next;
		outq_item = list_entry (list, struct outq_item, list);

		rc = qb_ipcs_event_send(conn, outq_item->msg, outq_item->mlen);
		if (rc < 0 && rc != -EAGAIN) {
			errno = -rc;
			qb_perror(LOG_ERR, "qb_ipcs_event_send");
			return;
		} else if (rc == -EAGAIN) {
			break;
		}
		assert(rc == outq_item->mlen);
		context->sent++;
		context->queued--;

		list_del (list);
		free (outq_item->msg);
		free (outq_item);
	}
	if (list_empty (&context->outq_head)) {
		context->queuing = QB_FALSE;
		log_printf(LOGSYS_LEVEL_INFO, "Q empty, queued:%d sent:%d.",
			context->queued, context->sent);
		context->queued = 0;
		context->sent = 0;
	} else {
		qb_loop_job_add(cs_poll_handle_get(), QB_LOOP_HIGH, conn, outq_flush);
	}
}

static void msg_send_or_queue(qb_ipcs_connection_t *conn, const struct iovec *iov, uint32_t iov_len)
{
	int32_t rc = 0;
	int32_t i;
	int32_t bytes_msg = 0;
	struct outq_item *outq_item;
	char *write_buf = 0;
	struct cs_ipcs_conn_context *context = qb_ipcs_context_get(conn);

	for (i = 0; i < iov_len; i++) {
		bytes_msg += iov[i].iov_len;
	}

	if (!context->queuing) {
		assert(list_empty (&context->outq_head));
		rc = qb_ipcs_event_sendv(conn, iov, iov_len);
		if (rc == bytes_msg) {
			context->sent++;
			return;
		}
		if (rc == -EAGAIN) {
			context->queued = 0;
			context->sent = 0;
			context->queuing = QB_TRUE;
			qb_loop_job_add(cs_poll_handle_get(), QB_LOOP_HIGH, conn, outq_flush);
		} else {
			log_printf(LOGSYS_LEVEL_ERROR, "event_send retuned %d, expected %d!", rc, bytes_msg);
			return;
		}
	}
	outq_item = malloc (sizeof (struct outq_item));
	if (outq_item == NULL) {
		qb_ipcs_disconnect(conn);
		return;
	}
	outq_item->msg = malloc (bytes_msg);
	if (outq_item->msg == NULL) {
		free (outq_item);
		qb_ipcs_disconnect(conn);
		return;
	}

	write_buf = outq_item->msg;
	for (i = 0; i < iov_len; i++) {
		memcpy (write_buf, iov[i].iov_base, iov[i].iov_len);
		write_buf += iov[i].iov_len;
	}
	outq_item->mlen = bytes_msg;
	list_init (&outq_item->list);
	list_add_tail (&outq_item->list, &context->outq_head);
	context->queued++;
}

int cs_ipcs_dispatch_send(void *conn, const void *msg, size_t mlen)
{
	struct iovec iov;
	iov.iov_base = (void *)msg;
	iov.iov_len = mlen;
	msg_send_or_queue (conn, &iov, 1);
	return 0;
}

int cs_ipcs_dispatch_iov_send (void *conn,
	const struct iovec *iov,
	unsigned int iov_len)
{
	msg_send_or_queue(conn, iov, iov_len);
	return 0;
}

static int32_t cs_ipcs_msg_process(qb_ipcs_connection_t *c,
		void *data, size_t size)
{
	struct qb_ipc_response_header response;
	struct qb_ipc_request_header *request_pt = (struct qb_ipc_request_header *)data;
	int32_t service = qb_ipcs_service_id_get(c);
	int32_t send_ok = 0;
	int32_t is_async_call = QB_FALSE;
	ssize_t res = -1;
	int sending_allowed_private_data;
	struct cs_ipcs_conn_context *cnx;

	send_ok = corosync_sending_allowed (service,
			request_pt->id,
			request_pt,
			&sending_allowed_private_data);

	is_async_call = (service == CPG_SERVICE && request_pt->id == 2);

	/*
	 * This happens when the message contains some kind of invalid
	 * parameter, such as an invalid size
	 */
	if (send_ok == -EINVAL) {
		response.size = sizeof (response);
		response.id = 0;
		response.error = CS_ERR_INVALID_PARAM;

		cnx = qb_ipcs_context_get(c);
		if (cnx) {
			cnx->invalid_request++;
		}

		if (is_async_call) {
			log_printf(LOGSYS_LEVEL_INFO, "*** %s() invalid message! size:%d error:%d",
				__func__, response.size, response.error);
		} else {
			qb_ipcs_response_send (c,
				&response,
				sizeof (response));
		}
		res = -EINVAL;
	} else if (send_ok < 0) {
		cnx = qb_ipcs_context_get(c);
		if (cnx) {
			cnx->overload++;
		}
		if (!is_async_call) {
			/*
			 * Overload, tell library to retry
			 */
			response.size = sizeof (response);
			response.id = 0;
			response.error = CS_ERR_TRY_AGAIN;
			qb_ipcs_response_send (c,
				&response,
				sizeof (response));
		} else {
			log_printf(LOGSYS_LEVEL_WARNING,
				"*** %s() (%d:%d - %d) %s!",
				__func__, service, request_pt->id,
				is_async_call, strerror(-send_ok));
		}
		res = -ENOBUFS;
	}

	if (send_ok >= 0) {
		corosync_service[service]->lib_engine[request_pt->id].lib_handler_fn(c, request_pt);
		res = 0;
	}
	corosync_sending_allowed_release (&sending_allowed_private_data);
	return res;
}


static int32_t cs_ipcs_job_add(enum qb_loop_priority p,	void *data, qb_loop_job_dispatch_fn fn)
{
	return qb_loop_job_add(cs_poll_handle_get(), p, data, fn);
}

static int32_t cs_ipcs_dispatch_add(enum qb_loop_priority p, int32_t fd, int32_t events,
	void *data, qb_ipcs_dispatch_fn_t fn)
{
	return qb_loop_poll_add(cs_poll_handle_get(), p, fd, events, data, fn);
}

static int32_t cs_ipcs_dispatch_mod(enum qb_loop_priority p, int32_t fd, int32_t events,
	void *data, qb_ipcs_dispatch_fn_t fn)
{
	return qb_loop_poll_mod(cs_poll_handle_get(), p, fd, events, data, fn);
}

static int32_t cs_ipcs_dispatch_del(int32_t fd)
{
	return qb_loop_poll_del(cs_poll_handle_get(), fd);
}

static void cs_ipcs_low_fds_event(int32_t not_enough, int32_t fds_available)
{
	ipc_not_enough_fds_left = not_enough;
	if (not_enough) {
		log_printf(LOGSYS_LEVEL_WARNING, "refusing new connections (fds_available:%d)",
			fds_available);
	} else {
		log_printf(LOGSYS_LEVEL_NOTICE, "allowing new connections (fds_available:%d)",
			fds_available);

	}
}

int32_t cs_ipcs_q_level_get(void)
{
	return ipc_fc_totem_queue_level;
}

static qb_loop_timer_handle ipcs_check_for_flow_control_timer;
static void cs_ipcs_check_for_flow_control(void)
{
	int32_t i;
	int32_t fc_enabled;

	for (i = 0; i < SERVICES_COUNT_MAX; i++) {
		if (corosync_service[i] == NULL || ipcs_mapper[i].inst == NULL) {
			continue;
		}
		fc_enabled = QB_IPCS_RATE_OFF;
		if (ipc_fc_is_quorate == 1 ||
			corosync_service[i]->allow_inquorate == CS_LIB_ALLOW_INQUORATE) {
			/*
			 * we are quorate
			 * now check flow control
			 */
			if (ipc_fc_totem_queue_level != TOTEM_Q_LEVEL_CRITICAL &&
			    ipc_fc_sync_in_process == 0) {
				fc_enabled = QB_FALSE;
			} else if (ipc_fc_totem_queue_level != TOTEM_Q_LEVEL_CRITICAL &&
			    i == VOTEQUORUM_SERVICE) {
				/*
				 * Allow message processing for votequorum service even
				 * in sync phase
				 */
				fc_enabled = QB_FALSE;
			} else {
				fc_enabled = QB_IPCS_RATE_OFF_2;
			}
		}
		if (fc_enabled) {
			qb_ipcs_request_rate_limit(ipcs_mapper[i].inst, fc_enabled);

			qb_loop_timer_add(cs_poll_handle_get(), QB_LOOP_MED, 1*QB_TIME_NS_IN_MSEC,
			       NULL, corosync_recheck_the_q_level, &ipcs_check_for_flow_control_timer);
		} else if (ipc_fc_totem_queue_level == TOTEM_Q_LEVEL_LOW) {
			qb_ipcs_request_rate_limit(ipcs_mapper[i].inst, QB_IPCS_RATE_FAST);
		} else if (ipc_fc_totem_queue_level == TOTEM_Q_LEVEL_GOOD) {
			qb_ipcs_request_rate_limit(ipcs_mapper[i].inst, QB_IPCS_RATE_NORMAL);
		} else if (ipc_fc_totem_queue_level == TOTEM_Q_LEVEL_HIGH) {
			qb_ipcs_request_rate_limit(ipcs_mapper[i].inst, QB_IPCS_RATE_SLOW);
		}
	}
}

static void cs_ipcs_fc_quorum_changed(int quorate, void *context)
{
	ipc_fc_is_quorate = quorate;
	cs_ipcs_check_for_flow_control();
}

static void cs_ipcs_totem_queue_level_changed(enum totem_q_level level)
{
	ipc_fc_totem_queue_level = level;
	cs_ipcs_check_for_flow_control();
}

void cs_ipcs_sync_state_changed(int32_t sync_in_process)
{
	ipc_fc_sync_in_process = sync_in_process;
	cs_ipcs_check_for_flow_control();
}

void cs_ipcs_stats_update(void)
{
	int32_t i;
	struct qb_ipcs_stats srv_stats;
	struct qb_ipcs_connection_stats stats;
	qb_ipcs_connection_t *c, *prev;
	struct cs_ipcs_conn_context *cnx;
	char key_name[ICMAP_KEYNAME_MAXLEN];

	for (i = 0; i < SERVICES_COUNT_MAX; i++) {
		if (corosync_service[i] == NULL || ipcs_mapper[i].inst == NULL) {
			continue;
		}
		qb_ipcs_stats_get(ipcs_mapper[i].inst, &srv_stats, QB_FALSE);

		for (c = qb_ipcs_connection_first_get(ipcs_mapper[i].inst);
			 c;
			 prev = c, c = qb_ipcs_connection_next_get(ipcs_mapper[i].inst, prev), qb_ipcs_connection_unref(prev)) {

			cnx = qb_ipcs_context_get(c);
			if (cnx == NULL) continue;

			qb_ipcs_connection_stats_get(c, &stats, QB_FALSE);

			snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s.client_pid", cnx->icmap_path);
			icmap_set_uint32(key_name, stats.client_pid);

			snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s.requests", cnx->icmap_path);
			icmap_set_uint64(key_name, stats.requests);

			snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s.responses", cnx->icmap_path);
			icmap_set_uint64(key_name, stats.responses);

			snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s.dispatched", cnx->icmap_path);
			icmap_set_uint64(key_name, stats.events);

			snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s.send_retries", cnx->icmap_path);
			icmap_set_uint64(key_name, stats.send_retries);

			snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s.recv_retries", cnx->icmap_path);
			icmap_set_uint64(key_name, stats.recv_retries);

			snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s.flow_control", cnx->icmap_path);
			icmap_set_uint32(key_name, stats.flow_control_state);

			snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s.flow_control_count", cnx->icmap_path);
			icmap_set_uint64(key_name, stats.flow_control_count);

			snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s.queue_size", cnx->icmap_path);
			icmap_set_uint32(key_name, cnx->queued);

			snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s.invalid_request", cnx->icmap_path);
			icmap_set_uint64(key_name, cnx->invalid_request);

			snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s.overload", cnx->icmap_path);
			icmap_set_uint64(key_name, cnx->overload);
		}
	}
}

static enum qb_ipc_type cs_get_ipc_type (void)
{
	char *str;
	int found = 0;
	enum qb_ipc_type ret = QB_IPC_NATIVE;

	if (icmap_get_string("qb.ipc_type", &str) != CS_OK) {
		log_printf(LOGSYS_LEVEL_DEBUG, "No configured qb.ipc_type. Using native ipc");
		return QB_IPC_NATIVE;
	}

	if (strcmp(str, "native") == 0) {
		ret = QB_IPC_NATIVE;
		found = 1;
	}

	if (strcmp(str, "shm") == 0) {
		ret = QB_IPC_SHM;
		found = 1;
	}

	if (strcmp(str, "socket") == 0) {
		ret = QB_IPC_SOCKET;
		found = 1;
	}

	if (found) {
		log_printf(LOGSYS_LEVEL_DEBUG, "Using %s ipc", str);
	} else {
		log_printf(LOGSYS_LEVEL_DEBUG, "Unknown ipc type %s", str);
	}

	free(str);

	return ret;
}

const char *cs_ipcs_service_init(struct corosync_service_engine *service)
{
	const char *serv_short_name;

	serv_short_name = cs_ipcs_serv_short_name(service->id);

	if (service->lib_engine_count == 0) {
		log_printf (LOGSYS_LEVEL_DEBUG,
			"NOT Initializing IPC on %s [%d]",
			serv_short_name,
			service->id);
		return NULL;
	}

	if (strlen(serv_short_name) >= CS_IPCS_MAPPER_SERV_NAME) {
		log_printf (LOGSYS_LEVEL_ERROR, "service name %s is too long", serv_short_name);
		return "qb_ipcs_run error";
	}

	ipcs_mapper[service->id].id = service->id;
	strcpy(ipcs_mapper[service->id].name, serv_short_name);
	log_printf (LOGSYS_LEVEL_DEBUG,
		"Initializing IPC on %s [%d]",
		ipcs_mapper[service->id].name,
		ipcs_mapper[service->id].id);
	ipcs_mapper[service->id].inst = qb_ipcs_create(ipcs_mapper[service->id].name,
		ipcs_mapper[service->id].id,
		cs_get_ipc_type(),
		&corosync_service_funcs);
	assert(ipcs_mapper[service->id].inst);
	qb_ipcs_poll_handlers_set(ipcs_mapper[service->id].inst,
		&corosync_poll_funcs);
	if (qb_ipcs_run(ipcs_mapper[service->id].inst) != 0) {
		log_printf (LOGSYS_LEVEL_ERROR, "Can't initialize IPC");
		return "qb_ipcs_run error";
	}

	return NULL;
}

void cs_ipcs_init(void)
{
	api = apidef_get ();

	qb_loop_poll_low_fds_event_set(cs_poll_handle_get(), cs_ipcs_low_fds_event);

	api->quorum_register_callback (cs_ipcs_fc_quorum_changed, NULL);
	totempg_queue_level_register_callback (cs_ipcs_totem_queue_level_changed);

	icmap_set_uint64("runtime.connections.active", 0);
	icmap_set_uint64("runtime.connections.closed", 0);
}

