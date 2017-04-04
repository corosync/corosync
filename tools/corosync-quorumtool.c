/*
 * Copyright (c) 2009-2014 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Authors: Christine Caulfield <ccaulfie@redhat.com>
 *          Fabio M. Di Nitto   (fdinitto@redhat.com)
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
 * - Neither the name of the Red Hat Inc. nor the names of its
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
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <limits.h>

#include <corosync/totem/totem.h>
#include <corosync/cfg.h>
#include <corosync/cmap.h>
#include <corosync/quorum.h>
#include <corosync/votequorum.h>

typedef enum {
	NODEID_FORMAT_DECIMAL,
	NODEID_FORMAT_HEX
} nodeid_format_t;

typedef enum {
	ADDRESS_FORMAT_NAME,
	ADDRESS_FORMAT_IP
} name_format_t;

typedef enum {
	CMD_UNKNOWN,
	CMD_SHOWNODES,
	CMD_SHOWSTATUS,
	CMD_SETVOTES,
	CMD_SETEXPECTED,
	CMD_MONITOR,
	CMD_UNREGISTER_QDEVICE
} command_t;

typedef enum {
	SORT_ADDR,
	SORT_NODEID,
	SORT_NODENAME
} sorttype_t;

/*
 * global vars
 */

/*
 * cmap bits
 */
static cmap_handle_t cmap_handle;

/*
 * quorum bits
 */
static void quorum_notification_fn(
	quorum_handle_t handle,
	uint32_t quorate,
	uint64_t ring_id,
	uint32_t view_list_entries,
	uint32_t *view_list);

static quorum_handle_t q_handle;
static uint32_t q_type;
static quorum_callbacks_t q_callbacks = {
	.quorum_notify_fn = quorum_notification_fn
};

/*
 * quorum call back vars
 */

/* Containing struct to keep votequorum & normal quorum bits together */
typedef struct {
      struct votequorum_info *vq_info; /* Might be NULL if votequorum not present */
      char *name;  /* Might be IP address or NULL */
      int node_id; /* Always present */
} view_list_entry_t;

static view_list_entry_t *g_view_list;
static uint32_t g_quorate;
static uint64_t g_ring_id;
static uint32_t g_ring_id_rep_node;
static uint32_t g_view_list_entries;
static uint32_t g_called;
static uint32_t g_vq_called;
static uint32_t g_show_all_addrs = 0;

/*
 * votequorum bits
 */
static void votequorum_notification_fn(
	votequorum_handle_t handle,
	uint64_t context,
	votequorum_ring_id_t ring_id,
	uint32_t node_list_entries,
	uint32_t node_list[]);
static votequorum_handle_t v_handle;
static votequorum_callbacks_t v_callbacks = {
	.votequorum_quorum_notify_fn = NULL,
	.votequorum_expectedvotes_notify_fn = NULL,
	.votequorum_nodelist_notify_fn = votequorum_notification_fn,
};
static uint32_t our_nodeid = 0;

/*
 * cfg bits
 */
static corosync_cfg_handle_t c_handle;
static corosync_cfg_callbacks_t c_callbacks = {
	.corosync_cfg_shutdown_callback = NULL
};

/*
 * global
 */
static int machine_parsable = 0;

static void show_usage(const char *name)
{
	printf("usage: \n");
	printf("%s <options>\n", name);
	printf("\n");
	printf("  options:\n");
	printf("\n");
	printf("  -s             show quorum status\n");
	printf("  -m             constantly monitor quorum status\n");
	printf("  -l             list nodes\n");
	printf("  -a             show all names or addresses for each node\n");
	printf("  -p             when used with -s or -l, generates machine parsable output\n");
	printf("  -v <votes>     change the number of votes for a node (*)\n");
	printf("  -n <nodeid>    optional nodeid of node for -v\n");
	printf("  -e <expected>  change expected votes for the cluster (*)\n");
	printf("  -H             show nodeids in hexadecimal rather than decimal\n");
	printf("  -i             show node IP addresses instead of the resolved name\n");
	printf("  -p             when used with -s or -l, generates machine parsable output\n");
	printf("  -o <a|n|i>     order by [a] IP address (default), [n] name, [i] nodeid\n");
	printf("  -f             forcefully unregister a quorum device *DANGEROUS* (*)\n");
	printf("  -h             show this help text\n");
	printf("  -V             show version and exit\n");
	printf("\n");
	printf("  (*) Starred items only work if votequorum is the quorum provider for corosync\n");
	printf("\n");
}

