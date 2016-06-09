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

#include "qnetd-cluster-list.h"

void
qnetd_cluster_list_init(struct qnetd_cluster_list *list)
{

	TAILQ_INIT(list);
}

struct qnetd_cluster *
qnetd_cluster_list_find_by_name(struct qnetd_cluster_list *list,
    const char *cluster_name, size_t cluster_name_len)
{
	struct qnetd_cluster *cluster;

	TAILQ_FOREACH(cluster, list, entries) {
		if (cluster->cluster_name_len == cluster_name_len &&
		    memcmp(cluster->cluster_name, cluster_name, cluster_name_len) == 0) {
			return (cluster);
		}
	}

	return (NULL);
}

struct qnetd_cluster *
qnetd_cluster_list_add_client(struct qnetd_cluster_list *list, struct qnetd_client *client)
{
	struct qnetd_cluster *cluster;

	cluster = qnetd_cluster_list_find_by_name(list, client->cluster_name,
	    client->cluster_name_len);
	if (cluster == NULL) {
		cluster = (struct qnetd_cluster *)malloc(sizeof(*cluster));
		if (cluster == NULL) {
			return (NULL);
		}

		if (qnetd_cluster_init(cluster, client->cluster_name,
		    client->cluster_name_len) != 0) {
			free(cluster);

			return (NULL);
		}

		TAILQ_INSERT_TAIL(list, cluster, entries);
	}

	TAILQ_INSERT_TAIL(&cluster->client_list, client, cluster_entries);

	return (cluster);
}

void
qnetd_cluster_list_del_client(struct qnetd_cluster_list *list, struct qnetd_cluster *cluster,
    struct qnetd_client *client)
{

	TAILQ_REMOVE(&cluster->client_list, client, cluster_entries);

	if (TAILQ_EMPTY(&cluster->client_list)) {
		TAILQ_REMOVE(list, cluster, entries);

		qnetd_cluster_destroy(cluster);
		free(cluster);
	}
}

void
qnetd_cluster_list_free(struct qnetd_cluster_list *list)
{
	struct qnetd_cluster *cluster;
	struct qnetd_cluster *cluster_next;

	cluster = TAILQ_FIRST(list);
	while (cluster != NULL) {
		cluster_next = TAILQ_NEXT(cluster, entries);

		qnetd_cluster_destroy(cluster);
		free(cluster);

		cluster = cluster_next;
	}

	TAILQ_INIT(list);
}

size_t
qnetd_cluster_list_size(const struct qnetd_cluster_list *list)
{
	size_t res;
	struct qnetd_cluster *cluster;

	res = 0;

	TAILQ_FOREACH(cluster, list, entries) {
		res++;
	}

	return (res);
}
