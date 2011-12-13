/*
 * Copyright (c) 2011 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Jan Friesse (jfriesse@redhat.com)
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
 * - Neither the name of the Red Hat, Inc. nor the names of its
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
#include <poll.h>

#include <corosync/corotypes.h>
#include <corosync/cmap.h>
#include "../lib/util.h"

#ifndef INFTIM
#define INFTIM -1
#endif

#define MAX_TRY_AGAIN 10

enum user_action {
	ACTION_GET,
	ACTION_SET,
	ACTION_DELETE,
	ACTION_PRINT_ALL,
	ACTION_PRINT_DEFAULT,
	ACTION_PRINT_PREFIX,
	ACTION_TRACK
};

struct name_to_type_item {
	const char *name;
	cmap_value_types_t type;
};

struct name_to_type_item name_to_type[] = {
	{"i8", CMAP_VALUETYPE_INT8},
	{"u8", CMAP_VALUETYPE_UINT8},
	{"i16", CMAP_VALUETYPE_INT16},
	{"u16", CMAP_VALUETYPE_UINT16},
	{"i32", CMAP_VALUETYPE_INT32},
	{"u32", CMAP_VALUETYPE_UINT32},
	{"i64", CMAP_VALUETYPE_INT64},
	{"u64", CMAP_VALUETYPE_UINT64},
	{"flt", CMAP_VALUETYPE_FLOAT},
	{"dbl", CMAP_VALUETYPE_DOUBLE},
	{"str", CMAP_VALUETYPE_STRING},
	{"bin", CMAP_VALUETYPE_BINARY}};

static int convert_name_to_type(const char *name)
{
	int i;

	for (i = 0; i < sizeof(name_to_type) / sizeof(*name_to_type); i++) {
		if (strcmp(name, name_to_type[i].name) == 0) {
			return (name_to_type[i].type);
		}
	}

	return (-1);
}

static int print_help(void)
{
	printf("\n");
	printf("usage:  corosync-cmapctl ");
	printf("\n");

	return (0);
}

static void print_key(cmap_handle_t handle,
		const char *key_name,
		size_t value_len,
		const void *value,
		cmap_value_types_t type)
{
	char *str;
	cs_error_t err;
	int8_t i8;
	uint8_t u8;
	int16_t i16;
	uint16_t u16;
	int32_t i32;
	uint32_t u32;
	int64_t i64;
	uint64_t u64;
	float flt;
	double dbl;
	int end_loop;
	int no_retries;

	end_loop = 0;
	no_retries = 0;

	err = CS_OK;

	while (!end_loop) {
		switch (type) {
		case CMAP_VALUETYPE_INT8:
			if (value == NULL) {
				err = cmap_get_int8(handle, key_name, &i8);
			} else {
				i8 = *((int8_t *)value);
			}
			break;
		case CMAP_VALUETYPE_INT16:
			if (value == NULL) {
				err = cmap_get_int16(handle, key_name, &i16);
			} else {
				i16 = *((int16_t *)value);
			}
			break;
		case CMAP_VALUETYPE_INT32:
			if (value == NULL) {
				err = cmap_get_int32(handle, key_name, &i32);
			} else {
				i32 = *((int32_t *)value);
			}
			break;
		case CMAP_VALUETYPE_INT64:
			if (value == NULL) {
				err = cmap_get_int64(handle, key_name, &i64);
			} else {
				i64 = *((int64_t *)value);
			}
			break;
		case CMAP_VALUETYPE_UINT8:
			if (value == NULL) {
				err = cmap_get_uint8(handle, key_name, &u8);
			} else {
				u8 = *((uint8_t *)value);
			}
			break;
		case CMAP_VALUETYPE_UINT16:
			if (value == NULL) {
				err = cmap_get_uint16(handle, key_name, &u16);
			} else {
				u16 = *((uint16_t *)value);
			}
			break;
		case CMAP_VALUETYPE_UINT32:
			if (value == NULL) {
				err = cmap_get_uint32(handle, key_name, &u32);
			} else {
				u32 = *((uint32_t *)value);
			}
			break;
		case CMAP_VALUETYPE_UINT64:
			if (value == NULL) {
				err = cmap_get_uint64(handle, key_name, &u64);
			} else {
				u64 = *((uint64_t *)value);
			}
			break;
		case CMAP_VALUETYPE_FLOAT:
			if (value == NULL) {
				err = cmap_get_float(handle, key_name, &flt);
			} else {
				flt = *((float *)value);
			}
			break;
		case CMAP_VALUETYPE_DOUBLE:
			if (value == NULL) {
				err = cmap_get_double(handle, key_name, &dbl);
			} else {
				dbl = *((double *)value);
			}
			break;
		case CMAP_VALUETYPE_STRING:
			if (value == NULL) {
				err = cmap_get_string(handle, key_name, &str);
			} else {
				str = (char *)value;
			}
			break;
		case CMAP_VALUETYPE_BINARY: break;
		}

		if (err == CS_OK)
			end_loop = 1;

		if (err == CS_ERR_TRY_AGAIN) {
			sleep(1);
			no_retries++;
		}

		if (no_retries > MAX_TRY_AGAIN) {
			end_loop = 1;
		}
	};

	if (err != CS_OK) {
		fprintf(stderr, "Can't get value of %s. Error %s\n", key_name, cs_strerror(err));

		return ;
	}

	printf("%s (", key_name);

	switch (type) {
	case CMAP_VALUETYPE_INT8:
		printf("%s) = %"PRId8, "i8", i8);
		break;
	case CMAP_VALUETYPE_UINT8:
		printf("%s) = %"PRIu8, "u8", u8);
		break;
	case CMAP_VALUETYPE_INT16:
		printf("%s) = %"PRId16, "i16", i16);
		break;
	case CMAP_VALUETYPE_UINT16:
		printf("%s) = %"PRIu16, "u16", u16);
		break;
	case CMAP_VALUETYPE_INT32:
		printf("%s) = %"PRId32, "i32", i32);
		break;
	case CMAP_VALUETYPE_UINT32:
		printf("%s) = %"PRIu32, "u32", u32);
		break;
	case CMAP_VALUETYPE_INT64:
		printf("%s) = %"PRId64, "i64", i64);
		break;
	case CMAP_VALUETYPE_UINT64:
		printf("%s) = %"PRIu64, "u64", u64);
		break;
	case CMAP_VALUETYPE_FLOAT:
		printf("%s) = %f", "flt", flt);
		break;
	case CMAP_VALUETYPE_DOUBLE:
		printf("%s) = %lf", "dbl", dbl);
		break;
	case CMAP_VALUETYPE_STRING:
		printf("%s) = %s", "str", str);
		if (value == NULL) {
			free(str);
		}
		break;
	case CMAP_VALUETYPE_BINARY:
		printf("%s)", "bin");
		break;
	}

	printf("\n");
}

static void print_iter(enum user_action action, cmap_handle_t handle, const char *prefix)
{
	cmap_iter_handle_t iter_handle;
	char key_name[CMAP_KEYNAME_MAXLEN + 1];
	size_t value_len;
	cmap_value_types_t type;
	cs_error_t err;

	err = cmap_iter_init(handle, prefix, &iter_handle);
	if (err != CS_OK) {
		fprintf (stderr, "Failed to initialize iteration. Error %s\n", cs_strerror(err));
		exit (EXIT_FAILURE);
	}

	while ((err = cmap_iter_next(handle, iter_handle, key_name, &value_len, &type)) == CS_OK) {
		if (action == ACTION_PRINT_DEFAULT &&
			strncmp(key_name, "internal_configuration", strlen("internal_configuration") == 0))
			continue;
		print_key(handle, key_name, value_len, NULL, type);
	}
}

static void cmap_notify_fn(
	cmap_handle_t cmap_handle,
	cmap_track_handle_t cmap_track_handle,
	int32_t event,
	const char *key_name,
	struct cmap_notify_value new_val,
	struct cmap_notify_value old_val,
	void *user_data)
{
	switch (event) {
	case CMAP_TRACK_ADD:
		printf("create> ");
		print_key(cmap_handle, key_name, new_val.len, new_val.data, new_val.type);
		break;
	case CMAP_TRACK_DELETE:
		printf("delete> ");
		print_key(cmap_handle, key_name, old_val.len, old_val.data, old_val.type);
		break;
	case CMAP_TRACK_MODIFY:
		printf("modify> ");
		print_key(cmap_handle, key_name, new_val.len, new_val.data, new_val.type);
		break;
	default:
		printf("unknown change> ");
		break;
	}

}

static void add_track(cmap_handle_t handle, const char *key_name, int prefix)
{
	cmap_track_handle_t track_handle;
	int32_t track_type;
	cs_error_t err;

	track_type = CMAP_TRACK_ADD | CMAP_TRACK_DELETE | CMAP_TRACK_MODIFY;
	if (prefix) {
		track_type |= CMAP_TRACK_PREFIX;
	}

	err = cmap_track_add(handle, key_name, track_type, cmap_notify_fn, NULL, &track_handle);
	if (err != CS_OK) {
		fprintf(stderr, "Failed to add tracking function. Error %s\n", cs_strerror(err));
		exit (EXIT_FAILURE);
	}
}

static void track_changes(cmap_handle_t handle)
{
	struct pollfd pfd[2];
	int cmap_fd;
	cs_error_t err;
	int poll_res;
	char inbuf[3];
	int quit = CS_FALSE;

	err = cmap_fd_get(handle, &cmap_fd);
	if (err != CS_OK) {
		fprintf(stderr, "Failed to get file handle. Error %s\n", cs_strerror(err));
		exit (EXIT_FAILURE);
	}

	pfd[0].fd = cmap_fd;
	pfd[1].fd = STDIN_FILENO;
	pfd[0].events = pfd[1].events = POLLIN;

	printf("Type \"q\" to finish\n");
	do {
		pfd[0].revents = pfd[1].revents = 0;
		poll_res = poll(pfd, 2, INFTIM);
		if (poll_res == -1) {
			perror("poll");
		}

		if (pfd[1].revents & POLLIN) {
			if (fgets(inbuf, sizeof(inbuf), stdin) == NULL) {
				quit = CS_TRUE;
			} else if (strncmp(inbuf, "q", 1) == 0) {
				quit = CS_TRUE;
			}
		}

		if (pfd[0].revents & POLLIN) {
			err = cmap_dispatch(handle, CS_DISPATCH_ALL);
			if (err != CS_OK) {
				fprintf(stderr, "Dispatch error %s\n", cs_strerror(err));
				quit = CS_TRUE;
			}
		}
	} while (poll_res > 0 && !quit);
}


static void set_key(cmap_handle_t handle, const char *key_name, const char *key_type_s, const char *key_value_s)
{
	int64_t i64;
	uint64_t u64;
	double dbl;
	float flt;
	cs_error_t err;
	int scanf_res;

	cmap_value_types_t type;

	if (convert_name_to_type(key_type_s) == -1) {
		fprintf(stderr, "Unknown type %s\n", key_type_s);
		exit (EXIT_FAILURE);
	}

	type = convert_name_to_type(key_type_s);

	switch (type) {
	case CMAP_VALUETYPE_INT8:
	case CMAP_VALUETYPE_INT16:
	case CMAP_VALUETYPE_INT32:
	case CMAP_VALUETYPE_INT64:
		scanf_res = sscanf(key_value_s, "%"PRId64, &i64);
		break;
	case CMAP_VALUETYPE_UINT8:
	case CMAP_VALUETYPE_UINT16:
	case CMAP_VALUETYPE_UINT32:
	case CMAP_VALUETYPE_UINT64:
		scanf_res = sscanf(key_value_s, "%"PRIu64, &u64);
		break;
	case CMAP_VALUETYPE_FLOAT:
		scanf_res = sscanf(key_value_s, "%f", &flt);
		break;
	case CMAP_VALUETYPE_DOUBLE:
		scanf_res = sscanf(key_value_s, "%lf", &dbl);
		break;
	case CMAP_VALUETYPE_STRING:
	case CMAP_VALUETYPE_BINARY:
		/*
		 * Do nothing
		 */
		scanf_res = 1;
		break;
	}

	if (scanf_res != 1) {
		fprintf(stderr, "%s is not valid %s type value\n", key_value_s, key_type_s);
		exit(EXIT_FAILURE);
	}
	/*
	 * We have parsed value, so insert value
	 */

	switch (type) {
	case CMAP_VALUETYPE_INT8:
		if (i64 > INT8_MAX || i64 < INT8_MIN) {
			fprintf(stderr, "%s is not valid i8 integer\n", key_value_s);
			exit(EXIT_FAILURE);
		}
		err = cmap_set_int8(handle, key_name, i64);
		break;
	case CMAP_VALUETYPE_INT16:
		if (i64 > INT16_MAX || i64 < INT16_MIN) {
			fprintf(stderr, "%s is not valid i16 integer\n", key_value_s);
			exit(EXIT_FAILURE);
		}
		err = cmap_set_int16(handle, key_name, i64);
		break;
	case CMAP_VALUETYPE_INT32:
		if (i64 > INT32_MAX || i64 < INT32_MIN) {
			fprintf(stderr, "%s is not valid i32 integer\n", key_value_s);
			exit(EXIT_FAILURE);
		}
		err = cmap_set_int32(handle, key_name, i64);
		break;
	case CMAP_VALUETYPE_INT64:
		err = cmap_set_int64(handle, key_name, i64);
		break;

	case CMAP_VALUETYPE_UINT8:
		if (u64 > UINT8_MAX) {
			fprintf(stderr, "%s is not valid u8 integer\n", key_value_s);
			exit(EXIT_FAILURE);
		}
		err = cmap_set_uint8(handle, key_name, u64);
		break;
	case CMAP_VALUETYPE_UINT16:
		if (u64 > UINT16_MAX) {
			fprintf(stderr, "%s is not valid u16 integer\n", key_value_s);
			exit(EXIT_FAILURE);
		}
		err = cmap_set_uint16(handle, key_name, u64);
		break;
	case CMAP_VALUETYPE_UINT32:
		if (u64 > UINT32_MAX) {
			fprintf(stderr, "%s is not valid u32 integer\n", key_value_s);
			exit(EXIT_FAILURE);
		}
		err = cmap_set_uint32(handle, key_name, u64);
		break;
	case CMAP_VALUETYPE_UINT64:
		err = cmap_set_uint64(handle, key_name, u64);
		break;
	case CMAP_VALUETYPE_FLOAT:
		err = cmap_set_float(handle, key_name, flt);
		break;
	case CMAP_VALUETYPE_DOUBLE:
		err = cmap_set_double(handle, key_name, dbl);
		break;
	case CMAP_VALUETYPE_STRING:
		err = cmap_set_string(handle, key_name, key_value_s);
		break;
	case CMAP_VALUETYPE_BINARY:
		exit(1);
		break;
	}

	if (err != CS_OK) {
		fprintf (stderr, "Failed to set key %s. Error %s\n", key_name, cs_strerror(err));
		exit (EXIT_FAILURE);
	}
}

