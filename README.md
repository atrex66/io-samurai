# io-samurai
An open-source, budget-friendly CNC interface for LinuxCNC, featuring 40 MHz SPI (~6000 Hz), 16 inputs (20–50 V), 8 high-current outputs (50 V, 500 mA), and single analog input (0–3.3 V). Supports Raspberry Pi Pico/Pico 2 with W5100S-EVB-Pico or W5500-Lite Ethernet modules.

![io-samurai PCB](docs/images/last_proto.png)

## Features
- **High-Speed Interface**: 40 MHz SPI (~6000 Hz burst) via W5100S/W5500-Lite Ethernet.
- **Inputs**: 16 channels (MCP23017, I2C), 20–50 V, with 10 kΩ/1.5 kΩ divider and 3.6 V Zener protection.
- **Outputs**: 8 channels (TD62783 Darlington driver, MCP23008-controlled), 50 V, 500 mA/channel.
- **Analog Inputs**: 1 channels (GP26), 12-bit, 10 kΩ potentiometer, 100 nF filtering.
- **Display**: Optional SH1106 OLED (128x64) for I/O status and IP address.
- **Software**:
  - LinuxCNC HAL driver (uspace, `.so`) with safety (timeout, data checks).
  - Python library for automation/remote I/O.
  - Mach3 driver in development.
- **Hardware Support**: Raspberry Pi Pico/Pico 2 + W5500-Lite, or W5100S-EVB-Pico (default option).
- **Open-Source**: All code, schematics, and docs under MIT License.

## Getting Started
### Prerequisites
- **Hardware**:
  - W5100S-EVB-Pico (~$10).
  - Raspberry Pi Pico or Pico 2 ($5) + W5500-Lite ($6).
  - 10 kΩ linear potentiometer (B10K) for analog input.
- **Software**:
  - Pico SDK: `git clone https://github.com/raspberrypi/pico-sdk.git ~/pico-sdk`
  - LinuxCNC (userspace mode, Debian/Ubuntu recommended).
  - Python3 for automation library.

### Building the Firmware
1. **Set up Pico SDK**:
   ```bash
   git clone https://github.com/raspberrypi/pico-sdk.git ~/pico-sdk
   cd pico-sdk
   git submodule update --init
```
2. **Clone the repository and build from source**
   ```bash
   git clone https://github/atrex66/io-samurai
   cd io-samurai/firmware/w5100s-evb-pico
   mkdir build
   cd build
   cmake ..
   make
```

## Support
- **Patreon**: Join our community at [patreon](https://www.patreon.com/c/user?u=43314769).

## License
This project is licensed under the MIT License. See [LICENSE](LICENSE).
The `ioLibrary_Driver` in `firmware/pico/ioLibrary_Driver.zip` is licensed under the MIT License by Wiznet. See [firmware/pico/ioLibrary_Driver.zip/LICENSE.txt](firmware/pico/ioLibrary_Driver.zip/LICENSE.txt).
