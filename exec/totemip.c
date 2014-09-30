/*
 * Copyright (c) 2005-2011 Red Hat, Inc.
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

#include <config.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <net/if.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <ifaddrs.h>

#include <corosync/totem/totemip.h>
#include <corosync/swab.h>

#define LOCALHOST_IPV4 "127.0.0.1"
#define LOCALHOST_IPV6 "::1"

#define NETLINK_BUFSIZE 16384

#ifdef SO_NOSIGPIPE
void totemip_nosigpipe(int s)
{
	int on = 1;
	setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (void *)&on, sizeof(on));
}
#endif

/* Compare two addresses */
int totemip_equal(const struct totem_ip_address *addr1,
		  const struct totem_ip_address *addr2)
{
	int addrlen = 0;

	if (addr1->family != addr2->family)
		return 0;

	if (addr1->family == AF_INET) {
		addrlen = sizeof(struct in_addr);
	}
	if (addr1->family == AF_INET6) {
		addrlen = sizeof(struct in6_addr);
	}
	assert(addrlen);

	if (memcmp(addr1->addr, addr2->addr, addrlen) == 0)
		return 1;
	else
		return 0;

}

/* Copy a totem_ip_address */
void totemip_copy(struct totem_ip_address *addr1,
		  const struct totem_ip_address *addr2)
{
	memcpy(addr1, addr2, sizeof(struct totem_ip_address));
}

void totemip_copy_endian_convert(struct totem_ip_address *addr1,
				 const struct totem_ip_address *addr2)
{
	addr1->nodeid = swab32(addr2->nodeid);
	addr1->family = swab16(addr2->family);
	memcpy(addr1->addr, addr2->addr, TOTEMIP_ADDRLEN);
}

/*
 * Multicast address range is 224.0.0.0 to 239.255.255.255 this
 * translates to the first 4 bits == 1110 (0xE).
 * http://en.wikipedia.org/wiki/Multicast_address
 */
int32_t totemip_is_mcast(struct totem_ip_address *ip_addr)
{
	uint32_t addr = 0;

	memcpy (&addr, ip_addr->addr, sizeof (uint32_t));

	if (ip_addr->family == AF_INET) {
		addr = ntohl(addr);
		if ((addr >> 28) != 0xE) {
			return -1;
		}
	}
	return 0;
}

/* For sorting etc. params are void * for qsort's benefit */
int totemip_compare(const void *a, const void *b)
{
	int i;
	const struct totem_ip_address *totemip_a = (const struct totem_ip_address *)a;
	const struct totem_ip_address *totemip_b = (const struct totem_ip_address *)b;
	struct in_addr ipv4_a1;
	struct in_addr ipv4_a2;
	struct in6_addr ipv6_a1;
	struct in6_addr ipv6_a2;
	unsigned short family;

	/*
	 * Use memcpy to align since totem_ip_address is unaligned on various archs
	 */
	memcpy (&family, &totemip_a->family, sizeof (unsigned short));

	if (family == AF_INET) {
		memcpy (&ipv4_a1, totemip_a->addr, sizeof (struct in_addr));
		memcpy (&ipv4_a2, totemip_b->addr, sizeof (struct in_addr));
		if (ipv4_a1.s_addr == ipv4_a2.s_addr) {
			return (0);
		}
		if (htonl(ipv4_a1.s_addr) < htonl(ipv4_a2.s_addr)) {
			return -1;
		} else {
			return +1;
		}
	} else
	if (family == AF_INET6) {
		/*
		 * We can only compare 8 bits at time for portability reasons
		 */
		memcpy (&ipv6_a1, totemip_a->addr, sizeof (struct in6_addr));
		memcpy (&ipv6_a2, totemip_b->addr, sizeof (struct in6_addr));
		for (i = 0; i < 16; i++) {
			int res = ipv6_a1.s6_addr[i] -
				ipv6_a2.s6_addr[i];
			if (res) {
				return res;
			}
		}
		return 0;
	} else {
		/*
		 * Family not set, should be!
	 	 */
		assert (0);
	}
	return 0;
}

/* Build a localhost totem_ip_address */
int totemip_localhost(int family, struct totem_ip_address *localhost)
{
	const char *addr_text;

	memset (localhost, 0, sizeof (struct totem_ip_address));

	if (family == AF_INET) {
		addr_text = LOCALHOST_IPV4;
		if (inet_pton(family, addr_text, (char *)&localhost->nodeid) <= 0) {
			return -1;
		}
	} else {
		addr_text = LOCALHOST_IPV6;
	}

	if (inet_pton(family, addr_text, (char *)localhost->addr) <= 0)
		return -1;

	localhost->family = family;

	return 0;
}

int totemip_localhost_check(const struct totem_ip_address *addr)
{
	struct totem_ip_address localhost;

	if (totemip_localhost(addr->family, &localhost))
		return 0;
	return totemip_equal(addr, &localhost);
}

