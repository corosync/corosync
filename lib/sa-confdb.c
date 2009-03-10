/*
 * Copyright (c) 2008 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Christine Caulfield (ccaulfie@redhat.com)
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
/*
 * Provides stand-alone access to data in the corosync object database
 * when aisexec is not running.
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <errno.h>

#include <corosync/corotypes.h>
#include <corosync/coroipc.h>
#include <corosync/engine/objdb.h>
#include <corosync/engine/config.h>
#include <corosync/engine/logsys.h>
#include <corosync/lcr/lcr_comp.h>
#include <corosync/lcr/lcr_ifact.h>

#include "sa-confdb.h"

static struct objdb_iface_ver0 *objdb;

static int num_config_modules;

static struct config_iface_ver0 *config_modules[128];

void main_get_config_modules(struct config_iface_ver0 ***modules, int *num);
char *strstr_rs (const char *haystack, const char *needle);

static int load_objdb(void)
{
	hdb_handle_t objdb_handle;
	void *objdb_p;
	int res;

	/*
	 * Load the object database interface
	 */
	res = lcr_ifact_reference (
		&objdb_handle,
		"objdb",
		0,
		&objdb_p,
		(void *)0);
	if (res == -1) {
		return -1;
	}

	objdb = (struct objdb_iface_ver0 *)objdb_p;

	objdb->objdb_init ();
	return CS_OK;
}

static int load_config(void)
{
	char *config_iface;
	char *iface;
	int res;
	hdb_handle_t config_handle;
	hdb_handle_t config_version = 0;
	void *config_p;
	struct config_iface_ver0 *config;
	char *error_string;

	/* User's bootstrap config service */
	config_iface = getenv("COROSYNC_DEFAULT_CONFIG_IFACE");
	if (!config_iface) {
		config_iface = strdup("corosync_parser");
	}

	/* Make a copy so we can deface it with strtok */
	config_iface = strdup(config_iface);

	iface = strtok(config_iface, ":");
	while (iface)
	{
		res = lcr_ifact_reference (
			&config_handle,
			iface,
			config_version,
			&config_p,
			0);

		config = (struct config_iface_ver0 *)config_p;
		if (res == -1) {
			return -1;
		}

		res = config->config_readconfig(objdb, &error_string);
		if (res == -1) {
			return -1;
		}

		config_modules[num_config_modules++] = config;

		iface = strtok(NULL, ":");
	}
	if (config_iface)
		free(config_iface);

	return CS_OK;
}

/* Needed by objdb when it writes back the configuration */
void main_get_config_modules(struct config_iface_ver0 ***modules, int *num)
{
	*modules = config_modules;
	*num = num_config_modules;
}

/* Needed by some modules ... */
char *strstr_rs (const char *haystack, const char *needle)
{
	char *end_address;
	char *new_needle;

	new_needle = (char *)strdup (needle);
	new_needle[strlen (new_needle) - 1] = '\0';

	end_address = strstr (haystack, new_needle);
	if (end_address) {
		end_address += strlen (new_needle);
		end_address = strstr (end_address, needle + strlen (new_needle));
	}
	if (end_address) {
		end_address += 1; /* skip past { or = */
		do {
			if (*end_address == '\t' || *end_address == ' ') {
				end_address++;
			} else {
				break;
			}
		} while (*end_address != '\0');
	}

	free (new_needle);
	return (end_address);
}

int confdb_sa_init (void)
{
	int res;

	res = load_objdb();
	if (res != CS_OK)
		return res;

	res = load_config();

	return res;
}


int confdb_sa_object_create (
	hdb_handle_t parent_object_handle,
	const void *object_name,
	int object_name_len,
	hdb_handle_t *object_handle)
{
	return objdb->object_create(parent_object_handle,
				    object_handle,
				    object_name, object_name_len);
}

int confdb_sa_object_destroy (
	hdb_handle_t object_handle)
{
	return objdb->object_destroy(object_handle);
}

int confdb_sa_object_parent_get (
	hdb_handle_t object_handle,
	hdb_handle_t *parent_object_handle)
{
	return objdb->object_parent_get(object_handle, parent_object_handle);
}

