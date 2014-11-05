/*
 * Copyright (c) 2002-2005 MontaVista Software, Inc.
 * Copyright (c) 2006-2010 Red Hat, Inc.
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/param.h>

#include <corosync/swab.h>
#include <corosync/list.h>
#include <corosync/totem/totem.h>
#include <corosync/engine/objdb.h>
#include <corosync/engine/config.h>
#include <corosync/engine/logsys.h>

#ifdef HAVE_LIBNSS
#include <nss.h>
#include <pk11pub.h>
#include <pkcs11.h>
#include <prerror.h>
#endif

#include "util.h"
#include "totemconfig.h"
#include "tlist.h" /* for HZ */

#define TOKEN_RETRANSMITS_BEFORE_LOSS_CONST	4
#define TOKEN_TIMEOUT				1000
#define TOKEN_RETRANSMIT_TIMEOUT		(int)(TOKEN_TIMEOUT / (TOKEN_RETRANSMITS_BEFORE_LOSS_CONST + 0.2))
#define TOKEN_HOLD_TIMEOUT			(int)(TOKEN_RETRANSMIT_TIMEOUT * 0.8 - (1000/(int)HZ))
#define JOIN_TIMEOUT				50
#define MERGE_TIMEOUT				200
#define DOWNCHECK_TIMEOUT			1000
#define FAIL_TO_RECV_CONST			2500
#define	SEQNO_UNCHANGED_CONST			30
#define MINIMUM_TIMEOUT				(int)(1000/HZ)*3
#define MAX_NETWORK_DELAY			50
#define WINDOW_SIZE				50
#define MAX_MESSAGES				17
#define MISS_COUNT_CONST			5
#define RRP_PROBLEM_COUNT_TIMEOUT		2000
#define RRP_PROBLEM_COUNT_THRESHOLD_DEFAULT	10
#define RRP_PROBLEM_COUNT_THRESHOLD_MIN		2
#define RRP_AUTORECOVERY_CHECK_TIMEOUT		1000

static char error_string_response[512];
static struct objdb_iface_ver0 *global_objdb;

static void add_totem_config_notification(
	struct objdb_iface_ver0 *objdb,
	struct totem_config *totem_config,
	hdb_handle_t totem_object_handle);


/* These just makes the code below a little neater */
static inline int objdb_get_string (
	const struct objdb_iface_ver0 *objdb,
	hdb_handle_t object_service_handle,
	const char *key, const char **value)
{
	int res;

	*value = NULL;
	if ( !(res = objdb->object_key_get (object_service_handle,
		key,
		strlen (key),
		(void *)value,
		NULL))) {

		if (*value) {
			return 0;
		}
	}
	return -1;
}

static inline void objdb_get_int (
	const struct objdb_iface_ver0 *objdb,
	hdb_handle_t object_service_handle,
	const char *key, unsigned int *intvalue)
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

static unsigned int totem_handle_find (
	struct objdb_iface_ver0 *objdb,
	hdb_handle_t *totem_find_handle)  {

	hdb_handle_t object_find_handle;
	unsigned int res;

	/*
	 * Find a network section
	 */
	objdb->object_find_create (
		OBJECT_PARENT_HANDLE,
		"network",
		strlen ("network"),
		&object_find_handle);

	res = objdb->object_find_next (
		object_find_handle,
		totem_find_handle);

	objdb->object_find_destroy (object_find_handle);

	/*
	 * Network section not found in configuration, checking for totem
	 */
	if (res == -1) {
		objdb->object_find_create (
			OBJECT_PARENT_HANDLE,
			"totem",
			strlen ("totem"),
			&object_find_handle);

		res = objdb->object_find_next (
			object_find_handle,
			totem_find_handle);

		objdb->object_find_destroy (object_find_handle);
	}

	if (res == -1) {
		return (-1);
	}

	return (0);
}

