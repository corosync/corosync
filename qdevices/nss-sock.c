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

#include <limits.h>

#include "nss-sock.h"

int
nss_sock_init_nss(char *config_dir)
{
	if (config_dir == NULL) {
		if (NSS_NoDB_Init(NULL) != SECSuccess) {
			return (-1);
		}
	} else {
		if (NSS_Init(config_dir) != SECSuccess) {
			return (-1);
		}
	}

	if (NSS_SetDomesticPolicy() != SECSuccess) {
		return (-1);
	}

	return (0);
}

/*
 * Set NSS socket non-blocking
 */
int
nss_sock_set_non_blocking(PRFileDesc *sock)
{
	PRSocketOptionData sock_opt;

	memset(&sock_opt, 0, sizeof(sock_opt));
	sock_opt.option = PR_SockOpt_Nonblocking;
	sock_opt.value.non_blocking = PR_TRUE;
	if (PR_SetSocketOption(sock, &sock_opt) != PR_SUCCESS) {
		return (-1);
	}

	return (0);
}

/*
 * Create TCP socket with af family. If reuse_addr is set, socket option
 * for reuse address is set.
 */
static PRFileDesc *
nss_sock_create_socket(PRIntn af, int reuse_addr)
{
	PRFileDesc *sock;
	PRSocketOptionData socket_option;

	sock = PR_OpenTCPSocket(af);
	if (sock == NULL) {
		return (NULL);
	}

	if (reuse_addr) {
		socket_option.option = PR_SockOpt_Reuseaddr;
		socket_option.value.reuse_addr = PR_TRUE;
		if (PR_SetSocketOption(sock, &socket_option) != PR_SUCCESS) {
			return (NULL);
		}
	}

	return (sock);
}

/*
 * Create listen socket and bind it to address. hostname can be NULL and then
 * any address is used. Address family (af) can be ether PR_AF_INET6,
 * PR_AF_INET or PR_AF_UNSPEC.
 */
PRFileDesc *
nss_sock_create_listen_socket(const char *hostname, uint16_t port, PRIntn af)
{
	PRNetAddr addr;
	PRFileDesc *sock;
	PRAddrInfo *addr_info;
	void *addr_iter;

	sock = NULL;

	if (hostname == NULL) {
		memset(&addr, 0, sizeof(addr));

		if (PR_InitializeNetAddr(PR_IpAddrAny, port, &addr) != PR_SUCCESS) {
			return (NULL);
		}
		if (af == PR_AF_UNSPEC) {
			af = PR_AF_INET6;
		}
		addr.raw.family = af;

		sock = nss_sock_create_socket(af, 1);
		if (sock == NULL) {
			return (NULL);
		}

		if (PR_Bind(sock, &addr) != PR_SUCCESS) {
			PR_Close(sock);

			return (NULL);
		}
	} else {
		addr_info = PR_GetAddrInfoByName(hostname, af, PR_AI_ADDRCONFIG);
		if (addr_info == NULL) {
			return (NULL);
		}

		addr_iter = NULL;

		while ((addr_iter = PR_EnumerateAddrInfo(addr_iter, addr_info, port,
		    &addr)) != NULL) {
			if (af == PR_AF_UNSPEC || addr.raw.family == af) {
				sock = nss_sock_create_socket(addr.raw.family, 1);
				if (sock == NULL) {
					continue;
				}

				if (PR_Bind(sock, &addr) != PR_SUCCESS) {
					PR_Close(sock);
					sock = NULL;

					continue;
				}

				/*
				 * Socket is sucesfully bound
				 */
				break;
			}
		}

		PR_FreeAddrInfo(addr_info);

		if (sock == NULL) {
			/*
			 * No address succeeded
			 */
			PR_SetError(PR_ADDRESS_NOT_AVAILABLE_ERROR, 0);

			return (NULL);
		}
	}

	return (sock);
}

