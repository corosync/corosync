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
#include <corosync/list.h>
#include <corosync/hdb.h>
#include <corosync/swab.h>
#include <corosync/totem/coropoll.h>
#define LOGSYS_UTILS_ONLY 1
#include <corosync/engine/logsys.h>
#include "totemudp.h"
#include "wthread.h"

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

#define MESSAGE_TYPE_MEMB_JOIN	3

#define HMAC_HASH_SIZE 20
struct security_header {
	unsigned char hash_digest[HMAC_HASH_SIZE]; /* The hash *MUST* be first in the data structure */
	unsigned char salt[16]; /* random number */
	char msg[0];
} __attribute__((packed));

struct totemudp_mcast_thread_state {
	unsigned char iobuf[FRAME_SIZE_MAX];
	prng_state prng_state;
};

struct totemudp_socket {
	int mcast_recv;
	int mcast_send;
	int token;
	/*
	 * Socket used for local multicast delivery. We don't rely on multicast
	 * loop and rather this UNIX DGRAM socket is used. Socket is created by
	 * socketpair call and they are used in same way as pipe (so [0] is read
	 * end and [1] is write end)
	 */
	int local_mcast_loop[2];
};

struct totemudp_instance {
	hmac_state totemudp_hmac_state;

	prng_state totemudp_prng_state;

#ifdef HAVE_LIBNSS
	PK11SymKey   *nss_sym_key;
	PK11SymKey   *nss_sym_key_sign;
#endif

	unsigned char totemudp_private_key[1024];

	unsigned int totemudp_private_key_len;

	hdb_handle_t totemudp_poll_handle;

	struct totem_interface *totem_interface;

	int netif_state_report;

	int netif_bind_state;

	struct worker_thread_group worker_thread_group;

	void *context;

	void (*totemudp_deliver_fn) (
		void *context,
		const void *msg,
		unsigned int msg_len);

	void (*totemudp_iface_change_fn) (
		void *context,
		const struct totem_ip_address *iface_address);

	void (*totemudp_target_set_completed) (void *context);

	/*
	 * Function and data used to log messages
	 */
	int totemudp_log_level_security;

	int totemudp_log_level_error;

	int totemudp_log_level_warning;

	int totemudp_log_level_notice;

	int totemudp_log_level_debug;

	int totemudp_subsys_id;

	void (*totemudp_log_printf) (
		unsigned int rec_ident,
		const char *function,
		const char *file,
		int line,
		const char *format,
		...)__attribute__((format(printf, 5, 6)));

	void *udp_context;

	char iov_buffer[FRAME_SIZE_MAX];

	char iov_buffer_flush[FRAME_SIZE_MAX];

	struct iovec totemudp_iov_recv;

	struct iovec totemudp_iov_recv_flush;

	struct totemudp_socket totemudp_sockets;

	struct totem_ip_address mcast_address;

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

	int flushing;

	struct totem_config *totem_config;

	totemsrp_stats_t *stats;

	struct totem_ip_address token_target;
};

struct work_item {
	const void *msg;
	unsigned int msg_len;
	struct totemudp_instance *instance;
};

static int totemudp_build_sockets (
	struct totemudp_instance *instance,
	struct totem_ip_address *bindnet_address,
	struct totem_ip_address *mcastaddress,
	struct totemudp_socket *sockets,
	struct totem_ip_address *bound_to);

static struct totem_ip_address localhost;

static void totemudp_instance_initialize (struct totemudp_instance *instance)
{
	memset (instance, 0, sizeof (struct totemudp_instance));

	instance->netif_state_report = NETIF_STATE_REPORT_UP | NETIF_STATE_REPORT_DOWN;

	instance->totemudp_iov_recv.iov_base = instance->iov_buffer;

	instance->totemudp_iov_recv.iov_len = FRAME_SIZE_MAX; //sizeof (instance->iov_buffer);
	instance->totemudp_iov_recv_flush.iov_base = instance->iov_buffer_flush;

	instance->totemudp_iov_recv_flush.iov_len = FRAME_SIZE_MAX; //sizeof (instance->iov_buffer);

	/*
	 * There is always atleast 1 processor
	 */
	instance->my_memb_entries = 1;
}

