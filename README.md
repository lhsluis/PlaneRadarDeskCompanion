# PlaneRadar Desk Companion

Desk Companion da aplicação [PlaneRadar](https://github.com/lhsluis/PlaneRadar):
um painel dedicado para a placa **Freenove ESP32-S3 Display FNK0104B**, com display
ILI9341 de 2,8", que mostra horário, clima e a aeronave mais próxima.

O Desk Companion não é um servidor independente. Ele consulta a API da aplicação
PlaneRadar pela rede Wi-Fi. Portanto, o PlaneRadar precisa estar **em execução,
acessível e disponível na rede** para que o painel consiga consultar:

```text
/api/nearest-aircraft?lat=..&lon=..&dist=..
```

Configure no `param.txt` a URL base do PlaneRadar, sem `/api/...`, usando a chave
`urlbase`.

## Hardware

- Freenove ESP32-S3 Display **FNK0104B**
- Display TFT ILI9341 de 2,8", resolução 240x320
- Cartão Micro SD em modo SDMMC de 4 bits
- Arduino IDE com a placa `ESP32S3 Dev Module`

O código já usa o pinout específico do FNK0104B. Não aplique o pinout de uma
CYD genérica ESP32-2432S028R.

## Bibliotecas e arquivos necessários

### Bibliotecas instaladas na Arduino IDE

Instale estas bibliotecas antes de compilar:

- **LVGL 8.3.x** — não use LVGL 9;
- **LovyanGFX**;
- **ArduinoJson 7.x**;
- **PNGdec**, de Larry Bank;
- **TFT_eSPI v2.5.43**;
- **TFT_eSPI_Setups v1.3**.

As versões `TFT_eSPI v2.5.43` e `TFT_eSPI_Setups v1.3` devem ser as versões
fornecidas pela Freenove para o modelo **FNK0104B**. Não substitua esses arquivos
pelas versões mais recentes do Library Manager e não aceite atualizações
automáticas, pois os setups e configurações de hardware são específicos da placa.

Ao publicar este projeto no Git, os arquivos originais dessas bibliotecas serão
disponibilizados na pasta `FREENOVE`, junto com o manual do fabricante
`Tutorial.pdf`. A referência oficial da placa e dos arquivos da Freenove é:

<https://github.com/Freenove/Freenove_ESP32_S3_Display/tree/main>

### Arquivos incluídos no sketch

- `nanosvg.h`;
- `nanosvgrast.h`;
- `dashboard_types.h`;
- `lv_conf.h`.

O Arduino/ESP32 já fornece `WiFi`, `WiFiClientSecure`, `HTTPClient`, `SD_MMC`,
`SPI` e `time`; essas APIs não precisam ser instaladas separadamente.

## Configuração do LVGL

O sketch inclui `cyd_flight_dashboard/lv_conf.h`, que já habilita GIF e PNG por
padrão. Se a instalação do LVGL exigir uma configuração global, copie os mesmos
valores para `libraries/lv_conf.h`:

```c
#if 1                                   // habilita o arquivo
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0
#define LV_MEM_CUSTOM 1                 // usa o heap do ESP32 (PNG precisa de RAM)
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())
#define LV_USE_PNG 1                    // decodifica o logo da companhia
#define LV_USE_GIF 1                   // animação radar.gif durante a busca
#define LV_IMG_CACHE_DEF_SIZE 1
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_48 1
```

## Placa (Arduino IDE)
- ESP32S3 Dev Module; se a sua CYD-S3 tem PSRAM, habilite (OPI/QSPI conforme o módulo).
- Partition Scheme com app maior (ex.: "Huge APP" ou 16MB), pois WiFi+TLS+LVGL passam de 1,3 MB.

## Pinos e orientação

O sketch está configurado para a Freenove FNK0104B. O bloco `PINOS` no topo do
`.ino` contém os pinos do TFT, backlight e SDMMC. A orientação atual é definida por:

```cpp
tft.setRotation(1);
```

Para a orientação paisagem invertida, use `tft.setRotation(3)`.
Se as cores estiverem trocadas, ajuste `PANEL_INVERT` e `PANEL_RGB_ORDER`.

## Cartão SD
Copie `param.txt` e `radar.gif` para a raiz do cartão (FAT32). O `radar.gif` é exibido
enquanto a busca não encontra aeronaves. Use a URL **base** em `urlbase` (sem `/api/...`); o código monta
`/api/nearest-aircraft?lat=..&lon=..&dist=..`. Chaves aceitas: `lat lon dist wfname wfpass wheaterpooling flighpooling urlbase tz sleeptimer wakeuptimer`.

`sleeptimer` e `wakeuptimer` definem o período de descanso no formato 24 horas (`HH:MM`).
Durante esse período, a consulta de voos é suspensa e o backlight é reduzido ao mínimo.
O período pode atravessar a meia-noite, por exemplo:

```ini
sleeptimer=22:30
wakeuptimer=08:30
```

O clima continua sendo atualizado normalmente. Para desativar o modo de descanso,
remova as duas chaves ou use valores inválidos.

### Configurar o horário local

O relógio é sincronizado pela internet usando NTP. O fuso horário é definido pela chave `tz` no
`param.txt`; ele não é calculado automaticamente a partir de `lat` e `lon`.

Depois de alterar `tz`, reinicie o ESP32 para que a configuração seja carregada. O formato usado é
o formato POSIX de timezone. Atenção: nesse formato, o número normalmente tem o sinal invertido
em relação ao UTC. Por exemplo, São Paulo é UTC-3 e deve ser configurado como `3`:

```ini
tz=<-03>3
```

Exemplos para algumas cidades:

```ini
# Lisboa / Porto - Portugal (UTC e horário de verão)
tz=WET0WEST,M3.5.0/1,M10.5.0/2

# São Paulo / Brasília / Rio de Janeiro - Brasil (UTC-3, sem horário de verão)
tz=<-03>3

# Nova York - Estados Unidos (UTC-5 e horário de verão)
tz=EST5EDT,M3.2.0/2,M11.1.0/2

# Londres - Reino Unido (UTC e horário de verão)
tz=GMT0BST,M3.5.0/1,M10.5.0/2

# Tóquio - Japão (UTC+9, sem horário de verão)
tz=JST-9

# Sydney - Austrália (UTC+10 e horário de verão)
tz=AEST-10AEDT,M10.1.0/2,M4.1.0/3
```

Use a regra de horário de verão correspondente ao país. As coordenadas `lat` e `lon` continuam
sendo usadas para clima e busca de aeronaves, mas não alteram o fuso configurado.

## Acentos (Á, ã, ç…)
As fontes embutidas do LVGL só têm ASCII; por padrão o código remove os acentos ("Sábado" -> "Sabado").
Para acentos de verdade: gere 5 fontes (10, 12, 14, 32, 48 px) com `lv_font_conv` incluindo
`--range 0x20-0x7F,0xA0-0xFF` (+ `--bpp 4 --format lvgl`), adicione os `.c` à pasta do sketch,
nomeie `font_ui_10` ... `font_ui_48` e ponha `USE_CUSTOM_FONTS 1`. O fonte do Figma (Manrope) pode ser usada aqui.

## Como funciona
- Task de rede (core 0) busca voo a cada `flighpooling` s e clima (Open-Meteo, sem chave) a cada `wheaterpooling` s.
- UI (core 1) só aplica os dados prontos — o LVGL nunca é tocado pela task de rede.
- Logo PNG: só baixa quando a companhia muda; decodificado e reduzido no próprio ESP32 com PNGdec.
- Silhueta SVG: rasterizada no próprio ESP32 com nanosvg para o tamanho configurado em `ICON_PX`, pintada na cor de destaque.
- HTTPS usa `setInsecure()` (sem validar certificado).
