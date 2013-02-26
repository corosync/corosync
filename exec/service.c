/*
 * Copyright (c) 2006 MontaVista Software, Inc.
 * Copyright (c) 2006-2009 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)
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

#include <stdlib.h>
#include <string.h>

#include <corosync/lcr/lcr_ifact.h>
#include <corosync/swab.h>
#include <corosync/totem/totem.h>

#include <corosync/corotypes.h>
#include <corosync/coroipc_types.h>
#include "mainconfig.h"
#include "util.h"
#include <corosync/engine/logsys.h>

#include "timer.h"
#include <corosync/totem/totempg.h>
#include <corosync/totem/totemip.h>
#include "main.h"
#include <corosync/engine/coroapi.h>
#include "service.h"

#include <corosync/coroipcs.h>

LOGSYS_DECLARE_SUBSYS ("SERV");

struct default_service {
	const char *name;
	int ver;
};

static struct default_service default_services[] = {
	{
		.name			 = "corosync_evs",
		.ver			 = 0,
	},
	{
		.name			 = "corosync_cfg",
		.ver			 = 0,
	},
	{
		.name			 = "corosync_cpg",
		.ver			 = 0,
	},
	{
		.name			 = "corosync_confdb",
		.ver			 = 0,
	},
	{
		.name			 = "corosync_pload",
		.ver			 = 0,
	},
	{
		.name			 = "corosync_quorum",
		.ver			 = 0,
	}
};

/*
 * service exit and unlink schedwrk handler data structure
 */
struct seus_handler_data {
	hdb_handle_t service_handle;
	int service_engine;
	struct corosync_api_v1 *api;
};

struct corosync_service_engine *ais_service[SERVICE_HANDLER_MAXIMUM_COUNT];

hdb_handle_t service_stats_handle[SERVICE_HANDLER_MAXIMUM_COUNT][64];

int ais_service_exiting[SERVICE_HANDLER_MAXIMUM_COUNT];

static hdb_handle_t object_internal_configuration_handle;

static hdb_handle_t object_stats_services_handle;

static void (*service_unlink_all_complete) (void) = NULL;

static hdb_handle_t swrk_service_exit_handle;
static hdb_handle_t swrk_service_unlink_handle;

static unsigned int default_services_requested (struct corosync_api_v1 *corosync_api)
{
	hdb_handle_t object_service_handle;
	hdb_handle_t object_find_handle;
	char *value;

	/*
	 * Don't link default services if they have been disabled
	 */
	corosync_api->object_find_create (
		OBJECT_PARENT_HANDLE,
		"aisexec",
		strlen ("aisexec"),
		&object_find_handle);

	if (corosync_api->object_find_next (
		object_find_handle,
		&object_service_handle) == 0) {

		if ( ! corosync_api->object_key_get (object_service_handle,
			"defaultservices",
			strlen ("defaultservices"),
			(void *)&value,
			NULL)) {

			if (value && strcmp (value, "no") == 0) {
				return 0;
			}
		}
	}

	corosync_api->object_find_destroy (object_find_handle);

	return (-1);
}

