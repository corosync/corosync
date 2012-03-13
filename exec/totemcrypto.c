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

#include <config.h>

#include <assert.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sched.h>
#include <time.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <limits.h>

#include <corosync/sq.h>
#include <corosync/swab.h>
#include <corosync/list.h>
#include <qb/qbdefs.h>
#include <qb/qbloop.h>
#define LOGSYS_UTILS_ONLY 1
#include <corosync/logsys.h>
#include <corosync/totem/totem.h>
#include "totemcrypto.h"

#include "util.h"

#include <nss.h>
#include <pk11pub.h>
#include <pkcs11.h>
#include <prerror.h>

#define CRYPTO_HMAC_HASH_SIZE 20
struct crypto_security_header {
	unsigned char hash_digest[CRYPTO_HMAC_HASH_SIZE]; /* The hash *MUST* be first in the data structure */
	unsigned char salt[16]; /* random number */
	char msg[0];
} __attribute__((packed));

struct crypto_instance {
	PK11SymKey   *nss_sym_key;
	PK11SymKey   *nss_sym_key_sign;

	unsigned char private_key[1024];

	unsigned int private_key_len;

	int crypto_crypt_type;

	int crypto_hash_type;

	void (*log_printf_func) (
		int level,
		int subsys,
		const char *function,
		const char *file,
		int line,
		const char *format,
		...)__attribute__((format(printf, 6, 7)));

	int log_level_security;
	int log_level_notice;
	int log_level_error;
	int log_subsys_id;
};

