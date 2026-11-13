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
//     STRING <text> / ENTER / TAB / DELAY <ms> / GUI <key> / #comment
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
#define BOTAO 0
#define MARGEM_X 12
#define MARGEM_Y 12

// ---------- Button gesture timing ----------
#define LONG_MS   700
#define DOUBLE_MS 350
#define DEBOUNCE  25

#define APP_NOME   "USB HID"
#define APP_AUTOR  "IsaacLouzeiro"
#define APP_VERSAO "v0.5"

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
USBHIDKeyboard teclado;

uint16_t COR_FUNDO, COR_VERDE, COR_VERDE2, COR_BORDA, COR_AMBAR, COR_CIANO;

bool vertical = false;     // false = horizontal, true = vertical
int enviados = 0;

int W() { return vertical ? 170 : 320; }
int H() { return vertical ? 320 : 170; }

// ---------- Files ----------
#define MAX_ARQ 16
String arquivos[MAX_ARQ];
int totalArq = 0;
int arqAtual = 0;
String scriptAtual = "";

// ---------- Menu items (shared buffer) ----------
#define MAX_ITENS (MAX_ARQ + 4)
String itens[MAX_ITENS];
int nItens = 0;
int sel = 0;

// ---------- Screen state ----------
enum Tela { MENU_PRINCIPAL, PAYLOADS, PREVIEW, CONFIG, ABOUT };
Tela estado = MENU_PRINCIPAL;

// ---------- Preview pagination ----------
#define MAX_LINHAS 48
String linhas[MAX_LINHAS];
int totalLinhas = 0;
int totalPaginas = 1;
int paginaAtual = 0;
int linhasPorPagina = 3;
const unsigned long INTERVALO_PAGINA = 3000;
unsigned long ultimaTroca = 0;

// ============================================================
//  CONFIG (LittleFS)
// ============================================================

void salvarConfig() {
  File f = LittleFS.open("/config.txt", "w");
  if (f) { f.print(vertical ? "V" : "H"); f.close(); }
}

void carregarConfig() {
  File f = LittleFS.open("/config.txt", "r");
  if (f) { if (f.available()) vertical = (f.read() == 'V'); f.close(); }
}

void aplicarOrientacao() {
  tft.setRotation(vertical ? 0 : 1);
}

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
    if (n.endsWith(".txt") && n != "config.txt") arquivos[totalArq++] = n;
    file = root.openNextFile();
  }
}

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
  totalPaginas = (totalLinhas + linhasPorPagina - 1) / linhasPorPagina;
  if (totalPaginas < 1) totalPaginas = 1;
}

