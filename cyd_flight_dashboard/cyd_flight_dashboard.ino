/*
 * Freenove ESP32-S3 Display 2.8" (ILI9341)  —  Dashboard hora/clima + voo mais próximo
 * LVGL 8.3.x | LovyanGFX | ArduinoJson 7 | nanosvg (headers na pasta do sketch)
 *
 * Configuração via /param.txt no SD (ver README.md)
 */
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <SD_MMC.h>
#include <WebServer.h>      // atualização OTA pelo navegador
#include <Update.h>
#include <ESPmDNS.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_task_wdt.h>    // watchdog da netTask
#if __has_include("esp_arduino_version.h")
#include "esp_arduino_version.h"
#endif
#ifndef ESP_ARDUINO_VERSION_MAJOR
#define ESP_ARDUINO_VERSION_MAJOR 2
#endif
#include <time.h>
#include <ArduinoJson.h>
#define LV_CONF_INCLUDE_SIMPLE
#include <lvgl.h>
#include <LovyanGFX.hpp>
#include <PNGdec.h>          // biblioteca "PNGdec" (Larry Bank)
#include "dashboard_types.h"
#include "ota_page.h"
#include "debug_page.h"
#include "debug_log.h"      // espelha o Serial para a página /debug (deve vir depois dos includes de bibliotecas)

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

// =====================================================================
//  PINOS  (padrão = CYD clássico ESP32-2432S028R)  >>> CONFIRME NA SUA PLACA <<<
// =====================================================================
#define TFT_SCLK_PIN 12
#define TFT_MOSI_PIN 11
#define TFT_MISO_PIN 13
#define TFT_DC_PIN   46
#define TFT_CS_PIN   10
#define TFT_RST_PIN  -1
#define TFT_BL_PIN   45
// SD em modo SDMMC de 4 bits (não é SPI)
#define SD_CLK_PIN   38
#define SD_CMD_PIN   40
#define SD_D0_PIN    39
#define SD_D1_PIN    41
#define SD_D2_PIN    48
#define SD_D3_PIN    47

// Inversão confirmada (preto/branco corretos). Na foto, com RGB_ORDER=true vermelho e azul saíam trocados.
#define PANEL_INVERT    true
#define PANEL_RGB_ORDER false
#define COLOR_TEST      0   // 1 = barras Vermelho/Verde/Azul/Branco por 2,5 s no boot
#define MEM_MIN_FREE    30000   // reinicia se a RAM interna livre ficar abaixo disso por 3 min seguidos
#define MEM_MIN_BLOCK   8192    // ... ou se o maior bloco contínuo ficar abaixo disso (fragmentação)
#define ICON_PX         34  // tamanho (px) do logo e da silhueta dentro dos círculos de 44 px

// =====================================================================
//  FONTES  — 0 = Montserrat embutida (sem acentos, o código remove).
//            1 = fontes próprias geradas com lv_font_conv (ver README).
// =====================================================================
#define USE_CUSTOM_FONTS 0
#if USE_CUSTOM_FONTS
  LV_FONT_DECLARE(font_ui_10) LV_FONT_DECLARE(font_ui_12) LV_FONT_DECLARE(font_ui_14)
  LV_FONT_DECLARE(font_ui_32) LV_FONT_DECLARE(font_ui_48)
  #define F10 (&font_ui_10)
  #define F12 (&font_ui_12)
  #define F14 (&font_ui_14)
  #define F32 (&font_ui_32)
  #define F48 (&font_ui_48)
  #define SEP " \302\267 "          // " · "
#else
  #define F10 (&lv_font_montserrat_10)
  #define F12 (&lv_font_montserrat_12)
  #define F14 (&lv_font_montserrat_14)
  #define F32 (&lv_font_montserrat_32)
  #define F48 (&lv_font_montserrat_48)
  #define SEP " - "
#endif

// Cores
#define C_SCREEN   0x03090D
#define C_CARD_TOP 0x0D2E32
#define C_CARD_BOT 0x061219
#define C_BORDER   0x15343A
#define C_FCARD    0x0A1B24
#define C_ACCENT   0x3DE0B5
#define C_WHITE    0xFFFFFF
#define C_MUTED    0x6F8E96
#define C_ROUTE    0x5F8497
#define C_ORANGE   0xE8A87C

// =====================================================================
//  Display (LovyanGFX)
// =====================================================================
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel;
  lgfx::Bus_SPI       _bus;
  lgfx::Light_PWM     _light;
public:
  LGFX() {
    { auto c = _bus.config();
      c.spi_host = SPI2_HOST; c.spi_mode = 0;
      c.freq_write = 40000000; c.freq_read = 16000000;
      c.spi_3wire = false; c.use_lock = true; c.dma_channel = SPI_DMA_CH_AUTO;
      c.pin_sclk = TFT_SCLK_PIN; c.pin_mosi = TFT_MOSI_PIN;
      c.pin_miso = TFT_MISO_PIN; c.pin_dc = TFT_DC_PIN;
      _bus.config(c); _panel.setBus(&_bus); }
    { auto c = _panel.config();
      c.pin_cs = TFT_CS_PIN; c.pin_rst = TFT_RST_PIN; c.pin_busy = -1;
      c.panel_width = 240; c.panel_height = 320; c.offset_x = 0; c.offset_y = 0;
      c.offset_rotation = 0; c.dummy_read_pixel = 8; c.dummy_read_bits = 1;
      c.readable = false; c.invert = PANEL_INVERT; c.rgb_order = PANEL_RGB_ORDER;
      c.dlen_16bit = false; c.bus_shared = false;
      _panel.config(c); }
    { auto c = _light.config();
      c.pin_bl = TFT_BL_PIN; c.invert = false; c.freq = 44100; c.pwm_channel = 7;
      _light.config(c); _panel.setLight(&_light); }
    setPanel(&_panel);
  }
};
static LGFX tft;

// =====================================================================
//  Parâmetros (param.txt)
// =====================================================================
struct Params {
  float lat = 0, lon = 0; int dist = 50;
  int brightness = 255;                       // screenbrightness (0..255); sem a chave, mantém 255 como antes
  String ssid, pass, urlbase;
  String tz = "WET0WEST,M3.5.0/1,M10.5.0/2";     // Portugal
  String sleepTimer, wakeupTimer;
  String otaUser = "admin", otaPass, otaHost = "cyd-flights";   // OTA só liga se otapass estiver definido
  uint32_t weatherMs = 1800000UL, flightMs = 15000UL;
} P;

static bool parseClock(const String& value, uint16_t& minutes) {
  if (value.length() != 5 || value[2] != ':') return false;
  for (int i : {0, 1, 3, 4}) {
    if (value[i] < '0' || value[i] > '9') return false;
  }
  int hour = value.substring(0, 2).toInt();
  int minute = value.substring(3, 5).toInt();
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59) return false;
  minutes = (uint16_t)(hour * 60 + minute);
  return true;
}

static String cleanUrl(String v) {
  int a = v.indexOf("](");                 // tolera [texto](url) colado do markdown
  if (a >= 0) { int b = v.lastIndexOf(')'); v = v.substring(a + 2, b > a ? b : v.length()); }
  v.replace("[", ""); v.replace("]", ""); v.trim();
  int p = v.indexOf("/api/"); if (p > 0) v = v.substring(0, p);   // só a base
  while (v.endsWith("/")) v.remove(v.length() - 1);
  return v;
}

static bool loadParams() {
  File f = SD_MMC.open("/param.txt");
  if (!f) return false;
  while (f.available()) {
    String line = f.readStringUntil('\n'); line.trim();
    if (!line.length() || line[0] == '#') continue;
    int eq = line.indexOf('='); if (eq < 0) continue;
    String k = line.substring(0, eq), v = line.substring(eq + 1);
    k.trim(); v.trim(); k.toLowerCase();
    if      (k == "lat") P.lat = v.toFloat();
    else if (k == "lon") P.lon = v.toFloat();
    else if (k == "dist") P.dist = v.toInt();
    else if (k == "wfname") P.ssid = v;
    else if (k == "wfpass") P.pass = v;
    else if (k == "urlbase") P.urlbase = cleanUrl(v);
    else if (k == "tz") P.tz = v;
    else if (k == "otauser") P.otaUser = v;
    else if (k == "otapass") P.otaPass = v;
    else if (k == "otahost") P.otaHost = v;
    else if (k == "screenbrightness") { long b = v.toInt(); if (b >= 0 && b <= 255 && (b > 0 || v == "0")) P.brightness = (int)b; }
    else if (k == "sleeptimer") P.sleepTimer = v;
    else if (k == "wakeuptimer") P.wakeupTimer = v;
    else if (k == "wheaterpooling" || k == "weatherpooling") P.weatherMs = max(60L, v.toInt()) * 1000UL;
    else if (k == "flighpooling" || k == "flightpooling")    P.flightMs  = max(5L,  v.toInt()) * 1000UL;
  }
  f.close();
  return P.ssid.length() && P.urlbase.length();
}

// Estado de sleep: calculado em UM só lugar (updateSleepState, chamado pelo loop 1x/s) e lido por
// todos (brilho e netTask) via isSleepTime(). Assim a tela e a busca de voos nunca discordam, e uma
// leitura de hora ruim/instável não faz o aparelho acordar e dormir de novo.
static volatile bool gSleeping = false;
static bool isSleepTime() { return gSleeping; }

// Resultado bruto da janela para a hora local atual. Retorna false em *valid se não dá para decidir.
static bool sleepWindowNow(bool* valid, int* hh, int* mm) {
  *valid = false;
  uint16_t sleepAt, wakeAt;
  if (!parseClock(P.sleepTimer, sleepAt) || !parseClock(P.wakeupTimer, wakeAt) || sleepAt == wakeAt) { *valid = true; *hh = *mm = -1; return false; }  // sem janela configurada
  time_t t = time(nullptr);
  if (t < 1700000000) return false;                       // hora ainda não sincronizada: não decide
  struct tm now; localtime_r(&t, &now);
  *hh = now.tm_hour; *mm = now.tm_min; *valid = true;
  int current = now.tm_hour * 60 + now.tm_min;
  if (sleepAt < wakeAt) return current >= sleepAt && current < wakeAt;
  return current >= sleepAt || current < wakeAt;           // janela atravessa a meia-noite (ex.: 23:00 -> 07:30)
}

static void updateSleepState() {
  static uint32_t lastChk = 0; static bool first = true; static int streak = 0;
  if (millis() - lastChk < 1000) return;
  lastChk = millis();
  bool valid; int hh, mm;
  bool raw = sleepWindowNow(&valid, &hh, &mm);
  if (!valid) return;                                      // sem hora: mantém o estado atual (nunca "acorda" por falha de leitura)
  if (raw == gSleeping) { streak = 0; first = false; return; }
  // mudança de estado: a primeira leitura válida vale na hora; depois exige 5 leituras seguidas (5 s)
  if (!first && ++streak < 5) return;
  streak = 0; first = false;
  gSleeping = raw;
  Serial.printf("[sleep] %s (hora local %02d:%02d, janela %s-%s)\n", raw ? "ENTRANDO em sleep" : "SAINDO do sleep",
                hh, mm, P.sleepTimer.c_str(), P.wakeupTimer.c_str());
}

// =====================================================================
//  Texto: remoção de acentos (quando usando fontes embutidas)
// =====================================================================
static char accentMap(uint8_t b) {
  if (b >= 0x80 && b <= 0x85) return 'A'; if (b == 0x87) return 'C';
  if (b >= 0x88 && b <= 0x8B) return 'E'; if (b >= 0x8C && b <= 0x8F) return 'I';
  if (b == 0x91) return 'N';              if (b >= 0x92 && b <= 0x96) return 'O';
  if (b >= 0x99 && b <= 0x9C) return 'U'; if (b >= 0xA0 && b <= 0xA5) return 'a';
  if (b == 0xA7) return 'c';              if (b >= 0xA8 && b <= 0xAB) return 'e';
  if (b >= 0xAC && b <= 0xAF) return 'i'; if (b == 0xB1) return 'n';
  if (b >= 0xB2 && b <= 0xB6) return 'o'; if (b >= 0xB9 && b <= 0xBC) return 'u';
  return '?';
}
static void plainInPlace(char* s) {
#if !USE_CUSTOM_FONTS
  char* w = s;
  for (const uint8_t* r = (const uint8_t*)s; *r; ) {
    if (*r < 0x80) { *w++ = *r++; }
    else if (r[0] == 0xC3 && r[1]) { *w++ = accentMap(r[1]); r += 2; }
    else if (r[0] == 0xC2 && r[1] == 0xB0) { *w++ = 0xC2; *w++ = 0xB0; r += 2; }   // °
    else { *w++ = '?'; r++; while ((*r & 0xC0) == 0x80) r++; }
  }
  *w = 0;
#endif
}
static String pt(const char* s) { char b[96]; strlcpy(b, s, sizeof b); plainInPlace(b); return String(b); }

static const char* S(JsonVariantConst v, const char* d) {
  const char* s = v.as<const char*>(); return (s && *s) ? s : d;
}
static void trimInPlace(char* s) {
  int n = strlen(s); while (n > 0 && isspace((uint8_t)s[n - 1])) s[--n] = 0;
}

