#ifndef MAIN_H
#define MAIN_H

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
bool i2c_check_address(i2c_inst_t *i2c, uint8_t addr);
float low_pass_filter(float new_sample, float previous_filtered, bool *first_sample);

#if USE_SPI_DMA
static void wizchip_write_burst(uint8_t *pBuf, uint16_t len);
static void wizchip_read_burst(uint8_t *pBuf, uint16_t len);
#endif

#endif // MAIN_H