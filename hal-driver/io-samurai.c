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
MODULE_DESCRIPTION("IO Samurai UDP driver");
MODULE_LICENSE("GPL");

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

// Struktúra az összes HAL adat tárolására
typedef struct {
    hal_float_t *analog_in;        // Analóg bemenet
    hal_s32_t *analog_in_s32;      // Analóg bemenet 32 bites egész számként
    hal_float_t *analog_scale;     // Analóg skála
    hal_bit_t *analog_lowpass;     // Analóg aluláteresztő szűrő
    hal_bit_t *analog_rounding;    // Analóg kerekítés
    hal_bit_t *input_data[16];     // 16 bemenet
    hal_bit_t *input_data_not[16]; // 16 bemenet negált értéke
    hal_bit_t *output_data[8];     // 8 kimenet
    hal_bit_t *connected;          // Kapcsolat állapota
    hal_s32_t *current_tm;         // Aktuális idő
    hal_bit_t *io_ready_in;        // io-ready-in
    hal_bit_t *io_ready_out;       // io-ready-out
    IpPort *ip_address;            // IP cím tárolása
    int sockfd;
    struct sockaddr_in local_addr, remote_addr;
    uint8_t rx_buffer[5];
    uint8_t tx_buffer[3];
    long long last_received_time;  // Utolsó sikeres fogadás ideje (nanoszekundum)
    long long watchdog_timeout;  // 100 ms timeout
    int watchdog_expired;  // Watchdog túlfutás jelzése (0: nem futott túl, 1: túlfutott)
    long long current_time;  // Globális változó az időzítéshez
    float filtered_adc; // Szűrt ADC érték
    bool adc_first_sample; // Első minta jelzése az analóg szűrőhöz
    int index;
    uint8_t checksum_index; // Ellenőrző összeg index
    uint8_t checksum_index_in; // Ellenőrző összeg index
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

float analog_scaling(float raw_value, float scale) {
    return (raw_value / ADC_MAX) * scale; // Scale the raw ADC value
}

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
    d->current_time += 1;  // Idő előrehaladása a period alapján

    // Watchdog logika: túlfutás ellenőrzése
    long long elapsed = d->current_time - d->last_received_time;
    if (elapsed < 0) {
        elapsed = 0;  // Ha negatív, akkor állítsuk 0-ra
    }
    if (elapsed > d->watchdog_timeout) {
        if (d->watchdog_expired == 0) {
            rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai.%d: watchdog timeout error, please restart Linuxcnc\n", d->index);
            d->checksum_index_in = 1;  // Reset checksum index
            d->checksum_index = 1;     // Reset checksum index
        }
        d->watchdog_expired = 1;  // Jelezzük, hogy a watchdog túlfutott
    } else {
        d->watchdog_expired = 0;  // Nincs túlfutás
    }
    // *d->current_tm = d->watchdog_expired;  // Frissítjük az eltelt időt
}