// =====================================================================
//  SVG -> imagem LVGL (TRUE_COLOR_ALPHA) com nanosvg
// =====================================================================
// Faz o parse direto em 'buf' (terminado em NUL) e o DESTRÓI — evita duplicar SVGs grandes na RAM.
static bool rasterSvgInPlace(char* buf, int box, bool tint, lv_color_t tc, SvgImg& out) {
  out = {nullptr, 0, 0};
  esp_task_wdt_reset();                                  // parse/raster de SVG grande pode demorar
  NSVGimage* im = nsvgParse(buf, "px", 96.0f);
  esp_task_wdt_reset();
  if (!im) return false;
  if (im->width <= 0 || im->height <= 0) { nsvgDelete(im); return false; }
  float sc = (float)box / fmaxf(im->width, im->height);
  int ow = (int)ceilf(im->width * sc), oh = (int)ceilf(im->height * sc);
  if (ow < 1) ow = 1; if (oh < 1) oh = 1;
  uint8_t* rgba = (uint8_t*)calloc((size_t)ow * oh, 4);
  NSVGrasterizer* rs = nsvgCreateRasterizer();
  uint8_t* px = (uint8_t*)malloc((size_t)ow * oh * LV_IMG_PX_SIZE_ALPHA_BYTE);
  if (!rgba || !rs || !px) {
    free(rgba); free(px); if (rs) nsvgDeleteRasterizer(rs); nsvgDelete(im); return false;
  }
  esp_task_wdt_reset();
  nsvgRasterize(rs, im, 0, 0, sc, rgba, ow, oh, ow * 4);
  esp_task_wdt_reset();
  for (size_t i = 0; i < (size_t)ow * oh; i++) {
    lv_color_t c = tint ? tc : lv_color_make(rgba[i * 4], rgba[i * 4 + 1], rgba[i * 4 + 2]);
    uint8_t* d = px + i * LV_IMG_PX_SIZE_ALPHA_BYTE;
    memcpy(d, &c, sizeof(lv_color_t));
    d[sizeof(lv_color_t)] = rgba[i * 4 + 3];
  }
  free(rgba); nsvgDeleteRasterizer(rs); nsvgDelete(im);
  out = {px, ow, oh};
  return true;
}
// Versão que preserva o texto original (usada com SVGs pequenos / literais em flash).
static bool rasterSvg(const char* svg, size_t len, int box, bool tint, lv_color_t tc, SvgImg& out) {
  out = {nullptr, 0, 0};
  char* tmp = (char*)malloc(len + 1); if (!tmp) return false;
  memcpy(tmp, svg, len); tmp[len] = 0;
  bool ok = rasterSvgInPlace(tmp, box, tint, tc, out);
  free(tmp);
  return ok;
}
static void fillDsc(lv_img_dsc_t& d, const SvgImg& s) {
  memset(&d, 0, sizeof d);
  d.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
  d.header.w = s.w; d.header.h = s.h;
  d.data_size = (uint32_t)s.w * s.h * LV_IMG_PX_SIZE_ALPHA_BYTE;
  d.data = s.data;
}

// ---- PNG -> bitmap pequeno (lê linha a linha e reduz; não precisa de RAM para a imagem inteira) ----
struct PngCtx { uint32_t* acc; int tw, th, w, h; bool bad; };

// PNGdec 1.1.7: o callback retorna int; 1 = continua, 0 = aborta a decodificação.
// d->iBpp = bits por CANAL (8), não por pixel.
static int pngDraw(PNGDRAW* d) {
  PngCtx* c = (PngCtx*)d->pUser;
  int ty = (int)((uint64_t)d->y * c->th / c->h);
  if (ty >= c->th) ty = c->th - 1;
  const uint8_t* p = d->pPixels;
  for (int x = 0; x < d->iWidth; x++) {
    uint8_t r, g, b, a = 255;
    switch (d->iPixelType) {
      case PNG_PIXEL_TRUECOLOR_ALPHA: if (d->iBpp != 8) { c->bad = true; return 0; }
        r = p[x * 4]; g = p[x * 4 + 1]; b = p[x * 4 + 2]; a = p[x * 4 + 3]; break;
      case PNG_PIXEL_TRUECOLOR:       if (d->iBpp != 8) { c->bad = true; return 0; }
        r = p[x * 3]; g = p[x * 3 + 1]; b = p[x * 3 + 2]; break;
      case PNG_PIXEL_GRAY_ALPHA:      if (d->iBpp != 8) { c->bad = true; return 0; }
        r = g = b = p[x * 2]; a = p[x * 2 + 1]; break;
      case PNG_PIXEL_GRAYSCALE:       if (d->iBpp != 8) { c->bad = true; return 0; }
        r = g = b = p[x]; break;
      case PNG_PIXEL_INDEXED: {       if (d->iBpp != 8) { c->bad = true; return 0; }
        uint8_t i = p[x]; r = d->pPalette[i * 3]; g = d->pPalette[i * 3 + 1]; b = d->pPalette[i * 3 + 2];
        a = d->pPalette[768 + i]; break; }
      default: c->bad = true; return 0;
    }
    int tx = (int)((uint64_t)x * c->tw / c->w);
    if (tx >= c->tw) tx = c->tw - 1;
    uint32_t* q = c->acc + ((size_t)ty * c->tw + tx) * 5;
    q[0] += (uint32_t)r * a; q[1] += (uint32_t)g * a; q[2] += (uint32_t)b * a; q[3] += a; q[4] += 1;
  }
  return 1;
}

static bool decodePngScaled(const uint8_t* data, size_t len, int box, bool tint, lv_color_t tc, SvgImg& out) {
  out = {nullptr, 0, 0};
  PNG* png = new PNG();                       // ~50 KB no heap
  if (!png) return false;
  int orc = png->openRAM((uint8_t*)data, (int)len, pngDraw);
  if (orc != PNG_SUCCESS) { Serial.printf("[png] openRAM erro %d (3=sem buffer, 4=recurso nao suportado, 5=invalido, 6=largo demais)\n", orc); delete png; return false; }
  int w = png->getWidth(), h = png->getHeight();
  if (w <= 0 || h <= 0) { delete png; return false; }   // linhas largas demais (>320 px RGBA) já voltam como PNG_TOO_BIG no openRAM
  float sc = (float)box / fmaxf(w, h); if (sc > 1.0f) sc = 1.0f;
  int tw = (int)lroundf(w * sc), th = (int)lroundf(h * sc); if (tw < 1) tw = 1; if (th < 1) th = 1;
  PngCtx ctx = {(uint32_t*)calloc((size_t)tw * th * 5, sizeof(uint32_t)), tw, th, w, h, false};
  uint8_t* px = (uint8_t*)malloc((size_t)tw * th * LV_IMG_PX_SIZE_ALPHA_BYTE);
  if (!ctx.acc || !px) { free(ctx.acc); free(px); delete png; return false; }
  int rc = png->decode(&ctx, 0);
  int ptype = png->getPixelType(), pbpp = png->getBpp(), perr = png->getLastError();
  delete png;
  if (rc != PNG_SUCCESS || ctx.bad) {
    Serial.printf("[png] decode rc=%d erro=%d bad=%d tipo=%d bpp=%d\n", rc, perr, (int)ctx.bad, ptype, pbpp);
    free(ctx.acc); free(px); return false;
  }
  for (int i = 0; i < tw * th; i++) {
    const uint32_t* q = ctx.acc + (size_t)i * 5;
    uint8_t r = 0, g = 0, b = 0, a = 0;
    if (q[4] && q[3]) { r = q[0] / q[3]; g = q[1] / q[3]; b = q[2] / q[3]; a = q[3] / q[4]; }
    lv_color_t c = tint ? tc : lv_color_make(r, g, b);
    uint8_t* d = px + (size_t)i * LV_IMG_PX_SIZE_ALPHA_BYTE;
    memcpy(d, &c, sizeof(lv_color_t)); d[sizeof(lv_color_t)] = a;
  }
  free(ctx.acc);
  out = {px, tw, th};
  return true;
}

// ---- Ícones de clima (SVG embutido, 24x24) ----
#define SV_HEAD "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
#define SV_END  "</svg>"
#define CLOUD(col) "<g fill='" col "'><circle cx='7' cy='14.5' r='3.5'/><circle cx='12' cy='10.5' r='5'/>" \
                   "<circle cx='17.5' cy='14' r='4'/><rect x='7' y='14' width='10.5' height='4.5'/></g>"
static const char* WX_SVG[9] = {
  /*0 sol*/       SV_HEAD "<circle cx='12' cy='12' r='5' fill='#FFC83D'/>"
                  "<path d='M12 1.5v3M12 19.5v3M1.5 12h3M19.5 12h3M4.6 4.6l2.1 2.1M17.3 17.3l2.1 2.1M4.6 19.4l2.1-2.1M17.3 6.7l2.1-2.1' stroke='#FFC83D' stroke-width='1.8' stroke-linecap='round' fill='none'/>" SV_END,
  /*1 parcial*/   SV_HEAD "<circle cx='8' cy='8' r='3.6' fill='#FFC83D'/>"
                  "<path d='M8 1.5v2M1.5 8h2M3.4 3.4l1.4 1.4M12.6 3.4l-1.4 1.4' stroke='#FFC83D' stroke-width='1.6' stroke-linecap='round' fill='none'/>"
                  "<g transform='translate(2.5,3)'>" CLOUD("#CFE8F3") "</g>" SV_END,
  /*2 nublado*/   SV_HEAD CLOUD("#CFE8F3") SV_END,
  /*3 chuva*/     SV_HEAD "<g transform='translate(0,-3)'>" CLOUD("#CFE8F3") "</g>"
                  "<path d='M8 17.5l-1 3M12 17.5l-1 3M16 17.5l-1 3' stroke='#5BB6FF' stroke-width='1.7' stroke-linecap='round' fill='none'/>" SV_END,
  /*4 neve*/      SV_HEAD "<g transform='translate(0,-3)'>" CLOUD("#CFE8F3") "</g>"
                  "<circle cx='8' cy='19' r='1.4' fill='#FFFFFF'/><circle cx='12' cy='21' r='1.4' fill='#FFFFFF'/><circle cx='16' cy='19' r='1.4' fill='#FFFFFF'/>" SV_END,
  /*5 nevoeiro*/  SV_HEAD "<g transform='translate(0,-3)'>" CLOUD("#9FB3BD") "</g>"
                  "<path d='M5 18h14M7 21h10' stroke='#9FB3BD' stroke-width='1.7' stroke-linecap='round' fill='none'/>" SV_END,
  /*6 trovoada*/  SV_HEAD "<g transform='translate(0,-3)'>" CLOUD("#9FB3BD") "</g>"
                  "<polygon points='12.5,14.5 9,20 11.5,20 10.5,23.5 15,18 12.5,18 13.5,14.5' fill='#FFC83D'/>" SV_END,
  /*7 lua (noite, limpo)*/  SV_HEAD "<path fill='#F4EBC1' d='M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z'/>" SV_END,
  /*8 lua + nuvem (noite, parcial)*/ SV_HEAD "<g transform='scale(0.62)'><path fill='#F4EBC1' d='M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z'/></g>"
                  "<g transform='translate(2.5,3)'>" CLOUD("#CFE8F3") "</g>" SV_END,
};
static SvgImg wxImg[9];
static lv_img_dsc_t wxDsc[9];

// À noite (18:00 até 05:59) os ícones com sol viram lua (limpo -> lua, parcial -> lua com nuvem)
static int  wxIc = 2;            // ícone base (0..6) do último clima recebido
static bool wxNight = false;
static bool isNightNow() {
  struct tm t;
  if (!getLocalTime(&t, 0) || t.tm_year < 120) return false;      // sem hora válida: ícones de dia
  return t.tm_hour >= 18 || t.tm_hour < 6;
}
static const lv_img_dsc_t* wxIconFor(int ic, bool night) {
  if (night && ic == 0) return &wxDsc[7];
  if (night && ic == 1) return &wxDsc[8];
  return &wxDsc[ic];
}

// Silhueta usada quando a API não fornece uma imagem válida.
static const char* GENERIC_SIL_SVG =
  "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 800 800'>"
  "<g fill='none' stroke='#000000' stroke-width='32' stroke-linecap='round' stroke-linejoin='round'>"
  "<path d='M400 64L620 650L400 520L180 650Z'/>"
  "<path d='M400 64V520'/>"
  "</g></svg>";

static void* sdFsOpen(lv_fs_drv_t*, const char* path, lv_fs_mode_t mode) {
  if (!(mode & LV_FS_MODE_RD)) return nullptr;
  const char* p = (path[0] == '/' ? path : path);
  File* f = new File(SD_MMC.open(p, FILE_READ));
  if (!*f) { delete f; return nullptr; }
  return f;
}
static lv_fs_res_t sdFsClose(lv_fs_drv_t*, void* file_p) {
  File* f = static_cast<File*>(file_p);
  f->close(); delete f; return LV_FS_RES_OK;
}
static lv_fs_res_t sdFsRead(lv_fs_drv_t*, void* file_p, void* buf, uint32_t btr, uint32_t* br) {
  File* f = static_cast<File*>(file_p);
  size_t n = f->read(static_cast<uint8_t*>(buf), btr);
  if (br) *br = (uint32_t)n;
  return LV_FS_RES_OK;
}
static lv_fs_res_t sdFsSeek(lv_fs_drv_t*, void* file_p, uint32_t pos, lv_fs_whence_t whence) {
  File* f = static_cast<File*>(file_p);
  if (whence == LV_FS_SEEK_CUR) pos += f->position();
  else if (whence == LV_FS_SEEK_END) pos += f->size();
  return f->seek(pos) ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}
static lv_fs_res_t sdFsTell(lv_fs_drv_t*, void* file_p, uint32_t* pos) {
  *pos = static_cast<File*>(file_p)->position(); return LV_FS_RES_OK;
}
static void registerSdFs() {
  static lv_fs_drv_t drv;
  lv_fs_drv_init(&drv);
  drv.letter = 'S';
  drv.open_cb = sdFsOpen; drv.close_cb = sdFsClose;
  drv.read_cb = sdFsRead; drv.seek_cb = sdFsSeek; drv.tell_cb = sdFsTell;
  lv_fs_drv_register(&drv);
}

static Wx wxFromCode(int c) {
  if (c == 0) return {"Céu limpo", 0};
  if (c == 1) return {"Quase limpo", 0};
  if (c == 2) return {"Parcialmente nublado", 1};
  if (c == 3) return {"Nublado", 2};
  if (c == 45 || c == 48) return {"Nevoeiro", 5};
  if (c >= 51 && c <= 57) return {"Garoa", 3};
  if (c >= 61 && c <= 67) return {"Chuva", 3};
  if (c >= 71 && c <= 77) return {"Neve", 4};
  if (c >= 80 && c <= 82) return {"Aguaceiros", 3};
  if (c == 85 || c == 86) return {"Neve", 4};
  if (c >= 95) return {"Trovoada", 6};
  return {"--", 2};
}

// =====================================================================
//  Dados compartilhados entre a task de rede e a UI
// =====================================================================
static SemaphoreHandle_t gMtx;
static FlightUI gFlight;  static bool gFlightDirty = false;
static uint8_t* gPendLogo = nullptr; static size_t gPendLogoLen = 0; static int gPendLogoW = 0, gPendLogoH = 0;
static bool gLogoChanged = false;
static SvgImg gPendSil = {nullptr, 0, 0}; static bool gSilChanged = false;
static float gTemp = 0; static int gWxCode = -1; static bool gWxDirty = false;

