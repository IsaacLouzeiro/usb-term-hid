// ============================================================
//  ESP32-S3 - USB HID study with ST7789 TFT
//  Single sketch: main menu -> Payloads / Config / About
// ------------------------------------------------------------
//  The board acts as a USB KEYBOARD over the native USB port.
//  Script files (.txt) live in the internal flash (LittleFS);
//  example files are created on the first boot.
//
//  The screen orientation (horizontal/vertical) is changed at
//  runtime in the Config menu and saved to /config.txt.
//
//  Navigation with the single BOOT button (GPIO0):
//     lists    click = next   double = previous   hold = select
//     preview  click = back    hold = send (type the script)
//     about    any press = back
//
//  Script commands (one per line):
//     STRING <text>     type the text
//     DELAY <ms>        wait
//     ENTER TAB ESC SPACE BACKSPACE DELETE INSERT   single keys
//     UP DOWN LEFT RIGHT HOME END PAGEUP PAGEDOWN    navigation
//     F1..F12           function keys
//     CTRL/ALT/SHIFT/GUI [+ key]   shortcut, e.g. "CTRL c", "CTRL ALT t", "GUI r"
//     #comment          ignored
//  (GUI = Windows key / Cmd on macOS / Super on Linux)
//
//  Arduino IDE options (Tools):
//     USB Mode        = "USB-OTG (TinyUSB)"
//     USB CDC On Boot = "Enabled"
//
//  Author: IsaacLouzeiro
// ============================================================

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Fonts/FreeMono9pt7b.h>
#include <SPI.h>
#include <LittleFS.h>
#include "USB.h"
#include "USBHIDKeyboard.h"

// ---------- Display pins ----------
#define TFT_SCLK 12
#define TFT_MOSI 11
#define TFT_CS   10
#define TFT_DC    9
#define TFT_RST   8
#define TFT_BLK   7

// ---------- Button ----------
#define BUTTON   0
#define MARGIN_X 12
#define MARGIN_Y 12

// ---------- Button gesture timing ----------
#define LONG_MS   700
#define DOUBLE_MS 350
#define DEBOUNCE  25

#define APP_NAME    "USB HID"
#define APP_AUTHOR  "IsaacLouzeiro"
#define APP_VERSION "v0.5"

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
USBHIDKeyboard keyboard;

uint16_t C_BG, C_GREEN, C_GREEN2, C_BORDER, C_AMBER, C_CYAN;

bool vertical = false;     // false = horizontal, true = vertical
int sentCount = 0;

int W() { return vertical ? 170 : 320; }
int H() { return vertical ? 320 : 170; }

// ---------- Files ----------
#define MAX_FILES 16
String files[MAX_FILES];
int totalFiles = 0;
int currentFile = 0;
String currentScript = "";

// ---------- Menu items (shared buffer) ----------
#define MAX_ITEMS (MAX_FILES + 4)
String items[MAX_ITEMS];
int nItems = 0;
int sel = 0;

// ---------- Screen state ----------
enum Screen { MAIN_MENU, PAYLOADS, PREVIEW, CONFIG, ABOUT };
Screen state = MAIN_MENU;

// ---------- Preview pagination ----------
#define MAX_LINES 48
String lines[MAX_LINES];
int totalLines = 0;
int totalPages = 1;
int currentPage = 0;
int linesPerPage = 3;
const unsigned long PAGE_INTERVAL = 3000;
unsigned long lastSwitch = 0;

// ============================================================
//  CONFIG (LittleFS)
// ============================================================

void saveConfig() {
  File f = LittleFS.open("/config.txt", "w");
  if (f) { f.print(vertical ? "V" : "H"); f.close(); }
}

void loadConfig() {
  File f = LittleFS.open("/config.txt", "r");
  if (f) { if (f.available()) vertical = (f.read() == 'V'); f.close(); }
}

void applyOrientation() {
  tft.setRotation(vertical ? 0 : 1);
}

// ============================================================
//  FILES (LittleFS)
// ============================================================

void writeFile(const char* path, const char* content) {
  File f = LittleFS.open(path, "w");
  if (f) { f.print(content); f.close(); }
}

