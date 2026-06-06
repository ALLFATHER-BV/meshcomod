/*******************************************************************************
 * KeyboardLayouts.cpp — keyboard map data for on-screen + T-Deck physical input
 *
 * To add a new language later:
 *   1. Add an entry to KeyboardLayoutId in KeyboardLayouts.h
 *   2. Define OsKeyboardLayout and HwKeyboardLayout structs below
 *   3. Append them to k_os_layouts[] and k_hw_layouts[]
 *   4. Update KEYBOARD_LAYOUT_COUNT in KeyboardLayouts.h
 ******************************************************************************/
#include "KeyboardLayouts.h"

#if 1
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

/* ================================================================
 * On-screen keyboard (LVGL lv_keyboard) maps
 * ================================================================ */

/* Helper: build a control entry for a special key with a given width multiplier. */
static constexpr lv_btnmatrix_ctrl_t KC(uint8_t width_mul) {
    return LV_BTNMATRIX_CTRL_NO_REPEAT | (width_mul << 4);
}

/* ---------- Bulgarian on-screen keyboard ---------- */

/* Lower-case Cyrillic, 3 letter rows + 1 control row. */
static const char* kb_bg_lower[] = {
    "я","в","е","р","т","ъ","у","и","о","п","\n",
    "а","с","д","ф","г","х","й","к","л","ь","\n",
    "з","ц","ч","ж","б","н","м","ш","щ","ю","\n",
    LV_SYMBOL_UP, "1#", " ", LV_SYMBOL_BACKSPACE, LV_SYMBOL_OK, NULL
};

static const lv_btnmatrix_ctrl_t kb_bg_lower_ctrl[] = {
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    KC(2), KC(1), KC(4), KC(2), KC(1)
};

/* Upper-case Cyrillic. */
static const char* kb_bg_upper[] = {
    "Я","В","Е","Р","Т","Ъ","У","И","О","П","\n",
    "А","С","Д","Ф","Г","Х","Й","К","Л","Ь","\n",
    "З","Ц","Ч","Ж","Б","Н","М","Ш","Щ","Ю","\n",
    LV_SYMBOL_UP, "1#", " ", LV_SYMBOL_BACKSPACE, LV_SYMBOL_OK, NULL
};

static const lv_btnmatrix_ctrl_t kb_bg_upper_ctrl[] = {
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    KC(2), KC(1), KC(4), KC(2), KC(1)
};

/* ---------- English on-screen keyboard (mirrors LVGL defaults) ---------- */
/* We keep explicit copies so we can always restore them after switching to BG. */
static const char* kb_en_lower[] = {
    "1","2","3","4","5","6","7","8","9","0","\n",
    "q","w","e","r","t","y","u","i","o","p","\n",
    "a","s","d","f","g","h","j","k","l","\n",
    "z","x","c","v","b","n","m", LV_SYMBOL_BACKSPACE,"\n",
    LV_SYMBOL_CLOSE, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, NULL
};

static const lv_btnmatrix_ctrl_t kb_en_lower_ctrl[] = {
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0, KC(2),
    KC(2), KC(1), KC(6), KC(1), KC(2)
};

static const char* kb_en_upper[] = {
    "1","2","3","4","5","6","7","8","9","0","\n",
    "Q","W","E","R","T","Y","U","I","O","P","\n",
    "A","S","D","F","G","H","J","K","L","\n",
    "Z","X","C","V","B","N","M", LV_SYMBOL_BACKSPACE,"\n",
    LV_SYMBOL_CLOSE, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, NULL
};

static const lv_btnmatrix_ctrl_t kb_en_upper_ctrl[] = {
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0, KC(2),
    KC(2), KC(1), KC(6), KC(1), KC(2)
};

struct OsKeyboardLayout {
    KeyboardLayoutId id;
    const char*      name;
    const char**     lower_map;
    const lv_btnmatrix_ctrl_t* lower_ctrl;
    const char**     upper_map;
    const lv_btnmatrix_ctrl_t* upper_ctrl;
};

static const OsKeyboardLayout k_os_layouts[] = {
    { KeyboardLayoutId::EN, "EN",
      kb_en_lower, kb_en_lower_ctrl,
      kb_en_upper, kb_en_upper_ctrl },
    { KeyboardLayoutId::BG, "BG",
      kb_bg_lower, kb_bg_lower_ctrl,
      kb_bg_upper, kb_bg_upper_ctrl },
};

/* ================================================================
 * T-Deck physical keyboard (phonetic transliteration)
 * ================================================================ */

struct HwKeyboardLayout {
    KeyboardLayoutId id;
    const char* name;
};

static const HwKeyboardLayout k_hw_layouts[] = {
    { KeyboardLayoutId::EN, "EN" },
    { KeyboardLayoutId::BG, "BG" },
};

/* Bulgarian phonetic mapping for T-Deck.
 * Index 0 = 'a' / 'A', 1 = 'b' / 'B', etc.
 * nullptr = pass through unchanged (not a mapped letter). */
