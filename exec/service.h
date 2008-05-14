/*
 * Copyright (c) 2002-2006 MontaVista Software, Inc.
 * Copyright (c) 2006-2007 Red Hat, Inc.
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
#ifndef OPENAIS_SERVICE_H_DEFINED
#define OPENAIS_SERVICE_H_DEFINED

#define SERVICE_ID_MAKE(a,b) ( ((a)<<16) | (b) )
#define SERVICE_HANDLER_MAXIMUM_COUNT 64

enum openais_flow_control {
	OPENAIS_FLOW_CONTROL_REQUIRED = 1,
	OPENAIS_FLOW_CONTROL_NOT_REQUIRED = 2
};

struct openais_lib_handler {
	void (*lib_handler_fn) (void *conn, void *msg);
	int response_size;
	int response_id;
	enum openais_flow_control flow_control;
};

struct openais_exec_handler {
	void (*exec_handler_fn) (void *msg, unsigned int nodeid);
	void (*exec_endian_convert_fn) (void *msg);
};

struct openais_service_handler {
	char *name;
	unsigned short id;
	unsigned int private_data_size;
	enum openais_flow_control flow_control;
	int (*lib_init_fn) (void *conn);
	int (*lib_exit_fn) (void *conn);
	struct openais_lib_handler *lib_service;
	int lib_service_count;
	struct openais_exec_handler *exec_service;
	int (*exec_init_fn) (struct objdb_iface_ver0 *);
	int (*exec_exit_fn) (struct objdb_iface_ver0 *);
	int (*config_init_fn) (struct objdb_iface_ver0 *);
	void (*exec_dump_fn) (void);
	int exec_service_count;
	void (*confchg_fn) (
		enum totem_configuration_type configuration_type,
		unsigned int *member_list, int member_list_entries,
		unsigned int *left_list, int left_list_entries,
		unsigned int *joined_list, int joined_list_entries,
		struct memb_ring_id *ring_id);
	void (*sync_init) (void);
	int (*sync_process) (void);
	void (*sync_activate) (void);
	void (*sync_abort) (void);
};

struct openais_service_handler_iface_ver0 {
	struct openais_service_handler *(*openais_get_service_handler_ver0) (void);
};

/*
 * Link and initialize a service
 */
extern unsigned int openais_service_link_and_init (
	struct objdb_iface_ver0 *objdb,
	char *service_name,
	unsigned int service_ver);

/*
 * Unlink and exit a service
 */
extern unsigned int openais_service_unlink_and_exit (
    struct objdb_iface_ver0 *objdb,
    char *service_name,
    unsigned int service_ver);

/*
 * Unlink and exit all openais services
 */
extern unsigned int openais_service_unlink_all (
    struct objdb_iface_ver0 *objdb);


/*
 * Load all of the default services
 */
extern unsigned int openais_service_defaults_link_and_init (
	struct objdb_iface_ver0 *objdb);

extern struct openais_service_handler *ais_service[];

#endif /* SERVICE_H_DEFINED */
