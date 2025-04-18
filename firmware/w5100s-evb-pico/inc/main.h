#ifndef MAIN_H
#define MAIN_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "pico/multicore.h"
#include "pico/binary_info.h"
#include "pico/time.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "hardware/watchdog.h"
#include "hardware/clocks.h"
#include "wizchip_conf.h"
#include "socket.h"
#include "config.h"
#include "sh1106.h"
#include "jump_table.h"

// -------------------------------------------
// Hardware Defines (Módosítsd a pinjeidnek megfelelően!)
// -------------------------------------------
#define SPI_PORT        spi0
#define PIN_MISO        16
#define PIN_CS          17
#define PIN_SCK         18
#define PIN_MOSI        19
#define PIN_RESET       20

#define MCP_ALL_RESET   14
#define MCP_ALL_I2C_SDA 2
#define MCP_ALL_I2C_SCK 3
#define MCP23017_INTA   6
#define MCP23017_INTB   7

#define IODIR           0x00
#define GPIO            0x09

#define MCP23017_ADDR   0x21
#define MCP23008_ADDR   0x20

#define USE_SPI_DMA     1

#define IRQ_PIN         21
#define LED_PIN         PICO_DEFAULT_LED_PIN

#define IMR_RECV      0x04
#define Sn_IMR_RECV   0x04
#define Sn_IR_RECV    0x04
#define SOCKET_DHCP   0

#define rx_size       3
#define tx_size       5

// Interrupt konfiguráció
#define INT_PIN 21
#define core1_running 1

// Low-pass filter parameters
#define ALPHA 0.25f // Smoothing factor (0.0 to 1.0, lower = more smoothing)

// -------------------------------------------
// Network Configuration
// -------------------------------------------
wiz_NetInfo net_info = {
    .mac = {0x00, 0x08, 0xDC, 0x11, 0x22, 0x33}, // Módosítsd!
    .ip = {192, 168, 0, 177},
    .sn = {255, 255, 255, 0},
    .gw = {192, 168, 0, 1},
    .dns = {8, 8, 8, 8},
    .dhcp = NETINFO_STATIC
};

#define UDPPort 8888

uint8_t checksum_lut[256] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    // ... (többi érték)
    0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF
};

// -------------------------------------------
// Globális változók
// -------------------------------------------
uint8_t rx_buffer[rx_size] = {0, 0, 0x5e};
uint8_t tx_buffer[tx_size] = {0};
uint8_t counter = 0;
uint8_t first_send = 1;
// Puffer a parancsokhoz
char buffer[64];
int buffer_pos = 0;

#ifdef USE_SPI_DMA
static uint dma_tx;
static uint dma_rx;
static dma_channel_config dma_channel_config_tx;
static dma_channel_config dma_channel_config_rx;
#endif

// IP cím tárolása uint8_t tömbben
uint8_t ip_address[4] = {0, 0, 0, 0}; // {192, 168, 0, 177} formátum
uint16_t port = 8888;

uint8_t src_ip[4];
uint16_t src_port;

// Data integrity variables
uint8_t checksum_error = 0;
uint8_t timeout_error = 0;
uint32_t last_time = 0;
static absolute_time_t last_packet_time;
static const uint32_t TIMEOUT_US = 100000; // 100 ms = 100000 us
uint32_t time_diff;
uint8_t temp_tx_buffer[5] = {0x00, 0x00, 0x00, 0x00, 0x04};

// -------------------------------------------
// Globális változók a magok közötti kommunikációhoz
// -------------------------------------------
// Függvény deklarációk
void jump_table_checksum();
void jump_table_checksum_in();
void i2c_setup(void);
uint8_t mcp_read_register(uint8_t i2c_addr, uint8_t reg);
void mcp_write_register(uint8_t i2c_addr, uint8_t reg, uint8_t value);
void cs_select();
void cs_deselect();
uint8_t spi_read();
void reset_with_watchdog();
void spi_write(uint8_t data);
int32_t _sendto(uint8_t sn, uint8_t *buf, uint16_t len, uint8_t *addr, uint16_t port);
int32_t _recvfrom(uint8_t sn, uint8_t *buf, uint16_t len, uint8_t *addr, uint16_t *port);
void handle_udp();
void w5100s_interrupt_init();
void w5100s_init();
void network_init();
void calculate_checksum(uint8_t *data, uint8_t len);
uint8_t xor_checksum(const uint8_t *data, uint8_t len);
void core1_entry();
void handle_serial_input();
void process_command(char* command); // Csak char* command, mert net_info globális
#if USE_SPI_DMA
static void wizchip_write_burst(uint8_t *pBuf, uint16_t len);
static void wizchip_read_burst(uint8_t *pBuf, uint16_t len);
#endif

#endif // MAIN_H