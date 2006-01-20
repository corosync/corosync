/*
 * Copyright (C) 2006 Steven Dake (sdake@mvista.com)
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
#include <dlfcn.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include "lcr_comp.h"
#include "lcr_ifact.h"
#include "../include/hdb.h"

struct lcr_component_instance {
	struct lcr_iface *ifaces;
	int iface_count;
	void *dl_handle;
	int (*lcr_comp_get) (struct lcr_comp **component);
	int refcount;
};

struct lcr_iface_instance {
	unsigned int component_handle;
	void *context;
	void (*destructor) (void *context);
};
	
static struct hdb_handle_database lcr_component_instance_database = {
	.handle_count	= 0,
	.handles	= 0,
	.iterator	= 0
};

static struct hdb_handle_database lcr_iface_instance_database = {
	.handle_count	= 0,
	.handles	= 0,
	.iterator	= 0
};

static int lcr_select_so (const struct dirent *dirent)
{
	unsigned int len;

	len = strlen (dirent->d_name);

	if (len > 6) {
		if (strcmp (".lcrso", dirent->d_name + len - 6) == 0) {
			return (1);
		}
	}
	return (0);
}

int lcr_ifact_reference (
	unsigned int *iface_handle,
	char *iface_name,
	int version,
	void **iface,
	void *context)
{
	struct lcr_component_instance *instance;
	struct lcr_component_instance new_component;
	struct lcr_iface_instance *iface_instance;
	int found = 0;
	int i;
	int res = -1;
	struct lcr_comp *comp;
	unsigned int component_handle;
	struct dirent **scandir_list;
	int scandir_entries;
	unsigned int libs_to_scan;
	char cwd[512];
	char dl_name[1024];

	getcwd (cwd, sizeof (cwd));
	strcat (cwd, "/");

	/*
	 * Try to find interface in already loaded component
	 */
	hdb_iterator_reset (&lcr_component_instance_database);
	while (hdb_iterator_next (&lcr_component_instance_database,
		(void **)&instance, &component_handle) == 0) {

		for (i = 0; i < instance->iface_count; i++) {
			if ((strcmp (instance->ifaces[i].name, iface_name) == 0) &&
				instance->ifaces[i].version == version) {

				found = 1;
				goto found;
			}
		}
		hdb_handle_put (&lcr_component_instance_database, component_handle);
	}

// TODO error checking in this code is weak
	/*
	 * Find all *.lcrso files in the cwd
	 */
	scandir_entries = scandir(".", &scandir_list, lcr_select_so, alphasort);
	if (scandir_entries < 0)
		printf ("ERROR %d\n", errno);
	else
	/*
	 * ELSE do the job
	 */
	for (libs_to_scan = 0; libs_to_scan < scandir_entries; libs_to_scan++) {

	/*
	 * Load objects, scan them, unload them if they are not a match
	 */
	fflush (stdout);
	sprintf (dl_name, "%s%s", cwd, scandir_list[libs_to_scan]->d_name);
	new_component.dl_handle =
		dlopen (dl_name, RTLD_NOW);
	if (new_component.dl_handle == 0) {
		printf ("Error loading interface %s\n", dlerror());
		return (-1);
	}
	new_component.lcr_comp_get =
		dlsym (new_component.dl_handle, "lcr_comp_get");
	if (new_component.lcr_comp_get == 0) {
		printf ("Error linking interface %s\n", dlerror());
		return (-1);
	}
	res = new_component.lcr_comp_get (&comp);
	new_component.ifaces = comp->ifaces;
	new_component.iface_count = comp->iface_count;

	/*
	 * Search loaded component for matching interface
	 */
	for (i = 0; i < new_component.iface_count; i++) {
		if ((strcmp (new_component.ifaces[i].name, iface_name) == 0) &&
			new_component.ifaces[i].version == version) {

			hdb_handle_create (&lcr_component_instance_database,
				sizeof (struct lcr_component_instance),
				&component_handle);
			hdb_handle_get (&lcr_component_instance_database,
				component_handle, (void *)&instance);
			memcpy (instance, &new_component,
				sizeof (struct lcr_component_instance));
	  
//			printf("Found interface %s ver %d in dynamically loaded object %s\n", iface_name, version, dl_name);
			found = 1;
			free(scandir_list[libs_to_scan]);
			goto found;
		}
	}

	/*
	 * No matching interfaces found, try next shared object
	 */
	dlclose (new_component.dl_handle);
	} /* scanning for loop */

	/*
	 * No matching interfaces found in all shared objects
	 */
	return (-1);
found:

	if (found) {
		*iface = instance->ifaces[i].interfaces;
		if (instance->ifaces[i].constructor) {
			instance->ifaces[i].constructor (context);
		}
		hdb_handle_create (&lcr_iface_instance_database,
			sizeof (struct lcr_iface_instance),
			iface_handle);
		hdb_handle_get (&lcr_iface_instance_database,
			*iface_handle, (void *)&iface_instance);
		iface_instance->component_handle = component_handle;
		iface_instance->context = context;
		iface_instance->destructor = instance->ifaces[i].destructor;
		res = 0;
	}

	return res;
}

int lcr_ifact_release (unsigned int handle)
{
	struct lcr_iface_instance *iface_instance;
	int res = 0;

	res = hdb_handle_get (&lcr_iface_instance_database,
		handle, (void *)&iface_instance);
	return (res);

	if (iface_instance->destructor) {
		iface_instance->destructor (iface_instance->context);
	}

	hdb_handle_put (&lcr_component_instance_database,
		iface_instance->component_handle);
	hdb_handle_put (&lcr_iface_instance_database, handle);
	hdb_handle_destroy (&lcr_iface_instance_database, handle);

	return (res);
}
