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

#include "qdevice-log.h"
#include "qdevice-net-instance.h"
#include "qnet-config.h"
#include "utils.h"

/*
 * Needed for creating nspr handle from unix fd
 */
#include <private/pprio.h>

int
qdevice_net_instance_init(struct qdevice_net_instance *instance, size_t initial_receive_size,
    size_t initial_send_size, size_t min_send_size, size_t max_send_buffers,
    size_t max_receive_size,
    enum tlv_tls_supported tls_supported,
    enum tlv_decision_algorithm_type decision_algorithm, uint32_t heartbeat_interval,
    uint32_t sync_heartbeat_interval, uint32_t cast_vote_timer_interval,
    const char *host_addr, uint16_t host_port, const char *cluster_name,
    const struct tlv_tie_breaker *tie_breaker, uint32_t connect_timeout,
    int cmap_fd, int votequorum_fd)
{

	memset(instance, 0, sizeof(*instance));

	instance->initial_receive_size = initial_receive_size;
	instance->initial_send_size = initial_send_size;
	instance->min_send_size = min_send_size;
	instance->max_receive_size = max_receive_size;
	instance->decision_algorithm = decision_algorithm;
	instance->heartbeat_interval = heartbeat_interval;
	instance->sync_heartbeat_interval = sync_heartbeat_interval;
	instance->cast_vote_timer_interval = cast_vote_timer_interval;
	instance->cast_vote_timer = NULL;
	instance->host_addr = host_addr;
	instance->host_port = host_port;
	instance->cluster_name = cluster_name;
	instance->connect_timeout = connect_timeout;
	instance->last_msg_seq_num = 1;
	instance->echo_request_expected_msg_seq_num = 1;
	instance->echo_reply_received_msg_seq_num = 1;
	memcpy(&instance->tie_breaker, tie_breaker, sizeof(*tie_breaker));

	dynar_init(&instance->receive_buffer, initial_receive_size);

	send_buffer_list_init(&instance->send_buffer_list, max_send_buffers,
	    initial_send_size);

	timer_list_init(&instance->main_timer_list);

	instance->tls_supported = tls_supported;

	if ((instance->cmap_poll_fd = PR_CreateSocketPollFd(cmap_fd)) == NULL) {
		qdevice_log_nss(LOG_CRIT, "Can't create NSPR cmap poll fd");
		return (-1);
	}

	if ((instance->votequorum_poll_fd = PR_CreateSocketPollFd(votequorum_fd)) == NULL) {
		qdevice_log_nss(LOG_CRIT, "Can't create NSPR votequorum poll fd");
		return (-1);
	}

	return (0);
}

void
qdevice_net_instance_clean(struct qdevice_net_instance *instance)
{

	dynar_clean(&instance->receive_buffer);

	send_buffer_list_free(&instance->send_buffer_list);

	instance->skipping_msg = 0;
	instance->msg_already_received_bytes = 0;
	instance->state = QDEVICE_NET_INSTANCE_STATE_WAITING_PREINIT_REPLY;
	instance->echo_request_expected_msg_seq_num = instance->echo_reply_received_msg_seq_num;
	instance->using_tls = 0;

	instance->schedule_disconnect = 0;
	instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_UNDEFINED;
}

int
qdevice_net_instance_destroy(struct qdevice_net_instance *instance)
{

	dynar_destroy(&instance->receive_buffer);

	send_buffer_list_free(&instance->send_buffer_list);

	timer_list_free(&instance->main_timer_list);

	free((void *)instance->cluster_name);
	free((void *)instance->host_addr);

	if (PR_DestroySocketPollFd(instance->votequorum_poll_fd) != PR_SUCCESS) {
		qdevice_log_nss(LOG_WARNING, "Unable to close votequorum connection fd");
	}

	if (PR_DestroySocketPollFd(instance->cmap_poll_fd) != PR_SUCCESS) {
		qdevice_log_nss(LOG_WARNING, "Unable to close votequorum connection fd");
	}

	return (0);
}

