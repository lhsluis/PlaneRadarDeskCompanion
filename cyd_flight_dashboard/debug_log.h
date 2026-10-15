#pragma once
// Espelha tudo que o sketch escreve em Serial para um buffer circular na memória,
// consultado pela página "Debug monitor" (/debug). A saída normal do Serial continua funcionando.
//
// Como funciona: a classe DebugOut repassa os bytes ao Serial real e também os guarda no buffer
// (com o tempo desde o boot no começo de cada linha). No final deste arquivo, "Serial" passa a
// significar DBG para o resto do sketch, então os Serial.printf()/println() existentes não mudam.
//
// Limites: só aparece o que o próprio sketch imprime via Serial.* (não as mensagens internas do
// core ESP32, nem o relatório de um crash). O buffer é apagado quando o aparelho reinicia.
#include <Arduino.h>
#include <esp_heap_caps.h>

class DebugOut : public Print {
 public:
  // begin() só abre o Serial real: o buffer do debug fica desligado (nada é alocado nem gravado)
  // até startCapture() ser chamado, para não pesar na inicialização.
  void begin(unsigned long baud) { Serial.begin(baud); }
  bool capturing() const { return _buf != nullptr; }
  void startCapture() {
    if (!_buf) {
      bool ps = psramFound();
      size_t cap = ps ? 32768 : 8192;
      _buf = (uint8_t*)heap_caps_malloc(cap, ps ? (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : MALLOC_CAP_8BIT);
      if (_buf) _cap = cap;
    }
  }
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* b, size_t n) override {
    Serial.write(b, n);
    append(b, n);
    return n;
  }
  using Print::write;

  // Copia até maxn bytes a partir do deslocamento absoluto 'from'. Retorna quantos copiou.
  // *next recebe o próximo deslocamento a pedir; *reset indica que 'from' era maior que o total
  // (o aparelho reiniciou) e a leitura recomeçou do início do buffer.
  size_t read(uint32_t from, char* out, size_t maxn, uint32_t* next, bool* reset) {
    size_t n = 0;
    portENTER_CRITICAL(&_mux);
    uint32_t t = _total;
    uint32_t st = (t > _cap) ? t - (uint32_t)_cap : 0;
    *reset = (from > t);
    if (*reset) from = 0;
    if (from < st) from = st;
    n = t - from;
    if (n > maxn) n = maxn;
    for (size_t i = 0; i < n; i++) out[i] = (char)_buf[(from + i) % _cap];
    *next = from + (uint32_t)n;
    portEXIT_CRITICAL(&_mux);
    return n;
  }

 private:
  void put(uint8_t c) { _buf[_total % _cap] = c; _total++; }
  void append(const uint8_t* b, size_t n) {
    if (!_buf) return;
    char ts[20];
    uint32_t ms = millis();
    int tl = snprintf(ts, sizeof ts, "[%lu.%03lu] ", (unsigned long)(ms / 1000), (unsigned long)(ms % 1000));
    portENTER_CRITICAL(&_mux);
    for (size_t i = 0; i < n; i++) {
      uint8_t c = b[i];
      if (_bol && c != '\n' && c != '\r') { for (int k = 0; k < tl; k++) put((uint8_t)ts[k]); _bol = false; }
      put(c);
      if (c == '\n') _bol = true;
    }
    portEXIT_CRITICAL(&_mux);
  }
  uint8_t* _buf = nullptr;
  size_t _cap = 0;
  uint32_t _total = 0;
  bool _bol = true;
  portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
};

static DebugOut DBG;

// A partir daqui, "Serial" no sketch = DBG (que também escreve no Serial real)
#ifdef Serial
#undef Serial
#endif
#define Serial DBG
