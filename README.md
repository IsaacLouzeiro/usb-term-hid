# ESP32-S3 USB HID + Tela ST7789

Projeto de estudo: um **ESP32-S3** que se conecta ao computador pela porta
USB nativa e se passa por um **teclado** (USB HID). Os scripts ficam em
**arquivos** na memória interna (LittleFS) e são escolhidos por um **menu na
tela**, navegado só pelo botão **BOOT**. Uma tela **TFT ST7789 de 1.9"** mostra
o menu, a prévia do texto (com paginação automática) e o estado atual.

> ⚠️ **Uso educacional.** Teste apenas em computadores seus ou com autorização.
> A emulação de teclado via USB deve ser usada de forma responsável.

## Hardware

- ESP32-S3 (com USB nativo)
- Tela TFT ST7789 1.9" — 170×320, interface SPI

### Ligação

| Tela | ESP32-S3 | Função          |
|------|----------|-----------------|
| VCC  | 3V3      | Alimentação     |
| GND  | GND      | Terra           |
| SCL  | GPIO 12  | Clock SPI (SCK) |
| SDA  | GPIO 11  | Dados SPI (MOSI)|
| CS   | GPIO 10  | Chip Select     |
| DC   | GPIO 9   | Dado/Comando    |
| RES  | GPIO 8   | Reset           |
| BLK  | GPIO 7   | Luz de fundo    |

O botão de disparo é o **BOOT** da própria placa (GPIO 0).

## Software

### Dependências

- [ESP32 Arduino core](https://github.com/espressif/arduino-esp32) (3.x)
- Adafruit ST7735 and ST7789 Library
- Adafruit GFX Library

### Configuração no Arduino IDE (menu Tools)

- **Board:** ESP32S3 Dev Module
- **USB Mode:** `USB-OTG (TinyUSB)`
- **USB CDC On Boot:** `Enabled`

Grave pela porta UART e conecte o cabo também na porta USB nativa, que é por
onde a placa vira teclado.

## Scripts em arquivos (LittleFS)

Os scripts ficam em arquivos `.txt` na memória interna da placa. No **primeiro
boot**, se não houver nenhum, a placa cria dois exemplos (`payload1.txt` e
`payload2.txt`) automaticamente — então funciona sem ferramenta de upload.

Para usar seus próprios scripts, coloque arquivos `.txt` na raiz do LittleFS
(pela ferramenta "LittleFS Data Upload" ou, mais pra frente, por Wi-Fi).

Cada arquivo tem um comando por linha:

| Comando        | O que faz                          |
|----------------|------------------------------------|
| `STRING texto` | digita o texto                     |
| `ENTER`        | tecla Enter                        |
| `TAB`          | tecla Tab                          |
| `DELAY 500`    | espera 500 ms                      |
| `GUI r`        | tecla Windows/Cmd + r              |
| `# ...`        | comentário (ignorado)              |

## Navegação pelo botão BOOT

Um único botão controla tudo, pelo tempo e número de toques:

| Tela      | Clique        | Duplo clique   | Segurar (~0,7s) |
|-----------|---------------|----------------|-----------------|
| Menu      | próximo arquivo | arquivo anterior | abrir o selecionado |
| Prévia    | voltar ao menu  | voltar ao menu   | enviar (digitar) |

## Layouts disponíveis

O projeto tem duas versões do mesmo visual (tema terminal), escolha a que
preferir:

- `esp32-usb-hid/` — tela na **horizontal** (320×170)
- `esp32-usb-hid-vertical/` — tela na **vertical** (170×320)

## Como usar

1. Abra `esp32-usb-hid/esp32-usb-hid.ino` (ou a pasta `-vertical`) no Arduino IDE.
2. Ajuste as opções de `Tools` acima e grave na placa.
3. No menu da tela, use o **BOOT** para escolher o arquivo (clique/duplo) e
   segure para abrir.
4. Na prévia, clique num editor de texto no PC e **segure o BOOT** para enviar.

## Próximos passos

- [x] Carregar scripts de arquivos na memória interna (LittleFS)
- [x] Menu na tela navegado pelo botão BOOT
- [ ] Enviar arquivos pelo Wi-Fi via página web
- [ ] Modo pen drive (USB MSC) para arrastar scripts
