/*
 * Copyright (C) 2012-2018 Red Hat, Inc.  All rights reserved.
 *
 * Author: Fabio M. Di Nitto <fabbione@kronosnet.org>
 *
 * This software licensed under GPL-2.0+, LGPL-2.0+
 */

#include "config.h"

#include <arpa/inet.h>
#include <netinet/ether.h>
#include <string.h>

#include "etherfilter.h"

/*
 * stole from linux kernel/include/linux/etherdevice.h
 */

static inline int is_zero_ether_addr(const uint8_t *addr)
{
	return !(addr[0] | addr[1] | addr[2] | addr[3] | addr[4] | addr[5]);
}

static inline int is_multicast_ether_addr(const uint8_t *addr)
{
	return 0x01 & addr[0];
}

static inline int is_broadcast_ether_addr(const uint8_t *addr)
{
	return (addr[0] & addr[1] & addr[2] & addr[3] & addr[4] & addr[5]) == 0xff;
}

int ether_host_filter_fn (void *private_data,
			  const unsigned char *outdata,
			  ssize_t outdata_len,
			  uint8_t tx_rx,
			  knet_node_id_t this_host_id,
			  knet_node_id_t src_host_id,
			  int8_t *channel,
			  knet_node_id_t *dst_host_ids,
			  size_t *dst_host_ids_entries)
{
	struct ether_header *eth_h = (struct ether_header *)outdata;
	uint8_t *dst_mac = (uint8_t *)eth_h->ether_dhost;
	uint16_t dst_host_id;

	if (is_zero_ether_addr(dst_mac))
		return -1;

	if (is_multicast_ether_addr(dst_mac) ||
	    is_broadcast_ether_addr(dst_mac)) {
		return 1;
	}

	memmove(&dst_host_id, &dst_mac[4], 2);

	dst_host_ids[0] = ntohs(dst_host_id);
	*dst_host_ids_entries = 1;

	return 0;
}
