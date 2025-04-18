# HAL User Guide for `io-samurai` Component

The `io-samurai` component is a Hardware Abstraction Layer (HAL) driver for a UDP-based I/O interface, designed for real-time control systems like LinuxCNC. It communicates with a remote device over UDP to handle digital and analog inputs/outputs. This guide explains the component's pins, parameters, and their roles.

## Overview

The `io-samurai` component:
- Communicates via UDP on port 8888 with a configurable IP address.
- Supports 16 digital inputs, 8 digital outputs, and 1 analog input.
- Includes a watchdog mechanism to detect communication timeouts.
- Provides low-pass filtering and scaling for the analog input.
- Integrates with LinuxCNC's HAL for real-time I/O processing.

## HAL Pins

### Digital Inputs
- **Pin Names**: `io-samurai.input-00` to `io-samurai.input-15` (HAL_OUT, bit)
- **Description**: These 16 pins represent the state of digital inputs received from the remote device (0 or 1). Each pin corresponds to one bit in the received UDP packet.
- **Negated Inputs**: `io-samurai.input-00-not` to `io-samurai.input-15-not` (HAL_OUT, bit)
  - These pins provide the logical negation of the corresponding input pins (e.g., `input-00-not` is the inverse of `input-00`).

### Digital Outputs
- **Pin Names**: `io-samurai.output-00` to `io-samurai.output-07` (HAL_IN, bit)
- **Description**: These 8 pins control the digital outputs sent to the remote device. Setting a pin to 1 or 0 determines the state of the corresponding output bit in the UDP packet.

### Analog Input
- **Pin Name**: `io-samurai.analog-in` (HAL_OUT, float)
- **Description**: Represents the scaled and filtered analog input value from the remote device, derived from a 12-bit ADC (0–4095 range). The value is processed with a low-pass filter (EMA, alpha = 0.1) and scaled using the `analog-scale` parameter.
- **Pin Name**: `io-samurai.analog-in-s32` (HAL_OUT, s32)
  - **Description**: Provides the same analog input value as `analog-in`, but cast to a 32-bit integer. This is useful for applications requiring integer values or for overriding floating-point behavior in certain LinuxCNC configurations.

### Analog Parameters
- **Pin Name**: `io-samurai.analog-scale` (HAL_IN, float)
  - **Description**: Defines the scaling factor for the analog input. The raw ADC value (0–4095) is divided by the maximum ADC value (4095) and multiplied by this scale to produce the `analog-in` value. Default value is 1.0.
  - **Example**: If `analog-scale` is set to 5.0, a raw ADC value of 4095 results in an `analog-in` value of 5.0.
- **Pin Name**: `io-samurai.analog-rounding` (HAL_IN, bit)
  - **Description**: Controls whether the scaled analog input value is rounded to the nearest integer before being assigned to `analog-in` and `analog-in-s32`. If set to 1, rounding is enabled; if 0, the value remains a floating-point number. Default is 0 (no rounding).
  - **Use Case**: Rounding is useful when precise integer values are needed for control logic or to avoid floating-point precision issues.

### Connection and Status Pins
- **Pin Name**: `io-samurai.connected` (HAL_IN, bit)
  - **Description**: Indicates the connection status with the remote device. Set to 1 when valid UDP packets with correct checksums are received; otherwise, set to 0.
- **Pin Name**: `io-samurai.io-ready-in` (HAL_IN, bit)
  - **Description**: An input pin that signals the readiness of the I/O system from the LinuxCNC side. It is typically set by the control system to indicate that it is ready to process I/O data.
- **Pin Name**: `io-samurai.io-ready-out` (HAL_OUT, bit)
  - **Description**: Reflects the overall readiness of the I/O system. It is set to the value of `io-ready-in` when communication is active and the watchdog has not timed out; otherwise, it is set to 0.

## Functions
The component exports three HAL functions that run periodically:
1. **io-samurai.watchdog-process**: Monitors communication timeouts (100 ms). If no valid packets are received within this period, it sets `watchdog_expired` and logs an error.
2. **io-samurai.udp-io-process-recv**: Handles incoming UDP packets, updates input pins, and processes the analog input.
3. **io-samurai.udp-io-process-send**: Sends output data to the remote device based on the state of output pins.

## Parameters
- **ip_address**: A module parameter (array of strings) specifying the IP address of the remote device. Configured via `RTAPI_MP_ARRAY_STRING`.
  - **Example**: `ip_address=192.168.1.100`

## Usage Notes
1. **Setup**: Load the component in LinuxCNC with the appropriate IP address (e.g., `loadrt io-samurai ip_address=192.168.1.100`).
2. **Analog Input Tuning**:
   - Adjust `analog-scale` to match the desired output range (e.g., 0–10 V).
   - Enable `analog-rounding` if integer values are required.
   - Use `analog-in-s32` for applications needing integer overrides or compatibility with certain LinuxCNC components.
3. **Watchdog**: If a watchdog timeout occurs, the component logs an error and requires a LinuxCNC restart. Monitor the `connected` pin to detect communication issues.
4. **Real-Time Constraints**: Ensure the component runs in a real-time thread to maintain low-latency I/O processing.

## Example HAL Configuration
```hal
# Load the component
loadrt io-samurai ip_address=192.168.1.100

# Set analog scaling
setp io-samurai.analog-scale 5.0

# Enable rounding
setp io-samurai.analog-rounding 1

# Connect input to a signal
net input-00-signal io-samurai.input-00 => some-component.input

# Connect output to a signal
net output-00-signal some-component.output => io-samurai.output-00

# Monitor connection status
net connected-signal io-samurai.connected => some-component.status

## Error Handling
- **Checksum Errors**: Invalid UDP packet checksums are logged, and `connected` is set to 0.
- **Watchdog Timeout**: If no valid packets are received within 100 ms, `watchdog_expired` is set, and `io-ready-out` is cleared.
- **Socket Errors**: Socket initialization or binding failures are logged, and the component exits with an error.

## Notes on `analog-in-s32`
The `analog-in-s32` pin exists to support scenarios where LinuxCNC components require integer inputs or where floating-point values need to be overridden for compatibility. For example, some motion control modules may expect integer ADC values, and `analog-in-s32` provides a direct way to supply these without additional conversion.

## License
The `io-samurai` component is licensed under the GPL.

---

This guide provides a concise overview of the `io-samurai` component's functionality and configuration. For advanced usage, refer to the source code or LinuxCNC documentation.
