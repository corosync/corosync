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
#include <netinet/in.h>
#include "../include/saAis.h"
#include "../include/list.h"
#include "aispoll.h"
#include "totemsrp.h"
#include "totempg.h"

#ifndef MAINCONFIG_H_DEFINED
#define MAINCONFIG_H_DEFINED

/*
 * All service handlers in the AIS
 */
#ifdef BUILD_DYNAMIC
struct dynamic_service {
	char *name;
	unsigned int ver;
	unsigned int handle;
	struct openais_service_handler_iface_ver0 *iface_ver0;
};
#define MAX_DYNAMIC_SERVICES 128
#endif

struct openais_config {
	/*
	 * logging
	 */
	int logmode;
	char *logfile;

	/*
	 * Event service
	 */
	unsigned int evt_delivery_queue_size;
	unsigned int evt_delivery_queue_resume;

	/*
	 * AMF service
	 */
	unsigned int amf_enabled;

	struct totem_config totem_config;
#ifdef BUILD_DYNAMIC
	struct dynamic_service dynamic_services[MAX_DYNAMIC_SERVICES];
	int num_dynamic_services;
#endif
};

extern char *strstr_rs (const char *haystack, const char *needle);

extern int openais_main_config_read (char **error_string,
	struct openais_config *openais_config,
	int interface_max);
	
#endif /* MAINCONFIG_H_DEFINED */
