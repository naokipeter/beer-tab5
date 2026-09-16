#include "ui_screens.h"
#include <Arduino.h>
#include "ui_lvgl.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "backend.h"
#include "catalog_layout.h"
#include "device_storage.h"
#include "product_catalog.h"
#include "purchase_log.h"
#include "resident_directory.h"
#include "wifi_manager.h"
#include "settings.h"
#include "ui_fonts.h"
#include "ui_keyboard.h"

namespace ui_screens {
namespace {

using app_state::Event;
using app_state::State;
using catalog_layout::Bounds;
using catalog_layout::GridGeometry;

// Price being composed on the NEW_PRODUCT keypad, in rappen.
int32_t g_entry_rappen = 0;
bool g_entry_free = false;
lv_obj_t* g_entry_price_label = nullptr;
lv_obj_t* g_entry_name = nullptr;
lv_obj_t* g_keyboard = nullptr;

lv_color_t col(uint32_t rgb) { return lv_color_hex(rgb); }

const lv_font_t* font_for(int16_t px) {
  if (px >= 44) return &font_de_48;
  if (px >= 30) return &font_de_32;
  if (px >= 24) return &font_de_28;
  return &font_de_20;
}

lv_obj_t* make_panel(lv_obj_t* parent, int16_t x, int16_t y, int16_t w, int16_t h,
                     uint32_t bg) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, col(bg), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_radius(o, 6, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  // lv_obj_create sets LV_OBJ_FLAG_CLICKABLE on every base object, so a
  // decorative panel would absorb the press instead of letting it reach the
  // button underneath. Labels remove the flag themselves, which is why a tile's
  // text used to respond while its photo did not. Callers that want a panel to
  // be tappable add the flag back explicitly.
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}

lv_obj_t* make_container(lv_obj_t* parent) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_radius(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}

lv_obj_t* make_label(lv_obj_t* parent, const char* text, uint32_t colour,
                     const lv_font_t* font) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_color(l, col(colour), 0);
  lv_obj_set_style_text_font(l, font, 0);
  return l;
}

lv_obj_t* make_button(lv_obj_t* parent, const char* text, int16_t x, int16_t y,
                      int16_t w, int16_t h, uint32_t bg, uint32_t fg,
                      lv_event_cb_t cb, void* user_data) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_bg_color(b, col(bg), 0);
  lv_obj_set_style_radius(b, 6, 0);
  lv_obj_set_style_border_width(b, 0, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_t* l = make_label(b, text, fg, font_for(h / 3));
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_width(l, w - 16);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(l);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user_data);
  return b;
}

void* as_ud(uintptr_t v) { return reinterpret_cast<void*>(v); }
uintptr_t from_ud(lv_event_t* e) {
  return reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
}

// ---- events -------------------------------------------------------------

void on_tile(lv_event_t* e) {
  app_state::select_product(static_cast<uint8_t>(from_ud(e)));
  app_state::dispatch(Event::ProductSelected);
}

void on_resident(lv_event_t* e) {
  app_state::context().resident_index = static_cast<int8_t>(from_ud(e));
  app_state::dispatch(Event::ResidentSelected);
}

void on_event_button(lv_event_t* e) {
  app_state::dispatch(static_cast<Event>(from_ud(e)));
}

void on_restore(lv_event_t* e) {
  const int8_t storage =
      product_catalog::storage_index_of_archived(static_cast<uint8_t>(from_ud(e)));
  if (product_catalog::restore(storage)) {
    app_state::dispatch(Event::ProductRestored);
  }
}

void on_header_long_press(lv_event_t*) {
  app_state::dispatch(Event::AdminRequested);
}

void refresh_entry_price() {
  if (!g_entry_price_label) return;
  char buf[32];
  product_catalog::format_rappen(g_entry_rappen, g_entry_free, buf, sizeof(buf));
  lv_label_set_text(g_entry_price_label, buf);
}

void on_keypad(lv_event_t* e) {
  lv_obj_t* mx = static_cast<lv_obj_t*>(lv_event_get_target(e));
  const char* txt =
      lv_buttonmatrix_get_button_text(mx, lv_buttonmatrix_get_selected_button(mx));
  if (!txt) return;
  if (strcmp(txt, "C") == 0) {
    g_entry_rappen = 0;
    g_entry_free = false;
  } else if (txt[0] >= '0' && txt[0] <= '9') {
    g_entry_free = false;
    // Cap at CHF 99.99 so a stuck touch cannot compose an absurd price.
    const int32_t next = g_entry_rappen * 10 + (txt[0] - '0');
    if (next <= 9999) g_entry_rappen = next;
  }
  refresh_entry_price();
}

