# PlaneRadar Desk Companion

Desk Companion da aplicação [PlaneRadar](https://github.com/lhsluis/PlaneRadar):
um painel dedicado para a placa **Freenove ESP32-S3 Display FNK0104B**, com display
ILI9341 de 2,8", que mostra horário, clima e a aeronave mais próxima.

O Desk Companion não é um servidor independente. Ele consulta a API da aplicação
PlaneRadar pela rede Wi-Fi. Portanto, o PlaneRadar precisa estar **em execução,
acessível e disponível na rede** para que o painel consiga consultar:

| Aeronave mais Proxima  | Buscando               |
| ---------------------- | ---------------------- |
|<img width="801" height="511" alt="Aeronave mais Proxima" src="https://github.com/user-attachments/assets/9fe1f316-cd41-4b67-aa5e-2d1612b6b4c4" />|<img width="903" height="545" alt="Buscando" src="https://github.com/user-attachments/assets/7cf917cb-9d08-4844-be53-d37a7909425c" />|


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

O `cyd_flight_dashboard.ino` foi dividido em headers incluídos por ele, nesta ordem:

- `config.h`: pinos, fontes, display (LovyanGFX), parâmetros do `param.txt` e helpers de texto;
- `images.h`: SVG/PNG para imagem LVGL (nanosvg) e ícones de clima;
- `web.h`: OTA pelo navegador, página de configuração do `param.txt` e debug monitor;
- `net.h`: dados compartilhados, HTTP, busca de voo/clima, rollback OTA, fallback de Wi-Fi e `netTask`;
- `ui.h`: UI (LVGL) e ponte LVGL <-> LovyanGFX;
- `cyd_flight_dashboard.ino`: includes, `setup()` e `loop()`.

Também fazem parte do sketch:

- `dashboard_types.h`;
- `debug_log.h`;
- `debug_page.h`;
- `ota_page.h`;
- `lv_conf.h`;
- `nanosvg.h`;
- `nanosvgrast.h`.

O Arduino/ESP32 já fornece `WiFi`, `WiFiClientSecure`, `HTTPClient`, `SD_MMC`,
`SPI`, `time`, `WebServer`, `Update` e `ESPmDNS`; essas APIs não precisam ser
instaladas separadamente.

## Configuração do LVGL

O sketch inclui `cyd_flight_dashboard/lv_conf.h`, que habilita GIF. O logo e as
silhuetas PNG são processados pela biblioteca **PNGdec**, e não pelo decoder
PNG do LVGL; por isso `LV_USE_PNG` pode permanecer desabilitado. Se a instalação
do LVGL exigir uma configuração global, copie os mesmos valores para
`libraries/lv_conf.h`:

copie `lv_conf.h` para o ../Arduino/libraries/lv_conf.h (Verifique na sua isntalação onde são salvos as libraries baixadas pelo Arduino IDE), ao lado da pasta lvgl e não dentro dela

```c
#if 1                                   // habilita o arquivo
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0
#define LV_MEM_CUSTOM 1                 // usa o heap do ESP32 (PNG precisa de RAM)
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())
#define LV_USE_PNG 0                    // PNG é processado pela biblioteca PNGdec
#define LV_USE_GIF 1                    // animação radar.gif durante a busca
#define LV_IMG_CACHE_DEF_SIZE 1
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_48 1
```

## Placa (Arduino IDE)
- ESP32S3 Dev Module; se a sua CYD-S3 tem PSRAM, habilite (OPI/QSPI conforme o módulo).
- Partition Scheme com suporte a OTA e app maior (por exemplo, uma opção
  `Default ... with OTA` ou equivalente para flash de 16 MB), pois
  WiFi+TLS+LVGL passam de 1,3 MB. **Não use `Huge APP`** se quiser atualizar
  pelo navegador: essa opção não possui partição OTA neste projeto. Depois de
  mudar o esquema, a primeira gravação deve ser feita por USB.
  Partition testada e aprovada
  `Partition Scheme: "16M Flash (3MB APP/9.9MB FATFS)"`
## Pinos e orientação

O sketch está configurado para a Freenove FNK0104B. O bloco `PINOS` no topo do
`.ino` contém os pinos do TFT, backlight e SDMMC. A orientação atual é definida por:

```cpp
tft.setRotation(3);
```

Se a tela ficar invertida, teste `tft.setRotation(1)`.
Se as cores estiverem trocadas, ajuste `PANEL_INVERT` e `PANEL_RGB_ORDER`.

## Cartão SD
Copie `param.txt` e `radar.gif` para a raiz do cartão (FAT32). O `radar.gif` é exibido
enquanto a busca não encontra aeronaves. Use a URL **base** em `urlbase` (sem `/api/...`); o código monta
`/api/nearest-aircraft?lat=..&lon=..&dist=..`. Chaves conhecidas: `lat lon dist wfname wfpass wheaterpooling flighpooling urlbase tz screenbrightness sleeptimer wakeuptimer otauser otapass otahost`.

O parâmetro `screenbrightness` define o brilho normal da tela entre `0` e
`255`. Durante o período de descanso, a tela é desligada: o backlight desce
suavemente (~1,5 s) até apagar e o painel entra em sleep. Ao acordar, o painel é
reativado e o brilho sobe de volta ao valor configurado, também em ~1,5 s. Com
`screenbrightness=0` a tela apenas fica apagada, sem transição.

Parâmetros desconhecidos também podem permanecer no arquivo. A página web os
exibe na seção **Parâmetros adicionais** e permite editá-los ou removê-los sem
perder comentários e chaves que o firmware ainda não utiliza. Parâmetros cujo
nome sugere segredo (`pass`, `senha`, `key`, `token`, etc.) não têm o valor
enviado ao navegador.

`sleeptimer` e `wakeuptimer` definem o período de descanso no formato 24 horas (`HH:MM`).
Durante esse período, a consulta de voos é suspensa e a tela é desligada, com transição suave (ver `screenbrightness`).
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

## Página web de configuração e atualização

Quando `otapass` existe no `param.txt` **e** a placa possui uma partição OTA, o
Desk Companion inicia uma página web acessível por:

```text
http://planeradarcompanion.local/
http://<IP-da-placa>/
```

O endereço IP é informado no Serial. O nome mDNS pode não funcionar em alguns
dispositivos Android ou redes; nesse caso, use o IP. A página é protegida por
HTTP Basic Authentication com as credenciais `otauser` e `otapass` e não usa
criptografia. Use esse recurso somente em uma rede local confiável.

Se `otapass` não existir ou não houver uma partição OTA, a página não é iniciada;
o Serial informa a condição e o restante do firmware continua funcionando
normalmente.

### Configuração pelo navegador

O formulário exibe as variáveis do `param.txt` organizadas nos blocos
**Localização**, **Servidor de voos**, **Wi-Fi**, **Horário e tela**, **Acesso** e
**Parâmetros adicionais**.
As senhas nunca são enviadas ao navegador: a interface apenas informa se existe
uma senha. Deixe um campo de senha vazio para manter o valor atual. Não é
possível apagar uma senha pela página; para deixar a rede ou o acesso OTA sem
senha, edite o `param.txt` diretamente no cartão SD.

Antes de salvar, o aparelho valida latitude/longitude, distância entre 1 e
1000, URL `http`/`https`, intervalos, horários `HH:MM` e nome mDNS contendo
somente letras, números e hífen. Os horários `sleeptimer` e `wakeuptimer` devem
ser informados juntos ou ambos deixados vazios. Quebras de linha são removidas.

Ao salvar, o firmware preserva comentários e chaves desconhecidas, grava
primeiro em `param.tmp`, mantém `param.bak` como backup e restaura o backup se
a troca falhar. Em seguida, reinicia a placa; a tela exibe
`REINICIANDO...`.

### Atualização OTA do firmware

Para atualizar o firmware pelo navegador:

1. No Arduino IDE, use **Sketch > Exportar binário compilado**.
2. Envie na página o arquivo `*.ino.bin` gerado para o sketch.
3. **Não** envie os arquivos que contêm `bootloader`, `partitions` ou `merged`
   no nome.

Durante a gravação, o progresso aparece no título da tela como
`ATUALIZANDO xx%` e as buscas de rede são pausadas.

**Rollback automático:** o firmware novo só é confirmado depois que a placa
conecta ao Wi-Fi e obtém clima ou voo. Se ela reiniciar ou travar antes disso,
ou ficar 3 min conectada sem obter nenhum dado, volta sozinha à versão anterior.
Se o Wi-Fi não conectar, nada é revertido (pode ser rede fora do ar). A proteção
vale para OTAs de firmwares que já contêm este recurso. Depois de alterar o
**Partition Scheme**, a primeira gravação precisa ser feita por USB; as
atualizações seguintes podem usar OTA.

As rotas HTTP disponíveis são:

- `GET /` — página web de configuração e OTA;
- `GET /debug` — monitor de debug com a saída serial em tempo real;
- `GET /debug/log?since=N` — leitura incremental do histórico serial;
- `GET /config.json` — configuração sem os valores das senhas, mais o campo `fw` com a data e hora de compilação do firmware instalado (exibido na seção Firmware da página);
- `POST /config` — valida e salva a configuração;
- `POST /update` — grava o firmware enviado.

O monitor é protegido pelas mesmas credenciais Basic Auth da página OTA e só
fica disponível quando a página OTA é iniciada (senha `otapass` configurada e
partição OTA presente). Ele mostra as mensagens que o sketch escreve via
`Serial`, com tempo desde o boot; não inclui mensagens internas do ESP32 nem
relatórios de crash. O histórico na placa usa um buffer circular de 32 KB com
PSRAM ou 8 KB sem PSRAM e é perdido quando a placa reinicia. A página do
navegador mantém os dados que já recebeu, permite pausar a consulta e baixá-los
como arquivo `.txt`. A captura começa já no `setup()`, logo após o `Serial.begin`,
então o boot e as primeiras consultas (relógio, clima, voos) ficam no histórico.
Mensagens internas do core ESP32 e crashes não entram.

## Acentos (Á, ã, ç…)
As fontes embutidas do LVGL só têm ASCII; por padrão o código remove os acentos ("Sábado" -> "Sabado").
Para acentos de verdade: gere 5 fontes (10, 12, 14, 32, 48 px) com `lv_font_conv` incluindo
`--range 0x20-0x7F,0xA0-0xFF` (+ `--bpp 4 --format lvgl`), adicione os `.c` à pasta do sketch,
nomeie `font_ui_10` ... `font_ui_48` e ponha `USE_CUSTOM_FONTS 1`. O fonte do Figma (Manrope) pode ser usada aqui.

## Como funciona
- Task de rede (core 0) busca voo a cada `flighpooling` s e clima (Open-Meteo, sem chave) a cada `wheaterpooling` s. Em falha, a 1ª nova tentativa é em 30 s (ou no próprio intervalo, se menor) e o intervalo dobra a cada nova falha, até o teto (voo 5 min; clima o intervalo configurado). Nos primeiros 2 min após conectar o Wi-Fi, falha de conexão (sem resposta do servidor) retenta em 5, 10, 20, 40 s... até o backoff normal. Volta ao configurado no sucesso, no descanso e ao reconectar o Wi-Fi.
- Aeronaves com `alt_baro` igual a `ground` são ignoradas; altitude e distância aparecem sob o modelo no formato `500 ft | 5.6 NM`.
- UI (core 1) só aplica os dados prontos — o LVGL nunca é tocado pela task de rede.
- Logo PNG: só baixa quando a companhia muda; decodificado e reduzido no próprio ESP32 com PNGdec.
- Silhueta SVG: rasterizada no próprio ESP32 com nanosvg para o tamanho configurado em `ICON_PX`, pintada na cor de destaque. Silhuetas PNG também são reduzidas pelo PNGdec; bitmaps vazios ou inválidos usam a silhueta genérica.
- `radar.gif` é carregado uma vez em memória, preferencialmente na PSRAM, e o timer da animação é pausado quando o radar não está visível.
- HTTPS usa `setInsecure()` (sem validar certificado).
- Fallback de Wi-Fi: sem conexão por 60 s (desde o boot ou desde a última queda), tenta `wfname`/`wfpass` do `param.bak`, se forem diferentes dos do `param.txt` (nenhum arquivo é alterado). Se ainda não conectar por mais 60 s, sobe o AP `PlaneRadarCompanion-XXXX` (XXXX = 4 últimos caracteres hex do MAC), com senha igual ao `otapass` se tiver 8 caracteres ou mais, senão aberto, e segue tentando o Wi-Fi em segundo plano a cada 60 s (não tenta enquanto há cliente na página). Enquanto o AP está no ar, o título mostra `AP 192.168.4.1` e a página fica em `http://192.168.4.1/`. Ao conectar no Wi-Fi, o AP é desligado e o IP aparece no título; se houver cliente na página, o AP fica mais 60 s antes de desligar. O AP só sobe se `otapass` estiver definido.
- Watchdog: a task de rede tem watchdog de 60 s, alimentado a cada volta, durante as leituras HTTP, a rasterização de SVG e a atualização OTA. Se travar, a placa reinicia (motivo `watchdog de task` no log de boot). O watchdog é único, então o `loop()` também passa a ser vigiado com 60 s (antes 5 s).

## Limitações conhecidas

- O AP de configuração só sobe com `otapass` definido. Sem ele, a página e o OTA
  ficam desligados; e, se o Wi-Fi não conectar, corrija o `param.txt` pelo cartão SD.
- O parse de SVG muito grande pode demorar vários segundos e manter a task de
  rede ocupada; isso só ocorre quando o modelo da aeronave é trocado.
- Logos ou silhuetas PNG fora dos limites do PNGdec são substituídos pelo texto
  do código ICAO da companhia ou pela silhueta genérica, respectivamente.
- Gravar ou escrever na flash pode causar uma pequena engasgada na interface
  durante o OTA.
