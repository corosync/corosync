/*
 * Copyright (c) 2009-2012 Red Hat, Inc.
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
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

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
static uint32_t g_quorate;
static uint64_t g_ring_id;
static uint32_t g_view_list_entries;
static uint32_t *g_view_list = NULL;
static uint32_t g_called;

/*
 * votequorum bits
 */
static votequorum_handle_t v_handle;
static votequorum_callbacks_t v_callbacks = {
	.votequorum_notify_fn = NULL,
	.votequorum_expectedvotes_notify_fn = NULL
};
static uint32_t our_nodeid = 0;
static uint8_t display_qdevice = 0;

/*
 * cfg bits
 */
static corosync_cfg_handle_t c_handle;
static corosync_cfg_callbacks_t c_callbacks = {
	.corosync_cfg_shutdown_callback = NULL
};

static void show_usage(const char *name)
{
	printf("usage: \n");
	printf("%s <options>\n", name);
	printf("\n");
	printf("  options:\n");
	printf("\n");
	printf("  -s             show quorum status\n");
	printf("  -m             monitor quorum status\n");
	printf("  -l             list nodes\n");
	printf("  -v <votes>     change the number of votes for a node (*)\n");
	printf("  -n <nodeid>    optional nodeid of node for -s or -v (*)\n");
	printf("  -e <expected>  change expected votes for the cluster (*)\n");
	printf("  -H             show nodeids in hexadecimal rather than decimal\n");
	printf("  -i             show node IP addresses instead of the resolved name\n");
#ifdef EXPERIMENTAL_QUORUM_DEVICE_API
	printf("  -f             forcefully unregister a quorum device *DANGEROUS* (*)\n");
#endif
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

static int get_votes(uint32_t nodeid)
{
	int votes = -1;
	struct votequorum_info info;

	if (votequorum_getinfo(v_handle, nodeid, &info) == CS_OK) {
		votes = info.node_votes;
	}

	return votes;
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
	static char buf[INET6_ADDRSTRLEN];
	socklen_t addrlen;
	struct sockaddr_storage *ss;

	err = corosync_cfg_get_node_addrs(c_handle, nodeid, INTERFACE_MAX, &numaddrs, addrs);
	if (err != CS_OK) {
		fprintf(stderr, "Unable to get node address for nodeid %u: %s\n", nodeid, cs_strerror(err));
		return "";
	}

	ss = (struct sockaddr_storage *)addrs[0].address;

	if (ss->ss_family == AF_INET6) {
		addrlen = sizeof(struct sockaddr_in6);
	} else {
		addrlen = sizeof(struct sockaddr_in);
	}

	if (!getnameinfo(
		(struct sockaddr *)addrs[0].address, addrlen,
		buf, sizeof(buf),
		NULL, 0,
		(name_format == ADDRESS_FORMAT_IP)?NI_NUMERICHOST:0)) {
			return buf;
	}

	return "";
}

static void quorum_notification_fn(
	quorum_handle_t handle,
	uint32_t quorate,
	uint64_t ring_id,
	uint32_t view_list_entries,
	uint32_t *view_list)
{
	g_called = 1;
	g_quorate = quorate;
	g_ring_id = ring_id;
	g_view_list_entries = view_list_entries;
	if (g_view_list) {
		free(g_view_list);
	}
	g_view_list = malloc(sizeof(uint32_t) * view_list_entries);
	if (g_view_list) {
		memcpy(g_view_list, view_list,sizeof(uint32_t) * view_list_entries);
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

static void display_nodes_data(uint32_t nodeid, nodeid_format_t nodeid_format, name_format_t name_format)
{
	int i;

	printf("\nMembership information\n");
	printf("----------------------\n");

	print_string_padded("Nodeid");
	if (v_handle) {
		print_string_padded("Votes");
	}
	printf("Name\n");

	for (i=0; i < g_view_list_entries; i++) {
		if (nodeid_format == NODEID_FORMAT_DECIMAL) {
			print_uint32_padded(g_view_list[i]);
		} else {
			printf("0x%08x ", g_view_list[i]);
		}
		if (v_handle) {
			print_uint32_padded(get_votes(g_view_list[i]));
		}
		printf("%s\n", node_name(g_view_list[i], name_format));
	}

	if (g_view_list_entries) {
		free(g_view_list);
		g_view_list = NULL;
	}

#ifdef EXPERIMENTAL_QUORUM_DEVICE_API
	if ((display_qdevice) && (v_handle)) {
		int err;
		struct votequorum_qdevice_info qinfo;

		err = votequorum_qdevice_getinfo(v_handle, nodeid, &qinfo);
		if (err != CS_OK) {
			fprintf(stderr, "Unable to get quorum device info: %s\n", cs_strerror(err));
		} else {
			if (nodeid_format == NODEID_FORMAT_DECIMAL) {
				print_uint32_padded(VOTEQUORUM_NODEID_QDEVICE);
			} else {
				printf("0x%08x ", VOTEQUORUM_NODEID_QDEVICE);
			}
			print_uint32_padded(qinfo.votes);
			printf("%s (%s)\n", qinfo.name, qinfo.state?"Voting":"Not voting");
		}
	}
#endif
}

static const char *decode_state(int state)
{
	switch(state) {
		case NODESTATE_MEMBER:
			return "Member";
			break;
		case NODESTATE_DEAD:
			return "Dead";
			break;
		case NODESTATE_LEAVING:
			return "Leaving";
			break;
		default:
			return "Unknown state (this is bad)";
			break;
	}
}

static int display_quorum_data(int is_quorate, uint32_t nodeid,
			       nodeid_format_t nodeid_format, name_format_t name_format,
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
	printf("Ring ID:          %" PRIu64 "\n", g_ring_id);
	printf("Quorate:          %s\n", is_quorate?"Yes":"No");

	if (!v_handle) {
		return CS_OK;
	}

	err=votequorum_getinfo(v_handle, nodeid, &info);
	if ((err == CS_OK) || (err == CS_ERR_NOT_EXIST)) {
		printf("\nVotequorum information\n");
		printf("----------------------\n");
		printf("Node ID:          %u\n", nodeid);
		printf("Node state:       ");
		if (err == CS_ERR_NOT_EXIST) {
			printf("Unknown\n");
			err = CS_OK;
			goto out;
		}
		printf("%s\n", decode_state(info.node_state));
		if (info.node_state != NODESTATE_MEMBER) {
			goto out;
		}
		printf("Node votes:       %d\n", info.node_votes);
		printf("Expected votes:   %d\n", info.node_expected_votes);
		printf("Highest expected: %d\n", info.highest_expected);
		printf("Total votes:      %d\n", info.total_votes);
		printf("Quorum:           %d %s\n", info.quorum, info.flags & VOTEQUORUM_INFO_FLAG_QUORATE?" ":"Activity blocked");
		printf("Flags:            ");
		if (info.flags & VOTEQUORUM_INFO_FLAG_TWONODE) printf("2Node ");
		if (info.flags & VOTEQUORUM_INFO_FLAG_QUORATE) printf("Quorate ");
		if (info.flags & VOTEQUORUM_INFO_WAIT_FOR_ALL) printf("WaitForAll ");
		if (info.flags & VOTEQUORUM_INFO_LAST_MAN_STANDING) printf("LastManStanding ");
		if (info.flags & VOTEQUORUM_INFO_AUTO_TIE_BREAKER) printf("AutoTieBreaker ");
		if (info.flags & VOTEQUORUM_INFO_LEAVE_REMOVE) printf("LeaveRemove ");
		if (info.flags & VOTEQUORUM_INFO_QDEVICE) {
			printf("Qdevice ");
			display_qdevice = 1;
		} else {
			display_qdevice = 0;
		}
		printf("\n");
	} else {
		fprintf(stderr, "Unable to get node %u info: %s\n", nodeid, cs_strerror(err));
	}

out:

	display_nodes_data(nodeid, nodeid_format, name_format);

	return err;
}

/*
 * return  1 if quorate
 *         0 if not quorate
 *        -1 on error
 */
static int show_status(uint32_t nodeid, nodeid_format_t nodeid_format, name_format_t name_format)
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

quorum_err:
	if (err != CS_OK) {
		return -1;
	}

	err = display_quorum_data(is_quorate, nodeid, nodeid_format, name_format, 0);
	if (err != CS_OK) {
		return -1;
	}

	return is_quorate;
}

static int monitor_status(nodeid_format_t nodeid_format, name_format_t name_format) {
	int err;
	int loop = 0;

	if (q_type == QUORUM_FREE) {
		printf("\nQuorum is not configured - cannot monitor\n");
		return show_status(our_nodeid, nodeid_format, name_format);
	}

	err=quorum_trackstart(q_handle, CS_TRACK_CHANGES);
	if (err != CS_OK) {
		fprintf(stderr, "Unable to start quorum status tracking: %s\n", cs_strerror(err));
		goto quorum_err;
	}

	while (1) {
		err = quorum_dispatch(q_handle, CS_DISPATCH_ONE);
		if (err != CS_OK) {
			fprintf(stderr, "Unable to dispatch quorum status: %s\n", cs_strerror(err));
			goto quorum_err;
		}
		err = display_quorum_data(g_quorate, our_nodeid, nodeid_format, name_format, loop);
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

static int show_nodes(nodeid_format_t nodeid_format, name_format_t name_format)
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

	display_nodes_data(our_nodeid, nodeid_format, name_format);

	result = EXIT_SUCCESS;
err_exit:
	return result;
}

static int unregister_qdevice(void)
{
#ifdef EXPERIMENTAL_QUORUM_DEVICE_API 
	int err;
	struct votequorum_qdevice_info qinfo;

	err = votequorum_qdevice_getinfo(v_handle, our_nodeid, &qinfo);
	if (err != CS_OK) {
		fprintf(stderr, "Unable to get quorum device info: %s\n", cs_strerror(err));
		return -1;
	}

	err = votequorum_qdevice_unregister(v_handle, qinfo.name);
	if (err != CS_OK) {
		fprintf(stderr, "Unable to unregister quorum device: %s\n", cs_strerror(err));
		return -1;
	}
	return 0;
#else
	fprintf(stderr, "votequorum quorum device support is not built-in\n");
	return -1;
#endif
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
		fprintf(stderr, "Unable to retrive this node nodeid\n");
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
	const char *options = "VHslmfe:v:hin:";
	char *endptr;
	int opt;
	int votes = 0;
	int ret = 0;
	uint32_t nodeid = 0;
	uint32_t nodeid_set = 0;
	nodeid_format_t nodeid_format = NODEID_FORMAT_DECIMAL;
	name_format_t address_format = ADDRESS_FORMAT_NAME;
	command_t command_opt = CMD_UNKNOWN;

	if (argc == 1) {
		show_usage (argv[0]);
		exit(0);
	}

	if (init_all()) {
		close_all();
		exit(1);
	}

	while ( (opt = getopt(argc, argv, options)) != -1 ) {
		switch (opt) {
#ifdef EXPERIMENTAL_QUORUM_DEVICE_API
		case 'f':
			if (using_votequorum() > 0) {
				command_opt = CMD_UNREGISTER_QDEVICE;
			} else {
				fprintf(stderr, "You cannot unregister quorum device, corosync is not using votequorum\n");
				exit(2);
			}
			break;
#endif
		case 's':
			command_opt = CMD_SHOWSTATUS;
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
			nodeid = strtol(optarg, &endptr, 0);
			if ((nodeid == 0 && endptr == optarg) || nodeid < 0) {
				fprintf(stderr, "The nodeid was not valid, try a positive number\n");
				exit(2);
			}
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
		ret = show_nodes(nodeid_format, address_format);
		break;
	case CMD_SHOWSTATUS:
		if (!nodeid_set) {
			nodeid = our_nodeid;
		}
		ret = show_status(nodeid, nodeid_format, address_format);
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
		ret = monitor_status(nodeid_format, address_format);
		break;
	case CMD_UNREGISTER_QDEVICE:
		ret = unregister_qdevice();
		break;
	}

	close_all();

	return (ret);
}
