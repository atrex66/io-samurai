#include <stdint.h>
#include "wizchip_conf.h" // W5100S/W5500 könyvtárból
#include "hardware/flash.h"
#include "pico/stdlib.h"
#include "pico/sync.h"
#include "wizchip_conf.h"
#include <string.h>
#include <stdio.h>
#include "config.h"

// Default network configuration
const wiz_NetInfo default_net_info = {
    .mac = {0x00, 0x08, 0xDC, 0x12, 0x34, 0x56},
    .ip = {192, 168, 0, 177},
    .sn = {255, 255, 255, 0},
    .gw = {192, 168, 0, 1},
    .dns = {8, 8, 8, 8},
    .dhcp = 1 // Static IP
};

// UDP port
uint16_t port = 8888;

// Flash tárolási beállítások
#define FLASH_TARGET_OFFSET (2097152 - 8192) // Utolsó 8 KB kezdete
#define FLASH_DATA_SIZE (sizeof(wiz_NetInfo) + 1) // wiz_NetInfo + 1 bájt checksum
const uint8_t* flash_target_contents = (const uint8_t*)(XIP_BASE + FLASH_TARGET_OFFSET);

// Checksum kiszámítása (egyszerű összeadás modulo 256)
static uint8_t calculate_checksum(const wiz_NetInfo* net_info) {
    uint8_t sum = 0;
    const uint8_t* data = (const uint8_t*)net_info;
    for (size_t i = 0; i < sizeof(wiz_NetInfo); i++) {
        sum += data[i];
    }
    return sum;
}

void save_to_flash(const wiz_NetInfo* net_info) {
    uint8_t data[FLASH_DATA_SIZE];
    memcpy(data, net_info, sizeof(wiz_NetInfo));
    data[sizeof(wiz_NetInfo)] = calculate_checksum(net_info);

    // Interrupt tiltás elhagyása tesztelésre
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_TARGET_OFFSET, data, FLASH_DATA_SIZE);

    printf("Network config saved to flash at offset %u, checksum: %d\r\n", 
           FLASH_TARGET_OFFSET, data[sizeof(wiz_NetInfo)]);
}


void load_from_flash(wiz_NetInfo* net_info) {
    const uint8_t* flash_data = flash_target_contents;
    
    // Checksum ellenőrzés
    uint8_t stored_checksum = flash_data[sizeof(wiz_NetInfo)];
    uint8_t calculated_checksum = calculate_checksum((wiz_NetInfo*)flash_data);

    if (stored_checksum == calculated_checksum) {
        memcpy(net_info, flash_data, sizeof(wiz_NetInfo));
        printf("Loaded from flash: IP {%d.%d.%d.%d}, DHCP mode: %d\r\n",
               net_info->ip[0], net_info->ip[1], 
               net_info->ip[2], net_info->ip[3], net_info->dhcp);
    } else {
        printf("Checksum mismatch (stored: %d, calculated: %d), loading defaults\r\n",
               stored_checksum, calculated_checksum);
        *net_info = default_net_info;
    }
}