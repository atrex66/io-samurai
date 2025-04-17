#include "main.h"

bool i2c_check_address(i2c_inst_t *i2c, uint8_t addr) {
    uint8_t buffer[1] = {0x00};
    int ret = i2c_write_blocking(i2c, addr, buffer, 1, false);
    if (ret >= 0) {
        return true;
    } else {
        return false;
    }
}

// -------------------------------------------
// Core 1 Entry Point (I2C, LCD, MCP23017, MCP23008)
// -------------------------------------------
void core1_entry() {
    uint8_t conf;
    bool lcd = false;
    i2c_setup();
    sleep_ms(100);
    printf("Detecting SH1106 on %#x address\n", SH1106_ADDR);
    lcd = i2c_check_address(i2c1, SH1106_ADDR);
    if (lcd) {
        printf("SH1106 Init\n");
        sh1106_init();    
    }
    else {
        printf("No SH1106 found on %#x address.\n", SH1106_ADDR);
    }

#ifdef MCP23008_ADDR
    printf("MCP23008 Init\n");
    mcp_write_register(MCP23008_ADDR, 0x00, 0x00); // IODIR = 0xFF (Inputs)
#endif

    // beallitjuk az MCP23017-et bemenetnek
#ifdef MCP23017_ADDR
    printf("MCP23017 Init\n");
    mcp_write_register(MCP23017_ADDR, 0x00, 0xff);   // GPIO-A Input
    mcp_write_register(MCP23017_ADDR, 0x01, 0xff);   // GPIO-B Input
#endif

    printf("Ready...\n");

    // Main loop for core1
    while (1) {
        // LED Indicating timeout status
        gpio_put(LED_PIN, !timeout_error);

#ifdef MCP23008_ADDR
        // when not checksum error or timeout error writing out the outputs
        if (checksum_error == 0 && timeout_error == 0) {
            mcp_write_register(MCP23008_ADDR, 0x09, rx_buffer[0]);
        }
        else
        {
            mcp_write_register(MCP23008_ADDR, 0x09, 0x00); // Writing zeros when error
        }
#endif

#ifdef MCP23017_ADDR
        //Reading the GPIO-A and GPIO-B ports from the MCP23017 and writing them to the temp_tx_buffer
        temp_tx_buffer[0] = mcp_read_register(MCP23017_ADDR, 0x13); // beolvassuk az MCP23017 GPIO-B portrol az also input sort 0-7
        temp_tx_buffer[1] = mcp_read_register(MCP23017_ADDR, 0x12); // beolvassuk az MCP23017 GPIO-A portrol az felso input sort 8-15
        temp_tx_buffer[2] = 0;
        temp_tx_buffer[3] = 0;

#endif
        // when oled connected (this slows down the program so use only when needed)
        if (lcd){
            // lcd clear
            sh1106_clear();
            // draw output and input bits
            draw_bytes(rx_buffer[0], 0, 0, 0); 
            draw_bytes(tx_buffer[0], tx_buffer[1], 0, 18); 
            draw_text("0123456789ABCDEF", 0, 9);
            draw_text("io-samurai \x01", 0, 32);
            char ip_str[16]; // this holds the IP address
            if (checksum_error == 0){
                if (src_ip[0] != 0) {
                    // IP to string conversion (remote ip)
                    sprintf(ip_str, "%d.%d.%d.%d", src_ip[0], src_ip[1], src_ip[2], src_ip[3]);
                
                } else {
                    // IP to string conversion (local ip)
                    sprintf(ip_str, "%d.%d.%d.%d", net_info.ip[0], net_info.ip[1], net_info.ip[2], net_info.ip[3]);
                }
                draw_text(ip_str, 0, 44);
                if (timeout_error == 1) {
                    draw_text("Timeout error", 0, 54);
                }
            }
            else {
                draw_text("Checksum error", 0, 54);
            }
            if (checksum_error == 0 && timeout_error == 0) {
                sprintf(ip_str, "Connected.");
                draw_text(ip_str, 0, 54);
            }
            sh1106_update();
            }
        // Check for timeout
        if (time_diff > TIMEOUT_US) {
            checksum_index = 1;
            checksum_index_in = 1;
            timeout_error = 1;
        }
        else {
            timeout_error = 0;
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

    rx_buffer[2] = jump_table[1]; // initial checksum

    printf("\033[2J"); // ANSI escape kód a képernyő törléséhez
    printf("\033[H");  // kurzor vissza az elejére (0,0 pozíció)
    
    printf("\n\n--- W5100S Init Start ---\n");

    // SYSCLK beállítása 160 MHz-re
    set_sys_clock_khz(125000, true);
    
    // CLK_PERI beállítása SYSCLK-ra (160 MHz)
    clock_configure(clk_peri,
                    0,
                    CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                    clock_get_hz(clk_sys),
                    clock_get_hz(clk_sys));

    load_from_flash(&net_info);

    // SPI 40 MHz
    spi_init(spi0, 40000000);
    hw_write_masked(&spi_get_hw(spi0)->cr0, (0) << SPI_SSPCR0_SCR_LSB, SPI_SSPCR0_SCR_BITS); // SCR = 0
    hw_write_masked(&spi_get_hw(spi0)->cpsr, 4, SPI_SSPCPSR_CPSDVSR_BITS); // CPSDVSR = 4
    
    // reset IO port expanders
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


    // W5100S Init
    w5100s_init();

    printf("W5100S Init Done\n");
    w5100s_interrupt_init();
    printf("W5100S Interrupt Init Done\n");
    network_init();
    printf("Network Init Done\n");
    printf("IP: %d.%d.%d.%d\n", net_info.ip[0], net_info.ip[1], net_info.ip[2], net_info.ip[3]);

    // Core1 launch
    multicore_launch_core1(core1_entry);

    // clear the RX and TX buffer
    setSn_CR(0, Sn_CR_CLOSE);
    setSn_CR(0, Sn_CR_OPEN);

    // UDP socket create
    uint8_t sock_num = 0;
    socket(sock_num, Sn_MR_UDP, UDPPort, 0);

    while(1) {
        handle_udp();
        asm volatile("" ::: "memory");
        }
}

// process the command, and print the result (ip, ip x.x.x.x)
void process_command(char* command) {
    printf("Command: %s\n", command);
    if (strcmp(command, "ip") == 0) {
        printf("IP: %d.%d.%d.%d\n", net_info.ip[0], net_info.ip[1], net_info.ip[2], net_info.ip[3]);
    } 
    else if (strcmp(command, "timeout") == 0){
        printf("Timeout: %d\n", TIMEOUT_US);
    }
    else if (strncmp(command, "timeout ", 8) == 0) {
        int timeout;
        if (sscanf(command, "timeout %d", &timeout) == 1) {
            //TIMEOUT_US = timeout;
            printf("Timeout changed to %d\n", timeout);
            printf("Saving to flash, please reboot after save....\n");
            // save_to_flash(&TIMEOUT_US);
            reset_with_watchdog();
        }
        else {
            printf("Invalid timeout format\n");
        }
    }
    else if (strncmp(command, "ip ", 3) == 0) {
        int ip0, ip1, ip2, ip3;
        if (sscanf(command, "ip %d.%d.%d.%d", &ip0, &ip1, &ip2, &ip3) == 4) {
            net_info.ip[0] = ip0;
            net_info.ip[1] = ip1;
            net_info.ip[2] = ip2;
            net_info.ip[3] = ip3;
            // wizchip_setnetinfo(&net_info);
            printf("IP changed to %d.%d.%d.%d\n", ip0, ip1, ip2, ip3);
            printf("Saving to flash, please reboot after save....\n");
            save_to_flash(&net_info);
            reset_with_watchdog();
        }
        else {
            printf("Invalid IP format\n");
        }
    } else if (strcmp(command, "reset") == 0) {
        reset_with_watchdog();
    } else {
        printf("Unknown command\n");
    }
}

void reset_with_watchdog() {
    watchdog_enable(1, 1);
    while(1);
}

// -------------------------------------------
// Optimalizált SPI függvények
// -------------------------------------------
void __time_critical_func(cs_select)() {
    gpio_put(PIN_CS, 0);
    asm volatile("nop \n nop \n nop"); // 3 ciklus késleltetés
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

int32_t _sendto(uint8_t sn, uint8_t *buf, uint16_t len, uint8_t *addr, uint16_t port) {
    uint16_t freesize;
    uint32_t taddr;

    // Cél IP cím és port beállítása
    if (first_send) {
        setSn_DIPR(sn, addr);
        setSn_DPORT(sn, port);
        first_send = 0;
    }

    // Adatok küldése
    wiz_send_data(sn, buf, len);
    setSn_CR(sn, Sn_CR_SEND);
    while (getSn_CR(sn));  // Várakozás a parancs végrehajtására

    // Várakozás a küldés sikerességére
    while (1) {
        uint8_t tmp = getSn_IR(sn);
        if (tmp & Sn_IR_SENDOK) {
            setSn_IR(sn, Sn_IR_SENDOK);  // Küldés sikerült
            break;
        } else if (tmp & Sn_IR_TIMEOUT) {
            setSn_IR(sn, Sn_IR_TIMEOUT);  // Időtúllépés
            return SOCKERR_TIMEOUT;
        }
    }
    return (int32_t)len;  // Sikeresen elküldött adatok hossza
}

int32_t _recvfrom(uint8_t sn, uint8_t *buf, uint16_t len, uint8_t *addr, uint16_t *port) {
    uint8_t head[8];  // UDP fejléc: 8 bájt (IP + port + hossz)
    uint16_t pack_len = 0;

     // Várakozás, amíg adat érkezik
    while ((pack_len = getSn_RX_RSR(sn)) == 0) {
        if (getSn_SR(sn) == SOCK_CLOSED) return SOCKERR_SOCKCLOSED;
    }

    // UDP fejléc beolvasása
    wiz_recv_data(sn, head, 8);
    setSn_CR(sn, Sn_CR_RECV);
    while (getSn_CR(sn));  // Várakozás a parancs végrehajtására

    // Cím és port kinyerése a fejlécből
    addr[0] = head[0];
    addr[1] = head[1];
    addr[2] = head[2];
    addr[3] = head[3];
    *port = (head[4] << 8) | head[5];
 
    // Csomag hosszának kinyerése
    uint16_t data_len = (head[6] << 8) | head[7];

    // Csomag hosszának ellenőrzése
    if (len < data_len) pack_len = len;
    else pack_len = data_len;

    // Adatok beolvasása
    wiz_recv_data(sn, buf, pack_len);
    setSn_CR(sn, Sn_CR_RECV);
    while (getSn_CR(sn));  // Várakozás a parancs végrehajtására

    return (int32_t)pack_len;  // Fogadott adatok hossza
}

void __time_critical_func(calculate_checksum)(uint8_t *data, uint8_t len) {
    uint8_t sum = 0;
    for (uint8_t i = 0; i < len; i++) {
        sum += data[i];
    }
    data[len] = sum;
}

void jump_table_checksum() {
    if (checksum_error == 0) {
        checksum_index += (uint8_t)(rx_buffer[0] + rx_buffer[1] + 1);
        uint8_t checksum = jump_table[checksum_index];
        if (checksum != rx_buffer[2]) {
            // Használj checksum-ot itt
            checksum_error = 1;
        }
    }
}

void jump_table_checksum_in() {
    checksum_index_in += (uint8_t)(tx_buffer[0] + tx_buffer[1] + tx_buffer[2] + tx_buffer[3] + 1);
    tx_buffer[4] = jump_table[checksum_index_in];
}

// -------------------------------------------
// UDP handler
// -------------------------------------------
void __time_critical_func(handle_udp)() {
    
    while(gpio_get(IRQ_PIN) == 0);  // Várakozás az interruptra
    time_diff = absolute_time_diff_us(last_packet_time, get_absolute_time());
    // Non-blocking adatellenőrzés
    if(getSn_RX_RSR(0) != 0) {
        counter++;
        int len = _recvfrom(0, rx_buffer, rx_size, src_ip, &src_port);
        if (len > 0) {
            // checksum error detection
            jump_table_checksum();
            last_packet_time = get_absolute_time();
        }
        memcpy(tx_buffer, temp_tx_buffer, 5); // copy the temp buffer to tx_buffer
        // checksum generation
        jump_table_checksum_in();
        _sendto(0, tx_buffer, tx_size, src_ip, src_port);
    }
}

void w5100s_interrupt_init() {
    gpio_init(INT_PIN);
    gpio_set_dir(INT_PIN, GPIO_IN);
    gpio_pull_up(INT_PIN);
    
    // W5100S interrupt beállítások
    uint8_t imr = IMR_RECV;        
    uint8_t sn_imr = Sn_IMR_RECV;  
    
    // socket interrupt definition
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

    // DMA init when using
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
    wizchip_init(0, 0);
    wizchip_setnetinfo(&net_info);
    }

// input a command from the serial console
void handle_serial_input() {
    // receive a character with timeout
    char inByte = getchar_timeout_us(0);
    if (inByte == PICO_ERROR_TIMEOUT) {
        return;
    }
    if (inByte != '\r'){
        if (inByte < 31 || inByte > 126 ) {
            return;
        }
    }
    printf("%c", inByte);
    //Message coming in (check not terminating character) and guard for over message size
    if ( inByte != '\r' && (buffer_pos < 63) )
    {
        //Add the incoming byte to our message
        buffer[buffer_pos] = inByte;
        buffer_pos++;
    }
    //Full message received...
    else
    {
        printf("\n");
        //Add null character to string
        buffer[buffer_pos] = '\0';
        //Process the command
        process_command(buffer);
        //Reset for the next message
        buffer_pos = 0;
    }
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