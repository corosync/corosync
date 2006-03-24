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
	int refcount;
	char library_name[256];
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

static unsigned int g_component_handle;

#ifdef OPENAIS_LINUX
static int lcr_select_so (const struct dirent *dirent)
#else
static int lcr_select_so (struct dirent *dirent)
#endif
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

static inline struct lcr_component_instance *lcr_comp_find (
	char *iface_name,
	unsigned int version,
	int *iface_number)
{
	struct lcr_component_instance *instance;
	unsigned int component_handle = 0;
	int i;

	/*
	 * Try to find interface in already loaded component
	 */
	hdb_iterator_reset (&lcr_component_instance_database);
	while (hdb_iterator_next (&lcr_component_instance_database,
		(void **)(void *)&instance, &component_handle) == 0) {

		for (i = 0; i < instance->iface_count; i++) {
			if ((strcmp (instance->ifaces[i].name, iface_name) == 0) &&
				instance->ifaces[i].version == version) {

				*iface_number = i;
				return (instance);
			}
		}
		hdb_handle_put (&lcr_component_instance_database, component_handle);
	}

	return (NULL);
}

static inline int lcr_lib_loaded (
	char *library_name)
{
	struct lcr_component_instance *instance;
	unsigned int component_handle = 0;

	/*
	 * Try to find interface in already loaded component
	 */
	hdb_iterator_reset (&lcr_component_instance_database);
	while (hdb_iterator_next (&lcr_component_instance_database,
		(void **)(void *)&instance, &component_handle) == 0) {

		if (strcmp (instance->library_name, library_name) == 0) {
			return (1);
		}

		hdb_handle_put (&lcr_component_instance_database, component_handle);
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
	void *dl_handle;
	struct lcr_iface_instance *iface_instance;
	struct lcr_component_instance *instance;
	int iface_number;
	struct dirent **scandir_list;
	int scandir_entries;
	unsigned int libs_to_scan;
	char cwd[512];
	char dl_name[1024];

	getcwd (cwd, sizeof (cwd));
	strcat (cwd, "/");

	/*
	 * Determine if the component is already loaded
	 */
	instance = lcr_comp_find (iface_name, version, &iface_number);
	if (instance) {
		goto found;
	}

// TODO error checking in this code is weak
	/*
	 * Find all *.lcrso files in the cwd
	 */
	scandir_entries = scandir(".", &scandir_list, lcr_select_so, alphasort);
	if (scandir_entries < 0)
		printf ("scandir error reason=%s\n", strerror (errno));
	else
	/*
	 * no error so load the object
	 */
	for (libs_to_scan = 0; libs_to_scan < scandir_entries; libs_to_scan++) {
		/*
		 * Load objects, scan them, unload them if they are not a match
		 */
		sprintf (dl_name, "%s%s", cwd, scandir_list[libs_to_scan]->d_name);
		/*
	 	 * Don't reload already loaded libraries
		 */
		if (lcr_lib_loaded (dl_name)) {
			continue;
		}
		dl_handle = dlopen (dl_name, RTLD_NOW);
		if (dl_handle == NULL) {
			printf ("Error loading interface %s reason=%s\n", dl_name, dlerror());
			return (-1);
		}
		instance = lcr_comp_find (iface_name, version, &iface_number);
		if (instance) {
			instance->dl_handle = dl_handle;
			strcpy (instance->library_name, dl_name);
			goto found;
		}

		/*
		 * No matching interfaces found, try next shared object
		 */
		if (g_component_handle != 0xFFFFFFFF) {
			hdb_handle_destroy (&lcr_component_instance_database,
				g_component_handle);
			g_component_handle = 0xFFFFFFFF;
		}
		dlclose (dl_handle);
	} /* scanning for lcrso loop */

	/*
	 * No matching interfaces found in all shared objects
	 */
	return (-1);
found:
	*iface = instance->ifaces[iface_number].interfaces;
	if (instance->ifaces[iface_number].constructor) {
		instance->ifaces[iface_number].constructor (context);
	}
	hdb_handle_create (&lcr_iface_instance_database,
		sizeof (struct lcr_iface_instance),
		iface_handle);
	hdb_handle_get (&lcr_iface_instance_database,
		*iface_handle, (void *)&iface_instance);
	iface_instance->component_handle = g_component_handle;
	iface_instance->context = context;
	iface_instance->destructor = instance->ifaces[iface_number].destructor;

	return (0);
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

void lcr_component_register (struct lcr_comp *comp)
{
	struct lcr_component_instance *instance;

	hdb_handle_create (&lcr_component_instance_database,
		sizeof (struct lcr_component_instance),
		&g_component_handle);
	hdb_handle_get (&lcr_component_instance_database,
		g_component_handle, (void *)&instance);

	instance->ifaces = comp->ifaces;
	instance->iface_count = comp->iface_count;
	instance->dl_handle = NULL;

	hdb_handle_put (&lcr_component_instance_database,
		g_component_handle);
}
