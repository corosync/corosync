/*
 * Copyright (c) 2005 MontaVista Software, Inc.
 * Copyright (c) 2006-2009 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)

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
#include <corosync/list.h>
#include <corosync/hdb.h>
#include <corosync/swab.h>
#include <corosync/totem/coropoll.h>
#define LOGSYS_UTILS_ONLY 1
#include <corosync/engine/logsys.h>
#include "totemudpu.h"

#include "crypto.h"
#include "util.h"

#ifdef HAVE_LIBNSS
#include <nss.h>
#include <pk11pub.h>
#include <pkcs11.h>
#include <prerror.h>
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define MCAST_SOCKET_BUFFER_SIZE (TRANSMITS_ALLOWED * FRAME_SIZE_MAX)
#define NETIF_STATE_REPORT_UP		1
#define NETIF_STATE_REPORT_DOWN		2

#define BIND_STATE_UNBOUND	0
#define BIND_STATE_REGULAR	1
#define BIND_STATE_LOOPBACK	2

#define HMAC_HASH_SIZE 20
struct security_header {
	unsigned char hash_digest[HMAC_HASH_SIZE]; /* The hash *MUST* be first in the data structure */
	unsigned char salt[16]; /* random number */
	char msg[0];
} __attribute__((packed));

struct totemudpu_member {
	struct list_head list;
	struct totem_ip_address member;
	int fd;
	int active;
};
	
struct totemudpu_instance {
	hmac_state totemudpu_hmac_state;

	prng_state totemudpu_prng_state;

#ifdef HAVE_LIBNSS
	PK11SymKey   *nss_sym_key;
	PK11SymKey   *nss_sym_key_sign;
#endif

	unsigned char totemudpu_private_key[1024];

	unsigned int totemudpu_private_key_len;

	hdb_handle_t totemudpu_poll_handle;

	struct totem_interface *totem_interface;

	int netif_state_report;

	int netif_bind_state;

	void *context;

	void (*totemudpu_deliver_fn) (
		void *context,
		const void *msg,
		unsigned int msg_len);

	void (*totemudpu_iface_change_fn) (
		void *context,
		const struct totem_ip_address *iface_address);

	void (*totemudpu_target_set_completed) (void *context);

	/*
	 * Function and data used to log messages
	 */
	int totemudpu_log_level_security;

	int totemudpu_log_level_error;

	int totemudpu_log_level_warning;

	int totemudpu_log_level_notice;

	int totemudpu_log_level_debug;

	int totemudpu_subsys_id;

	void (*totemudpu_log_printf) (
		unsigned int rec_ident,
		const char *function,
		const char *file,
		int line,
		const char *format,
		...)__attribute__((format(printf, 5, 6)));

	void *udpu_context;

	char iov_buffer[FRAME_SIZE_MAX];

	struct iovec totemudpu_iov_recv;

	struct list_head member_list;

	int stats_sent;

	int stats_recv;

	int stats_delv;

	int stats_remcasts;

	int stats_orf_token;

	struct timeval stats_tv_start;

	struct totem_ip_address my_id;

	int firstrun;

	poll_timer_handle timer_netif_check_timeout;

	unsigned int my_memb_entries;

	struct totem_config *totem_config;

	totemsrp_stats_t *stats;

	struct totem_ip_address token_target;

	int token_socket;

	poll_timer_handle timer_merge_detect_timeout;

	int send_merge_detect_message;

	unsigned int merge_detect_messages_sent_before_timeout;
};

struct work_item {
	const void *msg;
	unsigned int msg_len;
	struct totemudpu_instance *instance;
};

static int totemudpu_build_sockets (
	struct totemudpu_instance *instance,
	struct totem_ip_address *bindnet_address,
	struct totem_ip_address *bound_to);

static int totemudpu_create_sending_socket(
	void *udpu_context,
	const struct totem_ip_address *member);

int totemudpu_member_list_rebind_ip (
	void *udpu_context);

static void totemudpu_start_merge_detect_timeout(
	void *udpu_context);

static void totemudpu_stop_merge_detect_timeout(
	void *udpu_context);

static struct totem_ip_address localhost;

static void totemudpu_instance_initialize (struct totemudpu_instance *instance)
{
	memset (instance, 0, sizeof (struct totemudpu_instance));

	instance->netif_state_report = NETIF_STATE_REPORT_UP | NETIF_STATE_REPORT_DOWN;

	instance->totemudpu_iov_recv.iov_base = instance->iov_buffer;

	instance->totemudpu_iov_recv.iov_len = FRAME_SIZE_MAX; //sizeof (instance->iov_buffer);

	/*
	 * There is always atleast 1 processor
	 */
	instance->my_memb_entries = 1;

	list_init (&instance->member_list);
}

