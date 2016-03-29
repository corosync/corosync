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

#ifndef _QDEVICE_CONFIG_H_
#define _QDEVICE_CONFIG_H_

#include <config.h>

#include <qb/qbdefs.h>
#include <qb/qblog.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * There are "hardcoded" defines for qdevice. It's not so good
 * idea to change them as long as you are not 100% sure what you are doing.
 */
#define QDEVICE_LOCK_FILE			LOCALSTATEDIR"/run/corosync-qdevice.pid"
#define QDEVICE_LOCAL_SOCKET_FILE               LOCALSTATEDIR"/run/corosync-qdevice.sock"
#define QDEVICE_LOCAL_SOCKET_BACKLOG		10

#define QDEVICE_MAX_CS_TRY_AGAIN		10

#define QDEVICE_PROGRAM_NAME			"corosync-qdevice"
#define QDEVICE_LOG_SUBSYS			"QDEVICE"
#define QDEVICE_LOG_DEFAULT_TO_STDERR		1
#define QDEVICE_LOG_DEFAULT_TO_SYSLOG		1
#define QDEVICE_LOG_DEFAULT_TO_LOGFILE		0
#define QDEVICE_LOG_DEFAULT_SYSLOG_FACILITY	LOG_DAEMON
#define QDEVICE_LOG_DEFAULT_SYSLOG_PRIORITY	LOG_INFO
#define QDEVICE_LOG_DEFAULT_DEBUG		0
#define QDEVICE_LOG_DEFAULT_FILELINE		0
#define QDEVICE_LOG_DEFAULT_TIMESTAMP		0
#define QDEVICE_LOG_DEFAULT_FUNCTION_NAME	0

#define QDEVICE_VOTEQUORUM_DEVICE_NAME      "Qdevice"

#define QDEVICE_ENABLE_NSS			1

#define QDEVICE_IPC_MAX_CLIENTS			10
#define QDEVICE_IPC_MAX_RECEIVE_SIZE		(4*1024)
#define QDEVICE_IPC_MAX_SEND_SIZE		(64*1024)

#ifdef __cplusplus
}
#endif

#endif /* _QDEVICE_CONFIG_H_ */
