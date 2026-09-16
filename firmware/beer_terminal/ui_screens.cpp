#include "ui_screens.h"
#include <lvgl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "catalog_layout.h"
#include "product_catalog.h"
#include "settings.h"

namespace ui_screens {
namespace {

using app_state::Event;
using catalog_layout::GridGeometry;
using app_state::State;

// Price being composed on the NEW_PRODUCT keypad, in rappen.
int32_t g_entry_rappen = 0;
bool g_entry_free = false;
lv_obj_t* g_entry_price_label = nullptr;
lv_obj_t* g_entry_name = nullptr;
lv_obj_t* g_keyboard = nullptr;

lv_color_t col(uint32_t rgb) { return lv_color_hex(rgb); }

const lv_font_t* font_for(int16_t px) {
  if (px >= 44) return &lv_font_montserrat_48;
  if (px >= 30) return &lv_font_montserrat_32;
  if (px >= 24) return &lv_font_montserrat_28;
  return &lv_font_montserrat_20;
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

// ---- events -------------------------------------------------------------

void on_tile(lv_event_t* e) {
  const uint8_t index = static_cast<uint8_t>(
      reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  app_state::select_product(index);
  app_state::dispatch(Event::ProductSelected);
}

void on_resident(lv_event_t* e) {
  const int8_t index = static_cast<int8_t>(
      reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  app_state::context().resident_index = index;
  app_state::dispatch(Event::ResidentSelected);
}

void on_event_button(lv_event_t* e) {
  const Event ev = static_cast<Event>(
      reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  app_state::dispatch(ev);
}

void on_header_long_press(lv_event_t*) {
  app_state::dispatch(Event::AdminRequested);
}

void refresh_entry_price() {
  if (!g_entry_price_label) return;
  char buf[32];
  if (g_entry_free) {
    snprintf(buf, sizeof(buf), "Gratis");
  } else {
    snprintf(buf, sizeof(buf), "CHF %ld.%02ld",
             static_cast<long>(g_entry_rappen / 100),
             static_cast<long>(g_entry_rappen % 100));
  }
  lv_label_set_text(g_entry_price_label, buf);
}

void on_keypad(lv_event_t* e) {
  lv_obj_t* mx = static_cast<lv_obj_t*>(lv_event_get_target(e));
  const char* txt = lv_buttonmatrix_get_button_text(mx, lv_buttonmatrix_get_selected_button(mx));
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
  if (!typed || typed[0] == '\0') {
    snprintf(name, sizeof(name), "Unbekanntes Bier");
  } else {
    snprintf(name, sizeof(name), "%s", typed);
  }
  if (!g_entry_free && g_entry_rappen <= 0) return;  // needs a price or Gratis
  app_state::set_ad_hoc_product(name, g_entry_rappen, g_entry_free);
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
  lv_obj_t* l = make_label(bar, title, settings::theme::text, &lv_font_montserrat_28);
  lv_obj_align(l, LV_ALIGN_LEFT_MID, settings::grid_margin, 0);
  if (admin_gesture) {
    lv_obj_add_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(bar, on_header_long_press, LV_EVENT_LONG_PRESSED, nullptr);
  }
}

// ---- screens ------------------------------------------------------------

void build_catalog() {
  lv_obj_t* scr = build_root();
  build_header(scr, "Was trinksch?", true);

  const uint8_t n = product_catalog::count();
  const GridGeometry g = catalog_layout::grid_geometry(n);

  for (uint8_t i = 0; i < n; ++i) {
    const product_catalog::Product* p = product_catalog::at(i);
    int16_t x = 0, y = 0;
    catalog_layout::tile_position(g, n, i, x, y);

    lv_obj_t* tile = lv_button_create(scr);
    lv_obj_set_pos(tile, x, y);
    lv_obj_set_size(tile, g.tile_w, g.tile_h);
    lv_obj_set_style_bg_color(tile, col(settings::theme::surface), 0);
    lv_obj_set_style_radius(tile, 6, 0);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_set_style_shadow_width(tile, 0, 0);
    lv_obj_set_style_pad_all(tile, 14, 0);
    lv_obj_add_event_cb(tile, on_tile, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<uintptr_t>(i)));

    // Photo placeholder. Milestone 6 swaps this for the cached Open Food Facts
    // image; the deterministic colour stays as the no-photo fallback.
    const int16_t photo = static_cast<int16_t>(
        fminf(g.horizontal_card ? g.tile_h * 0.72f : g.tile_h * 0.52f,
              g.tile_w * 0.62f));
    lv_obj_t* img = make_panel(tile, 0, 0, photo, photo,
                               product_catalog::fallback_colour(*p));
    lv_obj_set_style_radius(img, 4, 0);

    char initial[2] = {p->name[0], '\0'};
    lv_obj_t* il = make_label(img, initial, 0xFFFFFF, font_for(photo / 3));
    lv_obj_set_style_text_opa(il, LV_OPA_70, 0);
    lv_obj_center(il);

    char price[32];
    product_catalog::format_price(*p, price, sizeof(price));
    const int16_t name_px = static_cast<int16_t>(fminf(g.tile_h * 0.11f, 26.0f));
    const int16_t price_px = static_cast<int16_t>(fminf(g.tile_h * 0.15f, 34.0f));

    lv_obj_t* name = make_label(tile, p->name, settings::theme::text, font_for(name_px));
    lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);
    lv_obj_t* cost = make_label(tile, price,
                                p->free_item ? settings::theme::ok : settings::theme::accent,
                                font_for(price_px));

    if (g.horizontal_card) {
      lv_obj_align(img, LV_ALIGN_LEFT_MID, 0, 0);
      const int16_t text_w = g.tile_w - photo - 28 - 14;
      lv_obj_set_width(name, text_w);
      lv_obj_align(name, LV_ALIGN_LEFT_MID, photo + 14, -price_px / 2);
      lv_obj_align(cost, LV_ALIGN_LEFT_MID, photo + 14, name_px);
    } else {
      lv_obj_align(img, LV_ALIGN_TOP_MID, 0, 0);
      lv_obj_set_width(name, g.tile_w - 28);
      lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_align(name, LV_ALIGN_TOP_MID, 0, photo + 12);
      lv_obj_align(cost, LV_ALIGN_BOTTOM_MID, 0, 0);
    }
  }

  // Constant actions live in the footer so the product count alone drives layout.
  const int16_t fy = settings::screen_h - settings::footer_h + 14;
  const int16_t fw = (settings::screen_w - 3 * settings::grid_margin) / 2;
  make_button(scr, "Nicht gelistet", settings::grid_margin, fy, fw, 44,
              settings::theme::surface_alt, settings::theme::text, on_event_button,
              reinterpret_cast<void*>(static_cast<uintptr_t>(Event::NotListed)));
  make_button(scr, "Abbrechen", 2 * settings::grid_margin + fw, fy, fw, 44,
              settings::theme::surface_alt, settings::theme::text_muted, on_event_button,
              reinterpret_cast<void*>(static_cast<uintptr_t>(Event::Sleep)));
}

void build_resident() {
  lv_obj_t* scr = build_root();
  const app_state::Context& ctx = app_state::context();

  char title[96];
  char price[32];
  if (ctx.free_item) {
    snprintf(price, sizeof(price), "Gratis");
  } else {
    snprintf(price, sizeof(price), "CHF %ld.%02ld",
             static_cast<long>(ctx.price_rappen / 100),
             static_cast<long>(ctx.price_rappen % 100));
  }
  snprintf(title, sizeof(title), "%s  -  %s", ctx.product_name, price);
  build_header(scr, title, false);

  lv_obj_t* prompt = make_label(scr, "Wer trinkt?", settings::theme::text_muted,
                                &lv_font_montserrat_20);
  lv_obj_set_pos(prompt, settings::grid_margin, settings::header_h - 4);

  // Residents use the same adaptive geometry as the catalog, one row per 3.
  const uint8_t n = settings::resident_count;
  const uint8_t cols = n <= 3 ? n : (n + 1) / 2;
  const uint8_t rows = (n + cols - 1) / cols;
  const int16_t top = settings::header_h + 28;
  const int16_t gw = settings::screen_w - 2 * settings::grid_margin;
  const int16_t gh = settings::screen_h - top - settings::footer_h - settings::grid_margin;
  const int16_t bw = (gw - (cols - 1) * settings::grid_gap) / cols;
  const int16_t bh = (gh - (rows - 1) * settings::grid_gap) / rows;

  for (uint8_t i = 0; i < n; ++i) {
    const uint8_t r = i / cols, c = i % cols;
    make_button(scr, settings::residents[i].name,
                settings::grid_margin + c * (bw + settings::grid_gap),
                top + r * (bh + settings::grid_gap), bw, bh,
                settings::theme::surface, settings::theme::text, on_resident,
                reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
  }

  make_button(scr, "Zuruck", settings::grid_margin,
              settings::screen_h - settings::footer_h + 14,
              settings::screen_w - 2 * settings::grid_margin, 44,
              settings::theme::surface_alt, settings::theme::text_muted,
              on_event_button,
              reinterpret_cast<void*>(static_cast<uintptr_t>(Event::Cancel)));
}

void build_new_product() {
  lv_obj_t* scr = build_root();
  build_header(scr, "Neues Getrank", false);

  g_entry_rappen = 0;
  g_entry_free = false;

  g_entry_name = lv_textarea_create(scr);
  lv_obj_set_pos(g_entry_name, settings::grid_margin, settings::header_h + 8);
  lv_obj_set_size(g_entry_name, 620, 68);
  lv_textarea_set_one_line(g_entry_name, true);
  lv_textarea_set_placeholder_text(g_entry_name, "Name des Getranks");
  lv_textarea_set_max_length(g_entry_name, 38);
  lv_obj_set_style_text_font(g_entry_name, &lv_font_montserrat_28, 0);
  lv_obj_set_style_bg_color(g_entry_name, col(settings::theme::surface), 0);
  lv_obj_set_style_text_color(g_entry_name, col(settings::theme::text), 0);
  lv_obj_set_style_border_width(g_entry_name, 0, 0);
  lv_obj_add_event_cb(g_entry_name, on_name_focus, LV_EVENT_ALL, nullptr);

  g_entry_price_label = make_label(scr, "CHF 0.00", settings::theme::accent,
                                   &lv_font_montserrat_48);
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
  lv_obj_set_style_text_font(pad, &lv_font_montserrat_32, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(pad, col(settings::theme::surface), LV_PART_ITEMS);
  lv_obj_set_style_text_color(pad, col(settings::theme::text), LV_PART_ITEMS);
  lv_obj_set_style_radius(pad, 6, LV_PART_ITEMS);
  lv_obj_add_event_cb(pad, on_keypad, LV_EVENT_VALUE_CHANGED, nullptr);

  const int16_t fy = settings::screen_h - settings::footer_h + 14;
  const int16_t fw = (settings::screen_w - 3 * settings::grid_margin) / 2;
  make_button(scr, "Abbrechen", settings::grid_margin, fy, fw, 44,
              settings::theme::surface_alt, settings::theme::text_muted,
              on_event_button,
              reinterpret_cast<void*>(static_cast<uintptr_t>(Event::Cancel)));
  make_button(scr, "Weiter", 2 * settings::grid_margin + fw, fy, fw, 44,
              settings::theme::accent, 0x12120F, on_new_product_confirm, nullptr);

  g_keyboard = lv_keyboard_create(scr);
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
                           &lv_font_montserrat_32);
  lv_obj_align(l, LV_ALIGN_CENTER, 0, 60);
}

void build_success() {
  lv_obj_t* scr = build_root();
  const app_state::Context& ctx = app_state::context();
  lv_obj_set_style_bg_color(scr, col(settings::theme::ok), 0);

  char line[128];
  const char* who = (ctx.resident_index >= 0 &&
                     ctx.resident_index < static_cast<int8_t>(settings::resident_count))
                        ? settings::residents[ctx.resident_index].name
                        : "?";
  snprintf(line, sizeof(line), "%s: %s", who, ctx.product_name);

  lv_obj_t* big = make_label(scr, "Gebucht", 0xFFFFFF, &lv_font_montserrat_48);
  lv_obj_align(big, LV_ALIGN_CENTER, 0, -50);
  lv_obj_t* l = make_label(scr, line, 0xFFFFFF, &lv_font_montserrat_32);
  lv_obj_align(l, LV_ALIGN_CENTER, 0, 20);
}

void build_error() {
  lv_obj_t* scr = build_root();
  const app_state::Context& ctx = app_state::context();
  build_header(scr, "Fehler", false);

  lv_obj_t* l = make_label(
      scr, ctx.message[0] ? ctx.message : "Unbekannter Fehler",
      settings::theme::text, &lv_font_montserrat_32);
  lv_obj_align(l, LV_ALIGN_CENTER, 0, -40);

  lv_obj_t* hint = make_label(scr, "Der Eintrag wird nicht doppelt erfasst.",
                              settings::theme::text_muted, &lv_font_montserrat_20);
  lv_obj_align(hint, LV_ALIGN_CENTER, 0, 10);

  const int16_t fy = settings::screen_h - settings::footer_h + 14;
  const int16_t fw = (settings::screen_w - 3 * settings::grid_margin) / 2;
  make_button(scr, "Abbrechen", settings::grid_margin, fy, fw, 44,
              settings::theme::surface_alt, settings::theme::text_muted,
              on_event_button,
              reinterpret_cast<void*>(static_cast<uintptr_t>(Event::Cancel)));
  make_button(scr, "Nochmal versuchen", 2 * settings::grid_margin + fw, fy, fw, 44,
              settings::theme::accent, 0x12120F, on_event_button,
              reinterpret_cast<void*>(static_cast<uintptr_t>(Event::Retry)));
}

void build_admin() {
  lv_obj_t* scr = build_root();
  build_header(scr, "Admin", false);

  int16_t y = settings::header_h + 8;
  for (uint8_t i = 0; i < product_catalog::count(); ++i) {
    const product_catalog::Product* p = product_catalog::at(i);
    char price[32];
    product_catalog::format_price(*p, price, sizeof(price));
    char row[80];
    snprintf(row, sizeof(row), "%-30s %10s", p->name, price);
    lv_obj_t* l = make_label(scr, row, settings::theme::text, &lv_font_montserrat_20);
    lv_obj_set_pos(l, settings::grid_margin, y);
    y += 30;
  }

  lv_obj_t* note = make_label(scr, "Preisanderung folgt in Meilenstein 11.",
                              settings::theme::text_muted, &lv_font_montserrat_20);
  lv_obj_set_pos(note, settings::grid_margin, y + 10);

  lv_obj_t* sw_label = make_label(scr, "Nachsten Fehler simulieren",
                                  settings::theme::text_muted, &lv_font_montserrat_20);
  lv_obj_set_pos(sw_label, 700, settings::header_h + 12);
  lv_obj_t* sw = lv_switch_create(scr);
  lv_obj_set_pos(sw, 700, settings::header_h + 44);
  lv_obj_set_size(sw, 90, 46);
  if (app_state::failure_simulated()) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw, on_simulate_failure, LV_EVENT_VALUE_CHANGED, nullptr);

  make_button(scr, "Zuruck", settings::grid_margin,
              settings::screen_h - settings::footer_h + 14,
              settings::screen_w - 2 * settings::grid_margin, 44,
              settings::theme::surface_alt, settings::theme::text,
              on_event_button,
              reinterpret_cast<void*>(static_cast<uintptr_t>(Event::AdminDone)));
}

void build_sleeping() {
  lv_obj_t* scr = build_root();
  lv_obj_t* l = make_label(scr, "Bildschirm tippen", settings::theme::text_muted,
                           &lv_font_montserrat_28);
  lv_obj_center(l);
  lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(scr, on_event_button, LV_EVENT_CLICKED,
                      reinterpret_cast<void*>(static_cast<uintptr_t>(Event::Wake)));
}

}  // namespace

void begin() { show(app_state::state()); }

void show(State current) {
  switch (current) {
    case State::Sleeping:         build_sleeping();    break;
    case State::Waking:                                break;
    case State::SelectingProduct: build_catalog();     break;
    case State::LookingUp:        build_submitting();  break;
    case State::ProductFound:     build_catalog();     break;
    case State::NewProduct:       build_new_product(); break;
    case State::SelectingUser:    build_resident();    break;
    case State::Submitting:       build_submitting();  break;
    case State::Success:          build_success();     break;
    case State::Error:            build_error();       break;
    case State::Admin:            build_admin();       break;
  }
}

}  // namespace ui_screens
