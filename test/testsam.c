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
#include <corosync/corotypes.h>
#include <corosync/sam.h>
#include <signal.h>
#include <sys/wait.h>

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

	printf ("%s: initialize\n", __FUNCTION__);
	error = sam_initialize (2000, SAM_RECOVERY_POLICY_RESTART);
	if (error != CS_OK) {
		fprintf (stderr, "Can't initialize SAM API. Error %d\n", error);
		return 1;
	}
	printf ("%s: register\n", __FUNCTION__);
	error = sam_register (&instance_id);
	if (error != CS_OK) {
		fprintf (stderr, "Can't register. Error %d\n", error);
		return 1;
	}

	if (instance_id == 1 || instance_id == 2) {
		printf ("%s iid %d: start\n", __FUNCTION__, instance_id);
		error = sam_start ();
		if (error != CS_OK) {
			fprintf (stderr, "Can't start hc. Error %d\n", error);
			return 1;
		}

		for (i = 0; i < 10; i++) {
			printf ("%s iid %d: sleep 1\n", __FUNCTION__, instance_id);
			sleep (1);

			printf ("%s iid %d: hc send\n", __FUNCTION__, instance_id);
			error = sam_hc_send ();
			if (error != CS_OK) {
				fprintf (stderr, "Can't send hc. Error %d\n", error);
				return 1;
			}
		}

		if (instance_id == 2) {
			printf ("%s iid %d: stop\n", __FUNCTION__, instance_id);
			error = sam_stop ();

			if (error != CS_OK) {
				fprintf (stderr, "Can't send hc. Error %d\n", error);
				return 1;
			}
		}

		printf ("%s iid %d: sleep 3\n", __FUNCTION__, instance_id);
		sleep (3);

		printf ("%s iid %d: start\n", __FUNCTION__, instance_id);
		error = sam_start ();
		if (error != CS_OK) {
			fprintf (stderr, "Can't start hc. Error %d\n", error);
			return 1;
		}

		printf ("%s iid %d: sleep 3\n", __FUNCTION__, instance_id);
		sleep (3);
		return 0;
	}

	if (instance_id == 3) {
		error = sam_initialize (2000, SAM_RECOVERY_POLICY_RESTART);
		if (error == CS_OK) {
			fprintf (stderr, "Can initialize SAM API after initialization");
			return 1;
		}

		error = sam_start ();
		if (error != CS_OK) {
			fprintf (stderr, "Can't start hc. Error %d\n", error);
			return 1;
		}
		error = sam_stop ();
		if (error != CS_OK) {
			fprintf (stderr, "Can't stop hc. Error %d\n", error);
			return 1;
		}
		error = sam_finalize ();
		if (error != CS_OK) {
			fprintf (stderr, "Can't finalize sam. Error %d\n", error);
			return 1;
		}
		error = sam_finalize ();
		if (error == CS_OK) {
			fprintf (stderr, "Can finalize sam after finalization!\n");
			return 1;
		}

		if (sam_initialize (2, SAM_RECOVERY_POLICY_RESTART) == CS_OK ||
			sam_start () == CS_OK || sam_stop () == CS_OK ||
			sam_register (NULL) == CS_OK || sam_hc_send () == CS_OK ||
			sam_hc_callback_register (NULL) == CS_OK) {

			fprintf (stderr, "Can call one of function after finalization!\n");

			return 1;
		}

		return 0;
	}

	return 1;
}


static void test2_signal (int sig) {
	printf ("%s\n", __FUNCTION__);

	test2_sig_delivered = 1;
}

/*
 * This tests recovery policy quit and callback.
 */