void udp_io_process_recv(void *arg, long period) {
    io_samurai_data_t *d = arg;

    if (d->watchdog_expired) {
        *d->output_data[10] = 0;  // Kapcsolat állapotát jelző pin
        return;  // Ne fogadjunk adatot, ha a watchdog túlfutott
    }
    int len = recvfrom(d->sockfd, d->rx_buffer, 5, 0, NULL, NULL);
    if (len == 5) {
        d->checksum_index_in += (uint8_t)(d->rx_buffer[0] + d->rx_buffer[1] + d->rx_buffer[2] + d->rx_buffer[3] + 1);
        uint8_t calcChecksum = jump_table[d->checksum_index_in];
        if (calcChecksum == d->rx_buffer[4]) {
            // Érvényes csomag érkezett, connected = 1
            *d->connected = 1;
            d->last_received_time = d->current_time;  // Frissítjük az utolsó fogadás idejét (arg-ból kapjuk az időt)
            for (int i = 0; i < 8; i++) {
                *d->input_data[i] = (d->rx_buffer[0] >> i) & 0x01;       // input-00 - input-07
                *d->input_data_not[i] = 1 - *d->input_data[i]; // input-00 - input-07 negált érték
                *d->input_data[i + 8] = (d->rx_buffer[1] >> i) & 0x01;   // input-08 - input-15
                *d->input_data_not[i + 8] = 1 - *d->input_data[i + 8]; // input-08 - input-15 negált érték
            }
            // Analóg bemenet feldolgozása (rx_buffer[2] alsó 8 bit, rx_buffer[3] felső 8 bit)
            uint16_t raw_adc = (d->rx_buffer[3] << 8) | d->rx_buffer[2]; // 16 bites érték
            float voltage = (float)raw_adc;
            d->filtered_adc = low_pass_filter(voltage, d->filtered_adc, &d->adc_first_sample);
            float scaled_adc = analog_scaling(d->filtered_adc, *d->analog_scale); // Szűrés
            if (*d->analog_rounding == 1) {
                scaled_adc = roundf(scaled_adc); // Kerekítés
            }
            *d->analog_in_s32 = (int32_t)scaled_adc; // Szűrt analóg érték 32 bites egész számként
            *d->analog_in = scaled_adc; // Szűrt analóg érték
        } else {
            // Érvénytelen checksum, de még mindig van adat
            rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai.%d: checksum error: %02x != %02x\n", d->index, d->rx_buffer[4], calcChecksum);
            *d->io_ready_out = 0;
            *d->connected = 0;
        }
    } else {
        // Nem érkezett adat, connected = 0
        *d->io_ready_out = 0;
        *d->connected = 0;
    }
}

uint8_t set_bit(uint8_t buffer, int bit_position, int value) {
    if (bit_position < 0 || bit_position >= 8) {
        return buffer; // Érvénytelen bit pozíció
    }
    if (value) {
        buffer |= (1 << bit_position); // Bit beállítása
    } else {
        buffer &= ~(1 << bit_position); // Bit törlése
    }
    return buffer; // Visszaadjuk a módosított bájtot
}

void udp_io_process_send(void *arg, long period) {
    io_samurai_data_t *d = arg;

    if (d->watchdog_expired) {
        *d->io_ready_out = 0;  // Kapcsolat állapotát jelző pin
        return;  // Ne küldjünk adatot, ha a watchdog túlfutott
    }
    for (int i = 0; i < 8; i++) {
        d->tx_buffer[0] |= (*d->output_data[i]) << i;      // output-00 - output-07
        //tx_buffer[1] |= (*output_data[i + 8]) << i;  // output-08 - output-15
    }
    
    if (*d->analog_lowpass == 1) {
        d->tx_buffer[1] = set_bit(d->tx_buffer[1], 0, 1);
    } else {
        d->tx_buffer[1] = set_bit(d->tx_buffer[1], 0, 0);
    }

    d->checksum_index += d->tx_buffer[0] + d->tx_buffer[1] + 1;
    d->tx_buffer[2] = jump_table[d->checksum_index];
    sendto(d->sockfd, &d->tx_buffer, sizeof(d->tx_buffer), 0, &d->remote_addr, sizeof(d->remote_addr));
    memset(d->tx_buffer, 0, 3);
    if (*d->io_ready_in == 1) {
        *d->io_ready_out = *d->io_ready_in;  // Kapcsolat állapotát jelző pin
    } else {
        *d->io_ready_out = 0;  // Kapcsolat állapotát jelző pin
    }
}

