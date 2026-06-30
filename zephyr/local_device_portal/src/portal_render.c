#include "portal_render.h"

#include "credential_store.h"
#include "portal_config.h"
#include "portal_state.h"
#include "wifi_manager.h"

#include <zephyr/net/net_ip.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

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

static void page_header(char *buf, size_t cap, size_t *off, const char *title)
{
	page_append(buf, cap, off,
		"<!doctype html><html><head><meta charset='utf-8'>"
		"<meta name='viewport' content='width=device-width,initial-scale=1'>"
		"<title>%s</title>"
		"<style>body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;background:radial-gradient(circle at top left,#00b8f024,transparent 30rem),radial-gradient(circle at bottom right,#00d97e12,transparent 28rem),#050b16;color:#d8e6f7;margin:0;padding:18px}"
		".wrap{max-width:480px;margin:auto}.card{background:linear-gradient(180deg,#101d32f5,#0d1727f5);border:1px solid #1b2d4a;border-radius:17px;padding:18px;margin:0 0 14px;box-shadow:0 18px 50px #0005}"
		"h1{font-size:22px;margin:0 0 4px;color:white}h2{font-size:18px;margin:0 0 8px;color:white}.muted,p{color:#7890ad;line-height:1.45}.title{font-size:11px;text-transform:uppercase;letter-spacing:.12em;color:#7890ad;font-weight:900;margin-bottom:10px}"
		"button,.btn,input{display:block;width:100%%;box-sizing:border-box;border-radius:11px;border:0;padding:14px;margin:8px 0;font:inherit;font-weight:800;text-decoration:none;text-align:center}"
		"button,.primary{background:linear-gradient(135deg,#00b8f0,#46d8ff);color:#001018}.ghost{background:#ffffff05;color:#9fb0c8;border:1px solid #1b2d4a}.danger{background:#ff5c5c14;color:#ff5c5c;border:1px solid #ff5c5c3d}"
		"input{background:#07101f;color:#d8e6f7;border:1px solid #1b2d4a;font-weight:500}.row{display:flex;justify-content:space-between;border-bottom:1px solid #fff1;padding:6px 0;gap:12px}.good{color:#00d97e}.bad{color:#ff5c5c}.net{text-align:left;background:#07101f;color:#d8e6f7;border:1px solid #1b2d4a}.bar{height:10px;border-radius:99px;background:#26344f;overflow:hidden}.fill{height:100%%;border-radius:99px;background:linear-gradient(90deg,#00b8f0,#00d97e)}"
		".steps{display:flex;align-items:center;margin:12px 0}.dot{width:28px;height:28px;border-radius:50%%;display:flex;align-items:center;justify-content:center;border:2px solid #274262;color:#7890ad;font-weight:900}.dot.on{border-color:#00b8f0;color:#00b8f0;background:#00b8f014}.dot.done{border-color:#00d97e;background:#00d97e;color:#00140b}.line{flex:1;height:2px;background:#274262}.line.done{background:#00d97e}.msg{border:1px solid #1b2d4a;border-radius:12px;padding:12px;background:#081325;color:#9fb0c8}.ip{font-size:20px;color:#00d97e;font-weight:900;word-break:break-all}.host{font-size:18px;color:#fff;font-weight:900;word-break:break-all}.small{font-size:12px;color:#7890ad}.spin{width:52px;height:52px;border-radius:50%%;border:4px solid #ffffff14;border-top-color:#00b8f0;border-right-color:#00d97e;animation:spin .85s linear infinite;margin:18px auto}@keyframes spin{to{transform:rotate(360deg)}}"
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
		portal_state_ap_ssid(), PORTAL_AP_IP,
		wifi_manager_sta_connected() ? "good" : "bad",
		wifi_manager_sta_connected() ? "connected" : "not connected",
		credential_store_has_ssid() ? credential_store_ssid() : "none");

	if (wifi_manager_sta_connected()) {
		page_append(buf, cap, off,
			"<div class='row'><span>Dashboard</span><b class='good'>%s.local</b></div>",
			portal_state_host());
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
	if (!wifi_manager_sta_connected() && !credential_store_has_ssid()) {
		return;
	}

	page_append(buf, cap, off,
		"<div class='card'><div class='title'>Settings</div><p class='muted'>Only use this if you want to change Wi-Fi.</p>"
		"<form method='POST' action='/forget'><button class='danger'>Reset saved Wi-Fi</button></form></div>");
}

void portal_render_setup(char *buf, size_t cap)
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

void portal_render_manual(char *buf, size_t cap, const char *error)
{
	size_t off = 0;

	page_header(buf, cap, &off, "Manual Wi-Fi");
	status_card(buf, cap, &off);
	steps(buf, cap, &off, 2);
	page_append(buf, cap, &off,
		"<div class='card'><div class='title'>Step 2 of 3</div><h2>Enter Wi-Fi</h2>"
		"<p>Type the network name and password.</p>");
	if (error != NULL && error[0]) {
		page_append(buf, cap, &off, "<p class='bad'>%s</p>", error);
	}
	page_append(buf, cap, &off,
		"<form method='POST' action='/connect'><input name='ssid' placeholder='Wi-Fi network name' autocomplete='off'>"
		"<input name='pass' type='password' placeholder='Wi-Fi password' autocomplete='current-password'>"
		"<button>Connect</button></form><a class='btn ghost' href='/'>Back</a></div>");
	bottom_actions(buf, cap, &off);
	page_footer(buf, cap, &off);
}

void portal_render_scan(char *buf, size_t cap)
{
	int ret = wifi_manager_scan_start();
	size_t off = 0;

	page_header(buf, cap, &off, "Scanning");
	page_append(buf, cap, &off, "<meta http-equiv='refresh' content='4;url=/scan-results'>");
	status_card(buf, cap, &off);
	steps(buf, cap, &off, 1);
	page_append(buf, cap, &off,
		"<div class='card'><div class='title'>Step 1 of 3</div><h2>Scanning networks</h2>"
		"<div class='spin'></div>");

	if (ret == -EALREADY) {
		page_append(buf, cap, &off,
			"<p>A scan is already running. Results will refresh when it finishes.</p>");
	} else if (ret != 0) {
		page_append(buf, cap, &off,
			"<p class='bad'>Could not start Wi-Fi scan (%d).</p>", ret);
	} else {
		page_append(buf, cap, &off,
			"<p>Refreshing the network list. Your device may briefly pause while the ESP32 radio scans.</p>");
	}

	page_append(buf, cap, &off,
		"<a class='btn primary' href='/scan-results'>Show results</a>"
		"<a class='btn ghost' href='/manual'>Type network manually</a></div>");
	bottom_actions(buf, cap, &off);
	page_footer(buf, cap, &off);
}

void portal_render_scan_results(char *buf, size_t cap)
{
	enum portal_scan_state state = wifi_manager_scan_state();
	int scan_count = wifi_manager_scan_count();
	const struct portal_net *scan_results = wifi_manager_scan_results();
	size_t off = 0;

	page_header(buf, cap, &off, "Scan Results");
	status_card(buf, cap, &off);
	steps(buf, cap, &off, 1);
	page_append(buf, cap, &off,
		"<div class='card'><div class='title'>Step 1 of 3</div><h2>Choose Wi-Fi</h2>");

	if (state == PORTAL_SCAN_IDLE) {
		page_append(buf, cap, &off,
			"<p>No scan has run yet.</p>"
			"<form method='GET' action='/scan'><button>Scan networks</button></form>"
			"<a class='btn ghost' href='/manual'>Type network manually</a></div>");
		bottom_actions(buf, cap, &off);
		page_footer(buf, cap, &off);
		return;
	}

	if (state == PORTAL_SCAN_RUNNING) {
		page_append(buf, cap, &off,
			"<meta http-equiv='refresh' content='3;url=/scan-results'>"
			"<div class='spin'></div><p>Still scanning. This page will refresh.</p>"
			"<a class='btn ghost' href='/manual'>Type network manually</a></div>");
		bottom_actions(buf, cap, &off);
		page_footer(buf, cap, &off);
		return;
	}

	if (state == PORTAL_SCAN_FAILED) {
		page_append(buf, cap, &off,
			"<p class='bad'>Wi-Fi scan failed (%d).</p>"
			"<form method='GET' action='/scan'><button>Scan again</button></form>"
			"<a class='btn ghost' href='/manual'>Type network manually</a></div>",
			wifi_manager_scan_last_error());
		bottom_actions(buf, cap, &off);
		page_footer(buf, cap, &off);
		return;
	}

	if (scan_count <= 0) {
		page_append(buf, cap, &off,
			"<p class='bad'>No networks found.</p>"
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

void portal_render_pick(char *buf, size_t cap, const char *ssid,
			const char *error)
{
	char esc[96];
	html_escape(ssid, esc, sizeof(esc));

	size_t off = 0;
	page_header(buf, cap, &off, "Password");
	status_card(buf, cap, &off);
	steps(buf, cap, &off, 2);
	page_append(buf, cap, &off,
		"<div class='card'><div class='title'>Step 2 of 3</div><h2>Enter password</h2><p><b style='color:white'>%s</b></p>",
		esc);
	if (error != NULL && error[0]) {
		page_append(buf, cap, &off, "<p class='bad'>%s</p>", error);
	}
	page_append(buf, cap, &off,
		"<form method='POST' action='/connect'><input type='hidden' name='ssid' value='%s'>"
		"<input name='pass' type='password' placeholder='Wi-Fi password'>"
		"<button>Connect</button></form><a class='btn ghost' href='/scan'>Choose different network</a></div>",
		esc);
	bottom_actions(buf, cap, &off);
	page_footer(buf, cap, &off);
}

void portal_render_success(char *buf, size_t cap, const char *ssid)
{
	char esc[96];
	char ip[NET_IPV4_ADDR_LEN];
	html_escape(ssid, esc, sizeof(esc));
	wifi_manager_local_ip(ip, sizeof(ip));

	size_t off = 0;
	page_header(buf, cap, &off, "Connected");
	status_card(buf, cap, &off);
	steps(buf, cap, &off, 3);
	page_append(buf, cap, &off,
		"<div class='card'><div class='title'>Step 3 of 3</div><h2>Connected</h2>"
		"<p>The sensor joined <b style='color:white'>%s</b>.</p>"
		"<p>Dashboard:</p><div class='host'>%s</div><p class='small'>Backup IP: %s</p>"
		"<p>Click below to close setup Wi-Fi. Then reconnect to your normal Wi-Fi and open the dashboard.</p>"
		"<form method='POST' action='/handoff'><button>Go to dashboard</button></form></div>",
		esc, portal_state_dashboard_url(), ip[0] ? ip : "check serial");
	bottom_actions(buf, cap, &off);
	page_footer(buf, cap, &off);
}

void portal_render_handoff(char *buf, size_t cap)
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
		portal_state_dashboard_url(), portal_state_dashboard_url(),
		portal_state_dashboard_url());
	page_footer(buf, cap, &off);
}

void portal_render_dashboard(char *buf, size_t cap)
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

void portal_render_reset(char *buf, size_t cap)
{
	size_t off = 0;

	page_header(buf, cap, &off, "Reset");
	page_append(buf, cap, &off,
		"<div class='card'><div class='title'>Reset Wi-Fi</div><h2>Saved Wi-Fi cleared</h2>"
		"<div class='spin'></div><p>Reconnect to <b style='color:white'>%s</b>, then open <b>%s</b>.</p></div>",
		portal_state_ap_ssid(), PORTAL_AP_IP);
	page_footer(buf, cap, &off);
}
