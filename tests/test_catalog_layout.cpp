// Host test for the adaptive catalog grid. Build and run with tools/test-layout.sh.
#include "../firmware/beer_terminal/catalog_layout.h"
#include <cstdio>
#include <cstdlib>

namespace {

int g_failures = 0;

void expect_grid(uint8_t n, uint8_t rows, uint8_t cols) {
  const auto g = catalog_layout::grid_geometry(n, catalog_layout::catalog_bounds());
  if (g.rows != rows || g.cols != cols) {
    std::printf("FAIL n=%u: expected %ux%u, got %ux%u\n", n, rows, cols, g.rows, g.cols);
    ++g_failures;
  } else {
    std::printf("  ok n=%u -> %u x %u, tile %dx%d, %s card\n", n, g.rows, g.cols,
                g.tile_w, g.tile_h, g.horizontal_card ? "horizontal" : "vertical");
  }
}

void expect_fits(uint8_t n) {
  const auto b = catalog_layout::catalog_bounds();
  const auto g = catalog_layout::grid_geometry(n, b);
  if (g.rows * g.cols < n) {
    std::printf("FAIL n=%u: %ux%u cannot hold %u tiles\n", n, g.rows, g.cols, n);
    ++g_failures;
  }
  for (uint8_t i = 0; i < n; ++i) {
    int16_t x = 0, y = 0;
    catalog_layout::tile_position(g, b, n, i, x, y);
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
  const auto b = catalog_layout::catalog_bounds();
  const auto g = catalog_layout::grid_geometry(n, b);
  const uint8_t last_row = (n - 1) / g.cols;
  const uint8_t in_row = n - last_row * g.cols;
  if (in_row == g.cols) return;  // full row, nothing to centre
  int16_t x = 0, y = 0;
  catalog_layout::tile_position(g, b, n, n - 1, x, y);
  const int16_t right_gap = settings::screen_w - settings::grid_margin - (x + g.tile_w);
  int16_t first_x = 0, first_y = 0;
  catalog_layout::tile_position(g, b, n, last_row * g.cols, first_x, first_y);
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

  for (uint8_t n = 1; n <= settings::max_active_products; ++n) {
    expect_fits(n);
    expect_last_row_centred(n);
  }

  // The resident screen reuses the rule for however many people the backend
  // supplies, plus the archive button. Seven residents must give the 2x4 the
  // household expects, and the grid must stay clear of the prompt line.
  std::printf("\nresident grid (people + archive button)\n");
  const auto rb = catalog_layout::resident_bounds();
  if (rb.y <= catalog_layout::catalog_bounds().y) {
    std::printf("FAIL resident bounds do not clear the prompt line\n");
    ++g_failures;
  }
  for (uint8_t people = 1; people <= settings::max_residents; ++people) {
    const uint8_t n = static_cast<uint8_t>(people + 1);
    const auto g = catalog_layout::grid_geometry(n, rb);
    if (g.rows * g.cols < n) {
      std::printf("FAIL %u people: %ux%u cannot hold %u buttons\n", people, g.rows,
                  g.cols, n);
      ++g_failures;
      continue;
    }
    for (uint8_t i = 0; i < n; ++i) {
      int16_t x = 0, y = 0;
      catalog_layout::tile_position(g, rb, n, i, x, y);
      if (x < rb.x || x + g.tile_w > rb.x + rb.w || y < rb.y ||
          y + g.tile_h > rb.y + rb.h) {
        std::printf("FAIL %u people, button %u escapes its bounds\n", people, i);
        ++g_failures;
      }
    }
    // Touch targets must stay comfortable for a thumb at arm's length.
    if (g.tile_h < 90 || g.tile_w < 140) {
      std::printf("FAIL %u people: button %dx%d is too small to tap\n", people,
                  g.tile_w, g.tile_h);
      ++g_failures;
    }
    if (people == settings::default_resident_count) {
      std::printf("  ok %u people + archive -> %u x %u, button %dx%d\n", people, g.rows,
                  g.cols, g.tile_w, g.tile_h);
      if (g.rows != 2 || g.cols != 4) {
        std::printf("FAIL expected 2 x 4 for the seven-resident household\n");
        ++g_failures;
      }
    }
  }

  // A 0-product catalog must not divide by zero or produce a negative tile.
  const auto empty =
      catalog_layout::grid_geometry(0, catalog_layout::catalog_bounds());
  if (empty.rows < 1 || empty.cols < 1 || empty.tile_w <= 0 || empty.tile_h <= 0) {
    std::printf("FAIL n=0: degenerate geometry %ux%u tile %dx%d\n", empty.rows,
                empty.cols, empty.tile_w, empty.tile_h);
    ++g_failures;
  }

  std::printf(g_failures ? "\n%d failure(s)\n" : "\nall checks passed\n", g_failures);
  return g_failures ? 1 : 0;
}
