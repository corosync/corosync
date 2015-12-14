/*
 * Copyright (c) 2015 Red Hat, Inc.
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

#include <config.h>

#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <unistd.h>

#include "nss-sock.h"
#include "qnetd-algorithm.h"
#include "qnetd-instance.h"
#include "qnetd-log.h"
#include "qnetd-client-net.h"
#include "qnetd-client-msg-received.h"
#include "utils.h"

/*
 * This is global variable used for comunication with main loop and signal (calls close)
 */
PRFileDesc *global_server_socket;

enum tlv_decision_algorithm_type
    qnetd_static_supported_decision_algorithms[QNETD_STATIC_SUPPORTED_DECISION_ALGORITHMS_SIZE] = {
	TLV_DECISION_ALGORITHM_TYPE_TEST,
	TLV_DECISION_ALGORITHM_TYPE_FFSPLIT,
	TLV_DECISION_ALGORITHM_TYPE_2NODELMS,
	TLV_DECISION_ALGORITHM_TYPE_LMS,
};

static void
qnetd_err_nss(void) {

	qnetd_log_nss(LOG_CRIT, "NSS error");

	exit(1);
}

static void
qnetd_warn_nss(void) {

	qnetd_log_nss(LOG_WARNING, "NSS warning");
}

static int
qnetd_poll(struct qnetd_instance *instance)
{
	struct qnetd_client *client;
	struct qnetd_client *client_next;
	PRPollDesc *pfds;
	PRInt32 poll_res;
	int i;
	int client_disconnect;

	client = NULL;
	client_disconnect = 0;

	pfds = qnetd_poll_array_create_from_client_list(&instance->poll_array,
	    &instance->clients, instance->server.socket, PR_POLL_READ);

	if (pfds == NULL) {
		return (-1);
	}

	if ((poll_res = PR_Poll(pfds, qnetd_poll_array_size(&instance->poll_array),
	    PR_INTERVAL_NO_TIMEOUT)) > 0) {
		/*
		 * Walk thru pfds array and process events
		 */
		for (i = 0; i < qnetd_poll_array_size(&instance->poll_array); i++) {
			/*
			 * Also traverse clients list
			 */
			if (i > 0) {
				if (i == 1) {
					client = TAILQ_FIRST(&instance->clients);
					client_next = TAILQ_NEXT(client, entries);
				} else {
					client = client_next;
					client_next = TAILQ_NEXT(client, entries);
				}
			}

			client_disconnect = 0;

			if (!client_disconnect && pfds[i].out_flags & PR_POLL_READ) {
				if (i == 0) {
					qnetd_client_net_accept(instance);
				} else {
					if (qnetd_client_net_read(instance, client) == -1) {
						client_disconnect = 1;
					}
				}
			}

			if (!client_disconnect && pfds[i].out_flags & PR_POLL_WRITE) {
				if (i == 0) {
					/*
					 * Poll write on listen socket -> fatal error
					 */
					qnetd_log(LOG_CRIT, "POLL_WRITE on listening socket");

					return (-1);
				} else {
					if (qnetd_client_net_write(instance, client) == -1) {
						client_disconnect = 1;
					}
				}
			}

			if (!client_disconnect &&
			    pfds[i].out_flags &
			    (PR_POLL_ERR|PR_POLL_NVAL|PR_POLL_HUP|PR_POLL_EXCEPT)) {
				if (i == 0) {
					if (pfds[i].out_flags != PR_POLL_NVAL) {
						/*
						 * Poll ERR on listening socket is fatal error.
						 * POLL_NVAL is used as a signal to quit poll loop.
						 */
						 qnetd_log(LOG_CRIT, "POLL_ERR (%u) on listening "
						    "socket", pfds[i].out_flags);
					} else {
						qnetd_log(LOG_DEBUG, "Listening socket is closed");
					}

					return (-1);

				} else {
					qnetd_log(LOG_DEBUG, "POLL_ERR (%u) on client socket. "
					    "Disconnecting.", pfds[i].out_flags);

					client_disconnect = 1;
				}
			}

			/*
			 * If client is scheduled for disconnect, disconnect it
			 */
			if (client_disconnect) {
				qnetd_instance_client_disconnect(instance, client, 0);
			}
		}
	}

	return (0);
}

static void
signal_int_handler(int sig)
{

	qnetd_log(LOG_DEBUG, "SIGINT received - closing server socket");

	PR_Close(global_server_socket);
}

static void
signal_term_handler(int sig)
{

	qnetd_log(LOG_DEBUG, "SIGTERM received - closing server socket");

	PR_Close(global_server_socket);
}

static void
signal_handlers_register(void)
{
	struct sigaction act;

	act.sa_handler = signal_int_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_RESTART;

	sigaction(SIGINT, &act, NULL);

	act.sa_handler = signal_term_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_RESTART;

	sigaction(SIGTERM, &act, NULL);
}

static void
usage(void)
{

	printf("usage: %s [-df] [-l listen_addr] [-p listen_port] [-s tls]\n", QNETD_PROGRAM_NAME);
	printf("%14s[-c client_cert_required] [-m max_clients]\n", "");
}

