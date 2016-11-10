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

#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qdevice-config.h"

#include "dynar.h"
#include "dynar-str.h"
#include "utils.h"
#include "unix-socket.h"

#define IPC_READ_BUF_SIZE	512

enum qdevice_tool_operation {
	QDEVICE_TOOL_OPERATION_NONE,
	QDEVICE_TOOL_OPERATION_SHUTDOWN,
	QDEVICE_TOOL_OPERATION_STATUS,
};

enum qdevice_tool_exit_code {
	QDEVICE_TOOL_EXIT_CODE_NO_ERROR = 0,
	QDEVICE_TOOL_EXIT_CODE_USAGE = 1,
	QDEVICE_TOOL_EXIT_CODE_INTERNAL_ERROR = 2,
	QDEVICE_TOOL_EXIT_CODE_SOCKET_CONNECT = 3,
	QDEVICE_TOOL_EXIT_CODE_QDEVICE_RETURNED_ERROR = 4,
};

static void
usage(void)
{

	printf("usage: %s [-Hhsv] [-p qdevice_ipc_socket_path]\n",
	    QDEVICE_TOOL_PROGRAM_NAME);
}

static void
cli_parse(int argc, char * const argv[], enum qdevice_tool_operation *operation,
    int *verbose, char **socket_path)
{
	int ch;

	*operation = QDEVICE_TOOL_OPERATION_NONE;
	*verbose = 0;
	*socket_path = strdup(QDEVICE_DEFAULT_LOCAL_SOCKET_FILE);

	if (*socket_path == NULL) {
		errx(QDEVICE_TOOL_EXIT_CODE_INTERNAL_ERROR,
		    "Can't alloc memory for socket path string");
	}

	while ((ch = getopt(argc, argv, "Hhsvp:")) != -1) {
		switch (ch) {
		case 'H':
			*operation = QDEVICE_TOOL_OPERATION_SHUTDOWN;
			break;
		case 's':
			*operation = QDEVICE_TOOL_OPERATION_STATUS;
			break;
		case 'v':
			*verbose = 1;
			break;
		case 'p':
			free(*socket_path);
			*socket_path = strdup(optarg);
			if (*socket_path == NULL) {
				errx(QDEVICE_TOOL_EXIT_CODE_INTERNAL_ERROR,
				    "Can't alloc memory for socket path string");
			}
			break;
		case 'h':
		case '?':
			usage();
			exit(QDEVICE_TOOL_EXIT_CODE_USAGE);
			break;
		}
	}

	if (*operation == QDEVICE_TOOL_OPERATION_NONE) {
		usage();
		exit(QDEVICE_TOOL_EXIT_CODE_USAGE);
	}
}

static int
store_command(struct dynar *str, enum qdevice_tool_operation operation, int verbose)
{
	const char *nline = "\n\0";
	const int nline_len = 2;

	switch (operation) {
	case QDEVICE_TOOL_OPERATION_NONE:
		errx(QDEVICE_TOOL_EXIT_CODE_INTERNAL_ERROR, "Unhandled operation none");
		break;
	case QDEVICE_TOOL_OPERATION_SHUTDOWN:
		if (dynar_str_cat(str, "shutdown ") != 0) {
			return (-1);
		}
		break;
	case QDEVICE_TOOL_OPERATION_STATUS:
		if (dynar_str_cat(str, "status ") != 0) {
			return (-1);
		}
		break;
	}

	if (verbose) {
		if (dynar_str_cat(str, "verbose ") != 0) {
			return (-1);
		}
	}

	if (dynar_cat(str, nline, nline_len) != 0) {
		return (-1);
	}

	return (0);
}

/*
 * -1 - Internal error (can't alloc memory)
 *  0 - No error
 *  1 - IPC returned error
 *  2 - Unknown status line
 */
static int
read_ipc_reply(FILE *f)
{
	struct dynar read_str;
	int ch;
	int status_readed;
	int res;
	static const char *ok_str = "OK";
	static const char *err_str = "Error";
	int err_set;
	char c;

	dynar_init(&read_str, IPC_READ_BUF_SIZE);

	status_readed = 0;
	err_set = 0;
	res = 0;

	while ((ch = fgetc(f)) != EOF) {
		if (status_readed) {
			putc(ch, (err_set ? stderr : stdout));
		} else {
			if (ch == '\r') {
			} else if (ch == '\n') {
				status_readed = 1;

				c = '\0';
				if (dynar_cat(&read_str, &c, sizeof(c)) != 0) {
					res = -1;
					goto exit_destroy;
				}

				if (strcasecmp(dynar_data(&read_str), ok_str) == 0) {
				} else if (strcasecmp(dynar_data(&read_str), err_str) == 0) {
					err_set = 1;
					res = 1;
					fprintf(stderr, "Error: ");
				} else {
					res = 2;
					goto exit_destroy;
				}
			} else {
				c = ch;
				if (dynar_cat(&read_str, &c, sizeof(c)) != 0) {
					res = -1;
					goto exit_destroy;
				}
			}
		}
	}

exit_destroy:
	dynar_destroy(&read_str);

	return (res);
}

int
main(int argc, char * const argv[])
{
	enum qdevice_tool_operation operation;
	int verbose;
	char *socket_path;
	int sock_fd;
	FILE *sock;
	struct dynar send_str;
	int res;
	int exit_code;

	exit_code = QDEVICE_TOOL_EXIT_CODE_NO_ERROR;

	cli_parse(argc, argv, &operation, &verbose, &socket_path);

	dynar_init(&send_str, QDEVICE_DEFAULT_IPC_MAX_RECEIVE_SIZE);

	sock_fd = unix_socket_client_create(socket_path, 0);
	if (sock_fd == -1) {
		err(QDEVICE_TOOL_EXIT_CODE_SOCKET_CONNECT,
		    "Can't connect to QDevice socket (is QDevice running?)");
	}

	sock = fdopen(sock_fd, "w+t");
	if (sock == NULL) {
		err(QDEVICE_TOOL_EXIT_CODE_INTERNAL_ERROR, "Can't open QDevice socket fd");
	}

	if (store_command(&send_str, operation, verbose) != 0) {
		errx(QDEVICE_TOOL_EXIT_CODE_INTERNAL_ERROR, "Can't store command");
	}

	res = fprintf(sock, "%s", dynar_data(&send_str));
	if (res < 0 || (size_t)res != strlen(dynar_data(&send_str)) ||
	    fflush(sock) != 0) {
		errx(QDEVICE_TOOL_EXIT_CODE_INTERNAL_ERROR, "Can't send command");
	}

	res = read_ipc_reply(sock);
	switch (res) {
	case -1:
		errx(QDEVICE_TOOL_EXIT_CODE_INTERNAL_ERROR, "Internal error during IPC status line read");
		break;
	case 0:
		break;
	case 1:
		exit_code = QDEVICE_TOOL_EXIT_CODE_QDEVICE_RETURNED_ERROR;
		break;
	case 2:
		errx(QDEVICE_TOOL_EXIT_CODE_SOCKET_CONNECT, "Unknown status line returned by IPC server");
		break;
	}

	if (fclose(sock) != 0) {
		warn("Can't close QDevice socket");
	}

	free(socket_path);
	dynar_destroy(&send_str);

	return (exit_code);
}
