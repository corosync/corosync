/*
 * Copyright (c) 2002-2005 MontaVista Software, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@mvista.com)
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
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../include/saAis.h"
#include "../include/list.h"
#include "util.h"
#include "mainconfig.h"
#include "mempool.h"
#include "print.h"
#include "totem.h"

DECLARE_LIST_INIT (saAmfGroupHead);

static char error_string_response[512];

typedef enum {
	MAIN_HEAD,
	MAIN_LOGGING,
	MAIN_EVENT,
	MAIN_AMF,
	MAIN_COMPONENTS
} main_parse_t;

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

#ifdef BUILD_DYNAMIC
static void set_default_services(struct openais_config *config)
{
	config->dynamic_services[0].name = "openais_evs";
	config->dynamic_services[0].ver = 0;
	config->dynamic_services[1].name = "openais_clm";
	config->dynamic_services[1].ver = 0;
	config->dynamic_services[2].name = "openais_amf";
	config->dynamic_services[2].ver = 0;
	config->dynamic_services[3].name = "openais_ckpt";
	config->dynamic_services[3].ver = 0;
	config->dynamic_services[4].name = "openais_evt";
	config->dynamic_services[4].ver = 0;
	config->dynamic_services[5].name = "openais_lck";
	config->dynamic_services[5].ver = 0;
	config->dynamic_services[6].name = "openais_msg";
	config->dynamic_services[6].ver = 0;
	config->dynamic_services[7].name = "openais_cfg";
	config->dynamic_services[7].ver = 0;
	config->num_dynamic_services = 8;
}

/* Returns an allocated string */
static char *get_component(const char *line, int *version)
{
	char *start_address;
	char *end_address;
	char *newline;
	char *compname;

	newline = strdup(line);

	start_address = newline + strspn(newline, " \t");
	end_address = start_address + strcspn(start_address, " \t:");

	*end_address = '\0';
	compname = strdup(start_address);

	/* Now get version */
	start_address = end_address+1;
	start_address = start_address + strspn(start_address, " \t:");

	*version = atoi(start_address);
	free(newline);

	return compname;
}
#endif

