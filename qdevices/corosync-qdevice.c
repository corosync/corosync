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

#include <err.h>
#include <signal.h>

#include "dynar.h"
#include "dynar-str.h"
#include "dynar-getopt-lex.h"
#include "qdevice-advanced-settings.h"
#include "qdevice-config.h"
#include "qdevice-cmap.h"
#include "qdevice-heuristics.h"
#include "qdevice-ipc.h"
#include "qdevice-log.h"
#include "qdevice-model.h"
#include "qdevice-votequorum.h"
#include "utils.h"

struct qdevice_instance *global_instance;

static void
signal_int_handler(int sig)
{
	qdevice_log(LOG_DEBUG, "SIGINT received - closing local unix socket");
	qdevice_ipc_close(global_instance);
}

static void
signal_term_handler(int sig)
{
	qdevice_log(LOG_DEBUG, "SIGTERM received - closing server socket");
	qdevice_ipc_close(global_instance);
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

	act.sa_handler = SIG_DFL;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_RESTART;

	sigaction(SIGCHLD, &act, NULL);

	act.sa_handler = SIG_IGN;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_RESTART;

	sigaction(SIGPIPE, &act, NULL);
}

static void
usage(void)
{

	printf("usage: %s [-dfh] [-S option=value[,option2=value2,...]]\n", QDEVICE_PROGRAM_NAME);
}

static void
cli_parse_long_opt(struct qdevice_advanced_settings *advanced_settings, const char *long_opt)
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

		res = qdevice_advanced_settings_set(advanced_settings, opt, val);
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
cli_parse(int argc, char * const argv[], int *foreground, int *force_debug,
    struct qdevice_advanced_settings *advanced_settings)
{
	int ch;

	*foreground = 0;
	*force_debug = 0;

	while ((ch = getopt(argc, argv, "dfhS:")) != -1) {
		switch (ch) {
		case 'd':
			*force_debug = 1;
			break;
		case 'f':
			*foreground = 1;
			break;
		case 'S':
			cli_parse_long_opt(advanced_settings, optarg);
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
	struct qdevice_instance instance;
	struct qdevice_advanced_settings advanced_settings;
	int foreground;
	int force_debug;
	int lock_file;
	int another_instance_running;

	if (qdevice_advanced_settings_init(&advanced_settings) != 0) {
		errx(1, "Can't alloc memory for advanced settings");
	}

	cli_parse(argc, argv, &foreground, &force_debug, &advanced_settings);

	qdevice_instance_init(&instance, &advanced_settings);

	qdevice_heuristics_init(&instance.heuristics_instance, &advanced_settings);
	instance.heuristics_instance.qdevice_instance_ptr = &instance;

	qdevice_cmap_init(&instance);
	qdevice_log_init(&instance, force_debug);

	/*
	 * Daemonize
	 */
	if (!foreground) {
		utils_tty_detach();
	}

	if ((lock_file = utils_flock(advanced_settings.lock_file, getpid(),
	    &another_instance_running)) == -1) {
		if (another_instance_running) {
			qdevice_log(LOG_ERR, "Another instance is running");
		} else {
			qdevice_log_err(LOG_ERR, "Can't aquire lock");
		}

		exit(1);
	}

	qdevice_log(LOG_DEBUG, "Initializing votequorum");
	qdevice_votequorum_init(&instance);

	qdevice_log(LOG_DEBUG, "Initializing local socket");
	if (qdevice_ipc_init(&instance) != 0) {
		return (1);
	}

	qdevice_log(LOG_DEBUG, "Registering qdevice models");
	qdevice_model_register_all();

	qdevice_log(LOG_DEBUG, "Configuring qdevice");
	if (qdevice_instance_configure_from_cmap(&instance) != 0) {
		return (1);
	}

	qdevice_log(LOG_DEBUG, "Configuring master_wins");
	if (qdevice_votequorum_master_wins(&instance, (advanced_settings.master_wins ==
	    QDEVICE_ADVANCED_SETTINGS_MASTER_WINS_FORCE_ON ? 1 : 0)) != 0) {
		return (1);
	}

	qdevice_log(LOG_DEBUG, "Getting configuration node list");
	if (qdevice_cmap_store_config_node_list(&instance) != 0) {
		return (1);
	}

	qdevice_log(LOG_DEBUG, "Initializing qdevice model");
	if (qdevice_model_init(&instance) != 0) {
		return (1);
	}

	qdevice_log(LOG_DEBUG, "Initializing cmap tracking");
	if (qdevice_cmap_add_track(&instance) != 0) {
		return (1);
	}

	qdevice_log(LOG_DEBUG, "Waiting for ring id");
	if (qdevice_votequorum_wait_for_ring_id(&instance) != 0) {
		return (1);
	}

	qdevice_log(LOG_DEBUG, "Waiting for initial heuristics exec result");
	if (qdevice_heuristics_wait_for_initial_exec_result(&instance.heuristics_instance) != 0) {
		return (1);
	}

	global_instance = &instance;
	signal_handlers_register();

	qdevice_log(LOG_DEBUG, "Running qdevice model");
	if (qdevice_model_run(&instance) != 0) {
		return (1);
	}

	qdevice_log(LOG_DEBUG, "Removing cmap tracking");
	if (qdevice_cmap_del_track(&instance) != 0) {
		return (1);
	}

	qdevice_log(LOG_DEBUG, "Destroying qdevice model");
	qdevice_model_destroy(&instance);

	qdevice_log(LOG_DEBUG, "Destroying qdevice ipc");
	qdevice_ipc_destroy(&instance);

	qdevice_log(LOG_DEBUG, "Destroying votequorum and cmap");
	qdevice_votequorum_destroy(&instance);
	qdevice_cmap_destroy(&instance);

	qdevice_log(LOG_DEBUG, "Destroying heuristics");
	qdevice_heuristics_destroy(&instance.heuristics_instance);

	qdevice_log(LOG_DEBUG, "Closing log");
	qdevice_log_close(&instance);

	qdevice_instance_destroy(&instance);

	qdevice_advanced_settings_destroy(&advanced_settings);

	return (0);
}