#define log_printf(level, format, args...)				\
do {									\
        instance->totemudp_log_printf (					\
		LOGSYS_ENCODE_RECID(level,				\
				    instance->totemudp_subsys_id,	\
				    LOGSYS_RECID_LOG),			\
                __FUNCTION__, __FILE__, __LINE__,			\
		(const char *)format, ##args);				\
} while (0);


static int authenticate_and_decrypt_sober (
	struct totemudp_instance *instance,
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
	sober128_add_entropy (instance->totemudp_private_key,
		instance->totemudp_private_key_len, &keygen_prng_state);
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
	hmac_init (&instance->totemudp_hmac_state, DIGEST_SHA1, hmac_key, 16);

	hmac_process (&instance->totemudp_hmac_state,
		(unsigned char *)iov->iov_base + HMAC_HASH_SIZE,
		iov->iov_len - HMAC_HASH_SIZE);

	len = hash_descriptor[DIGEST_SHA1]->hashsize;
	assert (HMAC_HASH_SIZE >= len);
	hmac_done (&instance->totemudp_hmac_state, digest_comparison, &len);

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
	struct totemudp_instance *instance)
{
	log_printf(instance->totemudp_log_level_notice,
		"Initializing transmit/receive security: libtomcrypt SOBER128/SHA1HMAC (mode 0).\n");
	rng_make_prng (128, PRNG_SOBER, &instance->totemudp_prng_state, NULL);
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
	struct totemudp_instance *instance)
{
	PK11SlotInfo*      aes_slot = NULL;
	PK11SlotInfo*      sha1_slot = NULL;
	SECItem            key_item;
	SECStatus          rv;

	log_printf(instance->totemudp_log_level_notice,
		"Initializing transmit/receive security: NSS AES128CBC/SHA1HMAC (mode 1).\n");
	rv = NSS_NoDB_Init(".");
	if (rv != SECSuccess)
	{
		log_printf(instance->totemudp_log_level_security, "NSS initialization failed (err %d)\n",
			PR_GetError());
		goto out;
	}

	aes_slot = PK11_GetBestSlot(instance->totem_config->crypto_crypt_type, NULL);
	if (aes_slot == NULL)
	{
		log_printf(instance->totemudp_log_level_security, "Unable to find security slot (err %d)\n",
			PR_GetError());
		goto out;
	}

	sha1_slot = PK11_GetBestSlot(CKM_SHA_1_HMAC, NULL);
	if (sha1_slot == NULL)
	{
		log_printf(instance->totemudp_log_level_security, "Unable to find security slot (err %d)\n",
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
		log_printf(instance->totemudp_log_level_security, "Failure to import key into NSS (err %d)\n",
			PR_GetError());
		goto out;
	}

	instance->nss_sym_key_sign = PK11_ImportSymKey(sha1_slot,
		CKM_SHA_1_HMAC,
		PK11_OriginUnwrap, CKA_SIGN,
		&key_item, NULL);
	if (instance->nss_sym_key_sign == NULL) {
		log_printf(instance->totemudp_log_level_security, "Failure to import key into NSS (err %d)\n",
			PR_GetError());
		goto out;
	}
out:
	return;
}

static int encrypt_and_sign_nss (
	struct totemudp_instance *instance,
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
		log_printf(instance->totemudp_log_level_security, "malloc error copying buffer from iovec\n");
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
		log_printf(instance->totemudp_log_level_security,
			"Failure to generate a random number %d\n",
			PR_GetError());
		free(inbuf);
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
		log_printf(instance->totemudp_log_level_security,
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
		log_printf(instance->totemudp_log_level_security,
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
		log_printf(instance->totemudp_log_level_security, "encrypt: PK11_CreateContext failed (digest) err %d: %s\n",
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
	struct totemudp_instance *instance,
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
			log_printf(instance->totemudp_log_level_security, "malloc error copying buffer from iovec\n");
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
		log_printf(instance->totemudp_log_level_security, "PK11_CreateContext failed (check digest) err %d: %s\n",
			PR_GetError(), err);
		goto out;
	}

	PK11_DigestBegin(enc_context);

	rv1 = PK11_DigestOp(enc_context, data, datalen);
	rv2 = PK11_DigestFinal(enc_context, digest, &tmp2_outlen, sizeof(digest));

	PK11_DestroyContext(enc_context, PR_TRUE);

	if (rv1 != SECSuccess || rv2 != SECSuccess) {
		log_printf(instance->totemudp_log_level_security, "Digest check failed\n");
		goto out;
	}

	if (memcmp(digest, header->hash_digest, tmp2_outlen) != 0) {
		log_printf(instance->totemudp_log_level_error, "Digest does not match\n");
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
		log_printf(instance->totemudp_log_level_security,
			"PK11_CreateContext (decrypt) failed (err %d)\n",
			PR_GetError());
		goto out;
	}

	rv1 = PK11_CipherOp(enc_context, outdata, &tmp1_outlen,
			    sizeof(outbuf) - sizeof (struct security_header),
			    data, datalen);
	if (rv1 != SECSuccess) {
		log_printf(instance->totemudp_log_level_security,
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
	struct totemudp_instance *instance,
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
	prng_state *prng_state_in = &instance->totemudp_prng_state;

	header = (struct security_header *)buf;
	addr = buf + sizeof (struct security_header);

	memset (keys, 0, sizeof (keys));
	memset (header->salt, 0, sizeof (header->salt));

	/*
	 * Generate MAC, CIPHER, IV keys from private key
	 */
	sober128_read (header->salt, sizeof (header->salt), prng_state_in);
	sober128_start (&keygen_prng_state);
	sober128_add_entropy (instance->totemudp_private_key,
		instance->totemudp_private_key_len,
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
	struct totemudp_instance *instance,
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
	struct totemudp_instance *instance,
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
	struct totemudp_instance *instance)
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

int totemudp_crypto_set (
	void *udp_context,
	 unsigned int type)
{
	struct totemudp_instance *instance = (struct totemudp_instance *)udp_context;
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
				log_printf(instance->totemudp_log_level_security,
					"Transmit security set to: libtomcrypt SOBER128/SHA1HMAC (mode 0)");
				break;
			case TOTEM_CRYPTO_NSS:
				log_printf(instance->totemudp_log_level_security,
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
	struct totemudp_instance *instance,
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
	res = sendmsg (instance->totemudp_sockets.mcast_send, &msg_ucast,
		MSG_NOSIGNAL);
	if (res < 0) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_debug,
			"sendmsg(ucast) failed (non-critical)");
	}
}

static inline void mcast_sendmsg (
	struct totemudp_instance *instance,
	const void *msg,
	unsigned int msg_len)
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
	totemip_totemip_to_sockaddr_convert(&instance->mcast_address,
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
	res = sendmsg (instance->totemudp_sockets.mcast_send, &msg_mcast,
		MSG_NOSIGNAL);
	if (res < 0) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_debug,
			"sendmsg(mcast) failed (non-critical)");
		instance->stats->continuous_sendmsg_failures++;
	} else {
		instance->stats->continuous_sendmsg_failures = 0;
	}

	/*
	 * Transmit multicast message to local unix mcast loop
	 * An error here is recovered by totemsrp
	 */
	msg_mcast.msg_name = NULL;
	msg_mcast.msg_namelen = 0;

	res = sendmsg (instance->totemudp_sockets.local_mcast_loop[1], &msg_mcast,
		MSG_NOSIGNAL);
	if (res < 0) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_debug,
			"sendmsg(local mcast loop) failed (non-critical)");
	}
}

static void totemudp_mcast_thread_state_constructor (
	void *totemudp_mcast_thread_state_in)
{
	struct totemudp_mcast_thread_state *totemudp_mcast_thread_state =
		(struct totemudp_mcast_thread_state *)totemudp_mcast_thread_state_in;
	memset (totemudp_mcast_thread_state, 0,
		sizeof (*totemudp_mcast_thread_state));

	rng_make_prng (128, PRNG_SOBER,
		&totemudp_mcast_thread_state->prng_state, NULL);
}


static void totemudp_mcast_worker_fn (void *thread_state, void *work_item_in)
{
	struct work_item *work_item = (struct work_item *)work_item_in;
	struct totemudp_mcast_thread_state *totemudp_mcast_thread_state =
		(struct totemudp_mcast_thread_state *)thread_state;
	struct totemudp_instance *instance = work_item->instance;
	struct msghdr msg_mcast;
	unsigned char sheader[sizeof (struct security_header)];
	int res = 0;
	size_t buf_len;
	struct iovec iovec_enc[2];
	struct iovec iovec;
	struct sockaddr_storage sockaddr;
	int addrlen;

	if (instance->totem_config->secauth == 1) {
		iovec_enc[0].iov_base = (void *)sheader;
		iovec_enc[0].iov_len = sizeof (struct security_header);
		iovec_enc[1].iov_base = (void *)work_item->msg;
		iovec_enc[1].iov_len = work_item->msg_len;

		/*
		 * Encrypt and digest the message
		 */
		encrypt_and_sign_worker (
			instance,
			totemudp_mcast_thread_state->iobuf,
			&buf_len,
			iovec_enc, 2);

		iovec.iov_base = (void *)totemudp_mcast_thread_state->iobuf;
		iovec.iov_len = buf_len;
	} else {
		iovec.iov_base = (void *)work_item->msg;
		iovec.iov_len = work_item->msg_len;
	}

	totemip_totemip_to_sockaddr_convert(&instance->mcast_address,
		instance->totem_interface->ip_port, &sockaddr, &addrlen);

	msg_mcast.msg_name = &sockaddr;
	msg_mcast.msg_namelen = addrlen;
	msg_mcast.msg_iov = &iovec;
	msg_mcast.msg_iovlen = 1;
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
	 * An error here is recovered by totemudp
	 */
	res = sendmsg (instance->totemudp_sockets.mcast_send, &msg_mcast,
		MSG_NOSIGNAL);
	if (res < 0) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_debug,
			"sendmsg(mcast) failed (non-critical)");
	}
}