PRFileDesc *
nss_sock_create_client_socket(const char *hostname, uint16_t port, PRIntn af,
    PRIntervalTime timeout)
{
	PRNetAddr addr;
	PRFileDesc *sock;
	PRAddrInfo *addr_info;
	void *addr_iter;
	PRStatus res;
	int connect_failed;
	PRIntn tmp_af;

	sock = NULL;
	connect_failed = 0;

	tmp_af = af;
	if (af == PR_AF_INET6) {
		tmp_af = PR_AF_UNSPEC;
	}

	addr_info = PR_GetAddrInfoByName(hostname, tmp_af, PR_AI_ADDRCONFIG);
	if (addr_info == NULL) {
		return (NULL);
	}

	addr_iter = NULL;

	while ((addr_iter = PR_EnumerateAddrInfo(addr_iter, addr_info, port, &addr)) != NULL) {
		if (af != PR_AF_UNSPEC && addr.raw.family != af) {
			continue;
		}

		sock = nss_sock_create_socket(addr.raw.family, 0);
		if (sock == NULL) {
			continue;
		}

		if ((res = PR_Connect(sock, &addr, timeout)) != PR_SUCCESS) {
			PR_Close(sock);
			sock = NULL;
			connect_failed = 1;
		}

		/*
		 * Connection attempt finished
		 */
		break;
	}

	PR_FreeAddrInfo(addr_info);

	if (sock == NULL && !connect_failed) {
		PR_SetError(PR_ADDRESS_NOT_AVAILABLE_ERROR, 0);
	}

	return (sock);
}

int
nss_sock_non_blocking_client_init(const char *host_name, uint16_t port, PRIntn af,
    struct nss_sock_non_blocking_client *client)
{
	PRIntn tmp_af;

	client->destroyed = 1;

	if ((client->host_name = strdup(host_name)) == NULL) {
		PR_SetError(PR_OUT_OF_MEMORY_ERROR, 0);

		return (-1);
	}

	client->port = port;
	client->af = af;

	tmp_af = af;
	if (af == PR_AF_INET6) {
		tmp_af = PR_AF_UNSPEC;
	}

	client->addr_info = PR_GetAddrInfoByName(client->host_name, tmp_af, PR_AI_ADDRCONFIG);
	if (client->addr_info == NULL) {
		free(client->host_name);

		return (-1);
	}
	client->addr_iter = NULL;
	client->connect_attempts = 0;
	client->socket = NULL;
	client->destroyed = 0;

	return (0);
}

int
nss_sock_non_blocking_client_try_next(struct nss_sock_non_blocking_client *client)
{
	PRNetAddr addr;
	PRStatus res;

	if (client->destroyed) {
		PR_SetError(PR_INVALID_ARGUMENT_ERROR, 0);
		return (-1);
	}

	if (client->socket != NULL) {
		PR_Close(client->socket);
		client->socket = NULL;
	}

	while ((client->addr_iter = PR_EnumerateAddrInfo(client->addr_iter, client->addr_info,
	    client->port, &addr)) != NULL) {
		if (client->af != PR_AF_UNSPEC && addr.raw.family != client->af) {
			continue;
		}

		client->socket = nss_sock_create_socket(addr.raw.family, 0);
		if (client->socket == NULL) {
			continue;
		}

		if (nss_sock_set_non_blocking(client->socket) == -1) {
			PR_Close(client->socket);
			client->socket = NULL;
			continue;
		}

		res = PR_Connect(client->socket, &addr, PR_INTERVAL_NO_TIMEOUT);
		if (res == PR_SUCCESS || PR_GetError() == PR_IN_PROGRESS_ERROR) {
			return (0);
		}

		PR_Close(client->socket);
		client->socket = NULL;

		if (client->connect_attempts < INT_MAX) {
			client->connect_attempts++;
		}
	}

	if (client->connect_attempts == 0) {
		PR_SetError(PR_ADDRESS_NOT_AVAILABLE_ERROR, 0);
	}

	return (-1);
}

void
nss_sock_non_blocking_client_destroy(struct nss_sock_non_blocking_client *client)
{

	if (client->destroyed) {
		return ;
	}

	if (client->addr_info != NULL) {
		PR_FreeAddrInfo(client->addr_info);
		client->addr_info = NULL;
	}

	free(client->host_name);
	client->host_name = NULL;

	client->destroyed = 1;
}

/*
 * -1 = Client connect failed
 *  0 = Client connect still in progress
 *  1 = Client successfuly connected
 */
