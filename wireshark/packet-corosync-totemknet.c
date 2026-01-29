/* packet-corosync-totemknet.c
 * Dissectors for totem single ring protocol implemented in corosync cluster engine v3
 * Copyright 2007 2009 2010 2014 Masatake YAMATO <yamato@redhat.com>
 * Updated for corosync 3 by Christine Caulfield <ccaulfie@redhat.com>
 * Copyright (c) 2010-2026 Red Hat, Inc.
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/* Fields description are taken from

   Y.AMIR, L.E.MOSER, P.M.MELLIAR-SMITH, D.A.AGARWAL, P.CIARFELLA.
   "The Totem Single-Ring Ordering and Membership Protocol"*/

#include <epan/packet.h>
#include <epan/aftypes.h>
#include <wsutil/plugins.h>

#include "packet-corosync-totemknet.h"

WS_DLL_PUBLIC_DEF const char plugin_version[] = "1.0";
WS_DLL_PUBLIC_DEF const int plugin_want_major = WIRESHARK_VERSION_MAJOR;
WS_DLL_PUBLIC_DEF const int plugin_want_minor = WIRESHARK_VERSION_MINOR;

/*
 * Utilities for subdissectors of corosync_totemknet.
 */
struct corosync_totemknet_info {
  unsigned encoding;
  unsigned nodeid;
};

/* Initialize the protocol and registered fields */
static int proto_corosync_totemknet;
static dissector_handle_t totemknet_handle;

static heur_dissector_list_t heur_subdissector_list;

/* fields for struct message_header */
static int hf_corosync_message_header_magic;
static int hf_corosync_message_header_version;
static int hf_corosync_message_header_type;
static int hf_corosync_message_header_encapsulated;
static int hf_corosync_message_header_nodeid;
static int hf_corosync_message_header_target_nodeid;
static int hf_corosync_totemknet_srp_addr;

/* fields for struct orf_token */
static int hf_corosync_totemknet_orf_token;
static int hf_corosync_totemknet_orf_token_seq;
static int hf_corosync_totemknet_orf_token_token_seq;
static int hf_corosync_totemknet_orf_token_aru;
static int hf_corosync_totemknet_orf_token_aru_addr;
static int hf_corosync_totemknet_orf_token_backlog;
static int hf_corosync_totemknet_orf_token_fcc;
static int hf_corosync_totemknet_orf_token_retrans_flg;
static int hf_corosync_totemknet_orf_token_rtr_list_entries;

/* field for struct memb_ring_id */
static int hf_corosync_totemknet_memb_ring_id;
static int hf_corosync_totemknet_memb_ring_id_rep;
static int hf_corosync_totemknet_memb_ring_id_seq;

/* field of struct mcast */
static int hf_corosync_totemknet_mcast;
static int hf_corosync_totemknet_mcast_seq;
static int hf_corosync_totemknet_mcast_this_seqno;
static int hf_corosync_totemknet_mcast_node_id;
static int hf_corosync_totemknet_mcast_system_from;
static int hf_corosync_totemknet_mcast_guarantee;

/* field of struct memb_merge_detect */
static int hf_corosync_totemknet_memb_merge_detect;
static int hf_corosync_totemknet_memb_merge_detect_system_from;

/* field of struct rtr_item */
static int hf_corosync_totemknet_rtr_item;
static int hf_corosync_totemknet_rtr_item_seq;

/* field of struct memb_join */
static int hf_corosync_totemknet_memb_join;
static int hf_corosync_totemknet_memb_join_system_from;
static int hf_corosync_totemknet_memb_join_proc_list_entries;
static int hf_corosync_totemknet_memb_join_proc_list_entry;
static int hf_corosync_totemknet_memb_join_failed_list_entries;
static int hf_corosync_totemknet_memb_join_failed_list_entry;
static int hf_corosync_totemknet_memb_join_ring_seq;

/* field of struct memb_commit_token  */
static int hf_corosync_totemknet_memb_commit_token;
static int hf_corosync_totemknet_memb_commit_token_token_seq;
static int hf_corosync_totemknet_memb_commit_token_retrans_flg;
static int hf_corosync_totemknet_memb_commit_token_memb_index;
static int hf_corosync_totemknet_memb_commit_token_addr_entries;
static int hf_corosync_totemknet_memb_commit_token_addr;

/* field of struct memb_commit_token_memb_entry  */
static int hf_corosync_totemknet_memb_commit_token_memb_entry;
static int hf_corosync_totemknet_memb_commit_token_memb_entry_aru;
static int hf_corosync_totemknet_memb_commit_token_memb_entry_high_delivered;
static int hf_corosync_totemknet_memb_commit_token_memb_entry_received_flg;

/* field of struct token_hold_cancel */
static int hf_corosync_totemknet_token_hold_cancel;

/* totemPG fields */
static int hf_corosync_totemknet_totempg;
static int hf_corosync_totemknet_totempg_mcast_header_version;
static int hf_corosync_totemknet_totempg_mcast_header_type;
static int hf_corosync_totemknet_totempg_mcast_fragmented;
static int hf_corosync_totemknet_totempg_mcast_continuation;
static int hf_corosync_totemknet_totempg_mcast_msg_count;
static int hf_corosync_totemknet_totempg_mcast_msg_len;
static int hf_corosync_totemknet_totempg_mcast_group_cnt;
static int hf_corosync_totemknet_totempg_mcast_group_len;
static int hf_corosync_totemknet_totempg_mcast_message;

/* CPG fields */
static int hf_corosync_totemknet_cpg;
static int hf_corosync_totemknet_cpg_ipc_header_id_service;
static int hf_corosync_totemknet_cpg_ipc_header_id_message;
static int hf_corosync_totemknet_cpg_ipc_header_size;
static int hf_corosync_totemknet_cpg_ipc_header_error;
static int hf_corosync_totemknet_cpg_name_name;
static int hf_corosync_totemknet_cpg_name_len;
static int hf_corosync_totemknet_cpg_name;

static int hf_corosync_totemknet_cpg_procjoin_pid;
static int hf_corosync_totemknet_cpg_procjoin_reason;
static int hf_corosync_totemknet_cpg_mcast_msglen;
static int hf_corosync_totemknet_cpg_mcast_pid;
static int hf_corosync_totemknet_cpg_mcast_source_nodeid;
static int hf_corosync_totemknet_cpg_mcast_source_conn;
static int hf_corosync_totemknet_cpg_mcast_message;
static int hf_corosync_totemknet_cpg_dlistold_left;
static int hf_corosync_totemknet_cpg_dlistold_node;
static int hf_corosync_totemknet_cpg_dlist_old;
static int hf_corosync_totemknet_cpg_dlist_left;
static int hf_corosync_totemknet_cpg_dlist_node;
static int hf_corosync_totemknet_cpg_pmcast_msglen;
static int hf_corosync_totemknet_cpg_pmcast_fraglen;
static int hf_corosync_totemknet_cpg_pmcast_pid;
static int hf_corosync_totemknet_cpg_pmcast_type;
static int hf_corosync_totemknet_cpg_pmcast_message;

/* Initialize the subtree pointers */
static int ett_corosync_totemknet;
static int ett_corosync_totemknet_orf_token;
static int ett_corosync_totemknet_memb_ring_id;
static int ett_corosync_totemknet_ip_address;
static int ett_corosync_totemknet_mcast;
static int ett_corosync_totemknet_memb_merge_detect;
static int ett_corosync_totemknet_rtr_item;
static int ett_corosync_totemknet_memb_join;
static int ett_corosync_totemknet_memb_commit_token;
static int ett_corosync_totemknet_memb_commit_token_memb_entry;
static int ett_corosync_totemknet_token_hold_cancel;
static int ett_corosync_totemknet_memb_join_proc_list;
static int ett_corosync_totemknet_memb_join_failed_list;
static int ett_corosync_totemknet_srp_addr;
static int ett_corosync_totemknet_totempg;
static int ett_corosync_totemknet_cpg;
static int ett_corosync_totemknet_cpg_name;