#define log_printf(level, format, args...)				\
do {									\
        instance->log_printf_func (					\
		level, instance->log_subsys_id,			\
                __FUNCTION__, __FILE__, __LINE__,			\
		(const char *)format, ##args);				\
} while (0);

#define LOGSYS_PERROR(err_num, level, fmt, args...)						\
do {												\
	char _error_str[LOGSYS_MAX_PERROR_MSG_LEN];						\
	const char *_error_ptr = qb_strerror_r(err_num, _error_str, sizeof(_error_str));	\
        instance->totemudp_log_printf (								\
		level, instance->log_subsys_id,						\
                __FUNCTION__, __FILE__, __LINE__,						\
		fmt ": %s (%d)", ##args, _error_ptr, err_num);				\
	} while(0)

static void init_nss_crypto(struct crypto_instance *instance)
{
	PK11SlotInfo*      aes_slot = NULL;
	PK11SlotInfo*      sha1_slot = NULL;
	SECItem            key_item;
	SECStatus          rv;

	log_printf(instance->log_level_notice,
		"Initializing transmit/receive security: NSS AES256CBC/SHA1HMAC (mode %u).", 0);
	rv = NSS_NoDB_Init(".");
	if (rv != SECSuccess)
	{
		log_printf(instance->log_level_security, "NSS initialization failed (err %d)",
			PR_GetError());
		goto out;
	}

	/*
	 * TODO: use instance info!
	 */
	aes_slot = PK11_GetBestSlot(CKM_AES_CBC_PAD, NULL);
	if (aes_slot == NULL)
	{
		log_printf(instance->log_level_security, "Unable to find security slot (err %d)",
			PR_GetError());
		goto out;
	}

	sha1_slot = PK11_GetBestSlot(CKM_SHA_1_HMAC, NULL);
	if (sha1_slot == NULL)
	{
		log_printf(instance->log_level_security, "Unable to find security slot (err %d)",
			PR_GetError());
		goto out;
	}
	/*
	 * Make the private key into a SymKey that we can use
	 */
	key_item.type = siBuffer;
	key_item.data = instance->private_key;
	key_item.len = 32; /* Use 256 bits */

	instance->nss_sym_key = PK11_ImportSymKey(aes_slot,
		CKM_AES_CBC_PAD,
		PK11_OriginUnwrap, CKA_ENCRYPT|CKA_DECRYPT,
		&key_item, NULL);
	if (instance->nss_sym_key == NULL)
	{
		log_printf(instance->log_level_security, "Failure to import key into NSS (err %d)",
			PR_GetError());
		goto out;
	}

	instance->nss_sym_key_sign = PK11_ImportSymKey(sha1_slot,
		CKM_SHA_1_HMAC,
		PK11_OriginUnwrap, CKA_SIGN,
		&key_item, NULL);
	if (instance->nss_sym_key_sign == NULL) {
		log_printf(instance->log_level_security, "Failure to import key into NSS (err %d)",
			PR_GetError());
		goto out;
	}
out:
	return;
}

static int encrypt_and_sign_nss (
	struct crypto_instance *instance,
	const unsigned char *buf_in,
	const size_t buf_in_len,
	unsigned char *buf_out,
	size_t *buf_out_len)
{
	PK11Context*       enc_context = NULL;
	SECStatus          rv1, rv2;
	int                tmp1_outlen;
	unsigned int       tmp2_outlen;
	unsigned char      *outdata;
	SECItem            no_params;
	SECItem            iv_item;
	struct crypto_security_header *header;
	SECItem      *nss_sec_param;
	unsigned char nss_iv_data[16];
	SECStatus          rv;

	no_params.type = siBuffer;
	no_params.data = 0;
	no_params.len = 0;

	tmp1_outlen = tmp2_outlen = 0;

	outdata = buf_out + sizeof (struct crypto_security_header);
	header = (struct crypto_security_header *)buf_out;

	rv = PK11_GenerateRandom (
		nss_iv_data,
		sizeof (nss_iv_data));
	if (rv != SECSuccess) {
		log_printf(instance->log_level_security,
			"Failure to generate a random number %d",
			PR_GetError());
	}

	memcpy(header->salt, nss_iv_data, sizeof(nss_iv_data));
	iv_item.type = siBuffer;
	iv_item.data = nss_iv_data;
	iv_item.len = sizeof (nss_iv_data);

	nss_sec_param = PK11_ParamFromIV (
		CKM_AES_CBC_PAD,
		&iv_item);
	if (nss_sec_param == NULL) {
		log_printf(instance->log_level_security,
			"Failure to set up PKCS11 param (err %d)",
			PR_GetError());
		return (-1);
	}

	/*
	 * Create cipher context for encryption
	 */
	enc_context = PK11_CreateContextBySymKey (
		CKM_AES_CBC_PAD,
		CKA_ENCRYPT,
		instance->nss_sym_key,
		nss_sec_param);
	if (!enc_context) {
		char err[1024];
		PR_GetErrorText(err);
		err[PR_GetErrorTextLength()] = 0;
		log_printf(instance->log_level_security,
			"PK11_CreateContext failed (encrypt) crypt_type=%d (err %d): %s",
			CKM_AES_CBC_PAD,
			PR_GetError(), err);
		return -1;
	}
	rv1 = PK11_CipherOp(enc_context, outdata,
			    &tmp1_outlen, FRAME_SIZE_MAX - sizeof(struct crypto_security_header),
			    (unsigned char *)buf_in, buf_in_len);
	rv2 = PK11_DigestFinal(enc_context, outdata + tmp1_outlen, &tmp2_outlen,
			       FRAME_SIZE_MAX - tmp1_outlen);
	PK11_DestroyContext(enc_context, PR_TRUE);

	*buf_out_len = tmp1_outlen + tmp2_outlen;

	if (rv1 != SECSuccess || rv2 != SECSuccess)
		goto out;

	/* Now do the digest */
	enc_context = PK11_CreateContextBySymKey(CKM_SHA_1_HMAC,
		CKA_SIGN, instance->nss_sym_key_sign, &no_params);
	if (!enc_context) {
		char err[1024];
		PR_GetErrorText(err);
		err[PR_GetErrorTextLength()] = 0;
		log_printf(instance->log_level_security, "encrypt: PK11_CreateContext failed (digest) err %d: %s",
			PR_GetError(), err);
		return -1;
	}


	PK11_DigestBegin(enc_context);

	rv1 = PK11_DigestOp(enc_context, outdata - 16, *buf_out_len + 16);
	rv2 = PK11_DigestFinal(enc_context, header->hash_digest, &tmp2_outlen, sizeof(header->hash_digest));

	PK11_DestroyContext(enc_context, PR_TRUE);

	if (rv1 != SECSuccess || rv2 != SECSuccess)
		goto out;


	*buf_out_len = *buf_out_len + sizeof(struct crypto_security_header);
	SECITEM_FreeItem(nss_sec_param, PR_TRUE);
	return 0;

out:
	return -1;
}


static int authenticate_and_decrypt_nss (
	struct crypto_instance *instance,
	unsigned char *buf,
	int *buf_len)
{
	PK11Context*  enc_context = NULL;
	SECStatus     rv1, rv2;
	int           tmp1_outlen;
	unsigned int  tmp2_outlen;
	unsigned char outbuf[FRAME_SIZE_MAX];
	unsigned char digest[CRYPTO_HMAC_HASH_SIZE];
	unsigned char *outdata;
	int           result_len;
	unsigned char *data;
	unsigned char *inbuf;
	size_t        datalen;
	struct crypto_security_header *header = (struct crypto_security_header *)buf;
	SECItem no_params;
	SECItem ivdata;

	no_params.type = siBuffer;
	no_params.data = 0;
	no_params.len = 0;

	tmp1_outlen = tmp2_outlen = 0;
	inbuf = (unsigned char *)buf;
	datalen = *buf_len;
	data = inbuf + sizeof (struct crypto_security_header) - 16;
	datalen = datalen - sizeof (struct crypto_security_header) + 16;

	outdata = outbuf + sizeof (struct crypto_security_header);

	/* Check the digest */
	enc_context = PK11_CreateContextBySymKey (
		CKM_SHA_1_HMAC, CKA_SIGN,
		instance->nss_sym_key_sign,
		&no_params);
	if (!enc_context) {
		char err[1024];
		PR_GetErrorText(err);
		err[PR_GetErrorTextLength()] = 0;
		log_printf(instance->log_level_security, "PK11_CreateContext failed (check digest) err %d: %s",
			PR_GetError(), err);
		return -1;
	}

	PK11_DigestBegin(enc_context);

	rv1 = PK11_DigestOp(enc_context, data, datalen);
	rv2 = PK11_DigestFinal(enc_context, digest, &tmp2_outlen, sizeof(digest));

	PK11_DestroyContext(enc_context, PR_TRUE);

	if (rv1 != SECSuccess || rv2 != SECSuccess) {
		log_printf(instance->log_level_security, "Digest check failed");
		return -1;
	}

	if (memcmp(digest, header->hash_digest, tmp2_outlen) != 0) {
		log_printf(instance->log_level_error, "Digest does not match");
		return -1;
	}

	/*
	 * Get rid of salt
	 */
	data += 16;
	datalen -= 16;

	/* Create cipher context for decryption */
	ivdata.type = siBuffer;
	ivdata.data = header->salt;
	ivdata.len = sizeof(header->salt);

	enc_context = PK11_CreateContextBySymKey(
		CKM_AES_CBC_PAD,
		CKA_DECRYPT,
		instance->nss_sym_key, &ivdata);
	if (!enc_context) {
		log_printf(instance->log_level_security,
			"PK11_CreateContext (decrypt) failed (err %d)",
			PR_GetError());
		return -1;
	}

	rv1 = PK11_CipherOp(enc_context, outdata, &tmp1_outlen,
			    sizeof(outbuf) - sizeof (struct crypto_security_header),
			    data, datalen);
	if (rv1 != SECSuccess) {
		log_printf(instance->log_level_security,
			"PK11_CipherOp (decrypt) failed (err %d)",
			PR_GetError());
	}
	rv2 = PK11_DigestFinal(enc_context, outdata + tmp1_outlen, &tmp2_outlen,
			       sizeof(outbuf) - tmp1_outlen);
	PK11_DestroyContext(enc_context, PR_TRUE);
	result_len = tmp1_outlen + tmp2_outlen + sizeof (struct crypto_security_header);

	memset(buf, 0, *buf_len);
	memcpy(buf, outdata, result_len);

	*buf_len = result_len;

	if (rv1 != SECSuccess || rv2 != SECSuccess)
		return -1;

	return 0;
}

size_t crypto_sec_header_size(int crypt_hash_type)
{
	/*
	 * TODO: add switch / size mapping
	 */
	return sizeof(struct crypto_security_header);
}

int crypto_encrypt_and_sign (
	struct crypto_instance *instance,
	const unsigned char *buf_in,
	const size_t buf_in_len,
	unsigned char *buf_out,
	size_t *buf_out_len)
{
	return (encrypt_and_sign_nss(instance, buf_in, buf_in_len, buf_out, buf_out_len));
}

int crypto_authenticate_and_decrypt (struct crypto_instance *instance,
	unsigned char *buf,
	int *buf_len)
{
	return (authenticate_and_decrypt_nss(instance, buf, buf_len));
}

struct crypto_instance *crypto_init(
	const unsigned char *private_key,
	unsigned int private_key_len,
	int crypto_crypt_type,
	int crypto_hash_type,
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
	int log_subsys_id)
{
	struct crypto_instance *instance;
	instance = malloc(sizeof(*instance));
	if (instance == NULL) {
		return (NULL);
	}
	memset(instance, 0, sizeof(struct crypto_instance));

	memcpy(instance->private_key, private_key, private_key_len);
	instance->private_key_len = private_key_len;
	instance->crypto_crypt_type = crypto_crypt_type;
	instance->crypto_hash_type = crypto_hash_type;
	instance->log_printf_func = log_printf_func;
	instance->log_level_security = log_level_security;
	instance->log_level_notice = log_level_notice;
	instance->log_level_error = log_level_error;
	instance->log_subsys_id = log_subsys_id;

	init_nss_crypto(instance);

	return (instance);
}
