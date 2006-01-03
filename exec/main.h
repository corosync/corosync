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

#define TRUE 1
#define FALSE 0
#include <sys/un.h>
#include "../include/saAis.h"
#include "../include/queue.h"
#include "../include/ipc_gen.h"
#include "mainconfig.h"
#include "poll.h"
#include "evs.h"
#include "clm.h"
#include "amf.h"
#include "ckpt.h"
#include "evt.h"
#include "lck.h"

#ifndef AIS_EXEC_H_DEFINED
#define AIS_EXEC_H_DEFINED

/*
 * Size of the queue (entries) for I/O's to the API over socket IPC.
 */

#define SIZEQUEUE 256

#define SOCKET_SERVICE_INIT 254

struct aisexec_ci {
	struct sockaddr_in in_addr;	/* address of AF_INET socket, MUST BE FIRST IN STRUCTURE */
	SaClmClusterNodeT clusterNode;
	SaClmClusterChangesT lastChange;
	unsigned char authentication_key[16];
	int authenticated;
};

/*
 * Connection information for AIS connections
 */
struct ais_ci {
	struct sockaddr_un un_addr;	/* address of AF_UNIX socket, MUST BE FIRST IN STRUCTURE */
	union {
		struct aisexec_ci aisexec_ci;
		struct libevs_ci libevs_ci;
		struct libclm_ci libclm_ci;
		struct libamf_ci libamf_ci;
		struct libckpt_ci libckpt_ci;
		struct libevt_ci libevt_ci;
		struct liblck_ci liblck_ci;
	} u;
};

struct outq_item {
	void *msg;
	size_t mlen;
};

#define SIZEINB MESSAGE_SIZE_MAX

enum conn_state {
	CONN_STATE_ACTIVE,
	CONN_STATE_DISCONNECTING,
	CONN_STATE_DISCONNECTING_DELAYED
};

struct conn_info {
	int fd;				/* File descriptor  */
	enum conn_state state;			/* State of this connection */
	char *inb;			/* Input buffer for non-blocking reads */
	int inb_nextheader;	/* Next message header starts here */
	int inb_start;		/* Start location of input buffer */
	int inb_inuse;		/* Bytes currently stored in input buffer */
	struct queue outq;		/* Circular queue for outgoing requests */
	int byte_start;			/* Byte to start sending from in head of queue */
	enum service_types service;/* Type of service so dispatch knows how to route message */
//	struct saAmfComponent *component;	/* Component for which this connection relates to  TODO shouldn't this be in the ci structure */
	int authenticated;		/* Is this connection authenticated? */
	struct list_head conn_list;
	struct ais_ci ais_ci;	/* libais connection information */
	struct conn_info *conn_info_partner;	/* partner connection dispatch<->response */
	int should_exit_fn;			/* Should call the exit function when closing this ipc */
};


enum nodeexec_message_types {
	MESSAGE_REQ_EXEC_EVS_MCAST = 0,
	MESSAGE_REQ_EXEC_CLM_NODEJOIN = 1,
	MESSAGE_REQ_EXEC_AMF_OPERATIONAL_STATE_COMP_SET = 2,
	MESSAGE_REQ_EXEC_AMF_PRESENCE_STATE_COMP_SET = 3,
	MESSAGE_REQ_EXEC_AMF_ADMINISTRATIVE_STATE_CSI_SET = 4,
	MESSAGE_REQ_EXEC_AMF_ADMINISTRATIVE_STATE_UNIT_SET = 5,
	MESSAGE_REQ_EXEC_AMF_ADMINISTRATIVE_STATE_GROUP_SET = 6,
	MESSAGE_REQ_EXEC_CKPT_CHECKPOINTOPEN = 7,
	MESSAGE_REQ_EXEC_CKPT_CHECKPOINTCLOSE = 8,
	MESSAGE_REQ_EXEC_CKPT_CHECKPOINTUNLINK = 9,
	MESSAGE_REQ_EXEC_CKPT_CHECKPOINTRETENTIONDURATIONSET = 10,
	MESSAGE_REQ_EXEC_CKPT_CHECKPOINTRETENTIONDURATIONEXPIRE = 11,
	MESSAGE_REQ_EXEC_CKPT_SECTIONCREATE = 12,
	MESSAGE_REQ_EXEC_CKPT_SECTIONDELETE = 13,
	MESSAGE_REQ_EXEC_CKPT_SECTIONEXPIRATIONTIMESET = 14,
	MESSAGE_REQ_EXEC_CKPT_SECTIONWRITE = 15,
	MESSAGE_REQ_EXEC_CKPT_SECTIONOVERWRITE = 16,
	MESSAGE_REQ_EXEC_CKPT_SECTIONREAD = 17,
	MESSAGE_REQ_EXEC_CKPT_SYNCHRONIZESTATE = 18,
	MESSAGE_REQ_EXEC_CKPT_SYNCHRONIZESECTION = 19,
	MESSAGE_REQ_EXEC_EVT_EVENTDATA = 20,
	MESSAGE_REQ_EXEC_EVT_CHANCMD = 21,
	MESSAGE_REQ_EXEC_EVT_RECOVERY_EVENTDATA = 22,
	MESSAGE_REQ_EXEC_LCK_RESOURCEOPEN = 23,
	MESSAGE_REQ_EXEC_LCK_RESOURCECLOSE = 24,
	MESSAGE_REQ_EXEC_LCK_RESOURCELOCK = 25,
	MESSAGE_REQ_EXEC_LCK_RESOURCEUNLOCK = 26,
	MESSAGE_REQ_EXEC_LCK_RESOURCELOCKORPHAN = 27,
	MESSAGE_REQ_EXEC_LCK_LOCKPURGE = 28
};

extern struct totem_ip_address *this_ip;

extern struct totempg_group openais_group;

extern totempg_groups_handle openais_group_handle;

poll_handle aisexec_poll_handle;

extern int libais_send_response (struct conn_info *conn_info, void *msg, int mlen);

extern int message_source_is_local(struct message_source *source);

extern void message_source_set(struct message_source *source, struct conn_info *conn_info);


#endif /* AIS_EXEC_H_DEFINED */
