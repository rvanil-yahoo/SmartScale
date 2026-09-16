# SmartScale (ESP32 + HX711 + ST7735)

This is an old fitbit smart scale that is constantly having issues connecting to wifi. Gutted out the internals and replaced it with ESP32-based firmware that reads an HX711 load-cell amplifier and shows weight on an ST7735 TFT. It also has a 3000ah 3.7v single cell lipo rechargeable battery for power.

The current firmware is optimized for low power:
- Wi-Fi and Bluetooth are disabled at startup
- HX711 is powered down during sleep and powered up on wake
- TFT panel and backlight stay off during sleep
- TFT only powers up and redraws when the detected rounded weight changes

## Hardware Wiring

All red wires go to the HX711 input lines E-, E+, A-, A+. Black and white wires are cross joined. Note that red wires carry signal in this case, so be careful while soldering wires properly.

![Fully Assembled](https://github.com/rvanil-yahoo/SmartScale/blob/main/20260719_132254.jpg)


### HX711
- `DT` -> GPIO `34`
- `SCK` -> GPIO `21`

### ST7735 TFT (SPI)
- `CS` -> GPIO `14`
- `DC` -> GPIO `4`
- `RST/RES` -> GPIO `5`
- `SDA/MOSI` -> GPIO `18`
- `SCL/SCLK` -> GPIO `19`
- `BL/LED` (backlight) -> GPIO `15` (active HIGH in firmware)

## Firmware Behavior (Current)

### Calibration and Weight Conversion
- Raw-to-weight model:

  `weight_lb = max(0, (raw - CAL_OFFSET_COUNTS) / CAL_COUNTS_PER_LB)`

- Calibration constants currently in code:
  - `CAL_OFFSET_COUNTS = 1114690.18`
  - `CAL_COUNTS_PER_LB = 9169.7606`

### Display Behavior
- Default UI background color: black (`ST77XX_BLACK`)
- Header: centered `Smart Scale`
- Reading text color:
  - green for valid weight
  - red for `saturated`/`not ready`
- Display update is event-based:
  - panel stays off by default
  - panel/backlight only turn on when rounded weight value changes

### Sleep and Power-Save Logic
- Radio power saving:
  - `esp_wifi_stop()`
  - `esp_wifi_deinit()`
  - `btStop()`
- Stability gate:
  - sleep only after `STABLE_SAMPLES_FOR_SLEEP` consecutive equal rounded samples
  - current value: `10`
- Sleep mode:
  - ESP32 light sleep (`esp_light_sleep_start()`)
  - timer wake configured with:
    - `SCREEN_REFRESH_MS = 1000`
    - wake interval expression in code:
      `SCREEN_REFRESH_MS * 300000` microseconds
    - effective wake period is ~5 minutes
- Around sleep:
  - `scale.power_down()` before sleep
  - `scale.power_up()` after wake
  - settle delay after wake: `HX711_WAKE_SETTLE_MS = 5`

## Build

Project uses PlatformIO (`platformio.ini`) with `esp32dev` environment.

Typical build command:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run
```

## Key Source File

- `src/main.cpp`

## Notes

- HX711 saturation rail values are treated as invalid:
  - `8388607`
  - `-8388608`
- Negative computed weight is clamped to `0`.
- If your TFT backlight is wired active-LOW instead of active-HIGH, invert backlight logic in `setBacklight()`.
