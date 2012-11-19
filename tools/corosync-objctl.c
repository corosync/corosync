/*
 * Copyright (c) 2008 Allied Telesis Labs NZ
 *                 (c) 2012 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Angus Salkeld <angus.salkeld@alliedtelesis.co.nz>
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

#include <sys/select.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/un.h>

#include <corosync/corotypes.h>
#include <corosync/confdb.h>

#define SEPERATOR '.'
#define SEPERATOR_STR "."
#define OBJ_NAME_SIZE 4096


typedef enum {
	ACTION_READ,
	ACTION_WRITE,
	ACTION_CREATE,
	ACTION_CREATE_KEY,
	ACTION_DELETE,
	ACTION_PRINT_ALL,
	ACTION_PRINT_DEFAULT,
	ACTION_TRACK,
} action_types_t;

typedef enum {
	FIND_OBJECT_ONLY,
	FIND_OBJECT_OR_KEY,
	FIND_KEY_ONLY
} find_object_of_type_t;

static void tail_key_changed(confdb_handle_t handle,
	confdb_change_type_t change_type,
	hdb_handle_t parent_object_handle,
	hdb_handle_t object_handle,
	const void *object_name,
	size_t  object_name_len,
	const void *key_name,
	size_t key_name_len,
	const void *key_value,
	size_t key_value_len);

static void tail_object_created(confdb_handle_t handle,
	hdb_handle_t parent_object_handle,
	hdb_handle_t object_handle,
	const void *name_pt,
	size_t name_len);

static void tail_object_deleted(confdb_handle_t handle,
	hdb_handle_t parent_object_handle,
	const void *name_pt,
	size_t name_len);

static void create_object(confdb_handle_t handle, char * name_pt);
static void create_object_key(confdb_handle_t handle, char * name_pt);
static void write_key(confdb_handle_t handle, char * path_pt);
static void get_parent_name(const char * name_pt, char * parent_name);

static confdb_callbacks_t callbacks = {
	.confdb_key_change_notify_fn = tail_key_changed,
	.confdb_object_create_change_notify_fn = tail_object_created,
	.confdb_object_delete_change_notify_fn = tail_object_deleted,
};

static int debug = 0;
static int show_binary = 0;
static int action;

static void print_binary_key (char *value, size_t value_len)
{
	size_t i;
	char c;

	for (i = 0; i < value_len; i++) {
		c = value[i];
		if (c >= ' ' && c < 0x7f && c != '\\') {
			fputc (c, stdout);
		} else {
			if (c == '\\') {
				printf ("\\\\");
			} else {
				printf ("\\x%02X", c);
			}
		}
	}
	printf ("\n");
}

static void print_key (char *key_name, void *value, size_t value_len, confdb_value_types_t type)
{
	switch (type) {
		case CONFDB_VALUETYPE_INT16:
			printf ("%s=%hd\n", key_name,
					  *(int16_t*)value);
			break;
		case CONFDB_VALUETYPE_UINT16:
			printf ("%s=%hu\n", key_name,
					  *(uint16_t*)value);
			break;
		case CONFDB_VALUETYPE_INT32:
			printf ("%s=%d\n", key_name,
					  *(int32_t*)value);
			break;
		case CONFDB_VALUETYPE_UINT32:
			printf ("%s=%u\n", key_name,
					  *(uint32_t*)value);
			break;
		case CONFDB_VALUETYPE_INT64:
			printf ("%s=%"PRIi64"\n", key_name,
					  *(int64_t*)value);
			break;
		case CONFDB_VALUETYPE_UINT64:
			printf ("%s=%"PRIu64"\n", key_name,
					  *(uint64_t*)value);
			break;
		case CONFDB_VALUETYPE_FLOAT:
			printf ("%s=%f\n", key_name,
					  *(float*)value);
			break;
		case CONFDB_VALUETYPE_DOUBLE:
			printf ("%s=%f\n", key_name,
					  *(double*)value);
			break;
		case CONFDB_VALUETYPE_STRING:
			printf ("%s=%s\n", key_name, (char*)value);
			break;
		default:
		case CONFDB_VALUETYPE_ANY:
			if (!show_binary) {
				printf ("%s=**binary**(%d)\n", key_name, type);
			} else {
				printf ("%s=", key_name);
				print_binary_key ((char *)value, value_len);
			}
			break;
	}
}

/* Recursively dump the object tree */
static void print_config_tree(confdb_handle_t handle, hdb_handle_t parent_object_handle, char * parent_name)
{
	hdb_handle_t object_handle;
	char object_name[OBJ_NAME_SIZE];
	size_t object_name_len;
	char key_name[OBJ_NAME_SIZE];
	char output_string[OBJ_NAME_SIZE];
	char *key_value=NULL;/* Dynamically allocated value */
	size_t key_value_len;
	cs_error_t res;
	int children_printed;
	confdb_value_types_t type;

	/* Show the keys */
	res = confdb_key_iter_start(handle, parent_object_handle);
	if (res != CS_OK) {
		fprintf(stderr, "error resetting key iterator for object "HDB_X_FORMAT" %d\n", parent_object_handle, res);
		exit(EXIT_FAILURE);
	}
	children_printed = 0;

	while ( (res = confdb_key_iter_typed2(handle,
								   parent_object_handle,
								   key_name,
					                           (void**)&key_value,
								   &key_value_len,
								   &type)) == CS_OK) {
		key_value[key_value_len] = '\0';
		if (parent_name != NULL)
			printf("%s%c", parent_name, SEPERATOR);

		print_key(key_name, key_value, key_value_len, type);
		free(key_value);
		key_value = NULL;

		children_printed++;
	}

	/* Show sub-objects */
	res = confdb_object_iter_start(handle, parent_object_handle);
	if (res != CS_OK) {
		fprintf(stderr, "error resetting object iterator for object "HDB_X_FORMAT" %d\n", parent_object_handle, res);
		exit(EXIT_FAILURE);
	}

	while ( (res = confdb_object_iter(handle,
		parent_object_handle,
		&object_handle,
		object_name,
		&object_name_len)) == CS_OK)	{

		object_name[object_name_len] = '\0';
		if (parent_name != NULL) {
			snprintf(output_string, OBJ_NAME_SIZE, "%s%c%s", parent_name, SEPERATOR, object_name);
		} else {
			if ((action == ACTION_PRINT_DEFAULT) && strcmp(object_name, "internal_configuration") == 0) continue;
			snprintf(output_string, OBJ_NAME_SIZE, "%s", object_name);
		}
		print_config_tree(handle, object_handle, output_string);
		children_printed++;
	}
	if (children_printed == 0 && parent_name != NULL) {
			printf("%s\n", parent_name);
	}
}

