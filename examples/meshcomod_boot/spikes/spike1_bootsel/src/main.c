// meshcomod_boot — Spike 1
//
// Goal: prove the stock ESP-IDF bootloader will divert the boot to a 'test'
// partition when a GPIO is held (CONFIG_BOOTLOADER_APP_TEST). This same binary is
// flashed to BOTH the 'factory' (MAIN) and 'test' (RECOVERY) partitions; it just
// reports which partition it is running from.

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

static const char *who_am_i(const esp_partition_t *p)
{
    switch (p->subtype) {
    case ESP_PARTITION_SUBTYPE_APP_FACTORY: return "MAIN (factory)";
    case ESP_PARTITION_SUBTYPE_APP_TEST:    return "RECOVERY (test)  <-- GPIO divert worked!";
    default:                                return "an OTA slot";
    }
}

void app_main(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();

    for (;;) {
        printf("\n==== meshcomod_boot SPIKE 1 ====\n");
        printf("Booted partition : %s\n", who_am_i(running));
        printf("  label=%s  offset=0x%06lx  size=0x%06lx\n",
               running->label,
               (unsigned long)running->address,
               (unsigned long)running->size);
        printf("Normal reset -> MAIN.  Reset, release, then hold trackball "
               "click (GPIO0) >=5s -> RECOVERY.\n");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
