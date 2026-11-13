# ESP32-S3 USB HID + Tela ST7789

Projeto de estudo: um **ESP32-S3** que se conecta ao computador pela porta
USB nativa e se passa por um **teclado** (USB HID). Ao apertar o botão **BOOT**,
ele "digita" um script de comandos. Uma tela **TFT ST7789 de 1.9"** mostra o
texto que será enviado e o estado atual (pronto / enviando / enviado).

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

## Comandos do script

O script fica na variável `script` dentro do `.ino`. Um comando por linha:

| Comando        | O que faz                          |
|----------------|------------------------------------|
| `STRING texto` | digita o texto                     |
| `ENTER`        | tecla Enter                        |
| `TAB`          | tecla Tab                          |
| `DELAY 500`    | espera 500 ms                      |
| `GUI r`        | tecla Windows/Cmd + r              |
| `# ...`        | comentário (ignorado)              |

## Layouts disponíveis

O projeto tem duas versões do mesmo visual (tema terminal), escolha a que
preferir:

- `esp32-usb-hid/` — tela na **horizontal** (320×170)
- `esp32-usb-hid-vertical/` — tela na **vertical** (170×320)

## Como usar

1. Abra `esp32-usb-hid/esp32-usb-hid.ino` (ou a pasta `-vertical`) no Arduino IDE.
2. Ajuste as opções de `Tools` acima e grave na placa.
3. Clique num editor de texto no PC para deixar o cursor ativo.
4. Aperte o botão **BOOT**. O texto da tela será digitado.

## Próximos passos

- [ ] Carregar scripts de arquivos na memória interna (LittleFS)
- [ ] Enviar arquivos pelo Wi-Fi via página web
- [ ] Modo pen drive (USB MSC) para arrastar scripts
