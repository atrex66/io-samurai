#include "io-samurai.h"
#include <iostream>
#include <chrono>
#include <thread>

int main() {
    IoSamurai io;
    
    // Initialize with IP and port
    if (!io.init("192.168.0.178", 8889)) {
        std::cerr << "Initialization failed" << std::endl;
        return 1;
    }

    // Set analog range
    io.set_analog_range(0.0f, 4095.0f);
    io.set_analog_lowpass(true);
    io.set_analog_rounding(true);
    io.set_oled_off(true);

    // Timing parameters
    using namespace std::chrono;
    auto interval = milliseconds(1); // 1 ms interval
    auto next_time = steady_clock::now();

    // Main loop
    while (true) {
        // Current time
        auto now = steady_clock::now();

        // Check if it's time to update
        if (now >= next_time) {
            // Set some outputs
            io.set_output(0, true);
            io.set_output(1, true);

            // Update communication
            io.update();

            // Read inputs
            for (int i = 0; i < 16; i++) {
                std::cout << "I-" << i << ":" << io.get_input(i);
            }
            std::cout << std::endl;

            // Read analog input
            std::cout << "Analog In: " << io.get_analog_in() << std::endl;
            std::cout << "Analog In (s32): " << io.get_analog_in_s32() << std::endl;
            std::cout << "Connected: " << io.is_connected() << std::endl;

            // Schedule next update
            next_time += interval;

            // If we're behind schedule, catch up to avoid drift
            if (next_time < now) {
                next_time = now + interval;
                std::cerr << "Warning: Update loop falling behind schedule" << std::endl;
            }
        }

        // Sleep until the next update (non-blocking if possible)
        std::this_thread::sleep_until(next_time);
    }

    return 0;
}