static int get_quorum_type(char *quorum_type, size_t quorum_type_len)
{
	int err;
	char *str = NULL;

	if ((!quorum_type) || (quorum_type_len <= 0)) {
		return -1;
	}

	if (q_type == QUORUM_FREE) {
		return -1;
	}

	if ((err = cmap_get_string(cmap_handle, "quorum.provider", &str)) != CS_OK) {
		goto out;
	}

	if (!str) {
		return -1;
	}

	strncpy(quorum_type, str, quorum_type_len - 1);
	free(str);

	return 0;
out:
	return err;
}

/*
 * Returns 1 if 'votequorum' is active. The called then knows that
 * votequorum calls should work and can provide extra information
 */
static int using_votequorum(void)
{
	char quorumtype[256];
	int using_voteq;

	memset(quorumtype, 0, sizeof(quorumtype));

	if (get_quorum_type(quorumtype, sizeof(quorumtype))) {
		return -1;
	}

	if (strcmp(quorumtype, "corosync_votequorum") == 0) {
		using_voteq = 1;
	} else {
		using_voteq = 0;
	}

	return using_voteq;
}

static int set_votes(uint32_t nodeid, int votes)
{
	int err;

	if ((err=votequorum_setvotes(v_handle, nodeid, votes)) != CS_OK) {
		fprintf(stderr, "Unable to set votes %d for nodeid: %u: %s\n",
			votes, nodeid, cs_strerror(err));
	}

	return err==CS_OK?0:err;
}

static int set_expected(int expected_votes)
{
	int err;

	if ((err=votequorum_setexpected(v_handle, expected_votes)) != CS_OK) {
		fprintf(stderr, "Unable to set expected votes: %s\n", cs_strerror(err));
	}

	return err==CS_OK?0:err;
}

/*
 *  node name by nodelist
 */

static const char *node_name_by_nodelist(uint32_t nodeid)
{
	cmap_iter_handle_t iter;
	char key_name[CMAP_KEYNAME_MAXLEN];
	char tmp_key[CMAP_KEYNAME_MAXLEN];
	static char ret_buf[_POSIX_HOST_NAME_MAX];
	char *str = NULL;
	uint32_t node_pos, cur_nodeid;
	int res = 0;

	if (cmap_iter_init(cmap_handle, "nodelist.node.", &iter) != CS_OK) {
		return "";
	}

	memset(ret_buf, 0, sizeof(ret_buf));

	while ((cmap_iter_next(cmap_handle, iter, key_name, NULL, NULL)) == CS_OK) {

		res = sscanf(key_name, "nodelist.node.%u.%s", &node_pos, tmp_key);
		if (res != 2) {
			continue;
		}

		if (strcmp(tmp_key, "ring0_addr") != 0) {
			continue;
		}

		snprintf(tmp_key, CMAP_KEYNAME_MAXLEN, "nodelist.node.%u.nodeid", node_pos);
		if (cmap_get_uint32(cmap_handle, tmp_key, &cur_nodeid) != CS_OK) {
			continue;
		}
		if (cur_nodeid != nodeid) {
			continue;
		}
		snprintf(tmp_key, CMAP_KEYNAME_MAXLEN, "nodelist.node.%u.ring0_addr", node_pos);
		if (cmap_get_string(cmap_handle, tmp_key, &str) != CS_OK) {
			continue;
		}
		if (!str) {
			continue;
		}
		strncpy(ret_buf, str, sizeof(ret_buf) - 1);
		free(str);
		break;
	}
	cmap_iter_finalize(cmap_handle, iter);

	return ret_buf;
}

/*
 * This resolves the first address assigned to a node
 * and returns the name or IP address. Use cfgtool if you need more information.
 */
