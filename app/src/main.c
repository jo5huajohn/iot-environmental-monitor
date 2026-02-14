#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(iot_environmental_monitor, CONFIG_APP_LOG_LEVEL);

int main(void)
{
    while (1) {
        k_sleep(K_SECONDS(1));
    }

    return 0;
}
