/*
 * Copyright (c) 2006-2013 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Patrick Caulfield (pcaulfie@redhat.com)
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

#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <limits.h>
#include <stddef.h>
#include <grp.h>
#include <pwd.h>

#include <corosync/list.h>
#include <qb/qbutil.h>
#define LOGSYS_UTILS_ONLY 1
#include <corosync/logsys.h>
#include <corosync/icmap.h>

#include "main.h"
#include "util.h"

enum parser_cb_type {
	PARSER_CB_START,
	PARSER_CB_END,
	PARSER_CB_SECTION_START,
	PARSER_CB_SECTION_END,
	PARSER_CB_ITEM,
};

enum main_cp_cb_data_state {
	MAIN_CP_CB_DATA_STATE_NORMAL,
	MAIN_CP_CB_DATA_STATE_TOTEM,
	MAIN_CP_CB_DATA_STATE_INTERFACE,
	MAIN_CP_CB_DATA_STATE_LOGGER_SUBSYS,
	MAIN_CP_CB_DATA_STATE_UIDGID,
	MAIN_CP_CB_DATA_STATE_LOGGING_DAEMON,
	MAIN_CP_CB_DATA_STATE_MEMBER,
	MAIN_CP_CB_DATA_STATE_QUORUM,
	MAIN_CP_CB_DATA_STATE_QDEVICE,
	MAIN_CP_CB_DATA_STATE_NODELIST,
	MAIN_CP_CB_DATA_STATE_NODELIST_NODE,
	MAIN_CP_CB_DATA_STATE_PLOAD,
	MAIN_CP_CB_DATA_STATE_QB,
	MAIN_CP_CB_DATA_STATE_RESOURCES,
	MAIN_CP_CB_DATA_STATE_RESOURCES_SYSTEM,
	MAIN_CP_CB_DATA_STATE_RESOURCES_PROCESS,
	MAIN_CP_CB_DATA_STATE_RESOURCES_SYSTEM_MEMUSED,
	MAIN_CP_CB_DATA_STATE_RESOURCES_PROCESS_MEMUSED
};

typedef int (*parser_cb_f)(const char *path,
			char *key,
			char *value,
			enum main_cp_cb_data_state *state,
			enum parser_cb_type type,
			const char **error_string,
			icmap_map_t config_map,
			void *user_data);

struct key_value_list_item {
	char *key;
	char *value;
	struct list_head list;
};

struct main_cp_cb_data {
	int ringnumber;
	char *bindnetaddr;
	char *mcastaddr;
	char *broadcast;
	int mcastport;
	int ttl;

	struct list_head logger_subsys_items_head;
	char *subsys;
	char *logging_daemon_name;
	struct list_head member_items_head;

	int node_number;
	int ring0_addr_added;
};

static int read_config_file_into_icmap(
	const char **error_string, icmap_map_t config_map);
static char error_string_response[512];

static int uid_determine (const char *req_user)
{
	int pw_uid = 0;
	struct passwd passwd;
	struct passwd* pwdptr = &passwd;
	struct passwd* temp_pwd_pt;
	char *pwdbuffer;
	int  pwdlinelen, rc;
	long int id;
	char *ep;

	id = strtol(req_user, &ep, 10);
	if (*ep == '\0' && id >= 0 && id <= UINT_MAX) {
		return (id);
	}

	pwdlinelen = sysconf (_SC_GETPW_R_SIZE_MAX);

	if (pwdlinelen == -1) {
	        pwdlinelen = 256;
	}

	pwdbuffer = malloc (pwdlinelen);

	while ((rc = getpwnam_r (req_user, pwdptr, pwdbuffer, pwdlinelen, &temp_pwd_pt)) == ERANGE) {
		char *n;

		pwdlinelen *= 2;
		if (pwdlinelen <= 32678) {
			n = realloc (pwdbuffer, pwdlinelen);
			if (n != NULL) {
				pwdbuffer = n;
				continue;
			}
		}
	}
	if (rc != 0) {
		free (pwdbuffer);
	        sprintf (error_string_response, "getpwnam_r(): %s", strerror(rc));
	        return (-1);
	}
	if (temp_pwd_pt == NULL) {
		free (pwdbuffer);
	        sprintf (error_string_response,
	                "The '%s' user is not found in /etc/passwd, please read the documentation.",
	                req_user);
	        return (-1);
	}
	pw_uid = passwd.pw_uid;
	free (pwdbuffer);

	return pw_uid;
}

static int gid_determine (const char *req_group)
{
	int corosync_gid = 0;
	struct group group;
	struct group * grpptr = &group;
	struct group * temp_grp_pt;
	char *grpbuffer;
	int  grplinelen, rc;
	long int id;
	char *ep;

	id = strtol(req_group, &ep, 10);
	if (*ep == '\0' && id >= 0 && id <= UINT_MAX) {
		return (id);
	}

	grplinelen = sysconf (_SC_GETGR_R_SIZE_MAX);

	if (grplinelen == -1) {
	        grplinelen = 256;
	}

	grpbuffer = malloc (grplinelen);

	while ((rc = getgrnam_r (req_group, grpptr, grpbuffer, grplinelen, &temp_grp_pt)) == ERANGE) {
		char *n;

		grplinelen *= 2;
		if (grplinelen <= 32678) {
			n = realloc (grpbuffer, grplinelen);
			if (n != NULL) {
				grpbuffer = n;
				continue;
			}
		}
	}
	if (rc != 0) {
		free (grpbuffer);
	        sprintf (error_string_response, "getgrnam_r(): %s", strerror(rc));
	        return (-1);
	}
	if (temp_grp_pt == NULL) {
		free (grpbuffer);
	        sprintf (error_string_response,
	                "The '%s' group is not found in /etc/group, please read the documentation.",
	                req_group);
		return (-1);
	}
	corosync_gid = group.gr_gid;
	free (grpbuffer);

	return corosync_gid;
}
static char *strchr_rs (const char *haystack, int byte)
{
	const char *end_address = strchr (haystack, byte);
	if (end_address) {
		end_address += 1; /* skip past { or = */

		while (*end_address == ' ' || *end_address == '\t')
			end_address++;
	}

	return ((char *) end_address);
}

