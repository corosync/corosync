/*
 * Copyright (c) 2005-2007, 2009 Red Hat, Inc.
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
#include <arpa/inet.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#if defined(COROSYNC_SOLARIS)
#include <net/if.h>
#include <sys/sockio.h>
#endif
#if defined(COROSYNC_BSD) || defined(COROSYNC_DARWIN)
#include <sys/sockio.h>
#include <net/if.h>
#ifdef HAVE_NET_IF_VAR_H
#include <net/if_var.h>
#endif
#include <netinet/in_var.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#endif
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>

#if defined(COROSYNC_LINUX)
#include <net/if.h>
#include <asm/types.h>
#include <linux/rtnetlink.h>
#endif

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
#if defined(COROSYNC_BSD) || defined(COROSYNC_DARWIN)
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
#if defined(COROSYNC_BSD) || defined(COROSYNC_DARWIN)
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

/*
 * On Solaris, man if_tcp describes this method
 */
#if defined(COROSYNC_SOLARIS)
int totemip_iface_check(struct totem_ip_address *bindnet,
	struct totem_ip_address *boundto,
	int *interface_up,
	int *interface_num,
	int mask_high_bit)
{
	struct sockaddr_storage bindnet_ss;
	struct sockaddr_in *bindnet_sin = (struct sockaddr_in *)&bindnet_ss;
        struct sockaddr_in *sockaddr_in;
        int id_fd;
        struct lifconf lifconf;
	struct lifreq *lifreq;
        int numreqs = 0;
        int i;
        in_addr_t mask_addr;
	int res = -1;
	int addrlen;

	totemip_totemip_to_sockaddr_convert (bindnet,
		0, &bindnet_ss, &addrlen);

	*interface_up = 0;
	id_fd = socket (AF_INET, SOCK_STREAM, 0);
	lifconf.lifc_family = AF_UNSPEC;
	lifconf.lifc_flags = 0;
	lifconf.lifc_buf = NULL;
	lifconf.lifc_len = 0;
	do {
		numreqs += 32;
		lifconf.lifc_len = sizeof (struct lifreq) * numreqs;
		lifconf.lifc_buf = (void *)realloc(lifconf.lifc_buf, lifconf.lifc_len);
		res = ioctl (id_fd, SIOCGLIFCONF, &lifconf);
		if (res < 0) {
			close (id_fd);
			return -1;
		}
	} while (lifconf.lifc_len == sizeof (struct lifconf) * numreqs);
	res = -1;

	lifreq = (struct lifreq *)lifconf.lifc_buf;
	/*
	* Find interface address to bind to
	*/
	for (i = 0; i < lifconf.lifc_len / sizeof (struct lifreq); i++) {
		sockaddr_in = (struct sockaddr_in *)&lifreq[i].lifr_addr;
		mask_addr = inet_addr ("255.255.255.0");

		if ((sockaddr_in->sin_family == AF_INET) &&
			(sockaddr_in->sin_addr.s_addr & mask_addr) ==
			(bindnet_sin->sin_addr.s_addr & mask_addr)) {

			res = i;

			/*
			 * Setup boundto output
			 */
			totemip_sockaddr_to_totemip_convert((struct sockaddr_storage *)sockaddr_in, boundto);
			boundto->nodeid = sockaddr_in->sin_addr.s_addr;
#if __BYTE_ORDER == __BIG_ENDIAN
			boundto->nodeid = swab32 (boundto->nodeid);
#endif

			if (ioctl(id_fd, SIOCGLIFFLAGS, &lifreq[i]) < 0) {
				printf ("couldn't do ioctl\n");
			}

			*interface_up = lifreq[i].lifr_flags & IFF_UP;

			if (ioctl(id_fd, SIOCGLIFINDEX, &lifreq[i]) < 0) {
				printf ("couldn't do ioctl\n");
			}
			*interface_num = lifreq[i].lifr_index;


			break;
		}
	}
	free (lifconf.lifc_buf);
	close (id_fd);

	return (res);
}
#endif

