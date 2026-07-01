#include "wifi_manager.h"

#include "portal_state.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/util.h>

#if defined(CONFIG_NET_DHCPV4_SERVER)
#include <zephyr/net/dhcpv4_server.h>
#endif

#include <errno.h>
#include <string.h>

LOG_MODULE_REGISTER(wifi_manager, LOG_LEVEL_INF);

#define MACSTR "%02X:%02X:%02X:%02X:%02X:%02X"

#define WIFI_EVENT_MASK \
	(NET_EVENT_WIFI_SCAN_RESULT | \
	 NET_EVENT_WIFI_SCAN_DONE | \
	 NET_EVENT_WIFI_CONNECT_RESULT | \
	 NET_EVENT_WIFI_DISCONNECT_RESULT | \
	 NET_EVENT_WIFI_AP_ENABLE_RESULT | \
	 NET_EVENT_WIFI_AP_DISABLE_RESULT | \
	 NET_EVENT_WIFI_AP_STA_CONNECTED | \
	 NET_EVENT_WIFI_AP_STA_DISCONNECTED)

static struct portal_net scan_results[PORTAL_MAX_NETS];
static int scan_count;
static enum portal_scan_state scan_state = PORTAL_SCAN_IDLE;
static int scan_last_error;
static bool ap_enabled;
static bool sta_connected;
static struct net_if *ap_net_iface;
static struct net_if *sta_net_iface;

static K_SEM_DEFINE(scan_request_sem, 0, 1);
static K_SEM_DEFINE(scan_done_sem, 0, 1);
static K_SEM_DEFINE(connect_done_sem, 0, 1);
static struct net_mgmt_event_callback wifi_cb;

static struct net_if *ap_iface(void)
{
	return ap_net_iface ? ap_net_iface : net_if_get_wifi_sap();
}

static struct net_if *sta_iface(void)
{
	return sta_net_iface ? sta_net_iface : net_if_get_wifi_sta();
}

static void log_wifi_ifaces(const char *stage)
{
	LOG_INF("%s interfaces: default=%p sta=%p sap=%p selected_sta=%p selected_ap=%p",
		stage,
		net_if_get_default(),
		net_if_get_wifi_sta(),
		net_if_get_wifi_sap(),
		sta_net_iface,
		ap_net_iface);
}

static void log_iface_ipv4(const char *label, struct net_if *iface)
{
	if (iface == NULL || iface->config.ip.ipv4 == NULL) {
		LOG_WRN("%s IPv4: interface unavailable", label);
		return;
	}

	for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
		if (iface->config.ip.ipv4->unicast[i].ipv4.is_used) {
			char ip[NET_IPV4_ADDR_LEN];
			net_addr_ntop(AF_INET,
				      &iface->config.ip.ipv4->unicast[i].ipv4.address.in_addr,
				      ip, sizeof(ip));
			LOG_INF("%s IPv4[%d]=%s iface=%p", label, i, ip, iface);
		}
	}
}

static void remember_scan_result(const struct wifi_scan_result *entry)
{
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
				scan_results[i].secure =
					entry->security != WIFI_SECURITY_TYPE_NONE;
			}
			return;
		}
	}

	memcpy(scan_results[scan_count].ssid, entry->ssid, ssid_len);
	scan_results[scan_count].ssid[ssid_len] = '\0';
	scan_results[scan_count].rssi = entry->rssi;
	scan_results[scan_count].secure = entry->security != WIFI_SECURITY_TYPE_NONE;
	scan_count++;
}

static void sort_scan_results(void)
{
	for (int i = 0; i < scan_count; i++) {
		for (int j = i + 1; j < scan_count; j++) {
			if (scan_results[j].rssi > scan_results[i].rssi) {
				struct portal_net tmp = scan_results[i];
				scan_results[i] = scan_results[j];
				scan_results[j] = tmp;
			}
		}
	}
}

