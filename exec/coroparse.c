/*
 * Copyright (c) 2006, 2009 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Patrick Caulfield (pcaulfie@redhat.com)
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

#include <corosync/lcr/lcr_comp.h>
#include <corosync/engine/objdb.h>
#include <corosync/engine/config.h>
#define LOGSYS_UTILS_ONLY 1
#include <corosync/engine/logsys.h>

#include "util.h"

static int read_config_file_into_objdb(
	struct objdb_iface_ver0 *objdb,
	const char **error_string);
static char error_string_response[512];


static char *strchr_rs (const char *haystack, int byte)
{
	const char *end_address = strchr (haystack, byte);
	if (end_address) {
		end_address += 1; /* skip past { or = */
		end_address += strspn (end_address, " \t");
	}

	return ((char *) end_address);
}

static int aisparser_readconfig (struct objdb_iface_ver0 *objdb,
				 const char **error_string)
{
	if (read_config_file_into_objdb(objdb, error_string)) {
		return -1;
	}

	return 0;
}


static char *remove_whitespace(char *string, int remove_colon_and_brace)
{
	char *start = string+strspn(string, " \t");
	char *end = start+(strlen(start))-1;

	while ((*end == ' ' || *end == '\t' || (remove_colon_and_brace && (*end == ':' || *end == '{'))) && end > start)
		end--;
	if (end != start)
		*(end+1) = '\0';

	return start;
}

#define PCHECK_ADD_SUBSECTION 1
#define PCHECK_ADD_ITEM       2

typedef int (*parser_check_item_f)(struct objdb_iface_ver0 *objdb,
				hdb_handle_t parent_handle,
				int type,
				const char *name,
				const char **error_string);

static int parse_section(FILE *fp,
			 struct objdb_iface_ver0 *objdb,
			 hdb_handle_t parent_handle,
			 const char **error_string,
			 int depth,
			 parser_check_item_f parser_check_item_call)
{
	char line[512];
	int i;
	char *loc;
	int ignore_line;

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
			hdb_handle_t new_parent;
			char *section = remove_whitespace(line, 1);

			loc--;
			*loc = '\0';
			if (parser_check_item_call) {
				if (!parser_check_item_call(objdb, parent_handle, PCHECK_ADD_SUBSECTION,
				    section, error_string))
					    return -1;
			}

			objdb->object_create (parent_handle, &new_parent,
					      section, strlen (section));
			if (parse_section(fp, objdb, new_parent, error_string, depth + 1, parser_check_item_call))
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
			if (parser_check_item_call) {
				if (!parser_check_item_call(objdb, parent_handle, PCHECK_ADD_ITEM,
				    key, error_string))
					    return -1;
			}
			objdb->object_key_create_typed (parent_handle, key,
				value, strlen (value) + 1, OBJDB_VALUETYPE_STRING);

			continue ;
		}

		if (strchr_rs (line, '}')) {
			if (depth == 0) {
				*error_string = "parser error: Unexpected closing brace";

				return -1;
			}

			return 0;
		}
	}

	if (parent_handle != OBJECT_PARENT_HANDLE) {
		*error_string = "parser error: Missing closing brace";
		return -1;
	}

	return 0;
}

static int parser_check_item_uidgid(struct objdb_iface_ver0 *objdb,
			hdb_handle_t parent_handle,
			int type,
			const char *name,
			const char **error_string)
{
	if (type == PCHECK_ADD_SUBSECTION) {
		if (parent_handle != OBJECT_PARENT_HANDLE) {
			*error_string = "uidgid: Can't add second level subsection";
			return 0;
		}

		if (strcmp (name, "uidgid") != 0) {
			*error_string = "uidgid: Can't add subsection different than uidgid";
			return 0;
		}
	}

	if (type == PCHECK_ADD_ITEM) {
		if (!(strcmp (name, "uid") == 0 || strcmp (name, "gid") == 0)) {
			*error_string = "uidgid: Only uid and gid are allowed items";
			return 0;
		}
	}

	return 1;
}

static int read_uidgid_files_into_objdb(
	struct objdb_iface_ver0 *objdb,
	const char **error_string)
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

	dirname = COROSYSCONFDIR "/uidgid.d";
	dp = opendir (dirname);

	if (dp == NULL)
		return 0;

	len = offsetof(struct dirent, d_name) + NAME_MAX + 1;

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

			res = parse_section(fp, objdb, OBJECT_PARENT_HANDLE, error_string, 0, parser_check_item_uidgid);

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

static int read_service_files_into_objdb(
	struct objdb_iface_ver0 *objdb,
	const char **error_string)
{
	FILE *fp;
	const char *dirname;
	DIR *dp;
	struct dirent *dirent;
	struct dirent *entry;
	char filename[PATH_MAX + FILENAME_MAX + 1];
	int res = 0;
	struct stat stat_buf;
	size_t len;
	int return_code;

	dirname = COROSYSCONFDIR "/service.d";
	dp = opendir (dirname);

	if (dp == NULL)
		return 0;

	len = offsetof(struct dirent, d_name) + NAME_MAX + 1;

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

			res = parse_section(fp, objdb, OBJECT_PARENT_HANDLE, error_string, 0, NULL);

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

/* Read config file and load into objdb */
static int read_config_file_into_objdb(
	struct objdb_iface_ver0 *objdb,
	const char **error_string)
{
	FILE *fp;
	const char *filename;
	char *error_reason = error_string_response;
	int res;

	filename = getenv ("COROSYNC_MAIN_CONFIG_FILE");
	if (!filename)
		filename = COROSYSCONFDIR "/corosync.conf";

	fp = fopen (filename, "r");
	if (fp == NULL) {
		char error_str[100];
		const char *error_ptr;
		LOGSYS_STRERROR_R (error_ptr, errno, error_str, sizeof(error_str));
		snprintf (error_reason, sizeof(error_string_response),
			"Can't read file %s reason = (%s)\n",
			 filename, error_ptr);
		*error_string = error_reason;
		return -1;
	}

	res = parse_section(fp, objdb, OBJECT_PARENT_HANDLE, error_string, 0, NULL);

	fclose(fp);

	if (res == 0) {
	        res = read_uidgid_files_into_objdb(objdb, error_string);
	}

	if (res == 0) {
	        res = read_service_files_into_objdb(objdb, error_string);
	}

	if (res == 0) {
		snprintf (error_reason, sizeof(error_string_response),
			"Successfully read main configuration file '%s'.\n", filename);
		*error_string = error_reason;
	}

	return res;
}

/*
 * Dynamic Loader definition
 */

struct config_iface_ver0 aisparser_iface_ver0 = {
	.config_readconfig        = aisparser_readconfig
};

struct lcr_iface corosync_aisparser_ver0[1] = {
	{
		.name				= "corosync_parser",
		.version			= 0,
		.versions_replace		= 0,
		.versions_replace_count		= 0,
		.dependencies			= 0,
		.dependency_count		= 0,
		.constructor			= NULL,
		.destructor			= NULL,
		.interfaces			= NULL,
	}
};

struct corosync_service_handler *aisparser_get_handler_ver0 (void);

struct lcr_comp aisparser_comp_ver0 = {
	.iface_count				= 1,
	.ifaces					= corosync_aisparser_ver0
};

#ifdef COROSYNC_SOLARIS
void corosync_lcr_component_register (void);

void corosync_lcr_component_register (void) {
#else
__attribute__ ((constructor)) static void corosync_lcr_component_register (void) {
#endif
        lcr_interfaces_set (&corosync_aisparser_ver0[0], &aisparser_iface_ver0);
	lcr_component_register (&aisparser_comp_ver0);
}
