/*
 * Copyright (c) 2006 MontaVista Software, Inc.
 * Copyright (c) 2006-2012 Red Hat, Inc.
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

#include <corosync/swab.h>
#include <corosync/totem/totem.h>

#include <corosync/corotypes.h>
#include "util.h"
#include <corosync/logsys.h>
#include <corosync/icmap.h>

#include "timer.h"
#include <corosync/totem/totempg.h>
#include <corosync/totem/totemip.h>
#include "main.h"
#include "service.h"

#include <qb/qbipcs.h>
#include <qb/qbloop.h>

LOGSYS_DECLARE_SUBSYS ("SERV");

static struct default_service default_services[] = {
	{
		.name		= "corosync_cmap",
		.ver		= 0,
		.loader		= cmap_get_service_engine_ver0
	},
	{
		.name		= "corosync_cfg",
		.ver		= 0,
		.loader		= cfg_get_service_engine_ver0
	},
	{
		.name		= "corosync_cpg",
		.ver		= 0,
		.loader		= cpg_get_service_engine_ver0
	},
	{
		.name		= "corosync_pload",
		.ver		= 0,
		.loader		= pload_get_service_engine_ver0
	},
#ifdef HAVE_MONITORING
	{
		.name		= "corosync_mon",
		.ver		= 0,
		.loader		= mon_get_service_engine_ver0
	},
#endif
#ifdef HAVE_WATCHDOG
	{
		.name		= "corosync_wd",
		.ver		= 0,
		.loader		= wd_get_service_engine_ver0
	},
#endif
	{
		.name		= "corosync_quorum",
		.ver		= 0,
		.loader		= vsf_quorum_get_service_engine_ver0
	},
};

/*
 * service exit and unlink schedwrk handler data structure
 */
struct seus_handler_data {
	int service_engine;
	struct corosync_api_v1 *api;
};

struct corosync_service_engine *corosync_service[SERVICES_COUNT_MAX];

const char *service_stats_rx[SERVICES_COUNT_MAX][SERVICE_HANDLER_MAXIMUM_COUNT];
const char *service_stats_tx[SERVICES_COUNT_MAX][SERVICE_HANDLER_MAXIMUM_COUNT];

static void (*service_unlink_all_complete) (void) = NULL;

char *corosync_service_link_and_init (
	struct corosync_api_v1 *corosync_api,
	struct default_service *service)
{
	struct corosync_service_engine *service_engine;
	int fn;
	char *name_sufix;
	char key_name[ICMAP_KEYNAME_MAXLEN];
	char *init_result;

	/*
	 * Initialize service
	 */
	service_engine = service->loader();

	corosync_service[service_engine->id] = service_engine;

	if (service_engine->config_init_fn) {
		service_engine->config_init_fn (corosync_api);
	}

	if (service_engine->exec_init_fn) {
		init_result = service_engine->exec_init_fn (corosync_api);
		if (init_result) {
			return (init_result);
		}
	}

	/*
	 * Store service in cmap db
	 */
	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "internal_configuration.service.%u.name", service_engine->id);
	icmap_set_string(key_name, service->name);

	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "internal_configuration.service.%u.ver", service_engine->id);
	icmap_set_uint32(key_name, service->ver);

	name_sufix = strrchr (service->name, '_');
	if (name_sufix)
		name_sufix++;
	else
		name_sufix = (char*)service->name;

	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "runtime.services.%s.service_id", name_sufix);
	icmap_set_uint16(key_name, service_engine->id);

	for (fn = 0; fn < service_engine->exec_engine_count; fn++) {
		snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "runtime.services.%s.%d.tx", name_sufix, fn);
		icmap_set_uint64(key_name, 0);
		service_stats_tx[service_engine->id][fn] = strdup(key_name);

		snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "runtime.services.%s.%d.rx", name_sufix, fn);
		icmap_set_uint64(key_name, 0);
		service_stats_rx[service_engine->id][fn] = strdup(key_name);
	}

	log_printf (LOGSYS_LEVEL_NOTICE,
		"Service engine loaded: %s [%d]", service_engine->name, service_engine->id);
	init_result = (char *)cs_ipcs_service_init(service_engine);
	if (init_result != NULL) {
		return (init_result);
	}

	return NULL;
}

static int service_priority_max(void)
{
	int lpc = 0, max = 0;
	for(; lpc < SERVICES_COUNT_MAX; lpc++) {
		if(corosync_service[lpc] != NULL && corosync_service[lpc]->priority > max) {
			max = corosync_service[lpc]->priority;
		}
	}
	return max;
}

/*
 * use the force
 */