int totemudp_finalize (
	void *udp_context)
{
	struct totemudp_instance *instance = (struct totemudp_instance *)udp_context;
	int res = 0;

	worker_thread_group_exit (&instance->worker_thread_group);

	if (instance->totemudp_sockets.mcast_recv > 0) {
		close (instance->totemudp_sockets.mcast_recv);
	 	poll_dispatch_delete (instance->totemudp_poll_handle,
			instance->totemudp_sockets.mcast_recv);
	}
	if (instance->totemudp_sockets.mcast_send > 0) {
		close (instance->totemudp_sockets.mcast_send);
	}
	if (instance->totemudp_sockets.local_mcast_loop[0] > 0) {
		poll_dispatch_delete (instance->totemudp_poll_handle,
			instance->totemudp_sockets.local_mcast_loop[0]);
		close (instance->totemudp_sockets.local_mcast_loop[0]);
		close (instance->totemudp_sockets.local_mcast_loop[1]);
	}
	if (instance->totemudp_sockets.token > 0) {
		close (instance->totemudp_sockets.token);
		poll_dispatch_delete (instance->totemudp_poll_handle,
			instance->totemudp_sockets.token);
	}

	return (res);
}

/*
 * Only designed to work with a message with one iov
 */

static int net_deliver_fn (
	hdb_handle_t handle,
	int fd,
	int revents,
	void *data)
{
	struct totemudp_instance *instance = (struct totemudp_instance *)data;
	struct msghdr msg_recv;
	struct iovec *iovec;
	struct security_header *security_header;
	struct sockaddr_storage system_from;
	int bytes_received;
	int res = 0;
	unsigned char *msg_offset;
	unsigned int size_delv;
	char *message_type;

	if (instance->flushing == 1) {
		iovec = &instance->totemudp_iov_recv_flush;
	} else {
		iovec = &instance->totemudp_iov_recv;
	}

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

		log_printf (instance->totemudp_log_level_security, "Received message is too short...  ignoring %d.\n", bytes_received);
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
			log_printf (instance->totemudp_log_level_security, "Received message has invalid digest... ignoring.\n");
			log_printf (instance->totemudp_log_level_security,
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
	 * Drop all non-mcast messages (more specifically join
	 * messages should be dropped)
	 */
	message_type = (char *)msg_offset;
	if (instance->flushing == 1 && *message_type == MESSAGE_TYPE_MEMB_JOIN) {
		log_printf(instance->totemudp_log_level_warning, "JOIN or LEAVE message was thrown away during flush operation.");
		iovec->iov_len = FRAME_SIZE_MAX;
		return (0);
	}
	
	/*
	 * Handle incoming message
	 */
	instance->totemudp_deliver_fn (
		instance->context,
		msg_offset,
		size_delv);

	iovec->iov_len = FRAME_SIZE_MAX;
	return (0);
}

static int netif_determine (
	struct totemudp_instance *instance,
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
	struct totemudp_instance *instance = (struct totemudp_instance *)data;
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

		poll_timer_add (instance->totemudp_poll_handle,
			instance->totem_config->downcheck_timeout,
			(void *)instance,
			timer_function_netif_check_timeout,
			&instance->timer_netif_check_timeout);

		/*
		 * Add a timer to check for a downed regular interface
		 */
		return;
	}

	if (instance->totemudp_sockets.mcast_recv > 0) {
		close (instance->totemudp_sockets.mcast_recv);
	 	poll_dispatch_delete (instance->totemudp_poll_handle,
			instance->totemudp_sockets.mcast_recv);
	}
	if (instance->totemudp_sockets.mcast_send > 0) {
		close (instance->totemudp_sockets.mcast_send);
	}
	if (instance->totemudp_sockets.local_mcast_loop[0] > 0) {
		poll_dispatch_delete (instance->totemudp_poll_handle,
			instance->totemudp_sockets.local_mcast_loop[0]);
		close (instance->totemudp_sockets.local_mcast_loop[0]);
		close (instance->totemudp_sockets.local_mcast_loop[1]);
	}
	if (instance->totemudp_sockets.token > 0) {
		close (instance->totemudp_sockets.token);
		poll_dispatch_delete (instance->totemudp_poll_handle,
			instance->totemudp_sockets.token);
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
		poll_timer_add (instance->totemudp_poll_handle,
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
	res = totemudp_build_sockets (instance,
		&instance->mcast_address,
		bind_address,
		&instance->totemudp_sockets,
		&instance->totem_interface->boundto);

	poll_dispatch_add (
		instance->totemudp_poll_handle,
		instance->totemudp_sockets.mcast_recv,
		POLLIN, instance, net_deliver_fn);

	poll_dispatch_add (
		instance->totemudp_poll_handle,
		instance->totemudp_sockets.local_mcast_loop[0],
		POLLIN, instance, net_deliver_fn);

	poll_dispatch_add (
		instance->totemudp_poll_handle,
		instance->totemudp_sockets.token,
		POLLIN, instance, net_deliver_fn);

	totemip_copy (&instance->my_id, &instance->totem_interface->boundto);

	/*
	 * This reports changes in the interface to the user and totemsrp
	 */
	if (instance->netif_bind_state == BIND_STATE_REGULAR) {
		if (instance->netif_state_report & NETIF_STATE_REPORT_UP) {
			log_printf (instance->totemudp_log_level_notice,
				"The network interface [%s] is now up.\n",
				totemip_print (&instance->totem_interface->boundto));
			instance->netif_state_report = NETIF_STATE_REPORT_DOWN;
			instance->totemudp_iface_change_fn (instance->context, &instance->my_id);
		}
		/*
		 * Add a timer to check for interface going down in single membership
		 */
		if (instance->my_memb_entries == 1) {
			poll_timer_add (instance->totemudp_poll_handle,
				instance->totem_config->downcheck_timeout,
				(void *)instance,
				timer_function_netif_check_timeout,
				&instance->timer_netif_check_timeout);
		}

	} else {
		if (instance->netif_state_report & NETIF_STATE_REPORT_DOWN) {
			log_printf (instance->totemudp_log_level_notice,
				"The network interface is down.\n");
			instance->totemudp_iface_change_fn (instance->context, &instance->my_id);
		}
		instance->netif_state_report = NETIF_STATE_REPORT_UP;

	}
}

/* Set the socket priority to INTERACTIVE to ensure
   that our messages don't get queued behind anything else */
static void totemudp_traffic_control_set(struct totemudp_instance *instance, int sock)
{
#ifdef SO_PRIORITY
	int prio = 6; /* TC_PRIO_INTERACTIVE */

	if (setsockopt(sock, SOL_SOCKET, SO_PRIORITY, &prio, sizeof(int))) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_warning, "Could not set traffic priority");
    }
#endif
}

