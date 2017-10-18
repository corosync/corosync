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

#ifndef _UTILS_H_
#define _UTILS_H_

#include <sys/types.h>
#include <inttypes.h>

#define UTILS_PRI_NODE_ID		"%" PRIu32
#define UTILS_PRI_DATACENTER_ID		"%" PRIu32
/*
#define UTILS_PRI_NODE_ID		"0x%" PRIx32
#define UTILS_PRI_DATACENTER_ID		"0x%" PRIx32
*/
#define UTILS_PRI_MSG_SEQ		"%" PRIu32
#define UTILS_PRI_RING_ID		"%" PRIx32 ".%" PRIx64
#define UTILS_PRI_CONFIG_VERSION	"%" PRIu64
#define UTILS_PRI_EXPECTED_VOTES	"%" PRIu32

#ifdef __cplusplus
extern "C" {
#endif

extern int		utils_parse_bool_str(const char *str);

extern int		utils_flock(const char *lockfile, pid_t pid,
    int *another_instance_running);

extern void		utils_tty_detach(void);

extern int		utils_fd_set_non_blocking(int fd);

#ifdef __cplusplus
}
#endif

#endif /* _UTILS_H_ */
