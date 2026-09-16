// Host test for the adaptive catalog grid. Build and run with tools/test-layout.sh.
#include "../firmware/beer_terminal/catalog_layout.h"
#include <cstdio>
#include <cstdlib>

namespace {

int g_failures = 0;

void expect_grid(uint8_t n, uint8_t rows, uint8_t cols) {
  const auto g = catalog_layout::grid_geometry(n);
  if (g.rows != rows || g.cols != cols) {
    std::printf("FAIL n=%u: expected %ux%u, got %ux%u\n", n, rows, cols, g.rows, g.cols);
    ++g_failures;
  } else {
    std::printf("  ok n=%u -> %u x %u, tile %dx%d, %s card\n", n, g.rows, g.cols,
                g.tile_w, g.tile_h, g.horizontal_card ? "horizontal" : "vertical");
  }
}

void expect_fits(uint8_t n) {
  const auto g = catalog_layout::grid_geometry(n);
  if (g.rows * g.cols < n) {
    std::printf("FAIL n=%u: %ux%u cannot hold %u tiles\n", n, g.rows, g.cols, n);
    ++g_failures;
  }
  for (uint8_t i = 0; i < n; ++i) {
    int16_t x = 0, y = 0;
    catalog_layout::tile_position(g, n, i, x, y);
    const int16_t right = x + g.tile_w;
    const int16_t bottom = y + g.tile_h;
    if (x < settings::grid_margin ||
        right > settings::screen_w - settings::grid_margin) {
      std::printf("FAIL n=%u tile %u: x span %d..%d escapes the margins\n", n, i, x, right);
      ++g_failures;
    }
    if (y < settings::header_h ||
        bottom > settings::screen_h - settings::footer_h) {
      std::printf("FAIL n=%u tile %u: y span %d..%d overlaps header/footer\n", n, i, y,
                  bottom);
      ++g_failures;
    }
  }
}

void expect_last_row_centred(uint8_t n) {
  const auto g = catalog_layout::grid_geometry(n);
  const uint8_t last_row = (n - 1) / g.cols;
  const uint8_t in_row = n - last_row * g.cols;
  if (in_row == g.cols) return;  // full row, nothing to centre
  int16_t x = 0, y = 0;
  catalog_layout::tile_position(g, n, n - 1, x, y);
  const int16_t right_gap = settings::screen_w - settings::grid_margin - (x + g.tile_w);
  int16_t first_x = 0, first_y = 0;
  catalog_layout::tile_position(g, n, last_row * g.cols, first_x, first_y);
  const int16_t left_gap = first_x - settings::grid_margin;
  if (std::abs(left_gap - right_gap) > 1) {
    std::printf("FAIL n=%u: partial row not centred (left %d, right %d)\n", n, left_gap,
                right_gap);
    ++g_failures;
  }
}

}  // namespace

int main() {
  std::printf("adaptive catalog grid\n");
  // The layouts specified in docs/architecture.md.
  expect_grid(1, 1, 1);
  expect_grid(2, 1, 2);
  expect_grid(3, 1, 3);
  expect_grid(4, 2, 2);
  expect_grid(5, 2, 3);
  expect_grid(6, 2, 3);
  expect_grid(7, 2, 4);
  expect_grid(8, 2, 4);

  for (uint8_t n = 1; n <= settings::max_products; ++n) {
    expect_fits(n);
    expect_last_row_centred(n);
  }

  // A 0-product catalog must not divide by zero or produce a negative tile.
  const auto empty = catalog_layout::grid_geometry(0);
  if (empty.rows < 1 || empty.cols < 1 || empty.tile_w <= 0 || empty.tile_h <= 0) {
    std::printf("FAIL n=0: degenerate geometry %ux%u tile %dx%d\n", empty.rows,
                empty.cols, empty.tile_w, empty.tile_h);
    ++g_failures;
  }

  std::printf(g_failures ? "\n%d failure(s)\n" : "\nall checks passed\n", g_failures);
  return g_failures ? 1 : 0;
}
