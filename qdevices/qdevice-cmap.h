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

#ifndef _QDEVICE_CMAP_H_
#define _QDEVICE_CMAP_H_

#include <cmap.h>

#include "node-list.h"
#include "qdevice-instance.h"

#ifdef __cplusplus
extern "C" {
#endif

struct qdevice_cmap_change_events {
	unsigned int logging	: 1;
	unsigned int node_list	: 1;
	unsigned int heuristics	: 1;
};

extern int		qdevice_cmap_get_nodelist(cmap_handle_t cmap_handle,
    struct node_list *list);

extern int		qdevice_cmap_get_config_version(cmap_handle_t cmap_handle,
    uint64_t *config_version);

extern void		qdevice_cmap_init(struct qdevice_instance *instance);

extern int		qdevice_cmap_add_track(struct qdevice_instance *instance);

extern int		qdevice_cmap_del_track(struct qdevice_instance *instance);

extern void		qdevice_cmap_destroy(struct qdevice_instance *instance);

extern int		qdevice_cmap_dispatch(struct qdevice_instance *instance);

extern int		qdevice_cmap_store_config_node_list(struct qdevice_instance *instance);

#ifdef __cplusplus
}
#endif

#endif /* _QDEVICE_CMAP_H_ */
