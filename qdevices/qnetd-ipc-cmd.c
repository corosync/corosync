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

#include "dynar-str.h"
#include "qnetd-ipc-cmd.h"
#include "qnetd-log.h"
#include "utils.h"

int
qnetd_ipc_cmd_status(struct qnetd_instance *instance, struct dynar *outbuf, int verbose)
{

	if (dynar_str_catf(outbuf, "QNetd address:\t\t\t%s:%"PRIu16"\n",
	    (instance->host_addr != NULL ? instance->host_addr : "*"), instance->host_port) == -1) {
		return (-1);
	}

	if (dynar_str_catf(outbuf, "TLS:\t\t\t\t%s%s\n",
	    tlv_tls_supported_to_str(instance->tls_supported),
	    ((instance->tls_supported != TLV_TLS_UNSUPPORTED && instance->tls_client_cert_required) ?
	    " (client certificate required)" : "")) == -1) {
		return (-1);
	}

	if (dynar_str_catf(outbuf, "Connected clients:\t\t%zu\n",
	    qnetd_client_list_no_clients(&instance->clients)) == -1) {
		return (-1);
	}

	if (dynar_str_catf(outbuf, "Connected clusters:\t\t%zu\n",
	    qnetd_cluster_list_no_clusters(&instance->clusters)) == -1) {
		return (-1);
	}

	if (!verbose) {
		return (0);
	}

	if (instance->max_clients != 0) {
		if (dynar_str_catf(outbuf, "Maximum allowed clients:\t%zu\n",
		    instance->max_clients) == -1) {
			return (-1);
		}
	}

	if (dynar_str_catf(outbuf, "Maximum send/receive size:\t%zu/%zu bytes\n",
	    instance->max_client_send_size, instance->max_client_receive_size) == -1) {
		return (-1);
	}

	return (0);
}