const char *totemip_print(const struct totem_ip_address *addr)
{
	static char buf[INET6_ADDRSTRLEN];

	return (inet_ntop(addr->family, addr->addr, buf, sizeof(buf)));
}

/* Make a totem_ip_address into a usable sockaddr_storage */
int totemip_totemip_to_sockaddr_convert(struct totem_ip_address *ip_addr,
					uint16_t port, struct sockaddr_storage *saddr, int *addrlen)
{
	int ret = -1;

	if (ip_addr->family == AF_INET) {
		struct sockaddr_in *sin = (struct sockaddr_in *)saddr;

		memset(sin, 0, sizeof(struct sockaddr_in));
#ifdef HAVE_SOCK_SIN_LEN
		sin->sin_len = sizeof(struct sockaddr_in);
#endif
		sin->sin_family = ip_addr->family;
		sin->sin_port = ntohs(port);
		memcpy(&sin->sin_addr, ip_addr->addr, sizeof(struct in_addr));
		*addrlen = sizeof(struct sockaddr_in);
		ret = 0;
	}

	if (ip_addr->family == AF_INET6) {
		struct sockaddr_in6 *sin = (struct sockaddr_in6 *)saddr;

		memset(sin, 0, sizeof(struct sockaddr_in6));
#ifdef HAVE_SOCK_SIN6_LEN
		sin->sin6_len = sizeof(struct sockaddr_in6);
#endif
		sin->sin6_family = ip_addr->family;
		sin->sin6_port = ntohs(port);
		sin->sin6_scope_id = 2;
		memcpy(&sin->sin6_addr, ip_addr->addr, sizeof(struct in6_addr));

		*addrlen = sizeof(struct sockaddr_in6);
		ret = 0;
	}

	return ret;
}

/* Converts an address string string into a totem_ip_address.
   family can be AF_INET, AF_INET6 or 0 ("for "don't care")
*/
int totemip_parse(struct totem_ip_address *totemip, const char *addr, int family)
{
	struct addrinfo *ainfo;
	struct addrinfo ahints;
	struct sockaddr_in *sa;
	struct sockaddr_in6 *sa6;
	int ret;

	memset(&ahints, 0, sizeof(ahints));
	ahints.ai_socktype = SOCK_DGRAM;
	ahints.ai_protocol = IPPROTO_UDP;
	ahints.ai_family   = family;

	/* Lookup the nodename address */
	ret = getaddrinfo(addr, NULL, &ahints, &ainfo);
	if (ret)
		return -1;

	sa = (struct sockaddr_in *)ainfo->ai_addr;
	sa6 = (struct sockaddr_in6 *)ainfo->ai_addr;
	totemip->family = ainfo->ai_family;

	if (ainfo->ai_family == AF_INET)
		memcpy(totemip->addr, &sa->sin_addr, sizeof(struct in_addr));
	else
		memcpy(totemip->addr, &sa6->sin6_addr, sizeof(struct in6_addr));

	freeaddrinfo(ainfo);
	return 0;
}

/* Make a sockaddr_* into a totem_ip_address */
int totemip_sockaddr_to_totemip_convert(const struct sockaddr_storage *saddr,
					struct totem_ip_address *ip_addr)
{
	int ret = -1;

	ip_addr->family = saddr->ss_family;
	ip_addr->nodeid = 0;

	if (saddr->ss_family == AF_INET) {
		const struct sockaddr_in *sin = (const struct sockaddr_in *)saddr;

		memcpy(ip_addr->addr, &sin->sin_addr, sizeof(struct in_addr));
		ret = 0;
	}

	if (saddr->ss_family == AF_INET6) {
		const struct sockaddr_in6 *sin
		  = (const struct sockaddr_in6 *)saddr;

		memcpy(ip_addr->addr, &sin->sin6_addr, sizeof(struct in6_addr));

		ret = 0;
	}
	return ret;
}

