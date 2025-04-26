#include "rtapi.h"              /* RTAPI realtime OS API */
#include "rtapi_app.h"          /* RTAPI realtime module decls */
#include "rtapi_errno.h"        /* EINVAL etc */
#include "hal.h"                /* HAL public API decls */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <stdlib.h>
#include "../firmware/w5100s-evb-pico/inc/jump_table.h"

/* module information */
MODULE_AUTHOR("Viola Zsolt");
MODULE_DESCRIPTION("IO Samurai V1.0 driver");
MODULE_LICENSE("MIT");

// to parse the modparam
char *ip_address[128] = {0,};
RTAPI_MP_ARRAY_STRING(ip_address, 128, "Ip address");

#define ALPHA 0.1f  // Low-pass filter constant (EMA)
#define ADC_MAX 4095.0f // Maximum ADC value (12-bit resolution)
#define MAX_CHAN 8

typedef struct {
    char ip[16]; // Holds IPv4 address (max 15 characters)
    int port;
} IpPort;


typedef struct {
    hal_float_t *analog_in;
    hal_s32_t *analog_in_s32;
    hal_float_t *analog_min; 
    hal_float_t *analog_max; 
    hal_bit_t *analog_lowpass;  
    hal_bit_t *analog_rounding; 
    hal_bit_t *input_data[16];  
    hal_bit_t *input_data_not[16];
    hal_bit_t *output_data[8]; 
    hal_bit_t *connected;  
    hal_s32_t *current_tm; 
    hal_bit_t *io_ready_in;  
    hal_bit_t *io_ready_out;
    hal_bit_t *oled_off; 
    IpPort *ip_address; 
    int sockfd;
    struct sockaddr_in local_addr, remote_addr;
    uint8_t rx_buffer[5];
    uint8_t tx_buffer[3];
    long long last_received_time;
    long long watchdog_timeout;
    int watchdog_expired; 
    long long current_time;
    int index;
    uint8_t checksum_index;
    uint8_t checksum_index_in;
    bool watchdog_running;
    bool error_triggered;
} io_samurai_data_t;

static int instances = 0; // Példányok száma
static int comp_id = -1; // HAL komponens azonosító
static io_samurai_data_t *hal_data; // Pointer a megosztott memóriában lévő adatra

// Low-pass filter function (EMA)
float low_pass_filter(float new_sample, float previous_filtered, bool *first_sample) {
    if (*first_sample) {
        *first_sample = false;
        return new_sample; // Initialize with first sample
    }
    return ALPHA * new_sample + (1.0f - ALPHA) * previous_filtered;
}

/*
 * scale_adc - Scales a 16-bit ADC value to a range between min_value and max_value.
 *
 * @adc_in: The raw ADC input value (0 to ADC_MAX).
 * @min_value: The minimum value of the output range.
 * @max_value: The maximum value of the output range.
 *
 * Returns:
 *   - The scaled value within [min_value, max_value].
 *
 * Notes:
 *   - Clamps adc_in to [0, ADC_MAX] to handle invalid inputs.
 *   - Assumes ADC_MAX is defined as the maximum ADC value.
 */
float scale_adc(uint16_t adc_in, float min_value, float max_value) {
    if (adc_in < 0) adc_in = 0;
    if (adc_in > ADC_MAX) adc_in = ADC_MAX;
    float ratio = (float)adc_in / ADC_MAX;
    float result = ratio * (max_value - min_value) + min_value;
    return result;
}

/*
 * init_socket - Initializes a UDP socket for the io-samurai module.
 *
 * @arg: Pointer to an io_samurai_data_t structure containing socket configuration data.
 *
 * Description:
 *   - Creates a UDP socket and binds it to the local address and port specified in the ip_address field.
 *   - Sets the socket to non-blocking mode.
 *   - Configures the remote address for communication using the provided IP and port.
 *   - Logs errors using rtapi_print_msg and closes the socket on failure.
 *
 * Notes:
 *   - The socket is bound to INADDR_ANY to accept packets from any interface.
 *   - If socket creation, binding, or IP address parsing fails, the socket is closed, and sockfd is set to -1.
 *   - The function assumes the ip_address field in the io_samurai_data_t structure is valid.
 */