/*
 * Value strings
 */
#define COROSYNC_TOTEMKNET_MESSAGE_TYPE_ORF_TOKEN         0
#define COROSYNC_TOTEMKNET_MESSAGE_TYPE_MCAST             1
#define COROSYNC_TOTEMKNET_MESSAGE_TYPE_MEMB_MERGE_DETECT 2
#define COROSYNC_TOTEMKNET_MESSAGE_TYPE_MEMB_JOIN         3
#define COROSYNC_TOTEMKNET_MESSAGE_TYPE_MEMB_COMMIT_TOKEN 4
#define COROSYNC_TOTEMKNET_MESSAGE_TYPE_TOKEN_HOLD_CANCEL 5

static const value_string corosync_totemknet_message_header_type[] = {
  { COROSYNC_TOTEMKNET_MESSAGE_TYPE_ORF_TOKEN,         "orf"               },
  { COROSYNC_TOTEMKNET_MESSAGE_TYPE_MCAST,             "mcast"             },
  { COROSYNC_TOTEMKNET_MESSAGE_TYPE_MEMB_MERGE_DETECT, "merge rings"       },
  { COROSYNC_TOTEMKNET_MESSAGE_TYPE_MEMB_JOIN,         "join message"      },
  { COROSYNC_TOTEMKNET_MESSAGE_TYPE_MEMB_COMMIT_TOKEN, "commit token"      },
  { COROSYNC_TOTEMKNET_MESSAGE_TYPE_TOKEN_HOLD_CANCEL, "cancel"            },
  { 0, NULL                                                               }
};

#define COROSYNC_TOTEMKNET_MESSAGE_ENCAPSULATED     1
#define COROSYNC_TOTEMKNET_MESSAGE_NOT_ENCAPSULATED 2

static const value_string corosync_totemknet_message_header_encapsulated[] = {
  { 0,                                              "not mcast message" },
  { COROSYNC_TOTEMKNET_MESSAGE_ENCAPSULATED,         "encapsulated"      },
  { COROSYNC_TOTEMKNET_MESSAGE_NOT_ENCAPSULATED,     "not encapsulated"  },
  { 0, NULL                                                             }
};

enum cpg_message_req_types {
        MESSAGE_REQ_EXEC_CPG_PROCJOIN = 0,
        MESSAGE_REQ_EXEC_CPG_PROCLEAVE = 1,
        MESSAGE_REQ_EXEC_CPG_JOINLIST = 2,
        MESSAGE_REQ_EXEC_CPG_MCAST = 3,
        MESSAGE_REQ_EXEC_CPG_DOWNLIST_OLD = 4,
        MESSAGE_REQ_EXEC_CPG_DOWNLIST = 5,
        MESSAGE_REQ_EXEC_CPG_PARTIAL_MCAST = 6,
};


static const value_string cpg_msg_names[] = {
    { MESSAGE_REQ_EXEC_CPG_PROCJOIN,        "Proc Join"      },
    { MESSAGE_REQ_EXEC_CPG_PROCLEAVE,       "Proc Leave"     },
    { MESSAGE_REQ_EXEC_CPG_JOINLIST,        "Join List"      },
    { MESSAGE_REQ_EXEC_CPG_MCAST,           "Mcast"          },
    { MESSAGE_REQ_EXEC_CPG_DOWNLIST_OLD,    "Downlist (old)" },
    { MESSAGE_REQ_EXEC_CPG_DOWNLIST,        "Downlist"       },
    { MESSAGE_REQ_EXEC_CPG_PARTIAL_MCAST,   "Partial Mcast"  },
    { 0,                    NULL                  }
};


static uint16_t
corosync_totemknet_get_uint16(tvbuff_t* tvb, int offset, const unsigned encoding)
{
  if (encoding == ENC_LITTLE_ENDIAN)
    return tvb_get_letohs(tvb, offset);

  return tvb_get_ntohs(tvb, offset);
}

static uint32_t
corosync_totemknet_get_uint32(tvbuff_t* tvb, int offset, const unsigned encoding)
{
  if (encoding == ENC_LITTLE_ENDIAN)
    return tvb_get_letohl(tvb, offset);

  return tvb_get_ntohl(tvb, offset);
}

static uint64_t
corosync_totemknet_get_uint64(tvbuff_t* tvb, int offset, const unsigned encoding)
{
  if (encoding == ENC_LITTLE_ENDIAN)
    return tvb_get_letoh64(tvb, offset);

  return tvb_get_ntoh64(tvb, offset);
}


static int dissect_corosync_totemknet0(tvbuff_t *tvb, packet_info *pinfo, proto_tree *parent_tree,
                                      bool encapsulated);


static int dissect_mar_cpg_name(tvbuff_t *tvb, int offset,
                                packet_info *pinfo, proto_tree *parent,
                                const unsigned encoding)
{
  proto_tree *name_tree;
  proto_item *item;
  uint32_t name_len;

  item = proto_tree_add_item(parent, hf_corosync_totemknet_cpg_name, tvb, offset,
                             -1, encoding);
  name_tree = proto_item_add_subtree(item, ett_corosync_totemknet_cpg_name);

  proto_tree_add_item(name_tree, hf_corosync_totemknet_cpg_name_len,
                      tvb, offset, 4, encoding);
  name_len = corosync_totemknet_get_uint32(tvb, offset, encoding);
  offset += 4 + 4; // 8 aligned
  proto_tree_add_item(name_tree, hf_corosync_totemknet_cpg_name_name,
                      tvb, offset, name_len, encoding);
  offset += 128;

  return 128+4;
}