#if defined(COROSYNC_BSD) || defined(COROSYNC_DARWIN)
int totemip_iface_check(struct totem_ip_address *bindnet,
	struct totem_ip_address *boundto,
	int *interface_up,
	int *interface_num,
	int mask_high_bit)
{
#define NEXT_IFR(a)	((struct ifreq *)((u_char *)&(a)->ifr_addr +\
	((a)->ifr_addr.sa_len ? (a)->ifr_addr.sa_len : sizeof((a)->ifr_addr))))

	struct sockaddr_in *intf_addr_mask;
	struct sockaddr_storage bindnet_ss;
	struct sockaddr_in *intf_addr_sin;
	struct sockaddr_in *bindnet_sin = (struct sockaddr_in *)&bindnet_ss;
	struct ifaddrs *ifap, *ifa;
	int res = -1;
	int addrlen;

	*interface_up = 0;
	*interface_num = 0;

	totemip_totemip_to_sockaddr_convert(bindnet,
		0, &bindnet_ss, &addrlen);

	if (getifaddrs(&ifap) != 0)
		return -1;

	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		intf_addr_sin	= (struct sockaddr_in *)ifa->ifa_addr;
		intf_addr_mask	= (struct sockaddr_in *)ifa->ifa_netmask;

		if (intf_addr_sin->sin_family != AF_INET)
			continue;

		if ( bindnet_sin->sin_family == AF_INET &&
			 (intf_addr_sin->sin_addr.s_addr & intf_addr_mask->sin_addr.s_addr) ==
			 (bindnet_sin->sin_addr.s_addr & intf_addr_mask->sin_addr.s_addr)) {

			totemip_copy(boundto, bindnet);
			memcpy(boundto->addr, &intf_addr_sin->sin_addr, sizeof(intf_addr_sin->sin_addr));

			/* Get interface infos
			 */
			*interface_up = ifa->ifa_flags & IFF_UP;
			*interface_num = if_nametoindex(ifa->ifa_name);

			/*
			 * Handle case, when nodeid is set to 0 or not set.
			 */
			if (bindnet->family == AF_INET && bindnet->nodeid == 0) {
				unsigned int nodeid = 0;
				memcpy (&nodeid, boundto->addr, sizeof (int));
#if _BYTE_ORDER == _BIG_ENDIAN
				nodeid = swab32 (nodeid);
#endif
				/*
				 * Mask 32nd bit off to workaround bugs in other peoples code
				 * (if configuration requests it).
				 */
				if (mask_high_bit) {
					nodeid &= 0x7FFFFFFF;
				}
				boundto->nodeid = nodeid;
			}
			res = 0;
			break; /* for */
		}
	}

	freeifaddrs(ifap);

	return (res);
}
#elif defined(COROSYNC_LINUX)

static void parse_rtattr(struct rtattr *tb[], int max, struct rtattr *rta, int len)
{
        while (RTA_OK(rta, len)) {
                if (rta->rta_type <= max)
                        tb[rta->rta_type] = rta;
                rta = RTA_NEXT(rta,len);
        }
}

