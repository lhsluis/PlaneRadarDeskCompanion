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
#include <time.h>
#include <ArduinoJson.h>
#define LV_CONF_INCLUDE_SIMPLE
#include <lvgl.h>
#include <LovyanGFX.hpp>
#include <PNGdec.h>          // biblioteca "PNGdec" (Larry Bank)
#include "dashboard_types.h"
#include "ota_page.h"

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
    else if (k == "sleeptimer") P.sleepTimer = v;
    else if (k == "wakeuptimer") P.wakeupTimer = v;
    else if (k == "wheaterpooling" || k == "weatherpooling") P.weatherMs = max(60L, v.toInt()) * 1000UL;
    else if (k == "flighpooling" || k == "flightpooling")    P.flightMs  = max(5L,  v.toInt()) * 1000UL;
  }
  f.close();
  return P.ssid.length() && P.urlbase.length();
}

static bool isSleepTime() {
  uint16_t sleepAt, wakeAt;
  if (!parseClock(P.sleepTimer, sleepAt) || !parseClock(P.wakeupTimer, wakeAt) || sleepAt == wakeAt)
    return false;
  struct tm now;
  if (!getLocalTime(&now, 0)) return false;
  int current = now.tm_hour * 60 + now.tm_min;
  if (sleepAt < wakeAt) return current >= sleepAt && current < wakeAt;
  return current >= sleepAt || current < wakeAt;
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
  NSVGimage* im = nsvgParse(buf, "px", 96.0f);
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
  nsvgRasterize(rs, im, 0, 0, sc, rgba, ow, oh, ow * 4);
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
static const char* WX_SVG[7] = {
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
};
static SvgImg wxImg[7];
static lv_img_dsc_t wxDsc[7];

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
struct FlightUI {
  bool valid;
  char flight[16], iata[16], airline[8], type[8], desc[48];
  char org[16], dst[16], orgLine[112], dstLine[112], city[64];
  char alt[16], dist[16];            // altitude (ft) e distância (NM) já formatadas
};
static SemaphoreHandle_t gMtx;
static FlightUI gFlight;  static bool gFlightDirty = false;
static uint8_t* gPendLogo = nullptr; static size_t gPendLogoLen = 0; static int gPendLogoW = 0, gPendLogoH = 0;
static bool gLogoChanged = false;
static SvgImg gPendSil = {nullptr, 0, 0}; static bool gSilChanged = false;
static int gTemp = 0, gWxCode = -1; static bool gWxDirty = false;

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
static bool gHttpTooBig = false;   // true quando o servidor respondeu 200 mas o arquivo excede o limite
static bool httpGet(const String& url, String& out, size_t maxLen) {
  gHttpTooBig = false;
  WiFiClientSecure sc; WiFiClient pc; HTTPClient http;
  http.setConnectTimeout(6000); http.setTimeout(8000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  bool ok;
  if (url.startsWith("https")) { sc.setInsecure(); ok = http.begin(sc, url); }
  else                         { ok = http.begin(pc, url); }
  if (!ok) return false;
  int code = http.GET();
  Serial.printf("[http] %d  %s  (len=%d, heap=%u)\n", code, url.c_str(), http.getSize(), (unsigned)ESP.getFreeHeap());
  if (code != 200) { http.end(); return false; }
  int sz = http.getSize();
  if (sz > 0 && (size_t)sz > maxLen) {
    Serial.printf("[http] arquivo de %d bytes excede o limite de %u\n", sz, (unsigned)maxLen);
    gHttpTooBig = true; http.end(); return false;
  }
  out = http.getString();
  http.end();
  return out.length() > 0 && out.length() <= maxLen;
}

// A API devolve links de imagem em http:// mesmo servindo a API em https://.
// O HTTPClient não segue redirecionamento http->https com cliente não-seguro; por isso tentamos https primeiro.
static bool httpGetImg(const String& url, String& out, size_t maxLen) {
  if (url.startsWith("http://") && P.urlbase.startsWith("https://")) {
    String up = "https://" + url.substring(7);
    if (httpGet(up, out, maxLen)) return true;
    if (gHttpTooBig) return false;            // o arquivo existe mas é grande demais: http não ajuda
    Serial.println("[img] https falhou, tentando http original");
  }
  return httpGet(url, out, maxLen);
}

// Baixa para um buffer terminado em NUL, usando PSRAM quando existir (não duplica na RAM).
// Só funciona com Content-Length conhecido.
static bool httpGetBuf(const String& url, uint8_t** out, size_t* outLen, size_t maxLen) {
  *out = nullptr; *outLen = 0; gHttpTooBig = false;
  WiFiClientSecure sc; WiFiClient pc; HTTPClient http;
  http.setConnectTimeout(6000); http.setTimeout(12000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  bool ok;
  if (url.startsWith("https")) { sc.setInsecure(); ok = http.begin(sc, url); }
  else                         { ok = http.begin(pc, url); }
  if (!ok) return false;
  int code = http.GET();
  int sz = http.getSize();
  Serial.printf("[http] %d  %s  (len=%d, heap=%u, psram=%u)\n", code, url.c_str(), sz,
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
  if (code != 200 || sz <= 0) { http.end(); return false; }
  if ((size_t)sz > maxLen) {
    Serial.printf("[http] arquivo de %d bytes excede o limite de %u\n", sz, (unsigned)maxLen);
    gHttpTooBig = true; http.end(); return false;
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
  }
  http.end();
  if (got != (size_t)sz) { Serial.printf("[http] leitura incompleta %u/%d\n", (unsigned)got, sz); free(buf); return false; }
  buf[sz] = 0; *out = buf; *outLen = got;
  return true;
}
static bool httpGetImgBuf(const String& url, uint8_t** out, size_t* outLen, size_t maxLen) {
  if (url.startsWith("http://") && P.urlbase.startsWith("https://")) {
    String up = "https://" + url.substring(7);
    if (httpGetBuf(up, out, outLen, maxLen)) return true;
    if (gHttpTooBig) return false;
    Serial.println("[img] https falhou, tentando http original");
  }
  return httpGetBuf(url, out, outLen, maxLen);
}

static uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

static String lastLogoUrl, lastSilUrl;
static bool silLoaded = false;

static void fetchFlight() {
  String url = P.urlbase + "/api/nearest-aircraft?lat=" + String(P.lat, 6) + "&lon=" + String(P.lon, 6) + "&dist=" + String(P.dist);
  String body;
  if (!httpGet(url, body, 32768)) return;              // mantém o último dado na tela
  JsonDocument doc;
  if (deserializeJson(doc, body)) { Serial.println("JSON invalido"); return; }

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
  if (ac.isNull()) {
    f.valid = false;
    xSemaphoreTake(gMtx, portMAX_DELAY); gFlight = f; gFlightDirty = true; xSemaphoreGive(gMtx);
    return;
  }
  f.valid = true;
  JsonObject fi  = doc["flight_info"].as<JsonObject>();
  JsonObject org = fi["origin"].as<JsonObject>();
  JsonObject dst = fi["destination"].as<JsonObject>();

  strlcpy(f.flight,  S(ac["flight"], "---"), sizeof f.flight);  trimInPlace(f.flight);
  strlcpy(f.iata,    S(fi["callsign_iata"], ""), sizeof f.iata);
  strlcpy(f.airline, S(fi["airline"]["icao"], ""), sizeof f.airline);
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
    if (ab.is<const char*>()) strlcpy(f.alt, "Solo", sizeof f.alt);          // alt_baro vem como "ground" em solo
    else if (ab.is<float>()) {
      int ft = (int)ab.as<float>();
      if (ft >= 1000)   snprintf(f.alt, sizeof f.alt, "%d.%03d ft", ft / 1000, ft % 1000);
      else if (ft >= 0) snprintf(f.alt, sizeof f.alt, "%d ft", ft);
    }
    JsonVariantConst dv = ac["dst"];
    if (dv.is<float>()) snprintf(f.dist, sizeof f.dist, "%.1f NM", dv.as<float>());
  }

  if (org.isNull() || !orgCode[0]) strlcpy(f.orgLine, "Rota desconhecida", sizeof f.orgLine);
  else snprintf(f.orgLine, sizeof f.orgLine, "%s" SEP "%s" SEP "%s" SEP "%s", f.org,
                S(org["name"], ""), S(org["municipality"], ""), S(org["country_name"], ""));
  if (dst.isNull() || !dstCode[0]) strlcpy(f.dstLine, "Indisponível", sizeof f.dstLine);
  else snprintf(f.dstLine, sizeof f.dstLine, "%s" SEP "%s" SEP "%s" SEP "%s", f.dst,
                S(dst["name"], ""), S(dst["municipality"], ""), S(dst["country_name"], ""));

  plainInPlace(f.flight); plainInPlace(f.iata); plainInPlace(f.desc);
  plainInPlace(f.orgLine); plainInPlace(f.dstLine);

  // Prefere as versões leves servidas pela API (se existirem); senão usa os campos originais.
  String logoUrl = S(doc["airline_logo_small"], S(doc["airline_logo"], ""));
  String silUrl  = S(doc["silhouette_png"], S(doc["silhouette"], ""));

  xSemaphoreTake(gMtx, portMAX_DELAY); gFlight = f; gFlightDirty = true; xSemaphoreGive(gMtx);

  Serial.printf("[flight] %s | logo=%s | sil=%s\n", f.flight, logoUrl.c_str(), silUrl.c_str());
  static int logoTries = 0, silTries = 0;

  // ---- logo da companhia (PNG, reduzido durante a decodificação) — só baixa quando muda ----
  if (logoUrl != lastLogoUrl) {
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
    if (ok || !logoUrl.length() || ++logoTries >= 3) { lastLogoUrl = logoUrl; logoTries = 0; }
  }

  // ---- silhueta (SVG -> bitmap); se falhar, usa a silhueta genérica ----
  if (!silLoaded || silUrl != lastSilUrl) {
    SvgImg im = {nullptr, 0, 0}; bool remoteOk = false;
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
        if (gHttpTooBig) silTries = 99;           // arquivo grande demais: não adianta repetir; usa a genérica
      }
    }
    if (!remoteOk) rasterSvg(GENERIC_SIL_SVG, strlen(GENERIC_SIL_SVG), ICON_PX, true, lv_color_hex(C_ACCENT), im);
    pushSil(im);
    silLoaded = true;
    if (remoteOk || !silUrl.length() || ++silTries >= 3) { lastSilUrl = silUrl; silTries = 0; }   // até 3 tentativas
  }
}

static void fetchWeather() {
  String url = String("https://api.open-meteo.com/v1/forecast?latitude=") + String(P.lat, 4) +
               "&longitude=" + String(P.lon, 4) + "&current=temperature_2m,weather_code";
  String body;
  if (!httpGet(url, body, 8192)) return;
  JsonDocument doc;
  if (deserializeJson(doc, body)) return;
  JsonObject c = doc["current"].as<JsonObject>();
  if (c.isNull()) return;
  xSemaphoreTake(gMtx, portMAX_DELAY);
  gTemp = (int)lroundf(c["temperature_2m"].as<float>());
  gWxCode = c["weather_code"] | 3;
  gWxDirty = true;
  xSemaphoreGive(gMtx);
}

// =====================================================================
//  OTA pelo navegador:  http://<ip-ou-otahost.local>/   (usuário/senha do param.txt)
//  Roda numa task própria (core 0, prioridade 0). A UI só lê gOtaPct para mostrar o progresso.
// =====================================================================
static volatile bool gOtaBusy = false;     // pausa as buscas de rede enquanto grava o firmware
static volatile int  gOtaPct  = -1;        // -1 inativo | 0..99 progresso | 101 concluído | 102 erro
static WebServer otaServer(80);
static bool otaAuthed = false;
static bool otaUpdateReady = false;
static size_t otaBytes = 0, otaTotal = 0;

// (a página HTML está em ota_page.h)

static void otaUploadHandler() {
  HTTPUpload& up = otaServer.upload();
  if (up.status == UPLOAD_FILE_START) {
    otaAuthed = otaServer.authenticate(P.otaUser.c_str(), P.otaPass.c_str());
    if (!otaAuthed) return;
    gOtaBusy = true; gOtaPct = 0; otaBytes = 0;
    otaUpdateReady = false;
    otaTotal = otaServer.header("Content-Length").toInt();
    Serial.printf("[ota] inicio: %s (%u bytes)\n", up.filename.c_str(), (unsigned)otaTotal);
    otaUpdateReady = Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
    if (!otaUpdateReady) {
      Update.printError(Serial);
      gOtaPct = 102;
      gOtaBusy = false;
    }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!otaAuthed || !otaUpdateReady) return;
    if (Update.write(up.buf, up.currentSize) != up.currentSize) {
      Update.printError(Serial);
      otaUpdateReady = false;
      gOtaPct = 102;
      gOtaBusy = false;
      Update.abort();
      return;
    }
    otaBytes += up.currentSize;
    if (otaTotal > 0) { int p = (int)((uint64_t)otaBytes * 100 / otaTotal); gOtaPct = p > 99 ? 99 : p; }
  } else if (up.status == UPLOAD_FILE_END) {
    if (!otaAuthed || !otaUpdateReady) return;
    if (Update.end(true)) Serial.printf("[ota] concluido: %u bytes\n", (unsigned)up.totalSize);
    else {
      Update.printError(Serial);
      otaUpdateReady = false;
      gOtaPct = 102;
      gOtaBusy = false;
    }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (otaAuthed && otaUpdateReady) { Update.abort(); Serial.println("[ota] abortado"); }
    otaUpdateReady = false;
    gOtaBusy = false; gOtaPct = -1;
  }
}