int coroparse_configparse (icmap_map_t config_map, const char **error_string)
{
	if (read_config_file_into_icmap(error_string, config_map)) {
		return -1;
	}

	return 0;
}

static char *remove_whitespace(char *string, int remove_colon_and_brace)
{
	char *start;
	char *end;

	start = string;
	while (*start == ' ' || *start == '\t')
		start++;

	end = start+(strlen(start))-1;
	while ((*end == ' ' || *end == '\t' || (remove_colon_and_brace && (*end == ':' || *end == '{'))) && end > start)
		end--;
	if (end != start)
		*(end+1) = '\0';

	return start;
}



static int parse_section(FILE *fp,
			 char *path,
			 const char **error_string,
			 int depth,
			 enum main_cp_cb_data_state state,
			 parser_cb_f parser_cb,
			 icmap_map_t config_map,
			 void *user_data)
{
	char line[512];
	int i;
	char *loc;
	int ignore_line;
	char new_keyname[ICMAP_KEYNAME_MAXLEN];

	if (strcmp(path, "") == 0) {
		parser_cb("", NULL, NULL, &state, PARSER_CB_START, error_string, config_map, user_data);
	}

	while (fgets (line, sizeof (line), fp)) {
		if (strlen(line) > 0) {
			if (line[strlen(line) - 1] == '\n')
				line[strlen(line) - 1] = '\0';
			if (strlen (line) > 0 && line[strlen(line) - 1] == '\r')
				line[strlen(line) - 1] = '\0';
		}
		/*
		 * Clear out white space and tabs
		 */
		for (i = strlen (line) - 1; i > -1; i--) {
			if (line[i] == '\t' || line[i] == ' ') {
				line[i] = '\0';
			} else {
				break;
			}
		}

		ignore_line = 1;
		for (i = 0; i < strlen (line); i++) {
			if (line[i] != '\t' && line[i] != ' ') {
				if (line[i] != '#')
					ignore_line = 0;

				break;
			}
		}
		/*
		 * Clear out comments and empty lines
		 */
		if (ignore_line) {
			continue;
		}

		/* New section ? */
		if ((loc = strchr_rs (line, '{'))) {
			char *section = remove_whitespace(line, 1);
			enum main_cp_cb_data_state newstate;

			loc--;
			*loc = '\0';

			if (strlen(path) + strlen(section) + 1 >= ICMAP_KEYNAME_MAXLEN) {
				*error_string = "parser error: Start of section makes total cmap path too long";
				return -1;
			}
			strcpy(new_keyname, path);
			if (strcmp(path, "") != 0) {
				strcat(new_keyname, ".");
			}
			strcat(new_keyname, section);

			/* Only use the new state for items further down the stack */
			newstate = state;
			if (!parser_cb(new_keyname, NULL, NULL, &newstate, PARSER_CB_SECTION_START, error_string, config_map, user_data)) {
				return -1;
			}

			if (parse_section(fp, new_keyname, error_string, depth + 1, newstate, parser_cb, config_map, user_data))
				return -1;

			continue ;
		}

		/* New key/value */
		if ((loc = strchr_rs (line, ':'))) {
			char *key;
			char *value;

			*(loc-1) = '\0';
			key = remove_whitespace(line, 1);
			value = remove_whitespace(loc, 0);

			if (strlen(path) + strlen(key) + 1 >= ICMAP_KEYNAME_MAXLEN) {
				*error_string = "parser error: New key makes total cmap path too long";
				return -1;
			}
			strcpy(new_keyname, path);
			if (strcmp(path, "") != 0) {
				strcat(new_keyname, ".");
			}
			strcat(new_keyname, key);

			if (!parser_cb(new_keyname, key, value, &state, PARSER_CB_ITEM, error_string, config_map, user_data)) {
				return -1;
			}

			continue ;
		}

		if (strchr_rs (line, '}')) {
			if (depth == 0) {
				*error_string = "parser error: Unexpected closing brace";

				return -1;
			}

			if (!parser_cb(path, NULL, NULL, &state, PARSER_CB_SECTION_END, error_string, config_map, user_data)) {
				return -1;
			}

			return 0;
		}
	}

	if (strcmp(path, "") != 0) {
		*error_string = "parser error: Missing closing brace";
		return -1;
	}

	if (strcmp(path, "") == 0) {
		parser_cb("", NULL, NULL, &state, PARSER_CB_END, error_string, config_map, user_data);
	}

	return 0;
}

