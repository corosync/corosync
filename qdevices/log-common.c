/*
 * Copyright (c) 2015-2019 Red Hat, Inc.
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

#include "log.h"
#include "log-common.h"
#include "utils.h"

void
log_common_debug_dump_node_list(const struct node_list *nlist)
{
	struct node_list_entry *node_info;
	size_t zi;

	log(LOG_DEBUG, "  Node list:");

	zi = 0;

	TAILQ_FOREACH(node_info, nlist, entries) {
		log(LOG_DEBUG, "    %zu node_id = "UTILS_PRI_NODE_ID", "
		    "data_center_id = "UTILS_PRI_DATACENTER_ID", node_state = %s",
		    zi, node_info->node_id, node_info->data_center_id,
		    tlv_node_state_to_str(node_info->node_state));
		zi++;
	}
}

void
log_common_msg_decode_error(int ret)
{

	switch (ret) {
	case -1:
		log(LOG_WARNING, "Received message with option with invalid length");
		break;
	case -2:
		log(LOG_CRIT, "Can't allocate memory");
		break;
	case -3:
		log(LOG_WARNING, "Received inconsistent msg (tlv len > msg size)");
		break;
	case -4:
		log(LOG_WARNING, "Received message with option with invalid value");
		break;
	default:
		log(LOG_ERR, "Unknown error occurred when decoding message");
		break;
	}
}
