#include "portal_http.h"

#include "credential_store.h"
#include "portal_config.h"
#include "portal_render.h"
#include "portal_state.h"
#include "wifi_manager.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/util.h>

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(portal_http, LOG_LEVEL_INF);

static void url_decode(char *s)
{
	char *src = s;
	char *dst = s;

	while (*src != '\0') {
		if (*src == '+') {
			*dst++ = ' ';
			src++;
		} else if (*src == '%' && src[1] && src[2]) {
			char hex[3] = { src[1], src[2], 0 };
			*dst++ = (char)strtol(hex, NULL, 16);
			src += 3;
		} else {
			*dst++ = *src++;
		}
	}

	*dst = '\0';
}

static void form_value(const char *body, const char *key, char *out, size_t out_len)
{
	out[0] = '\0';

	char pattern[32];
	snprintk(pattern, sizeof(pattern), "%s=", key);
	const char *start = strstr(body, pattern);

	if (start == NULL) {
		return;
	}

	start += strlen(pattern);
	const char *end = strchr(start, '&');
	if (end == NULL) {
		end = start + strlen(start);
	}

	size_t len = MIN((size_t)(end - start), out_len - 1);
	memcpy(out, start, len);
	out[len] = '\0';
	url_decode(out);
}

static void send_response(int client, const char *body)
{
	char header[192];
	int len = strlen(body);
	int hlen = snprintk(header, sizeof(header),
		"HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
		"Cache-Control: no-store\r\nConnection: close\r\nContent-Length: %d\r\n\r\n",
		len);

	zsock_send(client, header, hlen, 0);
	zsock_send(client, body, len, 0);
}

static void send_json_status(int client)
{
	char body[256];
	snprintk(body, sizeof(body),
		"{\"ap_ssid\":\"%s\",\"sta_connected\":%s,\"saved_ssid\":\"%s\",\"host\":\"%s\",\"dashboard\":\"%s\"}",
		portal_state_ap_ssid(), wifi_manager_sta_connected() ? "true" : "false",
		credential_store_ssid(), portal_state_host(), portal_state_dashboard_url());

	char header[192];
	int len = strlen(body);
	int hlen = snprintk(header, sizeof(header),
		"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: %d\r\n\r\n",
		len);

	zsock_send(client, header, hlen, 0);
	zsock_send(client, body, len, 0);
}

static void send_json_data(int client)
{
	char ip[NET_IPV4_ADDR_LEN];
	char body[256];
	int64_t uptime = k_uptime_get();

	wifi_manager_local_ip(ip, sizeof(ip));
	snprintk(body, sizeof(body),
		"{\"sta_connected\":%s,\"local_ip\":\"%s\",\"rssi\":0,\"uptime_ms\":%lld,\"uptime\":\"%llds\"}",
		wifi_manager_sta_connected() ? "true" : "false", ip,
		(long long)uptime, (long long)(uptime / 1000));

	char header[192];
	int len = strlen(body);
	int hlen = snprintk(header, sizeof(header),
		"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: %d\r\n\r\n",
		len);

	zsock_send(client, header, hlen, 0);
	zsock_send(client, body, len, 0);
}

static bool path_is_captive_probe(const char *path)
{
	return strstr(path, "generate_204") != NULL ||
	       strstr(path, "gen_204") != NULL ||
	       strstr(path, "hotspot-detect") != NULL ||
	       strstr(path, "library/test/success.html") != NULL ||
	       strstr(path, "connecttest") != NULL ||
	       strstr(path, "ncsi") != NULL ||
	       strstr(path, "redirect") != NULL;
}

static void render_setup_entry(char *page, size_t page_len)
{
	if (wifi_manager_sta_connected()) {
		const char *ssid = credential_store_has_ssid() ?
			credential_store_ssid() : "local Wi-Fi";
		portal_render_success(page, page_len, ssid);
	} else {
		portal_render_setup(page, page_len);
	}
}