static int test2 (void) {
	cs_error_t error;
	unsigned int instance_id;

	printf ("%s: initialize\n", __FUNCTION__);
	error = sam_initialize (2000, SAM_RECOVERY_POLICY_QUIT);
	if (error != CS_OK) {
		fprintf (stderr, "Can't initialize SAM API. Error %d\n", error);
		return 1;
	}
	printf ("%s: register\n", __FUNCTION__);
	error = sam_register (&instance_id);
	if (error != CS_OK) {
		fprintf (stderr, "Can't register. Error %d\n", error);
		return 1;
	}

	if (instance_id == 1) {
		signal (SIGTERM, test2_signal);

		printf ("%s iid %d: start\n", __FUNCTION__, instance_id);
		error = sam_start ();
		if (error != CS_OK) {
			fprintf (stderr, "Can't start hc. Error %d\n", error);
			return 1;
		}

		printf ("%s iid %d: sleep 1\n", __FUNCTION__, instance_id);
		sleep (1);

		printf ("%s iid %d: hc send\n", __FUNCTION__, instance_id);
		error = sam_hc_send ();
		if (error != CS_OK) {
			fprintf (stderr, "Can't send hc. Error %d\n", error);
			return 1;
		}


		printf ("%s iid %d: wait for delivery of signal\n", __FUNCTION__, instance_id);
		while (!test2_sig_delivered) {
			sleep (1);
		}

		printf ("%s iid %d: wait for real kill\n", __FUNCTION__, instance_id);

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

	printf ("%s: initialize\n", __FUNCTION__);
	error = sam_initialize (0, SAM_RECOVERY_POLICY_RESTART);
	if (error != CS_OK) {
		fprintf (stderr, "Can't initialize SAM API. Error %d\n", error);
		return 1;
	}
	printf ("%s: register\n", __FUNCTION__);
	error = sam_register (&instance_id);
	if (error != CS_OK) {
		fprintf (stderr, "Can't register. Error %d\n", error);
		return 1;
	}

	if (instance_id < 100) {
		printf ("%s iid %d: start\n", __FUNCTION__, instance_id);
		error = sam_start ();
		if (error != CS_OK) {
			fprintf (stderr, "Can't start hc. Error %d\n", error);
			return 1;
		}

		printf ("%s iid %d: divide by zero\n", __FUNCTION__, instance_id);
		tmp2 = rand ();
		tmp3 = 0;
		tmp1 = tmp2 / tmp3;

		return 1;
	}

	return 0;

}

static int test4_hc_cb (void)
{
	printf ("%s %d\n", __FUNCTION__, ++test4_hc_cb_count);

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

	printf ("%s: initialize\n", __FUNCTION__);
	error = sam_initialize (100, SAM_RECOVERY_POLICY_RESTART);
	if (error != CS_OK) {
		fprintf (stderr, "Can't initialize SAM API. Error %d\n", error);
		return 1;
	}
	printf ("%s: register\n", __FUNCTION__);
	error = sam_register (&instance_id);
	if (error != CS_OK) {
		fprintf (stderr, "Can't register. Error %d\n", error);
		return 1;
	}

	if (instance_id == 1) {
		printf ("%s iid %d: hc callback register\n", __FUNCTION__, instance_id);
		error = sam_hc_callback_register (test4_hc_cb);
		if (error != CS_OK) {
			fprintf (stderr, "Can't register hc cb. Error %d\n", error);
			return 1;
		}


		printf ("%s iid %d: start\n", __FUNCTION__, instance_id);
		error = sam_start ();
		if (error != CS_OK) {
			fprintf (stderr, "Can't start hc. Error %d\n", error);
			return 1;
		}

		sleep (2);

		printf ("%s iid %d: Failed. Wasn't killed.\n", __FUNCTION__, instance_id);
		return 1;
	}

	if (instance_id == 2) {
		return 0;
	}

	return 1;
}

int main(int argc, char *argv[])
{
	pid_t pid;
	int err;
	int stat;
	int all_passed = 1;

	pid = fork ();

	if (pid == -1) {
		fprintf (stderr, "Can't fork\n");
		return 1;
	}

	if (pid == 0) {
		err = test1 ();

		fprintf (stderr, "test1 %s\n", (err == 0 ? "passed" : "failed"));
		if (err != 0)
			all_passed = 0;

		return err;
	}

	waitpid (pid, NULL, 0);


	pid = fork ();

	if (pid == -1) {
		fprintf (stderr, "Can't fork\n");
		return 1;
	}

	if (pid == 0) {
		err = test2 ();

		return err;
	}

	waitpid (pid, &stat, 0);

	fprintf (stderr, "test2 %s\n", (WEXITSTATUS (stat) == 0 ? "passed" : "failed"));
	if (WEXITSTATUS (stat) != 0)
		all_passed = 0;

	pid = fork ();

	if (pid == -1) {
		fprintf (stderr, "Can't fork\n");
		return 1;
	}

	if (pid == 0) {
		err = test3 ();

		fprintf (stderr, "test3 %s\n", (err == 0 ? "passed" : "failed"));
		if (err != 0)
			all_passed = 0;
		return err;
	}

	waitpid (pid, NULL, 0);

	pid = fork ();

	if (pid == -1) {
		fprintf (stderr, "Can't fork\n");
		return 1;
	}

	if (pid == 0) {
		err = test4 ();

		fprintf (stderr, "test4 %s\n", (err == 0 ? "passed" : "failed"));
		if (err != 0)
			all_passed = 0;
		return err;
	}

	waitpid (pid, NULL, 0);

	if (all_passed)
		fprintf (stderr, "All tests passed\n");

	return (all_passed ? 0 : 1);
}
