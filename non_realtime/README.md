cat << 'EOF' > README.md
# IoSamurai C++ Library

The `IoSamurai` library is a C++ implementation for communicating with an IO Samurai device over UDP. It is derived from a LinuxCNC HAL driver and provides a simple interface to control digital outputs, read digital inputs, and process analog inputs. The library is designed for POSIX-compliant systems (e.g., Linux) and uses standard C++11 features.

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
- **Jump Table**: A 256-byte checksum lookup table (`jump_table`) is required for proper operation. A placeholder is included, but the actual table must be provided.

## Installation
1. **Clone or Download**: Obtain the source files:
   - `IoSamurai.h`
   - `IoSamurai.cpp`
   - `main.cpp` (example usage)
   - `jump_table.h` (if available, for the checksum table)

2. **Install Dependencies** (on Ubuntu/Debian):
   ```
   bash
   sudo apt update
   sudo apt install g++ libc6-dev
   ```

3. **Provide Jump Table**:
   - The library includes a placeholder `jump_table` in `IoSamurai.cpp`:
     ```
     cpp
     const uint8_t IoSamurai::jump_table[256] = {0}; // Dummy table
     ```
   - Replace it with the actual table from `jump_table.h` or define it directly in `IoSamurai.cpp`. Without the correct table, checksum verification will fail, causing communication errors.

## Compilation
Compile the library and example using `g++`:

```
bash
g++ -std=c++11 -o io_samurai IoSamurai.cpp main.cpp
```

**Flags**:
- `-std=c++11`: Enables C++11 features.
- `-o io_samurai`: Specifies the output executable name.

If you have a separate `jump_table` implementation (e.g., `jump_table.o`):
```
bash
g++ -std=c++11 -o io_samurai IoSamurai.cpp main.cpp jump_table.o
```

Run the program:
```
bash
./io_samurai
```

## Usage
The `IoSamurai` class provides a straightforward API for interacting with the device. Below is an example program (`main.cpp`):

```
cpp
#include "IoSamurai.h"
#include <iostream>
#include <chrono>
#include <thread>

int main() {
    IoSamurai io;

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
```

### API Overview
- **Constructor/Destructor**:
  - `IoSamurai()`: Initializes the object.
  - `~IoSamurai()`: Closes the socket.

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
- **Jump Table**: The `jump_table` is critical for checksum verification. Replace the placeholder in `IoSamurai.cpp` with the actual table to avoid `Checksum error` messages.
- **Timing**: The example uses `std::this_thread::sleep_for` for a 1 ms interval. Adjust the sleep duration as needed for your application.
- **Watchdog and Threading**: The watchdog and threading functionality were removed to simplify the library. The `update` function must be called manually in a loop.
- **Error Handling**: Errors (e.g., socket failures, checksum errors) are logged to `std::cerr`. Check the console output for issues.
- **Platform**: The library uses POSIX socket APIs (`sys/socket.h`, `netinet/in.h`). For Windows, rewrite the socket code using Winsock.

## Troubleshooting
- **Compilation Error: `sockaddr_in` incomplete type**:
  - Ensure `<netinet/in.h>` is included in `IoSamurai.h`.
- **Runtime Error: `Checksum error`**:
  - Provide the correct `jump_table` implementation.
- **Socket Errors**:
  - Verify the IP address and port are correct and the device is reachable.
  - Check for permission issues (e.g., run as root if binding to a low port).
- **Missing Headers**:
  - Install `libc6-dev`:
    ```
    bash
    sudo apt install libc6-dev
    ```

## Contributing
- Provide the `jump_table` implementation to improve functionality.
- Report issues or suggest enhancements via email to stellarwanderer@proton.me.

## License
MIT License (based on the original LinuxCNC HAL driver by Viola Zsolt).
EOF