static unsigned int
corosync_service_unlink_and_exit_priority (
	struct corosync_api_v1 *corosync_api,
	int lowest_priority,
	int *current_priority,
	int *current_service_engine)
{
	unsigned short service_id;
	int res;

	for(; *current_priority >= lowest_priority; *current_priority = *current_priority - 1) {
		for(*current_service_engine = 0;
			*current_service_engine < SERVICES_COUNT_MAX;
			*current_service_engine = *current_service_engine + 1) {

			if(corosync_service[*current_service_engine] == NULL ||
				corosync_service[*current_service_engine]->priority != *current_priority) {
				continue;
			}

			/*
			 * find service handle and unload it if possible.
			 *
			 * If the service engine's exec_exit_fn returns -1 indicating
			 * it was busy, this function returns -1 and can be called again
			 * at a later time (usually via the schedwrk api).
			 */
			service_id = corosync_service[*current_service_engine]->id;

			if (corosync_service[service_id]->exec_exit_fn) {
				res = corosync_service[service_id]->exec_exit_fn ();
				if (res == -1) {
					return (-1);
				}
			}

			/*
			 * Exit all ipc connections dependent on this service
			 */
			cs_ipcs_service_destroy (*current_service_engine);

			log_printf(LOGSYS_LEVEL_NOTICE,
				"Service engine unloaded: %s",
				corosync_service[*current_service_engine]->name);

			corosync_service[*current_service_engine] = NULL;

			/*
			 * Call should call this function again
			 */
			return (1);
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
	unsigned short service_id;
	char *name_sufix;
	int res;
	const char *iter_key_name;
	icmap_iter_t iter;
	char key_name[ICMAP_KEYNAME_MAXLEN];
	unsigned int found_service_ver;
	char *found_service_name;
	int service_found;

	name_sufix = strrchr (service_name, '_');
	if (name_sufix)
		name_sufix++;
	else
		name_sufix = (char*)service_name;


	service_found = 0;
	found_service_name = NULL;
	iter = icmap_iter_init("internal_configuration.service.");
	while ((iter_key_name = icmap_iter_next(iter, NULL, NULL)) != NULL) {
		res = sscanf(iter_key_name, "internal_configuration.service.%hu.%s", &service_id, key_name);
		if (res != 2) {
			continue;
		}

		snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "internal_configuration.service.%hu.name", service_id);
		if (icmap_get_string(key_name, &found_service_name) != CS_OK) {
			continue;
		}

		snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "internal_configuration.service.%u.ver", service_id);
		if (icmap_get_uint32(key_name, &found_service_ver) != CS_OK) {
			free(found_service_name);
			continue;
		}

		if (service_ver == found_service_ver && strcmp(found_service_name, service_name) == 0) {
			free(found_service_name);
			service_found = 1;
			break;
		}
		free(found_service_name);
	}
	icmap_iter_finalize(iter);

	if (service_found && service_id < SERVICES_COUNT_MAX
		&& corosync_service[service_id] != NULL) {

		if (corosync_service[service_id]->exec_exit_fn) {
			res = corosync_service[service_id]->exec_exit_fn ();
			if (res == -1) {
				return (-1);
			}
		}

		log_printf(LOGSYS_LEVEL_NOTICE,
			"Service engine unloaded: %s",
			   corosync_service[service_id]->name);

		corosync_service[service_id] = NULL;

		cs_ipcs_service_destroy (service_id);

		snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "internal_configuration.service.%u.handle", service_id);
		icmap_delete(key_name);
		snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "internal_configuration.service.%u.name", service_id);
		icmap_delete(key_name);
		snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "internal_configuration.service.%u.ver", service_id);
		icmap_delete(key_name);
	}

	return (0);
}

/*
 * Links default services into the executive
 */
unsigned int corosync_service_defaults_link_and_init (struct corosync_api_v1 *corosync_api)
{
	unsigned int i;
	char *error;

	for (i = 0;
		i < sizeof (default_services) / sizeof (struct default_service); i++) {

		default_services[i].loader();
		error = corosync_service_link_and_init (
			corosync_api,
			&default_services[i]);
		if (error) {
			log_printf(LOGSYS_LEVEL_ERROR,
				"Service engine '%s' failed to load for reason '%s'",
				default_services[i].name,
				error);
			corosync_exit_error (COROSYNC_DONE_SERVICE_ENGINE_INIT);
		}
	}

	return (0);
}

static void service_exit_schedwrk_handler (void *data) {
	int res;
	static int current_priority = 0;
	static int current_service_engine = 0;
	static int called = 0;
	struct seus_handler_data *cb_data = (struct seus_handler_data *)data;
	struct corosync_api_v1 *api = (struct corosync_api_v1 *)cb_data->api;

	if (called == 0) {
		log_printf(LOGSYS_LEVEL_NOTICE,
			"Unloading all Corosync service engines.");
 		current_priority = service_priority_max ();
		called = 1;
	}

	res = corosync_service_unlink_and_exit_priority (
		api,
		0,
		&current_priority,
		&current_service_engine);
	if (res == 0) {
		service_unlink_all_complete();
		return;
	}

	qb_loop_job_add(cs_poll_handle_get(),
		QB_LOOP_HIGH,
		data,
		service_exit_schedwrk_handler);
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

	qb_loop_job_add(cs_poll_handle_get(),
		QB_LOOP_HIGH,
		&cb_data,
		service_exit_schedwrk_handler);
}

struct service_unlink_and_exit_data {
	hdb_handle_t handle;
	struct corosync_api_v1 *api;
	const char *name;
	unsigned int ver;
};

static void service_unlink_and_exit_schedwrk_handler (void *data)
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
	} else {
		qb_loop_job_add(cs_poll_handle_get(),
			QB_LOOP_HIGH,
			data,
			service_unlink_and_exit_schedwrk_handler);
	}
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

	qb_loop_job_add(cs_poll_handle_get(),
		QB_LOOP_HIGH,
		service_unlink_and_exit_data,
		service_unlink_and_exit_schedwrk_handler);
	return (0);
}