extern int openais_main_config_read (char **error_string,
    struct openais_config *openais_config,
	int interface_max)
{
	FILE *fp;
	int line_number = 0;
	main_parse_t parse = MAIN_HEAD;
	int logging_parsed = 0;
	int event_parsed = 0;
	int amf_parsed = 0;
	int components_parsed = 0;
	char *loc;
	int i;
	int parse_done = 0;
	char line[512];
	char *error_reason = error_string_response;

	memset (openais_config, 0, sizeof (struct openais_config));
	fp = fopen ("/etc/ais/openais.conf", "r");
	if (fp == 0) {
		parse_done = 1;
		sprintf (error_reason, "Can't read file reason = (%s)\n", strerror (errno));
		*error_string = error_reason;
		return -1;
	}

	while (fgets (line, 255, fp)) {
		line_number += 1;
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
			
		line_number += 1;

		switch (parse) {
		case MAIN_HEAD:
			if (logging_parsed == 0 && strstr_rs (line, "logging{")) {
				logging_parsed = 1;
				parse = MAIN_LOGGING;
			} else
			if (event_parsed == 0 && strstr_rs (line, "event{")) {
				event_parsed = 1;
				parse = MAIN_EVENT;
			} else
			if (amf_parsed == 0 && strstr_rs (line, "amf{")) {
				amf_parsed = 1;
				parse = MAIN_AMF;
			} else
			if (components_parsed == 0 && strstr_rs (line, "components{")) {
				components_parsed = 1;
				parse = MAIN_COMPONENTS;
			} else {
				continue;
			}
			break;

		case MAIN_LOGGING:
			if ((loc = strstr_rs (line, "logoutput:"))) {
				if (strcmp (loc, "file") == 0) {
					openais_config->logmode |= LOG_MODE_FILE;
				} else
				if (strcmp (loc, "syslog") == 0) {
					openais_config->logmode |= LOG_MODE_SYSLOG;
				} else
				if (strcmp (loc, "stderr") == 0) {
					openais_config->logmode |= LOG_MODE_STDERR;
				} else {
					goto parse_error;
				}
			} else
			if ((loc = strstr_rs (line, "debug:"))) {
				if (strcmp (loc, "on") == 0) {
					openais_config->logmode |= LOG_MODE_DEBUG;
				} else
				if (strcmp (loc, "off") == 0) {
					openais_config->logmode &= ~LOG_MODE_DEBUG;
				} else {
					goto parse_error;
				}
			} else
			if ((loc = strstr_rs (line, "timestamp:"))) {
				if (strcmp (loc, "on") == 0) {
					openais_config->logmode |= LOG_MODE_TIMESTAMP;
				} else
				if (strcmp (loc, "off") == 0) {
					openais_config->logmode &= ~LOG_MODE_TIMESTAMP;
				} else {
					goto parse_error;
				}
			} else 
			if ((loc = strstr_rs (line, "logfile:"))) {
				openais_config->logfile = strdup (loc);
			} else
			if ((loc = strstr_rs (line, "}"))) {
				parse = MAIN_HEAD;
			} else {
				goto parse_error;
			}
			break;

		case MAIN_EVENT:
			if ((loc = strstr_rs (line, "delivery_queue_size:"))) {
					openais_config->evt_delivery_queue_size = atoi(loc);
			} else if ((loc = strstr_rs (line, "delivery_queue_resume:"))) {
					openais_config->evt_delivery_queue_resume = atoi(loc);
			} else if ((loc = strstr_rs (line, "}"))) {
				parse = MAIN_HEAD;
			} else {
				goto parse_error;
			}
			break;

		case MAIN_AMF:
			if ((loc = strstr_rs (line, "mode:"))) {
				if (strcmp (loc, "enabled") == 0) {
					openais_config->amf_enabled = 1;
				} else
				if (strcmp (loc, "disabled") == 0) {
					openais_config->amf_enabled = 0;
				} else {
					goto parse_error;
				}
			} else
			if ((loc = strstr_rs (line, "}"))) {
				parse = MAIN_HEAD;
			} else {
				goto parse_error;
			}
			break;
		case MAIN_COMPONENTS:
			if ((loc = strstr_rs (line, "}"))) {
				parse = MAIN_HEAD;
			}
#ifdef BUILD_DYNAMIC
			else {
				int version;
				char *name = get_component(line, &version);
				if (name) {
					openais_config->dynamic_services[openais_config->num_dynamic_services].name = name;
					openais_config->dynamic_services[openais_config->num_dynamic_services].ver = version;
					openais_config->num_dynamic_services++;
				}
			}
#endif
			break;
		default:
			assert (0 == 1); /* SHOULDN'T HAPPEN */
			break;
		}
	}

	if ((openais_config->logmode & LOG_MODE_FILE) && openais_config->logfile == 0) {
		error_reason = "logmode set to 'file' but no logfile specified";
		goto parse_error;
	}

#ifdef BUILD_DYNAMIC
	/* Load default services if the config file doesn't specify */
	if (!openais_config->num_dynamic_services) {
		set_default_services(openais_config);
	}
#endif
	if (parse == MAIN_HEAD) {
		fclose (fp);
		return (0);
	} else {
		error_reason = "Missing closing brace";
		goto parse_error;
	}

parse_error:
	if (parse_done) {
		sprintf (error_string_response,
			"parse error in /etc/ais/openais.conf: %s.\n", error_reason);
	} else {
		sprintf (error_string_response,
			"parse error in /etc/ais/openais.conf: %s (line %d).\n",
			error_reason, line_number);
	}
	*error_string = error_string_response;
	fclose (fp);
	return (-1);
}
