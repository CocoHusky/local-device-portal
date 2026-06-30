#include "portal_mdns.h"

#include "portal_state.h"
#include "wifi_manager.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

LOG_MODULE_REGISTER(portal_mdns, LOG_LEVEL_INF);

#define MDNS_PORT 5353
#define MDNS_MCAST 0xE00000FBu

static int ascii_lower(int c)
{
	return (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;
}

static bool name_equal_ci(const char *a, const char *b)
{
	while (*a && *b) {
		if (ascii_lower((unsigned char)*a) != ascii_lower((unsigned char)*b)) {
			return false;
		}
		a++;
		b++;
	}
	return *a == '\0' && *b == '\0';
}

static bool parse_query_name(const uint8_t *pkt, int len, int *off,
			     char *name, size_t name_len)
{
	int p = *off;
	size_t n = 0;

	if (name_len == 0) {
		return false;
	}

	while (p < len) {
		uint8_t lab_len = pkt[p++];

		if (lab_len == 0) {
			break;
		}
		if ((lab_len & 0xc0) != 0 || p + lab_len > len) {
			return false;
		}
		if (n != 0 && n + 1 < name_len) {
			name[n++] = '.';
		}
		for (uint8_t i = 0; i < lab_len && n + 1 < name_len; i++) {
			name[n++] = pkt[p++];
		}
	}

	if (p + 4 > len) {
		return false;
	}

	name[n] = '\0';
	*off = p;
	return true;
}

static uint16_t get_be16(const uint8_t *p)
{
	return ((uint16_t)p[0] << 8) | p[1];
}

static void put_be16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)v;
}

static void put_be32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

static int make_answer(const uint8_t *query, int qlen, uint8_t *reply,
		       size_t reply_len, const struct in_addr *addr)
{
	int off = 12;
	char qname[80];
	char target[80];

	if (qlen < 16 || reply_len < (size_t)qlen + 16) {
		return -EINVAL;
	}
	if (get_be16(&query[4]) == 0) {
		return -EINVAL;
	}
	if (!parse_query_name(query, qlen, &off, qname, sizeof(qname))) {
		return -EINVAL;
	}

	uint16_t qtype = get_be16(&query[off]);
	if (qtype != 1 && qtype != 255) {
		return -ENOENT;
	}
	off += 4;

	snprintk(target, sizeof(target), "%s.local", portal_state_host());
	if (!name_equal_ci(qname, target)) {
		return -ENOENT;
	}

	memcpy(reply, query, off);
	reply[2] = 0x84;
	reply[3] = 0x00;
	put_be16(&reply[6], 1);
	put_be16(&reply[8], 0);
	put_be16(&reply[10], 0);

	if ((size_t)off + 16 > reply_len) {
		return -ENOBUFS;
	}

	reply[off++] = 0xc0;
	reply[off++] = 0x0c;
	put_be16(&reply[off], 1); off += 2;
	put_be16(&reply[off], 0x8001); off += 2;
	put_be32(&reply[off], 120); off += 4;
	put_be16(&reply[off], 4); off += 2;
	memcpy(&reply[off], &addr->s_addr, 4); off += 4;

	LOG_INF("mDNS answer: %s -> %s", target, wifi_manager_sta_connected() ? "STA IP" : "not connected");
	return off;
}

static int open_mdns_socket(void)
{
	struct net_if *iface = net_if_get_wifi_sta();
	struct net_sockaddr_in bind_addr = {0};
	struct net_ip_mreqn mreq = {0};
	int fd;
	int on = 1;
	int ttl = 255;

	if (iface == NULL) {
		return -ENODEV;
	}

	fd = zsock_socket(NET_AF_INET, NET_SOCK_DGRAM, NET_IPPROTO_UDP);
	if (fd < 0) {
		return -errno;
	}

	zsock_setsockopt(fd, ZSOCK_SOL_SOCKET, ZSOCK_SO_REUSEADDR, &on, sizeof(on));
	zsock_setsockopt(fd, ZSOCK_SOL_SOCKET, ZSOCK_SO_REUSEPORT, &on, sizeof(on));

	mreq.imr_ifindex = net_if_get_by_iface(iface);
	mreq.imr_multiaddr.s_addr = net_htonl(MDNS_MCAST);
	if (zsock_setsockopt(fd, NET_IPPROTO_IP, ZSOCK_IP_ADD_MEMBERSHIP,
			      &mreq, sizeof(mreq)) < 0 && errno != EALREADY) {
		int err = errno;
		zsock_close(fd);
		return -err;
	}

	zsock_setsockopt(fd, NET_IPPROTO_IP, ZSOCK_IP_MULTICAST_LOOP, &on, sizeof(on));
	zsock_setsockopt(fd, NET_IPPROTO_IP, ZSOCK_IP_MULTICAST_IF, &mreq, sizeof(mreq));
	zsock_setsockopt(fd, NET_IPPROTO_IP, ZSOCK_IP_MULTICAST_TTL, &ttl, sizeof(ttl));
	zsock_setsockopt(fd, NET_IPPROTO_IP, ZSOCK_IP_TTL, &ttl, sizeof(ttl));

	bind_addr.sin_family = NET_AF_INET;
	bind_addr.sin_port = net_htons(MDNS_PORT);
	bind_addr.sin_addr.s_addr = net_htonl(INADDR_ANY);

	if (zsock_bind(fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
		int err = errno;
		zsock_close(fd);
		return -err;
	}

	LOG_INF("mDNS responder listening for %s.local on UDP 5353", portal_state_host());
	return fd;
}

static void mdns_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		char ip[NET_IPV4_ADDR_LEN];
		struct in_addr local_addr;

		wifi_manager_local_ip(ip, sizeof(ip));
		if (!wifi_manager_sta_connected() || ip[0] == '\0' ||
		    net_addr_pton(AF_INET, ip, &local_addr) != 0) {
			k_sleep(K_SECONDS(1));
			continue;
		}

		int fd = open_mdns_socket();
		if (fd < 0) {
			LOG_WRN("mDNS socket start failed: %d", fd);
			k_sleep(K_SECONDS(2));
			continue;
		}

		while (wifi_manager_sta_connected()) {
			uint8_t query[512];
			uint8_t reply[576];
			struct sockaddr_in peer;
			socklen_t peer_len = sizeof(peer);
			int len = zsock_recvfrom(fd, query, sizeof(query), 0,
						 (struct sockaddr *)&peer, &peer_len);
			if (len <= 0) {
				continue;
			}

			int out_len = make_answer(query, len, reply, sizeof(reply), &local_addr);
			if (out_len <= 0) {
				continue;
			}

			zsock_sendto(fd, reply, out_len, 0,
				     (struct sockaddr *)&peer, peer_len);

			struct sockaddr_in mcast = {0};
			mcast.sin_family = AF_INET;
			mcast.sin_port = htons(MDNS_PORT);
			mcast.sin_addr.s_addr = htonl(MDNS_MCAST);
			zsock_sendto(fd, reply, out_len, 0,
				     (struct sockaddr *)&mcast, sizeof(mcast));
		}

		zsock_close(fd);
	}
}

K_THREAD_DEFINE(mdns_tid, 4096, mdns_thread, NULL, NULL, NULL, 6, 0, SYS_FOREVER_MS);

void portal_mdns_start(void)
{
	k_thread_start(mdns_tid);
	LOG_INF("mDNS service thread started");
}
