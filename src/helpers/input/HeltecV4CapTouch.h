#pragma once

#if defined(HAS_HELTEC_V4_CAP_TOUCH) && defined(ESP32)

void heltecV4CapTouchBegin();
/** Returns BUTTON_EVENT_* (same as MomentaryButton::check). */
int heltecV4CapTouchCheck();

#endif
