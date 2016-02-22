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

#include "tlv.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * There are "hardcoded" defines for both qnetd and qdevice-net. It's not so good
 * idea to change them as long as you are not 100% sure what you are doing.
 */

#define QNETD_PROGRAM_NAME			"corosync-qnetd"
#define QNETD_DEFAULT_HOST_PORT			5403
#define QNETD_LISTEN_BACKLOG			10
#define QNETD_MAX_CLIENT_SEND_BUFFERS		10
#define QNETD_MAX_CLIENT_SEND_SIZE		(1 << 15)
#define QNETD_MAX_CLIENT_RECEIVE_SIZE		(1 << 15)
#define QNETD_DEFAULT_MAX_CLIENTS		0

#define QNETD_NSS_DB_DIR			COROSYSCONFDIR "/qdevice/net/qnetd/nssdb"
#define QNETD_CERT_NICKNAME			"QNetd Cert"

#define QNETD_DEFAULT_TLS_SUPPORTED		TLV_TLS_SUPPORTED
#define QNETD_DEFAULT_TLS_CLIENT_CERT_REQUIRED	1

#define QNETD_HEARTBEAT_INTERVAL_MIN		1000
#define QNETD_HEARTBEAT_INTERVAL_MAX		200000

#define QNETD_LOCK_FILE				LOCALSTATEDIR"/run/corosync-qnetd.pid"

#define QDEVICE_NET_NSS_DB_DIR			COROSYSCONFDIR "/qdevice/net/node/nssdb"

#define QDEVICE_NET_INITIAL_MSG_RECEIVE_SIZE	(1 << 15)
#define QDEVICE_NET_INITIAL_MSG_SEND_SIZE	(1 << 15)
#define QDEVICE_NET_MIN_MSG_SEND_SIZE		QDEVICE_NET_INITIAL_MSG_SEND_SIZE
#define QDEVICE_NET_MAX_MSG_RECEIVE_SIZE	(1 << 24)

#define QNETD_NSS_SERVER_CN			"Qnetd Server"
#define QDEVICE_NET_NSS_CLIENT_CERT_NICKNAME	"Cluster Cert"

#define QDEVICE_NET_MAX_SEND_BUFFERS		10

#define QDEVICE_NET_DEFAULT_ALGORITHM		TLV_DECISION_ALGORITHM_TYPE_TEST

#define QNETD_DEFAULT_TIE_BREAKER_MODE		TLV_TIE_BREAKER_MODE_LOWEST
#define QDEVICE_NET_DEFAULT_TIE_BREAKER_MODE	QNETD_DEFAULT_TIE_BREAKER_MODE

#define QDEVICE_NET_MIN_CONNECT_TIMEOUT		1L
#define QDEVICE_NET_MAX_CONNECT_TIMEOUT		(1000*60*10L)

/*
 * Decision algorithms supported by qnetd
 */
#define QNETD_STATIC_SUPPORTED_DECISION_ALGORITHMS_SIZE         4

extern enum tlv_decision_algorithm_type
    qnetd_static_supported_decision_algorithms[QNETD_STATIC_SUPPORTED_DECISION_ALGORITHMS_SIZE];

#define QDEVICE_NET_STATIC_SUPPORTED_DECISION_ALGORITHMS_SIZE	QNETD_STATIC_SUPPORTED_DECISION_ALGORITHMS_SIZE

#ifdef __cplusplus
}
#endif

#endif /* _QNET_CONFIG_H_ */
