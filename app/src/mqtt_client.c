#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(iem_mqtt_client, CONFIG_LOG_DEFAULT_LEVEL);

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/socket.h>
#include <zephyr/zbus/zbus.h>

#include "messages.h"

#define MQTT_THREAD_STACK_SIZE  4096
#define MQTT_THREAD_PRIORITY    7
#define MQTT_THREAD_DELAY_MS    5000

#define MQTT_CONNECT_FAILURE_BACKOFF_SEC 30
#define MQTT_PAYLOAD_SIZE       256
#define MQTT_POLL_TIMEOUT_MS    (CONFIG_MQTT_KEEPALIVE * MSEC_PER_SEC / 2)

#define TOPIC_TEMPERATURE       "sensors/temperature"
#define TOPIC_HUMIDITY          "sensors/humidity"
#define TOPIC_PRESSURE          "sensors/pressure"

static uint8_t rx_buffer[MQTT_PAYLOAD_SIZE];
static uint8_t tx_buffer[MQTT_PAYLOAD_SIZE];

ZBUS_MSG_SUBSCRIBER_DEFINE(mqtt_sub);

ZBUS_CHAN_DECLARE(net_state_chan);
ZBUS_CHAN_DECLARE(sensor_data_chan);

static struct mqtt_client client;
static struct sockaddr_storage broker;

static bool mqtt_connected;
static bool net_connected;

static void mqtt_event_handler(struct mqtt_client *const c, const struct mqtt_evt *evt)
{
    switch (evt->type) {
    case MQTT_EVT_CONNACK:
        if (evt->result != 0) {
            LOG_ERR("CONNACK error %d.", evt->result);
            break;
        }
        mqtt_connected = true;
        LOG_INF("MQTT connected to broker.");
        break;
    case MQTT_EVT_DISCONNECT:
        mqtt_connected = false;
        LOG_INF("MQTT disconnected.");
        break;
    case MQTT_EVT_PUBACK:
        LOG_DBG("PUBACK id=%u.", evt->param.puback.message_id);
        break;
    case MQTT_EVT_PINGRESP:
        LOG_DBG("PINGRESP received.");
        break;
    default:
        break;
    }
}

static int mqtt_connect_to_broker(void)
{
    struct zsock_pollfd fds;
    int rc;
    int attempt = 0;

    struct sockaddr_in *broker4 = (struct sockaddr_in *)&broker;
    broker4->sin_family = AF_INET;
    broker4->sin_port   = htons(CONFIG_MQTT_BROKER_PORT);

    if (zsock_inet_pton(AF_INET, CONFIG_MQTT_BROKER_ADDR, &broker4->sin_addr) != 1) {
        LOG_ERR("Invalid broker IP address: %s.", CONFIG_MQTT_BROKER_ADDR);
        return -EINVAL;
    }

    while (CONFIG_MQTT_MAX_RETRY_COUNT == 0 || attempt < CONFIG_MQTT_MAX_RETRY_COUNT) {
        if (attempt > 0) {
            k_sleep(K_SECONDS(5));
        }

        mqtt_client_init(&client);
        client.broker           = &broker;
        client.evt_cb           = mqtt_event_handler;
        client.client_id.utf8   = (uint8_t *)CONFIG_MQTT_CLIENTID;
        client.client_id.size   = sizeof(CONFIG_MQTT_CLIENTID) - 1;
        client.protocol_version = MQTT_VERSION_3_1_1;
        client.rx_buf           = rx_buffer;
        client.rx_buf_size      = sizeof(rx_buffer);
        client.tx_buf           = tx_buffer;
        client.tx_buf_size      = sizeof(tx_buffer);

        rc = mqtt_connect(&client);
        if (rc != 0) {
            LOG_WRN("mqtt_connect failed (%d), attempt %d.",
                rc, attempt + 1);
            attempt++;
            continue;
        }

        fds.fd = client.transport.tcp.sock;
        fds.events = ZSOCK_POLLIN;

        rc = zsock_poll(&fds, 1, 5 * MSEC_PER_SEC);
        if (rc <= 0) {
            LOG_WRN("CONNACK timeout, attempt %d", attempt + 1);
            mqtt_disconnect(&client, NULL);
            mqtt_connected = false;
            attempt++;
            continue;
        }

        rc = mqtt_input(&client);
        if (rc != 0) {
            LOG_WRN("mqtt_input failed (%d), attempt %d",
                rc, attempt + 1);
            mqtt_disconnect(&client, NULL);
            mqtt_connected = false;
            attempt++;
            continue;
        }

        if (mqtt_connected) {
            LOG_INF("MQTT ready (attempt %d)", attempt + 1);
            return 0;
        }

        LOG_WRN("CONNACK rejected by broker, attempt %d", attempt + 1);
        mqtt_disconnect(&client, NULL);
        attempt++;

    }

    LOG_ERR("MQTT broker unreachable after %d attempt(s)", attempt);
    return -ETIMEDOUT;
}

