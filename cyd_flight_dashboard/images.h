// Imagens: SVG/PNG -> imagem LVGL (nanosvg) e icones de clima.
#pragma once

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