static void pushLogo(uint8_t* b, size_t n, int w, int h) {
  xSemaphoreTake(gMtx, portMAX_DELAY);
  if (gPendLogo) free(gPendLogo);
  gPendLogo = b; gPendLogoLen = n; gPendLogoW = w; gPendLogoH = h; gLogoChanged = true;
  xSemaphoreGive(gMtx);
}
static void pushSil(const SvgImg& s) {
  xSemaphoreTake(gMtx, portMAX_DELAY);
  if (gPendSil.data) free(gPendSil.data);
  gPendSil = s; gSilChanged = true;
  xSemaphoreGive(gMtx);
}

// =====================================================================
//  HTTP
// =====================================================================
static uint16_t gHttpReadMs = 8000;     // tempo de leitura do HTTP (maior para imagens: o servidor pode gerar na 1ª vez)
static bool gHttpTooBig = false;   // true quando o servidor respondeu 200 mas o arquivo excede o limite
static int gLastNetCode = 0;       // código do último GET JSON (<0 = falha de conexão; o backoff usa)
static String gDnsSeen[6];         // hosts já resolvidos desde a última conexão (log [dns] uma vez por host)
static uint8_t gDnsN = 0;

// Diagnóstico de falha de conexão (GET < 0): estado do Wi-Fi e tempo gasto
static void logNetFail(int code, uint32_t t0) {
  Serial.printf("[net] GET %d em %u ms: wifi=%d rssi=%d ip=%s gw=%s dns=%s\n", code, (unsigned)(millis() - t0),
                (int)WiFi.status(), WiFi.RSSI(), WiFi.localIP().toString().c_str(),
                WiFi.gatewayIP().toString().c_str(), WiFi.dnsIP(0).toString().c_str());
}
static String urlHost(const String& url) {
  int a = url.indexOf("://"); a = a < 0 ? 0 : a + 3;
  int b = url.indexOf('/', a); if (b < 0) b = url.length();
  int c = url.indexOf(':', a); if (c >= 0 && c < b) b = c;
  return url.substring(a, b);
}
// Resolve o host da URL na 1ª consulta após cada conexão (gDnsN zera em wifiUp())
static void dnsCheck(const String& url) {
  if (WiFi.status() != WL_CONNECTED) return;
  String h = urlHost(url);
  for (uint8_t i = 0; i < gDnsN; i++) if (gDnsSeen[i] == h) return;
  if (gDnsN < 6) gDnsSeen[gDnsN++] = h;
  IPAddress ip; uint32_t t0 = millis();
  if (WiFi.hostByName(h.c_str(), ip) == 1)
    Serial.printf("[dns] %s -> %s (%u ms)\n", h.c_str(), ip.toString().c_str(), (unsigned)(millis() - t0));
  else Serial.printf("[dns] falhou %s (%u ms)\n", h.c_str(), (unsigned)(millis() - t0));
}
// Cliente de uma requisição: https usa WiFiClientSecure (sem CA), http usa WiFiClient.
// (struct só no corpo das funções: o gerador de protótipos do Arduino não enxerga tipos definidos aqui nas assinaturas)
struct HttpSess { WiFiClientSecure sc; WiFiClient pc; HTTPClient http; };
static HttpSess* gKeep = nullptr;      // conexão persistente do P.urlbase (criada sob demanda, nunca global)
static String gKeepHost;

// Descarta a conexão persistente (falha, troca de host, Wi-Fi caiu)
static void keepDrop() {
  if (!gKeep) return;
  gKeep->http.end(); gKeep->sc.stop(); gKeep->pc.stop();
  delete gKeep; gKeep = nullptr; gKeepHost = "";
}

// Abre o GET com o cliente passado. Devolve o código (<0 = falha de conexão; 0 = begin falhou).
// Com 200 e Content-Length acima de maxLen marca gHttpTooBig. Quem chama lê o corpo e chama http.end().
static int httpOpen(HTTPClient& http, WiFiClientSecure& sc, WiFiClient& pc, const String& url, uint16_t readMs, size_t maxLen, bool reuse) {
  gHttpTooBig = false;
  bool https = url.startsWith("https");
  if (https) sc.setInsecure();
  http.setConnectTimeout(6000); http.setTimeout(readMs);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setReuse(reuse);
  bool ok = https ? http.begin(sc, url) : http.begin(pc, url);
  if (!ok) return 0;
  bool reused = reuse && (https ? sc.connected() : pc.connected());   // socket ainda aberto do GET anterior
  dnsCheck(url);
  uint32_t t0 = millis();
  int code = http.GET();
  uint32_t ms = millis() - t0;
  int sz = http.getSize();
  Serial.printf("[http] %d  %s  (len=%d, heap=%u, psram=%u, %u ms, reuse=%d)\n", code, url.c_str(), sz,
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram(), (unsigned)ms, (int)reused);
  if (code < 0) {
    logNetFail(code, t0);
    Serial.printf("[http] erro de conexao: %s\n", http.errorToString(code).c_str());
    return code;
  }
  if (code == 200 && sz > 0 && (size_t)sz > maxLen) {
    Serial.printf("[http] arquivo de %d bytes excede o limite de %u\n", sz, (unsigned)maxLen);
    gHttpTooBig = true;
  }
  return code;
}

static bool httpGet(const String& url, String& out, size_t maxLen) {
  WiFiClientSecure sc; WiFiClient pc; HTTPClient http;
  int code = httpOpen(http, sc, pc, url, gHttpReadMs, maxLen, false);
  bool ok = false;
  if (code == 200 && !gHttpTooBig) { out = http.getString(); ok = out.length() > 0 && out.length() <= maxLen; }
  http.end();
  return ok;
}

// Baixa para um buffer terminado em NUL, usando PSRAM quando existir (não duplica na RAM).
// Só funciona com Content-Length conhecido (sem ele, lê a String inteira).
static bool httpGetBuf(const String& url, uint8_t** out, size_t* outLen, size_t maxLen) {
  *out = nullptr; *outLen = 0;
  WiFiClientSecure sc; WiFiClient pc; HTTPClient http;
  int code = httpOpen(http, sc, pc, url, 10000, maxLen, false);
  if (code != 200 || gHttpTooBig) { http.end(); return false; }
  int sz = http.getSize();
  if (sz <= 0) {                                    // sem Content-Length (resposta em blocos/chunked): getString() decodifica
    String body = http.getString(); http.end();
    size_t bl = body.length();
    if (!bl) return false;
    if (bl > maxLen) { Serial.printf("[http] arquivo de %u bytes excede o limite de %u\n", (unsigned)bl, (unsigned)maxLen); gHttpTooBig = true; return false; }
    uint8_t* b = (uint8_t*)heap_caps_malloc(bl + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!b) b = (uint8_t*)malloc(bl + 1);
    if (!b) { Serial.println("[http] sem memoria para o arquivo"); return false; }
    memcpy(b, body.c_str(), bl); b[bl] = 0; *out = b; *outLen = bl;
    Serial.printf("[http] resposta sem Content-Length: %u bytes\n", (unsigned)bl);
    return true;
  }
  uint8_t* buf = (uint8_t*)heap_caps_malloc((size_t)sz + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) buf = (uint8_t*)malloc((size_t)sz + 1);
  if (!buf) { Serial.println("[http] sem memoria para o arquivo"); http.end(); return false; }
  WiFiClient* st = http.getStreamPtr();
  size_t got = 0; uint32_t last = millis();
  while ((http.connected() || st->available()) && got < (size_t)sz && millis() - last < 10000) {
    size_t av = st->available();
    if (av) { size_t n = st->readBytes(buf + got, min(av, (size_t)sz - got)); got += n; last = millis(); }
    else vTaskDelay(1);
    esp_task_wdt_reset();                                // download longo: alimenta o watchdog da netTask
  }
  http.end();
  if (got != (size_t)sz) { Serial.printf("[http] leitura incompleta %u/%d\n", (unsigned)got, sz); free(buf); return false; }
  buf[sz] = 0; *out = buf; *outLen = got;
  return true;
}

// A API devolve links de imagem em http:// mesmo servindo a API em https://.
// O HTTPClient não segue redirecionamento http->https com cliente não-seguro; por isso tentamos https primeiro.
// Usada por httpGetImg (String) e httpGetImgBuf (buffer). Se o https falhar e não for "grande demais", tenta a URL original.
static bool httpGetImgCore(const String& url, bool useBuf, String* s, uint8_t** b, size_t* bl, size_t maxLen) {
  auto get = [&](const String& u) { return useBuf ? httpGetBuf(u, b, bl, maxLen) : httpGet(u, *s, maxLen); };
  if (url.startsWith("http://") && P.urlbase.startsWith("https://")) {
    if (get("https://" + url.substring(7))) return true;
    if (gHttpTooBig) return false;                // (se for grande demais, http também não ajuda)
    Serial.println("[img] https falhou, tentando http original");
  }
  return get(url);
}

static bool httpGetImg(const String& url, String& out, size_t maxLen) {
  gHttpReadMs = 10000;                          // imagens: 10 s (antes 8 s; 20 s deixava a netTask bloqueada demais)
  bool r = httpGetImgCore(url, false, &out, nullptr, nullptr, maxLen);
  gHttpReadMs = 8000;
  return r;
}

static bool httpGetImgBuf(const String& url, uint8_t** out, size_t* outLen, size_t maxLen) {
  return httpGetImgCore(url, true, nullptr, out, outLen, maxLen);
}

// GET de JSON filtrado. Hosts do P.urlbase reusam conexão persistente (keep-alive, TLS mantido entre as buscas);
// outros hosts (ex.: clima) usam cliente local. Qualquer falha descarta a conexão persistente.
// Com Content-Length lê do stream e drena o resto (conexão limpa para reuso); sem tamanho (chunked) lê a String, limitada a maxLen.
static bool httpGetJson(const String& url, JsonDocument& doc, const JsonDocument& filter, size_t maxLen) {
  if (WiFi.status() != WL_CONNECTED) keepDrop();      // Wi-Fi caiu: conexão antiga não vale mais
  String host = urlHost(url);
  bool keep = host == urlHost(P.urlbase);
  if (keep && gKeep && gKeepHost != host) keepDrop();  // host diferente do último: recria
  if (keep && !gKeep) { gKeep = new HttpSess(); gKeepHost = host; }
  bool pers = keep && gKeep;
  HttpSess loc;
  HttpSess& s = pers ? *gKeep : loc;
  int code = httpOpen(s.http, s.sc, s.pc, url, gHttpReadMs, maxLen, pers);
  gLastNetCode = code;
  int sz = s.http.getSize();
  bool ok = false;
  if (code == 200 && !gHttpTooBig) {
    DeserializationError err;
    if (sz > 0) {
      struct CntStream : Stream {                      // conta os bytes que o ArduinoJson consumiu do stream
        Stream* in; size_t n = 0;
        int available() override { return in->available(); }
        int read() override { int c = in->read(); if (c >= 0) n++; return c; }
        int peek() override { return in->peek(); }
        size_t write(uint8_t) override { return 0; }
      } cs;
      cs.in = s.http.getStreamPtr();
      err = deserializeJson(doc, cs, DeserializationOption::Filter(filter));
      ok = !err;
      if (ok) {                                        // drena o resto da resposta: conexão fica limpa para o próximo GET
        uint32_t last = millis(); uint8_t tmp[64];
        while (cs.n < (size_t)sz && (s.http.connected() || cs.in->available()) && millis() - last < gHttpReadMs) {
          size_t av = cs.in->available();
          if (av) {
            size_t k = cs.in->readBytes(tmp, min(min(av, sizeof tmp), (size_t)sz - cs.n));
            cs.n += k; last = millis();
          } else vTaskDelay(1);
          esp_task_wdt_reset();
        }
        if (cs.n != (size_t)sz) { Serial.printf("[http] leitura incompleta %u/%d\n", (unsigned)cs.n, sz); ok = false; }
      }
    } else {                                           // chunked/sem tamanho: getString decodifica
      String body = s.http.getString();
      if (body.length() > maxLen) Serial.printf("[http] corpo de %u bytes excede o limite de %u\n", (unsigned)body.length(), (unsigned)maxLen);
      else { err = deserializeJson(doc, body, DeserializationOption::Filter(filter)); ok = !err; }
    }
    if (err) Serial.printf("JSON invalido: %s (code=%d, sz=%d)\n", err.c_str(), code, sz);
  }
  s.http.end();
  if (!ok && pers) keepDrop();                         // falha: fecha e recria na próxima busca
  return ok;
}

static uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

static String lastLogoUrl, lastSilUrl;
static String lastAcId;                                // hex|callsign da última aeronave publicada por inteiro ("" = sem voo)
static bool silLoaded = false;
static String silShownUrl;                             // URL a que pertence o bitmap de silhueta enviado à UI ("" = genérica)