static int
dissect_corosync_totemknet_cpg(uint16_t cpg_msg, tvbuff_t *tvb, int offset,
                               packet_info *pinfo, proto_tree *parent,
                               const unsigned encoding)
{
  proto_tree *cpg_tree;
  proto_item *item;
  int left_nodes;
  int i;
  int cpg_msglen;

  item = proto_tree_add_item(parent, hf_corosync_totemknet_cpg, tvb, offset,
                             -1, encoding);
  cpg_tree = proto_item_add_subtree(item, ett_corosync_totemknet_cpg);


  offset += dissect_mar_cpg_name(tvb, offset, pinfo, cpg_tree, encoding);
  offset += 4; // Pad
  switch (cpg_msg) {
    case MESSAGE_REQ_EXEC_CPG_PROCJOIN:
    case MESSAGE_REQ_EXEC_CPG_PROCLEAVE:
      proto_tree_add_item(cpg_tree, hf_corosync_totemknet_cpg_procjoin_pid,
                          tvb, offset, 4, encoding);
      offset += 8; // Padded to 8
      proto_tree_add_item(cpg_tree, hf_corosync_totemknet_cpg_procjoin_reason,
                          tvb, offset, 4, encoding);
      offset += 8; // Padded to 8
      break;
    case MESSAGE_REQ_EXEC_CPG_JOINLIST:
      break;
    case MESSAGE_REQ_EXEC_CPG_MCAST:
      proto_tree_add_item(cpg_tree, hf_corosync_totemknet_cpg_mcast_msglen,
                          tvb, offset, 4, encoding);
      cpg_msglen = corosync_totemknet_get_uint32(tvb, offset, encoding);
      offset += 8; // Padded to 8
      proto_tree_add_item(cpg_tree, hf_corosync_totemknet_cpg_mcast_pid,
                          tvb, offset, 4, encoding);
      offset += 8; // Padded to 8
      proto_tree_add_item(cpg_tree, hf_corosync_totemknet_cpg_mcast_source_nodeid,
                          tvb, offset, 4, encoding);
      offset += 8; // Padded to 8
      proto_tree_add_item(cpg_tree, hf_corosync_totemknet_cpg_mcast_source_conn,
                          tvb, offset, 8, encoding);
      offset += 8; // Padded to 8
      proto_tree_add_item(cpg_tree, hf_corosync_totemknet_cpg_mcast_message,
                          tvb, offset, cpg_msglen, encoding);
      break;
    case MESSAGE_REQ_EXEC_CPG_DOWNLIST_OLD:
      proto_tree_add_item(cpg_tree, hf_corosync_totemknet_cpg_dlistold_left,
                          tvb, offset, 4, encoding);
      left_nodes = corosync_totemknet_get_uint32(tvb, offset, encoding);
      offset += 8;
      for (i=0; i<left_nodes; i++) {
        proto_tree_add_item(cpg_tree, hf_corosync_totemknet_cpg_dlistold_node,
                            tvb, offset, 4, encoding);
        offset += 8; // Padded to 8
      }
      break;
    case MESSAGE_REQ_EXEC_CPG_DOWNLIST:
      proto_tree_add_item(cpg_tree, hf_corosync_totemknet_cpg_dlist_old,
                          tvb, offset, 4, encoding);
      offset += 8; // Padded to 8
      proto_tree_add_item(cpg_tree, hf_corosync_totemknet_cpg_dlist_left,
                          tvb, offset, 4, encoding);
      left_nodes = corosync_totemknet_get_uint32(tvb, offset, encoding);
      offset += 8; // Padded to 8
      for (i=0; i<left_nodes; i++) {
        proto_tree_add_item(cpg_tree, hf_corosync_totemknet_cpg_dlist_node,
                            tvb, offset, 4, encoding);
        offset += 8; // Padded to 8
      }
      break;
    case MESSAGE_REQ_EXEC_CPG_PARTIAL_MCAST:
      proto_tree_add_item(cpg_tree, hf_corosync_totemknet_cpg_pmcast_msglen,
                          tvb, offset, 4, encoding);
      cpg_msglen = corosync_totemknet_get_uint32(tvb, offset, encoding);
      offset += 8; // Padded to 8
      proto_tree_add_item(cpg_tree, hf_corosync_totemknet_cpg_pmcast_fraglen,
                          tvb, offset, 4, encoding);
      offset += 8; // Padded to 8
      proto_tree_add_item(cpg_tree, hf_corosync_totemknet_cpg_pmcast_pid,
                          tvb, offset, 4, encoding);
      offset += 8; // Padded to 8
      proto_tree_add_item(cpg_tree, hf_corosync_totemknet_cpg_pmcast_type,
                          tvb, offset, 4, encoding);
      offset += 8; // Padded to 8
      proto_tree_add_item(cpg_tree, hf_corosync_totemknet_cpg_mcast_source_nodeid,
                          tvb, offset, 4, encoding);
      offset += 8; // Padded to 8
      proto_tree_add_item(cpg_tree, hf_corosync_totemknet_cpg_mcast_source_conn,
                          tvb, offset, 8, encoding);
      offset += 8; // Padded to 8
      proto_tree_add_item(cpg_tree, hf_corosync_totemknet_cpg_mcast_message,
                          tvb, offset, cpg_msglen, encoding);
      break;

  }
  return 0; // Caller already knows the message length

}

static int
dissect_corosync_totemknet_totempg(tvbuff_t *tvb, int offset,
                                   packet_info *pinfo, proto_tree *parent,
                                   const unsigned encoding)
{
  proto_tree *pg_tree;
  proto_tree *cpg_tree;
  proto_item *item;
  uint16_t msg_count;
  uint16_t group_count;
  uint16_t service;
  uint16_t cpg_msg;
  uint16_t msg_lens[32]; // Matches MAX_IOVECS_FROM_APP
  int saved_offset;
  int i;

  item = proto_tree_add_item(parent, hf_corosync_totemknet_totempg, tvb, offset,
                             -1, encoding);
  pg_tree = proto_item_add_subtree(item, ett_corosync_totemknet_totempg);

  proto_tree_add_item(pg_tree, hf_corosync_totemknet_totempg_mcast_header_version,
                      tvb, offset, 2, encoding);
  offset += 2;
  proto_tree_add_item(pg_tree, hf_corosync_totemknet_totempg_mcast_header_type,
                      tvb, offset, 2, encoding);
  offset += 2;
  proto_tree_add_item(pg_tree, hf_corosync_totemknet_totempg_mcast_fragmented,
                      tvb, offset, 1, encoding);
  offset += 1;
  proto_tree_add_item(pg_tree, hf_corosync_totemknet_totempg_mcast_continuation,
                      tvb, offset, 1, encoding);
  if (tvb_get_uint8(tvb, offset)) {
    // Don't decode continuation messages as they are just more data
    return 0;
  }
  offset += 1;


  proto_tree_add_item(pg_tree, hf_corosync_totemknet_totempg_mcast_msg_count,
                      tvb, offset, 2, encoding);
  msg_count = corosync_totemknet_get_uint16(tvb, offset, encoding);
  offset += 2;

  for (i = 0; i<msg_count; i++) {
    item = proto_tree_add_item(pg_tree, hf_corosync_totemknet_totempg_mcast_msg_len,
                        tvb, offset, 2, encoding);
    msg_lens[i] = corosync_totemknet_get_uint16(tvb, offset, encoding);
    offset += 2;
    proto_item_append_text(item, " (msg %d)", i+1);

  }
  proto_tree_add_item(pg_tree, hf_corosync_totemknet_totempg_mcast_group_cnt,
                      tvb, offset, 2, encoding);
  group_count = corosync_totemknet_get_uint16(tvb, offset, encoding);
  offset += 2;

  for (i = 0; i<group_count; i++) {
    item = proto_tree_add_item(pg_tree, hf_corosync_totemknet_totempg_mcast_group_len,
                      tvb, offset, 2, encoding);
    proto_item_append_text(item, " (group %d)", i+1);
    offset += 2;
  }
  offset += 1; // Skip for alignment

  for (i = 0; i<msg_count; i++) {
    saved_offset = offset;

    // All totem PG messages
    item = proto_tree_add_item(pg_tree, hf_corosync_totemknet_cpg, tvb, offset,
                               -1, encoding);
    cpg_tree = proto_item_add_subtree(item, ett_corosync_totemknet_cpg);


    // first, a qb_ipc_response_header
    proto_tree_add_item(cpg_tree, hf_corosync_totemknet_cpg_ipc_header_id_message,
                        tvb, offset, 2, encoding);
    cpg_msg = corosync_totemknet_get_uint16(tvb, offset, encoding);
    offset += 2;
    proto_tree_add_item(cpg_tree, hf_corosync_totemknet_cpg_ipc_header_id_service,
                        tvb, offset, 2, encoding);
    service = corosync_totemknet_get_uint16(tvb, offset, encoding);
    offset += 2;
    offset += 4; // 8 aligned
    proto_tree_add_item(cpg_tree, hf_corosync_totemknet_cpg_ipc_header_size,
                        tvb, offset, 4, encoding);
    offset += 4;
    proto_tree_add_item(cpg_tree, hf_corosync_totemknet_cpg_ipc_header_error,
                        tvb, offset, 4, encoding);
    offset += 4;

    // if service == 2 then it's CPG
    if (service == 2) {
      dissect_corosync_totemknet_cpg(cpg_msg, tvb, offset, pinfo, cpg_tree, encoding);
    } else {
      // Just call it 'data'
      proto_tree_add_item(pg_tree, hf_corosync_totemknet_totempg_mcast_message,
                          tvb, offset, msg_lens[i], encoding);
    }
    offset = saved_offset + msg_lens[i];
  }

  return offset;
}


