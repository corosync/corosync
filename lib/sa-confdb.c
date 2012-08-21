/*
 * Copyright (c) 2008, 2012 Red Hat, Inc.
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

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>

#include <corosync/corotypes.h>
#include <corosync/coroipcc.h>
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
	const char *error_string;
	char *strtok_savept;

	/* User's bootstrap config service */
	config_iface = getenv("COROSYNC_DEFAULT_CONFIG_IFACE");
	if (!config_iface) {
		if ((config_iface = strdup("corosync_parser")) == NULL) {
			return -1;
		}
	}

	/* Make a copy so we can deface it with strtok */
	if ((config_iface = strdup(config_iface)) == NULL) {
		return -1;
	}

	iface = strtok_r (config_iface, ":", &strtok_savept);
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

		iface = strtok_r (NULL, ":", &strtok_savept);
	}
	free(config_iface);

	return CS_OK;
}

/* Needed by objdb when it writes back the configuration */
void main_get_config_modules(struct config_iface_ver0 ***modules, int *num)
{
	*modules = config_modules;
	*num = num_config_modules;
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
	size_t object_name_len,
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

int confdb_sa_object_name_get(
	hdb_handle_t object_handle,
	char *object_name,
	size_t *object_name_len)
{
	return objdb->object_name_get(object_handle, object_name, object_name_len);
}

int confdb_sa_key_create (
	hdb_handle_t parent_object_handle,
	const void *key_name,
	size_t key_name_len,
	const void *value,
	size_t value_len)
{
	return objdb->object_key_create(parent_object_handle,
					key_name, key_name_len,
					value, value_len);
}

int confdb_sa_key_create_typed (
	hdb_handle_t parent_object_handle,
	const char *key_name,
	const void *value,
	size_t value_len,
	int type)
{
	return objdb->object_key_create_typed(parent_object_handle,
					key_name,
					value, value_len, type);
}

int confdb_sa_key_delete (
	hdb_handle_t parent_object_handle,
	const void *key_name,
	size_t key_name_len,
	const void *value,
	size_t value_len)
{
	return objdb->object_key_delete(parent_object_handle,
					key_name, key_name_len);
}

int confdb_sa_key_get (
	hdb_handle_t parent_object_handle,
	const void *key_name,
	size_t key_name_len,
	void *value,
	size_t *value_len)
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

int confdb_sa_key_get_typed (
	hdb_handle_t parent_object_handle,
	const char *key_name,
	void **value,
	size_t *value_len,
	int *type)
{
	int res;
	void *kvalue;

	res = objdb->object_key_get_typed(parent_object_handle,
				    key_name,
				    &kvalue, value_len, (objdb_value_types_t*)type);
	if (!res) {
		if (!*value) {
			*value = malloc(*value_len + 1);
			if (!*value) {
				res = CS_ERR_NO_MEMORY;
			}
		}
		if (*value) {
			memcpy(*value, kvalue, *value_len);
		}
	}
	return res;
}

int confdb_sa_key_increment (
	hdb_handle_t parent_object_handle,
	const void *key_name,
	size_t key_name_len,
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
	size_t key_name_len,
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
	size_t key_name_len,
	const void *old_value,
	size_t old_value_len,
	const void *new_value,
	size_t new_value_len)
{
	return objdb->object_key_replace(parent_object_handle,
					 key_name, key_name_len,
					 new_value, new_value_len);
}

int confdb_sa_write (char *error_text, size_t errbuf_len)
{
	const char *errtext;
	int ret;

	ret = objdb->object_write_config(&errtext);
	if (!ret) {
		strncpy(error_text, errtext, errbuf_len);
		if (errbuf_len > 0)
			error_text[errbuf_len-1] = '\0';
	}

	return ret;
}

int confdb_sa_reload (
	int flush,
	char *error_text,
	size_t errbuf_len)
{
	char *errtext;
	int ret;

	ret = objdb->object_reload_config(flush, (const char **) &errtext);
	if (!ret) {
		strncpy(error_text, errtext, errbuf_len);
		if (errbuf_len > 0)
			error_text[errbuf_len-1] = '\0';
	}

	return ret;
}

int confdb_sa_object_find (
	hdb_handle_t parent_object_handle,
	hdb_handle_t *find_handle,
	hdb_handle_t *object_handle,
	const void *object_name,
	size_t object_name_len)
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
	size_t object_name_len,
	void *found_object_name,
	size_t *found_object_name_len)
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
	size_t *key_name_len,
	void *value,
	size_t *value_len)
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

int confdb_sa_key_iter_typed (
	hdb_handle_t parent_object_handle,
	hdb_handle_t start_pos,
	char *key_name,
	void **value,
	size_t *value_len,
	int *type)
{
	int res;
	void *kname;
	void *kvalue;
	size_t key_name_len;

	res = objdb->object_key_iter_from(parent_object_handle,
					  start_pos,
					  &kname, &key_name_len,
					  &kvalue, value_len);

	if (!res) {
		memcpy(key_name, kname, key_name_len);
		key_name[key_name_len] = '\0';
		if (!*value) {
			*value = malloc(*value_len + 1);
			if (!*value) {
				res = CS_ERR_NO_MEMORY;
			}
		}
		if (*value) {
			memcpy(*value, kvalue, *value_len);
			objdb->object_key_get_typed(parent_object_handle,
						    key_name,
						    &kvalue, value_len, (objdb_value_types_t*)type);
		}
	}
	return res;
}

int confdb_sa_find_destroy(hdb_handle_t find_handle)
{
	return objdb->object_find_destroy(find_handle);
}
