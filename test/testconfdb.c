/*
 * Copyright (c) 2008, 2009 Red Hat Inc
 *
 * All rights reserved.
 *
 * Author: Christine Caulfield <ccaulfie@redhat.com>
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
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/un.h>

#include <corosync/corotypes.h>
#include <corosync/confdb.h>

#define INCDEC_VALUE 45

/* Callbacks are not supported yet */
confdb_callbacks_t callbacks = {
	.confdb_key_change_notify_fn = NULL,
	.confdb_object_create_change_notify_fn = NULL,
	.confdb_object_delete_change_notify_fn = NULL
};

/* Recursively dump the object tree */
static void print_config_tree(confdb_handle_t handle, hdb_handle_t parent_object_handle, int depth)
{
	hdb_handle_t object_handle;
	char object_name[1024];
	size_t object_name_len;
	char key_name[1024];
	size_t key_name_len;
	char key_value[1024];
	size_t key_value_len;
	int res;
	int i;

	/* Show the keys */
	res = confdb_key_iter_start(handle, parent_object_handle);
	if (res != CS_OK) {
		printf( "error resetting key iterator for object "HDB_X_FORMAT": %d\n", parent_object_handle, res);
		return;
	}

	while ( (res = confdb_key_iter(handle, parent_object_handle, key_name, &key_name_len,
				       key_value, &key_value_len)) == CS_OK) {
		key_name[key_name_len] = '\0';
		key_value[key_value_len] = '\0';
		for (i=0; i<depth; i++)	printf("  ");
		printf("  KEY %s=%s\n", key_name, key_value);
	}

	/* Show sub-objects */
	res = confdb_object_iter_start(handle, parent_object_handle);
	if (res != CS_OK) {
		printf( "error resetting object iterator for object "HDB_X_FORMAT": %d\n", parent_object_handle, res);
		return;
	}

	while ( (res = confdb_object_iter(handle, parent_object_handle, &object_handle, object_name, &object_name_len)) == CS_OK)	{
		hdb_handle_t parent;

		res = confdb_object_parent_get(handle, object_handle, &parent);
		if (res != CS_OK) {
			printf( "error getting parent for object "HDB_X_FORMAT": %d\n", object_handle, res);
			return;
		}

		for (i=0; i<depth; i++)	printf("  ");

		object_name[object_name_len] = '\0';
		printf("OBJECT: %s ("HDB_X_FORMAT", parent: "HDB_X_FORMAT")\n", object_name, object_handle, parent);

		/* Down we go ... */
		print_config_tree(handle, object_handle, depth+1);
	}
}

