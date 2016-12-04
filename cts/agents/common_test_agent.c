/*
 * Copyright (c) 2010 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Angus Salkeld (asalkeld@redhat.com)
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

#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <fcntl.h>

#include "common_test_agent.h"

#define MAX_CLIENTS	64

static char big_and_buf_rx[HOW_BIG_AND_BUF];
ta_do_command_fn do_command;
static qb_loop_t *poll_handle;
static pre_exit_fn pre_exit = NULL;
static int client_fds[MAX_CLIENTS];
static int client_fds_pos = 0;

qb_loop_t *ta_poll_handle_get(void)
{
	return poll_handle;
}

static void shut_me_down(void)
{
	if (pre_exit) {
		pre_exit();
	}
	qb_loop_stop(poll_handle);
}


static void ta_handle_command (int sock, char* msg)
{
	int num_args;
	char *saveptr = NULL;
	char *str = strdup (msg);
	char *str_len;
	char *str_arg;
	char *args[5];
	int i = 0;
	int a = 0;
	char* func = NULL;

	qb_log(LOG_DEBUG,"(MSG:%s)", msg);

	str_len = strtok_r (str, ":", &saveptr);
	assert (str_len);

	num_args = atoi (str_len) * 2;
	for (i = 0; i < num_args / 2; i++) {
		str_len = strtok_r (NULL, ":", &saveptr);
		str_arg = strtok_r (NULL, ":", &saveptr);
		if (func == NULL) {
			/* first "arg" is the function */
			qb_log (LOG_TRACE, "(LEN:%s, FUNC:%s)", str_len, str_arg);
			func = str_arg;
			a = 0;
		} else {
			args[a] = str_arg;
			a++;
			qb_log (LOG_TRACE, "(LEN:%s, ARG:%s)", str_len, str_arg);
		}
	}
	do_command (sock, func, args, a+1);

	free (str);
}

static int server_process_data_fn (
	int fd,
	int revents,
	void *data)
{
	char *saveptr;
	char *msg;
	char *cmd;
	int32_t nbytes;

	if (revents & POLLHUP || revents & POLLERR) {
		qb_log (LOG_INFO, "command sockect got POLLHUP exiting...");
		shut_me_down();
		return -1;
	}

	if ((nbytes = recv (fd, big_and_buf_rx, sizeof (big_and_buf_rx), 0)) <= 0) {
		/* got error or connection closed by client */
		if (nbytes == 0) {
			/* connection closed */
			qb_log (LOG_WARNING, "socket %d hung up: exiting...", fd);
		} else {
			qb_perror(LOG_ERR, "recv() failed");
		}
		shut_me_down();
		return -1;
	} else {
		big_and_buf_rx[nbytes] = '\0';

		msg = strtok_r (big_and_buf_rx, ";", &saveptr);
		assert (msg);
		while (msg) {
			cmd = strdup (msg);
			ta_handle_command (fd, cmd);
			free (cmd);
			msg = strtok_r (NULL, ";", &saveptr);
		}
	}

	return 0;
}

static int server_accept_fn (
	int fd, int revents, void *data)
{
	socklen_t addrlen;
	struct sockaddr_in in_addr;
	int new_fd;
	int res;

	if (revents & POLLHUP || revents & POLLERR) {
		qb_log (LOG_INFO, "command sockect got POLLHUP exiting...");
		shut_me_down();
		return -1;
	}

	addrlen = sizeof (struct sockaddr_in);

retry_accept:
	new_fd = accept (fd, (struct sockaddr *)&in_addr, &addrlen);
	if (new_fd == -1 && errno == EINTR) {
		goto retry_accept;
	}

	if (new_fd == -1) {
		qb_log (LOG_ERR,
			"Could not accept connection: %s", strerror (errno));
		return (0); /* This is an error, but -1 would indicate disconnect from poll loop */
	}

	res = fcntl (new_fd, F_SETFL, O_NONBLOCK);
	if (res == -1) {
		qb_log (LOG_ERR,
			"Could not set non-blocking operation on connection: %s",
			strerror (errno));
		close (new_fd);
		return (0); /* This is an error, but -1 would indicate disconnect from poll loop */
	}

	client_fds[client_fds_pos] = new_fd;
	client_fds_pos++;
	assert(client_fds_pos < MAX_CLIENTS);

	qb_loop_poll_add (poll_handle,
			QB_LOOP_MED,
			new_fd,
			POLLIN|POLLNVAL,
			NULL,
			server_process_data_fn);
	return 0;
}


