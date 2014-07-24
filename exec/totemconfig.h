/*
 * Copyright (c) 2005 MontaVista Software, Inc.
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
#ifndef TOTEMCONFIG_H_DEFINED
#define TOTEMCONFIG_H_DEFINED

#include <netinet/in.h>
#include <corosync/corotypes.h>
#include <corosync/list.h>
#include <qb/qbloop.h>
#include <corosync/totem/totempg.h>

#include "totemsrp.h"

#define TOTEM_CONFIG_WARNING_MEMBERS_IGNORED		(1<<1)
#define TOTEM_CONFIG_WARNING_MEMBERS_DEPRECATED		(1<<2)
#define TOTEM_CONFIG_WARNING_TOTEM_NODEID_IGNORED	(1<<3)

extern int totem_config_read (
	struct totem_config *totem_config,
	const char **error_string,
	uint64_t *warnings);

extern int totem_config_validate (
	struct totem_config *totem_config,
	const char **error_string);

extern int totem_config_keyread (
	struct totem_config *totem_config,
	const char **error_string);

extern int totem_config_find_local_addr_in_nodelist(
	const char *ipaddr_key_prefix,
	unsigned int *node_pos);

#endif /* TOTEMCONFIG_H_DEFINED */
