/*
 * Copyright (c) 2006-2022 Red Hat, Inc.
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
#include <libgen.h>
#include <limits.h>
#include <stddef.h>
#include <grp.h>
#include <pwd.h>

#include <qb/qblist.h>
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
	PARSER_CB_CLEANUP,
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
	MAIN_CP_CB_DATA_STATE_SYSTEM,
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
	struct qb_list_head list;
};

struct main_cp_cb_data {
	int linknumber;
	char *bindnetaddr;
	char *mcastaddr;
	char *broadcast;
	int mcastport;
	int ttl;
	int knet_link_priority;
	int knet_ping_interval;
	int knet_ping_timeout;
	int knet_ping_precision;
	int knet_pong_count;
	int knet_pmtud_interval;
	unsigned int knet_mtu;
	char *knet_transport;

	struct qb_list_head logger_subsys_items_head;
	char *subsys;
	char *logging_daemon_name;
	struct qb_list_head member_items_head;

	int node_number;
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
	if (*req_user != '\0' && *ep == '\0' && id >= 0 && id <= UINT_MAX) {
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
	if (*req_group != '\0' && *ep == '\0' && id >= 0 && id <= UINT_MAX) {
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

		while (*end_address == ' ' || *end_address == '\t' || (unsigned char)*end_address == 0xA0)
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
	if (*start == '\0')
		return start;

	while (*start == ' ' || *start == '\t' || (unsigned char)*start == 0xA0)
		start++;

	end = start+(strlen(start))-1;
	while ((*end == ' ' || *end == '\t' || (unsigned char)*end == 0xA0 || (remove_colon_and_brace && (*end == ':' || *end == '{'))) && end > start)
		end--;
	if (*end != '\0')
		*(end + 1) = '\0';

	return start;
}



static int parse_section(FILE *fp,
			const char *fname,
			int *line_no,
			const char *path,
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
	static char formated_err[384];
	const char *tmp_error_string;

	if (strcmp(path, "") == 0) {
		parser_cb("", NULL, NULL, &state, PARSER_CB_START, error_string, config_map, user_data);
	}

	tmp_error_string = NULL;

	while (fgets (line, sizeof (line), fp)) {
		(*line_no)++;

		if (strlen(line) > 0) {
			/*
			 * Check if complete line was read. Use feof to handle files
			 * without ending \n at the end of the file
			 */
			if ((line[strlen(line) - 1] != '\n') && !feof(fp)) {
				tmp_error_string = "Line too long";
				goto parse_error;
			}

			if (line[strlen(line) - 1] == '\n')
				line[strlen(line) - 1] = '\0';
			if (strlen (line) > 0 && line[strlen(line) - 1] == '\r')
				line[strlen(line) - 1] = '\0';
		}
		/*
		 * Clear out white space and tabs
		 */
		for (i = strlen (line) - 1; i > -1; i--) {
			if (line[i] == '\t' || line[i] == ' ' || (unsigned char)line[i] == 0xA0) {
				line[i] = '\0';
			} else {
				break;
			}
		}

		ignore_line = 1;
		for (i = 0; i < strlen (line); i++) {
			if (line[i] != '\t' && line[i] != ' ' && (unsigned char)line[i] != 0xA0) {
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
			char *section;
			char *after_section;
			enum main_cp_cb_data_state newstate;

			*(loc-1) = '\0';
			section = remove_whitespace(line, 1);
			after_section = remove_whitespace(loc, 0);

			if (strcmp(section, "") == 0) {
				tmp_error_string = "Missing section name before opening bracket '{'";
				goto parse_error;
			}

			if (strcmp(after_section, "") != 0) {
				tmp_error_string = "Extra characters after opening bracket '{'";
				goto parse_error;
			}

			if (strlen(path) + strlen(section) + 1 >= ICMAP_KEYNAME_MAXLEN) {
				tmp_error_string = "Start of section makes total cmap path too long";
				goto parse_error;
			}
			strcpy(new_keyname, path);
			if (strcmp(path, "") != 0) {
				strcat(new_keyname, ".");
			}
			strcat(new_keyname, section);

			/* Only use the new state for items further down the stack */
			newstate = state;
			if (!parser_cb(new_keyname, NULL, NULL, &newstate, PARSER_CB_SECTION_START,
			    &tmp_error_string, config_map, user_data)) {
				goto parse_error;
			}

			if (parse_section(fp, fname, line_no, new_keyname, error_string, depth + 1, newstate,
			    parser_cb, config_map, user_data))
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

			if (strlen(key) == 0) {
				tmp_error_string = "Key name can't be empty";
				goto parse_error;
			}

			if (strlen(path) + strlen(key) + 1 >= ICMAP_KEYNAME_MAXLEN) {
				tmp_error_string = "New key makes total cmap path too long";
				goto parse_error;
			}
			strcpy(new_keyname, path);
			if (strcmp(path, "") != 0) {
				strcat(new_keyname, ".");
			}
			strcat(new_keyname, key);

			if (!parser_cb(new_keyname, key, value, &state, PARSER_CB_ITEM, &tmp_error_string,
			    config_map, user_data)) {
				goto parse_error;
			}

			continue ;
		}

		if (strchr_rs (line, '}')) {
			char *trimmed_line;
			trimmed_line = remove_whitespace(line, 0);

			if (strcmp(trimmed_line, "}") != 0) {
				tmp_error_string = "Extra characters before or after closing bracket '}'";
				goto parse_error;
			}

			if (depth == 0) {
				tmp_error_string = "Unexpected closing brace";

				goto parse_error;
			}

			if (!parser_cb(path, NULL, NULL, &state, PARSER_CB_SECTION_END, &tmp_error_string,
			    config_map, user_data)) {
				goto parse_error;
			}

			return 0;
		}

		/*
		 * Line is not opening section, ending section or value -> error
		 */
		tmp_error_string = "Line is not opening or closing section or key value";
		goto parse_error;
	}

	if (strcmp(path, "") != 0) {
		tmp_error_string = "Missing closing brace";
		goto parse_error;
	}

	if (strcmp(path, "") == 0) {
		parser_cb("", NULL, NULL, &state, PARSER_CB_END, error_string, config_map, user_data);
		parser_cb("", NULL, NULL, &state, PARSER_CB_CLEANUP, error_string, config_map, user_data);
	}

	return 0;

