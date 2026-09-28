# Plane Radar — ESP32-2424S012

ESP32 firmware that displays live ADS-B aircraft around a configured location on the round 240×240 display built into the **ESP32-2424S012**.

This version is adapted from **MatixYo/ESP32-Plane-Radar** for the ESP32-2424S012 development board, which combines:

* ESP32-C3
* 1.28" round 240×240 GC9A01 LCD
* Integrated LCD backlight
* USB-C
* Optional CST816 capacitive touch controller

The display is built into the board, so no external LCD wiring is required.

## What it does

Plane Radar shows nearby aircraft on a circular radar-style display using live ADS-B data.

On startup the firmware:

1. Connects to the configured Wi-Fi network.
2. Opens a setup portal if Wi-Fi or location details have not yet been configured.
3. Downloads nearby aircraft data.
4. Displays aircraft position, direction, altitude and other available information on the round radar display.
5. Periodically refreshes ADS-B data while the radar continues running.

## Hardware

Target board:

**ESP32-2424S012**

Main hardware:

| Component         | Device                          |
| ----------------- | ------------------------------- |
| MCU               | ESP32-C3                        |
| Display           | GC9A01                          |
| Resolution        | 240 × 240                       |
| Display interface | SPI                             |
| Backlight         | GPIO controlled                 |
| Touch             | CST816S/D on supported variants |
| USB               | USB-C                           |

Touch is not currently required by Plane Radar.

## ESP32-2424S012 display pinout

The GC9A01 is permanently connected to the ESP32-C3 on this board.

| Function      |                 GPIO |
| ------------- | -------------------: |
| LCD SCLK      |               GPIO 6 |
| LCD MOSI      |               GPIO 7 |
| LCD DC        |               GPIO 2 |
| LCD CS        |              GPIO 10 |
| LCD RESET     | Not connected (`-1`) |
| LCD backlight |               GPIO 3 |
| BOOT button   |               GPIO 9 |

The LCD is write-only in the Plane Radar configuration, so MISO is not used.

### Optional touch controller

Capacitive-touch versions of the ESP32-2424S012 also use:

| Function    |   GPIO |
| ----------- | -----: |
| Touch SDA   | GPIO 4 |
| Touch SCL   | GPIO 5 |
| Touch INT   | GPIO 0 |
| Touch RESET | GPIO 1 |

Plane Radar does not currently use the touchscreen, but these GPIOs should be kept in mind when adding additional hardware.

## Display configuration

Board-specific display settings are defined in:

```text
include/config.h
```

The ESP32-2424S012 configuration is:

```cpp
constexpr gpio_num_t kDisplayPinCs   = GPIO_NUM_10;
constexpr gpio_num_t kDisplayPinDc   = GPIO_NUM_2;
constexpr gpio_num_t kDisplayPinMosi = GPIO_NUM_7;
constexpr gpio_num_t kDisplayPinSclk = GPIO_NUM_6;

constexpr gpio_num_t kDisplayPinRst = GPIO_NUM_NC;
constexpr gpio_num_t kDisplayPinBl  = GPIO_NUM_3;

constexpr int kDisplayWidth  = 240;
constexpr int kDisplayHeight = 240;

constexpr uint32_t kDisplaySpiWriteHz = 40000000;
constexpr uint32_t kDisplaySpiReadHz  = 20000000;

constexpr bool kDisplayInvert   = true;
constexpr bool kDisplayRgbOrder = false;
```

The LovyanGFX device configuration is located in:

```text
include/hardware/lgfx_config.hpp
```

The display uses:

* `lgfx::Panel_GC9A01`
* `SPI2_HOST`
* SPI mode 0
* 3-wire SPI enabled
* automatic DMA channel selection
* 240×240 panel geometry
* no hardware LCD reset pin
* inverted GC9A01 display mode
* BGR/RGB ordering required by this particular panel

A 40 MHz SPI write clock is used as a stable known-working configuration.

## Backlight

The ESP32-2424S012 LCD backlight is connected to **GPIO 3** and is active HIGH.

The backlight is enabled directly during display initialisation:

```cpp
pinMode(static_cast<int>(config::kDisplayPinBl), OUTPUT);
digitalWrite(static_cast<int>(config::kDisplayPinBl), HIGH);
```

This happens before the GC9A01 is initialised.

The current implementation intentionally does not use LovyanGFX `Light_PWM` or `tft.setBrightness()`.

## Controls

The user button is connected to **GPIO 9** and is active LOW.

| Action                           | Result                                |
| -------------------------------- | ------------------------------------- |
| Short press                      | Cycle through radar range presets     |
| Hold for approximately 3 seconds | Clear configuration and restart setup |

The available range presets are:

* 5 km
* 10 km
* 15 km
* 25 km

The selected range is stored in flash and restored after reboot.

## Wi-Fi setup

When no Wi-Fi configuration exists, the device creates the access point:

```text
PlaneRadar-Setup
```

Connect to that network and open the Plane Radar configuration portal.

The default local setup address is:

```text
plane-radar.local
```

If mDNS is unavailable, the setup AP can also be reached at:

```text
192.168.4.1
```

The portal allows configuration of:

* Wi-Fi credentials
* Radar latitude
* Radar longitude
* Distance units
* Airport runway display

Settings are stored in non-volatile storage.

After configuration the ESP32 reconnects automatically and starts the radar display.

## Radar display