static int read_in_config_file (char * filename)
{
	confdb_handle_t handle;
	int result;
	int ignore;
	int c;
	FILE* fh;
	char buf[1024];
	char * line;
	char * end;
	char parent_name[OBJ_NAME_SIZE];

	if (access (filename, R_OK) != 0) {
		perror ("Couldn't access file.");
		return -1;
	}

	fh = fopen(filename, "r");
	if (fh == NULL) {
		perror ("Couldn't open file.");
		return -1;
	}
	result = confdb_initialize (&handle, &callbacks);
	if (result != CONFDB_OK) {
		fprintf (stderr, "Could not initialize objdb library. Error %d\n", result);
		fclose (fh);
		return -1;
	}

	while (fgets (buf, 1024, fh) != NULL) {
		/* find the first real character, if it is
		 * a '#' then ignore this line.
		 * else process.
		 * if no real characters then also ignore.
		 */
		ignore = 1;
		for (c = 0; c < 1024; c++) {
			if (isblank (buf[c]))
				continue;

			if (buf[c] == '#' || buf[c] == '\n') {
				ignore = 1;
				break;
			}
			ignore = 0;
			line = &buf[c];
			break;
		}
		if (ignore == 1)
			continue;

		/* kill the \n */
		end = strchr (line, '\n');
		if (end != NULL)
			*end = '\0';

		if (debug == 2)
			printf ("%d: %s\n", __LINE__, line);

		/* find the parent object */
		get_parent_name(line, parent_name);

		if (debug == 2)
			printf ("%d: %s\n", __LINE__, parent_name);

		/* create the object */
		create_object (handle, parent_name);
		/* write the attribute */
		write_key (handle, line);
	}
	confdb_finalize (handle);
	fclose (fh);
	return 0;
}