static int totemudp_build_sockets_ip (
	struct totemudp_instance *instance,
	struct totem_ip_address *mcast_address,
	struct totem_ip_address *bindnet_address,
	struct totemudp_socket *sockets,
	struct totem_ip_address *bound_to,
	int interface_num)
{
	struct sockaddr_storage sockaddr;
	struct ipv6_mreq mreq6;
	struct ip_mreq mreq;
	struct sockaddr_storage mcast_ss, boundto_ss;
	struct sockaddr_in6 *mcast_sin6 = (struct sockaddr_in6 *)&mcast_ss;
	struct sockaddr_in  *mcast_sin = (struct sockaddr_in *)&mcast_ss;
	struct sockaddr_in  *boundto_sin = (struct sockaddr_in *)&boundto_ss;
	unsigned int sendbuf_size;
        unsigned int recvbuf_size;
        unsigned int optlen = sizeof (sendbuf_size);
	int addrlen;
	int res;
	int flag;
	int i;

	/*
	 * Create multicast recv socket
	 */
	sockets->mcast_recv = socket (bindnet_address->family, SOCK_DGRAM, 0);
	if (sockets->mcast_recv == -1) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
			"socket() failed");
		return (-1);
	}

	totemip_nosigpipe (sockets->mcast_recv);
	res = fcntl (sockets->mcast_recv, F_SETFL, O_NONBLOCK);
	if (res == -1) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
			"Could not set non-blocking operation on multicast socket");
		return (-1);
	}

	/*
	 * Force reuse
	 */
	 flag = 1;
	 if ( setsockopt(sockets->mcast_recv, SOL_SOCKET, SO_REUSEADDR, (char *)&flag, sizeof (flag)) < 0) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
				"setsockopt(SO_REUSEADDR) failed");
		return (-1);
	}

	/*
	 * Bind to multicast socket used for multicast receives
	 */
	totemip_totemip_to_sockaddr_convert(mcast_address,
		instance->totem_interface->ip_port, &sockaddr, &addrlen);
	res = bind (sockets->mcast_recv, (struct sockaddr *)&sockaddr, addrlen);
	if (res == -1) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
				"Unable to bind the socket to receive multicast packets");
		return (-1);
	}

	/*
	 * Create local multicast loop socket
	 */
	if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sockets->local_mcast_loop) == -1) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
			"socket() failed");
		return (-1);
	}

	for (i = 0; i < 2; i++) {
		totemip_nosigpipe (sockets->local_mcast_loop[i]);
		res = fcntl (sockets->local_mcast_loop[i], F_SETFL, O_NONBLOCK);
		if (res == -1) {
			LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
				"Could not set non-blocking operation on multicast socket");
			return (-1);
		}
	}

	/*
	 * Setup mcast send socket
	 */
	sockets->mcast_send = socket (bindnet_address->family, SOCK_DGRAM, 0);
	if (sockets->mcast_send == -1) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
			"socket() failed");
		return (-1);
	}

	totemip_nosigpipe (sockets->mcast_send);
	res = fcntl (sockets->mcast_send, F_SETFL, O_NONBLOCK);
	if (res == -1) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
			"Could not set non-blocking operation on multicast socket");
		return (-1);
	}

	/*
	 * Force reuse
	 */
	 flag = 1;
	 if ( setsockopt(sockets->mcast_send, SOL_SOCKET, SO_REUSEADDR, (char *)&flag, sizeof (flag)) < 0) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
			"setsockopt(SO_REUSEADDR) failed");
		return (-1);
	}

	totemip_totemip_to_sockaddr_convert(bound_to, instance->totem_interface->ip_port - 1,
		&sockaddr, &addrlen);
	res = bind (sockets->mcast_send, (struct sockaddr *)&sockaddr, addrlen);
	if (res == -1) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
			"Unable to bind the socket to send multicast packets");
		return (-1);
	}

	/*
	 * Setup unicast socket
	 */
	sockets->token = socket (bindnet_address->family, SOCK_DGRAM, 0);
	if (sockets->token == -1) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
			"socket() failed");
		return (-1);
	}

	totemip_nosigpipe (sockets->token);
	res = fcntl (sockets->token, F_SETFL, O_NONBLOCK);
	if (res == -1) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
			"Could not set non-blocking operation on token socket");
		return (-1);
	}

	/*
	 * Force reuse
	 */
	 flag = 1;
	 if ( setsockopt(sockets->token, SOL_SOCKET, SO_REUSEADDR, (char *)&flag, sizeof (flag)) < 0) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
			"setsockopt(SO_REUSEADDR) failed");
		return (-1);
	}

	/*
	 * Bind to unicast socket used for token send/receives
	 * This has the side effect of binding to the correct interface
	 */
	totemip_totemip_to_sockaddr_convert(bound_to, instance->totem_interface->ip_port, &sockaddr, &addrlen);
	res = bind (sockets->token, (struct sockaddr *)&sockaddr, addrlen);
	if (res == -1) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
			"Unable to bind UDP unicast socket");
		return (-1);
	}

	recvbuf_size = MCAST_SOCKET_BUFFER_SIZE;
	sendbuf_size = MCAST_SOCKET_BUFFER_SIZE;
	/*
	 * Set buffer sizes to avoid overruns
	 */
	res = setsockopt (sockets->mcast_recv, SOL_SOCKET, SO_RCVBUF, &recvbuf_size, optlen);
	if (res == -1) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_debug,
			"Unable to set SO_RCVBUF size on UDP mcast socket");
		return (-1);
	}
	res = setsockopt (sockets->mcast_send, SOL_SOCKET, SO_SNDBUF, &sendbuf_size, optlen);
	if (res == -1) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_debug,
			"Unable to set SO_SNDBUF size on UDP mcast socket");
		return (-1);
	}
	res = setsockopt (sockets->local_mcast_loop[0], SOL_SOCKET, SO_RCVBUF, &recvbuf_size, optlen);
	if (res == -1) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_debug,
			"Unable to set SO_RCVBUF size on UDP local mcast loop socket");
		return (-1);
	}
	res = setsockopt (sockets->local_mcast_loop[1], SOL_SOCKET, SO_SNDBUF, &sendbuf_size, optlen);
	if (res == -1) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_debug,
			"Unable to set SO_SNDBUF size on UDP local mcast loop socket");
		return (-1);
	}

	res = getsockopt (sockets->mcast_recv, SOL_SOCKET, SO_RCVBUF, &recvbuf_size, &optlen);
	if (res == 0) {
		log_printf (instance->totemudp_log_level_debug,
			"Receive multicast socket recv buffer size (%d bytes).\n", recvbuf_size);
	}

	res = getsockopt (sockets->mcast_send, SOL_SOCKET, SO_SNDBUF, &sendbuf_size, &optlen);
	if (res == 0) {
		log_printf (instance->totemudp_log_level_debug,
			"Transmit multicast socket send buffer size (%d bytes).\n", sendbuf_size);
	}

	res = getsockopt (sockets->local_mcast_loop[0], SOL_SOCKET, SO_RCVBUF, &recvbuf_size, &optlen);
	if (res == 0) {
		log_printf (instance->totemudp_log_level_debug,
			"Local receive multicast loop socket recv buffer size (%d bytes).", recvbuf_size);
	}

	res = getsockopt (sockets->local_mcast_loop[1], SOL_SOCKET, SO_SNDBUF, &sendbuf_size, &optlen);
	if (res == 0) {
		log_printf (instance->totemudp_log_level_debug,
			"Local transmit multicast loop socket send buffer size (%d bytes).", sendbuf_size);
	}


	/*
	 * Join group membership on socket
	 */
	totemip_totemip_to_sockaddr_convert(mcast_address, instance->totem_interface->ip_port, &mcast_ss, &addrlen);
	totemip_totemip_to_sockaddr_convert(bound_to, instance->totem_interface->ip_port, &boundto_ss, &addrlen);

	if (instance->totem_config->broadcast_use == 1) {
		unsigned int broadcast = 1;

		if ((setsockopt(sockets->mcast_recv, SOL_SOCKET,
			SO_BROADCAST, &broadcast, sizeof (broadcast))) == -1) {
			LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
				"setting broadcast option failed");
			return (-1);
		}
		if ((setsockopt(sockets->mcast_send, SOL_SOCKET,
			SO_BROADCAST, &broadcast, sizeof (broadcast))) == -1) {
			LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
				"setting broadcast option failed");
			return (-1);
		}
	} else {
		switch (bindnet_address->family) {
			case AF_INET:
			memset(&mreq, 0, sizeof(mreq));
			mreq.imr_multiaddr.s_addr = mcast_sin->sin_addr.s_addr;
			mreq.imr_interface.s_addr = boundto_sin->sin_addr.s_addr;
			res = setsockopt (sockets->mcast_recv, IPPROTO_IP, IP_ADD_MEMBERSHIP,
				&mreq, sizeof (mreq));
			if (res == -1) {
				LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
					"join ipv4 multicast group failed");
				return (-1);
			}
			break;
			case AF_INET6:
			memset(&mreq6, 0, sizeof(mreq6));
			memcpy(&mreq6.ipv6mr_multiaddr, &mcast_sin6->sin6_addr, sizeof(struct in6_addr));
			mreq6.ipv6mr_interface = interface_num;

			res = setsockopt (sockets->mcast_recv, IPPROTO_IPV6, IPV6_JOIN_GROUP,
				&mreq6, sizeof (mreq6));
			if (res == -1) {
				LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
					"join ipv6 multicast group failed");
				return (-1);
			}
			break;
		}
	}

	/*
	 * Turn off multicast loopback
	 */

	flag = 0;
	switch ( bindnet_address->family ) {
		case AF_INET:
		res = setsockopt (sockets->mcast_send, IPPROTO_IP, IP_MULTICAST_LOOP,
			&flag, sizeof (flag));
		break;
		case AF_INET6:
		res = setsockopt (sockets->mcast_send, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
			&flag, sizeof (flag));
	}
	if (res == -1) {
		LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
			"Unable to turn off multicast loopback");
		return (-1);
	}

	/*
	 * Set multicast packets TTL
	 */
	flag = instance->totem_interface->ttl;
	if (bindnet_address->family == AF_INET6) {
		res = setsockopt (sockets->mcast_send, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
			&flag, sizeof (flag));
		if (res == -1) {
			LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
				"set mcast v6 TTL failed");
			return (-1);
		}
	} else {
		res = setsockopt(sockets->mcast_send, IPPROTO_IP, IP_MULTICAST_TTL,
			&flag, sizeof(flag));
		if (res == -1) {
			LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
				"set mcast v4 TTL failed");
			return (-1);
		}
	}

	/*
	 * Bind to a specific interface for multicast send and receive
	 */
	switch ( bindnet_address->family ) {
		case AF_INET:
		if (setsockopt (sockets->mcast_send, IPPROTO_IP, IP_MULTICAST_IF,
			&boundto_sin->sin_addr, sizeof (boundto_sin->sin_addr)) < 0) {
			LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
				"cannot select interface for multicast packets (send)");
			return (-1);
		}
		if (setsockopt (sockets->mcast_recv, IPPROTO_IP, IP_MULTICAST_IF,
			&boundto_sin->sin_addr, sizeof (boundto_sin->sin_addr)) < 0) {
			LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
				"cannot select interface for multicast packets (recv)");
			return (-1);
		}
		break;
		case AF_INET6:
		if (setsockopt (sockets->mcast_send, IPPROTO_IPV6, IPV6_MULTICAST_IF,
			&interface_num, sizeof (interface_num)) < 0) {
			LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
				"cannot select interface for multicast packets (send v6)");
			return (-1);
		}
		if (setsockopt (sockets->mcast_recv, IPPROTO_IPV6, IPV6_MULTICAST_IF,
			&interface_num, sizeof (interface_num)) < 0) {
			LOGSYS_PERROR (errno, instance->totemudp_log_level_warning,
				"cannot select interface for multicast packets (recv v6)");
			return (-1);
		}
		break;
	}

	return 0;
}

