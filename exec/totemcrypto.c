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
#include <blapit.h>

#define SALT_SIZE 16

struct crypto_config_header {
	uint8_t	crypto_cipher_type;
	uint8_t	crypto_hash_type;
} __attribute__((packed));

enum crypto_crypt_t {
	CRYPTO_CIPHER_TYPE_NONE = 0,
	CRYPTO_CIPHER_TYPE_AES256 = 1
};

CK_MECHANISM_TYPE cipher_to_nss[] = {
	0,				/* CRYPTO_CIPHER_TYPE_NONE */
	CKM_AES_CBC_PAD			/* CRYPTO_CIPHER_TYPE_AES256 */
};

size_t cipher_key_len[] = {
	 0,				/* CRYPTO_CIPHER_TYPE_NONE */
	32,				/* CRYPTO_CIPHER_TYPE_AES256 */
};

size_t cypher_block_len[] = {
	 0,				/* CRYPTO_CIPHER_TYPE_NONE */
	AES_BLOCK_SIZE			/* CRYPTO_CIPHER_TYPE_AES256 */
};

enum crypto_hash_t {
	CRYPTO_HASH_TYPE_NONE = 0,
	CRYPTO_HASH_TYPE_SHA1 = 1
};

CK_MECHANISM_TYPE hash_to_nss[] = {
	 0,				/* CRYPTO_HASH_TYPE_NONE */
	CKM_SHA_1_HMAC			/* CRYPTO_HASH_TYPE_SHA1 */
};

size_t hash_len[] = {
	 0,				/* CRYPTO_HASH_TYPE_NONE */
	SHA1_LENGTH			/* CRYPTO_HASH_TYPE_SHA1 */
};

size_t hash_block_len[] = {
	 0,				/* CRYPTO_HASH_TYPE_NONE */
	SHA1_BLOCK_LENGTH		/* CRYPTO_HASH_TYPE_SHA1 */
};

struct crypto_instance {
	PK11SymKey   *nss_sym_key;
	PK11SymKey   *nss_sym_key_sign;

	unsigned char private_key[1024];

	unsigned int private_key_len;

	enum crypto_crypt_t crypto_cipher_type;

	enum crypto_hash_t crypto_hash_type;

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
		level, instance->log_subsys_id,				\
		__FUNCTION__, __FILE__, __LINE__,			\
		(const char *)format, ##args);				\
} while (0);

