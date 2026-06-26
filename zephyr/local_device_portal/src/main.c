// Local Device Portal — Zephyr MVP
// Captive-portal-style Wi-Fi setup and local dashboard for ESP32-C6 targets.

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

#if defined(CONFIG_NET_DHCPV4_SERVER)
#include <zephyr/net/dhcpv4_server.h>
#endif

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LOG_MODULE_REGISTER(local_device_portal, LOG_LEVEL_INF);

#define PORTAL_AP_SSID      "mmWave-Setup"
#define PORTAL_AP_PASS      "focusfetch"
#define PORTAL_AP_IP        "192.168.4.1"
#define PORTAL_HTTP_PORT    80
#define PORTAL_DNS_PORT     53
#define PORTAL_MAX_NETS     24
#define PORTAL_SSID_MAX     32
#define PORTAL_PASS_MAX     64
#define PORTAL_REQ_MAX      2048
#define PORTAL_PAGE_MAX     8192
#define PORTAL_HOST_MAX     32

struct portal_net {
	char ssid[PORTAL_SSID_MAX + 1];
	int rssi;
	bool secure;
};

static struct portal_net scan_results[PORTAL_MAX_NETS];
static int scan_count;
static char saved_ssid[PORTAL_SSID_MAX + 1];
static char saved_pass[PORTAL_PASS_MAX + 1];
static char device_host[PORTAL_HOST_MAX];
static char dashboard_url[64];
static bool sta_connected;
static bool handoff_pending;

static K_SEM_DEFINE(scan_done_sem, 0, 1);
static K_SEM_DEFINE(connect_done_sem, 0, 1);
static struct net_mgmt_event_callback wifi_cb;

static void append(char *buf, size_t cap, size_t *off, const char *fmt, ...)
{
	if (*off >= cap) {
		return;
	}

	va_list args;
	va_start(args, fmt);
	int n = vsnprintf(buf + *off, cap - *off, fmt, args);
	va_end(args);

	if (n < 0) {
		return;
	}

	if ((size_t)n >= cap - *off) {
		*off = cap - 1;
	} else {
		*off += (size_t)n;
	}
}

static void html_escape(const char *in, char *out, size_t out_len)
{
	size_t o = 0;

	if (out_len == 0) {
		return;
	}

	for (size_t i = 0; in[i] != '\0' && o + 1 < out_len; i++) {
		const char *rep = NULL;

		switch (in[i]) {
		case '&': rep = "&amp;"; break;
		case '<': rep = "&lt;"; break;
		case '>': rep = "&gt;"; break;
		case '"': rep = "&quot;"; break;
		case '\'': rep = "&#39;"; break;
		default: break;
		}

		if (rep != NULL) {
			size_t len = strlen(rep);
			if (o + len >= out_len) {
				break;
			}
			memcpy(out + o, rep, len);
			o += len;
		} else {
			out[o++] = in[i];
		}
	}

	out[o] = '\0';
}

