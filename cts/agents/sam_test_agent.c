/*
 * Copyright (c) 2009-2011 Red Hat, Inc.
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

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/un.h>
#include <corosync/corotypes.h>
#include <corosync/sam.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <corosync/cmap.h>

#include "common_test_agent.h"

extern const char *__progname;

static int test2_sig_delivered = 0;
static int test5_hc_cb_count = 0;
static int test6_sig_delivered = 0;

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

	qb_enter();
	error = sam_initialize (2000, SAM_RECOVERY_POLICY_RESTART);
	if (error != CS_OK) {
		qb_log (LOG_ERR, "Can't initialize SAM API. Error %d", error);
		return 1;
	}
	qb_log (LOG_INFO, "register");
	error = sam_register (&instance_id);
	if (error != CS_OK) {
		qb_log (LOG_ERR, "Can't register. Error %d", error);
		return 1;
	}

	if (instance_id == 1 || instance_id == 2) {
		qb_log (LOG_INFO, "iid %d: start", instance_id);
		error = sam_start ();
		if (error != CS_OK) {
			qb_log (LOG_ERR, "Can't start hc. Error %d", error);
			return 1;
		}

		for (i = 0; i < 10; i++) {
			qb_log (LOG_INFO, "iid %d: sleep 1", instance_id);
			sleep (1);

			qb_log (LOG_INFO, "iid %d: hc send", instance_id);
			error = sam_hc_send ();
			if (error != CS_OK) {
				qb_log (LOG_ERR, "Can't send hc. Error %d", error);
				return 1;
			}
		}

		if (instance_id == 2) {
			qb_log (LOG_INFO, "iid %d: stop", instance_id);
			error = sam_stop ();

			if (error != CS_OK) {
				qb_log (LOG_ERR, "Can't send hc. Error %d", error);
				return 1;
			}
		}

		qb_log (LOG_INFO, "iid %d: sleep 3", instance_id);
		sleep (3);

		qb_log (LOG_INFO, "iid %d: start", instance_id);
		error = sam_start ();
		if (error != CS_OK) {
			qb_log (LOG_ERR, "Can't start hc. Error %d", error);
			return 1;
		}

		qb_log (LOG_INFO, "iid %d: sleep 3", instance_id);
		sleep (3);
		return 0;
	}

	if (instance_id == 3) {
		error = sam_initialize (2000, SAM_RECOVERY_POLICY_RESTART);
		if (error == CS_OK) {
			qb_log (LOG_ERR, "Can initialize SAM API after initialization");
			return 1;
		}

		error = sam_start ();
		if (error != CS_OK) {
			qb_log (LOG_ERR, "Can't start hc. Error %d", error);
			return 1;
		}
		error = sam_stop ();
		if (error != CS_OK) {
			qb_log (LOG_ERR, "Can't stop hc. Error %d", error);
			return 1;
		}
		error = sam_finalize ();
		if (error != CS_OK) {
			qb_log (LOG_ERR, "Can't finalize sam. Error %d", error);
			return 1;
		}
		error = sam_finalize ();
		if (error == CS_OK) {
			qb_log (LOG_ERR, "Can finalize sam after finalization!");
			return 1;
		}

		if (sam_initialize (2, SAM_RECOVERY_POLICY_RESTART) == CS_OK ||
			sam_start () == CS_OK || sam_stop () == CS_OK ||
			sam_register (NULL) == CS_OK || sam_hc_send () == CS_OK ||
			sam_hc_callback_register (NULL) == CS_OK) {

			qb_log (LOG_ERR, "Can call one of function after finalization!");

			return 1;
		}

		return 0;
	}

	return 1;
}


static void test2_signal (int sig)
{
	qb_enter();

	test2_sig_delivered = 1;
}

/*
 * This tests recovery policy quit and callback.
 */
