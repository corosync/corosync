/*
 * Copyright (c) 2002-2005 MontaVista Software, Inc.
 * Copyright (c) 2006-2012 Red Hat, Inc.
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

static void totem_volatile_config_read (struct totem_config *totem_config)
{
	char *str;

	icmap_get_uint32("totem.token", &totem_config->token_timeout);
	icmap_get_uint32("totem.token_retransmit", &totem_config->token_retransmit_timeout);
	icmap_get_uint32("totem.hold", &totem_config->token_hold_timeout);
	icmap_get_uint32("totem.token_retransmits_before_loss_const", &totem_config->token_retransmits_before_loss_const);
	icmap_get_uint32("totem.join", &totem_config->join_timeout);
	icmap_get_uint32("totem.send_join", &totem_config->send_join_timeout);
	icmap_get_uint32("totem.consensus", &totem_config->consensus_timeout);
	icmap_get_uint32("totem.merge", &totem_config->merge_timeout);
	icmap_get_uint32("totem.downcheck", &totem_config->downcheck_timeout);
	icmap_get_uint32("totem.fail_recv_const", &totem_config->fail_to_recv_const);
	icmap_get_uint32("totem.seqno_unchanged_const", &totem_config->seqno_unchanged_const);
	icmap_get_uint32("totem.rrp_token_expired_timeout", &totem_config->rrp_token_expired_timeout);
	icmap_get_uint32("totem.rrp_problem_count_timeout", &totem_config->rrp_problem_count_timeout);
	icmap_get_uint32("totem.rrp_problem_count_threshold", &totem_config->rrp_problem_count_threshold);
	icmap_get_uint32("totem.rrp_problem_count_mcast_threshold", &totem_config->rrp_problem_count_mcast_threshold);
	icmap_get_uint32("totem.rrp_autorecovery_check_timeout", &totem_config->rrp_autorecovery_check_timeout);
	icmap_get_uint32("totem.heartbeat_failures_allowed", &totem_config->heartbeat_failures_allowed);
	icmap_get_uint32("totem.max_network_delay", &totem_config->max_network_delay);
	icmap_get_uint32("totem.window_size", &totem_config->window_size);
	icmap_get_uint32("totem.max_messages", &totem_config->max_messages);
	icmap_get_uint32("totem.miss_count_const", &totem_config->miss_count_const);
	if (icmap_get_string("totem.vsftype", &str) == CS_OK) {
		totem_config->vsf_type = str;
	}
}


static void totem_get_crypto(struct totem_config *totem_config)
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

	free(totem_config->crypto_cipher_type);
	free(totem_config->crypto_hash_type);

	totem_config->crypto_cipher_type = strdup(tmp_cipher);
	totem_config->crypto_hash_type = strdup(tmp_hash);
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
		const struct totem_ip_address *bindnet,
		unsigned int ringnumber,
		struct totem_ip_address *res)
{
	uint16_t clusterid;
	char addr[INET6_ADDRSTRLEN + 1];
	int err;

	if (cluster_name == NULL) {
		return (-1);
	}

	clusterid = generate_cluster_id(cluster_name) + ringnumber;
	memset (res, 0, sizeof(res));

	switch (bindnet->family) {
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

	err = totemip_parse (res, addr, 0);

	return (err);
}

static int find_local_node_in_nodelist(struct totem_config *totem_config)
{
	icmap_iter_t iter;
	const char *iter_key;
	int res = 0;
	int node_pos;
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

		res = totemip_parse (&node_addr, node_addr_str, 0);
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

static void put_nodelist_members_to_config(struct totem_config *totem_config)
{
	icmap_iter_t iter, iter2;
	const char *iter_key, *iter_key2;
	int res = 0;
	int node_pos;
	char tmp_key[ICMAP_KEYNAME_MAXLEN];
	char tmp_key2[ICMAP_KEYNAME_MAXLEN];
	char *node_addr_str;
	int member_count;
	unsigned int ringnumber = 0;

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
						node_addr_str, 0);
			if (res != -1) {
				totem_config->interfaces[ringnumber].member_count++;
			}
			free(node_addr_str);
		}

		icmap_iter_finalize(iter2);
	}

	icmap_iter_finalize(iter);
}

static void config_convert_nodelist_to_interface(struct totem_config *totem_config)
{
	icmap_iter_t iter;
	const char *iter_key;
	int res = 0;
	int node_pos;
	char tmp_key[ICMAP_KEYNAME_MAXLEN];
	char tmp_key2[ICMAP_KEYNAME_MAXLEN];
	char *node_addr_str;
	unsigned int ringnumber = 0;
	struct list_head addrs;
	struct list_head *list;
	struct totem_ip_if_address *if_addr;
	struct totem_ip_address node_addr;
	int node_found;

	if (totemip_getifaddrs(&addrs) == -1) {
		return ;
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

		if (icmap_get_string(iter_key, &node_addr_str) != CS_OK) {
			continue ;
		}

		if (totemip_parse(&node_addr, node_addr_str, 0) == -1) {
			free(node_addr_str);
			continue ;
		}
		free(node_addr_str);

		/*
		 * Try to find node in if_addrs
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

	icmap_iter_finalize(iter);

	if (node_found) {
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
	char *str;
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

	totem_get_crypto(totem_config);

	if (icmap_get_string("totem.rrp_mode", &str) == CS_OK) {
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

	icmap_get_string("totem.cluster_name", &cluster_name);

	/*
	 * Get things that might change in the future
	 */
	totem_volatile_config_read(totem_config);

	if (icmap_get_string("totem.interface.0.bindnetaddr", &str) != CS_OK) {
		/*
		 * We were not able to find ring 0 bindnet addr. Try to use nodelist informations
		 */
		config_convert_nodelist_to_interface(totem_config);
	} else {
		free(str);
	}

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
		/*
		 * Get the bind net address
		 */
		if (icmap_get_string(iter_key, &str) == CS_OK) {
			res = totemip_parse (&totem_config->interfaces[ringnumber].bindnet, str,
						     totem_config->interfaces[ringnumber].mcast_addr.family);
			free(str);
		}

		/*
		 * Get interface multicast address
		 */
		snprintf(tmp_key, ICMAP_KEYNAME_MAXLEN, "totem.interface.%u.mcastaddr", ringnumber);
		if (icmap_get_string(tmp_key, &str) == CS_OK) {
			res = totemip_parse (&totem_config->interfaces[ringnumber].mcast_addr, str, 0);
			free(str);
		} else {
			/*
			 * User not specified address -> autogenerate one from cluster_name key
			 * (if available)
			 */
			res = get_cluster_mcast_addr (cluster_name,
					&totem_config->interfaces[ringnumber].bindnet,
					ringnumber,
					&totem_config->interfaces[ringnumber].mcast_addr);
		}

		totem_config->broadcast_use = 0;
		snprintf(tmp_key, ICMAP_KEYNAME_MAXLEN, "totem.interface.%u.broadcast", ringnumber);
		if (icmap_get_string(tmp_key, &str) == CS_OK) {
			if (strcmp (str, "yes") == 0) {
				totem_config->broadcast_use = 1;
				totemip_parse (
					&totem_config->interfaces[ringnumber].mcast_addr,
					"255.255.255.255", 0);
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
						str, 0);
			}
		}
		icmap_iter_finalize(member_iter);

		totem_config->interfaces[ringnumber].member_count = member_count;
		totem_config->interface_count++;
	}
	icmap_iter_finalize(iter);

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

		put_nodelist_members_to_config(totem_config);
	}

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

		if (totem_config->broadcast_use == 0 && totem_config->transport_number == 0) {
			if (totem_config->interfaces[i].mcast_addr.family != totem_config->interfaces[i].bindnet.family) {
				error_reason = "Multicast address family does not match bind address family";
				goto parse_error;
			}

			if (totem_config->interfaces[i].mcast_addr.family != totem_config->interfaces[i].bindnet.family) {
				error_reason =  "Not all bind address belong to the same IP family";
				goto parse_error;
			}
			if (totemip_is_mcast (&totem_config->interfaces[i].mcast_addr) != 0) {
				error_reason = "mcastaddr is not a correct multicast address.";
				goto parse_error;
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
	}

	if (totem_config->max_network_delay == 0) {
		totem_config->max_network_delay = MAX_NETWORK_DELAY;
	}

	if (totem_config->max_network_delay < MINIMUM_TIMEOUT) {
		snprintf (local_error_reason, sizeof(local_error_reason),
			"The max_network_delay parameter (%d ms) may not be less then (%d ms).",
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
			"The token timeout parameter (%d ms) may not be less then (%d ms).",
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
			"The token retransmit timeout parameter (%d ms) may not be less then (%d ms).",
			totem_config->token_retransmit_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	if (totem_config->token_hold_timeout < MINIMUM_TIMEOUT) {
		snprintf (local_error_reason, sizeof(local_error_reason),
			"The token hold timeout parameter (%d ms) may not be less then (%d ms).",
			totem_config->token_hold_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	if (totem_config->join_timeout == 0) {
		totem_config->join_timeout = JOIN_TIMEOUT;
	}

	if (totem_config->join_timeout < MINIMUM_TIMEOUT) {
		snprintf (local_error_reason, sizeof(local_error_reason),
			"The join timeout parameter (%d ms) may not be less then (%d ms).",
			totem_config->join_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	if (totem_config->consensus_timeout == 0) {
		totem_config->consensus_timeout = (int)(float)(1.2 * totem_config->token_timeout);
	}

	if (totem_config->consensus_timeout < MINIMUM_TIMEOUT) {
		snprintf (local_error_reason, sizeof(local_error_reason),
			"The consensus timeout parameter (%d ms) may not be less then (%d ms).",
			totem_config->consensus_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	if (totem_config->merge_timeout == 0) {
		totem_config->merge_timeout = MERGE_TIMEOUT;
	}

	if (totem_config->merge_timeout < MINIMUM_TIMEOUT) {
		snprintf (local_error_reason, sizeof(local_error_reason),
			"The merge timeout parameter (%d ms) may not be less then (%d ms).",
			totem_config->merge_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	if (totem_config->downcheck_timeout == 0) {
		totem_config->downcheck_timeout = DOWNCHECK_TIMEOUT;
	}

	if (totem_config->downcheck_timeout < MINIMUM_TIMEOUT) {
		snprintf (local_error_reason, sizeof(local_error_reason),
			"The downcheck timeout parameter (%d ms) may not be less then (%d ms).",
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
			"The RRP problem count timeout parameter (%d ms) may not be less then (%d ms).",
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
			"The RRP problem count threshold (%d problem count) may not be less then (%d problem count).",
			totem_config->rrp_problem_count_threshold, RRP_PROBLEM_COUNT_THRESHOLD_MIN);
		goto parse_error;
	}
	if (totem_config->rrp_problem_count_mcast_threshold < RRP_PROBLEM_COUNT_THRESHOLD_MIN) {
		snprintf (local_error_reason, sizeof(local_error_reason),
			"The RRP multicast problem count threshold (%d problem count) may not be less then (%d problem count).",
			totem_config->rrp_problem_count_mcast_threshold, RRP_PROBLEM_COUNT_THRESHOLD_MIN);
		goto parse_error;
	}
	if (totem_config->rrp_token_expired_timeout == 0) {
		totem_config->rrp_token_expired_timeout =
			totem_config->token_retransmit_timeout;
	}

	if (totem_config->rrp_token_expired_timeout < MINIMUM_TIMEOUT) {
		snprintf (local_error_reason, sizeof(local_error_reason),
			"The RRP token expired timeout parameter (%d ms) may not be less then (%d ms).",
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
			"The max_messages parameter (%d messages) may not be greater then (%d messages).",
			totem_config->max_messages, MESSAGE_QUEUE_MAX);
		goto parse_error;
	}

	if (totem_config->threads > SEND_THREADS_MAX) {
		totem_config->threads = SEND_THREADS_MAX;
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

static void totem_change_notify(
	int32_t event,
	const char *key_name,
	struct icmap_notify_value new_val,
	struct icmap_notify_value old_val,
	void *user_data)
{
	totem_volatile_config_read((struct totem_config *)user_data);
}

static void add_totem_config_notification(struct totem_config *totem_config)
{
	icmap_track_t icmap_track;

	icmap_track_add("totem.",
		ICMAP_TRACK_ADD | ICMAP_TRACK_DELETE | ICMAP_TRACK_MODIFY | ICMAP_TRACK_PREFIX,
		totem_change_notify,
		totem_config,
		&icmap_track);
}