unsigned int corosync_service_link_and_init (
	struct corosync_api_v1 *corosync_api,
	const char *service_name,
	unsigned int service_ver)
{
	struct corosync_service_engine_iface_ver0 *iface_ver0;
	void *iface_ver0_p;
	hdb_handle_t handle;
	struct corosync_service_engine *service;
	int res;
	hdb_handle_t object_service_handle;
	hdb_handle_t object_stats_handle;
	int fn;
	char object_name[32];
	char *name_sufix;
	uint64_t zero_64 = 0;

	/*
	 * reference the service interface
	 */
	iface_ver0_p = NULL;
	res = lcr_ifact_reference (
		&handle,
		service_name,
		service_ver,
		&iface_ver0_p,
		(void *)0);

	iface_ver0 = (struct corosync_service_engine_iface_ver0 *)iface_ver0_p;

	if (res == -1 || iface_ver0 == 0) {
		log_printf(LOGSYS_LEVEL_ERROR, "Service failed to load '%s'.\n", service_name);
		return (-1);
	}


	/*
	 * Initialize service
	 */
	service = iface_ver0->corosync_get_service_engine_ver0();

	ais_service[service->id] = service;
	if (service->config_init_fn) {
		res = service->config_init_fn (corosync_api);
	}

	if (service->exec_init_fn) {
		res = service->exec_init_fn (corosync_api);
	}

	/*
	 * Store service in object database
	 */
	corosync_api->object_create (object_internal_configuration_handle,
		&object_service_handle,
		"service",
		strlen ("service"));

	corosync_api->object_key_create_typed (object_service_handle,
		"name",
		service_name,
		strlen (service_name) + 1, OBJDB_VALUETYPE_STRING);

	corosync_api->object_key_create_typed (object_service_handle,
		"ver",
		&service_ver,
		sizeof (service_ver), OBJDB_VALUETYPE_UINT32);

	res = corosync_api->object_key_create_typed (object_service_handle,
		"handle",
		&handle,
		sizeof (handle), OBJDB_VALUETYPE_UINT64);

	corosync_api->object_key_create_typed (object_service_handle,
		"service_id",
		&service->id,
		sizeof (service->id), OBJDB_VALUETYPE_UINT16);

	name_sufix = strrchr (service_name, '_');
	if (name_sufix)
		name_sufix++;
	else
		name_sufix = (char*)service_name;

	corosync_api->object_create (object_stats_services_handle,
								 &object_stats_handle,
								 name_sufix, strlen (name_sufix));

	corosync_api->object_key_create_typed (object_stats_handle,
										 "service_id",
										 &service->id, sizeof (service->id),
										 OBJDB_VALUETYPE_INT16);

	for (fn = 0; fn < service->exec_engine_count; fn++) {

		snprintf (object_name, 32, "%d", fn);
		corosync_api->object_create (object_stats_handle,
									 &service_stats_handle[service->id][fn],
									 object_name, strlen (object_name));

		corosync_api->object_key_create_typed (service_stats_handle[service->id][fn],
											 "tx",
											 &zero_64, sizeof (zero_64),
											 OBJDB_VALUETYPE_UINT64);

		corosync_api->object_key_create_typed (service_stats_handle[service->id][fn],
											 "rx",
											 &zero_64, sizeof (zero_64),
											 OBJDB_VALUETYPE_UINT64);
	}

	log_printf (LOGSYS_LEVEL_NOTICE, "Service engine loaded: %s\n", service->name);
	return (res);
}

static int service_priority_max(void)
{
	int lpc = 0, max = 0;
	for(; lpc < SERVICE_HANDLER_MAXIMUM_COUNT; lpc++) {
		if(ais_service[lpc] != NULL && ais_service[lpc]->priority > max) {
			max = ais_service[lpc]->priority;
		}
	}
	return max;
}

/*
 * use the force
 */
static unsigned int
corosync_service_unlink_priority (
	struct corosync_api_v1 *corosync_api,
	int lowest_priority,
	int *current_priority,
	int *current_service_engine,
	hdb_handle_t *current_service_handle)
{
	unsigned short *service_id;
	hdb_handle_t object_service_handle;
	hdb_handle_t object_find_handle;
	hdb_handle_t *found_service_handle;

	for(; *current_priority >= lowest_priority; *current_priority = *current_priority - 1) {
		for(*current_service_engine = 0;
			*current_service_engine < SERVICE_HANDLER_MAXIMUM_COUNT;
			*current_service_engine = *current_service_engine + 1) {

			if(ais_service[*current_service_engine] == NULL ||
				ais_service[*current_service_engine]->priority != *current_priority) {
				continue;
			}

			/*
			 * find service object in object database by service id
			 * and unload it if possible.
			 *
			 * If the service engine's exec_exit_fn returns -1 indicating
			 * it was busy, this function returns -1 and can be called again
			 * at a later time (usually via the schedwrk api).
			 */
			corosync_api->object_find_create (
			    object_internal_configuration_handle,
			    "service", strlen ("service"), &object_find_handle);

			while (corosync_api->object_find_next (
				  object_find_handle, &object_service_handle) == 0) {

				int res = corosync_api->object_key_get (
					object_service_handle,
					"service_id", strlen ("service_id"),
					(void *)&service_id, NULL);

				if (res == 0 && *service_id ==
					 ais_service[*current_service_engine]->id) {

					if (ais_service[*service_id]->exec_exit_fn) {
						res = ais_service[*service_id]->exec_exit_fn ();
						if (res == -1) {
							corosync_api->object_find_destroy (object_find_handle);
							return (-1);
						}
					}

					res = corosync_api->object_key_get (
						object_service_handle,
						"handle", strlen ("handle"),
						(void *)&found_service_handle,
						NULL);

					*current_service_handle = *found_service_handle;

					ais_service_exiting[*current_service_engine] = 1;

					corosync_api->object_find_destroy (object_find_handle);

					/*
					 * Call should call this function again
					 */
					return (1);
				}
			}

			corosync_api->object_find_destroy (object_find_handle);
		}
	}
	/*
	 * We finish unlink of all services -> no need to call this function again
	 */
	return (0);
}

