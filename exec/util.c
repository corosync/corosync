/*
 * Copyright (c) 2002-2004 MontaVista Software, Inc.
 * Copyright (c) 2004 Open Source Development Lab
 * Copyright (c) 2006-2012 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com), Mark Haverkamp (markh@osdl.org)
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
#include <sys/time.h>
#include <assert.h>

#include <libknet.h>

#include <corosync/corotypes.h>
#include <corosync/corodefs.h>
#include <corosync/icmap.h>
#include <corosync/logsys.h>
#include "util.h"

LOGSYS_DECLARE_SUBSYS ("MAIN");

struct service_names {
	const char *c_name;
	int32_t c_val;
};

static struct service_names servicenames[] =
{
	{ "CFG", CFG_SERVICE },
	{ "CPG", CPG_SERVICE },
	{ "QUORUM", QUORUM_SERVICE },
	{ "PLOAD", PLOAD_SERVICE },
	{ "VOTEQUORUM", VOTEQUORUM_SERVICE },
	{ "MON", MON_SERVICE },
	{ "WD", WD_SERVICE },
	{ "CMAP", CMAP_SERVICE },
	{ NULL, -1 }
};

const char * short_service_name_get(uint32_t service_id,
	char *buf, size_t buf_size)
{
	uint32_t i;

	for (i = 0; servicenames[i].c_name != NULL; i++) {
		if (service_id == servicenames[i].c_val) {
			return (servicenames[i].c_name);
		}
	}
	snprintf(buf, buf_size, "%d", service_id);
	return buf;
}

/*
 * Compare two names.  returns non-zero on match.
 */
int name_match(cs_name_t *name1, cs_name_t *name2)
{
	if (name1->length == name2->length) {
		return ((strncmp ((char *)name1->value, (char *)name2->value,
			name1->length)) == 0);
	}
	return 0;
}

/*
 * Get the time of day and convert to nanoseconds
 */
cs_time_t clust_time_now(void)
{
	struct timeval tv;
	cs_time_t time_now;

	if (gettimeofday(&tv, 0)) {
		return 0ULL;
	}

	time_now = (cs_time_t)(tv.tv_sec) * 1000000000ULL;
	time_now += (cs_time_t)(tv.tv_usec) * 1000ULL;

	return time_now;
}

void _corosync_out_of_memory_error (void) __attribute__((noreturn));
void _corosync_out_of_memory_error (void)
{
	assert (0==1);
	exit (EXIT_FAILURE);
}

void _corosync_exit_error (
	enum e_corosync_done err, const char *file, unsigned int line)  __attribute__((noreturn));

void _corosync_exit_error (
	enum e_corosync_done err, const char *file, unsigned int line)
{
	if (err == COROSYNC_DONE_EXIT) {
		log_printf (LOGSYS_LEVEL_NOTICE,
			"Corosync Cluster Engine exiting normally");
	} else {
		log_printf (LOGSYS_LEVEL_ERROR, "Corosync Cluster Engine exiting "
			"with status %d at %s:%u.", err, file, line);
	}
	logsys_system_fini ();
	exit (err);
}

char *getcs_name_t (cs_name_t *name)
{
	static char ret_name[CS_MAX_NAME_LENGTH];

	/* if string is corrupt (non-terminated), ensure it's displayed safely */
	if (name->length >= CS_MAX_NAME_LENGTH || name->value[name->length] != '\0') {
		memset (ret_name, 0, sizeof (ret_name));
		memcpy (ret_name, name->value, min(name->length, CS_MAX_NAME_LENGTH -1));
		return (ret_name);
	}
	return ((char *)name->value);
}

void setcs_name_t (cs_name_t *name, char *str) {
	strncpy ((char *)name->value, str, sizeof (name->value) - 1);
	((char *)name->value)[sizeof (name->value) - 1] = '\0';
	if (strlen ((char *)name->value) > CS_MAX_NAME_LENGTH) {
		name->length = CS_MAX_NAME_LENGTH;
	} else {
		name->length = strlen (str);
	}
}

int cs_name_tisEqual (cs_name_t *str1, char *str2) {
	if (str1->length == strlen (str2)) {
		return ((strncmp ((char *)str1->value, (char *)str2,
			str1->length)) == 0);
	} else {
		return 0;
	}
}

const char *get_state_dir(void)
{
	static char path[PATH_MAX] = {'\0'};
	char *state_dir;
	int res;

	if (path[0] == '\0') {
		if (icmap_get_string("system.state_dir", &state_dir) == CS_OK) {
			res = snprintf(path, PATH_MAX, "%s", state_dir);
			free(state_dir);
		} else if ((state_dir = getenv("STATE_DIRECTORY")) != NULL) {
			/*
			 * systemd allows multiple directory names that are
			 * passed to env variable separated by colon. Support for this feature
			 * is deliberately not implemented because corosync always
			 * uses just one state directory and it is unclear what behavior should
			 * be taken for multiple ones. If reasonable need for
			 * supporting multiple directories appear, it must be implemented also
			 * for cmap.
			 */
			res = snprintf(path, PATH_MAX, "%s", state_dir);
		} else {
			res = snprintf(path, PATH_MAX, "%s/%s", LOCALSTATEDIR, "lib/corosync");
		}

		assert(res < PATH_MAX);
	}

	return (path);
}