int totemip_getifaddrs(struct list_head *addrs)
{
	struct ifaddrs *ifap, *ifa;
	struct totem_ip_if_address *if_addr;

	if (getifaddrs(&ifap) != 0)
		return (-1);

	list_init(addrs);

	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL || ifa->ifa_netmask == NULL)
			continue ;

		if ((ifa->ifa_addr->sa_family != AF_INET && ifa->ifa_addr->sa_family != AF_INET6) ||
		    (ifa->ifa_netmask->sa_family != AF_INET && ifa->ifa_netmask->sa_family != AF_INET6 &&
		     ifa->ifa_netmask->sa_family != 0))
			continue ;

		if (ifa->ifa_netmask->sa_family == 0) {
			ifa->ifa_netmask->sa_family = ifa->ifa_addr->sa_family;
		}

		if_addr = malloc(sizeof(struct totem_ip_if_address));
		if (if_addr == NULL) {
			goto error_free_ifaddrs;
		}

		list_init(&if_addr->list);

		memset(if_addr, 0, sizeof(struct totem_ip_if_address));

		if_addr->interface_up = ifa->ifa_flags & IFF_UP;
		if_addr->interface_num = if_nametoindex(ifa->ifa_name);
		if_addr->name = strdup(ifa->ifa_name);
		if (if_addr->name == NULL) {
			goto error_free_addr;
		}

		if (totemip_sockaddr_to_totemip_convert((const struct sockaddr_storage *)ifa->ifa_addr,
		    &if_addr->ip_addr) == -1) {
			goto error_free_addr_name;
		}

		if (totemip_sockaddr_to_totemip_convert((const struct sockaddr_storage *)ifa->ifa_netmask,
		    &if_addr->mask_addr) == -1) {
			goto error_free_addr_name;
		}

		list_add_tail(&if_addr->list, addrs);
	}

	freeifaddrs(ifap);

	return (0);

error_free_addr_name:
	free(if_addr->name);

error_free_addr:
	free(if_addr);

error_free_ifaddrs:
	totemip_freeifaddrs(addrs);
	freeifaddrs(ifap);
	return (-1);
}

void totemip_freeifaddrs(struct list_head *addrs)
{
	struct totem_ip_if_address *if_addr;
	struct list_head *list;

	for (list = addrs->next; list != addrs;) {
		if_addr = list_entry(list, struct totem_ip_if_address, list);
		list = list->next;

		free(if_addr->name);
		list_del(&if_addr->list);
	        free(if_addr);
	}
	list_init(addrs);
}

int totemip_iface_check(struct totem_ip_address *bindnet,
			struct totem_ip_address *boundto,
			int *interface_up,
			int *interface_num,
			int mask_high_bit)
{
	struct list_head addrs;
	struct list_head *list;
	struct totem_ip_if_address *if_addr;
	struct totem_ip_address bn_netaddr, if_netaddr;
	socklen_t addr_len;
	socklen_t si;
	int res = -1;
	int exact_match_found = 0;
	int net_match_found = 0;

	*interface_up = 0;
	*interface_num = 0;

	if (totemip_getifaddrs(&addrs) == -1) {
		return (-1);
	}

	for (list = addrs.next; list != &addrs; list = list->next) {
		if_addr = list_entry(list, struct totem_ip_if_address, list);

		if (bindnet->family != if_addr->ip_addr.family)
			continue ;

		addr_len = 0;

		switch (bindnet->family) {
		case AF_INET:
			addr_len = sizeof(struct in_addr);
			break;
		case AF_INET6:
			addr_len = sizeof(struct in6_addr);
			break;
		}

		if (addr_len == 0)
			continue ;

		totemip_copy(&bn_netaddr, bindnet);
		totemip_copy(&if_netaddr, &if_addr->ip_addr);

		if (totemip_equal(&bn_netaddr, &if_netaddr)) {
			exact_match_found = 1;
		}

		for (si = 0; si < addr_len; si++) {
			bn_netaddr.addr[si] = bn_netaddr.addr[si] & if_addr->mask_addr.addr[si];
			if_netaddr.addr[si] = if_netaddr.addr[si] & if_addr->mask_addr.addr[si];
		}

		if (exact_match_found || (!net_match_found && totemip_equal(&bn_netaddr, &if_netaddr))) {
			totemip_copy(boundto, &if_addr->ip_addr);
			boundto->nodeid = bindnet->nodeid;
			*interface_up = if_addr->interface_up;
			*interface_num = if_addr->interface_num;

			if (boundto->family == AF_INET && boundto->nodeid == 0) {
				unsigned int nodeid = 0;
				memcpy (&nodeid, boundto->addr, sizeof (int));
#if __BYTE_ORDER == __LITTLE_ENDIAN
                                nodeid = swab32 (nodeid);
#endif
				if (mask_high_bit) {
					nodeid &= 0x7FFFFFFF;
				}
				boundto->nodeid = nodeid;
			}

			net_match_found = 1;
			res = 0;

			if (exact_match_found) {
				goto finished;
			}
		}
	}

finished:
	totemip_freeifaddrs(&addrs);
	return (res);
}

#define TOTEMIP_UDP_HEADER_SIZE		8
#define TOTEMIP_IPV4_HEADER_SIZE	20
#define TOTEMIP_IPV6_HEADER_SIZE	40

size_t totemip_udpip_header_size(int family)
{
	size_t header_size;

	header_size = 0;

	switch (family) {
	case AF_INET:
		header_size = TOTEMIP_UDP_HEADER_SIZE + TOTEMIP_IPV4_HEADER_SIZE;
		break;
	case AF_INET6:
		header_size = TOTEMIP_UDP_HEADER_SIZE + TOTEMIP_IPV6_HEADER_SIZE;
		break;
	}

	return (header_size);
}