static int safe_atoq_range(icmap_value_types_t value_type, long long int *min_val, long long int *max_val)
{
	switch (value_type) {
	case ICMAP_VALUETYPE_INT8: *min_val = INT8_MIN; *max_val = INT8_MAX; break;
	case ICMAP_VALUETYPE_UINT8: *min_val = 0; *max_val = UINT8_MAX; break;
	case ICMAP_VALUETYPE_INT16: *min_val = INT16_MIN; *max_val = INT16_MAX; break;
	case ICMAP_VALUETYPE_UINT16: *min_val = 0; *max_val = UINT16_MAX; break;
	case ICMAP_VALUETYPE_INT32: *min_val = INT32_MIN; *max_val = INT32_MAX; break;
	case ICMAP_VALUETYPE_UINT32: *min_val = 0; *max_val = UINT32_MAX; break;
	default:
		return (-1);
	}

	return (0);
}

/*
 * Convert string str to long long int res. Type of result is target_type and currently only
 * ICMAP_VALUETYPE_[U]INT[8|16|32] is supported.
 * Return 0 on success, -1 on failure.
 */
static int safe_atoq(const char *str, long long int *res, icmap_value_types_t target_type)
{
	long long int val;
	long long int min_val, max_val;
	char *endptr;

	errno = 0;

	val = strtoll(str, &endptr, 10);
	if (errno == ERANGE) {
		return (-1);
	}

	if (endptr == str) {
		return (-1);
	}

	if (*endptr != '\0') {
		return (-1);
	}

	if (safe_atoq_range(target_type, &min_val, &max_val) != 0) {
		return (-1);
	}

	if (val < min_val || val > max_val) {
		return (-1);
	}

	*res = val;
	return (0);
}

static int str_to_ull(const char *str, unsigned long long int *res)
{
	unsigned long long int val;
	char *endptr;

	errno = 0;

	val = strtoull(str, &endptr, 10);
	if (errno == ERANGE) {
		return (-1);
	}

	if (endptr == str) {
		return (-1);
	}

	if (*endptr != '\0') {
		return (-1);
	}

	*res = val;
	return (0);
}

