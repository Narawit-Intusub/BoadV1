# 2-Channel Logic Level Converter Module (3.3V - 5V)

## Overview
- **Module**: 2-Channel Bi-directional Logic Level Converter (CyberTice M1655 / MH-Level Converter)
- **Dimensions**: 15.5mm x 11.2mm
- **Row-to-Row Spacing**: 12.70mm (0.5 inch / 500 mil pitch)
- **Pin Pitch**: 2.54mm (0.1 inch)
- **Application**: Bidirectional UART/I2C voltage shifting between 3.3V (ESP32) and 5.0V (ATtiny1604)

## Physical Pinout (Matching Module Silkscreen)
### Left Column (High Voltage / 5V Side)
- **Pin 1**: `5V` (Square Pad, 5V Power Input)
- **Pin 2**: `GND` (Ground)
- **Pin 3**: `HV1` (High Voltage Channel 1, Transmit Out to ATtiny RX)
- **Pin 4**: `HV2` (High Voltage Channel 2, Receive In from ATtiny TX)

### Right Column (Low Voltage / 3.3V Side)
- **Pin 5**: `3V3` (Square Pad, 3.3V Power Input)
- **Pin 6**: `GND` (Ground)
- **Pin 7**: `LV1` (Low Voltage Channel 1, Transmit In from ESP32 TX)
- **Pin 8**: `LV2` (Low Voltage Channel 2, Receive Out to ESP32 RX)
