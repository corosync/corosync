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
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/un.h>
#include <syslog.h>

#include <corosync/corotypes.h>
#include <corosync/confdb.h>
#include "common_test_agent.h"

#define INCDEC_VALUE 45

confdb_callbacks_t callbacks = {
	.confdb_key_change_notify_fn = NULL,
	.confdb_object_create_change_notify_fn = NULL,
	.confdb_object_delete_change_notify_fn = NULL
};

typedef enum {
	NTF_OBJECT_CREATED,
	NTF_OBJECT_DELETED,
	NTF_KEY_CREATED,
	NTF_KEY_REPLACED,
	NTF_KEY_DELETED,
	NTF_NONE,
} ntf_callback_type_t;

static ntf_callback_type_t callback_type;
static char ntf_object_name[256];
static size_t ntf_object_name_len;
static char ntf_key_name[256];
static size_t ntf_key_name_len;
static char ntf_key_value[256];
static size_t ntf_key_value_len;

static void ta_key_change_notify (
	confdb_handle_t handle,
	confdb_change_type_t change_type,
	hdb_handle_t parent_object_handle,
	hdb_handle_t object_handle,
	const void *object_name,
	size_t  object_name_len,
	const void *key_name,
	size_t key_name_len,
	const void *key_value,
	size_t key_value_len)
{
	switch (change_type) {
	case OBJECT_KEY_CREATED:
		callback_type = NTF_KEY_CREATED;
		break;
	case OBJECT_KEY_DELETED:
		callback_type = NTF_KEY_DELETED;
		break;
	case OBJECT_KEY_REPLACED:
		callback_type = NTF_KEY_REPLACED;
		break;
	default:
		assert (0);
		break;
	}
	ntf_object_name_len = object_name_len;
	memcpy (ntf_object_name, object_name, object_name_len);

	ntf_key_name_len = key_name_len;
	memcpy (ntf_key_name, key_name, key_name_len);

	ntf_key_value_len = key_value_len;
	memcpy (ntf_key_value, key_value, key_value_len);
}

static void ta_object_create_notify (
	confdb_handle_t handle,
	hdb_handle_t parent_object_handle,
	hdb_handle_t object_handle,
	const void *name_pt,
	size_t name_len)
{
	callback_type = NTF_OBJECT_CREATED;
	ntf_object_name_len = name_len;
	memcpy (ntf_object_name, name_pt, name_len);
}

static void ta_object_delete_notify (
	confdb_handle_t handle,
	hdb_handle_t parent_object_handle,
	const void *name_pt,
	size_t name_len)
{
	callback_type = NTF_OBJECT_DELETED;
	ntf_object_name_len = name_len;
	memcpy (ntf_object_name, name_pt, name_len);
}

confdb_callbacks_t valid_callbacks = {
	.confdb_key_change_notify_fn = ta_key_change_notify,
	.confdb_object_create_change_notify_fn = ta_object_create_notify,
	.confdb_object_delete_change_notify_fn = ta_object_delete_notify
};

