/*
 * Copyright (c) 2002-2005 MontaVista Software, Inc.
 * Copyright (c) 2006-2013 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)
 *         Jan Friesse (jfriesse@redhat.com)
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
#include <qb/qbdefs.h>
#include <corosync/totem/totem.h>
#include <corosync/config.h>
#include <corosync/logsys.h>
#include <corosync/icmap.h>

#include "util.h"
#include "totemconfig.h"

#define TOKEN_RETRANSMITS_BEFORE_LOSS_CONST	4
#define TOKEN_TIMEOUT				1000
#define TOKEN_COEFFICIENT			650
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

#define DEFAULT_PORT				5405

static char error_string_response[512];

static void add_totem_config_notification(struct totem_config *totem_config);


/* All the volatile parameters are uint32s, luckily */
static uint32_t *totem_get_param_by_name(struct totem_config *totem_config, const char *param_name)
{
	if (strcmp(param_name, "totem.token") == 0)
		return &totem_config->token_timeout;
	if (strcmp(param_name, "totem.token_retransmit") == 0)
		return &totem_config->token_retransmit_timeout;
	if (strcmp(param_name, "totem.hold") == 0)
		return &totem_config->token_hold_timeout;
	if (strcmp(param_name, "totem.token_retransmits_before_loss_const") == 0)
		return &totem_config->token_retransmits_before_loss_const;
	if (strcmp(param_name, "totem.join") == 0)
		return &totem_config->join_timeout;
	if (strcmp(param_name, "totem.send_join") == 0)
		return &totem_config->send_join_timeout;
	if (strcmp(param_name, "totem.consensus") == 0)
		return &totem_config->consensus_timeout;
	if (strcmp(param_name, "totem.merge") == 0)
		return &totem_config->merge_timeout;
	if (strcmp(param_name, "totem.downcheck") == 0)
		return &totem_config->downcheck_timeout;
	if (strcmp(param_name, "totem.fail_recv_const") == 0)
		return &totem_config->fail_to_recv_const;
	if (strcmp(param_name, "totem.seqno_unchanged_const") == 0)
		return &totem_config->seqno_unchanged_const;
	if (strcmp(param_name, "totem.rrp_token_expired_timeout") == 0)
		return &totem_config->rrp_token_expired_timeout;
	if (strcmp(param_name, "totem.rrp_problem_count_timeout") == 0)
		return &totem_config->rrp_problem_count_timeout;
	if (strcmp(param_name, "totem.rrp_problem_count_threshold") == 0)
		return &totem_config->rrp_problem_count_threshold;
	if (strcmp(param_name, "totem.rrp_problem_count_mcast_threshold") == 0)
		return &totem_config->rrp_problem_count_mcast_threshold;
	if (strcmp(param_name, "totem.rrp_autorecovery_check_timeout") == 0)
		return &totem_config->rrp_autorecovery_check_timeout;
	if (strcmp(param_name, "totem.heartbeat_failures_allowed") == 0)
		return &totem_config->heartbeat_failures_allowed;
	if (strcmp(param_name, "totem.max_network_delay") == 0)
		return &totem_config->max_network_delay;
	if (strcmp(param_name, "totem.window_size") == 0)
		return &totem_config->window_size;
	if (strcmp(param_name, "totem.max_messages") == 0)
		return &totem_config->max_messages;
	if (strcmp(param_name, "totem.miss_count_const") == 0)
		return &totem_config->miss_count_const;

	return NULL;
}

/*
 * Read key_name from icmap. If key is not found or key_name == delete_key or if allow_zero is false
 * and readed value is zero, default value is used and stored into totem_config.
 */
static void totem_volatile_config_set_value (struct totem_config *totem_config,
	const char *key_name, const char *deleted_key, unsigned int default_value,
	int allow_zero_value)
{
	char runtime_key_name[ICMAP_KEYNAME_MAXLEN];

	if (icmap_get_uint32(key_name, totem_get_param_by_name(totem_config, key_name)) != CS_OK ||
	    (deleted_key != NULL && strcmp(deleted_key, key_name) == 0) ||
	    (!allow_zero_value && *totem_get_param_by_name(totem_config, key_name) == 0)) {
		*totem_get_param_by_name(totem_config, key_name) = default_value;
	}

	/*
	 * Store totem_config value to cmap runtime section
	 */
	if (strlen("runtime.config.") + strlen(key_name) >= ICMAP_KEYNAME_MAXLEN) {
		/*
		 * This shouldn't happen
		 */
		return ;
	}

	strcpy(runtime_key_name, "runtime.config.");
	strcat(runtime_key_name, key_name);

	icmap_set_uint32(runtime_key_name, *totem_get_param_by_name(totem_config, key_name));
}


/*
 * Read and validate config values from cmap and store them into totem_config. If key doesn't exists,
 * default value is stored. deleted_key is name of key beeing processed by delete operation
 * from cmap. It is considered as non existing even if it can be read. Can be NULL.
 */
