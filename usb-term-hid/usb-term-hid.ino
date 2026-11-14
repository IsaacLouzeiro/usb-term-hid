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
#include <WiFi.h>
#include <WebServer.h>
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
#define APP_VERSION "v0.6"

// ---------- Wi-Fi config AP ----------
#define WIFI_SSID "usb-term-hid"
#define WIFI_PASS "hidsetup123"      // >= 8 chars (WPA2)

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
USBHIDKeyboard keyboard;
WebServer server(80);
bool wifiOn = false;

uint16_t C_BG, C_GREEN, C_GREEN2, C_BORDER, C_AMBER, C_CYAN;

bool vertical = false;     // false = horizontal, true = vertical
bool layoutBR = false;     // false = US layout, true = ABNT2 (Brazil)
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
enum Screen { MAIN_MENU, PAYLOADS, PREVIEW, CONFIG, ABOUT, WIFI_SCREEN };
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
  if (f) { f.print(String(vertical ? "V" : "H") + (layoutBR ? "B" : "U")); f.close(); }
}

void loadConfig() {
  File f = LittleFS.open("/config.txt", "r");
  if (!f) return;
  String s = "";
  while (f.available()) s += (char)f.read();
  f.close();
  if (s.length() >= 1) vertical = (s[0] == 'V');
  if (s.length() >= 2) layoutBR = (s[1] == 'B');
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

// Fold a Latin-1 accented codepoint to its ASCII base (for the TFT preview only)
char foldChar(uint16_t cp) {
  if (cp >= 0xC0 && cp <= 0xC5) return 'A';
  if (cp == 0xC7)               return 'C';
  if (cp >= 0xC8 && cp <= 0xCB) return 'E';
  if (cp >= 0xCC && cp <= 0xCF) return 'I';
  if (cp == 0xD1)               return 'N';
  if (cp >= 0xD2 && cp <= 0xD6) return 'O';
  if (cp >= 0xD9 && cp <= 0xDC) return 'U';
  if (cp >= 0xE0 && cp <= 0xE5) return 'a';
  if (cp == 0xE7)               return 'c';
  if (cp >= 0xE8 && cp <= 0xEB) return 'e';
  if (cp >= 0xEC && cp <= 0xEF) return 'i';
  if (cp == 0xF1)               return 'n';
  if (cp >= 0xF2 && cp <= 0xF6) return 'o';
  if (cp >= 0xF9 && cp <= 0xFC) return 'u';
  return '?';
}

// The terminal font has no accent glyphs, so normalize UTF-8 text to ASCII
// for display. Typing uses the original bytes, so accents still go out right.
String foldAscii(const String &s) {
  String o = "";
  for (unsigned int i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    if (c == 0xC3 && i + 1 < s.length()) {
      o += foldChar(0xC0 | ((uint8_t)s[i + 1] & 0x3F));
      i++;
    } else if (c == 0xC2 && i + 1 < s.length()) {
      o += '?';
      i++;
    } else if (c < 0x80) {
      o += (char)c;
    } else {
      o += '?';
    }
  }
  return o;
}

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
  return foldAscii(preview);
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

// ============================================================
//  KEYBOARD LAYOUT (STRING typing)
//  US: use the library's print(). BR (ABNT2): remap punctuation
//  that sits on a different key than US, type accented letters
//  via dead keys, and type ç directly. Script files must be
//  saved as UTF-8 for accents to work.
// ============================================================

// Send one raw HID key (usage id) with a modifier byte, then release.
void sendRaw(uint8_t mod, uint8_t usage) {
  KeyReport r;
  r.modifiers = mod;
  r.reserved = 0;
  for (int i = 0; i < 6; i++) r.keys[i] = 0;
  r.keys[0] = usage;
  keyboard.sendReport(&r);
  r.modifiers = 0;
  r.keys[0] = 0;
  keyboard.sendReport(&r);
}

// ABNT2 remap for ASCII punctuation that sits on a different key than US.
// mod: 0x02 = Shift. usage = HID usage id of the physical key.
bool brRemap(char c, uint8_t &mod, uint8_t &usage) {
  switch (c) {
    case '/':  mod = 0x00; usage = 0x87; return true;
    case '?':  mod = 0x02; usage = 0x87; return true;
    case ';':  mod = 0x00; usage = 0x38; return true;
    case ':':  mod = 0x02; usage = 0x38; return true;
    case '\'': mod = 0x00; usage = 0x35; return true;
    case '"':  mod = 0x02; usage = 0x35; return true;
    case '[':  mod = 0x00; usage = 0x30; return true;
    case '{':  mod = 0x02; usage = 0x30; return true;
    case ']':  mod = 0x00; usage = 0x31; return true;
    case '}':  mod = 0x02; usage = 0x31; return true;
    case '\\': mod = 0x00; usage = 0x64; return true;
    case '|':  mod = 0x02; usage = 0x64; return true;
    default:   return false;
  }
}

// Type a dead key then a base letter (ABNT2). d: 1 acute, 2 grave, 3 circ, 4 tilde
void typeDead(int d, char base) {
  if (d == 1)      sendRaw(0x00, 0x2F);   // ´ acute
  else if (d == 2) sendRaw(0x02, 0x2F);   // ` grave
  else if (d == 3) sendRaw(0x02, 0x34);   // ^ circumflex
  else if (d == 4) sendRaw(0x00, 0x34);   // ~ tilde
  delay(5);
  keyboard.print(base);
}

// Type one Latin-1 accented codepoint (U+00C0..U+00FF) on ABNT2
void typeAccented(uint16_t cp) {
  switch (cp) {
    case 0xE1: typeDead(1, 'a'); break;  // á
    case 0xE9: typeDead(1, 'e'); break;  // é
    case 0xED: typeDead(1, 'i'); break;  // í
    case 0xF3: typeDead(1, 'o'); break;  // ó
    case 0xFA: typeDead(1, 'u'); break;  // ú
    case 0xC1: typeDead(1, 'A'); break;
    case 0xC9: typeDead(1, 'E'); break;
    case 0xCD: typeDead(1, 'I'); break;
    case 0xD3: typeDead(1, 'O'); break;
    case 0xDA: typeDead(1, 'U'); break;
    case 0xE0: typeDead(2, 'a'); break;  // à
    case 0xC0: typeDead(2, 'A'); break;
    case 0xE2: typeDead(3, 'a'); break;  // â
    case 0xEA: typeDead(3, 'e'); break;  // ê
    case 0xF4: typeDead(3, 'o'); break;  // ô
    case 0xC2: typeDead(3, 'A'); break;
    case 0xCA: typeDead(3, 'E'); break;
    case 0xD4: typeDead(3, 'O'); break;
    case 0xE3: typeDead(4, 'a'); break;  // ã
    case 0xF5: typeDead(4, 'o'); break;  // õ
    case 0xF1: typeDead(4, 'n'); break;  // ñ
    case 0xC3: typeDead(4, 'A'); break;
    case 0xD5: typeDead(4, 'O'); break;
    case 0xD1: typeDead(4, 'N'); break;
    case 0xE7: sendRaw(0x00, 0x33); break;  // ç
    case 0xC7: sendRaw(0x02, 0x33); break;  // Ç
    default: break;                          // unsupported: skip
  }
}

void typeString(const String &s) {
  if (!layoutBR) { keyboard.print(s); return; }
  unsigned int i = 0;
  while (i < s.length()) {
    uint8_t c = (uint8_t)s[i];

    // UTF-8 Latin-1 supplement (accents, ç): 0xC3 0x80..0xBF
    if (c == 0xC3 && i + 1 < s.length()) {
      uint16_t cp = 0xC0 | ((uint8_t)s[i + 1] & 0x3F);
      typeAccented(cp);
      i += 2;
      continue;
    }

    char ch = (char)c;
    uint8_t mod, usage;
    if (ch == '~')      { sendRaw(0x00, 0x34); sendRaw(0x00, 0x2C); }  // tilde + space
    else if (ch == '^') { sendRaw(0x02, 0x34); sendRaw(0x00, 0x2C); }  // circ + space
    else if (ch == '`') { sendRaw(0x02, 0x2F); sendRaw(0x00, 0x2C); }  // grave + space
    else if (brRemap(ch, mod, usage)) sendRaw(mod, usage);
    else keyboard.print(ch);
    i += 1;
  }
}

void runLine(String line) {
  line.trim();
  if (line.length() == 0 || line.startsWith("#")) return;

  int sp = line.indexOf(' ');
  String cmd = (sp == -1) ? line : line.substring(0, sp);
  String arg = (sp == -1) ? ""   : line.substring(sp + 1);
  String up = cmd; up.toUpperCase();

  if (up == "STRING") { typeString(arg); return; }
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
    items[nItems++] = String("Screen: ") + (vertical ? "Vertical" : "Horizontal");
    items[nItems++] = String("Layout: ") + (layoutBR ? "ABNT2" : "US");
    items[nItems++] = "Wi-Fi";
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

  // Config uses a smaller font so the full labels fit in both orientations
  int itemSize = (state == CONFIG) ? 1 : 2;
  int startY = vertical ? 44 : 36;
  int lineH  = (itemSize == 2) ? (vertical ? 28 : 24) : 20;
  int barH   = (itemSize == 2) ? (lineH - 4) : 14;
  int divY   = h - 46;
  int maxVis = (divY - startY) / lineH;
  if (maxVis < 1) maxVis = 1;
  int first = (sel >= maxVis) ? (sel - maxVis + 1) : 0;

  tft.setTextSize(itemSize);
  int y = startY;
  for (int i = first; i < nItems && i < first + maxVis; i++) {
    if (i == sel) {
      tft.fillRect(6, y - 2, w - 12, barH, C_BORDER);
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

// ============================================================
//  WEB UI (Wi-Fi SoftAP) - edit payload files from a browser
// ============================================================

String htmlEscape(const String &s) {
  String o = "";
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else o += c;
  }
  return o;
}

// Keep only a safe file name ending in .txt (never config.txt)
String sanitizeName(String n) {
  n.trim();
  int slash = n.lastIndexOf('/');
  if (slash >= 0) n = n.substring(slash + 1);
  String out = "";
  for (unsigned int i = 0; i < n.length(); i++) {
    char c = n[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    if (ok) out += c;
  }
  if (out.length() == 0) return "";
  if (!out.endsWith(".txt")) out += ".txt";
  if (out == "config.txt") return "";
  return out;
}

String htmlHeader(const String &title) {
  return "<!doctype html><html><head><meta charset=utf-8>"
         "<meta name=viewport content='width=device-width,initial-scale=1'>"
         "<title>" + title + "</title><style>"
         "body{background:#050805;color:#8cffa9;font-family:monospace;margin:0;padding:16px}"
         "h1,h2{color:#3dff6a}a{color:#4df0ff;text-decoration:none}"
         "textarea{width:100%;height:50vh;background:#000;color:#8cffa9;"
         "border:1px solid #1a5a28;font-family:monospace;font-size:14px;padding:8px;box-sizing:border-box}"
         "input[type=text]{background:#000;color:#8cffa9;border:1px solid #1a5a28;padding:6px;font-family:monospace}"
         "button,input[type=submit]{background:#1a5a28;color:#3dff6a;border:0;padding:8px 14px;"
         "font-family:monospace;cursor:pointer}"
         ".row{display:flex;gap:10px;align-items:center;margin:6px 0;"
         "padding:4px 0;border-bottom:1px solid #123}.del{color:#ff6a6a}"
         "</style></head><body>";
}

void handleRoot() {
  listFiles();
  String p = htmlHeader("usb-term-hid");
  p += "<h1>usb-term-hid</h1><h2>payloads</h2>";
  for (int i = 0; i < totalFiles; i++) {
    String n = files[i];
    p += "<div class=row><span style='flex:1'>" + n + "</span>";
    p += "<a href='/edit?file=" + n + "'>edit</a>";
    p += "<a class=del href='/delete?file=" + n + "'>delete</a></div>";
  }
  p += "<h2>new file</h2><form action='/edit' method='get'>"
       "<input type=text name=file placeholder='name.txt'> "
       "<input type=submit value='create / open'></form>";
  p += "</body></html>";
  server.send(200, "text/html", p);
}

void handleEdit() {
  String name = sanitizeName(server.arg("file"));
  if (name == "") { server.sendHeader("Location", "/"); server.send(303, "text/plain", ""); return; }
  String content = "";
  File f = LittleFS.open("/" + name, "r");
  if (f) { while (f.available()) content += (char)f.read(); f.close(); }
  String p = htmlHeader("edit " + name);
  p += "<h1>edit " + name + "</h1>";
  p += "<form action='/save' method='post'>";
  p += "<input type=hidden name=file value='" + name + "'>";
  p += "<textarea name=content>" + htmlEscape(content) + "</textarea><br><br>";
  p += "<button type=submit>save</button> <a href='/'>cancel</a></form>";
  p += "</body></html>";
  server.send(200, "text/html", p);
}

void handleSave() {
  String name = sanitizeName(server.arg("file"));
  if (name != "") {
    File f = LittleFS.open("/" + name, "w");
    if (f) { f.print(server.arg("content")); f.close(); }
  }
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "");
}

void handleDelete() {
  String name = sanitizeName(server.arg("file"));
  if (name != "") LittleFS.remove("/" + name);
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "");
}

void startWifi() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_SSID, WIFI_PASS);
  server.on("/", handleRoot);
  server.on("/edit", handleEdit);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/delete", handleDelete);
  server.begin();
  wifiOn = true;
}