static int scan_once_blocking(const char *label)
{
	struct net_if *iface = sta_iface();
	int ret;

	if (iface == NULL) {
		return -ENODEV;
	}

	scan_count = 0;
	k_sem_reset(&scan_done_sem);

	LOG_INF("Wi-Fi scan start: %s iface=%p ap_enabled=%s",
		label, iface, ap_enabled ? "yes" : "no");

	ret = net_mgmt(NET_REQUEST_WIFI_SCAN, iface, NULL, 0);
	if (ret != 0) {
		LOG_WRN("Wi-Fi scan request failed: %d", ret);
		return ret;
	}

	ret = k_sem_take(&scan_done_sem, K_SECONDS(12));
	if (ret != 0) {
		LOG_WRN("Wi-Fi scan timed out: %d", ret);
		return ret;
	}

	sort_scan_results();
	LOG_INF("Wi-Fi scan done: %d networks", scan_count);
	return 0;
}

static void scan_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		k_sem_take(&scan_request_sem, K_FOREVER);

		scan_state = PORTAL_SCAN_RUNNING;
		scan_last_error = 0;

		int ret = scan_once_blocking("button refresh");
		scan_last_error = ret;
		scan_state = (ret == 0) ? PORTAL_SCAN_DONE : PORTAL_SCAN_FAILED;
	}
}

K_THREAD_DEFINE(scan_tid, 4096, scan_thread, NULL, NULL, NULL, 7, 0, SYS_FOREVER_MS);

static int configure_ap_ipv4(struct net_if *iface)
{
	struct in_addr addr;
	struct in_addr mask;

	if (iface == NULL) {
		return -ENODEV;
	}

	if (net_addr_pton(AF_INET, PORTAL_AP_IP, &addr) != 0) {
		LOG_ERR("invalid setup AP IP: %s", PORTAL_AP_IP);
		return -EINVAL;
	}

	if (net_addr_pton(AF_INET, PORTAL_AP_MASK, &mask) != 0) {
		LOG_ERR("invalid setup AP netmask: %s", PORTAL_AP_MASK);
		return -EINVAL;
	}

	net_if_ipv4_set_gw(iface, &addr);

	if (net_if_ipv4_addr_add(iface, &addr, NET_ADDR_MANUAL, 0) == NULL) {
		LOG_WRN("setup AP IP may already be assigned on selected interface: %s", PORTAL_AP_IP);
	}

	if (!net_if_ipv4_set_netmask_by_addr(iface, &addr, &mask)) {
		LOG_ERR("setup AP netmask failed: %s", PORTAL_AP_MASK);
		return -EIO;
	}

	LOG_INF("setup AP IPv4 ready on interface %p: %s/%s", iface, PORTAL_AP_IP, PORTAL_AP_MASK);
	log_iface_ipv4("setup AP", iface);
	return 0;
}

static int start_ap_dhcp_server(struct net_if *iface)
{
#if defined(CONFIG_NET_DHCPV4_SERVER)
	struct in_addr dhcp_start;
	int ret;

	if (net_addr_pton(AF_INET, PORTAL_DHCP_START_IP, &dhcp_start) != 0) {
		LOG_ERR("invalid DHCP start IP: %s", PORTAL_DHCP_START_IP);
		return -EINVAL;
	}

	ret = net_dhcpv4_server_start(iface, &dhcp_start);
	if (ret != 0 && ret != -EALREADY) {
		LOG_ERR("DHCP server start failed: %d", ret);
		return ret;
	}

	LOG_INF("setup DHCP started: start=%s iface=%p", PORTAL_DHCP_START_IP, iface);
#else
	ARG_UNUSED(iface);
#endif

	return 0;
}

