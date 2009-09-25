/*
 * Copyright (c) 2009 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)

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

#include <assert.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sched.h>
#include <time.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <limits.h>

#include <corosync/sq.h>
#include <corosync/list.h>
#include <corosync/hdb.h>
#include <corosync/swab.h>
#include <corosync/totem/coropoll.h>
#define LOGSYS_UTILS_ONLY 1
#include <corosync/engine/logsys.h>
#include "totemiba.h"
#include "wthread.h"

#include "crypto.h"

struct totemiba_instance {
	int dummy;
};

DECLARE_HDB_DATABASE (totemiba_instance_database,NULL);

static void totemiba_instance_initialize (struct totemiba_instance *instance)
{
	memset (instance, 0, sizeof (struct totemiba_instance));
}

#define log_printf(level, format, args...)				\
do {									\
        instance->totemiba_log_printf (					\
		LOGSYS_ENCODE_RECID(level,				\
				    instance->totemiba_subsys_id,	\
				    LOGSYS_RECID_LOG),			\
                __FUNCTION__, __FILE__, __LINE__,			\
		(const char *)format, ##args);				\
} while (0);


int totemiba_crypto_set (hdb_handle_t handle,
			 unsigned int type)
{
	struct totemiba_instance *instance;
	int res = 0;

	res = hdb_handle_get (&totemiba_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}

	hdb_handle_put (&totemiba_instance_database, handle);

error_exit:
	return res;
}

int totemiba_finalize (
	hdb_handle_t handle)
{
	struct totemiba_instance *instance;
	int res = 0;

	res = hdb_handle_get (&totemiba_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}

	hdb_handle_put (&totemiba_instance_database, handle);

error_exit:
	return (res);
}

/*
 * Create an instance
 */
int totemiba_initialize (
	hdb_handle_t poll_handle,
	hdb_handle_t *handle,
	struct totem_config *totem_config,
	int interface_no,
	void *context,

	void (*deliver_fn) (
		void *context,
		const void *msg,
		unsigned int msg_len),

	void (*iface_change_fn) (
		void *context,
		const struct totem_ip_address *iface_address))
{
	struct totemiba_instance *instance;
	unsigned int res;

	res = hdb_handle_create (&totemiba_instance_database,
		sizeof (struct totemiba_instance), handle);
	if (res != 0) {
		goto error_exit;
	}
	res = hdb_handle_get (&totemiba_instance_database, *handle,
		(void *)&instance);
	if (res != 0) {
		goto error_destroy;
	}

	totemiba_instance_initialize (instance);

error_exit:
	hdb_handle_put (&totemiba_instance_database, *handle);
	return (0);

error_destroy:
	hdb_handle_destroy (&totemiba_instance_database, *handle);
	return (-1);
}

int totemiba_processor_count_set (
	hdb_handle_t handle,
	int processor_count)
{
	struct totemiba_instance *instance;
	int res = 0;

	res = hdb_handle_get (&totemiba_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}

	hdb_handle_put (&totemiba_instance_database, handle);

error_exit:
	return (res);
}

int totemiba_recv_flush (hdb_handle_t handle)
{
	struct totemiba_instance *instance;
	int res = 0;

	res = hdb_handle_get (&totemiba_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}

	hdb_handle_put (&totemiba_instance_database, handle);

error_exit:
	return (res);
}

int totemiba_send_flush (hdb_handle_t handle)
{
	struct totemiba_instance *instance;
	int res = 0;

	res = hdb_handle_get (&totemiba_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}

	hdb_handle_put (&totemiba_instance_database, handle);

error_exit:
	return (res);
}

int totemiba_token_send (
	hdb_handle_t handle,
	const void *msg,
	unsigned int msg_len)
{
	struct totemiba_instance *instance;
	int res = 0;

	res = hdb_handle_get (&totemiba_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}

	hdb_handle_put (&totemiba_instance_database, handle);

error_exit:
	return (res);
}
int totemiba_mcast_flush_send (
	hdb_handle_t handle,
	const void *msg,
	unsigned int msg_len)
{
	struct totemiba_instance *instance;
	int res = 0;

	res = hdb_handle_get (&totemiba_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}

	hdb_handle_put (&totemiba_instance_database, handle);

error_exit:
	return (res);
}

int totemiba_mcast_noflush_send (
	hdb_handle_t handle,
	const void *msg,
	unsigned int msg_len)
{
	struct totemiba_instance *instance;
	int res = 0;

	res = hdb_handle_get (&totemiba_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}

	hdb_handle_put (&totemiba_instance_database, handle);
error_exit:
	return (res);
}

extern int totemiba_iface_check (hdb_handle_t handle)
{
	struct totemiba_instance *instance;
	int res = 0;

	res = hdb_handle_get (&totemiba_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		res = ENOENT;
		goto error_exit;
	}

	hdb_handle_put (&totemiba_instance_database, handle);
error_exit:
	return (res);
}

extern void totemiba_net_mtu_adjust (hdb_handle_t handle, struct totem_config *totem_config)
{
}

const char *totemiba_iface_print (hdb_handle_t handle)  {
	struct totemiba_instance *instance;
	int res = 0;
	const char *ret_char;

	res = hdb_handle_get (&totemiba_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		ret_char = "Invalid totemiba handle";
		goto error_exit;
	}

	hdb_handle_put (&totemiba_instance_database, handle);
error_exit:
	return (ret_char);
}

int totemiba_iface_get (
	hdb_handle_t handle,
	struct totem_ip_address *addr)
{
	struct totemiba_instance *instance;
	unsigned int res;

	res = hdb_handle_get (&totemiba_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		goto error_exit;
	}

	hdb_handle_put (&totemiba_instance_database, handle);

error_exit:
	return (res);
}

int totemiba_token_target_set (
	hdb_handle_t handle,
	const struct totem_ip_address *token_target)
{
	struct totemiba_instance *instance;
	unsigned int res;

	res = hdb_handle_get (&totemiba_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		goto error_exit;
	}

	hdb_handle_put (&totemiba_instance_database, handle);

error_exit:
	return (res);
}

extern int totemiba_recv_mcast_empty (
	hdb_handle_t handle)
{
	struct totemiba_instance *instance;
	unsigned int res;

	res = hdb_handle_get (&totemiba_instance_database, handle,
		(void *)&instance);
	if (res != 0) {
		goto error_exit;
	}

	hdb_handle_put (&totemiba_instance_database, handle);

	return (0);

error_exit:
	return (res);
}

