#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "portal_config.h"

#include <stdbool.h>
#include <stddef.h>

struct portal_net {
	char ssid[PORTAL_SSID_MAX + 1];
	int rssi;
	bool secure;
};

void wifi_manager_init(void);

int wifi_manager_start_ap(void);
int wifi_manager_stop_ap(void);
int wifi_manager_scan_blocking(void);
int wifi_manager_connect_blocking(const char *ssid, const char *pass);
int wifi_manager_bind_socket_to_ap(int fd);

bool wifi_manager_sta_connected(void);
void wifi_manager_local_ip(char *out, size_t out_len);

const struct portal_net *wifi_manager_scan_results(void);
int wifi_manager_scan_count(void);

#endif
