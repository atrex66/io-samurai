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
#include "../firmware/w5100s-evb-pico/inc/jump_table.h"

/* module information */
MODULE_AUTHOR("Viola Zsolt");
MODULE_DESCRIPTION("IO Samurai UDP driver");
MODULE_LICENSE("GPL");

// to parse the modparam
char *ip_address[16] = {0,};
RTAPI_MP_ARRAY_STRING(ip_address, 16, "Ip address");

#define ALPHA 0.1f  // Low-pass filter constant (EMA)
#define ADC_MAX 4095.0f // Maximum ADC value (12-bit resolution)

static int sockfd;
static struct sockaddr_in local_addr, remote_addr;
static uint8_t rx_buffer[5];
static uint8_t tx_buffer[3];
static long long last_received_time = 0;  // Utolsó sikeres fogadás ideje (nanoszekundum)
static const long long watchdog_timeout = 5;  // 5 ms timeout
static int watchdog_expired = 0;  // Watchdog túlfutás jelzése (0: nem futott túl, 1: túlfutott)
static long long current_time = 0;  // Globális változó az időzítéshez
static float filtered_adc = 0.0f; // Szűrt ADC érték
static bool adc_first_sample = true; // Első minta jelzése az analóg szűrőhöz

// Struktúra az összes HAL adat tárolására
typedef struct {
    hal_float_t *analog_in;      // Analóg bemenet
    hal_float_t *analog_scale;   // Analóg skála
    hal_bit_t *input_data[16];   // 16 bemenet
    hal_bit_t *output_data[8];   // 8 kimenet
    hal_bit_t *connected;        // Kapcsolat állapota
    hal_bit_t *io_ready_in;      // io-ready-in
    hal_bit_t *io_ready_out;     // io-ready-out
} io_samurai_data_t;

static io_samurai_data_t *hal_data = NULL; // Pointer a megosztott memóriában lévő adatra

int comp_id;
uint8_t counter=0;

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

static void init_socket(void) {
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(8888);
    local_addr.sin_addr.s_addr = INADDR_ANY;
    bind(sockfd, (struct sockaddr*)&local_addr, sizeof(local_addr));
    
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(8888);
    inet_pton(AF_INET, *ip_address, &remote_addr.sin_addr);
    rtapi_print_msg(RTAPI_MSG_INFO, "io-samurai: Socket created\n");    
    tx_buffer[0] = 0x00;
    tx_buffer[1] = 0x00;
    tx_buffer[2] = 0x04;
    rx_buffer[0] = 0x00;
    rx_buffer[1] = 0x00;
    rx_buffer[2] = 0x00;
    rx_buffer[3] = 0x00;
    rx_buffer[4] = 0x04;
}

void watchdog_process(void *arg, long period) {
    current_time += 1;  // Idő előrehaladása a period alapján

    // Watchdog logika: túlfutás ellenőrzése
    long long elapsed = current_time - last_received_time;
    if (elapsed < 0) {
        elapsed = 0;  // Ha negatív, akkor állítsuk 0-ra
    }
    if (elapsed > watchdog_timeout) {
        if (watchdog_expired == 0) {
            rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: watchdog timeout error, please restart Linuxcnc\n");
            checksum_index_in = 1;  // Reset checksum index
            checksum_index = 1;     // Reset checksum index
        }
        watchdog_expired = 1;  // Jelezzük, hogy a watchdog túlfutott
    } else {
        watchdog_expired = 0;  // Nincs túlfutás
    }
}

void udp_io_process_recv(void *arg, long period) {
    io_samurai_data_t *d = arg;

    if (watchdog_expired) {
        *d->output_data[10] = 0;  // Kapcsolat állapotát jelző pin
        return;  // Ne fogadjunk adatot, ha a watchdog túlfutott
    }
    int len = recvfrom(sockfd, rx_buffer, 5, 0, NULL, NULL);
    if (len == 5) {
        checksum_index_in += (uint8_t)(rx_buffer[0] + rx_buffer[1] + rx_buffer[2] + rx_buffer[3] + 1);
        uint8_t calcChecksum = jump_table[checksum_index_in];
        if (calcChecksum == rx_buffer[4]) {
            // Érvényes csomag érkezett, connected = 1
            *d->connected = 1;
            last_received_time = current_time;  // Frissítjük az utolsó fogadás idejét (arg-ból kapjuk az időt)
            for (int i = 0; i < 8; i++) {
                *d->input_data[i] = (rx_buffer[0] >> i) & 0x01;       // input-00 - input-07
                *d->input_data[i + 8] = (rx_buffer[1] >> i) & 0x01;   // input-08 - input-15
            }
            // Analóg bemenet feldolgozása (rx_buffer[2] alsó 8 bit, rx_buffer[3] felső 8 bit)
            uint16_t raw_adc = (rx_buffer[3] << 8) | rx_buffer[2]; // 16 bites érték
            float voltage = (float)raw_adc;
            filtered_adc = low_pass_filter(voltage, filtered_adc, &adc_first_sample); // Szűrés
            *d->analog_in = analog_scaling(filtered_adc, *d->analog_scale); // Szűrt érték kiadása a HAL pinre
        } else {
            // Érvénytelen checksum, de még mindig van adat
            rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: checksum error: %02x != %02x\n", rx_buffer[4], calcChecksum);
            *d->input_data[0] = 0;
            *d->input_data[1] = 0;
            *d->connected = 0;
        }
    } else {
        // Nem érkezett adat, connected = 0
        *d->input_data[0] = 0;
        *d->input_data[1] = 0;
        *d->connected = 0;
    }
}

