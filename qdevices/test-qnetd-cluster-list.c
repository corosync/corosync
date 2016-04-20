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

#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "qnetd-cluster-list.h"
#include "qnetd-client.h"
#include "qnetd-client-list.h"

static struct qnetd_client_list clients;
static struct qnetd_cluster_list clusters;

static void
add_client(const char *cluster_name, size_t cluster_name_len,
    struct qnetd_client **client, struct qnetd_cluster **cluster)
{
	PRNetAddr addr;
	struct qnetd_client *tmp_client;
	struct qnetd_cluster *tmp_cluster;
	char *client_addr_str;

	memset(&addr, 0, sizeof(addr));

	client_addr_str = strdup("addrstr");
	assert(client_addr_str != NULL);

	tmp_client = qnetd_client_list_add(&clients, NULL, &addr, client_addr_str, 1000, 2, 1000, NULL);
	assert(tmp_client != NULL);
	tmp_client->cluster_name = malloc(cluster_name_len + 1);
	assert(tmp_client->cluster_name != NULL);
	memcpy(tmp_client->cluster_name, cluster_name, cluster_name_len);
	tmp_client->cluster_name_len = cluster_name_len;

	tmp_cluster = qnetd_cluster_list_add_client(&clusters, tmp_client);
	assert(cluster != NULL);
	tmp_client->cluster = tmp_cluster;

	*client = tmp_client;
	*cluster = tmp_cluster;
}

static int
no_clients_in_cluster(struct qnetd_cluster *cluster)
{
	int i;
	struct qnetd_client *client;

	i = 0;

	TAILQ_FOREACH(client, &cluster->client_list, cluster_entries) {
		i++;
	}

	return (i);
}

static int
no_clusters(void)
{
	int i;
	struct qnetd_cluster *cluster;

	i = 0;

	TAILQ_FOREACH(cluster, &clusters, entries) {
		i++;
	}

	return (i);
}

static int
is_client_in_cluster(struct qnetd_cluster *cluster, const struct qnetd_client *client)
{
	struct qnetd_client *tmp_client;

	TAILQ_FOREACH(tmp_client, &cluster->client_list, cluster_entries) {
		if (tmp_client == client) {
			return (1);
		}
	}

	return (0);
}

static void
del_client(struct qnetd_client *client)
{

	qnetd_cluster_list_del_client(&clusters, client->cluster, client);
	qnetd_client_list_del(&clients, client);
}

int
main(void)
{
	struct qnetd_client *client[4];
	struct qnetd_cluster *cluster[4];
	const char *cl_name;

	qnetd_client_list_init(&clients);
	qnetd_cluster_list_init(&clusters);

	assert(no_clusters() == 0);

	cl_name = "test_cluster";
	add_client(cl_name, strlen(cl_name), &client[0], &cluster[0]);
	assert(no_clusters() == 1);
	add_client(cl_name, strlen(cl_name), &client[1], &cluster[1]);
	assert(no_clusters() == 1);

	cl_name = "cluster2";
	add_client(cl_name, strlen(cl_name), &client[2], &cluster[2]);
	assert(no_clusters() == 2);
	add_client(cl_name, strlen(cl_name), &client[3], &cluster[3]);
	assert(no_clusters() == 2);

	assert(cluster[0] == cluster[1]);
	assert(cluster[2] == cluster[3]);
	assert(cluster[0] != cluster[2]);

	assert(no_clients_in_cluster(cluster[0]) == 2);
	assert(no_clients_in_cluster(cluster[2]) == 2);

	assert(is_client_in_cluster(cluster[0], client[0]));
	assert(is_client_in_cluster(client[0]->cluster, client[0]));
	assert(is_client_in_cluster(cluster[0], client[1]));
	assert(!is_client_in_cluster(cluster[0], client[2]));
	assert(!is_client_in_cluster(cluster[0], client[3]));

	assert(!is_client_in_cluster(cluster[2], client[0]));
	assert(!is_client_in_cluster(cluster[2], client[1]));
	assert(is_client_in_cluster(cluster[2], client[2]));
	assert(is_client_in_cluster(cluster[2], client[3]));
	assert(is_client_in_cluster(client[2]->cluster, client[2]));

	del_client(client[0]);
	assert(no_clusters() == 2);
	assert(no_clients_in_cluster(cluster[0]) == 1);
	assert(no_clients_in_cluster(cluster[2]) == 2);

	assert(!is_client_in_cluster(cluster[0], client[0]));
	assert(is_client_in_cluster(cluster[0], client[1]));
	assert(!is_client_in_cluster(cluster[0], client[2]));
	assert(!is_client_in_cluster(cluster[0], client[3]));

	add_client(cl_name, strlen(cl_name), &client[0], &cluster[0]);
	assert(no_clients_in_cluster(cluster[1]) == 1);
	assert(no_clients_in_cluster(cluster[2]) == 3);

	assert(!is_client_in_cluster(cluster[1], client[0]));
	assert(is_client_in_cluster(cluster[1], client[1]));
	assert(!is_client_in_cluster(cluster[1], client[2]));
	assert(!is_client_in_cluster(cluster[1], client[3]));

	assert(is_client_in_cluster(cluster[2], client[0]));
	assert(!is_client_in_cluster(cluster[2], client[1]));
	assert(is_client_in_cluster(cluster[2], client[2]));
	assert(is_client_in_cluster(cluster[2], client[3]));

	del_client(client[1]);
	assert(no_clusters() == 1);

	del_client(client[2]);
	assert(no_clusters() == 1);

	del_client(client[3]);
	assert(no_clusters() == 1);

	del_client(client[0]);
	assert(no_clusters() == 0);

	return (0);
}
