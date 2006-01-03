/*
 * Copyright (c) 2005 MontaVista Software, Inc.
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
#ifndef AIS_IPC_CFG_H_DEFINED
#define AIS_IPC_CFG_H_DEFINED

#include <netinet/in.h>
#include "ipc_gen.h"
#include "saAis.h"
#include "openaisCfg.h"

enum req_lib_cfg_types {
        MESSAGE_REQ_CFG_STATETRACKSTART = 0,
        MESSAGE_REQ_CFG_STATETRACKSTOP = 1,
        MESSAGE_REQ_CFG_ADMINISTRATIVESTATESET = 2,
        MESSAGE_REQ_CFG_ADMINISTRATIVESTATEGET = 3,
};

enum res_lib_cfg_types {
        MESSAGE_RES_CFG_STATETRACKSTART = 0,
        MESSAGE_RES_CFG_STATETRACKSTOP = 1,
        MESSAGE_RES_CFG_ADMINISTRATIVESTATESET = 2,
        MESSAGE_RES_CFG_ADMINISTRATIVESTATEGET = 3,
};

struct req_lib_cfg_statetrackstart {
	struct req_header header;
	SaUint8T trackFlags;
	OpenaisCfgStateNotificationT *notificationBufferAddress;
};

struct res_lib_cfg_statetrackstart {
	struct res_header header;
};

struct req_lib_cfg_statetrackstop {
	struct req_header header;
};

struct res_lib_cfg_statetrackstop {
	struct res_header header;
};

struct req_lib_cfg_administrativestateset {
	struct req_header header;
	SaNameT compName;
	OpenaisCfgAdministrativeTargetT administrativeTarget;
	OpenaisCfgAdministrativeStateT administrativeState;
};

struct res_lib_cfg_administrativestateset {
	struct res_header header;
};

struct req_lib_cfg_administrativestateget {
	struct req_header header;
	SaNameT compName;
	OpenaisCfgAdministrativeTargetT administrativeTarget;
	OpenaisCfgAdministrativeStateT administrativeState;
};

struct res_lib_cfg_administrativestateget {
	struct res_header header;
};

typedef enum {
	AIS_AMF_ADMINISTRATIVETARGET_SERVICEUNIT = 0,
	AIS_AMF_ADMINISTRATIVETARGET_SERVICEGROUP = 1,
	AIS_AMF_ADMINISTRATIVETARGET_COMPONENTSERVICEINSTANCE = 2,
	AIS_AMF_ADMINISTRATIVETARGET_NODE = 3
} openaisAdministrativeTarget;

typedef enum {
	AIS_AMF_ADMINISTRATIVESTATE_UNLOCKED = 0,
	AIS_AMF_ADMINISTRATIVESTATE_LOCKED = 1,
	AIS_AMF_ADMINISTRATIVESTATE_STOPPING = 2
} openaisAdministrativeState;

#endif /* AIS_IPC_CFG_H_DEFINED */
