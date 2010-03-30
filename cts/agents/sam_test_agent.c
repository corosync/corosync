/*
 * Copyright (c) 2009 Red Hat, Inc.
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

/*
 * Provides test of SAM API
 */

#include <config.h>

#include <sys/types.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <syslog.h>

#include <corosync/corotypes.h>
#include <corosync/sam.h>
#include "common_test_agent.h"


static int test2_sig_delivered = 0;
static int test4_hc_cb_count = 0;

/*
 * First test will just register SAM, with policy restart. First instance will
 * sleep one second, send hc and sleep another 3 seconds. This should force restart.
 * Second instance will sleep one second, send hc, stop hc and sleep 3 seconds.
 * Then start hc again and sleep 3 seconds. This should force restart again.
 * Last instance just calls initialize again. This should end with error.
 * Then call start, followed by stop and start again. Finally, we will call finalize
 * twice. One should succeed, second should fail. After this, we will call every function
 * (none should succeed).
 */
static int test1 (void)
{
	cs_error_t error;
	unsigned int instance_id;
	int i;

	syslog (LOG_INFO,"%s: initialize\n", __FUNCTION__);
	error = sam_initialize (2000, SAM_RECOVERY_POLICY_RESTART);
	if (error != CS_OK) {
		syslog (LOG_ERR, "Can't initialize SAM API. Error %d\n", error);
		return 1;
	}
	syslog (LOG_INFO,"%s: register\n", __FUNCTION__);
	error = sam_register (&instance_id);
	if (error != CS_OK) {
		syslog (LOG_ERR, "Can't register. Error %d\n", error);
		return 1;
	}

	if (instance_id == 1 || instance_id == 2) {
		syslog (LOG_INFO,"%s iid %d: start\n", __FUNCTION__, instance_id);
		error = sam_start ();
		if (error != CS_OK) {
			syslog (LOG_ERR, "Can't start hc. Error %d\n", error);
			return 1;
		}

		for (i = 0; i < 10; i++) {
			syslog (LOG_INFO,"%s iid %d: sleep 1\n", __FUNCTION__, instance_id);
			sleep (1);

			syslog (LOG_INFO,"%s iid %d: hc send\n", __FUNCTION__, instance_id);
			error = sam_hc_send ();
			if (error != CS_OK) {
				syslog (LOG_ERR, "Can't send hc. Error %d\n", error);
				return 1;
			}
		}

		if (instance_id == 2) {
			syslog (LOG_INFO,"%s iid %d: stop\n", __FUNCTION__, instance_id);
			error = sam_stop ();

			if (error != CS_OK) {
				syslog (LOG_ERR, "Can't send hc. Error %d\n", error);
				return 1;
			}
		}

		syslog (LOG_INFO,"%s iid %d: sleep 3\n", __FUNCTION__, instance_id);
		sleep (3);

		syslog (LOG_INFO,"%s iid %d: start\n", __FUNCTION__, instance_id);
		error = sam_start ();
		if (error != CS_OK) {
			syslog (LOG_ERR, "Can't start hc. Error %d\n", error);
			return 1;
		}

		syslog (LOG_INFO,"%s iid %d: sleep 3\n", __FUNCTION__, instance_id);
		sleep (3);
		return 0;
	}

	if (instance_id == 3) {
		error = sam_initialize (2000, SAM_RECOVERY_POLICY_RESTART);
		if (error == CS_OK) {
			syslog (LOG_ERR, "Can initialize SAM API after initialization");
			return 1;
		}

		error = sam_start ();
		if (error != CS_OK) {
			syslog (LOG_ERR, "Can't start hc. Error %d\n", error);
			return 1;
		}
		error = sam_stop ();
		if (error != CS_OK) {
			syslog (LOG_ERR, "Can't stop hc. Error %d\n", error);
			return 1;
		}
		error = sam_finalize ();
		if (error != CS_OK) {
			syslog (LOG_ERR, "Can't finalize sam. Error %d\n", error);
			return 1;
		}
		error = sam_finalize ();
		if (error == CS_OK) {
			syslog (LOG_ERR, "Can finalize sam after finalization!\n");
			return 1;
		}

		if (sam_initialize (2, SAM_RECOVERY_POLICY_RESTART) == CS_OK ||
			sam_start () == CS_OK || sam_stop () == CS_OK ||
			sam_register (NULL) == CS_OK || sam_hc_send () == CS_OK ||
			sam_hc_callback_register (NULL) == CS_OK) {

			syslog (LOG_ERR, "Can call one of function after finalization!\n");

			return 1;
		}

		return 0;
	}

	return 1;
}


static void test2_signal (int sig) {
	syslog (LOG_INFO,"%s\n", __FUNCTION__);

	test2_sig_delivered = 1;
}

/*
 * This tests recovery policy quit and callback.
 */
static int test2 (void) {
	cs_error_t error;
	unsigned int instance_id;

	syslog (LOG_INFO,"%s: initialize\n", __FUNCTION__);
	error = sam_initialize (2000, SAM_RECOVERY_POLICY_QUIT);
	if (error != CS_OK) {
		syslog (LOG_ERR, "Can't initialize SAM API. Error %d\n", error);
		return 1;
	}
	syslog (LOG_INFO,"%s: register\n", __FUNCTION__);
	error = sam_register (&instance_id);
	if (error != CS_OK) {
		syslog (LOG_ERR, "Can't register. Error %d\n", error);
		return 1;
	}

	if (instance_id == 1) {
		signal (SIGTERM, test2_signal);

		syslog (LOG_INFO,"%s iid %d: start\n", __FUNCTION__, instance_id);
		error = sam_start ();
		if (error != CS_OK) {
			syslog (LOG_ERR, "Can't start hc. Error %d\n", error);
			return 1;
		}

		syslog (LOG_INFO,"%s iid %d: sleep 1\n", __FUNCTION__, instance_id);
		sleep (1);

		syslog (LOG_INFO,"%s iid %d: hc send\n", __FUNCTION__, instance_id);
		error = sam_hc_send ();
		if (error != CS_OK) {
			syslog (LOG_ERR, "Can't send hc. Error %d\n", error);
			return 1;
		}


		syslog (LOG_INFO,"%s iid %d: wait for delivery of signal\n", __FUNCTION__, instance_id);
		while (!test2_sig_delivered) {
			sleep (1);
		}

		syslog (LOG_INFO,"%s iid %d: wait for real kill\n", __FUNCTION__, instance_id);

		sleep (3);
	}

	return 1;

}

