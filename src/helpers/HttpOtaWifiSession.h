#pragma once

#include <stdint.h>

/** Control path for HTTP OTA (CommonCLI / repeater TCP companion). */
enum : uint8_t {
  MESHCORE_HTTP_OTA_PATH_NONE = 0,
  MESHCORE_HTTP_OTA_PATH_TCP = 1,
  MESHCORE_HTTP_OTA_PATH_WS = 2,
};

/** Repeater TCP/WS companion emit context (shared by repeater_tcp/main.cpp and simple_repeater/MyMesh.cpp). */
struct MeshcoreRepeaterEmitCtx {
  void *tcp;
  void *ws;
  uint8_t transport_path;  // MESHCORE_HTTP_OTA_PATH_TCP or MESHCORE_HTTP_OTA_PATH_WS
  int client_index;
};
