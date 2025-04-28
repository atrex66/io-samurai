#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "pico/multicore.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "hardware/watchdog.h"
#include "hardware/clocks.h"
#include "serial_terminal.h"
#include "wizchip_conf.h"
#include "socket.h"
#include "sh1106.h"
#include "jump_table.h"
#include "main.h"
#include "config.h"

// Author:Viola Zsolt (atrex66@gmail.com)
// Date: 2025
// Description: IO-Samurai V1.0 driver for Raspberry Pi Pico + W5100S (W5100S-EVB-PICO)
// License: MIT
// Description: This code is a driver for the IO-Samurai V1.0 board, which uses a Raspberry Pi Pico and a W5100S Ethernet chip.
// It also includes a serial terminal interface for configuration and debugging.
// The code is designed to run on the Raspberry Pi Pico and uses the Pico SDK for hardware access.
// The code is structured to run on two cores, with core 0 handling network communication and core 1 handling GPIO, ADC, Oled and terminal config.
// The code is using DMA for SPI (burst) communication with the W5100S chip, which allows for high-speed data transfer.
// do not chase errors in the local network when the W5100S only run in 10Mbit/s mode, it's not necessary higher data rate, the driver capable over 6000 reads+writes per second (utitlity/benchmark.c).
// The code uses the Wiznet W5100S library for network communication.
// The code includes functions for initializing the hardware, handling network communication, and processing commands from the serial terminal.
// The code is designed to be modular and extensible, allowing for easy addition of new features and functionality.
// The code is also designed to be efficient and responsive, with low latency and high throughput.
// Note: checksum algorithm is used to ensure data integrity, and the code includes error handling for network communication. (timeout + jumpcode checksum)
// Note: The code is disables the terminal when the HAL driver connect to the io-samurai board, and enables when not running.

// -------------------------------------------
// Network Configuration
// -------------------------------------------
extern wiz_NetInfo default_net_info;
extern uint16_t port;
extern configuration_t *flash_config;
extern uint16_t adc_min;
extern uint16_t adc_max;
wiz_NetInfo net_info;

uint8_t rx_buffer[rx_size] = {0,};
uint8_t temp_tx_buffer[tx_size] = {0,};
uint8_t tx_buffer[tx_size] = {0,};
uint8_t counter = 0;
uint8_t first_send = 1;

#ifdef USE_SPI_DMA
static uint dma_tx;
static uint dma_rx;
static dma_channel_config dma_channel_config_tx;
static dma_channel_config dma_channel_config_rx;
#endif

uint8_t src_ip[4];
uint16_t src_port;

uint8_t checksum_error = 0;
uint8_t timeout_error = 0;
uint32_t last_time = 0;
static absolute_time_t last_packet_time;
uint32_t TIMEOUT_US = 100000;
uint32_t time_diff;


