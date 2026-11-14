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
- **Config** — device settings, saved to `/config.txt`:
  - **Screen** — orientation `[H]` horizontal / `[V]` vertical, switched live.
  - **Layout** — keyboard layout for `STRING`: `[U]` US / `[B]` ABNT2 (Brazil).
  - **Wi-Fi** — the payload web editor (see below).
- **About** — program name, author and version.

## Scripts in files (LittleFS)

Scripts are `.txt` files in the board's internal flash. On the **first boot**,
if none exist, the board creates two examples (`payload1.txt` and
`payload2.txt`) automatically — so it works without any upload tool.

To use your own scripts, place `.txt` files in the LittleFS root (with the
"LittleFS Data Upload" tool or, later, over Wi-Fi).

Each file has one command per line:

| Command            | What it does                                   |
|--------------------|------------------------------------------------|
| `STRING text`      | types the text                                 |
| `DELAY 500`        | waits 500 ms                                   |
| `ENTER` `TAB` `ESC` `SPACE` `BACKSPACE` `DELETE` `INSERT` | single keys |
| `UP` `DOWN` `LEFT` `RIGHT` `HOME` `END` `PAGEUP` `PAGEDOWN` | navigation keys |
| `F1` … `F12`       | function keys                                  |
| `CTRL` `ALT` `SHIFT` `GUI` `+ key` | shortcut combo (see below)     |
| `# ...`            | comment (ignored)                              |

Combos put the modifiers and the final key on one line:

```
GUI r              # Windows Run dialog / macOS has no direct equivalent
CTRL c             # copy
CTRL ALT t         # open a terminal on most Linux desktops
CTRL SHIFT ESC     # Task Manager on Windows
```

`GUI` is the Windows key on Windows, Cmd on macOS and Super on Linux.

### Cross-OS notes

- The keyboard is driverless, so it enumerates the same on Windows, macOS and Linux.
- **Shortcuts differ per OS** — write OS-specific scripts (e.g. `win.txt`,
  `mac.txt`, `linux.txt`). Modifiers and function keys are layout-independent.
- **Keyboard layout matters only for `STRING`.** HID sends key *positions*, and
  the target OS maps them using its own layout. Pick it in **Config → Layout**:
  - `US` — default; assumes the target PC uses a US layout.
  - `ABNT2` — remaps the ASCII punctuation (`/ ? ; : ' " [ ] { } \ |`) to the
    Brazilian layout so paths and symbols come out right.
  - Letters and digits are always correct. Accents and `ç` are not handled yet
    (they need UTF-8 input) — planned.

## BOOT button navigation

A single button controls everything, by press duration and count:

| Screen   | Click         | Double click    | Hold (~0.7s)       |
|----------|---------------|-----------------|--------------------|
| List     | next item     | previous item   | select             |
| Preview  | back to list  | back to list    | send (type script) |
| About    | back          | back            | back               |

The `< back` entry is always the first item (at the top) of every list.

## Wi-Fi payload editor

The board can bring up its own Wi-Fi access point with a small web page to
**list, create, edit and delete** the `.txt` payloads — no upload tool needed.

1. On the device: **Config → Wi-Fi**, then **hold BOOT** to turn it on.
2. On your phone/PC, connect to the Wi-Fi network:
   - SSID: `usb-term-hid`
   - Password: `hidsetup123`
3. Open `http://192.168.4.1` in a browser.
4. Edit/create/delete files, then **hold BOOT** again to turn Wi-Fi off.

> Change `WIFI_SSID` / `WIFI_PASS` in the sketch before real use, and keep Wi-Fi
> off when you are not editing — it is a configuration access point, not meant
> to stay on.

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
- [x] Shortcut commands (CTRL/ALT/SHIFT/GUI combos, function and navigation keys)
- [x] Wi-Fi access point with a web page to edit payloads
- [x] Keyboard layout option for `STRING` punctuation (US / ABNT2)
- [ ] Full ABNT2 support including accents and `ç` (UTF-8)
- [ ] USB mass-storage mode to drag-and-drop scripts
