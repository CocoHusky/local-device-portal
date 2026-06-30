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

static char page[PORTAL_PAGE_MAX];
static char req[PORTAL_REQ_MAX];
K_MSGQ_DEFINE(client_queue, sizeof(int), 4, 4);

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
	if (zsock_inet_pton(AF_INET, PORTAL_AP_IP, &addr.sin_addr) != 1) {
		LOG_ERR("HTTP invalid bind address: %s", PORTAL_AP_IP);
		zsock_close(fd);
		return -EINVAL;
	}

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

	LOG_INF("HTTP portal listener ready on %s:%u", PORTAL_AP_IP, PORTAL_HTTP_PORT);
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

static void render_debug_page(char *page, size_t page_len, const char *request)
{
	snprintk(page, page_len,
		"<!doctype html><html><head><meta charset='utf-8'>"
		"<meta name='viewport' content='width=device-width,initial-scale=1'>"
		"<title>Portal debug</title></head>"
		"<body style='font-family:system-ui;background:#050b16;color:#d8e6f7;padding:18px'>"
		"<h1>Portal debug</h1>"
		"<p>AP: <b>%s</b></p>"
		"<p>IP: <b>%s</b></p>"
		"<h2>Captive probe routes</h2>"
		"<ul>"
		"<li>Apple/macOS/iOS: /hotspot-detect.html, /library/test/success.html, /success.txt</li>"
		"<li>Android/ChromeOS: /generate_204, /gen_204</li>"
		"<li>Windows: /connecttest.txt, /ncsi.txt, /fwlink</li>"
		"<li>Firefox/Ubuntu: /canonical.html</li>"
		"</ul>"
		"<h2>Last request</h2>"
		"<pre style='white-space:pre-wrap;background:#07101f;border:1px solid #243852;padding:12px;border-radius:10px'>%.900s</pre>"
		"<p><a style='color:#46d8ff' href='/'>Setup</a> | "
		"<a style='color:#46d8ff' href='/manual'>Manual</a> | "
		"<a style='color:#46d8ff' href='/scan'>Scan</a></p>"
		"</body></html>",
		portal_state_ap_ssid(), PORTAL_AP_IP, request);
}

static bool is_captive_probe(const char *request)
{
	return strncmp(request, "GET /hotspot-detect.html", 24) == 0 ||
	       strncmp(request, "GET /library/test/success.html", 30) == 0 ||
	       strncmp(request, "GET /success.txt", 16) == 0 ||
	       strncmp(request, "GET /generate_204", 17) == 0 ||
	       strncmp(request, "GET /gen_204", 12) == 0 ||
	       strncmp(request, "GET /connecttest.txt", 20) == 0 ||
	       strncmp(request, "GET /ncsi.txt", 13) == 0 ||
	       strncmp(request, "GET /fwlink", 11) == 0 ||
	       strncmp(request, "GET /canonical.html", 19) == 0 ||
	       strncmp(request, "GET /mobile/status.php", 22) == 0 ||
	       strncmp(request, "GET /kindle-wifi/wifistub.html", 30) == 0;
}

static void handle_request(const char *req, char *page, size_t page_len)
{
	char ssid[PORTAL_SSID_MAX + 1];
	char pass[PORTAL_PASS_MAX + 1];

	if (is_captive_probe(req)) {
		LOG_INF("HTTP route: captive probe");
		portal_render_setup(page, page_len);
		return;
	}

	if (strncmp(req, "GET /debug", 10) == 0) {
		LOG_INF("HTTP route: debug");
		render_debug_page(page, page_len, req);
		return;
	}

	if (strncmp(req, "GET /dashboard", 14) == 0) {
		LOG_INF("HTTP route: dashboard");
		portal_render_dashboard(page, page_len);
		return;
	}

	if (strncmp(req, "GET /scan", 9) == 0) {
		LOG_INF("HTTP route: scan");
		portal_render_scan(page, page_len);
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

	if (wifi_manager_sta_connected()) {
		LOG_INF("HTTP route: dashboard default");
		portal_render_dashboard(page, page_len);
	} else {
		LOG_INF("HTTP route: setup");
		portal_render_setup(page, page_len);
	}
}

static void serve_client(int client)
{
	int total = 0;
	int content_len = 0;
	char *header_end = NULL;

	while (total < PORTAL_REQ_MAX - 1) {
		struct zsock_pollfd pfd = {
			.fd = client,
			.events = ZSOCK_POLLIN,
		};
		int ready = zsock_poll(&pfd, 1, 1500);
		if (ready <= 0) {
			if (total > 0) {
				LOG_WRN("HTTP rx timeout/closed ready=%d total=%d errno=%d",
					ready, total, errno);
			}
			goto done;
		}

		int n = zsock_recv(client, req + total, PORTAL_REQ_MAX - 1 - total, 0);
		if (n <= 0) {
			if (total > 0) {
				LOG_WRN("HTTP recv failed/closed n=%d errno=%d", n, errno);
			}
			goto done;
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
	LOG_INF("HTTP rx (%d bytes): %.120s", total, req);
	handle_request(req, page, sizeof(page));
	send_page(client, page);

done:
	close_client(client);
	LOG_INF("HTTP client closed");
}

static void http_accept_thread(void)
{
	while (true) {
		int fd = open_listener();
		if (fd < 0) {
			LOG_ERR("HTTP listener open failed; retrying");
			k_sleep(K_MSEC(500));
			continue;
		}

		int accept_errors = 0;
		while (true) {
			int client = zsock_accept(fd, NULL, NULL);
			if (client >= 0) {
				LOG_INF("HTTP client accepted");
				accept_errors = 0;
				if (k_msgq_put(&client_queue, &client, K_NO_WAIT) != 0) {
					LOG_WRN("HTTP client queue full; closing");
					close_client(client);
				}
				continue;
			}

			LOG_WRN("HTTP accept failed errno=%d", errno);
			accept_errors++;
			if (accept_errors >= 3) {
				LOG_WRN("HTTP listener unhealthy after accept errors; reopening");
				break;
			}
			k_sleep(K_MSEC(25));
		}

		zsock_close(fd);
	}
}

static void http_client_thread(void)
{
	while (true) {
		int client;
		k_msgq_get(&client_queue, &client, K_FOREVER);
		serve_client(client);
	}
}

K_THREAD_DEFINE(http_accept_tid, 3072, http_accept_thread, NULL, NULL, NULL, 5, 0, SYS_FOREVER_MS);
K_THREAD_DEFINE(http_client_tid, 6144, http_client_thread, NULL, NULL, NULL, 5, 0, SYS_FOREVER_MS);

void portal_http_start(void)
{
	k_thread_start(http_accept_tid);
	k_thread_start(http_client_tid);
	LOG_INF("HTTP portal service started on port %u", PORTAL_HTTP_PORT);
}
