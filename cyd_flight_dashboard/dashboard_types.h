#ifndef DASHBOARD_TYPES_H
#define DASHBOARD_TYPES_H

#include <stdint.h>

struct SvgImg {
  uint8_t* data;
  int w;
  int h;
};

struct Wx {
  const char* d;
  int ic;
};

// Fica no .h: a IDE do Arduino gera protótipos antes das structs do .ino
struct FlightUI {
  bool valid;
  char flight[16], iata[16], airline[8], type[8], desc[48];
  char org[16], dst[16], orgLine[112], dstLine[112], city[64];
  char alt[16], dist[16];            // altitude (ft) e distância (NM) já formatadas
  char airlineName[40];              // flight_info.airline.name
  bool showLogo, showSil;            // logo/silhueta JÁ carregados para este voo (mesma URL): só reexibir, sem novo download
  bool altOnly;                      // atualização parcial: só alt/dist mudaram (mesma aeronave); applyUpdates atualiza só lblAltDist
};

#endif
