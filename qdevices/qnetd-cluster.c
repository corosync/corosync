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

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "qnetd-cluster.h"

int
qnetd_cluster_init(struct qnetd_cluster *cluster, const char *cluster_name, size_t cluster_name_len)
{

	memset(cluster, 0, sizeof(*cluster));

	cluster->cluster_name = malloc(cluster_name_len + 1);
	if (cluster->cluster_name == NULL) {
		return (-1);
	}
	memset(cluster->cluster_name, 0, cluster_name_len + 1);
	memcpy(cluster->cluster_name, cluster_name, cluster_name_len);

	cluster->cluster_name_len = cluster_name_len;
	TAILQ_INIT(&cluster->client_list);

	return (0);
}

void
qnetd_cluster_destroy(struct qnetd_cluster *cluster)
{

	free(cluster->cluster_name);
	cluster->cluster_name = NULL;
}

size_t
qnetd_cluster_size(const struct qnetd_cluster *cluster)
{
	size_t res;
	struct qnetd_client *client;

	res = 0;

	TAILQ_FOREACH(client, &cluster->client_list, cluster_entries) {
		res++;
	}

	return (res);
}

struct qnetd_client *
qnetd_cluster_find_client_by_node_id(const struct qnetd_cluster *cluster, uint32_t node_id)
{
	struct qnetd_client *client;

	TAILQ_FOREACH(client, &cluster->client_list, cluster_entries) {
		if (client->node_id == node_id) {
			return (client);
		}
	}

	return (NULL);
}
