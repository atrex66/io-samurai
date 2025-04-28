# io-samurai C++ Library

The `io-samurai` library is a C++ implementation for communicating with an IO Samurai device over UDP. It is derived from a LinuxCNC HAL driver and provides a simple interface to control digital outputs, read digital inputs, and process analog inputs. The library is designed for POSIX-compliant systems (e.g., Linux) and uses standard C++11 features.

## Features
- Initialize communication with an IO Samurai device using an IP address and port.
- Set and reset digital outputs (8 outputs, indexed 0–7).
- Read digital inputs (16 inputs, indexed 0–15).
- Read analog input (scaled to a user-defined range, with optional low-pass filter and rounding).
- Control OLED display state (on/off).
- Check connection status.
- Perform UDP send/receive operations in a single `update` call.

## Requirements
- **Compiler**: `g++` (GCC 4.8 or later, supporting C++11).
- **Operating System**: POSIX-compliant system (e.g., Linux, macOS). Windows is not supported without modifications.
- **Libraries**: Standard C++ Library and POSIX socket APIs (included in `libc` on Linux).
- **Jump Table**: A 256-byte checksum lookup table (`jump_table`) is required, provided via a symbolic link to `../firmware/w5100s-evb-pico/inc/jump_table.h`.

## Installation
1. **Clone or Download**: Obtain the source files:
   - `io-samurai.h`
   - `io-samurai.cpp`
   - `jump_table.h`  (symbolic link precreated, to the firmware jump_table)
   - `main.cpp` (example usage)

2. **Install Dependencies** (on Ubuntu/Debian):
   """
   bash
   sudo apt update
   sudo apt install g++ libc6-dev
   """

## Compilation
Compile the library and example using `g++`:

"""
bash
g++ -std=c++11 -o io_samurai io-samurai.cpp main.cpp
"""

**Flags**:
- `-std=c++11`: Enables C++11 features.
- `-o io_samurai`: Specifies the output executable name.

If the `jump_table` implementation is in a separate object file (e.g., `jump_table.o`):
"""
bash
g++ -std=c++11 -o io_samurai io-samurai.cpp main.cpp jump_table.o
"""

Run the program:
"""
bash
./io_samurai
"""

## Usage
The `io-samurai` class provides a straightforward API for interacting with the device. Below is an example program (`main.cpp`):

"""
cpp
#include "io-samurai.h"
#include <iostream>
#include <chrono>
#include <thread>

int main() {
    io-samurai io;

    // Initialize with IP and port
    if (!io.init("192.168.1.100", 8080)) {
        std::cerr << "Initialization failed" << std::endl;
        return 1;
    }

    // Set analog range (e.g., 0 to 10 volts)
    io.set_analog_range(0.0f, 10.0f);
    io.set_analog_lowpass(true);  // Enable low-pass filter
    io.set_analog_rounding(true); // Enable rounding
    io.set_oled_off(false);       // Keep OLED on

    // Main loop
    while (true) {
        // Set outputs
        io.set_output(0, true);  // Set output 0
        io.set_output(1, false); // Clear output 1

        // Perform communication
        io.update();

        // Read inputs
        for (int i = 0; i < 16; i++) {
            std::cout << "Input " << i << ": " << io.get_input(i) << std::endl;
        }

        // Read analog input
        std::cout << "Analog In: " << io.get_analog_in() << std::endl;
        std::cout << "Analog In (s32): " << io.get_analog_in_s32() << std::endl;
        std::cout << "Connected: " << io.is_connected() << std::endl;

        // Sleep for 1 ms
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return 0;
}
"""

### API Overview
- **Constructor/Destructor**:
  - `io-samurai()`: Initializes the object.
  - `~io-samurai()`: Closes the socket.

- **Initialization**:
  - `bool init(const std::string& ip_address, int port)`: Sets up the UDP socket with the given IP and port. Returns `true` on success.

- **Output Control**:
  - `void set_output(int index, bool value)`: Sets output `index` (0–7) to `value`.
  - `void reset_output(int index)`: Clears output `index` (0–7).

- **Input Reading**:
  - `bool get_input(int index) const`: Returns the state of input `index` (0–15).
  - `float get_analog_in() const`: Returns the scaled analog input value.
  - `int32_t get_analog_in_s32() const`: Returns the analog input as an integer.

- **Analog Configuration**:
  - `void set_analog_range(float min_value, float max_value)`: Sets the analog input range.
  - `void set_analog_lowpass(bool enable)`: Enables/disables the low-pass filter.
  - `void set_analog_rounding(bool enable)`: Enables/disables rounding of analog values.

- **OLED Control**:
  - `void set_oled_off(bool off)`: Turns the OLED display on (`false`) or off (`true`).

- **Connection Status**:
  - `bool is_connected() const`: Returns `true` if the last communication was successful.

- **Communication**:
  - `void update()`: Sends output data and receives input data over UDP.

## Notes
- **Jump Table**: The `jump_table` is provided via a symbolic link to `../firmware/w5100s-evb-pico/inc/jump_table.h`. Ensure the original file exists and defines `extern const uint8_t jump_table[256];` or the actual table.
- **Timing**: The example uses `std::this_thread::sleep_for` for a 1 ms interval. Adjust the sleep duration as needed for your application.
- **Watchdog and Threading**: The watchdog and threading functionality were removed to simplify the library. The `update` function must be called manually in a loop.
- **Error Handling**: Errors (e.g., socket failures, checksum errors) are logged to `std::cerr`. Check the console output for issues.
- **Platform**: The library uses POSIX socket APIs (`sys/socket.h`, `netinet/in.h`). For Windows, rewrite the socket code using Winsock.

## Troubleshooting
- **Compilation Error: `sockaddr_in` incomplete type**:
  - Ensure `<netinet/in.h>` is included in `io-samurai.h`.
- **Compilation Error: `jump_table` undefined**:
  - Verify the symbolic link to `jump_table.h` is correct and the table is defined or linked.
- **Runtime Error: `Checksum error`**:
  - Ensure the `jump_table` implementation is correct and accessible.
- **Socket Errors**:
  - Verify the IP address and port are correct and the device is reachable.
  - Check for permission issues (e.g., run as root if binding to a low port).
- **Missing Headers**:
  - Install `libc6-dev`:
    """
    bash
    sudo apt install libc6-dev
    """

## Contributing
- Report issues or suggest enhancements via email to atrex66@gmail.com

## License
MIT License (based on the original LinuxCNC HAL driver by Viola Zsolt)