static bool fetchFlight() {
  String url = P.urlbase + "/api/nearest-aircraft?lat=" + String(P.lat, 6) + "&lon=" + String(P.lon, 6) + "&dist=" + String(P.dist);
  JsonDocument flt;                                    // filtro: só os campos lidos abaixo
  flt["location"]["city"] = true; flt["location"]["principalsubdivision"] = true; flt["location"]["countryname"] = true;
  flt["aircraft"]["hex"] = true; flt["aircraft"]["flight"] = true; flt["aircraft"]["alt_baro"] = true;
  flt["aircraft"]["dst"] = true; flt["aircraft"]["t"] = true; flt["aircraft"]["desc"] = true;
  flt["flight_info"]["callsign_iata"] = true;
  flt["flight_info"]["airline"]["icao"] = true; flt["flight_info"]["airline"]["name"] = true;
  for (const char* side : {"origin", "destination"}) {
    flt["flight_info"][side]["iata_code"] = true; flt["flight_info"][side]["icao_code"] = true;
    flt["flight_info"][side]["name"] = true; flt["flight_info"][side]["municipality"] = true;
    flt["flight_info"][side]["country_name"] = true;
  }
  flt["airline_logo_small"] = true; flt["airline_logo"] = true; flt["silhouette_png"] = true; flt["silhouette"] = true;
  JsonDocument doc;
  if (!httpGetJson(url, doc, flt, 32768)) return false; // mantém o último dado na tela

  FlightUI f; memset(&f, 0, sizeof f);

  // Localização (cidade - estado/país): a API devolve sempre, inclusive quando não há aeronave
  JsonObject loc = doc["location"].as<JsonObject>();
  {
    const char* city = S(loc["city"], ""), *sub = S(loc["principalsubdivision"], ""), *country = S(loc["countryname"], "");
    if (*city || *country) {
      if (*sub) snprintf(f.city, sizeof f.city, "%s - %s/%s", city, sub, country);
      else      snprintf(f.city, sizeof f.city, "%s/%s", city, country);
      plainInPlace(f.city);
    }
  }

  JsonObject ac = doc["aircraft"].as<JsonObject>();
  // aeronave em solo (alt_baro vem como "ground") é ignorada: tratada como "sem voo"
  if (!ac.isNull() && ac["alt_baro"].is<const char*>()) {
    Serial.printf("[flight] %s em solo: ignorada\n", S(ac["flight"], "---"));
    ac = JsonObject();
  }
  if (ac.isNull()) {
    f.valid = false; lastAcId = "";
    xSemaphoreTake(gMtx, portMAX_DELAY); gFlight = f; gFlightDirty = true; xSemaphoreGive(gMtx);
    return true;
  }
  f.valid = true;
  JsonObject fi  = doc["flight_info"].as<JsonObject>();
  JsonObject org = fi["origin"].as<JsonObject>();
  JsonObject dst = fi["destination"].as<JsonObject>();

  strlcpy(f.flight,  S(ac["flight"], "---"), sizeof f.flight);  trimInPlace(f.flight);
  strlcpy(f.iata,    S(fi["callsign_iata"], ""), sizeof f.iata);
  strlcpy(f.airline, S(fi["airline"]["icao"], ""), sizeof f.airline);
  strlcpy(f.airlineName, S(fi["airline"]["name"], ""), sizeof f.airlineName);
  strlcpy(f.type,    S(ac["t"], "---"), sizeof f.type);
  strlcpy(f.desc,    S(ac["desc"], ""), sizeof f.desc);
  const char* orgCode = S(org["iata_code"], S(org["icao_code"], "---"));
  const char* dstCode = S(dst["iata_code"], S(dst["icao_code"], "---"));
  strlcpy(f.org, orgCode[0] ? orgCode : "Indisponível", sizeof f.org);
  strlcpy(f.dst, dstCode[0] ? dstCode : "Indisponível", sizeof f.dst);

  // altitude (alt_baro, em ft) e distância (dst, em NM)
  f.alt[0] = 0; f.dist[0] = 0;
  {
    JsonVariantConst ab = ac["alt_baro"];
    if (ab.is<float>()) {
      int ft = (int)ab.as<float>();
      if (ft >= 1000)   snprintf(f.alt, sizeof f.alt, "%d.%03d ft", ft / 1000, ft % 1000);
      else if (ft >= 0) snprintf(f.alt, sizeof f.alt, "%d ft", ft);
    }
    JsonVariantConst dv = ac["dst"];
    if (dv.is<float>()) snprintf(f.dist, sizeof f.dist, "%.1f NM", dv.as<float>());
  }

  // Logo/silhueta: a URL é o que decide se há download (ver abaixo)
  String logoUrl = S(doc["airline_logo_small"], S(doc["airline_logo"], ""));   // prefere as versões leves
  String silUrl  = S(doc["silhouette_png"], S(doc["silhouette"], ""));

  // Mesma aeronave já publicada por inteiro e logo/silhueta resolvidos: publica só altitude/distância
  String id = String(S(ac["hex"], "")) + "|" + S(ac["flight"], "");
  if (id.length() > 1 && id == lastAcId && logoUrl == lastLogoUrl && silUrl == lastSilUrl && silLoaded) {
    xSemaphoreTake(gMtx, portMAX_DELAY);
    if (!gFlightDirty) gFlight.altOnly = true;        // se um voo completo ainda está pendente na UI, ele continua valendo
    strlcpy(gFlight.alt, f.alt, sizeof gFlight.alt); strlcpy(gFlight.dist, f.dist, sizeof gFlight.dist);
    gFlightDirty = true;
    xSemaphoreGive(gMtx);
    return true;
  }

  if (org.isNull() || !orgCode[0]) strlcpy(f.orgLine, "Rota desconhecida", sizeof f.orgLine);
  else snprintf(f.orgLine, sizeof f.orgLine, "%s" SEP "%s" SEP "%s" SEP "%s", f.org,
                S(org["name"], ""), S(org["municipality"], ""), S(org["country_name"], ""));
  if (dst.isNull() || !dstCode[0]) strlcpy(f.dstLine, "Indisponível", sizeof f.dstLine);
  else snprintf(f.dstLine, sizeof f.dstLine, "%s" SEP "%s" SEP "%s" SEP "%s", f.dst,
                S(dst["name"], ""), S(dst["municipality"], ""), S(dst["country_name"], ""));

  plainInPlace(f.flight); plainInPlace(f.iata); plainInPlace(f.desc); plainInPlace(f.airlineName);
  plainInPlace(f.orgLine); plainInPlace(f.dstLine);

  f.showLogo = logoUrl.length() && logoUrl == lastLogoUrl;        // já temos o bitmap desta URL
  f.showSil  = silLoaded && silUrl == silShownUrl;   // bitmap na UI é o desta URL (ou genérica, se ""), nunca de outra aeronave
  lastAcId = id;
  xSemaphoreTake(gMtx, portMAX_DELAY); gFlight = f; gFlightDirty = true; xSemaphoreGive(gMtx);

  Serial.printf("[flight] %s | logo=%s | sil=%s | em cache: logo=%d sil=%d\n", f.flight, logoUrl.c_str(), silUrl.c_str(), (int)f.showLogo, (int)f.showSil);
  // Tentativas com espera (3 vezes a cada 15 s, depois 1 a cada 5 min) em vez de desistir de vez:
  // uma falha passageira (rede, memória, servidor ainda gerando a imagem) não deixa o fallback para sempre.
  static String logoFailUrl, silFailUrl;
  static int logoFails = 0, silFails = 0;
  static uint32_t logoRetryAt = 0, silRetryAt = 0;

  // ---- logo da companhia (PNG, reduzido durante a decodificação) — só baixa quando muda ----
  if (logoUrl != lastLogoUrl) {
    if (logoUrl != logoFailUrl) { logoFailUrl = logoUrl; logoFails = 0; logoRetryAt = 0; }   // URL nova: zera o backoff
    if ((int32_t)(millis() - logoRetryAt) >= 0) {
      SvgImg im = {nullptr, 0, 0}; bool ok = false;
      if (logoUrl.length()) {
        String img;
        if (httpGetImg(logoUrl, img, 65536) && img.length() > 33 && !memcmp(img.c_str(), "\x89PNG", 4)) {
          const uint8_t* p = (const uint8_t*)img.c_str();
          uint32_t pw = be32(p + 16), ph = be32(p + 20);
          ok = decodePngScaled(p, img.length(), ICON_PX, false, lv_color_black(), im);
          Serial.printf("[logo] PNG %lux%lu, %u bytes -> %s (%dx%d), heap=%u\n", pw, ph, (unsigned)img.length(),
                        ok ? "OK" : "FALHOU", im.w, im.h, (unsigned)ESP.getFreeHeap());
        } else Serial.println("[logo] download falhou ou nao e PNG");
      }
      pushLogo(im.data, 0, im.w, im.h);
      if (ok || !logoUrl.length()) { lastLogoUrl = logoUrl; logoFails = 0; }
      else {
        logoFails++;
        uint32_t wait = logoFails <= 3 ? 15000UL : 300000UL;   // 3 tentativas a cada 15 s, depois 1 a cada 5 min
        logoRetryAt = millis() + wait;
        Serial.printf("[logo] falhou (%d); nova tentativa em %lu s\n", logoFails, (unsigned long)(wait / 1000));
      }
    }
  }

  // ---- silhueta (SVG -> bitmap); se falhar, usa a silhueta genérica ----
  if (!silLoaded || silUrl != lastSilUrl) {
    if (silUrl != silFailUrl) { silFailUrl = silUrl; silFails = 0; silRetryAt = 0; }
    if (!silLoaded || (int32_t)(millis() - silRetryAt) >= 0) {
      SvgImg im = {nullptr, 0, 0}; bool remoteOk = false, tooBig = false;
      if (silUrl.length()) {
        uint8_t* raw = nullptr; size_t rawLen = 0;
        size_t silMax = psramFound() ? 6UL * 1024 * 1024 : 120000UL;   // com PSRAM aceita SVG grande
        if (httpGetImgBuf(silUrl, &raw, &rawLen, silMax)) {
          uint32_t t0 = millis();
          if (rawLen > 33 && !memcmp(raw, "\x89PNG", 4))              // se a API passar a servir PNG pequeno
            remoteOk = decodePngScaled(raw, rawLen, ICON_PX, true, lv_color_hex(C_ACCENT), im);
          else
            remoteOk = rasterSvgInPlace((char*)raw, ICON_PX, true, lv_color_hex(C_ACCENT), im);   // destrói o buffer
          free(raw); raw = nullptr;
          Serial.printf("[sil] %u bytes, raster %s (%dx%d) em %lu ms, psram livre=%u\n", (unsigned)rawLen,
                        remoteOk ? "OK" : "FALHOU", im.w, im.h, (unsigned long)(millis() - t0), (unsigned)ESP.getFreePsram());
          if (remoteOk) {                             // bitmap vazio = nanosvg não entendeu o SVG
            size_t opaque = 0;
            for (size_t i = 0; i < (size_t)im.w * im.h; i++)
              if (im.data[i * LV_IMG_PX_SIZE_ALPHA_BYTE + sizeof(lv_color_t)] > 16) opaque++;
            if (opaque < 4) { Serial.println("[sil] bitmap vazio"); free(im.data); im = {nullptr, 0, 0}; remoteOk = false; }
          }
        } else {
          Serial.println("[sil] download falhou");
          tooBig = gHttpTooBig;                       // arquivo grande demais: não adianta repetir; usa a genérica
        }
      }
      if (!remoteOk) rasterSvg(GENERIC_SIL_SVG, strlen(GENERIC_SIL_SVG), ICON_PX, true, lv_color_hex(C_ACCENT), im);
      pushSil(im);
      silLoaded = true; silShownUrl = silUrl;
      if (remoteOk || !silUrl.length() || tooBig) { lastSilUrl = silUrl; silFails = 0; }
      else {
        silFails++;
        uint32_t wait = silFails <= 3 ? 15000UL : 300000UL;   // 3 tentativas a cada 15 s, depois 1 a cada 5 min
        silRetryAt = millis() + wait;
        Serial.printf("[sil] falhou (%d); nova tentativa em %lu s\n", silFails, (unsigned long)(wait / 1000));
      }
    }
  }
  return true;
}

static bool fetchWeather() {
  String url = String("https://api.open-meteo.com/v1/forecast?latitude=") + String(P.lat, 4) +
               "&longitude=" + String(P.lon, 4) + "&current=temperature_2m,weather_code";
  JsonDocument flt; flt["current"]["temperature_2m"] = true; flt["current"]["weather_code"] = true;
  JsonDocument doc;
  if (!httpGetJson(url, doc, flt, 8192)) { Serial.println("[wx] falha http/json"); return false; }
  JsonObject c = doc["current"].as<JsonObject>();
  if (c.isNull()) { Serial.println("[wx] sem 'current'"); return false; }
  float t = c["temperature_2m"].as<float>();               // mantém a casa decimal (ex.: 13.9)
  int wc = c["weather_code"] | 3;
  xSemaphoreTake(gMtx, portMAX_DELAY);
  gTemp = t;
  gWxCode = wc;
  gWxDirty = true;
  xSemaphoreGive(gMtx);
  Serial.printf("[wx] temp=%.1f code=%d\n", t, wc);
  return true;
}

// =====================================================================
//  OTA pelo navegador:  http://<ip-ou-otahost.local>/   (usuário/senha do param.txt)
//  Roda numa task própria (core 0, prioridade 0). A UI só lê gOtaPct para mostrar o progresso.
// =====================================================================
static volatile bool gOtaBusy = false;     // pausa as buscas de rede enquanto grava o firmware
static volatile int  gOtaPct  = -1;        // -1 inativo | 0..99 progresso | 101 concluído | 102 erro
static WebServer otaServer(80);
static bool otaAuthed = false;
static size_t otaBytes = 0, otaTotal = 0;

// (a página HTML está em ota_page.h)

static void otaUploadHandler() {
  HTTPUpload& up = otaServer.upload();
  if (up.status == UPLOAD_FILE_START) {
    otaAuthed = otaServer.authenticate(P.otaUser.c_str(), P.otaPass.c_str());
    if (!otaAuthed) return;
    gOtaBusy = true; gOtaPct = 0; otaBytes = 0;
    otaTotal = otaServer.header("Content-Length").toInt();
    Serial.printf("[ota] inicio: %s (%u bytes)\n", up.filename.c_str(), (unsigned)otaTotal);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!otaAuthed) return;
    if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
    otaBytes += up.currentSize;
    if (otaTotal > 0) { int p = (int)((uint64_t)otaBytes * 100 / otaTotal); gOtaPct = p > 99 ? 99 : p; }
  } else if (up.status == UPLOAD_FILE_END) {
    if (!otaAuthed) return;
    if (Update.end(true)) Serial.printf("[ota] concluido: %u bytes\n", (unsigned)up.totalSize);
    else Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (otaAuthed) { Update.abort(); Serial.println("[ota] abortado"); }
    gOtaBusy = false; gOtaPct = -1;
  }
}

static void otaFinishHandler() {
  if (!otaAuthed) { otaServer.requestAuthentication(); return; }
  bool ok = !Update.hasError();
  otaServer.sendHeader("Connection", "close");
  otaServer.send(ok ? 200 : 500, "text/plain", ok ? "OK" : Update.errorString());
  if (ok) { gOtaPct = 101; vTaskDelay(pdMS_TO_TICKS(800)); ESP.restart(); }
  else    { gOtaPct = 102; gOtaBusy = false; }
}


