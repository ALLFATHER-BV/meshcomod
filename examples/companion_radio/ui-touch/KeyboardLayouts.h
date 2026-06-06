/*******************************************************************************
 * KeyboardLayouts.h — extensible on-screen + physical keyboard layout registry
 *
 * Adding a new language later only requires editing KeyboardLayouts.cpp.
 ******************************************************************************/
#pragma once

#include <stdint.h>

/* Forward-declare LVGL types so we don't pull lvgl.h into every translation unit. */
struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

enum class KeyboardLayoutId : uint8_t {
    EN = 0,
    BG = 1,
    Count
};

constexpr int KEYBOARD_LAYOUT_COUNT = static_cast<int>(KeyboardLayoutId::Count);

/** Human-readable label for a layout (e.g. "EN", "BG"). */
const char* keyboardLayoutName(KeyboardLayoutId id);

/** Apply an on-screen keyboard layout to an LVGL keyboard widget.
 *  Replaces the TEXT_LOWER and TEXT_UPPER maps; keeps the default SPECIAL map.
 */
void keyboardLayoutsApply(lv_obj_t* keyboard, KeyboardLayoutId id);

/** Currently active layout (cached in RAM). */
KeyboardLayoutId keyboardLayoutsGetCurrent();

/** Toggle between English and the configured secondary layout.
 *  If secondary is None, returns EN unchanged.
 *  Returns the new active layout.
 */
KeyboardLayoutId keyboardLayoutsToggle(lv_obj_t* keyboard);

/** Secondary keyboard getter/setter (cached in RAM). */
KeyboardLayoutId keyboardLayoutsGetSecondary();
void keyboardLayoutsSetSecondary(KeyboardLayoutId id);

/** Map a physical T-Deck key to a UTF-8 string in the given layout.
 *  @param key    ASCII code from tdeckKeyboardReadKey() (e.g. 'a', 'A', '1', '!')
 *  @param shifted true if Shift was active (key >= 'A' && key <= 'Z', or symbol)
 *  @return UTF-8 string to insert, or nullptr to pass the key through unchanged.
 */
const char* keyboardLayoutMapHwKey(KeyboardLayoutId id, int key, bool shifted);