static int test2 (void) {
	cs_error_t error;
	unsigned int instance_id;

	qb_enter();
	error = sam_initialize (2000, SAM_RECOVERY_POLICY_QUIT);
	if (error != CS_OK) {
		qb_log (LOG_ERR, "Can't initialize SAM API. Error %d", error);
		return 1;
	}
	qb_log (LOG_INFO, "register");
	error = sam_register (&instance_id);
	if (error != CS_OK) {
		qb_log (LOG_ERR, "Can't register. Error %d", error);
		return 1;
	}

	if (instance_id == 1) {
		signal (SIGTERM, test2_signal);

		qb_log (LOG_INFO, "iid %d: start", instance_id);
		error = sam_start ();
		if (error != CS_OK) {
			qb_log (LOG_ERR, "Can't start hc. Error %d", error);
			return 1;
		}

		qb_log (LOG_INFO, "iid %d: sleep 1", instance_id);
		sleep (1);

		qb_log (LOG_INFO, "iid %d: hc send", instance_id);
		error = sam_hc_send ();
		if (error != CS_OK) {
			qb_log (LOG_ERR, "Can't send hc. Error %d", error);
			return 1;
		}


		qb_log (LOG_INFO, "iid %d: wait for delivery of signal", instance_id);
		while (!test2_sig_delivered) {
			sleep (1);
		}

		qb_log (LOG_INFO, "iid %d: wait for real kill", instance_id);

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

	qb_log (LOG_INFO, "initialize");
	error = sam_initialize (0, SAM_RECOVERY_POLICY_RESTART);
	if (error != CS_OK) {
		qb_log (LOG_ERR, "Can't initialize SAM API. Error %d", error);
		return 1;
	}
	qb_log (LOG_INFO, "register");
	error = sam_register (&instance_id);
	if (error != CS_OK) {
		qb_log (LOG_ERR, "Can't register. Error %d", error);
		return 1;
	}

	if (instance_id < 100) {
		qb_log (LOG_INFO, "iid %d: start", instance_id);
		error = sam_start ();
		if (error != CS_OK) {
			qb_log (LOG_ERR, "Can't start hc. Error %d", error);
			return 1;
		}

		qb_log (LOG_INFO, "iid %d: Sending signal", instance_id);
		kill(getpid(), SIGSEGV);
		return 1;
	}

	return 0;

}

/*
 * Test sam_data_store, sam_data_restore and sam_data_getsize
 */
static int test4 (void)
{
	size_t size;
	cs_error_t err;
	int i;
	unsigned int instance_id;
	char saved_data[128];
	char saved_data2[128];

	qb_log (LOG_INFO, "sam_data_getsize 1");
	err = sam_data_getsize (&size);
	if (err != CS_ERR_BAD_HANDLE) {
		qb_log (LOG_ERR, "Test should return CS_ERR_BAD_HANDLE. Error returned %d", err);
		return 1;
	}

	qb_log (LOG_INFO, "sam_data_getsize 2");
	err = sam_data_getsize (NULL);
	if (err != CS_ERR_INVALID_PARAM) {
		qb_log (LOG_ERR, "Test should return CS_ERR_INVALID_PARAM. Error returned %d", err);
		return 1;
	}

	qb_log (LOG_INFO, "sam_data_store 1");
	err = sam_data_store (NULL, 0);
	if (err != CS_ERR_BAD_HANDLE) {
		qb_log (LOG_ERR, "Test should return CS_ERR_BAD_HANDLE. Error returned %d", err);
		return 1;
	}

	qb_log (LOG_INFO, "sam_data_restore 1");
	err = sam_data_restore (saved_data, sizeof (saved_data));
	if (err != CS_ERR_BAD_HANDLE) {
		qb_log (LOG_ERR, "Test should return CS_ERR_BAD_HANDLE. Error returned %d", err);
		return 1;
	}

	qb_log (LOG_INFO, "sam_initialize");
	err = sam_initialize (0, SAM_RECOVERY_POLICY_RESTART);
	if (err != CS_OK) {
		qb_log (LOG_ERR, "Can't initialize SAM API. Error %d", err);
		return 1;
	}

	qb_log (LOG_INFO, "sam_data_getsize 3");
	err = sam_data_getsize (&size);
	if (err != CS_OK) {
		qb_log (LOG_ERR, "Test should return CS_ERR_BAD_HANDLE. Error returned %d", err);
		return 1;
	}
	if (size != 0) {
		qb_log (LOG_ERR, "Test should return size of 0. Returned %zx", size);
		return 1;
	}

	qb_log (LOG_INFO, "sam_data_restore 2");
	err = sam_data_restore (NULL, sizeof (saved_data));
	if (err != CS_ERR_INVALID_PARAM) {
		qb_log (LOG_ERR, "Test should return CS_ERR_INVALID_PARAM. Error returned %d", err);
		return 1;
	}

	/*
	 * Store some real data
	 */
	for (i = 0; i < sizeof (saved_data); i++) {
		saved_data[i] = (char)(i + 5);
	}

	qb_log (LOG_INFO, "sam_data_store 2");
	err = sam_data_store (saved_data, sizeof (saved_data));
	if (err != CS_OK) {
		qb_log (LOG_ERR, "Test should return CS_OK. Error returned %d", err);
		return 1;
	}

	qb_log (LOG_INFO, " sam_data_getsize 4");
	err = sam_data_getsize (&size);
	if (err != CS_OK) {
		qb_log (LOG_ERR, "Test should return CS_OK. Error returned %d", err);
		return 1;
	}
	if (size != sizeof (saved_data)) {
		qb_log (LOG_ERR, "Test should return size of 0. Returned %zx", size);
		return 1;
	}

	qb_log (LOG_INFO, " sam_data_restore 3");
	err = sam_data_restore (saved_data2, sizeof (saved_data2) - 1);
	if (err != CS_ERR_INVALID_PARAM) {
		qb_log (LOG_ERR, "Test should return CS_ERR_INVALID_PARAM. Error returned %d", err);
		return 1;
	}

	qb_log (LOG_INFO, " sam_data_restore 4");
	err = sam_data_restore (saved_data2, sizeof (saved_data2));
	if (err != CS_OK) {
		qb_log (LOG_ERR, "Test should return CS_OK. Error returned %d", err);
		return 1;
	}

	if (memcmp (saved_data, saved_data2, sizeof (saved_data2)) != 0) {
		qb_log (LOG_ERR, "Retored data are not same");
		return 1;
	}

	memset (saved_data2, 0, sizeof (saved_data2));

	qb_log (LOG_INFO, " sam_data_store 3");
	err = sam_data_store (NULL, 1);
	if (err != CS_OK) {
		qb_log (LOG_ERR, "Test should return CS_OK. Error returned %d", err);
		return 1;
	}

	qb_log (LOG_INFO, " sam_data_getsize 5");
	err = sam_data_getsize (&size);
	if (err != CS_OK) {
		qb_log (LOG_ERR, "Test should return CS_OK. Error returned %d", err);
		return 1;
	}
	if (size != 0) {
		qb_log (LOG_ERR, "Test should return size of 0. Returned %zx", size);
		return 1;
	}

	qb_log (LOG_INFO, " sam_data_store 4");
	err = sam_data_store (saved_data, sizeof (saved_data));
	if (err != CS_OK) {
		qb_log (LOG_ERR, "Test should return CS_OK. Error returned %d", err);
		return 1;
	}

	qb_log (LOG_INFO, " register");
	err = sam_register (&instance_id);
	if (err != CS_OK) {
		qb_log (LOG_ERR, "Can't register. Error %d", err);
		return 1;
	}

	if (instance_id == 1) {
		qb_log (LOG_INFO, "iid %d: sam_start", instance_id);
		err = sam_start ();
		if (err != CS_OK) {
			qb_log (LOG_ERR, "Can't start hc. Error %d", err);
			return 1;
		}

		qb_log (LOG_INFO, "iid %d: sam_data_getsize 6", instance_id);
		err = sam_data_getsize (&size);
		if (err != CS_OK) {
			qb_log (LOG_ERR, "Test should return CS_OK. Error returned %d", err);
			return 1;
		}
		if (size != sizeof (saved_data2)) {
			qb_log (LOG_ERR, "Test should return size of 0. Returned %zx", size);
			return 1;
		}

		qb_log (LOG_INFO, "iid %d: sam_data_restore 5", instance_id);
		err = sam_data_restore (saved_data2, sizeof (saved_data2));
		if (err != CS_OK) {
			qb_log (LOG_ERR, "Test should return CS_OK. Error returned %d", err);
			return 1;
		}

		if (memcmp (saved_data, saved_data2, sizeof (saved_data2)) != 0) {
			qb_log (LOG_ERR, "Retored data are not same");
			return 1;
		}

		for (i = 0; i < sizeof (saved_data); i++) {
			saved_data[i] = (char)(i - 5);
		}

		qb_log (LOG_INFO, "iid %d: sam_data_store 5", instance_id);
		err = sam_data_store (saved_data, sizeof (saved_data) - 7);
		if (err != CS_OK) {
			qb_log (LOG_ERR, "Test should return CS_OK. Error returned %d", err);
			return 1;
		}

		exit (1);
	}

	if (instance_id == 2) {
		qb_log (LOG_INFO, "iid %d: sam_start", instance_id);
		err = sam_start ();
		if (err != CS_OK) {
			qb_log (LOG_ERR, "Can't start hc. Error %d", err);
			return 1;
		}

		qb_log (LOG_INFO, "iid %d: sam_data_getsize 7", instance_id);
		err = sam_data_getsize (&size);
		if (err != CS_OK) {
			qb_log (LOG_ERR, "Test should return CS_OK. Error returned %d", err);
			return 1;
		}
		if (size != sizeof (saved_data2) - 7) {
			qb_log (LOG_ERR, "Test should return size of 0. Returned %zx", size);
			return 1;
		}

		qb_log (LOG_INFO, "iid %d: sam_data_restore 6", instance_id);
		err = sam_data_restore (saved_data2, sizeof (saved_data2));
		if (err != CS_OK) {
			qb_log (LOG_ERR, "Test should return CS_OK. Error returned %d", err);
			return 1;
		}

		for (i = 0; i < sizeof (saved_data); i++) {
			saved_data[i] = (char)(i - 5);
		}

		if (memcmp (saved_data, saved_data2, sizeof (saved_data2) - 7) != 0) {
			qb_log (LOG_ERR, "Retored data are not same");
			return 1;
		}

		qb_log (LOG_INFO, "iid %d: sam_data_store 6", instance_id);
		err = sam_data_store (NULL, 0);
		if (err != CS_OK) {
			qb_log (LOG_ERR, "Test should return CS_OK. Error returned %d", err);
			return 1;
		}

		exit (1);
	}

	if (instance_id == 3) {
		qb_log (LOG_INFO, "iid %d: sam_data_getsize 8", instance_id);
		err = sam_data_getsize (&size);
		if (err != CS_OK) {
			qb_log (LOG_ERR, "Test should return CS_OK. Error returned %d", err);
			return 1;
		}
		if (size != 0) {
			qb_log (LOG_ERR, "Test should return size of 0. Returned %zx", size);
			return 1;
		}
	}

	return (0);
}

static int test5_hc_cb (void)
{
	cs_error_t res;

	qb_log (LOG_INFO, "%d", ++test5_hc_cb_count);

	res = sam_data_store (&test5_hc_cb_count, sizeof (test5_hc_cb_count));

	if (res != CS_OK)
		return 1;

	if (test5_hc_cb_count > 10)
		return 1;

	return 0;
}
/*
 * Test event driven healtchecking.
 */
static int test5 (void)
{
	cs_error_t error;
	unsigned int instance_id;
	int hc_cb_count;

	qb_log (LOG_INFO, " initialize");
	error = sam_initialize (100, SAM_RECOVERY_POLICY_RESTART);
	if (error != CS_OK) {
		qb_log (LOG_ERR, "Can't initialize SAM API. Error %d", error);
		return 1;
	}
	qb_log (LOG_INFO, " register");
	error = sam_register (&instance_id);
	if (error != CS_OK) {
		qb_log (LOG_ERR, "Can't register. Error %d", error);
		return 1;
	}

	if (instance_id == 1) {
		qb_log (LOG_INFO, "iid %d: hc callback register", instance_id);
		error = sam_hc_callback_register (test5_hc_cb);
		if (error != CS_OK) {
			qb_log (LOG_ERR, "Can't register hc cb. Error %d", error);
			return 1;
		}


		qb_log (LOG_INFO, "iid %d: start", instance_id);
		error = sam_start ();
		if (error != CS_OK) {
			qb_log (LOG_ERR, "Can't start hc. Error %d", error);
			return 1;
		}

		sleep (2);

		qb_log (LOG_INFO, "iid %d: Failed. Wasn't killed.", instance_id);
		return 1;
	}

	if (instance_id == 2) {
		error = sam_data_restore (&hc_cb_count, sizeof (hc_cb_count));
		if (error != CS_OK) {
			qb_log (LOG_ERR, "sam_data_restore should return CS_OK. Error returned %d", error);
			return 1;
		}

		if (hc_cb_count != 11) {
			qb_log (LOG_ERR, "iid %d: Premature killed. hc_cb_count should be 11 and it is %d",
				__FUNCTION__, instance_id - 1, hc_cb_count);
			return 1;

		}
		return 0;
	}

	return 1;
}

static void test6_signal (int sig) {
	cs_error_t error;

	qb_enter();
	test6_sig_delivered++;

	if ((error = sam_data_store (&test6_sig_delivered, sizeof (test6_sig_delivered))) != CS_OK) {
		qb_log (LOG_ERR, "Can't store data! Error : %d", error);
	}
}

/*
 * Test warn signal set.
 */
static int test6 (void) {
	cs_error_t error;
	unsigned int instance_id;
	int test6_sig_del;

	qb_log (LOG_INFO, " initialize");
	error = sam_initialize (2000, SAM_RECOVERY_POLICY_RESTART);
	if (error != CS_OK) {
		qb_log (LOG_ERR, "Can't initialize SAM API. Error %d", error);
		return 1;
	}
	qb_log (LOG_INFO, " register");
	error = sam_register (&instance_id);
	if (error != CS_OK) {
		qb_log (LOG_ERR, "Can't register. Error %d", error);
		return 1;
	}

	if (instance_id == 1) {
		error = sam_warn_signal_set (SIGUSR1);
		if (error != CS_OK) {
			qb_log (LOG_ERR, "Can't set warn signal. Error %d", error);
			return 1;
		}

		signal (SIGUSR1, test6_signal);

		qb_log (LOG_INFO, " iid %d: start", instance_id);
		error = sam_start ();
		if (error != CS_OK) {
			qb_log (LOG_ERR, "Can't start hc. Error %d", error);
			return 1;
		}

		qb_log (LOG_INFO, "iid %d: sleep 1", instance_id);
		sleep (1);

		qb_log (LOG_INFO, "iid %d: hc send", instance_id);
		error = sam_hc_send ();
		if (error != CS_OK) {
			qb_log (LOG_ERR, "Can't send hc. Error %d", error);
			return 1;
		}


		qb_log (LOG_INFO, "iid %d: wait for delivery of signal", instance_id);
		while (!test6_sig_delivered) {
			sleep (1);
		}

		qb_log (LOG_INFO, "iid %d: wait for real kill", instance_id);

		sleep (3);

		qb_log (LOG_INFO, "iid %d: wasn't killed", instance_id);
		return (1);
	}

	if (instance_id == 2) {
		error = sam_data_restore (&test6_sig_del, sizeof (test6_sig_del));
		if (error != CS_OK) {
			qb_log (LOG_ERR, "Can't restore data. Error %d", error);
			return 1;
		}

		if (test6_sig_del != 1) {
			qb_log (LOG_ERR, "Previous test failed. Signal was not delivered");
			return 1;
		}

		error = sam_warn_signal_set (SIGKILL);
		if (error != CS_OK) {
			qb_log (LOG_ERR, "Can't set warn signal. Error %d", error);
			return 1;
		}

		signal (SIGUSR1, test6_signal);

		qb_log (LOG_INFO, "iid %d: start", instance_id);
		error = sam_start ();
		if (error != CS_OK) {
			qb_log (LOG_ERR, "Can't start hc. Error %d", error);
			return 1;
		}

		qb_log (LOG_INFO, "iid %d: sleep 1", instance_id);
		sleep (1);

		qb_log (LOG_INFO, "iid %d: hc send", instance_id);
		error = sam_hc_send ();
		if (error != CS_OK) {
			qb_log (LOG_ERR, "Can't send hc. Error %d", error);
			return 1;
		}


		qb_log (LOG_INFO, "iid %d: wait for delivery of signal", instance_id);
		while (!test6_sig_delivered) {
			sleep (1);
		}

		qb_log (LOG_INFO, "iid %d: wasn't killed", instance_id);
		return (1);
	}

	if (instance_id == 3) {
		error = sam_data_restore (&test6_sig_del, sizeof (test6_sig_del));
		if (error != CS_OK) {
			qb_log (LOG_ERR, "Can't restore data. Error %d", error);
			return 1;
		}

		if (test6_sig_del != 1) {
			qb_log (LOG_ERR, "Previous test failed. Signal WAS delivered");
			return 1;
		}

		return (0);
	}

	return 1;
}

/*
 * Test cmap integration + quit policy
 */
static int test8 (pid_t pid, pid_t old_pid, int test_n) {
	cmap_handle_t cmap_handle;
	cs_error_t err;
	uint64_t tstamp1, tstamp2;
	int32_t msec_diff;
	unsigned int instance_id;
	char key_name[CMAP_KEYNAME_MAXLEN];
	char *str;

	err = cmap_initialize (&cmap_handle);
	if (err != CS_OK) {
		qb_log (LOG_INFO, "Could not initialize Cluster Map API instance error %d. Test skipped", err);
		return (1);
	}

	qb_log (LOG_INFO, "test %d", test_n);

	if (test_n == 2) {
		/*
		 * Object should not exist
		 */
		qb_log (LOG_INFO, "Testing if object exists (it shouldn't)");

		snprintf(key_name, CMAP_KEYNAME_MAXLEN, "resources.process.%d.state", pid);
		err = cmap_get_string(cmap_handle, key_name, &str);
		if (err == CS_OK) {
			qb_log (LOG_INFO, "Could find key \"%s\": %d.", key_name, err);
			free(str);
			return (2);
		}
	}

	if (test_n == 1 || test_n == 2) {
		qb_log (LOG_INFO, " initialize");
		err = sam_initialize (2000, SAM_RECOVERY_POLICY_QUIT | SAM_RECOVERY_POLICY_CMAP);
		if (err != CS_OK) {
			qb_log (LOG_ERR, "Can't initialize SAM API. Error %d", err);
			return 2;
		}

		qb_log (LOG_INFO, " register");
		err = sam_register (&instance_id);
		if (err != CS_OK) {
			qb_log (LOG_ERR, "Can't register. Error %d", err);
			return 2;
		}

		snprintf(key_name, CMAP_KEYNAME_MAXLEN, "resources.process.%d.recovery", pid);
		err = cmap_get_string(cmap_handle, key_name, &str);
		if (err != CS_OK) {
			qb_log (LOG_INFO, "Could not get \"recovery\" key: %d.", err);
			return (2);
		}

		if (strcmp(str, "quit") != 0) {
			qb_log (LOG_INFO, "Recovery key \"%s\" is not \"quit\".", key_name);
			free(str);
			return (2);
		}
		free(str);

		snprintf(key_name, CMAP_KEYNAME_MAXLEN, "resources.process.%d.state", pid);
		err = cmap_get_string(cmap_handle, key_name, &str);
		if (err != CS_OK) {
			qb_log (LOG_INFO, "Could not get \"state\" key: %d.", err);
			return (2);
		}

		if (strcmp(str, "stopped") != 0) {
			qb_log (LOG_INFO, "State key is not \"stopped\".");
			free(str);
			return (2);
		}
		free(str);

		qb_log (LOG_INFO, "iid %d: start", instance_id);
		err = sam_start ();
		if (err != CS_OK) {
			qb_log (LOG_ERR, "Can't start hc. Error %d", err);
			return 2;
		}

		err = cmap_get_string(cmap_handle, key_name, &str);
		if (err != CS_OK) {
			qb_log (LOG_INFO, "Could not get \"state\" key: %d.", err);
			return (2);
		}

		if (strcmp(str, "running") != 0) {
			qb_log (LOG_INFO, "State key is not \"running\".");
			free(str);
			return (2);
		}
		free(str);

		qb_log (LOG_INFO, "iid %d: stop", instance_id);
		err = sam_stop ();
		if (err != CS_OK) {
			qb_log (LOG_ERR, "Can't stop hc. Error %d", err);
			return 2;
		}

		err = cmap_get_string(cmap_handle, key_name, &str);
		if (err != CS_OK) {
			qb_log (LOG_INFO, "Could not get \"state\" key: %d.", err);
			return (2);
		}

		if (strcmp(str, "stopped") != 0) {
			qb_log (LOG_INFO, "State key is not \"stopped\".");
			free(str);
			return (2);
		}
		free(str);

		qb_log (LOG_INFO, "iid %d: sleeping 5", instance_id);
		sleep (5);

		err = cmap_get_string(cmap_handle, key_name, &str);
		if (err != CS_OK) {
			qb_log (LOG_INFO, "Could not get \"state\" key: %d.", err);
			return (2);
		}

		if (strcmp(str, "stopped") != 0) {
			qb_log (LOG_INFO, "State key is not \"stopped\".");
			free(str);
			return (2);
		}
		free(str);

		qb_log (LOG_INFO, "iid %d: start 2", instance_id);
		err = sam_start ();
		if (err != CS_OK) {
			qb_log (LOG_ERR, "Can't start hc. Error %d", err);
			return 2;
		}

		err = cmap_get_string(cmap_handle, key_name, &str);
		if (err != CS_OK) {
			qb_log (LOG_INFO, "Could not get \"state\" key: %d.", err);
			return (2);
		}

		if (strcmp(str, "running") != 0) {
			qb_log (LOG_INFO, "State key is not \"running\".");
			free(str);
			return (2);
		}
		free(str);

		if (test_n == 2) {
			qb_log (LOG_INFO, "iid %d: sleeping 5. Should be killed", instance_id);
			sleep (5);

			return (2);
		} else {
			qb_log (LOG_INFO, "iid %d: Test HC", instance_id);
			err = sam_hc_send ();
			if (err != CS_OK) {
				qb_log (LOG_ERR, "Can't send hc. Error %d", err);
				return 2;
			}

			snprintf(key_name, CMAP_KEYNAME_MAXLEN, "resources.process.%d.last_updated", pid);
			err = cmap_get_uint64(cmap_handle, key_name, &tstamp1);
			if (err != CS_OK) {
				qb_log (LOG_INFO, "Could not get \"last_updated\" key: %d.", err);
				return (2);
			}
			qb_log (LOG_INFO, "iid %d: Sleep 1", instance_id);
			sleep (1);
			err = sam_hc_send ();
			if (err != CS_OK) {
				qb_log (LOG_ERR, "Can't send hc. Error %d", err);
				return 2;
			}
			sleep (1);
			err = cmap_get_uint64(cmap_handle, key_name, &tstamp2);
			if (err != CS_OK) {
				qb_log (LOG_INFO, "Could not get \"last_updated\" key: %d.", err);
				return (2);
			}
			msec_diff = (tstamp2 - tstamp1)/CS_TIME_NS_IN_MSEC;

			if (msec_diff < 500 || msec_diff > 2000) {
				qb_log (LOG_INFO, "Difference %d is not within <500, 2000> interval.", msec_diff);
				return (2);
			}

			qb_log (LOG_INFO, "iid %d: stop 2", instance_id);
			err = sam_stop ();
			if (err != CS_OK) {
				qb_log (LOG_ERR, "Can't stop hc. Error %d", err);
				return 2;
			}

			snprintf(key_name, CMAP_KEYNAME_MAXLEN, "resources.process.%d.state", pid);
			err = cmap_get_string(cmap_handle, key_name, &str);
			if (err != CS_OK) {
				qb_log (LOG_INFO, "Could not get \"state\" key: %d.", err);
				return (2);
			}

			if (strcmp(str, "stopped") != 0) {
				qb_log (LOG_INFO, "State key is not \"stopped\".");
				free(str);
				return (2);
			}
			free(str);

			qb_log (LOG_INFO, "iid %d: exiting", instance_id);
			return (0);
		}
	}

	if (test_n == 3) {
		qb_log (LOG_INFO, "Testing if status is failed");

		/*
		 * Previous should be FAILED
		 */

		snprintf(key_name, CMAP_KEYNAME_MAXLEN, "resources.process.%d.state", pid);
		err = cmap_get_string(cmap_handle, key_name, &str);
		if (err != CS_OK) {
			qb_log (LOG_INFO, "Could not get \"state\" key: %d.", err);
			return (2);
		}

		if (strcmp(str, "failed") != 0) {
			qb_log (LOG_INFO, "State key is not \"failed\".");
			free(str);
			return (2);
		}
		free(str);

		return (0);
	}

	return (2);
}

/*
 * Test cmap integration + restart policy
 */
static int test9 (pid_t pid, pid_t old_pid, int test_n) {
	cs_error_t err;
	cmap_handle_t cmap_handle;
	unsigned int instance_id;
	char *str;
	char key_name[CMAP_KEYNAME_MAXLEN];

	err = cmap_initialize (&cmap_handle);
	if (err != CS_OK) {
		qb_log (LOG_INFO, "Could not initialize Cluster Map API instance error %d. Test skipped", err);
		return (1);
	}

	qb_log (LOG_INFO, "test %d", test_n);

	if (test_n == 1) {
		qb_log (LOG_INFO, " initialize");
		err = sam_initialize (2000, SAM_RECOVERY_POLICY_RESTART | SAM_RECOVERY_POLICY_CMAP);
		if (err != CS_OK) {
			qb_log (LOG_ERR, "Can't initialize SAM API. Error %d", err);
			return 2;
		}

		qb_log (LOG_INFO, " register");
		err = sam_register (&instance_id);
		if (err != CS_OK) {
			qb_log (LOG_ERR, "Can't register. Error %d", err);
			return 2;
		}
		qb_log (LOG_INFO, " iid %d", instance_id);

		if (instance_id < 3) {
			snprintf(key_name, CMAP_KEYNAME_MAXLEN, "resources.process.%d.recovery", pid);
			err = cmap_get_string(cmap_handle, key_name, &str);
			if (err != CS_OK) {
				qb_log (LOG_INFO, "Could not get \"recovery\" key: %d.", err);
				return (2);
			}

			if (strcmp(str, "restart") != 0) {
				qb_log (LOG_INFO, "Recovery key \"%s\" is not \"restart\".", str);
				free(str);
				return (2);
			}
			free(str);

			snprintf(key_name, CMAP_KEYNAME_MAXLEN, "resources.process.%d.state", pid);
			err = cmap_get_string(cmap_handle, key_name, &str);
			if (err != CS_OK) {
				qb_log (LOG_INFO, "Could not get \"state\" key: %d.", err);
				return (2);
			}

			if (strcmp(str, "stopped") != 0) {
				qb_log (LOG_INFO, "State key is not \"stopped\".");
				free(str);
				return (2);
			}
			free(str);

			qb_log (LOG_INFO, "iid %d: start", instance_id);
			err = sam_start ();
			if (err != CS_OK) {
				qb_log (LOG_ERR, "Can't start hc. Error %d", err);
				return 2;
			}

			err = cmap_get_string(cmap_handle, key_name, &str);
			if (err != CS_OK) {
				qb_log (LOG_INFO, "Could not get \"state\" key: %d.", err);
				return (2);
			}

			if (strcmp(str, "running") != 0) {
				qb_log (LOG_INFO, "State key is not \"running\".");
				free(str);
				return (2);
			}
			free(str);

			qb_log (LOG_INFO, "iid %d: waiting for kill", instance_id);
			sleep (10);

			return (2);
		}

		if (instance_id == 3) {
			qb_log (LOG_INFO, "iid %d: mark failed", instance_id);
			err = sam_mark_failed ();
			if (err != CS_OK) {
				qb_log (LOG_ERR, "Can't mark failed. Error %d", err);
				return 2;
			}

			sleep (10);

			return (2);
		}

		return (2);
	}

	if (test_n == 2) {
		qb_log (LOG_INFO, "Testing if status is failed");

		/*
		 * Previous should be FAILED
		 */
		snprintf(key_name, CMAP_KEYNAME_MAXLEN, "resources.process.%d.state", pid);
		err = cmap_get_string(cmap_handle, key_name, &str);
		if (err != CS_OK) {
			qb_log (LOG_INFO, "Could not get \"state\" key: %d.", err);
			return (2);
		}

		if (strcmp(str, "failed") != 0) {
			qb_log (LOG_INFO, "State key is not \"failed\".");
			free(str);
			return (2);
		}
		free(str);

		return (0);
	}

	return (2);
}

static int hc_allways_respond_cb(void)
{
	qb_log (LOG_INFO, "health check OK.");
	return 0;
}

static int setup_hc (void)
{
	cs_error_t err;
	unsigned int instance_id;

	err = sam_initialize (1000, SAM_RECOVERY_POLICY_QUIT | SAM_RECOVERY_POLICY_CMAP);
	if (err != CS_OK) {
		qb_log (LOG_ERR, "Can't initialize SAM API. Error %d", err);
		return 2;
	}

	qb_log (LOG_INFO, " register");
	err = sam_register (&instance_id);
	if (err != CS_OK) {
		qb_log (LOG_ERR, "Can't register. Error %d", err);
		return 2;
	}
	err = sam_hc_callback_register (hc_allways_respond_cb);

	qb_log (LOG_INFO, "instance id %d: start", instance_id);
	err = sam_start ();
	if (err != CS_OK) {
		qb_log (LOG_ERR, "Can't start hc. Error %d", err);
		return 2;
	}

	return (0);
}

static int do_test_command(int sock, char* func, char*args[], int num_args)
{
	int err = 0;
	pid_t pid;
	int stat;

	pid = fork ();

	if (pid == -1) {
		qb_log (LOG_ERR, "Can't fork");
		return -1;
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
		} else if (strcmp ("test5", func) == 0) {
			err = test5 ();
		} else if (strcmp ("test6", func) == 0) {
			err = test6 ();
		} else if (strcmp ("test8", func) == 0) {
			err = test8 (getpid(), 0, 1);
		} else if (strcmp ("test9", func) == 0) {
			err = test9 (getpid(), 0, 1);
		}
		sam_finalize ();
		exit(err);
	}

	if (pid > 0) {
		waitpid (pid, &stat, 0);

		return WEXITSTATUS (stat);
	}
	return -1;
}


