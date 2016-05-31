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
#include <signal.h>
#include <unistd.h>

#include "qnet-config.h"

#include "dynar.h"
#include "dynar-str.h"
#include "dynar-getopt-lex.h"
#include "nss-sock.h"
#include "pr-poll-array.h"
#include "qnetd-advanced-settings.h"
#include "qnetd-algorithm.h"
#include "qnetd-instance.h"
#include "qnetd-ipc.h"
#include "qnetd-log.h"
#include "qnetd-client-net.h"
#include "qnetd-client-msg-received.h"
#include "qnetd-poll-array-user-data.h"
#include "utils.h"
#include "msg.h"

/*
 * This is global variable used for comunication with main loop and signal (calls close)
 */
struct qnetd_instance *global_instance;

enum tlv_decision_algorithm_type
    qnetd_static_supported_decision_algorithms[QNETD_STATIC_SUPPORTED_DECISION_ALGORITHMS_SIZE] = {
	TLV_DECISION_ALGORITHM_TYPE_TEST,
	TLV_DECISION_ALGORITHM_TYPE_FFSPLIT,
	TLV_DECISION_ALGORITHM_TYPE_2NODELMS,
	TLV_DECISION_ALGORITHM_TYPE_LMS,
};

static void
qnetd_err_nss(void)
{

	qnetd_log_nss(LOG_CRIT, "NSS error");

	exit(1);
}

static void
qnetd_warn_nss(void)
{

	qnetd_log_nss(LOG_WARNING, "NSS warning");
}

static PRPollDesc *
qnetd_pr_poll_array_create(struct qnetd_instance *instance)
{
	struct pr_poll_array *poll_array;
	const struct qnetd_client_list *client_list;
	struct qnetd_client *client;
	PRPollDesc *poll_desc;
	struct qnetd_poll_array_user_data *user_data;
	const struct unix_socket_client_list *ipc_client_list;
	struct unix_socket_client *ipc_client;

	poll_array = &instance->poll_array;
	client_list = &instance->clients;
	ipc_client_list = &instance->local_ipc.clients;

	pr_poll_array_clean(poll_array);

	if (pr_poll_array_add(poll_array, &poll_desc, (void **)&user_data) < 0) {
		return (NULL);
	}

	poll_desc->fd = instance->server.socket;
	poll_desc->in_flags = PR_POLL_READ;

	user_data->type = QNETD_POLL_ARRAY_USER_DATA_TYPE_SOCKET;

	if (qnetd_ipc_is_closed(instance)) {
		qnetd_log(LOG_DEBUG, "Listening socket is closed");

		return (NULL);
	}

	if (pr_poll_array_add(poll_array, &poll_desc, (void **)&user_data) < 0) {
		return (NULL);
	}

	poll_desc->fd = instance->ipc_socket_poll_fd;
	poll_desc->in_flags = PR_POLL_READ;
	user_data->type = QNETD_POLL_ARRAY_USER_DATA_TYPE_IPC_SOCKET;

	TAILQ_FOREACH(client, client_list, entries) {
		if (pr_poll_array_add(poll_array, &poll_desc, (void **)&user_data) < 0) {
			return (NULL);
		}
		poll_desc->fd = client->socket;
		poll_desc->in_flags = PR_POLL_READ;

		if (!send_buffer_list_empty(&client->send_buffer_list)) {
			poll_desc->in_flags |= PR_POLL_WRITE;
		}

		user_data->type = QNETD_POLL_ARRAY_USER_DATA_TYPE_CLIENT;
		user_data->client = client;
	}

	TAILQ_FOREACH(ipc_client, ipc_client_list, entries) {
		if (!ipc_client->reading_line && !ipc_client->writing_buffer) {
			continue;
		}

		if (pr_poll_array_add(poll_array, &poll_desc, (void **)&user_data) < 0) {
			return (NULL);
		}

		poll_desc->fd = ((struct qnetd_ipc_user_data *)ipc_client->user_data)->nspr_poll_fd;
		if (ipc_client->reading_line) {
			poll_desc->in_flags |= PR_POLL_READ;
		}

		if (ipc_client->writing_buffer) {
			poll_desc->in_flags |= PR_POLL_WRITE;
		}

		user_data->type = QNETD_POLL_ARRAY_USER_DATA_TYPE_IPC_CLIENT;
		user_data->ipc_client = ipc_client;
	}

	pr_poll_array_gc(poll_array);

	return (poll_array->array);
}

