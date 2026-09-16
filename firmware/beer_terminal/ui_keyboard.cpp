#include "ui_keyboard.h"
#include <stddef.h>

namespace ui_keyboard {
namespace {

// lv_buttonmatrix_ctrl_t is an enum, so the bitwise combinations LVGL's own C
// sources write directly need an explicit cast here.
constexpr lv_buttonmatrix_ctrl_t ctrl(uint32_t v) {
  return static_cast<lv_buttonmatrix_ctrl_t>(v);
}
// POPOVER magnifies the key on press, which is what makes a narrow key usable
// with a fingertip. LVGL keeps its equivalent private in lv_keyboard.c.
constexpr lv_buttonmatrix_ctrl_t key(uint32_t width) {
  return ctrl(LV_BUTTONMATRIX_CTRL_POPOVER | width);
}
constexpr lv_buttonmatrix_ctrl_t kCtrlKey2 =
    ctrl(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2);

const char* const kLower[] = {
    "1#", "q", "w", "e", "r", "t", "z", "u", "i", "o", "p", "ü", LV_SYMBOL_BACKSPACE, "\n",
    "ABC", "a", "s", "d", "f", "g", "h", "j", "k", "l", "ö", "ä", LV_SYMBOL_NEW_LINE, "\n",
    "_", "-", "y", "x", "c", "v", "b", "n", "m", "ß", ".", ",", "\n",
    LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""};

const char* const kUpper[] = {
    "1#", "Q", "W", "E", "R", "T", "Z", "U", "I", "O", "P", "Ü", LV_SYMBOL_BACKSPACE, "\n",
    "abc", "A", "S", "D", "F", "G", "H", "J", "K", "L", "Ö", "Ä", LV_SYMBOL_NEW_LINE, "\n",
    "_", "-", "Y", "X", "C", "V", "B", "N", "M", "ß", ".", ",", "\n",
    LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""};

// Twelve letter keys per row instead of LVGL's eleven, so the umlauts fit
// without shrinking the rest.
const lv_buttonmatrix_ctrl_t kCtrl[] = {
    ctrl(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 5),
    key(4), key(4), key(4), key(4), key(4), key(4),
    key(4), key(4), key(4), key(4), key(4),
    ctrl(LV_BUTTONMATRIX_CTRL_CHECKED | 7),

    ctrl(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 6),
    key(4), key(4), key(4), key(4), key(4), key(4),
    key(4), key(4), key(4), key(4), key(4),
    ctrl(LV_BUTTONMATRIX_CTRL_CHECKED | 7),

    ctrl(LV_BUTTONMATRIX_CTRL_CHECKED | LV_BUTTONMATRIX_CTRL_POPOVER | 2),
    ctrl(LV_BUTTONMATRIX_CTRL_CHECKED | LV_BUTTONMATRIX_CTRL_POPOVER | 2),
    key(2), key(2), key(2), key(2), key(2), key(2), key(2), key(2),
    ctrl(LV_BUTTONMATRIX_CTRL_CHECKED | LV_BUTTONMATRIX_CTRL_POPOVER | 2),
    ctrl(LV_BUTTONMATRIX_CTRL_CHECKED | LV_BUTTONMATRIX_CTRL_POPOVER | 2),

    kCtrlKey2, kCtrlKey2, ctrl(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 6), kCtrlKey2,
    kCtrlKey2};

// Each map holds one entry per button, three row breaks and a terminator, while
// the control array holds exactly one entry per button. LVGL indexes the two in
// lockstep, so a mismatch would read past the end of kCtrl at runtime.
constexpr size_t kButtons = sizeof(kCtrl) / sizeof(kCtrl[0]);
static_assert(sizeof(kLower) / sizeof(kLower[0]) == kButtons + 4,
              "lower keyboard map and control array disagree on button count");
static_assert(sizeof(kUpper) / sizeof(kUpper[0]) == kButtons + 4,
              "upper keyboard map and control array disagree on button count");

}  // namespace

void apply_german_layout(lv_obj_t* kb) {
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_LOWER, kLower, kCtrl);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_UPPER, kUpper, kCtrl);
}

}  // namespace ui_keyboard