void udp_io_process_send(void *arg, long period) {
    io_samurai_data_t *d = arg;

    if (watchdog_expired) {
        *d->io_ready_out = 0;  // Kapcsolat állapotát jelző pin
        return;  // Ne küldjünk adatot, ha a watchdog túlfutott
    }
    counter ++;
    for (int i = 0; i < 8; i++) {
        tx_buffer[0] |= (*d->output_data[i]) << i;      // output-00 - output-07
        //tx_buffer[1] |= (*output_data[i + 8]) << i;  // output-08 - output-15
    }
    checksum_index += tx_buffer[0] + tx_buffer[1] + 1;
    tx_buffer[2] = jump_table[checksum_index];
    sendto(sockfd, tx_buffer, 3, 0, (struct sockaddr*)&remote_addr, sizeof(remote_addr));
    memset(tx_buffer, 0, 3);
    if (*d->io_ready_in == 1) {
        *d->io_ready_out = *d->io_ready_in;  // Kapcsolat állapotát jelző pin
    } else {
        *d->io_ready_out = 0;  // Kapcsolat állapotát jelző pin
    }
}

int rtapi_app_main(void) {
    int r;

    // Üzenetszint beállítása INFO-ra
    rtapi_set_msg_level(RTAPI_MSG_INFO);

    init_socket();

    comp_id = hal_init("io-samurai");
    if (comp_id < 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: hal_init failed: %d\n", comp_id);
        return comp_id;
    }

    // Memóriafoglalás a HAL struktúrának
    hal_data = (io_samurai_data_t *)hal_malloc(sizeof(io_samurai_data_t));
    if (hal_data == NULL) {
        rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: hal_malloc failed for hal_data\n");
        hal_exit(comp_id);
        return -ENOMEM;
    }
    rtapi_print_msg(RTAPI_MSG_INFO, "io-samurai: hal_data allocated at %p\n", hal_data);

    // Bemeneti pinek létrehozása (HAL_OUT)
    for (int i = 0; i < 16; i++) {
        r = hal_pin_bit_newf(HAL_OUT, &hal_data->input_data[i], comp_id, "io-samurai.input-%02d", i);
        if (r < 0) {
            rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: ERROR: pin input-%02d export failed with err=%i\n", i, r);
            hal_exit(comp_id);
            return r;
        }
    }

    // Kimeneti pinek létrehozása (HAL_IN)
    for (int i = 0; i < 8; i++) {
        r = hal_pin_bit_newf(HAL_IN, &hal_data->output_data[i], comp_id, "io-samurai.output-%02d", i);
        if (r < 0) {
            rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: ERROR: pin output-%02d export failed with err=%i\n", i, r);
            hal_exit(comp_id);
            return r;
        }
    }

    // Connected pin létrehozása (HAL_OUT)
    r = hal_pin_bit_newf(HAL_IN, &hal_data->connected, comp_id, "io-samurai.connected");
    if (r < 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: ERROR: pin connected export failed with err=%i\n", r);
        hal_exit(comp_id);
        return r;
    }

    // io-ready-in pin létrehozása (HAL_OUT)
    r = hal_pin_bit_newf(HAL_IN, &hal_data->io_ready_in, comp_id, "io-samurai.io-ready-in");
    if (r < 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: ERROR: pin connected export failed with err=%i\n", r);
        hal_exit(comp_id);
        return r;
    }

    // io-ready-out pin létrehozása (HAL_OUT)
    r = hal_pin_bit_newf(HAL_OUT, &hal_data->io_ready_out, comp_id, "io-samurai.io-ready-out");
    if (r < 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: ERROR: pin connected export failed with err=%i\n", r);
        hal_exit(comp_id);
        return r;
    }

    r = hal_pin_float_newf(HAL_OUT, &hal_data->analog_in, comp_id, "io-samurai.analog-in");
    if (r < 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: ERROR: pin analog-in export failed with err=%i\n", r);
        hal_exit(comp_id);
        return r;
    }

    r = hal_pin_float_newf(HAL_IN, &hal_data->analog_scale, comp_id, "io-samurai.analog-scale");
    if (r < 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: ERROR: pin analog-in export failed with err=%i\n", r);
        hal_exit(comp_id);
        return r;
    }
    *hal_data->analog_scale = 1.0f; // Kezdeti skála érték

    // Watchdog függvény exportálása
    r = hal_export_funct("io-samurai.watchdog-process", watchdog_process, NULL, 1, 0, comp_id);
    if (r < 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: hal_export_funct failed for watchdog-process: %d\n", r);
        hal_exit(comp_id);
        return r;
    }

    r = hal_export_funct("io-samurai.udp-io-process-send", udp_io_process_send, hal_data, 1, 0, comp_id);
    if (r < 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: hal_export_funct failed: %d\n", r);
        hal_exit(comp_id);
        return r;
    }

    r = hal_export_funct("io-samurai.udp-io-process-recv", udp_io_process_recv, hal_data, 1, 0, comp_id);
    if (r < 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: hal_export_funct failed: %d\n", r);
        hal_exit(comp_id);
        return r;
    }

    r = hal_ready(comp_id);
    if (r < 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, "io-samurai: hal_ready failed: %d\n", r);
        hal_exit(comp_id);
        return r;
    }

    rtapi_print_msg(RTAPI_MSG_INFO, "io-samurai: Component initialized successfully\n");
    return 0;
}

void rtapi_app_exit(void) {
    rtapi_print_msg(RTAPI_MSG_INFO, "io-samurai: Exiting component\n");
    close(sockfd);
    hal_exit(comp_id);
}