static int print_all(void)
{
	confdb_handle_t handle;
	int result;

	result = confdb_initialize (&handle, &callbacks);
	if (result != CS_OK) {
		fprintf (stderr, "Could not initialize objdb library. Error %d\n", result);
		return 1;
	}

	print_config_tree(handle, OBJECT_PARENT_HANDLE, NULL);

	result = confdb_finalize (handle);

	return 0;
}


static int print_help(void)
{
	printf("\n");
	printf ("usage:  corosync-objctl [-b] object%ckey ...               Print an object\n", SEPERATOR);
	printf ("        corosync-objctl -c object%cchild_obj ...           Create Object\n", SEPERATOR);
	printf ("        corosync-objctl -d object%cchild_obj ...           Delete object\n", SEPERATOR);
	printf ("        corosync-objctl -w object%cchild_obj.key=value ... Create a key\n", SEPERATOR);
	printf ("        corosync-objctl -n object%cchild_obj.key=value ... Create a new object with the key\n", SEPERATOR);
	printf ("        corosync-objctl -t object%cchild_obj ...           Track changes\n", SEPERATOR);
	printf ("        corosync-objctl [-b] -a                           Print all objects\n");
	printf ("        corosync-objctl -p <filename> Load in config from the specified file.\n");
	printf("\n");
	return 0;
}

static cs_error_t validate_name(char * obj_name_pt)
{
	if ((strchr (obj_name_pt, SEPERATOR) == NULL) &&
		(strchr (obj_name_pt, '=') == NULL))
		return CS_OK;
	else
		return CS_ERR_INVALID_PARAM;
}

static void get_parent_name(const char * name_pt, char * parent_name)
{
	char * tmp;
	strcpy(parent_name, name_pt);

	/* first remove the value (it could be a file path */
	tmp = strchr(parent_name, '=');
	if (tmp != NULL) {
		*tmp = '\0';
		tmp--;
		while (isblank (*tmp)) {
			*tmp = '\0';
			tmp--;
		}
	}

	/* then truncate the child name */
	tmp = strrchr(parent_name, SEPERATOR);
	if (tmp != NULL) *tmp = '\0';
}

static void get_key(const char * name_pt, char * key_name, char * key_value)
{
	char * tmp = (char*)name_pt;
	char str_copy[OBJ_NAME_SIZE];
	char * copy_tmp = str_copy;
	int equals_seen = 0;
	int in_quotes = 0;

	/* strip out spaces when not in quotes */
	while (*tmp != '\0') {
		if (*tmp == '=')
			equals_seen = 1;
		if (equals_seen && *tmp == '"') {
			if (in_quotes)
				in_quotes = 0;
			else
				in_quotes = 1;
		}
		if (*tmp != ' ' || in_quotes) {
			*copy_tmp = *tmp;
			copy_tmp++;
		}
		tmp++;
	}
	*copy_tmp = '\0';

	/* first remove the value (it could have a SEPERATOR in it */
	tmp = strchr(str_copy, '=');
	if (tmp != NULL && strlen(tmp) > 0) {
		strcpy(key_value, tmp+1);
		*tmp = '\0';
	} else {
		key_value[0] = '\0';
	}
	/* then remove the name */
	tmp = strrchr(str_copy, SEPERATOR);
	if (tmp == NULL) tmp = str_copy;
	strcpy(key_name, tmp+1);
}