parse_error:
	if (snprintf(formated_err, sizeof(formated_err), "parser error: %s:%u: %s", fname, *line_no,
	    tmp_error_string) >= sizeof(formated_err)) {
		*error_string = "Can't format parser error message";
	} else {
		*error_string = formated_err;
	}

	parser_cb("", NULL, NULL, &state, PARSER_CB_CLEANUP, error_string, config_map, user_data);

	return -1;
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

static int handle_crypto_model(const char *val, const char **error_string)
{

	if (util_is_valid_knet_crypto_model(val, NULL, 0,
	    "Invalid crypto model. Should be ", error_string) == 1) {
		return (0);
	} else {
		return (-1);
	}
}

static int handle_compress_model(const char *val, const char **error_string)
{

	if (util_is_valid_knet_compress_model(val, NULL, 0,
	    "Invalid compression model. Should be ", error_string) == 1) {
		return (0);
	} else {
		return (-1);
	}
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
	char key_name[ICMAP_KEYNAME_MAXLEN + 1];
	static char formated_err[256];
	struct main_cp_cb_data *data = (struct main_cp_cb_data *)user_data;
	struct key_value_list_item *kv_item;
	struct qb_list_head *iter, *tmp_iter;
	int uid, gid;
	cs_error_t cs_err;
	const char *path_prefix;

	cs_err = CS_OK;

	/*
	 * Formally this check is not needed because length is checked by parse_section
	 */
	if (strlen(path) >= sizeof(key_name)) {
		if (snprintf(formated_err, sizeof(formated_err),
		    "Can't store path \"%s\" into key_name", path) >= sizeof(formated_err)) {
			*error_string = "Can't format path into key_name error message";
		} else {
			*error_string = formated_err;
		}
		return (0);
	}
	/*
	 * Key_name is used in atoi_error/icmap_set_error, but many of icmap_set*
	 * are using path, so initialize key_name to valid value
	 */
	strncpy(key_name, path, sizeof(key_name) - 1);

	switch (type) {
	case PARSER_CB_START:
		memset(data, 0, sizeof(struct main_cp_cb_data));
		qb_list_init(&data->logger_subsys_items_head);
		qb_list_init(&data->member_items_head);
		*state = MAIN_CP_CB_DATA_STATE_NORMAL;
		break;
	case PARSER_CB_END:
		break;
	case PARSER_CB_CLEANUP:
		free(data->bindnetaddr);
		free(data->mcastaddr);
		free(data->broadcast);
		free(data->knet_transport);

		qb_list_for_each_safe(iter, tmp_iter, &(data->logger_subsys_items_head)) {
			kv_item = qb_list_entry(iter, struct key_value_list_item, list);
			qb_list_del(&kv_item->list);

			free(kv_item->value);
			free(kv_item->key);
			free(kv_item);
		}

		free(data->subsys);
		free(data->logging_daemon_name);

		qb_list_for_each_safe(iter, tmp_iter, &(data->member_items_head)) {
			kv_item = qb_list_entry(iter, struct key_value_list_item, list);
			qb_list_del(&kv_item->list);

			free(kv_item->value);
			free(kv_item->key);
			free(kv_item);
		}
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
					goto safe_atoq_error;
				}
				if ((cs_err = icmap_set_uint32_r(config_map, path, val)) != CS_OK) {
					goto icmap_set_error;
				}
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
					goto safe_atoq_error;
				}
				if ((cs_err = icmap_set_uint32_r(config_map, path, val)) != CS_OK) {
					goto icmap_set_error;
				}
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
					goto safe_atoq_error;
				}
				if ((cs_err = icmap_set_uint8_r(config_map, path, val)) != CS_OK) {
					goto icmap_set_error;
				}
				add_as_string = 0;
			}
			break;
		case MAIN_CP_CB_DATA_STATE_QDEVICE:
			if ((strcmp(path, "quorum.device.timeout") == 0) ||
			    (strcmp(path, "quorum.device.sync_timeout") == 0) ||
			    (strcmp(path, "quorum.device.votes") == 0)) {
				val_type = ICMAP_VALUETYPE_UINT32;
				if (safe_atoq(value, &val, val_type) != 0) {
					goto safe_atoq_error;
				}
				if ((cs_err = icmap_set_uint32_r(config_map, path, val)) != CS_OK) {
					goto icmap_set_error;
				}
				add_as_string = 0;
			}
			if ((strcmp(path, "quorum.device.master_wins") == 0)) {
				val_type = ICMAP_VALUETYPE_UINT8;
				if (safe_atoq(value, &val, val_type) != 0) {
					goto safe_atoq_error;
				}
				if ((cs_err = icmap_set_uint8_r(config_map, path, val)) != CS_OK) {
					goto icmap_set_error;
				}
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
			    (strcmp(path, "totem.token_warning") == 0) ||
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
			    (strcmp(path, "totem.knet_pmtud_interval") == 0) ||
			    (strcmp(path, "totem.knet_mtu") == 0) ||
			    (strcmp(path, "totem.knet_compression_threshold") == 0) ||
			    (strcmp(path, "totem.netmtu") == 0)) {
				val_type = ICMAP_VALUETYPE_UINT32;
				if (safe_atoq(value, &val, val_type) != 0) {
					goto safe_atoq_error;
				}
				if ((cs_err = icmap_set_uint32_r(config_map,path, val)) != CS_OK) {
					goto icmap_set_error;
				}
				add_as_string = 0;
			}
			if (strcmp(path, "totem.knet_compression_level") == 0) {
				val_type = ICMAP_VALUETYPE_INT32;
				if (safe_atoq(value, &val, val_type) != 0) {
					goto safe_atoq_error;
				}
				if ((cs_err = icmap_set_int32_r(config_map, path, val)) != CS_OK) {
					goto icmap_set_error;
				}
				add_as_string = 0;
			}
			if (strcmp(path, "totem.config_version") == 0) {
				if (str_to_ull(value, &ull) != 0) {
					goto str_to_ull_error;
				}
				if ((cs_err = icmap_set_uint64_r(config_map, path, ull)) != CS_OK) {
					goto icmap_set_error;
				}
				add_as_string = 0;
			}
			if (strcmp(path, "totem.ip_version") == 0) {
				if ((strcmp(value, "ipv4") != 0) &&
				    (strcmp(value, "ipv6") != 0) &&
				    (strcmp(value, "ipv6-4") != 0) &&
				    (strcmp(value, "ipv4-6") != 0)) {
					*error_string = "Invalid ip_version type";

					return (0);
				}
			}
			if (strcmp(path, "totem.crypto_model") == 0) {
				if (handle_crypto_model(value, error_string) != 0) {
					return (0);
				}
			}

			if (strcmp(path, "totem.crypto_cipher") == 0) {
				if ((strcmp(value, "none") != 0) &&
				    (strcmp(value, "aes256") != 0) &&
				    (strcmp(value, "aes192") != 0) &&
				    (strcmp(value, "aes128") != 0)) {
					*error_string = "Invalid cipher type. "
					    "Should be none, aes256, aes192 or aes128";

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
					*error_string = "Invalid hash type. "
					    "Should be none, md5, sha1, sha256, sha384 or sha512";

					return (0);
				}
			}

			if (strcmp(path, "totem.knet_compression_model") == 0) {
				if (handle_compress_model(value, error_string) != 0) {
					return (0);
				}
			}

			break;

		case MAIN_CP_CB_DATA_STATE_SYSTEM:
			if (strcmp(path, "system.qb_ipc_type") == 0) {
				if ((strcmp(value, "native") != 0) &&
				    (strcmp(value, "shm") != 0) &&
				    (strcmp(value, "socket") != 0)) {
					*error_string = "Invalid system.qb_ipc_type";

					return (0);
				}
			}
			if (strcmp(path, "system.sched_rr") == 0) {
				if ((strcmp(value, "yes") != 0) &&
				    (strcmp(value, "no") != 0)) {
					*error_string = "Invalid system.sched_rr value";

					return (0);
				}
			}
			if (strcmp(path, "system.move_to_root_cgroup") == 0) {
				if ((strcmp(value, "yes") != 0) &&
				    (strcmp(value, "no") != 0) &&
				    (strcmp(value, "auto") != 0)) {
					*error_string = "Invalid system.move_to_root_cgroup";

					return (0);
				}
			}
			if (strcmp(path, "system.allow_knet_handle_fallback") == 0) {
				if ((strcmp(value, "yes") != 0) &&
				    (strcmp(value, "no") != 0)) {
					*error_string = "Invalid system.allow_knet_handle_fallback";

					return (0);
				}
			}
			break;

		case MAIN_CP_CB_DATA_STATE_INTERFACE:
			if (strcmp(path, "totem.interface.linknumber") == 0) {
				val_type = ICMAP_VALUETYPE_UINT8;
				if (safe_atoq(value, &val, val_type) != 0) {
					goto safe_atoq_error;
				}

				data->linknumber = val;
				add_as_string = 0;
			}
			if (strcmp(path, "totem.interface.bindnetaddr") == 0) {
				free(data->bindnetaddr);
				data->bindnetaddr = strdup(value);
				add_as_string = 0;
			}
			if (strcmp(path, "totem.interface.mcastaddr") == 0) {
				free(data->mcastaddr);
				data->mcastaddr = strdup(value);
				add_as_string = 0;
			}
			if (strcmp(path, "totem.interface.broadcast") == 0) {
				free(data->broadcast);
				data->broadcast = strdup(value);
				add_as_string = 0;
			}
			if (strcmp(path, "totem.interface.mcastport") == 0) {
				val_type = ICMAP_VALUETYPE_UINT16;
				if (safe_atoq(value, &val, val_type) != 0) {
					goto safe_atoq_error;
				}
				data->mcastport = val;
				add_as_string = 0;
			}
			if (strcmp(path, "totem.interface.ttl") == 0) {
				val_type = ICMAP_VALUETYPE_UINT8;
				if (safe_atoq(value, &val, val_type) != 0) {
					goto safe_atoq_error;
				}
				data->ttl = val;
				add_as_string = 0;
			}
			if (strcmp(path, "totem.interface.knet_link_priority") == 0) {
				val_type = ICMAP_VALUETYPE_UINT8;
				if (safe_atoq(value, &val, val_type) != 0) {
					goto safe_atoq_error;
				}
				data->knet_link_priority = val;
				add_as_string = 0;
			}
			if (strcmp(path, "totem.interface.knet_ping_interval") == 0) {
				val_type = ICMAP_VALUETYPE_UINT32;
				if (safe_atoq(value, &val, val_type) != 0) {
					goto safe_atoq_error;
				}
				data->knet_ping_interval = val;
				add_as_string = 0;
			}
			if (strcmp(path, "totem.interface.knet_ping_timeout") == 0) {
				val_type = ICMAP_VALUETYPE_UINT32;
				if (safe_atoq(value, &val, val_type) != 0) {
					goto safe_atoq_error;
				}
				data->knet_ping_timeout = val;
				add_as_string = 0;
			}
			if (strcmp(path, "totem.interface.knet_ping_precision") == 0) {
				val_type = ICMAP_VALUETYPE_UINT32;
				if (safe_atoq(value, &val, val_type) != 0) {
					goto safe_atoq_error;
				}
				data->knet_ping_precision = val;
				add_as_string = 0;
			}
			if (strcmp(path, "totem.interface.knet_pong_count") == 0) {
				val_type = ICMAP_VALUETYPE_UINT32;
				if (safe_atoq(value, &val, val_type) != 0) {
					goto safe_atoq_error;
				}
				data->knet_pong_count = val;
				add_as_string = 0;
			}
			if (strcmp(path, "totem.interface.knet_transport") == 0) {
				free(data->knet_transport);
				data->knet_transport = strdup(value);
				add_as_string = 0;
			}
			break;
		case MAIN_CP_CB_DATA_STATE_LOGGER_SUBSYS:
			if (strcmp(path, "logging.logger_subsys.subsys") == 0) {
				free(data->subsys);
				data->subsys = strdup(value);
				if (data->subsys == NULL) {
					*error_string = "Can't alloc memory";

					return (0);
				}
			} else {
				path_prefix = "logging.logger_subsys.";
				if (strlen(path) < strlen(path_prefix) ||
				    strncmp(path, path_prefix, strlen(path_prefix)) != 0) {
					*error_string = "Internal error - incorrect path prefix for logger subsys state";

					return (0);
				}

				kv_item = malloc(sizeof(*kv_item));
				if (kv_item == NULL) {
					*error_string = "Can't alloc memory";

					return (0);
				}
				memset(kv_item, 0, sizeof(*kv_item));

				kv_item->key = strdup(path + strlen(path_prefix));
				kv_item->value = strdup(value);
				if (kv_item->key == NULL || kv_item->value == NULL) {
					free(kv_item->key);
					free(kv_item->value);
					free(kv_item);
					*error_string = "Can't alloc memory";

					return (0);
				}
				qb_list_init(&kv_item->list);
				qb_list_add(&kv_item->list, &data->logger_subsys_items_head);
			}
			add_as_string = 0;
			break;
		case MAIN_CP_CB_DATA_STATE_LOGGING_DAEMON:
			if (strcmp(path, "logging.logging_daemon.subsys") == 0) {
				free(data->subsys);
				data->subsys = strdup(value);
				if (data->subsys == NULL) {
					*error_string = "Can't alloc memory";

					return (0);
				}
			} else if (strcmp(path, "logging.logging_daemon.name") == 0) {
				free(data->logging_daemon_name);
				data->logging_daemon_name = strdup(value);
				if (data->logging_daemon_name == NULL) {
					*error_string = "Can't alloc memory";

					return (0);
				}
			} else {
				path_prefix = "logging.logging_daemon.";
				if (strlen(path) < strlen(path_prefix) ||
				    strncmp(path, path_prefix, strlen(path_prefix)) != 0) {
					*error_string = "Internal error - incorrect path prefix for logging daemon state";

					return (0);
				}

				kv_item = malloc(sizeof(*kv_item));
				if (kv_item == NULL) {
					*error_string = "Can't alloc memory";

					return (0);
				}
				memset(kv_item, 0, sizeof(*kv_item));

				kv_item->key = strdup(path + strlen(path_prefix));
				kv_item->value = strdup(value);
				if (kv_item->key == NULL || kv_item->value == NULL) {
					free(kv_item->key);
					free(kv_item->value);
					free(kv_item);
					*error_string = "Can't alloc memory";

					return (0);
				}
				qb_list_init(&kv_item->list);
				qb_list_add(&kv_item->list, &data->logger_subsys_items_head);
			}
			add_as_string = 0;
			break;
		case MAIN_CP_CB_DATA_STATE_UIDGID:
			if (strcmp(path, "uidgid.uid") == 0) {
				uid = uid_determine(value);
				if (uid == -1) {
					*error_string = error_string_response;
					return (0);
				}
				snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "uidgid.config.uid.%u",
						uid);
				if ((cs_err = icmap_set_uint8_r(config_map, key_name, 1)) != CS_OK) {
					goto icmap_set_error;
				}
				add_as_string = 0;
			} else if (strcmp(path, "uidgid.gid") == 0) {
				gid = gid_determine(value);
				if (gid == -1) {
					*error_string = error_string_response;
					return (0);
				}
				snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "uidgid.config.gid.%u",
						gid);
				if ((cs_err = icmap_set_uint8_r(config_map, key_name, 1)) != CS_OK) {
					goto icmap_set_error;
				}
				add_as_string = 0;
			} else {
				*error_string = "uidgid: Only uid and gid are allowed items";
				return (0);
			}
			break;
		case MAIN_CP_CB_DATA_STATE_MEMBER:
			if (strcmp(path, "totem.interface.member.memberaddr") != 0) {
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
				free(kv_item->key);
				free(kv_item->value);
				free(kv_item);
				*error_string = "Can't alloc memory";

				return (0);
			}
			qb_list_init(&kv_item->list);
			qb_list_add(&kv_item->list, &data->member_items_head);
			add_as_string = 0;
			break;
		case MAIN_CP_CB_DATA_STATE_NODELIST:
			break;
		case MAIN_CP_CB_DATA_STATE_NODELIST_NODE:
			path_prefix = "nodelist.node.";
			if (strlen(path) < strlen(path_prefix) ||
			    strncmp(path, path_prefix, strlen(path_prefix)) != 0) {
				*error_string = "Internal error - incorrect path prefix for nodelist node state";

				return (0);
			}

			snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "nodelist.node.%u.%s", data->node_number,
			    path + strlen(path_prefix));
			if ((strcmp(path, "nodelist.node.nodeid") == 0) ||
			    (strcmp(path, "nodelist.node.quorum_votes") == 0)) {
				val_type = ICMAP_VALUETYPE_UINT32;
				if (safe_atoq(value, &val, val_type) != 0) {
					goto safe_atoq_error;
				}

				if ((cs_err = icmap_set_uint32_r(config_map, key_name, val)) != CS_OK) {
					goto icmap_set_error;
				}
				add_as_string = 0;
			}

			if (add_as_string) {
				if ((cs_err = icmap_set_string_r(config_map, key_name, value)) != CS_OK) {
					goto icmap_set_error;
				};
				add_as_string = 0;
			}
			break;
		case MAIN_CP_CB_DATA_STATE_RESOURCES:
			if (strcmp(key, "watchdog_timeout") == 0) {
				val_type = ICMAP_VALUETYPE_UINT32;
				if (safe_atoq(value, &val, val_type) != 0) {
					goto safe_atoq_error;
				}
				if ((cs_err = icmap_set_uint32_r(config_map,path, val)) != CS_OK) {
					goto icmap_set_error;
				}
				add_as_string = 0;
			}
			break;
		case MAIN_CP_CB_DATA_STATE_RESOURCES_SYSTEM:
		case MAIN_CP_CB_DATA_STATE_RESOURCES_SYSTEM_MEMUSED:
			if (strcmp(key, "poll_period") == 0) {
				if (str_to_ull(value, &ull) != 0) {
					goto str_to_ull_error;
				}
				if ((cs_err = icmap_set_uint64_r(config_map,path, ull)) != CS_OK) {
					goto icmap_set_error;
				}
				add_as_string = 0;
			}
			break;
		case MAIN_CP_CB_DATA_STATE_RESOURCES_PROCESS:
		case MAIN_CP_CB_DATA_STATE_RESOURCES_PROCESS_MEMUSED:
			if (strcmp(key, "poll_period") == 0) {
				if (str_to_ull(value, &ull) != 0) {
					goto str_to_ull_error;
				}
				if ((cs_err = icmap_set_uint64_r(config_map,path, ull)) != CS_OK) {
					goto icmap_set_error;
				}
				add_as_string = 0;
			}
			break;
		}

		if (add_as_string) {
			if ((cs_err = icmap_set_string_r(config_map, path, value)) != CS_OK) {
				goto icmap_set_error;
			}
		}
		break;
	case PARSER_CB_SECTION_START:
		if (strcmp(path, "totem.interface") == 0) {
			*state = MAIN_CP_CB_DATA_STATE_INTERFACE;
			data->linknumber = 0;
			data->mcastport = -1;
			data->ttl = -1;
			data->knet_link_priority = -1;
			data->knet_ping_interval = -1;
			data->knet_ping_timeout = -1;
			data->knet_ping_precision = -1;
			data->knet_pong_count = -1;
			data->knet_transport = NULL;
			qb_list_init(&data->member_items_head);
		};
		if (strcmp(path, "totem") == 0) {
			*state = MAIN_CP_CB_DATA_STATE_TOTEM;
		};
		if (strcmp(path, "system") == 0) {
			*state = MAIN_CP_CB_DATA_STATE_SYSTEM;
		}
		if (strcmp(path, "logging.logger_subsys") == 0) {
			*state = MAIN_CP_CB_DATA_STATE_LOGGER_SUBSYS;
			qb_list_init(&data->logger_subsys_items_head);
			data->subsys = NULL;
		}
		if (strcmp(path, "logging.logging_daemon") == 0) {
			*state = MAIN_CP_CB_DATA_STATE_LOGGING_DAEMON;
			qb_list_init(&data->logger_subsys_items_head);
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

		if (*state == MAIN_CP_CB_DATA_STATE_UIDGID && strcmp(path, "uidgid") != 0) {
			*error_string = "Subsections are not allowed within uidgid section";

			return (0);
		};

		if (*state == MAIN_CP_CB_DATA_STATE_MEMBER && strcmp(path, "totem.interface.member") != 0) {
			*error_string = "Subsections are not allowed within totem.interface.member section";

			return (0);
		};
		break;
	case PARSER_CB_SECTION_END:
		switch (*state) {
		case MAIN_CP_CB_DATA_STATE_INTERFACE:
			if (strcmp(path, "totem.interface") != 0) {
				/*
				 * Process only end of totem.interface section, not subsections
				 */
				break;
			}

			/*
			 * Create new interface section
			 */
			if (data->bindnetaddr != NULL) {
				snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "totem.interface.%u.bindnetaddr",
						data->linknumber);
				cs_err = icmap_set_string_r(config_map, key_name, data->bindnetaddr);

				free(data->bindnetaddr);
				data->bindnetaddr = NULL;

				if (cs_err != CS_OK) {
					goto icmap_set_error;
				}
			}

			if (data->mcastaddr != NULL) {
				snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "totem.interface.%u.mcastaddr",
						data->linknumber);
				cs_err = icmap_set_string_r(config_map, key_name, data->mcastaddr);

				free(data->mcastaddr);
				data->mcastaddr = NULL;

				if (cs_err != CS_OK) {
					goto icmap_set_error;
				}
			}

			if (data->broadcast != NULL) {
				snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "totem.interface.%u.broadcast",
						data->linknumber);
				cs_err = icmap_set_string_r(config_map, key_name, data->broadcast);

				free(data->broadcast);
				data->broadcast = NULL;

				if (cs_err != CS_OK) {
					goto icmap_set_error;
				}
			}

			if (data->mcastport > -1) {
				snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "totem.interface.%u.mcastport",
						data->linknumber);
				if ((cs_err = icmap_set_uint16_r(config_map, key_name,
				    data->mcastport)) != CS_OK) {
					goto icmap_set_error;
				}
			}

			if (data->ttl > -1) {
				snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "totem.interface.%u.ttl",
						data->linknumber);
				if ((cs_err = icmap_set_uint8_r(config_map, key_name, data->ttl)) != CS_OK) {
					goto icmap_set_error;
				}
			}
			if (data->knet_link_priority > -1) {
				snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "totem.interface.%u.knet_link_priority",
						data->linknumber);
				if ((cs_err = icmap_set_uint8_r(config_map, key_name,
				    data->knet_link_priority)) != CS_OK) {
					goto icmap_set_error;
				}
			}
			if (data->knet_ping_interval > -1) {
				snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "totem.interface.%u.knet_ping_interval",
						data->linknumber);
				if ((cs_err = icmap_set_uint32_r(config_map, key_name,
				    data->knet_ping_interval)) != CS_OK) {
					goto icmap_set_error;
				}
			}
			if (data->knet_ping_timeout > -1) {
				snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "totem.interface.%u.knet_ping_timeout",
						data->linknumber);
				if ((cs_err = icmap_set_uint32_r(config_map, key_name,
				    data->knet_ping_timeout)) != CS_OK) {
					goto icmap_set_error;
				}
			}
			if (data->knet_ping_precision > -1) {
				snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "totem.interface.%u.knet_ping_precision",
						data->linknumber);
				if ((cs_err = icmap_set_uint32_r(config_map, key_name,
				    data->knet_ping_precision)) != CS_OK) {
					goto icmap_set_error;
				}
			}
			if (data->knet_pong_count > -1) {
				snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "totem.interface.%u.knet_pong_count",
						data->linknumber);
				if ((cs_err = icmap_set_uint32_r(config_map, key_name,
				    data->knet_pong_count)) != CS_OK) {
					goto icmap_set_error;
				}
			}
			if (data->knet_transport) {
				snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "totem.interface.%u.knet_transport",
						data->linknumber);
				cs_err = icmap_set_string_r(config_map, key_name, data->knet_transport);
				free(data->knet_transport);
				data->knet_transport = NULL;

				if (cs_err != CS_OK) {
					goto icmap_set_error;
				}
			}

			ii = 0;

			qb_list_for_each_safe(iter, tmp_iter, &(data->member_items_head)) {
				kv_item = qb_list_entry(iter, struct key_value_list_item, list);
				qb_list_del(&kv_item->list);

				snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "totem.interface.%u.member.%u",
						data->linknumber, ii);
				cs_err = icmap_set_string_r(config_map, key_name, kv_item->value);

				free(kv_item->value);
				free(kv_item->key);
				free(kv_item);
				ii++;

				if (cs_err != CS_OK) {
					goto icmap_set_error;
				}
			}

			qb_list_init(&data->member_items_head);

			break;
		case MAIN_CP_CB_DATA_STATE_LOGGER_SUBSYS:
			if (strcmp(path, "logging.logger_subsys") != 0) {
				/*
				 * Process only end of logging.logger_subsys section, not subsections
				 */
				break;
			}

			if (data->subsys == NULL) {
				*error_string = "No subsys key in logger_subsys directive";

				return (0);
			}

			qb_list_for_each_safe(iter, tmp_iter, &(data->logger_subsys_items_head)) {
				kv_item = qb_list_entry(iter, struct key_value_list_item, list);
				qb_list_del(&kv_item->list);

				snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "logging.logger_subsys.%s.%s",
					 data->subsys, kv_item->key);
				cs_err = icmap_set_string_r(config_map, key_name, kv_item->value);

				free(kv_item->value);
				free(kv_item->key);
				free(kv_item);

				if (cs_err != CS_OK) {
					goto icmap_set_error;
				}
			}

			snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "logging.logger_subsys.%s.subsys",
					data->subsys);
			cs_err = icmap_set_string_r(config_map, key_name, data->subsys);

			qb_list_init(&data->logger_subsys_items_head);
			free(data->subsys);
			data->subsys = NULL;

			if (cs_err != CS_OK) {
				goto icmap_set_error;
			}
			break;
		case MAIN_CP_CB_DATA_STATE_LOGGING_DAEMON:
			if (strcmp(path, "logging.logging_daemon") != 0) {
				/*
				 * Process only end of logging.logging_daemon section, not subsections
				 */
				break;
			}

			if (data->logging_daemon_name == NULL) {
				*error_string = "No name key in logging_daemon directive";

				return (0);
			}

			qb_list_for_each_safe(iter, tmp_iter, &(data->logger_subsys_items_head)) {
				kv_item = qb_list_entry(iter, struct key_value_list_item, list);
				qb_list_del(&kv_item->list);

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
				cs_err = icmap_set_string_r(config_map, key_name, kv_item->value);

				free(kv_item->value);
				free(kv_item->key);
				free(kv_item);

				if (cs_err != CS_OK) {
					goto icmap_set_error;
				}
			}

			if (data->subsys == NULL) {
				if (strcmp(data->logging_daemon_name, "corosync") != 0) {
					snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "logging.logging_daemon.%s.name",
							data->logging_daemon_name);
					cs_err = icmap_set_string_r(config_map, key_name, data->logging_daemon_name);
				}
			} else {
				if (strcmp(data->logging_daemon_name, "corosync") == 0) {
					snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "logging.logger_subsys.%s.subsys",
							data->subsys);
					cs_err = icmap_set_string_r(config_map, key_name, data->subsys);

				} else {
					snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "logging.logging_daemon.%s.%s.subsys",
							data->logging_daemon_name, data->subsys);
					cs_err = icmap_set_string_r(config_map, key_name, data->subsys);

					if (cs_err != CS_OK) {
						goto icmap_set_error;
					}
					snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "logging.logging_daemon.%s.%s.name",
							data->logging_daemon_name, data->subsys);
					cs_err = icmap_set_string_r(config_map, key_name, data->logging_daemon_name);
				}
			}

			qb_list_init(&data->logger_subsys_items_head);
			free(data->subsys);
			data->subsys = NULL;
			free(data->logging_daemon_name);
			data->logging_daemon_name = NULL;

			if (cs_err != CS_OK) {
				goto icmap_set_error;
			}
			break;
		case MAIN_CP_CB_DATA_STATE_NODELIST_NODE:
			if (strcmp(path, "nodelist.node") != 0) {
				/*
				 * Process only end of nodelist.node section, not subsections
				 */
				break;
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
		case MAIN_CP_CB_DATA_STATE_SYSTEM:
		case MAIN_CP_CB_DATA_STATE_RESOURCES:
		case MAIN_CP_CB_DATA_STATE_RESOURCES_SYSTEM:
		case MAIN_CP_CB_DATA_STATE_RESOURCES_SYSTEM_MEMUSED:
		case MAIN_CP_CB_DATA_STATE_RESOURCES_PROCESS:
		case MAIN_CP_CB_DATA_STATE_RESOURCES_PROCESS_MEMUSED:
			break;
		}
		break;
	}

	return (1);

