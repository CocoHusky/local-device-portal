// Local Device Portal — Zephyr MVP
// Captive-portal-style Wi-Fi setup and local dashboard for ESP32-C6 targets.

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/settings/settings.h>

#if defined(CONFIG_NET_DHCPV4_SERVER)
#include <zephyr/net/dhcpv4_server.h>
#endif

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(local_device_portal, LOG_LEVEL_INF);

#define PORTAL_AP_SSID      "mmWave-Setup"
#define PORTAL_AP_PASS      "focusfetch"
#define PORTAL_AP_IP        "192.168.4.1"
#define PORTAL_AP_MASK      "255.255.255.0"
#define PORTAL_HTTP_PORT    80
#define PORTAL_DNS_PORT     53
#define PORTAL_MAX_NETS     24
#define PORTAL_SSID_MAX     32
#define PORTAL_PASS_MAX     64
#define PORTAL_REQ_MAX      2048
#define PORTAL_PAGE_MAX     12288
#define PORTAL_HOST_MAX     32

struct portal_net {
	char ssid[PORTAL_SSID_MAX + 1];
	int rssi;
	bool secure;
};

static struct portal_net scan_results[PORTAL_MAX_NETS];
static int scan_count;
static bool scan_running;
static int scan_status;

static char saved_ssid[PORTAL_SSID_MAX + 1];
static char saved_pass[PORTAL_PASS_MAX + 1];
static char device_host[PORTAL_HOST_MAX];
static char dashboard_url[64];
static bool sta_connected;
static bool ap_started;
static bool handoff_pending;

static K_SEM_DEFINE(scan_done_sem, 0, 1);
static K_SEM_DEFINE(connect_done_sem, 0, 1);
static struct net_mgmt_event_callback wifi_cb;

static void page_append(char *buf, size_t cap, size_t *off, const char *fmt, ...)
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
			size_t rlen = strlen(rep);
			if (o + rlen >= out_len) {
				break;
			}
			memcpy(out + o, rep, rlen);
			o += rlen;
		} else {
			out[o++] = in[i];
		}
	}

	out[o] = '\0';
}