static int totemudp_build_sockets (
	struct totemudp_instance *instance,
	struct totem_ip_address *mcast_address,
	struct totem_ip_address *bindnet_address,
	struct totemudp_socket *sockets,
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

	res = totemudp_build_sockets_ip (instance, mcast_address,
		bindnet_address, sockets, bound_to, interface_num);

	/* We only send out of the token socket */
	totemudp_traffic_control_set(instance, sockets->token);
	return res;
}

/*
 * Totem Network interface - also does encryption/decryption
 * depends on poll abstraction, POSIX, IPV4
 */

/*
 * Create an instance
 */
int totemudp_initialize (
	hdb_handle_t poll_handle,
	void **udp_context,
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
	struct totemudp_instance *instance;

	instance = malloc (sizeof (struct totemudp_instance));
	if (instance == NULL) {
		return (-1);
	}

	totemudp_instance_initialize (instance);

	instance->totem_config = totem_config;
	instance->stats = stats;

	/*
	* Configure logging
	*/
	instance->totemudp_log_level_security = 1; //totem_config->totem_logging_configuration.log_level_security;
	instance->totemudp_log_level_error = totem_config->totem_logging_configuration.log_level_error;
	instance->totemudp_log_level_warning = totem_config->totem_logging_configuration.log_level_warning;
	instance->totemudp_log_level_notice = totem_config->totem_logging_configuration.log_level_notice;
	instance->totemudp_log_level_debug = totem_config->totem_logging_configuration.log_level_debug;
	instance->totemudp_subsys_id = totem_config->totem_logging_configuration.log_subsys_id;
	instance->totemudp_log_printf = totem_config->totem_logging_configuration.log_printf;

	/*
	* Initialize random number generator for later use to generate salt
	*/
	memcpy (instance->totemudp_private_key, totem_config->private_key,
		totem_config->private_key_len);

	instance->totemudp_private_key_len = totem_config->private_key_len;

	init_crypto(instance);

	/*
	 * Initialize local variables for totemudp
	 */
	instance->totem_interface = &totem_config->interfaces[interface_no];
	totemip_copy (&instance->mcast_address, &instance->totem_interface->mcast_addr);
	memset (instance->iov_buffer, 0, FRAME_SIZE_MAX);

	/*
	* If threaded send requested, initialize thread group data structure
	*/
	if (totem_config->threads) {
		worker_thread_group_init (
			&instance->worker_thread_group,
			totem_config->threads, 128,
			sizeof (struct work_item),
			sizeof (struct totemudp_mcast_thread_state),
			totemudp_mcast_thread_state_constructor,
			totemudp_mcast_worker_fn);
	}

	instance->totemudp_poll_handle = poll_handle;

	instance->totem_interface->bindnet.nodeid = instance->totem_config->node_id;

	instance->context = context;
	instance->totemudp_deliver_fn = deliver_fn;

	instance->totemudp_iface_change_fn = iface_change_fn;

	instance->totemudp_target_set_completed = target_set_completed;

	totemip_localhost (instance->mcast_address.family, &localhost);
	localhost.nodeid = instance->totem_config->node_id;

	/*
	 * RRP layer isn't ready to receive message because it hasn't
	 * initialized yet.  Add short timer to check the interfaces.
	 */
	poll_timer_add (instance->totemudp_poll_handle,
		100,
		(void *)instance,
		timer_function_netif_check_timeout,
		&instance->timer_netif_check_timeout);

	*udp_context = instance;
	return (0);
}