static void set_get_test (int sock)
{
	confdb_handle_t handle;
	char response[100];
	int res;
	hdb_handle_t object_handle;
	confdb_value_types_t type;
	char key_value[256];
	char key2_value[256];
	size_t value_len;
	size_t value2_len;

	syslog (LOG_ERR, "%s START", __func__);

	snprintf (response, 100, "%s", OK_STR);

	res = confdb_initialize (&handle, &callbacks);
	if (res != CS_OK) {
		syslog (LOG_ERR, "Could not initialize confdb error %d", res);
		goto send_response;
	}
	/* Add a scratch object and put 2 keys into it */
	res = confdb_object_create (handle, OBJECT_PARENT_HANDLE,
		"testconfdb", strlen("testconfdb"), &object_handle);
	if (res != CS_OK) {
		syslog (LOG_ERR, "error creating 'testconfdb' object: %d", res);
		goto send_response;
	}

	res = confdb_key_create (handle, object_handle,
	       "testkey", strlen ("testkey"),
		"one", strlen ("one"));
	if (res != CS_OK) {
		syslog (LOG_ERR, "error creating 'testconfdb' key 1: %d", res);
		goto send_response;
	}

	res = confdb_key_replace (handle, object_handle,
		"testkey", strlen ("testkey"),
		"one", strlen ("one"),
		"newone", strlen ("newone"));
	if (res != CS_OK) {
		syslog (LOG_ERR, "error replace 'testconfdb' key 2: %d", res);
		goto send_response;
	}

	res = confdb_key_get_typed (handle, object_handle,
		"testkey", key_value, &value_len, &type);
	if (res != CS_OK) {
		syslog (LOG_ERR, "Could not get \"testkey\" key: %d", res);
		goto send_response;
	}
	if (strcmp (key_value, "newone") != 0) {
		syslog (LOG_ERR, "Key not set correctly");
		goto send_response;
	}
	if (type != CONFDB_VALUETYPE_ANY) {
		syslog (LOG_ERR, "Key type not set correctly");
		goto send_response;
	}
	res = confdb_key_get (handle, object_handle,
		"testkey", strlen ("testkey"), key2_value, &value2_len);
	if (res != CS_OK) {
		syslog (LOG_ERR, "Could not get \"testkey\" key: %d", res);
		goto send_response;
	}
	if (value2_len != value_len) {
		syslog (LOG_ERR, "value length from confdb_key_get:%u and confdb_key_get_typed:%u differ.",
			(uint32_t)value_len, (uint32_t)value2_len);
		goto send_response;
	}

	res = confdb_key_delete (handle, object_handle,
		"testkey", strlen ("testkey"), key2_value, value2_len);
	if (res != CS_OK) {
		syslog (LOG_ERR, "Could not get \"testkey\" key: %d", res);
		goto send_response;
	}

	/* Remove it.
	   Check that it doesn't exist when the full tree dump runs next */
	res = confdb_object_destroy(handle, object_handle);
	if (res != CS_OK) {
		syslog (LOG_ERR, "error destroying 'testconfdb' object: %d", res);
		goto send_response;
	}

	snprintf (response, 100, "%s", OK_STR);

send_response:
	syslog (LOG_ERR, "%s %s", __func__, response);
	send (sock, response, strlen (response) + 1, 0);
	confdb_finalize (handle);
}

static void increment_decrement_test (int sock)
{
	char response[100];
	int res;
	uint32_t incdec_value;
	hdb_handle_t object_handle;
	confdb_handle_t handle;
	confdb_handle_t par_handle;

	snprintf (response, 100, "%s", FAIL_STR);

	res = confdb_initialize (&handle, &callbacks);
	if (res != CS_OK) {
		syslog (LOG_ERR, "Could not initialize confdb error %d", res);
		goto send_response;
	}
	/* Add a scratch object and put 1 keys into it */
	res = confdb_object_create(handle, OBJECT_PARENT_HANDLE,
	       "testconfdb", strlen("testconfdb"), &object_handle);
	if (res != CS_OK) {
		syslog (LOG_ERR, "error creating 'testconfdb' object: %d", res);
		goto send_response;
	}

	res = confdb_object_parent_get (handle, object_handle, &par_handle);
	if (res != CS_OK) {
		syslog (LOG_ERR, "error getting parent of 'testconfdb' object: %d", res);
		goto send_response;
	}
	if (par_handle != OBJECT_PARENT_HANDLE) {
		syslog (LOG_ERR, "wrong parent handle");
		goto send_response;
	}


	incdec_value = INCDEC_VALUE;
	res = confdb_key_create_typed (handle, object_handle, "incdec",
		&incdec_value, sizeof(incdec_value), CONFDB_VALUETYPE_UINT32);
	if (res != CS_OK) {
		syslog (LOG_ERR, "error creating 'testconfdb' key 4: %d\n", res);
		goto send_response;
	}
	res = confdb_key_increment(handle, object_handle, "incdec", strlen("incdec"), &incdec_value);
	if (res != CS_OK) {
		syslog (LOG_ERR, "error incrementing 'testconfdb' key 4: %d\n", res);
		goto send_response;
	}
	if (incdec_value == INCDEC_VALUE + 1) {
		syslog (LOG_INFO, "incremented value = %d\n", incdec_value);
	}
	else {
		syslog (LOG_ERR, "ERROR: incremented value = %d (should be %d)\n", incdec_value, INCDEC_VALUE+1);
		goto send_response;
	}
	res = confdb_key_decrement(handle, object_handle, "incdec", strlen("incdec"), &incdec_value);
	if (res != CS_OK) {
		syslog (LOG_ERR, "error decrementing 'testconfdb' key 4: %d\n", res);
		goto send_response;
	}
	if (incdec_value == INCDEC_VALUE) {
		syslog (LOG_ERR, "decremented value = %d\n", incdec_value);
	}
	else {
		syslog (LOG_ERR, "ERROR: decremented value = %d (should be %d)\n", incdec_value, INCDEC_VALUE);
		goto send_response;
	}
	/* Remove it.
	   Check that it doesn't exist when the full tree dump runs next */
	res = confdb_object_destroy(handle, object_handle);
	if (res != CS_OK) {
		syslog (LOG_ERR, "error destroying 'testconfdb' object: %d\n", res);
		goto send_response;
	}

	snprintf (response, 100, "%s", OK_STR);

send_response:
	confdb_finalize (handle);
	send (sock, response, strlen (response) + 1, 0);
}