static cs_error_t find_object (confdb_handle_t handle,
			char * name_pt,
			find_object_of_type_t type,
			hdb_handle_t * out_handle)
{
	char * obj_name_pt;
	char * save_pt;
	hdb_handle_t obj_handle;
	confdb_handle_t parent_object_handle = OBJECT_PARENT_HANDLE;
	char tmp_name[OBJ_NAME_SIZE];
	cs_error_t res = CS_OK;

	strncpy (tmp_name, name_pt, sizeof (tmp_name));
	tmp_name[sizeof (tmp_name) - 1] = '\0';
	obj_name_pt = strtok_r(tmp_name, SEPERATOR_STR, &save_pt);

	while (obj_name_pt != NULL) {
		res = confdb_object_find_start(handle, parent_object_handle);
		if (res != CS_OK) {
			fprintf (stderr, "Could not start object_find %d\n", res);
			exit (EXIT_FAILURE);
		}

		res = confdb_object_find(handle, parent_object_handle,
				obj_name_pt, strlen (obj_name_pt), &obj_handle);
		if (res != CS_OK) {
			return res;
		}

		parent_object_handle = obj_handle;
		obj_name_pt = strtok_r (NULL, SEPERATOR_STR, &save_pt);
	}

	*out_handle = parent_object_handle;
	return res;
}

static void read_object(confdb_handle_t handle, char * name_pt)
{
	char parent_name[OBJ_NAME_SIZE];
	hdb_handle_t obj_handle;
	cs_error_t res;

	get_parent_name(name_pt, parent_name);
	res = find_object (handle, name_pt, FIND_OBJECT_OR_KEY, &obj_handle);
	if (res == CS_OK) {
		print_config_tree(handle, obj_handle, parent_name);
	}
}

static void write_key(confdb_handle_t handle, char * path_pt)
{
	hdb_handle_t obj_handle;
	char parent_name[OBJ_NAME_SIZE];
	char key_name[OBJ_NAME_SIZE];
	char key_value[OBJ_NAME_SIZE];
	char old_key_value[OBJ_NAME_SIZE];
	char *old_key_value_ptr = old_key_value;
	size_t old_key_value_len;
	cs_error_t res;
	confdb_value_types_t type;

	/* find the parent object */
	get_parent_name(path_pt, parent_name);
	get_key(path_pt, key_name, key_value);

	if (debug == 1)
		printf ("%d: key:\"%s\", value:\"%s\"\n",
				__LINE__, key_name, key_value);

	if (validate_name(key_name) != CS_OK) {
		fprintf(stderr, "Incorrect key name, can not have \"=\" or \"%c\"\n", SEPERATOR);
		exit(EXIT_FAILURE);
	}
	res = find_object (handle, parent_name, FIND_OBJECT_ONLY, &obj_handle);

	if (res != CS_OK) {
		fprintf(stderr, "Can't find parent object of \"%s\"\n", path_pt);
		exit(EXIT_FAILURE);
	}

	/* get the current key */
	res = confdb_key_get_typed2 (handle,
						  obj_handle,
						  key_name,
				                  (void**)&old_key_value_ptr,
						  &old_key_value_len, &type);

	if (debug == 1 && res==CS_OK)
		printf ("%d: key:\"%s\", old value:\"%s\"\n",
				__LINE__, key_name, old_key_value);

	if (res == CS_OK) {
		/* replace the current value */
		res = confdb_key_replace (handle,
								  obj_handle,
								  key_name,
								  strlen(key_name),
								  old_key_value,
								  old_key_value_len,
								  key_value,
								  strlen(key_value)+1);

		if (res != CS_OK)
			fprintf(stderr, "Failed to replace the key %s=%s. Error %d\n", key_name, key_value, res);
	} else {
		/* not there, create a new key */
		res = confdb_key_create_typed (handle,
								 obj_handle,
								 key_name,
								 key_value,
								 strlen(key_value)+1,
								 CONFDB_VALUETYPE_STRING);
		if (res != CS_OK)
			fprintf(stderr, "Failed to create the key %s=%s. Error %d\n", key_name, key_value, res);
	}

}