// ---------------------------------------------------------------------
//  Configuração do param.txt pela página web
// ---------------------------------------------------------------------
struct CfgField { const char* key; const char* alias; bool secret; };
static const CfgField CFG[] = {
  {"lat", nullptr, false}, {"lon", nullptr, false}, {"dist", nullptr, false},
  {"urlbase", nullptr, false},
  {"flighpooling", "flightpooling", false}, {"wheaterpooling", "weatherpooling", false},
  {"wfname", nullptr, false}, {"wfpass", nullptr, true},
  {"tz", nullptr, false}, {"screenbrightness", nullptr, false}, {"sleeptimer", nullptr, false}, {"wakeuptimer", nullptr, false},
  {"otauser", nullptr, false}, {"otapass", nullptr, true}, {"otahost", nullptr, false},
};
static const int CFG_N = sizeof(CFG) / sizeof(CFG[0]);

static bool otaCheckAuth() {
  if (otaServer.authenticate(P.otaUser.c_str(), P.otaPass.c_str())) return true;
  otaServer.requestAuthentication();
  return false;
}

// Chave da linha "chave=valor" (minúscula, sem espaços); "" se não for linha de configuração
static String cfgLineKey(String line) {
  line.trim();
  if (!line.length() || line[0] == '#') return "";
  int eq = line.indexOf('=');
  if (eq < 0) return "";
  String k = line.substring(0, eq); k.trim(); k.toLowerCase();
  return k;
}
static int cfgIndexOfKey(const String& k) {
  for (int i = 0; i < CFG_N; i++)
    if (k == CFG[i].key || (CFG[i].alias && k == CFG[i].alias)) return i;
  return -1;
}
static String readTextFile(const char* path) {
  String s; File f = SD_MMC.open(path);
  if (f) { s = f.readString(); f.close(); }
  return s;
}
// Valor padrão (da configuração em uso) quando a chave não está no arquivo
static String cfgDefault(const char* key) {
  String k = key;
  if (k == "lat") return String(P.lat, 6);
  if (k == "lon") return String(P.lon, 6);
  if (k == "dist") return String(P.dist);
  if (k == "urlbase") return P.urlbase;
  if (k == "flighpooling") return String(P.flightMs / 1000UL);
  if (k == "wheaterpooling") return String(P.weatherMs / 1000UL);
  if (k == "wfname") return P.ssid;
  if (k == "tz") return P.tz;
  if (k == "screenbrightness") return String(P.brightness);
  if (k == "sleeptimer") return P.sleepTimer;
  if (k == "wakeuptimer") return P.wakeupTimer;
  if (k == "otauser") return P.otaUser;
  if (k == "otahost") return P.otaHost;
  return "";
}

// Parâmetros extras cujo nome sugere segredo nunca são enviados ao navegador
static bool isSecretKey(String k) {
  k.toLowerCase();
  return k.indexOf("pass") >= 0 || k.indexOf("pwd") >= 0 || k.indexOf("senha") >= 0 ||
         k.indexOf("secret") >= 0 || k.indexOf("token") >= 0 || k.indexOf("key") >= 0;
}

static void cfgGetHandler() {
  if (!otaCheckAuth()) return;
  String txt = readTextFile("/param.txt");
  String vals[CFG_N]; bool found[CFG_N];
  for (int i = 0; i < CFG_N; i++) found[i] = false;
  JsonDocument doc;
  doc["fw"] = String(__DATE__) + " " + __TIME__;      // mesma informação do "[boot] firmware de ..."
  JsonArray extras = doc["extras"].to<JsonArray>();
  int pos = 0;
  while (pos <= (int)txt.length()) {
    int nl = txt.indexOf('\n', pos); if (nl < 0) nl = txt.length();
    String line = txt.substring(pos, nl); pos = nl + 1;
    line.replace("\r", "");
    String k = cfgLineKey(line); if (!k.length()) continue;
    int eq = line.indexOf('=');
    String v = line.substring(eq + 1); v.trim();
    int i = cfgIndexOfKey(k);
    if (i >= 0) { vals[i] = v; found[i] = true; continue; }
    String keyAsWritten = line.substring(0, eq); keyAsWritten.trim();       // chave desconhecida -> parâmetro adicional
    JsonObject o = extras.add<JsonObject>();
    o["k"] = keyAsWritten;
    if (isSecretKey(k)) o["s"] = true; else o["v"] = v;
  }
  for (int i = 0; i < CFG_N; i++) {
    String v = found[i] ? vals[i] : cfgDefault(CFG[i].key);
    if (CFG[i].secret) doc[String(CFG[i].key) + "_set"] = (v.length() > 0);   // a senha NUNCA vai para o navegador
    else doc[CFG[i].key] = v;
  }
  String out; serializeJson(doc, out);
  otaServer.sendHeader("Cache-Control", "no-store");
  otaServer.send(200, "application/json", out);
}

static String cfgClean(String v, size_t maxLen) {      // remove quebras de linha (evita injetar outras chaves)
  v.replace("\r", ""); v.replace("\n", ""); v.trim();
  if (v.length() > maxLen) v = v.substring(0, maxLen);
  return v;
}
static bool isNumber(const String& v, bool allowDot) {
  if (!v.length()) return false;
  int dots = 0;
  for (size_t i = 0; i < v.length(); i++) {
    char c = v[i];
    if (c == '-' && i == 0) continue;
    if (allowDot && c == '.' && ++dots == 1) continue;
    if (c < '0' || c > '9') return false;
  }
  return true;
}
// Retorna "" se tudo estiver válido; senão a mensagem de erro
static String cfgValidate(const String v[]) {
  auto at = [&](const char* key) -> const String& { for (int i = 0; i < CFG_N; i++) if (!strcmp(CFG[i].key, key)) return v[i]; return v[0]; };
  uint16_t m;
  if (!isNumber(at("lat"), true) || at("lat").toFloat() < -90 || at("lat").toFloat() > 90) return "Latitude inválida (-90 a 90)";
  if (!isNumber(at("lon"), true) || at("lon").toFloat() < -180 || at("lon").toFloat() > 180) return "Longitude inválida (-180 a 180)";
  if (!isNumber(at("dist"), false) || at("dist").toInt() < 1 || at("dist").toInt() > 1000) return "Raio de busca inválido (1 a 1000)";
  if (!(at("urlbase").startsWith("http://") || at("urlbase").startsWith("https://"))) return "URL base deve começar com http:// ou https://";
  if (!isNumber(at("flighpooling"), false) || at("flighpooling").toInt() < 5 || at("flighpooling").toInt() > 3600) return "Intervalo de voos inválido (5 a 3600 s)";
  if (!isNumber(at("wheaterpooling"), false) || at("wheaterpooling").toInt() < 60 || at("wheaterpooling").toInt() > 86400) return "Intervalo de clima inválido (60 a 86400 s)";
  if (!at("wfname").length()) return "Informe o nome da rede Wi-Fi";
  if (!at("tz").length()) return "Informe o fuso horário";
  if (!isNumber(at("screenbrightness"), false) || at("screenbrightness").toInt() < 0 || at("screenbrightness").toInt() > 255) return "Brilho inválido (0 a 255)";
  bool s = at("sleeptimer").length() > 0, w = at("wakeuptimer").length() > 0;
  if ((s && !parseClock(at("sleeptimer"), m)) || (w && !parseClock(at("wakeuptimer"), m))) return "Horários devem estar no formato HH:MM";
  if (s != w) return "Preencha os dois horários (apagar e acender) ou deixe ambos vazios";
  if (!at("otauser").length()) return "Informe o usuário de acesso";
  const String& h = at("otahost");
  if (!h.length()) return "Informe o nome na rede";
  for (size_t i = 0; i < h.length(); i++) if (!isalnum((uint8_t)h[i]) && h[i] != '-') return "Nome na rede: apenas letras, números e hífen";
  return "";
}

static void cfgPostHandler() {
  if (!otaCheckAuth()) return;
  String v[CFG_N];
  for (int i = 0; i < CFG_N; i++) {
    if (!CFG[i].secret && !otaServer.hasArg(CFG[i].key)) { otaServer.send(400, "text/plain", String("Campo ausente: ") + CFG[i].key); return; }
    v[i] = cfgClean(otaServer.arg(CFG[i].key), CFG[i].key[0] == 'u' ? 200 : 96);       // urlbase até 200
  }
  String err = cfgValidate(v);
  if (err.length()) { otaServer.send(400, "text/plain", err); return; }

  // Parâmetros adicionais enviados pela página (ek<i>/ev<i>); xs=1 indica que a seção foi enviada
  bool extrasSent = otaServer.hasArg("xs");
  String ek[32], ev[32]; bool exDone[32]; int en = 0;
  for (int i = 0; i < 32; i++) {
    String nk = "ek" + String(i), nv = "ev" + String(i);
    if (!otaServer.hasArg(nk)) continue;
    String k = cfgClean(otaServer.arg(nk), 32); String val = cfgClean(otaServer.arg(nv), 200);
    if (!k.length()) continue;
    for (size_t c = 0; c < k.length(); c++)
      if (!isalnum((uint8_t)k[c]) && k[c] != '_' && k[c] != '-') { otaServer.send(400, "text/plain", "Nome de parâmetro inválido: use letras, números, _ e -"); return; }
    String kl = k; kl.toLowerCase();
    if (cfgIndexOfKey(kl) >= 0) { otaServer.send(400, "text/plain", "O parâmetro '" + k + "' já existe no formulário acima"); return; }
    for (int j = 0; j < en; j++) { String o = ek[j]; o.toLowerCase(); if (o == kl) { otaServer.send(400, "text/plain", "Parâmetro duplicado: " + k); return; } }
    ek[en] = k; ev[en] = val; exDone[en] = false; en++;
  }

  // Reescreve o param.txt preservando comentários e chaves desconhecidas
  String old = readTextFile("/param.txt"), out;
  bool done[CFG_N]; for (int i = 0; i < CFG_N; i++) done[i] = false;
  int pos = 0;
  while (pos < (int)old.length()) {
    int nl = old.indexOf('\n', pos); if (nl < 0) nl = old.length();
    String line = old.substring(pos, nl); pos = nl + 1;
    line.replace("\r", "");
    String k = cfgLineKey(line);
    int i = k.length() ? cfgIndexOfKey(k) : -1;
    if (k.length() && i < 0 && extrasSent) {                       // chave desconhecida = parâmetro adicional
      int j = -1;
      for (int t = 0; t < en; t++) { String o = ek[t]; o.toLowerCase(); if (o == k) { j = t; break; } }
      if (j < 0 || exDone[j]) continue;                              // removido pelo usuário (ou linha repetida)
      exDone[j] = true;
      if (isSecretKey(k) && !ev[j].length()) { out += line + "\n"; continue; }   // segredo vazio = mantém
      String keyAsWritten = line.substring(0, line.indexOf('=')); keyAsWritten.trim();
      out += keyAsWritten + "=" + ev[j] + "\n";
      continue;
    }
    if (i >= 0 && !(CFG[i].secret && !v[i].length())) {           // senha vazia = mantém a linha como está
      String keyAsWritten = line.substring(0, line.indexOf('=')); keyAsWritten.trim();
      out += keyAsWritten + "=" + v[i] + "\n"; done[i] = true;
    } else {
      out += line + "\n"; if (i >= 0) done[i] = true;
    }
  }
  for (int i = 0; i < CFG_N; i++)
    if (!done[i] && !(CFG[i].secret && !v[i].length())) out += String(CFG[i].key) + "=" + v[i] + "\n";
  for (int j = 0; j < en; j++)                                        // parâmetros adicionais novos
    if (!exDone[j] && !(isSecretKey(ek[j]) && !ev[j].length())) out += ek[j] + "=" + ev[j] + "\n";

  // grava em arquivo temporário e troca (mantém param.bak como backup)
  File f = SD_MMC.open("/param.tmp", FILE_WRITE);
  if (!f) { otaServer.send(500, "text/plain", "Não foi possível abrir o cartão SD para escrita"); return; }
  size_t wrote = f.print(out); f.close();
  if (wrote != out.length()) { SD_MMC.remove("/param.tmp"); otaServer.send(500, "text/plain", "Falha ao gravar no cartão SD"); return; }
  SD_MMC.remove("/param.bak");
  if (SD_MMC.exists("/param.txt") && !SD_MMC.rename("/param.txt", "/param.bak")) { otaServer.send(500, "text/plain", "Falha ao criar backup"); return; }
  if (!SD_MMC.rename("/param.tmp", "/param.txt")) {
    SD_MMC.rename("/param.bak", "/param.txt");                     // restaura
    otaServer.send(500, "text/plain", "Falha ao salvar; configuração anterior restaurada"); return;
  }
  Serial.println("[cfg] param.txt atualizado; reiniciando");
  otaServer.send(200, "text/plain", "OK");
  gOtaPct = 101;                                                   // mostra "REINICIANDO..." na tela
  vTaskDelay(pdMS_TO_TICKS(800));
  ESP.restart();
}

// ---------------------------------------------------------------------
//  Debug monitor: /debug (página) e /debug/log?since=N (texto novo desde o deslocamento N)
// ---------------------------------------------------------------------
static void dbgPageHandler() {
  if (!otaCheckAuth()) return;
  otaServer.send_P(200, "text/html", DEBUG_PAGE);
}
static void dbgLogHandler() {
  if (!otaCheckAuth()) return;
  uint32_t since = strtoul(otaServer.arg("since").c_str(), nullptr, 10);
  String out; out.reserve(2048);
  char chunk[512]; uint32_t next = since; bool reset = false, r1 = false;
  for (int i = 0; i < 32; i++) {                       // até 16 KB por resposta
    size_t n = DBG.read(next, chunk, sizeof chunk, &next, &r1);
    if (i == 0) reset = r1;
    if (!n) break;
    out.concat(chunk, (unsigned int)n);
  }
  otaServer.sendHeader("Cache-Control", "no-store");
  otaServer.sendHeader("X-Next", String(next));
  otaServer.sendHeader("X-Reset", reset ? "1" : "0");
  otaServer.send(200, "text/plain; charset=utf-8", out);
}

static void otaTask(void*) {
  for (;;) { otaServer.handleClient(); vTaskDelay(pdMS_TO_TICKS(gOtaBusy ? 1 : 10)); }
}

