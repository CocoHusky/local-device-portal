#include "credential_store.h"

#include "portal_config.h"

#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <errno.h>
#include <stdbool.h>
#include <string.h>

LOG_MODULE_REGISTER(credential_store, LOG_LEVEL_INF);

static char saved_ssid[PORTAL_SSID_MAX + 1];
static char saved_pass[PORTAL_PASS_MAX + 1];

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

void credential_store_init(void)
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

void credential_store_save(const char *ssid, const char *pass)
{
	strncpy(saved_ssid, ssid, sizeof(saved_ssid) - 1);
	saved_ssid[sizeof(saved_ssid) - 1] = '\0';
	strncpy(saved_pass, pass, sizeof(saved_pass) - 1);
	saved_pass[sizeof(saved_pass) - 1] = '\0';

	settings_save_one("portal/ssid", saved_ssid, strlen(saved_ssid) + 1);
	settings_save_one("portal/pass", saved_pass, strlen(saved_pass) + 1);
}

void credential_store_clear(void)
{
	saved_ssid[0] = '\0';
	saved_pass[0] = '\0';
	settings_delete("portal/ssid");
	settings_delete("portal/pass");
}

const char *credential_store_ssid(void)
{
	return saved_ssid;
}

const char *credential_store_pass(void)
{
	return saved_pass;
}

bool credential_store_has_ssid(void)
{
	return saved_ssid[0] != '\0';
}