static void init_socket(io_samurai_data_t *arg) {
    io_samurai_data_t *d = arg;

    if ((d->sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai.%d: socket creation failed: %s\n", 
                       d->index, strerror(errno));
        return;
    }

    d->local_addr.sin_family = AF_INET;
    d->local_addr.sin_port = htons(d->ip_address->port);
    d->local_addr.sin_addr.s_addr = INADDR_ANY;

    rtapi_print_msg(RTAPI_MSG_INFO, "io-samurai.%d: binding to %s:%d\n",
                   d->index, d->ip_address->ip, d->ip_address->port);

    if (bind(d->sockfd, (struct sockaddr*)&d->local_addr, sizeof(d->local_addr)) < 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai.%d: bind failed: %s\n",
                       d->index, strerror(errno));
        close(d->sockfd);
        d->sockfd = -1;
        return;
    }
    
    // Set non-blocking
    int flags = fcntl(d->sockfd, F_GETFL, 0);
    fcntl(d->sockfd, F_SETFL, flags | O_NONBLOCK);
    
    // Setup remote address
    d->remote_addr.sin_family = AF_INET;
    d->remote_addr.sin_port = htons(d->ip_address->port);
    if (inet_pton(AF_INET, d->ip_address->ip, &d->remote_addr.sin_addr) <= 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai.%d: invalid IP address: %s\n",
                       d->index, d->ip_address->ip);
        close(d->sockfd);
        d->sockfd = -1;
    }
}

// Watchdog process
void watchdog_process(void *arg, long period) {
    io_samurai_data_t *d = arg;
    d->current_time += 1; 
    d->watchdog_running = 1; 
    
    long long elapsed = d->current_time - d->last_received_time;
    if (elapsed < 0) {
        elapsed = 0; 
    }
    if (elapsed > d->watchdog_timeout) {
        if (d->watchdog_expired == 0) {
            rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai.%d: watchdog timeout error, please restart Linuxcnc\n", d->index);
            d->checksum_index_in = 1;  // Reset checksum index
            d->checksum_index = 1;     // Reset checksum index
        }
        d->watchdog_expired = 1; 
    } else {
        d->watchdog_expired = 0; 
    }
    
}

// parse inputs
void udp_io_process_recv(void *arg, long period) {
    io_samurai_data_t *d = arg;
    if (d->watchdog_expired) {
        *d->io_ready_out = 0;
        return;
    }
    int len = recvfrom(d->sockfd, d->rx_buffer, 5, 0, NULL, NULL);
    if (len == 5) {
        d->checksum_index_in += (uint8_t)(d->rx_buffer[0] + d->rx_buffer[1] + d->rx_buffer[2] + d->rx_buffer[3] + 1);
        uint8_t calcChecksum = jump_table[d->checksum_index_in];
        if (calcChecksum == d->rx_buffer[4]) {
            *d->connected = 1;
            d->last_received_time = d->current_time;
            for (int i = 0; i < 8; i++) {
                *d->input_data[i] = (d->rx_buffer[0] >> i) & 0x01;
                *d->input_data_not[i] = 1 - *d->input_data[i];
                *d->input_data[i + 8] = (d->rx_buffer[1] >> i) & 0x01;
                *d->input_data_not[i + 8] = 1 - *d->input_data[i + 8];
            }
            uint16_t raw_adc = (d->rx_buffer[3] << 8 | d->rx_buffer[2]) & 0xfff;
            float scaled_adc = scale_adc(raw_adc, *d->analog_min, *d->analog_max);
            if (*d->analog_rounding == 1) {
                scaled_adc = roundf(scaled_adc);
            }
            *d->analog_in_s32 = (int32_t)scaled_adc;
            *d->analog_in = scaled_adc;
        } else {
            rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai.%d: checksum error: %02x != %02x\n", d->index, d->rx_buffer[4], calcChecksum);
            *d->io_ready_out = 0;
            *d->connected = 0;
        }
    } else {
        *d->io_ready_out = 0;
        *d->connected = 0;
    }
}