void abrirArquivo(int i) {
  arqAtual = i;
  scriptAtual = "";
  File f = LittleFS.open("/" + arquivos[i], "r");
  if (f) { while (f.available()) scriptAtual += (char)f.read(); f.close(); }
  linhasPorPagina = vertical ? 10 : 3;
  quebrarEmLinhas(montarPreview(scriptAtual.c_str()), vertical ? 13 : 24);
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
//  BUTTON GESTURES - non-blocking
//  returns: 0 none, 1 click, 2 double, 3 long
// ============================================================

int lerBotao() {
  static bool pressed = false;
  static unsigned long tDown = 0, tUp = 0;
  static int clicks = 0;
  static bool longFired = false;
  unsigned long now = millis();
  bool nivel = (digitalRead(BOTAO) == LOW);

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
//  MENU ITEMS
// ============================================================

void montarItens() {
  nItens = 0;
  if (estado == MENU_PRINCIPAL) {
    itens[nItens++] = "Payloads";
    itens[nItens++] = "Config";
    itens[nItens++] = "About";
  } else if (estado == PAYLOADS) {
    itens[nItens++] = "< voltar";
    for (int i = 0; i < totalArq; i++) itens[nItens++] = nomeExibicao(i);
  } else if (estado == CONFIG) {
    itens[nItens++] = String("Tela: ") + (vertical ? "Vert" : "Horiz");
    itens[nItens++] = "< voltar";
  }
}

// ============================================================
//  DISPLAY
// ============================================================

void rodape(const char* l1, const char* l2) {
  int w = W(), h = H();
  int divY = h - 46;
  tft.drawFastHLine(MARGEM_X, divY, w - 2 * MARGEM_X, COR_BORDA);
  tft.setFont();
  tft.setTextSize(1);
  tft.setTextColor(COR_VERDE);
  tft.setCursor(MARGEM_X, divY + 6);
  tft.print(l1);
  if (l2[0]) { tft.setCursor(MARGEM_X, divY + 18); tft.print(l2); }
}

void desenharLista(const char* titulo) {
  int w = W(), h = H();
  tft.fillScreen(COR_FUNDO);
  tft.setFont();
  tft.drawRect(4, 4, w - 8, h - 8, COR_BORDA);

  tft.setTextSize(1);
  tft.setTextColor(COR_VERDE);
  tft.setCursor(MARGEM_X, MARGEM_Y);
  tft.print(titulo);

  int startY = vertical ? 44 : 36;
  int lineH  = vertical ? 28 : 24;
  int divY   = h - 46;
  int maxVis = (divY - startY) / lineH;
  if (maxVis < 1) maxVis = 1;
  int first = (sel >= maxVis) ? (sel - maxVis + 1) : 0;

  tft.setTextSize(2);
  int y = startY;
  for (int i = first; i < nItens && i < first + maxVis; i++) {
    if (i == sel) {
      tft.fillRect(6, y - 2, w - 12, lineH - 4, COR_BORDA);
      tft.setTextColor(COR_VERDE);
    } else {
      tft.setTextColor(COR_VERDE2);
    }
    tft.setCursor(MARGEM_X, y);
    tft.print(i == sel ? "> " : "  ");
    tft.print(itens[i]);
    y += lineH;
  }

  rodape("click:prox  2x:ant", "hold:selecionar");
}

void desenharAbout() {
  int w = W(), h = H();
  tft.fillScreen(COR_FUNDO);
  tft.setFont();
  tft.drawRect(4, 4, w - 8, h - 8, COR_BORDA);

  tft.setTextSize(1);
  tft.setTextColor(COR_VERDE);
  tft.setCursor(MARGEM_X, MARGEM_Y);
  tft.print("esp32@hid:~$ about");

  tft.setTextSize(2);
  tft.setTextColor(COR_VERDE);
  tft.setCursor(MARGEM_X, vertical ? 56 : 40);
  tft.print(APP_NOME);

  tft.setTextSize(1);
  tft.setTextColor(COR_VERDE2);
  int y = vertical ? 86 : 70;
  tft.setCursor(MARGEM_X, y); tft.print("ESP32-S3 keyboard"); y += 16;
  tft.setCursor(MARGEM_X, y); tft.print("Autor: " APP_AUTOR);  y += 16;
  tft.setCursor(MARGEM_X, y); tft.print("Estudo de USB HID");  y += 16;
  tft.setCursor(MARGEM_X, y); tft.print(APP_VERSAO);

  rodape("click/hold: voltar", "");
}

void desenharPagina(int p) {
  int w = W();
  int plBot = vertical ? 248 : 102;
  tft.fillRect(6, 26, w - 12, plBot - 26, COR_FUNDO);

  tft.setFont();
  tft.setTextSize(1);
  if (totalPaginas > 1) {
    tft.fillRect(w - 74, 10, 66, 10, COR_FUNDO);
    tft.setTextColor(COR_VERDE);
    char pg[12];
    snprintf(pg, sizeof(pg), "pg %d/%d", p + 1, totalPaginas);
    tft.setCursor(w - 74, MARGEM_Y);
    tft.print(pg);
  }

  tft.setFont(&FreeMono9pt7b);
  tft.setTextColor(COR_VERDE2);
  int y = 42;
  int start = p * linhasPorPagina;
  int drawn = 0;
  for (int i = start; i < totalLinhas && drawn < linhasPorPagina; i++, drawn++) {
    tft.setCursor(MARGEM_X, y);
    tft.print(linhas[i]);
    y += 18;
  }
  tft.setFont();
  if (p == totalPaginas - 1) tft.fillRect(MARGEM_X, y - 14, 11, 15, COR_VERDE);
}

void desenharEstado(const char* texto, uint16_t cor) {
  int w = W();
  int sy = vertical ? 290 : 138;
  tft.fillRect(6, sy - 4, w - 12, 26, COR_FUNDO);
  tft.setFont();
  tft.setTextSize(2);
  tft.setTextColor(cor);
  tft.setCursor(MARGEM_X, sy);
  tft.print(texto);

  char buf[16];
  snprintf(buf, sizeof(buf), "sent:%d", enviados);
  tft.setTextSize(1);
  tft.setTextColor(COR_VERDE2);
  if (vertical) {
    tft.fillRect(104, 10, 58, 10, COR_FUNDO);
    tft.setCursor(104, MARGEM_Y);
  } else {
    tft.setCursor(w - 70, sy + 6);
  }
  tft.print(buf);
}

void desenharPreview() {
  int w = W(), h = H();
  tft.fillScreen(COR_FUNDO);
  tft.setFont();
  tft.drawRect(4, 4, w - 8, h - 8, COR_BORDA);

  tft.setTextSize(1);
  tft.setTextColor(COR_VERDE);
  tft.setCursor(MARGEM_X, MARGEM_Y);
  tft.print("cat ");
  tft.print(nomeExibicao(arqAtual));

  rodape("click:voltar", "hold:enviar");
  desenharPagina(paginaAtual);
  desenharEstado("[ READY ]", COR_VERDE);
}

// Draw whatever the current screen is
void redraw() {
  if (estado == MENU_PRINCIPAL || estado == PAYLOADS || estado == CONFIG) montarItens();
  switch (estado) {
    case MENU_PRINCIPAL: desenharLista("esp32@hid:~$ menu");   break;
    case PAYLOADS:       desenharLista("esp32@hid:~$ ls");     break;
    case CONFIG:         desenharLista("esp32@hid:~$ config"); break;
    case ABOUT:          desenharAbout();                      break;
    case PREVIEW:        desenharPreview();                    break;
  }
}

void mudar(int t) {
  estado = (Tela)t;
  sel = 0;
  redraw();
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

  COR_FUNDO  = ST77XX_BLACK;
  COR_VERDE  = tft.color565(61, 255, 106);
  COR_VERDE2 = tft.color565(140, 255, 169);
  COR_BORDA  = tft.color565(26, 90, 40);
  COR_AMBAR  = tft.color565(255, 210, 61);
  COR_CIANO  = tft.color565(77, 240, 255);

  LittleFS.begin(true);
  carregarConfig();
  aplicarOrientacao();
  listarArquivos();
  if (totalArq == 0) { seedDefaults(); listarArquivos(); }

  mudar(MENU_PRINCIPAL);

  teclado.begin();
  USB.begin();
}

void loop() {
  int g = lerBotao();

  switch (estado) {

    case MENU_PRINCIPAL:
      if (g == 1) { sel = (sel + 1) % nItens; redraw(); }
      else if (g == 2) { sel = (sel - 1 + nItens) % nItens; redraw(); }
      else if (g == 3) {
        if (sel == 0) mudar(PAYLOADS);
        else if (sel == 1) mudar(CONFIG);
        else mudar(ABOUT);
      }
      break;

    case PAYLOADS:
      if (totalArq == 0 && nItens <= 1) { /* only back item */ }
      if (g == 1) { sel = (sel + 1) % nItens; redraw(); }
      else if (g == 2) { sel = (sel - 1 + nItens) % nItens; redraw(); }
      else if (g == 3) {
        if (sel == 0) mudar(MENU_PRINCIPAL);          // "< voltar"
        else {
          abrirArquivo(sel - 1);
          estado = PREVIEW;
          ultimaTroca = millis();
          desenharPreview();
        }
      }
      break;

    case CONFIG:
      if (g == 1) { sel = (sel + 1) % nItens; redraw(); }
      else if (g == 2) { sel = (sel - 1 + nItens) % nItens; redraw(); }
      else if (g == 3) {
        if (sel == 0) {                               // toggle orientation
          vertical = !vertical;
          aplicarOrientacao();
          salvarConfig();
          redraw();
        } else {
          mudar(MENU_PRINCIPAL);
        }
      }
      break;

    case ABOUT:
      if (g != 0) mudar(MENU_PRINCIPAL);
      break;

    case PREVIEW:
      if (totalPaginas > 1 && millis() - ultimaTroca >= INTERVALO_PAGINA) {
        ultimaTroca = millis();
        paginaAtual = (paginaAtual + 1) % totalPaginas;
        desenharPagina(paginaAtual);
      }
      if (g == 1 || g == 2) {
        mudar(PAYLOADS);
      } else if (g == 3) {
        desenharEstado("[ SENDING ]", COR_AMBAR);
        rodarScript(scriptAtual.c_str());
        enviados++;
        desenharEstado("[ SENT ]", COR_CIANO);
        delay(1200);
        desenharEstado("[ READY ]", COR_VERDE);
      }
      break;
  }
}