static void startOta() {
  if (!P.otaPass.length()) { Serial.println("[ota] desativado: defina otapass no param.txt"); return; }
  if (!esp_ota_get_next_update_partition(NULL)) {
    Serial.println("[ota] sem particao OTA: escolha um Partition Scheme com OTA (nao use 'Huge APP')");
    return;
  }
  if (MDNS.begin(P.otaHost.c_str())) { MDNS.addService("http", "tcp", 80); Serial.printf("[ota] http://%s.local/\n", P.otaHost.c_str()); }
  Serial.printf("[ota] http://%s/  (usuario: %s)\n", WiFi.localIP().toString().c_str(), P.otaUser.c_str());
  static const char* hdrs[] = {"Content-Length"};
  otaServer.collectHeaders(hdrs, 1);
  otaServer.on("/", HTTP_GET, []() {
    if (!otaServer.authenticate(P.otaUser.c_str(), P.otaPass.c_str())) { otaServer.requestAuthentication(); return; }
    otaServer.send_P(200, "text/html", OTA_PAGE);
  });
  otaServer.on("/debug", HTTP_GET, dbgPageHandler);
  otaServer.on("/debug/log", HTTP_GET, dbgLogHandler);
  otaServer.on("/config.json", HTTP_GET, cfgGetHandler);
  otaServer.on("/config", HTTP_POST, cfgPostHandler);
  otaServer.on("/update", HTTP_POST, otaFinishHandler, otaUploadHandler);
  otaServer.begin();
  xTaskCreatePinnedToCore(otaTask, "ota", 8192, nullptr, 0, nullptr, 0);
}

static char gBootInfo[160] = "";

// Rollback OTA: o core confirma o app no boot por padrão (verifyRollbackLater() == false). Aqui a
// confirmação fica com a netTask (após clima/voo); antes disso, reinício/crash volta ao firmware anterior.
extern "C" bool verifyRollbackLater() { return true; }
static volatile bool gOtaPendingVerify = false;        // firmware novo aguardando primeiro fetch válido

static void otaValidateIfPending() {
  if (!gOtaPendingVerify) return;
  esp_ota_mark_app_valid_cancel_rollback();
  gOtaPendingVerify = false;
  Serial.println("[ota] firmware validado");
}

// Backoff por nº de falhas seguidas: 0 falhas = configurado; 1ª falha = min(configurado, 30 s), dobra a cada
// nova falha até o teto = max(configurado, teto). Clima: teto = configurado. Voo: teto = 5 min.
static uint32_t backoffMs(uint32_t fails, uint32_t cfg, uint32_t teto) {
  if (!fails) return cfg;
  uint32_t cap = cfg > teto ? cfg : teto;
  uint32_t d = cfg < 30000UL ? cfg : 30000UL;
  for (uint32_t i = 1; i < fails && d < cap; i++) d *= 2;
  return d < cap ? d : cap;
}

// ---------------------------------------------------------------------
//  Fallback de Wi-Fi: 60 s sem conexão -> wfname/wfpass do param.bak; mais 60 s -> AP de configuração
// ---------------------------------------------------------------------
static volatile bool gApMode = false;        // SoftAP ligado (o loop() lê para mostrar "AP ..." no título)
static uint32_t gWifiDownSince = 0;          // ms desde a queda do STA (0 = conectado)
static uint32_t gApStopAt = 0;               // AP desliga neste ms (0 = sem prazo; cliente ainda na página)
static uint32_t gUpSince = 0;                // ms da última conexão STA (janela de retentativa rápida)
static bool gBakLoaded = false, gHasBak = false;
static String gBakSsid, gBakPass;

// Chamado quando o STA conecta (setup e reconexões): loga a rede e zera o cache de DNS
static void wifiUp() {
  gUpSince = millis(); gDnsN = 0;
  Serial.printf("[wifi] conectado ip=%s gw=%s dns=%s rssi=%d canal=%d\n", WiFi.localIP().toString().c_str(),
                WiFi.gatewayIP().toString().c_str(), WiFi.dnsIP(0).toString().c_str(), (int)WiFi.RSSI(), (int)WiFi.channel());
}

// Retentativa: 2 min após conectar, falha de conexão (code<0) retenta em 5, 10, 20, 40 s... até o backoff normal
static uint32_t retryMs(uint32_t fails, uint32_t cfg, uint32_t teto, bool connFail) {
  uint32_t n = backoffMs(fails, cfg, teto);
  if (connFail && fails && millis() - gUpSince < 120000UL) {
    uint32_t sh = fails - 1; if (sh > 6) sh = 6;
    uint32_t f = 5000UL << sh;
    if (f < n) n = f;
  }
  return n;
}

// Lê wfname/wfpass do param.bak (uma vez). Só vale se for diferente do param.txt. Não altera arquivos.
static void loadBakWifi() {
  gBakLoaded = true;
  String txt = readTextFile("/param.bak"), ssid, pass;
  int pos = 0;
  while (pos < (int)txt.length()) {
    int nl = txt.indexOf('\n', pos); if (nl < 0) nl = txt.length();
    String line = txt.substring(pos, nl); pos = nl + 1;
    String k = cfgLineKey(line);
    line.trim();
    if (k == "wfname")      { ssid = line.substring(line.indexOf('=') + 1); ssid.trim(); }
    else if (k == "wfpass") { pass = line.substring(line.indexOf('=') + 1); pass.trim(); }
  }
  gHasBak = ssid.length() && (ssid != P.ssid || pass != P.pass);
  gBakSsid = ssid; gBakPass = pass;
  Serial.printf("[wifi] param.bak: %s\n", gHasBak ? "credenciais alternativas encontradas" : "sem credenciais alternativas");
}

// AP+STA: "PlaneRadarCompanion-XXXX" (4 hex finais do MAC). Senha = otapass se >= 8 chars; senão aberto.
// O WebServer já escuta em todas as interfaces, então a página responde em 192.168.4.1.
static void apStart() {
  String id = WiFi.macAddress().substring(12); id.replace(":", "");   // "EE:FF" -> "EEFF"
  String ssid = "PlaneRadarCompanion-" + id;
  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAP(ssid.c_str(), P.otaPass.length() >= 8 ? P.otaPass.c_str() : nullptr)) {
    Serial.println("[wifi] falha ao criar o AP"); return;
  }
  gApMode = true;
  Serial.printf("[wifi] modo AP %s\n", ssid.c_str());
}
static void apStop() {
  WiFi.softAPdisconnect(true);                 // também volta o rádio para WIFI_STA
  gApMode = false;
  Serial.println("[wifi] conectado, AP desligado");
}

// Watchdog da netTask: 60 s (maior que o pior caso de HTTP + SVG). O WDT é global: vale também para o loop().
static void netWdtConfig() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t cfg;
  cfg.timeout_ms = 60000; cfg.idle_core_mask = 0; cfg.trigger_panic = true;   // sem monitorar o idle
  if (esp_task_wdt_reconfigure(&cfg) != ESP_OK) Serial.println("[wdt] reconfigure falhou; seguindo com o padrao");
#else
  if (esp_task_wdt_init(60, true) != ESP_OK) Serial.println("[wdt] init falhou; seguindo com o padrao");
#endif
}