static int signal_percent(int rssi)
{
	int pct = 2 * (rssi + 100);

	if (pct < 0) {
		pct = 0;
	}
	if (pct > 100) {
		pct = 100;
	}

	return pct;
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

static void make_device_host(void)
{
	uint32_t cycles = k_cycle_get_32();
	snprintk(device_host, sizeof(device_host), "mmwave-%04x", (unsigned int)(cycles & 0xffff));
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
	if (ret && ret != -EALREADY) {
		LOG_WRN("settings init failed: %d", ret);
	}

	ret = settings_load_subtree("portal");
	if (ret) {
		LOG_WRN("settings load failed: %d", ret);
	}
}

static void credentials_save(const char *ssid, const char *pass)
{
	strncpy(saved_ssid, ssid, sizeof(saved_ssid) - 1);
	saved_ssid[sizeof(saved_ssid) - 1] = '\0';
	strncpy(saved_pass, pass, sizeof(saved_pass) - 1);
	saved_pass[sizeof(saved_pass) - 1] = '\0';

	settings_save_one("portal/ssid", saved_ssid, strlen(saved_ssid) + 1);
	settings_save_one("portal/pass", saved_pass, strlen(saved_pass) + 1);
}

static void credentials_clear(void)
{
	saved_ssid[0] = '\0';
	saved_pass[0] = '\0';
	settings_delete("portal/ssid");
	settings_delete("portal/pass");
}

static struct net_if *portal_iface(void)
{
	return net_if_get_default();
}

static void wifi_event_handler(struct net_mgmt_event_callback *cb,
			       uint64_t mgmt_event, struct net_if *iface)
{
	ARG_UNUSED(iface);

	if (mgmt_event == NET_EVENT_WIFI_SCAN_RESULT) {
		const struct wifi_scan_result *entry = cb->info;
		if (entry == NULL || scan_count >= PORTAL_MAX_NETS) {
			return;
		}

		int ssid_len = MIN((int)entry->ssid_length, PORTAL_SSID_MAX);
		if (ssid_len <= 0) {
			return;
		}

		for (int i = 0; i < scan_count; i++) {
			if (strlen(scan_results[i].ssid) == (size_t)ssid_len &&
			    memcmp(scan_results[i].ssid, entry->ssid, ssid_len) == 0) {
				if (entry->rssi > scan_results[i].rssi) {
					scan_results[i].rssi = entry->rssi;
					scan_results[i].secure = entry->security != WIFI_SECURITY_TYPE_NONE;
				}
				return;
			}
		}

		memcpy(scan_results[scan_count].ssid, entry->ssid, ssid_len);
		scan_results[scan_count].ssid[ssid_len] = '\0';
		scan_results[scan_count].rssi = entry->rssi;
		scan_results[scan_count].secure = entry->security != WIFI_SECURITY_TYPE_NONE;
		scan_count++;
		return;
	}

	if (mgmt_event == NET_EVENT_WIFI_SCAN_DONE) {
		scan_running = false;
		scan_status = 0;
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
		return;
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
		net_if_ipv4_addr_add(iface, &addr, NET_ADDR_MANUAL, 0);
#if defined(CONFIG_NET_DHCPV4_SERVER)
		net_dhcpv4_server_start(iface, &addr);
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
	if (ret) {
		LOG_ERR("AP enable failed: %d", ret);
		return ret;
	}

	ap_started = true;
	LOG_INF("setup AP started: %s / %s", PORTAL_AP_SSID, PORTAL_AP_IP);
	return 0;
}

static int wifi_stop_ap(void)
{
	struct net_if *iface = portal_iface();
	if (iface == NULL) {
		return -ENODEV;
	}

	int ret = net_mgmt(NET_REQUEST_WIFI_AP_DISABLE, iface, NULL, 0);
	if (ret) {
		LOG_WRN("AP disable failed: %d", ret);
	}
	ap_started = false;
	return ret;
}

static int wifi_scan_blocking(void)
{
	struct net_if *iface = portal_iface();
	if (iface == NULL) {
		return -ENODEV;
	}

	scan_count = 0;
	scan_running = true;
	scan_status = 0;
	k_sem_reset(&scan_done_sem);

	int ret = net_mgmt(NET_REQUEST_WIFI_SCAN, iface, NULL, 0);
	if (ret) {
		scan_running = false;
		scan_status = ret;
		return ret;
	}

	ret = k_sem_take(&scan_done_sem, K_SECONDS(12));
	if (ret) {
		scan_running = false;
		scan_status = ret;
		return ret;
	}

	for (int i = 0; i < scan_count; i++) {
		for (int j = i + 1; j < scan_count; j++) {
			if (scan_results[j].rssi > scan_results[i].rssi) {
				struct portal_net tmp = scan_results[i];
				scan_results[i] = scan_results[j];
				scan_results[j] = tmp;
			}
		}
	}

	return 0;
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
	cnx.security = strlen(pass) ? WIFI_SECURITY_TYPE_PSK : WIFI_SECURITY_TYPE_NONE;
	cnx.channel = 0;

	k_sem_reset(&connect_done_sem);
	sta_connected = false;

	int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &cnx, sizeof(cnx));
	if (ret) {
		return ret;
	}

	ret = k_sem_take(&connect_done_sem, K_SECONDS(25));
	if (ret) {
		return ret;
	}

	return sta_connected ? 0 : -ECONNREFUSED;
}

static void page_header(char *buf, size_t cap, size_t *off, const char *title)
{
	page_append(buf, cap, off,
		"<!doctype html><html><head><meta charset='utf-8'>"
		"<meta name='viewport' content='width=device-width,initial-scale=1'>"
		"<title>%s</title>"
		"<style>body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;background:#050b16;color:#d8e6f7;margin:0;padding:18px}"
		".wrap{max-width:480px;margin:auto}.card{background:#0d1727;border:1px solid #1b2d4a;border-radius:17px;padding:18px;margin:0 0 14px;box-shadow:0 18px 50px #0005}"
		"h1{font-size:22px;margin:0 0 4px;color:white}h2{font-size:18px;margin:0 0 8px;color:white}.muted,p{color:#7890ad;line-height:1.45}.title{font-size:11px;text-transform:uppercase;letter-spacing:.12em;color:#7890ad;font-weight:900;margin-bottom:10px}"
		"button,.btn,input{display:block;width:100%%;box-sizing:border-box;border-radius:11px;border:0;padding:14px;margin:8px 0;font:inherit;font-weight:800;text-decoration:none;text-align:center}"
		"button,.primary{background:linear-gradient(135deg,#00b8f0,#46d8ff);color:#001018}.ghost{background:#ffffff05;color:#9fb0c8;border:1px solid #1b2d4a}.danger{background:#ff5c5c14;color:#ff5c5c;border:1px solid #ff5c5c3d}"
		"input{background:#07101f;color:#d8e6f7;border:1px solid #1b2d4a;font-weight:500}.row{display:flex;justify-content:space-between;border-bottom:1px solid #fff1;padding:6px 0;gap:12px}.good{color:#00d97e}.bad{color:#ff5c5c}.net{text-align:left;background:#07101f;color:#d8e6f7;border:1px solid #1b2d4a}.bar{height:10px;border-radius:99px;background:#26344f;overflow:hidden}.fill{height:100%%;border-radius:99px;background:linear-gradient(90deg,#00b8f0,#00d97e)}"
		".steps{display:flex;align-items:center;margin:12px 0}.dot{width:28px;height:28px;border-radius:50%%;display:flex;align-items:center;justify-content:center;border:2px solid #274262;color:#7890ad;font-weight:900}.dot.on{border-color:#00b8f0;color:#00b8f0}.dot.done{border-color:#00d97e;background:#00d97e;color:#00140b}.line{flex:1;height:2px;background:#274262}.line.done{background:#00d97e}.ip{font-size:20px;color:#00d97e;font-weight:900;word-break:break-all}.spin{width:52px;height:52px;border-radius:50%%;border:4px solid #ffffff14;border-top-color:#00b8f0;border-right-color:#00d97e;animation:spin .85s linear infinite;margin:18px auto}@keyframes spin{to{transform:rotate(360deg)}}"
		"</style></head><body><div class='wrap'><div class='card'><h1>mmWave Sensor</h1><div class='muted'>Wi-Fi setup</div></div>",
		title);
}

static void page_footer(char *buf, size_t cap, size_t *off)
{
	page_append(buf, cap, off, "</div></body></html>");
}

static void status_card(char *buf, size_t cap, size_t *off)
{
	page_append(buf, cap, off,
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
		page_append(buf, cap, off,
			"<div class='row'><span>Dashboard</span><b class='good'>%s.local</b></div>",
			device_host);
	}

	page_append(buf, cap, off, "</div>");
}

static void steps(char *buf, size_t cap, size_t *off, int step)
{
	page_append(buf, cap, off,
		"<div class='steps'><div class='dot %s'>1</div><div class='line %s'></div>"
		"<div class='dot %s'>2</div><div class='line %s'></div><div class='dot %s'>3</div></div>",
		step == 1 ? "on" : "done",
		step >= 2 ? "done" : "",
		step == 2 ? "on" : (step > 2 ? "done" : ""),
		step >= 3 ? "done" : "",
		step == 3 ? "done" : "");
}

static void bottom_actions(char *buf, size_t cap, size_t *off)
{
	if (!sta_connected && saved_ssid[0] == '\0') {
		return;
	}

	page_append(buf, cap, off,
		"<div class='card'><div class='title'>Settings</div><p class='muted'>Only use this if you want to change Wi-Fi.</p>"
		"<form method='POST' action='/forget'><button class='danger'>Reset saved Wi-Fi</button></form></div>");
}

static void render_setup(char *buf, size_t cap)
{
	size_t off = 0;
	page_header(buf, cap, &off, "Setup");
	status_card(buf, cap, &off);
	steps(buf, cap, &off, 1);
	page_append(buf, cap, &off,
		"<div class='card'><div class='title'>Step 1 of 3</div><h2>Choose Wi-Fi</h2>"
		"<p>Pick the Wi-Fi network for this sensor.</p>"
		"<form method='GET' action='/scan'><button>Scan networks</button></form>"
		"<a class='btn ghost' href='/manual'>Type network manually</a></div>");
	bottom_actions(buf, cap, &off);
	page_footer(buf, cap, &off);
}

static void render_manual(char *buf, size_t cap, const char *error)
{
	size_t off = 0;
	page_header(buf, cap, &off, "Manual Wi-Fi");
	status_card(buf, cap, &off);
	steps(buf, cap, &off, 2);
	page_append(buf, cap, &off,
		"<div class='card'><div class='title'>Step 2 of 3</div><h2>Enter Wi-Fi</h2>");
	if (error != NULL && error[0]) {
		page_append(buf, cap, &off, "<p class='bad'>%s</p>", error);
	}
	page_append(buf, cap, &off,
		"<form method='POST' action='/connect'><input name='ssid' placeholder='Wi-Fi network name'>"
		"<input name='pass' type='password' placeholder='Wi-Fi password'>"
		"<button>Connect</button></form><a class='btn ghost' href='/'>Back</a></div>");
	bottom_actions(buf, cap, &off);
	page_footer(buf, cap, &off);
}

static void render_scan(char *buf, size_t cap)
{
	wifi_scan_blocking();

	size_t off = 0;
	page_header(buf, cap, &off, "Scan");
	status_card(buf, cap, &off);
	steps(buf, cap, &off, 1);
	page_append(buf, cap, &off,
		"<div class='card'><div class='title'>Step 1 of 3</div><h2>Choose Wi-Fi</h2>");

	if (scan_count <= 0) {
		page_append(buf, cap, &off,
			"<p class='bad'>No networks found. Try again or enter it manually.</p>"
			"<form method='GET' action='/scan'><button>Scan again</button></form>"
			"<a class='btn ghost' href='/manual'>Type network manually</a></div>");
		bottom_actions(buf, cap, &off);
		page_footer(buf, cap, &off);
		return;
	}

	page_append(buf, cap, &off, "<p>Tap your network.</p>");
	for (int i = 0; i < scan_count; i++) {
		char esc[96];
		html_escape(scan_results[i].ssid, esc, sizeof(esc));
		int pct = signal_percent(scan_results[i].rssi);
		page_append(buf, cap, &off,
			"<form method='GET' action='/pick'><input type='hidden' name='ssid' value='%s'>"
			"<button class='net'><div style='display:flex;justify-content:space-between;gap:10px'><b>%s</b><span>%s</span></div>"
			"<div class='muted' style='display:flex;justify-content:space-between'><span>%s</span><span>%d dBm</span></div>"
			"<div class='bar'><div class='fill' style='width:%d%%'></div></div></button></form>",
			esc, esc, scan_results[i].secure ? "&#128274;" : "&nbsp;",
			signal_label(scan_results[i].rssi), scan_results[i].rssi, pct);
	}
	page_append(buf, cap, &off,
		"<hr><form method='GET' action='/scan'><button class='ghost'>Scan again</button></form>"
		"<a class='btn ghost' href='/manual'>Type network manually</a></div>");
	bottom_actions(buf, cap, &off);
	page_footer(buf, cap, &off);
}

static void render_pick(char *buf, size_t cap, const char *ssid, const char *error)
{
	char esc[96];
	html_escape(ssid, esc, sizeof(esc));

	size_t off = 0;
	page_header(buf, cap, &off, "Password");
	status_card(buf, cap, &off);
	steps(buf, cap, &off, 2);
	page_append(buf, cap, &off,
		"<div class='card'><div class='title'>Step 2 of 3</div><h2>Enter password</h2><p><b style='color:white'>%s</b></p>", esc);
	if (error != NULL && error[0]) {
		page_append(buf, cap, &off, "<p class='bad'>%s</p>", error);
	}
	page_append(buf, cap, &off,
		"<form method='POST' action='/connect'><input type='hidden' name='ssid' value='%s'>"
		"<input name='pass' type='password' placeholder='Wi-Fi password'>"
		"<button>Connect</button></form><a class='btn ghost' href='/scan'>Choose different network</a></div>", esc);
	bottom_actions(buf, cap, &off);
	page_footer(buf, cap, &off);
}

static void render_success(char *buf, size_t cap, const char *ssid)
{
	char esc[96];
	html_escape(ssid, esc, sizeof(esc));

	struct net_if *iface = portal_iface();
	char ip[NET_IPV4_ADDR_LEN] = "";
	if (iface != NULL && iface->config.ip.ipv4 != NULL) {
		for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
			if (iface->config.ip.ipv4->unicast[i].is_used) {
				net_addr_ntop(AF_INET, &iface->config.ip.ipv4->unicast[i].address.in_addr,
					      ip, sizeof(ip));
				break;
			}
		}
	}

	size_t off = 0;
	page_header(buf, cap, &off, "Connected");
	status_card(buf, cap, &off);
	steps(buf, cap, &off, 3);
	page_append(buf, cap, &off,
		"<div class='card'><div class='title'>Step 3 of 3</div><h2>Connected</h2>"
		"<p>The sensor joined <b style='color:white'>%s</b>.</p>"
		"<p>Dashboard:</p><div class='ip'>%s</div><p class='muted'>Backup IP: %s</p>"
		"<form method='POST' action='/handoff'><button>Go to dashboard</button></form></div>",
		esc, dashboard_url, ip[0] ? ip : "check serial");
	bottom_actions(buf, cap, &off);
	page_footer(buf, cap, &off);
}

