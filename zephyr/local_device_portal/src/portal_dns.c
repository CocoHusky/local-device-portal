#include "portal_dns.h"

#include "portal_config.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

LOG_MODULE_REGISTER(portal_dns, LOG_LEVEL_INF);

static void dns_thread(void)
{
	int fd = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fd < 0) {
		LOG_ERR("dns socket failed: %d", errno);
		return;
	}

	struct sockaddr_in addr = { 0 };
	addr.sin_family = AF_INET;
	addr.sin_port = htons(PORTAL_DNS_PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (zsock_bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		LOG_ERR("dns bind failed: %d", errno);
		zsock_close(fd);
		return;
	}

	LOG_INF("DNS captive responder listening on port %d", PORTAL_DNS_PORT);

	uint8_t query[512];
	uint8_t reply[576];

	while (true) {
		struct sockaddr_in peer;
		socklen_t peer_len = sizeof(peer);
		int len = zsock_recvfrom(fd, query, sizeof(query), 0,
					 (struct sockaddr *)&peer, &peer_len);
		if (len < 12) {
			continue;
		}

		memcpy(reply, query, len);
		reply[2] = 0x81;
		reply[3] = 0x80;
		reply[6] = 0x00;
		reply[7] = 0x01;

		int off = len;
		if (off + 16 > (int)sizeof(reply)) {
			continue;
		}

		reply[off++] = 0xc0;
		reply[off++] = 0x0c;
		reply[off++] = 0x00;
		reply[off++] = 0x01;
		reply[off++] = 0x00;
		reply[off++] = 0x01;
		reply[off++] = 0x00;
		reply[off++] = 0x00;
		reply[off++] = 0x00;
		reply[off++] = 0x1e;
		reply[off++] = 0x00;
		reply[off++] = 0x04;
		reply[off++] = 192;
		reply[off++] = 168;
		reply[off++] = 4;
		reply[off++] = 1;

		zsock_sendto(fd, reply, off, 0, (struct sockaddr *)&peer, peer_len);
	}
}

K_THREAD_DEFINE(dns_tid, 4096, dns_thread, NULL, NULL, NULL, 5, 0, 0);

void portal_dns_start(void)
{
	LOG_INF("DNS service scheduled");
}
