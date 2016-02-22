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

#include <config.h>

#include "qdevice-config.h"
#include "qdevice-cmap.h"
#include "qdevice-log.h"
#include "qdevice-model.h"
#include "qdevice-votequorum.h"
#include "utils.h"

int
main(void)
{
	struct qdevice_instance instance;

	qdevice_instance_init(&instance);
	qdevice_cmap_init(&instance);
	qdevice_log_init(&instance);
	qdevice_votequorum_init(&instance);

	qdevice_log(LOG_DEBUG, "Registering qdevice models");
	qdevice_model_register_all();

	qdevice_log(LOG_DEBUG, "Configuring qdevice");
	if (qdevice_instance_configure_from_cmap(&instance) != 0) {
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

	qdevice_log(LOG_DEBUG, "Running qdevice model");
	if (qdevice_model_run(&instance) != 0) {
		return (1);
	}

	qdevice_log(LOG_DEBUG, "Removing cmap tracking");
	if (qdevice_cmap_del_track(&instance) != 0) {
		return (1);
	}

	qdevice_log(LOG_DEBUG, "Destorying qdevice model");
	qdevice_model_destroy(&instance);

	qdevice_votequorum_destroy(&instance);
	qdevice_cmap_destroy(&instance);
	qdevice_log_close(&instance);
	qdevice_instance_destroy(&instance);

	return (0);
}
