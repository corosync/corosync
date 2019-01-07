/*
 * Copyright (C) 2012-2018 Red Hat, Inc.  All rights reserved.
 *
 * Author: Fabio M. Di Nitto <fabbione@kronosnet.org>
 *
 * This software licensed under GPL-2.0+, LGPL-2.0+
 */

#ifndef __ETHERFILTER_H__
#define __ETHERFILTER_H__

#include <stdint.h>
#include "libknet.h"

int ether_host_filter_fn (void *private_data,
			  const unsigned char *outdata,
			  ssize_t outdata_len,
			  uint8_t tx_rx,
			  knet_node_id_t this_host_id,
			  knet_node_id_t src_host_id,
			  int8_t *channel,
			  knet_node_id_t *dst_host_ids,
			  size_t *dst_host_ids_entries);

#endif