static int safe_strcat(char *dst, size_t dst_len, const char *src)
{

	if (strlen(dst) + strlen(src) >= dst_len - 1) {
		return (-1);
	}

	strcat(dst, src);

	return (0);
}

/*
 * val - knet crypto model to find
 * crypto_list_str - string with concatenated list of available crypto models - can be NULL
 * machine_parseable_str - 0 - split strings by space, 1 - use human form (split by "," and last item with "or")
 * error_string_prefix - Prefix to add into error string
 * error_string - Complete error string
 */
int util_is_valid_knet_crypto_model(const char *val,
	const char **list_str, int machine_parseable_str,
	const char *error_string_prefix, const char **error_string)
{
	size_t entries;
	struct knet_crypto_info crypto_list[16];
	size_t zi;
	static char local_error_str[512];
	static char local_list_str[256];
	int model_found = 0;

	if (list_str != NULL) {
		*list_str = local_list_str;
	}

	memset(local_error_str, 0, sizeof(local_error_str));
	memset(local_list_str, 0, sizeof(local_list_str));

	safe_strcat(local_error_str, sizeof(local_error_str), error_string_prefix);

	if (knet_get_crypto_list(NULL, &entries) != 0) {
		*error_string = "internal error - cannot get knet crypto list";
		return (-1);
	}

	if (entries > sizeof(crypto_list) / sizeof(crypto_list[0])) {
		*error_string = "internal error - too many knet crypto list entries";
		return (-1);
	}

	if (knet_get_crypto_list(crypto_list, &entries) != 0) {
		*error_string = "internal error - cannot get knet crypto list";
		return (-1);
	}

	for (zi = 0; zi < entries; zi++) {
		if (zi == 0) {
		} else if (zi == entries - 1) {
			if (machine_parseable_str) {
				(void)safe_strcat(local_list_str, sizeof(local_list_str), " ");
			} else {
				(void)safe_strcat(local_list_str, sizeof(local_list_str), " or ");
			}
		} else {
			if (machine_parseable_str) {
				(void)safe_strcat(local_list_str, sizeof(local_list_str), " ");
			} else {
				(void)safe_strcat(local_list_str, sizeof(local_list_str), ", ");
			}
		}

		(void)safe_strcat(local_list_str, sizeof(local_list_str), crypto_list[zi].name);

		if (val != NULL && strcmp(val, crypto_list[zi].name) == 0) {
			model_found = 1;
		}
	}

	if (!model_found) {
		(void)safe_strcat(local_error_str, sizeof(local_error_str), local_list_str);
		*error_string = local_error_str;
	}

	return (model_found);
}

int util_is_valid_knet_compress_model(const char *val,
	const char **list_str, int machine_parseable_str,
	const char *error_string_prefix, const char **error_string)
{
	size_t entries;
	struct knet_compress_info compress_list[16];
	size_t zi;
	static char local_error_str[512];
	static char local_list_str[256];
	int model_found = 0;

	if (list_str != NULL) {
		*list_str = local_list_str;
	}

	memset(local_error_str, 0, sizeof(local_error_str));
	memset(local_list_str, 0, sizeof(local_list_str));

	safe_strcat(local_error_str, sizeof(local_error_str), error_string_prefix);

	if (knet_get_compress_list(NULL, &entries) != 0) {
		*error_string = "internal error - cannot get knet compress list";
		return (-1);
	}

	if (entries > sizeof(compress_list) / sizeof(compress_list[0])) {
		*error_string = "internal error - too many knet compress list entries";
		return (-1);
	}

	if (knet_get_compress_list(compress_list, &entries) != 0) {
		*error_string = "internal error - cannot get knet compress list";
		return (-1);
	}

	for (zi = 0; zi < entries; zi++) {
		if (zi == 0) {
		} else if (zi == entries - 1) {
			if (machine_parseable_str) {
				(void)safe_strcat(local_list_str, sizeof(local_list_str), " ");
			} else {
				(void)safe_strcat(local_list_str, sizeof(local_list_str), " or ");
			}
		} else {
			if (machine_parseable_str) {
				(void)safe_strcat(local_list_str, sizeof(local_list_str), " ");
			} else {
				(void)safe_strcat(local_list_str, sizeof(local_list_str), ", ");
			}
		}

		(void)safe_strcat(local_list_str, sizeof(local_list_str), compress_list[zi].name);

		if (val != NULL && strcmp(val, compress_list[zi].name) == 0) {
			model_found = 1;
		}
	}

	if (!model_found) {
		(void)safe_strcat(local_error_str, sizeof(local_error_str), local_list_str);
		*error_string = local_error_str;
	}

	return (model_found);
}