static void totem_volatile_config_read (struct totem_config *totem_config, const char *deleted_key)
{
	uint32_t u32;

	totem_volatile_config_set_value(totem_config, "totem.token_retransmits_before_loss_const", deleted_key,
	    TOKEN_RETRANSMITS_BEFORE_LOSS_CONST, 0);

	totem_volatile_config_set_value(totem_config, "totem.token", deleted_key, TOKEN_TIMEOUT, 0);

	if (totem_config->interface_count > 0 && totem_config->interfaces[0].member_count > 2) {
		u32 = TOKEN_COEFFICIENT;
		icmap_get_uint32("totem.token_coefficient", &u32);
		totem_config->token_timeout += (totem_config->interfaces[0].member_count - 2) * u32;

		/*
		 * Store totem_config value to cmap runtime section
		 */
		icmap_set_uint32("runtime.config.totem.token", totem_config->token_timeout);
	}

	totem_volatile_config_set_value(totem_config, "totem.max_network_delay", deleted_key, MAX_NETWORK_DELAY, 0);

	totem_volatile_config_set_value(totem_config, "totem.window_size", deleted_key, WINDOW_SIZE, 0);

	totem_volatile_config_set_value(totem_config, "totem.max_messages", deleted_key, MAX_MESSAGES, 0);

	totem_volatile_config_set_value(totem_config, "totem.miss_count_const", deleted_key, MISS_COUNT_CONST, 0);

	totem_volatile_config_set_value(totem_config, "totem.token_retransmit", deleted_key,
	   (int)(totem_config->token_timeout / (totem_config->token_retransmits_before_loss_const + 0.2)), 0);

	totem_volatile_config_set_value(totem_config, "totem.hold", deleted_key,
	    (int)(totem_config->token_retransmit_timeout * 0.8 - (1000/HZ)), 0);

	totem_volatile_config_set_value(totem_config, "totem.join", deleted_key, JOIN_TIMEOUT, 0);

	totem_volatile_config_set_value(totem_config, "totem.consensus", deleted_key,
	    (int)(float)(1.2 * totem_config->token_timeout), 0);

	totem_volatile_config_set_value(totem_config, "totem.merge", deleted_key, MERGE_TIMEOUT, 0);

	totem_volatile_config_set_value(totem_config, "totem.downcheck", deleted_key, DOWNCHECK_TIMEOUT, 0);

	totem_volatile_config_set_value(totem_config, "totem.fail_recv_const", deleted_key, FAIL_TO_RECV_CONST, 0);

	totem_volatile_config_set_value(totem_config, "totem.seqno_unchanged_const", deleted_key,
	    SEQNO_UNCHANGED_CONST, 0);

	totem_volatile_config_set_value(totem_config, "totem.send_join", deleted_key, 0, 1);

	totem_volatile_config_set_value(totem_config, "totem.rrp_problem_count_timeout", deleted_key,
	    RRP_PROBLEM_COUNT_TIMEOUT, 0);

	totem_volatile_config_set_value(totem_config, "totem.rrp_problem_count_threshold", deleted_key,
	    RRP_PROBLEM_COUNT_THRESHOLD_DEFAULT, 0);

	totem_volatile_config_set_value(totem_config, "totem.rrp_problem_count_mcast_threshold", deleted_key,
	    totem_config->rrp_problem_count_threshold * 10, 0);

	totem_volatile_config_set_value(totem_config, "totem.rrp_token_expired_timeout", deleted_key,
	    totem_config->token_retransmit_timeout, 0);

	totem_volatile_config_set_value(totem_config, "totem.rrp_autorecovery_check_timeout", deleted_key,
	    RRP_AUTORECOVERY_CHECK_TIMEOUT, 0);

	totem_volatile_config_set_value(totem_config, "totem.heartbeat_failures_allowed", deleted_key, 0, 1);
}