/*
 * Smoke test. Better to turn off coredump ;) This has no time limit, just restart process
 * when it dies.
 */
static int test3 (void) {
	cs_error_t error;
	unsigned int instance_id;
	int tmp1, tmp2, tmp3;

	syslog (LOG_INFO,"%s: initialize\n", __FUNCTION__);
	error = sam_initialize (0, SAM_RECOVERY_POLICY_RESTART);
	if (error != CS_OK) {
		syslog (LOG_ERR, "Can't initialize SAM API. Error %d\n", error);
		return 1;
	}
	syslog (LOG_INFO,"%s: register\n", __FUNCTION__);
	error = sam_register (&instance_id);
	if (error != CS_OK) {
		syslog (LOG_ERR, "Can't register. Error %d\n", error);
		return 1;
	}

	if (instance_id < 100) {
		syslog (LOG_INFO,"%s iid %d: start\n", __FUNCTION__, instance_id);
		error = sam_start ();
		if (error != CS_OK) {
			syslog (LOG_ERR, "Can't start hc. Error %d\n", error);
			return 1;
		}

		syslog (LOG_INFO,"%s iid %d: divide by zero\n", __FUNCTION__, instance_id);
		tmp2 = rand ();
		tmp3 = 0;
		tmp1 = tmp2 / tmp3;

		return 1;
	}

	return 0;

}

static int test4_hc_cb (void)
{
	syslog (LOG_INFO,"%s %d\n", __FUNCTION__, ++test4_hc_cb_count);

	if (test4_hc_cb_count > 10)
		return 1;

	return 0;
}
/*
 * Test event driven healtchecking.
 */
static int test4 (void)
{
	cs_error_t error;
	unsigned int instance_id;

	syslog (LOG_INFO,"%s: initialize\n", __FUNCTION__);
	error = sam_initialize (100, SAM_RECOVERY_POLICY_RESTART);
	if (error != CS_OK) {
		syslog (LOG_ERR, "Can't initialize SAM API. Error %d\n", error);
		return 1;
	}
	syslog (LOG_INFO,"%s: register\n", __FUNCTION__);
	error = sam_register (&instance_id);
	if (error != CS_OK) {
		syslog (LOG_ERR, "Can't register. Error %d\n", error);
		return 1;
	}

	if (instance_id == 1) {
		syslog (LOG_INFO,"%s iid %d: hc callback register\n", __FUNCTION__, instance_id);
		error = sam_hc_callback_register (test4_hc_cb);
		if (error != CS_OK) {
			syslog (LOG_ERR, "Can't register hc cb. Error %d\n", error);
			return 1;
		}


		syslog (LOG_INFO,"%s iid %d: start\n", __FUNCTION__, instance_id);
		error = sam_start ();
		if (error != CS_OK) {
			syslog (LOG_ERR, "Can't start hc. Error %d\n", error);
			return 1;
		}

		sleep (2);

		syslog (LOG_INFO,"%s iid %d: Failed. Wasn't killed.\n", __FUNCTION__, instance_id);
		return 1;
	}

	if (instance_id == 2) {
		return 0;
	}

	return 1;
}

static void do_command (int sock, char* func, char*args[], int num_args)
{
	char response[100];
	pid_t pid;
	int err;
	int stat;
	int please_wait = 1;

	snprintf (response, 100, "%s", FAIL_STR);

	if (parse_debug)
		syslog (LOG_INFO,"RPC:%s() called.", func);

	pid = fork ();

	if (pid == -1) {
		syslog (LOG_ERR, "Can't fork\n");
		send (sock, response, strlen (response) + 1, 0);
		return;
	}

	if (pid == 0) {
		if (strcmp ("test1", func) == 0) {
			err = test1 ();
		} else if (strcmp ("test2", func) == 0) {
			err = test2 ();
		} else if (strcmp ("test3", func) == 0) {
			err = test3 ();
		} else if (strcmp ("test4", func) == 0) {
			err = test4 ();
		} else {
			err = -1;
			please_wait = 0;
			syslog (LOG_ERR,"%s RPC:%s not supported!", __func__, func);
			snprintf (response, 100, "%s", NOT_SUPPORTED_STR);
		}
	}

	if (please_wait) {
		waitpid (pid, &stat, 0);

		if (WEXITSTATUS (stat) == 0) {
			snprintf (response, 100, "%s", OK_STR);
		} else {
			snprintf (response, 100, "%s", FAIL_STR);
		}
	}
	send (sock, response, strlen (response) + 1, 0);
}


int main (int argc, char *argv[])
{
	int ret;

	openlog (NULL, LOG_CONS|LOG_PID, LOG_DAEMON);
	syslog (LOG_ERR, "sam_test_agent STARTING");

	parse_debug = 1;
	ret = test_agent_run (9036, do_command);
	syslog (LOG_ERR, "sam_test_agent EXITING");

	return ret;
}