static void render_handoff(char *buf, size_t cap)
{
	size_t off = 0;
	page_header(buf, cap, &off, "Opening dashboard");
	page_append(buf, cap, &off,
		"<meta http-equiv='refresh' content='30;url=%s'>"
		"<div class='card'><div class='title'>Opening dashboard</div><h2>Switching Wi-Fi</h2>"
		"<div class='spin'></div><div class='ip' style='text-align:center;color:white'>30 sec</div>"
		"<p>The setup Wi-Fi is turning off. Your device should reconnect to your normal Wi-Fi, then open the dashboard.</p>"
		"<p>This can take up to <b style='color:white'>30 seconds</b>.</p>"
		"<p>Dashboard:</p><div class='ip'>%s</div><a class='btn primary' href='%s'>Open dashboard now</a></div>",
		dashboard_url, dashboard_url, dashboard_url);
	page_footer(buf, cap, &off);
}

static void render_dashboard(char *buf, size_t cap)
{
	size_t off = 0;
	page_header(buf, cap, &off, "Dashboard");
	status_card(buf, cap, &off);
	page_append(buf, cap, &off,
		"<div class='card'><div class='title'>Dashboard</div><h2>Sensor ready</h2>"
		"<p class='good'>Connected on local Wi-Fi.</p><p>Sensor data will appear here next.</p></div>");
	bottom_actions(buf, cap, &off);
	page_footer(buf, cap, &off);
}