static void create_object(confdb_handle_t handle, char * name_pt)
{
	char * obj_name_pt;
	char * save_pt;
	hdb_handle_t obj_handle;
	hdb_handle_t parent_object_handle = OBJECT_PARENT_HANDLE;
	char tmp_name[OBJ_NAME_SIZE];
	cs_error_t res;

	strncpy (tmp_name, name_pt, sizeof (tmp_name));
	tmp_name[sizeof (tmp_name) - 1] = '\0';
	obj_name_pt = strtok_r(tmp_name, SEPERATOR_STR, &save_pt);

	while (obj_name_pt != NULL) {
		res = confdb_object_find_start(handle, parent_object_handle);
		if (res != CS_OK) {
			fprintf (stderr, "Could not start object_find %d\n", res);
			exit (EXIT_FAILURE);
		}

		res = confdb_object_find(handle, parent_object_handle,
			 obj_name_pt, strlen (obj_name_pt), &obj_handle);
		if (res != CS_OK) {

			if (validate_name(obj_name_pt) != CS_OK) {
				fprintf(stderr, "Incorrect object name \"%s\", \"=\" not allowed.\n",
						obj_name_pt);
				exit(EXIT_FAILURE);
			}

			if (debug)
				printf ("%s:%d: %s\n", __func__,__LINE__, obj_name_pt);
			res = confdb_object_create (handle,
										parent_object_handle,
										obj_name_pt,
										strlen (obj_name_pt),
										&obj_handle);
			if (res != CS_OK)
				fprintf(stderr, "Failed to create object \"%s\". Error %d.\n",
						obj_name_pt, res);
		}

		parent_object_handle = obj_handle;
		obj_name_pt = strtok_r (NULL, SEPERATOR_STR, &save_pt);
	}
}

static void create_object_key(confdb_handle_t handle, char *name_pt)
{
	char * obj_name_pt;
	char * new_obj_name_pt;
	char * save_pt;
	hdb_handle_t obj_handle;
	hdb_handle_t parent_object_handle = OBJECT_PARENT_HANDLE;
	char tmp_name[OBJ_NAME_SIZE];
	cs_error_t res;
	char parent_name[OBJ_NAME_SIZE];
	char key_name[OBJ_NAME_SIZE];
	char key_value[OBJ_NAME_SIZE];

	get_parent_name(name_pt, parent_name);
	get_key(name_pt, key_name, key_value);

	strncpy (tmp_name, parent_name, sizeof (tmp_name));
	tmp_name[sizeof (tmp_name) - 1] = '\0';
	obj_name_pt = strtok_r(tmp_name, SEPERATOR_STR, &save_pt);

	/*
	 * Create parent object tree
	 */
	while (obj_name_pt != NULL) {
		res = confdb_object_find_start(handle, parent_object_handle);
		if (res != CS_OK) {
			fprintf (stderr, "Could not start object_find %d\n", res);
			exit (EXIT_FAILURE);
		}

		new_obj_name_pt = strtok_r (NULL, SEPERATOR_STR, &save_pt);
		res = confdb_object_find(handle, parent_object_handle,
			 obj_name_pt, strlen (obj_name_pt), &obj_handle);
		if (res != CS_OK || new_obj_name_pt == NULL) {

			if (validate_name(obj_name_pt) != CS_OK) {
				fprintf(stderr, "Incorrect object name \"%s\", \"=\" not allowed.\n",
						obj_name_pt);
				exit(EXIT_FAILURE);
			}

			if (debug)
				printf ("%s:%d: %s\n", __func__,__LINE__, obj_name_pt);
			res = confdb_object_create (handle,
				parent_object_handle,
				obj_name_pt,
				strlen (obj_name_pt),
				&obj_handle);
			if (res != CS_OK) {
				fprintf(stderr, "Failed to create object \"%s\". Error %d.\n",
					obj_name_pt, res);
			}
		}
		parent_object_handle = obj_handle;
		obj_name_pt = new_obj_name_pt;
	}

	/*
	 * Create key
	 */
	res = confdb_key_create_typed (handle,
		obj_handle,
		key_name,
		key_value,
		strlen(key_value)+1,
		CONFDB_VALUETYPE_STRING);
	if (res != CS_OK) {
		fprintf(stderr,
			"Failed to create the key %s=%s. Error %d\n",
			key_name, key_value, res);
	}
}