static int main_config_parser_cb(const char *path,
			char *key,
			char *value,
			enum main_cp_cb_data_state *state,
			enum parser_cb_type type,
			const char **error_string,
			icmap_map_t config_map,
			void *user_data)
{
	int ii;
	long long int val;
	long long int min_val, max_val;
	icmap_value_types_t val_type = ICMAP_VALUETYPE_BINARY;
	unsigned long long int ull;
	int add_as_string;
	char key_name[ICMAP_KEYNAME_MAXLEN];
	static char formated_err[256];
	struct main_cp_cb_data *data = (struct main_cp_cb_data *)user_data;
	struct key_value_list_item *kv_item;
	struct list_head *iter, *iter_next;
	int uid, gid;

	switch (type) {
	case PARSER_CB_START:
		memset(data, 0, sizeof(struct main_cp_cb_data));
		*state = MAIN_CP_CB_DATA_STATE_NORMAL;
		break;
	case PARSER_CB_END:
		break;
	case PARSER_CB_ITEM:
		add_as_string = 1;

		switch (*state) {
		case MAIN_CP_CB_DATA_STATE_NORMAL:
			break;
		case MAIN_CP_CB_DATA_STATE_PLOAD:
			if ((strcmp(path, "pload.count") == 0) ||
			    (strcmp(path, "pload.size") == 0)) {
				val_type = ICMAP_VALUETYPE_UINT32;
				if (safe_atoq(value, &val, val_type) != 0) {
					goto atoi_error;
				}
				icmap_set_uint32_r(config_map, path, val);
				add_as_string = 0;
			}
			break;
		case MAIN_CP_CB_DATA_STATE_QUORUM:
			if ((strcmp(path, "quorum.expected_votes") == 0) ||
			    (strcmp(path, "quorum.votes") == 0) ||
			    (strcmp(path, "quorum.last_man_standing_window") == 0) ||
			    (strcmp(path, "quorum.leaving_timeout") == 0)) {
				val_type = ICMAP_VALUETYPE_UINT32;
				if (safe_atoq(value, &val, val_type) != 0) {
					goto atoi_error;
				}
				icmap_set_uint32_r(config_map, path, val);
				add_as_string = 0;
			}

			if ((strcmp(path, "quorum.two_node") == 0) ||
			    (strcmp(path, "quorum.expected_votes_tracking") == 0) ||
			    (strcmp(path, "quorum.allow_downscale") == 0) ||
			    (strcmp(path, "quorum.wait_for_all") == 0) ||
			    (strcmp(path, "quorum.auto_tie_breaker") == 0) ||
			    (strcmp(path, "quorum.last_man_standing") == 0)) {
				val_type = ICMAP_VALUETYPE_UINT8;
				if (safe_atoq(value, &val, val_type) != 0) {
					goto atoi_error;
				}
				icmap_set_uint8_r(config_map, path, val);
				add_as_string = 0;
			}
			break;
		case MAIN_CP_CB_DATA_STATE_QDEVICE:
			if ((strcmp(path, "quorum.device.timeout") == 0) ||
			    (strcmp(path, "quorum.device.sync_timeout") == 0) ||
			    (strcmp(path, "quorum.device.votes") == 0)) {
				val_type = ICMAP_VALUETYPE_UINT32;
				if (safe_atoq(value, &val, val_type) != 0) {
					goto atoi_error;
				}
				icmap_set_uint32_r(config_map, path, val);
				add_as_string = 0;
			}
			if ((strcmp(path, "quorum.device.master_wins") == 0)) {
				val_type = ICMAP_VALUETYPE_UINT8;
				if (safe_atoq(value, &val, val_type) != 0) {
					goto atoi_error;
				}
				icmap_set_uint8_r(config_map, path, val);
				add_as_string = 0;
			}
			break;
		case MAIN_CP_CB_DATA_STATE_TOTEM:
			if ((strcmp(path, "totem.version") == 0) ||
			    (strcmp(path, "totem.nodeid") == 0) ||
			    (strcmp(path, "totem.threads") == 0) ||
			    (strcmp(path, "totem.token") == 0) ||
			    (strcmp(path, "totem.token_coefficient") == 0) ||
			    (strcmp(path, "totem.token_retransmit") == 0) ||
			    (strcmp(path, "totem.hold") == 0) ||
			    (strcmp(path, "totem.token_retransmits_before_loss_const") == 0) ||
			    (strcmp(path, "totem.join") == 0) ||
			    (strcmp(path, "totem.send_join") == 0) ||
			    (strcmp(path, "totem.consensus") == 0) ||
			    (strcmp(path, "totem.merge") == 0) ||
			    (strcmp(path, "totem.downcheck") == 0) ||
			    (strcmp(path, "totem.fail_recv_const") == 0) ||
			    (strcmp(path, "totem.seqno_unchanged_const") == 0) ||
			    (strcmp(path, "totem.rrp_token_expired_timeout") == 0) ||
			    (strcmp(path, "totem.rrp_problem_count_timeout") == 0) ||
			    (strcmp(path, "totem.rrp_problem_count_threshold") == 0) ||
			    (strcmp(path, "totem.rrp_problem_count_mcast_threshold") == 0) ||
			    (strcmp(path, "totem.rrp_autorecovery_check_timeout") == 0) ||
			    (strcmp(path, "totem.heartbeat_failures_allowed") == 0) ||
			    (strcmp(path, "totem.max_network_delay") == 0) ||
			    (strcmp(path, "totem.window_size") == 0) ||
			    (strcmp(path, "totem.max_messages") == 0) ||
			    (strcmp(path, "totem.miss_count_const") == 0) ||
			    (strcmp(path, "totem.netmtu") == 0)) {
				val_type = ICMAP_VALUETYPE_UINT32;
				if (safe_atoq(value, &val, val_type) != 0) {
					goto atoi_error;
				}
				icmap_set_uint32_r(config_map,path, val);
				add_as_string = 0;
			}
			if (strcmp(path, "totem.config_version") == 0) {
				if (str_to_ull(value, &ull) != 0) {
					goto atoi_error;
				}
				icmap_set_uint64_r(config_map, path, ull);
				add_as_string = 0;
			}
			if (strcmp(path, "totem.ip_version") == 0) {
				if ((strcmp(value, "ipv4") != 0) &&
				    (strcmp(value, "ipv6") != 0)) {
					*error_string = "Invalid ip_version type";

					return (0);
				}
			}
			if (strcmp(path, "totem.crypto_type") == 0) {
				if ((strcmp(value, "nss") != 0) &&
				    (strcmp(value, "aes256") != 0) &&
				    (strcmp(value, "aes192") != 0) &&
				    (strcmp(value, "aes128") != 0) &&
				    (strcmp(value, "3des") != 0)) {
					*error_string = "Invalid crypto type";

					return (0);
				}
			}
			if (strcmp(path, "totem.crypto_cipher") == 0) {
				if ((strcmp(value, "none") != 0) &&
				    (strcmp(value, "aes256") != 0) &&
				    (strcmp(value, "aes192") != 0) &&
				    (strcmp(value, "aes128") != 0) &&
				    (strcmp(value, "3des") != 0)) {
					*error_string = "Invalid cipher type";

					return (0);
				}
			}
			if (strcmp(path, "totem.crypto_hash") == 0) {
				if ((strcmp(value, "none") != 0) &&
				    (strcmp(value, "md5") != 0) &&
				    (strcmp(value, "sha1") != 0) &&
				    (strcmp(value, "sha256") != 0) &&
				    (strcmp(value, "sha384") != 0) &&
				    (strcmp(value, "sha512") != 0)) {
					*error_string = "Invalid hash type";

					return (0);
				}
			}
			break;

		case MAIN_CP_CB_DATA_STATE_QB:
			if (strcmp(path, "qb.ipc_type") == 0) {
				if ((strcmp(value, "native") != 0) &&
				    (strcmp(value, "shm") != 0) &&
				    (strcmp(value, "socket") != 0)) {
					*error_string = "Invalid qb ipc_type";

					return (0);
				}
			}
			break;

		case MAIN_CP_CB_DATA_STATE_INTERFACE:
			if (strcmp(path, "totem.interface.ringnumber") == 0) {
				val_type = ICMAP_VALUETYPE_UINT8;
				if (safe_atoq(value, &val, val_type) != 0) {
					goto atoi_error;
				}

				data->ringnumber = val;
				add_as_string = 0;
			}
			if (strcmp(path, "totem.interface.bindnetaddr") == 0) {
				data->bindnetaddr = strdup(value);
				add_as_string = 0;
			}
			if (strcmp(path, "totem.interface.mcastaddr") == 0) {
				data->mcastaddr = strdup(value);
				add_as_string = 0;
			}
			if (strcmp(path, "totem.interface.broadcast") == 0) {
				data->broadcast = strdup(value);
				add_as_string = 0;
			}
			if (strcmp(path, "totem.interface.mcastport") == 0) {
				val_type = ICMAP_VALUETYPE_UINT16;
				if (safe_atoq(value, &val, val_type) != 0) {
					goto atoi_error;
				}
				data->mcastport = val;
				add_as_string = 0;
			}
			if (strcmp(path, "totem.interface.ttl") == 0) {
				val_type = ICMAP_VALUETYPE_UINT8;
				if (safe_atoq(value, &val, val_type) != 0) {
					goto atoi_error;
				}
				data->ttl = val;
				add_as_string = 0;
			}
			break;
		case MAIN_CP_CB_DATA_STATE_LOGGER_SUBSYS:
			if (strcmp(key, "subsys") == 0) {
				data->subsys = strdup(value);
				if (data->subsys == NULL) {
					*error_string = "Can't alloc memory";

					return (0);
				}
			} else {
				kv_item = malloc(sizeof(*kv_item));
				if (kv_item == NULL) {
					*error_string = "Can't alloc memory";

					return (0);
				}
				memset(kv_item, 0, sizeof(*kv_item));

				kv_item->key = strdup(key);
				kv_item->value = strdup(value);
				if (kv_item->key == NULL || kv_item->value == NULL) {
					free(kv_item);
					*error_string = "Can't alloc memory";

					return (0);
				}
				list_init(&kv_item->list);
				list_add(&kv_item->list, &data->logger_subsys_items_head);
			}
			add_as_string = 0;
			break;
		case MAIN_CP_CB_DATA_STATE_LOGGING_DAEMON:
			if (strcmp(key, "subsys") == 0) {
				data->subsys = strdup(value);
				if (data->subsys == NULL) {
					*error_string = "Can't alloc memory";

					return (0);
				}
			} else if (strcmp(key, "name") == 0) {
				data->logging_daemon_name = strdup(value);
				if (data->logging_daemon_name == NULL) {
					*error_string = "Can't alloc memory";

					return (0);
				}
			} else {
				kv_item = malloc(sizeof(*kv_item));
				if (kv_item == NULL) {
					*error_string = "Can't alloc memory";

					return (0);
				}
				memset(kv_item, 0, sizeof(*kv_item));

				kv_item->key = strdup(key);
				kv_item->value = strdup(value);
				if (kv_item->key == NULL || kv_item->value == NULL) {
					free(kv_item);
					*error_string = "Can't alloc memory";

					return (0);
				}
				list_init(&kv_item->list);
				list_add(&kv_item->list, &data->logger_subsys_items_head);
			}
			add_as_string = 0;
			break;
		case MAIN_CP_CB_DATA_STATE_UIDGID:
			if (strcmp(key, "uid") == 0) {
				uid = uid_determine(value);
				if (uid == -1) {
					*error_string = error_string_response;
					return (0);
				}
				snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "uidgid.config.uid.%u",
						uid);
				icmap_set_uint8_r(config_map, key_name, 1);
				add_as_string = 0;
			} else if (strcmp(key, "gid") == 0) {
				gid = gid_determine(value);
				if (gid == -1) {
					*error_string = error_string_response;
					return (0);
				}
				snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "uidgid.config.gid.%u",
						gid);
				icmap_set_uint8_r(config_map, key_name, 1);
				add_as_string = 0;
			} else {
				*error_string = "uidgid: Only uid and gid are allowed items";
				return (0);
			}
			break;
		case MAIN_CP_CB_DATA_STATE_MEMBER:
			if (strcmp(key, "memberaddr") != 0) {
				*error_string = "Only memberaddr is allowed in member section";

				return (0);
			}

			kv_item = malloc(sizeof(*kv_item));
			if (kv_item == NULL) {
				*error_string = "Can't alloc memory";

				return (0);
			}
			memset(kv_item, 0, sizeof(*kv_item));

			kv_item->key = strdup(key);
			kv_item->value = strdup(value);
			if (kv_item->key == NULL || kv_item->value == NULL) {
				free(kv_item);
				*error_string = "Can't alloc memory";

				return (0);
			}
			list_init(&kv_item->list);
			list_add(&kv_item->list, &data->member_items_head);
			add_as_string = 0;
			break;
		case MAIN_CP_CB_DATA_STATE_NODELIST:
			break;
		case MAIN_CP_CB_DATA_STATE_NODELIST_NODE:
			snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "nodelist.node.%u.%s", data->node_number, key);
			if ((strcmp(key, "nodeid") == 0) ||
			    (strcmp(key, "quorum_votes") == 0)) {
				val_type = ICMAP_VALUETYPE_UINT32;
				if (safe_atoq(value, &val, val_type) != 0) {
					goto atoi_error;
				}

				icmap_set_uint32_r(config_map, key_name, val);
				add_as_string = 0;
			}

			if (strcmp(key, "ring0_addr") == 0) {
				data->ring0_addr_added = 1;
			}

			if (add_as_string) {
				icmap_set_string_r(config_map, key_name, value);
				add_as_string = 0;
			}
			break;
		case MAIN_CP_CB_DATA_STATE_RESOURCES:
			if (strcmp(key, "watchdog_timeout") == 0) {
				val_type = ICMAP_VALUETYPE_UINT32;
				if (safe_atoq(value, &val, val_type) != 0) {
					goto atoi_error;
				}
				icmap_set_uint32_r(config_map,path, val);
				add_as_string = 0;
			}
			break;
		case MAIN_CP_CB_DATA_STATE_RESOURCES_SYSTEM:
			break;
		case MAIN_CP_CB_DATA_STATE_RESOURCES_SYSTEM_MEMUSED:
			if (strcmp(key, "poll_period") == 0) {
				if (str_to_ull(value, &ull) != 0) {
					goto atoi_error;
				}
				icmap_set_uint64_r(config_map,path, ull);
				add_as_string = 0;
			}
			break;
		case MAIN_CP_CB_DATA_STATE_RESOURCES_PROCESS:
			break;
		case MAIN_CP_CB_DATA_STATE_RESOURCES_PROCESS_MEMUSED:
			if (strcmp(key, "poll_period") == 0) {
				if (str_to_ull(value, &ull) != 0) {
					goto atoi_error;
				}
				icmap_set_uint64_r(config_map,path, ull);
				add_as_string = 0;
			}
			break;
		}

		if (add_as_string) {
			icmap_set_string_r(config_map, path, value);
		}
		break;
	case PARSER_CB_SECTION_START:
		if (strcmp(path, "totem.interface") == 0) {
			*state = MAIN_CP_CB_DATA_STATE_INTERFACE;
			data->ringnumber = 0;
			data->mcastport = -1;
			data->ttl = -1;
			list_init(&data->member_items_head);
		};
		if (strcmp(path, "totem") == 0) {
			*state = MAIN_CP_CB_DATA_STATE_TOTEM;
		};
		if (strcmp(path, "qb") == 0) {
			*state = MAIN_CP_CB_DATA_STATE_QB;
		}
		if (strcmp(path, "logging.logger_subsys") == 0) {
			*state = MAIN_CP_CB_DATA_STATE_LOGGER_SUBSYS;
			list_init(&data->logger_subsys_items_head);
			data->subsys = NULL;
		}
		if (strcmp(path, "logging.logging_daemon") == 0) {
			*state = MAIN_CP_CB_DATA_STATE_LOGGING_DAEMON;
			list_init(&data->logger_subsys_items_head);
			data->subsys = NULL;
			data->logging_daemon_name = NULL;
		}
		if (strcmp(path, "uidgid") == 0) {
			*state = MAIN_CP_CB_DATA_STATE_UIDGID;
		}
		if (strcmp(path, "totem.interface.member") == 0) {
			*state = MAIN_CP_CB_DATA_STATE_MEMBER;
		}
		if (strcmp(path, "quorum") == 0) {
			*state = MAIN_CP_CB_DATA_STATE_QUORUM;
		}
		if (strcmp(path, "quorum.device") == 0) {
			*state = MAIN_CP_CB_DATA_STATE_QDEVICE;
		}
		if (strcmp(path, "nodelist") == 0) {
			*state = MAIN_CP_CB_DATA_STATE_NODELIST;
			data->node_number = 0;
		}
		if (strcmp(path, "nodelist.node") == 0) {
			*state = MAIN_CP_CB_DATA_STATE_NODELIST_NODE;
			data->ring0_addr_added = 0;
		}
		if (strcmp(path, "resources") == 0) {
			*state = MAIN_CP_CB_DATA_STATE_RESOURCES;
		}
		if (strcmp(path, "resources.system") == 0) {
			*state = MAIN_CP_CB_DATA_STATE_RESOURCES_SYSTEM;
		}
		if (strcmp(path, "resources.system.memory_used") == 0) {
			*state = MAIN_CP_CB_DATA_STATE_RESOURCES_SYSTEM_MEMUSED;
		}
		if (strcmp(path, "resources.process") == 0) {
			*state = MAIN_CP_CB_DATA_STATE_RESOURCES_PROCESS;
		}
		if (strcmp(path, "resources.process.memory_used") == 0) {
			*state = MAIN_CP_CB_DATA_STATE_RESOURCES_PROCESS_MEMUSED;
		}
		break;
	case PARSER_CB_SECTION_END:
		switch (*state) {
		case MAIN_CP_CB_DATA_STATE_INTERFACE:
			/*
			 * Create new interface section
			 */
			if (data->bindnetaddr != NULL) {
				snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "totem.interface.%u.bindnetaddr",
						data->ringnumber);
				icmap_set_string_r(config_map, key_name, data->bindnetaddr);

				free(data->bindnetaddr);
				data->bindnetaddr = NULL;
			}

			if (data->mcastaddr != NULL) {
				snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "totem.interface.%u.mcastaddr",
						data->ringnumber);
				icmap_set_string_r(config_map, key_name, data->mcastaddr);

				free(data->mcastaddr);
				data->mcastaddr = NULL;
			}

			if (data->broadcast != NULL) {
				snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "totem.interface.%u.broadcast",
						data->ringnumber);
				icmap_set_string_r(config_map, key_name, data->broadcast);

				free(data->broadcast);
				data->broadcast = NULL;
			}

			if (data->mcastport > -1) {
				snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "totem.interface.%u.mcastport",
						data->ringnumber);
				icmap_set_uint16_r(config_map, key_name, data->mcastport);
			}

			if (data->ttl > -1) {
				snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "totem.interface.%u.ttl",
						data->ringnumber);
				icmap_set_uint8_r(config_map, key_name, data->ttl);
			}

			ii = 0;
			for (iter = data->member_items_head.next;
			     iter != &data->member_items_head; iter = iter_next) {
				kv_item = list_entry(iter, struct key_value_list_item, list);

				snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "totem.interface.%u.member.%u",
						data->ringnumber, ii);
				icmap_set_string_r(config_map, key_name, kv_item->value);

				iter_next = iter->next;

				free(kv_item->value);
				free(kv_item->key);
				free(kv_item);
				ii++;
			}

			break;
		case MAIN_CP_CB_DATA_STATE_LOGGER_SUBSYS:
			if (data->subsys == NULL) {
				*error_string = "No subsys key in logger_subsys directive";

				return (0);
			}

			for (iter = data->logger_subsys_items_head.next;
			     iter != &data->logger_subsys_items_head; iter = iter_next) {
				kv_item = list_entry(iter, struct key_value_list_item, list);

				snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "logging.logger_subsys.%s.%s",
					 data->subsys, kv_item->key);
				icmap_set_string_r(config_map, key_name, kv_item->value);

				iter_next = iter->next;

				free(kv_item->value);
				free(kv_item->key);
				free(kv_item);
			}

			snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "logging.logger_subsys.%s.subsys",
					data->subsys);
			icmap_set_string_r(config_map, key_name, data->subsys);

			free(data->subsys);

			break;
		case MAIN_CP_CB_DATA_STATE_LOGGING_DAEMON:
			if (data->logging_daemon_name == NULL) {
				*error_string = "No name key in logging_daemon directive";

				return (0);
			}

			for (iter = data->logger_subsys_items_head.next;
			     iter != &data->logger_subsys_items_head; iter = iter_next) {
				kv_item = list_entry(iter, struct key_value_list_item, list);

				if (data->subsys == NULL) {
					if (strcmp(data->logging_daemon_name, "corosync") == 0) {
						snprintf(key_name, ICMAP_KEYNAME_MAXLEN,
								"logging.%s",
								kv_item->key);
					} else {
						snprintf(key_name, ICMAP_KEYNAME_MAXLEN,
								"logging.logging_daemon.%s.%s",
								data->logging_daemon_name, kv_item->key);
					}
				} else {
					if (strcmp(data->logging_daemon_name, "corosync") == 0) {
						snprintf(key_name, ICMAP_KEYNAME_MAXLEN,
								"logging.logger_subsys.%s.%s",
								data->subsys,
								kv_item->key);
					} else {
						snprintf(key_name, ICMAP_KEYNAME_MAXLEN,
								"logging.logging_daemon.%s.%s.%s",
								data->logging_daemon_name, data->subsys,
								kv_item->key);
					}
				}
				icmap_set_string_r(config_map, key_name, kv_item->value);

				iter_next = iter->next;

				free(kv_item->value);
				free(kv_item->key);
				free(kv_item);
			}

			if (data->subsys == NULL) {
				if (strcmp(data->logging_daemon_name, "corosync") != 0) {
					snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "logging.logging_daemon.%s.name",
							data->logging_daemon_name);
					icmap_set_string_r(config_map, key_name, data->logging_daemon_name);
				}
			} else {
				if (strcmp(data->logging_daemon_name, "corosync") == 0) {
					snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "logging.logger_subsys.%s.subsys",
							data->subsys);
					icmap_set_string_r(config_map, key_name, data->subsys);

				} else {
					snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "logging.logging_daemon.%s.%s.subsys",
							data->logging_daemon_name, data->subsys);
					icmap_set_string_r(config_map, key_name, data->subsys);
					snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "logging.logging_daemon.%s.%s.name",
							data->logging_daemon_name, data->subsys);
					icmap_set_string_r(config_map, key_name, data->logging_daemon_name);
				}
			}

			free(data->subsys);
			free(data->logging_daemon_name);

			break;
		case MAIN_CP_CB_DATA_STATE_NODELIST_NODE:
			if (!data->ring0_addr_added) {
				*error_string = "No ring0_addr specified for node";

				return (0);
			}
			data->node_number++;
			break;
		case MAIN_CP_CB_DATA_STATE_NORMAL:
		case MAIN_CP_CB_DATA_STATE_PLOAD:
		case MAIN_CP_CB_DATA_STATE_UIDGID:
		case MAIN_CP_CB_DATA_STATE_MEMBER:
		case MAIN_CP_CB_DATA_STATE_QUORUM:
		case MAIN_CP_CB_DATA_STATE_QDEVICE:
		case MAIN_CP_CB_DATA_STATE_NODELIST:
		case MAIN_CP_CB_DATA_STATE_TOTEM:
		case MAIN_CP_CB_DATA_STATE_QB:
			break;
		case MAIN_CP_CB_DATA_STATE_RESOURCES:
			*state = MAIN_CP_CB_DATA_STATE_NORMAL;
			break;
		case MAIN_CP_CB_DATA_STATE_RESOURCES_SYSTEM:
			*state = MAIN_CP_CB_DATA_STATE_RESOURCES;
			break;
		case MAIN_CP_CB_DATA_STATE_RESOURCES_SYSTEM_MEMUSED:
			*state = MAIN_CP_CB_DATA_STATE_RESOURCES_SYSTEM;
			break;
		case MAIN_CP_CB_DATA_STATE_RESOURCES_PROCESS:
			*state = MAIN_CP_CB_DATA_STATE_RESOURCES;
			break;
		case MAIN_CP_CB_DATA_STATE_RESOURCES_PROCESS_MEMUSED:
			*state = MAIN_CP_CB_DATA_STATE_RESOURCES_PROCESS;
			break;
		}
		break;
	}

	return (1);

