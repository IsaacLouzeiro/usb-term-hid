// ============================================================
//  ESP32-S3 - USB HID study with ST7789 TFT (horizontal)
//  Scripts stored in files (LittleFS) + menu driven by BOOT
// ------------------------------------------------------------
//  The board acts as a USB KEYBOARD over the native USB port.
//  Script files (.txt) live in the internal flash (LittleFS).
//  On the first boot it creates a couple of example files, so
//  it works without any upload tool.
//
//  Navigation with the single BOOT button (GPIO0):
//     MENU screen     click = next file   double = previous
//                     hold  = open the selected file
//     PREVIEW screen  click = back to menu
//                     hold  = send (type the script)
//
//  Script commands (one per line):
//     STRING <text> / ENTER / TAB / DELAY <ms> / GUI <key> / #comment
//
//  Arduino IDE options (Tools):
//     USB Mode        = "USB-OTG (TinyUSB)"
//     USB CDC On Boot = "Enabled"
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
#define BOTAO 0
#define MARGEM_X 12
#define MARGEM_Y 12

// ---------- Button gesture timing ----------
#define LONG_MS   700   // hold time for a "long press"
#define DOUBLE_MS 350   // max gap to count a double click
#define DEBOUNCE  25

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
USBHIDKeyboard teclado;

// ---------- Terminal theme colors (set in setup) ----------
uint16_t COR_FUNDO, COR_VERDE, COR_VERDE2, COR_BORDA, COR_AMBAR, COR_CIANO;

int enviados = 0;

// ---------- Files ----------
#define MAX_ARQ 16
String arquivos[MAX_ARQ];
int totalArq = 0;
int selecionado = 0;
String scriptAtual = "";

// ---------- Screen state ----------
enum Tela { MENU, PREVIEW };
Tela estado = MENU;

// ---------- Preview pagination ----------
#define MAX_LINHAS 48
String linhas[MAX_LINHAS];
int totalLinhas = 0;
int totalPaginas = 1;
int paginaAtual = 0;
const int LINHAS_POR_PAGINA = 3;
const unsigned long INTERVALO_PAGINA = 3000;
unsigned long ultimaTroca = 0;

// ============================================================
//  FILES (LittleFS)
// ============================================================

void escreverArquivo(const char* path, const char* conteudo) {
  File f = LittleFS.open(path, "w");
  if (f) { f.print(conteudo); f.close(); }
}

void seedDefaults() {
  escreverArquivo("/payload1.txt",
    "DELAY 500\n"
    "STRING Hello from file 1! A longer demo text to show the pagination feature on the ST7789 screen.\n"
    "ENTER\n"
    "STRING Loaded from LittleFS.\n");
  escreverArquivo("/payload2.txt",
    "DELAY 300\n"
    "STRING Second script, loaded from its own file.\n"
    "ENTER\n");
}

void listarArquivos() {
  totalArq = 0;
  File root = LittleFS.open("/");
  if (!root) return;
  File file = root.openNextFile();
  while (file && totalArq < MAX_ARQ) {
    String n = String(file.name());
    int bar = n.lastIndexOf('/');
    if (bar >= 0) n = n.substring(bar + 1);
    if (n.endsWith(".txt")) arquivos[totalArq++] = n;
    file = root.openNextFile();
  }
}

// Display name without the ".txt" extension
String nomeExibicao(int i) {
  String n = arquivos[i];
  if (n.endsWith(".txt")) n = n.substring(0, n.length() - 4);
  return n;
}

// ============================================================
//  SCRIPT PARSING / PREVIEW
// ============================================================

String montarPreview(const char* texto) {
  String preview = "", linha = "";
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
    } else linha += c;
  }
  return preview;
}

