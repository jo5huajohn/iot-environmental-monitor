#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor_data_types.h>
#include <zephyr/kernel.h>
#include <zephyr/rtio/rtio.h>
#include <zephyr/sys/timeutil.h>
#include <zephyr/zbus/zbus.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(iem_sensor, CONFIG_LOG_DEFAULT_LEVEL);

#include "messages.h"

#define DEVICE_READINESS_CHECK_INTERVAL_SEC 5
#define BUFFER_SIZE                 128
#define PRIORITY_SENSOR             7
#define SENSOR_STACK_SIZE           2048

static const struct device *const dev = DEVICE_DT_GET_ANY(bosch_bme680);

SENSOR_DT_READ_IODEV(iodev, DT_COMPAT_GET_ANY_STATUS_OKAY(bosch_bme680),
                     {SENSOR_CHAN_AMBIENT_TEMP, 0},
                     {SENSOR_CHAN_HUMIDITY, 0},
                     {SENSOR_CHAN_PRESS, 0});

RTIO_DEFINE(rtio_ctx, 1, 1);

ZBUS_CHAN_DEFINE(sensor_data_chan,
                 struct sensor_reading,
                 NULL,
                 NULL,
                 ZBUS_OBSERVERS(mqtt_sub),
                 ZBUS_MSG_INIT(.timestamp_ms = 0, .temperature_mc = 0,
                               .humidity = 0, .pressure = 0));

static const struct device *check_bme680_device(const struct device *dev)
{
    if (dev == NULL) {
        LOG_ERR("No device found.");
        return NULL;
    }

    if (!device_is_ready(dev)) {
        LOG_ERR("Device \"%s\" is not ready.", dev->name);
        return NULL;
    }

    LOG_INF("Found device \"%s\", getting sensor data.", dev->name);
    return dev;
}

static void sensor_thread(void *arg1, void *arg2, void *arg3)
{
    while (check_bme680_device(dev) == NULL) {
        LOG_ERR("Retrying in %ds...", DEVICE_READINESS_CHECK_INTERVAL_SEC);
        k_sleep(K_SECONDS(DEVICE_READINESS_CHECK_INTERVAL_SEC));
    }

    LOG_INF("Reading sensor data every %d seconds...", CONFIG_SENSOR_READ_INTERVAL);

    uint8_t buf[BUFFER_SIZE];

    while (1) {
        int rc = sensor_read(&iodev, &rtio_ctx, buf, BUFFER_SIZE);
        if (rc != 0) {
            LOG_ERR("%s: sensor_read() failed: %d", dev->name, rc);
            k_sleep(K_SECONDS(60));
            continue;
        }

        struct sensor_reading reading = {0};
        reading.timestamp_ms = k_uptime_get();
        const struct sensor_decoder_api *decoder;

        rc = sensor_get_decoder(dev, &decoder);
        if (rc != 0) {
            LOG_ERR("%s: Failed to get decoder: %d", dev->name, rc);
            continue;
        }

        uint32_t temp_fit = 0;
        uint32_t press_fit = 0;
        uint32_t hum_fit = 0;
        struct sensor_q31_data temp_data = {0};
        struct sensor_q31_data press_data = {0};
        struct sensor_q31_data hum_data = {0};

        decoder->decode(buf, (struct sensor_chan_spec) {SENSOR_CHAN_AMBIENT_TEMP, 0},
                        &temp_fit, 1, &temp_data);
        decoder->decode(buf, (struct sensor_chan_spec) {SENSOR_CHAN_PRESS, 0},
                        &press_fit, 1, &press_data);
        decoder->decode(buf, (struct sensor_chan_spec) {SENSOR_CHAN_HUMIDITY, 0},
                        &hum_fit, 1, &hum_data);

        reading.temperature_mc = (int32_t)((int64_t)temp_data.readings[0].temperature * 1000 >> (31 - temp_data.shift));
        reading.pressure = (uint32_t)((int64_t)press_data.readings[0].pressure * 1000 >> (31 - press_data.shift));
        reading.humidity = (uint32_t)((int64_t)hum_data.readings[0].humidity * 1000 >> (31 - hum_data.shift));

        LOG_INF("Sensor reading: Temperature: %d m°C, Pressure: %d mPa, Humidity: %d m%%RH",
                reading.temperature_mc, reading.pressure, reading.humidity);

        int ret = zbus_chan_pub(&sensor_data_chan, &reading, K_NO_WAIT);
        switch (ret) {
        case 0:
            LOG_DBG("Sensor data published to channel.");
            break;
        case -ENOMSG:
            LOG_ERR("Invalid message received for sensor data");
            break;
        case -EBUSY:
            LOG_ERR("Sensor data channel is busy, message not published");
            break;
        default:
            break;
        }

        k_sleep(K_SECONDS(CONFIG_SENSOR_READ_INTERVAL));
    }
}

K_THREAD_DEFINE(sensor_thread_id, SENSOR_STACK_SIZE, sensor_thread, NULL, NULL,
                NULL, PRIORITY_SENSOR, 0, 0);