// -------------------------------------------
// Core 1 Entry Point (I2C, LCD, MCP23017, MCP23008)
// -------------------------------------------
void core1_entry() {
    int16_t result;
    uint8_t conf;
    bool MCP23008_present = false;
    bool MCP23017_present = false;
    float filtered_adc = 0.0f;
    bool first_sample = true;

    bool lcd = false;

    i2c_setup();
    sleep_ms(100);
    printf("Detecting SH1106 (OLED display) on %#x address\n", SH1106_ADDR);
    lcd = i2c_check_address(i2c1, SH1106_ADDR);
    if (lcd) {
        printf("SH1106 Init (OLED)\n");
        sh1106_init();    
    }
    else {
        printf("No SH1106 (OLED display) found on %#x address.\n", SH1106_ADDR);
    }

#ifdef MCP23008_ADDR
    printf("Detecting MCP23008 (Outputs) on %#x address\n", MCP23008_ADDR);
    if (i2c_check_address(i2c1, MCP23008_ADDR)) {
        MCP23008_present = true;
        printf("MCP23008 (Outputs) Init\n");
        mcp_write_register(MCP23008_ADDR, 0x00, 0x00);
    }
    else {
        printf("No MCP23008 (Outputs) found on %#x address.\n", MCP23008_ADDR);
    }

#endif

#ifdef MCP23017_ADDR
    printf("Detecting MCP23017 (Inputs) on %#x address\n", MCP23017_ADDR);
    if (i2c_check_address(i2c1, MCP23017_ADDR)) {
        MCP23017_present = true;
        printf("MCP23017 (Inputs) Init\n");
        mcp_write_register(MCP23017_ADDR, 0x00, 0xff);
        mcp_write_register(MCP23017_ADDR, 0x01, 0xff);
    }
    else {
        printf("No MCP23017 (Inputs) found on %#x address.\n", MCP23017_ADDR);
    }
#endif

    printf("Ready...\n");
    while (1) {
        gpio_put(LED_PIN, !timeout_error);

        if (time_diff > TIMEOUT_US) {
            checksum_index = 1;
            checksum_index_in = 1;
            timeout_error = 1;
            checksum_error = 0;
            src_ip[0] = 0;
        }
        else {
            timeout_error = 0;
        }

#ifdef MCP23008_ADDR
        if (checksum_error == 0 && timeout_error == 0) {
            mcp_write_register(MCP23008_ADDR, 0x09, rx_buffer[0]);
        }
        else
        {
            rx_buffer[0] = 0x00;
            mcp_write_register(MCP23008_ADDR, 0x09, 0x00);
        }
#endif

        result = scale_value(adc_read());
        float voltage = (float) result;

        if (rx_buffer[1] && 0x01){
            filtered_adc = low_pass_filter(voltage, filtered_adc, &first_sample);
        }
        else {
            filtered_adc = result;
        }
        

#ifdef MCP23017_ADDR
        memset(temp_tx_buffer, 0, sizeof(temp_tx_buffer));
        temp_tx_buffer[0] = mcp_read_register(MCP23017_ADDR, 0x13);
        temp_tx_buffer[1] = mcp_read_register(MCP23017_ADDR, 0x12);
        temp_tx_buffer[2] = (uint16_t)filtered_adc & 0xFF;
        temp_tx_buffer[3] = (uint16_t)filtered_adc >> 8;
        temp_tx_buffer[3] |= MCP23008_present ? 0x80 : 0x00;
        temp_tx_buffer[3] |= MCP23017_present ? 0x40 : 0x00;
        temp_tx_buffer[3] |= lcd ? 0x20 : 0x00;

#endif
        if (lcd){
            char txt_buff[16];
            sh1106_clear();
            draw_bytes(rx_buffer[0], 0, 0, 0); 
            draw_text("0123456789ABCDEF", 0, 9);
            draw_bytes(tx_buffer[0], tx_buffer[1], 0, 18); 
            sprintf(txt_buff, "ADC: %d", (uint16_t)filtered_adc);
            draw_text(txt_buff, 0, 32);
            if (checksum_error == 0){
                if (src_ip[0] != 0) {
                    sprintf(txt_buff, "%d.%d.%d.%d", src_ip[0], src_ip[1], src_ip[2], src_ip[3]);
                
                } else {
                    sprintf(txt_buff, "%d.%d.%d.%d", net_info.ip[0], net_info.ip[1], net_info.ip[2], net_info.ip[3]);
                }
                draw_text(txt_buff, 0, 44);
                if (timeout_error == 1) {
                    draw_text("Timeout error", 0, 54);
                }
            }
            else {
                draw_text("Checksum error", 0, 54);
            }
            if (checksum_error == 0 && timeout_error == 0) {
                sprintf(txt_buff, "Connected.");
                draw_text(txt_buff, 0, 54);
            }
            sh1106_update();
            }

        handle_serial_input();
        }
}

// -------------------------------------------
// (Core 0) UDP communication (DMA, SPI)
// -------------------------------------------
int main() {

    stdio_init_all();
    stdio_usb_init();
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
   
    sleep_ms(2000);


    printf("\033[2J");
    printf("\033[H");
    
    printf("\n\n--- IO-Samurai V1.0 ---\n");
    printf("Viola Zsolt 2025\n");
    printf("E-mail:atrex66@gmail.com\n");
    printf("\n");
    set_sys_clock_khz(125000, true);
    
    clock_configure(clk_peri,
                    0,
                    CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                    clock_get_hz(clk_sys),
                    clock_get_hz(clk_sys));

    // SPI 40 MHz
    spi_init(spi0, 40000000);
    hw_write_masked(&spi_get_hw(spi0)->cr0, (0) << SPI_SSPCR0_SCR_LSB, SPI_SSPCR0_SCR_BITS); // SCR = 0
    hw_write_masked(&spi_get_hw(spi0)->cpsr, 4, SPI_SSPCPSR_CPSDVSR_BITS); // CPSDVSR = 4
    
    gpio_init(MCP_ALL_RESET);
    gpio_set_dir(MCP_ALL_RESET, GPIO_OUT);
    gpio_put(MCP_ALL_RESET, 0);
    sleep_ms(10);
    gpio_put(MCP_ALL_RESET, 1);

    gpio_init(MCP23017_INTA);
    gpio_init(MCP23017_INTB);
    gpio_set_dir(MCP23017_INTA, GPIO_IN);
    gpio_set_dir(MCP23017_INTB, GPIO_IN);
    gpio_pull_up(MCP23017_INTA);
    gpio_pull_up(MCP23017_INTB);

    w5100s_init();
    w5100s_interrupt_init();
    adc_init();
    adc_gpio_init(26);
    adc_select_input(0);
    load_configuration();
    network_init();
    multicore_launch_core1(core1_entry);
    handle_udp();
}

