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

#ifndef _NSS_SOCK_H_
#define _NSS_SOCK_H_

#include <nss.h>
#include <ssl.h>
#include <prnetdb.h>

#ifdef __cplusplus
extern "C" {
#endif

struct nss_sock_non_blocking_client {
	char *host_name;
	uint16_t port;
	PRIntn af;
	PRFileDesc *socket;
	PRAddrInfo *addr_info;
	void *addr_iter;
	unsigned int connect_attempts;
	int destroyed;
};

extern int		nss_sock_init_nss(char *config_dir);

extern PRFileDesc	*nss_sock_create_listen_socket(const char *hostname, uint16_t port,
    PRIntn af);

extern int		nss_sock_set_non_blocking(PRFileDesc *sock);

extern PRFileDesc 	*nss_sock_create_client_socket(const char *hostname, uint16_t port,
    PRIntn af, PRIntervalTime timeout);

extern PRFileDesc	*nss_sock_start_ssl_as_client(PRFileDesc *input_sock, const char *ssl_url,
    SSLBadCertHandler bad_cert_hook, SSLGetClientAuthData client_auth_hook,
    void *client_auth_hook_arg, int force_handshake, int *reset_would_block);

extern PRFileDesc	*nss_sock_start_ssl_as_server(PRFileDesc *input_sock,
    CERTCertificate *server_cert, SECKEYPrivateKey *server_key, int require_client_cert,
    int force_handshake, int *reset_would_block);

extern int		 nss_sock_non_blocking_client_init(const char *host_name,
    uint16_t port, PRIntn af, struct nss_sock_non_blocking_client *client);

extern int		 nss_sock_non_blocking_client_try_next(
    struct nss_sock_non_blocking_client *client);

extern void		 nss_sock_non_blocking_client_destroy(
    struct nss_sock_non_blocking_client *client);

extern int		 nss_sock_non_blocking_client_succeeded(const PRPollDesc *pfd);

#ifdef __cplusplus
}
#endif

#endif /* _NSS_SOCK_H_ */
