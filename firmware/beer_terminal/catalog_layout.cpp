#include "catalog_layout.h"

namespace catalog_layout {
namespace {

// Integer square root, so the layout never depends on floating-point rounding.
uint8_t isqrt(uint8_t n) {
  uint8_t r = 1;
  while ((r + 1) * (r + 1) <= n) ++r;
  return r;
}

}  // namespace

GridGeometry grid_geometry(uint8_t n) {
  GridGeometry g = {};
  if (n == 0) n = 1;
  g.rows = isqrt(n);
  g.cols = static_cast<uint8_t>((n + g.rows - 1) / g.rows);

  const int16_t gw = settings::screen_w - 2 * settings::grid_margin;
  const int16_t gh = settings::screen_h - settings::header_h - settings::footer_h -
                     2 * settings::grid_margin;
  g.tile_w = static_cast<int16_t>((gw - (g.cols - 1) * settings::grid_gap) / g.cols);
  g.tile_h = static_cast<int16_t>((gh - (g.rows - 1) * settings::grid_gap) / g.rows);
  // Compared as a ratio scaled by 100 to keep this unit float-free.
  g.horizontal_card = (static_cast<int32_t>(g.tile_w) * 100) >
                      (static_cast<int32_t>(g.tile_h) *
                       static_cast<int32_t>(settings::tile_horizontal_ratio_x100));
  return g;
}

void tile_position(const GridGeometry& g, uint8_t n, uint8_t index, int16_t& x,
                   int16_t& y) {
  const uint8_t r = index / g.cols;
  const uint8_t c = index % g.cols;
  const uint8_t remaining = static_cast<uint8_t>(n - r * g.cols);
  const uint8_t in_row = remaining < g.cols ? remaining : g.cols;
  const int16_t gw = settings::screen_w - 2 * settings::grid_margin;
  const int16_t row_w =
      static_cast<int16_t>(in_row * g.tile_w + (in_row - 1) * settings::grid_gap);
  x = static_cast<int16_t>(settings::grid_margin + (gw - row_w) / 2 +
                           c * (g.tile_w + settings::grid_gap));
  y = static_cast<int16_t>(settings::header_h + settings::grid_margin +
                           r * (g.tile_h + settings::grid_gap));
}

}  // namespace catalog_layout