static const char* hw_bg_lower[26] = {
    "а", "б", "ц", "д", "е", "ф", "г", "х", "и", "й",
    "к", "л", "м", "н", "о", "п", "я", "р", "с", "т",
    "у", "ж", "в", "ь", "ъ", "з"
};

static const char* hw_bg_upper[26] = {
    "А", "Б", "Ц", "Д", "Е", "Ф", "Г", "Х", "И", "Й",
    "К", "Л", "М", "Н", "О", "П", "Я", "Р", "С", "Т",
    "У", "Ж", "В", "Ь", "Ъ", "З"
};

/* Digits 1-4 map to Cyrillic letters; 5-9,0 pass through as numbers.
 * Note: 'ю' replaces the earlier Russian 'э' so the Bulgarian alphabet
 * is fully covered (30 letters). */
static const char* hw_bg_digits[10] = {
    /* 0 */ nullptr,
    /* 1 */ "ш",
    /* 2 */ "щ",
    /* 3 */ "ч",
    /* 4 */ "ю",
    /* 5 */ nullptr,
    /* 6 */ nullptr,
    /* 7 */ nullptr,
    /* 8 */ nullptr,
    /* 9 */ nullptr
};

static const char* hw_bg_digits_shift[10] = {
    /* 0 */ nullptr,
    /* 1 */ "Ш",
    /* 2 */ "Щ",
    /* 3 */ "Ч",
    /* 4 */ "Ю",
    /* 5 */ nullptr,
    /* 6 */ nullptr,
    /* 7 */ nullptr,
    /* 8 */ nullptr,
    /* 9 */ nullptr
};

/* ================================================================
 * Runtime state
 * ================================================================ */
static KeyboardLayoutId s_current_layout = KeyboardLayoutId::EN;
static KeyboardLayoutId s_secondary_layout = KeyboardLayoutId::EN;   // None = EN internally

/* ================================================================
 * Public API
 * ================================================================ */

const char* keyboardLayoutName(KeyboardLayoutId id) {
    if (static_cast<uint8_t>(id) >= KEYBOARD_LAYOUT_COUNT) id = KeyboardLayoutId::EN;
    return k_os_layouts[static_cast<int>(id)].name;
}

void keyboardLayoutsApply(lv_obj_t* keyboard, KeyboardLayoutId id) {
    if (!keyboard) return;
    if (static_cast<uint8_t>(id) >= KEYBOARD_LAYOUT_COUNT) id = KeyboardLayoutId::EN;
    s_current_layout = id;

  const OsKeyboardLayout& lo = k_os_layouts[static_cast<int>(id)];
  lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER, lo.lower_map, lo.lower_ctrl);
  lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_UPPER, lo.upper_map, lo.upper_ctrl);
}

KeyboardLayoutId keyboardLayoutsGetCurrent() {
    return s_current_layout;
}

KeyboardLayoutId keyboardLayoutsToggle(lv_obj_t* keyboard) {
    if (s_secondary_layout == KeyboardLayoutId::EN) {
        return s_current_layout;  // no secondary configured — nothing to toggle
    }
    KeyboardLayoutId next = (s_current_layout == KeyboardLayoutId::EN)
                            ? s_secondary_layout
                            : KeyboardLayoutId::EN;
    keyboardLayoutsApply(keyboard, next);
    return s_current_layout;
}

KeyboardLayoutId keyboardLayoutsGetSecondary() {
    return s_secondary_layout;
}

void keyboardLayoutsSetSecondary(KeyboardLayoutId id) {
    if (static_cast<uint8_t>(id) >= KEYBOARD_LAYOUT_COUNT) id = KeyboardLayoutId::EN;
    s_secondary_layout = id;
    // If the current layout is no longer valid (e.g. we removed the secondary
    // that was currently active), fall back to English.
    if (s_current_layout != KeyboardLayoutId::EN && s_current_layout != s_secondary_layout) {
        s_current_layout = KeyboardLayoutId::EN;
    }
}

const char* keyboardLayoutMapHwKey(KeyboardLayoutId id, int key, bool shifted) {
    if (id != KeyboardLayoutId::BG) return nullptr;  // English = pass-through

    if (key >= 'a' && key <= 'z') {
        return hw_bg_lower[key - 'a'];
    }
    if (key >= 'A' && key <= 'Z') {
        return hw_bg_upper[key - 'A'];
    }
    if (key >= '0' && key <= '9') {
        int idx = key - '0';
        return shifted ? hw_bg_digits_shift[idx] : hw_bg_digits[idx];
    }
    /* Shifted symbols that correspond to 1-4 on a US QWERTY layout */
    switch (key) {
        case '!': return "Ш";
        case '@': return "Щ";
        case '#': return "Ч";
        case '$': return "Ю";
        default:  break;
    }
    return nullptr;  /* pass through unchanged (space, punctuation, etc.) */
}