static int
dissect_corosync_totemknet_memb_ring_id(tvbuff_t *tvb,
                                       __attribute__((unused)) packet_info *pinfo, proto_tree *parent_tree,
                                       __attribute__((unused)) unsigned length, int offset,
                                       const unsigned encoding,
                                       unsigned *node_id,
                                       uint64_t *ring_id)
{
  int original_offset = offset;
  proto_tree *tree;
  proto_item *item;
  uint64_t rid;
  unsigned nid;

  item = proto_tree_add_item(parent_tree, hf_corosync_totemknet_memb_ring_id, tvb, offset,
                               -1, encoding);
  tree = proto_item_add_subtree(item, ett_corosync_totemknet_memb_ring_id);

  proto_tree_add_item(tree, hf_corosync_totemknet_memb_ring_id_rep,
                        tvb, offset, 4, encoding);
  nid = corosync_totemknet_get_uint32(tvb, offset, encoding);
  offset += 4;

  proto_tree_add_item(tree, hf_corosync_totemknet_memb_ring_id_seq,
                        tvb, offset, 8, encoding);
  rid = corosync_totemknet_get_uint64(tvb, offset, encoding);
  offset += 8;

  proto_item_append_text(item, " (ring: %" PRIu64 ")", rid);

  if (node_id)
    *node_id = nid;
  if (ring_id)
    *ring_id = rid;

  proto_item_set_len(item, offset - original_offset);
  return offset - original_offset;
}

static int
dissect_corosync_totemknet_rtr_list(tvbuff_t *tvb,
                                   packet_info *pinfo, proto_tree *parent_tree,
                                   unsigned length, int offset,
                                   const unsigned encoding)
{
  int original_offset = offset;
  proto_tree *tree;
  proto_item *item;

  unsigned node_id;
  uint64_t ring_id;
  uint32_t seq;

  item = proto_tree_add_item(parent_tree, hf_corosync_totemknet_rtr_item, tvb, offset,
                               -1, ENC_NA);
  tree = proto_item_add_subtree(item, ett_corosync_totemknet_rtr_item);

  offset += dissect_corosync_totemknet_memb_ring_id(tvb, pinfo, tree,
                                                        length, offset,
                                                        encoding,
                                                        &node_id,
                                                        &ring_id);

  proto_tree_add_item(tree, hf_corosync_totemknet_rtr_item_seq,
                        tvb, offset, 4, encoding);

  seq = corosync_totemknet_get_uint32(tvb, offset, encoding);
  proto_item_append_text(item, " (ring: %" PRIu64 " node: %u seq: %u)",
                           ring_id, node_id, seq);
  offset += 4;

  proto_item_set_len(item, offset - original_offset);
  return (offset - original_offset);
}

static int
dissect_corosync_totemknet_orf_token(tvbuff_t *tvb,
                                    packet_info *pinfo, proto_tree *parent_tree,
                                    unsigned length, int offset,
                                    const unsigned encoding)
{
  int original_offset = offset;
  uint32_t rtr_list_entries = 0, seq, aru, i;
  proto_tree *tree;
  proto_item *item;
  unsigned   node_id;
  uint64_t ring_id;

  item = proto_tree_add_item(parent_tree, hf_corosync_totemknet_orf_token,
                             tvb, offset, -1, ENC_NA);
  tree = proto_item_add_subtree(item, ett_corosync_totemknet_orf_token);

  proto_tree_add_item(tree, hf_corosync_totemknet_orf_token_seq,
                        tvb, offset, 4, encoding);
  offset += 4;

  proto_tree_add_item(tree, hf_corosync_totemknet_orf_token_token_seq,
                        tvb, offset, 4, encoding);
  seq = corosync_totemknet_get_uint32(tvb, offset, encoding);
  offset += 4;

  proto_tree_add_item(tree, hf_corosync_totemknet_orf_token_aru,
                        tvb, offset, 4, encoding);
  aru = corosync_totemknet_get_uint32(tvb, offset, encoding);
  offset += 4;

  proto_tree_add_item(tree, hf_corosync_totemknet_orf_token_aru_addr,
                        tvb, offset, 4, encoding);
  offset += 4;

  offset += dissect_corosync_totemknet_memb_ring_id(tvb, pinfo, tree,
                                                        length, offset,
                                                        encoding,
                                                        &node_id,
                                                        &ring_id);

  proto_tree_add_item(tree, hf_corosync_totemknet_orf_token_backlog,
                        tvb, offset, 4, encoding);
  offset += 4;

  proto_tree_add_item(tree, hf_corosync_totemknet_orf_token_fcc,
                        tvb, offset, 4, encoding);
  offset += 4;

  proto_tree_add_item(tree, hf_corosync_totemknet_orf_token_retrans_flg,
                        tvb, offset, 4, encoding);
  offset += 4;

  proto_tree_add_item(tree, hf_corosync_totemknet_orf_token_rtr_list_entries,
                        tvb, offset, 4, encoding);
  rtr_list_entries = corosync_totemknet_get_uint32(tvb, offset, encoding);
  offset += 4;

  for (i = 0; i < rtr_list_entries; i++) {
    offset += dissect_corosync_totemknet_rtr_list(tvb, pinfo,
                                                    tree,
                                                    length, offset,
                                                    encoding);
  }

  proto_item_append_text(item, " (ring: %" PRIu64 " node: %u nrtr: %d seq: %d au: %u)",
                           ring_id, node_id, rtr_list_entries, seq, aru);

  proto_item_set_len(item, offset - original_offset);
  return offset - original_offset;
}

static int
dissect_corosync_totemknet_srp_addr(tvbuff_t *tvb,
                                   packet_info *pinfo _U_, proto_tree *parent_tree,
                                   unsigned length _U_, int offset,
                                   int   hf,
                                   const unsigned encoding)
{
//  proto_tree_add_item(parent_tree, hf_corosync_totemknet_srp_addr, tvb, offset, 4, encoding);
  proto_tree_add_item(parent_tree, hf, tvb, offset, 4, encoding);
  return 4;
}

static int
// NOLINTNEXTLINE(misc-no-recursion)
dissect_corosync_totemknet_mcast(tvbuff_t *tvb,
                                  packet_info *pinfo, proto_tree *tree,
                                  unsigned length, int offset,
                                  uint8_t message_header__encapsulated,
                                  const unsigned encoding, proto_tree *parent_tree,
                                  struct corosync_totemknet_info *totemknet_info)
{
  int original_offset = offset;
  proto_tree *mcast_tree;

  proto_item *item;
  unsigned node_id;
  uint64_t ring_id;
  tvbuff_t *next_tvb;

  heur_dtbl_entry_t *hdtbl_entry = NULL;

  item = proto_tree_add_item(tree, hf_corosync_totemknet_mcast, tvb, offset,
                               -1, encoding);
  mcast_tree = proto_item_add_subtree(item, ett_corosync_totemknet_mcast);

  proto_tree_add_item(mcast_tree, hf_corosync_totemknet_mcast_system_from,
                        tvb, offset, 4, encoding);
  offset += 4;

  proto_tree_add_item(mcast_tree, hf_corosync_totemknet_mcast_seq,
                        tvb, offset, 4, encoding);
  offset += 4;

  proto_tree_add_item(mcast_tree, hf_corosync_totemknet_mcast_this_seqno,
                        tvb, offset, 4, encoding);
  offset += 4;

  offset += dissect_corosync_totemknet_memb_ring_id(tvb, pinfo, mcast_tree,
                                                        length, offset,
                                                        encoding,
                                                        &node_id,
                                                        &ring_id);

  proto_item_append_text(item, " (ring: %" PRIu64 " node: %u)",
                         ring_id, node_id);

  proto_tree_add_item(tree, hf_corosync_totemknet_mcast_node_id,
                      tvb, offset, 4, encoding);
  offset += 4;

  proto_tree_add_item(tree, hf_corosync_totemknet_mcast_guarantee,
                      tvb, offset, 4, encoding);
  offset += 4;

  next_tvb = tvb_new_subset_remaining(tvb, offset);