static void handle_http_client(int client)
{
	char req[PORTAL_REQ_MAX];
	char page[PORTAL_PAGE_MAX];
	int n = zsock_recv(client, req, sizeof(req) - 1, 0);

	if (n <= 0) {
		return;
	}
	req[n] = '\0';

	char method[8] = {0};
	char path[256] = {0};
	sscanf(req, "%7s %255s", method, path);

	char *body = strstr(req, "\r\n\r\n");
	body = body ? body + 4 : (char *)"";

	if (strcmp(path, "/status") == 0) {
		send_json_status(client);
		return;
	}

	if (strcmp(path, "/data") == 0) {
		send_json_data(client);
		return;
	}

	if (strcmp(path, "/") == 0 || strcmp(path, "/setup") == 0) {
		render_setup_entry(page, sizeof(page));
		send_response(client, page);
		return;
	}

	if (strcmp(path, "/scan") == 0) {
		portal_render_scan(page, sizeof(page));
		send_response(client, page);
		return;
	}

	if (strncmp(path, "/pick?", 6) == 0) {
		char ssid[PORTAL_SSID_MAX + 1];
		form_value(path + 6, "ssid", ssid, sizeof(ssid));
		portal_render_pick(page, sizeof(page), ssid, NULL);
		send_response(client, page);
		return;
	}

	if (strcmp(path, "/manual") == 0) {
		portal_render_manual(page, sizeof(page), NULL);
		send_response(client, page);
		return;
	}

	if (strcmp(path, "/connect") == 0 && strcmp(method, "POST") == 0) {
		char ssid[PORTAL_SSID_MAX + 1];
		char pass[PORTAL_PASS_MAX + 1];
		form_value(body, "ssid", ssid, sizeof(ssid));
		form_value(body, "pass", pass, sizeof(pass));

		if (ssid[0] == '\0') {
			portal_render_manual(page, sizeof(page), "Missing network name.");
			send_response(client, page);
			return;
		}

		if (wifi_manager_connect_blocking(ssid, pass) == 0) {
			credential_store_save(ssid, pass);
			portal_render_success(page, sizeof(page), ssid);
		} else {
			portal_render_pick(page, sizeof(page), ssid,
				"Connection failed. Check the password or move closer to the router.");
		}
		send_response(client, page);
		return;
	}

	if (strcmp(path, "/handoff") == 0) {
		if (wifi_manager_sta_connected()) {
			portal_state_request_handoff();
			portal_render_handoff(page, sizeof(page));
		} else {
			portal_render_setup(page, sizeof(page));
		}
		send_response(client, page);
		return;
	}

	if (strcmp(path, "/forget") == 0 && strcmp(method, "POST") == 0) {
		credential_store_clear();
		portal_state_request_reboot();
		portal_render_reset(page, sizeof(page));
		send_response(client, page);
		return;
	}

	if (strcmp(path, "/dashboard") == 0) {
		if (wifi_manager_sta_connected()) {
			portal_render_dashboard(page, sizeof(page));
		} else {
			portal_render_setup(page, sizeof(page));
		}
		send_response(client, page);
		return;
	}

	if (path_is_captive_probe(path)) {
		render_setup_entry(page, sizeof(page));
		send_response(client, page);
		return;
	}

	render_setup_entry(page, sizeof(page));
	send_response(client, page);
}

static void http_thread(void)
{
	int server_fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (server_fd < 0) {
		LOG_ERR("http socket failed: %d", errno);
		return;
	}

	int opt = 1;
	zsock_setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	(void)wifi_manager_bind_socket_to_ap(server_fd);

	struct sockaddr_in addr = { 0 };
	addr.sin_family = AF_INET;
	addr.sin_port = htons(PORTAL_HTTP_PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (zsock_bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		LOG_ERR("http bind failed: %d", errno);
		zsock_close(server_fd);
		return;
	}

	if (zsock_listen(server_fd, 4) < 0) {
		LOG_ERR("http listen failed: %d", errno);
		zsock_close(server_fd);
		return;
	}

	LOG_INF("HTTP server listening on port %d", PORTAL_HTTP_PORT);

	while (true) {
		int client = zsock_accept(server_fd, NULL, NULL);
		if (client >= 0) {
			handle_http_client(client);
			zsock_close(client);
		}
	}
}

K_THREAD_DEFINE(http_tid, 8192, http_thread, NULL, NULL, NULL, 5, 0, SYS_FOREVER_MS);

void portal_http_start(void)
{
	k_thread_start(http_tid);
	LOG_INF("HTTP service started");
}
