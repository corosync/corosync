/*
 * Copyright (c) 2002-2006 MontaVista Software, Inc.
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
#ifndef OPENAIS_HANDLERS_H_DEFINED
#define OPENAIS_HANDLERS_H_DEFINED

#include <netinet/in.h>
#include "mainconfig.h" /* openais_config */
#include "main.h" 	/* conn_info */
#include "totemip.h"

#define SERVICE_ID_MAKE(a,b) ( ((a)<<16) | (b) )

// TODO we need to abstract the conn_info data structure to make dynamic loading work perfectly

enum openais_flow_control {
	OPENAIS_FLOW_CONTROL_REQUIRED = 1,
	OPENAIS_FLOW_CONTROL_NOT_REQUIRED = 2
};

struct openais_lib_handler {
	void (*lib_handler_fn) (struct conn_info *conn_info, void *msg);
	int response_size;
	int response_id;
	enum openais_flow_control flow_control;
};

struct openais_exec_handler {
	void (*exec_handler_fn) (void *msg, struct totem_ip_address *source_addr);
	void (*exec_endian_convert_fn) (void *msg);
};
	
struct openais_service_handler {
	unsigned char *name;
	unsigned short id;
	int (*lib_init_fn) (struct conn_info *conn_info);
	int (*lib_exit_fn) (struct conn_info *conn_info);
	struct openais_lib_handler *lib_handlers;
	int lib_handlers_count;
	struct openais_exec_handler *exec_handlers;
	int (*exec_init_fn) (struct openais_config *);
	void (*exec_dump_fn) (void);
	int exec_handlers_count;
	void (*confchg_fn) (
		enum totem_configuration_type configuration_type,
		struct totem_ip_address *member_list, int member_list_entries,
		struct totem_ip_address *left_list, int left_list_entries,
		struct totem_ip_address *joined_list, int joined_list_entries,
		struct memb_ring_id *ring_id);
	void (*sync_init) (void);
	int (*sync_process) (void);
	void (*sync_activate) (void);
	void (*sync_abort) (void);
};

struct openais_service_handler_iface_ver0 {
	void (*test) (void);
	struct openais_service_handler *(*openais_get_service_handler_ver0) (void);
};

#endif /* HANDLERS_H_DEFINED */
