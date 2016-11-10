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

#ifndef _QDEVICE_NET_SEND_H_
#define _QDEVICE_NET_SEND_H_

#include <sys/types.h>

#include "qdevice-net-instance.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int		qdevice_net_send_echo_request(struct qdevice_net_instance *instance);

extern int		qdevice_net_send_preinit(struct qdevice_net_instance *instance);

extern int		qdevice_net_send_init(struct qdevice_net_instance *instance);

extern int		qdevice_net_send_ask_for_vote(struct qdevice_net_instance *instance);

extern int		qdevice_net_send_config_node_list(struct qdevice_net_instance *instance,
    const struct node_list *nlist, int config_version_set, uint64_t config_version,
    int initial);

extern int		qdevice_net_send_heuristics_change(struct qdevice_net_instance *instance,
    enum tlv_heuristics heuristics);

extern int		qdevice_net_send_membership_node_list(
    struct qdevice_net_instance *instance, const struct tlv_ring_id *ring_id,
    uint32_t node_list_entries, uint32_t node_list[], enum tlv_heuristics heuristics);

extern int		qdevice_net_send_quorum_node_list(
    struct qdevice_net_instance *instance, enum tlv_quorate quorate,
    uint32_t node_list_entries, votequorum_node_t node_list[]);

#ifdef __cplusplus
}
#endif

#endif /* _QDEVICE_NET_SEND_H_ */