static void otaFinishHandler() {
  if (!otaAuthed) { otaServer.requestAuthentication(); return; }
  bool ok = otaAuthed && otaUpdateReady && !Update.hasError();
  otaServer.sendHeader("Connection", "close");
  otaServer.send(ok ? 200 : 500, "text/plain", ok ? "OK" : Update.errorString());
  if (ok) { gOtaPct = 101; vTaskDelay(pdMS_TO_TICKS(800)); ESP.restart(); }
  else    { gOtaPct = 102; gOtaBusy = false; otaUpdateReady = false; }
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
  {"tz", nullptr, false}, {"sleeptimer", nullptr, false}, {"wakeuptimer", nullptr, false},
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
  if (k == "sleeptimer") return P.sleepTimer;
  if (k == "wakeuptimer") return P.wakeupTimer;
  if (k == "otauser") return P.otaUser;
  if (k == "otahost") return P.otaHost;
  return "";
}

static void cfgGetHandler() {
  if (!otaCheckAuth()) return;
  String txt = readTextFile("/param.txt");
  String vals[CFG_N]; bool found[CFG_N];
  for (int i = 0; i < CFG_N; i++) found[i] = false;
  int pos = 0;
  while (pos <= (int)txt.length()) {
    int nl = txt.indexOf('\n', pos); if (nl < 0) nl = txt.length();
    String line = txt.substring(pos, nl); pos = nl + 1;
    String k = cfgLineKey(line); if (!k.length()) continue;
    int i = cfgIndexOfKey(k); if (i < 0) continue;
    String v = line.substring(line.indexOf('=') + 1); v.trim();
    vals[i] = v; found[i] = true;
  }
  JsonDocument doc;
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
  int dots = 0; bool digit = false;
  for (size_t i = 0; i < v.length(); i++) {
    char c = v[i];
    if (c == '-' && i == 0) continue;
    if (allowDot && c == '.' && ++dots == 1) continue;
    if (c < '0' || c > '9') return false;
    digit = true;
  }
  return digit;
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
  bool s = at("sleeptimer").length() > 0, w = at("wakeuptimer").length() > 0;
  if ((s && !parseClock(at("sleeptimer"), m)) || (w && !parseClock(at("wakeuptimer"), m))) return "Horários devem estar no formato HH:MM";
  if (s != w) return "Preencha os dois horários (apagar e acender) ou deixe ambos vazios";
  if (!at("otauser").length()) return "Informe o usuário de acesso";
  const String& h = at("otahost");
  if (!h.length() || h.length() > 32) return "Nome na rede deve ter de 1 a 32 caracteres";
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
    if (i >= 0 && !(CFG[i].secret && !v[i].length())) {           // senha vazia = mantém a linha como está
      String keyAsWritten = line.substring(0, line.indexOf('=')); keyAsWritten.trim();
      out += keyAsWritten + "=" + v[i] + "\n"; done[i] = true;
    } else {
      out += line + "\n"; if (i >= 0) done[i] = true;
    }
  }
  for (int i = 0; i < CFG_N; i++)
    if (!done[i] && !(CFG[i].secret && !v[i].length())) out += String(CFG[i].key) + "=" + v[i] + "\n";

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
  otaServer.on("/config.json", HTTP_GET, cfgGetHandler);
  otaServer.on("/config", HTTP_POST, cfgPostHandler);
  otaServer.on("/update", HTTP_POST, otaFinishHandler, otaUploadHandler);
  otaServer.begin();
  xTaskCreatePinnedToCore(otaTask, "ota", 8192, nullptr, 0, nullptr, 0);
}