void seedDefaults() {
  writeFile("/payload1.txt",
    "DELAY 500\n"
    "STRING Hello from file 1! A longer demo text to show the pagination feature on the ST7789 screen.\n"
    "ENTER\n"
    "STRING Loaded from LittleFS.\n");
  writeFile("/payload2.txt",
    "DELAY 300\n"
    "STRING Second script, loaded from its own file.\n"
    "ENTER\n");
}

void listFiles() {
  totalFiles = 0;
  File root = LittleFS.open("/");
  if (!root) return;
  File file = root.openNextFile();
  while (file && totalFiles < MAX_FILES) {
    String n = String(file.name());
    int slash = n.lastIndexOf('/');
    if (slash >= 0) n = n.substring(slash + 1);
    if (n.endsWith(".txt") && n != "config.txt") files[totalFiles++] = n;
    file = root.openNextFile();
  }
}

// File name without the ".txt" extension
String displayName(int i) {
  String n = files[i];
  if (n.endsWith(".txt")) n = n.substring(0, n.length() - 4);
  return n;
}

// ============================================================
//  SCRIPT PARSING / PREVIEW
// ============================================================

String buildPreview(const char* text) {
  String preview = "", line = "";
  for (int i = 0; ; i++) {
    char c = text[i];
    if (c == '\n' || c == '\0') {
      String t = line; t.trim();
      if (t.startsWith("STRING ")) {
        if (preview.length() > 0) preview += " ";
        preview += t.substring(7);
      }
      line = "";
      if (c == '\0') break;
    } else line += c;
  }
  return preview;
}

void wrapLines(const String &txt, int maxChars) {
  String word = "", line = "";
  totalLines = 0;
  for (unsigned int i = 0; i <= txt.length(); i++) {
    char c = (i < txt.length()) ? txt[i] : ' ';
    if (c == ' ') {
      if (line.length() == 0) line = word;
      else if (line.length() + 1 + word.length() <= (unsigned)maxChars) line += " " + word;
      else { if (totalLines < MAX_LINES) lines[totalLines++] = line; line = word; }
      word = "";
    } else word += c;
  }
  if (line.length() > 0 && totalLines < MAX_LINES) lines[totalLines++] = line;
  totalPages = (totalLines + linesPerPage - 1) / linesPerPage;
  if (totalPages < 1) totalPages = 1;
}

void openFile(int i) {
  currentFile = i;
  currentScript = "";
  File f = LittleFS.open("/" + files[i], "r");
  if (f) { while (f.available()) currentScript += (char)f.read(); f.close(); }
  linesPerPage = vertical ? 10 : 3;
  wrapLines(buildPreview(currentScript.c_str()), vertical ? 13 : 24);
  currentPage = 0;
}

// Modifier key for a token, or 0
uint8_t modifierFor(const String &t) {
  if (t == "CTRL" || t == "CONTROL") return KEY_LEFT_CTRL;
  if (t == "ALT")                    return KEY_LEFT_ALT;
  if (t == "SHIFT")                  return KEY_LEFT_SHIFT;
  if (t == "GUI" || t == "WIN" || t == "CMD" || t == "META") return KEY_LEFT_GUI;
  return 0;
}

// HID code for a named key (uppercase token), or 0
uint8_t namedKey(const String &t) {
  if (t == "ENTER" || t == "RETURN")   return KEY_RETURN;
  if (t == "TAB")                      return KEY_TAB;
  if (t == "ESC" || t == "ESCAPE")     return KEY_ESC;
  if (t == "SPACE")                    return ' ';
  if (t == "BACKSPACE" || t == "BKSP") return KEY_BACKSPACE;
  if (t == "DELETE" || t == "DEL")     return KEY_DELETE;
  if (t == "INSERT" || t == "INS")     return KEY_INSERT;
  if (t == "UP")    return KEY_UP_ARROW;
  if (t == "DOWN")  return KEY_DOWN_ARROW;
  if (t == "LEFT")  return KEY_LEFT_ARROW;
  if (t == "RIGHT") return KEY_RIGHT_ARROW;
  if (t == "HOME")  return KEY_HOME;
  if (t == "END")   return KEY_END;
  if (t == "PAGEUP"   || t == "PGUP") return KEY_PAGE_UP;
  if (t == "PAGEDOWN" || t == "PGDN") return KEY_PAGE_DOWN;
  if (t == "CAPS" || t == "CAPSLOCK") return KEY_CAPS_LOCK;
  if (t.length() >= 2 && t[0] == 'F') {
    int n = t.substring(1).toInt();
    if (n >= 1 && n <= 12) return KEY_F1 + (n - 1);
  }
  return 0;
}