static void service_mqtt_connection(void)
{
    struct zsock_pollfd fds = {
        .fd     = client.transport.tcp.sock,
        .events = ZSOCK_POLLIN,
    };
    int rc;

    rc = zsock_poll(&fds, 1, 0);
    if (rc < 0) {
        LOG_ERR("poll error %d, dropping MQTT connection", errno);
        mqtt_connected = false;
        return;
    }

    if (rc > 0 && (fds.revents & ZSOCK_POLLIN)) {
        rc = mqtt_input(&client);
        if (rc != 0) {
            LOG_ERR("mqtt_input error %d, dropping connection", rc);
            mqtt_connected = false;
            return;
        }
    }

    rc = mqtt_live(&client);
    if (rc != 0 && rc != -EAGAIN) {
        LOG_ERR("mqtt_live error %d, dropping connection", rc);
        mqtt_connected = false;
    }
}

static int publish_to_topic(const char *topic, const char *payload)
{
    struct mqtt_publish_param param = {
        .message = {
            .topic = {
                .topic = {
                    .utf8 = (const uint8_t *)topic,
                    .size = strlen(topic),
                },
                .qos = MQTT_QOS_0_AT_MOST_ONCE,
            },
            .payload = {
                .data = (uint8_t *)payload,
                .len  = strlen(payload),
            },
        },
        .message_id  = 0,
        .dup_flag    = 0,
        .retain_flag = 0,
    };

    int rc = mqtt_publish(&client, &param);

    if (rc != 0) {
        LOG_ERR("mqtt_publish(%s) failed: %d", topic, rc);
    } else {
        LOG_DBG("%s -> %s", topic, payload);
    }

    return rc;
}

static void publish_sensor_data(const struct sensor_reading *reading)
{
    char buf[32];

    int32_t temp_int  = reading->temperature_mc / 1000;
    int32_t temp_frac = reading->temperature_mc % 1000;
    if (temp_frac < 0) {
        temp_frac = -temp_frac;
    }
    if (reading->temperature_mc < 0 && temp_int == 0) {
        snprintk(buf, sizeof(buf), "-0.%03d", temp_frac);
    } else {
        snprintk(buf, sizeof(buf), "%d.%03d", temp_int, temp_frac);
    }
    publish_to_topic(TOPIC_TEMPERATURE, buf);

    snprintk(buf, sizeof(buf), "%u.%03u", reading->humidity / 1000, reading->humidity % 1000);
    publish_to_topic(TOPIC_HUMIDITY, buf);

    snprintk(buf, sizeof(buf), "%u.%03u", reading->pressure / 1000, reading->pressure % 1000);
    publish_to_topic(TOPIC_PRESSURE, buf);
}

static void mqtt_thread(void *arg1, void *arg2, void *arg3)
{
    const struct zbus_channel *chan;
    union {
        struct net_state net;
        struct sensor_reading sensor;
    } msg;
    int rc;

    LOG_INF("MQTT client thread started.");

    while (1) {
        rc = zbus_sub_wait_msg(&mqtt_sub, &chan, &msg, K_MSEC(MQTT_POLL_TIMEOUT_MS));
if (rc == 0) {
            if (chan == &net_state_chan) {
                net_connected = msg.net.is_connected;
                if (!net_connected && mqtt_connected) {
                    mqtt_disconnect(&client, NULL);
                    mqtt_connected = false;
                }
            } else if (chan == &sensor_data_chan) {
                if (mqtt_connected) {
                    publish_sensor_data(&msg.sensor);
                } else {
                    LOG_WRN("MQTT not ready, dropping sensor reading.");
                }
            }
        } else if (rc == -EAGAIN || rc == -ENOMSG) { 
            if (mqtt_connected) {
                service_mqtt_connection();
            }
        } else {
            LOG_ERR("zbus_sub_wait_msg error: %s.", strerror(rc));
        }

        if (net_connected && !mqtt_connected) {
            LOG_INF("Connecting to MQTT broker %s:%d...", CONFIG_MQTT_BROKER_ADDR, CONFIG_MQTT_BROKER_PORT);
            rc = mqtt_connect_to_broker();
            if (rc < 0) {
                LOG_ERR("MQTT connect failed (%s), backing off for %d seconds.",
                        strerror(rc), MQTT_CONNECT_FAILURE_BACKOFF_SEC);
                k_sleep(K_SECONDS(MQTT_CONNECT_FAILURE_BACKOFF_SEC));
            }
        }
    }
}

K_THREAD_DEFINE(mqtt_thread_id, MQTT_THREAD_STACK_SIZE, mqtt_thread,
                NULL, NULL, NULL, MQTT_THREAD_PRIORITY, 0, MQTT_THREAD_DELAY_MS);