  if (message_header__encapsulated == COROSYNC_TOTEMKNET_MESSAGE_ENCAPSULATED)
  {
    offset += dissect_corosync_totemknet0(next_tvb, pinfo, tree, true);
  }
  else
  {
    // TotemPG header
    if (dissect_corosync_totemknet_totempg(tvb, offset, pinfo, mcast_tree, encoding) == 0) {
      if (dissector_try_heuristic(heur_subdissector_list,
                                next_tvb,
                                pinfo,
                                parent_tree,
                                &hdtbl_entry,
                                totemknet_info))
        offset = length;
    }
  }

  proto_item_set_len(item, offset - original_offset);
  return (offset - original_offset);
}


static int
dissect_corosync_totemknet_memb_merge_detect(tvbuff_t *tvb,
                                            packet_info *pinfo, proto_tree *parent_tree,
                                            unsigned length, int offset,
                                            const unsigned encoding)
{
  int original_offset = offset;
  proto_tree *tree;
  proto_item *item;
  unsigned node_id;
  uint64_t ring_id;

  item = proto_tree_add_item(parent_tree, hf_corosync_totemknet_memb_merge_detect, tvb, offset,
                               -1, ENC_NA);
  tree = proto_item_add_subtree(item, ett_corosync_totemknet_memb_merge_detect);

  proto_tree_add_item(tree, hf_corosync_totemknet_memb_merge_detect_system_from,
                      tvb, offset, 4, encoding);
  offset += 4;


  offset += dissect_corosync_totemknet_memb_ring_id(tvb, pinfo, tree,
                                                        length, offset,
                                                        encoding,
                                                        &node_id,
                                                        &ring_id);

  proto_item_append_text(item, " (ring: %" PRIu64 " node: %u)",
                           ring_id, node_id);

  proto_item_set_len(item, offset - original_offset);
  return (offset - original_offset);
}

static int
dissect_corosync_totemknet_memb_join(tvbuff_t *tvb,
                                    packet_info *pinfo, proto_tree *parent_tree,
                                    unsigned length, int offset,
                                    const unsigned encoding)
{
  int original_offset = offset;
  proto_tree *tree;
  proto_item *item;

  uint32_t proc_list_entries;
  proto_tree *proc_tree;

  uint32_t failed_list_entries;
  proto_tree *failed_tree;
  proto_item *failed_item;

  unsigned i;

  proto_item *proc_item;

  item = proto_tree_add_item(parent_tree, hf_corosync_totemknet_memb_join, tvb, offset,
                               -1, encoding);
  tree = proto_item_add_subtree(item, ett_corosync_totemknet_memb_join);

  proto_tree_add_item(tree, hf_corosync_totemknet_memb_join_system_from,
                      tvb, offset, 4, encoding);
  offset += 4;

  proc_item = proto_tree_add_item(tree, hf_corosync_totemknet_memb_join_proc_list_entries,
                                    tvb, offset, 4, encoding);
  proc_list_entries = corosync_totemknet_get_uint32(tvb, offset, encoding);
  offset += 4;

  failed_item = proto_tree_add_item(tree, hf_corosync_totemknet_memb_join_failed_list_entries,
                                      tvb, offset, 4, encoding);
  failed_list_entries = corosync_totemknet_get_uint32(tvb, offset, encoding);
  offset += 4;

  proto_tree_add_item(tree, hf_corosync_totemknet_memb_join_ring_seq,
                        tvb, offset, 8, encoding);
  offset += 8;

  proc_tree = proto_item_add_subtree(proc_item, ett_corosync_totemknet_memb_join_proc_list);

  proto_item_append_text(item, " (nprocs: %u nfailed: %u)",
                           proc_list_entries, failed_list_entries);

  for (i = 0; i < proc_list_entries; i++) {
    offset += dissect_corosync_totemknet_srp_addr(tvb, pinfo, proc_tree,
                                                  length, offset,
                                                  hf_corosync_totemknet_memb_join_proc_list_entry,
                                                  encoding);
  }

  failed_tree = proto_item_add_subtree(failed_item,
                                         ett_corosync_totemknet_memb_join_failed_list);

  for (i = 0; i < failed_list_entries; i++) {
    offset += dissect_corosync_totemknet_srp_addr(tvb, pinfo, failed_tree,
                                                  length, offset,
                                                  hf_corosync_totemknet_memb_join_failed_list_entry,
                                                  encoding);
  }

  proto_item_set_len(item, offset - original_offset);
  return (offset - original_offset);
}

static int
dissect_corosync_totemknet_memb_commit_token_memb_entry(tvbuff_t *tvb,
                                                       packet_info *pinfo,
                                                       proto_tree *parent_tree,
                                                       unsigned length, int offset,
                                                       const unsigned encoding,
                                                       unsigned *node_id,
                                                       uint64_t *ring_id)
{
  int original_offset = offset;

  proto_tree *tree;
  proto_item *item;

  item = proto_tree_add_item(parent_tree, hf_corosync_totemknet_memb_commit_token_memb_entry,
                               tvb, offset, -1, encoding);
  tree = proto_item_add_subtree(item, ett_corosync_totemknet_memb_commit_token_memb_entry);


  offset += dissect_corosync_totemknet_memb_ring_id(tvb, pinfo, tree,
                                                        length, offset,
                                                        encoding,
                                                        node_id,
                                                        ring_id);

  proto_tree_add_item(tree, hf_corosync_totemknet_memb_commit_token_memb_entry_aru,
                        tvb, offset, 4, encoding);
  offset += 4;

  proto_tree_add_item(tree, hf_corosync_totemknet_memb_commit_token_memb_entry_high_delivered,
                        tvb, offset, 4, encoding);
  offset += 4;

  proto_tree_add_item(tree, hf_corosync_totemknet_memb_commit_token_memb_entry_received_flg,
                        tvb, offset, 4, encoding);
  offset += 4;

  proto_item_set_len(item, offset - original_offset);
  return (offset - original_offset);
}

static int
dissect_corosync_totemknet_memb_commit_token(tvbuff_t *tvb,
                                            packet_info *pinfo, proto_tree *parent_tree,
                                            unsigned length, int offset,
                                            const unsigned encoding)
{
  int original_offset = offset;
  proto_tree *tree;
  proto_item *item;

  uint32_t i, addr_entries;

  uint32_t seq;
  unsigned node_id;
  uint64_t ring_id;

  item = proto_tree_add_item(parent_tree, hf_corosync_totemknet_memb_commit_token,
                               tvb, offset, -1, ENC_NA);
  tree = proto_item_add_subtree(item, ett_corosync_totemknet_memb_commit_token);

  proto_tree_add_item(tree, hf_corosync_totemknet_memb_commit_token_token_seq,
                        tvb, offset, 4, encoding);
  seq = corosync_totemknet_get_uint32(tvb, offset, encoding);
  offset += 4;

  offset += dissect_corosync_totemknet_memb_ring_id(tvb, pinfo, tree,
                                                    length, offset,
                                                    encoding,
                                                    &node_id,
                                                    &ring_id);

  proto_tree_add_item(tree, hf_corosync_totemknet_memb_commit_token_retrans_flg,
                        tvb, offset, 4, encoding);
  offset += 4;

  proto_tree_add_item(tree, hf_corosync_totemknet_memb_commit_token_memb_index,
                        tvb, offset, 4, encoding);
  offset += 4;

  proto_tree_add_item(tree, hf_corosync_totemknet_memb_commit_token_addr_entries,
                        tvb, offset, 4, encoding);
  addr_entries = corosync_totemknet_get_uint32(tvb, offset, encoding);
  offset += 4;

  for (i = 0; i < addr_entries; i++) {
    offset += dissect_corosync_totemknet_srp_addr(tvb, pinfo, tree,
                                                    length, offset,
                                                    hf_corosync_totemknet_memb_commit_token_addr,
                                                    encoding);
  }

  for (i = 0; i < addr_entries; i++) {
    offset += dissect_corosync_totemknet_memb_commit_token_memb_entry(tvb, pinfo, tree,
                                                                        length, offset,
                                                                        encoding,
                                                                        NULL,
                                                                        NULL);
  }

  proto_item_append_text(item, " (ring: %" PRIu64 " node: %u seq: %u entries: %u)",
                           ring_id, node_id, seq, addr_entries);

  proto_item_set_len(item, offset - original_offset);
  return (offset - original_offset);
}

