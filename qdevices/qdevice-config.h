/*
 * Copyright (c) 2015-2017 Red Hat, Inc.
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

#include "qdevice-heuristics-mode.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * There are "hardcoded" defines for qdevice. It's not so good
 * idea to change them as long as you are not 100% sure what you are doing. Also
 * most of them can be changed in CLI via advanced_settings (-S).
 */
#define QDEVICE_DEFAULT_LOCK_FILE		LOCALSTATEDIR"/run/corosync-qdevice/corosync-qdevice.pid"
#define QDEVICE_DEFAULT_LOCAL_SOCKET_FILE	LOCALSTATEDIR"/run/corosync-qdevice/corosync-qdevice.sock"
#define QDEVICE_DEFAULT_LOCAL_SOCKET_BACKLOG	10
#define QDEVICE_MIN_LOCAL_SOCKET_BACKLOG	1

#define QDEVICE_DEFAULT_MAX_CS_TRY_AGAIN	10
#define QDEVICE_MIN_MAX_CS_TRY_AGAIN		1

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

#define QDEVICE_DEFAULT_VOTEQUORUM_DEVICE_NAME	"Qdevice"

#define QDEVICE_DEFAULT_IPC_MAX_CLIENTS		10
#define QDEVICE_MIN_IPC_MAX_CLIENTS		0
#define QDEVICE_DEFAULT_IPC_MAX_RECEIVE_SIZE	(4*1024)
#define QDEVICE_DEFAULT_IPC_MAX_SEND_SIZE	(64*1024)
#define QDEVICE_MIN_IPC_RECEIVE_SEND_SIZE	1024

#define QDEVICE_DEFAULT_HEURISTICS_IPC_MAX_SEND_BUFFERS		128
#define QDEVICE_MIN_HEURISTICS_IPC_MAX_SEND_BUFFERS		10
#define QDEVICE_DEFAULT_HEURISTICS_IPC_MAX_SEND_RECEIVE_SIZE	(4 * 1024)
#define QDEVICE_MIN_HEURISTICS_IPC_MAX_SEND_RECEIVE_SIZE	1024

#define QDEVICE_DEFAULT_HEURISTICS_MIN_TIMEOUT			(1 * 1000)
#define QDEVICE_DEFAULT_HEURISTICS_MAX_TIMEOUT			(2 * 60 * 1000)
#define QDEVICE_MIN_HEURISTICS_TIMEOUT				250
#define QDEVICE_DEFAULT_HEURISTICS_MIN_INTERVAL			QDEVICE_DEFAULT_HEURISTICS_MIN_TIMEOUT
#define QDEVICE_DEFAULT_HEURISTICS_MAX_INTERVAL			(60 * 60 * 1000)
#define QDEVICE_MIN_HEURISTICS_INTERVAL				QDEVICE_MIN_HEURISTICS_TIMEOUT

#define QDEVICE_DEFAULT_HEURISTICS_MODE				QDEVICE_HEURISTICS_MODE_DISABLED

#define QDEVICE_DEFAULT_HEURISTICS_MAX_EXECS			32
#define QDEVICE_MIN_HEURISTICS_MAX_EXECS			1

#define QDEVICE_DEFAULT_HEURISTICS_USE_EXECVP			0

#define QDEVICE_DEFAULT_HEURISTICS_MAX_PROCESSES		(QDEVICE_DEFAULT_HEURISTICS_MAX_EXECS * 5)
#define QDEVICE_MIN_HEURISTICS_MAX_PROCESSES			1

#define QDEVICE_DEFAULT_HEURISTICS_KILL_LIST_INTERVAL		(5 * 1000)
#define QDEVICE_MIN_HEURISTICS_KILL_LIST_INTERVAL		QDEVICE_MIN_HEURISTICS_TIMEOUT

#define QDEVICE_TOOL_PROGRAM_NAME		"corosync-qdevice-tool"

#ifdef __cplusplus
}
#endif

#endif /* _QDEVICE_CONFIG_H_ */