static const char *node_name(uint32_t nodeid, name_format_t name_format)
{
	int err;
	int numaddrs;
	corosync_cfg_node_address_t addrs[INTERFACE_MAX];
	static char buf[(INET6_ADDRSTRLEN + 1) * INTERFACE_MAX];
	const char *nodelist_name = NULL;
	socklen_t addrlen;
	struct sockaddr_storage *ss;
	int start_addr = 0;
	int i;
	int bufptr = 0;

	buf[0] = '\0';

	/* If a name is required, always look for the nodelist node0_addr name first */
	if (name_format == ADDRESS_FORMAT_NAME) {
		nodelist_name = node_name_by_nodelist(nodeid);
	}
	if ((nodelist_name) &&
	    (strlen(nodelist_name) > 0)) {
		start_addr = 1;
		strcpy(buf, nodelist_name);
		bufptr = strlen(buf);
	}

	err = corosync_cfg_get_node_addrs(c_handle, nodeid, INTERFACE_MAX, &numaddrs, addrs);
	if (err != CS_OK) {
		fprintf(stderr, "Unable to get node address for nodeid %u: %s\n", nodeid, cs_strerror(err));
		return "";
	}

	/* Don't show all addressess */
	if (!g_show_all_addrs) {
		numaddrs = 1;
	}

	for (i=start_addr; i<numaddrs; i++) {

		if (i) {
			buf[bufptr++] = ',';
			buf[bufptr++] = ' ';
		}

		ss = (struct sockaddr_storage *)addrs[i].address;

		if (ss->ss_family == AF_INET6) {
			addrlen = sizeof(struct sockaddr_in6);
		} else {
			addrlen = sizeof(struct sockaddr_in);
		}

		if (!getnameinfo(
			    (struct sockaddr *)addrs[i].address, addrlen,
			    buf+bufptr, sizeof(buf)-bufptr,
			    NULL, 0,
			    (name_format == ADDRESS_FORMAT_IP)?NI_NUMERICHOST:0)) {
			bufptr += strlen(buf+bufptr);
		}
	}

	return buf;
}


static void votequorum_notification_fn(
	votequorum_handle_t handle,
	uint64_t context,
	votequorum_ring_id_t ring_id,
	uint32_t node_list_entries,
	uint32_t node_list[])
{
	g_ring_id_rep_node = ring_id.nodeid;
	g_vq_called = 1;
}

static void quorum_notification_fn(
	quorum_handle_t handle,
	uint32_t quorate,
	uint64_t ring_id,
	uint32_t view_list_entries,
	uint32_t *view_list)
{
        int i;

	g_called = 1;
	g_quorate = quorate;
	g_ring_id = ring_id;
	g_view_list_entries = view_list_entries;
	if (g_view_list) {
		free(g_view_list);
	}
	g_view_list = malloc(sizeof(view_list_entry_t) * view_list_entries);
	if (g_view_list) {
	        for (i=0; i< view_list_entries; i++) {
		        g_view_list[i].node_id = view_list[i];
			g_view_list[i].name = NULL;
			g_view_list[i].vq_info = NULL;
		}
	}
}

static void print_string_padded(const char *buf)
{
	int len, delta;

	len = strlen(buf);
	delta = 10 - len;
	while (delta > 0) {
		printf(" ");
		delta--;
	}
	printf("%s ", buf);
}

static void print_uint32_padded(uint32_t value)
{
	char buf[12];

	snprintf(buf, sizeof(buf) - 1, "%u", value);
	print_string_padded(buf);
}

/* for qsort */
static int compare_nodeids(const void *one, const void *two)
{
      const view_list_entry_t *info1 = one;
      const view_list_entry_t *info2 = two;

      if (info1->node_id == info2->node_id) {
	  return 0;
      }
      if (info1->node_id > info2->node_id) {
	  return 1;
      }
      return -1;
}

static int compare_nodenames(const void *one, const void *two)
{
      const view_list_entry_t *info1 = one;
      const view_list_entry_t *info2 = two;

      return strcmp(info1->name, info2->name);
}

