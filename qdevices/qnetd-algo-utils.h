/*
 * Copyright (c) 2016 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Christine Caulfield (ccaulfie@redhat.com)
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

#ifndef _QNETD_ALGO_UTILS_H_
#define _QNETD_ALGO_UTILS_H_

#ifdef __cplusplus
extern "C" {
#endif

struct qnetd_algo_partition {
	struct tlv_ring_id ring_id;
	int num_nodes;
	TAILQ_ENTRY(qnetd_algo_partition) entries;
};

typedef TAILQ_HEAD(, qnetd_algo_partition) partitions_list_t;

extern int qnetd_algo_all_ring_ids_match(struct qnetd_client* client, const struct tlv_ring_id* ring_id);

extern struct qnetd_algo_partition* qnetd_algo_find_partition(partitions_list_t* partitions, const struct tlv_ring_id* ring_id);

extern int qnetd_algo_create_partitions(struct qnetd_client* client, partitions_list_t* partitions, const struct tlv_ring_id* ring_id);

extern void qnetd_algo_free_partitions(partitions_list_t* partitions);

extern void qnetd_algo_dump_partitions(partitions_list_t* partitions);

#ifdef __cplusplus
}
#endif

#endif /* _QNETD_ALGO_UTILS_H_ */
