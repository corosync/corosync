/*
 * Copyright (c) 2005 Red Hat Inc
 *
 * All rights reserved.
 *
 * Author: Patrick Caulfield (pcaulfie@redhat.com)
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

/* IPv4/6 abstraction */

#ifndef TOTEMIP_H_DEFINED
#define TOTEMIP_H_DEFINED

#define TOTEMIP_ADDRLEN (sizeof(struct in6_addr))

/* These are the things that get passed around */
struct totem_ip_address
{
	unsigned int   nodeid;
	unsigned short family;
	unsigned char  addr[TOTEMIP_ADDRLEN];
} __attribute__((packed));


extern int totemip_equal(struct totem_ip_address *addr1, struct totem_ip_address *addr2);
extern int totemip_compare(const void *a, const void *b);
extern void totemip_copy(struct totem_ip_address *addr1, struct totem_ip_address *addr2);
int totemip_localhost(int family, struct totem_ip_address *localhost);
extern int totemip_localhost_check(struct totem_ip_address *addr);
extern const char *totemip_print(struct totem_ip_address *addr);
extern int totemip_sockaddr_to_totemip_convert(struct sockaddr_storage *saddr, struct totem_ip_address *ip_addr);
extern int totemip_totemip_to_sockaddr_convert(struct totem_ip_address *ip_addr,
					       uint16_t port, struct sockaddr_storage *saddr, int *addrlen);
extern int totemip_parse(struct totem_ip_address *totemip, char *addr);
extern int totemip_iface_check(struct totem_ip_address *bindnet, struct totem_ip_address *boundto, int *interface_up, int *interface_num);

/* These two simulate a zero in_addr by clearing the family field */
static inline void totemip_zero_set(struct totem_ip_address *addr)
{
	addr->family = 0;
}
static inline int totemip_zero_check(struct totem_ip_address *addr)
{
	return (addr->family == 0);
}

#endif
