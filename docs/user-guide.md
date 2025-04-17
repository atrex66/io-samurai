# io-samurai Setup Guide

This guide explains how to set up the `io-samurai` project for LinuxCNC. It assumes you have a Linux system (e.g., Ubuntu, Debian) with LinuxCNC uspace installed and an `io-samurai` board (W5100S-EVB-Pico prototype or W5500-Lite final PCB).

## Prerequisites
- LinuxCNC uspace (`sudo apt install linuxcnc-uspace`).
- Git (`sudo apt install git`).
- Minicom (`sudo apt install minicom`) or `sterminal.sh` (included).
- `io-samurai` hardware (W5100S-EVB-Pico or W5500-Lite PCB with MCP23017, MCP23008, TD62783, 10 kΩ potentiometer).
- USB cable for Pico/Pico 2.

## 1.
- **Clone the Repository**:
  ```bash
  git clone https://github.com/atrex66/io-samurai.git
  cd io-samurai
  ```

## 2.
- **Install hal driver**:
   ```bash
   cd hal-driver
   chmod +x install.sh
   ./install.sh
   ```