int totemudp_processor_count_set (
	void *udp_context,
	int processor_count)
{
	struct totemudp_instance *instance = (struct totemudp_instance *)udp_context;
	int res = 0;

	instance->my_memb_entries = processor_count;
	poll_timer_delete (instance->totemudp_poll_handle,
		instance->timer_netif_check_timeout);
	if (processor_count == 1) {
		poll_timer_add (instance->totemudp_poll_handle,
			instance->totem_config->downcheck_timeout,
			(void *)instance,
			timer_function_netif_check_timeout,
			&instance->timer_netif_check_timeout);
	}

	return (res);
}

int totemudp_recv_flush (void *udp_context)
{
	struct totemudp_instance *instance = (struct totemudp_instance *)udp_context;
	struct pollfd ufd;
	int nfds;
	int res = 0;
	int i;
	int sock;

	instance->flushing = 1;

	for (i = 0; i < 2; i++) {
		sock = -1;
		if (i == 0) {
		    sock = instance->totemudp_sockets.mcast_recv;
		}
		if (i == 1) {
		    sock = instance->totemudp_sockets.local_mcast_loop[0];
		}
		assert(sock != -1);

		do {
			ufd.fd = sock;
			ufd.events = POLLIN;
			nfds = poll (&ufd, 1, 0);
			if (nfds == 1 && ufd.revents & POLLIN) {
				net_deliver_fn (0, sock, ufd.revents, instance);
			}
		} while (nfds == 1);
	}

	instance->flushing = 0;

	return (res);
}

