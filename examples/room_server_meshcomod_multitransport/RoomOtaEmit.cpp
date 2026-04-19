// Links MultiTransport / ESP32Board HTTP OTA diagnostics to the room multitransport serial stack.

#include "../simple_room_server/MyMesh.h"

#if defined(ESP32) && defined(MESHCOMOD_ROOM_MULTITRANSPORT)

#include "../repeater_tcp/repeater_companion_proto.h"
#include <Arduino.h>
#include <string.h>

extern MyMesh the_mesh;

void MyMesh::pushRoomOtaProgressLine(const char* line) {
  if (!line || !_serial) return;
  if (!_serial->isConnected()) return;
  int j = 0;
  out_frame[j++] = PUSH_CODE_BINARY_RESPONSE;
  out_frame[j++] = 0;
  uint32_t tag = 0;
  memcpy(&out_frame[j], &tag, 4);
  j += 4;
  int ll = (int)strlen(line);
  if (j + ll > MAX_FRAME_SIZE) ll = MAX_FRAME_SIZE - j;
  memcpy(&out_frame[j], line, (size_t)ll);
  j += ll;
  _serial->writeFrame(out_frame, (size_t)j);
}

void meshcoreRepeaterTcpOtaEmitLine(const char* line) {
  the_mesh.pushRoomOtaProgressLine(line);
  if (line && line[0]) {
    Serial.printf("%s\n", line);
  }
}

#endif
