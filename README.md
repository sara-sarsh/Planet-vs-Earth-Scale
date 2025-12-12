# Interplanetary Weight Scale

A unique Arduino-based digital scale that calculates and displays your weight as it would be on different planets in our solar system.

## Features

- **Multi-Planet Weight Calculation**: Automatically converts earth weight to Moon, Mars, Jupiter, Saturn, and Sun weights.
- **Dual Display**: Uses 7-segment displays to show your "Earth Weight" and "Planet Weight" simultaneously.
- **Interactive Controls**:
    - Cycle through planets with a button press.
    - Tare/Zero function.
    - Calibration mode.
    - Trim mode for fine-tuning.
- **EEPROM Storage**: Saves calibration settings, current planet selection, and tare offsets so they persist after power loss.
- **Serial Interface**: Advanced control and debugging via USB Serial Monitor.

## Hardware Setup

### Components
- Arduino Board (Uno, Nano, etc.)
- HX711 Load Cell Amplifier
- Load Cell (e.g., 50kg strain gauge)
- MAX7219 Dot Matrix/7-Segment Display Driver (driven via `LedControl`)
    - *Note: Code assumes 2 devices in the chain.*
- 2x Push Buttons

### Wiring / Pinout

| Component | Arduino Pin | Description |
| :--- | :--- | :--- |
| **HX711** | | |
| DOUT | D4 | Data Out |
| SCK | D5 | Serial Clock |
| **Buttons** | | |
| TARE | D6 | Push button to GND (Input Pullup) |
| PLANET | D7 | Push button to GND (Input Pullup) |
| **MAX7219** | | |
| DIN | D9 | Data In |
| CLK | D10 | Clock |
| CS/LOAD | D11 | Chip Select |

## Dependencies

You will need the following Arduino libraries:
1. **[LedControl](https://github.com/wayoda/LedControl)** - For MAX7219 displays.
2. **[HX711](https://github.com/bogde/HX711)** - For the load cell amplifier.
3. `EEPROM` (Built-in)

## Usage

### Run Mode (Default)
- **Weighing**: Step on the scale.
- **Change Planet**: Short press the **PLANET** button.
    - Order: EARTH -> MOON -> MARS -> JUPITER -> SATURN -> SUN
- **Zero/Tare**: Short press the **TARE** button to reset weight to 0.

### Calibration Mode
1. **Enter**: Long press **TARE** button (approx 1.2s).
2. Follow Serial Monitor prompts or:
    - Ensure scale is empty -> Press **TARE**.
    - Place a known weight.
    - Type `KG <weight>` in Serial Monitor (e.g., `KG 5.0`) and press Enter.
3. Scale saves and returns to Run Mode.

### Trim Mode (Fine Tuning)
1. **Enter**: Long press **PLANET** button.
2. Step on scale.
3. Open Serial Monitor.
    - Type `KG <actual_weight>` then `OK` to auto-calculate correction.
    - Or type `OK` to assume the default target weight (check code `TRIM_TARGET_KG`).

## Serial Commands
Open Serial Monitor at **115200 baud**.
- `HELP`: Show commands.
- `TARE`: Trigger tare.
- `KG <value>`: Input weight for calibration.
- `SF <value>`: Manually set scale factor.
- `OFF <value>`: Manually set offset.
- `SAVE` / `LOAD`: Save to or load from EEPROM.

## License
MIT
