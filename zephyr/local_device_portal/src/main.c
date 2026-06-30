#include "credential_store.h"
#include "portal_config.h"
#include "portal_dns.h"
#include "portal_http.h"
#include "portal_state.h"
#include "wifi_manager.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_REGISTER(local_device_portal, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("Local Device Portal Zephyr starting");

	portal_state_init();
	credential_store_init();
	wifi_manager_init();

	LOG_INF("setup Wi-Fi: %s", portal_state_ap_ssid());
	LOG_INF("setup URL: http://%s/", PORTAL_AP_IP);
	LOG_INF("dashboard URL target: %s", portal_state_dashboard_url());

	int ret = wifi_manager_start_ap();
	if (ret != 0) {
		LOG_ERR("setup AP failed: %d", ret);
	}

	portal_dns_start();
	portal_http_start();

	if (credential_store_has_ssid()) {
		LOG_INF("saved network found: %s", credential_store_ssid());
		if (wifi_manager_connect_blocking(credential_store_ssid(),
						  credential_store_pass()) == 0) {
			LOG_INF("connected to saved Wi-Fi");
		}
	}

	while (true) {
		if (portal_state_take_handoff_request()) {
			k_sleep(K_MSEC(1800));
			wifi_manager_stop_ap();
		}
		if (portal_state_take_reboot_request()) {
			k_sleep(K_SECONDS(1));
			sys_reboot(SYS_REBOOT_COLD);
		}
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
