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
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/param.h>

#include "../include/list.h"
#include "util.h"
#include "totem.h"
#include "totemconfig.h"
#include "print.h"

#define LOG_SERVICE LOG_SERVICE_GMI

#if defined(OPENAIS_BSD) || defined(OPENAIS_DARWIN)
	#define HZ 100  /* 10ms */
#endif

#define TOKEN_RETRANSMITS_BEFORE_LOSS_CONST	4
#define TOKEN_TIMEOUT				1000
#define TOKEN_RETRANSMIT_TIMEOUT		(int)(TOKEN_TIMEOUT / (TOKEN_RETRANSMITS_BEFORE_LOSS_CONST + 0.2))
#define TOKEN_HOLD_TIMEOUT			(int)(TOKEN_RETRANSMIT_TIMEOUT * 0.8 - (1000/(int)HZ))
#define JOIN_TIMEOUT				100
#define CONSENSUS_TIMEOUT			200
#define MERGE_TIMEOUT				200
#define DOWNCHECK_TIMEOUT			1000
#define FAIL_TO_RECV_CONST			50
#define	SEQNO_UNCHANGED_CONST			30
#define MINIMUM_TIMEOUT				(int)(1000/HZ)*3
#define MAX_NETWORK_DELAY			50 /*In milliseconds*/

static char error_string_response[512];

typedef enum {
	MAIN_HEAD,
	MAIN_TOTEM,
} main_parse_t;

static inline char *
strstr_rs (const char *haystack, const char *needle)
{
	char *end_address;
	char *new_needle;
	char *token = (char *)(needle + strlen (needle) - 1); /* last char is always the token */

	
	new_needle = (char *)strdup (needle);
	new_needle[strlen(new_needle) - 1] = '\0'; /* remove token */

	end_address = strstr (haystack, new_needle);
	if (end_address == 0) {
		goto not_found;
	}

	end_address += strlen (new_needle);

	/*
	 * Parse all tabs and spaces until token is found
	 * if other character found besides token, its not a match
	 */
	do {
		if (*end_address == '\t' || *end_address == ' ') {
			end_address++;
		} else {
			break;
		}
	} while (*end_address != '\0' && *end_address != token[0]);

	if (*end_address != token[0]) {
		end_address = 0;
		goto not_found;
	}
		
	end_address++;	/* skip past token */

	do {
		if (*end_address == '\t' || *end_address == ' ') {
			end_address++;
		} else {
			break;
		}
	} while (*end_address != '\0');

not_found:
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

	totem_config->secauth = 1;

	fp = fopen (OPENAIS_CONFDIR "/openais.conf", "r");

	if (fp == 0) {
		parse_done = 1;
		sprintf (error_reason, "Can't read file %s reason = (%s)\n",
			 OPENAIS_CONFDIR, strerror (errno));
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
			} else {
				continue;
			}
			break;

		case MAIN_TOTEM:
			if ((loc = strstr_rs (line, "version:"))) {
				if (strcmp (loc, "1") == 0) {
					totem_config->version = 1;
				}
			} else
			if ((loc = strstr_rs (line, "secauth:"))) {
				if (strcmp (loc, "on") == 0) {
					totem_config->secauth = 1;
				} else
				if (strcmp (loc, "off") == 0) {
					totem_config->secauth = 0;
				}
			} else
			if ((loc = strstr_rs (line, "threads:"))) {
				totem_config->threads = atoi (loc);
			} else
			if ((loc = strstr_rs (line, "nodeid:"))) {
				res = totem_config->node_id = atoi (loc);
			} else
			if ((loc = strstr_rs (line, "netmtu:"))) {
				totem_config->net_mtu = atoi (loc);
			} else
			if ((loc = strstr_rs (line, "mcastaddr:"))) {
				res = totemip_parse (&totem_config->mcast_addr, loc);
			} else
			if ((loc = strstr_rs (line, "mcastport:"))) {
				res = totem_config->ip_port = htons (atoi (loc));
			} else
			if ((loc = strstr_rs (line, "bindnetaddr:"))) {
				if (interface_max == totem_config->interface_count) {
					sprintf (error_reason,
						"%d is too many interfaces in %s/network.conf",
						totem_config->interface_count, OPENAIS_CONFDIR);
					goto parse_error;
				}
				res = totemip_parse (&totem_config->interfaces[totem_config->interface_count].bindnet, loc);
				totem_config->interface_count += 1;
			} else
			if ((loc = strstr_rs (line, "token:"))) {
				totem_config->token_timeout = atoi(loc);
			} else if ((loc = strstr_rs (line, "token_retransmit:"))) {
				totem_config->token_retransmit_timeout = atoi(loc);
			} else if ((loc = strstr_rs (line, "hold:"))) {
				totem_config->token_hold_timeout = atoi(loc);
			} else if ((loc = strstr_rs (line, "token_retransmits_before_loss_const:"))) {
				totem_config->token_retransmits_before_loss_const = atoi(loc);
			} else if ((loc = strstr_rs (line, "join:"))) {
				totem_config->join_timeout = atoi(loc);
			} else if ((loc = strstr_rs (line, "consensus:"))) {
				totem_config->consensus_timeout = atoi(loc);
			} else if ((loc = strstr_rs (line, "merge:"))) {
				totem_config->merge_timeout = atoi(loc);
			} else if ((loc = strstr_rs (line, "downcheck:"))) {
				totem_config->downcheck_timeout = atoi(loc);
			} else if ((loc = strstr_rs (line, "fail_recv_const:"))) {
				totem_config->fail_to_recv_const = atoi(loc);
			} else if ((loc = strstr_rs (line, "seqno_unchanged_const:"))) {
				totem_config->seqno_unchanged_const = atoi(loc);
			} else if ((loc = strstr_rs (line, "heartbeat_failures_allowed:"))) {
				totem_config->heartbeat_failures_allowed = atoi(loc);
			} else if ((loc = strstr_rs (line, "max_network_delay:"))) {
				totem_config->max_network_delay = atoi(loc);
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
			"parse error in %s/openais.conf: %s.\n",
			 OPENAIS_CONFDIR, error_reason);
	} else {
		sprintf (error_string_response,
			"parse error in %s/openais.conf: %s (line %d).\n",
			 OPENAIS_CONFDIR, error_reason, line_number);
	}
	*error_string = error_string_response;
	fclose (fp);
	return (-1);
}

