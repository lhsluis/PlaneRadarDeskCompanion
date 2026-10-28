// UI (LVGL) e ponte LVGL <-> LovyanGFX.
#pragma once

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

