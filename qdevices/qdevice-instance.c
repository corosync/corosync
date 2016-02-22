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

#include "qdevice-instance.h"
#include "qdevice-log.h"
#include "qdevice-model.h"

int
qdevice_instance_init(struct qdevice_instance *instance)
{

	memset(instance, 0, sizeof(*instance));

	node_list_init(&instance->config_node_list);

	return (0);
}

int
qdevice_instance_destroy(struct qdevice_instance *instance)
{

	node_list_free(&instance->config_node_list);

	return (0);
}

int
qdevice_instance_configure_from_cmap(struct qdevice_instance *instance)
{
	char *str;

	if (cmap_get_string(instance->cmap_handle, "quorum.device.model", &str) != CS_OK) {
		qdevice_log(LOG_ERR, "Can't read quorum.device.model cmap key.");

		return (-1);
	}

	if (qdevice_model_str_to_type(str, &instance->model_type) != 0) {
		qdevice_log(LOG_ERR, "Configured device model %s is not supported.", str);
		free(str);

		return (-1);
	}
	free(str);

	if (cmap_get_uint32(instance->cmap_handle, "runtime.votequorum.this_node_id",
	    &instance->node_id) != CS_OK) {
		qdevice_log(LOG_ERR, "Unable to retrive this node nodeid.");

		return (-1);
	}

	if (cmap_get_uint32(instance->cmap_handle, "quorum.device.timeout", &instance->heartbeat_interval) != CS_OK) {
		instance->heartbeat_interval = VOTEQUORUM_QDEVICE_DEFAULT_TIMEOUT;
	}

	if (cmap_get_uint32(instance->cmap_handle, "quorum.device.sync_timeout",
	    &instance->sync_heartbeat_interval) != CS_OK) {
		instance->sync_heartbeat_interval = VOTEQUORUM_QDEVICE_DEFAULT_SYNC_TIMEOUT;
	}

	return (0);
}