void reset_with_watchdog() {
    watchdog_enable(1, 1);
    while(1);
}

void __time_critical_func(cs_select)() {
    gpio_put(PIN_CS, 0);
    asm volatile("nop \n nop \n nop");
}

void __time_critical_func(cs_deselect)() {
    gpio_put(PIN_CS, 1);
}

uint8_t __time_critical_func(spi_read)() {
    uint8_t data;
    spi_read_blocking(SPI_PORT, 0x00, &data, 1);
    return data;
}

void __time_critical_func(spi_write)(uint8_t data) {
    spi_write_blocking(SPI_PORT, &data, 1);
}

int32_t __time_critical_func(_sendto)(uint8_t sn, uint8_t *buf, uint16_t len, uint8_t *addr, uint16_t port) {
    uint16_t freesize;
    uint32_t taddr;

    if (first_send) {
        setSn_DIPR(sn, addr);
        setSn_DPORT(sn, port);
        first_send = 0;
    }

    wiz_send_data(sn, buf, len);
    setSn_CR(sn, Sn_CR_SEND);
    while (getSn_CR(sn));

    while (1) {
        uint8_t tmp = getSn_IR(sn);
        if (tmp & Sn_IR_SENDOK) {
            setSn_IR(sn, Sn_IR_SENDOK);
            break;
        } else if (tmp & Sn_IR_TIMEOUT) {
            setSn_IR(sn, Sn_IR_TIMEOUT);
            return SOCKERR_TIMEOUT;
        }
    }
    return (int32_t)len;
}

int32_t __time_critical_func(_recvfrom)(uint8_t sn, uint8_t *buf, uint16_t len, uint8_t *addr, uint16_t *port) {
    uint8_t head[8];
    uint16_t pack_len = 0;

    while ((pack_len = getSn_RX_RSR(sn)) == 0) {
        if (getSn_SR(sn) == SOCK_CLOSED) return SOCKERR_SOCKCLOSED;
    }

    wiz_recv_data(sn, head, 8);
    setSn_CR(sn, Sn_CR_RECV);
    while (getSn_CR(sn));

    addr[0] = head[0];
    addr[1] = head[1];
    addr[2] = head[2];
    addr[3] = head[3];
    *port = (head[4] << 8) | head[5];
 
    uint16_t data_len = (head[6] << 8) | head[7];

    if (len < data_len) pack_len = len;
    else pack_len = data_len;

    wiz_recv_data(sn, buf, pack_len);
    setSn_CR(sn, Sn_CR_RECV);
    while (getSn_CR(sn));

    return (int32_t)pack_len;
}

void __time_critical_func(calculate_checksum)(uint8_t *data, uint8_t len) {
    uint8_t sum = 0;
    for (uint8_t i = 0; i < len; i++) {
        sum += data[i];
    }
    data[len] = sum;
}

void __time_critical_func(jump_table_checksum)() {
    if (checksum_error == 0) {
        checksum_index += (uint8_t)(rx_buffer[0] + rx_buffer[1] + 1);
        uint8_t checksum = jump_table[checksum_index];
        if (checksum != rx_buffer[2]) {
            checksum_error = 1;
        }
    }
}

void __time_critical_func(jump_table_checksum_in)() {
    checksum_index_in += (uint8_t)(tx_buffer[0] + tx_buffer[1] + tx_buffer[2] + tx_buffer[3] + 1);
    tx_buffer[4] = jump_table[checksum_index_in];
}


void __not_in_flash_func(core0_wait)(void) {
    while (!multicore_fifo_wready()) {
        tight_loop_contents();
    }
    multicore_fifo_push_blocking(0xFEEDFACE);
    printf("Core0 is ready to write...\n");
    uint32_t signal = multicore_fifo_pop_blocking();
    if (signal == 0xDEADBEEF) {
        watchdog_reboot(0, 0, 0);
    }
}


