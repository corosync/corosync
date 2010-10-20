/*
 * Copyright (c) 2009 Red Hat, Inc.
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

#ifndef CORODEFS_H_DEFINED
#define CORODEFS_H_DEFINED

#include <netinet/in.h>

#define COROSYNC_SOCKET_NAME	"corosync.ipc"

enum corosync_service_types {
	EVS_SERVICE = 0,
	CLM_SERVICE = 1,
	AMF_SERVICE = 2,
	CKPT_SERVICE = 3,
	EVT_SERVICE = 4,
	LCK_SERVICE = 5,
	MSG_SERVICE = 6,
	CFG_SERVICE = 7,
	CPG_SERVICE = 8,
	CMAN_SERVICE = 9,
	PCMK_SERVICE = 10,
	CONFDB_SERVICE = 11,
	QUORUM_SERVICE = 12,
	PLOAD_SERVICE = 13,
	TMR_SERVICE = 14,
	VOTEQUORUM_SERVICE = 15,
	NTF_SERVICE = 16,
	AMF_V2_SERVICE = 17,
	TST_SV1_SERVICE = 18,
	TST_SV2_SERVICE = 19,
	MON_SERVICE = 20,
	WD_SERVICE = 21
};

#ifdef HAVE_SMALL_MEMORY_FOOTPRINT
#define PROCESSOR_COUNT_MAX 16
#else
#define PROCESSOR_COUNT_MAX 384
#endif /* HAVE_SMALL_MEMORY_FOOTPRINT */

#define TOTEMIP_ADDRLEN (sizeof(struct in6_addr))

#endif /* CORODEFS_H_DEFINED */
