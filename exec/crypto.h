/*
 * Copyright (c) 2006-2012 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)
 *         Christine Caulfield (ccaulfie@redhat.com)
 *         Jan Friesse (jfriesse@redhat.com)
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
#ifndef CRYPTO_H_DEFINED
#define CRYPTO_H_DEFINED

#include <sys/types.h>

#define CRYPTO_HMAC_HASH_SIZE 20
struct crypto_security_header {
	unsigned char hash_digest[CRYPTO_HMAC_HASH_SIZE]; /* The hash *MUST* be first in the data structure */
	unsigned char salt[16]; /* random number */
	char msg[0];
} __attribute__((packed));

struct crypto_instance;

extern int crypto_authenticate_and_decrypt (struct crypto_instance *instance,
	struct iovec *iov,
	unsigned int iov_len);

extern int crypto_encrypt_and_sign (
	struct crypto_instance *instance,
	unsigned char *buf,
	size_t *buf_len,
	const struct iovec *iovec,
	unsigned int iov_len);

extern struct crypto_instance *crypto_init(
	const unsigned char *private_key,
	unsigned int private_key_len,
	void (*log_printf_func) (
		int level,
		int subsys,
		const char *function,
		const char *file,
		int line,
		const char *format,
		...)__attribute__((format(printf, 6, 7))),
	int log_level_security,
	int log_level_notice,
	int log_level_error,
	int log_subsys_id);

#endif /* CRYPTO_H_DEFINED */