int totemudp_send_flush (void *udp_context)
{
	struct totemudp_instance *instance = (struct totemudp_instance *)udp_context;
	int res = 0;

	worker_thread_group_wait (&instance->worker_thread_group);

	return (res);
}

int totemudp_token_send (
	void *udp_context,
	const void *msg,
	unsigned int msg_len)
{
	struct totemudp_instance *instance = (struct totemudp_instance *)udp_context;
	int res = 0;

	ucast_sendmsg (instance, &instance->token_target, msg, msg_len);

	return (res);
}
int totemudp_mcast_flush_send (
	void *udp_context,
	const void *msg,
	unsigned int msg_len)
{
	struct totemudp_instance *instance = (struct totemudp_instance *)udp_context;
	int res = 0;

	mcast_sendmsg (instance, msg, msg_len);

	return (res);
}

int totemudp_mcast_noflush_send (
	void *udp_context,
	const void *msg,
	unsigned int msg_len)
{
	struct totemudp_instance *instance = (struct totemudp_instance *)udp_context;
	struct work_item work_item;
	int res = 0;

	if (instance->totem_config->threads) {
		work_item.msg = msg;
		work_item.msg_len = msg_len;
		work_item.instance = instance;

		worker_thread_group_work_add (&instance->worker_thread_group,
			&work_item);
	} else {
		mcast_sendmsg (instance, msg, msg_len);
	}

	return (res);
}

