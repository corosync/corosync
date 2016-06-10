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

#ifndef _QNETD_CLUSTER_H_
#define _QNETD_CLUSTER_H_

#include <sys/types.h>

#include <sys/queue.h>
#include <inttypes.h>

#include "tlv.h"
#include "qnetd-client-list.h"

#ifdef __cplusplus
extern "C" {
#endif

struct qnetd_cluster {
	char *cluster_name;
	size_t cluster_name_len;
	void *algorithm_data;
	struct qnetd_client_list client_list;
	TAILQ_ENTRY(qnetd_cluster) entries;
};

extern int			qnetd_cluster_init(struct qnetd_cluster *cluster,
    const char *cluster_name, size_t cluster_name_len);

extern void			qnetd_cluster_destroy(struct qnetd_cluster *cluster);

extern size_t			qnetd_cluster_size(const struct qnetd_cluster *cluster);

extern struct qnetd_client	*qnetd_cluster_find_client_by_node_id(
    const struct qnetd_cluster *cluster, uint32_t node_id);

#ifdef __cplusplus
}
#endif

#endif /* _QNETD_CLUSTER_H_ */