The UI shows a circular radar centred on the configured location.

Displayed information can include:

* Nearby aircraft
* Aircraft heading
* Callsign
* Aircraft type
* Altitude
* Speed vector
* Range rings
* Cardinal directions
* Major airport runways

Aircraft outside the main radar ring but still inside the ADS-B query area can appear as indicators around the edge of the screen.

## ADS-B data

Aircraft information is retrieved from **adsb.fi**.

The query location is based on the latitude and longitude entered in the configuration portal.

The search radius scales with the currently selected radar range.

Ground aircraft are hidden by default and can be enabled through the firmware configuration.

## Configuration

Most hardware and firmware defaults are located in:

```text
include/config.h
```

Important options include:

| Area            | Configuration                                     |
| --------------- | ------------------------------------------------- |
| Wi-Fi portal    | AP name, IP address and mDNS hostname             |
| Wi-Fi behaviour | Connection attempts and retry timing              |
| Button          | GPIO and long-press timing                        |
| Display         | SPI pins, clock speed, colour order and inversion |
| Backlight       | GPIO 3                                            |
| Radar location  | Default latitude and longitude                    |
| ADS-B           | Polling interval and ground-aircraft visibility   |

Radar range presets are defined separately in:

```text
include/ui/radar_range.h
```

## Project structure

```text
include/
  config.h

  hardware/
    lgfx_config.hpp
    display.h
    display_font.h

  data/
    large_airports.h

  ui/
    radar_theme.h
    radar_range.h
    radar_display.h
    runway_overlay.h
    status_screens.h

  services/
    wifi_setup.h
    radar_location.h
    adsb_client.h

data/
  ui_font.vlw

scripts/
  build_large_airports.py

src/
  main.cpp

  data/
    large_airports_data.cpp

  hardware/
  ui/
  services/
```

## Building

The firmware uses PlatformIO.

Build and upload with:

```bash
pio run -t upload
```

Open the serial monitor with:

```bash
pio device monitor
```

Serial output runs at:

```text
115200 baud
```

The existing `supermini` PlatformIO environment can continue to be used because both the original target and the ESP32-2424S012 use the ESP32-C3.

## Flashing problems

If PlatformIO cannot connect to the ESP32-C3, manually enter download mode:

1. Hold the BOOT button.
2. Press or trigger RESET.
3. Release BOOT.
4. Retry the upload.

Depending on the ESP32-2424S012 revision and USB connection, using a known-good USB data cable may also be necessary.

## Display troubleshooting

### Backlight is completely off

Check that GPIO 3 is configured as an output and driven HIGH:

```cpp
pinMode(3, OUTPUT);
digitalWrite(3, HIGH);
```

If the backlight does not illuminate, troubleshoot the backlight or board power before investigating SPI communication.

### Backlight works but the display remains black

Verify the following LCD configuration:

```text
SCLK  = GPIO 6
MOSI  = GPIO 7
DC    = GPIO 2
CS    = GPIO 10
RST   = -1
BL    = GPIO 3
```

Also verify:

```text
SPI host     = SPI2_HOST
SPI mode     = 0
SPI 3-wire   = true
invert       = true
rgb_order    = false
resolution   = 240 × 240
```

If necessary, temporarily reduce the SPI write speed to help diagnose communication problems.

For example:

```cpp
constexpr uint32_t kDisplaySpiWriteHz = 10000000;
```

Once the display works reliably, restore the known-working 40 MHz value.

### Wrong colours

Verify:

```cpp
constexpr bool kDisplayInvert   = true;
constexpr bool kDisplayRgbOrder = false;
```

Incorrect RGB/BGR ordering can make the interface appear with incorrect colours even though the display otherwise works normally.

### Test the LCD independently

When diagnosing display problems, temporarily replace the normal UI with:

```cpp
tft.init();
tft.fillScreen(TFT_RED);
```

If the panel becomes red, the GC9A01 and SPI configuration are working and the problem is likely elsewhere in the UI or application startup.

Remove this test once the display is confirmed working.

## Dependencies

The project uses:

* LovyanGFX
* WiFiManager
* ArduinoJson

## Changes from the original hardware configuration

The original ESP32-Plane-Radar project targets an ESP32-C3 Super Mini connected to a separate GC9A01 display.

This version targets the **ESP32-2424S012**, where the ESP32-C3 and GC9A01 are on the same board.

The main hardware changes are:

| Function      | Original Super Mini configuration | ESP32-2424S012 |
| ------------- | --------------------------------: | -------------: |
| LCD RESET     |                            GPIO 0 |  Not connected |
| LCD CS        |                            GPIO 1 |        GPIO 10 |
| LCD DC        |                           GPIO 10 |         GPIO 2 |
| LCD MOSI      |                            GPIO 3 |         GPIO 7 |
| LCD SCLK      |                            GPIO 4 |         GPIO 6 |
| LCD backlight |            External / unspecified |         GPIO 3 |
| RGB order     |                            `true` |        `false` |

The ESP32-2424S012 also requires GPIO 3 to be explicitly enabled for the LCD backlight.

## Credits

Based on the open-source **ESP32-Plane-Radar** project by MatixYo.

ESP32-2424S012 display support was adapted for the board's integrated GC9A01 display and ESP32-C3 pin mapping.

## License

Retain the original ESP32-Plane-Radar project's license and copyright notices when distributing modified versions.
