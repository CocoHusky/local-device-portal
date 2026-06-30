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

#define FW_MARKER "zephyr-arduino-style-portal-2026-06-30-01"

int main(void)
{
	LOG_INF("Local Device Portal Zephyr starting");
	LOG_INF("FIRMWARE MARKER: %s", FW_MARKER);

	portal_state_init();
	credential_store_init();
	wifi_manager_init();

	LOG_INF("setup Wi-Fi: %s", portal_state_ap_ssid());
	LOG_INF("setup URL: http://%s/", PORTAL_AP_IP);
	LOG_INF("dashboard URL target: %s", portal_state_dashboard_url());

	/* Arduino reference flow:
	 *   WiFi.mode(WIFI_AP_STA);
	 *   WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK);
	 *   WiFi.softAP(setupApSsid.c_str(), AP_PASS);
	 *   dns.start(DNS_PORT, "*", AP_IP);
	 *   server.begin();
	 *
	 * Zephyr follows the same order with Zephyr APIs: AP IP first, AP enable,
	 * DHCP, then global DNS and global HTTP listeners.
	 */
	int ret = wifi_manager_start_ap();
	if (ret != 0) {
		LOG_ERR("setup AP failed: %d", ret);
	}

	portal_dns_start();
	portal_http_start();

	/* Keep setup mode AP-only until HTTP is proven stable on WROOM.  This keeps
	 * the portal path closest to Arduino's known-working setup page behavior.
	 */
	if (credential_store_has_ssid()) {
		LOG_INF("saved network present but STA auto-connect disabled during setup debug: %s",
			credential_store_ssid());
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
