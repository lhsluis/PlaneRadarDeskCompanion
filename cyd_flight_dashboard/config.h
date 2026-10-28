// Configuracao: pinos, fontes, display (LovyanGFX), parametros (param.txt) e helpers de texto.
#pragma once

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