static int
qnetd_poll(struct qnetd_instance *instance)
{
	struct qnetd_client *client;
	PRPollDesc *pfds;
	PRInt32 poll_res;
	ssize_t i;
	int client_disconnect;
	struct qnetd_poll_array_user_data *user_data;
	struct unix_socket_client *ipc_client;

	client = NULL;
	client_disconnect = 0;

	pfds = qnetd_pr_poll_array_create(instance);
	if (pfds == NULL) {
		return (-1);
	}

	if ((poll_res = PR_Poll(pfds, pr_poll_array_size(&instance->poll_array),
	    timer_list_time_to_expire(&instance->main_timer_list))) >= 0) {
		timer_list_expire(&instance->main_timer_list);

		/*
		 * Walk thru pfds array and process events
		 */
		for (i = 0; i < pr_poll_array_size(&instance->poll_array); i++) {
			user_data = pr_poll_array_get_user_data(&instance->poll_array, i);

			client = NULL;
			ipc_client = NULL;
			client_disconnect = 0;

			switch (user_data->type) {
			case QNETD_POLL_ARRAY_USER_DATA_TYPE_SOCKET:
				break;
			case QNETD_POLL_ARRAY_USER_DATA_TYPE_CLIENT:
				client = user_data->client;
				client_disconnect = client->schedule_disconnect;
				break;
			case QNETD_POLL_ARRAY_USER_DATA_TYPE_IPC_SOCKET:
				break;
			case QNETD_POLL_ARRAY_USER_DATA_TYPE_IPC_CLIENT:
				ipc_client = user_data->ipc_client;
				client_disconnect = ipc_client->schedule_disconnect;
			}

			if (!client_disconnect && poll_res > 0 &&
			    pfds[i].out_flags & PR_POLL_READ) {
				switch (user_data->type) {
				case QNETD_POLL_ARRAY_USER_DATA_TYPE_SOCKET:
					qnetd_client_net_accept(instance);
					break;
				case QNETD_POLL_ARRAY_USER_DATA_TYPE_CLIENT:
					if (qnetd_client_net_read(instance, client) == -1) {
						client_disconnect = 1;
					}
					break;
				case QNETD_POLL_ARRAY_USER_DATA_TYPE_IPC_SOCKET:
					qnetd_ipc_accept(instance, &ipc_client);
					break;
				case QNETD_POLL_ARRAY_USER_DATA_TYPE_IPC_CLIENT:
					qnetd_ipc_io_read(instance, ipc_client);
					break;
				}
			}

			if (!client_disconnect && poll_res > 0 &&
			    pfds[i].out_flags & PR_POLL_WRITE) {
				switch (user_data->type) {
				case QNETD_POLL_ARRAY_USER_DATA_TYPE_SOCKET:
					/*
					 * Poll write on listen socket -> fatal error
					 */
					qnetd_log(LOG_CRIT, "POLL_WRITE on listening socket");

					return (-1);
					break;
				case QNETD_POLL_ARRAY_USER_DATA_TYPE_CLIENT:
					if (qnetd_client_net_write(instance, client) == -1) {
						client_disconnect = 1;
					}
					break;
				case QNETD_POLL_ARRAY_USER_DATA_TYPE_IPC_SOCKET:
					qnetd_log(LOG_CRIT, "POLL_WRITE on listening IPC socket");
					return (-1);
					break;
				case QNETD_POLL_ARRAY_USER_DATA_TYPE_IPC_CLIENT:
					qnetd_ipc_io_write(instance, ipc_client);
					break;
				}
			}

			if (!client_disconnect && poll_res > 0 &&
			    (pfds[i].out_flags & (PR_POLL_ERR|PR_POLL_NVAL|PR_POLL_HUP|PR_POLL_EXCEPT)) &&
			    !(pfds[i].out_flags & (PR_POLL_READ|PR_POLL_WRITE))) {
				switch (user_data->type) {
				case QNETD_POLL_ARRAY_USER_DATA_TYPE_SOCKET:
				case QNETD_POLL_ARRAY_USER_DATA_TYPE_IPC_SOCKET:
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
					break;
				case QNETD_POLL_ARRAY_USER_DATA_TYPE_CLIENT:
					qnetd_log(LOG_DEBUG, "POLL_ERR (%u) on client socket. "
					    "Disconnecting.", pfds[i].out_flags);

					client_disconnect = 1;
					break;
				case QNETD_POLL_ARRAY_USER_DATA_TYPE_IPC_CLIENT:
					qnetd_log(LOG_DEBUG, "POLL_ERR (%u) on ipc client socket."
					    " Disconnecting.", pfds[i].out_flags);

					client_disconnect = 1;
					break;
				}
			}

			/*
			 * If client is scheduled for disconnect, disconnect it
			 */
			if (user_data->type == QNETD_POLL_ARRAY_USER_DATA_TYPE_CLIENT &&
			    client_disconnect) {
				qnetd_instance_client_disconnect(instance, client, 0);
			} else if (user_data->type == QNETD_POLL_ARRAY_USER_DATA_TYPE_IPC_CLIENT &&
			    (client_disconnect || ipc_client->schedule_disconnect)) {
				qnetd_ipc_client_disconnect(instance, ipc_client);
			}
		}
	}


	return (0);
}

