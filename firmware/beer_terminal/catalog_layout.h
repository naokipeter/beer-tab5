#pragma once
#include <stdint.h>
#include "settings.h"

// Pure geometry for the adaptive grids. Deliberately free of LVGL and Arduino
// dependencies so it can be compiled and tested on the host.
// See tools/test-layout.sh and docs/architecture.md.
namespace catalog_layout {

// The content rectangle a grid fills, with margins already applied.
struct Bounds {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
};

struct GridGeometry {
  uint8_t rows;
  uint8_t cols;
  int16_t tile_w;
  int16_t tile_h;
  bool horizontal_card;
};

// Between the header and the footer: where the product tiles go.
Bounds catalog_bounds();
// The same, lowered by the "Wer trinkt?" prompt line.
Bounds resident_bounds();

// rows = max(1, floor(sqrt(n))), cols = ceil(n / rows).
// 1..3 give one row; 4 gives 2x2; 5..6 give 2x3; 7..8 give 2x4; 9 gives 3x3.
GridGeometry grid_geometry(uint8_t n, const Bounds& b);

// Top-left corner of tile `index`. A partial last row is centred, so every tile
// on a screen is the same size.
void tile_position(const GridGeometry& g, const Bounds& b, uint8_t n, uint8_t index,
                   int16_t& x, int16_t& y);

}  // namespace catalog_layout
