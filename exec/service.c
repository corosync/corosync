/*
 * Copyright (c) 2006 MontaVista Software, Inc.
 * Copyright (c) 2006-2007 Red Hat, Inc.
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

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../lcr/lcr_ifact.h"
#include "swab.h"
#include "objdb.h"
#include "totem.h"
#include "service.h"
#include "mainconfig.h"
#include "util.h"
#include "logsys.h"

LOGSYS_DECLARE_SUBSYS ("SERV", LOG_INFO);

struct default_service {
	char *name;
	int ver;
};

static struct default_service default_services[] = {
	{
		.name			 = "openais_evs",
		.ver			 = 0,
	},
	{
		.name			 = "openais_clm",
		.ver			 = 0,
	},
	{
		.name			 = "openais_amf",
		.ver			 = 0,
	},
	{
		.name			 = "openais_ckpt",
		.ver			 = 0,
	},
	{
		.name			 = "openais_evt",
		.ver			 = 0,
	},
	{
		.name			 = "openais_lck",
		.ver			 = 0,
	},
	{
		.name			 = "openais_msg",
		.ver			 = 0,
	},
	{
		.name			 = "openais_cfg",
		.ver			 = 0,
	},
	{
		.name			 = "openais_cpg",
		.ver			 = 0,
	},
	{
		.name			 = "openais_confdb",
		.ver			 = 0,
	}
};

struct openais_service_handler *ais_service[SERVICE_HANDLER_MAXIMUM_COUNT];

static unsigned int default_services_requested (struct objdb_iface_ver0 *objdb)
{
	unsigned int object_service_handle;
	char *value;

	/*
	 * Don't link default services if they have been disabled
	 */
	objdb->object_find_reset (OBJECT_PARENT_HANDLE);
	if (objdb->object_find (
		OBJECT_PARENT_HANDLE,
		"aisexec",
		strlen ("aisexec"),
		&object_service_handle) == 0) {

		if ( ! objdb->object_key_get (object_service_handle,
			"defaultservices",
			strlen ("defaultservices"),
			(void *)&value,
			NULL)) {

			if (value && strcmp (value, "no") == 0) {
				return 0;
			}
		}
	}
		return (-1);
}

unsigned int openais_service_link_and_init (
	struct objdb_iface_ver0 *objdb,
	char *service_name,
	unsigned int service_ver)
{
	struct openais_service_handler_iface_ver0 *iface_ver0;
	void *iface_ver0_p;
	unsigned int handle;
	struct openais_service_handler *service;
	unsigned int res;
	unsigned int object_handle;

	/*
	 * reference the service interface
	 */
	iface_ver0_p = NULL;
	lcr_ifact_reference (
		&handle,
		service_name,
		service_ver,
		&iface_ver0_p,
		(void *)0);

	iface_ver0 = (struct openais_service_handler_iface_ver0 *)iface_ver0_p;

	if (iface_ver0 == 0) {
		log_printf(LOG_LEVEL_ERROR, "Service failed to load '%s'.\n", service_name);
		return (-1);
	}


	/*
	 * Initialize service
	 */
	service = iface_ver0->openais_get_service_handler_ver0();

	ais_service[service->id] = service;
	if (service->config_init_fn) {
		res = service->config_init_fn (objdb);
	}

	if (service->exec_init_fn) {
		res = service->exec_init_fn (objdb);
	}

	/*
	 * Store service in object database
	 */
	objdb->object_create (OBJECT_PARENT_HANDLE,
		&object_handle,
		"service",
		strlen ("service"));

	objdb->object_key_create (object_handle,
		"name",
		strlen ("name"),
		service_name,
		strlen (service_name) + 1);

	objdb->object_key_create (object_handle,
		"ver",
		strlen ("ver"),
		&service_ver,
		sizeof (service_ver));

	res = objdb->object_key_create (object_handle,
		"handle",
		strlen ("handle"),
		&handle,
		sizeof (handle));

	objdb->object_key_create (object_handle,
		"service_id",
		strlen ("service_id"),
		&service->id,
		sizeof (service->id));

	log_printf (LOG_LEVEL_NOTICE, "Service initialized '%s'\n", service->name);
	return (res);
}

extern unsigned int openais_service_unlink_and_exit (
	struct objdb_iface_ver0 *objdb,
	char *service_name,
	unsigned int service_ver)
{
	unsigned int object_service_handle;
	char *found_service_name;
	unsigned int *found_service_ver;
	unsigned int *found_service_handle;
	unsigned short *service_id;
	unsigned int res;

	objdb->object_find_reset (OBJECT_PARENT_HANDLE);
	while (objdb->object_find (
		OBJECT_PARENT_HANDLE,
		"service",
		strlen ("service"),
		&object_service_handle) == 0) {

		objdb->object_key_get (object_service_handle,
			"name",
			strlen ("name"),
			(void *)&found_service_name,
			NULL);

		objdb->object_key_get (object_service_handle,
			"ver",
			strlen ("ver"),
			(void *)&found_service_ver,
			NULL);
				
		/*
		 * If service found and linked exit it
		 */
		if ((strcmp (service_name, found_service_name) == 0) &&
			(service_ver == *found_service_ver)) {

			res = objdb->object_key_get (object_service_handle,
				"handle",
				strlen ("handle"),
				(void *)&found_service_handle,
				NULL);

			res = objdb->object_key_get (object_service_handle,
				"service_id",
				strlen ("service_id"),
				(void *)&service_id,
				NULL);
					
			if (ais_service[*service_id]->exec_exit_fn) {
				ais_service[*service_id]->exec_exit_fn (objdb);
			}
			ais_service[*service_id] = NULL;

			res = lcr_ifact_release (*found_service_handle);	
			objdb->object_destroy (object_service_handle);
			return (res);
		}
	}
	return (-1);
}

/*
 * Links default services into the executive
 */
unsigned int openais_service_defaults_link_and_init (struct objdb_iface_ver0 *objdb)
{
	unsigned int i;

	if (default_services_requested (objdb) == 0) {
		return (0);
	}
	for (i = 0;
		i < sizeof (default_services) / sizeof (struct default_service); i++) {

		openais_service_link_and_init (
			objdb,
			default_services[i].name,
			default_services[i].ver);
	}
			
	return (0);
}