/* Print "?" in place of any non-printable byte of OBJ. */
static void print_name (FILE *fp, const void *obj, size_t obj_len)
{
	const char *p = obj;
	size_t i;
	for (i = 0; i < obj_len; i++) {
		int c = *p++;
		if (!isprint (c)) {
			c = '?';
		}
		fputc (c, fp);
	}
}

static void tail_key_changed(confdb_handle_t handle,
	confdb_change_type_t change_type,
	hdb_handle_t parent_object_handle,
	hdb_handle_t object_handle,
	const void *object_name_pt,
	size_t  object_name_len,
	const void *key_name_pt,
	size_t key_name_len,
	const void *key_value_pt,
	size_t key_value_len)
{
	/* printf("key_changed> %.*s.%.*s=%.*s\n", */
	fputs("key_changed> ", stdout);
	print_name (stdout, object_name_pt, object_name_len);
	fputs(".", stdout);
	print_name (stdout, key_name_pt, key_name_len);
	fputs("=", stdout);
	print_name (stdout, key_value_pt, key_value_len);
	fputs("\n", stdout);
}

static void tail_object_created(confdb_handle_t handle,
	hdb_handle_t parent_object_handle,
	hdb_handle_t object_handle,
	const void *name_pt,
	size_t name_len)
{
	fputs("object_created>", stdout);
	print_name(stdout, name_pt, name_len);
	fputs("\n", stdout);
}

static void tail_object_deleted(confdb_handle_t handle,
	hdb_handle_t parent_object_handle,
	const void *name_pt,
	size_t name_len)
{
	fputs("object_deleted>", stdout);
	print_name(stdout, name_pt, name_len);
	fputs("\n", stdout);
}

static void listen_for_object_changes(confdb_handle_t handle)
{
	int result;
	fd_set read_fds;
	int select_fd;
	int quit = CS_FALSE;

	FD_ZERO (&read_fds);
	if (confdb_fd_get (handle, &select_fd) != CS_OK) {
		printf ("can't get the confdb selector object.\n");
		return;
	}
	printf ("Type \"q\" to finish\n");
	do {
		FD_SET (select_fd, &read_fds);
		FD_SET (STDIN_FILENO, &read_fds);
		result = select (select_fd + 1, &read_fds, 0, 0, 0);
		if (result == -1) {
			perror ("select\n");
		}
		if (FD_ISSET (STDIN_FILENO, &read_fds)) {
			char inbuf[3];

			if (fgets(inbuf, sizeof(inbuf), stdin) == NULL)
				quit = CS_TRUE;
			else if (strncmp(inbuf, "q", 1) == 0)
				quit = CS_TRUE;
		}
		if (FD_ISSET (select_fd, &read_fds)) {
			if (confdb_dispatch (handle, CONFDB_DISPATCH_ALL) != CS_OK)
				exit(1);
		}
	} while (result && quit == CS_FALSE);

	(void)confdb_stop_track_changes(handle);

}

static void track_object(confdb_handle_t handle, char * name_pt)
{
	cs_error_t res;
	hdb_handle_t obj_handle;

	res = find_object (handle, name_pt, FIND_OBJECT_ONLY, &obj_handle);

	if (res != CS_OK) {
		fprintf (stderr, "Could not find object \"%s\". Error %d\n",
				 name_pt, res);
		return;
	}

	res = confdb_track_changes (handle, obj_handle, CONFDB_TRACK_DEPTH_RECURSIVE);
	if (res != CS_OK) {
		fprintf (stderr, "Could not enable tracking on object \"%s\". Error %d\n",
				 name_pt, res);
		return;
	}
}