int
nss_sock_non_blocking_client_succeeded(const PRPollDesc *pfd)
{
	int res;

	res = -1;

	if (PR_GetConnectStatus(pfd) == PR_SUCCESS) {
		res = 1;
	} else {
		if (PR_GetError() == PR_IN_PROGRESS_ERROR) {
			res = 0;
		} else {
			res = -1;
		}
	}

	return (res);
}

/*
 * Start client side SSL connection. This can block.
 *
 * ssl_url is expected server URL, bad_cert_hook is callback called when server certificate
 * verification fails.
 */
PRFileDesc *
nss_sock_start_ssl_as_client(PRFileDesc *input_sock, const char *ssl_url,
    SSLBadCertHandler bad_cert_hook, SSLGetClientAuthData client_auth_hook,
    void *client_auth_hook_arg, int force_handshake, int *reset_would_block)
{
	PRFileDesc *ssl_sock;

	if (force_handshake) {
		*reset_would_block = 0;
	}

	ssl_sock = SSL_ImportFD(NULL, input_sock);
	if (ssl_sock == NULL) {
		return (NULL);
	}

	if (SSL_SetURL(ssl_sock, ssl_url) != SECSuccess) {
		return (NULL);
	}

	if ((SSL_OptionSet(ssl_sock, SSL_SECURITY, PR_TRUE) != SECSuccess) ||
	    (SSL_OptionSet(ssl_sock, SSL_HANDSHAKE_AS_SERVER, PR_FALSE) != SECSuccess) ||
	    (SSL_OptionSet(ssl_sock, SSL_HANDSHAKE_AS_CLIENT, PR_TRUE) != SECSuccess)) {
		return (NULL);
	}
	if (bad_cert_hook != NULL && SSL_BadCertHook(ssl_sock, bad_cert_hook, NULL) != SECSuccess) {
		return (NULL);
	}

	if (client_auth_hook != NULL &&
	    (SSL_GetClientAuthDataHook(ssl_sock, client_auth_hook,
	    client_auth_hook_arg) != SECSuccess)) {
		return (NULL);
	}

	if (SSL_ResetHandshake(ssl_sock, PR_FALSE) != SECSuccess) {
		return (NULL);
	}

	if (force_handshake && SSL_ForceHandshake(ssl_sock) != SECSuccess) {
		if (PR_GetError() == PR_WOULD_BLOCK_ERROR) {
			/*
			 * Mask would block error.
			 */
			*reset_would_block = 1;
		} else {
			return (NULL);
		}
	}

	return (ssl_sock);
}

PRFileDesc *
nss_sock_start_ssl_as_server(PRFileDesc *input_sock, CERTCertificate *server_cert,
    SECKEYPrivateKey *server_key, int require_client_cert, int force_handshake,
    int *reset_would_block)
{
	PRFileDesc *ssl_sock;

	if (force_handshake) {
		*reset_would_block = 0;
	}

	ssl_sock = SSL_ImportFD(NULL, input_sock);
	if (ssl_sock == NULL) {
		return (NULL);
	}

	if (SSL_ConfigSecureServer(ssl_sock, server_cert, server_key,
	    NSS_FindCertKEAType(server_cert)) != SECSuccess) {
		return (NULL);
	}

	if ((SSL_OptionSet(ssl_sock, SSL_SECURITY, PR_TRUE) != SECSuccess) ||
	    (SSL_OptionSet(ssl_sock, SSL_HANDSHAKE_AS_SERVER, PR_TRUE) != SECSuccess) ||
	    (SSL_OptionSet(ssl_sock, SSL_HANDSHAKE_AS_CLIENT, PR_FALSE) != SECSuccess) ||
	    (SSL_OptionSet(ssl_sock, SSL_REQUEST_CERTIFICATE, require_client_cert) != SECSuccess) ||
	    (SSL_OptionSet(ssl_sock, SSL_REQUIRE_CERTIFICATE, require_client_cert) != SECSuccess)) {
		return (NULL);
	}

	if (SSL_ResetHandshake(ssl_sock, PR_TRUE) != SECSuccess) {
		return (NULL);
	}

	if (force_handshake && SSL_ForceHandshake(ssl_sock) != SECSuccess) {
		if (PR_GetError() == PR_WOULD_BLOCK_ERROR) {
			/*
			 * Mask would block error.
			 */
			*reset_would_block = 1;
		} else {
			return (NULL);
		}
	}

	return (ssl_sock);
}