static void wifi_event_handler(struct net_mgmt_event_callback *cb,
			       uint64_t mgmt_event, struct net_if *iface)
{
	switch (mgmt_event) {
	case NET_EVENT_WIFI_SCAN_RESULT:
		remember_scan_result(cb->info);
		break;
	case NET_EVENT_WIFI_SCAN_DONE:
		k_sem_give(&scan_done_sem);
		break;
	case NET_EVENT_WIFI_CONNECT_RESULT: {
		const struct wifi_status *status = cb->info;

		sta_connected = (status == NULL || status->status == 0);
		LOG_INF("STA connect result: status=%d connected=%s iface=%p",
			status ? status->status : 0,
			sta_connected ? "yes" : "no",
			iface);
		k_sem_give(&connect_done_sem);
		break;
	}
	case NET_EVENT_WIFI_DISCONNECT_RESULT:
		sta_connected = false;
		LOG_INF("STA disconnected iface=%p", iface);
		break;
	case NET_EVENT_WIFI_AP_ENABLE_RESULT:
		ap_enabled = true;
		LOG_INF("AP mode is enabled; waiting for setup client to connect");
		break;
	case NET_EVENT_WIFI_AP_DISABLE_RESULT:
		ap_enabled = false;
		LOG_INF("AP mode is disabled");
		break;
	case NET_EVENT_WIFI_AP_STA_CONNECTED: {
		const struct wifi_ap_sta_info *sta_info = cb->info;

		if (sta_info != NULL) {
			LOG_INF("station: " MACSTR " joined",
				sta_info->mac[0], sta_info->mac[1], sta_info->mac[2],
				sta_info->mac[3], sta_info->mac[4], sta_info->mac[5]);
		}
		break;
	}
	case NET_EVENT_WIFI_AP_STA_DISCONNECTED: {
		const struct wifi_ap_sta_info *sta_info = cb->info;

		if (sta_info != NULL) {
			LOG_INF("station: " MACSTR " left",
				sta_info->mac[0], sta_info->mac[1], sta_info->mac[2],
				sta_info->mac[3], sta_info->mac[4], sta_info->mac[5]);
		}
		break;
	}
	default:
		break;
	}
}

void wifi_manager_init(void)
{
	net_mgmt_init_event_callback(&wifi_cb, wifi_event_handler, WIFI_EVENT_MASK);
	net_mgmt_add_event_callback(&wifi_cb);

	/* Match Zephyr samples/net/wifi/apsta_mode: keep AP and STA as separate
	 * AP-STA interfaces instead of falling back to the default interface.
	 */
	ap_net_iface = net_if_get_wifi_sap();
	sta_net_iface = net_if_get_wifi_sta();

	k_thread_start(scan_tid);
	log_wifi_ifaces("wifi init");
}

int wifi_manager_start_ap(void)
{
	struct wifi_connect_req_params ap = { 0 };
	int ret;

	log_wifi_ifaces("before AP start");

	ap_net_iface = net_if_get_wifi_sap();
	if (ap_net_iface == NULL) {
		LOG_ERR("AP interface is not initialized");
		return -ENODEV;
	}

	LOG_INF("Turning on setup AP mode");

	ap.ssid = (const uint8_t *)portal_state_ap_ssid();
	ap.ssid_length = strlen(portal_state_ap_ssid());
	ap.psk = (const uint8_t *)PORTAL_AP_PASS;
	ap.psk_length = strlen(PORTAL_AP_PASS);
	ap.channel = WIFI_CHANNEL_ANY;
	ap.band = WIFI_FREQ_BAND_2_4_GHZ;
	ap.security = strlen(PORTAL_AP_PASS) ? WIFI_SECURITY_TYPE_PSK : WIFI_SECURITY_TYPE_NONE;

	ret = configure_ap_ipv4(ap_net_iface);
	if (ret != 0) {
		return ret;
	}

	ret = start_ap_dhcp_server(ap_net_iface);
	if (ret != 0) {
		return ret;
	}

	ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, ap_net_iface, &ap,
		       sizeof(struct wifi_connect_req_params));
	if (ret != 0) {
		LOG_ERR("NET_REQUEST_WIFI_AP_ENABLE failed: %d", ret);
		return ret;
	}

	ap_enabled = true;
	LOG_INF("setup AP enabled: ssid=%s iface=%p",
		portal_state_ap_ssid(), ap_net_iface);
	LOG_INF("SETUP PORTAL READY: join %s, open http://%s/",
		portal_state_ap_ssid(), PORTAL_AP_IP);

	return 0;
}

int wifi_manager_bind_socket_to_ap(int fd)
{
	struct net_if *iface = ap_iface();
	struct net_ifreq ifreq = { 0 };
	int ret;

	if (iface == NULL) {
		return -ENODEV;
	}

	ret = net_if_get_name(iface, ifreq.ifr_name, sizeof(ifreq.ifr_name));
	if (ret < 0) {
		LOG_WRN("AP interface name unavailable: %d", ret);
		return ret;
	}

	ret = zsock_setsockopt(fd, ZSOCK_SOL_SOCKET, ZSOCK_SO_BINDTODEVICE,
			       &ifreq, sizeof(ifreq));
	if (ret < 0) {
		ret = -errno;
		LOG_WRN("AP socket bind-to-device failed: %d", ret);
		return ret;
	}

	LOG_INF("socket bound to AP interface: %s", ifreq.ifr_name);
	return 0;
}

