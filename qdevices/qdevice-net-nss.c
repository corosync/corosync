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

#include <sslerr.h>
#include <secerr.h>

#include "qdevice-log.h"
#include "qdevice-net-nss.h"
#include "qdevice-net-instance.h"
#include "qnet-config.h"

SECStatus
qdevice_net_nss_bad_cert_hook(void *arg, PRFileDesc *fd) {
	if (PR_GetError() == SEC_ERROR_EXPIRED_CERTIFICATE ||
	    PR_GetError() == SEC_ERROR_EXPIRED_ISSUER_CERTIFICATE ||
	    PR_GetError() == SEC_ERROR_CRL_EXPIRED ||
	    PR_GetError() == SEC_ERROR_KRL_EXPIRED ||
	    PR_GetError() == SSL_ERROR_EXPIRED_CERT_ALERT) {
		qdevice_log(LOG_WARNING, "Server certificate is expired.");

		return (SECSuccess);
	}

	qdevice_log_nss(LOG_ERR, "Server certificate verification failure.");

	return (SECFailure);
}

SECStatus
qdevice_net_nss_get_client_auth_data(void *arg, PRFileDesc *sock, struct CERTDistNamesStr *caNames,
    struct CERTCertificateStr **pRetCert, struct SECKEYPrivateKeyStr **pRetKey)
{
	struct qdevice_net_instance *instance;

	qdevice_log(LOG_DEBUG, "Sending client auth data.");

	instance = (struct qdevice_net_instance *)arg;

	instance->tls_client_cert_sent = 1;

	return (NSS_GetClientAuthData((void *)instance->advanced_settings->net_nss_client_cert_nickname,
	    sock, caNames, pRetCert, pRetKey));
}
