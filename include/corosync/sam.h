/*
 * Copyright (c) 2009-2010 Red Hat, Inc.
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
#ifndef COROSYNC_SAM_H_DEFINED
#define COROSYNC_SAM_H_DEFINED

#include <corosync/corotypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	SAM_RECOVERY_POLICY_QUIT = 1,
	SAM_RECOVERY_POLICY_RESTART = 2,
} sam_recovery_policy_t;

/*
 * Callback definition for event driven checking
 */
typedef int (*sam_hc_callback_t)(void);

/*
 * Create a new SAM connection. This function must be called before any other.
 * It is recommended to call it as one of first in application.
 *
 * @param time_interval Time interval in miliseconds of healthcheck. After this time, application
 * will be killed and recovery policy will be taken. This can be zero, which means,
 * that there is no time limit (only fall of application is checked and only then
 * recovery action is taken)
 * @param recovery_policy One of SAM_RECOVERY_POLICY_RESTART, which means, that after
 * timeout application will be killed and new instance will be started.
 * SAM_RECOVERY_POLICY_QUIT will just stop application
 * @return
 * - CS_OK in case no problem appeared
 * - CS_ERR_BAD_HANDLE in case user is trying to initialize initialized instance
 * - CS_ERR_INVALID_PARAM in case recovery_policy had bad value
 */
cs_error_t sam_initialize (
        int time_interval,
        sam_recovery_policy_t recovery_policy);

/*
 * Close the SAM handle. This function should be called as late as possible
 * (in reality, if you plan just quit, and checking is stopped, there is no need
 * to call it). Function will stop healtchecking and put library to state, where
 * no new start is possible.
 *
 * @return
 * - CS_OK in case no problem appeared
 * - CS_ERR_BAD_HANDLE library was not initialized by #sam_initialize
 */
cs_error_t sam_finalize (void);

/*
 * Start healthchecking. From this time, you should call every time_interval
 * sam_hc_send, otherwise, recovery action will be taken.
 * @return
 * - CS_OK in case no problem appeared
 * - CS_ERR_BAD_HANDLE component was not registered by #sam_register
 */
cs_error_t sam_start (void);

/*
 * Stop healthchecking. Oposite of #sam_start. You can call sam_start and
 * sam_stop how many times you want.
 *
 * @return
 * - CS_OK in case no problem appeared
 * - CS_ERR_BAD_HANDLE healthchecking is not in running state (no sam_start
 *   was called)
 */
cs_error_t sam_stop (void);

/*
 * Set warning signal to be send. Default signal is SIGTERM. You can
 * use SIGKILL to emulate NOT sending warning signal and just send
 * SIGKILL.
 *
 * @return
 * - CS_OK in case no problem appeared
 * - CS_ERR_BAD_HANDLE library was not initialized by #sam_initialize or
 *   is finalized
 */
cs_error_t sam_warn_signal_set (int warn_signal);

/*
 * Register application. This is one of most crucial function. In case, your
 * application will be restarted, you will always return to point after calling
 * this function. This function can be called only once, and SAM must be initialized
 * by sam_initialize. You can choose any place in your application, where to call
 * this function.
 *
 * @param instance_id NULL or pointer to int memory, where current instance
 * of application will be returned. It's always safe to suppose, that first instance
 * (this means, no recovery action was taken yet) will be always 1 and instance_id
 * will be raising up to MAX_INT (after this, it will fall to 0).
 * @return
 * - CS_OK in case no problem appeared
 * - CS_ERR_BAD_HANDLE in case, you call this function twice, or before sam_init
 * - CS_ERR_LIBRARY internal library call failed. This can be one of pipe or fork
 *   creation. You can get more information from errno
 */
cs_error_t sam_register (
	unsigned int *instance_id);

/*
 * Send healthcheck confirmation. This should be called after #sam_start
 *
 * - CS_OK in case no problem appeared
 * - CS_ERR_BAD_HANDLE healthchecking is not in running state (no sam_start was
 *   called, or called after sam_stop/sam_finalize)
 */
cs_error_t sam_hc_send (void);

/*
 * Register healtcheck callback. After you will call this function, and set
 * cb to something else then NULL, SAM is automatically switched from
 * application driven healtchecking to event driven healtchecking. In other
 * words, is not longer needed to call sam_hc_send, but your callback function
 * must return 0 in case of healtchecking is correct, or value different then
 * 0, in case something happend. After next hc iteration, warning signal and
 * after that kill signal is sent back to your application.
 * @param cb Pointer to healtcheck function, or NULL to switch back to application driven hc
 * @return
 * - CS_OK in case no problem appeared
 * - CS_ERR_BAD_HANDLE in case, you call this function before sam_init or after sam_start
 * - CS_ERR_LIBRARY internal library call failed. This can be one of pipe or pthread
 *   creation.
 */
cs_error_t sam_hc_callback_register (sam_hc_callback_t cb);

#ifdef __cplusplus
}
#endif

#endif /* COROSYNC_SAM_H_DEFINED */
