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
#ifndef HANDLERS_H_DEFINED
#define HANDLERS_H_DEFINED

#include <netinet/in.h>
#include "main.h"
#include "totempg.h"
#include "totemsrp.h"

struct libais_handler {
	int (*libais_handler_fn) (struct conn_info *conn_info, void *msg);
	int response_size;
	int response_id;
};

struct service_handler {
	struct libais_handler *libais_handlers;
	int libais_handlers_count;
	int (**aisexec_handler_fns) (void *msg, struct in_addr source_addr, int endian_conversion_needed);
	int aisexec_handler_fns_count;
	int (*confchg_fn) (
		enum totempg_configuration_type configuration_type,
		struct in_addr *member_list, void *member_list_private,
			int member_list_entries,
		struct in_addr *left_list, void *left_list_private,
			int left_list_entries,
		struct in_addr *joined_list, void *joined_list_private,
			int joined_list_entries,
		struct memb_ring_id *ring_id);
	int (*libais_init_fn) (struct conn_info *conn_info, void *msg);
	int (*libais_exit_fn) (struct conn_info *conn_info);
	int (*exec_init_fn) (void);
	void (*exec_dump_fn) (void);
};

#endif /* HANDLERS_H_DEFINED */
