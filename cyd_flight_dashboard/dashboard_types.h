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

#endif
