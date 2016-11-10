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
#include "qdevice-model.h"
#include "qdevice-model-net.h"

static struct qdevice_model *qdevice_model_array[QDEVICE_MODEL_TYPE_ARRAY_SIZE];

int
qdevice_model_init(struct qdevice_instance *instance)
{

	if (instance->model_type >= QDEVICE_MODEL_TYPE_ARRAY_SIZE ||
	    qdevice_model_array[instance->model_type] == NULL) {
		qdevice_log(LOG_CRIT, "qdevice_model_init unhandled model");
		exit(1);
	}

	return (qdevice_model_array[instance->model_type]->init(instance));
}

int
qdevice_model_destroy(struct qdevice_instance *instance)
{

	if (instance->model_type >= QDEVICE_MODEL_TYPE_ARRAY_SIZE ||
	    qdevice_model_array[instance->model_type] == NULL) {
		qdevice_log(LOG_CRIT, "qdevice_model_destroy unhandled model");
		exit(1);
	}

	return (qdevice_model_array[instance->model_type]->destroy(instance));
}

int
qdevice_model_run(struct qdevice_instance *instance)
{

	if (instance->model_type >= QDEVICE_MODEL_TYPE_ARRAY_SIZE ||
	    qdevice_model_array[instance->model_type] == NULL) {
		qdevice_log(LOG_CRIT, "qdevice_model_run unhandled model");
		exit(1);
	}

	return (qdevice_model_array[instance->model_type]->run(instance));
}

int
qdevice_model_get_config_node_list_failed(struct qdevice_instance *instance)
{

	if (instance->model_type >= QDEVICE_MODEL_TYPE_ARRAY_SIZE ||
	    qdevice_model_array[instance->model_type] == NULL) {
		qdevice_log(LOG_CRIT, "qdevice_model_run unhandled model");
		exit(1);
	}

	return (qdevice_model_array[instance->model_type]->get_config_node_list_failed(instance));
}

int
qdevice_model_config_node_list_changed(struct qdevice_instance *instance,
    const struct node_list *nlist, int config_version_set, uint64_t config_version)
{

	if (instance->model_type >= QDEVICE_MODEL_TYPE_ARRAY_SIZE ||
	    qdevice_model_array[instance->model_type] == NULL) {
		qdevice_log(LOG_CRIT, "qdevice_model_run unhandled model");
		exit(1);
	}

	return (qdevice_model_array[instance->model_type]->
	    config_node_list_changed(instance, nlist, config_version_set, config_version));
}

int
qdevice_model_votequorum_quorum_notify(struct qdevice_instance *instance,
    uint32_t quorate, uint32_t node_list_entries, votequorum_node_t node_list[])
{

	if (instance->model_type >= QDEVICE_MODEL_TYPE_ARRAY_SIZE ||
	    qdevice_model_array[instance->model_type] == NULL) {
		qdevice_log(LOG_CRIT, "qdevice_model_votequorum_quorum_notify unhandled model");
		exit(1);
	}

	return (qdevice_model_array[instance->model_type]->
	    votequorum_quorum_notify(instance, quorate, node_list_entries, node_list));
}

int
qdevice_model_votequorum_node_list_notify(struct qdevice_instance *instance,
    votequorum_ring_id_t votequorum_ring_id, uint32_t node_list_entries, uint32_t node_list[])
{

	if (instance->model_type >= QDEVICE_MODEL_TYPE_ARRAY_SIZE ||
	    qdevice_model_array[instance->model_type] == NULL) {
		qdevice_log(LOG_CRIT, "qdevice_model_votequorum_node_list_notify unhandled model");
		exit(1);
	}

	return (qdevice_model_array[instance->model_type]->
	    votequorum_node_list_notify(instance, votequorum_ring_id, node_list_entries, node_list));
}

int
qdevice_model_votequorum_node_list_heuristics_notify(struct qdevice_instance *instance,
    votequorum_ring_id_t votequorum_ring_id, uint32_t node_list_entries, uint32_t node_list[],
    enum qdevice_heuristics_exec_result heuristics_exec_result)
{

	if (instance->model_type >= QDEVICE_MODEL_TYPE_ARRAY_SIZE ||
	    qdevice_model_array[instance->model_type] == NULL) {
		qdevice_log(LOG_CRIT, "qdevice_model_votequorum_node_list_heuristics_notify unhandled model");
		exit(1);
	}

	return (qdevice_model_array[instance->model_type]->
	    votequorum_node_list_heuristics_notify(instance, votequorum_ring_id, node_list_entries,
		node_list, heuristics_exec_result));
}

int
qdevice_model_votequorum_expected_votes_notify(struct qdevice_instance *instance,
    uint32_t expected_votes)
{

	if (instance->model_type >= QDEVICE_MODEL_TYPE_ARRAY_SIZE ||
	    qdevice_model_array[instance->model_type] == NULL) {
		qdevice_log(LOG_CRIT, "qdevice_model_votequorum_expected_votes_notify unhandled model");
		exit(1);
	}

	return (qdevice_model_array[instance->model_type]->
	    votequorum_expected_votes_notify(instance, expected_votes));
}

int
qdevice_model_ipc_cmd_status(struct qdevice_instance *instance, struct dynar *outbuf, int verbose)
{

	if (instance->model_type >= QDEVICE_MODEL_TYPE_ARRAY_SIZE ||
	    qdevice_model_array[instance->model_type] == NULL) {
		qdevice_log(LOG_CRIT, "qdevice_model_ipc_cmd_status unhandled model");
		exit(1);
	}

	return (qdevice_model_array[instance->model_type]->
	    ipc_cmd_status(instance, outbuf, verbose));
}

int
qdevice_model_cmap_changed(struct qdevice_instance *instance,
    const struct qdevice_cmap_change_events *events)
{

	if (instance->model_type >= QDEVICE_MODEL_TYPE_ARRAY_SIZE ||
	    qdevice_model_array[instance->model_type] == NULL) {
		qdevice_log(LOG_CRIT, "qdevice_model_cmap_chaged unhandled model");
		exit(1);
	}

	return (qdevice_model_array[instance->model_type]->
	    cmap_changed(instance, events));
}

int
qdevice_model_register(enum qdevice_model_type model_type,
    struct qdevice_model *model)
{

	if (model_type >= QDEVICE_MODEL_TYPE_ARRAY_SIZE) {
		return (-1);
	}

	if (qdevice_model_array[model_type] != NULL) {
		return (-1);
	}

	qdevice_model_array[model_type] = model;

	return (0);
}

void
qdevice_model_register_all(void)
{

	if (qdevice_model_net_register() != 0) {
		qdevice_log(LOG_CRIT, "Failed to register model 'net' ");
		exit(1);
	}
}

int
qdevice_model_str_to_type(const char *str, enum qdevice_model_type *model_type)
{
	int i;

	for (i = 0; i < QDEVICE_MODEL_TYPE_ARRAY_SIZE; i++) {
		if (qdevice_model_array[i] != NULL &&
		    strcmp(qdevice_model_array[i]->name, str) == 0) {
			*model_type = i;

			return (0);
		}
	}

	return (-1);
}

const char *
qdevice_model_type_to_str(enum qdevice_model_type model_type)
{

	switch (model_type) {
	case QDEVICE_MODEL_TYPE_NET: return ("Net"); break;
	case QDEVICE_MODEL_TYPE_ARRAY_SIZE: return ("Unknown model"); break;
	/*
	 * Default is not defined intentionally. Compiler shows warning when new model is added
	 */
	}

	return ("Unknown model");
}