static int
dissect_corosync_totemknet_token_hold_cancel(tvbuff_t *tvb,
                                            packet_info *pinfo, proto_tree *parent_tree,
                                            unsigned length, int offset,
                                            const unsigned encoding)
{
  int original_offset = offset;
  proto_tree *tree;
  proto_item *item;
  unsigned node_id;
  uint64_t ring_id;

  item = proto_tree_add_item(parent_tree, hf_corosync_totemknet_token_hold_cancel, tvb, offset,
                               -1, ENC_NA);
  tree = proto_item_add_subtree(item, ett_corosync_totemknet_token_hold_cancel);

  offset += dissect_corosync_totemknet_memb_ring_id(tvb, pinfo, tree,
                                                        length, offset,
                                                        encoding,
                                                        &node_id,
                                                        &ring_id);

  proto_item_append_text(item, " (ring: %" PRIu64 " node: %u)",
                             ring_id, node_id);

  proto_item_set_len(item, offset - original_offset);
  return (offset - original_offset);
}

static int
dissect_corosync_totemknet(tvbuff_t *tvb, packet_info *pinfo, proto_tree *parent_tree, void* data _U_)
{
  return dissect_corosync_totemknet0(tvb, pinfo, parent_tree, false);
}

#define COROSYNC_TOTEMKNET_TEST_LITTLE_ENDIAN    0x22FF
#define COROSYNC_TOTEMKNET_TEST_BIG_ENDIAN       0xFF22

static int
// NOLINTNEXTLINE(misc-no-recursion)
dissect_corosync_totemknet0(tvbuff_t *tvb,
                           packet_info *pinfo, proto_tree *tree,
                           bool encapsulated)
{
  proto_item *item;
  unsigned    length;
  int         offset = 0;
  proto_tree *corosync_tree;
  uint8_t     message_header__type;
  uint16_t    message_header__magic;
  uint8_t     message_header__encapsulated;

  unsigned encoding;
  struct corosync_totemknet_info info;

  /* Check that there's enough data */
  length = tvb_reported_length(tvb);
  if (length < 1 + 1 + 2 + 4)
    return 0;

  /* message header */
  message_header__magic = tvb_get_uint16(tvb, 0, 0);
  if (message_header__magic == 0xC070) {
    encoding = ENC_BIG_ENDIAN;
  } else if (message_header__magic == 0x70c0) {
    encoding = ENC_LITTLE_ENDIAN;
  } else {
    return 0;
  }
  message_header__encapsulated = tvb_get_uint8(tvb, 4);
  message_header__type = tvb_get_uint8(tvb, 3);

  if (encapsulated == false)
  {
    col_set_str(pinfo->cinfo, COL_PROTOCOL, "COROSYNC/TOTEMKNET");
    col_set_str(pinfo->cinfo, COL_INFO,
              ((message_header__type == COROSYNC_TOTEMKNET_MESSAGE_TYPE_MCAST)
               && (message_header__encapsulated == COROSYNC_TOTEMKNET_MESSAGE_ENCAPSULATED))?
              "ENCAPSULATED":
              val_to_str_const(message_header__type,
                               corosync_totemknet_message_header_type,
                               "Unknown"));
  }

  item = proto_tree_add_item(tree, proto_corosync_totemknet, tvb, offset, -1, ENC_NA);
  corosync_tree = proto_item_add_subtree(item, ett_corosync_totemknet);

  proto_tree_add_item(corosync_tree, hf_corosync_message_header_magic,
                        tvb, offset, 2, ENC_NA);
  offset += 2;
  proto_tree_add_item(corosync_tree, hf_corosync_message_header_version,
                        tvb, offset, 1, ENC_NA);
  offset += 1;

  proto_tree_add_item(corosync_tree, hf_corosync_message_header_type,
                        tvb, offset, 1, ENC_NA);
  offset += 1;

  proto_tree_add_item(corosync_tree, hf_corosync_message_header_encapsulated,
                        tvb, offset, 1, ENC_NA);
  offset += 1;

  proto_tree_add_item(corosync_tree, hf_corosync_message_header_nodeid,
                        tvb, offset, 4, encoding);
  info.nodeid = corosync_totemknet_get_uint32(tvb, offset, encoding);
  offset += 4;

  proto_tree_add_item(corosync_tree,
                        hf_corosync_message_header_target_nodeid,
                        tvb, offset, 4, encoding);
  info.encoding = encoding;
  offset += 4;

  increment_dissection_depth(pinfo);
  switch (message_header__type) {
  case COROSYNC_TOTEMKNET_MESSAGE_TYPE_ORF_TOKEN:
    dissect_corosync_totemknet_orf_token(tvb, pinfo, corosync_tree, length, offset, encoding);
    break;
  case COROSYNC_TOTEMKNET_MESSAGE_TYPE_MCAST:
    dissect_corosync_totemknet_mcast(tvb, pinfo, corosync_tree, length, offset,
                                    message_header__encapsulated,
                                    encoding, tree, &info);
    break;
  case COROSYNC_TOTEMKNET_MESSAGE_TYPE_MEMB_MERGE_DETECT:
    dissect_corosync_totemknet_memb_merge_detect(tvb, pinfo, corosync_tree, length, offset,
                                                encoding);
    break;
  case COROSYNC_TOTEMKNET_MESSAGE_TYPE_MEMB_JOIN:
    dissect_corosync_totemknet_memb_join(tvb, pinfo, corosync_tree, length, offset,
                                        encoding);
    break;
  case COROSYNC_TOTEMKNET_MESSAGE_TYPE_MEMB_COMMIT_TOKEN:
    dissect_corosync_totemknet_memb_commit_token(tvb, pinfo, corosync_tree, length, offset,
                                                encoding);
    break;
  case COROSYNC_TOTEMKNET_MESSAGE_TYPE_TOKEN_HOLD_CANCEL:
    dissect_corosync_totemknet_token_hold_cancel(tvb, pinfo, corosync_tree, length, offset,
                                                encoding);
    break;
  default:
    break;
  }
  decrement_dissection_depth(pinfo);

  return length;
}

