# Arduino-WeightSensor

PlatformIO project for an **Arduino Nano (ATmega328P)** that reads a **HX711 load-cell amplifier**, stores calibration in EEPROM, and exchanges weight/calibration data with an ESP32 over I2C.

This project is used by the Wattmeter Test Bench project: https://github.com/mace04/Arduino-WattmeterTestBench (currently via the `ESP` branch).

## Project Overview

The firmware in `src/main.cpp` does the following:

- Reads HX711 on:
  - `DT` pin `9`
  - `SCK` pin `10`
- Uses EEPROM to persist calibration:
  - `scale` default: `1.0`
  - `offset` default: `0`
- Exposes an I2C slave interface at Arduino address `0x11`:
  - Returns current weight on request
  - Accepts calibration updates
  - Accepts a tare/reset command
- Sends weight to ESP32 address `0x42` every `500 ms`
- Emits serial debug logs at `115200`

## PlatformIO Environment

Configured in `platformio.ini`:

- Platform: `atmelavr`
- Board: `nanoatmega328`
- Framework: `arduino`
- Library dependency:
  - `bogde/HX711 @ ^0.7.5`

## Hardware Wiring

### Arduino Nano ↔ HX711

- Nano `D9`  → HX711 `DT`
- Nano `D10` → HX711 `SCK`
- Nano `5V`  → HX711 `VCC` *(or module-required voltage)*
- Nano `GND` → HX711 `GND`

### Arduino Nano ↔ ESP32 (I2C)

- Nano `A4 (SDA)` ↔ ESP32 `SDA`
- Nano `A5 (SCL)` ↔ ESP32 `SCL`
- Nano `GND` ↔ ESP32 `GND`

> Ensure shared ground and correct I2C voltage compatibility.

## Runtime Behavior

### Boot Sequence

1. Starts serial logging at `115200`.
2. Initializes HX711 (`DT=9`, `SCK=10`).
3. Loads calibration from EEPROM.
4. If EEPROM data is invalid/uninitialized, writes defaults (`scale=1.0`, `offset=0`).
5. Starts I2C as slave at `0x11` and registers receive/request callbacks.

### EEPROM Layout

Stored as a `CalibrationData` struct at EEPROM address `0`:

- `magic` (`uint32_t`)
- `scale` (`float`)
- `offset` (`long`)

`magic` must match `EEPROM_MAGIC` (`0x57475331`) for values to be accepted.

## I2C Protocol

### Addresses

- Arduino (this device): `0x11`
- ESP32 target for periodic push: `0x42`

### Arduino Receive (`onReceive`)

Accepts either:

1. **Calibration packet** (8 bytes expected in current implementation):
   - `float scale`
   - `long offset`
2. **Tare/reset command** (1 byte):
   - `0xA5`

Behavior:

- On calibration packet:
  - Validates `scale` (`finite` and non-zero)
  - Saves to EEPROM
  - Re-applies scale/offset
  - Performs `tare()`
- On command `0xA5`:
  - Re-applies current calibration
  - Performs `tare()`

### Arduino Request (`onRequest`)

When master requests data from `0x11`, Arduino returns:

- `latestWeight` as `float` (4 bytes)

### Periodic Arduino → ESP32 Push

Every `500 ms` Arduino transmits to `0x42`:

- `latestWeight` as `float` (4 bytes)

`Wire.endTransmission()` status is logged for diagnostics.

## Serial Debug Output

The firmware prints logs with tags such as:

- `[BOOT]` startup + pin/address summary
- `[EEPROM]` load/save/default decisions
- `[HX711]` calibration apply + tare
- `[I2C]` calibration updates and tare command handling
- `[TX]` sent weight/raw/tx status

Baud rate: `115200`.

## Build, Upload, Monitor

From project root:

```bash
platformio run
platformio run --target upload
platformio device monitor --baud 115200
```

## ESP32 Test Harness

A helper sketch is included at:

- `test/esp32_harness/esp32_harness.ino`

It automatically:

- Sends calibration packets to Arduino `0x11`
- Requests weight every `500 ms`
- Validates payload size and value sanity
- Prints communication/error counters

## Important Notes

1. **Current test override in `src/main.cpp`**
   
   The current code includes:

   ```cpp
   latestWeight = 55;
   ```

   This forces transmitted/reported weight to a constant for testing and overrides computed HX711 weight. Remove/comment this line to use real sensor values.

2. **Data type interoperability**

   `long` is platform-dependent across architectures. If you want stricter cross-device packet compatibility, switch protocol fields to fixed-width types (for example `int32_t`).

3. **I2C push model**

   The Arduino periodically transmits to `0x42`, so the ESP32 side should be prepared to receive or otherwise tolerate these writes.

## Repository Structure

- `platformio.ini` — PlatformIO environment configuration
- `src/main.cpp` — main firmware
- `test/esp32_harness/esp32_harness.ino` — ESP32 validation sketch
- `include/`, `lib/`, `test/README` — standard PlatformIO scaffold folders