static void totem_volatile_config_read (
	struct objdb_iface_ver0 *objdb,
	struct totem_config *totem_config,
	hdb_handle_t object_totem_handle)
{
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

	objdb_get_int (objdb,object_totem_handle, "rrp_problem_count_mcast_threshold", &totem_config->rrp_problem_count_mcast_threshold);

	objdb_get_int (objdb,object_totem_handle, "rrp_autorecovery_check_timeout", &totem_config->rrp_autorecovery_check_timeout);

	objdb_get_int (objdb,object_totem_handle, "heartbeat_failures_allowed", &totem_config->heartbeat_failures_allowed);

	objdb_get_int (objdb,object_totem_handle, "max_network_delay", &totem_config->max_network_delay);

	objdb_get_int (objdb,object_totem_handle, "window_size", &totem_config->window_size);
	(void)objdb_get_string (objdb, object_totem_handle, "vsftype", &totem_config->vsf_type);

	objdb_get_int (objdb,object_totem_handle, "max_messages", &totem_config->max_messages);

	objdb_get_int (objdb,object_totem_handle, "miss_count_const", &totem_config->miss_count_const);
}


static void totem_get_crypto_type(
	const struct objdb_iface_ver0 *objdb,
	hdb_handle_t object_totem_handle,
	struct totem_config *totem_config)
{
	const char *str;

	totem_config->crypto_accept = TOTEM_CRYPTO_ACCEPT_OLD;
	if (!objdb_get_string (objdb, object_totem_handle, "crypto_accept", &str)) {
		if (strcmp(str, "new") == 0) {
			totem_config->crypto_accept = TOTEM_CRYPTO_ACCEPT_NEW;
		}
	}

	totem_config->crypto_type = TOTEM_CRYPTO_SOBER;

#ifdef HAVE_LIBNSS
	/*
	 * We must set these even if the key does not exist.
	 * Encryption type can be set on-the-fly using CFG
	 */
	totem_config->crypto_crypt_type = CKM_AES_CBC_PAD;
	totem_config->crypto_sign_type = CKM_SHA256_RSA_PKCS;
#endif

	if (!objdb_get_string (objdb, object_totem_handle, "crypto_type", &str)) {
		if (strcmp(str, "sober") == 0) {
			return;
		}
#ifdef HAVE_LIBNSS
		if (strcmp(str, "nss") == 0) {
			totem_config->crypto_type = TOTEM_CRYPTO_NSS;

		}
#endif
	}
}