/*
 * set_bit - Sets or clears a specific bit in an 8-bit buffer.
 *
 * @buffer: The 8-bit value to modify.
 * @bit_position: The position of the bit to set or clear (0 to 7).
 * @value: The value to set the bit to (0 to clear, non-zero to set).
 *
 * Returns:
 *   - The modified 8-bit buffer with the specified bit set or cleared.
 *   - The original buffer if bit_position is out of range (< 0 or >= 8).
 *
 * Notes:
 *   - Bit positions are zero-based (0 is the least significant bit).
 *   - The function uses bitwise operations to set or clear the bit.
 */
uint8_t set_bit(uint8_t buffer, int bit_position, int value) {
    if (bit_position < 0 || bit_position >= 8) {
        return buffer;
    }
    if (value) {
        buffer |= (1 << bit_position);
    } else {
        buffer &= ~(1 << bit_position);
    }
    return buffer;
}

// parse outputs
void udp_io_process_send(void *arg, long period) {
    io_samurai_data_t *d = arg;
    // if watchdog expired, do not send data
    if (d->watchdog_expired) {
        *d->io_ready_out = 0;  // turn off io-ready-out (breaking estop-loop)
        return;
    }
    if (d->watchdog_running == 1) {
        for (int i = 0; i < 8; i++) {
            d->tx_buffer[0] |= (*d->output_data[i]) << i;
        }
       
        if (*d->oled_off){
            d->tx_buffer[1] = set_bit(d->tx_buffer[1], 1, 1);
        }

        if (*d->analog_lowpass == 1) {
            d->tx_buffer[1] = set_bit(d->tx_buffer[1], 0, 1);
        } else {
            d->tx_buffer[1] = set_bit(d->tx_buffer[1], 0, 0);
        }
        
        // calculate next checksum
        d->checksum_index += d->tx_buffer[0] + d->tx_buffer[1] + 1;
        d->tx_buffer[2] = jump_table[d->checksum_index];

        if (*d->io_ready_in == 1) {
            *d->io_ready_out = *d->io_ready_in;  // Seems to be all ok so pass the io-ready-in to io-ready-out
        } else {
            *d->io_ready_out = 0;  // no io-ready-in, no io-ready-out
        }

    }
    else{
        // if the watchdog is not running, we should not send data (io-samurai side is going to timeout error and turn off outputs)
        if (!d->error_triggered){
            d->error_triggered = true; // Set the error triggered flag to prevent multiple messages
            *d->io_ready_out = 0;      // set the io-ready-out pin to 0 to break the estop-loop
            rtapi_print_msg(RTAPI_MSG_ERR ,"io-samurai.%d: watchdog not running\n", d->index);
            return;  // No data to send (generate io-samurai side timeout error)
        }
    }
    sendto(d->sockfd, &d->tx_buffer, sizeof(d->tx_buffer), 0, &d->remote_addr, sizeof(d->remote_addr));
    memset(d->tx_buffer, 0, 3);

}

/*
 * parse_ip_port - Parses a string containing IP:port pairs separated by semicolons.
 *
 * @input: A null-terminated string containing IP:port pairs (e.g., "192.168.1.1:8080;10.0.0.1:80").
 * @output: An array of IpPort structures to store the parsed IP addresses and ports.
 * @max_count: The maximum number of entries that can be stored in the output array.
 *
 * Returns:
 *   - The number of valid IP:port pairs successfully parsed and stored in output.
 *   - -1 if input is NULL, output is NULL, max_count <= 0, or memory allocation fails.
 *
 * Notes:
 *   - Each IP:port pair must be in the format "IP:port" (e.g., "192.168.1.1:8080").
 *   - Invalid entries (missing colon, invalid port, etc.) are logged and skipped.
 *   - The function ensures the IP string is null-terminated and port is within valid range (0-65535).
 *   - The input string is duplicated to avoid modifying the original string.
 */
