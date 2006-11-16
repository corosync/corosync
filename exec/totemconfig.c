/*
 * Copyright (c) 2002-2005 MontaVista Software, Inc.
 * Copyright (c) 2006 RedHat, Inc.
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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/param.h>

#include "../include/list.h"
#include "util.h"
#include "totem.h"
#include "totemconfig.h"
#include "print.h"
#include "objdb.h"

#if defined(OPENAIS_BSD) || defined(OPENAIS_DARWIN)
	#define HZ 100  /* 10ms */
#endif

#define TOKEN_RETRANSMITS_BEFORE_LOSS_CONST	4
#define TOKEN_TIMEOUT				1000
#define TOKEN_RETRANSMIT_TIMEOUT		(int)(TOKEN_TIMEOUT / (TOKEN_RETRANSMITS_BEFORE_LOSS_CONST + 0.2))
#define TOKEN_HOLD_TIMEOUT			(int)(TOKEN_RETRANSMIT_TIMEOUT * 0.8 - (1000/(int)HZ))
#define JOIN_TIMEOUT				100
#define CONSENSUS_TIMEOUT			200
#define MERGE_TIMEOUT				200
#define DOWNCHECK_TIMEOUT			1000
#define FAIL_TO_RECV_CONST			50
#define	SEQNO_UNCHANGED_CONST			30
#define MINIMUM_TIMEOUT				(int)(1000/HZ)*3
#define MAX_NETWORK_DELAY			50
#define WINDOW_SIZE				50
#define MAX_MESSAGES				17
#define RRP_PROBLEM_COUNT_TIMEOUT		2000
#define RRP_PROBLEM_COUNT_THRESHOLD_DEFAULT	10
#define RRP_PROBLEM_COUNT_THRESHOLD_MIN		5

static char error_string_response[512];

/* These just makes the code below a little neater */
static inline int objdb_get_string (
	struct objdb_iface_ver0 *objdb, unsigned int object_service_handle,
	char *key, char **value)
{
	int res;

	*value = NULL;
	if ( !(res = objdb->object_key_get (object_service_handle,
					    key,
					    strlen (key),
					    (void *)value,
					    NULL))) {
		if (*value)
			return 0;
	}
	return -1;
}

static inline void objdb_get_int (
	struct objdb_iface_ver0 *objdb, unsigned int object_service_handle,
	char *key, unsigned int *intvalue)
{
	char *value = NULL;

	if (!objdb->object_key_get (object_service_handle,
				    key,
				    strlen (key),
				    (void *)&value,
				    NULL)) {
		if (value) {
			*intvalue = atoi(value);
		}
	}
}