void
proto_register_corosync_totemknet(void)
{
  static hf_register_info hf[] = {
    /* message_header */
    { &hf_corosync_message_header_magic,
      { "Magic", "corosync_totemknet.message_header.magic",
        FT_UINT16, BASE_HEX, NULL, 0x0,
        NULL, HFILL }},
    { &hf_corosync_message_header_version,
      { "Version", "corosync_totemknet.message_header.version",
        FT_INT8, BASE_DEC, NULL, 0x0,
        NULL, HFILL }},
    { &hf_corosync_message_header_type,
      { "Type", "corosync_totemknet.message_header.type",
        FT_INT8, BASE_DEC, VALS(corosync_totemknet_message_header_type), 0x0,
        NULL, HFILL }},
    { &hf_corosync_message_header_encapsulated,
      { "Encapsulated", "corosync_totemknet.message_header.encapsulated",
        FT_INT8, BASE_DEC, VALS(corosync_totemknet_message_header_encapsulated), 0x0,
        NULL, HFILL }},
    { &hf_corosync_message_header_nodeid,
      { "Node ID", "corosync_totemknet.message_header.nodeid",
        FT_UINT32, BASE_DEC, NULL, 0x0,
        NULL, HFILL }},
    { &hf_corosync_message_header_target_nodeid,
      { "Target Node ID", "corosync_totemknet.message_header.target_nodeid",
        FT_UINT32, BASE_DEC, NULL, 0x0,
        NULL, HFILL }},

    /* Orf_token */
    { &hf_corosync_totemknet_orf_token,
      { "Ordering, Reliability, Flow (ORF) control Token", "corosync_totemknet.orf_token",
        FT_NONE, BASE_NONE, NULL, 0x0,
        NULL, HFILL }},
    { &hf_corosync_totemknet_orf_token_seq,
      { "Sequence number allowing recognition of redundant copies of the token", "corosync_totemknet.orf_token.seq",
        FT_UINT32, BASE_DEC, NULL, 0x0,
        NULL, HFILL }},
    { &hf_corosync_totemknet_orf_token_token_seq,
      { "The largest sequence number", "corosync_totemknet.orf_token.seq",
        FT_UINT32, BASE_DEC, NULL, 0x0,
        "The largest sequence number of any message "
        "that has been broadcast on the ring"
        "[1]" ,
        HFILL }},
    { &hf_corosync_totemknet_orf_token_aru,
      { "Sequence number all received up to", "corosync_totemknet.orf_token.aru",
        FT_UINT32, BASE_DEC, NULL, 0x0,
        NULL, HFILL }},
    { &hf_corosync_totemknet_orf_token_aru_addr,
      { "ID of node setting ARU", "corosync_totemknet.orf_token.aru_addr",
        FT_UINT32, BASE_DEC, NULL, 0x0,
        NULL, HFILL }},
    { &hf_corosync_totemknet_orf_token_backlog,
      { "Backlog", "corosync_totemknet.orf_token.backlog",
        FT_UINT32, BASE_DEC, NULL, 0x0,
        "The sum of the number of new message waiting to be transmitted by each processor on the ring "
        "at the time at which that processor forwarded the token during the previous rotation"
        "[1]",
        HFILL }},
    { &hf_corosync_totemknet_orf_token_fcc,
      { "FCC",
        "corosync_totemknet.orf_token.fcc",
        FT_UINT32, BASE_DEC, NULL, 0x0,
        "A count of the number of messages broadcast by all processors "
        "during the previous rotation of the token"
        "[1]",
        HFILL }},
    { &hf_corosync_totemknet_orf_token_retrans_flg,
      { "Retransmission flag", "corosync_totemknet.orf_token.retrans_flg",
        FT_INT32, BASE_DEC, NULL, 0x0,
        NULL, HFILL }},
    { &hf_corosync_totemknet_orf_token_rtr_list_entries,
      { "The number of retransmission list entries", "corosync_totemknet.orf_token.rtr_list_entries",
        FT_INT32, BASE_DEC, NULL, 0x0,
        NULL, HFILL }},

    /* memb_ring_id */
    { &hf_corosync_totemknet_memb_ring_id,
      { "Member ring id", "corosync_totemknet.memb_ring_id",
        FT_NONE, BASE_NONE, NULL, 0x0,
        NULL, HFILL }},
    { &hf_corosync_totemknet_memb_ring_id_seq,
      { "Sequence in member ring id", "corosync_totemknet.memb_ring_id.seq",
        FT_UINT64, BASE_DEC, NULL, 0x0,
        NULL, HFILL }},
    { &hf_corosync_totemknet_memb_ring_id_rep,
      { "Sequence in member ring id", "corosync_totemknet.memb_ring_id.rep",
        FT_UINT32, BASE_DEC, NULL, 0x0,
        NULL, HFILL }},

    /* mcast */
    { &hf_corosync_totemknet_mcast,
      { "ring ordered multicast message", "corosync_totemknet.mcast",
        FT_NONE, BASE_NONE, NULL, 0x0,
        NULL, HFILL }},
    { &hf_corosync_totemknet_mcast_seq,
      {"Multicast sequence number", "corosync_totemknet.mcast.seq",
       FT_UINT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL }},
    { &hf_corosync_totemknet_mcast_this_seqno,
      {"This Sequence number", "corosync_totemknet.mcast.this_seqno",
       FT_INT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL }},
    { &hf_corosync_totemknet_mcast_node_id,
      {"Node id(unused?)", "corosync_totemknet.mcast.node_id",
       FT_UINT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL }},
    { &hf_corosync_totemknet_mcast_system_from,
      {"System from nodeid", "corosync_totemknet.mcast.system_from",
       FT_UINT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL }},

    { &hf_corosync_totemknet_mcast_guarantee,
      {"Guarantee", "corosync_totemknet.mcast.guarantee",
       FT_INT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL }},

    /* memb_merge_detect */
    { &hf_corosync_totemknet_memb_merge_detect,
      { "Merge rings if there are available rings", "corosync_totemknet.memb_merge_detect",
        FT_NONE, BASE_NONE, NULL, 0x0,
        NULL, HFILL }},
    { &hf_corosync_totemknet_memb_merge_detect_system_from,
      {"System from nodeid", "corosync_totemknet.memb_merge_detect.system_from",
       FT_UINT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL }},

    /* srp_addr */
    { &hf_corosync_totemknet_srp_addr,
      {"Single Ring Protocol Address", "corosync_totemknet.srp_addr",
       FT_UINT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL }},

    /* rtr_item */
    { &hf_corosync_totemknet_rtr_item,
      {"Retransmission Item", "corosync_totemknet.rtr_item",
       FT_NONE, BASE_NONE, NULL, 0x0,
       NULL, HFILL }},
    { &hf_corosync_totemknet_rtr_item_seq,
      {"Sequence of Retransmission Item", "corosync_totemknet.rtr_item.seq",
       FT_UINT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL }},

    /* memb_join */
    { &hf_corosync_totemknet_memb_join,
      {"Membership join message", "corosync_totemknet.memb_join",
       FT_NONE, BASE_NONE, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_memb_join_system_from,
      {"System from address", "corosync_totemknet.memb_join.system_from",
       FT_UINT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL }},

    { &hf_corosync_totemknet_memb_join_proc_list_entries,
      {"The number of processor list entries", "corosync_totemknet.memb_join.proc_list_entries",
       FT_UINT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_memb_join_proc_list_entry,
      {"Processor node", "corosync_totemknet.memb_join.proc_list_entry",
       FT_UINT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_memb_join_failed_list_entries,
      {"The number of failed list entries", "corosync_totemknet.memb_join.failed_list_entries",
       FT_UINT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_memb_join_failed_list_entry,
      {"Failed node", "corosync_totemknet.memb_join.failed_list_entry",
       FT_UINT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_memb_join_ring_seq,
      {"Ring sequence number", "corosync_totemknet.memb_join.ring_seq",
       FT_UINT64, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},

    /* memb_commit_token */
    { &hf_corosync_totemknet_memb_commit_token,
      {"Membership commit token", "corosync_totemknet.memb_commit_token",
       FT_NONE, BASE_NONE, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_memb_commit_token_token_seq,
      {"Token sequence", "corosync_totemknet.memb_commit_token.token_seq",
       FT_UINT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_memb_commit_token_retrans_flg,
      {"Retransmission flag", "corosync_totemknet.memb_commit_token.retrans_flg",
       FT_UINT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_memb_commit_token_memb_index,
      {"Member index", "corosync_totemknet.memb_commit_token.memb_index",
       FT_INT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_memb_commit_token_addr_entries,
      {"The number of address entries", "corosync_totemknet.memb_commit_token.addr_entries",
       FT_INT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_memb_commit_token_addr,
      {"Commit token address", "corosync_totemknet.memb_commit_token.addr",
       FT_INT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},

    /* memb_commit_token_memb_entry */
    { &hf_corosync_totemknet_memb_commit_token_memb_entry,
      { "Membership entry", "corosync_totemknet.memb_commit_token_memb_entry",
        FT_NONE, BASE_NONE, NULL, 0x0,
        NULL, HFILL}},
    { &hf_corosync_totemknet_memb_commit_token_memb_entry_aru,
      {"Sequence number all received up to", "corosync_totemknet.memb_commit_token_memb_entry.aru",
       FT_UINT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_memb_commit_token_memb_entry_high_delivered,
      {"High delivered", "corosync_totemknet.memb_commit_token_memb_entry.high_delivered",
       FT_UINT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_memb_commit_token_memb_entry_received_flg,
      {"Received flag", "corosync_totemknet.memb_commit_token_memb_entry.received_flg",
       FT_UINT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},

    /* token_hold_cancel */
    { &hf_corosync_totemknet_token_hold_cancel,
      {"Hold cancel token", "corosync_totemknet.token_hold_cancel",
       FT_NONE, BASE_NONE, NULL, 0x0,
       NULL, HFILL}},

    /* totempg */
    { &hf_corosync_totemknet_totempg,
      {"TotemPG message", "corosync_totemknet.totempg",
       FT_NONE, BASE_NONE, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_totempg_mcast_header_version,
      {"TotemPG header version", "corosync_totemknet.totempg.header.version",
       FT_UINT16, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_totempg_mcast_header_type,
      {"TotemPG header type", "corosync_totemknet.totempg.header.type",
       FT_UINT16, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_totempg_mcast_fragmented,
      {"TotemPG is fragmented", "corosync_totemknet.totempg.fragmented",
       FT_UINT8, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_totempg_mcast_continuation,
      {"TotemPG is continuation", "corosync_totemknet.totempg.continuation",
       FT_UINT8, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_totempg_mcast_msg_count,
      {"TotemPG message count", "corosync_totemknet.totempg.msg_count",
       FT_UINT16, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_totempg_mcast_msg_len,
      {"TotemPG message length", "corosync_totemknet.totempg.msg_len",
       FT_UINT16, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_totempg_mcast_group_cnt,
      {"TotemPG group count", "corosync_totemknet.totempg.group_cnt",
       FT_UINT16, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_totempg_mcast_group_len,
      {"TotemPG group length", "corosync_totemknet.totempg.group_len",
       FT_UINT16, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_totempg_mcast_message,
      {"TotemPG unknown data", "corosync_totemknet.totempg.message",
       FT_NONE, BASE_NONE, NULL, 0x0,
       NULL, HFILL}},

    /* CPG ipc_header */
    { &hf_corosync_totemknet_cpg,
      {"Closed Process Groups message", "corosync_totemknet.cpg",
       FT_NONE, BASE_NONE, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_cpg_ipc_header_id_service,
      {"CPG header ID service", "corosync_totemknet.cpg.header.id.service",
       FT_UINT16, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_cpg_ipc_header_id_message,
      {"CPG header ID message", "corosync_totemknet.cpg.header.id.message",
       FT_UINT16, BASE_DEC, VALS(cpg_msg_names), 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_cpg_ipc_header_size,
      {"CPG header size", "corosync_totemknet.cpg.header.size",
       FT_UINT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_cpg_ipc_header_error,
      {"CPG header error", "corosync_totemknet.cpg.header.error",
       FT_UINT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_cpg_name,
      {"CPG group name", "corosync_totemknet.cpg.name",
       FT_NONE, BASE_NONE, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_cpg_name_name,
      {"CPG name", "corosync_totemknet.cpg.name.name.name",
       FT_STRING, BASE_NONE, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_cpg_name_len,
      {"CPG name length", "corosync_totemknet.cpg.name.len",
       FT_UINT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},

    { &hf_corosync_totemknet_cpg_procjoin_pid,
      {"PID", "corosync_totemknet.cpg.procjoin.pid",
       FT_UINT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_cpg_procjoin_reason,
      {"Join/leave reason", "corosync_totemknet.cpg.procjoin.reason",
       FT_UINT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_cpg_mcast_msglen,
      {"mcast message length", "corosync_totemknet.cpg.mcast.msg_len",
       FT_UINT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_cpg_mcast_pid,
      {"PID", "corosync_totemknet.cpg.mcast.pid",
       FT_UINT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_cpg_mcast_source_nodeid,
      {"Source Nodeid", "corosync_totemknet.cpg.mcast.source_nodeid",
       FT_UINT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_cpg_mcast_source_conn,
      {"Source con ID", "corosync_totemknet.cpg.mcast.source_con",
       FT_UINT64, BASE_HEX, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_cpg_mcast_message,
      {"Message", "corosync_totemknet.cpg.mcast.message",
       FT_NONE, BASE_NONE, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_cpg_dlistold_left,
      {"Num of lefet nodes", "corosync_totemknet.cpg.dlist_old.left_nodes",
       FT_UINT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_cpg_dlistold_node,
      {"Node ID", "corosync_totemknet.cpg.dlist_old.node",
       FT_UINT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_cpg_pmcast_msglen,
      {"Message len", "corosync_totemknet.cpg.pmcast.msglen",
       FT_UINT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_cpg_pmcast_fraglen,
      {"Fragment len", "corosync_totemknet.cpg.pmcast.fraglen",
       FT_UINT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_cpg_pmcast_pid,
      {"PID", "corosync_totemknet.cpg.pmcast.pid",
       FT_UINT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_cpg_pmcast_type,
      {"Type", "corosync_totemknet.cpg.pmcast.type",
       FT_UINT32, BASE_DEC, NULL, 0x0,
       NULL, HFILL}},
    { &hf_corosync_totemknet_cpg_pmcast_message,
      {"Message", "corosync_totemknet.cpg.pmcast.message",
       FT_NONE, BASE_NONE, NULL, 0x0,
       NULL, HFILL}},

  };

  static int *ett[] = {
    &ett_corosync_totemknet,
    &ett_corosync_totemknet_orf_token,
    &ett_corosync_totemknet_memb_ring_id,
    &ett_corosync_totemknet_ip_address,
    &ett_corosync_totemknet_mcast,
    &ett_corosync_totemknet_memb_merge_detect,
    &ett_corosync_totemknet_srp_addr,
    &ett_corosync_totemknet_rtr_item,
    &ett_corosync_totemknet_memb_join,
    &ett_corosync_totemknet_memb_commit_token,
    &ett_corosync_totemknet_memb_commit_token_memb_entry,
    &ett_corosync_totemknet_token_hold_cancel,
    &ett_corosync_totemknet_memb_join_proc_list,
    &ett_corosync_totemknet_memb_join_failed_list

  };

  proto_corosync_totemknet = proto_register_protocol("Totem Single Ring Protocol implemented in Corosync Cluster Engine 3",
                                                    "COROSYNC/TOTEMKNET", "corosync_totemknet");
  proto_register_field_array(proto_corosync_totemknet, hf, array_length(hf));
  proto_register_subtree_array(ett, array_length(ett));

  heur_subdissector_list = register_heur_dissector_list_with_description("corosync_totemknet.mcast", "COROSYNC/TOTEMKNET multicast data", proto_corosync_totemknet);

  totemknet_handle = register_dissector( "corosync_totemknet", dissect_corosync_totemknet, proto_corosync_totemknet);
}

void
proto_reg_handoff_corosync_totemknet(void)
{
    dissector_add_uint_with_preference("udp.port", 5405, totemknet_handle);
  /* Nothing to be done.
     dissect_corosync_totemknet is directly called from kronsnet dissector. */
}

WS_DLL_PUBLIC_DEF void plugin_register(void)
{
    static proto_plugin plug_corosync_totemknet;

    plug_corosync_totemknet.register_protoinfo = proto_register_corosync_totemknet;
    plug_corosync_totemknet.register_handoff = proto_reg_handoff_corosync_totemknet;
    proto_register_plugin(&plug_corosync_totemknet);
}


/*
 * Editor modelines  -  https://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 2
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * vi: set shiftwidth=2 tabstop=8 expandtab:
 * :indentSize=2:tabSize=8:noTabs=true:
 */