atoi_error:
	min_val = max_val = 0;
	/*
	 * This is really assert, because developer ether doesn't set val_type correctly or
	 * we've got here after some nasty memory overwrite
	 */
	assert(safe_atoq_range(val_type, &min_val, &max_val) == 0);

	snprintf(formated_err, sizeof(formated_err),
	    "Value of key \"%s\" is expected to be integer in range (%lld..%lld), but \"%s\" was given",
	    key, min_val, max_val, value);
	*error_string = formated_err;

	return (0);
}

static int uidgid_config_parser_cb(const char *path,
			char *key,
			char *value,
			enum main_cp_cb_data_state *state,
			enum parser_cb_type type,
			const char **error_string,
			icmap_map_t config_map,
			void *user_data)
{
	char key_name[ICMAP_KEYNAME_MAXLEN];
	int uid, gid;

	switch (type) {
	case PARSER_CB_START:
		break;
	case PARSER_CB_END:
		break;
	case PARSER_CB_ITEM:
		if (strcmp(path, "uidgid.uid") == 0) {
			uid = uid_determine(value);
			if (uid == -1) {
				*error_string = error_string_response;
				return (0);
			}
			snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "uidgid.config.uid.%u",
					uid);
			icmap_set_uint8_r(config_map, key_name, 1);
		} else if (strcmp(path, "uidgid.gid") == 0) {
			gid = gid_determine(value);
			if (gid == -1) {
				*error_string = error_string_response;
				return (0);
			}
			snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "uidgid.config.gid.%u",
					gid);
			icmap_set_uint8_r(config_map, key_name, 1);
		} else {
			*error_string = "uidgid: Only uid and gid are allowed items";
			return (0);
		}
		break;
	case PARSER_CB_SECTION_START:
		if (strcmp(path, "uidgid") != 0) {
			*error_string = "uidgid: Can't add subsection different than uidgid";
			return (0);
		};
		break;
	case PARSER_CB_SECTION_END:
		break;
	}

	return (1);
}