int main(int argc, char *argv[])
{
	enum user_action action;
	int c;
	cs_error_t err;
	cmap_handle_t handle;
	int i;
	size_t value_len;
	cmap_value_types_t type;
	int track_prefix;
	int no_retries;

	action = ACTION_PRINT_PREFIX;
	track_prefix = 1;

	while ((c = getopt(argc, argv, "hagsdtT")) != -1) {
		switch (c) {
		case 'h':
			return print_help();
			break;
		case 'a':
			action = ACTION_PRINT_ALL;
			break;
		case 'g':
			action = ACTION_GET;
			break;
		case 's':
			action = ACTION_SET;
			break;
		case 'd':
			action = ACTION_DELETE;
			break;
		case 't':
			action = ACTION_TRACK;
			break;
		case 'T':
			action = ACTION_TRACK;
			track_prefix = 0;
			break;
		case '?':
			return (EXIT_FAILURE);
			break;
		default:
			action = ACTION_PRINT_PREFIX;
			break;
		}
	}

	if (argc == 1) {
		action = ACTION_PRINT_DEFAULT;
	}

	argc -= optind;
	argv += optind;

	if (argc == 0 && action != ACTION_PRINT_DEFAULT && action != ACTION_PRINT_ALL) {
		fprintf(stderr, "Expected key after options\n");
		return (EXIT_FAILURE);
	}

	no_retries = 0;
	while ((err = cmap_initialize(&handle)) == CS_ERR_TRY_AGAIN && no_retries++ < MAX_TRY_AGAIN) {
		sleep(1);
	}

	if (err != CS_OK) {
		fprintf (stderr, "Failed to initialize the cmap API. Error %s\n", cs_strerror(err));
		exit (EXIT_FAILURE);
	}

	switch (action) {
	case ACTION_PRINT_DEFAULT:
	case ACTION_PRINT_ALL:
		print_iter(action, handle, NULL);
		break;
	case ACTION_PRINT_PREFIX:
		for (i = 0; i < argc; i++) {
			print_iter(action, handle, argv[i]);
		}
		break;
	case ACTION_GET:
		for (i = 0; i < argc; i++) {
			err = cmap_get(handle, argv[i], NULL, &value_len, &type);
			if (err == CS_OK) {
				print_key(handle, argv[i], value_len, NULL, type);
			} else {
				fprintf(stderr, "Can't get key %s. Error %s\n", argv[i], cs_strerror(err));
			}
		}
		break;
	case ACTION_DELETE:
		for (i = 0; i < argc; i++) {
			err = cmap_delete(handle, argv[i]);
			if (err != CS_OK) {
				fprintf(stderr, "Can't delete key %s. Error %s\n", argv[i], cs_strerror(err));
			}
		}
		break;
	case ACTION_TRACK:
		for (i = 0; i < argc; i++) {
			add_track(handle, argv[i], track_prefix);
		}
		track_changes(handle);
		break;
	case ACTION_SET:
		if (argc < 3) {
			fprintf(stderr, "At least 3 parameters are expected for set\n");
			return (EXIT_FAILURE);
		}

		set_key(handle, argv[0], argv[1], argv[2]);
		break;

	}

	err = cmap_finalize(handle);
	if (err != CS_OK) {
		fprintf (stderr, "Failed to finalize the cmap API. Error %s\n", cs_strerror(err));
		exit (EXIT_FAILURE);
	}

	return (0);
}