// Press one token: modifier, named key or single character. Returns true if handled.
bool pressToken(const String &raw) {
  String t = raw; t.toUpperCase();
  uint8_t m = modifierFor(t); if (m) { keyboard.press(m); return true; }
  uint8_t k = namedKey(t);    if (k) { keyboard.press(k); return true; }
  if (raw.length() == 1) { keyboard.press(raw[0]); return true; }
  return false;
}

void runLine(String line) {
  line.trim();
  if (line.length() == 0 || line.startsWith("#")) return;

  int sp = line.indexOf(' ');
  String cmd = (sp == -1) ? line : line.substring(0, sp);
  String arg = (sp == -1) ? ""   : line.substring(sp + 1);
  String up = cmd; up.toUpperCase();

  if (up == "STRING") { keyboard.print(arg); return; }
  if (up == "DELAY")  { delay(arg.toInt()); return; }

  // Otherwise it is a key or a combo: press each token, then release all.
  // Examples: ENTER / TAB / ESC / F5 / UP / "CTRL c" / "CTRL ALT t" / "GUI r"
  String tok = "";
  String rest = line + " ";
  for (unsigned int i = 0; i < rest.length(); i++) {
    char c = rest[i];
    if (c == ' ') { if (tok.length()) { pressToken(tok); tok = ""; } }
    else tok += c;
  }
  delay(10);
  keyboard.releaseAll();
}

void runScript(const char* text) {
  String line = "";
  for (int i = 0; text[i] != '\0'; i++) {
    char c = text[i];
    if (c == '\n') { runLine(line); line = ""; }
    else            line += c;
  }
  if (line.length() > 0) runLine(line);
}

// ============================================================
//  BUTTON GESTURES - non-blocking
//  returns: 0 none, 1 click, 2 double, 3 long
// ============================================================

int readButton() {
  static bool pressed = false;
  static unsigned long tDown = 0, tUp = 0;
  static int clicks = 0;
  static bool longFired = false;
  unsigned long now = millis();
  bool level = (digitalRead(BUTTON) == LOW);   // true = pressed

  if (level && !pressed) { pressed = true; tDown = now; longFired = false; }

  if (level && pressed && !longFired && (now - tDown >= LONG_MS)) {
    longFired = true; clicks = 0;
    return 3;
  }

  if (!level && pressed) {
    pressed = false; tUp = now;
    if (!longFired && (now - tDown >= DEBOUNCE)) clicks++;
  }

  if (!level && clicks > 0 && (now - tUp >= DOUBLE_MS)) {
    int c = clicks; clicks = 0;
    return (c >= 2) ? 2 : 1;
  }
  return 0;
}

// ============================================================
//  MENU ITEMS
// ============================================================

void buildItems() {
  nItems = 0;
  if (state == MAIN_MENU) {
    items[nItems++] = "Payloads";
    items[nItems++] = "Config";
    items[nItems++] = "About";
  } else if (state == PAYLOADS) {
    items[nItems++] = "< back";
    for (int i = 0; i < totalFiles; i++) items[nItems++] = displayName(i);
  } else if (state == CONFIG) {
    items[nItems++] = "< back";
    items[nItems++] = String("Screen [") + (vertical ? "V" : "H") + "]";
  }
}

// ============================================================
//  DISPLAY
// ============================================================

void footer(const char* l1, const char* l2) {
  int w = W(), h = H();
  int divY = h - 46;
  tft.drawFastHLine(MARGIN_X, divY, w - 2 * MARGIN_X, C_BORDER);
  tft.setFont();
  tft.setTextSize(1);
  tft.setTextColor(C_GREEN);
  tft.setCursor(MARGIN_X, divY + 6);
  tft.print(l1);
  if (l2[0]) { tft.setCursor(MARGIN_X, divY + 18); tft.print(l2); }
}

