#pragma once

#include <stdint.h>
#include <stddef.h>

#if defined(REPEATER_TCP_COMPANION) && defined(ESP32)

/** While handling MESHCM `ota url`, firmware may push extra 0x8C lines (same tag as final reply). */
void meshcoreRepeaterTcpOtaEmitBegin(void (*emit)(void *, const uint8_t *, size_t), void *ctx, uint32_t tag);
void meshcoreRepeaterTcpOtaEmitEnd();
void meshcoreRepeaterTcpOtaEmitLine(const char *line);

#else

static inline void meshcoreRepeaterTcpOtaEmitBegin(void (*)(void *, const uint8_t *, size_t), void *, uint32_t) {}
static inline void meshcoreRepeaterTcpOtaEmitEnd() {}
static inline void meshcoreRepeaterTcpOtaEmitLine(const char *) {}

#endif