static void object_find_test (int sock)
{
	char response[100];
	confdb_handle_t handle;
	int result;
	hdb_handle_t totem_handle;
	char key_value[256];
	size_t value_len;

	snprintf (response, 100, "%s", FAIL_STR);

	result = confdb_initialize (&handle, &callbacks);
	if (result != CS_OK) {
		syslog (LOG_ERR, "Could not initialize confdb error %d\n", result);
		goto send_response;
	}

	/* Find "totem" and dump bits of it again, to test the direct APIs */
	result = confdb_object_find_start(handle, OBJECT_PARENT_HANDLE);
	if (result != CS_OK) {
		syslog (LOG_ERR, "Could not start object_find %d\n", result);
		goto send_response;
	}

	result = confdb_object_find(handle, OBJECT_PARENT_HANDLE, "totem", strlen("totem"), &totem_handle);
	if (result != CS_OK) {
		syslog (LOG_ERR, "Could not object_find \"totem\": %d\n", result);
		goto send_response;
	}

	result = confdb_key_get(handle, totem_handle, "version", strlen("version"), key_value, &value_len);
	if (result != CS_OK) {
		syslog (LOG_ERR, "Could not get \"version\" key: %d\n", result);
		goto send_response;
	}

	result = confdb_object_find_destroy (handle, OBJECT_PARENT_HANDLE);
	if (result != CS_OK) {
		syslog (LOG_ERR, "Could not destroy find object %d\n", result);
		goto send_response;
	}


	snprintf (response, 100, "%s", OK_STR);

send_response:
	confdb_finalize (handle);
	send (sock, response, strlen (response) + 1, 0);
}

