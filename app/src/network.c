#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(iem_network, CONFIG_LOG_DEFAULT_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/zbus/zbus.h>

#include "messages.h"

#define NETWORK_THREAD_DELAY_MS     5000
#define NETWORK_THREAD_PRIORITY     7
#define NETWORK_THREAD_STACK_SIZE   2048

#define WIFI_EVENT_MASK                                                        \
        (NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT)
#define NET_EVENT_MASK                                                         \
        (NET_EVENT_IPV4_ADDR_ADD)


ZBUS_CHAN_DEFINE(net_state_chan,
                 struct net_state,
                 NULL,
                 NULL,
                 ZBUS_OBSERVERS(mqtt_sub),
                 ZBUS_MSG_INIT(.is_connected = false));

static struct net_if *iface;
static struct wifi_connect_req_params sta_config;

static int net_state_publish(bool connected)
{
    struct net_state state = {
        .is_connected = connected
    };

    int rc = zbus_chan_pub(&net_state_chan, &state, K_NO_WAIT);
    switch (rc) {
    case -ENOMSG:
        LOG_ERR("Invalid message received for network state.");
        break;
    case -EBUSY:
        LOG_ERR("Network state channel is busy, message not published.");
        break;
    default:
        break;
    }

    return rc;
}

static int wifi_connect(void)
{
    sta_config.ssid = (const uint8_t *)CONFIG_WIFI_SSID;
    sta_config.ssid_length = sizeof(CONFIG_WIFI_SSID) - 1;
    sta_config.psk = (const uint8_t *)CONFIG_WIFI_PSK;
    sta_config.psk_length = sizeof(CONFIG_WIFI_PSK) - 1;
    sta_config.security = WIFI_SECURITY_TYPE_PSK;
    sta_config.channel = WIFI_CHANNEL_ANY;
    sta_config.band = WIFI_FREQ_BAND_2_4_GHZ;

    LOG_INF("Connecting to WiFi SSID: %s.", CONFIG_WIFI_SSID);

    int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &sta_config,
                       sizeof(struct wifi_connect_req_params));
    if (ret) {
        LOG_ERR("Failed to connect to WiFi (%s), error: %d.", CONFIG_WIFI_SSID, ret);
    }

    return ret;
}

static void net_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
                              struct net_if *iface)
{
    switch (mgmt_event) {
    case NET_EVENT_WIFI_CONNECT_RESULT:
        LOG_INF("WiFi connection established.");
        break;
    case NET_EVENT_WIFI_DISCONNECT_RESULT:
        LOG_INF("WiFi disconnected.");
        net_state_publish(false);
        wifi_connect();
        break;
    case NET_EVENT_IPV4_ADDR_ADD:
        LOG_INF("IPv4 address added.");
        net_state_publish(true);
        break;
    default:
        LOG_INF("Unhandled network event: 0x%llx.", mgmt_event);
        break;
    }
}

static void network_thread(void *arg1, void *arg2, void *arg3)
{
    LOG_INF("Network thread started, waiting for network events...");

    iface = net_if_get_wifi_sta();
    if (!iface) {
        LOG_ERR("Network interface not initialized.");
    }

    static struct net_mgmt_event_callback wifi_cb;
    net_mgmt_init_event_callback(&wifi_cb, net_event_handler, WIFI_EVENT_MASK);
    net_mgmt_add_event_callback(&wifi_cb);
    static struct net_mgmt_event_callback net_cb;
    net_mgmt_init_event_callback(&net_cb, net_event_handler, NET_EVENT_MASK);
    net_mgmt_add_event_callback(&net_cb);

    wifi_connect();

    while (1) {
        k_sleep(K_SECONDS(1));
    }
}

    K_THREAD_DEFINE(network_thread_id, NETWORK_THREAD_STACK_SIZE, network_thread,
                    NULL, NULL, NULL, NETWORK_THREAD_PRIORITY, 0, NETWORK_THREAD_DELAY_MS);