extern int totem_config_read (
	struct objdb_iface_ver0 *objdb,
	struct totem_config *totem_config,
	const char **error_string)
{
	int res = 0;
	hdb_handle_t object_totem_handle;
	hdb_handle_t object_interface_handle;
	hdb_handle_t object_member_handle;
	const char *str;
	unsigned int ringnumber = 0;
	hdb_handle_t object_find_interface_handle;
	hdb_handle_t object_find_member_handle;
	const char *transport_type;
	int member_count = 0;

	res = totem_handle_find (objdb, &object_totem_handle);
	if (res == -1) {
printf ("couldn't find totem handle\n");
		return (-1);
	}

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

	if (!objdb_get_string (objdb, object_totem_handle, "version", &str)) {
		if (strcmp (str, "2") == 0) {
			totem_config->version = 2;
		}
	}
	if (!objdb_get_string (objdb, object_totem_handle, "secauth", &str)) {
		if (strcmp (str, "on") == 0) {
			totem_config->secauth = 1;
		}
		if (strcmp (str, "off") == 0) {
			totem_config->secauth = 0;
		}
	}

	if (totem_config->secauth == 1) {
		totem_get_crypto_type(objdb, object_totem_handle, totem_config);
	}

	if (!objdb_get_string (objdb, object_totem_handle, "rrp_mode", &str)) {
		strcpy (totem_config->rrp_mode, str);
	}

	/*
	 * Get interface node id
	 */
	objdb_get_int (objdb, object_totem_handle, "nodeid", &totem_config->node_id);

	totem_config->clear_node_high_bit = 0;
	if (!objdb_get_string (objdb,object_totem_handle, "clear_node_high_bit", &str)) {
		if (strcmp (str, "yes") == 0) {
			totem_config->clear_node_high_bit = 1;
		}
	}

	objdb_get_int (objdb,object_totem_handle, "threads", &totem_config->threads);


	objdb_get_int (objdb,object_totem_handle, "netmtu", &totem_config->net_mtu);

	/*
	 * Get things that might change in the future
	 */
	totem_volatile_config_read (objdb, totem_config, object_totem_handle);

	/*
	 * Broadcast option is global but set in interface section,
	 * so reset before processing interfaces.
	 */
	totem_config->broadcast_use = 0;

	objdb->object_find_create (
		object_totem_handle,
		"interface",
		strlen ("interface"),
		&object_find_interface_handle);

	while (objdb->object_find_next (
		object_find_interface_handle,
		&object_interface_handle) == 0) {

		member_count = 0;

		objdb_get_int (objdb, object_interface_handle, "ringnumber", &ringnumber);


		if (ringnumber >= INTERFACE_MAX) {
			snprintf (error_string_response, sizeof(error_string_response),
			    "parse error in config: interface ring number %u is bigger than allowed maximum %u\n",
			    ringnumber, INTERFACE_MAX - 1);

			*error_string = error_string_response;
			return -1;
		}

		/*
		 * Get interface multicast address
		 */
		if (!objdb_get_string (objdb, object_interface_handle, "mcastaddr", &str)) {
			res = totemip_parse (&totem_config->interfaces[ringnumber].mcast_addr, str, 0);
		}
		if (!objdb_get_string (objdb, object_interface_handle, "broadcast", &str)) {
			if (strcmp (str, "yes") == 0) {
				totem_config->broadcast_use = 1;
			}
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

		/*
		 * Get the TTL
		 */
		totem_config->interfaces[ringnumber].ttl = 1;
		if (!objdb_get_string (objdb, object_interface_handle, "ttl", &str)) {
			totem_config->interfaces[ringnumber].ttl = atoi (str);
		}

		objdb->object_find_create (
			object_interface_handle,
			"member",
			strlen ("member"),
			&object_find_member_handle);

		while (objdb->object_find_next (
			object_find_member_handle,
			&object_member_handle) == 0) {

			if (!objdb_get_string (objdb, object_member_handle, "memberaddr", &str)) {
				res = totemip_parse (&totem_config->interfaces[ringnumber].member_list[member_count++], str, 0);
			}
		
		}
		totem_config->interfaces[ringnumber].member_count = member_count;
		totem_config->interface_count++;
		objdb->object_find_destroy (object_find_member_handle);
	}

	objdb->object_find_destroy (object_find_interface_handle);

	/*
	 * Use broadcast is global, so if set, make sure to fill mcast addr correctly
	 */
	if (totem_config->broadcast_use) {
		for (ringnumber = 0; ringnumber < totem_config->interface_count; ringnumber++) {
			totemip_parse (&totem_config->interfaces[ringnumber].mcast_addr,
				"255.255.255.255", 0);
		}
	}

	add_totem_config_notification(objdb, totem_config, object_totem_handle);

	totem_config->transport_number = TOTEM_TRANSPORT_UDP;
	(void)objdb_get_string (objdb, object_totem_handle, "transport", &transport_type);

	if (transport_type) {
		if (strcmp (transport_type, "udpu") == 0) {
			totem_config->transport_number = TOTEM_TRANSPORT_UDPU;
		}
	}
	if (transport_type) {
		if (strcmp (transport_type, "iba") == 0) {
			totem_config->transport_number = TOTEM_TRANSPORT_RDMA;
		}
	}

	return 0;
}

int totem_config_validate (
	struct totem_config *totem_config,
	const char **error_string)
{
	static char local_error_reason[512];
	char parse_error[512];
	const char *error_reason = local_error_reason;
	int i, j;
	unsigned int interface_max = INTERFACE_MAX;
	unsigned int port1, port2;

	if (totem_config->interface_count == 0) {
		error_reason = "No interfaces defined";
		goto parse_error;
	}

	for (i = 0; i < totem_config->interface_count; i++) {
		/*
		 * Some error checking of parsed data to make sure its valid
		 */

		struct totem_ip_address null_addr;
		memset (&null_addr, 0, sizeof (struct totem_ip_address));

		if ((totem_config->transport_number == 0) &&
			memcmp (&totem_config->interfaces[i].mcast_addr, &null_addr,
				sizeof (struct totem_ip_address)) == 0) {
			error_reason = "No multicast address specified";
			goto parse_error;
		}

		if (totem_config->interfaces[i].ip_port == 0) {
			error_reason = "No multicast port specified";
			goto parse_error;
		}

		if (totem_config->interfaces[i].ttl > 255) {
			error_reason = "Invalid TTL (should be 0..255)";
			goto parse_error;
		}
		if (totem_config->transport_number != TOTEM_TRANSPORT_UDP &&
		    totem_config->interfaces[i].ttl != 1) {
			error_reason = "Can only set ttl on multicast transport types";
			goto parse_error;
		}

		if (totem_config->interfaces[i].mcast_addr.family == AF_INET6 &&
			totem_config->node_id == 0) {

			error_reason = "An IPV6 network requires that a node ID be specified.";
			goto parse_error;
		}

		if (totem_config->broadcast_use == 0 && totem_config->transport_number == TOTEM_TRANSPORT_UDP) {
			if (totem_config->interfaces[i].mcast_addr.family != totem_config->interfaces[i].bindnet.family) {
				error_reason = "Multicast address family does not match bind address family";
				goto parse_error;
			}

			if (totemip_is_mcast (&totem_config->interfaces[i].mcast_addr) != 0) {
				error_reason = "mcastaddr is not a correct multicast address.";
				goto parse_error;
			}
		}

		if (totem_config->interfaces[0].bindnet.family != totem_config->interfaces[i].bindnet.family) {
			error_reason =  "Not all bind address belong to the same IP family";
			goto parse_error;
		}

		/*
		 * Ensure mcast address/port differs
		 */
		if (totem_config->transport_number == TOTEM_TRANSPORT_UDP) {
			for (j = i + 1; j < totem_config->interface_count; j++) {
				port1 = totem_config->interfaces[i].ip_port;
				port2 = totem_config->interfaces[j].ip_port;
				if (totemip_equal(&totem_config->interfaces[i].mcast_addr,
				    &totem_config->interfaces[j].mcast_addr) &&
				    (((port1 > port2 ? port1 : port2)  - (port1 < port2 ? port1 : port2)) <= 1)) {
					error_reason = "Interfaces multicast address/port pair must differ";
					goto parse_error;
				}
			}
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
		snprintf (local_error_reason, sizeof(local_error_reason),
			"The max_network_delay parameter (%d ms) may not be less than (%d ms).",
			totem_config->max_network_delay, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	if (totem_config->window_size == 0) {
		totem_config->window_size = WINDOW_SIZE;
	}

	if (totem_config->max_messages == 0) {
		totem_config->max_messages = MAX_MESSAGES;
	}

	if (totem_config->miss_count_const == 0) {
		totem_config->miss_count_const = MISS_COUNT_CONST;
	}

	if (totem_config->token_timeout < MINIMUM_TIMEOUT) {
		snprintf (local_error_reason, sizeof(local_error_reason),
			"The token timeout parameter (%d ms) may not be less than (%d ms).",
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
		snprintf (local_error_reason, sizeof(local_error_reason),
			"The token retransmit timeout parameter (%d ms) may not be less than (%d ms).",
			totem_config->token_retransmit_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	if (totem_config->token_hold_timeout == 0) {
		totem_config->token_hold_timeout = TOKEN_HOLD_TIMEOUT;
	}

	if (totem_config->token_hold_timeout < MINIMUM_TIMEOUT) {
		snprintf (local_error_reason, sizeof(local_error_reason),
			"The token hold timeout parameter (%d ms) may not be less than (%d ms).",
			totem_config->token_hold_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	if (totem_config->join_timeout == 0) {
		totem_config->join_timeout = JOIN_TIMEOUT;
	}

	if (totem_config->join_timeout < MINIMUM_TIMEOUT) {
		snprintf (local_error_reason, sizeof(local_error_reason),
			"The join timeout parameter (%d ms) may not be less than (%d ms).",
			totem_config->join_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	if (totem_config->consensus_timeout == 0) {
		totem_config->consensus_timeout = (int)(float)(1.2 * totem_config->token_timeout);
	}

	if (totem_config->consensus_timeout < MINIMUM_TIMEOUT) {
		snprintf (local_error_reason, sizeof(local_error_reason),
			"The consensus timeout parameter (%d ms) may not be less than (%d ms).",
			totem_config->consensus_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	if (totem_config->consensus_timeout < totem_config->join_timeout) {
		snprintf (local_error_reason, sizeof(local_error_reason),
			"The consensus timeout parameter (%d ms) may not be less than join timeout (%d ms).",
			totem_config->consensus_timeout, totem_config->join_timeout);
		goto parse_error;
	}

	if (totem_config->merge_timeout == 0) {
		totem_config->merge_timeout = MERGE_TIMEOUT;
	}

	if (totem_config->merge_timeout < MINIMUM_TIMEOUT) {
		snprintf (local_error_reason, sizeof(local_error_reason),
			"The merge timeout parameter (%d ms) may not be less than (%d ms).",
			totem_config->merge_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	if (totem_config->downcheck_timeout == 0) {
		totem_config->downcheck_timeout = DOWNCHECK_TIMEOUT;
	}

	if (totem_config->downcheck_timeout < MINIMUM_TIMEOUT) {
		snprintf (local_error_reason, sizeof(local_error_reason),
			"The downcheck timeout parameter (%d ms) may not be less than (%d ms).",
			totem_config->downcheck_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	/*
	 * RRP values validation
	 */
	if (strcmp (totem_config->rrp_mode, "none") &&
		strcmp (totem_config->rrp_mode, "active") &&
		strcmp (totem_config->rrp_mode, "passive")) {
		snprintf (local_error_reason, sizeof(local_error_reason),
			"The RRP mode \"%s\" specified is invalid.  It must be none, active, or passive.\n", totem_config->rrp_mode);
		goto parse_error;
	}
	if (totem_config->rrp_problem_count_timeout == 0) {
		totem_config->rrp_problem_count_timeout = RRP_PROBLEM_COUNT_TIMEOUT;
	}
	if (totem_config->rrp_problem_count_timeout < MINIMUM_TIMEOUT) {
		snprintf (local_error_reason, sizeof(local_error_reason),
			"The RRP problem count timeout parameter (%d ms) may not be less than (%d ms).",
			totem_config->rrp_problem_count_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}
	if (totem_config->rrp_problem_count_threshold == 0) {
		totem_config->rrp_problem_count_threshold = RRP_PROBLEM_COUNT_THRESHOLD_DEFAULT;
	}
	if (totem_config->rrp_problem_count_mcast_threshold == 0) {
		totem_config->rrp_problem_count_mcast_threshold = totem_config->rrp_problem_count_threshold * 10;
	}
	if (totem_config->rrp_problem_count_threshold < RRP_PROBLEM_COUNT_THRESHOLD_MIN) {
		snprintf (local_error_reason, sizeof(local_error_reason),
			"The RRP problem count threshold (%d problem count) may not be less than (%d problem count).",
			totem_config->rrp_problem_count_threshold, RRP_PROBLEM_COUNT_THRESHOLD_MIN);
		goto parse_error;
	}
	if (totem_config->rrp_problem_count_mcast_threshold < RRP_PROBLEM_COUNT_THRESHOLD_MIN) {
		snprintf (local_error_reason, sizeof(local_error_reason),
			"The RRP multicast problem count threshold (%d problem count) may not be less than (%d problem count).",
			totem_config->rrp_problem_count_mcast_threshold, RRP_PROBLEM_COUNT_THRESHOLD_MIN);
		goto parse_error;
	}
	if (totem_config->rrp_token_expired_timeout == 0) {
		totem_config->rrp_token_expired_timeout =
			totem_config->token_retransmit_timeout;
	}

	if (totem_config->rrp_token_expired_timeout < MINIMUM_TIMEOUT) {
		snprintf (local_error_reason, sizeof(local_error_reason),
			"The RRP token expired timeout parameter (%d ms) may not be less than (%d ms).",
			totem_config->rrp_token_expired_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	if (totem_config->rrp_autorecovery_check_timeout == 0) {
		totem_config->rrp_autorecovery_check_timeout = RRP_AUTORECOVERY_CHECK_TIMEOUT;
	}

	if (strcmp (totem_config->rrp_mode, "none") == 0) {
		interface_max = 1;
	}
	if (interface_max < totem_config->interface_count) {
		snprintf (parse_error, sizeof(parse_error),
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

	if ((MESSAGE_QUEUE_MAX) < totem_config->max_messages) {
		snprintf (local_error_reason, sizeof(local_error_reason),
			"The max_messages parameter (%d messages) may not be greater than (%d messages).",
			totem_config->max_messages, MESSAGE_QUEUE_MAX);
		goto parse_error;
	}

	if (totem_config->threads > SEND_THREADS_MAX) {
		totem_config->threads = SEND_THREADS_MAX;
	}
	if (totem_config->secauth == 0) {
		totem_config->threads = 0;
	}
	if (totem_config->net_mtu > FRAME_SIZE_MAX) {
		error_reason = "This net_mtu parameter is greater than the maximum frame size";
		goto parse_error;
	}
	if (totem_config->vsf_type == NULL) {
		totem_config->vsf_type = "none";
	}

	return (0);

parse_error:
	snprintf (error_string_response, sizeof(error_string_response),
		 "parse error in config: %s\n", error_reason);
	*error_string = error_string_response;
	return (-1);
}

static int read_keyfile (
	const char *key_location,
	struct totem_config *totem_config,
	const char **error_string)
{
	int fd;
	int res;
	ssize_t expected_key_len = sizeof (totem_config->private_key);
	int saved_errno;
	char error_str[100];
	const char *error_ptr;

	fd = open (key_location, O_RDONLY);
	if (fd == -1) {
		LOGSYS_STRERROR_R (error_ptr, errno, error_str, sizeof(error_str));
		snprintf (error_string_response, sizeof(error_string_response),
			"Could not open %s: %s\n",
			 key_location, error_ptr);
		goto parse_error;
	}

	res = read (fd, totem_config->private_key, expected_key_len);
	saved_errno = errno;
	close (fd);

	if (res == -1) {
		LOGSYS_STRERROR_R (error_ptr, saved_errno, error_str, sizeof(error_str));
		snprintf (error_string_response, sizeof(error_string_response),
			"Could not read %s: %s\n",
			 key_location, error_ptr);
		goto parse_error;
	}

	totem_config->private_key_len = expected_key_len;

	if (res != expected_key_len) {
		snprintf (error_string_response, sizeof(error_string_response),
			"Could only read %d bits of 1024 bits from %s.\n",
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
	const char **error_string)
{
	int got_key = 0;
	const char *key_location = NULL;
	hdb_handle_t object_totem_handle;
	int res;

	memset (totem_config->private_key, 0, 128);
	totem_config->private_key_len = 128;

	if (totem_config->secauth == 0) {
		return (0);
	}

	res = totem_handle_find (objdb, &object_totem_handle);
	if (res == -1) {
		return (-1);
	}
	/* objdb may store the location of the key file */
	if (!objdb_get_string (objdb,object_totem_handle, "keyfile", &key_location)
	    && key_location) {
		res = read_keyfile(key_location, totem_config, error_string);
		if (res)  {
			goto key_error;
		}
		got_key = 1;
	} else { /* Or the key itself may be in the objdb */
		char *key = NULL;
		size_t key_len;
		res = objdb->object_key_get (object_totem_handle,
			"key",
			strlen ("key"),
			(void *)&key,
			&key_len);

		if (res == 0 && key) {
			if (key_len > sizeof (totem_config->private_key)) {
				goto key_error;
			}
			memcpy(totem_config->private_key, key, key_len);
			totem_config->private_key_len = key_len;
			got_key = 1;
		}
	}

	/* In desperation we read the default filename */
	if (!got_key) {
		const char *filename = getenv("COROSYNC_TOTEM_AUTHKEY_FILE");
		if (!filename)
			filename = COROSYSCONFDIR "/authkey";
		res = read_keyfile(filename, totem_config, error_string);
		if (res)
			goto key_error;

	}

	return (0);

key_error:
	*error_string = error_string_response;
	return (-1);

}

static void totem_key_change_notify(object_change_type_t change_type,
			      hdb_handle_t parent_object_handle,
			      hdb_handle_t object_handle,
			      const void *object_name_pt, size_t object_name_len,
			      const void *key_name_pt, size_t key_len,
			      const void *key_value_pt, size_t key_value_len,
			      void *priv_data_pt)
{
	struct totem_config *totem_config = priv_data_pt;

	if (memcmp(object_name_pt, "totem", object_name_len) == 0)
		totem_volatile_config_read(global_objdb,
					   totem_config,
					   object_handle); // CHECK
}

static void totem_objdb_reload_notify(objdb_reload_notify_type_t type, int flush,
				      void *priv_data_pt)
{
	struct totem_config *totem_config = priv_data_pt;
	hdb_handle_t totem_object_handle;

	if (totem_config == NULL)
	        return;

	/*
	 * A new totem {} key might exist, cancel the
	 * existing notification at the start of reload,
	 * and start a new one on the new object when
	 * it's all settled.
	 */

	if (type == OBJDB_RELOAD_NOTIFY_START) {
		global_objdb->object_track_stop(
			totem_key_change_notify,
			NULL,
			NULL,
			NULL,
			totem_config);
	}

	if (type == OBJDB_RELOAD_NOTIFY_END ||
	    type == OBJDB_RELOAD_NOTIFY_FAILED) {


		if (!totem_handle_find(global_objdb,
				      &totem_object_handle)) {

		        global_objdb->object_track_start(totem_object_handle,
						  1,
						  totem_key_change_notify,
						  NULL, // object_create_notify,
						  NULL, // object_destroy_notify,
						  NULL, // object_reload_notify
						  totem_config); // priv_data
			/*
			 * Reload the configuration
			 */
			totem_volatile_config_read(global_objdb,
						   totem_config,
						   totem_object_handle);

		}
		else {
			log_printf(LOGSYS_LEVEL_ERROR, "totem objdb tracking stopped, cannot find totem{} handle on objdb\n");
		}
	}
}


static void add_totem_config_notification(
	struct objdb_iface_ver0 *objdb,
	struct totem_config *totem_config,
	hdb_handle_t totem_object_handle)
{

	global_objdb = objdb;
	objdb->object_track_start(totem_object_handle,
				  1,
				  totem_key_change_notify,
				  NULL, // object_create_notify,
				  NULL, // object_destroy_notify,
				  NULL, // object_reload_notify
				  totem_config); // priv_data

	/*
	 * Reload notify must be on the parent object
	 */
	objdb->object_track_start(OBJECT_PARENT_HANDLE,
				  1,
				  NULL, // key_change_notify,
				  NULL, // object_create_notify,
				  NULL, // object_destroy_notify,
				  totem_objdb_reload_notify, // object_reload_notify
				  totem_config); // priv_data

}
