#ifndef IO_SAMURAI_H
#define IO_SAMURAI_H

#include <string>
#include <cstdint>
#include <vector>
#include <thread>
#include <atomic>
#include <netinet/in.h> // Added for sockaddr_in

class IoSamurai {
public:
    // Constructor
    IoSamurai();

    // Destructor
    ~IoSamurai();

    // Initialize with IP address and port
    bool init(const std::string& ip_address, int port);

    // Set output bit (0 to 7)
    void set_output(int index, bool value);

    // Reset output bit (0 to 7)
    void reset_output(int index);

    // Get input bit (0 to 15)
    bool get_input(int index) const;

    // Get analog input value (scaled)
    float get_analog_in() const;

    // Get analog input value (integer)
    int32_t get_analog_in_s32() const;

    // Set analog range
    void set_analog_range(float min_value, float max_value);

    // Enable/disable analog low-pass filter
    void set_analog_lowpass(bool enable);

    // Enable/disable analog rounding
    void set_analog_rounding(bool enable);

    // Enable/disable OLED
    void set_oled_off(bool off);

    // Check connection status
    bool is_connected() const;

    // Start periodic updates in a separate thread
    void start_periodic_update(int interval_ms);

    // Stop periodic updates
    void stop_periodic_update();

    // Update function to handle send and receive
    void update();

private:
    // Constants
    static constexpr float ALPHA = 0.1f; // Low-pass filter constant (EMA)
    static constexpr float ADC_MAX = 4095.0f; // Maximum ADC value (12-bit resolution)
    static constexpr int WATCHDOG_TIMEOUT = 100; // ~10 ms timeout
    static constexpr size_t RX_BUFFER_SIZE = 5;
    static constexpr size_t TX_BUFFER_SIZE = 3;

    // Structure for IP and port
    struct IpPort {
        std::string ip;
        int port;
    };

    // Low-pass filter function
    float low_pass_filter(float new_sample, float previous_filtered, bool& first_sample);

    // Scale ADC value
    float scale_adc(uint16_t adc_in, float min_value, float max_value);

    // Initialize UDP socket
    bool init_socket();

    // Send data over UDP
    void udp_io_process_send();

    // Receive data over UDP
    void udp_io_process_recv();

    // Set bit in buffer
    uint8_t set_bit(uint8_t buffer, int bit_position, int value);

    // Data members
    IpPort ip_address;
    int sockfd;
    struct sockaddr_in local_addr, remote_addr;
    std::vector<uint8_t> rx_buffer;
    std::vector<uint8_t> tx_buffer;
    std::vector<bool> input_data; // 16 inputs
    std::vector<bool> output_data; // 8 outputs
    float analog_in;
    int32_t analog_in_s32;
    float analog_min;
    float analog_max;
    bool analog_lowpass;
    bool analog_rounding;
    bool oled_off;
    bool connected;
    long long current_time;
    long long last_received_time;
    bool watchdog_running;
    bool watchdog_expired;
    bool error_triggered;
    uint8_t checksum_index;
    uint8_t checksum_index_in;
    float previous_analog;
    bool first_analog_sample;
    std::thread update_thread;
    std::atomic<bool> running;

    // Placeholder for jump_table (checksum lookup table)
    // Note: Replace with actual jump_table implementation
    static uint8_t jump_tbl[256];
};

#endif // IO_SAMURAI_H