int parse_ip_port(const char *input, IpPort *output, int max_count) {
    if (input == NULL || output == NULL || max_count <= 0) {
        return -1;
    }

    char *input_copy = strdup(input);
    if (input_copy == NULL) {
        return -1; // Memory allocation failed
    }

    char *saveptr1;
    char *entry;
    int count = 0;

    for (entry = strtok_r(input_copy, ";", &saveptr1);
         entry != NULL && count < max_count;
         entry = strtok_r(NULL, ";", &saveptr1)) {

        char *colon = strchr(entry, ':');
        if (colon == NULL) {
            continue; // Skip invalid entry without a colon
        }

        *colon = '\0'; // Split into IP and port parts
        char *ip = entry;
        char *port_str = colon + 1;

        // Parse port number
        char *endptr;
        long port = strtol(port_str, &endptr, 10);
        if (*endptr != '\0' || port < 0 || port > 65535) {
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

    // Üzenetszint beállítása INFO-ra
    rtapi_set_msg_level(RTAPI_MSG_INFO);

        IpPort results[8];
        int num_entries = parse_ip_port((char *)ip_address[0], results, 8);
        instances = num_entries;

        for (int i = 0; i < num_entries; i++) {
            rtapi_print_msg(RTAPI_MSG_INFO, "Parsed IP: %s, Port: %d\n", results[i].ip, results[i].port);
        }

        if (num_entries > MAX_CHAN) {
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
            hal_data[j].index = j; // Store the index for later use
            hal_data[j].adc_first_sample = true; // Initialize the first sample flag
            hal_data[j].watchdog_timeout = 10; // ~10 ms timeout
            hal_data[j].current_time = 0; // Initialize the current time
            hal_data[j].last_received_time = 0; // Initialize the last received time
            hal_data[j].watchdog_expired = 0; // Initialize the watchdog expired flag
            hal_data[j].filtered_adc = 0.0f; // Initialize the filtered ADC value

            hal_data[j].ip_address = &results[j];

            rtapi_print_msg(RTAPI_MSG_INFO, "io-samurai.%d: init_socket\n", j);
            init_socket(&hal_data[j]);
            rtapi_print_msg(RTAPI_MSG_INFO, "io-samurai.%d: init_socket ready..\n", j);

            memchr(name, 0, sizeof(name));

            // Bemeneti pinek létrehozása (HAL_OUT)
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

            // Bemeneti pinek létrehozása (HAL_OUT)
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

            // Kimeneti pinek létrehozása (HAL_IN)
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

            // io-ready-in pin létrehozása (HAL_OUT)
            r = hal_pin_bit_newf(HAL_IN, &hal_data[j].io_ready_in, comp_id, name, j);
            if (r < 0) {
                rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: ERROR: pin connected export failed with err=%i\n", r);
                hal_exit(comp_id);
                return r;
            }

            memchr(name, 0, sizeof(name));
            snprintf(name, sizeof(name), "io-samurai.%d.io-ready-out", j);

            // io-ready-out pin létrehozása (HAL_OUT)
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
            snprintf(name, sizeof(name), "io-samurai.%d.analog-scale", j);

            r = hal_pin_float_newf(HAL_IN, &hal_data[j].analog_scale, comp_id, name, j);
            if (r < 0) {
                rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: ERROR: pin analog-in export failed with err=%i\n", r);
                hal_exit(comp_id);
                return r;
            }
            *hal_data[j].analog_scale = 1.0f; // Kezdeti skála érték

            memchr(name, 0, sizeof(name));
            snprintf(name, sizeof(name), "io-samurai.%d.analog-rounding", j);

            // Kerekítés pin létrehozása (HAL_OUT)
            r = hal_pin_bit_newf(HAL_IN, &hal_data[j].analog_rounding, comp_id, name, j);
            if (r < 0) {
                rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: ERROR: pin analog-rounding export failed with err=%i\n", r);
                hal_exit(comp_id);
                return r;
            }
            *hal_data[j].analog_rounding = 0; // Kezdeti kerekítés érték

            memchr(name, 0, sizeof(name));
            snprintf(name, sizeof(name), "io-samurai.%d.analog-lowpass", j);

            // Kerekítés pin létrehozása (HAL_OUT)
            r = hal_pin_bit_newf(HAL_IN, &hal_data[j].analog_lowpass, comp_id, name, j);
            if (r < 0) {
                rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: ERROR: pin analog-rounding export failed with err=%i\n", r);
                hal_exit(comp_id);
                return r;
            }
            *hal_data[j].analog_lowpass = 0; // Kezdeti aluláteresztő szűrő érték

            memchr(name, 0, sizeof(name));
            snprintf(name, sizeof(name), "io-samurai.%d.elapsed-time", j);

            // Kerekítés pin létrehozása (HAL_OUT)
            r = hal_pin_s32_newf(HAL_OUT, &hal_data[j].current_tm, comp_id, name, j);
            if (r < 0) {
                rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: ERROR: pin analog-rounding export failed with err=%i\n", r);
                hal_exit(comp_id);
                return r;
            }
            *hal_data[j].current_tm = 0; // Kezdeti aluláteresztő szűrő érték

            // Watchdog függvény exportálása
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