static int read_uidgid_files_into_icmap(
	const char **error_string,
	icmap_map_t config_map)
{
	FILE *fp;
	const char *dirname;
	DIR *dp;
	struct dirent *dirent;
	struct dirent *entry;
	char filename[PATH_MAX + FILENAME_MAX + 1];
	int res = 0;
	size_t len;
	int return_code;
	struct stat stat_buf;
	enum main_cp_cb_data_state state = MAIN_CP_CB_DATA_STATE_NORMAL;
	char key_name[ICMAP_KEYNAME_MAXLEN];

	dirname = COROSYSCONFDIR "/uidgid.d";
	dp = opendir (dirname);

	if (dp == NULL)
		return 0;

	len = offsetof(struct dirent, d_name) + FILENAME_MAX + 1;

	entry = malloc(len);
	if (entry == NULL) {
		res = 0;
		goto error_exit;
	}

	for (return_code = readdir_r(dp, entry, &dirent);
		dirent != NULL && return_code == 0;
		return_code = readdir_r(dp, entry, &dirent)) {

		snprintf(filename, sizeof (filename), "%s/%s", dirname, dirent->d_name);
		res = stat (filename, &stat_buf);
		if (res == 0 && S_ISREG(stat_buf.st_mode)) {

			fp = fopen (filename, "r");
			if (fp == NULL) continue;

			key_name[0] = 0;

			res = parse_section(fp, key_name, error_string, 0, state, uidgid_config_parser_cb, config_map, NULL);

			fclose (fp);

			if (res != 0) {
				goto error_exit;
			}
		}
	}

error_exit:
	free (entry);
	closedir(dp);

	return res;
}

