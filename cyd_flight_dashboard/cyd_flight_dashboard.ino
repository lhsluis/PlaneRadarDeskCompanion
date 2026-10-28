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

#include "config.h"
#include "images.h"
#include "web.h"
#include "net.h"
#include "ui.h"

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
