#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/dhcpv4_server.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/wifi_mgmt.h>
#include <errno.h>
#include <string.h>

LOG_MODULE_REGISTER(ap_tcp_probe, LOG_LEVEL_INF);

#define SSID "ZephyrTcpProbe"
#define PASS "focusfetch"
#define AP_IP "192.168.4.1"
#define AP_MASK "255.255.255.0"
#define DHCP_START "192.168.4.2"
#define PORT 80

static int setup_addr(struct net_if *iface)
{
	struct in_addr ip;
	struct in_addr mask;

	if (net_addr_pton(AF_INET, AP_IP, &ip) != 0 ||
	    net_addr_pton(AF_INET, AP_MASK, &mask) != 0) {
		return -EINVAL;
	}

	int ret = net_if_up(iface);
	if (ret < 0 && ret != -EALREADY) {
		return ret;
	}

	net_if_ipv4_set_gw(iface, &ip);
	net_if_ipv4_addr_add(iface, &ip, NET_ADDR_MANUAL, 0);
	net_if_ipv4_set_netmask_by_addr(iface, &ip, &mask);

	LOG_INF("address ready %s", AP_IP);
	return 0;
}

static int setup_ap(struct net_if *iface)
{
	struct wifi_connect_req_params ap = {0};

	ap.ssid = (const uint8_t *)SSID;
	ap.ssid_length = strlen(SSID);
	ap.psk = (const uint8_t *)PASS;
	ap.psk_length = strlen(PASS);
	ap.security = WIFI_SECURITY_TYPE_PSK;
	ap.channel = 6;
	ap.band = WIFI_FREQ_BAND_2_4_GHZ;

	int ret = setup_addr(iface);
	if (ret != 0) {
		LOG_ERR("address failed %d", ret);
		return ret;
	}

	ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, iface, &ap, sizeof(ap));
	if (ret != 0) {
		LOG_ERR("ap failed %d", ret);
		return ret;
	}

	k_sleep(K_SECONDS(1));

	struct in_addr start;
	if (net_addr_pton(AF_INET, DHCP_START, &start) != 0) {
		return -EINVAL;
	}

	ret = net_dhcpv4_server_start(iface, &start);
	if (ret != 0 && ret != -EALREADY) {
		LOG_ERR("dhcp failed %d", ret);
		return ret;
	}

	LOG_INF("ap ready ssid=%s pass=%s ip=%s", SSID, PASS, AP_IP);
	return 0;
}

static int setup_server(void)
{
	int fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (fd < 0) {
		LOG_ERR("socket errno %d", errno);
		return -errno;
	}

	int opt = 1;
	zsock_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (zsock_bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		LOG_ERR("bind errno %d", errno);
		return -errno;
	}

	if (zsock_listen(fd, 4) < 0) {
		LOG_ERR("listen errno %d", errno);
		return -errno;
	}

	LOG_INF("tcp listening on port %d", PORT);
	return fd;
}

int main(void)
{
	LOG_INF("probe start ssid=%s", SSID);

	struct net_if *iface = net_if_get_wifi_sap();
	if (iface == NULL) {
		iface = net_if_get_default();
	}
	if (iface == NULL) {
		LOG_ERR("no iface");
		return 0;
	}

	net_if_set_default(iface);

	if (setup_ap(iface) != 0) {
		return 0;
	}

	int server = setup_server();
	if (server < 0) {
		return 0;
	}

	while (true) {
		int client = zsock_accept(server, NULL, NULL);
		if (client < 0) {
			LOG_WRN("accept errno %d", errno);
			k_sleep(K_MSEC(100));
			continue;
		}

		LOG_INF("client accepted");

		char tmp[64];
		(void)zsock_recv(client, tmp, sizeof(tmp), 0);

		const char msg[] = "hello from zephyr tcp probe\n";
		zsock_send(client, msg, sizeof(msg) - 1, 0);
		zsock_close(client);
	}
}
