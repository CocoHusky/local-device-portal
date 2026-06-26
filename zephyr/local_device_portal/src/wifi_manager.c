#include "wifi_manager.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/util.h>

#if defined(CONFIG_NET_DHCPV4_SERVER)
#include <zephyr/net/dhcpv4_server.h>
#endif

#include <errno.h>
#include <string.h>

LOG_MODULE_REGISTER(wifi_manager, LOG_LEVEL_INF);

static struct portal_net scan_results[PORTAL_MAX_NETS];
static int scan_count;
static bool sta_connected;

static K_SEM_DEFINE(scan_done_sem, 0, 1);
static K_SEM_DEFINE(connect_done_sem, 0, 1);
static struct net_mgmt_event_callback wifi_cb;

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
					scan_results[i].secure =
						entry->security != WIFI_SECURITY_TYPE_NONE;
				}
				return;
			}
		}

		memcpy(scan_results[scan_count].ssid, entry->ssid, ssid_len);
		scan_results[scan_count].ssid[ssid_len] = '\0';
		scan_results[scan_count].rssi = entry->rssi;
		scan_results[scan_count].secure =
			entry->security != WIFI_SECURITY_TYPE_NONE;
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
		return;
	}
}

void wifi_manager_init(void)
{
	net_mgmt_init_event_callback(&wifi_cb, wifi_event_handler,
		NET_EVENT_WIFI_SCAN_RESULT |
		NET_EVENT_WIFI_SCAN_DONE |
		NET_EVENT_WIFI_CONNECT_RESULT |
		NET_EVENT_WIFI_DISCONNECT_RESULT);
	net_mgmt_add_event_callback(&wifi_cb);
}

int wifi_manager_start_ap(void)
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
	if (ret != 0) {
		LOG_ERR("AP enable failed: %d", ret);
		return ret;
	}

	LOG_INF("setup AP started: %s / %s", PORTAL_AP_SSID, PORTAL_AP_IP);
	return 0;
}

int wifi_manager_stop_ap(void)
{
	struct net_if *iface = portal_iface();
	if (iface == NULL) {
		return -ENODEV;
	}

	int ret = net_mgmt(NET_REQUEST_WIFI_AP_DISABLE, iface, NULL, 0);
	if (ret != 0) {
		LOG_WRN("AP disable failed: %d", ret);
	}

	return ret;
}

int wifi_manager_scan_blocking(void)
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
	if (ret != 0) {
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

int wifi_manager_connect_blocking(const char *ssid, const char *pass)
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
	if (ret != 0) {
		return ret;
	}

	ret = k_sem_take(&connect_done_sem, K_SECONDS(25));
	if (ret != 0) {
		return ret;
	}

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

	struct net_if *iface = portal_iface();
	if (iface == NULL || iface->config.ip.ipv4 == NULL) {
		return;
	}

	for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
		if (iface->config.ip.ipv4->unicast[i].is_used) {
			net_addr_ntop(AF_INET,
				      &iface->config.ip.ipv4->unicast[i].address.in_addr,
				      out, out_len);
			return;
		}
	}
}

const struct portal_net *wifi_manager_scan_results(void)
{
	return scan_results;
}

int wifi_manager_scan_count(void)
{
	return scan_count;
}
