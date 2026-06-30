#include "portal_http.h"

#include "portal_config.h"

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

	LOG_INF("HTTP minimal listener ready on 0.0.0.0:%u", PORTAL_HTTP_PORT);
	return fd;
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

		char req[128];
		int n = zsock_recv(client, req, sizeof(req) - 1, 0);
		if (n > 0) {
			req[n] = '\0';
			LOG_INF("HTTP rx: %.80s", req);
		}

		const char body[] = "hello from portal minimal tcp probe\n";
		char header[160];
		int header_len = snprintk(header, sizeof(header),
			"HTTP/1.1 200 OK\r\n"
			"Content-Type: text/plain\r\n"
			"Connection: close\r\n"
			"Content-Length: %u\r\n"
			"\r\n",
			(unsigned int)(sizeof(body) - 1));

		zsock_send(client, header, header_len, 0);
		zsock_send(client, body, sizeof(body) - 1, 0);
		zsock_close(client);
	}
}

K_THREAD_DEFINE(http_tid, 8192, http_thread, NULL, NULL, NULL, 5, 0, SYS_FOREVER_MS);

void portal_http_start(void)
{
	k_thread_start(http_tid);
	LOG_INF("HTTP minimal service started on port %u", PORTAL_HTTP_PORT);
}
