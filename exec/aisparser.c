/*
 * Copyright (c) 2006 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Patrick Caulfield (pcaulfie@redhat.com)
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
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <string.h>

#include "../lcr/lcr_comp.h"
#include "objdb.h"
#include "config.h"
#include "mempool.h"

static int read_config_file_into_objdb(
	struct objdb_iface_ver0 *objdb,
	char **error_string);
static char error_string_response[512];


static int aisparser_readconfig (struct objdb_iface_ver0 *objdb, char **error_string)
{
	if (read_config_file_into_objdb(objdb, error_string)) {
		return -1;
	}

	return 0;
}


char *remove_whitespace(char *string)
{
	char *start = string+strspn(string, " \t");
	char *end = start+(strlen(start))-1;

	while ((*end == ' ' || *end == '\t' || *end == ':' || *end == '{') && end > start)
		end--;
	if (end != start)
		*(end+1) = '\0';

	return start;
}

char *strstr_rs (const char *haystack, const char *needle)
{
	char *end_address;
	char *new_needle;

	new_needle = (char *)mempool_strdup (needle);
	new_needle[strlen(new_needle) - 1] = '\0';

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

	mempool_free (new_needle);
	return (end_address);
}

static int parse_section(FILE *fp,
			 struct objdb_iface_ver0 *objdb,
			 unsigned int parent_handle,
			 char **error_string)
{
	char line[512];
	int i;
	char *loc;

	while (fgets (line, 255, fp)) {
		line[strlen(line) - 1] = '\0';
		/*
		 * Clear out white space and tabs
		 */
		for (i = strlen (line) - 1; i > -1; i--) {
			if (line[i] == '\t' || line[i] == ' ') {
				line[i] = '\0';
			} else {
				break;
			}
		}
		/*
		 * Clear out comments and empty lines
		 */
		if (line[0] == '#' || line[0] == '\0') {
			continue;
		}

		/* New section ? */
		if ((loc = strstr_rs (line, "{"))) {
			unsigned int new_parent;
			char *section = remove_whitespace(line);

			loc--;
			*loc = '\0';
printf ("creating object %d %s\n", parent_handle, section);
			objdb->object_create (parent_handle, &new_parent,
					      section, strlen (section));
			if (parse_section(fp, objdb, new_parent, error_string))
				return -1;
		}

		/* New key/value */
		if ((loc = strstr_rs (line, ":"))) {
			char *key;
			char *value;

			*(loc-1) = '\0';
			key = remove_whitespace(line);
			value = remove_whitespace(loc);
			objdb->object_key_create (parent_handle, key,
				strlen (key),
				value, strlen (value) + 1);
		}

		if ((loc = strstr_rs (line, "}"))) {
			return 0;
		}
	}

	if (parent_handle != OBJECT_PARENT_HANDLE) {
		*error_string = "Missing closing brace";
		return -1;
	}

	return 0;
}



/* Read config file and load into objdb */
static int read_config_file_into_objdb(
	struct objdb_iface_ver0 *objdb,
	char **error_string)
{
	FILE *fp;
	char *error_reason = error_string_response;
	int res;

	fp = fopen (OPENAIS_CONFDIR "/openais.conf", "r");
	if (fp == 0) {
		sprintf (error_reason, "Can't read file %s/openais.conf reason = (%s)\n",
			 OPENAIS_CONFDIR, strerror (errno));
		*error_string = error_reason;
		return -1;
	}

	res = parse_section(fp, objdb, OBJECT_PARENT_HANDLE, error_string);

	fclose(fp);

	return res;
}

/*
 * Dynamic Loader definition
 */

struct config_iface_ver0 aisparser_iface_ver0 = {
	.config_readconfig        = aisparser_readconfig
};

struct lcr_iface openais_aisparser_ver0[1] = {
	{
		.name				= "aisparser",
		.version			= 0,
		.versions_replace		= 0,
		.versions_replace_count		= 0,
		.dependencies			= 0,
		.dependency_count		= 0,
		.constructor			= NULL,
		.destructor			= NULL,
		.interfaces			= (void **)(void *)&aisparser_iface_ver0,
	}
};

struct openais_service_handler *aisparser_get_handler_ver0 (void);

struct lcr_comp aisparser_comp_ver0 = {
	.iface_count				= 1,
	.ifaces					= openais_aisparser_ver0
};


__attribute__ ((constructor)) static void aisparser_comp_register (void) {
	lcr_component_register (&aisparser_comp_ver0);
}