static void netTask(void*) {
  bool doneF = false, doneW = false;
  uint32_t lastF = 0, lastW = 0;
  uint32_t fIv = P.flightMs, wIv = P.weatherMs;          // intervalo atual de cada fetch (com backoff)
  uint32_t fFail = 0, wFail = 0;                         // falhas seguidas de cada fetch
  uint32_t otaTick = millis(), otaConnMs = 0;            // rollback: ms acumulados com Wi-Fi sem validar
  uint32_t wifiTry = 0; bool bakLogged = false;          // tentativas da queda atual; log do param.bak
  uint32_t lastReconnect = millis();                     // setup já esperou o WiFi.begin: 1ª retentativa só após 10 s
  if (esp_task_wdt_add(NULL) == ESP_OK) Serial.println("[wdt] netTask monitorada (60 s)");
  else Serial.println("[wdt] netTask nao entrou no watchdog");
  for (;;) {
    esp_task_wdt_reset();                                // alimenta o watchdog em toda volta (inclusive durante OTA)
    uint32_t tick = millis(), dt = tick - otaTick; otaTick = tick;
    if (gOtaBusy) { vTaskDelay(pdMS_TO_TICKS(200)); continue; }   // não disputa o WiFi durante a atualização
    if (WiFi.status() != WL_CONNECTED) {
      if (!gWifiDownSince) gWifiDownSince = millis();    // queda detectada agora
      uint32_t down = millis() - gWifiDownSince;
      fFail = wFail = 0;
      fIv = P.flightMs; wIv = P.weatherMs;               // sem Wi-Fi: backoff volta ao configurado
      gApStopAt = 0;                                     // queda: o AP fica no ar até reconectar
      if (gApMode && WiFi.softAPgetStationNum() > 0) { vTaskDelay(pdMS_TO_TICKS(500)); continue; }   // cliente no AP: não mexe no STA
      if (millis() - lastReconnect > (gApMode ? 60000UL : 10000UL)) {   // AP sem cliente: tenta a cada 60 s
        lastReconnect = millis();
        if (down >= 60000UL && !gBakLoaded) loadBakWifi();
        bool useBak = false;                             // 60-120 s: param.bak; depois do AP: alterna com o principal
        if (down >= 60000UL && gHasBak) {
          useBak = down < 120000UL ? true : ((wifiTry++ & 1) != 0);
          if (useBak && !bakLogged) { bakLogged = true; Serial.println("[wifi] tentando param.bak"); }
        }
        if (down >= 120000UL && !gApMode) apStart();
        String ssid = useBak ? gBakSsid : P.ssid, pass = useBak ? gBakPass : P.pass;
        WiFi.disconnect(); WiFi.begin(ssid.c_str(), pass.c_str());
      }
      vTaskDelay(pdMS_TO_TICKS(500)); continue;
    }
    if (gWifiDownSince || gApMode) {                     // voltou a conectar: zera a queda e desliga o AP
      if (gWifiDownSince) wifiUp();
      gWifiDownSince = 0; wifiTry = 0; bakLogged = false;
      if (gApMode && !gApStopAt) {
        if (WiFi.softAPgetStationNum() > 0) {            // cliente na página: mantém o AP 60 s para ver o novo IP
          gApStopAt = millis() + 60000UL;
          Serial.printf("[wifi] conectado (%s), AP desliga em 60 s\n", WiFi.localIP().toString().c_str());
        } else apStop();
      }
    }
    if (gApStopAt && (int32_t)(millis() - gApStopAt) >= 0) { gApStopAt = 0; apStop(); }   // fim dos 60 s
    // firmware novo conectado há 3 min sem nenhum clima/voo válido: volta ao anterior
    if (gOtaPendingVerify && (otaConnMs += dt) > 180000UL) {
      Serial.println("[ota] 3 min conectado sem clima/voo: voltando a versao anterior");
      esp_ota_mark_app_invalid_rollback_and_reboot();     // só retorna se o rollback não estiver disponível
      Serial.println("[ota] rollback indisponivel");
      otaConnMs = 0;
    }
    uint32_t now = millis();
    if (!doneW || now - lastW >= wIv) {
      doneW = true; lastW = now;
      gLastNetCode = 0;
      if (fetchWeather()) { wFail = 0; wIv = P.weatherMs; otaValidateIfPending(); }
      else { wFail++; wIv = retryMs(wFail, P.weatherMs, P.weatherMs, gLastNetCode < 0); Serial.printf("[net] clima falhou, proxima em %lu s\n", (unsigned long)(wIv / 1000)); }
    }
    now = millis();
    if (isSleepTime()) {
      doneF = false; fFail = 0; fIv = P.flightMs;        // descanso: backoff zerado, ao acordar começa do configurado
    } else if (!doneF || now - lastF >= fIv) {
      doneF = true; lastF = now;
      gLastNetCode = 0;
      if (fetchFlight()) { fFail = 0; fIv = P.flightMs; otaValidateIfPending(); }
      else { fFail++; fIv = retryMs(fFail, P.flightMs, 300000UL, gLastNetCode < 0); Serial.printf("[net] voo falhou, proxima em %lu s\n", (unsigned long)(fIv / 1000)); }
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

// =====================================================================
//  UI (LVGL)
// =====================================================================
static lv_obj_t *lblTitle, *lblTime, *lblDate, *lblCity, *imgWx, *lblTemp, *lblCond;
static lv_obj_t *lblAirline, *lblFlight, *lblIata, *lblType, *lblDesc, *lblOrg, *lblDst, *lblOrgLine, *lblDstLine;
static lv_obj_t *lblAltDist;
static lv_obj_t *imgLogo, *lblLogoTxt, *imgSil, *imgRadar;

// O lv_gif continua decodificando frames mesmo escondido (HIDDEN não pausa o timer dele).
// Pausamos o timer enquanto o radar não está visível para não gerar leituras/alocações à toa.
static void radarSet(bool on) {
  lv_gif_t* g = (lv_gif_t*)imgRadar;
  if (on) { lv_obj_clear_flag(imgRadar, LV_OBJ_FLAG_HIDDEN); if (g->timer) lv_timer_resume(g->timer); }
  else    { lv_obj_add_flag(imgRadar, LV_OBJ_FLAG_HIDDEN);   if (g->timer) lv_timer_pause(g->timer); }
}

// Carrega o radar.gif do SD UMA vez para a memória (PSRAM) e anima a partir dela:
// sem leituras contínuas do cartão durante horas.
static lv_img_dsc_t radarDsc;
static void loadRadarGif() {
  File rf = SD_MMC.open("/radar.gif");
  size_t n = rf ? rf.size() : 0;
  uint8_t* buf = n ? (uint8_t*)heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : nullptr;
  if (n && !buf) buf = (uint8_t*)malloc(n);
  if (buf && rf.read(buf, n) == n) {
    rf.close();
    memset(&radarDsc, 0, sizeof radarDsc);
    radarDsc.header.cf = LV_IMG_CF_RAW;
    radarDsc.data = buf; radarDsc.data_size = n;
    lv_gif_set_src(imgRadar, &radarDsc);
    Serial.printf("[radar] GIF em memoria (%u bytes); sem leituras do SD em execucao\n", (unsigned)n);
    return;
  }
  if (buf) free(buf);
  if (rf) rf.close();
  Serial.println("[radar] nao foi possivel carregar em memoria; lendo do SD");
  lv_gif_set_src(imgRadar, "S:/radar.gif");
}
static lv_img_dsc_t dscLogo, dscSil;
static uint8_t* uiLogoBuf = nullptr;
static SvgImg uiSil = {nullptr, 0, 0};

static lv_obj_t* mkLabel(lv_obj_t* p, const lv_font_t* f, uint32_t col, const char* txt) {
  lv_obj_t* l = lv_label_create(p);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
  lv_label_set_text(l, txt);
  return l;
}
static lv_obj_t* mkBox(lv_obj_t* p, int x, int y, int w, int h, int r, uint32_t bg, int bw = 0, uint32_t bc = 0) {
  lv_obj_t* o = lv_obj_create(p);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y); lv_obj_set_size(o, w, h);
  lv_obj_set_style_radius(o, r, 0);
  lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0); lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  if (bw) { lv_obj_set_style_border_width(o, bw, 0); lv_obj_set_style_border_color(o, lv_color_hex(bc), 0); }
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}
static void centerIn(lv_obj_t* l, int x, int y, int w, lv_label_long_mode_t mode) {
  lv_obj_set_width(l, w);
  lv_label_set_long_mode(l, mode);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(l, x, y);
}
static void mkCol(lv_obj_t* p, int x, int w, const char* head, lv_obj_t** val, lv_obj_t** sub, const lv_font_t* vf = F14) {
  lv_obj_t* h = mkLabel(p, F10, C_ACCENT, head);
  lv_obj_set_style_text_letter_space(h, 1, 0);
  centerIn(h, x, 6, w, LV_LABEL_LONG_CLIP);
  if (val) { *val = mkLabel(p, vf, C_WHITE, "--"); centerIn(*val, x, 19, w, LV_LABEL_LONG_CLIP); }
  if (sub) { *sub = mkLabel(p, F10, C_MUTED, ""); centerIn(*sub, x, 36, w, LV_LABEL_LONG_SCROLL_CIRCULAR); }
}

static void buildUI() {
  lv_obj_t* scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(C_SCREEN), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);   // sem isso o LVGL deixa aparecer o fundo claro padrão nas bordas/cantos
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // ---- cartão externo (gradiente) ----
  lv_obj_t* card = mkBox(scr, 4, 4, 312, 232, 16, C_CARD_TOP, 1, C_BORDER);
  lv_obj_set_style_bg_grad_color(card, lv_color_hex(C_CARD_BOT), 0);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);

  // ---- bloco hora / data (24 h) ----
  lblTitle = mkLabel(card, F10, C_ACCENT, pt("HORÁRIO LOCAL").c_str());
  lv_obj_set_style_text_letter_space(lblTitle, 2, 0);
  lv_obj_set_pos(lblTitle, 18, 9);

  lblTime = mkLabel(card, F48, C_WHITE, "--:--");
  lv_obj_set_pos(lblTime, 14, 17);

  lblDate = mkLabel(card, F12, C_MUTED, "");
  lv_obj_set_pos(lblDate, 18, 66);

  // ---- bloco clima (direita) ----
  lblCity = mkLabel(card, F10, 0xC5D6DB, "");
  lv_obj_set_width(lblCity, 150);
  lv_label_set_long_mode(lblCity, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_align(lblCity, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(lblCity, LV_ALIGN_TOP_RIGHT, -14, 9);

  lv_obj_t* row = lv_obj_create(card);            // [ícone] [temperatura] [°C]
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_column(row, 4, 0);
  lv_obj_align(row, LV_ALIGN_TOP_RIGHT, -14, 18);
  lv_obj_t* ib = lv_obj_create(row);
  lv_obj_remove_style_all(ib);
  lv_obj_set_size(ib, 24, 37);
  lv_obj_clear_flag(ib, LV_OBJ_FLAG_SCROLLABLE);
  imgWx = lv_img_create(ib);
  lv_img_set_src(imgWx, &wxDsc[2]);
  lv_obj_center(imgWx);
  lblTemp = mkLabel(row, F32, C_WHITE, "--");
  lv_obj_t* unit = mkLabel(row, F12, C_ACCENT, "\302\260C");
  lv_obj_set_style_pad_top(unit, 4, 0);

  lblCond = mkLabel(card, F10, C_MUTED, "");
  lv_obj_set_width(lblCond, 150);
  lv_label_set_long_mode(lblCond, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_align(lblCond, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(lblCond, LV_ALIGN_TOP_RIGHT, -14, 52);

  // ---- bloco voo (maior) ----
  lv_obj_t* fc = mkBox(card, 6, 86, 298, 140, 12, C_FCARD, 1, C_BORDER);

  // círculo esquerdo: logo da companhia
  lv_obj_t* cl = mkBox(fc, 6, 8, 44, 44, LV_RADIUS_CIRCLE, 0x103834, 1, 0x24655A);
  lblLogoTxt = mkLabel(cl, F12, C_ACCENT, "");
  lv_obj_center(lblLogoTxt);
  imgLogo = lv_img_create(cl);
  lv_obj_add_flag(imgLogo, LV_OBJ_FLAG_HIDDEN);

  // círculo direito: silhueta (ou radar quando não há voo)
  lv_obj_t* cr = mkBox(fc, 248, 8, 44, 44, LV_RADIUS_CIRCLE, 0x103834, 1, 0x24655A);
  imgSil = lv_img_create(cr);
  lv_obj_add_flag(imgSil, LV_OBJ_FLAG_HIDDEN);
  // O GIF é um quadrado 40x40; este contêiner redondo recorta os cantos para o radar ficar circular
  lv_obj_t* rc = lv_obj_create(cr);
  lv_obj_remove_style_all(rc);
  lv_obj_set_size(rc, 40, 40);
  lv_obj_set_style_radius(rc, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_clip_corner(rc, true, 0);
  lv_obj_clear_flag(rc, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_center(rc);
  imgRadar = lv_gif_create(rc);
  lv_obj_add_flag(imgRadar, LV_OBJ_FLAG_HIDDEN);
  lv_obj_center(imgRadar);

  // colunas VOO | AERONAVE
  mkCol(fc, 54, 90, "VOO", &lblFlight, &lblIata);
  lblAirline = mkLabel(fc, F10, C_MUTED, "");                       // nome da companhia, abaixo do código IATA do voo
  centerIn(lblAirline, 54, 48, 90, LV_LABEL_LONG_SCROLL_CIRCULAR);
  mkCol(fc, 144, 100, "AERONAVE", &lblType, &lblDesc);
  // altitude | distância, abaixo do modelo da aeronave (ex.: 500 ft | 5.6 NM)
  lblAltDist = mkLabel(fc, F10, C_ACCENT, "");
  centerIn(lblAltDist, 144, 49, 100, LV_LABEL_LONG_SCROLL_CIRCULAR);   // rola em círculo se passar da largura (antes cortava)
  mkBox(fc, 144, 8, 1, 52, 0, 0x1F4047);

  // separador + ROTA em linha própria
  mkBox(fc, 12, 64, 274, 1, 0, 0x16323A);
  lv_obj_t* rh = mkLabel(fc, F10, C_ACCENT, "ROTA");
  lv_obj_set_style_text_letter_space(rh, 1, 0);
  centerIn(rh, 0, 69, 298, LV_LABEL_LONG_CLIP);

  lblOrg = mkLabel(fc, F14, C_WHITE, "---");
  lv_obj_set_width(lblOrg, 88); lv_label_set_long_mode(lblOrg, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(lblOrg, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_pos(lblOrg, 26, 82);
  mkBox(fc, 118, 88, 4, 4, 2, C_ORANGE);
  mkBox(fc, 122, 89, 54, 1, 0, 0x2A5A55);
  mkBox(fc, 176, 88, 4, 4, 2, C_ACCENT);
  lblDst = mkLabel(fc, F14, C_WHITE, "---");
  lv_obj_set_width(lblDst, 88); lv_label_set_long_mode(lblDst, LV_LABEL_LONG_CLIP);
  lv_obj_set_pos(lblDst, 184, 82);


  // detalhe da rota (formato da imagem 2)
  lblOrgLine = mkLabel(fc, F10, C_ROUTE, "");
  lv_obj_set_width(lblOrgLine, 274); lv_label_set_long_mode(lblOrgLine, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_pos(lblOrgLine, 12, 104);
  lblDstLine = mkLabel(fc, F10, C_ROUTE, "");
  lv_obj_set_width(lblDstLine, 274); lv_label_set_long_mode(lblDstLine, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_pos(lblDstLine, 12, 119);
}

// IP exibido por 10 s no título (mesmo lugar de "CONECTANDO WIFI...") assim que conecta
static uint32_t ipShowUntil = 0;
static void showIpTitle() {
  lv_obj_set_style_text_letter_space(lblTitle, 0, 0);          // sem espaçamento extra: o IP é longo e a cidade fica logo à direita
  lv_label_set_text_fmt(lblTitle, "IP %s", WiFi.localIP().toString().c_str());
  ipShowUntil = millis() + 10000;
}
static void restoreTitle(const char* txt) {
  lv_obj_set_style_text_letter_space(lblTitle, 2, 0);
  lv_label_set_text(lblTitle, pt(txt).c_str());
  ipShowUntil = 0;
}
// IP do AP de configuração enquanto gApMode (chamado só pelo loop)
static void apTitle() {
  lv_obj_set_style_text_letter_space(lblTitle, 0, 0);
  lv_label_set_text_fmt(lblTitle, "AP %s", WiFi.softAPIP().toString().c_str());
  ipShowUntil = 0;
}

static void setStatus(const char* msg) {
  lv_label_set_text(lblTitle, pt(msg).c_str());
  lv_timer_handler();
}

// ---- relógio ----
static const char* WD[7]  = {"Domingo", "Segunda-feira", "Terça-feira", "Quarta-feira", "Quinta-feira", "Sexta-feira", "Sábado"};
static const char* MON[12] = {"Janeiro", "Fevereiro", "Março", "Abril", "Maio", "Junho", "Julho", "Agosto", "Setembro", "Outubro", "Novembro", "Dezembro"};
static int lastMin = -1, lastDay = -1;
static void clockCb(lv_timer_t*) {
  struct tm t;
  if (!getLocalTime(&t, 0) || t.tm_year < 120) return;
  if (t.tm_min == lastMin && t.tm_mday == lastDay) return;
  lastMin = t.tm_min; lastDay = t.tm_mday;
  lv_label_set_text_fmt(lblTime, "%02d:%02d", t.tm_hour, t.tm_min);
  char d[80];
  snprintf(d, sizeof d, "%s, %d de %s de %d", WD[t.tm_wday], t.tm_mday, MON[t.tm_mon], t.tm_year + 1900);
  plainInPlace(d);
  lv_label_set_text(lblDate, d);
  bool n = (t.tm_hour >= 18 || t.tm_hour < 6);                      // troca sol <-> lua na virada das 18:00 / 06:00
  if (n != wxNight) { wxNight = n; lv_img_set_src(imgWx, wxIconFor(wxIc, wxNight)); }
}

// altitude | distância (só reescreve se mudou: lv_label_set_text() reinicia a rolagem a cada consulta)
static void setAltDist(const FlightUI& f) {
  char ad[40];
  if (f.alt[0] && f.dist[0]) snprintf(ad, sizeof ad, "%s | %s", f.alt, f.dist);
  else                       snprintf(ad, sizeof ad, "%s", f.alt[0] ? f.alt : f.dist);
  if (strcmp(lv_label_get_text(lblAltDist), ad) != 0) lv_label_set_text(lblAltDist, ad);
}

static void fadeIn(lv_obj_t* o);            // definido mais abaixo (seção LVGL <-> LovyanGFX)

// ---- aplica dados vindos da rede (roda na thread do LVGL) ----
static void applyUpdates() {
  if (!xSemaphoreTake(gMtx, 0)) return;
  bool fd = gFlightDirty, wd = gWxDirty, lc = gLogoChanged, sc = gSilChanged;
  static FlightUI cur;                         // último voo aplicado (usado no fallback do logo)
  FlightUI f; memset(&f, 0, sizeof f); float temp = gTemp; int code = gWxCode;
  uint8_t* logo = nullptr; size_t ln = 0; int lw = 0, lh = 0;
  SvgImg sil = {nullptr, 0, 0};
  if (fd) { f = gFlight; if (!f.altOnly) cur = f; gFlightDirty = false; }
  if (wd) gWxDirty = false;
  if (lc) { logo = gPendLogo; ln = gPendLogoLen; lw = gPendLogoW; lh = gPendLogoH;
            gPendLogo = nullptr; gLogoChanged = false; }
  if (sc) { sil = gPendSil; gPendSil = {nullptr, 0, 0}; gSilChanged = false; }
  xSemaphoreGive(gMtx);

  if (wd) {
    Wx w = wxFromCode(code);
    char tb[12]; snprintf(tb, sizeof tb, "%.1f", (double)temp);   // (o printf do LVGL não suporta %f)
    char* dot = strchr(tb, '.'); if (dot) *dot = ',';  
    lv_label_set_text(lblTemp, tb);
    lv_label_set_text(lblCond, pt(w.d).c_str());
    wxIc = w.ic; wxNight = isNightNow();
    lv_img_set_src(imgWx, wxIconFor(wxIc, wxNight));
  }

  if (sc) {                                   // silhueta: antes do voo, pois f.showSil refere-se a este bitmap
    lv_img_set_src(imgSil, LV_SYMBOL_DUMMY);
    free(uiSil.data); uiSil = sil;
    if (sil.data) {
      fillDsc(dscSil, sil);
      lv_img_set_src(imgSil, &dscSil);
      lv_obj_clear_flag(imgSil, LV_OBJ_FLAG_HIDDEN);
      lv_obj_center(imgSil);
      fadeIn(imgSil);
    } else {
      lv_obj_add_flag(imgSil, LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (fd && f.altOnly) {
    setAltDist(f);                                          // mesma aeronave: só altitude/distância
  } else if (fd) {
    if (f.city[0]) lv_label_set_text(lblCity, f.city);      // com ou sem voo
    if (!f.valid) {
      lv_label_set_text(lblFlight, "--");  lv_label_set_text(lblIata, "");  lv_label_set_text(lblAirline, "");
      lv_label_set_text(lblType, "--");    lv_label_set_text(lblDesc, "");
      lv_label_set_text(lblOrg, "---");    lv_label_set_text(lblDst, "---");
      lv_label_set_text(lblAltDist, "");                                 // sem voo: nada de altitude/distância
      lv_label_set_text(lblOrgLine, pt("").c_str());
      lv_label_set_text(lblDstLine, pt("Rastreando...").c_str());
      lv_label_set_text(lblLogoTxt, "");
      lv_obj_add_flag(imgLogo, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(imgSil, LV_OBJ_FLAG_HIDDEN);
      radarSet(true);
    } else {
      lv_label_set_text(lblFlight, f.flight);   lv_label_set_text(lblIata, f.iata);
      lv_label_set_text(lblAirline, f.airlineName);                 // vazio quando a API não traz o nome
      lv_label_set_text(lblType, f.type);       lv_label_set_text(lblDesc, f.desc);
      lv_label_set_text(lblOrg, f.org);         lv_label_set_text(lblDst, f.dst);
      setAltDist(f);
      lv_label_set_text(lblOrgLine, f.orgLine); lv_label_set_text(lblDstLine, f.dstLine);
      // Logo e silhueta foram escondidos no estado "sem voo". Se a companhia/modelo for o mesmo, não há novo download
      // (a URL não mudou) e nada mais os mostraria de volta: reexibe aqui as imagens já carregadas.
      if (f.showLogo && uiLogoBuf) {
        if (lv_obj_has_flag(imgLogo, LV_OBJ_FLAG_HIDDEN)) Serial.println("[img] reexibindo logo ja carregado");
        lv_obj_clear_flag(imgLogo, LV_OBJ_FLAG_HIDDEN); lv_obj_center(imgLogo); lv_label_set_text(lblLogoTxt, "");
      } else {
        lv_obj_add_flag(imgLogo, LV_OBJ_FLAG_HIDDEN); lv_label_set_text(lblLogoTxt, f.airline);   // fallback até chegar o novo logo
      }
      if (f.showSil && uiSil.data) {
        if (lv_obj_has_flag(imgSil, LV_OBJ_FLAG_HIDDEN)) Serial.println("[img] reexibindo silhueta ja carregada");
        lv_obj_clear_flag(imgSil, LV_OBJ_FLAG_HIDDEN); lv_obj_center(imgSil);
      } else {
        lv_obj_add_flag(imgSil, LV_OBJ_FLAG_HIDDEN);                                             // não mostra a silhueta do modelo anterior
      }
      radarSet(false);
    }
  }

  if (lc) {                                   // logo PNG
    lv_img_cache_invalidate_src(&dscLogo);
    lv_img_set_src(imgLogo, LV_SYMBOL_DUMMY); // solta a referência antiga antes de liberar
    free(uiLogoBuf); uiLogoBuf = logo;
    if (logo) {
      SvgImg sv = {logo, lw, lh};
      fillDsc(dscLogo, sv);
      lv_img_set_src(imgLogo, &dscLogo);
      lv_img_set_zoom(imgLogo, 256);
      lv_obj_clear_flag(imgLogo, LV_OBJ_FLAG_HIDDEN);
      lv_obj_center(imgLogo);
      fadeIn(imgLogo);
      lv_label_set_text(lblLogoTxt, "");
    } else {
      lv_obj_add_flag(imgLogo, LV_OBJ_FLAG_HIDDEN);       // fallback: texto ICAO
      lv_label_set_text(lblLogoTxt, cur.valid ? cur.airline : "");
    }
  }

}

// =====================================================================
//  LVGL <-> LovyanGFX
// =====================================================================
static lv_disp_draw_buf_t drawBuf;
static lv_disp_drv_t dispDrv;

static volatile bool gPanelOff = false;     // painel em sleep (tft.sleep): o flush não envia pixels

static void flushCb(lv_disp_drv_t* d, const lv_area_t* a, lv_color_t* c) {
  if (gPanelOff) { lv_disp_flush_ready(d); return; }
  uint32_t w = a->x2 - a->x1 + 1, h = a->y2 - a->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(a->x1, a->y1, w, h);
  tft.writePixels((lgfx::rgb565_t*)&c->full, w * h);
  tft.endWrite();
  lv_disp_flush_ready(d);
}

// brilho e sleep do painel: rampa não bloqueante no loop (sem fade se screenbrightness=0)
static const uint32_t BL_FADE_MS = 1500;
static void updateBacklight() {
  static int level = P.brightness;          // brilho atual do backlight
  static bool panelOff = false;
  static uint32_t last = millis();
  uint32_t now = millis(), dt = min(now - last, BL_FADE_MS); last = now;
  bool sleeping = isSleepTime();
  if (!sleeping && panelOff) {              // acordou: painel de pé e frame pronto antes de subir o brilho
    tft.wakeup(); panelOff = false; gPanelOff = false;
    lv_obj_invalidate(lv_scr_act()); lv_refr_now(NULL);
  }
  int target = sleeping ? 0 : P.brightness;
  if (level != target) {                    // rampa linear até o alvo (entra e sai do descanso)
    int step = max(1, (int)((uint32_t)P.brightness * dt / BL_FADE_MS));
    level = level < target ? min(target, level + step) : max(target, level - step);
    tft.setBrightness(level);
  }
  if (sleeping && level == 0 && !panelOff) { tft.sleep(); panelOff = true; gPanelOff = true; }   // apagou: desliga o painel
}

// fade-in do logo e da silhueta na troca de aeronave (opa 0 -> 255 em 400 ms)
static void setOpa(void* o, int32_t v) { lv_obj_set_style_opa((lv_obj_t*)o, (lv_opa_t)v, 0); }
static void fadeIn(lv_obj_t* o) {
  lv_anim_del(o, setOpa);                   // cancela o fade anterior do mesmo objeto
  lv_obj_set_style_opa(o, LV_OPA_TRANSP, 0);
  lv_anim_t a; lv_anim_init(&a);
  lv_anim_set_var(&a, o); lv_anim_set_exec_cb(&a, setOpa);
  lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER); lv_anim_set_time(&a, 400);
  lv_anim_start(&a);
}

static void fatal(const char* msg) {
  setStatus(msg);
  for (;;) { lv_timer_handler(); delay(5); }
}

static const char* resetReasonText(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "energia ligada";
    case ESP_RST_EXT:       return "reset externo";
    case ESP_RST_SW:        return "software (ESP.restart)";
    case ESP_RST_PANIC:     return "PANICO (excecao/crash)";
    case ESP_RST_INT_WDT:   return "watchdog de interrupcao";
    case ESP_RST_TASK_WDT:  return "watchdog de task (loop travado)";
    case ESP_RST_WDT:       return "watchdog";
    case ESP_RST_DEEPSLEEP: return "saida do deep sleep";
    case ESP_RST_BROWNOUT:  return "queda de tensao (brownout)";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "desconhecido";
  }
}

void setup() {
  Serial.begin(115200);
  DBG.startCapture();                                     // buffer do monitor já desde o boot (alocado uma vez)
  snprintf(gBootInfo, sizeof gBootInfo, "\n[boot] firmware de %s %s | motivo do reinicio: %s\n", __DATE__, __TIME__, resetReasonText(esp_reset_reason()));
  Serial.print(gBootInfo);
  {   // app recém-gravado por OTA ainda não confirmado pelo bootloader
    esp_ota_img_states_t st;
    const esp_partition_t* run = esp_ota_get_running_partition();
    if (run && esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
      gOtaPendingVerify = true;
      Serial.println("[ota] firmware novo em verificacao");
    }
  }
  gMtx = xSemaphoreCreateMutex();

  //IF your screen is upsidedown uncoment the line bellow and comment the next one
  //tft.init(); tft.setRotation(1); tft.setBrightness(255); tft.fillScreen(0);
  tft.init(); tft.setRotation(3); tft.setBrightness(255); tft.fillScreen(0);
#if COLOR_TEST
  {
    int w = tft.width(), h = tft.height();
    tft.fillRect(0 * w / 4, 0, w / 4, h, tft.color888(255, 0, 0));
    tft.fillRect(1 * w / 4, 0, w / 4, h, tft.color888(0, 255, 0));
    tft.fillRect(2 * w / 4, 0, w / 4, h, tft.color888(0, 0, 255));
    tft.fillRect(3 * w / 4, 0, w / 4, h, tft.color888(255, 255, 255));
    Serial.println("[cor] barras: VERMELHO | VERDE | AZUL | BRANCO (nessa ordem)");
    delay(2500);
    tft.fillScreen(0);
  }
#endif

  lv_init();
  const size_t BUF_PX = 320 * 40;
  lv_color_t* b1 = (lv_color_t*)heap_caps_malloc(BUF_PX * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  lv_color_t* b2 = (lv_color_t*)heap_caps_malloc(BUF_PX * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  lv_disp_draw_buf_init(&drawBuf, b1, b2, BUF_PX);
  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = 320; dispDrv.ver_res = 240;
  dispDrv.flush_cb = flushCb; dispDrv.draw_buf = &drawBuf;
  lv_disp_drv_register(&dispDrv);
  lv_disp_set_bg_color(lv_disp_get_default(), lv_color_hex(C_SCREEN));   // fundo escuro também por trás da tela
  lv_disp_set_bg_opa(lv_disp_get_default(), LV_OPA_COVER);

  // ícones de clima
  for (int i = 0; i < 9; i++) {
    if (rasterSvg(WX_SVG[i], strlen(WX_SVG[i]), 24, false, lv_color_black(), wxImg[i])) fillDsc(wxDsc[i], wxImg[i]);
  }

  buildUI();
  lv_img_set_src(imgWx, &wxDsc[2]);
  setStatus("LENDO SD...");

  SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN, SD_D1_PIN, SD_D2_PIN, SD_D3_PIN);
  if (!SD_MMC.begin("/sdcard", false)) fatal("SEM CARTÃO SD");
  registerSdFs();
  if (!SD_MMC.exists("/radar.gif")) fatal("RADAR.GIF AUSENTE");
  loadRadarGif();
  radarSet(false);                 // só anima enquanto estiver visível (sem voos)
  if (!loadParams()) fatal("PARAM.TXT INVÁLIDO");
  tft.setBrightness(P.brightness);     // screenbrightness (padrão 255 se a chave não existir)

  setStatus("CONECTANDO WIFI...");
  WiFi.mode(WIFI_STA);
  gWifiDownSince = millis();                 // contagem do fallback começa no boot (zerada se conectar)
  WiFi.begin(P.ssid.c_str(), P.pass.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) { lv_timer_handler(); delay(50); }
  if (WiFi.status() == WL_CONNECTED) { gWifiDownSince = 0; wifiUp(); showIpTitle(); lv_timer_handler(); }
  else setStatus("WIFI FALHOU (RETENTANDO)");

  configTzTime(P.tz.c_str(), "pt.pool.ntp.org", "time.google.com");
  lv_timer_create(clockCb, 1000, nullptr);
  clockCb(nullptr);

  startOta();
  netWdtConfig();                  // watchdog de 60 s para a netTask (ela se registra ao iniciar)
  xTaskCreatePinnedToCore(netTask, "net", 20480, nullptr, 0, nullptr, 0);   // prio 0: divide tempo com a idle task (parse de SVG grande demora)

  enableLoopWDT();                 // se o loop() travar por >5 s (ex.: assert do LVGL em while(1)), o aparelho reinicia sozinho
}

void loop() {
  uint32_t lvNext = lv_timer_handler();   // única chamada no loop; retorna ms até o próximo timer
  applyUpdates();
  updateSleepState();
  updateBacklight();
  // diagnóstico e autorrecuperação de memória (1x por minuto)
  static uint32_t lastMem = 0; static int lowCount = 0;
  if (millis() - lastMem >= 60000) {
    lastMem = millis();
    uint32_t freeInt = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    uint32_t maxBlk  = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    Serial.printf("[mem] interna livre=%u maior_bloco=%u minimo_historico=%u psram_livre=%u\n",
                  (unsigned)freeInt, (unsigned)maxBlk, (unsigned)ESP.getMinFreeHeap(), (unsigned)ESP.getFreePsram());
    if (gOtaPct < 0 && (freeInt < MEM_MIN_FREE || maxBlk < MEM_MIN_BLOCK)) {
      if (++lowCount >= 3) { Serial.println("[mem] memoria critica por 3 min -> reiniciando"); delay(200); ESP.restart(); }
    } else lowCount = 0;
  }
  // progresso do OTA no título (lido de gOtaPct; o LVGL só é tocado aqui)
  static int lastOta = -1;
  int ota = gOtaPct;
  if (ota != lastOta) {
    lastOta = ota;
    if (ota >= 0 && ota <= 99)  lv_label_set_text_fmt(lblTitle, "ATUALIZANDO %d%%", ota);
    else if (ota == 101)        lv_label_set_text(lblTitle, "REINICIANDO...");
    else if (ota == 102)        lv_label_set_text(lblTitle, "FALHA NA ATUALIZACAO");
  }
  // volta o título quando o WiFi reconecta ou o AP liga/desliga
  static bool wasUp = true, wasAp = false;
  bool up = WiFi.status() == WL_CONNECTED;
  bool ap = gApMode;
  if (up != wasUp || ap != wasAp) {
    wasUp = up; wasAp = ap;
    if (up) showIpTitle();                 // reconectou: mostra o IP por 10 s
    else if (ap) apTitle();                // sem STA, mas o AP de configuração está no ar
    else restoreTitle("SEM WIFI");
  }
  // fim dos 10 s do IP: volta ao título normal (se não houver atualização OTA em andamento)
  if (ipShowUntil && (int32_t)(millis() - ipShowUntil) >= 0) {
    if (gOtaPct < 0) { if (ap) apTitle(); else restoreTitle("HORÁRIO LOCAL"); }
    else ipShowUntil = 0;
  }
  // espera pelo ritmo do LVGL: 1..30 ms, até 100 ms durante o descanso
  uint32_t wait = min<uint32_t>(max<uint32_t>(lvNext, 1), (isSleepTime() && gPanelOff) ? 100 : 30);   // 100 ms só com o painel já apagado (rampa fica suave)
  delay(wait);
}
