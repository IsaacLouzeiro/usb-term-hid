// ============================================================
//  ESP32-S3 - USB HID study with ST7789 TFT (VERTICAL layout)
// ------------------------------------------------------------
//  Same project as esp32-usb-hid, but with the display in
//  PORTRAIT orientation (170 wide x 320 tall). Terminal theme,
//  English UI. Press BOOT (GPIO0) to "type" the script.
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
// ============================================================

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Fonts/FreeMono9pt7b.h>
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

// ---------- Margins ----------
#define MARGEM_X 12
#define MARGEM_Y 12

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
USBHIDKeyboard teclado;

// ---------- Terminal theme colors (set in setup) ----------
uint16_t COR_FUNDO;
uint16_t COR_VERDE;
uint16_t COR_VERDE2;
uint16_t COR_BORDA;
uint16_t COR_AMBAR;
uint16_t COR_CIANO;

int enviados = 0;

// ---- Preview pagination ----
#define MAX_LINHAS 48
String linhas[MAX_LINHAS];
int totalLinhas = 0;
int totalPaginas = 1;
int paginaAtual = 0;
const int LINHAS_POR_PAGINA = 10;              // the portrait area fits more lines
const unsigned long INTERVALO_PAGINA = 3000;
unsigned long ultimaTroca = 0;

const char* script =
  "DELAY 500\n"
  "STRING Hello, this text was typed by the ESP32-S3!\n"
  "ENTER\n"
  "STRING USB HID study complete.\n";

// ============================================================
//  DISPLAY
// ============================================================

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

// Word-wrap the preview into the global linhas[] array and count pages
void quebrarEmLinhas(const String &txt, int maxChars) {
  String word = "", line = "";
  totalLinhas = 0;
  for (unsigned int i = 0; i <= txt.length(); i++) {
    char c = (i < txt.length()) ? txt[i] : ' ';
    if (c == ' ') {
      if (line.length() == 0) {
        line = word;
      } else if (line.length() + 1 + word.length() <= (unsigned)maxChars) {
        line += " " + word;
      } else {
        if (totalLinhas < MAX_LINHAS) linhas[totalLinhas++] = line;
        line = word;
      }
      word = "";
    } else {
      word += c;
    }
  }
  if (line.length() > 0 && totalLinhas < MAX_LINHAS) linhas[totalLinhas++] = line;
  totalPaginas = (totalLinhas + LINHAS_POR_PAGINA - 1) / LINHAS_POR_PAGINA;
  if (totalPaginas < 1) totalPaginas = 1;
}

// Draw one page of the preview in the payload area
void desenharPagina(int p) {
  tft.fillRect(6, 26, 158, 222, COR_FUNDO);  // clear only the payload area

  // Page indicator (only when there is more than one page)
  tft.setFont();
  tft.setTextSize(1);
  if (totalPaginas > 1) {
    tft.fillRect(116, 252, 48, 10, COR_FUNDO);
    tft.setTextColor(COR_VERDE);
    char pg[12];
    snprintf(pg, sizeof(pg), "pg %d/%d", p + 1, totalPaginas);
    tft.setCursor(116, 258);
    tft.print(pg);
  }

  // Lines of this page (FreeMono)
  tft.setFont(&FreeMono9pt7b);
  tft.setTextColor(COR_VERDE2);
  int y = 44;
  int start = p * LINHAS_POR_PAGINA;
  int drawn = 0;
  for (int i = start; i < totalLinhas && drawn < LINHAS_POR_PAGINA; i++, drawn++) {
    tft.setCursor(MARGEM_X, y);
    tft.print(linhas[i]);
    y += 18;
  }
  tft.setFont();

  // Cursor block only on the last page
  if (p == totalPaginas - 1) tft.fillRect(MARGEM_X, y - 14, 11, 15, COR_VERDE);
}

void desenharBase() {
  tft.fillScreen(COR_FUNDO);
  tft.setFont();

  // Frame (portrait 170x320)
  tft.drawRect(4, 4, 162, 312, COR_BORDA);

  // Header / prompt (counter is drawn by desenharEstado, top-right)
  tft.setTextSize(1);
  tft.setTextColor(COR_VERDE);
  tft.setCursor(MARGEM_X, MARGEM_Y);
  tft.print("esp32@hid:~$");

  // Divider + BOOT hint
  tft.drawFastHLine(MARGEM_X, 250, 170 - 2 * MARGEM_X, COR_BORDA);
  tft.setTextColor(COR_VERDE);
  tft.setCursor(MARGEM_X, 258);
  tft.print("press [BOOT]");
  tft.setCursor(MARGEM_X, 270);
  tft.print("to send");

  // First page of the payload
  desenharPagina(paginaAtual);
}

void desenharEstado(const char* texto, uint16_t cor) {
  // Status text (bottom)
  tft.fillRect(6, 286, 158, 26, COR_FUNDO);  // clear status strip only
  tft.setTextSize(2);
  tft.setTextColor(cor);
  tft.setCursor(MARGEM_X, 290);
  tft.print(texto);

  // Send counter (top-right, away from the status text)
  tft.fillRect(104, 10, 58, 10, COR_FUNDO);
  tft.setTextSize(1);
  tft.setTextColor(COR_VERDE2);
  char buf[16];
  snprintf(buf, sizeof(buf), "sent:%d", enviados);
  tft.setCursor(104, MARGEM_Y);
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
  tft.setRotation(0);   // portrait 170x320

  COR_FUNDO  = ST77XX_BLACK;
  COR_VERDE  = tft.color565(61, 255, 106);
  COR_VERDE2 = tft.color565(140, 255, 169);
  COR_BORDA  = tft.color565(26, 90, 40);
  COR_AMBAR  = tft.color565(255, 210, 61);
  COR_CIANO  = tft.color565(77, 240, 255);

  quebrarEmLinhas(montarPreview(script), 13);  // 13 chars per line (narrow)
  ultimaTroca = millis();

  desenharBase();
  desenharEstado("[ READY ]", COR_VERDE);

  teclado.begin();
  USB.begin();
}

void loop() {
  // Auto-advance the preview pages on a timer (only if text overflows)
  if (totalPaginas > 1 && millis() - ultimaTroca >= INTERVALO_PAGINA) {
    ultimaTroca = millis();
    paginaAtual = (paginaAtual + 1) % totalPaginas;
    desenharPagina(paginaAtual);
  }

  if (digitalRead(BOTAO) == LOW) {
    delay(50);  // debounce

    desenharEstado("[ SEND ]", COR_AMBAR);
    rodarScript(script);
    enviados++;

    desenharEstado("[ SENT ]", COR_CIANO);
    delay(1200);
    desenharEstado("[ READY ]", COR_VERDE);
  }
}
