/*
 * Copyright (C) 2026 Red Hat, Inc.  All rights reserved.
 *
 * Routines for the corosync 3 protocol as used with Kronosnet
 *
 * Authors: Christine Caulfield <ccaulfie@redhat.com>
 *
 * This software licensed under LGPL-2.0+
 */

#ifndef PACKET_COROSYNC_TOTEMKNET_H
#define PACKET_COROSYNC_TOTEMKNET_H

void proto_register_corosync_totemknet(void);
void proto_reg_handoff_corosync_totemknet(void);
WS_DLL_PUBLIC_DEF void plugin_register(void);

#endif /* PACKET_KRONOSNET_H */
