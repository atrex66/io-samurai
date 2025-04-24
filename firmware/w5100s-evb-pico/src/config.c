#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "wizchip_conf.h" // W5100S/W5500 könyvtárból
#include "hardware/flash.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include "pico/sync.h"
#include "wizchip_conf.h"
#include <string.h>
#include <stdio.h>
#include "config.h"

extern void reset_with_watchdog();

configuration_t default_config = {
    .mac = {0x00, 0x08, 0xDC, 0x12, 0x34, 0x56},
    .ip = {192, 168, 0, 177},
    .sn = {255, 255, 255, 0},
    .gw = {192, 168, 0, 1},
    .dns = {8, 8, 8, 8},
    .dhcp = 1, // satic IP address
    .port = 8888,
    .timeout = 100000, // 100000 uS = 100 ms
};

// UDP port
uint16_t port = 0;
configuration_t *flash_config = NULL;

// Flash tárolási beállítások
#define FLASH_TARGET_OFFSET (2097152 - 8192) // Utolsó 8 KB kezdete
#define FLASH_DATA_SIZE (sizeof(configuration_t)) // wiz_NetInfo + 1 bájt checksum

void __time_critical_func(save_config_to_flash()) {
    if (flash_config == NULL) {
        printf("Nothing to save flash_config is empty.\n");
    }
    uint8_t data[FLASH_PAGE_SIZE] __attribute__((aligned(4))) = {0xFF};
    if (data == NULL) {
        printf("Failed to allocate memory for flash data.\n");
        return;
    }
    
    // Spinlock foglalása a core1 várakoztatásához
    watchdog_enable(1000, 1);

    uint32_t status = save_and_disable_interrupts();
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
    memcpy(data, flash_config, sizeof(configuration_t)); // Másoljuk át a konfigurációt
    flash_range_program(FLASH_TARGET_OFFSET, data, FLASH_PAGE_SIZE);
    restore_interrupts(status);
    while (1)
    {
        /* code */
    }
}

void load_config_from_flash() {
    if (flash_config == NULL) {
        flash_config = (configuration_t *)malloc(sizeof(configuration_t));
    }
    memcpy(flash_config, (configuration_t *)(XIP_BASE + FLASH_TARGET_OFFSET), sizeof(configuration_t));
    if (flash_config->dhcp < 1 || flash_config->dhcp > 2) {
        printf("Invalid DHCP mode restore config.\n");
        restore_default_config();
        save_config_to_flash();
    }
}

void restore_default_config() {
    if (flash_config == NULL) {
        flash_config = (configuration_t *)malloc(sizeof(configuration_t));
    }
    memcpy(flash_config, &default_config, sizeof(configuration_t));
    save_config_to_flash();
}