extern int totem_config_read (
	struct objdb_iface_ver0 *objdb,
	struct totem_config *totem_config,
	char **error_string)
{
	int res = 0;
	unsigned int object_totem_handle;
	unsigned int object_interface_handle;
	char *str;
	unsigned int ringnumber = 0;

	memset (totem_config, 0, sizeof (struct totem_config));
	totem_config->interfaces = malloc (sizeof (struct totem_interface) * INTERFACE_MAX);
	if (totem_config->interfaces == 0) {
		*error_string = "Out of memory trying to allocate ethernet interface storage area";
		return -1;
	}

	memset (totem_config->interfaces, 0,
		sizeof (struct totem_interface) * INTERFACE_MAX);

	totem_config->secauth = 1;

	strcpy (totem_config->rrp_mode, "none");

	objdb->object_find_reset (OBJECT_PARENT_HANDLE);

	if (objdb->object_find (
		    OBJECT_PARENT_HANDLE,
		    "network",
		    strlen ("network"),
		    &object_totem_handle) == 0 ||
	    objdb->object_find (
		    OBJECT_PARENT_HANDLE,
		    "totem",
		    strlen ("totem"),
		    &object_totem_handle) == 0) {

		if (!objdb_get_string (objdb,object_totem_handle, "version", &str)) {
			if (strcmp (str, "2") == 0) {
				totem_config->version = 2;
			}
		}
		if (!objdb_get_string (objdb,object_totem_handle, "secauth", &str)) {
			if (strcmp (str, "on") == 0) {
				totem_config->secauth = 1;
			}
			if (strcmp (str, "off") == 0) {
				totem_config->secauth = 0;
			}
		}
		if (!objdb_get_string (objdb, object_totem_handle, "rrp_mode", &str)) {
			strcpy (totem_config->rrp_mode, str);
		}

		/*
		 * Get interface node id
		 */
		objdb_get_int (objdb, object_totem_handle, "nodeid", &totem_config->node_id);

		objdb_get_int (objdb,object_totem_handle, "threads", &totem_config->threads);


		objdb_get_int (objdb,object_totem_handle, "netmtu", &totem_config->net_mtu);

		objdb_get_int (objdb,object_totem_handle, "token", &totem_config->token_timeout);

		objdb_get_int (objdb,object_totem_handle, "token_retransmit", &totem_config->token_retransmit_timeout);

		objdb_get_int (objdb,object_totem_handle, "hold", &totem_config->token_hold_timeout);

		objdb_get_int (objdb,object_totem_handle, "token_retransmits_before_loss_const", &totem_config->token_retransmits_before_loss_const);

		objdb_get_int (objdb,object_totem_handle, "join", &totem_config->join_timeout);
		objdb_get_int (objdb,object_totem_handle, "send_join", &totem_config->send_join_timeout);

		objdb_get_int (objdb,object_totem_handle, "consensus", &totem_config->consensus_timeout);

		objdb_get_int (objdb,object_totem_handle, "merge", &totem_config->merge_timeout);

		objdb_get_int (objdb,object_totem_handle, "downcheck", &totem_config->downcheck_timeout);

		objdb_get_int (objdb,object_totem_handle, "fail_recv_const", &totem_config->fail_to_recv_const);

		objdb_get_int (objdb,object_totem_handle, "seqno_unchanged_const", &totem_config->seqno_unchanged_const);

		objdb_get_int (objdb,object_totem_handle, "rrp_token_expired_timeout", &totem_config->rrp_token_expired_timeout);

		objdb_get_int (objdb,object_totem_handle, "rrp_problem_count_timeout", &totem_config->rrp_problem_count_timeout);

		objdb_get_int (objdb,object_totem_handle, "rrp_problem_count_threshold", &totem_config->rrp_problem_count_threshold);

		objdb_get_int (objdb,object_totem_handle, "heartbeat_failures_allowed", &totem_config->heartbeat_failures_allowed);

		objdb_get_int (objdb,object_totem_handle, "max_network_delay", &totem_config->max_network_delay);

		objdb_get_int (objdb,object_totem_handle, "window_size", &totem_config->window_size);
		objdb_get_string (objdb, object_totem_handle, "vsftype", &totem_config->vsf_type);

		objdb_get_int (objdb,object_totem_handle, "max_messages", &totem_config->max_messages);
	}
	while (objdb->object_find (
		    object_totem_handle,
		    "interface",
		    strlen ("interface"),
		    &object_interface_handle) == 0) {

		objdb_get_int (objdb, object_interface_handle, "ringnumber", &ringnumber);

		/*
		 * Get interface multicast address
		 */
		if (!objdb_get_string (objdb, object_interface_handle, "mcastaddr", &str)) {
			res = totemip_parse (&totem_config->interfaces[ringnumber].mcast_addr, str, 0);
		}

		/*
		 * Get mcast port
		 */
		if (!objdb_get_string (objdb, object_interface_handle, "mcastport", &str)) {
			totem_config->interfaces[ringnumber].ip_port = atoi (str);
		}

		/*
		 * Get the bind net address
		 */
		if (!objdb_get_string (objdb, object_interface_handle, "bindnetaddr", &str)) {

			res = totemip_parse (&totem_config->interfaces[ringnumber].bindnet, str,
					     totem_config->interfaces[ringnumber].mcast_addr.family);
		}
		totem_config->interface_count++;
	}

	return 0;

	return (-1);
}

