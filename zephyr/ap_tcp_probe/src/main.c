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

static void dump_ifaces(const char *stage)
{
	LOG_INF("%s: default=%p sta=%p sap=%p", stage,
		net_if_get_default(), net_if_get_wifi_sta(), net_if_get_wifi_sap());
}

static int set_ap_addr(struct net_if *iface)
{
	struct in_addr ip;
	struct in_addr mask;

	if (net_addr_pton(AF_INET, AP_IP, &ip) != 0 ||
	    net_addr_pton(AF_INET, AP_MASK, &mask) != 0) {
		LOG_ERR("bad AP address config");
		return -EINVAL;
	}

	int ret = net_if_up(iface);
	if (ret < 0 && ret != -EALREADY) {
		LOG_ERR("net_if_up failed: %d", ret);
		return ret;
	}

	net_if_ipv4_set_gw(iface, &ip);
	net_if_ipv4_addr_add(iface, &ip, NET_ADDR_MANUAL, 0);
	if (!net_if_ipv4_set_netmask_by_addr(iface, &ip, &mask)) {
		LOG_ERR("set netmask failed");
		return -EIO;
	}

	LOG_INF("AP IPv4 ready: %s/%s iface=%p", AP_IP, AP_MASK, iface);
	return 0;
}

static int start_ap(struct net_if *iface)
{
	struct wifi_connect_req_params ap = {0};
	ap.ssid = (const uint8_t *)SSID;
	ap.ssid_length = strlen(SSID);
	ap.psk = (const uint8_t *)PASS;
	ap.psk_length = strlen(PASS);
	ap.security = WIFI_SECURITY_TYPE_PSK;
	ap.channel = 6;
	ap.band = WIFI_FREQ_BAND_2_4_GHZ;

	dump_ifaces("before AP enable");

	int ret = net_if_up(iface);
	if (ret < 0 && ret != -EALREADY) {
		LOG_ERR("net_if_up before AP failed: %d", ret);
		return ret;
	}

	ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, iface, &ap, sizeof(ap));
	if (ret != 0) {
		LOG_ERR("AP enable failed: %d", ret);
		return ret;
	}

	LOG_INF("AP enabled: ssid=%s pass=%s iface=%p", SSID, PASS, iface);
	k_sleep(K_SECONDS(1));
	dump_ifaces("after AP enable before IPv4");

	ret = set_ap_addr(iface);
	if (ret != 0) {
		return ret;
	}

	k_sleep(K_MSEC(250));

	struct in_addr start;
	if (net_addr_pton(AF_INET, DHCP_START, &start) != 0) {
		LOG_ERR("bad DHCP start");
		return -EINVAL;
	}

	ret = net_dhcpv4_server_start(iface, &start);
	if (ret != 0 && ret != -EALREADY) {
		LOG_ERR("DHCP start failed: %d", ret);
		return ret;
	}

	LOG_INF("DHCP started at %s", DHCP_START);
	LOG_INF("READY: join %s pass %s then open http://%s/", SSID, PASS, AP_IP);
	return 0;
}

static int start_tcp(void)
{
	int fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (fd < 0) {
		LOG_ERR("socket failed errno=%d", errno);
		return -errno;
	}

	int opt = 1;
	zsock_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (zsock_bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		int err = errno;
		LOG_ERR("bind failed errno=%d", err);
		zsock_close(fd);
		return -err;
	}

	if (zsock_listen(fd, 4) < 0) {
		int err = errno;
		LOG_ERR("listen failed errno=%d", err);
		zsock_close(fd);
		return -err;
	}

	LOG_INF("TCP listening on 0.0.0.0:%d", PORT);
	return fd;
}

int main(void)
{
	LOG_INF("AP TCP probe starting");
	LOG_INF("SSID=%s PASS=%s URL=http://%s/", SSID, PASS, AP_IP);

	struct net_if *iface = net_if_get_wifi_sap();
	if (iface == NULL) {
		iface = net_if_get_default();
		LOG_WRN("SAP iface missing, using default iface=%p", iface);
	}
	if (iface == NULL) {
		LOG_ERR("no Wi-Fi iface");
		return 0;
	}

	net_if_set_default(iface);

	if (start_ap(iface) != 0) {
		LOG_ERR("AP start failed");
		return 0;
	}

	int server = start_tcp();
	if (server < 0) {
		LOG_ERR("TCP server failed");
		return 0;
	}

	while (true) {
		int client = zsock_accept(server, NULL, NULL);
		if (client < 0) {
			LOG_WRN("accept failed errno=%d", errno);
			k_sleep(K_MSEC(100));
			continue;
		}

		LOG_INF("client accepted");

		char tmp[128];
		int n = zsock_recv(client, tmp, sizeof(tmp) - 1, 0);
		if (n > 0) {
			tmp[n] = '\0';
			LOG_INF("rx: %.80s", tmp);
		}

		const char msg[] =
			"HTTP/1.1 200 OK\r\n"
			"Content-Type: text/plain\r\n"
			"Connection: close\r\n"
			"Content-Length: 29\r\n"
			"\r\n"
			"hello from zephyr tcp probe\n";

		zsock_send(client, msg, sizeof(msg) - 1, 0);
		zsock_close(client);
	}
}