static unsigned int service_unlink_and_exit (
	struct corosync_api_v1 *corosync_api,
	const char *service_name,
	unsigned int service_ver)
{
	hdb_handle_t object_service_handle;
	char *found_service_name;
	unsigned short *service_id;
	unsigned int *found_service_ver;
	hdb_handle_t object_find_handle;
	hdb_handle_t *found_service_handle;
	char *name_sufix;
	int res;

	name_sufix = strrchr (service_name, '_');
	if (name_sufix)
		name_sufix++;
	else
		name_sufix = (char*)service_name;

	corosync_api->object_find_create (
		object_stats_services_handle,
		name_sufix, strlen (name_sufix),
		&object_find_handle);

	if (corosync_api->object_find_next (
			object_find_handle,
			&object_service_handle) == 0) {

		corosync_api->object_destroy (object_service_handle);

	}
	corosync_api->object_find_destroy (object_find_handle);


	corosync_api->object_find_create (
		object_internal_configuration_handle,
		"service",
		strlen ("service"),
		&object_find_handle);

	while (corosync_api->object_find_next (
		object_find_handle,
		&object_service_handle) == 0) {

		res = corosync_api->object_key_get (object_service_handle,
			"name",
			strlen ("name"),
			(void *)&found_service_name,
			NULL);

		if (res != 0 || strcmp (service_name, found_service_name) != 0) {
		    continue;
		}

		res = corosync_api->object_key_get (object_service_handle,
			"ver",
			strlen ("ver"),
			(void *)&found_service_ver,
			NULL);

		/*
		 * If service found and linked exit it
		 */
		if (res != 0 || service_ver != *found_service_ver) {
			continue;
		}

		res = corosync_api->object_key_get (
			object_service_handle,
			"service_id", strlen ("service_id"),
			(void *)&service_id, NULL);

		if(res == 0
			&& service_id != NULL
			&& *service_id < SERVICE_HANDLER_MAXIMUM_COUNT
			&& ais_service[*service_id] != NULL) {

			corosync_api->object_find_destroy (object_find_handle);

			if (ais_service[*service_id]->exec_exit_fn) {
				res = ais_service[*service_id]->exec_exit_fn ();
				if (res == -1) {
					return (-1);
				}
			}

			log_printf(LOGSYS_LEVEL_NOTICE,
				"Service engine unloaded: %s\n",
				   ais_service[*service_id]->name);

			ais_service[*service_id] = NULL;

			res = corosync_api->object_key_get (
				object_service_handle,
				"handle", strlen ("handle"),
				(void *)&found_service_handle,
				NULL);

			if (res == 0) {
				lcr_ifact_release (*found_service_handle);

				corosync_api->object_destroy (object_service_handle);
			}
		}
	}

	corosync_api->object_find_destroy (object_find_handle);

	return (0);
}

/*
 * Links default services into the executive
 */
unsigned int corosync_service_defaults_link_and_init (struct corosync_api_v1 *corosync_api)
{
	unsigned int i;

	hdb_handle_t object_service_handle;
	char *found_service_name;
	char *found_service_ver;
	unsigned int found_service_ver_atoi;
	hdb_handle_t object_find_handle;
	hdb_handle_t object_find2_handle;
	hdb_handle_t object_runtime_handle;
	int res;

	corosync_api->object_find_create (
		OBJECT_PARENT_HANDLE,
		"runtime",
		strlen ("runtime"),
		&object_find2_handle);

	if (corosync_api->object_find_next (
			object_find2_handle,
			&object_runtime_handle) == 0) {

		corosync_api->object_create (object_runtime_handle,
			&object_stats_services_handle,
			"services", strlen ("services"));
	}
	corosync_api->object_find_destroy (object_find2_handle);

	corosync_api->object_create (OBJECT_PARENT_HANDLE,
		&object_internal_configuration_handle,
		"internal_configuration",
		strlen ("internal_configuration"));

	corosync_api->object_find_create (
		OBJECT_PARENT_HANDLE,
		"service",
		strlen ("service"),
		&object_find_handle);

	while (corosync_api->object_find_next (
		object_find_handle,
		&object_service_handle) == 0) {

		res = corosync_api->object_key_get (object_service_handle,
			"name",
			strlen ("name"),
			(void *)&found_service_name,
			NULL);

		if (res != 0) {
			log_printf(LOGSYS_LEVEL_ERROR,
				"Service section defined in config file without name key\n");

			return (-1);
		}

		found_service_ver = NULL;

		res = corosync_api->object_key_get (object_service_handle,
			"ver",
			strlen ("ver"),
			(void *)&found_service_ver,
			NULL);

		found_service_ver_atoi = ((res == 0 && found_service_ver) ? atoi (found_service_ver) : 0);

		corosync_service_link_and_init (
			corosync_api,
			found_service_name,
			found_service_ver_atoi);
	}

	corosync_api->object_find_destroy (object_find_handle);

 	if (default_services_requested (corosync_api) == 0) {
 		return (0);
 	}

	for (i = 0;
		i < sizeof (default_services) / sizeof (struct default_service); i++) {

		corosync_service_link_and_init (
			corosync_api,
			default_services[i].name,
			default_services[i].ver);
	}

	return (0);
}