int confdb_sa_key_create (
	hdb_handle_t parent_object_handle,
	const void *key_name,
	int key_name_len,
	const void *value,
	int value_len)
{
	return objdb->object_key_create(parent_object_handle,
					key_name, key_name_len,
					value, value_len);
}

int confdb_sa_key_delete (
	hdb_handle_t parent_object_handle,
	const void *key_name,
	int key_name_len,
	const void *value,
	int value_len)
{
	return objdb->object_key_delete(parent_object_handle,
					key_name, key_name_len);
}

int confdb_sa_key_get (
	hdb_handle_t parent_object_handle,
	const void *key_name,
	int key_name_len,
	void *value,
	int *value_len)
{
	int res;
	void *kvalue;

	res = objdb->object_key_get(parent_object_handle,
				    key_name, key_name_len,
				    &kvalue, value_len);
	if (!res) {
		memcpy(value, kvalue, *value_len);
	}
	return res;
}

int confdb_sa_key_increment (
	hdb_handle_t parent_object_handle,
	const void *key_name,
	int key_name_len,
	unsigned int *value)
{
	int res;

	res = objdb->object_key_increment(parent_object_handle,
					  key_name, key_name_len,
					  value);
	return res;
}

int confdb_sa_key_decrement (
	hdb_handle_t parent_object_handle,
	const void *key_name,
	int key_name_len,
	unsigned int *value)
{
	int res;

	res = objdb->object_key_decrement(parent_object_handle,
					  key_name, key_name_len,
					  value);
	return res;
}


int confdb_sa_key_replace (
	hdb_handle_t parent_object_handle,
	const void *key_name,
	int key_name_len,
	const void *old_value,
	int old_value_len,
	const void *new_value,
	int new_value_len)
{
	return objdb->object_key_replace(parent_object_handle,
					 key_name, key_name_len,
					 new_value, new_value_len);
}

int confdb_sa_write (
	char *error_text)
{
	char *errtext;
	int ret;

	ret = objdb->object_write_config(&errtext);
	if (!ret)
		strcpy(error_text, errtext);

	return ret;
}

int confdb_sa_reload (
	int flush,
	char *error_text)
{
	char *errtext;
	int ret;

	ret = objdb->object_reload_config(flush, &errtext);
	if (!ret)
		strcpy(error_text, errtext);

	return ret;
}

int confdb_sa_object_find (
	hdb_handle_t parent_object_handle,
	hdb_handle_t *find_handle,
	hdb_handle_t *object_handle,
	const void *object_name,
	int object_name_len)
{
	int res;

	if (!*find_handle) {
		objdb->object_find_create(parent_object_handle,
					  object_name, object_name_len,
					  find_handle);
	}

	res = objdb->object_find_next(*find_handle,
				      object_handle);
	return res;
}

int confdb_sa_object_iter (
	hdb_handle_t parent_object_handle,
	hdb_handle_t *find_handle,
	hdb_handle_t *object_handle,
	const void *object_name,
	int object_name_len,
	void *found_object_name,
	int *found_object_name_len)
{
	int res;

	if (!*find_handle) {
		objdb->object_find_create(parent_object_handle,
					  object_name, object_name_len,
					  find_handle);
	}

	res = objdb->object_find_next(*find_handle,
				      object_handle);
	/* Return object name if we were called as _iter */
	if (!res) {
		objdb->object_name_get(*object_handle,
				       found_object_name, found_object_name_len);
	}
	return res;
}

int confdb_sa_key_iter (
	hdb_handle_t parent_object_handle,
	hdb_handle_t start_pos,
	void *key_name,
	int *key_name_len,
	void *value,
	int *value_len)
{
	int res;
	void *kname, *kvalue;

	res = objdb->object_key_iter_from(parent_object_handle,
					  start_pos,
					  &kname, key_name_len,
					  &kvalue, value_len);

	if (!res) {
		memcpy(key_name, kname, *key_name_len);
		memcpy(value, kvalue, *value_len);
	}
	return res;
}

int confdb_sa_find_destroy(hdb_handle_t find_handle)
{
	return objdb->object_find_destroy(find_handle);
}