static void do_write_tests(confdb_handle_t handle)
{
	int res;
	unsigned int incdec_value;
	hdb_handle_t object_handle;
	char error_string[1024];

	/* Add a scratch object and put some keys into it */
	res = confdb_object_create(handle, OBJECT_PARENT_HANDLE, "testconfdb", strlen("testconfdb"), &object_handle);
	if (res != CS_OK) {
		printf( "error creating 'testconfdb' object: %d\n", res);
		return;
	}

	res = confdb_key_create(handle, object_handle, "testkey", strlen("testkey"), "one", strlen("one"));
	if (res != CS_OK) {
		printf( "error creating 'testconfdb' key 1: %d\n", res);
		return;
	}

	res = confdb_key_create(handle, object_handle, "testkey", strlen("testkey"), "two", strlen("two"));
	if (res != CS_OK) {
		printf( "error creating 'testconfdb' key 2: %d\n", res);
		return;
	}

	res = confdb_key_create(handle, object_handle, "grot", strlen("grot"), "perrins", strlen("perrins"));
	if (res != CS_OK) {
		printf( "error creating 'testconfdb' key 3: %d\n", res);
		return;
	}

	res = confdb_key_replace(handle, object_handle, "testkey", strlen("testkey"), "two", strlen("two"),
				 "newtwo", strlen("newtwo"));

	if (res != CS_OK) {
		printf( "error replace 'testconfdb' key 2: %d\n", res);
		return;
	}

	/* Print it for verification */
	print_config_tree(handle, object_handle, 0);

	incdec_value = INCDEC_VALUE;
	res = confdb_key_create(handle, object_handle, "incdec", strlen("incdec"), &incdec_value, sizeof(incdec_value));
	if (res != CS_OK) {
		printf( "error creating 'testconfdb' key 4: %d\n", res);
		return;
	}
	res = confdb_key_increment(handle, object_handle, "incdec", strlen("incdec"), &incdec_value);
	if (res != CS_OK) {
		printf( "error incrementing 'testconfdb' key 4: %d\n", res);
		return;
	}
	if (incdec_value == INCDEC_VALUE+1)
		printf("incremented value = %d\n", incdec_value);
	else
		printf("ERROR: incremented value = %d (should be %d)\n", incdec_value, INCDEC_VALUE+1);

	res = confdb_key_decrement(handle, object_handle, "incdec", strlen("incdec"), &incdec_value);
	if (res != CS_OK) {
		printf( "error decrementing 'testconfdb' key 4: %d\n", res);
		return;
	}
	if (incdec_value == INCDEC_VALUE)
		printf("decremented value = %d\n", incdec_value);
	else
		printf("ERROR: decremented value = %d (should be %d)\n", incdec_value, INCDEC_VALUE);

	printf("-------------------------\n");

	/* Remove it.
	   Check that it doesn't exist when the full tree dump runs next */
	res = confdb_object_destroy(handle, object_handle);
	if (res != CS_OK) {
		printf( "error destroying 'testconfdb' object: %d\n", res);
		return;
	}

	res = confdb_write(handle, error_string, sizeof error_string);
	printf("confdb_write returned %d: %s\n", res, error_string);
}


int main (int argc, char *argv[]) {
	confdb_handle_t handle;
	int result;
	hdb_handle_t totem_handle;
	char key_value[256];
	size_t value_len;

	result = confdb_initialize (&handle, &callbacks);
	if (result != CS_OK) {
		printf ("Could not initialize Cluster Configuration Database API instance error %d\n", result);
		exit (1);
	}

	if (argv[1] && strcmp(argv[1], "write")==0)
		do_write_tests(handle);

	if (argv[1] && strcmp(argv[1], "reload")==0) {
		/* Test reload interface */
		result = confdb_reload(handle, 0, key_value, sizeof key_value);
		printf ("Try to reload the config (noflush): %d (should be 1)\n", result);

		result = confdb_reload(handle, 1, key_value, sizeof key_value);
		printf ("Try to reload the config (flush): %d (should be 1)\n", result);
	}

	/* Test iterators */
	print_config_tree(handle, OBJECT_PARENT_HANDLE, 0);

	/* Find "totem" and dump bits of it again, to test the direct APIs */
	result = confdb_object_find_start(handle, OBJECT_PARENT_HANDLE);
	if (result != CS_OK) {
		printf ("Could not start object_find %d\n", result);
		exit (1);
	}

	result = confdb_object_find(handle, OBJECT_PARENT_HANDLE, "totem", strlen("totem"), &totem_handle);
	if (result != CS_OK) {
		printf ("Could not object_find \"totem\": %d\n", result);
		exit (1);
	}

	result = confdb_key_get(handle, totem_handle, "version", strlen("version"), key_value, &value_len);
	if (result != CS_OK) {
		printf ("Could not get \"version\" key: %d\n", result);
		exit (1);
	}
	key_value[value_len] = '\0';
	printf("totem/version = '%s'\n", key_value);

	result = confdb_key_get(handle, totem_handle, "secauth", strlen("secauth"), key_value, &value_len);
	if (result != CS_OK) {
		printf ("Could not get \"secauth\" key: %d\n", result);
		exit (1);
	}
	key_value[value_len] = '\0';
	printf("totem/secauth = '%s'\n", key_value);

	/* Try a call that fails */
	result = confdb_key_get(handle, totem_handle, "grot", strlen("grot"), key_value, &value_len);
	printf ("Get \"grot\" key returned: %d (should fail)\n", result);

	result = confdb_finalize (handle);
	printf ("Finalize  result is %d (should be 1)\n", result);
	return (0);
}
