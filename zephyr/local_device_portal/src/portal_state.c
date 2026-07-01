#include "portal_state.h"

#include "portal_config.h"

#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>

#include <stdio.h>
#include <string.h>

static char device_host[PORTAL_HOST_MAX];
static char ap_ssid[PORTAL_SSID_MAX + 1];
static char dashboard_url[PORTAL_URL_MAX];
static bool handoff_pending;
static bool reboot_pending;

void portal_state_init(void)
{
	uint8_t id[16];
	uint8_t suffix[3];
	ssize_t id_len = hwinfo_get_device_id(id, sizeof(id));

	if (id_len >= sizeof(suffix)) {
		const uint8_t *addr = id + id_len - sizeof(suffix);
		memcpy(suffix, addr, sizeof(suffix));
	} else {
		uint32_t fallback = k_cycle_get_32();

		suffix[0] = (uint8_t)(fallback >> 16);
		suffix[1] = (uint8_t)(fallback >> 8);
		suffix[2] = (uint8_t)fallback;
	}

	snprintk(device_host, sizeof(device_host), "%s-%02x%02x%02x",
		 PORTAL_HOST_PREFIX, suffix[0], suffix[1], suffix[2]);
	snprintk(ap_ssid, sizeof(ap_ssid), "%s-%02X%02X%02X",
		 PORTAL_AP_SSID_PREFIX, suffix[0], suffix[1], suffix[2]);
	snprintk(dashboard_url, sizeof(dashboard_url), "http://%s.local/",
		 device_host);
}

const char *portal_state_host(void)
{
	return device_host;
}

const char *portal_state_ap_ssid(void)
{
	return ap_ssid;
}

const char *portal_state_dashboard_url(void)
{
	return dashboard_url;
}

void portal_state_request_handoff(void)
{
	handoff_pending = true;
}

bool portal_state_take_handoff_request(void)
{
	if (!handoff_pending) {
		return false;
	}

	handoff_pending = false;
	return true;
}

void portal_state_request_reboot(void)
{
	reboot_pending = true;
}

bool portal_state_take_reboot_request(void)
{
	if (!reboot_pending) {
		return false;
	}

	reboot_pending = false;
	return true;
}
