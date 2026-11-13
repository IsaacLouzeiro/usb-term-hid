// ============================================================
//  ESP32-S3 - USB HID study with ST7789 TFT (terminal theme)
// ------------------------------------------------------------
//  The board connects to the PC over the NATIVE USB port and
//  acts as a KEYBOARD (USB HID). Pressing the BOOT button
//  (GPIO0) makes it "type" the script below. The ST7789 1.9"
//  display shows the text to be sent and the current state.
//
//  Script commands (one per line):
//     STRING <text>    type the text
//     ENTER            Enter key
//     TAB              Tab key
//     DELAY <ms>       wait X milliseconds
//     GUI <key>        Windows/Cmd + key   (e.g. GUI r)
//     #comment         ignored line
//
//  Arduino IDE options (Tools):
//     USB Mode        = "USB-OTG (TinyUSB)"
//     USB CDC On Boot = "Enabled"
//
//  NOTE: UI text is English for now; a language menu is planned.
// ============================================================

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
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
#define BOTAO 0   // on-board BOOT button

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
USBHIDKeyboard teclado;

// ---------- Terminal theme colors (set in setup) ----------
uint16_t COR_FUNDO;     // background
uint16_t COR_VERDE;     // bright green (prompt / ready)
uint16_t COR_VERDE2;    // soft green (output text)
uint16_t COR_BARRA;     // status bar background
uint16_t COR_AMBAR;     // amber (sending)
uint16_t COR_CIANO;     // cyan (sent)

int enviados = 0;       // send counter

// ---- Command "file" (embedded in code for now) ----
const char* script =
  "DELAY 500\n"
  "STRING Hello, this text was typed by the ESP32-S3!\n"
  "ENTER\n"
  "STRING USB HID study complete.\n";

// ============================================================
//  DISPLAY
// ============================================================

// Build the preview text by joining the STRING lines of the script
String montarPreview(const char* texto) {
  String preview = "";
  String linha = "";
  for (int i = 0; ; i++) {
    char c = texto[i];
    if (c == '\n' || c == '\0') {
      String t = linha; t.trim();
      if (t.startsWith("STRING ")) {
        if (preview.length() > 0) preview += " ";
        preview += t.substring(7);
      }
      linha = "";
      if (c == '\0') break;
    } else {
      linha += c;
    }
  }
  return preview;
}

void desenharBase() {
  tft.fillScreen(COR_FUNDO);

  // Prompt line
  tft.setTextSize(1);
  tft.setTextColor(COR_VERDE);
  tft.setCursor(8, 10);
  tft.print("esp32@hid:~$ send");

  // Output label
  tft.setTextColor(COR_VERDE2);
  tft.setCursor(8, 30);
  tft.print("> payload:");

  // Preview text (wrapped)
  tft.setTextWrap(true);
  tft.setCursor(8, 48);
  tft.print(montarPreview(script));
  tft.setTextWrap(false);

  // Cursor block just below the text
  int cy = tft.getCursorY() + 14;
  if (cy > 120) cy = 120;
  tft.fillRect(8, cy, 9, 12, COR_VERDE);
}

// Redraw only the status bar at the bottom
void desenharEstado(const char* texto, uint16_t cor) {
  tft.fillRect(0, 138, 320, 32, COR_BARRA);

  tft.setTextSize(2);
  tft.setTextColor(cor);
  tft.setCursor(8, 146);
  tft.print(texto);

  // Send counter on the right
  tft.setTextSize(1);
  tft.setTextColor(COR_VERDE2);
  char buf[16];
  snprintf(buf, sizeof(buf), "sent:%d", enviados);
  tft.setCursor(258, 150);
  tft.print(buf);
}

// ============================================================
//  SCRIPT (keyboard)
// ============================================================

void executarLinha(String linha) {
  linha.trim();
  if (linha.length() == 0 || linha.startsWith("#")) return;

  int esp = linha.indexOf(' ');
  String cmd = (esp == -1) ? linha : linha.substring(0, esp);
  String arg = (esp == -1) ? ""    : linha.substring(esp + 1);
  cmd.toUpperCase();

  if (cmd == "STRING") {
    teclado.print(arg);
  } else if (cmd == "ENTER") {
    teclado.write(KEY_RETURN);
  } else if (cmd == "TAB") {
    teclado.write(KEY_TAB);
  } else if (cmd == "DELAY") {
    delay(arg.toInt());
  } else if (cmd == "GUI") {
    teclado.press(KEY_LEFT_GUI);
    if (arg.length() > 0) teclado.press(arg[0]);
    delay(100);
    teclado.releaseAll();
  }
}

void rodarScript(const char* texto) {
  String linha = "";
  for (int i = 0; texto[i] != '\0'; i++) {
    char c = texto[i];
    if (c == '\n') { executarLinha(linha); linha = ""; }
    else            linha += c;
  }
  if (linha.length() > 0) executarLinha(linha);
}

// ============================================================
//  SETUP / LOOP
// ============================================================

void setup() {
  pinMode(BOTAO, INPUT_PULLUP);

  pinMode(TFT_BLK, OUTPUT);
  digitalWrite(TFT_BLK, HIGH);

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.init(170, 320);
  tft.setRotation(1);   // landscape 320x170

  // Build theme colors
  COR_FUNDO  = tft.color565(5, 8, 5);
  COR_VERDE  = tft.color565(61, 255, 106);
  COR_VERDE2 = tft.color565(140, 255, 169);
  COR_BARRA  = tft.color565(11, 22, 12);
  COR_AMBAR  = tft.color565(255, 210, 61);
  COR_CIANO  = tft.color565(77, 240, 255);

  desenharBase();
  desenharEstado("[ READY ]", COR_VERDE);

  teclado.begin();
  USB.begin();
}

void loop() {
  if (digitalRead(BOTAO) == LOW) {
    delay(50);  // debounce

    desenharEstado("[ SENDING ]", COR_AMBAR);
    rodarScript(script);
    enviados++;

    desenharEstado("[ SENT ]", COR_CIANO);
    delay(1200);
    desenharEstado("[ READY ]", COR_VERDE);
  }
}
