# io-samurai Pinout and Ratings

## 1.Power (5v)
- **Connector**: 2-pin header (J5)
- **Notes**: Connects to the Vsys pin trough schottky diode

## 2.Power (20v - 50v)
- **Connector**: 4-pin header (J4)
- **Notes**: Powering the TD62783

## Inputs (16)
- **IC**: MCP23017 (I2C, 3.3 V V_DD, GPA0–GPA7, GPB0–GPB7).
- **Connector**: 2x8-pin header.
- **Protection**: 10 kΩ/1.5 kΩ voltage divider, 3.6 V Zener diode (SOD523).
- **Ratings**:
  - **Min. Input Voltage**: 20.25 V (for high logic level, ≥ 2.64 V on MCP23017).
  - **Max. Input Voltage**: 50 V (Zener limits to 3.6 V).
  - **Max. Input Current**: ~2.04 mA (24 V input).
  - **Logic Levels**: High ≥ 2.64 V, Low ≤ 0.66 V (3.3 V V_DD).
  - **Power Dissipation**:
    - R1 (10 kΩ): ~41.6 mW (24 V).
    - R2 (1.5 kΩ): ~8.64 mW.
    - Zener: ~7.34 mW (24 V).
- **Notes**: Designed for 20–50 V industrial signals. For 5 V or 12 V inputs, additional interfacing (e.g., transistor, optocoupler) recommended.

## Outputs (8)
- **IC**: TD62783 (8-channel Darlington driver), driven by MCP23008 (I2C, 3.3 V).
- **Connector**: 8-pin header.
- **Ratings**:
  - **Input Voltage**: 3.3 V (MCP23008 GPIO, high ≥ 2.0 V, low ≤ 0.8 V).
  - **Max. Output Voltage**: 50 V (open collector).
  - **Max. Output Current**: 500 mA/channel, ~1.5 A total (cooling required).
  - **Typical Load**: 5–24 V, 50–200 mA/channel (relays, LEDs).
  - **Power Dissipation**: Max. 1.47 W (25°C, DIP-18).
- **Notes**: Use flyback diodes (e.g., 1N4007) for inductive loads. MCP23008 controlled via I2C (Pico GP4 SDA, GP5 SCL).

## Analog Inputs (1)
- **Pins**: GP26 (ADC0).
- **Connector**: 3-pin header (VCC, GND, signal).
- **Protection/Filtering**: 100 nF capacitor, 10 kΩ pull-down.
- **Potentiometer**:
  - **Recommended Value**: 10 kΩ (linear, B10K).
  - **Range**: 0–3.3 V.
  - **Time Constant**: ~0.5 ms (with 100 nF capacitor).
  - **Power Consumption**: ~1 mW (3.3 V).
  - **Alternatives**: 4.7 kΩ (faster, ~2 mW), 22 kΩ (slower, ~0.5 mW).
- **Ratings**:
  - **Input Voltage**: 0–3.3 V.
  - **Resolution**: 12-bit (0–4095).