extern int totemudp_iface_check (void *udp_context)
{
	struct totemudp_instance *instance = (struct totemudp_instance *)udp_context;
	int res = 0;

	timer_function_netif_check_timeout (instance);

	return (res);
}

extern void totemudp_net_mtu_adjust (void *udp_context, struct totem_config *totem_config)
{
	assert(totem_config->interface_count > 0);

	if (totem_config->secauth == 1) {
		totem_config->net_mtu -= sizeof (struct security_header) +
			totemip_udpip_header_size(totem_config->interfaces[0].bindnet.family);
	} else {
		totem_config->net_mtu -= totemip_udpip_header_size(totem_config->interfaces[0].bindnet.family);
	}
}

const char *totemudp_iface_print (void *udp_context)  {
	struct totemudp_instance *instance = (struct totemudp_instance *)udp_context;
	const char *ret_char;

	ret_char = totemip_print (&instance->my_id);

	return (ret_char);
}

int totemudp_iface_get (
	void *udp_context,
	struct totem_ip_address *addr)
{
	struct totemudp_instance *instance = (struct totemudp_instance *)udp_context;
	int res = 0;

	memcpy (addr, &instance->my_id, sizeof (struct totem_ip_address));

	return (res);
}

int totemudp_token_target_set (
	void *udp_context,
	const struct totem_ip_address *token_target)
{
	struct totemudp_instance *instance = (struct totemudp_instance *)udp_context;
	int res = 0;

	memcpy (&instance->token_target, token_target,
		sizeof (struct totem_ip_address));

	instance->totemudp_target_set_completed (instance->context);

	return (res);
}

extern int totemudp_recv_mcast_empty (
	void *udp_context)
{
	struct totemudp_instance *instance = (struct totemudp_instance *)udp_context;
	unsigned int res;
	struct sockaddr_storage system_from;
	struct msghdr msg_recv;
	struct pollfd ufd;
	int nfds;
	int msg_processed = 0;
	int i;
	int sock;

	/*
	 * Receive datagram
	 */
	msg_recv.msg_name = &system_from;
	msg_recv.msg_namelen = sizeof (struct sockaddr_storage);
	msg_recv.msg_iov = &instance->totemudp_iov_recv_flush;
	msg_recv.msg_iovlen = 1;
#if !defined(COROSYNC_SOLARIS)
	msg_recv.msg_control = 0;
	msg_recv.msg_controllen = 0;
	msg_recv.msg_flags = 0;
#else
	msg_recv.msg_accrights = NULL;
	msg_recv.msg_accrightslen = 0;
#endif

	for (i = 0; i < 2; i++) {
		sock = -1;
		if (i == 0) {
		    sock = instance->totemudp_sockets.mcast_recv;
		}
		if (i == 1) {
		    sock = instance->totemudp_sockets.local_mcast_loop[0];
		}
		assert(sock != -1);

		do {
			ufd.fd = sock;
			ufd.events = POLLIN;
			nfds = poll (&ufd, 1, 0);
			if (nfds == 1 && ufd.revents & POLLIN) {
				res = recvmsg (sock, &msg_recv, MSG_NOSIGNAL | MSG_DONTWAIT);
				if (res != -1) {
					msg_processed = 1;
				} else {
					msg_processed = -1;
				}
			}
		} while (nfds == 1);
	}

	return (msg_processed);
}