#define log_printf(level, format, args...)				\
do {									\
        instance->totemudpu_log_printf (					\
		LOGSYS_ENCODE_RECID(level,				\
				    instance->totemudpu_subsys_id,	\
				    LOGSYS_RECID_LOG),			\
                __FUNCTION__, __FILE__, __LINE__,			\
		(const char *)format, ##args);				\
} while (0);

static int authenticate_and_decrypt_sober (
	struct totemudpu_instance *instance,
	struct iovec *iov,
	unsigned int iov_len)
{
	unsigned char keys[48];
	struct security_header *header = (struct security_header *)iov[0].iov_base;
	prng_state keygen_prng_state;
	prng_state stream_prng_state;
	unsigned char *hmac_key = &keys[32];
	unsigned char *cipher_key = &keys[16];
	unsigned char *initial_vector = &keys[0];
	unsigned char digest_comparison[HMAC_HASH_SIZE];
	unsigned long len;

	/*
	 * Generate MAC, CIPHER, IV keys from private key
	 */
	memset (keys, 0, sizeof (keys));
	sober128_start (&keygen_prng_state);
	sober128_add_entropy (instance->totemudpu_private_key,
		instance->totemudpu_private_key_len, &keygen_prng_state);
	sober128_add_entropy (header->salt, sizeof (header->salt), &keygen_prng_state);

	sober128_read (keys, sizeof (keys), &keygen_prng_state);

	/*
	 * Setup stream cipher
	 */
	sober128_start (&stream_prng_state);
	sober128_add_entropy (cipher_key, 16, &stream_prng_state);
	sober128_add_entropy (initial_vector, 16, &stream_prng_state);

	/*
	 * Authenticate contents of message
	 */
	hmac_init (&instance->totemudpu_hmac_state, DIGEST_SHA1, hmac_key, 16);

	hmac_process (&instance->totemudpu_hmac_state,
		(unsigned char *)iov->iov_base + HMAC_HASH_SIZE,
		iov->iov_len - HMAC_HASH_SIZE);

	len = hash_descriptor[DIGEST_SHA1]->hashsize;
	assert (HMAC_HASH_SIZE >= len);
	hmac_done (&instance->totemudpu_hmac_state, digest_comparison, &len);

	if (memcmp (digest_comparison, header->hash_digest, len) != 0) {
		return (-1);
	}

	/*
	 * Decrypt the contents of the message with the cipher key
	 */
	sober128_read ((unsigned char*)iov->iov_base +
			sizeof (struct security_header),
		iov->iov_len - sizeof (struct security_header),
		&stream_prng_state);

	return (0);
}

static void init_sober_crypto(
	struct totemudpu_instance *instance)
{
	log_printf(instance->totemudpu_log_level_notice,
		"Initializing transmit/receive security: libtomcrypt SOBER128/SHA1HMAC (mode 0).\n");
	rng_make_prng (128, PRNG_SOBER, &instance->totemudpu_prng_state, NULL);
}

#ifdef HAVE_LIBNSS

static unsigned char *copy_from_iovec(
	const struct iovec *iov,
	unsigned int iov_len,
	size_t *buf_size)
{
	int i;
	size_t bufptr;
	size_t buflen = 0;
	unsigned char *newbuf;

	for (i=0; i<iov_len; i++)
		buflen += iov[i].iov_len;

	newbuf = malloc(buflen);
	if (!newbuf)
		return NULL;

	bufptr=0;
	for (i=0; i<iov_len; i++) {
		memcpy(newbuf+bufptr, iov[i].iov_base, iov[i].iov_len);
		bufptr += iov[i].iov_len;
	}
	*buf_size = buflen;
	return newbuf;
}

static void copy_to_iovec(
	struct iovec *iov,
	unsigned int iov_len,
	const unsigned char *buf,
	size_t buf_size)
{
	int i;
	size_t copylen;
	size_t bufptr = 0;

	bufptr=0;
	for (i=0; i<iov_len; i++) {
		copylen = iov[i].iov_len;
		if (bufptr + copylen > buf_size) {
			copylen = buf_size - bufptr;
		}
		memcpy(iov[i].iov_base, buf+bufptr, copylen);
		bufptr += copylen;
		if (iov[i].iov_len != copylen) {
			iov[i].iov_len = copylen;
			return;
		}
	}
}

static void init_nss_crypto(
	struct totemudpu_instance *instance)
{
	PK11SlotInfo*      aes_slot = NULL;
	PK11SlotInfo*      sha1_slot = NULL;
	SECItem            key_item;
	SECStatus          rv;

	log_printf(instance->totemudpu_log_level_notice,
		"Initializing transmit/receive security: NSS AES128CBC/SHA1HMAC (mode 1).\n");
	rv = NSS_NoDB_Init(".");
	if (rv != SECSuccess)
	{
		log_printf(instance->totemudpu_log_level_security, "NSS initialization failed (err %d)\n",
			PR_GetError());
		goto out;
	}

	aes_slot = PK11_GetBestSlot(instance->totem_config->crypto_crypt_type, NULL);
	if (aes_slot == NULL)
	{
		log_printf(instance->totemudpu_log_level_security, "Unable to find security slot (err %d)\n",
			PR_GetError());
		goto out;
	}

	sha1_slot = PK11_GetBestSlot(CKM_SHA_1_HMAC, NULL);
	if (sha1_slot == NULL)
	{
		log_printf(instance->totemudpu_log_level_security, "Unable to find security slot (err %d)\n",
			PR_GetError());
		goto out;
	}
	/*
	 * Make the private key into a SymKey that we can use
	 */
	key_item.type = siBuffer;
	key_item.data = instance->totem_config->private_key;
	key_item.len = 32; /* Use 128 bits */

	instance->nss_sym_key = PK11_ImportSymKey(aes_slot,
		instance->totem_config->crypto_crypt_type,
		PK11_OriginUnwrap, CKA_ENCRYPT|CKA_DECRYPT,
		&key_item, NULL);
	if (instance->nss_sym_key == NULL)
	{
		log_printf(instance->totemudpu_log_level_security, "Failure to import key into NSS (err %d)\n",
			PR_GetError());
		goto out;
	}

	instance->nss_sym_key_sign = PK11_ImportSymKey(sha1_slot,
		CKM_SHA_1_HMAC,
		PK11_OriginUnwrap, CKA_SIGN,
		&key_item, NULL);
	if (instance->nss_sym_key_sign == NULL) {
		log_printf(instance->totemudpu_log_level_security, "Failure to import key into NSS (err %d)\n",
			PR_GetError());
		goto out;
	}
out:
	return;
}

static int encrypt_and_sign_nss (
	struct totemudpu_instance *instance,
	unsigned char *buf,
	size_t *buf_len,
	const struct iovec *iovec,
	unsigned int iov_len)
{
	PK11Context*       enc_context = NULL;
	SECStatus          rv1, rv2;
	int                tmp1_outlen;
	unsigned int       tmp2_outlen;
	unsigned char      *inbuf;
	unsigned char      *data;
	unsigned char      *outdata;
	size_t             datalen;
	SECItem            no_params;
	SECItem            iv_item;
	struct security_header *header;
	SECItem      *nss_sec_param;
	unsigned char nss_iv_data[16];
	SECStatus          rv;

	no_params.type = siBuffer;
	no_params.data = 0;
	no_params.len = 0;

	tmp1_outlen = tmp2_outlen = 0;
	inbuf = copy_from_iovec(iovec, iov_len, &datalen);
	if (!inbuf) {
		log_printf(instance->totemudpu_log_level_security, "malloc error copying buffer from iovec\n");
		goto out;
	}

	data = inbuf + sizeof (struct security_header);
	datalen -= sizeof (struct security_header);

	outdata = buf + sizeof (struct security_header);
	header = (struct security_header *)buf;

	rv = PK11_GenerateRandom (
		nss_iv_data,
		sizeof (nss_iv_data));
	if (rv != SECSuccess) {
		log_printf(instance->totemudpu_log_level_security,
			"Failure to generate a random number %d\n",
			PR_GetError());
		free (inbuf);
		goto out;
	}

	memcpy(header->salt, nss_iv_data, sizeof(nss_iv_data));
	iv_item.type = siBuffer;
	iv_item.data = nss_iv_data;
	iv_item.len = sizeof (nss_iv_data);

	nss_sec_param = PK11_ParamFromIV (
		instance->totem_config->crypto_crypt_type,
		&iv_item);
	if (nss_sec_param == NULL) {
		log_printf(instance->totemudpu_log_level_security,
			"Failure to set up PKCS11 param (err %d)\n",
			PR_GetError());
		free (inbuf);
		goto out;
	}

	/*
	 * Create cipher context for encryption
	 */
	enc_context = PK11_CreateContextBySymKey (
		instance->totem_config->crypto_crypt_type,
		CKA_ENCRYPT,
		instance->nss_sym_key,
		nss_sec_param);
	if (!enc_context) {
		char err[1024];
		PR_GetErrorText(err);
		err[PR_GetErrorTextLength()] = 0;
		log_printf(instance->totemudpu_log_level_security,
			"PK11_CreateContext failed (encrypt) crypt_type=%d (err %d): %s\n",
			instance->totem_config->crypto_crypt_type,
			PR_GetError(), err);
		free(inbuf);
		goto sec_out;
	}
	rv1 = PK11_CipherOp(enc_context, outdata,
			    &tmp1_outlen, FRAME_SIZE_MAX - sizeof(struct security_header),
			    data, datalen);
	rv2 = PK11_DigestFinal(enc_context, outdata + tmp1_outlen, &tmp2_outlen,
			       FRAME_SIZE_MAX - tmp1_outlen);
	PK11_DestroyContext(enc_context, PR_TRUE);

	*buf_len = tmp1_outlen + tmp2_outlen;
	free(inbuf);
//	memcpy(&outdata[*buf_len], nss_iv_data, sizeof(nss_iv_data));

	if (rv1 != SECSuccess || rv2 != SECSuccess)
		goto sec_out;

	/* Now do the digest */
	enc_context = PK11_CreateContextBySymKey(CKM_SHA_1_HMAC,
		CKA_SIGN, instance->nss_sym_key_sign, &no_params);
	if (!enc_context) {
		char err[1024];
		PR_GetErrorText(err);
		err[PR_GetErrorTextLength()] = 0;
		log_printf(instance->totemudpu_log_level_security, "encrypt: PK11_CreateContext failed (digest) err %d: %s\n",
			PR_GetError(), err);
		goto sec_out;
	}


	PK11_DigestBegin(enc_context);

	rv1 = PK11_DigestOp(enc_context, outdata - 16, *buf_len + 16);
	rv2 = PK11_DigestFinal(enc_context, header->hash_digest, &tmp2_outlen, sizeof(header->hash_digest));

	PK11_DestroyContext(enc_context, PR_TRUE);

	if (rv1 != SECSuccess || rv2 != SECSuccess)
		goto sec_out;


	*buf_len = *buf_len + sizeof(struct security_header);
	SECITEM_FreeItem(nss_sec_param, PR_TRUE);
	return 0;

sec_out:
	if (nss_sec_param != NULL) {
		SECITEM_FreeItem(nss_sec_param, PR_TRUE);
	}
out:
	return -1;
}


static int authenticate_and_decrypt_nss (
	struct totemudpu_instance *instance,
	struct iovec *iov,
	unsigned int iov_len)
{
	PK11Context*  enc_context = NULL;
	SECStatus     rv1, rv2;
	int           tmp1_outlen;
	unsigned int  tmp2_outlen;
	unsigned char outbuf[FRAME_SIZE_MAX];
	unsigned char digest[HMAC_HASH_SIZE];
	unsigned char *outdata;
	int           result_len;
	unsigned char *data;
	unsigned char *inbuf;
	size_t        datalen;
	struct security_header *header = (struct security_header *)iov[0].iov_base;
	SECItem no_params;
	SECItem ivdata;

	no_params.type = siBuffer;
	no_params.data = 0;
	no_params.len = 0;

	tmp1_outlen = tmp2_outlen = 0;
	if (iov_len > 1) {
		inbuf = copy_from_iovec(iov, iov_len, &datalen);
		if (!inbuf) {
			log_printf(instance->totemudpu_log_level_security, "malloc error copying buffer from iovec\n");
			return -1;
		}
	}
	else {
		inbuf = (unsigned char *)iov[0].iov_base;
		datalen = iov[0].iov_len;
	}
	data = inbuf + sizeof (struct security_header) - 16;
	datalen = datalen - sizeof (struct security_header) + 16;

	outdata = outbuf + sizeof (struct security_header);

	/* Check the digest */
	enc_context = PK11_CreateContextBySymKey (
		CKM_SHA_1_HMAC, CKA_SIGN,
		instance->nss_sym_key_sign,
		&no_params);
	if (!enc_context) {
		char err[1024];
		PR_GetErrorText(err);
		err[PR_GetErrorTextLength()] = 0;
		log_printf(instance->totemudpu_log_level_security, "PK11_CreateContext failed (check digest) err %d: %s\n",
			PR_GetError(), err);
		goto out;
	}

	PK11_DigestBegin(enc_context);

	rv1 = PK11_DigestOp(enc_context, data, datalen);
	rv2 = PK11_DigestFinal(enc_context, digest, &tmp2_outlen, sizeof(digest));

	PK11_DestroyContext(enc_context, PR_TRUE);

	if (rv1 != SECSuccess || rv2 != SECSuccess) {
		log_printf(instance->totemudpu_log_level_security, "Digest check failed\n");
		goto out;
	}

	if (memcmp(digest, header->hash_digest, tmp2_outlen) != 0) {
		log_printf(instance->totemudpu_log_level_error, "Digest does not match\n");
		goto out;
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
		instance->totem_config->crypto_crypt_type,
		CKA_DECRYPT,
		instance->nss_sym_key, &ivdata);
	if (!enc_context) {
		log_printf(instance->totemudpu_log_level_security,
			"PK11_CreateContext (decrypt) failed (err %d)\n",
			PR_GetError());
		goto out;
	}

	rv1 = PK11_CipherOp(enc_context, outdata, &tmp1_outlen,
			    sizeof(outbuf) - sizeof (struct security_header),
			    data, datalen);
	if (rv1 != SECSuccess) {
		log_printf(instance->totemudpu_log_level_security,
			"PK11_CipherOp (decrypt) failed (err %d)\n",
			PR_GetError());
	}
	rv2 = PK11_DigestFinal(enc_context, outdata + tmp1_outlen, &tmp2_outlen,
			       sizeof(outbuf) - tmp1_outlen);
	PK11_DestroyContext(enc_context, PR_TRUE);
	result_len = tmp1_outlen + tmp2_outlen + sizeof (struct security_header);

	/* Copy it back to the buffer */
	copy_to_iovec(iov, iov_len, outbuf, result_len);
	if (iov_len > 1)
		free(inbuf);

	if (rv1 != SECSuccess || rv2 != SECSuccess)
		return -1;

	return 0;

out:
	if (iov_len > 1 && inbuf != NULL) {
		free (inbuf);
	}

	return (-1);
}
#endif

static int encrypt_and_sign_sober (
	struct totemudpu_instance *instance,
	unsigned char *buf,
	size_t *buf_len,
	const struct iovec *iovec,
	unsigned int iov_len)
{
	int i;
	unsigned char *addr;
	unsigned char keys[48];
	struct security_header *header;
	unsigned char *hmac_key = &keys[32];
	unsigned char *cipher_key = &keys[16];
	unsigned char *initial_vector = &keys[0];
	unsigned long len;
	size_t outlen = 0;
	hmac_state hmac_st;
	prng_state keygen_prng_state;
	prng_state stream_prng_state;
	prng_state *prng_state_in = &instance->totemudpu_prng_state;

	header = (struct security_header *)buf;
	addr = buf + sizeof (struct security_header);

	memset (keys, 0, sizeof (keys));
	memset (header->salt, 0, sizeof (header->salt));

	/*
	 * Generate MAC, CIPHER, IV keys from private key
	 */
	sober128_read (header->salt, sizeof (header->salt), prng_state_in);
	sober128_start (&keygen_prng_state);
	sober128_add_entropy (instance->totemudpu_private_key,
		instance->totemudpu_private_key_len,
		&keygen_prng_state);
	sober128_add_entropy (header->salt, sizeof (header->salt),
		&keygen_prng_state);

	sober128_read (keys, sizeof (keys), &keygen_prng_state);

	/*
	 * Setup stream cipher
	 */
	sober128_start (&stream_prng_state);
	sober128_add_entropy (cipher_key, 16, &stream_prng_state);
	sober128_add_entropy (initial_vector, 16, &stream_prng_state);

	outlen = sizeof (struct security_header);
	/*
	 * Copy remainder of message, then encrypt it
	 */
	for (i = 1; i < iov_len; i++) {
		memcpy (addr, iovec[i].iov_base, iovec[i].iov_len);
		addr += iovec[i].iov_len;
		outlen += iovec[i].iov_len;
	}

	/*
 	 * Encrypt message by XORing stream cipher data
	 */
	sober128_read (buf + sizeof (struct security_header),
		outlen - sizeof (struct security_header),
		&stream_prng_state);

	memset (&hmac_st, 0, sizeof (hmac_st));

	/*
	 * Sign the contents of the message with the hmac key and store signature in message
	 */
	hmac_init (&hmac_st, DIGEST_SHA1, hmac_key, 16);

	hmac_process (&hmac_st,
		buf + HMAC_HASH_SIZE,
		outlen - HMAC_HASH_SIZE);

	len = hash_descriptor[DIGEST_SHA1]->hashsize;

	hmac_done (&hmac_st, header->hash_digest, &len);

	*buf_len = outlen;

	return 0;
}

static int encrypt_and_sign_worker (
	struct totemudpu_instance *instance,
	unsigned char *buf,
	size_t *buf_len,
	const struct iovec *iovec,
	unsigned int iov_len)
{
	if (instance->totem_config->crypto_type == TOTEM_CRYPTO_SOBER ||
	    instance->totem_config->crypto_accept == TOTEM_CRYPTO_ACCEPT_OLD)
		return encrypt_and_sign_sober(instance, buf, buf_len, iovec, iov_len);
#ifdef HAVE_LIBNSS
	if (instance->totem_config->crypto_type == TOTEM_CRYPTO_NSS)
		return encrypt_and_sign_nss(instance, buf, buf_len, iovec, iov_len);
#endif
	return -1;
}

static int authenticate_and_decrypt (
	struct totemudpu_instance *instance,
	struct iovec *iov,
	unsigned int iov_len)
{
	unsigned char type;
	unsigned char *endbuf = (unsigned char *)iov[iov_len-1].iov_base;
	int res = -1;

	/*
	 * Get the encryption type and remove it from the buffer
	 */
	type = endbuf[iov[iov_len-1].iov_len-1];
	iov[iov_len-1].iov_len -= 1;

	if (type == TOTEM_CRYPTO_SOBER)
		res = authenticate_and_decrypt_sober(instance, iov, iov_len);

	/*
	 * Only try higher crypto options if NEW has been requested
	 */
	if (instance->totem_config->crypto_accept == TOTEM_CRYPTO_ACCEPT_NEW) {
#ifdef HAVE_LIBNSS
		if (type == TOTEM_CRYPTO_NSS)
		    res = authenticate_and_decrypt_nss(instance, iov, iov_len);
#endif
	}

	/*
	 * If it failed, then try decrypting the whole packet as it might be
	 * from aisexec
	 */
	if (res == -1) {
		iov[iov_len-1].iov_len += 1;
		res = authenticate_and_decrypt_sober(instance, iov, iov_len);
	}

	return res;
}

static void init_crypto(
	struct totemudpu_instance *instance)
{
	/*
	 * If we are expecting NEW crypto type then initialise all available
	 * crypto options. For OLD then we only need SOBER128.
	 */

	init_sober_crypto(instance);

	if (instance->totem_config->crypto_accept == TOTEM_CRYPTO_ACCEPT_OLD)
		return;

#ifdef HAVE_LIBNSS
	init_nss_crypto(instance);
#endif
}

int totemudpu_crypto_set (
	void *udpu_context,
	 unsigned int type)
{
	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;
	int res = 0;

	/*
	 * Can't set crypto type if OLD is selected
	 */
	if (instance->totem_config->crypto_accept == TOTEM_CRYPTO_ACCEPT_OLD) {
		res = -1;
	} else {
		/*
		 * Validate crypto algorithm
		 */
		switch (type) {
			case TOTEM_CRYPTO_SOBER:
				log_printf(instance->totemudpu_log_level_security,
					"Transmit security set to: libtomcrypt SOBER128/SHA1HMAC (mode 0)");
				break;
			case TOTEM_CRYPTO_NSS:
				log_printf(instance->totemudpu_log_level_security,
					"Transmit security set to: NSS AES128CBC/SHA1HMAC (mode 1)");
				break;
			default:
				res = -1;
				break;
		}
	}

	return (res);
}


static inline void ucast_sendmsg (
	struct totemudpu_instance *instance,
	struct totem_ip_address *system_to,
	const void *msg,
	unsigned int msg_len)
{
	struct msghdr msg_ucast;
	int res = 0;
	size_t buf_len;
	unsigned char sheader[sizeof (struct security_header)];
	unsigned char encrypt_data[FRAME_SIZE_MAX];
	struct iovec iovec_encrypt[2];
	const struct iovec *iovec_sendmsg;
	struct sockaddr_storage sockaddr;
	struct iovec iovec;
	unsigned int iov_len;
	int addrlen;

	if (instance->totem_config->secauth == 1) {
		iovec_encrypt[0].iov_base = (void *)sheader;
		iovec_encrypt[0].iov_len = sizeof (struct security_header);
		iovec_encrypt[1].iov_base = (void *)msg;
		iovec_encrypt[1].iov_len = msg_len;

		/*
		 * Encrypt and digest the message
		 */
		encrypt_and_sign_worker (
			instance,
			encrypt_data,
			&buf_len,
			iovec_encrypt,
			2);

		if (instance->totem_config->crypto_accept == TOTEM_CRYPTO_ACCEPT_NEW) {
			encrypt_data[buf_len++] = instance->totem_config->crypto_type;
		}
		else {
			encrypt_data[buf_len++] = 0;
		}

		iovec_encrypt[0].iov_base = (void *)encrypt_data;
		iovec_encrypt[0].iov_len = buf_len;
		iovec_sendmsg = &iovec_encrypt[0];
		iov_len = 1;
	} else {
		iovec.iov_base = (void *)msg;
		iovec.iov_len = msg_len;
		iovec_sendmsg = &iovec;
		iov_len = 1;
	}

	/*
	 * Build unicast message
	 */
	totemip_totemip_to_sockaddr_convert(system_to,
		instance->totem_interface->ip_port, &sockaddr, &addrlen);
	msg_ucast.msg_name = &sockaddr;
	msg_ucast.msg_namelen = addrlen;
	msg_ucast.msg_iov = (void *) iovec_sendmsg;
	msg_ucast.msg_iovlen = iov_len;
#if !defined(COROSYNC_SOLARIS)
	msg_ucast.msg_control = 0;
	msg_ucast.msg_controllen = 0;
	msg_ucast.msg_flags = 0;
#else
	msg_ucast.msg_accrights = NULL;
	msg_ucast.msg_accrightslen = 0;
#endif


	/*
	 * Transmit unicast message
	 * An error here is recovered by totemsrp
	 */
	res = sendmsg (instance->token_socket, &msg_ucast, MSG_NOSIGNAL);
	if (res < 0) {
		LOGSYS_PERROR (errno, instance->totemudpu_log_level_debug,
				"sendmsg(ucast) failed (non-critical)");
	}
}

static inline void mcast_sendmsg (
	struct totemudpu_instance *instance,
	const void *msg,
	unsigned int msg_len,
	int only_active)
{
	struct msghdr msg_mcast;
	int res = 0;
	size_t buf_len;
	unsigned char sheader[sizeof (struct security_header)];
	unsigned char encrypt_data[FRAME_SIZE_MAX];
	struct iovec iovec_encrypt[2];
	struct iovec iovec;
	const struct iovec *iovec_sendmsg;
	struct sockaddr_storage sockaddr;
	unsigned int iov_len;
	int addrlen;
        struct list_head *list;
	struct totemudpu_member *member;

	if (instance->totem_config->secauth == 1) {
		iovec_encrypt[0].iov_base = (void *)sheader;
		iovec_encrypt[0].iov_len = sizeof (struct security_header);
		iovec_encrypt[1].iov_base = (void *)msg;
		iovec_encrypt[1].iov_len = msg_len;

		/*
		 * Encrypt and digest the message
		 */
		encrypt_and_sign_worker (
			instance,
			encrypt_data,
			&buf_len,
			iovec_encrypt,
			2);

		if (instance->totem_config->crypto_accept == TOTEM_CRYPTO_ACCEPT_NEW) {
			encrypt_data[buf_len++] = instance->totem_config->crypto_type;
		}
		else {
			encrypt_data[buf_len++] = 0;
		}

		iovec_encrypt[0].iov_base = (void *)encrypt_data;
		iovec_encrypt[0].iov_len = buf_len;
		iovec_sendmsg = &iovec_encrypt[0];
		iov_len = 1;
	} else {
		iovec.iov_base = (void *)msg;
		iovec.iov_len = msg_len;

		iovec_sendmsg = &iovec;
		iov_len = 1;
	}

	/*
	 * Build multicast message
	 */
        for (list = instance->member_list.next;
		list != &instance->member_list;
		list = list->next) {

                member = list_entry (list,
			struct totemudpu_member,
			list);

		/*
		 * Do not send multicast message if message is not "flush", member
		 * is inactive and timeout for sending merge message didn't expired.
		 */
		if (only_active && !member->active && !instance->send_merge_detect_message)
			continue ;

		totemip_totemip_to_sockaddr_convert(&member->member,
			instance->totem_interface->ip_port, &sockaddr, &addrlen);
		msg_mcast.msg_name = &sockaddr;
		msg_mcast.msg_namelen = addrlen;
		msg_mcast.msg_iov = (void *) iovec_sendmsg;
		msg_mcast.msg_iovlen = iov_len;
	#if !defined(COROSYNC_SOLARIS)
		msg_mcast.msg_control = 0;
		msg_mcast.msg_controllen = 0;
		msg_mcast.msg_flags = 0;
	#else
		msg_mcast.msg_accrights = NULL;
		msg_mcast.msg_accrightslen = 0;
	#endif

		/*
		 * Transmit multicast message
		 * An error here is recovered by totemsrp
		 */
		res = sendmsg (member->fd, &msg_mcast, MSG_NOSIGNAL);
		if (res < 0) {
			LOGSYS_PERROR (errno, instance->totemudpu_log_level_debug,
				"sendmsg(mcast) failed (non-critical)");
		}
	}

	if (!only_active || instance->send_merge_detect_message) {
		/*
		 * Current message was sent to all nodes
		 */
		instance->merge_detect_messages_sent_before_timeout++;
		instance->send_merge_detect_message = 0;
	}
}

int totemudpu_finalize (
	void *udpu_context)
{
	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;
	int res = 0;

	if (instance->token_socket > 0) {
		close (instance->token_socket);
		poll_dispatch_delete (instance->totemudpu_poll_handle,
			instance->token_socket);
	}

	totemudpu_stop_merge_detect_timeout(instance);

	return (res);
}

static int net_deliver_fn (
	hdb_handle_t handle,
	int fd,
	int revents,
	void *data)
{
	struct totemudpu_instance *instance = (struct totemudpu_instance *)data;
	struct msghdr msg_recv;
	struct iovec *iovec;
	struct security_header *security_header;
	struct sockaddr_storage system_from;
	int bytes_received;
	int res = 0;
	unsigned char *msg_offset;
	unsigned int size_delv;

	iovec = &instance->totemudpu_iov_recv;

	/*
	 * Receive datagram
	 */
	msg_recv.msg_name = &system_from;
	msg_recv.msg_namelen = sizeof (struct sockaddr_storage);
	msg_recv.msg_iov = iovec;
	msg_recv.msg_iovlen = 1;
#if !defined(COROSYNC_SOLARIS)
	msg_recv.msg_control = 0;
	msg_recv.msg_controllen = 0;
	msg_recv.msg_flags = 0;
#else
	msg_recv.msg_accrights = NULL;
	msg_recv.msg_accrightslen = 0;
#endif

	bytes_received = recvmsg (fd, &msg_recv, MSG_NOSIGNAL | MSG_DONTWAIT);
	if (bytes_received == -1) {
		return (0);
	} else {
		instance->stats_recv += bytes_received;
	}

	if ((instance->totem_config->secauth == 1) &&
		(bytes_received < sizeof (struct security_header))) {

		log_printf (instance->totemudpu_log_level_security, "Received message is too short...  ignoring %d.\n", bytes_received);
		return (0);
	}

	security_header = (struct security_header *)iovec->iov_base;

	iovec->iov_len = bytes_received;
	if (instance->totem_config->secauth == 1) {
		/*
		 * Authenticate and if authenticated, decrypt datagram
		 */

		res = authenticate_and_decrypt (instance, iovec, 1);
		if (res == -1) {
			log_printf (instance->totemudpu_log_level_security, "Received message has invalid digest... ignoring.\n");
			log_printf (instance->totemudpu_log_level_security,
				"Invalid packet data\n");
			iovec->iov_len = FRAME_SIZE_MAX;
			return 0;
		}
		msg_offset = (unsigned char *)iovec->iov_base +
			sizeof (struct security_header);
		size_delv = bytes_received - sizeof (struct security_header);
	} else {
		msg_offset = (void *)iovec->iov_base;
		size_delv = bytes_received;
	}

	/*
	 * Handle incoming message
	 */
	instance->totemudpu_deliver_fn (
		instance->context,
		msg_offset,
		size_delv);

	iovec->iov_len = FRAME_SIZE_MAX;
	return (0);
}

static int netif_determine (
	struct totemudpu_instance *instance,
	struct totem_ip_address *bindnet,
	struct totem_ip_address *bound_to,
	int *interface_up,
	int *interface_num)
{
	int res;

	res = totemip_iface_check (bindnet, bound_to,
		interface_up, interface_num,
                instance->totem_config->clear_node_high_bit);


	return (res);
}


/*
 * If the interface is up, the sockets for totem are built.  If the interface is down
 * this function is requeued in the timer list to retry building the sockets later.
 */
static void timer_function_netif_check_timeout (
	void *data)
{
	struct totemudpu_instance *instance = (struct totemudpu_instance *)data;
	int res;
	int interface_up;
	int interface_num;
	struct totem_ip_address *bind_address;

	/*
	 * Build sockets for every interface
	 */
	netif_determine (instance,
		&instance->totem_interface->bindnet,
		&instance->totem_interface->boundto,
		&interface_up, &interface_num);
	/*
	 * If the network interface isn't back up and we are already
	 * in loopback mode, add timer to check again and return
	 */
	if ((instance->netif_bind_state == BIND_STATE_LOOPBACK &&
		interface_up == 0) ||

	(instance->my_memb_entries == 1 &&
		instance->netif_bind_state == BIND_STATE_REGULAR &&
		interface_up == 1)) {

		poll_timer_add (instance->totemudpu_poll_handle,
			instance->totem_config->downcheck_timeout,
			(void *)instance,
			timer_function_netif_check_timeout,
			&instance->timer_netif_check_timeout);

		/*
		 * Add a timer to check for a downed regular interface
		 */
		return;
	}

	if (instance->token_socket > 0) {
		close (instance->token_socket);
		poll_dispatch_delete (instance->totemudpu_poll_handle,
			instance->token_socket);
	}

	if (interface_up == 0) {
		/*
		 * Interface is not up
		 */
		instance->netif_bind_state = BIND_STATE_LOOPBACK;
		bind_address = &localhost;

		/*
		 * Add a timer to retry building interfaces and request memb_gather_enter
		 */
		poll_timer_add (instance->totemudpu_poll_handle,
			instance->totem_config->downcheck_timeout,
			(void *)instance,
			timer_function_netif_check_timeout,
			&instance->timer_netif_check_timeout);
	} else {
		/*
		 * Interface is up
		 */
		instance->netif_bind_state = BIND_STATE_REGULAR;
		bind_address = &instance->totem_interface->bindnet;
	}
	/*
	 * Create and bind the multicast and unicast sockets
	 */
	res = totemudpu_build_sockets (instance,
		bind_address,
		&instance->totem_interface->boundto);

	poll_dispatch_add (
		instance->totemudpu_poll_handle,
		instance->token_socket,
		POLLIN, instance, net_deliver_fn);

	totemip_copy (&instance->my_id, &instance->totem_interface->boundto);

	/*
	 * This reports changes in the interface to the user and totemsrp
	 */
	if (instance->netif_bind_state == BIND_STATE_REGULAR) {
		if (instance->netif_state_report & NETIF_STATE_REPORT_UP) {
			log_printf (instance->totemudpu_log_level_notice,
				"The network interface [%s] is now up.\n",
				totemip_print (&instance->totem_interface->boundto));
			instance->netif_state_report = NETIF_STATE_REPORT_DOWN;
			instance->totemudpu_iface_change_fn (instance->context, &instance->my_id);
		}
		/*
		 * Add a timer to check for interface going down in single membership
		 */
		if (instance->my_memb_entries == 1) {
			poll_timer_add (instance->totemudpu_poll_handle,
				instance->totem_config->downcheck_timeout,
				(void *)instance,
				timer_function_netif_check_timeout,
				&instance->timer_netif_check_timeout);
		}

	} else {
		if (instance->netif_state_report & NETIF_STATE_REPORT_DOWN) {
			log_printf (instance->totemudpu_log_level_notice,
				"The network interface is down.\n");
			instance->totemudpu_iface_change_fn (instance->context, &instance->my_id);
		}
		instance->netif_state_report = NETIF_STATE_REPORT_UP;

	}
}

/* Set the socket priority to INTERACTIVE to ensure
   that our messages don't get queued behind anything else */
static void totemudpu_traffic_control_set(struct totemudpu_instance *instance, int sock)
{
#ifdef SO_PRIORITY
	int prio = 6; /* TC_PRIO_INTERACTIVE */

	if (setsockopt(sock, SOL_SOCKET, SO_PRIORITY, &prio, sizeof(int))) {
		LOGSYS_PERROR (errno, instance->totemudpu_log_level_warning,
			"Could not set traffic priority");
    }
#endif
}

static int totemudpu_build_sockets_ip (
	struct totemudpu_instance *instance,
	struct totem_ip_address *bindnet_address,
	struct totem_ip_address *bound_to,
	int interface_num)
{
	struct sockaddr_storage sockaddr;
	int addrlen;
	int res;
	unsigned int recvbuf_size;
	unsigned int optlen = sizeof (recvbuf_size);

	/*
	 * Setup unicast socket
	 */
	instance->token_socket = socket (bindnet_address->family, SOCK_DGRAM, 0);
	if (instance->token_socket == -1) {
		LOGSYS_PERROR (errno, instance->totemudpu_log_level_warning,
			"socket() failed");
		return (-1);
	}

	totemip_nosigpipe (instance->token_socket);
	res = fcntl (instance->token_socket, F_SETFL, O_NONBLOCK);
	if (res == -1) {
		LOGSYS_PERROR (errno, instance->totemudpu_log_level_warning,
			"Could not set non-blocking operation on token socket");
		return (-1);
	}

	/*
	 * Bind to unicast socket used for token send/receives
	 * This has the side effect of binding to the correct interface
	 */
	totemip_totemip_to_sockaddr_convert(bound_to, instance->totem_interface->ip_port, &sockaddr, &addrlen);
	res = bind (instance->token_socket, (struct sockaddr *)&sockaddr, addrlen);
	if (res == -1) {
		LOGSYS_PERROR (errno, instance->totemudpu_log_level_warning,
			"bind token socket failed");
		return (-1);
	}

	/*
	 * the token_socket can receive many messages.  Allow a large number
	 * of receive messages on this socket
	 */
	recvbuf_size = MCAST_SOCKET_BUFFER_SIZE;
	res = setsockopt (instance->token_socket, SOL_SOCKET, SO_RCVBUF,
		&recvbuf_size, optlen);
	if (res == -1) {
		LOGSYS_PERROR (errno, instance->totemudpu_log_level_notice,
			"Could not set recvbuf size");
	}

	return 0;
}

static int totemudpu_build_sockets (
	struct totemudpu_instance *instance,
	struct totem_ip_address *bindnet_address,
	struct totem_ip_address *bound_to)
{
	int interface_num;
	int interface_up;
	int res;

	/*
	 * Determine the ip address bound to and the interface name
	 */
	res = netif_determine (instance,
		bindnet_address,
		bound_to,
		&interface_up,
		&interface_num);

	if (res == -1) {
		return (-1);
	}

	totemip_copy(&instance->my_id, bound_to);

	res = totemudpu_build_sockets_ip (instance,
		bindnet_address, bound_to, interface_num);

	/* We only send out of the token socket */
	totemudpu_traffic_control_set(instance, instance->token_socket);

	/*
	 * Rebind all members to new ips
	 */
	totemudpu_member_list_rebind_ip(instance);

	return res;
}

/*
 * Totem Network interface - also does encryption/decryption
 * depends on poll abstraction, POSIX, IPV4
 */

/*
 * Create an instance
 */
int totemudpu_initialize (
	hdb_handle_t poll_handle,
	void **udpu_context,
	struct totem_config *totem_config,
	totemsrp_stats_t *stats,
	int interface_no,
	void *context,

	void (*deliver_fn) (
		void *context,
		const void *msg,
		unsigned int msg_len),

	void (*iface_change_fn) (
		void *context,
		const struct totem_ip_address *iface_address),

	void (*target_set_completed) (
		void *context))
{
	struct totemudpu_instance *instance;

	instance = malloc (sizeof (struct totemudpu_instance));
	if (instance == NULL) {
		return (-1);
	}

	totemudpu_instance_initialize (instance);

	instance->totem_config = totem_config;
	instance->stats = stats;

	/*
	* Configure logging
	*/
	instance->totemudpu_log_level_security = 1; //totem_config->totem_logging_configuration.log_level_security;
	instance->totemudpu_log_level_error = totem_config->totem_logging_configuration.log_level_error;
	instance->totemudpu_log_level_warning = totem_config->totem_logging_configuration.log_level_warning;
	instance->totemudpu_log_level_notice = totem_config->totem_logging_configuration.log_level_notice;
	instance->totemudpu_log_level_debug = totem_config->totem_logging_configuration.log_level_debug;
	instance->totemudpu_subsys_id = totem_config->totem_logging_configuration.log_subsys_id;
	instance->totemudpu_log_printf = totem_config->totem_logging_configuration.log_printf;

	/*
	* Initialize random number generator for later use to generate salt
	*/
	memcpy (instance->totemudpu_private_key, totem_config->private_key,
		totem_config->private_key_len);

	instance->totemudpu_private_key_len = totem_config->private_key_len;

	init_crypto(instance);

	/*
	 * Initialize local variables for totemudpu
	 */
	instance->totem_interface = &totem_config->interfaces[interface_no];
	memset (instance->iov_buffer, 0, FRAME_SIZE_MAX);

	instance->totemudpu_poll_handle = poll_handle;

	instance->totem_interface->bindnet.nodeid = instance->totem_config->node_id;

	instance->context = context;
	instance->totemudpu_deliver_fn = deliver_fn;

	instance->totemudpu_iface_change_fn = iface_change_fn;

	instance->totemudpu_target_set_completed = target_set_completed;

        totemip_localhost (AF_INET, &localhost);
	localhost.nodeid = instance->totem_config->node_id;

	/*
	 * RRP layer isn't ready to receive message because it hasn't
	 * initialized yet.  Add short timer to check the interfaces.
	 */
	poll_timer_add (instance->totemudpu_poll_handle,
		100,
		(void *)instance,
		timer_function_netif_check_timeout,
		&instance->timer_netif_check_timeout);

	totemudpu_start_merge_detect_timeout(instance);

	*udpu_context = instance;
	return (0);
}

int totemudpu_processor_count_set (
	void *udpu_context,
	int processor_count)
{
	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;
	int res = 0;

	instance->my_memb_entries = processor_count;
	poll_timer_delete (instance->totemudpu_poll_handle,
		instance->timer_netif_check_timeout);
	if (processor_count == 1) {
		poll_timer_add (instance->totemudpu_poll_handle,
			instance->totem_config->downcheck_timeout,
			(void *)instance,
			timer_function_netif_check_timeout,
			&instance->timer_netif_check_timeout);
	}

	return (res);
}

int totemudpu_recv_flush (void *udpu_context)
{
	int res = 0;

	return (res);
}

int totemudpu_send_flush (void *udpu_context)
{
	int res = 0;

	return (res);
}

int totemudpu_token_send (
	void *udpu_context,
	const void *msg,
	unsigned int msg_len)
{
	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;
	int res = 0;

	ucast_sendmsg (instance, &instance->token_target, msg, msg_len);

	return (res);
}
int totemudpu_mcast_flush_send (
	void *udpu_context,
	const void *msg,
	unsigned int msg_len)
{
	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;
	int res = 0;

	mcast_sendmsg (instance, msg, msg_len, 0);

	return (res);
}

int totemudpu_mcast_noflush_send (
	void *udpu_context,
	const void *msg,
	unsigned int msg_len)
{
	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;
	int res = 0;

	mcast_sendmsg (instance, msg, msg_len, 1);

	return (res);
}

extern int totemudpu_iface_check (void *udpu_context)
{
	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;
	int res = 0;

	timer_function_netif_check_timeout (instance);

	return (res);
}

extern void totemudpu_net_mtu_adjust (void *udpu_context, struct totem_config *totem_config)
{

	assert(totem_config->interface_count > 0);

	if (totem_config->secauth == 1) {
		totem_config->net_mtu -= sizeof (struct security_header) +
			totemip_udpip_header_size(totem_config->interfaces[0].bindnet.family);
	} else {
		totem_config->net_mtu -= totemip_udpip_header_size(totem_config->interfaces[0].bindnet.family);
	}
}

const char *totemudpu_iface_print (void *udpu_context)  {
	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;
	const char *ret_char;

	ret_char = totemip_print (&instance->my_id);

	return (ret_char);
}

int totemudpu_iface_get (
	void *udpu_context,
	struct totem_ip_address *addr)
{
	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;
	int res = 0;

	memcpy (addr, &instance->my_id, sizeof (struct totem_ip_address));

	return (res);
}

int totemudpu_token_target_set (
	void *udpu_context,
	const struct totem_ip_address *token_target)
{
	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;
	int res = 0;

	memcpy (&instance->token_target, token_target,
		sizeof (struct totem_ip_address));

	instance->totemudpu_target_set_completed (instance->context);

	return (res);
}

extern int totemudpu_recv_mcast_empty (
	void *udpu_context)
{
	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;
	unsigned int res;
	struct sockaddr_storage system_from;
	struct msghdr msg_recv;
	struct pollfd ufd;
	int nfds;
	int msg_processed = 0;

	/*
	 * Receive datagram
	 */
	msg_recv.msg_name = &system_from;
	msg_recv.msg_namelen = sizeof (struct sockaddr_storage);
	msg_recv.msg_iov = &instance->totemudpu_iov_recv;
	msg_recv.msg_iovlen = 1;
#if !defined(COROSYNC_SOLARIS)
	msg_recv.msg_control = 0;
	msg_recv.msg_controllen = 0;
	msg_recv.msg_flags = 0;
#else
	msg_recv.msg_accrights = NULL;
	msg_recv.msg_accrightslen = 0;
#endif

	do {
		ufd.fd = instance->token_socket;
		ufd.events = POLLIN;
		nfds = poll (&ufd, 1, 0);
		if (nfds == 1 && ufd.revents & POLLIN) {
			res = recvmsg (instance->token_socket, &msg_recv, MSG_NOSIGNAL | MSG_DONTWAIT);
			if (res != -1) {
				msg_processed = 1;
			} else {
				msg_processed = -1;
			}
		}
	} while (nfds == 1);

	return (msg_processed);
}

static int totemudpu_create_sending_socket(
	void *udpu_context,
	const struct totem_ip_address *member)
{
	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;
	int fd;
	int res;
	unsigned int sendbuf_size;
	unsigned int optlen = sizeof (sendbuf_size);
	struct sockaddr_storage sockaddr;
	int addrlen;

	fd = socket (member->family, SOCK_DGRAM, 0);
	if (fd == -1) {
		LOGSYS_PERROR (errno, instance->totemudpu_log_level_warning,
			"Could not create socket for new member");
		return (-1);
	}
	totemip_nosigpipe (fd);
	res = fcntl (fd, F_SETFL, O_NONBLOCK);
	if (res == -1) {
		LOGSYS_PERROR (errno, instance->totemudpu_log_level_warning,
			"Could not set non-blocking operation on token socket");
		return (-1);
	}

	/*
 	 * These sockets are used to send multicast messages, so their buffers
 	 * should be large
 	 */
	sendbuf_size = MCAST_SOCKET_BUFFER_SIZE;
	res = setsockopt (fd, SOL_SOCKET, SO_SNDBUF,
		&sendbuf_size, optlen);
	if (res == -1) {
		LOGSYS_PERROR (errno, instance->totemudpu_log_level_notice,
			"Could not set sendbuf size");
	}

	/*
	 * Bind to sending interface
	 */
	totemip_totemip_to_sockaddr_convert(&instance->my_id, 0, &sockaddr, &addrlen);
	res = bind (fd, (struct sockaddr *)&sockaddr, addrlen);
	if (res == -1) {
		LOGSYS_PERROR (errno, instance->totemudpu_log_level_warning,
			"bind token socket failed");
		return (-1);
	}

	return (fd);

}

int totemudpu_member_add (
	void *udpu_context,
	const struct totem_ip_address *member)
{
	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;

	struct totemudpu_member *new_member;

	new_member = malloc (sizeof (struct totemudpu_member));
	if (new_member == NULL) {
		return (-1);
	}

	memset(new_member, 0, sizeof(*new_member));

	log_printf (LOGSYS_LEVEL_NOTICE, "adding new UDPU member {%s}",
		totemip_print(member));
	list_init (&new_member->list);
	list_add_tail (&new_member->list, &instance->member_list);
	memcpy (&new_member->member, member, sizeof (struct totem_ip_address));
	new_member->fd = totemudpu_create_sending_socket(udpu_context, member);
	new_member->active = 0;

	return (0);
}

int totemudpu_member_remove (
	void *udpu_context,
	const struct totem_ip_address *token_target)
{
	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;

	instance = NULL;
	return (0);
}

int totemudpu_member_list_rebind_ip (
	void *udpu_context)
{
	struct list_head *list;
	struct totemudpu_member *member;

	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;

	for (list = instance->member_list.next;
		list != &instance->member_list;
		list = list->next) {

		member = list_entry (list,
			struct totemudpu_member,
			list);

		if (member->fd > 0) {
			close (member->fd);
		}

		member->fd = totemudpu_create_sending_socket(udpu_context, &member->member);
	}

	return (0);
}

int totemudpu_member_set_active (
	void *udpu_context,
	const struct totem_ip_address *member_ip,
	int active)
{
	struct list_head *list;
	struct totemudpu_member *member;
	int addr_found = 0;

	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;

	/*
	 * Find the member to set active flag
	 */
	for (list = instance->member_list.next; list != &instance->member_list;	list = list->next) {
		member = list_entry (list, struct totemudpu_member, list);

		if (totemip_compare (member_ip, &member->member) == 0) {
			log_printf(LOGSYS_LEVEL_DEBUG,
			    "Marking UDPU member %s %s",
			    totemip_print(&member->member),
			    (active ? "active" : "inactive"));

			member->active = active;
			addr_found = 1;

			break;
		}
	}

	if (!addr_found) {
		log_printf(LOGSYS_LEVEL_DEBUG,
		    "Can't find UDPU member %s (should be marked as %s)",
			    totemip_print(member_ip),
			    (active ? "active" : "inactive"));
	}

	return (0);
}

static void timer_function_merge_detect_timeout (
	void *data)
{
	struct totemudpu_instance *instance = (struct totemudpu_instance *)data;

	if (instance->merge_detect_messages_sent_before_timeout == 0) {
		instance->send_merge_detect_message = 1;
	}

	instance->merge_detect_messages_sent_before_timeout = 0;

	totemudpu_start_merge_detect_timeout(instance);
}

static void totemudpu_start_merge_detect_timeout(
	void *udpu_context)
{
	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;

	poll_timer_add(instance->totemudpu_poll_handle,
	    instance->totem_config->merge_timeout * 2,
	    (void *)instance,
	    timer_function_merge_detect_timeout,
	    &instance->timer_merge_detect_timeout);

}

static void totemudpu_stop_merge_detect_timeout(
	void *udpu_context)
{
	struct totemudpu_instance *instance = (struct totemudpu_instance *)udpu_context;

	poll_timer_delete(instance->totemudpu_poll_handle,
	    instance->timer_merge_detect_timeout);
}