static void url_decode(char *s)
{
	char *src = s;
	char *dst = s;

	while (*src != '\0') {
		if (*src == '+') {
			*dst++ = ' ';
			src++;
		} else if (*src == '%' && src[1] && src[2]) {
			char hex[3] = { src[1], src[2], '\0' };
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
	char pattern[32];
	out[0] = '\0';
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

static int signal_percent(int rssi)
{
	int pct = 2 * (rssi + 100);
	return CLAMP(pct, 0, 100);
}

static const char *signal_label(int rssi)
{
	if (rssi >= -55) {
		return "Excellent";
	}
	if (rssi >= -67) {
		return "Good";
	}
	if (rssi >= -75) {
		return "Fair";
	}
	return "Weak";
}

static struct net_if *portal_iface(void)
{
	return net_if_get_default();
}

static void make_device_host(void)
{
	struct net_if *iface = portal_iface();
	const struct net_linkaddr *link = iface ? net_if_get_link_addr(iface) : NULL;
	uint16_t suffix = (uint16_t)(k_cycle_get_32() & 0xffff);

	if (link != NULL && link->addr != NULL && link->len >= 2) {
		suffix = ((uint16_t)link->addr[link->len - 2] << 8) | link->addr[link->len - 1];
	}

	snprintk(device_host, sizeof(device_host), "mmwave-%04x", suffix);
	snprintk(dashboard_url, sizeof(dashboard_url), "http://%s.local/", device_host);
}

static int portal_settings_set(const char *key, size_t len_rd,
			       settings_read_cb read_cb, void *cb_arg)
{
	char *target = NULL;
	size_t target_len = 0;

	if (strcmp(key, "ssid") == 0) {
		target = saved_ssid;
		target_len = sizeof(saved_ssid);
	} else if (strcmp(key, "pass") == 0) {
		target = saved_pass;
		target_len = sizeof(saved_pass);
	} else {
		return -ENOENT;
	}

	size_t len = MIN(len_rd, target_len - 1);
	ssize_t rc = read_cb(cb_arg, target, len);
	if (rc < 0) {
		return (int)rc;
	}

	target[MIN((size_t)rc, target_len - 1)] = '\0';
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(portal_settings, "portal", NULL,
			       portal_settings_set, NULL, NULL);

static void credentials_init(void)
{
	int ret = settings_subsys_init();
	if (ret != 0 && ret != -EALREADY) {
		LOG_WRN("settings init failed: %d", ret);
	}

	ret = settings_load_subtree("portal");
	if (ret != 0) {
		LOG_WRN("settings load failed: %d", ret);
	}
}

static void credentials_save(const char *ssid, const char *pass)
{
	strncpy(saved_ssid, ssid, sizeof(saved_ssid) - 1);
	saved_ssid[sizeof(saved_ssid) - 1] = '\0';
	strncpy(saved_pass, pass, sizeof(saved_pass) - 1);
	saved_pass[sizeof(saved_pass) - 1] = '\0';
	(void)settings_save_one("portal/ssid", saved_ssid, strlen(saved_ssid) + 1);
	(void)settings_save_one("portal/pass", saved_pass, strlen(saved_pass) + 1);
}

static void credentials_clear(void)
{
	saved_ssid[0] = '\0';
	saved_pass[0] = '\0';
	(void)settings_delete("portal/ssid");
	(void)settings_delete("portal/pass");
}

static void wifi_event_handler(struct net_mgmt_event_callback *cb,
			       uint64_t mgmt_event, struct net_if *iface)
{
	ARG_UNUSED(iface);

	if (mgmt_event == NET_EVENT_WIFI_SCAN_RESULT) {
		const struct wifi_scan_result *entry = cb->info;
		if (entry == NULL || scan_count >= PORTAL_MAX_NETS || entry->ssid_length == 0) {
			return;
		}

		int ssid_len = MIN((int)entry->ssid_length, PORTAL_SSID_MAX);
		memcpy(scan_results[scan_count].ssid, entry->ssid, ssid_len);
		scan_results[scan_count].ssid[ssid_len] = '\0';
		scan_results[scan_count].rssi = entry->rssi;
		scan_results[scan_count].secure = entry->security != WIFI_SECURITY_TYPE_NONE;
		scan_count++;
		return;
	}

	if (mgmt_event == NET_EVENT_WIFI_SCAN_DONE) {
		k_sem_give(&scan_done_sem);
		return;
	}

	if (mgmt_event == NET_EVENT_WIFI_CONNECT_RESULT) {
		const struct wifi_status *status = cb->info;
		sta_connected = (status == NULL || status->status == 0);
		k_sem_give(&connect_done_sem);
		return;
	}

	if (mgmt_event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
		sta_connected = false;
	}
}

static void wifi_init(void)
{
	net_mgmt_init_event_callback(&wifi_cb, wifi_event_handler,
		NET_EVENT_WIFI_SCAN_RESULT |
		NET_EVENT_WIFI_SCAN_DONE |
		NET_EVENT_WIFI_CONNECT_RESULT |
		NET_EVENT_WIFI_DISCONNECT_RESULT);
	net_mgmt_add_event_callback(&wifi_cb);
}

static int wifi_start_ap(void)
{
	struct net_if *iface = portal_iface();
	if (iface == NULL) {
		return -ENODEV;
	}

	struct in_addr addr;
	if (net_addr_pton(AF_INET, PORTAL_AP_IP, &addr) == 0) {
		(void)net_if_ipv4_addr_add(iface, &addr, NET_ADDR_MANUAL, 0);
#if defined(CONFIG_NET_DHCPV4_SERVER)
		(void)net_dhcpv4_server_start(iface, &addr);
#endif
	}

	struct wifi_connect_req_params ap = { 0 };
	ap.ssid = (const uint8_t *)PORTAL_AP_SSID;
	ap.ssid_length = strlen(PORTAL_AP_SSID);
	ap.psk = (const uint8_t *)PORTAL_AP_PASS;
	ap.psk_length = strlen(PORTAL_AP_PASS);
	ap.security = WIFI_SECURITY_TYPE_PSK;
	ap.channel = 6;

	int ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, iface, &ap, sizeof(ap));
	if (ret != 0) {
		LOG_ERR("AP enable failed: %d", ret);
		return ret;
	}

	LOG_INF("setup AP started: %s / %s", PORTAL_AP_SSID, PORTAL_AP_IP);
	return 0;
}

static void wifi_stop_ap(void)
{
	struct net_if *iface = portal_iface();
	if (iface != NULL) {
		(void)net_mgmt(NET_REQUEST_WIFI_AP_DISABLE, iface, NULL, 0);
	}
}

static int wifi_scan_blocking(void)
{
	struct net_if *iface = portal_iface();
	if (iface == NULL) {
		return -ENODEV;
	}

	scan_count = 0;
	k_sem_reset(&scan_done_sem);

	int ret = net_mgmt(NET_REQUEST_WIFI_SCAN, iface, NULL, 0);
	if (ret != 0) {
		return ret;
	}

	ret = k_sem_take(&scan_done_sem, K_SECONDS(12));
	return ret;
}

static int wifi_connect_blocking(const char *ssid, const char *pass)
{
	struct net_if *iface = portal_iface();
	if (iface == NULL) {
		return -ENODEV;
	}

	struct wifi_connect_req_params cnx = { 0 };
	cnx.ssid = (const uint8_t *)ssid;
	cnx.ssid_length = strlen(ssid);
	cnx.psk = (const uint8_t *)pass;
	cnx.psk_length = strlen(pass);
	cnx.security = strlen(pass) > 0 ? WIFI_SECURITY_TYPE_PSK : WIFI_SECURITY_TYPE_NONE;
	cnx.channel = 0;

	sta_connected = false;
	k_sem_reset(&connect_done_sem);

	int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &cnx, sizeof(cnx));
	if (ret != 0) {
		return ret;
	}

	ret = k_sem_take(&connect_done_sem, K_SECONDS(25));
	if (ret != 0) {
		return ret;
	}

	return sta_connected ? 0 : -ECONNREFUSED;
}

static void page_start(char *buf, size_t cap, size_t *off, const char *title)
{
	append(buf, cap, off,
		"<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>%s</title>"
		"<style>body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;background:#050b16;color:#d8e6f7;margin:0;padding:18px}.wrap{max-width:480px;margin:auto}.card{background:#0d1727;border:1px solid #1b2d4a;border-radius:17px;padding:18px;margin:0 0 14px}h1,h2{color:white}.muted,p{color:#7890ad;line-height:1.45}.title{font-size:11px;text-transform:uppercase;letter-spacing:.12em;color:#7890ad;font-weight:900}button,.btn,input{display:block;width:100%%;box-sizing:border-box;border-radius:11px;border:0;padding:14px;margin:8px 0;font:inherit;font-weight:800;text-decoration:none;text-align:center}button,.primary{background:#00b8f0;color:#001018}.ghost{background:#ffffff05;color:#9fb0c8;border:1px solid #1b2d4a}.danger{background:#ff5c5c14;color:#ff5c5c;border:1px solid #ff5c5c3d}input{background:#07101f;color:#d8e6f7;border:1px solid #1b2d4a}.row{display:flex;justify-content:space-between;border-bottom:1px solid #fff1;padding:6px 0;gap:12px}.good{color:#00d97e}.bad{color:#ff5c5c}.bar{height:10px;border-radius:99px;background:#26344f;overflow:hidden}.fill{height:100%%;background:#00d97e}.ip{font-size:20px;color:#00d97e;font-weight:900;word-break:break-all}</style></head><body><div class='wrap'><div class='card'><h1>mmWave Sensor</h1><div class='muted'>Wi-Fi setup</div></div>",
		title);
}

static void page_end(char *buf, size_t cap, size_t *off)
{
	append(buf, cap, off, "</div></body></html>");
}

static void status_card(char *buf, size_t cap, size_t *off)
{
	append(buf, cap, off,
		"<div class='card'><div class='title'>Device Status</div>"
		"<div class='row'><span>Setup Wi-Fi</span><b>%s</b></div>"
		"<div class='row'><span>Setup IP</span><b>%s</b></div>"
		"<div class='row'><span>Local Wi-Fi</span><b class='%s'>%s</b></div>"
		"<div class='row'><span>Saved network</span><b>%s</b></div>",
		PORTAL_AP_SSID, PORTAL_AP_IP,
		sta_connected ? "good" : "bad",
		sta_connected ? "connected" : "not connected",
		saved_ssid[0] ? saved_ssid : "none");
	if (sta_connected) {
		append(buf, cap, off, "<div class='row'><span>Dashboard</span><b class='good'>%s.local</b></div>", device_host);
	}
	append(buf, cap, off, "</div>");
}

static void bottom_actions(char *buf, size_t cap, size_t *off)
{
	if (!sta_connected && saved_ssid[0] == '\0') {
		return;
	}
	append(buf, cap, off, "<div class='card'><div class='title'>Settings</div><p class='muted'>Only use this if you want to change Wi-Fi.</p><form method='POST' action='/forget'><button class='danger'>Reset saved Wi-Fi</button></form></div>");
}

static void render_setup(char *buf, size_t cap)
{
	size_t off = 0;
	page_start(buf, cap, &off, "Setup");
	status_card(buf, cap, &off);
	append(buf, cap, &off, "<div class='card'><div class='title'>Step 1 of 3</div><h2>Choose Wi-Fi</h2><p>Pick the Wi-Fi network for this sensor.</p><form method='GET' action='/scan'><button>Scan networks</button></form><a class='btn ghost' href='/manual'>Type network manually</a></div>");
	bottom_actions(buf, cap, &off);
	page_end(buf, cap, &off);
}

static void render_manual(char *buf, size_t cap, const char *error)
{
	size_t off = 0;
	page_start(buf, cap, &off, "Manual Wi-Fi");
	status_card(buf, cap, &off);
	append(buf, cap, &off, "<div class='card'><div class='title'>Step 2 of 3</div><h2>Enter Wi-Fi</h2>");
	if (error != NULL) {
		append(buf, cap, &off, "<p class='bad'>%s</p>", error);
	}
	append(buf, cap, &off, "<form method='POST' action='/connect'><input name='ssid' placeholder='Wi-Fi network name'><input name='pass' type='password' placeholder='Wi-Fi password'><button>Connect</button></form><a class='btn ghost' href='/'>Back</a></div>");
	bottom_actions(buf, cap, &off);
	page_end(buf, cap, &off);
}

static void render_scan(char *buf, size_t cap)
{
	(void)wifi_scan_blocking();
	size_t off = 0;
	page_start(buf, cap, &off, "Scan");
	status_card(buf, cap, &off);
	append(buf, cap, &off, "<div class='card'><div class='title'>Step 1 of 3</div><h2>Choose Wi-Fi</h2>");
	if (scan_count == 0) {
		append(buf, cap, &off, "<p class='bad'>No networks found. Try again or enter it manually.</p><form method='GET' action='/scan'><button>Scan again</button></form><a class='btn ghost' href='/manual'>Type network manually</a></div>");
	} else {
		for (int i = 0; i < scan_count; i++) {
			char esc[96];
			html_escape(scan_results[i].ssid, esc, sizeof(esc));
			append(buf, cap, &off, "<form method='GET' action='/pick'><input type='hidden' name='ssid' value='%s'><button><b>%s</b> %s<br><span class='muted'>%s %d dBm</span><div class='bar'><div class='fill' style='width:%d%%'></div></div></button></form>", esc, esc, scan_results[i].secure ? "&#128274;" : "&nbsp;", signal_label(scan_results[i].rssi), scan_results[i].rssi, signal_percent(scan_results[i].rssi));
		}
		append(buf, cap, &off, "<a class='btn ghost' href='/manual'>Type network manually</a></div>");
	}
	bottom_actions(buf, cap, &off);
	page_end(buf, cap, &off);
}

static void render_pick(char *buf, size_t cap, const char *ssid, const char *error)
{
	char esc[96];
	html_escape(ssid, esc, sizeof(esc));
	size_t off = 0;
	page_start(buf, cap, &off, "Password");
	status_card(buf, cap, &off);
	append(buf, cap, &off, "<div class='card'><div class='title'>Step 2 of 3</div><h2>Enter password</h2><p><b style='color:white'>%s</b></p>", esc);
	if (error != NULL) {
		append(buf, cap, &off, "<p class='bad'>%s</p>", error);
	}
	append(buf, cap, &off, "<form method='POST' action='/connect'><input type='hidden' name='ssid' value='%s'><input name='pass' type='password' placeholder='Wi-Fi password'><button>Connect</button></form><a class='btn ghost' href='/scan'>Choose different network</a></div>", esc);
	bottom_actions(buf, cap, &off);
	page_end(buf, cap, &off);
}

static void render_success(char *buf, size_t cap, const char *ssid)
{
	char esc[96];
	html_escape(ssid, esc, sizeof(esc));
	size_t off = 0;
	page_start(buf, cap, &off, "Connected");
	status_card(buf, cap, &off);
	append(buf, cap, &off, "<div class='card'><div class='title'>Step 3 of 3</div><h2>Connected</h2><p>The sensor joined <b style='color:white'>%s</b>.</p><p>Dashboard:</p><div class='ip'>%s</div><p class='muted'>If .local does not resolve, check the router client list or serial log for the DHCP IP.</p><form method='POST' action='/handoff'><button>Go to dashboard</button></form></div>", esc, dashboard_url);
	bottom_actions(buf, cap, &off);
	page_end(buf, cap, &off);
}

static void render_dashboard(char *buf, size_t cap)
{
	size_t off = 0;
	page_start(buf, cap, &off, "Dashboard");
	status_card(buf, cap, &off);
	append(buf, cap, &off, "<div class='card'><div class='title'>Dashboard</div><h2>Sensor ready</h2><p class='good'>Connected on local Wi-Fi.</p><p>Sensor data will appear here next.</p></div>");
	bottom_actions(buf, cap, &off);
	page_end(buf, cap, &off);
}

static void send_html(int client, const char *body)
{
	char header[192];
	int len = strlen(body);
	int hlen = snprintk(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: %d\r\n\r\n", len);
	(void)zsock_send(client, header, hlen, 0);
	(void)zsock_send(client, body, len, 0);
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
	(void)sscanf(req, "%7s %255s", method, path);
	char *body = strstr(req, "\r\n\r\n");
	body = body ? body + 4 : (char *)"";

	if (strcmp(path, "/scan") == 0) {
		render_scan(page, sizeof(page));
	} else if (strncmp(path, "/pick?", 6) == 0) {
		char ssid[PORTAL_SSID_MAX + 1];
		form_value(path + 6, "ssid", ssid, sizeof(ssid));
		render_pick(page, sizeof(page), ssid, NULL);
	} else if (strcmp(path, "/manual") == 0) {
		render_manual(page, sizeof(page), NULL);
	} else if (strcmp(path, "/connect") == 0 && strcmp(method, "POST") == 0) {
		char ssid[PORTAL_SSID_MAX + 1];
		char pass[PORTAL_PASS_MAX + 1];
		form_value(body, "ssid", ssid, sizeof(ssid));
		form_value(body, "pass", pass, sizeof(pass));
		if (ssid[0] == '\0') {
			render_manual(page, sizeof(page), "Missing network name.");
		} else if (wifi_connect_blocking(ssid, pass) == 0) {
			credentials_save(ssid, pass);
			render_success(page, sizeof(page), ssid);
		} else {
			render_pick(page, sizeof(page), ssid, "Connection failed. Check the password or move closer to the router.");
		}
	} else if (strcmp(path, "/handoff") == 0 && strcmp(method, "POST") == 0) {
		handoff_pending = true;
		size_t off = 0;
		page_start(page, sizeof(page), &off, "Opening dashboard");
		append(page, sizeof(page), &off, "<meta http-equiv='refresh' content='30;url=%s'><div class='card'><div class='title'>Opening dashboard</div><h2>Switching Wi-Fi</h2><p>This can take up to <b style='color:white'>30 seconds</b>.</p><div class='ip'>%s</div><a class='btn primary' href='%s'>Open dashboard now</a></div>", dashboard_url, dashboard_url, dashboard_url);
		page_end(page, sizeof(page), &off);
	} else if (strcmp(path, "/forget") == 0 && strcmp(method, "POST") == 0) {
		credentials_clear();
		render_setup(page, sizeof(page));
	} else if (strcmp(path, "/dashboard") == 0 && sta_connected) {
		render_dashboard(page, sizeof(page));
	} else {
		render_setup(page, sizeof(page));
	}

	send_html(client, page);
}

static void http_thread(void)
{
	int server_fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (server_fd < 0) {
		LOG_ERR("http socket failed: %d", errno);
		return;
	}

	int opt = 1;
	(void)zsock_setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	struct sockaddr_in addr = { 0 };
	addr.sin_family = AF_INET;
	addr.sin_port = htons(PORTAL_HTTP_PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (zsock_bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		LOG_ERR("http bind failed: %d", errno);
		return;
	}

	if (zsock_listen(server_fd, 4) < 0) {
		LOG_ERR("http listen failed: %d", errno);
		return;
	}

	LOG_INF("HTTP server listening on port %d", PORTAL_HTTP_PORT);

	while (true) {
		int client = zsock_accept(server_fd, NULL, NULL);
		if (client >= 0) {
			handle_http_client(client);
			(void)zsock_close(client);
		}
	}
}

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
		return;
	}

	LOG_INF("DNS captive responder listening on port %d", PORTAL_DNS_PORT);

	uint8_t query[512];
	uint8_t reply[576];

	while (true) {
		struct sockaddr_in peer;
		socklen_t peer_len = sizeof(peer);
		int len = zsock_recvfrom(fd, query, sizeof(query), 0, (struct sockaddr *)&peer, &peer_len);
		if (len < 12 || len + 16 > (int)sizeof(reply)) {
			continue;
		}

		memcpy(reply, query, len);
		reply[2] = 0x81;
		reply[3] = 0x80;
		reply[6] = 0x00;
		reply[7] = 0x01;

		int off = len;
		const uint8_t answer[] = { 0xc0, 0x0c, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x04, 192, 168, 4, 1 };
		memcpy(reply + off, answer, sizeof(answer));
		off += sizeof(answer);
		(void)zsock_sendto(fd, reply, off, 0, (struct sockaddr *)&peer, peer_len);
	}
}

K_THREAD_DEFINE(http_tid, 8192, http_thread, NULL, NULL, NULL, 5, 0, 0);
K_THREAD_DEFINE(dns_tid, 4096, dns_thread, NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
	LOG_INF("Local Device Portal Zephyr MVP starting");

	wifi_init();
	make_device_host();
	credentials_init();

	LOG_INF("setup URL: http://%s/", PORTAL_AP_IP);
	LOG_INF("dashboard URL target: %s", dashboard_url);

	int ret = wifi_start_ap();
	if (ret != 0) {
		LOG_ERR("setup AP failed: %d", ret);
	}

	if (saved_ssid[0] != '\0') {
		LOG_INF("saved network found: %s", saved_ssid);
		if (wifi_connect_blocking(saved_ssid, saved_pass) == 0) {
			LOG_INF("connected to saved Wi-Fi");
		}
	}

	while (true) {
		if (handoff_pending) {
			handoff_pending = false;
			k_sleep(K_MSEC(1800));
			wifi_stop_ap();
		}
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