static void display_nodes_data(nodeid_format_t nodeid_format, name_format_t name_format, sorttype_t sort_type)
{
	int i, display_qdevice = 0;
	unsigned int our_flags = 0;
	struct votequorum_info info[g_view_list_entries];
	/*
	 * cache node info because we need to parse them twice
	 */
	if (v_handle) {
		for (i=0; i < g_view_list_entries; i++) {
			if (votequorum_getinfo(v_handle, g_view_list[i].node_id, &info[i]) != CS_OK) {
				printf("Unable to get node %u info\n", g_view_list[i].node_id);
			}
			g_view_list[i].vq_info = &info[i];
			if (info[i].flags & VOTEQUORUM_INFO_QDEVICE_REGISTERED) {
				display_qdevice = 1;
			}
		}
	}

	/*
	 * Get node names
	 */
	for (i=0; i < g_view_list_entries; i++) {
	        g_view_list[i].name = strdup(node_name(g_view_list[i].node_id, name_format));
	}

	printf("\nMembership information\n");
	printf("----------------------\n");

	print_string_padded("Nodeid");
	if (v_handle) {
		print_string_padded("Votes");
		if ((display_qdevice) || (machine_parsable)) {
			print_string_padded("Qdevice");
		}
	}
	printf("Name\n");

	/* corosync sends them already sorted by address */
	if (sort_type == SORT_NODEID) {
		qsort(g_view_list, g_view_list_entries, sizeof(view_list_entry_t), compare_nodeids);
	}
	if (sort_type == SORT_NODENAME) {
		qsort(g_view_list, g_view_list_entries, sizeof(view_list_entry_t), compare_nodenames);
	}
	for (i=0; i < g_view_list_entries; i++) {
		if (nodeid_format == NODEID_FORMAT_DECIMAL) {
			print_uint32_padded(g_view_list[i].node_id);
		} else {
			printf("0x%08x ", g_view_list[i].node_id);
		}
		if (v_handle) {
			int votes = -1;

			votes = info[i].node_votes;
			print_uint32_padded(votes);

			if ((display_qdevice) || (machine_parsable)) {
				if (info[i].flags & VOTEQUORUM_INFO_QDEVICE_REGISTERED) {
					char buf[10];

					snprintf(buf, sizeof(buf) - 1,
						 "%s,%s,%s",
						 info[i].flags & VOTEQUORUM_INFO_QDEVICE_ALIVE?"A":"NA",
						 info[i].flags & VOTEQUORUM_INFO_QDEVICE_CAST_VOTE?"V":"NV",
						 info[i].flags & VOTEQUORUM_INFO_QDEVICE_MASTER_WINS?"MW":"NMW");
					print_string_padded(buf);
				} else {
					print_string_padded("NR");
				}
			}
		}
		printf("%s", g_view_list[i].name);
		if (g_view_list[i].node_id == our_nodeid) {
			printf(" (local)");
			our_flags = info[i].flags;
		}
		printf("\n");
	}

	if (g_view_list_entries) {
	        for (i=0; i < g_view_list_entries; i++) {
		        free(g_view_list[i].name);
		}
		free(g_view_list);
		g_view_list = NULL;
	}

	if (display_qdevice) {
		if (nodeid_format == NODEID_FORMAT_DECIMAL) {
			print_uint32_padded(VOTEQUORUM_QDEVICE_NODEID);
		} else {
			printf("0x%08x ", VOTEQUORUM_QDEVICE_NODEID);
		}
		/* If the quorum device is inactive on this node then show votes as 0
		   so that the display is not confusing */
		if (our_flags & VOTEQUORUM_INFO_QDEVICE_CAST_VOTE) {
			print_uint32_padded(info[0].qdevice_votes);
		}
		else {
			print_uint32_padded(0);
		}
		printf("           %s", info[0].qdevice_name);
		if (our_flags & VOTEQUORUM_INFO_QDEVICE_CAST_VOTE) {
			printf("\n");
		}
		else {
			printf(" (votes %d)\n", info[0].qdevice_votes);
		}
	}

}