static int totem_volatile_config_validate (
	struct totem_config *totem_config,
	const char **error_string)
{
	static char local_error_reason[512];
	const char *error_reason = local_error_reason;

	if (totem_config->max_network_delay < MINIMUM_TIMEOUT) {
		snprintf (local_error_reason, sizeof(local_error_reason),
			"The max_network_delay parameter (%d ms) may not be less than (%d ms).",
			totem_config->max_network_delay, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	if (totem_config->token_timeout < MINIMUM_TIMEOUT) {
		snprintf (local_error_reason, sizeof(local_error_reason),
			"The token timeout parameter (%d ms) may not be less than (%d ms).",
			totem_config->token_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	if (totem_config->token_retransmit_timeout < MINIMUM_TIMEOUT) {
		snprintf (local_error_reason, sizeof(local_error_reason),
			"The token retransmit timeout parameter (%d ms) may not be less than (%d ms).",
			totem_config->token_retransmit_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	if (totem_config->token_hold_timeout < MINIMUM_TIMEOUT) {
		snprintf (local_error_reason, sizeof(local_error_reason),
			"The token hold timeout parameter (%d ms) may not be less than (%d ms).",
			totem_config->token_hold_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	if (totem_config->join_timeout < MINIMUM_TIMEOUT) {
		snprintf (local_error_reason, sizeof(local_error_reason),
			"The join timeout parameter (%d ms) may not be less than (%d ms).",
			totem_config->join_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
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

	if (totem_config->merge_timeout < MINIMUM_TIMEOUT) {
		snprintf (local_error_reason, sizeof(local_error_reason),
			"The merge timeout parameter (%d ms) may not be less than (%d ms).",
			totem_config->merge_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	if (totem_config->downcheck_timeout < MINIMUM_TIMEOUT) {
		snprintf (local_error_reason, sizeof(local_error_reason),
			"The downcheck timeout parameter (%d ms) may not be less than (%d ms).",
			totem_config->downcheck_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	if (totem_config->rrp_problem_count_timeout < MINIMUM_TIMEOUT) {
		snprintf (local_error_reason, sizeof(local_error_reason),
			"The RRP problem count timeout parameter (%d ms) may not be less than (%d ms).",
			totem_config->rrp_problem_count_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
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

	if (totem_config->rrp_token_expired_timeout < MINIMUM_TIMEOUT) {
		snprintf (local_error_reason, sizeof(local_error_reason),
			"The RRP token expired timeout parameter (%d ms) may not be less than (%d ms).",
			totem_config->rrp_token_expired_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	return 0;

parse_error:
	snprintf (error_string_response, sizeof(error_string_response),
		 "parse error in config: %s\n", error_reason);
	*error_string = error_string_response;
	return (-1);

}

static int totem_get_crypto(struct totem_config *totem_config)
{
	char *str;
	const char *tmp_cipher;
	const char *tmp_hash;

	tmp_hash = "sha1";
	tmp_cipher = "aes256";

	if (icmap_get_string("totem.secauth", &str) == CS_OK) {
		if (strcmp (str, "off") == 0) {
			tmp_hash = "none";
			tmp_cipher = "none";
		}
		free(str);
	}

	if (icmap_get_string("totem.crypto_cipher", &str) == CS_OK) {
		if (strcmp(str, "none") == 0) {
			tmp_cipher = "none";
		}
		if (strcmp(str, "aes256") == 0) {
			tmp_cipher = "aes256";
		}
		if (strcmp(str, "aes192") == 0) {
			tmp_cipher = "aes192";
		}
		if (strcmp(str, "aes128") == 0) {
			tmp_cipher = "aes128";
		}
		if (strcmp(str, "3des") == 0) {
			tmp_cipher = "3des";
		}
		free(str);
	}

	if (icmap_get_string("totem.crypto_hash", &str) == CS_OK) {
		if (strcmp(str, "none") == 0) {
			tmp_hash = "none";
		}
		if (strcmp(str, "md5") == 0) {
			tmp_hash = "md5";
		}
		if (strcmp(str, "sha1") == 0) {
			tmp_hash = "sha1";
		}
		if (strcmp(str, "sha256") == 0) {
			tmp_hash = "sha256";
		}
		if (strcmp(str, "sha384") == 0) {
			tmp_hash = "sha384";
		}
		if (strcmp(str, "sha512") == 0) {
			tmp_hash = "sha512";
		}
		free(str);
	}

	if ((strcmp(tmp_cipher, "none") != 0) &&
	    (strcmp(tmp_hash, "none") == 0)) {
		return -1;
	}

	free(totem_config->crypto_cipher_type);
	free(totem_config->crypto_hash_type);

	totem_config->crypto_cipher_type = strdup(tmp_cipher);
	totem_config->crypto_hash_type = strdup(tmp_hash);

	return 0;
}

static int totem_config_get_ip_version(void)
{
	int res;
	char *str;

	res = AF_INET;
	if (icmap_get_string("totem.ip_version", &str) == CS_OK) {
		if (strcmp(str, "ipv4") == 0) {
			res = AF_INET;
		}
		if (strcmp(str, "ipv6") == 0) {
			res = AF_INET6;
		}
		free(str);
	}

	return (res);
}

static uint16_t generate_cluster_id (const char *cluster_name)
{
	int i;
	int value = 0;

	for (i = 0; i < strlen(cluster_name); i++) {
		value <<= 1;
		value += cluster_name[i];
	}

	return (value & 0xFFFF);
}

static int get_cluster_mcast_addr (
		const char *cluster_name,
		unsigned int ringnumber,
		int ip_version,
		struct totem_ip_address *res)
{
	uint16_t clusterid;
	char addr[INET6_ADDRSTRLEN + 1];
	int err;

	if (cluster_name == NULL) {
		return (-1);
	}

	clusterid = generate_cluster_id(cluster_name) + ringnumber;
	memset (res, 0, sizeof(*res));

	switch (ip_version) {
	case AF_INET:
		snprintf(addr, sizeof(addr), "239.192.%d.%d", clusterid >> 8, clusterid % 0xFF);
		break;
	case AF_INET6:
		snprintf(addr, sizeof(addr), "ff15::%x", clusterid);
		break;
	default:
		/*
		 * Unknown family
		 */
		return (-1);
	}

	err = totemip_parse (res, addr, ip_version);

	return (err);
}

static unsigned int generate_nodeid_for_duplicate_test(
	struct totem_config *totem_config,
	char *addr)
{
	unsigned int nodeid;
	struct totem_ip_address totemip;

	/* AF_INET hard-coded here because auto-generated nodeids
	   are only for IPv4 */
	if (totemip_parse(&totemip, addr, AF_INET) != 0)
		return -1;

	memcpy (&nodeid, &totemip.addr, sizeof (unsigned int));

#if __BYTE_ORDER == __LITTLE_ENDIAN
	nodeid = swab32 (nodeid);
#endif

	if (totem_config->clear_node_high_bit) {
		nodeid &= 0x7FFFFFFF;
	}
	return nodeid;
}

static int check_for_duplicate_nodeids(
	struct totem_config *totem_config,
	const char **error_string)
{
	icmap_iter_t iter;
	icmap_iter_t subiter;
	const char *iter_key;
	int res = 0;
	int retval = 0;
	char tmp_key[ICMAP_KEYNAME_MAXLEN];
	char *ring0_addr=NULL;
	char *ring0_addr1=NULL;
	unsigned int node_pos;
	unsigned int node_pos1;
	unsigned int nodeid;
	unsigned int nodeid1;
	int autogenerated;

	iter = icmap_iter_init("nodelist.node.");
	while ((iter_key = icmap_iter_next(iter, NULL, NULL)) != NULL) {
		res = sscanf(iter_key, "nodelist.node.%u.%s", &node_pos, tmp_key);
		if (res != 2) {
			continue;
		}

		if (strcmp(tmp_key, "ring0_addr") != 0) {
			continue;
		}

		snprintf(tmp_key, ICMAP_KEYNAME_MAXLEN, "nodelist.node.%u.nodeid", node_pos);
		autogenerated = 0;
		if (icmap_get_uint32(tmp_key, &nodeid) != CS_OK) {

			snprintf(tmp_key, ICMAP_KEYNAME_MAXLEN, "nodelist.node.%u.ring0_addr", node_pos);
			if (icmap_get_string(tmp_key, &ring0_addr) != CS_OK) {
				continue;
			}

			/* Generate nodeid so we can check that auto-generated nodeids don't clash either */
			nodeid = generate_nodeid_for_duplicate_test(totem_config, ring0_addr);
			if (nodeid == -1) {
				continue;
			}
			autogenerated = 1;
		}

		node_pos1 = 0;
		subiter = icmap_iter_init("nodelist.node.");
		while (((iter_key = icmap_iter_next(subiter, NULL, NULL)) != NULL) && (node_pos1 < node_pos)) {
			res = sscanf(iter_key, "nodelist.node.%u.%s", &node_pos1, tmp_key);
			if ((res != 2) || (node_pos1 >= node_pos)) {
				continue;
			}

			if (strcmp(tmp_key, "ring0_addr") != 0) {
				continue;
			}

			snprintf(tmp_key, ICMAP_KEYNAME_MAXLEN, "nodelist.node.%u.nodeid", node_pos1);
			if (icmap_get_uint32(tmp_key, &nodeid1) != CS_OK) {

				snprintf(tmp_key, ICMAP_KEYNAME_MAXLEN, "nodelist.node.%u.ring0_addr", node_pos1);
				if (icmap_get_string(tmp_key, &ring0_addr1) != CS_OK) {
					continue;
				}
				nodeid1 = generate_nodeid_for_duplicate_test(totem_config, ring0_addr1);
				if (nodeid1 == -1) {
					continue;
				}
			}

			if (nodeid == nodeid1) {
				retval = -1;
				snprintf (error_string_response, sizeof(error_string_response),
					  "Nodeid %u%s%s%s appears twice in corosync.conf", nodeid,
					  autogenerated?"(autogenerated from ":"",
					  autogenerated?ring0_addr:"",
					  autogenerated?")":"");
				log_printf (LOGSYS_LEVEL_ERROR, error_string_response);
				*error_string = error_string_response;
				break;
			}
		}
		icmap_iter_finalize(subiter);
	}
	icmap_iter_finalize(iter);
	return retval;
}


static int find_local_node_in_nodelist(struct totem_config *totem_config)
{
	icmap_iter_t iter;
	const char *iter_key;
	int res = 0;
	unsigned int node_pos;
	int local_node_pos = -1;
	struct totem_ip_address bind_addr;
	int interface_up, interface_num;
	char tmp_key[ICMAP_KEYNAME_MAXLEN];
	char *node_addr_str;
	struct totem_ip_address node_addr;

	res = totemip_iface_check(&totem_config->interfaces[0].bindnet,
		&bind_addr, &interface_up, &interface_num,
		totem_config->clear_node_high_bit);
	if (res == -1) {
		return (-1);
	}

	iter = icmap_iter_init("nodelist.node.");
	while ((iter_key = icmap_iter_next(iter, NULL, NULL)) != NULL) {
		res = sscanf(iter_key, "nodelist.node.%u.%s", &node_pos, tmp_key);
		if (res != 2) {
			continue;
		}

		if (strcmp(tmp_key, "ring0_addr") != 0) {
			continue;
		}

		snprintf(tmp_key, ICMAP_KEYNAME_MAXLEN, "nodelist.node.%u.ring0_addr", node_pos);
		if (icmap_get_string(tmp_key, &node_addr_str) != CS_OK) {
			continue;
		}

		res = totemip_parse (&node_addr, node_addr_str, totem_config->ip_version);
		free(node_addr_str);
		if (res == -1) {
			continue ;
		}

		if (totemip_equal(&bind_addr, &node_addr)) {
			local_node_pos = node_pos;
		}
	}
	icmap_iter_finalize(iter);

	return (local_node_pos);
}

/*
 * Compute difference between two set of totem interface arrays. set1 and set2
 * are changed so for same ring, ip existing in both set1 and set2 are cleared
 * (set to 0), and ips which are only in set1 or set2 remains untouched.
 * totempg_node_add/remove is called.
 */
static void compute_interfaces_diff(int interface_count,
	struct totem_interface *set1,
	struct totem_interface *set2)
{
	int ring_no, set1_pos, set2_pos;
	struct totem_ip_address empty_ip_address;

	memset(&empty_ip_address, 0, sizeof(empty_ip_address));

	for (ring_no = 0; ring_no < interface_count; ring_no++) {
		for (set1_pos = 0; set1_pos < set1[ring_no].member_count; set1_pos++) {
			for (set2_pos = 0; set2_pos < set2[ring_no].member_count; set2_pos++) {
				/*
				 * For current ring_no remove all set1 items existing
				 * in set2
				 */
				if (memcmp(&set1[ring_no].member_list[set1_pos],
				    &set2[ring_no].member_list[set2_pos],
				    sizeof(struct totem_ip_address)) == 0) {
					memset(&set1[ring_no].member_list[set1_pos], 0,
					    sizeof(struct totem_ip_address));
					memset(&set2[ring_no].member_list[set2_pos], 0,
					    sizeof(struct totem_ip_address));
				}
			}
		}
	}

	for (ring_no = 0; ring_no < interface_count; ring_no++) {
		for (set1_pos = 0; set1_pos < set1[ring_no].member_count; set1_pos++) {
			/*
			 * All items which remained in set1 doesn't exists in set2 any longer so
			 * node has to be removed.
			 */
			if (memcmp(&set1[ring_no].member_list[set1_pos], &empty_ip_address, sizeof(empty_ip_address)) != 0) {
				log_printf(LOGSYS_LEVEL_DEBUG,
					"removing dynamic member %s for ring %u",
					totemip_print(&set1[ring_no].member_list[set1_pos]),
					ring_no);

				totempg_member_remove(&set1[ring_no].member_list[set1_pos], ring_no);
			}
		}
		for (set2_pos = 0; set2_pos < set2[ring_no].member_count; set2_pos++) {
			/*
			 * All items which remained in set2 doesn't existed in set1 so this is no node
			 * and has to be added.
			 */
			if (memcmp(&set2[ring_no].member_list[set2_pos], &empty_ip_address, sizeof(empty_ip_address)) != 0) {
				log_printf(LOGSYS_LEVEL_DEBUG,
					"adding dynamic member %s for ring %u",
					totemip_print(&set2[ring_no].member_list[set2_pos]),
					ring_no);

				totempg_member_add(&set2[ring_no].member_list[set2_pos], ring_no);
			}
		}
	}
}

static void put_nodelist_members_to_config(struct totem_config *totem_config, int reload)
{
	icmap_iter_t iter, iter2;
	const char *iter_key, *iter_key2;
	int res = 0;
	unsigned int node_pos;
	char tmp_key[ICMAP_KEYNAME_MAXLEN];
	char tmp_key2[ICMAP_KEYNAME_MAXLEN];
	char *node_addr_str;
	int member_count;
	unsigned int ringnumber = 0;
	int i, j;
	struct totem_interface *orig_interfaces = NULL;
	struct totem_interface *new_interfaces = NULL;

	if (reload) {
		/*
		 * We need to compute diff only for reload. Also for initial configuration
		 * not all totem structures are initialized so corosync will crash during
		 * member_add/remove
		 */
		orig_interfaces = malloc (sizeof (struct totem_interface) * INTERFACE_MAX);
		assert(orig_interfaces != NULL);
		new_interfaces = malloc (sizeof (struct totem_interface) * INTERFACE_MAX);
		assert(new_interfaces != NULL);

		memcpy(orig_interfaces, totem_config->interfaces, sizeof (struct totem_interface) * INTERFACE_MAX);
	}

	/* Clear out nodelist so we can put the new one in if needed */
	for (i = 0; i < totem_config->interface_count; i++) {
		for (j = 0; j < PROCESSOR_COUNT_MAX; j++) {
			memset(&totem_config->interfaces[i].member_list[j], 0, sizeof(struct totem_ip_address));
		}
		totem_config->interfaces[i].member_count = 0;
	}

	iter = icmap_iter_init("nodelist.node.");
	while ((iter_key = icmap_iter_next(iter, NULL, NULL)) != NULL) {
		res = sscanf(iter_key, "nodelist.node.%u.%s", &node_pos, tmp_key);
		if (res != 2) {
			continue;
		}

		if (strcmp(tmp_key, "ring0_addr") != 0) {
			continue;
		}

		snprintf(tmp_key, ICMAP_KEYNAME_MAXLEN, "nodelist.node.%u.", node_pos);
		iter2 = icmap_iter_init(tmp_key);
		while ((iter_key2 = icmap_iter_next(iter2, NULL, NULL)) != NULL) {
			res = sscanf(iter_key2, "nodelist.node.%u.ring%u%s", &node_pos, &ringnumber, tmp_key2);
			if (res != 3 || strcmp(tmp_key2, "_addr") != 0) {
				continue;
			}

			if (icmap_get_string(iter_key2, &node_addr_str) != CS_OK) {
				continue;
			}

			member_count = totem_config->interfaces[ringnumber].member_count;

			res = totemip_parse(&totem_config->interfaces[ringnumber].member_list[member_count],
						node_addr_str, totem_config->ip_version);
			if (res != -1) {
				totem_config->interfaces[ringnumber].member_count++;
			}
			free(node_addr_str);
		}

		icmap_iter_finalize(iter2);
	}

	icmap_iter_finalize(iter);

	if (reload) {
		memcpy(new_interfaces, totem_config->interfaces, sizeof (struct totem_interface) * INTERFACE_MAX);

		compute_interfaces_diff(totem_config->interface_count, orig_interfaces, new_interfaces);

		free(new_interfaces);
		free(orig_interfaces);
	}
}

static void nodelist_dynamic_notify(
	int32_t event,
	const char *key_name,
	struct icmap_notify_value new_val,
	struct icmap_notify_value old_val,
	void *user_data)
{
	int res;
	unsigned int ring_no;
	unsigned int member_no;
	char tmp_str[ICMAP_KEYNAME_MAXLEN];
	uint8_t reloading;
	struct totem_config *totem_config = (struct totem_config *)user_data;

	/*
	* If a full reload is in progress then don't do anything until it's done and
	* can reconfigure it all atomically
	*/
	if (icmap_get_uint8("config.totemconfig_reload_in_progress", &reloading) == CS_OK && reloading) {
		return ;
	}

	res = sscanf(key_name, "nodelist.node.%u.ring%u%s", &member_no, &ring_no, tmp_str);
	if (res != 3)
		return ;

	if (strcmp(tmp_str, "_addr") != 0) {
		return;
	}

	put_nodelist_members_to_config(totem_config, 1);
}


/*
 * Tries to find node (node_pos) in config nodelist which address matches any
 * local interface. Address can be stored in ring0_addr or if ipaddr_key_prefix is not NULL
 * key with prefix ipaddr_key is used (there can be multiuple of them)
 * This function differs  * from find_local_node_in_nodelist because it doesn't need bindnetaddr,
 * but doesn't work when bind addr is network address (so IP must be exact
 * match).
 *
 * Returns 1 on success (address was found, node_pos is then correctly set) or 0 on failure.
 */
int totem_config_find_local_addr_in_nodelist(const char *ipaddr_key_prefix, unsigned int *node_pos)
{
	struct list_head addrs;
	struct totem_ip_if_address *if_addr;
	icmap_iter_t iter, iter2;
	const char *iter_key, *iter_key2;
	struct list_head *list;
	const char *ipaddr_key;
	int ip_version;
	struct totem_ip_address node_addr;
	char *node_addr_str;
	int node_found = 0;
	int res = 0;
	char tmp_key[ICMAP_KEYNAME_MAXLEN];

	if (totemip_getifaddrs(&addrs) == -1) {
		return 0;
	}

	ip_version = totem_config_get_ip_version();

	iter = icmap_iter_init("nodelist.node.");

	while ((iter_key = icmap_iter_next(iter, NULL, NULL)) != NULL) {
		res = sscanf(iter_key, "nodelist.node.%u.%s", node_pos, tmp_key);
		if (res != 2) {
			continue;
		}

		if (strcmp(tmp_key, "ring0_addr") != 0) {
			continue;
		}

		if (icmap_get_string(iter_key, &node_addr_str) != CS_OK) {
			continue ;
		}

		free(node_addr_str);

		/*
		 * ring0_addr found -> let's iterate thru ipaddr_key_prefix
		 */
		snprintf(tmp_key, sizeof(tmp_key), "nodelist.node.%u.%s", *node_pos,
		    (ipaddr_key_prefix != NULL ? ipaddr_key_prefix : "ring0_addr"));

		iter2 = icmap_iter_init(tmp_key);
		while ((iter_key2 = icmap_iter_next(iter2, NULL, NULL)) != NULL) {
			/*
			 * ring0_addr must be exact match, not prefix
			 */
			ipaddr_key = (ipaddr_key_prefix != NULL ? iter_key2 : tmp_key);
			if (icmap_get_string(ipaddr_key, &node_addr_str) != CS_OK) {
				continue ;
			}

			if (totemip_parse(&node_addr, node_addr_str, ip_version) == -1) {
				free(node_addr_str);
				continue ;
			}
			free(node_addr_str);

			/*
			 * Try to match ip with if_addrs
			 */
			node_found = 0;
			for (list = addrs.next; list != &addrs; list = list->next) {
				if_addr = list_entry(list, struct totem_ip_if_address, list);

				if (totemip_equal(&node_addr, &if_addr->ip_addr)) {
					node_found = 1;
					break;
				}
			}

			if (node_found) {
				break ;
			}
		}

		icmap_iter_finalize(iter2);

		if (node_found) {
			break ;
		}
	}

	icmap_iter_finalize(iter);
	totemip_freeifaddrs(&addrs);

	return (node_found);
}

static void config_convert_nodelist_to_interface(struct totem_config *totem_config)
{
	int res = 0;
	unsigned int node_pos;
	char tmp_key[ICMAP_KEYNAME_MAXLEN];
	char tmp_key2[ICMAP_KEYNAME_MAXLEN];
	char *node_addr_str;
	unsigned int ringnumber = 0;
	icmap_iter_t iter;
	const char *iter_key;

	if (totem_config_find_local_addr_in_nodelist(NULL, &node_pos)) {
		/*
		 * We found node, so create interface section
		 */
		snprintf(tmp_key, ICMAP_KEYNAME_MAXLEN, "nodelist.node.%u.", node_pos);
		iter = icmap_iter_init(tmp_key);
		while ((iter_key = icmap_iter_next(iter, NULL, NULL)) != NULL) {
			res = sscanf(iter_key, "nodelist.node.%u.ring%u%s", &node_pos, &ringnumber, tmp_key2);
			if (res != 3 || strcmp(tmp_key2, "_addr") != 0) {
				continue ;
			}

			if (icmap_get_string(iter_key, &node_addr_str) != CS_OK) {
				continue;
			}

			snprintf(tmp_key2, ICMAP_KEYNAME_MAXLEN, "totem.interface.%u.bindnetaddr", ringnumber);
			icmap_set_string(tmp_key2, node_addr_str);
			free(node_addr_str);
		}
		icmap_iter_finalize(iter);
	}
}


extern int totem_config_read (
	struct totem_config *totem_config,
	const char **error_string,
	uint64_t *warnings)
{
	int res = 0;
	char *str, *ring0_addr_str;
	unsigned int ringnumber = 0;
	int member_count = 0;
	icmap_iter_t iter, member_iter;
	const char *iter_key;
	const char *member_iter_key;
	char ringnumber_key[ICMAP_KEYNAME_MAXLEN];
	char tmp_key[ICMAP_KEYNAME_MAXLEN];
	uint8_t u8;
	uint16_t u16;
	char *cluster_name = NULL;
	int i;
	int local_node_pos;
	int nodeid_set;

	*warnings = 0;

	memset (totem_config, 0, sizeof (struct totem_config));
	totem_config->interfaces = malloc (sizeof (struct totem_interface) * INTERFACE_MAX);
	if (totem_config->interfaces == 0) {
		*error_string = "Out of memory trying to allocate ethernet interface storage area";
		return -1;
	}

	memset (totem_config->interfaces, 0,
		sizeof (struct totem_interface) * INTERFACE_MAX);

	strcpy (totem_config->rrp_mode, "none");

	icmap_get_uint32("totem.version", (uint32_t *)&totem_config->version);

	if (totem_get_crypto(totem_config) != 0) {
		*error_string = "crypto_cipher requires crypto_hash with value other than none";
		return -1;
	}

	if (icmap_get_string("totem.rrp_mode", &str) == CS_OK) {
		if (strlen(str) >= TOTEM_RRP_MODE_BYTES) {
			*error_string = "totem.rrp_mode is too long";
			free(str);

			return -1;
		}
		strcpy (totem_config->rrp_mode, str);
		free(str);
	}

	icmap_get_uint32("totem.nodeid", &totem_config->node_id);

	totem_config->clear_node_high_bit = 0;
	if (icmap_get_string("totem.clear_node_high_bit", &str) == CS_OK) {
		if (strcmp (str, "yes") == 0) {
			totem_config->clear_node_high_bit = 1;
		}
		free(str);
	}

	icmap_get_uint32("totem.threads", &totem_config->threads);

	icmap_get_uint32("totem.netmtu", &totem_config->net_mtu);

	if (icmap_get_string("totem.cluster_name", &cluster_name) != CS_OK) {
		cluster_name = NULL;
	}

	totem_config->ip_version = totem_config_get_ip_version();

	if (icmap_get_string("totem.interface.0.bindnetaddr", &str) != CS_OK) {
		/*
		 * We were not able to find ring 0 bindnet addr. Try to use nodelist informations
		 */
		config_convert_nodelist_to_interface(totem_config);
	} else {
		if (icmap_get_string("nodelist.node.0.ring0_addr", &ring0_addr_str) == CS_OK) {
			/*
			 * Both bindnetaddr and ring0_addr are set.
			 * Log warning information, and use nodelist instead
			 */
			*warnings |= TOTEM_CONFIG_BINDNETADDR_NODELIST_SET;

			config_convert_nodelist_to_interface(totem_config);

			free(ring0_addr_str);
		}

		free(str);
	}

	/*
	 * Broadcast option is global but set in interface section,
	 * so reset before processing interfaces.
	 */
	totem_config->broadcast_use = 0;

	iter = icmap_iter_init("totem.interface.");
	while ((iter_key = icmap_iter_next(iter, NULL, NULL)) != NULL) {
		res = sscanf(iter_key, "totem.interface.%[^.].%s", ringnumber_key, tmp_key);
		if (res != 2) {
			continue;
		}

		if (strcmp(tmp_key, "bindnetaddr") != 0) {
			continue;
		}

		member_count = 0;

		ringnumber = atoi(ringnumber_key);

		if (ringnumber >= INTERFACE_MAX) {
			free(cluster_name);

			snprintf (error_string_response, sizeof(error_string_response),
			    "parse error in config: interface ring number %u is bigger than allowed maximum %u\n",
			    ringnumber, INTERFACE_MAX - 1);

			*error_string = error_string_response;
			return -1;
		}

		/*
		 * Get the bind net address
		 */
		if (icmap_get_string(iter_key, &str) == CS_OK) {
			res = totemip_parse (&totem_config->interfaces[ringnumber].bindnet, str,
			    totem_config->ip_version);
			free(str);
		}

		/*
		 * Get interface multicast address
		 */
		snprintf(tmp_key, ICMAP_KEYNAME_MAXLEN, "totem.interface.%u.mcastaddr", ringnumber);
		if (icmap_get_string(tmp_key, &str) == CS_OK) {
			res = totemip_parse (&totem_config->interfaces[ringnumber].mcast_addr, str, totem_config->ip_version);
			free(str);
		} else {
			/*
			 * User not specified address -> autogenerate one from cluster_name key
			 * (if available). Return code is intentionally ignored, because
			 * udpu doesn't need mcastaddr and validity of mcastaddr for udp is
			 * checked later anyway.
			 */
			(void)get_cluster_mcast_addr (cluster_name,
					ringnumber,
					totem_config->ip_version,
					&totem_config->interfaces[ringnumber].mcast_addr);
		}

		snprintf(tmp_key, ICMAP_KEYNAME_MAXLEN, "totem.interface.%u.broadcast", ringnumber);
		if (icmap_get_string(tmp_key, &str) == CS_OK) {
			if (strcmp (str, "yes") == 0) {
				totem_config->broadcast_use = 1;
			}
			free(str);
		}

		/*
		 * Get mcast port
		 */
		snprintf(tmp_key, ICMAP_KEYNAME_MAXLEN, "totem.interface.%u.mcastport", ringnumber);
		if (icmap_get_uint16(tmp_key, &totem_config->interfaces[ringnumber].ip_port) != CS_OK) {
			if (totem_config->broadcast_use) {
				totem_config->interfaces[ringnumber].ip_port = DEFAULT_PORT + (2 * ringnumber);
			} else {
				totem_config->interfaces[ringnumber].ip_port = DEFAULT_PORT;
			}
		}

		/*
		 * Get the TTL
		 */
		totem_config->interfaces[ringnumber].ttl = 1;

		snprintf(tmp_key, ICMAP_KEYNAME_MAXLEN, "totem.interface.%u.ttl", ringnumber);

		if (icmap_get_uint8(tmp_key, &u8) == CS_OK) {
			totem_config->interfaces[ringnumber].ttl = u8;
		}

		snprintf(tmp_key, ICMAP_KEYNAME_MAXLEN, "totem.interface.%u.member.", ringnumber);
		member_iter = icmap_iter_init(tmp_key);
		while ((member_iter_key = icmap_iter_next(member_iter, NULL, NULL)) != NULL) {
			if (member_count == 0) {
				if (icmap_get_string("nodelist.node.0.ring0_addr", &str) == CS_OK) {
					free(str);
					*warnings |= TOTEM_CONFIG_WARNING_MEMBERS_IGNORED;
					break;
				} else {
					*warnings |= TOTEM_CONFIG_WARNING_MEMBERS_DEPRECATED;
				}
			}

			if (icmap_get_string(member_iter_key, &str) == CS_OK) {
				res = totemip_parse (&totem_config->interfaces[ringnumber].member_list[member_count++],
						str, totem_config->ip_version);
			}
		}
		icmap_iter_finalize(member_iter);

		totem_config->interfaces[ringnumber].member_count = member_count;
		totem_config->interface_count++;
	}
	icmap_iter_finalize(iter);

	/*
	 * Use broadcast is global, so if set, make sure to fill mcast addr correctly
	 */
	if (totem_config->broadcast_use) {
		for (ringnumber = 0; ringnumber < totem_config->interface_count; ringnumber++) {
			totemip_parse (&totem_config->interfaces[ringnumber].mcast_addr,
				"255.255.255.255", 0);
		}
	}

	/*
	 * Store automatically generated items back to icmap
	 */
	for (i = 0; i < totem_config->interface_count; i++) {
		snprintf(tmp_key, ICMAP_KEYNAME_MAXLEN, "totem.interface.%u.mcastaddr", i);
		if (icmap_get_string(tmp_key, &str) == CS_OK) {
			free(str);
		} else {
			str = (char *)totemip_print(&totem_config->interfaces[i].mcast_addr);
			icmap_set_string(tmp_key, str);
		}

		snprintf(tmp_key, ICMAP_KEYNAME_MAXLEN, "totem.interface.%u.mcastport", i);
		if (icmap_get_uint16(tmp_key, &u16) != CS_OK) {
			icmap_set_uint16(tmp_key, totem_config->interfaces[i].ip_port);
		}
	}

	totem_config->transport_number = TOTEM_TRANSPORT_UDP;
	if (icmap_get_string("totem.transport", &str) == CS_OK) {
		if (strcmp (str, "udpu") == 0) {
			totem_config->transport_number = TOTEM_TRANSPORT_UDPU;
		}

		if (strcmp (str, "iba") == 0) {
			totem_config->transport_number = TOTEM_TRANSPORT_RDMA;
		}
		free(str);
	}

	free(cluster_name);

	/*
	 * Check existence of nodelist
	 */
	if (icmap_get_string("nodelist.node.0.ring0_addr", &str) == CS_OK) {
		free(str);
		/*
		 * find local node
		 */
		local_node_pos = find_local_node_in_nodelist(totem_config);
		if (local_node_pos != -1) {
			icmap_set_uint32("nodelist.local_node_pos", local_node_pos);

			snprintf(tmp_key, ICMAP_KEYNAME_MAXLEN, "nodelist.node.%u.nodeid", local_node_pos);

			nodeid_set = (totem_config->node_id != 0);
			if (icmap_get_uint32(tmp_key, &totem_config->node_id) == CS_OK && nodeid_set) {
				*warnings |= TOTEM_CONFIG_WARNING_TOTEM_NODEID_IGNORED;
			}

			/*
			 * Make localnode ring0_addr read only, so we can be sure that local
			 * node never changes. If rebinding to other IP would be in future
			 * supported, this must be changed and handled properly!
			 */
			snprintf(tmp_key, ICMAP_KEYNAME_MAXLEN, "nodelist.node.%u.ring0_addr", local_node_pos);
			icmap_set_ro_access(tmp_key, 0, 1);
			icmap_set_ro_access("nodelist.local_node_pos", 0, 1);
		}

		put_nodelist_members_to_config(totem_config, 0);
	}

	/*
	 * Get things that might change in the future (and can depend on totem_config->interfaces);
	 */
	totem_volatile_config_read(totem_config, NULL);

	icmap_set_uint8("config.totemconfig_reload_in_progress", 0);

	add_totem_config_notification(totem_config);

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

	if (totem_volatile_config_validate(totem_config, error_string) == -1) {
		return (-1);
	}

	if (check_for_duplicate_nodeids(totem_config, error_string) == -1) {
		return (-1);
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

	if (totem_config->net_mtu == 0) {
		totem_config->net_mtu = 1500;
	}

	return 0;

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
		error_ptr = qb_strerror_r(errno, error_str, sizeof(error_str));
		snprintf (error_string_response, sizeof(error_string_response),
			"Could not open %s: %s\n",
			 key_location, error_ptr);
		goto parse_error;
	}

	res = read (fd, totem_config->private_key, expected_key_len);
	saved_errno = errno;
	close (fd);

	if (res == -1) {
		error_ptr = qb_strerror_r (saved_errno, error_str, sizeof(error_str));
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
	struct totem_config *totem_config,
	const char **error_string)
{
	int got_key = 0;
	char *key_location = NULL;
	int res;
	size_t key_len;

	memset (totem_config->private_key, 0, 128);
	totem_config->private_key_len = 128;

	if (strcmp(totem_config->crypto_cipher_type, "none") == 0 &&
	    strcmp(totem_config->crypto_hash_type, "none") == 0) {
		return (0);
	}

	/* cmap may store the location of the key file */
	if (icmap_get_string("totem.keyfile", &key_location) == CS_OK) {
		res = read_keyfile(key_location, totem_config, error_string);
		free(key_location);
		if (res)  {
			goto key_error;
		}
		got_key = 1;
	} else { /* Or the key itself may be in the cmap */
		if (icmap_get("totem.key", NULL, &key_len, NULL) == CS_OK) {
			if (key_len > sizeof (totem_config->private_key)) {
				sprintf(error_string_response, "key is too long");
				goto key_error;
			}
			if (icmap_get("totem.key", totem_config->private_key, &key_len, NULL) == CS_OK) {
				totem_config->private_key_len = key_len;
				got_key = 1;
			} else {
				sprintf(error_string_response, "can't store private key");
				goto key_error;
			}
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

static void debug_dump_totem_config(const struct totem_config *totem_config)
{

	log_printf(LOGSYS_LEVEL_DEBUG, "Token Timeout (%d ms) retransmit timeout (%d ms)",
	    totem_config->token_timeout, totem_config->token_retransmit_timeout);
	log_printf(LOGSYS_LEVEL_DEBUG, "token hold (%d ms) retransmits before loss (%d retrans)",
	    totem_config->token_hold_timeout, totem_config->token_retransmits_before_loss_const);
	log_printf(LOGSYS_LEVEL_DEBUG, "join (%d ms) send_join (%d ms) consensus (%d ms) merge (%d ms)",
	    totem_config->join_timeout, totem_config->send_join_timeout, totem_config->consensus_timeout,
	    totem_config->merge_timeout);
	log_printf(LOGSYS_LEVEL_DEBUG, "downcheck (%d ms) fail to recv const (%d msgs)",
	    totem_config->downcheck_timeout, totem_config->fail_to_recv_const);
	log_printf(LOGSYS_LEVEL_DEBUG,
	    "seqno unchanged const (%d rotations) Maximum network MTU %d",
	    totem_config->seqno_unchanged_const, totem_config->net_mtu);
	log_printf(LOGSYS_LEVEL_DEBUG,
	    "window size per rotation (%d messages) maximum messages per rotation (%d messages)",
	    totem_config->window_size, totem_config->max_messages);
	log_printf(LOGSYS_LEVEL_DEBUG, "missed count const (%d messages)", totem_config->miss_count_const);
	log_printf(LOGSYS_LEVEL_DEBUG, "RRP token expired timeout (%d ms)",
	    totem_config->rrp_token_expired_timeout);
	log_printf(LOGSYS_LEVEL_DEBUG, "RRP token problem counter (%d ms)",
	    totem_config->rrp_problem_count_timeout);
	log_printf(LOGSYS_LEVEL_DEBUG, "RRP threshold (%d problem count)",
	    totem_config->rrp_problem_count_threshold);
	log_printf(LOGSYS_LEVEL_DEBUG, "RRP multicast threshold (%d problem count)",
	    totem_config->rrp_problem_count_mcast_threshold);
	log_printf(LOGSYS_LEVEL_DEBUG, "RRP automatic recovery check timeout (%d ms)",
	    totem_config->rrp_autorecovery_check_timeout);
	log_printf(LOGSYS_LEVEL_DEBUG, "RRP mode set to %s.",
	    totem_config->rrp_mode);
	log_printf(LOGSYS_LEVEL_DEBUG, "heartbeat_failures_allowed (%d)",
	    totem_config->heartbeat_failures_allowed);
	log_printf(LOGSYS_LEVEL_DEBUG, "max_network_delay (%d ms)", totem_config->max_network_delay);
}

static void totem_change_notify(
	int32_t event,
	const char *key_name,
	struct icmap_notify_value new_val,
	struct icmap_notify_value old_val,
	void *user_data)
{
	struct totem_config *totem_config = (struct totem_config *)user_data;
	uint32_t *param;
	uint8_t reloading;
	const char *deleted_key = NULL;
	const char *error_string;

	/*
	 * If a full reload is in progress then don't do anything until it's done and
	 * can reconfigure it all atomically
	 */
	if (icmap_get_uint8("config.reload_in_progress", &reloading) == CS_OK && reloading)
		return;

	param = totem_get_param_by_name((struct totem_config *)user_data, key_name);
	/*
	 * Process change only if changed key is found in totem_config (-> param is not NULL)
	 * or for special key token_coefficient. token_coefficient key is not stored in
	 * totem_config, but it is used for computation of token timeout.
	 */
	if (!param && strcmp(key_name, "totem.token_coefficient") != 0)
		return;

	/*
	 * Values other than UINT32 are not supported, or needed (yet)
	 */
	switch (event) {
	case ICMAP_TRACK_DELETE:
		deleted_key = key_name;
		break;
	case ICMAP_TRACK_ADD:
	case ICMAP_TRACK_MODIFY:
		deleted_key = NULL;
		break;
	default:
		break;
	}

	totem_volatile_config_read (totem_config, deleted_key);
	log_printf(LOGSYS_LEVEL_DEBUG, "Totem related config key changed. Dumping actual totem config.");
	debug_dump_totem_config(totem_config);
	if (totem_volatile_config_validate(totem_config, &error_string) == -1) {
		log_printf (LOGSYS_LEVEL_ERROR, "%s", error_string);
		/*
		 * TODO: Consider corosync exit and/or load defaults for volatile
		 * values. For now, log error seems to be enough
		 */
	}
}

static void totem_reload_notify(
	int32_t event,
	const char *key_name,
	struct icmap_notify_value new_val,
	struct icmap_notify_value old_val,
	void *user_data)
{
	struct totem_config *totem_config = (struct totem_config *)user_data;
	uint32_t local_node_pos;
	const char *error_string;

	/* Reload has completed */
	if (*(uint8_t *)new_val.data == 0) {
		put_nodelist_members_to_config (totem_config, 1);
		totem_volatile_config_read (totem_config, NULL);
		log_printf(LOGSYS_LEVEL_DEBUG, "Configuration reloaded. Dumping actual totem config.");
		debug_dump_totem_config(totem_config);
		if (totem_volatile_config_validate(totem_config, &error_string) == -1) {
			log_printf (LOGSYS_LEVEL_ERROR, "%s", error_string);
			/*
			 * TODO: Consider corosync exit and/or load defaults for volatile
			 * values. For now, log error seems to be enough
			 */
		}

		/* Reinstate the local_node_pos */
		local_node_pos = find_local_node_in_nodelist(totem_config);
		if (local_node_pos != -1) {
			icmap_set_uint32("nodelist.local_node_pos", local_node_pos);
		}

		icmap_set_uint8("config.totemconfig_reload_in_progress", 0);
	} else {
		icmap_set_uint8("config.totemconfig_reload_in_progress", 1);
	}
}

static void add_totem_config_notification(struct totem_config *totem_config)
{
	icmap_track_t icmap_track;

	icmap_track_add("totem.",
		ICMAP_TRACK_ADD | ICMAP_TRACK_DELETE | ICMAP_TRACK_MODIFY | ICMAP_TRACK_PREFIX,
		totem_change_notify,
		totem_config,
		&icmap_track);

	icmap_track_add("config.reload_in_progress",
		ICMAP_TRACK_ADD | ICMAP_TRACK_MODIFY,
		totem_reload_notify,
		totem_config,
		&icmap_track);

	icmap_track_add("nodelist.node.",
		ICMAP_TRACK_ADD | ICMAP_TRACK_DELETE | ICMAP_TRACK_MODIFY | ICMAP_TRACK_PREFIX,
		nodelist_dynamic_notify,
		(void *)totem_config,
		&icmap_track);
}
