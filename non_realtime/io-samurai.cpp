#include "io-samurai.h"
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <cmath>
#include "../firmware/w5100s-evb-pico/inc/jump_table.h"

// Placeholder jump_table (replace with actual implementation)
uint8_t IoSamurai::jump_tbl[256];

IoSamurai::IoSamurai()
    : sockfd(-1),
      rx_buffer(RX_BUFFER_SIZE, 0),
      tx_buffer(TX_BUFFER_SIZE, 0),
      input_data(16, false),
      output_data(8, false),
      analog_in(0.0f),
      analog_in_s32(0),
      analog_min(0.0f),
      analog_max(1.0f),
      analog_lowpass(false),
      analog_rounding(false),
      oled_off(false),
      connected(false),
      current_time(0),
      last_received_time(0),
      watchdog_running(false),
      watchdog_expired(false),
      error_triggered(false),
      checksum_index(1),
      checksum_index_in(1),
      previous_analog(0.0f),
      first_analog_sample(true) {
    memset(&local_addr, 0, sizeof(local_addr));
    memset(&remote_addr, 0, sizeof(remote_addr));
}

IoSamurai::~IoSamurai() {
    if (sockfd >= 0) {
        close(sockfd);
    }
}

bool IoSamurai::init(const std::string& ip_address, int port) {
    this->ip_address.ip = ip_address;
    this->ip_address.port = port;

    memcpy(jump_tbl, jump_table, sizeof(jump_table));

    if (!init_socket()) {
        std::cerr << "Failed to initialize socket" << std::endl;
        return false;
    }

    std::cout << "IoSamurai initialized with IP: " << ip_address << ", Port: " << port << std::endl;
    return true;
}

bool IoSamurai::init_socket() {
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        std::cerr << "Socket creation failed: " << strerror(errno) << std::endl;
        return false;
    }

    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(ip_address.port);
    local_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        std::cerr << "Bind failed: " << strerror(errno) << std::endl;
        close(sockfd);
        sockfd = -1;
        return false;
    }

    // Set non-blocking
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    // Setup remote address
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(ip_address.port);
    if (inet_pton(AF_INET, ip_address.ip.c_str(), &remote_addr.sin_addr) <= 0) {
        std::cerr << "Invalid IP address: " << ip_address.ip << std::endl;
        close(sockfd);
        sockfd = -1;
        return false;
    }

    return true;
}

void IoSamurai::set_output(int index, bool value) {
    if (index >= 0 && index < 8) {
        output_data[index] = value;
    }
}

void IoSamurai::reset_output(int index) {
    if (index >= 0 && index < 8) {
        output_data[index] = false;
    }
}

bool IoSamurai::get_input(int index) const {
    if (index >= 0 && index < 16) {
        return input_data[index];
    }
    return false;
}

float IoSamurai::get_analog_in() const {
    return analog_in;
}

int32_t IoSamurai::get_analog_in_s32() const {
    return analog_in_s32;
}

void IoSamurai::set_analog_range(float min_value, float max_value) {
    analog_min = min_value;
    analog_max = max_value;
}

void IoSamurai::set_analog_lowpass(bool enable) {
    analog_lowpass = enable;
}

void IoSamurai::set_analog_rounding(bool enable) {
    analog_rounding = enable;
}

void IoSamurai::set_oled_off(bool off) {
    oled_off = off;
}

bool IoSamurai::is_connected() const {
    return connected;
}

float IoSamurai::low_pass_filter(float new_sample, float previous_filtered, bool& first_sample) {
    if (first_sample) {
        first_sample = false;
        return new_sample;
    }
    return ALPHA * new_sample + (1.0f - ALPHA) * previous_filtered;
}

float IoSamurai::scale_adc(uint16_t adc_in, float min_value, float max_value) {
    if (adc_in < 0) adc_in = 0;
    if (adc_in > ADC_MAX) adc_in = ADC_MAX;
    float ratio = static_cast<float>(adc_in) / ADC_MAX;
    return ratio * (max_value - min_value) + min_value;
}

void IoSamurai::udp_io_process_send() {
    tx_buffer[0] = 0;
    for (int i = 0; i < 8; i++) {
        tx_buffer[0] |= (output_data[i] ? 1 : 0) << i;
    }

    tx_buffer[1] = 0;
    if (oled_off) {
        tx_buffer[1] = set_bit(tx_buffer[1], 1, 1);
    }
    if (analog_lowpass) {
        tx_buffer[1] = set_bit(tx_buffer[1], 0, 1);
    }

    checksum_index += tx_buffer[0] + tx_buffer[1] + 1;
    tx_buffer[2] = jump_tbl[checksum_index];

    sendto(sockfd, tx_buffer.data(), TX_BUFFER_SIZE, 0,
           (struct sockaddr*)&remote_addr, sizeof(remote_addr));
    std::fill(tx_buffer.begin(), tx_buffer.end(), 0);
}

void IoSamurai::udp_io_process_recv() {

    socklen_t addr_len = sizeof(remote_addr);
    int len = recvfrom(sockfd, rx_buffer.data(), RX_BUFFER_SIZE, 0, nullptr, nullptr);

    if (len == RX_BUFFER_SIZE) {
        checksum_index_in += rx_buffer[0] + rx_buffer[1] + rx_buffer[2] + rx_buffer[3] + 1;
        uint8_t calc_checksum = jump_tbl[checksum_index_in];
        if (calc_checksum == rx_buffer[4]) {
            connected = true;
            last_received_time = current_time;

            // Parse inputs
            for (int i = 0; i < 8; i++) {
                input_data[i] = (rx_buffer[0] >> i) & 0x01;
                input_data[i + 8] = (rx_buffer[1] >> i) & 0x01;
            }

            // Parse ADC
            uint16_t raw_adc = (rx_buffer[3] << 8 | rx_buffer[2]) & 0xfff;
            float scaled_adc = scale_adc(raw_adc, analog_min, analog_max);
            if (analog_lowpass) {
                scaled_adc = low_pass_filter(scaled_adc, previous_analog, first_analog_sample);
                previous_analog = scaled_adc;
            }
            if (analog_rounding) {
                scaled_adc = std::round(scaled_adc);
            }
            analog_in = scaled_adc;
            analog_in_s32 = static_cast<int32_t>(scaled_adc);
        } else {
            std::cerr << "Checksum error: " << static_cast<int>(rx_buffer[4])
                      << " != " << static_cast<int>(calc_checksum) << std::endl;
            connected = false;
        }
    } else {
        connected = false;
    }
}

uint8_t IoSamurai::set_bit(uint8_t buffer, int bit_position, int value) {
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

void IoSamurai::update() {
    udp_io_process_send();
    udp_io_process_recv();
}