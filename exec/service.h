/*
 * Copyright (c) 2002-2006 MontaVista Software, Inc.
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
#ifndef COROSYNC_SERVICE_H_DEFINED
#define COROSYNC_SERVICE_H_DEFINED

#include <corosync/hdb.h>

struct corosync_api_v1;

struct default_service {
	const char *name;
	int ver;
	struct corosync_service_engine *(*loader)(void);
};

/**
 * Link and initialize a service
 */
char *corosync_service_link_and_init (
	struct corosync_api_v1 *objdb,
	struct default_service *service_engine);

/**
 * Unlink and exit a service
 */
extern unsigned int corosync_service_unlink_and_exit (
	struct corosync_api_v1 *objdb,
	const char *service_name,
	unsigned int service_ver);

/**
 * Unlink and exit all corosync services
 */
extern void corosync_service_unlink_all (
	struct corosync_api_v1 *api,
	void (*unlink_all_complete) (void));

/**
 * Load all of the default services
 */
extern unsigned int corosync_service_defaults_link_and_init (
	struct corosync_api_v1 *objdb);

extern struct corosync_service_engine *corosync_service[];

extern const char *service_stats_rx[SERVICES_COUNT_MAX][SERVICE_HANDLER_MAXIMUM_COUNT];
extern const char *service_stats_tx[SERVICES_COUNT_MAX][SERVICE_HANDLER_MAXIMUM_COUNT];

struct corosync_service_engine *votequorum_get_service_engine_ver0 (void);
struct corosync_service_engine *vsf_quorum_get_service_engine_ver0 (void);
struct corosync_service_engine *quorum_get_service_handler_ver0 (void);
struct corosync_service_engine *pload_get_service_engine_ver0 (void);
struct corosync_service_engine *cfg_get_service_engine_ver0 (void);
struct corosync_service_engine *cpg_get_service_engine_ver0 (void);
struct corosync_service_engine *mon_get_service_engine_ver0 (void);
struct corosync_service_engine *wd_get_service_engine_ver0 (void);
struct corosync_service_engine *cmap_get_service_engine_ver0 (void);

#endif /* SERVICE_H_DEFINED */