static void do_command (int sock, char* func, char*args[], int num_args)
{
	char response[100];
	int err = 0;
	ssize_t rc;
	size_t send_len;

	qb_log (LOG_INFO, "RPC:%s() called.", func);
	if (strncmp ("test", func, 4) == 0) {
		err = do_test_command(sock, func, args, num_args);
	} else if (strcmp ("setup_hc", func) == 0) {
		err = setup_hc ();
	} else if (strcmp ("sam_stop", func) == 0) {
		err = sam_stop ();
		if (err != CS_OK) {
			err = -1;
			qb_log (LOG_ERR,"RPC:%s sam_stop failed!", func);
			snprintf (response, 100, "%s", FAIL_STR);
		}
		err = sam_finalize();
	} else {
		err = -1;
		qb_log (LOG_ERR,"RPC:%s not supported!", func);
		snprintf (response, 100, "%s", NOT_SUPPORTED_STR);
	}

	if (err == 0) {
		snprintf (response, 100, "%s", OK_STR);
	} else if (err == 1) {
		snprintf (response, 100, "%s", FAIL_STR);
		qb_log (LOG_ERR, "%s() test skipped?! (%d).", func, err);
	} else {
		snprintf (response, 100, "%s", FAIL_STR);
		qb_log (LOG_ERR, "%s() failed (%d).", func, err);
	}
	send_len = strlen (response);
	rc = send (sock, response, send_len, 0);
	assert(rc == send_len);
}

int
main (int argc, char *argv[])
{
	return test_agent_run ("sam_test_agent", 9036, do_command, NULL);
}
