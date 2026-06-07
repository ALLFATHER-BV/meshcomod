/*
 * meshcomod_boot — custom 2nd-stage bootloader for the launcher model.
 *
 * Default boot target is the FACTORY partition (the recovery launcher). Recovery
 * requests a ONE-SHOT launch of the firmware slot (ota_0) by writing LAUNCH_MAGIC
 * to the 4 KB `bootsel` flag region, then rebooting. This bootloader consumes the
 * flag (erases it) and boots ota_0 once; every other boot — including any cold
 * power-on — lands back in the recovery launcher.
 *
 * Based on ESP-IDF examples/custom_bootloader/bootloader_override + _multiboot.
 */
#include <stdbool.h>
#include <stdint.h>
#include <sys/reent.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "bootloader_init.h"
#include "bootloader_utility.h"
#include "bootloader_common.h"
#include "bootloader_flash_priv.h"

static const char *TAG = "boot";

#define BOOTSEL_OFFSET   0x0000d000u   /* must match the `bootsel` partition offset */
#define BOOTSEL_SIZE     0x00001000u   /* one 4 KB sector                            */
#define LAUNCH_MAGIC     0x4C41554Eu   /* 'L','A','U','N' -> launch ota_0 once       */

void __attribute__((noreturn)) call_start_cpu0(void)
{
    bootloader_state_t bs = {0};

    if (bootloader_init() != ESP_OK) {
        bootloader_reset();
    }

    if (!bootloader_utility_load_partition_table(&bs)) {
        ESP_LOGE(TAG, "load partition table error!");
        bootloader_reset();
    }

    /* One-shot launch flag set by the recovery launcher. */
    uint32_t flag = 0;
    bootloader_flash_read(BOOTSEL_OFFSET, &flag, sizeof(flag), false);

    int boot_index;
    if (flag == LAUNCH_MAGIC && bs.app_count > 0) {
        /* Consume the one-shot flag, then boot the otadata-SELECTED app slot so
         * in-firmware A/B OTA works (recovery / meshcomod set otadata to the
         * target slot before raising the flag). Falls back to ota_0 if otadata
         * is empty/invalid. The DEFAULT (no flag) is always factory = recovery,
         * so recovery-first holds for ANY app in a slot — Meshtastic included,
         * since the bootloader, not the app, enforces the return to recovery. */
        bootloader_flash_erase_range(BOOTSEL_OFFSET, BOOTSEL_SIZE);
        boot_index = bootloader_utility_get_selected_boot_partition(&bs);
        if (boot_index == INVALID_INDEX || boot_index == FACTORY_INDEX) boot_index = 0;
        esp_rom_printf("[boot] launch flag set -> booting ota slot %d\n", boot_index);
    } else {
        boot_index = FACTORY_INDEX;           /* recovery launcher (default) */
        esp_rom_printf("[boot] -> factory (recovery launcher)\n");
    }

    bootloader_utility_load_boot_image(&bs, boot_index);
}

#if CONFIG_LIBC_NEWLIB
/* Return global reent struct if any newlib functions are linked to the bootloader. */
struct _reent *__getreent(void)
{
    return _GLOBAL_REENT;
}
#endif