/* Read config file and load into icmap */
static int read_config_file_into_icmap(
	const char **error_string,
	icmap_map_t config_map)
{
	FILE *fp;
	const char *filename;
	char *error_reason = error_string_response;
	int res;
	char key_name[ICMAP_KEYNAME_MAXLEN];
	struct main_cp_cb_data data;
	enum main_cp_cb_data_state state = MAIN_CP_CB_DATA_STATE_NORMAL;

	filename = getenv ("COROSYNC_MAIN_CONFIG_FILE");
	if (!filename)
		filename = COROSYSCONFDIR "/corosync.conf";

	fp = fopen (filename, "r");
	if (fp == NULL) {
		char error_str[100];
		const char *error_ptr = qb_strerror_r(errno, error_str, sizeof(error_str));
		snprintf (error_reason, sizeof(error_string_response),
			"Can't read file %s reason = (%s)",
			 filename, error_ptr);
		*error_string = error_reason;
		return -1;
	}

	key_name[0] = 0;

	res = parse_section(fp, key_name, error_string, 0, state, main_config_parser_cb, config_map, &data);

	fclose(fp);

	if (res == 0) {
	        res = read_uidgid_files_into_icmap(error_string, config_map);
	}

	if (res == 0) {
		snprintf (error_reason, sizeof(error_string_response),
			"Successfully read main configuration file '%s'.", filename);
		*error_string = error_reason;
	}

	return res;
}