static int display_quorum_data(int is_quorate,
			       nodeid_format_t nodeid_format, name_format_t name_format, sorttype_t sort_type,
			       int loop)
{
	struct votequorum_info info;
	int err;
	char quorumtype[256];
	time_t t;

	memset(quorumtype, 0, sizeof(quorumtype));

	printf("Quorum information\n");
	printf("------------------\n");
	time(&t);
	printf("Date:             %s", ctime((const time_t *)&t));

	if (get_quorum_type(quorumtype, sizeof(quorumtype))) {
		strncpy(quorumtype, "Not configured", sizeof(quorumtype) - 1);
	}
	printf("Quorum provider:  %s\n", quorumtype);
	printf("Nodes:            %d\n", g_view_list_entries);
	if (nodeid_format == NODEID_FORMAT_DECIMAL) {
		printf("Node ID:          %u\n", our_nodeid);
	} else {
		printf("Node ID:          0x%08x\n", our_nodeid);
	}

	if (v_handle) {
		printf("Ring ID:          %d/%" PRIu64 "\n", g_ring_id_rep_node, g_ring_id);
	}
	else {
		printf("Ring ID:          %" PRIu64 "\n", g_ring_id);
	}
	printf("Quorate:          %s\n", is_quorate?"Yes":"No");

	if (!v_handle) {
		return CS_OK;
	}

	err=votequorum_getinfo(v_handle, our_nodeid, &info);
	if ((err == CS_OK) || (err == CS_ERR_NOT_EXIST)) {
		printf("\nVotequorum information\n");
		printf("----------------------\n");
		printf("Expected votes:   %d\n", info.node_expected_votes);
		printf("Highest expected: %d\n", info.highest_expected);
		printf("Total votes:      %d\n", info.total_votes);
		printf("Quorum:           %d %s\n", info.quorum, info.flags & VOTEQUORUM_INFO_QUORATE?" ":"Activity blocked");
		printf("Flags:            ");
		if (info.flags & VOTEQUORUM_INFO_TWONODE) printf("2Node ");
		if (info.flags & VOTEQUORUM_INFO_QUORATE) printf("Quorate ");
		if (info.flags & VOTEQUORUM_INFO_WAIT_FOR_ALL) printf("WaitForAll ");
		if (info.flags & VOTEQUORUM_INFO_LAST_MAN_STANDING) printf("LastManStanding ");
		if (info.flags & VOTEQUORUM_INFO_AUTO_TIE_BREAKER) printf("AutoTieBreaker ");
		if (info.flags & VOTEQUORUM_INFO_ALLOW_DOWNSCALE) printf("AllowDownscale ");
		if (info.flags & VOTEQUORUM_INFO_QDEVICE_REGISTERED) printf("Qdevice ");
		printf("\n");
	} else {
		fprintf(stderr, "Unable to get node info: %s\n", cs_strerror(err));
	}

	display_nodes_data(nodeid_format, name_format, sort_type);

	return err;
}

/*
 * return  1 if quorate
 *         0 if not quorate
 *        -1 on error
 */
static int show_status(nodeid_format_t nodeid_format, name_format_t name_format, sorttype_t sort_type)
{
	int is_quorate;
	int err;

	err=quorum_getquorate(q_handle, &is_quorate);
	if (err != CS_OK) {
		fprintf(stderr, "Unable to get cluster quorate status: %s\n", cs_strerror(err));
		goto quorum_err;
	}

	err=quorum_trackstart(q_handle, CS_TRACK_CURRENT);
	if (err != CS_OK) {
		fprintf(stderr, "Unable to start quorum status tracking: %s\n", cs_strerror(err));
		goto quorum_err;
	}

	g_called = 0;
	while (g_called == 0 && err == CS_OK) {
		err = quorum_dispatch(q_handle, CS_DISPATCH_ONE);
		if (err != CS_OK) {
			fprintf(stderr, "Unable to dispatch quorum status: %s\n", cs_strerror(err));
		}
	}

	if (quorum_trackstop(q_handle) != CS_OK) {
		fprintf(stderr, "Unable to stop quorum status tracking: %s\n", cs_strerror(err));
	}

	if (using_votequorum()) {

		if ( (err=votequorum_trackstart(v_handle, 0LL, CS_TRACK_CURRENT)) != CS_OK) {
			fprintf(stderr, "Unable to start votequorum status tracking: %s\n", cs_strerror(err));
			goto quorum_err;
		}

		g_vq_called = 0;
		while (g_vq_called == 0 && err == CS_OK) {
			err = votequorum_dispatch(v_handle, CS_DISPATCH_ONE);
			if (err != CS_OK) {
				fprintf(stderr, "Unable to dispatch votequorum status: %s\n", cs_strerror(err));
			}
		}
	}

quorum_err:
	if (err != CS_OK) {
		return -1;
	}

	err = display_quorum_data(is_quorate, nodeid_format, name_format, sort_type, 0);
	if (err != CS_OK) {
		return -1;
	}

	return is_quorate;
}

