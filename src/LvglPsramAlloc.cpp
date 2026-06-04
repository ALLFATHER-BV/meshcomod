#include "LvglPsramAlloc.h"

#if defined(ESP32)
  #include <esp_heap_caps.h>
#endif
#include <stdlib.h>
#include <string.h>

// PSRAM allocations need MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT. The OR-ed mask
// means "must satisfy both" — i.e. backed by SPIRAM and byte-addressable
// (LVGL writes/reads bytes, never instruction fetches). Fall back to default
// malloc when SPIRAM is exhausted or the request is too small for the SPIRAM
// pool to satisfy efficiently (the IDF heap will sometimes refuse very small
// SPIRAM allocations and route through DRAM anyway).

extern "C" void* lvglPsramAlloc(size_t size) {
  if (size == 0) return nullptr;
#if defined(ESP32)
  void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (p) return p;
#endif
  return malloc(size);
}

extern "C" void lvglPsramFree(void* ptr) {
  if (!ptr) return;
#if defined(ESP32)
  // heap_caps_free works for both SPIRAM and DRAM allocations.
  heap_caps_free(ptr);
#else
  free(ptr);
#endif
}

extern "C" void* lvglPsramRealloc(void* ptr, size_t size) {
  if (size == 0) {
    lvglPsramFree(ptr);
    return nullptr;
  }
  if (!ptr) {
    return lvglPsramAlloc(size);
  }
  // Plain realloc preserves whatever heap the original block was on (PSRAM
  // stays in PSRAM, DRAM stays in DRAM). Migrating between heaps on realloc
  // would need to know the original size to memcpy safely; not worth the
  // complexity for LVGL's usage pattern.
  return realloc(ptr, size);
}
