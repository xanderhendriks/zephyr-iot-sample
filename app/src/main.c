#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/random/random.h>

LOG_MODULE_REGISTER(main);

#define MY_SSID "nxwireless-legacy"
#define MY_PASS "koalaandtreeandpuppylonglegs"
#define MQTT_BROKER_ADDR "192.168.0.253"
#define MQTT_BROKER_PORT 1883
#define CLIENT_ID "zephyr_client"
#define MQTT_TOPIC "iot/test"
#define MQTT_MSG "hello from zephyr"

static struct net_mgmt_event_callback wifi_cb;

static bool mqtt_is_connected = false;

static struct mqtt_client client;
static struct sockaddr_in broker;
static uint8_t rx_buffer[2048], tx_buffer[2048];
static struct mqtt_utf8 client_id;
static void print_ip_info(struct net_if *iface) {
  char buf[NET_IPV4_ADDR_LEN];

  struct net_if_addr *ifaddr =
      (struct net_if_addr *)&iface->config.ip.ipv4->unicast[0];
  if (ifaddr && ifaddr->address.family == AF_INET) {
    net_addr_ntop(AF_INET, &ifaddr->address.in_addr, buf, sizeof(buf));
    LOG_INF("Assigned IP: %s", buf);
  } else {
    LOG_WRN("No IPv4 address assigned");
  }
}

static void mqtt_publish_message(void);

static void wifi_event_handler(struct net_mgmt_event_callback *cb,
                               uint32_t mgmt_event, struct net_if *iface) {
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

static void mqtt_evt_handler(struct mqtt_client *c,
                             const struct mqtt_evt *evt) {
  switch (evt->type) {
  case MQTT_EVT_CONNACK:
    if (evt->result == 0) {
      LOG_WRN("MQTT connected");
      mqtt_is_connected = true;
      // mqtt_publish_message();
    }
    break;

  case MQTT_EVT_DISCONNECT:
    LOG_WRN("MQTT disconnected: %d", evt->result);
    mqtt_is_connected = false;
    break;

  default:
    LOG_WRN("MQTT type: %d, err: %d", evt->type, evt->result);
    break;
  }
}

static int my_mqtt_connect(void) {
  client_id.utf8 = (uint8_t *)CLIENT_ID;
  client_id.size = strlen(CLIENT_ID);

  mqtt_client_init(&client);

  client.broker = &broker;
  client.evt_cb = mqtt_evt_handler;
  client.client_id = client_id;
  client.password = NULL;
  client.user_name = NULL;

  client.protocol_version = MQTT_VERSION_3_1_1;

  client.rx_buf = rx_buffer;
  client.rx_buf_size = sizeof(rx_buffer);
  client.tx_buf = tx_buffer;
  client.tx_buf_size = sizeof(tx_buffer);

  client.transport.type = MQTT_TRANSPORT_NON_SECURE;

  return mqtt_connect(&client);
}

static void mqtt_publish_message(void) {
  struct mqtt_publish_param param;

  param.message.topic.qos = MQTT_QOS_0_AT_MOST_ONCE;
  param.message.topic.topic.utf8 = (uint8_t *)MQTT_TOPIC;
  param.message.topic.topic.size = strlen(MQTT_TOPIC);
  param.message.payload.data = (uint8_t *)MQTT_MSG;
  param.message.payload.len = strlen(MQTT_MSG);
  param.message_id = sys_rand32_get();
  param.dup_flag = 0;
  param.retain_flag = 0;

  int rc = mqtt_publish(&client, &param);
  if (rc) {
    LOG_ERR("Publish failed: %d", rc);
  } else {
    LOG_INF("Message published");
  }
}

int main(void) {
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

  LOG_INF("Firmware version: %s", GIT_VERSION);

  net_mgmt_init_event_callback(&wifi_cb, wifi_event_handler,
                               NET_EVENT_WIFI_CONNECT_RESULT |
                                   NET_EVENT_WIFI_DISCONNECT_RESULT);

  net_mgmt_add_event_callback(&wifi_cb);

  int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &cnx_params,
                     sizeof(cnx_params));
  if (ret) {
    LOG_ERR("WiFi connect failed: %d", ret);
  } else {
    LOG_INF("WiFi connected");

    broker.sin_family = AF_INET;
    broker.sin_port = htons(MQTT_BROKER_PORT);
    zsock_inet_pton(AF_INET, MQTT_BROKER_ADDR, &broker.sin_addr);

    if (my_mqtt_connect() == 0) {
      LOG_INF("MQTT connection started");
      k_sleep(K_MSEC(200));

      while (!mqtt_is_connected) {
        ret = mqtt_input(&client);
        if (ret) {
          LOG_ERR("mqtt_input: %d", ret);
        }
        // ret = mqtt_live(&client);
        // if (ret) {
        //   LOG_ERR("mqtt_live: %d", ret);
        // }
        k_sleep(K_MSEC(200));
      }
    }

    LOG_INF("MQTT fully connected");
    mqtt_publish_message();
    while (!mqtt_is_connected) {
      ret = mqtt_input(&client);
      if (ret) {
        LOG_ERR("mqtt_input: %d", ret);
      }
      // ret = mqtt_live(&client);
      // if (ret) {
      //   LOG_ERR("mqtt_live: %d", ret);
      // }
      k_sleep(K_MSEC(200));
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
}