static void
cli_parse(int argc, char * const argv[], char **host_addr, uint16_t *host_port, int *foreground,
    int *debug_log, int *bump_log_priority, enum tlv_tls_supported *tls_supported,
    int *client_cert_required, size_t *max_clients)
{
	int ch;
	char *ep;
	long long int tmpll;

	*host_addr = NULL;
	*host_port = QNETD_DEFAULT_HOST_PORT;
	*foreground = 0;
	*debug_log = 0;
	*bump_log_priority = 0;
	*tls_supported = QNETD_DEFAULT_TLS_SUPPORTED;
	*client_cert_required = QNETD_DEFAULT_TLS_CLIENT_CERT_REQUIRED;
	*max_clients = QNETD_DEFAULT_MAX_CLIENTS;

	while ((ch = getopt(argc, argv, "fdc:l:m:p:s:")) != -1) {
		switch (ch) {
		case 'f':
			*foreground = 1;
			break;
		case 'd':
			if (*debug_log) {
				*bump_log_priority = 1;
			}
			*debug_log = 1;
			break;
		case 'c':
			if ((*client_cert_required = utils_parse_bool_str(optarg)) == -1) {
				errx(1, "client_cert_required should be on/yes/1, off/no/0");
			}
			break;
		case 'l':
			*host_addr = strdup(optarg);
			break;
		case 'm':
			errno = 0;

			tmpll = strtoll(optarg, &ep, 10);
			if (tmpll < 0 || errno != 0 || *ep != '\0') {
				errx(1, "max clients value %s is invalid", optarg);
			}
			*max_clients = (size_t)tmpll;
			break;
		case 'p':
			*host_port = strtol(optarg, &ep, 10);
			if (*host_port <= 0 || *host_port > ((uint16_t)~0) || *ep != '\0') {
				errx(1, "host port must be in range 0-65535");
			}
			break;
		case 's':
			if (strcasecmp(optarg, "on") == 0) {
				*tls_supported = QNETD_DEFAULT_TLS_SUPPORTED;
			} else if (strcasecmp(optarg, "off") == 0) {
				*tls_supported = TLV_TLS_UNSUPPORTED;
			} else if (strcasecmp(optarg, "req") == 0) {
				*tls_supported = TLV_TLS_REQUIRED;
			} else {
				errx(1, "tls must be one of on, off, req");
			}
			break;
		case '?':
			usage();
			exit(1);
			break;
		}
	}
}

int
main(int argc, char *argv[])
{
	struct qnetd_instance instance;
	char *host_addr;
	uint16_t host_port;
	int foreground;
	int debug_log;
	int bump_log_priority;
	enum tlv_tls_supported tls_supported;
	int client_cert_required;
	size_t max_clients;

	cli_parse(argc, argv, &host_addr, &host_port, &foreground, &debug_log, &bump_log_priority,
	    &tls_supported, &client_cert_required, &max_clients);

	if (foreground) {
		qnetd_log_init(QNETD_LOG_TARGET_STDERR);
	} else {
		qnetd_log_init(QNETD_LOG_TARGET_SYSLOG);
	}

	qnetd_log_set_debug(debug_log);
	qnetd_log_set_priority_bump(bump_log_priority);

	/*
	 * Daemonize
	 */
	if (!foreground) {
		utils_tty_detach();
	}

	if (utils_flock(QNETD_LOCK_FILE, getpid(), qnetd_log_printf) != 0) {
		exit(1);
	}

	qnetd_log_printf(LOG_DEBUG, "Initializing nss");
	if (nss_sock_init_nss((tls_supported != TLV_TLS_UNSUPPORTED ?
	    (char *)QNETD_NSS_DB_DIR : NULL)) != 0) {
		qnetd_err_nss();
	}

	if (SSL_ConfigServerSessionIDCache(0, 0, 0, NULL) != SECSuccess) {
		qnetd_err_nss();
	}

	if (qnetd_instance_init(&instance, QNETD_MAX_CLIENT_RECEIVE_SIZE,
	    QNETD_MAX_CLIENT_SEND_BUFFERS, QNETD_MAX_CLIENT_SEND_SIZE,
	    tls_supported, client_cert_required, max_clients) == -1) {
		qnetd_log(LOG_ERR, "Can't initialize qnetd");
		exit(1);
	}
	instance.host_addr = host_addr;
	instance.host_port = host_port;

	if (qnetd_instance_init_certs(&instance) == -1) {
		qnetd_err_nss();
	}

	qnetd_log_printf(LOG_DEBUG, "Creating listening socket");
	instance.server.socket = nss_sock_create_listen_socket(instance.host_addr,
	    instance.host_port, PR_AF_INET6);
	if (instance.server.socket == NULL) {
		qnetd_err_nss();
	}

	if (nss_sock_set_nonblocking(instance.server.socket) != 0) {
		qnetd_err_nss();
	}

	if (PR_Listen(instance.server.socket, QNETD_LISTEN_BACKLOG) != PR_SUCCESS) {
		qnetd_err_nss();
	}

	global_server_socket = instance.server.socket;
	signal_handlers_register();

	qnetd_log_printf(LOG_DEBUG, "Registering algorithms");
	algorithms_register();

	qnetd_log_printf(LOG_DEBUG, "QNetd ready to provide service");
	/*
	 * MAIN LOOP
	 */
	while (qnetd_poll(&instance) == 0) {
	}

	/*
	 * Cleanup
	 */
	CERT_DestroyCertificate(instance.server.cert);
	SECKEY_DestroyPrivateKey(instance.server.private_key);

	SSL_ClearSessionCache();

	SSL_ShutdownServerSessionIDCache();

	qnetd_instance_destroy(&instance);

	if (NSS_Shutdown() != SECSuccess) {
		qnetd_warn_nss();
	}

	if (PR_Cleanup() != PR_SUCCESS) {
		qnetd_warn_nss();
	}

	qnetd_log_close();

	return (0);
}
