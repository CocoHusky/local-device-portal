#include "portal_http.h"

#include "portal_config.h"
#include "portal_render.h"
#include "wifi_manager.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>

#include <errno.h>
#include <string.h>

LOG_MODULE_REGISTER(portal_http, LOG_LEVEL_INF);

static void send_setup_page(int client)
{
	static char page[PORTAL_PAGE_MAX];
	char header[192];

	portal_render_setup(page, sizeof(page));

	int body_len = strlen(page);
	int header_len = snprintk(header, sizeof(header),
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: text/html; charset=utf-8\r\n"
		"Cache-Control: no-store\r\n"
		"Connection: close\r\n"
		"Content-Length: %d\r\n\r\n",
		body_len);

	zsock_send(client, header, header_len, 0);
	zsock_send(client, page, body_len, 0);
}

static int open_listener(uint16_t port, const char *label)
{
	int fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (fd < 0) {
		LOG_ERR("HTTP %s socket failed: errno=%d", label, errno);
		return -errno;
	}

	int opt = 1;
	zsock_setsockopt(fd, ZSOCK_SOL_SOCKET, ZSOCK_SO_REUSEADDR, &opt, sizeof(opt));

	int ret = wifi_manager_bind_socket_to_ap(fd);
	if (ret != 0) {
		LOG_WRN("HTTP %s AP bind-to-device failed: %d", label, ret);
	} else {
		LOG_INF("HTTP %s socket bound to AP interface", label);
	}

	struct sockaddr_in addr = { 0 };
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	if (net_addr_pton(AF_INET, PORTAL_AP_IP, &addr.sin_addr) != 0) {
		LOG_ERR("HTTP %s invalid AP IP: %s", label, PORTAL_AP_IP);
		zsock_close(fd);
		return -EINVAL;
	}

	if (zsock_bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		int err = errno;
		LOG_ERR("HTTP %s bind failed on %s:%u errno=%d", label, PORTAL_AP_IP, port, err);
		zsock_close(fd);
		return -err;
	}

	if (zsock_listen(fd, 4) < 0) {
		int err = errno;
		LOG_ERR("HTTP %s listen failed on %s:%u errno=%d", label, PORTAL_AP_IP, port, err);
		zsock_close(fd);
		return -err;
	}

	LOG_INF("HTTP %s listening on %s:%u", label, PORTAL_AP_IP, port);
	return fd;
}

static void serve_forever(uint16_t port, const char *label)
{
	k_sleep(K_SECONDS(2));

	int fd = open_listener(port, label);
	if (fd < 0) {
		LOG_ERR("HTTP %s failed to start", label);
		return;
	}

	while (true) {
		struct sockaddr_in peer;
		socklen_t peer_len = sizeof(peer);
		int client = zsock_accept(fd, (struct sockaddr *)&peer, &peer_len);
		if (client < 0) {
			LOG_WRN("HTTP %s accept failed: errno=%d", label, errno);
			k_sleep(K_MSEC(100));
			continue;
		}

		LOG_INF("HTTP %s client accepted", label);
		char req[256];
		int n = zsock_recv(client, req, sizeof(req) - 1, 0);
		if (n > 0) {
			req[n] = '\0';
			LOG_INF("HTTP %s request: %.80s", label, req);
			send_setup_page(client);
		} else {
			LOG_WRN("HTTP %s recv failed/closed: n=%d errno=%d", label, n, errno);
		}
		zsock_close(client);
	}
}

static void http80_thread(void)
{
	serve_forever(PORTAL_HTTP_PORT, "primary");
}

static void http8080_thread(void)
{
	serve_forever(PORTAL_HTTP_FALLBACK_PORT, "fallback");
}

K_THREAD_DEFINE(http80_tid, 8192, http80_thread, NULL, NULL, NULL, 5, 0, SYS_FOREVER_MS);
K_THREAD_DEFINE(http8080_tid, 8192, http8080_thread, NULL, NULL, NULL, 5, 0, SYS_FOREVER_MS);

void portal_http_start(void)
{
	k_thread_start(http80_tid);
	k_thread_start(http8080_tid);
	LOG_INF("HTTP debug portal starting on ports %u and %u", PORTAL_HTTP_PORT, PORTAL_HTTP_FALLBACK_PORT);
}