static void render_reset(char *buf, size_t cap)
{
	size_t off = 0;
	page_header(buf, cap, &off, "Reset");
	page_append(buf, cap, &off,
		"<div class='card'><div class='title'>Reset Wi-Fi</div><h2>Saved Wi-Fi cleared</h2>"
		"<div class='spin'></div><p>Reconnect to <b style='color:white'>%s</b>, then open <b>%s</b>.</p></div>",
		PORTAL_AP_SSID, PORTAL_AP_IP);
	page_footer(buf, cap, &off);
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
		"{\"ap_ssid\":\"%s\",\"sta_connected\":%s,\"saved_ssid\":\"%s\",\"dashboard\":\"%s\"}",
		PORTAL_AP_SSID, sta_connected ? "true" : "false", saved_ssid, dashboard_url);

	char header[192];
	int len = strlen(body);
	int hlen = snprintk(header, sizeof(header),
		"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: %d\r\n\r\n", len);
	zsock_send(client, header, hlen, 0);
	zsock_send(client, body, len, 0);
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

	if (strcmp(path, "/scan") == 0) {
		render_scan(page, sizeof(page));
		send_response(client, page);
		return;
	}

	if (strncmp(path, "/pick?", 6) == 0) {
		char ssid[PORTAL_SSID_MAX + 1];
		form_value(path + 6, "ssid", ssid, sizeof(ssid));
		render_pick(page, sizeof(page), ssid, NULL);
		send_response(client, page);
		return;
	}

	if (strcmp(path, "/manual") == 0) {
		render_manual(page, sizeof(page), NULL);
		send_response(client, page);
		return;
	}

	if (strcmp(path, "/connect") == 0 && strcmp(method, "POST") == 0) {
		char ssid[PORTAL_SSID_MAX + 1];
		char pass[PORTAL_PASS_MAX + 1];
		form_value(body, "ssid", ssid, sizeof(ssid));
		form_value(body, "pass", pass, sizeof(pass));

		if (ssid[0] == '\0') {
			render_manual(page, sizeof(page), "Missing network name.");
			send_response(client, page);
			return;
		}

		if (wifi_connect_blocking(ssid, pass) == 0) {
			credentials_save(ssid, pass);
			render_success(page, sizeof(page), ssid);
		} else {
			render_pick(page, sizeof(page), ssid,
				"Connection failed. Check the password or move closer to the router.");
		}
		send_response(client, page);
		return;
	}

	if (strcmp(path, "/handoff") == 0 && strcmp(method, "POST") == 0) {
		handoff_pending = true;
		render_handoff(page, sizeof(page));
		send_response(client, page);
		return;
	}

	if (strcmp(path, "/forget") == 0 && strcmp(method, "POST") == 0) {
		credentials_clear();
		render_reset(page, sizeof(page));
		send_response(client, page);
		return;
	}

	if (strcmp(path, "/dashboard") == 0 || strstr(path, "generate_204") ||
	    strstr(path, "hotspot-detect") || strstr(path, "connecttest") ||
	    strstr(path, "ncsi") || strstr(path, "redirect")) {
		if (strcmp(path, "/dashboard") == 0 && sta_connected) {
			render_dashboard(page, sizeof(page));
		} else {
			render_setup(page, sizeof(page));
		}
		send_response(client, page);
		return;
	}

	render_setup(page, sizeof(page));
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

K_THREAD_DEFINE(http_tid, 8192, http_thread, NULL, NULL, NULL, 5, 0, 0);
K_THREAD_DEFINE(dns_tid, 4096, dns_thread, NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
	LOG_INF("Local Device Portal Zephyr MVP starting");

	make_device_host();
	credentials_init();
	wifi_init();

	LOG_INF("setup URL: http://%s/", PORTAL_AP_IP);
	LOG_INF("dashboard URL target: %s", dashboard_url);

	int ret = wifi_start_ap();
	if (ret) {
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