void drawList(const char* title) {
  int w = W(), h = H();
  tft.fillScreen(C_BG);
  tft.setFont();
  tft.drawRect(4, 4, w - 8, h - 8, C_BORDER);

  tft.setTextSize(1);
  tft.setTextColor(C_GREEN);
  tft.setCursor(MARGIN_X, MARGIN_Y);
  tft.print(title);

  int startY = vertical ? 44 : 36;
  int lineH  = vertical ? 28 : 24;
  int divY   = h - 46;
  int maxVis = (divY - startY) / lineH;
  if (maxVis < 1) maxVis = 1;
  int first = (sel >= maxVis) ? (sel - maxVis + 1) : 0;

  tft.setTextSize(2);
  int y = startY;
  for (int i = first; i < nItems && i < first + maxVis; i++) {
    if (i == sel) {
      tft.fillRect(6, y - 2, w - 12, lineH - 4, C_BORDER);
      tft.setTextColor(C_GREEN);
    } else {
      tft.setTextColor(C_GREEN2);
    }
    tft.setCursor(MARGIN_X, y);
    tft.print(i == sel ? "> " : "  ");
    tft.print(items[i]);
    y += lineH;
  }

  footer("click:next  2x:prev", "hold:select");
}

void drawAbout() {
  int w = W(), h = H();
  tft.fillScreen(C_BG);
  tft.setFont();
  tft.drawRect(4, 4, w - 8, h - 8, C_BORDER);

  tft.setTextSize(1);
  tft.setTextColor(C_GREEN);
  tft.setCursor(MARGIN_X, MARGIN_Y);
  tft.print("esp32@hid:~$ about");

  tft.setTextSize(2);
  tft.setTextColor(C_GREEN);
  tft.setCursor(MARGIN_X, vertical ? 58 : 38);
  tft.print(APP_NAME);

  tft.setTextSize(1);
  tft.setTextColor(C_GREEN2);
  int dy = vertical ? 16 : 14;
  int y  = vertical ? 90 : 64;
  tft.setCursor(MARGIN_X, y); tft.print("ESP32-S3 keyboard"); y += dy;
  tft.setCursor(MARGIN_X, y); tft.print("Author: " APP_AUTHOR); y += dy;
  tft.setCursor(MARGIN_X, y); tft.print("USB HID study");       y += dy;
  tft.setCursor(MARGIN_X, y); tft.print(APP_VERSION);

  footer("click/hold: back", "");
}

void drawPage(int p) {
  int w = W();
  // vertical reserves an extra line above the footer for the sent counter
  int plBottom = vertical ? (H() - 76) : (H() - 56);
  tft.fillRect(6, 26, w - 12, plBottom - 26, C_BG);

  tft.setFont();
  tft.setTextSize(1);
  if (totalPages > 1) {
    tft.fillRect(w - 74, 10, 66, 10, C_BG);
    tft.setTextColor(C_GREEN);
    char pg[12];
    snprintf(pg, sizeof(pg), "pg %d/%d", p + 1, totalPages);
    tft.setCursor(w - 74, MARGIN_Y);
    tft.print(pg);
  }

  tft.setFont(&FreeMono9pt7b);
  tft.setTextColor(C_GREEN2);
  int y = 42;
  int start = p * linesPerPage;
  int drawn = 0;
  for (int i = start; i < totalLines && drawn < linesPerPage; i++, drawn++) {
    tft.setCursor(MARGIN_X, y);
    tft.print(lines[i]);
    y += 18;
  }
  tft.setFont();
  if (p == totalPages - 1) tft.fillRect(MARGIN_X, y - 14, 11, 15, C_GREEN);
}

void drawStatus(const char* text, uint16_t color) {
  int w = W();
  int sy = H() - 24;                  // bottom-most line, just inside the frame
  tft.fillRect(6, sy - 6, w - 12, 24, C_BG);
  tft.setFont();
  tft.setTextSize(2);
  tft.setTextColor(color);
  tft.setCursor(MARGIN_X, sy);
  tft.print(text);

  char buf[16];
  snprintf(buf, sizeof(buf), "sent:%d", sentCount);
  tft.setTextSize(1);
  tft.setTextColor(C_GREEN2);
  if (vertical) {
    // own line just above the footer, right-aligned (no clash with pg x/y)
    int cy = H() - 68;
    int cx = w - MARGIN_X - 6 * (int)strlen(buf);
    tft.fillRect(6, cy - 4, w - 12, 10, C_BG);
    tft.setCursor(cx, cy);
  } else {
    tft.setCursor(w - 70, sy + 6);
  }
  tft.print(buf);
}

