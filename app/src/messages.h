#ifndef IEM_MESSAGES_H
#define IEM_MESSAGES_H

#include <stdbool.h>
#include <zephyr/types.h>

struct l4_state {
    bool is_connected;
};

struct sensor_reading {
    int64_t timestamp_ms;   /* Timestamp in milliseconds */
    int32_t temperature_mc; /* Temperature in milli-Celsius */
    uint32_t humidity;      /* Humidity in percent * 1000 (e.g., 55.5% = 55500) */
    uint32_t pressure;      /* Pressure in Pa */
};

#endif /* IEM_MESSAGES_H */