# LondonBrief_P4

`LondonBrief_P4` targets the `JC8012P4A1C_I_W_Y` board: an `ESP32-P4` application processor with an onboard `ESP32-C6` used for hosted Wi-Fi.

This repo should be maintained around the real hardware workflow, not around generic ESP32 assumptions.

## Hardware Rule

In the current setup, the **P4 is the only practical user-flashable device**.

The onboard `ESP32-C6` should be treated as a **Wi-Fi companion for the P4**, not as a separately maintained firmware target in the normal workflow.

Why this matters:

- The `USB-TTL` schematic shows the `CH340C` connected to the P4 programming path.
- That path reaches the P4 `UART0_RXD`, `UART0_TXD`, `BOOTMODE`, and `CHIP_PU` signals.
- The `ESP32-C6` schematic shows its own separate header and control pins on `CN5`:
  - `C6_U0TXD`
  - `C6_U0RXD`
  - `C6_CHIP_PU`
  - `C6_IO9`
- That C6 header is not the normal development/upload path available in this project workflow.

Practical rule:

- Default development must remain `P4-only`.
- Do not make core functionality depend on reflashing the C6.
- Treat worker-style C6 firmware paths as experimental/reference only unless the physical flashing workflow changes.

## Current Architecture

- `ESP32-P4` runs the UI, touch handling, dashboard logic, and application services.
- The onboard `ESP32-C6` is used by the hosted Wi-Fi stack for P4 networking.
- The normal production environment is `esp32p4-local`.

## PlatformIO Environments

### Main

- `esp32p4-local`
  - Normal P4 build using the hosted Wi-Fi path.

### Debug

- `esp32p4-local-wifi-debug`
  - Extra Wi-Fi startup and hosted-link diagnostics.
- `esp32p4-local-service-debug`
  - Extra Weather/TfL/News service logging.
- `esp32p4-local-full-debug`
  - Combines Wi-Fi and service debug output.

### Experimental / Reference

- `esp32p4-c6-sdio-worker`
- `esp32p4-c6-worker`
- `esp32p4-c6-uart-worker`

These exist for reference and experimentation only. They should not be treated as the default path for this board.

### Tests

- `esp32p4-test`

## Schematic Notes

These notes are here to keep future changes aligned with the attached schematics.

### Flashing / Boot

- `USB-TTL` page:
  - `CH340C` is wired to the P4 upload path.
  - `UART0_RXD`, `UART0_TXD`, `BOOTMODE`, and `CHIP_PU` belong to the P4 flash/reset flow.
- `ESP32-C6` page:
  - The C6 has a separate header `CN5`.
  - That is not the normal user programming path for this project.

### Battery Measurement

- `1_PWR` page:
  - Battery sense is routed to `GPIO52`.
  - Divider values are `68K` and `100K`.
  - Code should continue to assume a divider ratio near `1.68`.

### Display / Backlight

- `2_LCD` page:
  - LCD reset is on `GPIO23`.
  - Backlight control is `LCD_PWM`.

### Touch

- `4_CONN` page:
  - Touch uses `TOUCH_INT`, `TOUCH_RST`, `RTC_CLK/SCL1`, and `RTC_DAT/SDA1`.
- `3_ESP32-P4` page:
  - `GPIO21` -> `TOUCH_INT`
  - `GPIO22` -> `TOUCH_RST`

### TF / SD Card

- `4_CONN` page:
  - The board includes a `TF CARD` section with `SD_CLK`, `SD_CMD`, `SD_DATA0`, `SD_DATA1`, `SD_DATA2`, and `SD_DATA3`.
  - This means removable storage is present in the hardware design and is a viable future path for larger local presets, cached assets, or persistent offline bundles.
  - It should be treated as an optional storage tier rather than a dependency for the core dashboard boot path until the software integration is implemented and validated.

### Audio

- `3_ESP32-P4` and `7_CODEC` pages:
  - `GPIO20` is `PA_CTRL`.

### P4 / C6 Link

- `3_ESP32-P4` and `5_ESP32-C6` pages show the P4/C6 interconnect.
- That interconnect supports hosted networking, but it does **not** change the flashing constraint above.

### Development Note

- Future hardware-facing changes should be checked against the `docs/JC8012P4A1C_I_W_Y/4-Schematic` pages first.
- In particular, storage, touch, audio, and hosted networking decisions should stay aligned with the published board wiring rather than generic ESP32 examples.

## Secrets / Local Configuration

Expected local secrets live in `include/app_secrets.h`.

Typical values include:

- `LONDONBRIEF_WIFI_SSID`
- `LONDONBRIEF_WIFI_PASSWORD`
- `LONDONBRIEF_TFL_APP_KEY`
- TLS settings if required

The app now treats Wi-Fi as configured only when both SSID and password are set to non-placeholder values.

## Testing

Current tests cover:

- protocol framing
- snapshot adaptation
- runtime Wi-Fi helper logic
- Weather/TfL/News parsing helpers

Run tests with:

```powershell
pio test -e esp32p4-test
```

If PlatformIO reports permissions problems in the user home cache, rerun with the necessary permissions so it can access its lock and cache files.