int wifi_manager_stop_ap(void)
{
	struct net_if *iface = ap_iface();
	if (iface == NULL) {
		return -ENODEV;
	}

	int ret = net_mgmt(NET_REQUEST_WIFI_AP_DISABLE, iface, NULL, 0);
	if (ret != 0) {
		LOG_WRN("AP disable failed: %d", ret);
	} else {
		ap_enabled = false;
		ap_net_iface = NULL;
	}

	return ret;
}

int wifi_manager_scan_start(void)
{
	if (scan_state == PORTAL_SCAN_RUNNING) {
		return -EALREADY;
	}

	scan_state = PORTAL_SCAN_RUNNING;
	scan_last_error = 0;
	k_sem_give(&scan_request_sem);
	return 0;
}

int wifi_manager_scan_blocking(void)
{
	scan_state = PORTAL_SCAN_RUNNING;
	scan_last_error = 0;

	int ret = scan_once_blocking("blocking refresh");
	scan_last_error = ret;
	scan_state = (ret == 0) ? PORTAL_SCAN_DONE : PORTAL_SCAN_FAILED;
	return ret;
}

int wifi_manager_connect_blocking(const char *ssid, const char *pass)
{
	struct net_if *iface = sta_iface();
	int ret;

	if (iface == NULL) {
		LOG_ERR("STA interface is not initialized");
		return -ENODEV;
	}

	struct wifi_connect_req_params cnx = { 0 };
	cnx.ssid = (const uint8_t *)ssid;
	cnx.ssid_length = strlen(ssid);
	cnx.psk = (const uint8_t *)pass;
	cnx.psk_length = strlen(pass);
	cnx.security = strlen(pass) ? WIFI_SECURITY_TYPE_PSK : WIFI_SECURITY_TYPE_NONE;
	cnx.channel = WIFI_CHANNEL_ANY;
	cnx.band = WIFI_FREQ_BAND_2_4_GHZ;

	k_sem_reset(&connect_done_sem);
	sta_connected = false;

	LOG_INF("STA connect start: ssid=%s iface=%p", ssid, iface);
	ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &cnx,
		       sizeof(struct wifi_connect_req_params));
	if (ret != 0) {
		LOG_ERR("NET_REQUEST_WIFI_CONNECT failed: %d", ret);
		return ret;
	}

	ret = k_sem_take(&connect_done_sem, K_SECONDS(25));
	if (ret != 0) {
		LOG_ERR("STA connect timed out: %d", ret);
		return ret;
	}

	char sta_ip[NET_IPV4_ADDR_LEN];
	wifi_manager_local_ip(sta_ip, sizeof(sta_ip));
	return sta_connected ? 0 : -ECONNREFUSED;
}

bool wifi_manager_sta_connected(void)
{
	return sta_connected;
}

void wifi_manager_local_ip(char *out, size_t out_len)
{
	if (out_len == 0) {
		return;
	}

	out[0] = '\0';

	struct net_if *iface = sta_iface();
	if (iface == NULL || iface->config.ip.ipv4 == NULL) {
		return;
	}

	for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
		if (iface->config.ip.ipv4->unicast[i].ipv4.is_used) {
			net_addr_ntop(AF_INET,
				      &iface->config.ip.ipv4->unicast[i].ipv4.address.in_addr,
				      out, out_len);
			LOG_INF("STA local IPv4=%s iface=%p", out, iface);
			return;
		}
	}
}

enum portal_scan_state wifi_manager_scan_state(void)
{
	return scan_state;
}

int wifi_manager_scan_last_error(void)
{
	return scan_last_error;
}

const struct portal_net *wifi_manager_scan_results(void)
{
	return scan_results;
}

int wifi_manager_scan_count(void)
{
	return scan_count;
}