static void notification_test (int sock)
{
	char response[100];
	confdb_handle_t handle;
	int res;
	hdb_handle_t object_handle;
	hdb_handle_t new_object_handle;
	uint16_t incdec_value;
	uint16_t incdec_value_new;
	uint32_t incdec_value_out;

	snprintf (response, 100, "%s", FAIL_STR);

	res = confdb_initialize (&handle, &valid_callbacks);
	if (res != CS_OK) {
		syslog (LOG_ERR, "Could not initialize confdb error %d\n", res);
		goto send_response;
	}

	/* Add a base scratch object (we don't want to track the parent object) */
	res = confdb_object_create(handle, OBJECT_PARENT_HANDLE,
	       "testconfdb", strlen("testconfdb"), &object_handle);
	if (res != CS_OK) {
		syslog (LOG_ERR, "error creating 'testconfdb' object: %d", res);
		goto send_response;
	}

	res = confdb_track_changes (handle, object_handle, 1 /*OBJECT_TRACK_DEPTH_RECURSIVE*/);
	if (res != CS_OK) {
		syslog (LOG_ERR, "can't track changes on object: %d", res);
		goto send_response;
	}

	/* Test 'object created' notification
	 */
	callback_type = NTF_NONE;

	res = confdb_object_create(handle, object_handle,
	       "duck", strlen("duck"), &new_object_handle);
	if (res != CS_OK) {
		syslog (LOG_ERR, "error creating 'duck' object: %d", res);
		goto send_response;
	}

	confdb_dispatch (handle, CS_DISPATCH_ALL);

	if (callback_type != NTF_OBJECT_CREATED) {
		syslog (LOG_ERR, "no notification received for the creation of 'duck'");
		goto send_response;
	}
	if (strcmp ("duck", ntf_object_name) != 0) {
		syslog (LOG_ERR, "expected notification for 'duck' but got %s", ntf_object_name);
		goto send_response;
	}

	/* Test 'key created' notification
	 */
	callback_type = NTF_NONE;

	incdec_value = INCDEC_VALUE;
	res = confdb_key_create_typed (handle, new_object_handle, "incdec",
		&incdec_value, sizeof(incdec_value), CONFDB_VALUETYPE_UINT16);
	if (res != CS_OK) {
		syslog (LOG_ERR, "error creating 'testconfdb' key 4: %d\n", res);
		goto send_response;
	}

	confdb_dispatch (handle, CS_DISPATCH_ALL);

	if (callback_type != NTF_KEY_CREATED) {
		syslog (LOG_ERR, "no notification received for the creation of key 'incdec'");
		goto send_response;
	}
	if (strcmp ("incdec", ntf_key_name) != 0) {
		syslog (LOG_ERR, "expected notification for 'incdec' but got %s", ntf_key_name);
		goto send_response;
	}

	/* Test 'key replaced' notification for key_replace()
	 */
	callback_type = NTF_NONE;
	incdec_value_new = 413;
	res = confdb_key_replace(handle, new_object_handle, "incdec", strlen("incdec"),
			&incdec_value, sizeof(incdec_value),
			&incdec_value_new, sizeof(incdec_value_new));
	if (res != CS_OK) {
		syslog (LOG_ERR, "error replacing 'incdec' key: %d\n", res);
		goto send_response;
	}

	confdb_dispatch (handle, CS_DISPATCH_ALL);

	if (callback_type != NTF_KEY_REPLACED) {
		syslog (LOG_ERR, "no notification received for the incrementing of key 'incdec'");
		goto send_response;
	}
	if (strcmp ("incdec", ntf_key_name) != 0) {
		syslog (LOG_ERR, "expected notification for 'incdec' but got %s", ntf_key_name);
		goto send_response;
	}
	/* Test NO 'key replaced' notification for key_replace() of the same
	 * value.
	 */
	callback_type = NTF_NONE;
	incdec_value = incdec_value_new;
	res = confdb_key_replace(handle, new_object_handle, "incdec", strlen("incdec"),
			&incdec_value_new, sizeof(incdec_value),
			&incdec_value, sizeof(incdec_value_new));
	if (res != CS_OK) {
		syslog (LOG_ERR, "error replacing 'incdec' key: %d\n", res);
		goto send_response;
	}

	confdb_dispatch (handle, CS_DISPATCH_ALL);

	if (callback_type != NTF_NONE) {
		syslog (LOG_ERR, "notification received for the replacing the same value of key 'incdec'");
		goto send_response;
	}
	if (strcmp ("incdec", ntf_key_name) != 0) {
		syslog (LOG_ERR, "expected notification for 'incdec' but got %s", ntf_key_name);
		goto send_response;
	}


	/* Test 'key replaced' notification for key_increment()
	 */
	callback_type = NTF_NONE;

	res = confdb_key_increment(handle, new_object_handle, "incdec", strlen("incdec"), &incdec_value_out);
	if (res != CS_OK) {
		syslog (LOG_ERR, "error incrementing 'testconfdb' key 4: %d\n", res);
		goto send_response;
	}

	confdb_dispatch (handle, CS_DISPATCH_ALL);

	if (callback_type != NTF_KEY_REPLACED) {
		syslog (LOG_ERR, "no notification received for the incrementing of key 'incdec'");
		goto send_response;
	}
	if (strcmp ("incdec", ntf_key_name) != 0) {
		syslog (LOG_ERR, "expected notification for 'incdec' but got %s", ntf_key_name);
		goto send_response;
	}

	/* Test 'key destroyed' notification
	 */
	callback_type = NTF_NONE;

	res = confdb_key_delete (handle, new_object_handle,
		"incdec", strlen ("incdec"), ntf_key_value, ntf_key_value_len);
	if (res != CS_OK) {
		syslog (LOG_ERR, "Could not delete \"incdec\" key: %d", res);
		goto send_response;
	}

	confdb_dispatch (handle, CS_DISPATCH_ALL);

	if (callback_type != NTF_KEY_DELETED) {
		syslog (LOG_ERR, "no notification received for the deletion of key 'incdec'");
		goto send_response;
	}
	if (strcmp ("incdec", ntf_key_name) != 0) {
		syslog (LOG_ERR, "expected notification for 'incdec' but got %s", ntf_key_name);
		goto send_response;
	}

	/* Test 'object destroyed' notification
	 */
	callback_type = NTF_NONE;

	res = confdb_object_destroy(handle, new_object_handle);
	if (res != CS_OK) {
		syslog (LOG_ERR, "error destroying 'testconfdb' object: %d", res);
		goto send_response;
	}

	confdb_dispatch (handle, CS_DISPATCH_ALL);

	if (callback_type != NTF_OBJECT_DELETED) {
		syslog (LOG_ERR, "no notification received for the deletion of 'duck'");
		goto send_response;
	}
	if (strcmp ("duck", ntf_object_name) != 0) {
		syslog (LOG_ERR, "expected notification for 'duck' but got %s", ntf_object_name);
		goto send_response;
	}
	confdb_stop_track_changes (handle);
	confdb_object_destroy(handle, object_handle);

	snprintf (response, 100, "%s", OK_STR);

send_response:
	send (sock, response, strlen (response) + 1, 0);
	confdb_finalize (handle);
}


