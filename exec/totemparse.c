/*
 * Copyright (c) 2002-2004 MontaVista Software, Inc.
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
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../include/list.h"
#include "util.h"
#include "totem.h"
#include "totemparse.h"
#include "print.h"

#define LOG_SERVICE LOG_SERVICE_GMI

static char error_string_response[512];

typedef enum {
	MAIN_HEAD,
	MAIN_TOTEM,
	MAIN_TIMEOUT,
} main_parse_t;

static inline char *
strstr_rs (const char *haystack, const char *needle)
{
	char *end_address;
	char *new_needle;

	new_needle = (char *)strdup (needle);
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

	free (new_needle);
	return (end_address);
}

extern int totem_config_read (
	struct totem_config *totem_config,
	char **error_string,
	int interface_max)
{
	FILE *fp;
	int res = 0;
	int line_number = 0;
	main_parse_t parse = MAIN_HEAD;
	int totem_parsed = 0;
	int timeout_parsed = 0;
	char *loc;
	int i;
	int parse_done = 0;
	char line[512];
	char *error_reason = error_string_response;

	memset (totem_config, 0, sizeof (struct totem_config));
	totem_config->interfaces = malloc (sizeof (struct totem_interface) * interface_max);
	if (totem_config->interfaces == 0) {
		parse_done = 1;
		*error_string = "Out of memory trying to allocate ethernet interface storage area";
		return -1;
	}

	memset (totem_config->interfaces, 0,
		sizeof (struct totem_interface) * interface_max);

	totem_config->mcast_addr.sin_family = AF_INET;
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
			if (totem_parsed == 0 && strstr_rs (line, "network{")) {
				totem_parsed = 1;
				parse = MAIN_TOTEM;
			} else
			if (totem_parsed == 0 && strstr_rs (line, "totem{")) {
				totem_parsed = 1;
				parse = MAIN_TOTEM;
			} else
			if (timeout_parsed == 0 && strstr_rs (line, "timeout{")) {
				timeout_parsed = 1;
				parse = MAIN_TIMEOUT;
			} else {
				continue;
			}
			break;

		case MAIN_TOTEM:
			if ((loc = strstr_rs (line, "mcastaddr:"))) {
				res = inet_aton (loc, &totem_config->mcast_addr.sin_addr);
			} else
			if ((loc = strstr_rs (line, "mcastport:"))) {
				res = totem_config->mcast_addr.sin_port = htons (atoi (loc));
			} else
			if ((loc = strstr_rs (line, "bindnetaddr:"))) {
				if (interface_max == totem_config->interface_count) {
					sprintf (error_reason,
						"%d is too many interfaces in /etc/ais/network.conf",
					totem_config->interface_count);
					goto parse_error;
				}
				res = inet_aton (loc,
					&totem_config->interfaces[totem_config->interface_count].bindnet.sin_addr);
				totem_config->interface_count += 1;
			} else
			if ((loc = strstr_rs (line, "}"))) {
				parse = MAIN_HEAD;
				res = 1; /* any nonzero is ok */
			} else {
				goto parse_error;
			}

			if (res == 0) {
				sprintf (error_reason, "invalid network address or port number\n");
				goto parse_error;
			}
			break;

		case MAIN_TIMEOUT:
			if ((loc = strstr_rs (line, "token:"))) {
				totem_config->timeouts[TOTEM_TOKEN]= atoi(loc);
			} else if ((loc = strstr_rs (line, "token_retransmit:"))) {
				totem_config->timeouts[TOTEM_RETRANSMIT_TOKEN] = atoi(loc);
			} else if ((loc = strstr_rs (line, "hold:"))) {
				totem_config->timeouts[TOTEM_HOLD_TOKEN] = atoi(loc);
			} else if ((loc = strstr_rs (line, "retransmits_before_loss:"))) {
				totem_config->timeouts[TOTEM_RETRANSMITS_BEFORE_LOSS] = atoi(loc);
		
			} else if ((loc = strstr_rs (line, "join:"))) {
				totem_config->timeouts[TOTEM_JOIN] = atoi(loc);
			} else if ((loc = strstr_rs (line, "consensus:"))) {
				totem_config->timeouts[TOTEM_CONSENSUS] = atoi(loc);
			} else if ((loc = strstr_rs (line, "merge:"))) {
				totem_config->timeouts[TOTEM_MERGE] = atoi(loc);
			} else if ((loc = strstr_rs (line, "downcheck:"))) {
				totem_config->timeouts[TOTEM_DOWNCHECK] = atoi(loc);
			} else if ((loc = strstr_rs (line, "fail_recv_const:"))) {
				totem_config->timeouts[TOTEM_FAIL_RECV_CONST] = atoi(loc);
			} else if ((loc = strstr_rs (line, "}"))) {
				parse = MAIN_HEAD;
			} else {
				goto parse_error;
			}
			break;

		default:
			assert (0 == 1); /* SHOULDN'T HAPPEN */
			break;	
		}
	}

	/*
	 * Some error checking of parsed data to make sure its valid
	 */
	parse_done = 1;
	if (totem_config->mcast_addr.sin_addr.s_addr == 0) {
		error_reason = "No multicast address specified";
		goto parse_error;
	}

	if (totem_config->mcast_addr.sin_port == 0) {
		error_reason = "No multicast port specified";
		goto parse_error;
	}

	if (totem_config->interface_count == 0) {
		error_reason = "No bindnet specified";
		goto parse_error;
	}

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

int totemparse_keyread (struct totem_config *totem_config)
{
	int fd;
	int res;

	fd = open ("/etc/ais/authkey", O_RDONLY);
	if (fd == -1) {
		log_printf (LOG_LEVEL_ERROR, "Could not open /etc/ais/authkey: %s\n", strerror (errno));
		return (-1);
	}

	res = read (fd, totem_config->private_key, 128);
	if (res == -1) {
		log_printf (LOG_LEVEL_ERROR, "Could not read /etc/ais/authkey: %s\n", strerror (errno));
		return (-1);
	}

	totem_config->private_key_len = 128;

	if (res != 128) {
		log_printf (LOG_LEVEL_ERROR, "Could only read %d bits of 1024 bits from /etc/ais/authkey.\n", res * 8);
		return (-1);
	}

	close (fd);
	return (0);
}
