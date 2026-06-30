#include "portal_http.h"

#include "credential_store.h"
#include "portal_config.h"
#include "portal_render.h"
#include "wifi_manager.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/posix/sys/socket.h>

#include <errno.h>
#include <stdlib.h>
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

static char page[PORTAL_PAGE_MAX];

static int hex_val(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static void url_decode(const char *in, char *out, size_t out_len)
{
	size_t o = 0;

	if (out_len == 0) {
		return;
	}

	for (size_t i = 0; in[i] != '\0' && o + 1 < out_len; i++) {
		if (in[i] == '+') {
			out[o++] = ' ';
			continue;
		}
		if (in[i] == '%' && in[i + 1] && in[i + 2]) {
			int hi = hex_val(in[i + 1]);
			int lo = hex_val(in[i + 2]);
			if (hi >= 0 && lo >= 0) {
				out[o++] = (char)((hi << 4) | lo);
				i += 2;
				continue;
			}
		}
		out[o++] = in[i];
	}
	out[o] = '\0';
}

static bool get_param(const char *src, const char *name, char *out, size_t out_len)
{
	char needle[32];
	snprintk(needle, sizeof(needle), "%s=", name);

	const char *p = strstr(src, needle);
	if (p == NULL) {
		if (out_len) out[0] = '\0';
		return false;
	}

	p += strlen(needle);
	const char *end = strpbrk(p, "& \r\n");
	size_t len = end ? (size_t)(end - p) : strlen(p);

	char enc[PORTAL_PASS_MAX + PORTAL_SSID_MAX + 8];
	if (len >= sizeof(enc)) {
		len = sizeof(enc) - 1;
	}
	memcpy(enc, p, len);
	enc[len] = '\0';
	url_decode(enc, out, out_len);
	return true;
}

static void send_page(int client, const char *body)
{
	char header[192];

	int body_len = strlen(body);

	int header_len = snprintk(header, sizeof(header),
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: text/html; charset=utf-8\r\n"
		"Cache-Control: no-store\r\n"
		"Connection: close\r\n"
		"Content-Length: %d\r\n"
		"\r\n",
		body_len);

	LOG_INF("HTTP sending page: %d bytes", body_len);

	const char *chunks[] = { header, body };
	int lens[] = { header_len, body_len };

	for (int i = 0; i < ARRAY_SIZE(chunks); i++) {
		const char *data = chunks[i];
		int remaining = lens[i];

		while (remaining > 0) {
			int sent = zsock_send(client, data, remaining, 0);
			if (sent <= 0) {
				LOG_WRN("HTTP send stopped sent=%d errno=%d", sent, errno);
				return;
			}
			data += sent;
			remaining -= sent;
		}
	}
}

static void close_client(int client)
{
	struct linger linger = {
		.l_onoff = 1,
		.l_linger = 0,
	};

	(void)zsock_setsockopt(client, SOL_SOCKET, SO_LINGER, &linger, sizeof(linger));
	(void)zsock_shutdown(client, SHUT_RDWR);
	zsock_close(client);
	LOG_INF("HTTP client closed; returning to accept");
}

static void handle_request(const char *req)
{
	char ssid[PORTAL_SSID_MAX + 1];
	char pass[PORTAL_PASS_MAX + 1];

	if (strncmp(req, "GET /scan", 9) == 0) {
		LOG_INF("HTTP route: scan");
		portal_render_scan(page, sizeof(page));
		return;
	}

	if (strncmp(req, "GET /manual", 11) == 0) {
		LOG_INF("HTTP route: manual");
		portal_render_manual(page, sizeof(page), NULL);
		return;
	}

	if (strncmp(req, "GET /pick", 9) == 0) {
		LOG_INF("HTTP route: pick");
		get_param(req, "ssid", ssid, sizeof(ssid));
		portal_render_pick(page, sizeof(page), ssid, NULL);
		return;
	}

	if (strncmp(req, "POST /connect", 13) == 0) {
		LOG_INF("HTTP route: connect");
		get_param(req, "ssid", ssid, sizeof(ssid));
		get_param(req, "pass", pass, sizeof(pass));
		if (ssid[0] == '\0') {
			portal_render_manual(page, sizeof(page), "Enter a Wi-Fi network name.");
			return;
		}
		credential_store_save(ssid, pass);
		int ret = wifi_manager_connect_blocking(ssid, pass);
		if (ret == 0 && wifi_manager_sta_connected()) {
			portal_render_success(page, sizeof(page), ssid);
		} else {
			portal_render_pick(page, sizeof(page), ssid,
					   "Connection failed. Check the password and try again.");
		}
		return;
	}

	LOG_INF("HTTP route: setup");
	portal_render_setup(page, sizeof(page));
}

static int recv_request(int client, char *req, size_t req_len)
{
	int total = 0;
	int content_len = 0;
	char *header_end = NULL;

	while (total < (int)req_len - 1) {
		int n = zsock_recv(client, req + total, req_len - 1 - total, 0);
		if (n <= 0) {
			return total > 0 ? total : n;
		}

		total += n;
		req[total] = '\0';

		if (header_end == NULL) {
			header_end = strstr(req, "\r\n\r\n");
			if (header_end != NULL) {
				const char *content = strstr(req, "Content-Length:");
				if (content != NULL && content < header_end) {
					content += strlen("Content-Length:");
					content_len = atoi(content);
					if (content_len < 0) {
						content_len = 0;
					}
				}
			}
		}

		if (header_end != NULL) {
			int header_len = (int)(header_end - req) + 4;
			if (total >= header_len + content_len) {
				break;
			}
		}
	}

	req[total] = '\0';
	return total;
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

		char req[PORTAL_REQ_MAX];
		int n = recv_request(client, req, sizeof(req));
		if (n > 0) {
			LOG_INF("HTTP rx (%d bytes): %.120s", n, req);
			handle_request(req);
			send_page(client, page);
		} else {
			LOG_WRN("HTTP empty/failed request n=%d errno=%d", n, errno);
		}

		close_client(client);
	}
}

K_THREAD_DEFINE(http_tid, 8192, http_thread, NULL, NULL, NULL, 5, 0, SYS_FOREVER_MS);

void portal_http_start(void)
{
	k_thread_start(http_tid);
	LOG_INF("HTTP portal service started on port %u", PORTAL_HTTP_PORT);
}
