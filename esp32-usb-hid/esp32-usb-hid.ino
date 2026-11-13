// ============================================================
//  ESP32-S3 - Estudo de USB HID com tela TFT ST7789
// ------------------------------------------------------------
//  A placa se conecta ao PC pela porta USB NATIVA e se passa
//  por um TECLADO. Ao apertar o botao BOOT (GPIO0) ela "digita"
//  o script abaixo. A tela ST7789 mostra o texto que sera
//  enviado e o estado atual (pronto / enviando / enviado).
//
//  Comandos do script (um por linha):
//     STRING <texto>   digita o texto
//     ENTER            tecla Enter
//     TAB              tecla Tab
//     DELAY <ms>       espera X milissegundos
//     GUI <tecla>      tecla Windows/Cmd + tecla   (ex: GUI r)
//     #comentario      linha ignorada
//
//  Opcoes no Arduino IDE (Tools):
//     USB Mode        = "USB-OTG (TinyUSB)"
//     USB CDC On Boot = "Enabled"
// ============================================================

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include "USB.h"
#include "USBHIDKeyboard.h"

// ---------- Pinos da tela ----------
#define TFT_SCLK 12
#define TFT_MOSI 11
#define TFT_CS   10
#define TFT_DC    9
#define TFT_RST   8
#define TFT_BLK   7

// ---------- Botao ----------
#define BOTAO 0   // botao BOOT

// ---------- Cores do tema ----------
#define COR_FUNDO    0x0841   // cinza bem escuro
#define COR_BARRA    0x04DF   // azul
#define COR_CAIXA    0x18E3   // cinza escuro
#define COR_BORDA    0x3186   // cinza medio
#define COR_TEXTO    ST77XX_WHITE
#define COR_LABEL    0x8410   // cinza claro
#define COR_PRONTO   0x05E0   // verde
#define COR_ENVIANDO 0xFD20   // laranja
#define COR_FEITO    0x04DF   // azul

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
USBHIDKeyboard teclado;

int enviados = 0;  // contador de envios

// ---- O "arquivo" de comandos (por enquanto embutido no codigo) ----
const char* script =
  "DELAY 500\n"
  "STRING Ola, este texto foi digitado pelo ESP32-S3!\n"
  "ENTER\n"
  "STRING Estudo de USB HID concluido.\n";

// ============================================================
//  TELA
// ============================================================

// Monta o texto de previa juntando as linhas STRING do script
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

  // Barra de titulo
  tft.fillRect(0, 0, 320, 30, COR_BARRA);
  tft.setTextColor(COR_TEXTO);
  tft.setTextSize(2);
  tft.setCursor(10, 7);
  tft.print("USB HID  ESP32-S3");

  // Rotulo da caixa
  tft.setTextSize(1);
  tft.setTextColor(COR_LABEL);
  tft.setCursor(10, 40);
  tft.print("TEXTO A ENVIAR:");

  // Caixa com o texto de previa
  tft.fillRoundRect(8, 52, 304, 60, 6, COR_CAIXA);
  tft.drawRoundRect(8, 52, 304, 60, 6, COR_BORDA);

  tft.setTextColor(COR_TEXTO);
  tft.setTextSize(1);
  tft.setTextWrap(true);
  tft.setCursor(16, 60);
  tft.print(montarPreview(script));
  tft.setTextWrap(false);
}

// Atualiza so a barra de estado (parte de baixo)
void desenharEstado(const char* texto, uint16_t cor) {
  tft.fillRoundRect(8, 122, 304, 40, 6, cor);

  // texto centralizado
  tft.setTextSize(2);
  int16_t x1, y1; uint16_t w, h;
  tft.getTextBounds(texto, 0, 0, &x1, &y1, &w, &h);
  tft.setTextColor(ST77XX_BLACK);
  tft.setCursor(160 - w / 2, 133);
  tft.print(texto);

  // contador de envios
  tft.setTextSize(1);
  tft.setTextColor(COR_LABEL);
  char buf[24];
  snprintf(buf, sizeof(buf), "Envios: %d", enviados);
  tft.fillRect(0, 164, 320, 8, COR_FUNDO);
  tft.setCursor(10, 164);
  tft.print(buf);
}

// ============================================================
//  SCRIPT (teclado)
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
  tft.setRotation(1);   // paisagem 320x170

  desenharBase();
  desenharEstado("PRONTO", COR_PRONTO);

  teclado.begin();
  USB.begin();
}

void loop() {
  if (digitalRead(BOTAO) == LOW) {
    delay(50);  // anti-ruido

    desenharEstado("ENVIANDO...", COR_ENVIANDO);
    rodarScript(script);
    enviados++;

    desenharEstado("ENVIADO!", COR_FEITO);
    delay(1200);
    desenharEstado("PRONTO", COR_PRONTO);
  }
}
