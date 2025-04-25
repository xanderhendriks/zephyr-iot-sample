#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main);

#define MY_SSID     "nxwireless-legacy"
#define MY_PASS     "koalaandtreeandpuppylonglegs"


static struct net_mgmt_event_callback wifi_cb;

static void print_ip_info(struct net_if *iface)
{
    char buf[NET_IPV4_ADDR_LEN];

    struct net_if_addr *ifaddr = (struct net_if_addr*) &iface->config.ip.ipv4->unicast[0];
    if (ifaddr && ifaddr->address.family == AF_INET) {
        net_addr_ntop(AF_INET, &ifaddr->address.in_addr, buf, sizeof(buf));
        LOG_INF("Assigned IP: %s", buf);
    } else {
        LOG_WRN("No IPv4 address assigned");
    }
}

static void wifi_event_handler(struct net_mgmt_event_callback *cb,
                               uint32_t mgmt_event, struct net_if *iface)
{
    switch (mgmt_event) {
    case NET_EVENT_WIFI_CONNECT_RESULT:
        struct wifi_status *status = (struct wifi_status *)cb->info;
        if (status->status) {
            LOG_ERR("WiFi connection failed (%d)", status->status);
        } else {
            LOG_INF("WiFi successfully connected");
        }
		print_ip_info(iface);
        break;

    case NET_EVENT_WIFI_DISCONNECT_RESULT:
        LOG_WRN("WiFi disconnected");
        break;
    }
}

int main(void)
{
    struct net_if *iface = net_if_get_default();

    struct wifi_connect_req_params cnx_params = {
        .ssid = MY_SSID,
        .ssid_length = strlen(MY_SSID),
        .psk = MY_PASS,
        .psk_length = strlen(MY_PASS),
        .security = WIFI_SECURITY_TYPE_PSK,
        .channel = WIFI_CHANNEL_ANY,
        .timeout = SYS_FOREVER_MS,
    };

    net_mgmt_init_event_callback(&wifi_cb, wifi_event_handler,
        NET_EVENT_WIFI_CONNECT_RESULT |
        NET_EVENT_WIFI_DISCONNECT_RESULT);

    net_mgmt_add_event_callback(&wifi_cb);

    int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &cnx_params, sizeof(cnx_params));
    if (ret) {
        LOG_ERR("WiFi connect failed: %d", ret);
    } else {
        LOG_INF("WiFi connected");
    }

    /* Optional: Blink LED */
    const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
    gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);

    while (1) {
        gpio_pin_toggle_dt(&led);
        k_sleep(K_MSEC(1000));
    }
    return 0;
}