static void stop_tracking(confdb_handle_t handle)
{
	cs_error_t res;

	res = confdb_stop_track_changes (handle);
	if (res != CS_OK) {
		fprintf (stderr, "Could not stop tracking. Error %d\n", res);
		return;
	}
}

static void delete_object(confdb_handle_t handle, char * name_pt)
{
	cs_error_t res;
	hdb_handle_t obj_handle;
	res = find_object (handle, name_pt, FIND_OBJECT_ONLY, &obj_handle);

	if (res == CS_OK) {
		res = confdb_object_destroy (handle, obj_handle);

		if (res != CS_OK)
			fprintf(stderr, "Failed to find object \"%s\" to delete. Error %d\n", name_pt, res);
	} else {
		char parent_name[OBJ_NAME_SIZE];
		char key_name[OBJ_NAME_SIZE];
		char key_value[OBJ_NAME_SIZE];

		/* find the parent object */
		get_parent_name(name_pt, parent_name);
		get_key(name_pt, key_name, key_value);
		res = find_object (handle, parent_name, FIND_OBJECT_ONLY, &obj_handle);

		if (res != CS_OK) {
			fprintf(stderr, "Failed to find the key's parent object \"%s\". Error %d\n", parent_name, res);
			exit (EXIT_FAILURE);
		}

		res = confdb_key_delete (handle,
								 obj_handle,
								 key_name,
								 strlen(key_name),
								 key_value,
								 strlen(key_value));

		if (res != CS_OK)
			fprintf(stderr, "Failed to delete key \"%s=%s\" from object \"%s\". Error %d\n",
					key_name, key_value, parent_name, res);
	}
}


int main (int argc, char *argv[]) {
	confdb_handle_t handle;
	cs_error_t result;
	int c;

	action = ACTION_READ;

	for (;;){
		c = getopt (argc,argv,"habwncvdtp:");
		if (c==-1) {
			break;
		}
		switch (c) {
			case 'v':
				debug++;
				break;
			case 'h':
				return print_help();
				break;
			case 'a':
				action = ACTION_PRINT_ALL;
				break;
			case 'b':
				show_binary++;
				break;
			case 'p':
				return read_in_config_file (optarg);
				break;
			case 'c':
				action = ACTION_CREATE;
				break;
			case 'd':
				action = ACTION_DELETE;
				break;
			case 'w':
				action = ACTION_WRITE;
				break;
			case 'n':
				action = ACTION_CREATE_KEY;
				break;
			case 't':
				action = ACTION_TRACK;
				break;
			default :
				action = ACTION_READ;
				break;
		}
	}

	if (argc == 1) {
		action = ACTION_PRINT_DEFAULT;
		return print_all();
	} else if (action == ACTION_PRINT_ALL) {
		return print_all();
	} else if (optind >= argc) {
		fprintf(stderr, "Expected an object path after options\n");
		exit(EXIT_FAILURE);
	}

	result = confdb_initialize (&handle, &callbacks);
	if (result != CS_OK) {
		fprintf (stderr, "Failed to initialize the objdb API. Error %d\n", result);
		exit (EXIT_FAILURE);
	}
	while (optind < argc) {
		switch (action) {
			case ACTION_READ:
				read_object(handle, argv[optind++]);
				break;
			case ACTION_WRITE:
				write_key(handle, argv[optind++]);
				break;
			case ACTION_CREATE:
				create_object(handle, argv[optind++]);
				break;
			case ACTION_CREATE_KEY:
				create_object_key(handle, argv[optind++]);
				break;
			case ACTION_DELETE:
				delete_object(handle, argv[optind++]);
				break;
			case ACTION_TRACK:
				track_object(handle, argv[optind++]);
				break;
		}
	}

	if (action == ACTION_TRACK) {
		listen_for_object_changes(handle);
		stop_tracking(handle);
	}

	result = confdb_finalize (handle);
	if (result != CS_OK) {
		fprintf (stderr, "Error finalizing objdb API. Error %d\n", result);
		exit(EXIT_FAILURE);
	}

	return 0;
}