static int create_server_sockect (int server_port)
{
	int listener;
	int yes = 1;
	int rv;
	struct addrinfo hints, *ai, *p;
	char server_port_str[16];
	char addr_str[INET_ADDRSTRLEN];
	void *ptr = NULL;

	/* get a socket and bind it
	 */
	sprintf(server_port_str, "%d", server_port);
	memset (&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	if ((rv = getaddrinfo (NULL, server_port_str, &hints, &ai)) != 0) {
		qb_log (LOG_ERR, "%s", gai_strerror (rv));
		exit (1);
	}

	for (p = ai; p != NULL; p = p->ai_next) {
		listener = socket (p->ai_family, p->ai_socktype, p->ai_protocol);
		if (listener < 0) {
			continue;
		}

		/* lose the pesky "address already in use" error message
		 */
		if (setsockopt (listener, SOL_SOCKET, SO_REUSEADDR,
				&yes, sizeof(int)) < 0) {
			qb_log (LOG_ERR, "setsockopt() failed: %s", strerror (errno));
		}

		switch (p->ai_family)
		{
		case AF_INET:
			ptr = &((struct sockaddr_in *) p->ai_addr)->sin_addr;
			break;
		case AF_INET6:
			ptr = &((struct sockaddr_in6 *) p->ai_addr)->sin6_addr;
			break;
		default:
			qb_log (LOG_ERR, "address family wrong");
			exit (4);
			
		}

		if (inet_ntop(p->ai_family, ptr, addr_str, INET_ADDRSTRLEN) == NULL) {
			qb_log (LOG_ERR, "inet_ntop() failed: %s", strerror (errno));
		}

		if (bind (listener, p->ai_addr, p->ai_addrlen) < 0) {
			qb_log (LOG_ERR, "bind(%s) failed: %s", addr_str, strerror (errno));
			close (listener);
			continue;
		}

		break;
	}

	if (p == NULL) {
		qb_log (LOG_ERR, "failed to bind");
		exit (2);
	}

	freeaddrinfo (ai);

	if (listen (listener, 10) == -1) {
		qb_log (LOG_ERR, "listen() failed: %s", strerror(errno));
		exit (3);
	}

	return listener;
}

static int32_t sig_exit_handler (int num, void *data)
{
	qb_log (LOG_INFO, "got signal %d, exiting", num);
	shut_me_down();
	return 0;
}

int
test_agent_run(const char * prog_name, int server_port,
		ta_do_command_fn func, pre_exit_fn exit_fn)
{
	int listener;
	int i;

	qb_log_init(prog_name, LOG_DAEMON, LOG_DEBUG);
	qb_log_format_set(QB_LOG_SYSLOG, "%n() [%p] %b");

	qb_log (LOG_INFO, "STARTING");

	do_command = func;
	pre_exit = exit_fn;
	poll_handle = qb_loop_create ();

	if (exit_fn) {
		qb_loop_signal_add(poll_handle, QB_LOOP_HIGH,
			SIGINT, NULL, sig_exit_handler, NULL);
		qb_loop_signal_add(poll_handle, QB_LOOP_HIGH,
			SIGQUIT, NULL, sig_exit_handler, NULL);
		qb_loop_signal_add(poll_handle, QB_LOOP_HIGH,
			SIGTERM, NULL, sig_exit_handler, NULL);
	}

	listener = create_server_sockect (server_port);
	qb_loop_poll_add (poll_handle,
			  QB_LOOP_MED,
			  listener,
			  POLLIN|POLLNVAL,
			  NULL, server_accept_fn);

	qb_loop_run (poll_handle);

	close(listener);

	for (i = 0; i < client_fds_pos; i++) {
		close(client_fds[client_fds_pos]);
	}

	qb_log (LOG_INFO, "EXITING");
	qb_log_fini();
	return 0;
}

