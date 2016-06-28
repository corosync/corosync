/*
 * Copyright (c) 2015-2016 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Jan Friesse (jfriesse@redhat.com)
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
 * - Neither the name of the Red Hat, Inc. nor the names of its
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

#ifndef _QNET_CONFIG_H_
#define _QNET_CONFIG_H_

#include <config.h>

#include "tlv.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * There are "hardcoded" defaults for both qnetd and qdevice-net. It's not so good
 * idea to change them as long as you are not 100% sure what you are doing. Also
 * most of them can be changed in CLI via advanced_settings (-S).
 */

#define QNETD_PROGRAM_NAME				"corosync-qnetd"
#define QNETD_DEFAULT_HOST_PORT				5403
#define QNETD_DEFAULT_LISTEN_BACKLOG			10
#define QNETD_MIN_LISTEN_BACKLOG			1
#define QNETD_DEFAULT_MAX_CLIENT_SEND_BUFFERS		32
#define QNETD_MIN_CLIENT_SEND_BUFFERS			2
#define QNETD_DEFAULT_MAX_CLIENT_SEND_SIZE		(1 << 15)
#define QNETD_DEFAULT_MAX_CLIENT_RECEIVE_SIZE		(1 << 15)
#define QNETD_MIN_CLIENT_RECEIVE_SEND_SIZE		16
#define QNETD_DEFAULT_MAX_CLIENTS			0

#define QNETD_DEFAULT_NSS_DB_DIR			COROSYSCONFDIR "/qnetd/nssdb"
#define QNETD_DEFAULT_CERT_NICKNAME			"QNetd Cert"

#define QNETD_DEFAULT_TLS_SUPPORTED			TLV_TLS_SUPPORTED
#define QNETD_DEFAULT_TLS_CLIENT_CERT_REQUIRED		1

#define QNETD_DEFAULT_HEARTBEAT_INTERVAL_MIN		(1*1000)
#define QNETD_DEFAULT_HEARTBEAT_INTERVAL_MAX		(2*60*1000)
#define QNETD_MIN_HEARTBEAT_INTERVAL			1

#define QNETD_DEFAULT_DPD_ENABLED			1
#define QNETD_DEFAULT_DPD_INTERVAL			(10*1000)
#define QNETD_MIN_DPD_INTERVAL				1

#define QNETD_DEFAULT_LOCK_FILE				LOCALSTATEDIR"/run/corosync-qnetd/corosync-qnetd.pid"
#define QNETD_DEFAULT_LOCAL_SOCKET_FILE			LOCALSTATEDIR"/run/corosync-qnetd/corosync-qnetd.sock"
#define QNETD_DEFAULT_LOCAL_SOCKET_BACKLOG		10
#define QNETD_MIN_LOCAL_SOCKET_BACKLOG			1

#define QNETD_DEFAULT_IPC_MAX_CLIENTS			10
#define QNETD_MIN_IPC_MAX_CLIENTS			0
#define QNETD_DEFAULT_IPC_MAX_RECEIVE_SIZE		(4*1024)
#define QNETD_DEFAULT_IPC_MAX_SEND_SIZE			(10*1024*1024)
#define QNETD_MIN_IPC_RECEIVE_SEND_SIZE			1024

#define QNETD_TOOL_PROGRAM_NAME				"corosync-qnetd-tool"

#define QDEVICE_NET_DEFAULT_NSS_DB_DIR			COROSYSCONFDIR "/qdevice/net/nssdb"

#define QDEVICE_NET_DEFAULT_INITIAL_MSG_RECEIVE_SIZE	(1 << 15)
#define QDEVICE_NET_DEFAULT_INITIAL_MSG_SEND_SIZE	(1 << 15)
#define QDEVICE_NET_DEFAULT_MIN_MSG_SEND_SIZE		QDEVICE_NET_DEFAULT_INITIAL_MSG_SEND_SIZE
#define QDEVICE_NET_DEFAULT_MAX_MSG_RECEIVE_SIZE	(1 << 24)
#define QDEVICE_NET_DEFAULT_MAX_SEND_BUFFERS		10
#define QDEVICE_NET_MIN_MAX_SEND_BUFFERS		2
#define QDEVICE_NET_MIN_MSG_RECEIVE_SEND_SIZE		16

#define QDEVICE_NET_DEFAULT_NSS_QNETD_CN		"Qnetd Server"

#define QDEVICE_NET_DEFAULT_NSS_CLIENT_CERT_NICKNAME	"Cluster Cert"


#define QDEVICE_NET_DEFAULT_ALGORITHM			TLV_DECISION_ALGORITHM_TYPE_FFSPLIT

#define QDEVICE_NET_DEFAULT_TLS_SUPPORTED		TLV_TLS_SUPPORTED

#define QDEVICE_NET_DEFAULT_TIE_BREAKER_MODE		TLV_TIE_BREAKER_MODE_LOWEST

#define QDEVICE_NET_DEFAULT_HEARTBEAT_INTERVAL_MIN	QNETD_DEFAULT_HEARTBEAT_INTERVAL_MIN
#define QDEVICE_NET_DEFAULT_HEARTBEAT_INTERVAL_MAX	QNETD_DEFAULT_HEARTBEAT_INTERVAL_MAX
#define QDEVICE_NET_MIN_HEARTBEAT_INTERVAL		1

#define QDEVICE_NET_DEFAULT_MIN_CONNECT_TIMEOUT		(1*1000)
#define QDEVICE_NET_DEFAULT_MAX_CONNECT_TIMEOUT		(2*60*1000)
#define QDEVICE_NET_MIN_CONNECT_TIMEOUT			1

#ifdef DEBUG
#define QDEVICE_NET_DEFAULT_TEST_ALGORITHM_ENABLED	1
#else
#define QDEVICE_NET_DEFAULT_TEST_ALGORITHM_ENABLED	0
#endif

/*
 * Decision algorithms supported by qnetd
 */
#define QNETD_STATIC_SUPPORTED_DECISION_ALGORITHMS_SIZE	4

extern enum tlv_decision_algorithm_type
    qnetd_static_supported_decision_algorithms[QNETD_STATIC_SUPPORTED_DECISION_ALGORITHMS_SIZE];

#define QDEVICE_NET_STATIC_SUPPORTED_DECISION_ALGORITHMS_SIZE	QNETD_STATIC_SUPPORTED_DECISION_ALGORITHMS_SIZE

#ifdef __cplusplus
}
#endif

#endif /* _QNET_CONFIG_H_ */
