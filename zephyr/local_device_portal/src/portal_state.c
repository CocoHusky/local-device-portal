#include "portal_state.h"

#include "portal_config.h"

#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>

#include <stdio.h>

static char device_host[PORTAL_HOST_MAX];
static char dashboard_url[PORTAL_URL_MAX];
static bool handoff_pending;
static bool reboot_pending;

void portal_state_init(void)
{
	uint8_t id[16];
	uint16_t suffix = (uint16_t)(k_cycle_get_32() & 0xffff);
	ssize_t id_len = hwinfo_get_device_id(id, sizeof(id));

	if (id_len >= 2) {
		suffix = ((uint16_t)id[id_len - 2] << 8) | id[id_len - 1];
	}

	snprintk(device_host, sizeof(device_host), "mmwave-%04x",
		 (unsigned int)suffix);
	snprintk(dashboard_url, sizeof(dashboard_url), "http://%s.local/",
		 device_host);
}

const char *portal_state_host(void)
{
	return device_host;
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