static int monitor_status(nodeid_format_t nodeid_format, name_format_t name_format, sorttype_t sort_type) {
	int err;
	int loop = 0;

	if (q_type == QUORUM_FREE) {
		printf("\nQuorum is not configured - cannot monitor\n");
		return show_status(nodeid_format, name_format, sort_type);
	}

	err=quorum_trackstart(q_handle, CS_TRACK_CHANGES);
	if (err != CS_OK) {
		fprintf(stderr, "Unable to start quorum status tracking: %s\n", cs_strerror(err));
		goto quorum_err;
	}

	if (using_votequorum()) {
		if ( (err=votequorum_trackstart(v_handle, 0LL, CS_TRACK_CHANGES)) != CS_OK) {
			fprintf(stderr, "Unable to start votequorum status tracking: %s\n", cs_strerror(err));
			goto quorum_err;
		}
	}


	while (1) {
		err = quorum_dispatch(q_handle, CS_DISPATCH_ONE);
		if (err != CS_OK) {
			fprintf(stderr, "Unable to dispatch quorum status: %s\n", cs_strerror(err));
			goto quorum_err;
		}
		if (using_votequorum()) {
			g_vq_called = 0;
			while (!g_vq_called) {
				err = votequorum_dispatch(v_handle, CS_DISPATCH_ONE);
				if (err != CS_OK) {
					fprintf(stderr, "Unable to dispatch votequorum status: %s\n", cs_strerror(err));
					goto quorum_err;
				}
			}
		}

		err = display_quorum_data(g_quorate, nodeid_format, name_format, sort_type, loop);
		printf("\n");
		loop = 1;
		if (err != CS_OK) {
			fprintf(stderr, "Unable to display quorum data: %s\n", cs_strerror(err));
			goto quorum_err;
		}
	}

quorum_err:
	return -1;
}

static int show_nodes(nodeid_format_t nodeid_format, name_format_t name_format, sorttype_t sort_type)
{
	int err;
	int result = EXIT_FAILURE;

	err = quorum_trackstart(q_handle, CS_TRACK_CURRENT);
	if (err != CS_OK) {
		fprintf(stderr, "Unable to start quorum status tracking: %s\n", cs_strerror(err));
		goto err_exit;
	}

	g_called = 0;
	while (g_called == 0) {
		err = quorum_dispatch(q_handle, CS_DISPATCH_ONE);
		if (err != CS_OK) {
			fprintf(stderr, "Unable to dispatch quorum status: %s\n", cs_strerror(err));
			goto err_exit;
		}
	}

	display_nodes_data(nodeid_format, name_format, sort_type);

	result = EXIT_SUCCESS;
err_exit:
	return result;
}

static int unregister_qdevice(void)
{
	int err;
	struct votequorum_info info;

	err = votequorum_getinfo(v_handle, our_nodeid, &info);
	if (err != CS_OK) {
		fprintf(stderr, "Unable to get quorum device info: %s\n", cs_strerror(err));
		return -1;
	}

	if (!(info.flags & VOTEQUORUM_INFO_QDEVICE_REGISTERED)) {
		return 0;
	}

	err = votequorum_qdevice_unregister(v_handle, info.qdevice_name);
	if (err != CS_OK) {
		fprintf(stderr, "Unable to unregister quorum device: %s\n", cs_strerror(err));
		return -1;
	}
	return 0;
}

/*
 * return -1 on error
 *         0 if OK
 */

static int init_all(void) {
	cmap_handle = 0;
	q_handle = 0;
	v_handle = 0;
	c_handle = 0;

	if (cmap_initialize(&cmap_handle) != CS_OK) {
		fprintf(stderr, "Cannot initialize CMAP service\n");
		cmap_handle = 0;
		goto out;
	}

	if (quorum_initialize(&q_handle, &q_callbacks, &q_type) != CS_OK) {
		fprintf(stderr, "Cannot initialize QUORUM service\n");
		q_handle = 0;
		goto out;
	}

	if (corosync_cfg_initialize(&c_handle, &c_callbacks) != CS_OK) {
		fprintf(stderr, "Cannot initialise CFG service\n");
		c_handle = 0;
		goto out;
	}

	if (using_votequorum() <= 0) {
		return 0;
	}

	if (votequorum_initialize(&v_handle, &v_callbacks) != CS_OK) {
		fprintf(stderr, "Cannot initialise VOTEQUORUM service\n");
		v_handle = 0;
		goto out;
	}

	if (cmap_get_uint32(cmap_handle, "runtime.votequorum.this_node_id", &our_nodeid) != CS_OK) {
		fprintf(stderr, "Unable to retrieve this_node_id\n");
		goto out;
	}

	return 0;
out:
	return -1;
}

static void close_all(void) {
	if (cmap_handle) {
		cmap_finalize(cmap_handle);
	}
	if (q_handle) {
		quorum_finalize(q_handle);
	}
	if (c_handle) {
		corosync_cfg_finalize(c_handle);
	}
	if (v_handle) {
		votequorum_finalize(v_handle);
	}
}

