/*
 * Copyright (c) 2005 MontaVista Software, Inc.
 * Copyright (c) 2009-2013 Red Hat, Inc.
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
#ifndef IPC_CFG_H_DEFINED
#define IPC_CFG_H_DEFINED

#include <netinet/in.h>
#include <corosync/corotypes.h>
#include <corosync/mar_gen.h>

#define CFG_INTERFACE_NAME_MAX_LEN		128
#define CFG_INTERFACE_STATUS_MAX_LEN		512
/*
 * Too keep future ABI compatibility, this value
 * is intentionaly bigger then INTERFACE_MAX
 */
#define CFG_MAX_INTERFACES			16

/**
 * @brief The req_lib_cfg_types enum
 */
enum req_lib_cfg_types {
	MESSAGE_REQ_CFG_RINGSTATUSGET = 0,
	MESSAGE_REQ_CFG_RINGREENABLE = 1,
	MESSAGE_REQ_CFG_KILLNODE = 2,
	MESSAGE_REQ_CFG_TRYSHUTDOWN = 3,
	MESSAGE_REQ_CFG_REPLYTOSHUTDOWN = 4,
	MESSAGE_REQ_CFG_GET_NODE_ADDRS = 5,
	MESSAGE_REQ_CFG_LOCAL_GET = 6,
	MESSAGE_REQ_CFG_RELOAD_CONFIG = 7,
	MESSAGE_REQ_CFG_CRYPTO_SET = 8
};

/**
 * @brief The res_lib_cfg_types enum
 */
enum res_lib_cfg_types {
        MESSAGE_RES_CFG_RINGSTATUSGET = 0,
        MESSAGE_RES_CFG_RINGREENABLE = 1,
        MESSAGE_RES_CFG_STATETRACKSTART = 2,
        MESSAGE_RES_CFG_STATETRACKSTOP = 3,
        MESSAGE_RES_CFG_ADMINISTRATIVESTATESET = 4,
        MESSAGE_RES_CFG_ADMINISTRATIVESTATEGET = 5,
        MESSAGE_RES_CFG_SERVICELOAD = 6,
        MESSAGE_RES_CFG_SERVICEUNLOAD = 7,
	MESSAGE_RES_CFG_KILLNODE = 8,
	MESSAGE_RES_CFG_TRYSHUTDOWN = 9,
	MESSAGE_RES_CFG_TESTSHUTDOWN = 10,
	MESSAGE_RES_CFG_GET_NODE_ADDRS = 11,
	MESSAGE_RES_CFG_LOCAL_GET = 12,
	MESSAGE_RES_CFG_REPLYTOSHUTDOWN = 13,
	MESSAGE_RES_CFG_CRYPTO_SET = 14,
	MESSAGE_RES_CFG_RELOAD_CONFIG = 15
};

/**
 * @brief The req_lib_cfg_ringstatusget struct
 */
struct req_lib_cfg_ringstatusget {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
};

/**
 * @brief The res_lib_cfg_ringstatusget struct
 */
struct res_lib_cfg_ringstatusget {
	struct qb_ipc_response_header header __attribute__((aligned(8)));
	mar_uint32_t interface_count __attribute__((aligned(8)));
	char interface_name[CFG_MAX_INTERFACES][CFG_INTERFACE_NAME_MAX_LEN] __attribute__((aligned(8)));
	char interface_status[CFG_MAX_INTERFACES][CFG_INTERFACE_STATUS_MAX_LEN] __attribute__((aligned(8)));
};

/**
 * @brief The req_lib_cfg_ringreenable struct
 */
struct req_lib_cfg_ringreenable {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
};

/**
 * @brief The res_lib_cfg_ringreenable struct
 */
struct res_lib_cfg_ringreenable {
	struct qb_ipc_response_header header __attribute__((aligned(8)));
};

/**
 * @brief The req_lib_cfg_killnode struct
 */
struct req_lib_cfg_killnode {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
	unsigned int nodeid __attribute__((aligned(8)));
	cs_name_t reason __attribute__((aligned(8)));
};

/**
 * @brief The res_lib_cfg_killnode struct
 */
struct res_lib_cfg_killnode {
	struct qb_ipc_response_header header __attribute__((aligned(8)));
};

/**
 * @brief The req_lib_cfg_tryshutdown struct
 */
struct req_lib_cfg_tryshutdown {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
	unsigned int flags;
};

/**
 * @brief The res_lib_cfg_tryshutdown struct
 */
struct res_lib_cfg_tryshutdown {
	struct qb_ipc_response_header header __attribute__((aligned(8)));
};

/**
 * @brief The req_lib_cfg_replytoshutdown struct
 */
struct req_lib_cfg_replytoshutdown {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
	unsigned int response;
};

/**
 * @brief The res_lib_cfg_replytoshutdown struct
 */
struct res_lib_cfg_replytoshutdown {
	struct qb_ipc_response_header header __attribute__((aligned(8)));
};

/**
 * @brief The res_lib_cfg_testshutdown struct
 */
struct res_lib_cfg_testshutdown {
	struct qb_ipc_response_header header __attribute__((aligned(8)));
	unsigned int flags;
};

/**
 * @brief The req_lib_cfg_get_node_addrs struct
 */
struct req_lib_cfg_get_node_addrs {
        struct qb_ipc_request_header header __attribute__((aligned(8)));
	unsigned int nodeid;
};

/**
 * @brief The res_lib_cfg_get_node_addrs struct
 */
struct res_lib_cfg_get_node_addrs {
        struct qb_ipc_response_header header __attribute__((aligned(8)));
	unsigned int family;
	unsigned int num_addrs;
	/* array of TOTEMIP_ADDRLEN items */
	char addrs[];
};

/**
 * @brief The req_lib_cfg_local_get struct
 */
struct req_lib_cfg_local_get {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
};

/**
 * @brief The res_lib_cfg_local_get struct
 */
struct res_lib_cfg_local_get {
	struct qb_ipc_response_header header __attribute__((aligned(8)));
	mar_uint32_t local_nodeid __attribute__((aligned(8)));
};

/**
 * @brief The req_lib_cfg_reload_config struct
 */
struct req_lib_cfg_reload_config {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
};

/**
 * @brief The res_lib_cfg_reload_config struct
 */
struct res_lib_cfg_reload_config {
	struct qb_ipc_response_header header __attribute__((aligned(8)));
};

/**
 * @brief corosync_administrative_target_t enum
 */
typedef enum {
	AIS_AMF_ADMINISTRATIVETARGET_SERVICEUNIT = 0,
	AIS_AMF_ADMINISTRATIVETARGET_SERVICEGROUP = 1,
	AIS_AMF_ADMINISTRATIVETARGET_COMPONENTSERVICEINSTANCE = 2,
	AIS_AMF_ADMINISTRATIVETARGET_NODE = 3
} corosync_administrative_target_t;

/**
 * @brief corosync_administrative_state_t enum
 */
typedef enum {
	AIS_AMF_ADMINISTRATIVESTATE_UNLOCKED = 0,
	AIS_AMF_ADMINISTRATIVESTATE_LOCKED = 1,
	AIS_AMF_ADMINISTRATIVESTATE_STOPPING = 2
} corosync_administrative_state_t;

/**
 * @brief corosync_shutdown_flags_t enum
 */
typedef enum {
	CFG_SHUTDOWN_FLAG_REQUEST = 0,
	CFG_SHUTDOWN_FLAG_REGARDLESS = 1,
	CFG_SHUTDOWN_FLAG_IMMEDIATE = 2,
} corosync_shutdown_flags_t;


#endif /* IPC_CFG_H_DEFINED */