int parse_ip_port(const char *input, IpPort *output, int max_count) {
    if (input == NULL || output == NULL || max_count <= 0) {
        return -1;
    }

    char *input_copy = strdup(input);
    if (input_copy == NULL) {
        return -1;
    }

    char *saveptr1;
    char *entry;
    int count = 0;

    for (entry = strtok_r(input_copy, ";", &saveptr1);
         entry != NULL && count < max_count;
         entry = strtok_r(NULL, ";", &saveptr1)) {

        char *colon = strchr(entry, ':');
        if (colon == NULL) {
            rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: Invalid entry format: %s\n", entry);
            continue; // Skip invalid entry without a colon
        }

        *colon = '\0';
        char *ip = entry;
        char *port_str = colon + 1;

        // Parse port number
        char *endptr;
        long port = strtol(port_str, &endptr, 10);
        if (*endptr != '\0' || port < 0 || port > 65535) {
            rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: Invalid port number: %s\n", port_str);
            continue; // Skip invalid port
        }

        // Copy IP into the struct, ensuring it is null-terminated
        snprintf(output[count].ip, sizeof(output[count].ip), "%s", ip);
        output[count].port = (int)port;

        count++;
    }

    free(input_copy);
    return count; // Return the number of valid entries parsed
}

int rtapi_app_main(void) {
    int r;

    rtapi_set_msg_level(RTAPI_MSG_INFO);

        IpPort results[8];
        instances = parse_ip_port((char *)ip_address[0], results, 8);

        for (int i = 0; i < instances; i++) {
            rtapi_print_msg(RTAPI_MSG_INFO, "Parsed IP: %s, Port: %d\n", results[i].ip, results[i].port);
        }

        if (instances > MAX_CHAN) {
            rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: Too many channels, max %d allowed\n", MAX_CHAN);
            return -1;
        }

        hal_data = hal_malloc(instances * sizeof(io_samurai_data_t));
        if (hal_data == NULL) {
            rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: hal_data allocation failed\n");
            return -1;
        }

        comp_id = hal_init("io-samurai");
        if (comp_id < 0) {
            rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai.%d: hal_init failed: %d\n", 0, comp_id);
            return comp_id;
        }

        char name[48] = {0};
        for (int j = 0; j< instances; j++) {

            rtapi_print_msg(RTAPI_MSG_INFO, "io-samurai.%d: hal_data allocated at %p\n", j, &hal_data[j]);
            hal_data[j].checksum_index = 1;
            hal_data[j].checksum_index_in = 1;
            hal_data[j].index = j; 
            hal_data[j].watchdog_timeout = 10; // ~10 ms timeout
            hal_data[j].current_time = 0;
            hal_data[j].last_received_time = 0;
            hal_data[j].watchdog_expired = 0;
            hal_data[j].watchdog_running = 0;
            hal_data[j].ip_address = &results[j];
            hal_data[j].error_triggered = false;

            rtapi_print_msg(RTAPI_MSG_INFO, "io-samurai.%d: init_socket\n", j);
            init_socket(&hal_data[j]);
            rtapi_print_msg(RTAPI_MSG_INFO, "io-samurai.%d: init_socket ready..\n", j);

            memchr(name, 0, sizeof(name));

            for (int i = 0; i < 16; i++) {
                memchr(name, 0, sizeof(name));
                snprintf(name, sizeof(name), "io-samurai.%d.input-%02d", j, i);
                r = hal_pin_bit_newf(HAL_OUT, &hal_data[j].input_data[i], comp_id, name, i);
                if (r < 0) {
                    rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai.%d: ERROR: pin input-%02d export failed with err=%i\n", j, i, r);
                    hal_exit(comp_id);
                    return r;
                }
            }

            for (int i = 0; i < 16; i++) {
                memchr(name, 0, sizeof(name));
                // create io-samurai instance
                snprintf(name, sizeof(name), "io-samurai.%d.input-%02d-not", j, i);
                r = hal_pin_bit_newf(HAL_OUT, &hal_data[j].input_data_not[i], comp_id, name, i);
                if (r < 0) {
                    rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai.%d: ERROR: pin input-%02d export failed with err=%i\n", j, i, r);
                    hal_exit(comp_id);
                    return r;
                }
            }

            for (int i = 0; i < 8; i++) {
                memchr(name, 0, sizeof(name));
                snprintf(name, sizeof(name), "io-samurai.%d.output-%02d", j, i);
                r = hal_pin_bit_newf(HAL_IN, &hal_data[j].output_data[i], comp_id, name, i);
                if (r < 0) {
                    rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai.%d: ERROR: pin output-%02d export failed with err=%i\n", j, i, r);
                    hal_exit(comp_id);
                    return r;
                }
            }

            memchr(name, 0, sizeof(name));
            snprintf(name, sizeof(name), "io-samurai.%d.connected", j);

            r = hal_pin_bit_newf(HAL_IN, &hal_data[j].connected, comp_id, name, j);
            if (r < 0) {
                rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai.%d: ERROR: pin connected export failed with err=%i\n", j, r);
                hal_exit(comp_id);
                return r;
            }

            memchr(name, 0, sizeof(name));
            snprintf(name, sizeof(name), "io-samurai.%d.io-ready-in", j);

               r = hal_pin_bit_newf(HAL_IN, &hal_data[j].io_ready_in, comp_id, name, j);
            if (r < 0) {
                rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: ERROR: pin connected export failed with err=%i\n", r);
                hal_exit(comp_id);
                return r;
            }

            memchr(name, 0, sizeof(name));
            snprintf(name, sizeof(name), "io-samurai.%d.io-ready-out", j);

            r = hal_pin_bit_newf(HAL_OUT, &hal_data[j].io_ready_out, comp_id, name, j);
            if (r < 0) {
                rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: ERROR: pin connected export failed with err=%i\n", r);
                hal_exit(comp_id);
                return r;
            }

            memchr(name, 0, sizeof(name));
            snprintf(name, sizeof(name), "io-samurai.%d.analog-in", j);

            r = hal_pin_float_newf(HAL_OUT, &hal_data[j].analog_in, comp_id, name, j);
            if (r < 0) {
                rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: ERROR: pin analog-in export failed with err=%i\n", r);
                hal_exit(comp_id);
                return r;
            }

            memchr(name, 0, sizeof(name));
            snprintf(name, sizeof(name), "io-samurai.%d.analog-in-s32", j);

            r = hal_pin_s32_newf(HAL_OUT, &hal_data[j].analog_in_s32, comp_id, name, j);
            if (r < 0) {
                rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: ERROR: pin analog-in export failed with err=%i\n", r);
                hal_exit(comp_id);
                return r;
            }

            memchr(name, 0, sizeof(name));
            snprintf(name, sizeof(name), "io-samurai.%d.analog-min", j);

            r = hal_pin_float_newf(HAL_IN, &hal_data[j].analog_min, comp_id, name, j);
            if (r < 0) {
                rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: ERROR: pin analog-in export failed with err=%i\n", r);
                hal_exit(comp_id);
                return r;
            }
            *hal_data[j].analog_min = 0.0f;

            memchr(name, 0, sizeof(name));
            snprintf(name, sizeof(name), "io-samurai.%d.analog-max", j);

            r = hal_pin_float_newf(HAL_IN, &hal_data[j].analog_max, comp_id, name, j);
            if (r < 0) {
                rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: ERROR: pin analog-in export failed with err=%i\n", r);
                hal_exit(comp_id);
                return r;
            }
            *hal_data[j].analog_max = 1.0f;

            memchr(name, 0, sizeof(name));
            snprintf(name, sizeof(name), "io-samurai.%d.analog-rounding", j);


            r = hal_pin_bit_newf(HAL_IN, &hal_data[j].analog_rounding, comp_id, name, j);
            if (r < 0) {
                rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: ERROR: pin analog-rounding export failed with err=%i\n", r);
                hal_exit(comp_id);
                return r;
            }
            *hal_data[j].analog_rounding = 0;

            memchr(name, 0, sizeof(name));
            snprintf(name, sizeof(name), "io-samurai.%d.analog-lowpass", j);


            r = hal_pin_bit_newf(HAL_IN, &hal_data[j].analog_lowpass, comp_id, name, j);
            if (r < 0) {
                rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: ERROR: pin analog-rounding export failed with err=%i\n", r);
                hal_exit(comp_id);
                return r;
            }
            *hal_data[j].analog_lowpass = 0;

            memchr(name, 0, sizeof(name));
            snprintf(name, sizeof(name), "io-samurai.%d.elapsed-time", j);


            r = hal_pin_s32_newf(HAL_OUT, &hal_data[j].current_tm, comp_id, name, j);
            if (r < 0) {
                rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: ERROR: pin analog-rounding export failed with err=%i\n", r);
                hal_exit(comp_id);
                return r;
            }
            *hal_data[j].current_tm = 0;

            memchr(name, 0, sizeof(name));
            snprintf(name, sizeof(name), "io-samurai.%d.oled-off", j);


            r = hal_pin_bit_newf(HAL_IN, &hal_data[j].oled_off, comp_id, name, j);
            if (r < 0) {
                rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: ERROR: pin oled-off export failed with err=%i\n", r);
                hal_exit(comp_id);
                return r;
            }
            *hal_data[j].oled_off = 0;

            char watchdog_name[48] = {0};
            snprintf(watchdog_name, sizeof(watchdog_name),"io-samurai.%d.watchdog-process", j);
            r = hal_export_funct(watchdog_name, watchdog_process, &hal_data[j], 1, 0, comp_id);
            if (r < 0) {
                rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: hal_export_funct failed for watchdog-process: %d\n", r);
                hal_exit(comp_id);
                return r;
            }
            rtapi_print_msg(RTAPI_MSG_INFO, "io-samurai.%d: hal_export_funct for watchdog-process: %d\n", j, r);

            char process_send[48] = {0};
            snprintf(process_send, sizeof(process_send), "io-samurai.%d.process-send", j);
            r = hal_export_funct(process_send, udp_io_process_send, &hal_data[j], 1, 0, comp_id);
            if (r < 0) {
                rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: hal_export_funct failed: %d\n", r);
                hal_exit(comp_id);
                return r;
            }
            rtapi_print_msg(RTAPI_MSG_INFO, "io-samurai.%d: hal_export_funct for process_send: %d\n", j, r);

            char process_recv[48] = {0};
            snprintf(process_recv, sizeof(process_recv), "io-samurai.%d.process-recv", j);
            r = hal_export_funct(process_recv, udp_io_process_recv, &hal_data[j], 1, 0, comp_id);
            if (r < 0) {
                rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: hal_export_funct failed: %d\n", r);
                hal_exit(comp_id);
                return r;
            }
            rtapi_print_msg(RTAPI_MSG_INFO, "io-samurai.%d: hal_export_funct for process_recv: %d\n", j, r);
        }
        r = hal_ready(comp_id);
        if (r < 0) {
            rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: hal_ready failed: %d\n", r);
            hal_exit(comp_id);
            return r;
        }
        rtapi_print_msg(RTAPI_MSG_INFO, "io-samurai: Component ready\n");

    rtapi_print_msg(RTAPI_MSG_INFO, "io-samurai.all: hal_ready done\n");
    return 0;
}

void rtapi_app_exit(void) {
    for (int i = 0; i < instances; i++) {
        rtapi_print_msg(RTAPI_MSG_INFO, "io-samurai.%d: Exiting component\n", i);
        close(hal_data[i].sockfd);
    }
    hal_exit(comp_id);
}