static void
signal_int_handler(int sig)
{

	qnetd_log(LOG_DEBUG, "SIGINT received - closing server IPC socket");

	qnetd_ipc_close(global_instance);
}

static void
signal_term_handler(int sig)
{

	qnetd_log(LOG_DEBUG, "SIGTERM received - closing server IPC socket");

	qnetd_ipc_close(global_instance);
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

	printf("usage: %s [-46dfhv] [-l listen_addr] [-p listen_port] [-s tls]\n", QNETD_PROGRAM_NAME);
	printf("%14s[-c client_cert_required] [-m max_clients] [-S option=value[,option2=value2,...]]\n", "");
}

static void
display_version(void)
{
	enum msg_type *supported_messages;
	size_t no_supported_messages;
	size_t zi;

	msg_get_supported_messages(&supported_messages, &no_supported_messages);
	printf("Corosync Qdevice Network Daemon, version '%s'\n\n", VERSION);
	printf("Supported algorithms: ");
	for (zi = 0; zi < QNETD_STATIC_SUPPORTED_DECISION_ALGORITHMS_SIZE; zi++) {
		if (zi != 0) {
			printf(", ");
		}
		printf("%s (%u)",
		    tlv_decision_algorithm_type_to_str(qnetd_static_supported_decision_algorithms[zi]),
		    qnetd_static_supported_decision_algorithms[zi]);
	}
	printf("\n");
	printf("Supported message types: ");
	for (zi = 0; zi < no_supported_messages; zi++) {
		if (zi != 0) {
			printf(", ");
		}
		printf("%s (%u)", msg_type_to_str(supported_messages[zi]), supported_messages[zi]);
	}
	printf("\n");
}

static void
cli_parse_long_opt(struct qnetd_advanced_settings *advanced_settings, const char *long_opt)
{
	struct dynar_getopt_lex lex;
	struct dynar dynar_long_opt;
	const char *opt;
	const char *val;
	int res;

	dynar_init(&dynar_long_opt, strlen(long_opt) + 1);
	if (dynar_str_cpy(&dynar_long_opt, long_opt) != 0) {
		errx(1, "Can't alloc memory for long option");
	}

	dynar_getopt_lex_init(&lex, &dynar_long_opt);

	while (dynar_getopt_lex_token_next(&lex) == 0 && strcmp(dynar_data(&lex.option), "") != 0) {
		opt = dynar_data(&lex.option);
		val = dynar_data(&lex.value);

		res = qnetd_advanced_settings_set(advanced_settings, opt, val);
		switch (res) {
		case -1:
			errx(1, "Unknown option '%s'", opt);
			break;
		case -2:
			errx(1, "Invalid value '%s' for option '%s'", val, opt);
			break;
		}
	}

	dynar_getopt_lex_destroy(&lex);
	dynar_destroy(&dynar_long_opt);
}

static void
cli_parse(int argc, char * const argv[], char **host_addr, uint16_t *host_port, int *foreground,
    int *debug_log, int *bump_log_priority, enum tlv_tls_supported *tls_supported,
    int *client_cert_required, size_t *max_clients, PRIntn *address_family,
    struct qnetd_advanced_settings *advanced_settings)
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
	*address_family = PR_AF_UNSPEC;

	while ((ch = getopt(argc, argv, "46dfhvc:l:m:p:S:s:")) != -1) {
		switch (ch) {
		case '4':
			*address_family = PR_AF_INET;
			break;
		case '6':
			*address_family = PR_AF_INET6;
			break;
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
			free(*host_addr);
			*host_addr = strdup(optarg);
			if (*host_addr == NULL) {
				errx(1, "Can't alloc memory for host addr string");
			}
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
		case 'S':
			cli_parse_long_opt(advanced_settings, optarg);
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
		case 'v':
			display_version();
			exit(1);
			break;
		case 'h':
		case '?':
			usage();
			exit(1);
			break;
		}
	}
}

