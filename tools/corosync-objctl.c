/*
 * Copyright (c) 2008 Allied Telesis Labs NZ
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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/un.h>
#include "saAis.h"
#include "confdb.h"

#define SEPERATOR '.'
#define SEPERATOR_STR "."
#define OBJ_NAME_SIZE 512

typedef enum {
	ACTION_READ,
	ACTION_WRITE,
	ACTION_CREATE,
	ACTION_DELETE,
	ACTION_PRINT_ALL,
	ACTION_PRINT_DEFAULT,
} action_types_t;

typedef enum {
	FIND_OBJECT_ONLY,
	FIND_OBJECT_OR_KEY,
	FIND_KEY_ONLY
} find_object_of_type_t;

confdb_callbacks_t callbacks = {
	.confdb_change_notify_fn = NULL,
};

static int action;

/* Recursively dump the object tree */
static void print_config_tree(confdb_handle_t handle, unsigned int parent_object_handle, char * parent_name)
{
	unsigned int object_handle;
	char object_name[OBJ_NAME_SIZE];
	int object_name_len;
	char key_name[OBJ_NAME_SIZE];
	int key_name_len;
	char key_value[OBJ_NAME_SIZE];
	int key_value_len;
	confdb_error_t res;
	int children_printed;

	/* Show the keys */
	res = confdb_key_iter_start(handle, parent_object_handle);
	if (res != CONFDB_OK) {
		fprintf(stderr, "error resetting key iterator for object %d: %d\n", parent_object_handle, res);
		exit(EXIT_FAILURE);
	}
	children_printed = 0;

	while ( (res = confdb_key_iter(handle,
								   parent_object_handle,
								   key_name,
								   &key_name_len,
								   key_value,
								   &key_value_len)) == CONFDB_OK) {
		key_name[key_name_len] = '\0';
		key_value[key_value_len] = '\0';
		if (parent_name != NULL)
			printf("%s%c%s=%s\n", parent_name, SEPERATOR,key_name, key_value);
		else
			printf("%s=%s\n", key_name, key_value);

		children_printed++;
	}

	/* Show sub-objects */
	res = confdb_object_iter_start(handle, parent_object_handle);
	if (res != CONFDB_OK) {
		fprintf(stderr, "error resetting object iterator for object %d: %d\n", parent_object_handle, res);
		exit(EXIT_FAILURE);
	}

	while ( (res = confdb_object_iter(handle,
									  parent_object_handle,
									  &object_handle,
									  object_name,
									  &object_name_len)) == CONFDB_OK)	{

		object_name[object_name_len] = '\0';
		if (parent_name != NULL) {
			snprintf(key_value, OBJ_NAME_SIZE, "%s%c%s", parent_name, SEPERATOR, object_name);
		} else {
			if ((action == ACTION_PRINT_DEFAULT) && strcmp(object_name, "internal_configuration") == 0) continue;
			snprintf(key_value, OBJ_NAME_SIZE, "%s", object_name);
		}
		print_config_tree(handle, object_handle, key_value);
		children_printed++;
	}
	if (children_printed == 0 && parent_name != NULL) {
			printf("%s\n", parent_name);
	}
}