int main (int argc, char *argv[]) {
	const char *options = "VHaslpmfe:v:hin:o:";
	char *endptr;
	int opt;
	int votes = 0;
	int ret = 0;
	uint32_t nodeid = 0;
	uint32_t nodeid_set = 0;
	nodeid_format_t nodeid_format = NODEID_FORMAT_DECIMAL;
	name_format_t address_format = ADDRESS_FORMAT_NAME;
	command_t command_opt = CMD_SHOWSTATUS;
	sorttype_t sort_opt = SORT_ADDR;
	char sortchar;
	long int l;

	if (init_all()) {
		close_all();
		exit(1);
	}

	while ( (opt = getopt(argc, argv, options)) != -1 ) {
		switch (opt) {
		case 'f':
			if (using_votequorum() > 0) {
				command_opt = CMD_UNREGISTER_QDEVICE;
			} else {
				fprintf(stderr, "You cannot unregister quorum device, corosync is not using votequorum\n");
				exit(2);
			}
			break;
		case 's':
			command_opt = CMD_SHOWSTATUS;
			break;
		case 'a':
			g_show_all_addrs = 1;
			break;
		case 'm':
			command_opt = CMD_MONITOR;
			break;
		case 'i':
			address_format = ADDRESS_FORMAT_IP;
			break;
		case 'H':
			nodeid_format = NODEID_FORMAT_HEX;
			break;
		case 'l':
			command_opt = CMD_SHOWNODES;
			break;
		case 'p':
			machine_parsable = 1;
			break;
		case 'e':
			if (using_votequorum() > 0) {
				votes = strtol(optarg, &endptr, 0);
				if ((votes == 0 && endptr == optarg) || votes <= 0) {
					fprintf(stderr, "New expected votes value was not valid, try a positive number\n");
				} else {
					command_opt = CMD_SETEXPECTED;
				}
			} else {
				fprintf(stderr, "You cannot change expected votes, corosync is not using votequorum\n");
				exit(2);
			}
			break;
		case 'n':
			l = strtol(optarg, &endptr, 0);
			if ((l == 0 && endptr == optarg) || l < 0) {
				fprintf(stderr, "The nodeid was not valid, try a positive number\n");
				exit(2);
			}
			nodeid = l;
			nodeid_set = 1;
			break;
		case 'v':
			if (using_votequorum() > 0) {
				votes = strtol(optarg, &endptr, 0);
				if ((votes == 0 && endptr == optarg) || votes < 0) {
					fprintf(stderr, "New votes value was not valid, try a positive number or zero\n");
					exit(2);
				} else {
					command_opt = CMD_SETVOTES;
				}
			}
			else {
				fprintf(stderr, "You cannot change node votes, corosync is not using votequorum\n");
				exit(2);
			}
			break;
		case 'o':
			sortchar = optarg[0];
			switch (sortchar) {
			        case 'a': sort_opt = SORT_ADDR;
					break;
			        case 'i': sort_opt = SORT_NODEID;
					break;
			        case 'n': sort_opt = SORT_NODENAME;
					break;
			        default:
					fprintf(stderr, "Invalid ordering option. valid orders are a(address), i(node ID) or n(name)\n");
					exit(2);
					break;
			}
			break;
		case 'V':
			printf("corosync-quorumtool version: %s\n", VERSION);
			exit(0);
		case ':':
		case 'h':
		case '?':
		default:
			command_opt = CMD_UNKNOWN;
			break;
		}
	}

	switch (command_opt) {
	case CMD_UNKNOWN:
		show_usage(argv[0]);
		ret = -1;
		break;
	case CMD_SHOWNODES:
		ret = show_nodes(nodeid_format, address_format, sort_opt);
		break;
	case CMD_SHOWSTATUS:
		ret = show_status(nodeid_format, address_format, sort_opt);
		break;
	case CMD_SETVOTES:
		if (!nodeid_set) {
			nodeid = our_nodeid;
		}
		ret = set_votes(nodeid, votes);
		break;
	case CMD_SETEXPECTED:
		ret = set_expected(votes);
		break;
	case CMD_MONITOR:
		ret = monitor_status(nodeid_format, address_format, sort_opt);
		break;
	case CMD_UNREGISTER_QDEVICE:
		ret = unregister_qdevice();
		break;
	}

	close_all();

	return (ret);
}