// -------------------------------------------
// UDP handler
// -------------------------------------------
void handle_udp() {
    while (1){
        while(gpio_get(IRQ_PIN) == 0)
        {
            if (multicore_fifo_rvalid()) {
                break;
            }
        }
        time_diff = absolute_time_diff_us(last_packet_time, get_absolute_time());
        if(getSn_RX_RSR(0) != 0) {
            counter++;
            int len = _recvfrom(0, rx_buffer, rx_size, src_ip, &src_port);
            if (len > 0) {
                jump_table_checksum();
                last_packet_time = get_absolute_time();
            }
            memcpy(tx_buffer, temp_tx_buffer, 5);
            jump_table_checksum_in();
            _sendto(0, tx_buffer, tx_size, src_ip, src_port);
        }

        if (multicore_fifo_rvalid()) {
        uint32_t signal = multicore_fifo_pop_blocking();
        printf("Core1 signal: %08X\n", signal);
        if (signal == 0xCAFEBABE) {
            core0_wait();
            }
        }

    }
}

void w5100s_interrupt_init() {
    gpio_init(INT_PIN);
    gpio_set_dir(INT_PIN, GPIO_IN);
    gpio_pull_up(INT_PIN);
    
    uint8_t imr = IMR_RECV;        
    uint8_t sn_imr = Sn_IMR_RECV;  
    
    setIMR(imr);
    setSn_IMR(SOCKET_DHCP, sn_imr);
}

// -------------------------------------------
// W5100S Init
// -------------------------------------------
void w5100s_init() {

    // GPIO init
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    cs_deselect();

    gpio_init(PIN_RESET);
    gpio_set_dir(PIN_RESET, GPIO_OUT);
    gpio_put(PIN_RESET, 1);

    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
   
    reg_wizchip_cs_cbfunc(cs_select, cs_deselect);
    reg_wizchip_spi_cbfunc(spi_read, spi_write);
    
    #if USE_SPI_DMA
        reg_wizchip_spiburst_cbfunc(wizchip_read_burst, wizchip_write_burst);
    #endif

    gpio_put(PIN_RESET, 0);
    sleep_ms(100);
    gpio_put(PIN_RESET, 1);
    sleep_ms(500);

#ifdef USE_SPI_DMA
    dma_tx = dma_claim_unused_channel(true);
    dma_rx = dma_claim_unused_channel(true);

    dma_channel_config_tx = dma_channel_get_default_config(dma_tx);
    channel_config_set_transfer_data_size(&dma_channel_config_tx, DMA_SIZE_8);
    channel_config_set_dreq(&dma_channel_config_tx, DREQ_SPI0_TX);

    dma_channel_config_rx = dma_channel_get_default_config(dma_rx);
    channel_config_set_transfer_data_size(&dma_channel_config_rx, DMA_SIZE_8);
    channel_config_set_dreq(&dma_channel_config_rx, DREQ_SPI0_RX);
    channel_config_set_read_increment(&dma_channel_config_rx, false);
    channel_config_set_write_increment(&dma_channel_config_rx, true);
#endif

}

// -------------------------------------------
// Network Init
// -------------------------------------------
void network_init() {
    wiz_PhyConf phyconf;

    wizchip_init(0, 0);
    wizchip_setnetinfo(&net_info);

    setSn_CR(0, Sn_CR_CLOSE);
    setSn_CR(0, Sn_CR_OPEN);
    uint8_t sock_num = 0;
    socket(sock_num, Sn_MR_UDP, port, 0);

    printf("Network Init Done\n");
    wizchip_getnetinfo(&net_info);
    wizphy_getphyconf(&phyconf);
    printf("**************Network Info read from W5100S\n");
    printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", net_info.mac[0], net_info.mac[1], net_info.mac[2], net_info.mac[3], net_info.mac[4], net_info.mac[5]);
    printf("IP: %d.%d.%d.%d\n", net_info.ip[0], net_info.ip[1], net_info.ip[2], net_info.ip[3]);
    printf("Subnet: %d.%d.%d.%d\n", net_info.sn[0], net_info.sn[1], net_info.sn[2], net_info.sn[3]);
    printf("Gateway: %d.%d.%d.%d\n", net_info.gw[0], net_info.gw[1], net_info.gw[2], net_info.gw[3]);
    printf("DNS: %d.%d.%d.%d\n", net_info.dns[0], net_info.dns[1], net_info.dns[2], net_info.dns[3]);
    printf("DHCP: %d   (1-Static, 2-Dinamic)\n", net_info.dhcp);
    printf("PORT: %d\n", port);
    printf("*******************PHY status**************\n");
    printf("PHY Duplex: %s\n", phyconf.duplex == PHY_DUPLEX_FULL ? "Full" : "Half");
    printf("PHY Speed: %s\n", phyconf.speed == PHY_SPEED_100 ? "100Mbps" : "10Mbps");
    printf("*******************************************\n");
    }