safe_atoq_error:
	/*
	 * For integers supported by safe_atoq display range
	 */
	min_val = max_val = 0;
	/*
	 * This is really assert, because developer ether doesn't set val_type correctly or
	 * we've got here after some nasty memory overwrite
	 */
	assert(safe_atoq_range(val_type, &min_val, &max_val) == 0);

	if (snprintf(formated_err, sizeof(formated_err),
	    "Value of key \"%s\" is expected to be integer in range (%lld..%lld), but \"%s\" was given",
	    key_name, min_val, max_val, value) >= sizeof(formated_err)) {
		*error_string = "Can't format parser error message";
	} else {
		*error_string = formated_err;
	}

	return (0);

str_to_ull_error:
	/*
	 * For integers not supported by safe_atoq (64-bit int)
	 */
	if (snprintf(formated_err, sizeof(formated_err),
	    "Value of key \"%s\" is expected to be unsigned integer, but \"%s\" was given",
	    key_name, value) >= sizeof(formated_err)) {
		*error_string = "Can't format parser error message";
	} else {
		*error_string = formated_err;
	}

	return (0);

icmap_set_error:
	if (snprintf(formated_err, sizeof(formated_err),
	    "Can't store key \"%s\" into icmap, returned error is %s",
	    key_name, cs_strerror(cs_err)) >= sizeof(formated_err)) {
		*error_string = "Can't format parser error message";
	} else {
		*error_string = formated_err;
	}

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
	static char formated_err[256];
	cs_error_t cs_err;

	switch (type) {
	case PARSER_CB_START:
		break;
	case PARSER_CB_END:
		break;
	case PARSER_CB_CLEANUP:
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
			if ((cs_err = icmap_set_uint8_r(config_map, key_name, 1)) != CS_OK) {
				goto icmap_set_error;
			}
		} else if (strcmp(path, "uidgid.gid") == 0) {
			gid = gid_determine(value);
			if (gid == -1) {
				*error_string = error_string_response;
				return (0);
			}
			snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "uidgid.config.gid.%u",
					gid);
			if ((cs_err = icmap_set_uint8_r(config_map, key_name, 1)) != CS_OK) {
				goto icmap_set_error;
			}
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