#define LOGSYS_PERROR(err_num, level, fmt, args...)						\
do {												\
	char _error_str[LOGSYS_MAX_PERROR_MSG_LEN];						\
	const char *_error_ptr = qb_strerror_r(err_num, _error_str, sizeof(_error_str));	\
	instance->totemudp_log_printf (								\
		level, instance->log_subsys_id,							\
                __FUNCTION__, __FILE__, __LINE__,						\
		fmt ": %s (%d)", ##args, _error_ptr, err_num);					\
} while(0)

static int init_nss_crypto(struct crypto_instance *instance,
			   const char *crypto_cipher_type,
			   const char *crypto_hash_type)
{
	PK11SlotInfo*	crypt_slot = NULL;
	PK11SlotInfo*	hash_slot = NULL;
	SECItem		crypt_param;
	SECItem		hash_param;

	if ((!cipher_to_nss[instance->crypto_cipher_type]) &&
	    (!hash_to_nss[instance->crypto_hash_type])) {
		log_printf(instance->log_level_notice,
			   "Initializing transmit/receive security: NONE");
		return 0;
	}

	log_printf(instance->log_level_notice,
		   "Initializing transmit/receive security: NSS crypto: %s hash: %s",
		   crypto_cipher_type, crypto_hash_type);

	if (NSS_NoDB_Init(".") != SECSuccess) {
		log_printf(instance->log_level_security, "NSS initialization failed (err %d)",
			   PR_GetError());
		goto out;
	}

	if (cipher_to_nss[instance->crypto_cipher_type]) {
		crypt_param.type = siBuffer;
		crypt_param.data = instance->private_key;
		crypt_param.len = cipher_key_len[instance->crypto_cipher_type];

		crypt_slot = PK11_GetBestSlot(cipher_to_nss[instance->crypto_cipher_type], NULL);
		if (crypt_slot == NULL) {
			log_printf(instance->log_level_security, "Unable to find security slot (err %d)",
				   PR_GetError());
			goto out;
		}
		instance->nss_sym_key = PK11_ImportSymKey(crypt_slot,
							  cipher_to_nss[instance->crypto_cipher_type],
							  PK11_OriginUnwrap, CKA_ENCRYPT|CKA_DECRYPT,
							  &crypt_param, NULL);
		if (instance->nss_sym_key == NULL) {
			log_printf(instance->log_level_security, "Failure to import key into NSS (err %d)",
				   PR_GetError());
			goto out;
		}
	}

	if (hash_to_nss[instance->crypto_hash_type]) {
		hash_param.type = siBuffer;
		hash_param.data = 0;
		hash_param.len = 0;

		hash_slot = PK11_GetBestSlot(hash_to_nss[instance->crypto_hash_type], NULL);
		if (hash_slot == NULL) {
			log_printf(instance->log_level_security, "Unable to find security slot (err %d)",
				   PR_GetError());
			goto out;
		}

		instance->nss_sym_key_sign = PK11_ImportSymKey(hash_slot,
							       hash_to_nss[instance->crypto_hash_type],
							       PK11_OriginUnwrap, CKA_SIGN,
							       &hash_param, NULL);
		if (instance->nss_sym_key_sign == NULL) {
			log_printf(instance->log_level_security, "Failure to import key into NSS (err %d)",
				   PR_GetError());
			goto out;
		}
	}

	return 0;
out:
	return -1;
}

static int encrypt_and_sign_nss (
	struct crypto_instance *instance,
	const unsigned char *buf_in,
	const size_t buf_in_len,
	unsigned char *buf_out,
	size_t *buf_out_len)
{
	PK11Context*	enc_context = NULL;
	SECItem		crypt_param;
	SECItem		hash_param;
	SECItem		*nss_sec_param = NULL;

	unsigned char	*outdata;
	int		tmp1_outlen = 0;
	unsigned int	tmp2_outlen = 0;

	unsigned char	salt[SALT_SIZE];
	unsigned char	hash_block[hash_block_len[instance->crypto_hash_type]];

	outdata = buf_out + hash_len[instance->crypto_hash_type];

	if (!cipher_to_nss[instance->crypto_cipher_type]) {
		memcpy(outdata, buf_in, buf_in_len);
		*buf_out_len = buf_in_len;
		goto only_hash;
	}

	if (PK11_GenerateRandom (salt, SALT_SIZE) != SECSuccess) {
		log_printf(instance->log_level_security,
			"Failure to generate a random number %d",
			PR_GetError());
		goto out;
	}
	memcpy(outdata, salt, SALT_SIZE);

	crypt_param.type = siBuffer;
	crypt_param.data = salt;
	crypt_param.len = SALT_SIZE;

	nss_sec_param = PK11_ParamFromIV (cipher_to_nss[instance->crypto_cipher_type],
					  &crypt_param);
	if (nss_sec_param == NULL) {
		log_printf(instance->log_level_security,
			   "Failure to set up PKCS11 param (err %d)",
			   PR_GetError());
		goto out;
	}

	/*
	 * Create cipher context for encryption
	 */
	enc_context = PK11_CreateContextBySymKey (cipher_to_nss[instance->crypto_cipher_type],
						  CKA_ENCRYPT,
						  instance->nss_sym_key,
						  nss_sec_param);
	if (!enc_context) {
		log_printf(instance->log_level_security,
			   "PK11_CreateContext failed (encrypt) crypt_type=%d (err %d)",
			   (int)cipher_to_nss[instance->crypto_cipher_type],
			   PR_GetError());
		goto out;
	}

	if (PK11_CipherOp(enc_context, outdata + SALT_SIZE,
			  &tmp1_outlen,
			  FRAME_SIZE_MAX - (sizeof(struct crypto_config_header) + hash_len[instance->crypto_hash_type] + SALT_SIZE),
			  (unsigned char *)buf_in, buf_in_len) != SECSuccess) {
		log_printf(instance->log_level_security,
			   "PK11_CipherOp failed (encrypt) crypt_type=%d (err %d)",
			   (int)cipher_to_nss[instance->crypto_cipher_type],
			   PR_GetError());
		goto out;
	}
	if (PK11_DigestFinal(enc_context, outdata + SALT_SIZE + tmp1_outlen,
			     &tmp2_outlen, FRAME_SIZE_MAX - tmp1_outlen) != SECSuccess) {
		log_printf(instance->log_level_security,
			   "PK11_DigestFinal failed (encrypt) crypt_type=%d (err %d)",
			   (int)cipher_to_nss[instance->crypto_cipher_type],
			   PR_GetError());
		goto out;

	}

	if (enc_context) {
		PK11_DestroyContext(enc_context, PR_TRUE);
		enc_context = 0;
	}

	*buf_out_len = tmp1_outlen + tmp2_outlen + SALT_SIZE;

only_hash:

	if (!hash_to_nss[instance->crypto_hash_type]) {
		goto no_hash;
	}

	/* Now do the digest */
	hash_param.type = siBuffer;
	hash_param.data = 0;
	hash_param.len = 0;

	enc_context = PK11_CreateContextBySymKey(hash_to_nss[instance->crypto_hash_type],
						 CKA_SIGN,
						 instance->nss_sym_key_sign,
						 &hash_param);
	if (!enc_context) {
		log_printf(instance->log_level_security,
			   "PK11_CreateContext failed (hash) hash_type=%d (err %d)",
			   (int)hash_to_nss[instance->crypto_hash_type],
			   PR_GetError());
		goto out;
	}

	if (PK11_DigestBegin(enc_context) != SECSuccess) {
		log_printf(instance->log_level_security,
			   "PK11_DigestBegin failed (hash) hash_type=%d (err %d)",
			   (int)hash_to_nss[instance->crypto_hash_type],
			   PR_GetError());
		goto out;
	}

	if (PK11_DigestOp(enc_context,
			  outdata,
			  *buf_out_len) != SECSuccess) {
		log_printf(instance->log_level_security,
			   "PK11_DigestOp failed (hash) hash_type=%d (err %d)",
			   (int)hash_to_nss[instance->crypto_hash_type],
			   PR_GetError());
		goto out;
	}
	if (PK11_DigestFinal(enc_context,
			     hash_block,
			     &tmp2_outlen,
			     hash_block_len[instance->crypto_hash_type]) != SECSuccess) {
		log_printf(instance->log_level_security,
			   "PK11_DigestFinale failed (hash) hash_type=%d (err %d)",
			   (int)hash_to_nss[instance->crypto_hash_type],
			   PR_GetError());
		goto out;
	}

	if (enc_context) {
		PK11_DestroyContext(enc_context, PR_TRUE);
		enc_context = 0;
	}

	memcpy(buf_out, hash_block, hash_len[instance->crypto_hash_type]);

	*buf_out_len = *buf_out_len + hash_len[instance->crypto_hash_type];

no_hash:

	SECITEM_FreeItem(nss_sec_param, PR_TRUE);

	return 0;

out:
	if (enc_context) {
		PK11_DestroyContext(enc_context, PR_TRUE);
	}
	if (nss_sec_param) {
		SECITEM_FreeItem(nss_sec_param, PR_TRUE);
	}
	return -1;
}

static int authenticate_and_decrypt_nss (
	struct crypto_instance *instance,
	unsigned char *buf,
	int *buf_len)
{
	PK11Context*	enc_context = NULL;
	SECItem		crypt_param;
	SECItem		hash_param;
	unsigned char	hash_block[hash_block_len[instance->crypto_hash_type]];

	int		tmp1_outlen = 0;
	unsigned int	tmp2_outlen = 0;

	unsigned char	*data;
	size_t		datalen;

	unsigned char	outbuf[FRAME_SIZE_MAX];

	int		result_len;

	data = buf + hash_len[instance->crypto_hash_type];
	datalen = *buf_len - hash_len[instance->crypto_hash_type];

	if (!hash_to_nss[instance->crypto_hash_type]) {
		goto only_decrypt;
	}

	hash_param.type = siBuffer;
	hash_param.data = 0;
	hash_param.len = 0;

	/* Check the digest */
	enc_context = PK11_CreateContextBySymKey (hash_to_nss[instance->crypto_hash_type],
						  CKA_SIGN,
						  instance->nss_sym_key_sign,
						  &hash_param);
	if (!enc_context) {
		log_printf(instance->log_level_security,
			   "PK11_CreateContext failed (check digest) err %d",
			   PR_GetError());
		goto out;
	}

	if (PK11_DigestBegin(enc_context) != SECSuccess) {
		log_printf(instance->log_level_security,
			  "PK11_DigestBegin failed (check digest) err %d",
			  PR_GetError());
		goto out;
	}

	if (PK11_DigestOp(enc_context, data, datalen) != SECSuccess) {
		log_printf(instance->log_level_security,
			   "PK11_DigestOp failed (check digest) err %d",
			   PR_GetError());
		goto out;
	}

	if (PK11_DigestFinal(enc_context, hash_block,
			     &tmp2_outlen, hash_block_len[instance->crypto_hash_type]) != SECSuccess) {
		log_printf(instance->log_level_security,
			   "PK11_DigestFinal failed (check digest) err %d",
			   PR_GetError());
		goto out;
	}

	if (enc_context) {
		PK11_DestroyContext(enc_context, PR_TRUE);
		enc_context = 0;
	}

	if (memcmp(hash_block, buf, tmp2_outlen) != 0) {
		log_printf(instance->log_level_error, "Digest does not match");
		goto out;
	}

only_decrypt:

	if (!cipher_to_nss[instance->crypto_cipher_type]) {
		memcpy(outbuf, data, datalen);
		result_len = datalen;
		goto no_decrypt;
	}

	/* Create cipher context for decryption */
	crypt_param.type = siBuffer;
	crypt_param.data = data;
	crypt_param.len = SALT_SIZE;

	/*
	 * Get rid of salt
	 */
	data += SALT_SIZE;
	datalen -= SALT_SIZE;

	enc_context = PK11_CreateContextBySymKey(cipher_to_nss[instance->crypto_cipher_type],
						 CKA_DECRYPT,
						 instance->nss_sym_key, &crypt_param);
	if (!enc_context) {
		log_printf(instance->log_level_security,
			   "PK11_CreateContext (decrypt) failed (err %d)",
			   PR_GetError());
		goto out;
	}

	if (PK11_CipherOp(enc_context, outbuf, &tmp1_outlen,
			  sizeof(outbuf), data, datalen) != SECSuccess) {
		log_printf(instance->log_level_security,
			   "PK11_CipherOp (decrypt) failed (err %d)",
			   PR_GetError());
		goto out;
	}
	if (PK11_DigestFinal(enc_context, outbuf + tmp1_outlen, &tmp2_outlen,
			     sizeof(outbuf) - tmp1_outlen) != SECSuccess) {
		log_printf(instance->log_level_security,
			   "PK11_DigestFinal (decrypt) failed (err %d)",
			   PR_GetError()); 
		goto out;
	}

	if (enc_context) {
		PK11_DestroyContext(enc_context, PR_TRUE);
		enc_context = 0;
	}

	result_len = tmp1_outlen + tmp2_outlen;

no_decrypt:

	memset(buf, 0, *buf_len);
	memcpy(buf, outbuf, result_len);

	*buf_len = result_len;

	return 0;

out:
	if (enc_context) {
		PK11_DestroyContext(enc_context, PR_TRUE);
	}
	return -1;
}

static int string_to_crypto_cipher_type(const char* crypto_cipher_type)
{
	if (strcmp(crypto_cipher_type, "none") == 0) {
		return CRYPTO_CIPHER_TYPE_NONE;
	} else if (strcmp(crypto_cipher_type, "aes256") == 0) {
		return CRYPTO_CIPHER_TYPE_AES256;
	}
	return CRYPTO_CIPHER_TYPE_NONE;
}

static int string_to_crypto_hash_type(const char* crypto_hash_type)
{
	if (strcmp(crypto_hash_type, "none") == 0) {
		return CRYPTO_HASH_TYPE_NONE;
	} else if (strcmp(crypto_hash_type, "sha1") == 0) {
		return CRYPTO_HASH_TYPE_SHA1;
	}

	return CRYPTO_HASH_TYPE_NONE;
}

size_t crypto_sec_header_size(
	const char *crypto_cipher_type,
	const char *crypto_hash_type)
{
	int crypto_cipher = string_to_crypto_cipher_type(crypto_cipher_type);
	int crypto_hash = string_to_crypto_hash_type(crypto_hash_type);
	size_t hdr_size = 0;

	hdr_size = sizeof(struct crypto_config_header);

	if (crypto_hash) {
		hdr_size += hash_len[crypto_hash];
	}

	if (crypto_cipher) {
		hdr_size += SALT_SIZE;
		hdr_size += cypher_block_len[crypto_cipher];
	}

	return hdr_size;
}

int crypto_encrypt_and_sign (
	struct crypto_instance *instance,
	const unsigned char *buf_in,
	const size_t buf_in_len,
	unsigned char *buf_out,
	size_t *buf_out_len)
{
	int err = 0;
	struct crypto_config_header *cch;

	cch = (struct crypto_config_header *)buf_out;
	cch->crypto_cipher_type = instance->crypto_cipher_type;
	cch->crypto_hash_type = instance->crypto_hash_type;

	if ((!cipher_to_nss[instance->crypto_cipher_type]) &&
	    (!hash_to_nss[instance->crypto_hash_type])) {
		memcpy(buf_out + sizeof(struct crypto_config_header), buf_in, buf_in_len);
		*buf_out_len = buf_in_len;
		err = 0;
	} else {
		err = encrypt_and_sign_nss(instance,
					   buf_in, buf_in_len,
					   buf_out + sizeof(struct crypto_config_header),
					   buf_out_len);
	}

	*buf_out_len = *buf_out_len + sizeof(struct crypto_config_header);

	return err;
}

int crypto_authenticate_and_decrypt (struct crypto_instance *instance,
	unsigned char *buf,
	int *buf_len)
{
	int err = 0;
	struct crypto_config_header *cch;

	cch = (struct crypto_config_header *)buf;

	/*
	 * decode crypto config of incoming packets
	 */

	if (cch->crypto_cipher_type != instance->crypto_cipher_type) {
		log_printf(instance->log_level_security,
			   "Incoming packet has different crypto type. Rejecting");
		return -1;
	}

	if (cch->crypto_hash_type != instance->crypto_hash_type) {
		log_printf(instance->log_level_security,
			   "Incoming packet has different hash type. Rejecting");
		return -1;
	}

	/*
	 * invalidate config header
	 */
	cch = NULL;

	/*
	 * and kill it
	 */
	*buf_len = *buf_len - sizeof(struct crypto_config_header);
	memmove(buf, buf + sizeof(struct crypto_config_header), *buf_len);


	/*
	 * if crypto is totally disabled, there is no work for us
	 */
	if ((!cipher_to_nss[instance->crypto_cipher_type]) &&
	    (!hash_to_nss[instance->crypto_hash_type])) {
		err = 0;
	} else {
		err = authenticate_and_decrypt_nss(instance, buf, buf_len);
	}

	return err;
}

struct crypto_instance *crypto_init(
	const unsigned char *private_key,
	unsigned int private_key_len,
	const char *crypto_cipher_type,
	const char *crypto_hash_type,
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

	instance->crypto_cipher_type = string_to_crypto_cipher_type(crypto_cipher_type);
	instance->crypto_hash_type = string_to_crypto_hash_type(crypto_hash_type);

	instance->log_printf_func = log_printf_func;
	instance->log_level_security = log_level_security;
	instance->log_level_notice = log_level_notice;
	instance->log_level_error = log_level_error;
	instance->log_subsys_id = log_subsys_id;

	if (init_nss_crypto(instance, crypto_cipher_type, crypto_hash_type) < 0) {
		free(instance);
		return(NULL);
	}

	return (instance);
}