int totemip_iface_check(struct totem_ip_address *bindnet,
			struct totem_ip_address *boundto,
			int *interface_up,
			int *interface_num,
			int mask_high_bit)
{
	int fd;
	int res = -1;
	struct {
                struct nlmsghdr nlh;
                struct rtgenmsg g;
        } req;
        struct sockaddr_nl nladdr;
	struct totem_ip_address ipaddr;
	static char rcvbuf[NETLINK_BUFSIZE];
	int exact_match_found = 0;
	int net_match_found = 0;

	*interface_up = 0;
	*interface_num = 0;
	memset(&ipaddr, 0, sizeof(ipaddr));

	/* Make sure we preserve these */
	ipaddr.family = bindnet->family;
	ipaddr.nodeid = bindnet->nodeid;

	/* Ask netlink for a list of interface addresses */
	fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (fd <0)
		return -1;

        if (setsockopt(fd,SOL_SOCKET,SO_RCVBUF,&rcvbuf,sizeof(rcvbuf)) == -1) {
		close(fd);
		return -1;
	}

        memset(&nladdr, 0, sizeof(nladdr));
        nladdr.nl_family = AF_NETLINK;

        memset(&req, 0, sizeof(req));
        req.nlh.nlmsg_len = sizeof(req);
        req.nlh.nlmsg_type = RTM_GETADDR;
        req.nlh.nlmsg_flags = NLM_F_ROOT|NLM_F_MATCH|NLM_F_REQUEST;
        req.nlh.nlmsg_pid = 0;
        req.nlh.nlmsg_seq = 1;
        req.g.rtgen_family = bindnet->family;

        if (sendto(fd, (void *)&req, sizeof(req), 0,
		   (struct sockaddr*)&nladdr, sizeof(nladdr)) < 0)  {
		close(fd);
		return -1;
	}

	/* Look through the return buffer for our address */
	while (1)
	{
		int status;
		struct nlmsghdr *h;
		struct iovec iov = { rcvbuf, sizeof(rcvbuf) };
		struct msghdr msg = {
			(void*)&nladdr, sizeof(nladdr),
			&iov,   1,
			NULL,   0,
			0
		};

		status = recvmsg(fd, &msg, 0);
		if (!status) {
			close(fd);
			return -1;
		}

		h = (struct nlmsghdr *)rcvbuf;
		if (h->nlmsg_type == NLMSG_DONE)
			break;

		if (h->nlmsg_type == NLMSG_ERROR) {
			close(fd);
			return -1;
		}

		while (NLMSG_OK(h, status)) {
			if (h->nlmsg_type == RTM_NEWADDR) {
				struct ifaddrmsg *ifa = NLMSG_DATA(h);
				struct rtattr *tb[IFA_MAX+1];
				int len = h->nlmsg_len - NLMSG_LENGTH(sizeof(*ifa));
				int found_if = 0;

				memset(tb, 0, sizeof(tb));

				parse_rtattr(tb, IFA_MAX, IFA_RTA(ifa), len);

				if (ifa->ifa_family == AF_INET6 && tb[IFA_ADDRESS]) {
					memcpy(ipaddr.addr, RTA_DATA(tb[IFA_ADDRESS]), TOTEMIP_ADDRLEN);
					if (totemip_equal(&ipaddr, bindnet)) {
						found_if = 1;
						exact_match_found = 1;
					}
				}
				if (tb[IFA_LOCAL]) {
					memcpy(ipaddr.addr, RTA_DATA(tb[IFA_LOCAL]), TOTEMIP_ADDRLEN);
					if (totemip_equal(&ipaddr, bindnet)) {
						found_if = 1;
						exact_match_found = 1;
					}
				}

				/* If the address we have is an IPv4 network address, then
				   substitute the actual IP address of this interface */
				if (!found_if && !net_match_found && tb[IFA_LOCAL] && ifa->ifa_family == AF_INET) {
					uint32_t network;
					uint32_t addr;
					uint32_t netmask = htonl(~((1<<(32-ifa->ifa_prefixlen))-1));

					memcpy(&network, RTA_DATA(tb[IFA_LOCAL]), sizeof(uint32_t));
					memcpy(&addr, bindnet->addr, sizeof(uint32_t));

					if ((addr) == (network & netmask)) {
						memcpy(ipaddr.addr, RTA_DATA(tb[IFA_ADDRESS]), TOTEMIP_ADDRLEN);
						found_if = 1;
					}
				}

				if (found_if) {
					/*
					 * Mask 32nd bit off to workaround bugs in other peoples code
					 * (if configuration requests it).
					 */
					if (ipaddr.family == AF_INET && ipaddr.nodeid == 0) {
						unsigned int nodeid = 0;
						memcpy (&nodeid, ipaddr.addr, sizeof (int));
#if __BYTE_ORDER == __BIG_ENDIAN
                                                nodeid = swab32 (nodeid);
#endif
						if (mask_high_bit) {
							nodeid &= 0x7FFFFFFF;
						}
						ipaddr.nodeid = nodeid;
					}
					totemip_copy (boundto, &ipaddr);
					*interface_num = ifa->ifa_index;

					net_match_found = 1;

					if (exact_match_found) {
						goto finished;
					}
				}
			}
			h = NLMSG_NEXT(h, status);
		}
	}
finished:
	close(fd);
	if (net_match_found) {
		/*
		 * Found it - check I/F is UP
		 */
		struct ifreq ifr;
		int ioctl_fd; /* Can't do ioctls on netlink FDs */

		ioctl_fd = socket(AF_INET, SOCK_STREAM, 0);
		if (ioctl_fd < 0) {
			return -1;
		}
		memset(&ifr, 0, sizeof(ifr));
		ifr.ifr_ifindex = *interface_num;

		/* SIOCGIFFLAGS needs an interface name */
		res = ioctl(ioctl_fd, SIOCGIFNAME, &ifr);
		res = ioctl(ioctl_fd, SIOCGIFFLAGS, &ifr);
		close(ioctl_fd);
		if (res) {
			return (-1);
		}

		if (ifr.ifr_flags & IFF_UP)
			*interface_up = 1;

		res = 0;
	} else {
		res = -1;
	}
	return res;
}
#endif /* COROSYNC_LINUX */

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
