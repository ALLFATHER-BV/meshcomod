// meshcomod room server: framed transport (handshake + explicit unsupported for companion chat/sync paths).
// Built only for ESP32 envs that define MESHCOMOD_ROOM_MULTITRANSPORT and add this TU via build_src_filter.

#include "../simple_room_server/MyMesh.h"

#if defined(ESP32) && defined(MESHCOMOD_ROOM_MULTITRANSPORT)

#include "../repeater_tcp/repeater_companion_proto.h"
#include <helpers/AdvertDataHelpers.h>
#include <helpers/TxtDataHelpers.h>
#include <string.h>
#include <target.h>

#ifndef FIRMWARE_VER_CODE
#define FIRMWARE_VER_CODE 27
#endif

#ifndef MAX_GROUP_CHANNELS
#define MAX_GROUP_CHANNELS 1
#endif

#ifndef MAX_CONTACTS
#define MAX_CONTACTS 100
#endif

#ifndef MAX_CLIENT_ID_LEN
#define MAX_CLIENT_ID_LEN 31
#endif

#ifndef MAX_LORA_TX_POWER
#define MAX_LORA_TX_POWER LORA_TX_POWER
#endif

#define CMD_DEVICE_QEURY CMD_DEVICE_QUERY

static void writeRoomTransportErr(BaseSerialInterface* serial, uint8_t err) {
  uint8_t buf[2] = {RESP_CODE_ERR, err};
  serial->writeFrame(buf, 2);
}

void MyMesh::handleRoomTransportCmdFrame(size_t len) {
  if (!_serial || len == 0) return;

  if (cmd_frame[0] == CMD_DEVICE_QEURY && len >= 2) {
    _room_transport_app_ver = cmd_frame[1];
    (void)_room_transport_app_ver;
    int i = 0;
    out_frame[i++] = RESP_CODE_DEVICE_INFO;
    out_frame[i++] = FIRMWARE_VER_CODE;
    out_frame[i++] = (uint8_t)(MAX_CONTACTS / 2);
    out_frame[i++] = MAX_GROUP_CHANNELS;
    uint32_t ble_pin = 0;
#if defined(BLE_PIN_CODE)
    ble_pin = (uint32_t)BLE_PIN_CODE;
#endif
    memcpy(&out_frame[i], &ble_pin, 4);
    i += 4;
    memset(&out_frame[i], 0, 12);
    StrHelper::strzcpy((char*)&out_frame[i], FIRMWARE_BUILD_DATE, 12);
    i += 12;
    StrHelper::strzcpy((char*)&out_frame[i], board.getManufacturerName(), 40);
    i += 40;
    StrHelper::strzcpy((char*)&out_frame[i], FIRMWARE_VERSION, 20);
    i += 20;
    out_frame[i++] = 1;
    out_frame[i++] = _prefs.path_hash_mode;
    _serial->writeFrame(out_frame, (size_t)i);
    return;
  }

  if (cmd_frame[0] == CMD_APP_START && len >= 8) {
    char* app_name;
    if (len >= 2 && cmd_frame[1] > 0 && len >= 2 + (size_t)cmd_frame[1]) {
      uint8_t cid_len = cmd_frame[1];
      if (cid_len > MAX_CLIENT_ID_LEN) cid_len = MAX_CLIENT_ID_LEN;
      char cid_buf[MAX_CLIENT_ID_LEN + 1];
      memcpy(cid_buf, &cmd_frame[2], cid_len);
      cid_buf[cid_len] = '\0';
      _serial->setCurrentClientId(cid_buf);
      app_name = (char*)&cmd_frame[2 + cmd_frame[1]];
    } else {
      app_name = (char*)&cmd_frame[8];
    }
    cmd_frame[len] = 0;
    (void)app_name;

    int i = 0;
    out_frame[i++] = RESP_CODE_SELF_INFO;
    out_frame[i++] = ADV_TYPE_ROOM;
    out_frame[i++] = (uint8_t)_prefs.tx_power_dbm;
    out_frame[i++] = MAX_LORA_TX_POWER;
    memcpy(&out_frame[i], self_id.pub_key, PUB_KEY_SIZE);
    i += PUB_KEY_SIZE;

    int32_t lat = (int32_t)(sensors.node_lat * 1000000.0);
    int32_t lon = (int32_t)(sensors.node_lon * 1000000.0);
    memcpy(&out_frame[i], &lat, 4);
    i += 4;
    memcpy(&out_frame[i], &lon, 4);
    i += 4;
    out_frame[i++] = _prefs.multi_acks;
    out_frame[i++] = _prefs.advert_loc_policy;
    out_frame[i++] = 0;
    out_frame[i++] = 0;

    uint32_t freq = (uint32_t)(_prefs.freq * 1000);
    memcpy(&out_frame[i], &freq, 4);
    i += 4;
    uint32_t bw = (uint32_t)(_prefs.bw * 1000);
    memcpy(&out_frame[i], &bw, 4);
    i += 4;
    out_frame[i++] = _prefs.sf;
    out_frame[i++] = _prefs.cr;

    int tlen = (int)strlen(_prefs.node_name);
    memcpy(&out_frame[i], _prefs.node_name, (size_t)tlen);
    i += tlen;
    _serial->writeFrame(out_frame, (size_t)i);
    return;
  }

  writeRoomTransportErr(_serial, ERR_CODE_UNSUPPORTED_CMD);
}

#endif
