#include "credential_store.h"
#include "portal_config.h"
#include "portal_http.h"
#include "portal_mdns.h"
#include "portal_state.h"
#include "wifi_manager.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/hostname.h>

#include <string.h>

LOG_MODULE_REGISTER(local_device_portal, LOG_LEVEL_INF);

#define FW_MARKER "zephyr-portal-dashboard-mdns-lite-2026-06-30-01"

int main(void)
{
	int ret;

	LOG_INF("Local Device Portal Zephyr starting");
	LOG_INF("FIRMWARE MARKER: %s", FW_MARKER);

	portal_state_init();
	credential_store_init();
	wifi_manager_init();

	ret = net_hostname_set(portal_state_host(), strlen(portal_state_host()));
	if (ret != 0) {
		LOG_WRN("hostname set failed: %d", ret);
	}

	LOG_INF("setup Wi-Fi: %s", portal_state_ap_ssid());
	LOG_INF("setup URL: http://%s/", PORTAL_AP_IP);
	LOG_INF("dashboard URL target: %s", portal_state_dashboard_url());

	ret = wifi_manager_start_ap();
	if (ret != 0) {
		LOG_ERR("setup AP failed: %d", ret);
		while (true) {
			k_sleep(K_SECONDS(1));
		}
	}

	portal_http_start();
	portal_mdns_start();

	while (true) {
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
