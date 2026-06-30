#include "portal_http.h"

#include "portal_config.h"
#include "portal_render.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>

#include <errno.h>
#include <string.h>

LOG_MODULE_REGISTER(portal_http, LOG_LEVEL_INF);

static int open_listener(void)
{
	int fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (fd < 0) {
		LOG_ERR("HTTP socket failed errno=%d", errno);
		return -errno;
	}

	int opt = 1;
	zsock_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(PORTAL_HTTP_PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (zsock_bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		int err = errno;
		LOG_ERR("HTTP bind failed errno=%d", err);
		zsock_close(fd);
		return -err;
	}

	if (zsock_listen(fd, 4) < 0) {
		int err = errno;
		LOG_ERR("HTTP listen failed errno=%d", err);
		zsock_close(fd);
		return -err;
	}

	LOG_INF("HTTP portal listener ready on 0.0.0.0:%u", PORTAL_HTTP_PORT);
	return fd;
}

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
		"Content-Length: %d\r\n"
		"\r\n",
		body_len);

	LOG_INF("HTTP sending setup page: %d bytes", body_len);
	zsock_send(client, header, header_len, 0);
	zsock_send(client, page, body_len, 0);
}

static void http_thread(void)
{
	int fd = open_listener();
	if (fd < 0) {
		LOG_ERR("HTTP server did not start");
		return;
	}

	while (true) {
		int client = zsock_accept(fd, NULL, NULL);
		if (client < 0) {
			LOG_WRN("HTTP accept failed errno=%d", errno);
			k_sleep(K_MSEC(100));
			continue;
		}

		LOG_INF("HTTP client accepted");

		char req[256];
		int n = zsock_recv(client, req, sizeof(req) - 1, 0);
		if (n > 0) {
			req[n] = '\0';
			LOG_INF("HTTP rx: %.80s", req);
		}

		send_setup_page(client);
		zsock_close(client);
	}
}

K_THREAD_DEFINE(http_tid, 8192, http_thread, NULL, NULL, NULL, 5, 0, SYS_FOREVER_MS);

void portal_http_start(void)
{
	k_thread_start(http_tid);
	LOG_INF("HTTP portal service started on port %u", PORTAL_HTTP_PORT);
}
