// Rede: dados compartilhados, HTTP, busca de voo/clima, rollback OTA, fallback de Wi-Fi e netTask.
#pragma once

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

// Escolhe a sessão: se allow e o host for o do P.urlbase, usa a persistente (pers = true); senão a local 'loc'.
// Wi-Fi caído ou host diferente do último descarta a persistente antes.
static HttpSess& pickSess(const String& url, bool allow, HttpSess& loc, bool& pers) {
  if (WiFi.status() != WL_CONNECTED) keepDrop();      // Wi-Fi caiu: conexão antiga não vale mais
  String host = urlHost(url);
  bool keep = allow && host == urlHost(P.urlbase);
  if (keep && gKeep && gKeepHost != host) keepDrop();  // host diferente do último: recria
  if (keep && !gKeep) { gKeep = new HttpSess(); gKeepHost = host; }
  pers = keep && gKeep;
  return pers ? *gKeep : loc;
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

// Imagens em https no host do P.urlbase reusam a conexão persistente; falha (ou corpo não lido) descarta a conexão.
static bool httpGet(const String& url, String& out, size_t maxLen) {
  HttpSess loc; bool pers;
  HttpSess& s = pickSess(url, url.startsWith("https") && P.urlbase.startsWith("https"), loc, pers);
  int code = httpOpen(s.http, s.sc, s.pc, url, gHttpReadMs, maxLen, pers);
  int sz = s.http.getSize();
  bool ok = false;
  if (code == 200 && !gHttpTooBig) {
    out = s.http.getString();
    ok = out.length() > 0 && out.length() <= maxLen && (sz <= 0 || (int)out.length() == sz);   // leitura curta: conexão suja
  }
  s.http.end();
  if (!ok && pers) keepDrop();
  return ok;
}

// Baixa para um buffer terminado em NUL, usando PSRAM quando existir (não duplica na RAM).
// Só funciona com Content-Length conhecido (sem ele, lê a String inteira).
// Mesma regra de sessão de httpGet (https no host do P.urlbase = persistente).
static bool httpGetBuf(const String& url, uint8_t** out, size_t* outLen, size_t maxLen) {
  *out = nullptr; *outLen = 0;
  HttpSess loc; bool pers;
  HttpSess& s = pickSess(url, url.startsWith("https") && P.urlbase.startsWith("https"), loc, pers);
  int code = httpOpen(s.http, s.sc, s.pc, url, 10000, maxLen, pers);
  bool ok = false;
  if (code == 200 && !gHttpTooBig) {
    int sz = s.http.getSize();
    if (sz <= 0) {                                    // sem Content-Length (resposta em blocos/chunked): getString() decodifica
      String body = s.http.getString();
      size_t bl = body.length();
      if (bl > maxLen) { Serial.printf("[http] arquivo de %u bytes excede o limite de %u\n", (unsigned)bl, (unsigned)maxLen); gHttpTooBig = true; }
      else if (bl) {
        uint8_t* b = (uint8_t*)heap_caps_malloc(bl + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!b) b = (uint8_t*)malloc(bl + 1);
        if (!b) Serial.println("[http] sem memoria para o arquivo");
        else {
          memcpy(b, body.c_str(), bl); b[bl] = 0; *out = b; *outLen = bl; ok = true;
          Serial.printf("[http] resposta sem Content-Length: %u bytes\n", (unsigned)bl);
        }
      }
    } else {
      uint8_t* buf = (uint8_t*)heap_caps_malloc((size_t)sz + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (!buf) buf = (uint8_t*)malloc((size_t)sz + 1);
      if (!buf) Serial.println("[http] sem memoria para o arquivo");
      else {
        WiFiClient* st = s.http.getStreamPtr();
        size_t got = 0; uint32_t last = millis();
        while ((s.http.connected() || st->available()) && got < (size_t)sz && millis() - last < 10000) {
          size_t av = st->available();
          if (av) { size_t n = st->readBytes(buf + got, min(av, (size_t)sz - got)); got += n; last = millis(); }
          else vTaskDelay(1);
          esp_task_wdt_reset();                        // download longo: alimenta o watchdog da netTask
        }
        if (got != (size_t)sz) { Serial.printf("[http] leitura incompleta %u/%d\n", (unsigned)got, sz); free(buf); }
        else { buf[sz] = 0; *out = buf; *outLen = got; ok = true; }
      }
    }
  }
  s.http.end();
  if (!ok && pers) keepDrop();                        // falha: corpo não lido ou incompleto, descarta a conexão
  return ok;
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
  HttpSess loc; bool pers;
  HttpSess& s = pickSess(url, true, loc, pers);
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

