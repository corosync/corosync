/*
 * Copyright (c) 2009-2011 Red Hat, Inc.
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

#ifndef CORODEFS_H_DEFINED
#define CORODEFS_H_DEFINED

#include <netinet/in.h>

/**
 * @brief The corosync_service_types enum
 */
enum corosync_service_types {
	CMAP_SERVICE = 0,
	CFG_SERVICE = 1,
	CPG_SERVICE = 2,
	QUORUM_SERVICE = 3,
	PLOAD_SERVICE = 4,
	VOTEQUORUM_SERVICE = 5,
	MON_SERVICE = 6,
	WD_SERVICE = 7,
};

#ifdef HAVE_SMALL_MEMORY_FOOTPRINT
#define PROCESSOR_COUNT_MAX 16
#else
#define PROCESSOR_COUNT_MAX 384
#endif /* HAVE_SMALL_MEMORY_FOOTPRINT */

#define TOTEMIP_ADDRLEN (sizeof(struct in6_addr))

#endif /* CORODEFS_H_DEFINED */