void drawPreview() {
  int w = W(), h = H();
  tft.fillScreen(C_BG);
  tft.setFont();
  tft.drawRect(4, 4, w - 8, h - 8, C_BORDER);

  tft.setTextSize(1);
  tft.setTextColor(C_GREEN);
  tft.setCursor(MARGIN_X, MARGIN_Y);
  tft.print("cat ");
  tft.print(displayName(currentFile));

  // Bottom chrome: divider, single hint line, then the status line below it
  int divY = H() - 52;
  tft.drawFastHLine(MARGIN_X, divY, W() - 2 * MARGIN_X, C_BORDER);
  tft.setTextSize(1);
  tft.setTextColor(C_GREEN);
  tft.setCursor(MARGIN_X, divY + 6);
  tft.print("click:back   hold:send");

  drawPage(currentPage);
  drawStatus("[ READY ]", C_GREEN);
}

// Draw whatever the current screen is
void redraw() {
  if (state == MAIN_MENU || state == PAYLOADS || state == CONFIG) buildItems();
  switch (state) {
    case MAIN_MENU: drawList("esp32@hid:~$ menu");   break;
    case PAYLOADS:  drawList("esp32@hid:~$ ls");     break;
    case CONFIG:    drawList("esp32@hid:~$ config"); break;
    case ABOUT:     drawAbout();                     break;
    case PREVIEW:   drawPreview();                   break;
  }
}

void goTo(int s) {
  state = (Screen)s;
  sel = 0;
  redraw();
}

// ============================================================
//  SETUP / LOOP
// ============================================================

void setup() {
  pinMode(BUTTON, INPUT_PULLUP);
  pinMode(TFT_BLK, OUTPUT);
  digitalWrite(TFT_BLK, HIGH);

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.init(170, 320);

  C_BG     = ST77XX_BLACK;
  C_GREEN  = tft.color565(61, 255, 106);
  C_GREEN2 = tft.color565(140, 255, 169);
  C_BORDER = tft.color565(26, 90, 40);
  C_AMBER  = tft.color565(255, 210, 61);
  C_CYAN   = tft.color565(77, 240, 255);

  LittleFS.begin(true);
  loadConfig();
  applyOrientation();
  listFiles();
  if (totalFiles == 0) { seedDefaults(); listFiles(); }

  goTo(MAIN_MENU);

  keyboard.begin();
  USB.begin();
}

void loop() {
  int g = readButton();

  switch (state) {

    case MAIN_MENU:
      if (g == 1) { sel = (sel + 1) % nItems; redraw(); }
      else if (g == 2) { sel = (sel - 1 + nItems) % nItems; redraw(); }
      else if (g == 3) {
        if (sel == 0) goTo(PAYLOADS);
        else if (sel == 1) goTo(CONFIG);
        else goTo(ABOUT);
      }
      break;

    case PAYLOADS:
      if (g == 1) { sel = (sel + 1) % nItems; redraw(); }
      else if (g == 2) { sel = (sel - 1 + nItems) % nItems; redraw(); }
      else if (g == 3) {
        if (sel == 0) goTo(MAIN_MENU);                // "< back"
        else {
          openFile(sel - 1);
          state = PREVIEW;
          lastSwitch = millis();
          drawPreview();
        }
      }
      break;

    case CONFIG:
      if (g == 1) { sel = (sel + 1) % nItems; redraw(); }
      else if (g == 2) { sel = (sel - 1 + nItems) % nItems; redraw(); }
      else if (g == 3) {
        if (sel == 0) {                               // "< back"
          goTo(MAIN_MENU);
        } else {                                      // toggle orientation
          vertical = !vertical;
          applyOrientation();
          saveConfig();
          redraw();
        }
      }
      break;

    case ABOUT:
      if (g != 0) goTo(MAIN_MENU);
      break;

    case PREVIEW:
      if (totalPages > 1 && millis() - lastSwitch >= PAGE_INTERVAL) {
        lastSwitch = millis();
        currentPage = (currentPage + 1) % totalPages;
        drawPage(currentPage);
      }
      if (g == 1 || g == 2) {
        goTo(PAYLOADS);
      } else if (g == 3) {
        drawStatus("[ SENDING ]", C_AMBER);
        runScript(currentScript.c_str());
        sentCount++;
        drawStatus("[ SENT ]", C_CYAN);
        delay(1200);
        drawStatus("[ READY ]", C_GREEN);
      }
      break;
  }
}