void on_free(lv_event_t*) {
  g_entry_free = true;
  g_entry_rappen = 0;
  refresh_entry_price();
}

void on_new_product_confirm(lv_event_t*) {
  char name[40];
  const char* typed = g_entry_name ? lv_textarea_get_text(g_entry_name) : "";
  // Sanitised properly server-side in milestone 8; this only bounds the length
  // and substitutes a placeholder so a purchase is never nameless.
  snprintf(name, sizeof(name), "%s", (typed && typed[0]) ? typed : "Unbekanntes Bier");
  if (!g_entry_free && g_entry_rappen <= 0) return;  // needs a price or Gratis

  const int8_t storage =
      product_catalog::add_product(name, g_entry_rappen, g_entry_free);
  app_state::set_ad_hoc_product(name, g_entry_rappen, g_entry_free);
  if (storage >= 0) app_state::context().product_index = storage;
  app_state::dispatch(Event::NewProductReady);
}

void on_name_focus(lv_event_t* e) {
  if (!g_keyboard) return;
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_FOCUSED) {
    lv_keyboard_set_textarea(g_keyboard, g_entry_name);
    lv_obj_remove_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);
  } else if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_READY ||
             code == LV_EVENT_CANCEL) {
    lv_obj_add_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);
  }
}

void on_simulate_failure(lv_event_t* e) {
  lv_obj_t* sw = static_cast<lv_obj_t*>(lv_event_get_target(e));
  app_state::simulate_next_failure(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

// Animates an object's width; used by the dwell bar below.
void anim_set_width(void* obj, int32_t value) {
  lv_obj_set_width(static_cast<lv_obj_t*>(obj), value);
}

// A thin bar along the bottom edge that fills left to right over the screen's
// dwell, so the time left to act is visible rather than guessed. The duration
// comes from app_state rather than the constant it was set from, so the bar can
// never disagree with the deadline it is showing. LVGL deletes an animation with
// its object, so leaving the screen cancels it.
void add_dwell_bar(lv_obj_t* scr, uint32_t fill_rgb) {
  const uint32_t duration_ms = app_state::current_dwell_ms();
  if (duration_ms == 0) return;
  constexpr int16_t kBarH = 10;
  lv_obj_t* track = make_panel(scr, 0, settings::screen_h - kBarH,
                               settings::screen_w, kBarH, 0x000000);
  lv_obj_set_style_radius(track, 0, 0);
  lv_obj_set_style_bg_opa(track, LV_OPA_20, 0);

  lv_obj_t* fill = make_panel(track, 0, 0, 0, kBarH, fill_rgb);
  lv_obj_set_style_radius(fill, 0, 0);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, fill);
  lv_anim_set_exec_cb(&a, anim_set_width);
  lv_anim_set_values(&a, 0, settings::screen_w);
  lv_anim_set_duration(&a, duration_ms);
  lv_anim_start(&a);
}

// ---- chrome -------------------------------------------------------------

lv_obj_t* build_root() {
  lv_obj_t* scr = lv_screen_active();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, col(settings::theme::bg), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  g_entry_price_label = nullptr;
  g_entry_name = nullptr;
  g_keyboard = nullptr;
  return scr;
}

void build_header(lv_obj_t* scr, const char* title, bool admin_gesture) {
  lv_obj_t* bar = make_panel(scr, 0, 0, settings::screen_w, settings::header_h,
                             settings::theme::bg);
  lv_obj_t* l = make_label(bar, title, settings::theme::text, &font_de_28);
  lv_obj_align(l, LV_ALIGN_LEFT_MID, settings::grid_margin, 0);
  if (admin_gesture) {
    lv_obj_add_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(bar, on_header_long_press, LV_EVENT_LONG_PRESSED, nullptr);
  }
}

// Two footer buttons across the bottom strip.
void build_footer_pair(lv_obj_t* scr, const char* left, lv_event_cb_t left_cb,
                       void* left_ud, uint32_t left_bg, uint32_t left_fg,
                       const char* right, lv_event_cb_t right_cb, void* right_ud,
                       uint32_t right_bg, uint32_t right_fg) {
  const int16_t fy = settings::screen_h - settings::footer_h + 14;
  const int16_t fw = (settings::screen_w - 3 * settings::grid_margin) / 2;
  make_button(scr, left, settings::grid_margin, fy, fw, 44, left_bg, left_fg, left_cb,
              left_ud);
  make_button(scr, right, 2 * settings::grid_margin + fw, fy, fw, 44, right_bg,
              right_fg, right_cb, right_ud);
}

void build_footer_single(lv_obj_t* scr, const char* text, Event ev, uint32_t bg,
                         uint32_t fg) {
  make_button(scr, text, settings::grid_margin,
              settings::screen_h - settings::footer_h + 14,
              settings::screen_w - 2 * settings::grid_margin, 44, bg, fg,
              on_event_button, as_ud(static_cast<uintptr_t>(ev)));
}

// Shared product tile, used by the catalog and the archived list. The contents
// are a flex stack centred in the tile rather than pinned to its edges, so a
// short name does not leave a gap between the name and the price.
void build_product_tile(lv_obj_t* scr, const product_catalog::Product& p,
                        const GridGeometry& g, int16_t x, int16_t y,
                        lv_event_cb_t cb, void* ud, bool dim) {
  constexpr int16_t kPad = 14;
  constexpr int16_t kGap = 10;

  lv_obj_t* tile = lv_button_create(scr);
  lv_obj_set_pos(tile, x, y);
  lv_obj_set_size(tile, g.tile_w, g.tile_h);
  lv_obj_set_style_bg_color(tile, col(settings::theme::surface), 0);
  lv_obj_set_style_radius(tile, 6, 0);
  lv_obj_set_style_border_width(tile, 0, 0);
  lv_obj_set_style_shadow_width(tile, 0, 0);
  lv_obj_set_style_pad_all(tile, kPad, 0);
  if (cb) lv_obj_add_event_cb(tile, cb, LV_EVENT_CLICKED, ud);

  const int16_t content_w = static_cast<int16_t>(g.tile_w - 2 * kPad);
  const int16_t content_h = static_cast<int16_t>(g.tile_h - 2 * kPad);
  const lv_font_t* name_font =
      font_for(static_cast<int16_t>(fminf(g.tile_h * 0.11f, 26.0f)));
  const lv_font_t* price_font =
      font_for(static_cast<int16_t>(fminf(g.tile_h * 0.15f, 34.0f)));

  lv_obj_t* body = make_container(tile);
  lv_obj_set_size(body, content_w, content_h);
  lv_obj_center(body);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  // The photo takes whatever the text does not need, so a two-line name can
  // never push the price out of the tile.
  int16_t photo;
  if (g.horizontal_card) {
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(body, kPad, 0);
    photo = static_cast<int16_t>(fminf(content_h, content_w * 0.45f));
  } else {
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, kGap, 0);
    const int16_t text_h =
        static_cast<int16_t>(2 * lv_font_get_line_height(name_font) +
                             lv_font_get_line_height(price_font));
    photo = static_cast<int16_t>(content_h - text_h - 2 * kGap);
    photo = static_cast<int16_t>(fminf(photo, content_w * 0.62f));
  }
  if (photo < 48) photo = 48;

  // Photo placeholder. Milestone 6 swaps this for the cached Open Food Facts
  // image; the deterministic colour stays as the no-photo fallback.
  lv_obj_t* img = make_panel(body, 0, 0, photo, photo,
                             product_catalog::fallback_colour(p));
  lv_obj_set_style_radius(img, 4, 0);
  if (dim) lv_obj_set_style_bg_opa(img, LV_OPA_40, 0);

  char initial[2] = {p.name[0], '\0'};
  lv_obj_t* il = make_label(img, initial, 0xFFFFFF, font_for(photo / 3));
  lv_obj_set_style_text_opa(il, LV_OPA_70, 0);
  lv_obj_center(il);

  // In a wide tile the text sits in its own centred column beside the photo.
  lv_obj_t* text_parent = body;
  if (g.horizontal_card) {
    lv_obj_t* column = make_container(body);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(column, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(column, kGap, 0);
    lv_obj_set_flex_grow(column, 1);
    lv_obj_set_height(column, LV_SIZE_CONTENT);
    text_parent = column;
  }

  char price[32];
  product_catalog::format_price(p, price, sizeof(price));

  lv_obj_t* name = make_label(text_parent, p.name,
                              dim ? settings::theme::text_muted : settings::theme::text,
                              name_font);
  lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(name, g.horizontal_card ? lv_pct(100) : content_w);
  lv_obj_set_style_text_align(
      name, g.horizontal_card ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t* cost = make_label(
      text_parent, price,
      dim ? settings::theme::text_muted
          : (p.free_item ? settings::theme::ok : settings::theme::accent),
      price_font);
  if (!g.horizontal_card) {
    lv_obj_set_width(cost, content_w);
    lv_obj_set_style_text_align(cost, LV_TEXT_ALIGN_CENTER, 0);
  }
}

// ---- screens ------------------------------------------------------------

void build_catalog() {
  lv_obj_t* scr = build_root();
  build_header(scr, "Was trinksch?", true);

  const uint8_t n = product_catalog::active_count();
  if (n == 0) {
    lv_obj_t* l = make_label(scr, "Kein Bier im Kühlschrank",
                             settings::theme::text_muted, &font_de_32);
    lv_obj_center(l);
  } else {
    const Bounds b = catalog_layout::catalog_bounds();
    const GridGeometry g = catalog_layout::grid_geometry(n, b);
    for (uint8_t i = 0; i < n; ++i) {
      const product_catalog::Product* p = product_catalog::active_at(i);
      int16_t x = 0, y = 0;
      catalog_layout::tile_position(g, b, n, i, x, y);
      build_product_tile(scr, *p, g, x, y, on_tile, as_ud(i), false);
    }
  }

  // Constant actions live in the footer so the product count alone drives layout.
  build_footer_pair(scr, "Nicht gelistet", on_event_button,
                    as_ud(static_cast<uintptr_t>(Event::NotListed)),
                    settings::theme::surface_alt, settings::theme::text,
                    "Abbrechen", on_event_button,
                    as_ud(static_cast<uintptr_t>(Event::Sleep)),
                    settings::theme::surface_alt, settings::theme::text_muted);
}

void build_resident() {
  lv_obj_t* scr = build_root();
  const app_state::Context& ctx = app_state::context();

  char price[32];
  product_catalog::format_rappen(ctx.price_rappen, ctx.free_item, price, sizeof(price));
  char title[96];
  snprintf(title, sizeof(title), "%s  -  %s", ctx.product_name, price);
  build_header(scr, title, false);

  lv_obj_t* prompt = make_label(scr, "Wer trinkt?", settings::theme::text_muted,
                                &font_de_20);
  lv_obj_set_pos(prompt, settings::grid_margin, settings::header_h);

  // Residents plus one archive button share the same tested grid rule, so the
  // layout follows however many people the backend supplies.
  const uint8_t people = resident_directory::count();
  const uint8_t n = static_cast<uint8_t>(people + 1);
  const Bounds b = catalog_layout::resident_bounds();
  const GridGeometry g = catalog_layout::grid_geometry(n, b);

  for (uint8_t i = 0; i < n; ++i) {
    int16_t x = 0, y = 0;
    catalog_layout::tile_position(g, b, n, i, x, y);
    if (i < people) {
      const resident_directory::Entry* r = resident_directory::at(i);
      make_button(scr, r->name, x, y, g.tile_w, g.tile_h, settings::theme::surface,
                  settings::theme::text, on_resident, as_ud(i));
    } else {
      make_button(scr, "Bier archivieren", x, y, g.tile_w, g.tile_h,
                  settings::theme::surface_alt, settings::theme::danger,
                  on_event_button,
                  as_ud(static_cast<uintptr_t>(Event::ArchiveRequested)));
    }
  }

  build_footer_single(scr, "Zurück", Event::Cancel, settings::theme::surface_alt,
                      settings::theme::text_muted);
}

void build_archived() {
  lv_obj_t* scr = build_root();
  build_header(scr, "Schon mal dagewesen", false);

  const bool room = product_catalog::can_restore();
  lv_obj_t* prompt = make_label(
      scr,
      room ? "Zurück in den Kühlschrank, oder neu anlegen?"
           : "Kühlschrank voll – zuerst ein Bier archivieren",
      room ? settings::theme::text_muted : settings::theme::danger,
      &font_de_20);
  lv_obj_set_pos(prompt, settings::grid_margin, settings::header_h);

  const uint8_t n = product_catalog::archived_count();
  const Bounds b = catalog_layout::resident_bounds();
  const GridGeometry g = catalog_layout::grid_geometry(n, b);
  for (uint8_t i = 0; i < n; ++i) {
    const product_catalog::Product* p = product_catalog::archived_at(i);
    int16_t x = 0, y = 0;
    catalog_layout::tile_position(g, b, n, i, x, y);
    build_product_tile(scr, *p, g, x, y, room ? on_restore : nullptr, as_ud(i), !room);
  }

  build_footer_pair(scr, "Abbrechen", on_event_button,
                    as_ud(static_cast<uintptr_t>(Event::Cancel)),
                    settings::theme::surface_alt, settings::theme::text_muted,
                    "Neues Bier anlegen", on_event_button,
                    as_ud(static_cast<uintptr_t>(Event::CreateNewProduct)),
                    settings::theme::accent, 0x12120F);
}

void build_confirm_archive() {
  lv_obj_t* scr = build_root();
  build_header(scr, "Archivieren", false);

  const product_catalog::Product* p =
      product_catalog::at_storage(app_state::context().archive_storage_index);

  char line[96];
  snprintf(line, sizeof(line), "%s", p ? p->name : app_state::context().product_name);
  lv_obj_t* name = make_label(scr, line, settings::theme::text, &font_de_48);
  lv_obj_align(name, LV_ALIGN_CENTER, 0, -60);

  lv_obj_t* q = make_label(scr, "aus dem Kühlschrank nehmen?", settings::theme::text,
                           &font_de_32);
  lv_obj_align(q, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t* hint = make_label(
      scr, "Bleibt gespeichert und kann über „Nicht gelistet“ zurückgeholt werden.",
      settings::theme::text_muted, &font_de_20);
  lv_obj_align(hint, LV_ALIGN_CENTER, 0, 50);

  build_footer_pair(scr, "Abbrechen", on_event_button,
                    as_ud(static_cast<uintptr_t>(Event::Cancel)),
                    settings::theme::surface_alt, settings::theme::text,
                    "Archivieren", on_event_button,
                    as_ud(static_cast<uintptr_t>(Event::ArchiveConfirmed)),
                    settings::theme::danger, 0xFFFFFF);
}

void build_new_product() {
  lv_obj_t* scr = build_root();
  build_header(scr, "Neues Getränk", false);

  g_entry_rappen = 0;
  g_entry_free = false;

  g_entry_name = lv_textarea_create(scr);
  lv_obj_set_pos(g_entry_name, settings::grid_margin, settings::header_h + 8);
  lv_obj_set_size(g_entry_name, 620, 68);
  lv_textarea_set_one_line(g_entry_name, true);
  lv_textarea_set_placeholder_text(g_entry_name, "Name des Getränks");
  lv_textarea_set_max_length(g_entry_name, 38);
  lv_obj_set_style_text_font(g_entry_name, &font_de_28, 0);
  lv_obj_set_style_bg_color(g_entry_name, col(settings::theme::surface), 0);
  lv_obj_set_style_text_color(g_entry_name, col(settings::theme::text), 0);
  lv_obj_set_style_border_width(g_entry_name, 0, 0);
  lv_obj_add_event_cb(g_entry_name, on_name_focus, LV_EVENT_ALL, nullptr);

  g_entry_price_label = make_label(scr, "CHF 0.00", settings::theme::accent,
                                   &font_de_48);
  lv_obj_set_pos(g_entry_price_label, settings::grid_margin, settings::header_h + 96);

  make_button(scr, "Gratis", settings::grid_margin, settings::header_h + 170, 300, 80,
              settings::theme::ok, 0xFFFFFF, on_free, nullptr);

  static const char* kKeys[] = {"1", "2", "3", "\n",
                                "4", "5", "6", "\n",
                                "7", "8", "9", "\n",
                                "C", "0", ""};
  lv_obj_t* pad = lv_buttonmatrix_create(scr);
  lv_buttonmatrix_set_map(pad, kKeys);
  lv_obj_set_pos(pad, 680, settings::header_h + 8);
  lv_obj_set_size(pad, 584, 400);
  lv_obj_set_style_bg_opa(pad, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pad, 0, 0);
  lv_obj_set_style_text_font(pad, &font_de_32, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(pad, col(settings::theme::surface), LV_PART_ITEMS);
  lv_obj_set_style_text_color(pad, col(settings::theme::text), LV_PART_ITEMS);
  lv_obj_set_style_radius(pad, 6, LV_PART_ITEMS);
  lv_obj_add_event_cb(pad, on_keypad, LV_EVENT_VALUE_CHANGED, nullptr);

  build_footer_pair(scr, "Abbrechen", on_event_button,
                    as_ud(static_cast<uintptr_t>(Event::Cancel)),
                    settings::theme::surface_alt, settings::theme::text_muted,
                    "Weiter", on_new_product_confirm, nullptr, settings::theme::accent,
                    0x12120F);

  g_keyboard = lv_keyboard_create(scr);
  ui_keyboard::apply_german_layout(g_keyboard);
  lv_obj_set_size(g_keyboard, settings::screen_w, 300);
  lv_obj_align(g_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(g_keyboard, on_name_focus, LV_EVENT_READY, nullptr);
  lv_obj_add_event_cb(g_keyboard, on_name_focus, LV_EVENT_CANCEL, nullptr);
}

void build_submitting() {
  lv_obj_t* scr = build_root();
  lv_obj_t* sp = lv_spinner_create(scr);
  lv_obj_set_size(sp, 120, 120);
  lv_obj_align(sp, LV_ALIGN_CENTER, 0, -60);
  lv_obj_set_style_arc_color(sp, col(settings::theme::accent), LV_PART_INDICATOR);
  lv_obj_t* l = make_label(scr, "Wird gespeichert...", settings::theme::text,
                           &font_de_32);
  lv_obj_align(l, LV_ALIGN_CENTER, 0, 60);
}

void build_success() {
  lv_obj_t* scr = build_root();
  const app_state::Context& ctx = app_state::context();
  lv_obj_set_style_bg_color(scr, col(ctx.undone ? settings::theme::surface_alt
                                                : settings::theme::ok),
                            0);

  const resident_directory::Entry* r =
      ctx.resident_index >= 0
          ? resident_directory::at(static_cast<uint8_t>(ctx.resident_index))
          : nullptr;
  char line[128];
  snprintf(line, sizeof(line), "%s: %s", r ? r->name : "?", ctx.product_name);

  lv_obj_t* big = make_label(scr, ctx.undone ? "Rückgängig gemacht" : "Gebucht",
                             0xFFFFFF, &font_de_48);
  lv_obj_align(big, LV_ALIGN_CENTER, 0, ctx.undone ? -40 : -90);
  lv_obj_t* l = make_label(scr, line, 0xFFFFFF, &font_de_32);
  lv_obj_align(l, LV_ALIGN_CENTER, 0, ctx.undone ? 30 : -20);

  // A reversal is already final, so it offers nothing further and clears itself.
  if (ctx.undone) return;

  // Both are optional: ignoring them returns to the catalog on its own. Undo sits
  // on the left, away from the summary button, so a reflex tap cannot reverse a
  // purchase by accident.
  const int16_t bw = 460;
  const int16_t gap = 24;
  const int16_t by = settings::screen_h / 2 + 60;
  const int16_t left = (settings::screen_w - (2 * bw + gap)) / 2;
  make_button(scr, "Rückgängig", left, by, bw, 84, settings::theme::surface,
              0xFFFFFF, on_event_button,
              as_ud(static_cast<uintptr_t>(Event::Undo)));
  make_button(scr, "Übersicht anzeigen", left + bw + gap, by, bw, 84, 0xFFFFFF,
              settings::theme::ok, on_event_button,
              as_ud(static_cast<uintptr_t>(Event::ShowSummary)));

  add_dwell_bar(scr, 0xFFFFFF);
}

void build_undoing() {
  lv_obj_t* scr = build_root();
  lv_obj_t* sp = lv_spinner_create(scr);
  lv_obj_set_size(sp, 120, 120);
  lv_obj_align(sp, LV_ALIGN_CENTER, 0, -60);
  lv_obj_set_style_arc_color(sp, col(settings::theme::accent), LV_PART_INDICATOR);
  lv_obj_t* l = make_label(scr, "Wird rückgängig gemacht...", settings::theme::text,
                           &font_de_32);
  lv_obj_align(l, LV_ALIGN_CENTER, 0, 60);
}

void build_summary() {
  lv_obj_t* scr = build_root();
  build_header(scr, "Übersicht", false);

  const uint8_t n = purchase_log::count();
  if (n == 0) {
    lv_obj_t* l = make_label(scr, "Noch nichts erfasst", settings::theme::text_muted,
                             &font_de_32);
    lv_obj_center(l);
  } else {
    const int16_t top = settings::header_h + 12;
    const int16_t row_h = 52;
    const int16_t w = settings::screen_w - 2 * settings::grid_margin;

    lv_obj_t* h1 = make_label(scr, "Name", settings::theme::text_muted,
                              &font_de_20);
    lv_obj_set_pos(h1, settings::grid_margin + 16, top);
    lv_obj_t* h2 = make_label(scr, "Anzahl", settings::theme::text_muted,
                              &font_de_20);
    lv_obj_set_pos(h2, settings::grid_margin + w - 420, top);
    lv_obj_t* h3 = make_label(scr, "Total", settings::theme::text_muted,
                              &font_de_20);
    lv_obj_set_pos(h3, settings::grid_margin + w - 230, top);

    for (uint8_t i = 0; i < n; ++i) {
      const purchase_log::Tally* t = purchase_log::at(i);
      const int16_t y = static_cast<int16_t>(top + 32 + i * row_h);
      lv_obj_t* row = make_panel(scr, settings::grid_margin, y, w, row_h - 8,
                                 settings::theme::surface);
      lv_obj_t* nm = make_label(row, t->resident_name, settings::theme::text,
                                &font_de_28);
      lv_obj_align(nm, LV_ALIGN_LEFT_MID, 16, 0);

      char cnt[16];
      snprintf(cnt, sizeof(cnt), "%u", static_cast<unsigned>(t->drinks));
      lv_obj_t* cl = make_label(row, cnt, settings::theme::text, &font_de_28);
      lv_obj_align(cl, LV_ALIGN_LEFT_MID, w - 420, 0);

      char sum[32];
      product_catalog::format_rappen(t->total_rappen, false, sum, sizeof(sum));
      lv_obj_t* sl = make_label(row, sum, settings::theme::accent, &font_de_28);
      lv_obj_align(sl, LV_ALIGN_LEFT_MID, w - 230, 0);
    }

    char total[64];
    char amount[32];
    product_catalog::format_rappen(purchase_log::total_rappen(), false, amount,
                                   sizeof(amount));
    snprintf(total, sizeof(total), "%u Getränke, %s",
             static_cast<unsigned>(purchase_log::total_drinks()), amount);
    lv_obj_t* tl = make_label(scr, total, settings::theme::text_muted,
                              &font_de_20);
    lv_obj_align(tl, LV_ALIGN_BOTTOM_LEFT, settings::grid_margin,
                 -settings::footer_h - 4);
  }

  build_footer_single(scr, "Zurück", Event::Cancel, settings::theme::surface_alt,
                      settings::theme::text);
  add_dwell_bar(scr, settings::theme::accent);
}

void build_error() {
  lv_obj_t* scr = build_root();
  const app_state::Context& ctx = app_state::context();
  build_header(scr, "Fehler", false);

  lv_obj_t* l = make_label(scr, ctx.message[0] ? ctx.message : "Unbekannter Fehler",
                           settings::theme::text, &font_de_32);
  lv_obj_align(l, LV_ALIGN_CENTER, 0, -40);

  lv_obj_t* hint = make_label(scr, "Der Eintrag wird nicht doppelt erfasst.",
                              settings::theme::text_muted, &font_de_20);
  lv_obj_align(hint, LV_ALIGN_CENTER, 0, 10);

  build_footer_pair(scr, "Abbrechen", on_event_button,
                    as_ud(static_cast<uintptr_t>(Event::Cancel)),
                    settings::theme::surface_alt, settings::theme::text_muted,
                    "Nochmal versuchen", on_event_button,
                    as_ud(static_cast<uintptr_t>(Event::Retry)), settings::theme::accent,
                    0x12120F);
}

void build_admin() {
  lv_obj_t* scr = build_root();
  build_header(scr, "Admin", false);

  int16_t y = settings::header_h + 8;
  for (uint8_t i = 0; i < product_catalog::active_count(); ++i) {
    const product_catalog::Product* p = product_catalog::active_at(i);
    char price[32];
    product_catalog::format_price(*p, price, sizeof(price));
    char row[80];
    snprintf(row, sizeof(row), "%-30s %10s", p->name, price);
    lv_obj_t* l = make_label(scr, row, settings::theme::text, &font_de_20);
    lv_obj_set_pos(l, settings::grid_margin, y);
    y += 30;
  }

  char note[96];
  snprintf(note, sizeof(note), "%u archiviert. Preisänderung folgt in Meilenstein 11.",
           static_cast<unsigned>(product_catalog::archived_count()));
  lv_obj_t* nl = make_label(scr, note, settings::theme::text_muted,
                            &font_de_20);
  lv_obj_set_pos(nl, settings::grid_margin, y + 10);

  char src[96];
  snprintf(src, sizeof(src), "%u Bewohner (%s)",
           static_cast<unsigned>(resident_directory::count()),
           resident_directory::from_backend() ? "vom Server" : "lokale Vorgabe");
  lv_obj_t* sl = make_label(scr, src, settings::theme::text_muted, &font_de_20);
  lv_obj_set_pos(sl, settings::grid_margin, y + 40);

  // Storage state, so persistence can be judged on the device rather than
  // inferred from the serial log.
  char store[128];
  if (device_storage::mounted()) {
    snprintf(store, sizeof(store), "Speicher: %u kB frei, Revision %lu%s",
             static_cast<unsigned>(device_storage::free_bytes() / 1024),
             static_cast<unsigned long>(product_catalog::revision()),
             product_catalog::dirty() ? ", ungesichert" : ", gesichert");
  } else {
    snprintf(store, sizeof(store), "Speicher nicht eingebunden - nichts wird gesichert");
  }
  lv_obj_t* stl = make_label(scr, store,
                             device_storage::mounted() ? settings::theme::text_muted
                                                       : settings::theme::danger,
                             &font_de_20);
  lv_obj_set_pos(stl, settings::grid_margin, y + 70);

  char net[160];
  if (!backend::configured()) {
    snprintf(net, sizeof(net),
             "Kein Server konfiguriert - Buchungen bleiben lokal (secrets.h)");
  } else if (wifi_manager::online()) {
    const uint32_t last = backend::last_sync_ms();
    snprintf(net, sizeof(net), "WLAN %s (%ld dBm), Sync vor %lu s%s",
             wifi_manager::status_text(), static_cast<long>(wifi_manager::rssi()),
             last == 0 ? 0UL : static_cast<unsigned long>((millis() - last) / 1000),
             backend::sync_in_flight() ? ", laeuft" : "");
  } else {
    snprintf(net, sizeof(net), "WLAN %s%s", wifi_manager::status_text(),
             backend::sync_in_flight() ? ", Sync wartet" : "");
  }
  lv_obj_t* nl2 = make_label(scr, net,
                             backend::configured() && wifi_manager::online()
                                 ? settings::theme::text_muted
                                 : settings::theme::accent,
                             &font_de_20);
  lv_obj_set_pos(nl2, settings::grid_margin, y + 100);

  lv_obj_t* sw_label = make_label(scr, "Nächsten Fehler simulieren",
                                  settings::theme::text_muted, &font_de_20);
  lv_obj_set_pos(sw_label, 700, settings::header_h + 12);
  lv_obj_t* sw = lv_switch_create(scr);
  lv_obj_set_pos(sw, 700, settings::header_h + 44);
  lv_obj_set_size(sw, 90, 46);
  if (app_state::failure_simulated()) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw, on_simulate_failure, LV_EVENT_VALUE_CHANGED, nullptr);

  build_footer_single(scr, "Zurück", Event::AdminDone, settings::theme::surface_alt,
                      settings::theme::text);
}

void build_sleeping() {
  lv_obj_t* scr = build_root();
  lv_obj_t* l = make_label(scr, "Bildschirm tippen", settings::theme::text_muted,
                           &font_de_28);
  lv_obj_center(l);
  lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(scr, on_event_button, LV_EVENT_CLICKED,
                      as_ud(static_cast<uintptr_t>(Event::Wake)));
}

}  // namespace

void begin() { show(app_state::state()); }

void show(State current) {
  switch (current) {
    case State::Sleeping:          build_sleeping();        break;
    case State::Waking:                                     break;
    case State::SelectingProduct:  build_catalog();         break;
    case State::LookingUp:         build_submitting();      break;
    case State::ProductFound:      build_catalog();         break;
    case State::SelectingArchived: build_archived();        break;
    case State::ConfirmArchive:    build_confirm_archive(); break;
    case State::NewProduct:        build_new_product();     break;
    case State::SelectingUser:     build_resident();        break;
    case State::Submitting:        build_submitting();      break;
    case State::Undoing:           build_undoing();         break;
    case State::Success:           build_success();         break;
    case State::Summary:           build_summary();         break;
    case State::Error:             build_error();           break;
    case State::Admin:             build_admin();           break;
  }
}

}  // namespace ui_screens