int totem_config_validate (
	struct totem_config *totem_config,
	char **error_string)
{
	static char local_error_reason[512];
	char parse_error[512];
	char *error_reason = local_error_reason;
	int i;
	unsigned int interface_max = INTERFACE_MAX;

	if (totem_config->interface_count == 0) {
		error_reason = "No interfaces defined";
		goto parse_error;
	}

	for (i = 0; i < totem_config->interface_count; i++) {
		/*
		 * Some error checking of parsed data to make sure its valid
		 */
		if ((int *)&totem_config->interfaces[i].mcast_addr.addr == 0) {
			error_reason = "No multicast address specified";
			goto parse_error;
		}
	
		if (totem_config->interfaces[i].ip_port == 0) {
			error_reason = "No multicast port specified";
			goto parse_error;
		}

		if (totem_config->interfaces[i].mcast_addr.family == AF_INET6 &&
			totem_config->node_id == 0) {
	
			error_reason = "An IPV6 network requires that a node ID be specified.";
			goto parse_error;
		}

		if (totem_config->interfaces[i].mcast_addr.family != totem_config->interfaces[i].bindnet.family) {
			error_reason = "Multicast address family does not match bind address family";
			goto parse_error;
		}

		if (totem_config->interfaces[i].mcast_addr.family != totem_config->interfaces[i].bindnet.family) {
			error_reason =  "Not all bind address belong to the same IP family";
			goto parse_error;
		}
	}

	if (totem_config->version != 2) {
		error_reason = "This totem parser can only parse version 2 configurations.";
		goto parse_error;
	}


	if (totem_config->token_retransmits_before_loss_const == 0) {
		totem_config->token_retransmits_before_loss_const =
			TOKEN_RETRANSMITS_BEFORE_LOSS_CONST;
	}

	/*
	 * Setup timeout values that are not setup by user
	 */
	if (totem_config->token_timeout == 0) {
		totem_config->token_timeout = TOKEN_TIMEOUT;
		if (totem_config->token_retransmits_before_loss_const == 0) {
			totem_config->token_retransmits_before_loss_const = TOKEN_RETRANSMITS_BEFORE_LOSS_CONST;
		}

		if (totem_config->token_retransmit_timeout == 0) {
			totem_config->token_retransmit_timeout =
				(int)(totem_config->token_timeout /
				(totem_config->token_retransmits_before_loss_const + 0.2));
		}
		if (totem_config->token_hold_timeout == 0) {
			totem_config->token_hold_timeout =
				(int)(totem_config->token_retransmit_timeout * 0.8 -
				(1000/HZ));
		}
	}

	if (totem_config->max_network_delay == 0) {
		totem_config->max_network_delay = MAX_NETWORK_DELAY;
	}

	if (totem_config->max_network_delay < MINIMUM_TIMEOUT) {
		sprintf (local_error_reason, "The max_network_delay parameter (%d ms) may not be less then (%d ms).",
			totem_config->max_network_delay, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	if (totem_config->window_size == 0) {
		totem_config->window_size = WINDOW_SIZE;
	}

	if (totem_config->max_messages == 0) {
		totem_config->max_messages = MAX_MESSAGES;
	}

	if (totem_config->token_timeout < MINIMUM_TIMEOUT) {
		sprintf (local_error_reason, "The token timeout parameter (%d ms) may not be less then (%d ms).",
			totem_config->token_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	if (totem_config->token_retransmit_timeout == 0) {
		totem_config->token_retransmit_timeout =
			(int)(totem_config->token_timeout /
			(totem_config->token_retransmits_before_loss_const + 0.2));
	}
	if (totem_config->token_hold_timeout == 0) {
		totem_config->token_hold_timeout =
			(int)(totem_config->token_retransmit_timeout * 0.8 -
			(1000/HZ));
	}
	if (totem_config->token_retransmit_timeout < MINIMUM_TIMEOUT) {
		sprintf (local_error_reason, "The token retransmit timeout parameter (%d ms) may not be less then (%d ms).",
			totem_config->token_retransmit_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	if (totem_config->token_hold_timeout == 0) {
		totem_config->token_hold_timeout = TOKEN_HOLD_TIMEOUT;
	}

	if (totem_config->token_hold_timeout < MINIMUM_TIMEOUT) {
		sprintf (local_error_reason, "The token hold timeout parameter (%d ms) may not be less then (%d ms).",
			totem_config->token_hold_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	if (totem_config->join_timeout == 0) {
		totem_config->join_timeout = JOIN_TIMEOUT;
	}

	if (totem_config->join_timeout < MINIMUM_TIMEOUT) {
		sprintf (local_error_reason, "The join timeout parameter (%d ms) may not be less then (%d ms).",
			totem_config->join_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	if (totem_config->consensus_timeout == 0) {
		totem_config->consensus_timeout = CONSENSUS_TIMEOUT;
	}

	if (totem_config->consensus_timeout < MINIMUM_TIMEOUT) {
		sprintf (local_error_reason, "The consensus timeout parameter (%d ms) may not be less then (%d ms).",
			totem_config->consensus_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	if (totem_config->merge_timeout == 0) {
		totem_config->merge_timeout = MERGE_TIMEOUT;
	}

	if (totem_config->merge_timeout < MINIMUM_TIMEOUT) {
		sprintf (local_error_reason, "The merge timeout parameter (%d ms) may not be less then (%d ms).",
			totem_config->merge_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	if (totem_config->downcheck_timeout == 0) {
		totem_config->downcheck_timeout = DOWNCHECK_TIMEOUT;
	}

	if (totem_config->downcheck_timeout < MINIMUM_TIMEOUT) {
		sprintf (local_error_reason, "The downcheck timeout parameter (%d ms) may not be less then (%d ms).",
			totem_config->downcheck_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	/*
	 * RRP values validation
	 */
	if (strcmp (totem_config->rrp_mode, "none") &&
		strcmp (totem_config->rrp_mode, "active") &&
		strcmp (totem_config->rrp_mode, "passive")) {
		sprintf (local_error_reason, "The RRP mode \"%s\" specified is invalid.  It must be none, active, or passive.\n", totem_config->rrp_mode);
		goto parse_error;
	}
	if (totem_config->rrp_problem_count_timeout == 0) {
		totem_config->rrp_problem_count_timeout = RRP_PROBLEM_COUNT_TIMEOUT;
	}
	if (totem_config->rrp_problem_count_timeout < MINIMUM_TIMEOUT) {
		sprintf (local_error_reason, "The RRP problem count timeout parameter (%d ms) may not be less then (%d ms).",
			totem_config->rrp_problem_count_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}
	if (totem_config->rrp_problem_count_threshold == 0) {
		totem_config->rrp_problem_count_threshold = RRP_PROBLEM_COUNT_THRESHOLD_DEFAULT;
	}
	if (totem_config->rrp_problem_count_threshold < RRP_PROBLEM_COUNT_THRESHOLD_MIN) {
		sprintf (local_error_reason, "The RRP problem count threshold (%d problem count) may not be less then (%d problem count).",
			totem_config->rrp_problem_count_threshold, RRP_PROBLEM_COUNT_THRESHOLD_MIN);
		goto parse_error;
	}
	if (totem_config->rrp_token_expired_timeout == 0) {
		totem_config->rrp_token_expired_timeout =
			totem_config->token_retransmit_timeout;
	}
		
	if (totem_config->rrp_token_expired_timeout < MINIMUM_TIMEOUT) {
		sprintf (local_error_reason, "The RRP token expired timeout parameter (%d ms) may not be less then (%d ms).",
			totem_config->rrp_token_expired_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	if (strcmp (totem_config->rrp_mode, "none") == 0) {
		interface_max = 1;
	}
	if (interface_max < totem_config->interface_count) {
		sprintf (parse_error,
			"%d is too many configured interfaces for the rrp_mode setting %s.",
			totem_config->interface_count,
			totem_config->rrp_mode);
		error_reason = parse_error;
		goto parse_error;
	}


	if (totem_config->fail_to_recv_const == 0) {
		totem_config->fail_to_recv_const = FAIL_TO_RECV_CONST;
	}
	if (totem_config->seqno_unchanged_const == 0) {
		totem_config->seqno_unchanged_const = SEQNO_UNCHANGED_CONST;
	}
	if (totem_config->net_mtu == 0) {
		totem_config->net_mtu = 1500;
	}

	if ((MESSAGE_SIZE_MAX / totem_config->net_mtu) < totem_config->max_messages) {
		sprintf (local_error_reason, "The max_messages parameter (%d messages) may not be greater then (%d messages).",
			totem_config->max_messages, MESSAGE_SIZE_MAX / totem_config->net_mtu);
		goto parse_error;
	}

	if (totem_config->threads > SEND_THREADS_MAX) {
		totem_config->threads = SEND_THREADS_MAX;
	}
	if (totem_config->secauth == 0) {
		totem_config->threads = 0;
	}
	if (totem_config->net_mtu > FRAME_SIZE_MAX) {
		error_reason = "This net_mtu parameter is greater then the maximum frame size";
		goto parse_error;
	}
	if (totem_config->vsf_type == NULL) {
		totem_config->vsf_type = "none";
	}

	return (0);

parse_error:
	sprintf (error_string_response,
		 "parse error in config: %s\n", error_reason);
	*error_string = error_string_response;
	return (-1);
}

static int read_keyfile (
	char *key_location,
	struct totem_config *totem_config,
	char **error_string)
{
	int fd;
	int res;

	fd = open (key_location, O_RDONLY);
	if (fd == -1) {
		sprintf (error_string_response, "Could not open %s: %s\n",
			 key_location, strerror (errno));
		goto parse_error;
	}

	res = read (fd, totem_config->private_key, 128);
	if (res == -1) {
		close (fd);
		sprintf (error_string_response, "Could not read %s: %s\n",
			 key_location, strerror (errno));
		goto parse_error;
	}

	totem_config->private_key_len = 128;

	if (res != 128) {
		close (fd);
		sprintf (error_string_response, "Could only read %d bits of 1024 bits from %s.\n",
			 res * 8, key_location);
		goto parse_error;
	}
	return 0;

parse_error:
	*error_string = error_string_response;
	return (-1);
}

int totem_config_keyread (
	struct objdb_iface_ver0 *objdb,
	struct totem_config *totem_config,
	char **error_string)
{
	int got_key = 0;
	char *key_location = NULL;
	unsigned int object_service_handle;
	int res;

	memset (totem_config->private_key, 0, 128);
	totem_config->private_key_len = 128;

	if (totem_config->secauth == 0) {
		return (0);
	}

	if (objdb->object_find (
		    OBJECT_PARENT_HANDLE,
		    "network",
		    strlen ("network"),
		    &object_service_handle) == 0 ||
	    objdb->object_find (
		    OBJECT_PARENT_HANDLE,
		    "totem",
		    strlen ("totem"),
		    &object_service_handle) == 0) {

		/* objdb may store the location of the key file */
		if (!objdb_get_string (objdb,object_service_handle, "keyfile", &key_location)
		    && key_location) {
			res = read_keyfile(key_location, totem_config, error_string);
			if (res)
				goto key_error;
			got_key = 1;
		}
		else { /* Or the key itself may be in the objdb */
			char *key = NULL;
			int key_len;
			res = objdb->object_key_get (object_service_handle,
						     "key",
						     strlen ("key"),
						     (void *)&key,
						     &key_len);
			if (res == 0 && key) {
				memcpy(totem_config->private_key, key, key_len);
				totem_config->private_key_len = key_len;
				got_key = 1;
			}
		}
	}

	/* In desperation we read the default filename */
	if (!got_key) {
		char *filename = getenv("OPENAIS_TOTEM_AUTHKEY_FILE");
		if (!filename)
			filename = "/etc/ais/authkey";
		res = read_keyfile(filename, totem_config, error_string);
		if (res)
			goto key_error;

	}

	return (0);

	*error_string = error_string_response;
key_error:
	return (-1);

}