static void context_test (int sock)
{
	confdb_handle_t handle;
	char response[100];
	char *cmp;
	int res;

	snprintf (response, 100, "%s", OK_STR);

	res = confdb_initialize (&handle, &valid_callbacks);
	if (res != CS_OK) {
		syslog (LOG_ERR, "Could not initialize confdb error %d\n", res);
		goto send_response;
	}

	confdb_context_set (handle, response);
	confdb_context_get (handle, (const void**)&cmp);
	if (response != cmp) {
		snprintf (response, 100, "%s", FAIL_STR);
	}

send_response:
	send (sock, response, strlen (response) + 1, 0);
	confdb_finalize (handle);
}

static void do_command (int sock, char* func, char*args[], int num_args)
{
	char response[100];

	if (parse_debug)
		syslog (LOG_DEBUG,"RPC:%s() called.", func);

	if (strcmp ("set_get_test", func) == 0) {
		set_get_test (sock);
	} else if (strcmp ("increment_decrement_test", func) == 0) {
		increment_decrement_test (sock);
	} else if (strcmp ("object_find_test", func) == 0) {
		object_find_test (sock);
	} else if (strcmp ("notification_test", func) == 0) {
		notification_test (sock);
	} else if (strcmp ("context_test", func) == 0) {
		context_test (sock);
	} else if (strcmp ("are_you_ok_dude", func) == 0) {
		snprintf (response, 100, "%s", OK_STR);
		send (sock, response, strlen (response) + 1, 0);
	} else {
		syslog (LOG_ERR,"%s RPC:%s not supported!", __func__, func);
		snprintf (response, 100, "%s", NOT_SUPPORTED_STR);
		send (sock, response, strlen (response) + 1, 0);
	}
}


int main (int argc, char *argv[])
{
	int ret;

	openlog (NULL, LOG_CONS|LOG_PID, LOG_DAEMON);
	syslog (LOG_ERR, "confdb_test_agent STARTING");

	parse_debug = 1;
	ret = test_agent_run (9035, do_command);
	syslog (LOG_ERR, "confdb_test_agent EXITING");

	return ret;
}