void stopWifi() {
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiOn = false;
}

void drawWifi() {
  int w = W(), h = H();
  tft.fillScreen(C_BG);
  tft.setFont();
  tft.drawRect(4, 4, w - 8, h - 8, C_BORDER);

  tft.setTextSize(1);
  tft.setTextColor(C_GREEN);
  tft.setCursor(MARGIN_X, MARGIN_Y);
  tft.print("esp32@hid:~$ wifi");

  tft.setTextSize(2);
  tft.setTextColor(C_GREEN);
  tft.setCursor(MARGIN_X, vertical ? 58 : 38);
  tft.print(wifiOn ? "Wi-Fi ON" : "Wi-Fi OFF");

  tft.setTextSize(1);
  tft.setTextColor(C_GREEN2);
  int dy = vertical ? 16 : 14;
  int y  = vertical ? 90 : 64;
  if (wifiOn) {
    tft.setCursor(MARGIN_X, y); tft.print("SSID: " WIFI_SSID);  y += dy;
    tft.setCursor(MARGIN_X, y); tft.print("Pass: " WIFI_PASS);  y += dy;
    tft.setCursor(MARGIN_X, y); tft.print("URL:  192.168.4.1"); y += dy;
    tft.setCursor(MARGIN_X, y); tft.print("Clients: ");
    tft.print(WiFi.softAPgetStationNum());
  } else {
    tft.setCursor(MARGIN_X, y); tft.print("Turn on to edit"); y += dy;
    tft.setCursor(MARGIN_X, y); tft.print("payloads from a"); y += dy;
    tft.setCursor(MARGIN_X, y); tft.print("web browser.");
  }

  footer(wifiOn ? "hold: turn off" : "hold: turn on", "click: back");
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
    case WIFI_SCREEN: drawWifi();                    break;
  }
}

void goTo(int s) {
  state = (Screen)s;
  sel = 0;
  if (state == PAYLOADS) listFiles();   // reflect web edits
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
  if (wifiOn) server.handleClient();
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
        if (sel == 0) goTo(MAIN_MENU);                // "< back"
        else if (sel == 1) {                          // toggle orientation
          vertical = !vertical;
          applyOrientation();
          saveConfig();
          redraw();
        } else if (sel == 2) {                        // toggle keyboard layout
          layoutBR = !layoutBR;
          saveConfig();
          redraw();
        } else {                                      // Wi-Fi
          goTo(WIFI_SCREEN);
        }
      }
      break;

    case ABOUT:
      if (g != 0) goTo(MAIN_MENU);
      break;

    case WIFI_SCREEN:
      if (g == 3) { if (wifiOn) stopWifi(); else startWifi(); drawWifi(); }
      else if (g == 1 || g == 2) goTo(CONFIG);
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