int totem_config_validate (
	struct totem_config *totem_config,
	char **error_string)
{
	static char local_error_reason[512];
	char *error_reason = local_error_reason;

	/*
	 * Some error checking of parsed data to make sure its valid
	 */
	if ((int *)&totem_config->mcast_addr.addr == 0) {
		error_reason = "No multicast address specified";
		goto parse_error;
	}

	if (totem_config->ip_port == 0) {
		error_reason = "No multicast port specified";
		goto parse_error;
	}

	if (totem_config->mcast_addr.family == AF_INET6 &&
		totem_config->node_id == 0) {

		error_reason = "An IPV6 network requires that a node ID be specified.";
		goto parse_error;
	}

	if (totem_config->interface_count == 0) {
		error_reason = "No bindnet specified";
		goto parse_error;
	}

	if (totem_config->version != 1) {
		error_reason = "This totem parser can only parse version 1 configuration files.";
		goto parse_error;
	}


	if (totem_config->token_retransmits_before_loss_const == 0) {
		totem_config->token_retransmits_before_loss_const =
			TOKEN_RETRANSMITS_BEFORE_LOSS_CONST;
	}

	/*
	 * Setup timeout values that are not setup by user
	 */
	if (totem_config->token_timeout == 0) {
		totem_config->token_timeout = TOKEN_TIMEOUT;
		if (totem_config->token_retransmits_before_loss_const == 0) {
			totem_config->token_retransmits_before_loss_const = TOKEN_RETRANSMITS_BEFORE_LOSS_CONST;
		}

		if (totem_config->token_retransmit_timeout == 0) {
			totem_config->token_retransmit_timeout =
				(int)(totem_config->token_timeout /
				(totem_config->token_retransmits_before_loss_const + 0.2));
		}
		if (totem_config->token_hold_timeout == 0) {
			totem_config->token_hold_timeout =
				(int)(totem_config->token_retransmit_timeout * 0.8 -
				(1000/HZ));
		}
	}

	if (totem_config->max_network_delay == 0) {
		totem_config->max_network_delay = MAX_NETWORK_DELAY;
	}

	if (totem_config->max_network_delay < MINIMUM_TIMEOUT) {
		sprintf (local_error_reason, "The max_network_delay parameter (%d ms) may not be less then (%d ms).",
			totem_config->max_network_delay, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	if (totem_config->token_timeout < MINIMUM_TIMEOUT) {
		sprintf (local_error_reason, "The token timeout parameter (%d ms) may not be less then (%d ms).",
			totem_config->token_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	if (totem_config->token_retransmit_timeout == 0) {
		totem_config->token_retransmit_timeout =
			(int)(totem_config->token_timeout /
			(totem_config->token_retransmits_before_loss_const + 0.2));
	}
	if (totem_config->token_hold_timeout == 0) {
		totem_config->token_hold_timeout =
			(int)(totem_config->token_retransmit_timeout * 0.8 -
			(1000/HZ));
	}
	if (totem_config->token_retransmit_timeout < MINIMUM_TIMEOUT) {
		sprintf (local_error_reason, "The token retransmit timeout parameter (%d ms) may not be less then (%d ms).",
			totem_config->token_retransmit_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	if (totem_config->token_hold_timeout == 0) {
		totem_config->token_hold_timeout = TOKEN_HOLD_TIMEOUT;
	}

	if (totem_config->token_hold_timeout < MINIMUM_TIMEOUT) {
		sprintf (local_error_reason, "The token hold timeout parameter (%d ms) may not be less then (%d ms).",
			totem_config->token_hold_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	if (totem_config->join_timeout == 0) {
		totem_config->join_timeout = JOIN_TIMEOUT;
	}

	if (totem_config->join_timeout < MINIMUM_TIMEOUT) {
		sprintf (local_error_reason, "The join timeout parameter (%d ms) may not be less then (%d ms).",
			totem_config->join_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	if (totem_config->consensus_timeout == 0) {
		totem_config->consensus_timeout = CONSENSUS_TIMEOUT;
	}

	if (totem_config->consensus_timeout < MINIMUM_TIMEOUT) {
		sprintf (local_error_reason, "The consensus timeout parameter (%d ms) may not be less then (%d ms).",
			totem_config->consensus_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	if (totem_config->merge_timeout == 0) {
		totem_config->merge_timeout = MERGE_TIMEOUT;
	}

	if (totem_config->merge_timeout < MINIMUM_TIMEOUT) {
		sprintf (local_error_reason, "The merge timeout parameter (%d ms) may not be less then (%d ms).",
			totem_config->merge_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	if (totem_config->downcheck_timeout == 0) {
		totem_config->downcheck_timeout = DOWNCHECK_TIMEOUT;
	}

	if (totem_config->downcheck_timeout < MINIMUM_TIMEOUT) {
		sprintf (local_error_reason, "The downcheck timeout parameter (%d ms) may not be less then (%d ms).",
			totem_config->downcheck_timeout, MINIMUM_TIMEOUT);
		goto parse_error;
	}

	if (totem_config->fail_to_recv_const == 0) {
		totem_config->fail_to_recv_const = FAIL_TO_RECV_CONST;
	}
	if (totem_config->seqno_unchanged_const == 0) {
		totem_config->seqno_unchanged_const = SEQNO_UNCHANGED_CONST;
	}
	if (totem_config->net_mtu == 0) {
		totem_config->net_mtu = 1500;
	}
	if (totem_config->threads > SEND_THREADS_MAX) {
		totem_config->threads = SEND_THREADS_MAX;
	}
	if (totem_config->secauth == 0) {
		totem_config->threads = 0;
	}
	if (totem_config->net_mtu > FRAME_SIZE_MAX) {
		error_reason = "This net_mtu parameter is greater then the maximum frame size";
		goto parse_error;
	}
		
	return (0);

parse_error:
	sprintf (error_string_response,
		"parse error in %s/openais.conf: %s\n", OPENAIS_CONFDIR, error_reason);
	*error_string = error_string_response;
	return (-1);
}

int totem_config_keyread (
	char *key_location,
	struct totem_config *totem_config,
	char **error_string)
{
	int fd;
	int res;
	int i;

	if (totem_config->secauth == 0) {
		return (0);
	}
	fd = open (key_location, O_RDONLY);
	if (fd == -1) {
		sprintf (error_string_response, "Could not open %s: %s\n",
			key_location, strerror (errno));
		goto parse_error;
	}

	res = read (fd, totem_config->private_key, 128);
	if (res == -1) {
		close (fd);
		sprintf (error_string_response, "Could not read %s: %s\n",
			key_location, strerror (errno));
		goto parse_error;
	}

	totem_config->private_key_len = 128;

	if (res != 128) {
		close (fd);
		sprintf (error_string_response, "Could only read %d bits of 1024 bits from %s.\n",
			res * 8, key_location);
		goto parse_error;
	}
	if (totem_config->mcast_addr.family != totem_config->interfaces[0].bindnet.family) {
		strcpy (error_string_response, "Multicast address family does not match bind address family");
		goto parse_error;
	}
	for (i = 0; i < totem_config->interface_count; i++) {
		if (totem_config->mcast_addr.family != totem_config->interfaces[i].bindnet.family) {
			strcpy (error_string_response, "Not all bind address belong to the same IP family");
			goto parse_error;
		}
	}
	return (0);

parse_error:
	*error_string = error_string_response;
	return (-1);
}