int
qdevice_net_instance_init_from_cmap(struct qdevice_instance *instance)
{
	char *str;
	cmap_handle_t cmap_handle;
	enum tlv_tls_supported tls_supported;
	int i;
	long int li;
	enum tlv_decision_algorithm_type decision_algorithm;
	struct tlv_tie_breaker tie_breaker;
	uint32_t heartbeat_interval;
	uint32_t sync_heartbeat_interval;
	uint32_t cast_vote_timer_interval;
	char *host_addr;
	int host_port;
	char *ep;
	char *cluster_name;
	uint32_t connect_timeout;
	struct qdevice_net_instance *net_instance;

	cmap_handle = instance->cmap_handle;

	net_instance = malloc(sizeof(*net_instance));
	if (net_instance == NULL) {
		qdevice_log(LOG_ERR, "Can't alloc qdevice_net_instance");
		return (-1);
	}

	/*
	 * Check tls
	 */
	tls_supported = QDEVICE_NET_DEFAULT_TLS_SUPPORTED;

	if (cmap_get_string(cmap_handle, "quorum.device.net.tls", &str) == CS_OK) {
		if ((i = utils_parse_bool_str(str)) == -1) {
			if (strcasecmp(str, "required") != 0) {
				free(str);
				qdevice_log(LOG_ERR, "quorum.device.net.tls value is not valid.");

				goto error_free_instance;
			} else {
				tls_supported = TLV_TLS_REQUIRED;
			}
		} else {
			if (i == 1) {
				tls_supported = TLV_TLS_SUPPORTED;
			} else {
				tls_supported = TLV_TLS_UNSUPPORTED;
			}
		}

		free(str);
	}

	/*
	 * Host
	 */
	if (cmap_get_string(cmap_handle, "quorum.device.net.host", &str) != CS_OK) {
		qdevice_log(LOG_ERR, "Qdevice net daemon address is not defined (quorum.device.net.host)");
		goto error_free_instance;
	}
	host_addr = str;

	if (cmap_get_string(cmap_handle, "quorum.device.net.port", &str) == CS_OK) {
		host_port = strtol(str, &ep, 10);

		free(str);

		if (host_port <= 0 || host_port > ((uint16_t)~0) || *ep != '\0') {
			qdevice_log(LOG_ERR, "quorum.device.net.port must be in range 0-65535");
			goto error_free_host_addr;
		}
	} else {
		host_port = QNETD_DEFAULT_HOST_PORT;
	}

	/*
	 * Cluster name
	 */
	if (cmap_get_string(cmap_handle, "totem.cluster_name", &str) != CS_OK) {
		qdevice_log(LOG_ERR, "Cluster name (totem.cluster_name) has to be defined.");
		goto error_free_host_addr;
	}
	cluster_name = str;

	/*
	 * Adjust qdevice timeouts to better suit qnetd
	 */
	cast_vote_timer_interval = instance->heartbeat_interval * 0.5;
	heartbeat_interval = instance->heartbeat_interval * 0.8;

	sync_heartbeat_interval = instance->sync_heartbeat_interval * 0.8;

	/*
	 * Choose decision algorithm
	 */
	if (cmap_get_string(cmap_handle, "quorum.device.net.algorithm", &str) != CS_OK) {
		decision_algorithm = QDEVICE_NET_DEFAULT_ALGORITHM;
	} else {
		if (strcmp(str, "test") == 0) {
			decision_algorithm = TLV_DECISION_ALGORITHM_TYPE_TEST;
		} else if (strcmp(str, "ffsplit") == 0) {
			decision_algorithm = TLV_DECISION_ALGORITHM_TYPE_FFSPLIT;
		} else if (strcmp(str, "2nodelms") == 0) {
			decision_algorithm = TLV_DECISION_ALGORITHM_TYPE_2NODELMS;
		} else if (strcmp(str, "lms") == 0) {
			decision_algorithm = TLV_DECISION_ALGORITHM_TYPE_LMS;
		} else {
			qdevice_log(LOG_ERR, "Unknown decision algorithm %s", str);
			free(str);
			goto error_free_cluster_name;
		}

		free(str);
	}

	/*
	 * Load tie_breaker mode
	 */
	memset(&tie_breaker, 0, sizeof(tie_breaker));

	if (cmap_get_string(cmap_handle, "quorum.device.net.tie_breaker", &str) != CS_OK) {
		tie_breaker.mode = QDEVICE_NET_DEFAULT_TIE_BREAKER_MODE;
	} else {
		if (strcmp(str, "lowest") == 0) {
			tie_breaker.mode = TLV_TIE_BREAKER_MODE_LOWEST;
		} else if (strcmp(str, "highest") == 0) {
			tie_breaker.mode = TLV_TIE_BREAKER_MODE_HIGHEST;
		} else {
			li = strtol(str, &ep, 10);
			if (li <= 0 || li > ((uint32_t)~0) || *ep != '\0') {
				qdevice_log(LOG_ERR, "tie_breaker must be lowest|highest|valid_node_id");
				free(str);
				goto error_free_cluster_name;
			}

			tie_breaker.mode = TLV_TIE_BREAKER_MODE_NODE_ID;
			tie_breaker.node_id = li;
		}

		free(str);
	}

	/*
	 * Get connect timeout
	 */
	if (cmap_get_string(cmap_handle, "quorum.device.net.connect_timeout", &str) != CS_OK) {
		connect_timeout = heartbeat_interval;
	} else {
		li = strtol(str, &ep, 10);
		if (li < QDEVICE_NET_MIN_CONNECT_TIMEOUT || li > QDEVICE_NET_MAX_CONNECT_TIMEOUT || *ep != '\0') {
			qdevice_log(LOG_ERR, "connect_timeout must be valid number in range <%lu,%lu>",
			    QDEVICE_NET_MIN_CONNECT_TIMEOUT, QDEVICE_NET_MAX_CONNECT_TIMEOUT);
			free(str);
			goto error_free_cluster_name;
		}

		connect_timeout = li;

		free(str);
	}

	/*
	 * Really initialize instance
	 */
	if (qdevice_net_instance_init(net_instance,
	    QDEVICE_NET_INITIAL_MSG_RECEIVE_SIZE, QDEVICE_NET_INITIAL_MSG_SEND_SIZE,
	    QDEVICE_NET_MIN_MSG_SEND_SIZE, QDEVICE_NET_MAX_SEND_BUFFERS,
	    QDEVICE_NET_MAX_MSG_RECEIVE_SIZE,
	    tls_supported, decision_algorithm,
	    heartbeat_interval, sync_heartbeat_interval, cast_vote_timer_interval,
	    host_addr, host_port, cluster_name, &tie_breaker, connect_timeout,
	    instance->cmap_poll_fd, instance->votequorum_poll_fd) == -1) {
		qdevice_log(LOG_ERR, "Can't initialize qdevice-net instance");
		goto error_free_instance;
	}

	net_instance->qdevice_instance_ptr = instance;
	instance->model_data = net_instance;

	return (0);

error_free_cluster_name:
	free(cluster_name);
error_free_host_addr:
	free(host_addr);
error_free_instance:
	free(net_instance);
	return (-1);
}