/*
 * Declaration of exit_schedwrk_handler, because of cycle
 * (service_exit_schedwrk_handler calls service_unlink_schedwrk_handler, and vice-versa)
 */
static int service_exit_schedwrk_handler (const void *data);

static int service_unlink_schedwrk_handler (const void *data) {
	struct seus_handler_data *cb_data = (struct seus_handler_data *)data;
	struct corosync_api_v1 *api = (struct corosync_api_v1 *)cb_data->api;

	/*
	 * Exit all ipc connections dependent on this service
	 */
	if (coroipcs_ipc_service_exit (cb_data->service_engine) == -1)
		return -1;

	log_printf(LOGSYS_LEVEL_NOTICE,
		"Service engine unloaded: %s\n",
		ais_service[cb_data->service_engine]->name);

	ais_service[cb_data->service_engine] = NULL;

	lcr_ifact_release (cb_data->service_handle);

	api->schedwrk_create (
		&swrk_service_exit_handle,
		&service_exit_schedwrk_handler,
		data);

	return 0;
}

static int service_exit_schedwrk_handler (const void *data) {
	int res;
	static int current_priority = 0;
	static int current_service_engine = 0;
	static int called = 0;
	struct seus_handler_data *cb_data = (struct seus_handler_data *)data;
	struct corosync_api_v1 *api = (struct corosync_api_v1 *)cb_data->api;
	hdb_handle_t service_handle;

	if (called == 0) {
		log_printf(LOGSYS_LEVEL_NOTICE,
			"Unloading all Corosync service engines.\n");
 		current_priority = service_priority_max ();
		called = 1;
	}

	res = corosync_service_unlink_priority (
		api,
		0,
		&current_priority,
		&current_service_engine,
		&service_handle);
	if (res == 0) {
		service_unlink_all_complete();
		return (res);
	}

	if (res == 1) {
		cb_data->service_engine = current_service_engine;
		cb_data->service_handle = service_handle;

		api->schedwrk_create_nolock (
			&swrk_service_unlink_handle,
			&service_unlink_schedwrk_handler,
			data);

		return (0);
	}

	return (res);
}
		
void corosync_service_unlink_all (
	struct corosync_api_v1 *api,
	void (*unlink_all_complete) (void))
{
	static int called = 0;
	static struct seus_handler_data cb_data;

	assert (api);

	service_unlink_all_complete = unlink_all_complete;

	if (called) {
		return;
	}
	if (called == 0) {
		called = 1;
	}

	cb_data.api = api;

	api->schedwrk_create (
		&swrk_service_exit_handle,
		&service_exit_schedwrk_handler,
		&cb_data);
}

struct service_unlink_and_exit_data {
	hdb_handle_t handle;
	struct corosync_api_v1 *api;
	const char *name;
	unsigned int ver;
};

static int service_unlink_and_exit_schedwrk_handler (void *data)
{
	struct service_unlink_and_exit_data *service_unlink_and_exit_data =
		data;
	int res;

	res = service_unlink_and_exit (
		service_unlink_and_exit_data->api,
		service_unlink_and_exit_data->name,
		service_unlink_and_exit_data->ver);

	if (res == 0) {
		free (service_unlink_and_exit_data);
	}
	return (res);
}

typedef int (*schedwrk_cast) (const void *);

unsigned int corosync_service_unlink_and_exit (
        struct corosync_api_v1 *api,
        const char *service_name,
        unsigned int service_ver)
{
	struct service_unlink_and_exit_data *service_unlink_and_exit_data;

	assert (api);
	service_unlink_and_exit_data = malloc (sizeof (struct service_unlink_and_exit_data));
	service_unlink_and_exit_data->api = api;
	service_unlink_and_exit_data->name = strdup (service_name);
	service_unlink_and_exit_data->ver = service_ver;
	
	api->schedwrk_create (
		&service_unlink_and_exit_data->handle,
		(schedwrk_cast)service_unlink_and_exit_schedwrk_handler,
		service_unlink_and_exit_data);
	return (0);
}