static int print_all(void)
{
	confdb_handle_t handle;
	int result;

	result = confdb_initialize (&handle, &callbacks);
	if (result != CONFDB_OK) {
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
	printf ("usage:  corosync-objctl object%ckey ...\n", SEPERATOR);
	printf ("        corosync-objctl -c object%cchild_obj ...\n", SEPERATOR);
	printf ("        corosync-objctl -d object%cchild_obj ...\n", SEPERATOR);
	printf ("        corosync-objctl -w object%cchild_obj.key=value ...\n", SEPERATOR);
	printf ("        corosync-objctl -a (print all objects)\n");
	printf("\n");
	return 0;
}

static confdb_error_t validate_name(char * obj_name_pt)
{
	if ((strchr (obj_name_pt, SEPERATOR) == NULL) &&
		(strchr (obj_name_pt, '=') == NULL))
		return CONFDB_OK;
	else
		return CONFDB_ERR_INVALID_PARAM;
}

void get_child_name(char * name_pt, char * child_name)
{
	char * tmp;
	char str_copy[OBJ_NAME_SIZE];

	strcpy(str_copy, name_pt);

	/* first remove the value (it could be a file path */
	tmp = strchr(str_copy, '=');
	if (tmp != NULL) *tmp = '\0';

	/* truncate the  */
	tmp = strrchr(str_copy, SEPERATOR);
	if (tmp == NULL) tmp = str_copy;
	strcpy(child_name, tmp+1);
}

void get_parent_name(char * name_pt, char * parent_name)
{
	char * tmp;
	strcpy(parent_name, name_pt);

	/* first remove the value (it could be a file path */
	tmp = strchr(parent_name, '=');
	if (tmp != NULL) *tmp = '\0';

	/* then truncate the child name */
	tmp = strrchr(parent_name, SEPERATOR);
	if (tmp != NULL) *tmp = '\0';
}

void get_key(char * name_pt, char * key_name, char * key_value)
{
	char * tmp;
	char str_copy[OBJ_NAME_SIZE];

	strcpy(str_copy, name_pt);

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

static confdb_error_t find_object (confdb_handle_t handle,
			char * name_pt,
			find_object_of_type_t type,
			uint32_t * out_handle)
{
	char * obj_name_pt;
	char * save_pt;
	uint32_t obj_handle;
	confdb_handle_t parent_object_handle = OBJECT_PARENT_HANDLE;
	char tmp_name[OBJ_NAME_SIZE];
	confdb_error_t res;

	strncpy (tmp_name, name_pt, OBJ_NAME_SIZE);
	obj_name_pt = strtok_r(tmp_name, SEPERATOR_STR, &save_pt);

	while (obj_name_pt != NULL) {
		res = confdb_object_find_start(handle, parent_object_handle);
		if (res != CONFDB_OK) {
			fprintf (stderr, "Could not start object_find %d\n", res);
			exit (EXIT_FAILURE);
		}

		res = confdb_object_find(handle, parent_object_handle,
				obj_name_pt, strlen (obj_name_pt), &obj_handle);
		if (res != CONFDB_OK) {
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
	uint32_t obj_handle;
	confdb_error_t res;

	get_parent_name(name_pt, parent_name);
	res = find_object (handle, name_pt, FIND_OBJECT_OR_KEY, &obj_handle);
	if (res == CONFDB_OK) {
		print_config_tree(handle, obj_handle, parent_name);
	}
}

static void write_key(confdb_handle_t handle, char * path_pt)
{
	uint32_t obj_handle;
	char parent_name[OBJ_NAME_SIZE];
	char key_name[OBJ_NAME_SIZE];
	char key_value[OBJ_NAME_SIZE];
	char old_key_value[OBJ_NAME_SIZE];
	int old_key_value_len;
	confdb_error_t res;

	/* find the parent object */
	get_parent_name(path_pt, parent_name);
	get_key(path_pt, key_name, key_value);

	if (validate_name(key_name) != CONFDB_OK) {
		fprintf(stderr, "Incorrect key name, can not have \"=\" or \"%c\"\n", SEPERATOR);
		exit(EXIT_FAILURE);
	}
	res = find_object (handle, parent_name, FIND_OBJECT_ONLY, &obj_handle);

	if (res != CONFDB_OK) {
		fprintf(stderr, "Can't find parent object of \"%s\"\n", path_pt);
		exit(EXIT_FAILURE);
	}

	/* get the current key */
	res = confdb_key_get (handle,
						  obj_handle,
						  key_name,
						  strlen(key_name),
						  old_key_value,
						  &old_key_value_len);

	if (res == CONFDB_OK) {
		/* replace the current value */
		res = confdb_key_replace (handle,
								  obj_handle,
								  key_name,
								  strlen(key_name),
								  old_key_value,
								  old_key_value_len,
								  key_value,
								  strlen(key_value));

		if (res != CONFDB_OK)
			fprintf(stderr, "Failed to replace the key %s=%s. Error %d\n", key_name, key_value, res);
	} else {
		/* not there, create a new key */
		res = confdb_key_create (handle,
								 obj_handle,
								 key_name,
								 strlen(key_name),
								 key_value,
								 strlen(key_value));
		if (res != CONFDB_OK)
			fprintf(stderr, "Failed to create the key %s=%s. Error %d\n", key_name, key_value, res);
	}

}

static void create_object(confdb_handle_t handle, char * name_pt)
{
	char * obj_name_pt;
	char * save_pt;
	uint32_t obj_handle;
	uint32_t parent_object_handle = OBJECT_PARENT_HANDLE;
	char tmp_name[OBJ_NAME_SIZE];
	confdb_error_t res;

	strncpy (tmp_name, name_pt, OBJ_NAME_SIZE);
	obj_name_pt = strtok_r(tmp_name, SEPERATOR_STR, &save_pt);

	while (obj_name_pt != NULL) {
		res = confdb_object_find_start(handle, parent_object_handle);
		if (res != CONFDB_OK) {
			fprintf (stderr, "Could not start object_find %d\n", res);
			exit (EXIT_FAILURE);
		}

		res = confdb_object_find(handle, parent_object_handle,
								 obj_name_pt, strlen (obj_name_pt), &obj_handle);
		if (res != CONFDB_OK) {

			if (validate_name(obj_name_pt) != CONFDB_OK) {
				fprintf(stderr, "Incorrect object name \"%s\", \"=\" not allowed.\n",
						obj_name_pt);
				exit(EXIT_FAILURE);
			}
			res = confdb_object_create (handle,
										parent_object_handle,
										obj_name_pt,
										strlen (obj_name_pt),
										&obj_handle);
			if (res != CONFDB_OK)
				fprintf(stderr, "Failed to create object \"%s\". Error %d.\n",
						obj_name_pt, res);
		}

		parent_object_handle = obj_handle;
		obj_name_pt = strtok_r (NULL, SEPERATOR_STR, &save_pt);
	}
}

static void delete_object(confdb_handle_t handle, char * name_pt)
{
	confdb_error_t res;
	uint32_t obj_handle;
	res = find_object (handle, name_pt, FIND_OBJECT_ONLY, &obj_handle);

	if (res == CONFDB_OK) {
		res = confdb_object_destroy (handle, obj_handle);

		if (res != CONFDB_OK)
			fprintf(stderr, "Failed to find object \"%s\" to delete. Error %d\n", name_pt, res);
	} else {
		char parent_name[OBJ_NAME_SIZE];
		char key_name[OBJ_NAME_SIZE];
		char key_value[OBJ_NAME_SIZE];

		/* find the parent object */
		get_parent_name(name_pt, parent_name);
		get_key(name_pt, key_name, key_value);
		res = find_object (handle, parent_name, FIND_OBJECT_ONLY, &obj_handle);

		if (res != CONFDB_OK) {
			fprintf(stderr, "Failed to find the key's parent object \"%s\". Error %d\n", parent_name, res);
			exit (EXIT_FAILURE);
		}

		res = confdb_key_delete (handle,
								 obj_handle,
								 key_name,
								 strlen(key_name),
								 key_value,
								 strlen(key_value));

		if (res != CONFDB_OK)
			fprintf(stderr, "Failed to delete key \"%s=%s\" from object \"%s\". Error %d\n",
					key_name, key_value, parent_name, res);
	}
}


int main (int argc, char *argv[]) {
	confdb_handle_t handle;
	confdb_error_t result;
	char c;

	action = ACTION_READ;

	for (;;){
		c = getopt (argc,argv,"hawcdp:");
		if (c==-1) {
			break;
		}
		switch (c) {
			case 'h':
				return print_help();
				break;
			case 'a':
				action = ACTION_PRINT_ALL;
				break;
			case 'p':
				printf("%s:%d NOT Implemented yet.\n", __FUNCTION__, __LINE__);
				return -1;
				//return read_in_config_file();
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
	if (result != CONFDB_OK) {
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
			case ACTION_DELETE:
				delete_object(handle, argv[optind++]);
				break;
		}
	}

	result = confdb_finalize (handle);
	if (result != CONFDB_OK) {
		fprintf (stderr, "Error finalizing objdb API. Error %d\n", result);
		exit(EXIT_FAILURE);
	}

	return 0;
}

