#include "portal_http.h"

#include "credential_store.h"
#include "portal_config.h"
#include "portal_render.h"
#include "portal_state.h"
#include "wifi_manager.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

LOG_MODULE_REGISTER(portal_http, LOG_LEVEL_INF);

static int http_listener = -1;
static K_SEM_DEFINE(listener_ready_sem, 0, 2);
static K_MUTEX_DEFINE(page_mutex);
static char page[PORTAL_PAGE_MAX];
static char req_buffers[2][PORTAL_REQ_MAX];

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
	if (out_len == 0) return;

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
	if (len >= sizeof(enc)) len = sizeof(enc) - 1;
	memcpy(enc, p, len);
	enc[len] = '\0';
	url_decode(enc, out, out_len);
	return true;
}

static void send_page(int client, const char *page)
{
	char header[192];
	int body_len = strlen(page);
	int header_len = snprintk(header, sizeof(header),
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: text/html; charset=utf-8\r\n"
		"Cache-Control: no-store\r\n"
		"Connection: close\r\n"
		"Content-Length: %d\r\n"
		"\r\n",
		body_len);

	LOG_INF("HTTP sending page: %d bytes", body_len);

	const char *chunks[] = { header, page };
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
	zsock_shutdown(client, SHUT_RDWR);
	zsock_close(client);
}

static void render_scan_placeholder(char *page, size_t page_len)
{
	snprintk(page, page_len,
		"<!doctype html><html><head><meta charset='utf-8'>"
		"<meta name='viewport' content='width=device-width,initial-scale=1'>"
		"<title>Scan</title></head><body style='font-family:system-ui;background:#050b16;color:#d8e6f7;padding:18px'>"
		"<h1>Wi-Fi scan temporarily disabled</h1>"
		"<p>The direct setup portal is stable. Use manual Wi-Fi entry for now while scan is isolated.</p>"
		"<p><a style='color:#46d8ff' href='/manual'>Type network manually</a></p>"
		"<p><a style='color:#46d8ff' href='/'>Back</a></p>"
		"</body></html>");
}

static void handle_request(const char *req, char *page, size_t page_len)
{
	char ssid[PORTAL_SSID_MAX + 1];
	char pass[PORTAL_PASS_MAX + 1];

	if (strncmp(req, "GET /scan", 9) == 0) {
		LOG_INF("HTTP route: scan placeholder");
		render_scan_placeholder(page, page_len);
		return;
	}

	if (strncmp(req, "GET /manual", 11) == 0) {
		LOG_INF("HTTP route: manual");
		portal_render_manual(page, page_len, NULL);
		return;
	}

	if (strncmp(req, "GET /pick", 9) == 0) {
		LOG_INF("HTTP route: pick");
		get_param(req, "ssid", ssid, sizeof(ssid));
		portal_render_pick(page, page_len, ssid, NULL);
		return;
	}

	if (strncmp(req, "POST /connect", 13) == 0) {
		LOG_INF("HTTP route: connect");
		get_param(req, "ssid", ssid, sizeof(ssid));
		get_param(req, "pass", pass, sizeof(pass));
		if (ssid[0] == '\0') {
			portal_render_manual(page, page_len, "Enter a Wi-Fi network name.");
			return;
		}
		credential_store_save(ssid, pass);
		int ret = wifi_manager_connect_blocking(ssid, pass);
		if (ret == 0 && wifi_manager_sta_connected()) {
			portal_render_success(page, page_len, ssid);
		} else {
			portal_render_pick(page, page_len, ssid, "Connection failed. Check the password and try again.");
		}
		return;
	}

	if (strncmp(req, "POST /handoff", 13) == 0) {
		LOG_INF("HTTP route: handoff");
		portal_state_request_handoff();
		portal_render_handoff(page, page_len);
		return;
	}

	if (strncmp(req, "POST /forget", 12) == 0) {
		LOG_INF("HTTP route: forget");
		credential_store_clear();
		portal_state_request_reboot();
		portal_render_reset(page, page_len);
		return;
	}

	LOG_INF("HTTP route: setup");
	portal_render_setup(page, page_len);
}

static int serve_one_client(int listener, int worker_id)
{
	int client = zsock_accept(listener, NULL, NULL);
	if (client < 0) {
		int err = errno;
		LOG_WRN("HTTP worker %d accept failed errno=%d", worker_id, err);
		return -err;
	}

	LOG_INF("HTTP worker %d client accepted", worker_id);
	char *req = req_buffers[worker_id];
	int total = 0;
	int content_len = 0;
	char *header_end = NULL;

	while (total < PORTAL_REQ_MAX - 1) {
		struct zsock_pollfd pfd = {
			.fd = client,
			.events = ZSOCK_POLLIN,
		};
		int timeout_ms = header_end == NULL ? 1500 : 3000;
		int ready = zsock_poll(&pfd, 1, timeout_ms);
		if (ready <= 0) {
			LOG_WRN("HTTP rx timeout/closed ready=%d total=%d errno=%d", ready, total, errno);
			break;
		}

		int n = zsock_recv(client, req + total, PORTAL_REQ_MAX - 1 - total, 0);
		if (n <= 0) {
			LOG_WRN("HTTP recv failed/closed n=%d errno=%d", n, errno);
			break;
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

	if (total > 0) {
		req[total] = '\0';
		LOG_INF("HTTP worker %d rx (%d bytes): %.120s", worker_id, total, req);
		k_mutex_lock(&page_mutex, K_FOREVER);
		handle_request(req, page, sizeof(page));
		send_page(client, page);
		k_mutex_unlock(&page_mutex);
	} else {
		LOG_WRN("HTTP worker %d client closed without request", worker_id);
	}

	close_client(client);
	LOG_INF("HTTP worker %d client closed", worker_id);
	return 0;
}

static void http_accept_worker(void *worker_arg, void *unused1, void *unused2)
{
	ARG_UNUSED(unused1);
	ARG_UNUSED(unused2);

	int worker_id = (int)(intptr_t)worker_arg;

	while (true) {
		k_sem_take(&listener_ready_sem, K_FOREVER);
		k_sem_give(&listener_ready_sem);

		int accept_errors = 0;
		while (true) {
			int ret = serve_one_client(http_listener, worker_id);
			if (ret == 0) {
				accept_errors = 0;
				continue;
			}

			accept_errors++;
			if (accept_errors >= 3) {
				LOG_WRN("HTTP worker %d has repeated accept errors", worker_id);
				break;
			}
			k_sleep(K_MSEC(25));
		}
	}
}

static void http_thread(void)
{
	while (true) {
		http_listener = open_listener();
		if (http_listener < 0) {
			LOG_ERR("HTTP listener open failed; retrying");
			k_sleep(K_MSEC(500));
			continue;
		}

		k_sem_give(&listener_ready_sem);
		k_sem_give(&listener_ready_sem);
		LOG_INF("HTTP accept workers released");

		while (true) {
			k_sleep(K_SECONDS(60));
		}
	}
}

K_THREAD_DEFINE(http_worker0_tid, 3072, http_accept_worker,
		(void *)0, NULL, NULL, 5, 0, SYS_FOREVER_MS);
K_THREAD_DEFINE(http_worker1_tid, 3072, http_accept_worker,
		(void *)1, NULL, NULL, 5, 0, SYS_FOREVER_MS);
K_THREAD_DEFINE(http_tid, 1536, http_thread, NULL, NULL, NULL, 5, 0, SYS_FOREVER_MS);

void portal_http_start(void)
{
	k_thread_start(http_tid);
	LOG_INF("HTTP portal service started on port %u", PORTAL_HTTP_PORT);
}
