#include <stdio.h>
#include <string.h>
#include "wizchip_conf.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"
#include "config.h"

// Puffer a parancsokhoz
char buffer[64];
int buffer_pos = 0;
extern wiz_NetInfo net_info;
extern uint32_t TIMEOUT_US;

extern void reset_with_watchdog();

void handle_serial_input();
void process_command(char* command); // Csak char* command, mert net_info globális

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