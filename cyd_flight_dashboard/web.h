// Web: OTA pelo navegador, pagina de configuracao do param.txt e debug monitor.
#pragma once

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