icmap_set_error:
	if (snprintf(formated_err, sizeof(formated_err),
	    "Can't store key \"%s\" into icmap, returned error is %s",
	    key_name, cs_strerror(cs_err)) >= sizeof(formated_err)) {
		*error_string = "Can't format parser error message";
	} else {
		*error_string = formated_err;
	}

	return (0);
}

static int read_uidgid_files_into_icmap(
	const char **error_string,
	icmap_map_t config_map)
{
	FILE *fp;
	char *dirname_res;
	DIR *dp;
	struct dirent *dirent;
	char filename[PATH_MAX + FILENAME_MAX + 1];
	char uidgid_dirname[PATH_MAX + FILENAME_MAX + 1];
	int res = 0;
	struct stat stat_buf;
	enum main_cp_cb_data_state state = MAIN_CP_CB_DATA_STATE_NORMAL;
	char key_name[ICMAP_KEYNAME_MAXLEN];
	int line_no;

	/*
	 * Build uidgid directory based on corosync.conf file location
	 */
	res = snprintf(filename, sizeof(filename), "%s",
	    corosync_get_config_file());
	if (res >= sizeof(filename)) {
		*error_string = "uidgid.d path too long";

		return (-1);
	}

	dirname_res = dirname(filename);

	res = snprintf(uidgid_dirname, sizeof(uidgid_dirname), "%s/%s",
	    dirname_res, "uidgid.d");
	if (res >= sizeof(uidgid_dirname)) {
		*error_string = "uidgid.d path too long";

		return (-1);
	}

	dp = opendir (uidgid_dirname);

	if (dp == NULL)
		return 0;

	for (dirent = readdir(dp);
		dirent != NULL;
		dirent = readdir(dp)) {

		res = snprintf(filename, sizeof (filename), "%s/%s", uidgid_dirname, dirent->d_name);
		if (res >= sizeof(filename)) {
			res = -1;
			*error_string = "uidgid.d dirname path too long";

			goto error_exit;
		}
		res = stat (filename, &stat_buf);
		if (res == 0 && S_ISREG(stat_buf.st_mode)) {

			fp = fopen (filename, "r");
			if (fp == NULL) continue;

			key_name[0] = 0;

			line_no = 0;
			res = parse_section(fp, filename, &line_no, key_name, error_string, 0, state,
			    uidgid_config_parser_cb, config_map, NULL);

			fclose (fp);

			if (res != 0) {
				goto error_exit;
			}
		}
	}

error_exit:
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
	int line_no;

	filename = corosync_get_config_file();

	fp = fopen (filename, "r");
	if (fp == NULL) {
		char error_str[100];
		const char *error_ptr = qb_strerror_r(errno, error_str, sizeof(error_str));
		snprintf (error_reason, sizeof(error_string_response),
			"Can't read file %s: %s",
			 filename, error_ptr);
		*error_string = error_reason;
		return -1;
	}

	key_name[0] = 0;

	line_no = 0;
	res = parse_section(fp, filename, &line_no, key_name, error_string, 0, state,
	    main_config_parser_cb, config_map, &data);

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