int
main(int argc, char * const argv[])
{
	struct qnetd_instance instance;
	struct qnetd_advanced_settings advanced_settings;
	char *host_addr;
	uint16_t host_port;
	int foreground;
	int debug_log;
	int bump_log_priority;
	enum tlv_tls_supported tls_supported;
	int client_cert_required;
	size_t max_clients;
	PRIntn address_family;
	int lock_file;
	int another_instance_running;

	if (qnetd_advanced_settings_init(&advanced_settings) != 0) {
		errx(1, "Can't alloc memory for advanced settings");
	}

	cli_parse(argc, argv, &host_addr, &host_port, &foreground, &debug_log, &bump_log_priority,
	    &tls_supported, &client_cert_required, &max_clients, &address_family, &advanced_settings);

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

	if ((lock_file = utils_flock(advanced_settings.lock_file, getpid(),
	    &another_instance_running)) == -1) {
		if (another_instance_running) {
			qnetd_log(LOG_ERR, "Another instance is running");
		} else {
			qnetd_log_err(LOG_ERR, "Can't acquire lock");
		}

		exit(1);
	}

	qnetd_log(LOG_DEBUG, "Initializing nss");
	if (nss_sock_init_nss((tls_supported != TLV_TLS_UNSUPPORTED ?
	    advanced_settings.nss_db_dir : NULL)) != 0) {
		qnetd_err_nss();
	}

	if (SSL_ConfigServerSessionIDCache(0, 0, 0, NULL) != SECSuccess) {
		qnetd_err_nss();
	}

	if (qnetd_instance_init(&instance, tls_supported, client_cert_required,
	    max_clients, &advanced_settings) == -1) {
		qnetd_log(LOG_ERR, "Can't initialize qnetd");
		exit(1);
	}
	instance.host_addr = host_addr;
	instance.host_port = host_port;

	if (tls_supported != TLV_TLS_UNSUPPORTED && qnetd_instance_init_certs(&instance) == -1) {
		qnetd_err_nss();
	}

	qnetd_log(LOG_DEBUG, "Initializing local socket");
	if (qnetd_ipc_init(&instance) != 0) {
		return (1);
	}

	qnetd_log(LOG_DEBUG, "Creating listening socket");
	instance.server.socket = nss_sock_create_listen_socket(instance.host_addr,
	    instance.host_port, address_family);
	if (instance.server.socket == NULL) {
		qnetd_err_nss();
	}

	if (nss_sock_set_non_blocking(instance.server.socket) != 0) {
		qnetd_err_nss();
	}

	if (PR_Listen(instance.server.socket, instance.advanced_settings->listen_backlog) !=
	    PR_SUCCESS) {
		qnetd_err_nss();
	}

	global_instance = &instance;
	signal_handlers_register();

	qnetd_log(LOG_DEBUG, "Registering algorithms");
	if (qnetd_algorithm_register_all() != 0) {
		exit(1);
	}

	qnetd_log(LOG_DEBUG, "QNetd ready to provide service");
	/*
	 * MAIN LOOP
	 */
	while (qnetd_poll(&instance) == 0) {
	}

	/*
	 * Cleanup
	 */
	qnetd_ipc_destroy(&instance);

	if (PR_Close(instance.server.socket) != PR_SUCCESS) {
		qnetd_warn_nss();
	}

	CERT_DestroyCertificate(instance.server.cert);
	SECKEY_DestroyPrivateKey(instance.server.private_key);

	SSL_ClearSessionCache();

	SSL_ShutdownServerSessionIDCache();

	qnetd_instance_destroy(&instance);

	qnetd_advanced_settings_destroy(&advanced_settings);

	if (NSS_Shutdown() != SECSuccess) {
		qnetd_warn_nss();
	}

	if (PR_Cleanup() != PR_SUCCESS) {
		qnetd_warn_nss();
	}

	qnetd_log_close();

	return (0);
}