void quebrarEmLinhas(const String &txt, int maxChars) {
  String word = "", line = "";
  totalLinhas = 0;
  for (unsigned int i = 0; i <= txt.length(); i++) {
    char c = (i < txt.length()) ? txt[i] : ' ';
    if (c == ' ') {
      if (line.length() == 0) line = word;
      else if (line.length() + 1 + word.length() <= (unsigned)maxChars) line += " " + word;
      else { if (totalLinhas < MAX_LINHAS) linhas[totalLinhas++] = line; line = word; }
      word = "";
    } else word += c;
  }
  if (line.length() > 0 && totalLinhas < MAX_LINHAS) linhas[totalLinhas++] = line;
  totalPaginas = (totalLinhas + LINHAS_POR_PAGINA - 1) / LINHAS_POR_PAGINA;
  if (totalPaginas < 1) totalPaginas = 1;
}

void abrirArquivo(int i) {
  scriptAtual = "";
  File f = LittleFS.open("/" + arquivos[i], "r");
  if (f) { while (f.available()) scriptAtual += (char)f.read(); f.close(); }
  quebrarEmLinhas(montarPreview(scriptAtual.c_str()), 24);
  paginaAtual = 0;
}

void executarLinha(String linha) {
  linha.trim();
  if (linha.length() == 0 || linha.startsWith("#")) return;
  int esp = linha.indexOf(' ');
  String cmd = (esp == -1) ? linha : linha.substring(0, esp);
  String arg = (esp == -1) ? ""    : linha.substring(esp + 1);
  cmd.toUpperCase();
  if (cmd == "STRING")      teclado.print(arg);
  else if (cmd == "ENTER")  teclado.write(KEY_RETURN);
  else if (cmd == "TAB")    teclado.write(KEY_TAB);
  else if (cmd == "DELAY")  delay(arg.toInt());
  else if (cmd == "GUI") {
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
//  BUTTON GESTURES (single / double / long) - non-blocking
//  returns: 0 none, 1 click, 2 double, 3 long
// ============================================================

int lerBotao() {
  static bool pressed = false;
  static unsigned long tDown = 0, tUp = 0;
  static int clicks = 0;
  static bool longFired = false;
  unsigned long now = millis();
  bool nivel = (digitalRead(BOTAO) == LOW);   // true = pressed

  if (nivel && !pressed) { pressed = true; tDown = now; longFired = false; }

  if (nivel && pressed && !longFired && (now - tDown >= LONG_MS)) {
    longFired = true; clicks = 0;
    return 3;
  }

  if (!nivel && pressed) {
    pressed = false; tUp = now;
    if (!longFired && (now - tDown >= DEBOUNCE)) clicks++;
  }

  if (!nivel && clicks > 0 && (now - tUp >= DOUBLE_MS)) {
    int c = clicks; clicks = 0;
    return (c >= 2) ? 2 : 1;
  }
  return 0;
}

// ============================================================
//  DISPLAY
// ============================================================

void desenharMenu() {
  tft.fillScreen(COR_FUNDO);
  tft.setFont();
  tft.drawRect(4, 4, 312, 162, COR_BORDA);

  tft.setTextSize(1);
  tft.setTextColor(COR_VERDE);
  tft.setCursor(MARGEM_X, MARGEM_Y);
  tft.print("esp32@hid:~$ ls");

  tft.setTextSize(2);
  int y = 34;
  for (int i = 0; i < totalArq && i < 4; i++) {
    if (i == selecionado) {
      tft.fillRect(8, y - 2, 304, 20, COR_BORDA);
      tft.setTextColor(COR_VERDE);
    } else {
      tft.setTextColor(COR_VERDE2);
    }
    tft.setCursor(MARGEM_X, y);
    tft.print(i == selecionado ? "> " : "  ");
    tft.print(nomeExibicao(i));
    y += 22;
  }

  tft.drawFastHLine(MARGEM_X, 120, 320 - 2 * MARGEM_X, COR_BORDA);
  tft.setTextSize(1);
  tft.setTextColor(COR_VERDE);
  tft.setCursor(MARGEM_X, 126);
  tft.print("click:next  2x:prev  hold:open");
}

void desenharPagina(int p) {
  tft.fillRect(6, 26, 308, 78, COR_FUNDO);

  tft.setFont();
  tft.setTextSize(1);
  if (totalPaginas > 1) {
    tft.fillRect(246, 10, 66, 10, COR_FUNDO);
    tft.setTextColor(COR_VERDE);
    char pg[12];
    snprintf(pg, sizeof(pg), "pg %d/%d", p + 1, totalPaginas);
    tft.setCursor(250, MARGEM_Y);
    tft.print(pg);
  }

  tft.setFont(&FreeMono9pt7b);
  tft.setTextColor(COR_VERDE2);
  int y = 42;
  int start = p * LINHAS_POR_PAGINA;
  int drawn = 0;
  for (int i = start; i < totalLinhas && drawn < LINHAS_POR_PAGINA; i++, drawn++) {
    tft.setCursor(MARGEM_X, y);
    tft.print(linhas[i]);
    y += 18;
  }
  tft.setFont();
  if (p == totalPaginas - 1) tft.fillRect(MARGEM_X, y - 14, 11, 15, COR_VERDE);
}

void desenharEstado(const char* texto, uint16_t cor) {
  tft.fillRect(6, 136, 304, 26, COR_FUNDO);
  tft.setFont();
  tft.setTextSize(2);
  tft.setTextColor(cor);
  tft.setCursor(MARGEM_X, 140);
  tft.print(texto);

  tft.setTextSize(1);
  tft.setTextColor(COR_VERDE2);
  char buf[16];
  snprintf(buf, sizeof(buf), "sent:%d", enviados);
  tft.setCursor(250, 146);
  tft.print(buf);
}

void desenharPreview() {
  tft.fillScreen(COR_FUNDO);
  tft.setFont();
  tft.drawRect(4, 4, 312, 162, COR_BORDA);

  tft.setTextSize(1);
  tft.setTextColor(COR_VERDE);
  tft.setCursor(MARGEM_X, MARGEM_Y);
  tft.print("cat ");
  tft.print(nomeExibicao(selecionado));

  tft.drawFastHLine(MARGEM_X, 108, 320 - 2 * MARGEM_X, COR_BORDA);
  tft.setTextColor(COR_VERDE);
  tft.setCursor(MARGEM_X, 114);
  tft.print("click:back  hold:send");

  desenharPagina(paginaAtual);
  desenharEstado("[ READY ]", COR_VERDE);
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
  tft.setRotation(1);

  COR_FUNDO  = ST77XX_BLACK;
  COR_VERDE  = tft.color565(61, 255, 106);
  COR_VERDE2 = tft.color565(140, 255, 169);
  COR_BORDA  = tft.color565(26, 90, 40);
  COR_AMBAR  = tft.color565(255, 210, 61);
  COR_CIANO  = tft.color565(77, 240, 255);

  LittleFS.begin(true);              // mount, format on first use
  listarArquivos();
  if (totalArq == 0) { seedDefaults(); listarArquivos(); }

  estado = MENU;
  selecionado = 0;
  desenharMenu();

  teclado.begin();
  USB.begin();
}

void loop() {
  int gesto = lerBotao();

  if (estado == MENU) {
    if (totalArq == 0) return;
    if (gesto == 1) { selecionado = (selecionado + 1) % totalArq; desenharMenu(); }
    else if (gesto == 2) { selecionado = (selecionado - 1 + totalArq) % totalArq; desenharMenu(); }
    else if (gesto == 3) {
      abrirArquivo(selecionado);
      estado = PREVIEW;
      ultimaTroca = millis();
      desenharPreview();
    }
  } else {  // PREVIEW
    if (totalPaginas > 1 && millis() - ultimaTroca >= INTERVALO_PAGINA) {
      ultimaTroca = millis();
      paginaAtual = (paginaAtual + 1) % totalPaginas;
      desenharPagina(paginaAtual);
    }
    if (gesto == 1 || gesto == 2) {
      estado = MENU;
      desenharMenu();
    } else if (gesto == 3) {
      desenharEstado("[ SENDING ]", COR_AMBAR);
      rodarScript(scriptAtual.c_str());
      enviados++;
      desenharEstado("[ SENT ]", COR_CIANO);
      delay(1200);
      desenharEstado("[ READY ]", COR_VERDE);
    }
  }
}
