/*
 * Copyright (c) 2015-2017 Red Hat, Inc.
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
#include "qdevice-net-votequorum.h"

enum tlv_node_state
qdevice_net_votequorum_node_state_to_tlv(uint32_t votequorum_node_state)
{
	enum tlv_node_state res;

	switch (votequorum_node_state) {
	case VOTEQUORUM_NODESTATE_MEMBER: res = TLV_NODE_STATE_MEMBER; break;
	case VOTEQUORUM_NODESTATE_DEAD: res = TLV_NODE_STATE_DEAD; break;
	case VOTEQUORUM_NODESTATE_LEAVING: res = TLV_NODE_STATE_LEAVING; break;
	default:
		qdevice_log(LOG_ERR, "qdevice_net_votequorum_node_state_to_tlv: Unhandled votequorum "
		    "node state %"PRIu32, votequorum_node_state);
		exit(1);
		break;
	}

	return (res);
}

void
qdevice_net_votequorum_ring_id_to_tlv(struct tlv_ring_id *tlv_rid,
    const votequorum_ring_id_t *votequorum_rid)
{

	tlv_rid->node_id = votequorum_rid->nodeid;
	tlv_rid->seq = votequorum_rid->seq;
}
