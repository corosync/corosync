/*
 * Copyright (c) 2010 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)
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
 * - Neither the name of the MontaVista Software, Inc. nor the names of its
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
#ifndef COROSYNC_CPG_H_INT_DEFINED
#define COROSYNC_CPG_H_INT_DEFINED

#include <corosync/totem/totem.h>
#include <corosync/totem/totemip.h>

struct rbmi {
	uint32_t idx;
	uint32_t msg_len;
};

struct srp_addr {
	struct totem_ip_address addr[INTERFACE_MAX];
};

struct message_header {
	uint8_t type;
	uint8_t encapsulated;
	uint16_t endian_detector;
	uint32_t nodeid;
} __attribute__((packed));


struct mcast {
	struct message_header header;
	struct srp_addr system_from;
	struct memb_ring_id ring_id;
	uint32_t seq;
	uint8_t guarantee;
	uint8_t group_len;
	uint16_t msg_count;
	uint16_t fragment_id;
	uint16_t goup;
	uint8_t group[0];
	uint8_t contents[0];
} __attribute__((packed));

#endif /* COROSYNC_CPG_INT_H_DEFINED */
