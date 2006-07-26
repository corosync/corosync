/*
 * Copyright (C) 2006 Red Hat, Inc.
 * Copyright (C) 2006 Sun Microsystems, Inc.
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

#ifndef AIS_MAR_CLM_H_DEFINED
#define AIS_MAR_CLM_H_DEFINED

#include "swab.h"
#include "saAis.h"
#include "saClm.h"
#include "mar_gen.h"

#define MAR_CLM_MAX_ADDRESS_LENGTH 64

typedef enum {
		MAR_CLM_AF_INET = 1,
		MAR_CLM_AF_INET6 = 2
} mar_clm_node_address_family_t;

/*
 * Marshalling the SaClmNodeAdressT data structure
 */
typedef struct {
	unsigned short length __attribute__((aligned(8)));
	mar_clm_node_address_family_t family __attribute__((aligned(8)));
	unsigned char value[MAR_CLM_MAX_ADDRESS_LENGTH] __attribute__((aligned(8)));
} mar_clm_node_address_t;

static inline void swab_mar_clm_node_address_t(mar_clm_node_address_t *to_swab)
{
	swab_mar_uint16_t (&to_swab->length);
	swab_mar_uint32_t (&to_swab->family);
}

static inline void marshall_from_mar_clm_node_address_t (
	SaClmNodeAddressT *dest,
	mar_clm_node_address_t *src)
{
	dest->family = src->family;
	dest->length = src->length;
	memcpy (dest->value, src->value, SA_CLM_MAX_ADDRESS_LENGTH);	
}

static inline void marshall_to_mar_clm_node_address_t (
	mar_clm_node_address_t *dest,
	SaClmNodeAddressT *src)
{
	dest->family = src->family;
	dest->length = src->length;
	memcpy (dest->value, src->value, SA_CLM_MAX_ADDRESS_LENGTH);	
}
	
/*
 * Marshalling the SaClmClusterNodeT data structure
 */
typedef struct {
	mar_uint32_t node_id __attribute__((aligned(8)));
	mar_clm_node_address_t node_address __attribute__((aligned(8)));
	mar_name_t node_name __attribute__((aligned(8)));
	mar_uint32_t member __attribute__((aligned(8)));
	mar_uint64_t boot_timestamp __attribute__((aligned(8)));
	mar_uint64_t initial_view_number __attribute__((aligned(8)));
} mar_clm_cluster_node_t;

static inline void swab_mar_clm_cluster_node_t(mar_clm_cluster_node_t *to_swab)
{
	swab_mar_uint32_t (&to_swab->node_id);
	swab_mar_uint32_t (&to_swab->member);
	swab_mar_clm_node_address_t (&to_swab->node_address);
	swab_mar_name_t (&to_swab->node_name);
	swab_mar_uint64_t (&to_swab->boot_timestamp);
	swab_mar_uint64_t (&to_swab->initial_view_number);
}

static inline void marshall_to_mar_clm_cluster_node_t (
	mar_clm_cluster_node_t *dest,
	SaClmClusterNodeT *src)
{
	dest->node_id = src->nodeId;
	marshall_to_mar_clm_node_address_t (&dest->node_address,
		&src->nodeAddress);
	marshall_to_mar_name_t (&dest->node_name, &src->nodeName);
	dest->member = src->member;
	dest->boot_timestamp = src->bootTimestamp;
	dest->initial_view_number = src->initialViewNumber;
}

static inline void marshall_from_mar_clm_cluster_node_t (
	SaClmClusterNodeT *dest,
	mar_clm_cluster_node_t *src)
{
	dest->nodeId = src->node_id;
	marshall_from_mar_clm_node_address_t (&dest->nodeAddress,
		&src->node_address);
	marshall_from_mar_name_t (&dest->nodeName, &src->node_name);
	dest->member = src->member;
	dest->bootTimestamp = src->boot_timestamp;
	dest->initialViewNumber = src->initial_view_number;
}

typedef enum {
	MAR_NODE_NO_CHANGE = 1,
	MAR_NODE_JOINED = 2,
	MAR_NODE_LEFT = 3,
	MAR_NODE_RECONFIGURED = 4
} mar_clm_cluster_change_t;

/*
 * Marshalling the SaClmClusterNotificationT data structure
 */
typedef struct {
	mar_clm_cluster_node_t cluster_node __attribute__((aligned(8)));
	mar_clm_cluster_change_t cluster_change __attribute__((aligned(8)));
} mar_clm_cluster_notification_t;

static inline void marshall_to_mar_clm_cluster_notification_t (
	mar_clm_cluster_notification_t *dest,
	SaClmClusterNotificationT *src)
{
	marshall_to_mar_clm_cluster_node_t (&dest->cluster_node,
		&src->clusterNode);
	dest->cluster_change = src->clusterChange;
}

static inline void marshall_from_mar_clm_cluster_notification_t (
	SaClmClusterNotificationT *dest,
	mar_clm_cluster_notification_t *src)
{
	marshall_from_mar_clm_cluster_node_t (&dest->clusterNode,
		&src->cluster_node);
	dest->clusterChange = src->cluster_change;
}

typedef struct {
	unsigned long long view_number __attribute__((aligned(8)));
	unsigned int number_of_items __attribute__((aligned(8)));
	unsigned long long notification __attribute__((aligned(8)));
} mar_clm_cluster_notification_buffer_t;

static inline void marshall_to_mar_cluster_notification_buffer_t (
	mar_clm_cluster_notification_buffer_t *dest,
	SaClmClusterNotificationBufferT *src)
{
	dest->view_number = src->viewNumber;
	dest->number_of_items = src->numberOfItems;
	memcpy (&dest->notification, &src->notification,
		sizeof (SaClmClusterNotificationBufferT *));
}

static inline void marshall_from_mar_cluster_notification_buffer_t (
	SaClmClusterNotificationBufferT *dest,
	mar_clm_cluster_notification_buffer_t *src)
{
	dest->viewNumber = src->view_number;
	dest->numberOfItems = src->number_of_items;
	memcpy (&dest->notification, &src->notification,
		sizeof (SaClmClusterNotificationBufferT *));
}

#endif /* AIS_MAR_CLM_H_DEFINED */