static void netTask(void*) {
  bool doneF = false, doneW = false;
  uint32_t lastF = 0, lastW = 0, lastReconnect = 0;
  for (;;) {
    if (gOtaBusy) { vTaskDelay(pdMS_TO_TICKS(200)); continue; }   // não disputa o WiFi durante a atualização
    if (WiFi.status() != WL_CONNECTED) {
      if (millis() - lastReconnect > 10000) { lastReconnect = millis(); WiFi.disconnect(); WiFi.begin(P.ssid.c_str(), P.pass.c_str()); }
      vTaskDelay(pdMS_TO_TICKS(500)); continue;
    }
    uint32_t now = millis();
    if (!doneW || now - lastW >= P.weatherMs) { doneW = true; lastW = now; fetchWeather(); }
    now = millis();
    if (isSleepTime()) {
      doneF = false;
    } else if (!doneF || now - lastF >= P.flightMs) {
      doneF = true; lastF = now; fetchFlight();
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

// =====================================================================
//  UI (LVGL)
// =====================================================================
static lv_obj_t *lblTitle, *lblTime, *lblDate, *lblCity, *imgWx, *lblTemp, *lblCond;
static lv_obj_t *lblFlight, *lblIata, *lblType, *lblDesc, *lblOrg, *lblDst, *lblOrgLine, *lblDstLine;
static lv_obj_t *imgLogo, *lblLogoTxt, *imgSil, *imgRadar;
static lv_obj_t *lblAlt, *lblDist;
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
  centerIn(h, x, 8, w, LV_LABEL_LONG_CLIP);
  if (val) { *val = mkLabel(p, vf, C_WHITE, "--"); centerIn(*val, x, 22, w, LV_LABEL_LONG_CLIP); }
  if (sub) { *sub = mkLabel(p, F10, C_MUTED, ""); centerIn(*sub, x, 40, w, LV_LABEL_LONG_SCROLL_CIRCULAR); }
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
  lv_obj_set_pos(lblDate, 18, 62);

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
  mkCol(fc, 144, 100, "AERONAVE", &lblType, &lblDesc);
  mkBox(fc, 144, 8, 1, 44, 0, 0x1F4047);

  // separador + ROTA em linha própria
  mkBox(fc, 12, 60, 274, 1, 0, 0x16323A);
  lv_obj_t* rh = mkLabel(fc, F10, C_ACCENT, "ROTA");
  lv_obj_set_style_text_letter_space(rh, 1, 0);
  centerIn(rh, 0, 67, 298, LV_LABEL_LONG_CLIP);

  lblOrg = mkLabel(fc, F14, C_WHITE, "---");
  lv_obj_set_width(lblOrg, 88); lv_label_set_long_mode(lblOrg, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(lblOrg, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_pos(lblOrg, 26, 80);
  mkBox(fc, 118, 86, 4, 4, 2, C_ORANGE);
  mkBox(fc, 122, 87, 54, 1, 0, 0x2A5A55);
  mkBox(fc, 176, 86, 4, 4, 2, C_ACCENT);
  lblDst = mkLabel(fc, F14, C_WHITE, "---");
  lv_obj_set_width(lblDst, 88); lv_label_set_long_mode(lblDst, LV_LABEL_LONG_CLIP);
  lv_obj_set_pos(lblDst, 184, 80);

  // altitude à esquerda da rota e distância à direita (fonte menor, mesma linha dos códigos)
  lblAlt = mkLabel(fc, F10, C_MUTED, "");
  lv_obj_set_width(lblAlt, 66); lv_label_set_long_mode(lblAlt, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(lblAlt, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_pos(lblAlt, 6, 82);
  lblDist = mkLabel(fc, F10, C_MUTED, "");
  lv_obj_set_width(lblDist, 66); lv_label_set_long_mode(lblDist, LV_LABEL_LONG_CLIP);
  lv_obj_set_pos(lblDist, 226, 82);

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
}

// ---- aplica dados vindos da rede (roda na thread do LVGL) ----
static void applyUpdates() {
  if (!xSemaphoreTake(gMtx, 0)) return;
  bool fd = gFlightDirty, wd = gWxDirty, lc = gLogoChanged, sc = gSilChanged;
  static FlightUI cur;                         // último voo aplicado (usado no fallback do logo)
  FlightUI f; memset(&f, 0, sizeof f); int temp = gTemp, code = gWxCode;
  uint8_t* logo = nullptr; size_t ln = 0; int lw = 0, lh = 0;
  SvgImg sil = {nullptr, 0, 0};
  if (fd) { f = gFlight; cur = f; gFlightDirty = false; }
  if (wd) gWxDirty = false;
  if (lc) { logo = gPendLogo; ln = gPendLogoLen; lw = gPendLogoW; lh = gPendLogoH;
            gPendLogo = nullptr; gLogoChanged = false; }
  if (sc) { sil = gPendSil; gPendSil = {nullptr, 0, 0}; gSilChanged = false; }
  xSemaphoreGive(gMtx);

  if (wd) {
    Wx w = wxFromCode(code);
    lv_label_set_text_fmt(lblTemp, "%d", temp);
    lv_label_set_text(lblCond, pt(w.d).c_str());
    lv_img_set_src(imgWx, &wxDsc[w.ic]);
  }

  if (fd) {
    if (f.city[0]) lv_label_set_text(lblCity, f.city);      // com ou sem voo
    if (!f.valid) {
      lv_label_set_text(lblFlight, "--");  lv_label_set_text(lblIata, "");
      lv_label_set_text(lblType, "--");    lv_label_set_text(lblDesc, "");
      lv_label_set_text(lblOrg, "---");    lv_label_set_text(lblDst, "---");
      lv_label_set_text(lblAlt, "");       lv_label_set_text(lblDist, "");   // sem voo: nada de altitude/distância
      lv_label_set_text(lblOrgLine, pt("Rastreando...").c_str());
      lv_label_set_text(lblDstLine, pt("Rota indisponível").c_str());
      lv_label_set_text(lblLogoTxt, "");
      lv_obj_add_flag(imgLogo, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(imgSil, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(imgRadar, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_label_set_text(lblFlight, f.flight);   lv_label_set_text(lblIata, f.iata);
      lv_label_set_text(lblType, f.type);       lv_label_set_text(lblDesc, f.desc);
      lv_label_set_text(lblOrg, f.org);         lv_label_set_text(lblDst, f.dst);
      lv_label_set_text(lblAlt,  strlen(f.org) <= 4 ? f.alt  : "");   // com "Indisponível" não há espaço ao lado
      lv_label_set_text(lblDist, strlen(f.dst) <= 4 ? f.dist : "");
      lv_label_set_text(lblOrgLine, f.orgLine); lv_label_set_text(lblDstLine, f.dstLine);
      if (lv_obj_has_flag(imgLogo, LV_OBJ_FLAG_HIDDEN)) lv_label_set_text(lblLogoTxt, f.airline);
      lv_obj_add_flag(imgRadar, LV_OBJ_FLAG_HIDDEN);
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
      lv_label_set_text(lblLogoTxt, "");
    } else {
      lv_obj_add_flag(imgLogo, LV_OBJ_FLAG_HIDDEN);       // fallback: texto ICAO
      lv_label_set_text(lblLogoTxt, cur.valid ? cur.airline : "");
    }
  }

  if (sc) {                                   // silhueta
    lv_img_set_src(imgSil, LV_SYMBOL_DUMMY);
    free(uiSil.data); uiSil = sil;
    if (sil.data) {
      fillDsc(dscSil, sil);
      lv_img_set_src(imgSil, &dscSil);
      lv_obj_clear_flag(imgSil, LV_OBJ_FLAG_HIDDEN);
      lv_obj_center(imgSil);
    } else {
      lv_obj_add_flag(imgSil, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

// =====================================================================
//  LVGL <-> LovyanGFX
// =====================================================================
static lv_disp_draw_buf_t drawBuf;
static lv_disp_drv_t dispDrv;

static void flushCb(lv_disp_drv_t* d, const lv_area_t* a, lv_color_t* c) {
  uint32_t w = a->x2 - a->x1 + 1, h = a->y2 - a->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(a->x1, a->y1, w, h);
  tft.writePixels((lgfx::rgb565_t*)&c->full, w * h);
  tft.endWrite();
  lv_disp_flush_ready(d);
}

static void fatal(const char* msg) {
  setStatus(msg);
  for (;;) { lv_timer_handler(); delay(5); }
}

void setup() {
  Serial.begin(115200);
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
  for (int i = 0; i < 7; i++) {
    if (rasterSvg(WX_SVG[i], strlen(WX_SVG[i]), 24, false, lv_color_black(), wxImg[i])) fillDsc(wxDsc[i], wxImg[i]);
  }

  buildUI();
  lv_img_set_src(imgWx, &wxDsc[2]);
  setStatus("LENDO SD...");

  SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN, SD_D1_PIN, SD_D2_PIN, SD_D3_PIN);
  if (!SD_MMC.begin("/sdcard", false)) fatal("SEM CARTÃO SD");
  registerSdFs();
  if (!SD_MMC.exists("/radar.gif")) fatal("RADAR.GIF AUSENTE");
  lv_gif_set_src(imgRadar, "S:/radar.gif");
  if (!loadParams()) fatal("PARAM.TXT INVÁLIDO");

  setStatus("CONECTANDO WIFI...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(P.ssid.c_str(), P.pass.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) { lv_timer_handler(); delay(50); }
  if (WiFi.status() == WL_CONNECTED) { showIpTitle(); lv_timer_handler(); }
  else setStatus("WIFI FALHOU (RETENTANDO)");

  configTzTime(P.tz.c_str(), "pt.pool.ntp.org", "time.google.com");
  lv_timer_create(clockCb, 1000, nullptr);
  clockCb(nullptr);

  startOta();
  xTaskCreatePinnedToCore(netTask, "net", 20480, nullptr, 0, nullptr, 0);   // prio 0: divide tempo com a idle task (parse de SVG grande demora)
}

void loop() {
  lv_timer_handler();
  applyUpdates();
  static bool wasSleeping = false;
  bool sleeping = isSleepTime();
  if (sleeping != wasSleeping) {
    wasSleeping = sleeping;
    tft.setBrightness(sleeping ? 1 : 255);
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
  // volta o título quando o WiFi reconecta
  static bool wasUp = true;
  bool up = WiFi.status() == WL_CONNECTED;
  if (up != wasUp) {
    wasUp = up;
    if (up) showIpTitle();                 // reconectou: mostra o IP por 10 s
    else    restoreTitle("SEM WIFI");
  }
  // fim dos 10 s do IP: volta ao título normal (se não houver atualização OTA em andamento)
  if (ipShowUntil && (int32_t)(millis() - ipShowUntil) >= 0) {
    if (gOtaPct < 0) restoreTitle("HORÁRIO LOCAL"); else ipShowUntil = 0;
  }
  delay(5);
}