#ifdef USE_SPI_DMA
static void wizchip_read_burst(uint8_t *pBuf, uint16_t len)
{
    uint8_t dummy_data = 0xFF;

    channel_config_set_read_increment(&dma_channel_config_tx, false);
    channel_config_set_write_increment(&dma_channel_config_tx, false);
    dma_channel_configure(dma_tx, &dma_channel_config_tx,
                          &spi_get_hw(SPI_PORT)->dr,
                          &dummy_data,              
                          len,                      
                          false);                   

    channel_config_set_read_increment(&dma_channel_config_rx, false);
    channel_config_set_write_increment(&dma_channel_config_rx, true);
    dma_channel_configure(dma_rx, &dma_channel_config_rx,
                          pBuf,                     
                          &spi_get_hw(SPI_PORT)->dr,
                          len,                      
                          false);                   

    dma_start_channel_mask((1u << dma_tx) | (1u << dma_rx));
    dma_channel_wait_for_finish_blocking(dma_rx);
}

static void wizchip_write_burst(uint8_t *pBuf, uint16_t len)
{
    uint8_t dummy_data;

    channel_config_set_read_increment(&dma_channel_config_tx, true);
    channel_config_set_write_increment(&dma_channel_config_tx, false);
    dma_channel_configure(dma_tx, &dma_channel_config_tx,
                          &spi_get_hw(SPI_PORT)->dr,
                          pBuf,                     
                          len,                      
                          false);                   

    channel_config_set_read_increment(&dma_channel_config_rx, false);
    channel_config_set_write_increment(&dma_channel_config_rx, false);
    dma_channel_configure(dma_rx, &dma_channel_config_rx,
                          &dummy_data,              
                          &spi_get_hw(SPI_PORT)->dr,
                          len,                      
                          false);                   

    dma_start_channel_mask((1u << dma_tx) | (1u << dma_rx));
    dma_channel_wait_for_finish_blocking(dma_rx);
}
#endif

void i2c_setup(void) {
    i2c_init(i2c1, 400 * 1000); // 400 kHz
    gpio_set_function(MCP_ALL_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(MCP_ALL_I2C_SCK, GPIO_FUNC_I2C);
    gpio_pull_up(MCP_ALL_I2C_SDA);
    gpio_pull_up(MCP_ALL_I2C_SCK);
}

uint8_t mcp_read_register(uint8_t i2c_addr, uint8_t reg) {
    uint8_t data;
    int ret;
    ret = i2c_write_blocking(i2c1, i2c_addr, &reg, 1, true);
    if (ret < 0) return 0xFF;
    ret = i2c_read_blocking(i2c1, i2c_addr, &data, 1, false);
    if (ret < 0) return 0xFF;
    return data;
}

void mcp_write_register(uint8_t i2c_addr, uint8_t reg, uint8_t value) {
    uint8_t data[2] = {reg, value};
    int ret = i2c_write_blocking(i2c1, i2c_addr, data, 2, false);
    if (ret < 0) {
        printf("I2C write failed %02X\n", i2c_addr);
        asm("nop");
    }
}

bool i2c_check_address(i2c_inst_t *i2c, uint8_t addr) {
    uint8_t buffer[1] = {0x00};
    int ret = i2c_write_blocking_until(i2c, addr, buffer, 1, false, make_timeout_time_us(1000));
    if (ret != PICO_ERROR_GENERIC) {
        return true;
    } else {
        return false;
    }
}

// Low-pass filter function (EMA)
float low_pass_filter(float new_sample, float previous_filtered, bool *first_sample) {
    if (*first_sample) {
        *first_sample = false;
        return new_sample; // Initialize with first sample
    }
    return ALPHA * new_sample + (1.0f - ALPHA) * previous_filtered;
}

typedef struct {
    float min;
    float max;
} minmax_t;

float scale_value(uint16_t xt) {
    minmax_t x;
    minmax_t y;

    y.min = 0.0f;
    y.max = 4095.0f;
    x.min = (float)adc_min;
    x.max = (float)adc_max;

    return (((float)xt - x.min) / (x.max - x.min)) * (y.max - y.min) + y.min;
}
