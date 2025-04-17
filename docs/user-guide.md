# io-samurai Setup Guide

This guide explains how to set up the `io-samurai` project for LinuxCNC. It assumes you have a Linux system (e.g., Ubuntu, Debian) with LinuxCNC uspace installed and an `io-samurai` board (W5100S-EVB-Pico prototype or W5500-Lite final PCB).

## Prerequisites
- LinuxCNC (`sudo apt install linuxcnc-uspace`).
- Git (`sudo apt install git`).
- Minicom (`sudo apt install minicom`).
- `io-samurai` hardware (W5100S-EVB-Pico or W5500-Lite PCB with MCP23017, MCP23008, TD62783, 10 kΩ potentiometer).
- USB cable for Pico/Pico 2.

## 0.
- **Connect the io-samurai to the local network (RJ45 cable)**

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

## 3.
- **Install miniterm**:
   ```bash
   sudo apt update
   sudo apt install miniterm
   ```

## 4.
- **(disconnect other pico-s if any) -> Connect the io-samurai card to the PC via USB cable**

## 5.
- **Start miniterm**:
  ```bash
  cd ..
  cd firmware/pico/
  chmod +x sterminal.sh
  ./sterminal.sh
  ```

## 6.
- **Set the IP address for io-samurai in the terminal window**:
  - Default ip:192.168.0.177
  - "ip ###.###.###.###" + Enter
- **When the io-samurai restarting you succesfully set the ip address**
- **Disconnect the io-samurai from the USB (unplug) -> close the terminal window**

## 7.
- **Copy the io-samurai.hal file to the linuxcnc/configs/{your_machine} directory**
  - add the following line to your {your_machine}.hal file
  - source io-samurai.hal

## 8.
- **Change the ip address to the set io-samurai ip address in the io-samurai.hal first line**

## 9.
- **Start your linuxcnc machine**
  - check the usable hal pins in 'halshow' like io-samurai.input-00 and io-samurai.output-00

  
