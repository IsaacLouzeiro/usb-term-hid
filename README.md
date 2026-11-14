# ESP32-S3 USB HID + ST7789 Display

Study project: an **ESP32-S3** that connects to a computer over the native USB
port and acts as a **keyboard** (USB HID). Scripts are stored as **files** in the
internal flash (LittleFS) and chosen from an **on-screen menu** navigated with a
single button (**BOOT**). A **1.9" ST7789 TFT** shows the menu, a preview of the
text (with automatic pagination) and the current status.

> ⚠️ **Educational use.** Only test on computers you own or are authorized to use.
> USB keyboard emulation must be used responsibly.

## Hardware

- ESP32-S3 (with native USB)
- ST7789 1.9" TFT — 170×320, SPI interface

### Wiring

| Display | ESP32-S3 | Function        |
|---------|----------|-----------------|
| VCC     | 3V3      | Power           |
| GND     | GND      | Ground          |
| SCL     | GPIO 12  | SPI clock (SCK) |
| SDA     | GPIO 11  | SPI data (MOSI) |
| CS      | GPIO 10  | Chip select     |
| DC      | GPIO 9   | Data/command    |
| RES     | GPIO 8   | Reset           |
| BLK     | GPIO 7   | Backlight       |

The trigger button is the board's own **BOOT** button (GPIO 0).

## Software

### Dependencies

- [ESP32 Arduino core](https://github.com/espressif/arduino-esp32) (3.x)
- Adafruit ST7735 and ST7789 Library
- Adafruit GFX Library

### Arduino IDE settings (Tools menu)

- **Board:** ESP32S3 Dev Module
- **USB Mode:** `USB-OTG (TinyUSB)`
- **USB CDC On Boot:** `Enabled`

Flash through the UART port and also connect the cable to the native USB port,
which is where the board acts as a keyboard.

## On-screen menu

A single sketch with a main menu navigated by the BOOT button:

- **Payloads** — lists the script files; opening one shows the preview and lets
  you send it.
- **Config** — device settings. Today: **screen orientation**
  (`Screen [H]` / `Screen [V]`), switched live and saved to `/config.txt`.
- **About** — program name, author and version.

## Scripts in files (LittleFS)

Scripts are `.txt` files in the board's internal flash. On the **first boot**,
if none exist, the board creates two examples (`payload1.txt` and
`payload2.txt`) automatically — so it works without any upload tool.

To use your own scripts, place `.txt` files in the LittleFS root (with the
"LittleFS Data Upload" tool or, later, over Wi-Fi).

Each file has one command per line:

| Command       | What it does                      |
|---------------|-----------------------------------|
| `STRING text` | types the text                    |
| `ENTER`       | Enter key                         |
| `TAB`         | Tab key                           |
| `DELAY 500`   | waits 500 ms                      |
| `GUI r`       | Windows/Cmd + r                   |
| `# ...`       | comment (ignored)                 |

## BOOT button navigation

A single button controls everything, by press duration and count:

| Screen   | Click         | Double click    | Hold (~0.7s)       |
|----------|---------------|-----------------|--------------------|
| List     | next item     | previous item   | select             |
| Preview  | back to list  | back to list    | send (type script) |
| About    | back          | back            | back               |

The `< back` entry is always the first item (at the top) of every list.

## How to use

1. Open `usb-term-hid/usb-term-hid.ino` in the Arduino IDE.
2. Set the `Tools` options above and flash the board.
3. In the main menu, open **Payloads** and pick a file with **BOOT**
   (click/double to move, hold to open).
4. In the preview, click into a text editor on the PC and **hold BOOT** to send.
5. Screen orientation lives in **Config → Screen**.

## Roadmap

- [x] Load scripts from files in internal flash (LittleFS)
- [x] On-screen menu navigated by the BOOT button
- [x] Config menu (screen orientation)
- [ ] Upload payloads over Wi-Fi from a web page
- [ ] USB mass-storage mode to drag-and-drop scripts
