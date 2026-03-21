#include "MyMesh.h"
#include <algorithm>

#if defined(REPEATER_TCP_COMPANION) && defined(ESP32)
#include <WiFi.h>
#include <cstring>
#include <strings.h>
#include <helpers/esp32/WifiRuntimeStore.h>
extern void repeater_on_wifi_radio_toggled();
#endif

/* ------------------------------ Config -------------------------------- */

#ifndef LORA_FREQ
  #define LORA_FREQ 915.0
#endif
#ifndef LORA_BW
  #define LORA_BW 250
#endif
#ifndef LORA_SF
  #define LORA_SF 10
#endif
#ifndef LORA_CR
  #define LORA_CR 5
#endif
#ifndef LORA_TX_POWER
  #define LORA_TX_POWER 20
#endif

#ifndef ADVERT_NAME
  #define ADVERT_NAME "repeater"
#endif
#ifndef ADVERT_LAT
  #define ADVERT_LAT 0.0
#endif
#ifndef ADVERT_LON
  #define ADVERT_LON 0.0
#endif

#ifndef ADMIN_PASSWORD
  #define ADMIN_PASSWORD "password"
#endif

#ifndef SERVER_RESPONSE_DELAY
  #define SERVER_RESPONSE_DELAY 300
#endif

#ifndef TXT_ACK_DELAY
  #define TXT_ACK_DELAY 200
#endif

#define FIRMWARE_VER_LEVEL       2

#define REQ_TYPE_GET_STATUS         0x01 // same as _GET_STATS
#define REQ_TYPE_KEEP_ALIVE         0x02
#define REQ_TYPE_GET_TELEMETRY_DATA 0x03
#define REQ_TYPE_GET_ACCESS_LIST    0x05
#define REQ_TYPE_GET_NEIGHBOURS     0x06
#define REQ_TYPE_GET_OWNER_INFO     0x07     // FIRMWARE_VER_LEVEL >= 2

#define RESP_SERVER_LOGIN_OK        0 // response to ANON_REQ

#define ANON_REQ_TYPE_REGIONS      0x01
#define ANON_REQ_TYPE_OWNER        0x02
#define ANON_REQ_TYPE_BASIC        0x03   // just remote clock

#define CLI_REPLY_DELAY_MILLIS      600

#define LAZY_CONTACTS_WRITE_DELAY    5000

void MyMesh::putNeighbour(const mesh::Identity &id, uint32_t timestamp, float snr) {
#if MAX_NEIGHBOURS // check if neighbours enabled
  // find existing neighbour, else use least recently updated
  uint32_t oldest_timestamp = 0xFFFFFFFF;
  NeighbourInfo *neighbour = &neighbours[0];
  for (int i = 0; i < MAX_NEIGHBOURS; i++) {
    // if neighbour already known, we should update it
    if (id.matches(neighbours[i].id)) {
      neighbour = &neighbours[i];
      break;
    }

    // otherwise we should update the least recently updated neighbour
    if (neighbours[i].heard_timestamp < oldest_timestamp) {
      neighbour = &neighbours[i];
      oldest_timestamp = neighbour->heard_timestamp;
    }
  }

  // update neighbour info
  neighbour->id = id;
  neighbour->advert_timestamp = timestamp;
  neighbour->heard_timestamp = getRTCClock()->getCurrentTime();
  neighbour->snr = (int8_t)(snr * 4);
#endif
}

uint8_t MyMesh::handleLoginReq(const mesh::Identity& sender, const uint8_t* secret, uint32_t sender_timestamp, const uint8_t* data, bool is_flood) {
  ClientInfo* client = NULL;
  if (data[0] == 0) {   // blank password, just check if sender is in ACL
    client = acl.getClient(sender.pub_key, PUB_KEY_SIZE);
    if (client == NULL) {
    #if MESH_DEBUG
      MESH_DEBUG_PRINTLN("Login, sender not in ACL");
    #endif
    }
  }
  if (client == NULL) {
    uint8_t perms;
    if (strcmp((char *)data, _prefs.password) == 0) { // check for valid admin password
      perms = PERM_ACL_ADMIN;
    } else if (strcmp((char *)data, _prefs.guest_password) == 0) { // check guest password
      perms = PERM_ACL_GUEST;
    } else {
#if MESH_DEBUG
      MESH_DEBUG_PRINTLN("Invalid password: %s", data);
#endif
      return 0;
    }

    client = acl.putClient(sender, 0);  // add to contacts (if not already known)
    if (sender_timestamp <= client->last_timestamp) {
      MESH_DEBUG_PRINTLN("Possible login replay attack!");
      return 0;  // FATAL: client table is full -OR- replay attack
    }

    MESH_DEBUG_PRINTLN("Login success!");
    client->last_timestamp = sender_timestamp;
    client->last_activity = getRTCClock()->getCurrentTime();
    client->permissions &= ~0x03;
    client->permissions |= perms;
    memcpy(client->shared_secret, secret, PUB_KEY_SIZE);

    if (perms != PERM_ACL_GUEST) {   // keep number of FS writes to a minimum
      dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
    }
  }

  if (is_flood) {
    client->out_path_len = OUT_PATH_UNKNOWN;  // need to rediscover out_path
  }

  uint32_t now = getRTCClock()->getCurrentTimeUnique();
  memcpy(reply_data, &now, 4);   // response packets always prefixed with timestamp
  reply_data[4] = RESP_SERVER_LOGIN_OK;
  reply_data[5] = 0;  // Legacy: was recommended keep-alive interval (secs / 16)
  reply_data[6] = client->isAdmin() ? 1 : 0;
  reply_data[7] = client->permissions;
  getRNG()->random(&reply_data[8], 4);   // random blob to help packet-hash uniqueness
  reply_data[12] = FIRMWARE_VER_LEVEL;  // New field

  return 13;  // reply length
}

uint8_t MyMesh::handleAnonRegionsReq(const mesh::Identity& sender, uint32_t sender_timestamp, const uint8_t* data) {
  if (anon_limiter.allow(rtc_clock.getCurrentTime())) {
    // request data has: {reply-path-len}{reply-path}
    reply_path_len = *data & 63;
    reply_path_hash_size = (*data >> 6) + 1;
    data++;

    memcpy(reply_path, data, ((uint8_t)reply_path_len) * reply_path_hash_size);
    // data += (uint8_t)reply_path_len * reply_path_hash_size;

    memcpy(reply_data, &sender_timestamp, 4);   // prefix with sender_timestamp, like a tag
    uint32_t now = getRTCClock()->getCurrentTime();
    memcpy(&reply_data[4], &now, 4);     // include our clock (for easy clock sync, and packet hash uniqueness)

    return 8 + region_map.exportNamesTo((char *) &reply_data[8], sizeof(reply_data) - 12, REGION_DENY_FLOOD);   // reply length
  }
  return 0;
}

uint8_t MyMesh::handleAnonOwnerReq(const mesh::Identity& sender, uint32_t sender_timestamp, const uint8_t* data) {
  if (anon_limiter.allow(rtc_clock.getCurrentTime())) {
    // request data has: {reply-path-len}{reply-path}
    reply_path_len = *data & 63;
    reply_path_hash_size = (*data >> 6) + 1;
    data++;

    memcpy(reply_path, data, ((uint8_t)reply_path_len) * reply_path_hash_size);
    // data += (uint8_t)reply_path_len * reply_path_hash_size;

    memcpy(reply_data, &sender_timestamp, 4);   // prefix with sender_timestamp, like a tag
    uint32_t now = getRTCClock()->getCurrentTime();
    memcpy(&reply_data[4], &now, 4);     // include our clock (for easy clock sync, and packet hash uniqueness)
    sprintf((char *) &reply_data[8], "%s\n%s", _prefs.node_name, _prefs.owner_info);

    return 8 + strlen((char *) &reply_data[8]);   // reply length
  }
  return 0;
}

uint8_t MyMesh::handleAnonClockReq(const mesh::Identity& sender, uint32_t sender_timestamp, const uint8_t* data) {
  if (anon_limiter.allow(rtc_clock.getCurrentTime())) {
    // request data has: {reply-path-len}{reply-path}
    reply_path_len = *data & 63;
    reply_path_hash_size = (*data >> 6) + 1;
    data++;

    memcpy(reply_path, data, ((uint8_t)reply_path_len) * reply_path_hash_size);
    // data += (uint8_t)reply_path_len * reply_path_hash_size;

    memcpy(reply_data, &sender_timestamp, 4);   // prefix with sender_timestamp, like a tag
    uint32_t now = getRTCClock()->getCurrentTime();
    memcpy(&reply_data[4], &now, 4);     // include our clock (for easy clock sync, and packet hash uniqueness)
    reply_data[8] = 0;  // features
#ifdef WITH_RS232_BRIDGE
    reply_data[8] |= 0x01;  // is bridge, type UART
#elif WITH_ESPNOW_BRIDGE
    reply_data[8] |= 0x03;  // is bridge, type ESP-NOW
#endif
    if (_prefs.disable_fwd) {   // is this repeater currently disabled
      reply_data[8] |= 0x80;  // is disabled
    }
    // TODO:  add some kind of moving-window utilisation metric, so can query 'how busy' is this repeater
    return 9;   // reply length
  }
  return 0;
}

int MyMesh::handleRequest(ClientInfo *sender, uint32_t sender_timestamp, uint8_t *payload, size_t payload_len) {
  // uint32_t now = getRTCClock()->getCurrentTimeUnique();
  // memcpy(reply_data, &now, 4);   // response packets always prefixed with timestamp
  memcpy(reply_data, &sender_timestamp, 4); // reflect sender_timestamp back in response packet (kind of like a 'tag')

  if (payload[0] == REQ_TYPE_GET_STATUS) {  // guests can also access this now
    RepeaterStats stats;
    stats.batt_milli_volts = board.getBattMilliVolts();
    stats.curr_tx_queue_len = _mgr->getOutboundCount(0xFFFFFFFF);
    stats.noise_floor = (int16_t)_radio->getNoiseFloor();
    stats.last_rssi = (int16_t)radio_driver.getLastRSSI();
    stats.n_packets_recv = radio_driver.getPacketsRecv();
    stats.n_packets_sent = radio_driver.getPacketsSent();
    stats.total_air_time_secs = getTotalAirTime() / 1000;
    stats.total_up_time_secs = uptime_millis / 1000;
    stats.n_sent_flood = getNumSentFlood();
    stats.n_sent_direct = getNumSentDirect();
    stats.n_recv_flood = getNumRecvFlood();
    stats.n_recv_direct = getNumRecvDirect();
    stats.err_events = _err_flags;
    stats.last_snr = (int16_t)(radio_driver.getLastSNR() * 4);
    stats.n_direct_dups = ((SimpleMeshTables *)getTables())->getNumDirectDups();
    stats.n_flood_dups = ((SimpleMeshTables *)getTables())->getNumFloodDups();
    stats.total_rx_air_time_secs = getReceiveAirTime() / 1000;
    stats.n_recv_errors = radio_driver.getPacketsRecvErrors();
    memcpy(&reply_data[4], &stats, sizeof(stats));

    return 4 + sizeof(stats); //  reply_len
  }
  if (payload[0] == REQ_TYPE_GET_TELEMETRY_DATA) {
    uint8_t perm_mask = ~(payload[1]); // NEW: first reserved byte (of 4), is now inverse mask to apply to permissions

    telemetry.reset();
    telemetry.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);

    // query other sensors -- target specific
    if ((sender->permissions & PERM_ACL_ROLE_MASK) == PERM_ACL_GUEST) {
      perm_mask = 0x00;  // just base telemetry allowed
    }
    sensors.querySensors(perm_mask, telemetry);

	// This default temperature will be overridden by external sensors (if any)
    float temperature = board.getMCUTemperature();
    if(!isnan(temperature)) { // Supported boards with built-in temperature sensor. ESP32-C3 may return NAN
      telemetry.addTemperature(TELEM_CHANNEL_SELF, temperature); // Built-in MCU Temperature
    }

    uint8_t tlen = telemetry.getSize();
    memcpy(&reply_data[4], telemetry.getBuffer(), tlen);
    return 4 + tlen; // reply_len
  }
  if (payload[0] == REQ_TYPE_GET_ACCESS_LIST && sender->isAdmin()) {
    uint8_t res1 = payload[1];   // reserved for future  (extra query params)
    uint8_t res2 = payload[2];
    if (res1 == 0 && res2 == 0) {
      uint8_t ofs = 4;
      for (int i = 0; i < acl.getNumClients() && ofs + 7 <= sizeof(reply_data) - 4; i++) {
        auto c = acl.getClientByIdx(i);
        if (c->permissions == 0) continue;  // skip deleted entries
        memcpy(&reply_data[ofs], c->id.pub_key, 6); ofs += 6;  // just 6-byte pub_key prefix
        reply_data[ofs++] = c->permissions;
      }
      return ofs;
    }
  }
  if (payload[0] == REQ_TYPE_GET_NEIGHBOURS) {
    uint8_t request_version = payload[1];
    if (request_version == 0) {

      // reply data offset (after response sender_timestamp/tag)
      int reply_offset = 4;

      // get request params
      uint8_t count = payload[2]; // how many neighbours to fetch (0-255)
      uint16_t offset;
      memcpy(&offset, &payload[3], 2); // offset from start of neighbours list (0-65535)
      uint8_t order_by = payload[5]; // how to order neighbours. 0=newest_to_oldest, 1=oldest_to_newest, 2=strongest_to_weakest, 3=weakest_to_strongest
      uint8_t pubkey_prefix_length = payload[6]; // how many bytes of neighbour pub key we want
      // we also send a 4 byte random blob in payload[7...10] to help packet uniqueness

      MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS count=%d, offset=%d, order_by=%d, pubkey_prefix_length=%d", count, offset, order_by, pubkey_prefix_length);

      // clamp pub key prefix length to max pub key length
      if(pubkey_prefix_length > PUB_KEY_SIZE){
        pubkey_prefix_length = PUB_KEY_SIZE;
        MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS invalid pubkey_prefix_length=%d clamping to %d", pubkey_prefix_length, PUB_KEY_SIZE);
      }

      // create copy of neighbours list, skipping empty entries so we can sort it separately from main list
      int16_t neighbours_count = 0;
#if MAX_NEIGHBOURS
      NeighbourInfo* sorted_neighbours[MAX_NEIGHBOURS];
      for (int i = 0; i < MAX_NEIGHBOURS; i++) {
        auto neighbour = &neighbours[i];
        if (neighbour->heard_timestamp > 0) {
          sorted_neighbours[neighbours_count] = neighbour;
          neighbours_count++;
        }
      }

      // sort neighbours based on order
      if (order_by == 0) {
        // sort by newest to oldest
        MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS sorting newest to oldest");
        std::sort(sorted_neighbours, sorted_neighbours + neighbours_count, [](const NeighbourInfo* a, const NeighbourInfo* b) {
          return a->heard_timestamp > b->heard_timestamp; // desc
        });
      } else if (order_by == 1) {
        // sort by oldest to newest
        MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS sorting oldest to newest");
        std::sort(sorted_neighbours, sorted_neighbours + neighbours_count, [](const NeighbourInfo* a, const NeighbourInfo* b) {
          return a->heard_timestamp < b->heard_timestamp; // asc
        });
      } else if (order_by == 2) {
        // sort by strongest to weakest
        MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS sorting strongest to weakest");
        std::sort(sorted_neighbours, sorted_neighbours + neighbours_count, [](const NeighbourInfo* a, const NeighbourInfo* b) {
          return a->snr > b->snr; // desc
        });
      } else if (order_by == 3) {
        // sort by weakest to strongest
        MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS sorting weakest to strongest");
        std::sort(sorted_neighbours, sorted_neighbours + neighbours_count, [](const NeighbourInfo* a, const NeighbourInfo* b) {
          return a->snr < b->snr; // asc
        });
      }
#endif

      // build results buffer
      int results_count = 0;
      int results_offset = 0;
      uint8_t results_buffer[130];
      for(int index = 0; index < count && index + offset < neighbours_count; index++){
        
        // stop if we can't fit another entry in results
        int entry_size = pubkey_prefix_length + 4 + 1;
        if(results_offset + entry_size > sizeof(results_buffer)){
          MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS no more entries can fit in results buffer");
          break;
        }

#if MAX_NEIGHBOURS
        // add next neighbour to results
        auto neighbour = sorted_neighbours[index + offset];
        uint32_t heard_seconds_ago = getRTCClock()->getCurrentTime() - neighbour->heard_timestamp;
        memcpy(&results_buffer[results_offset], neighbour->id.pub_key, pubkey_prefix_length); results_offset += pubkey_prefix_length;
        memcpy(&results_buffer[results_offset], &heard_seconds_ago, 4); results_offset += 4;
        memcpy(&results_buffer[results_offset], &neighbour->snr, 1); results_offset += 1;
        results_count++;
#endif

      }

      // build reply
      MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS neighbours_count=%d results_count=%d", neighbours_count, results_count);
      memcpy(&reply_data[reply_offset], &neighbours_count, 2); reply_offset += 2;
      memcpy(&reply_data[reply_offset], &results_count, 2); reply_offset += 2;
      memcpy(&reply_data[reply_offset], &results_buffer, results_offset); reply_offset += results_offset;

      return reply_offset;
    }
  } else if (payload[0] == REQ_TYPE_GET_OWNER_INFO) {
    sprintf((char *) &reply_data[4], "%s\n%s\n%s", FIRMWARE_VERSION, _prefs.node_name, _prefs.owner_info);
    return 4 + strlen((char *) &reply_data[4]);
  }
  return 0; // unknown command
}

mesh::Packet *MyMesh::createSelfAdvert() {
  uint8_t app_data[MAX_ADVERT_DATA_SIZE];
  uint8_t app_data_len = _cli.buildAdvertData(ADV_TYPE_REPEATER, app_data);

  return createAdvert(self_id, app_data, app_data_len);
}

File MyMesh::openAppend(const char *fname) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  return _fs->open(fname, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  return _fs->open(fname, "a");
#else
  return _fs->open(fname, "a", true);
#endif
}

static uint8_t max_loop_minimal[] =  { 0, /* 1-byte */  4, /* 2-byte */  2, /* 3-byte */  1 };
static uint8_t max_loop_moderate[] = { 0, /* 1-byte */  2, /* 2-byte */  1, /* 3-byte */  1 };
static uint8_t max_loop_strict[] =   { 0, /* 1-byte */  1, /* 2-byte */  1, /* 3-byte */  1 };

bool MyMesh::isLooped(const mesh::Packet* packet, const uint8_t max_counters[]) {
  uint8_t hash_size = packet->getPathHashSize();
  uint8_t hash_count = packet->getPathHashCount();
  uint8_t n = 0;
  const uint8_t* path = packet->path;
  while (hash_count > 0) {      // count how many times this node is already in the path
    if (self_id.isHashMatch(path, hash_size)) n++;
    hash_count--;
    path += hash_size;
  }
  return n >= max_counters[hash_size];
}

bool MyMesh::allowPacketForward(const mesh::Packet *packet) {
  if (_prefs.disable_fwd) return false;
  if (packet->isRouteFlood() && packet->getPathHashCount() >= _prefs.flood_max) return false;
  if (packet->isRouteFlood() && recv_pkt_region == NULL) {
    MESH_DEBUG_PRINTLN("allowPacketForward: unknown transport code, or wildcard not allowed for FLOOD packet");
    return false;
  }
  if (packet->isRouteFlood() && _prefs.loop_detect != LOOP_DETECT_OFF) {
    const uint8_t* maximums;
    if (_prefs.loop_detect == LOOP_DETECT_MINIMAL) {
      maximums = max_loop_minimal;
    } else if (_prefs.loop_detect == LOOP_DETECT_MODERATE) {
      maximums = max_loop_moderate;
    } else {
      maximums = max_loop_strict;
    }
    if (isLooped(packet, maximums)) {
      MESH_DEBUG_PRINTLN("allowPacketForward: FLOOD packet loop detected!");
      return false;
    }
  }
  return true;
}

const char *MyMesh::getLogDateTime() {
  static char tmp[32];
  uint32_t now = getRTCClock()->getCurrentTime();
  DateTime dt = DateTime(now);
  sprintf(tmp, "%02d:%02d:%02d - %d/%d/%d U", dt.hour(), dt.minute(), dt.second(), dt.day(), dt.month(),
          dt.year());
  return tmp;
}

void MyMesh::logRxRaw(float snr, float rssi, const uint8_t raw[], int len) {
#if MESH_PACKET_LOGGING
  Serial.print(getLogDateTime());
  Serial.print(" RAW: ");
  mesh::Utils::printHex(Serial, raw, len);
  Serial.println();
#endif
}

void MyMesh::logRx(mesh::Packet *pkt, int len, float score) {
#ifdef WITH_BRIDGE
  if (_prefs.bridge_pkt_src == 1) {
    bridge.sendPacket(pkt);
  }
#endif

  if (_logging) {
    File f = openAppend(PACKET_LOG_FILE);
    if (f) {
      f.print(getLogDateTime());
      f.printf(": RX, len=%d (type=%d, route=%s, payload_len=%d) SNR=%d RSSI=%d score=%d", len,
               pkt->getPayloadType(), pkt->isRouteDirect() ? "D" : "F", pkt->payload_len,
               (int)_radio->getLastSNR(), (int)_radio->getLastRSSI(), (int)(score * 1000));

      if (pkt->getPayloadType() == PAYLOAD_TYPE_PATH || pkt->getPayloadType() == PAYLOAD_TYPE_REQ ||
          pkt->getPayloadType() == PAYLOAD_TYPE_RESPONSE || pkt->getPayloadType() == PAYLOAD_TYPE_TXT_MSG) {
        f.printf(" [%02X -> %02X]\n", (uint32_t)pkt->payload[1], (uint32_t)pkt->payload[0]);
      } else {
        f.printf("\n");
      }
      f.close();
    }
  }
}

void MyMesh::logTx(mesh::Packet *pkt, int len) {
#ifdef WITH_BRIDGE
  if (_prefs.bridge_pkt_src == 0) {
    bridge.sendPacket(pkt);
  }
#endif

  if (_logging) {
    File f = openAppend(PACKET_LOG_FILE);
    if (f) {
      f.print(getLogDateTime());
      f.printf(": TX, len=%d (type=%d, route=%s, payload_len=%d)", len, pkt->getPayloadType(),
               pkt->isRouteDirect() ? "D" : "F", pkt->payload_len);

      if (pkt->getPayloadType() == PAYLOAD_TYPE_PATH || pkt->getPayloadType() == PAYLOAD_TYPE_REQ ||
          pkt->getPayloadType() == PAYLOAD_TYPE_RESPONSE || pkt->getPayloadType() == PAYLOAD_TYPE_TXT_MSG) {
        f.printf(" [%02X -> %02X]\n", (uint32_t)pkt->payload[1], (uint32_t)pkt->payload[0]);
      } else {
        f.printf("\n");
      }
      f.close();
    }
  }
}

void MyMesh::logTxFail(mesh::Packet *pkt, int len) {
  if (_logging) {
    File f = openAppend(PACKET_LOG_FILE);
    if (f) {
      f.print(getLogDateTime());
      f.printf(": TX FAIL!, len=%d (type=%d, route=%s, payload_len=%d)\n", len, pkt->getPayloadType(),
               pkt->isRouteDirect() ? "D" : "F", pkt->payload_len);
      f.close();
    }
  }
}

int MyMesh::calcRxDelay(float score, uint32_t air_time) const {
  if (_prefs.rx_delay_base <= 0.0f) return 0;
  return (int)((pow(_prefs.rx_delay_base, 0.85f - score) - 1.0) * air_time);
}

uint32_t MyMesh::getRetransmitDelay(const mesh::Packet *packet) {
  uint32_t t = (_radio->getEstAirtimeFor(packet->getPathByteLen() + packet->payload_len + 2) * _prefs.tx_delay_factor);
  return getRNG()->nextInt(0, 5*t + 1);
}
uint32_t MyMesh::getDirectRetransmitDelay(const mesh::Packet *packet) {
  uint32_t t = (_radio->getEstAirtimeFor(packet->getPathByteLen() + packet->payload_len + 2) * _prefs.direct_tx_delay_factor);
  return getRNG()->nextInt(0, 5*t + 1);
}

bool MyMesh::filterRecvFloodPacket(mesh::Packet* pkt) {
  // just try to determine region for packet (apply later in allowPacketForward())
  if (pkt->getRouteType() == ROUTE_TYPE_TRANSPORT_FLOOD) {
    recv_pkt_region = region_map.findMatch(pkt, REGION_DENY_FLOOD);
  } else if (pkt->getRouteType() == ROUTE_TYPE_FLOOD) {
    if (region_map.getWildcard().flags & REGION_DENY_FLOOD) {
      recv_pkt_region = NULL;
    } else {
      recv_pkt_region =  &region_map.getWildcard();
    }
  } else {
    recv_pkt_region = NULL;
  }
  // do normal processing
  return false;
}

void MyMesh::onAnonDataRecv(mesh::Packet *packet, const uint8_t *secret, const mesh::Identity &sender,
                            uint8_t *data, size_t len) {
  if (packet->getPayloadType() == PAYLOAD_TYPE_ANON_REQ) { // received an initial request by a possible admin
                                                           // client (unknown at this stage)
    uint32_t timestamp;
    memcpy(&timestamp, data, 4);

    data[len] = 0;  // ensure null terminator
    uint8_t reply_len;

    reply_path_len = -1;
    if (data[4] == 0 || data[4] >= ' ') {   // is password, ie. a login request
      reply_len = handleLoginReq(sender, secret, timestamp, &data[4], packet->isRouteFlood());
    } else if (data[4] == ANON_REQ_TYPE_REGIONS && packet->isRouteDirect()) {
      reply_len = handleAnonRegionsReq(sender, timestamp, &data[5]);
    } else if (data[4] == ANON_REQ_TYPE_OWNER && packet->isRouteDirect()) {
      reply_len = handleAnonOwnerReq(sender, timestamp, &data[5]);
    } else if (data[4] == ANON_REQ_TYPE_BASIC && packet->isRouteDirect()) {
      reply_len = handleAnonClockReq(sender, timestamp, &data[5]);
    } else {
      reply_len = 0;  // unknown/invalid request type
    }

    if (reply_len == 0) return;   // invalid request

    if (packet->isRouteFlood()) {
      // let this sender know path TO here, so they can use sendDirect(), and ALSO encode the response
      mesh::Packet* path = createPathReturn(sender, secret, packet->path, packet->path_len,
                                            PAYLOAD_TYPE_RESPONSE, reply_data, reply_len);
      if (path) sendFlood(path, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
    } else if (reply_path_len < 0) {
      mesh::Packet* reply = createDatagram(PAYLOAD_TYPE_RESPONSE, sender, secret, reply_data, reply_len);
      if (reply) sendFlood(reply, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
    } else {
      mesh::Packet* reply = createDatagram(PAYLOAD_TYPE_RESPONSE, sender, secret, reply_data, reply_len);
      uint8_t path_len = ((reply_path_hash_size - 1) << 6) | (reply_path_len & 63);
      if (reply) sendDirect(reply, reply_path,  path_len, SERVER_RESPONSE_DELAY);
    }
  }
}

int MyMesh::searchPeersByHash(const uint8_t *hash) {
  int n = 0;
  for (int i = 0; i < acl.getNumClients(); i++) {
    if (acl.getClientByIdx(i)->id.isHashMatch(hash)) {
      matching_peer_indexes[n++] = i; // store the INDEXES of matching contacts (for subsequent 'peer' methods)
    }
  }
  return n;
}

void MyMesh::getPeerSharedSecret(uint8_t *dest_secret, int peer_idx) {
  int i = matching_peer_indexes[peer_idx];
  if (i >= 0 && i < acl.getNumClients()) {
    // lookup pre-calculated shared_secret
    memcpy(dest_secret, acl.getClientByIdx(i)->shared_secret, PUB_KEY_SIZE);
  } else {
    MESH_DEBUG_PRINTLN("getPeerSharedSecret: Invalid peer idx: %d", i);
  }
}

static bool isShare(const mesh::Packet *packet) {
  if (packet->hasTransportCodes()) {
    return packet->transport_codes[0] == 0 && packet->transport_codes[1] == 0;  // codes { 0, 0 } means 'send to nowhere'
  }
  return false;
}

void MyMesh::onAdvertRecv(mesh::Packet *packet, const mesh::Identity &id, uint32_t timestamp,
                          const uint8_t *app_data, size_t app_data_len) {
  mesh::Mesh::onAdvertRecv(packet, id, timestamp, app_data, app_data_len); // chain to super impl

  // if this a zero hop advert (and not via 'Share'), add it to neighbours
  if (packet->path_len == 0 && !isShare(packet)) {
    AdvertDataParser parser(app_data, app_data_len);
    if (parser.isValid() && parser.getType() == ADV_TYPE_REPEATER) { // just keep neigbouring Repeaters
      putNeighbour(id, timestamp, packet->getSNR());
    }
  }
}

void MyMesh::onPeerDataRecv(mesh::Packet *packet, uint8_t type, int sender_idx, const uint8_t *secret,
                            uint8_t *data, size_t len) {
  int i = matching_peer_indexes[sender_idx];
  if (i < 0 || i >= acl.getNumClients()) { // get from our known_clients table (sender SHOULD already be known in this context)
    MESH_DEBUG_PRINTLN("onPeerDataRecv: invalid peer idx: %d", i);
    return;
  }
  ClientInfo* client = acl.getClientByIdx(i);

  if (type == PAYLOAD_TYPE_REQ) { // request (from a Known admin client!)
    uint32_t timestamp;
    memcpy(&timestamp, data, 4);

    if (timestamp > client->last_timestamp) { // prevent replay attacks
      int reply_len = handleRequest(client, timestamp, &data[4], len - 4);
      if (reply_len == 0) return; // invalid command

      client->last_timestamp = timestamp;
      client->last_activity = getRTCClock()->getCurrentTime();

      if (packet->isRouteFlood()) {
        // let this sender know path TO here, so they can use sendDirect(), and ALSO encode the response
        mesh::Packet *path = createPathReturn(client->id, secret, packet->path, packet->path_len,
                                              PAYLOAD_TYPE_RESPONSE, reply_data, reply_len);
        if (path) sendFlood(path, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
      } else {
        mesh::Packet *reply =
            createDatagram(PAYLOAD_TYPE_RESPONSE, client->id, secret, reply_data, reply_len);
        if (reply) {
          if (client->out_path_len != OUT_PATH_UNKNOWN) { // we have an out_path, so send DIRECT
            sendDirect(reply, client->out_path, client->out_path_len, SERVER_RESPONSE_DELAY);
          } else {
            sendFlood(reply, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
          }
        }
      }
    } else {
      MESH_DEBUG_PRINTLN("onPeerDataRecv: possible replay attack detected");
    }
  } else if (type == PAYLOAD_TYPE_TXT_MSG && len > 5 && client->isAdmin()) { // a CLI command
    uint32_t sender_timestamp;
    memcpy(&sender_timestamp, data, 4); // timestamp (by sender's RTC clock - which could be wrong)
    uint8_t flags = (data[4] >> 2);        // message attempt number, and other flags

    if (!(flags == TXT_TYPE_PLAIN || flags == TXT_TYPE_CLI_DATA)) {
      MESH_DEBUG_PRINTLN("onPeerDataRecv: unsupported text type received: flags=%02x", (uint32_t)flags);
    } else if (sender_timestamp >= client->last_timestamp) { // prevent replay attacks
      bool is_retry = (sender_timestamp == client->last_timestamp);
      client->last_timestamp = sender_timestamp;
      client->last_activity = getRTCClock()->getCurrentTime();

      // len can be > original length, but 'text' will be padded with zeroes
      data[len] = 0; // need to make a C string again, with null terminator

      if (flags == TXT_TYPE_PLAIN) { // for legacy CLI, send Acks
        uint32_t ack_hash; // calc truncated hash of the message timestamp + text + sender pub_key, to prove
                           // to sender that we got it
        mesh::Utils::sha256((uint8_t *)&ack_hash, 4, data, 5 + strlen((char *)&data[5]), client->id.pub_key,
                            PUB_KEY_SIZE);

        mesh::Packet *ack = createAck(ack_hash);
        if (ack) {
          if (client->out_path_len == OUT_PATH_UNKNOWN) {
            sendFlood(ack, TXT_ACK_DELAY, packet->getPathHashSize());
          } else {
            sendDirect(ack, client->out_path, client->out_path_len, TXT_ACK_DELAY);
          }
        }
      }

      uint8_t temp[166];
      char *command = (char *)&data[5];
      char *reply = (char *)&temp[5];
      if (is_retry) {
        *reply = 0;
      } else {
        handleCommand(sender_timestamp, command, reply);
      }
      int text_len = strlen(reply);
      if (text_len > 0) {
        uint32_t timestamp = getRTCClock()->getCurrentTimeUnique();
        if (timestamp == sender_timestamp) {
          // WORKAROUND: the two timestamps need to be different, in the CLI view
          timestamp++;
        }
        memcpy(temp, &timestamp, 4);        // mostly an extra blob to help make packet_hash unique
        temp[4] = (TXT_TYPE_CLI_DATA << 2);

        auto reply = createDatagram(PAYLOAD_TYPE_TXT_MSG, client->id, secret, temp, 5 + text_len);
        if (reply) {
          if (client->out_path_len == OUT_PATH_UNKNOWN) {
            sendFlood(reply, CLI_REPLY_DELAY_MILLIS, packet->getPathHashSize());
          } else {
            sendDirect(reply, client->out_path, client->out_path_len, CLI_REPLY_DELAY_MILLIS);
          }
        }
      }
    } else {
      MESH_DEBUG_PRINTLN("onPeerDataRecv: possible replay attack detected");
    }
  }
}

bool MyMesh::onPeerPathRecv(mesh::Packet *packet, int sender_idx, const uint8_t *secret, uint8_t *path,
                            uint8_t path_len, uint8_t extra_type, uint8_t *extra, uint8_t extra_len) {
  // TODO: prevent replay attacks
  int i = matching_peer_indexes[sender_idx];

  if (i >= 0 && i < acl.getNumClients()) { // get from our known_clients table (sender SHOULD already be known in this context)
    MESH_DEBUG_PRINTLN("PATH to client, path_len=%d", (uint32_t)path_len);
    auto client = acl.getClientByIdx(i);

    // store a copy of path, for sendDirect()
    client->out_path_len = mesh::Packet::copyPath(client->out_path, path, path_len);
    client->last_activity = getRTCClock()->getCurrentTime();
  } else {
    MESH_DEBUG_PRINTLN("onPeerPathRecv: invalid peer idx: %d", i);
  }

  // NOTE: no reciprocal path send!!
  return false;
}

#define CTL_TYPE_NODE_DISCOVER_REQ   0x80
#define CTL_TYPE_NODE_DISCOVER_RESP  0x90

void MyMesh::onControlDataRecv(mesh::Packet* packet) {
  uint8_t type = packet->payload[0] & 0xF0;    // just test upper 4 bits
  if (type == CTL_TYPE_NODE_DISCOVER_REQ && packet->payload_len >= 6
      && !_prefs.disable_fwd && discover_limiter.allow(rtc_clock.getCurrentTime())
  ) {
    int i = 1;
    uint8_t  filter = packet->payload[i++];
    uint32_t tag;
    memcpy(&tag, &packet->payload[i], 4); i += 4;
    uint32_t since;
    if (packet->payload_len >= i+4) {   // optional since field
      memcpy(&since, &packet->payload[i], 4); i += 4;
    } else {
      since = 0;
    }

    if ((filter & (1 << ADV_TYPE_REPEATER)) != 0 && _prefs.discovery_mod_timestamp >= since) {
      bool prefix_only = packet->payload[0] & 1;
      uint8_t data[6 + PUB_KEY_SIZE];
      data[0] = CTL_TYPE_NODE_DISCOVER_RESP | ADV_TYPE_REPEATER;   // low 4-bits for node type
      data[1] = packet->_snr;   // let sender know the inbound SNR ( x 4)
      memcpy(&data[2], &tag, 4);     // include tag from request, for client to match to
      memcpy(&data[6], self_id.pub_key, PUB_KEY_SIZE);
      auto resp = createControlData(data, prefix_only ? 6 + 8 : 6 + PUB_KEY_SIZE);
      if (resp) {
        sendZeroHop(resp, getRetransmitDelay(resp)*4);  // apply random delay (widened x4), as multiple nodes can respond to this
      }
    }
  } else if (type == CTL_TYPE_NODE_DISCOVER_RESP && packet->payload_len >= 6) {
    uint8_t node_type = packet->payload[0] & 0x0F;
    if (node_type != ADV_TYPE_REPEATER) {
      return;
    }
    if (packet->payload_len < 6 + PUB_KEY_SIZE) {
      MESH_DEBUG_PRINTLN("onControlDataRecv: DISCOVER_RESP pubkey too short: %d", (uint32_t)packet->payload_len);
      return;
    }

    if (pending_discover_tag == 0 || millisHasNowPassed(pending_discover_until)) {
      pending_discover_tag = 0;
      return;
    }
    uint32_t tag;
    memcpy(&tag, &packet->payload[2], 4);
    if (tag != pending_discover_tag) {
      return;
    }

    mesh::Identity id(&packet->payload[6]);
    if (id.matches(self_id)) {
      return;
    }
    putNeighbour(id, rtc_clock.getCurrentTime(), packet->getSNR());
  }
}

void MyMesh::sendNodeDiscoverReq() {
  uint8_t data[10];
  data[0] = CTL_TYPE_NODE_DISCOVER_REQ; // prefix_only=0
  data[1] = (1 << ADV_TYPE_REPEATER);
  getRNG()->random(&data[2], 4); // tag
  memcpy(&pending_discover_tag, &data[2], 4);
  pending_discover_until = futureMillis(60000);
  uint32_t since = 0;
  memcpy(&data[6], &since, 4);

  auto pkt = createControlData(data, sizeof(data));
  if (pkt) {
    sendZeroHop(pkt);
  }
}

MyMesh::MyMesh(mesh::MainBoard &board, mesh::Radio &radio, mesh::MillisecondClock &ms, mesh::RNG &rng,
               mesh::RTCClock &rtc, mesh::MeshTables &tables)
    : mesh::Mesh(radio, ms, rng, rtc, *new StaticPoolPacketManager(32), tables),
      _cli(board, rtc, sensors, acl, &_prefs, this), telemetry(MAX_PACKET_PAYLOAD - 4), region_map(key_store), temp_map(key_store),
      discover_limiter(4, 120),  // max 4 every 2 minutes
      anon_limiter(4, 180)   // max 4 every 3 minutes
#if defined(WITH_RS232_BRIDGE)
      , bridge(&_prefs, WITH_RS232_BRIDGE, _mgr, &rtc)
#endif
#if defined(WITH_ESPNOW_BRIDGE)
      , bridge(&_prefs, _mgr, &rtc)
#endif
{
  last_millis = 0;
  uptime_millis = 0;
  next_local_advert = next_flood_advert = 0;
  dirty_contacts_expiry = 0;
  set_radio_at = revert_radio_at = 0;
  _logging = false;
  region_load_active = false;

#if MAX_NEIGHBOURS
  memset(neighbours, 0, sizeof(neighbours));
#endif

  // defaults
  memset(&_prefs, 0, sizeof(_prefs));
  _prefs.airtime_factor = 1.0;   // one half
  _prefs.rx_delay_base = 0.0f;   // turn off by default, was 10.0;
  _prefs.tx_delay_factor = 0.5f; // was 0.25f
  _prefs.direct_tx_delay_factor = 0.3f; // was 0.2
  StrHelper::strncpy(_prefs.node_name, ADVERT_NAME, sizeof(_prefs.node_name));
  _prefs.node_lat = ADVERT_LAT;
  _prefs.node_lon = ADVERT_LON;
  StrHelper::strncpy(_prefs.password, ADMIN_PASSWORD, sizeof(_prefs.password));
  _prefs.freq = LORA_FREQ;
  _prefs.sf = LORA_SF;
  _prefs.bw = LORA_BW;
  _prefs.cr = LORA_CR;
  _prefs.tx_power_dbm = LORA_TX_POWER;
  _prefs.advert_interval = 1;        // default to 2 minutes for NEW installs
  _prefs.flood_advert_interval = 12; // 12 hours
  _prefs.flood_max = 64;
  _prefs.interference_threshold = 0; // disabled

  // bridge defaults
  _prefs.bridge_enabled = 1;    // enabled
  _prefs.bridge_delay   = 500;  // milliseconds
  _prefs.bridge_pkt_src = 0;    // logTx
  _prefs.bridge_baud = 115200;  // baud rate
  _prefs.bridge_channel = 1;    // channel 1

  StrHelper::strncpy(_prefs.bridge_secret, "LVSITANOS", sizeof(_prefs.bridge_secret));

  // GPS defaults
  _prefs.gps_enabled = 0;
  _prefs.gps_interval = 0;
  _prefs.advert_loc_policy = ADVERT_LOC_PREFS;

  _prefs.adc_multiplier = 0.0f; // 0.0f means use default board multiplier

  pending_discover_tag = 0;
  pending_discover_until = 0;
}

void MyMesh::begin(FILESYSTEM *fs) {
  mesh::Mesh::begin();
  _fs = fs;
  // load persisted prefs
  _cli.loadPrefs(_fs);
  acl.load(_fs, self_id);
  // TODO: key_store.begin();
  region_map.load(_fs);

#if defined(WITH_BRIDGE)
  if (_prefs.bridge_enabled) {
    bridge.begin();
  }
#endif

  radio_set_params(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
  radio_set_tx_power(_prefs.tx_power_dbm);

  updateAdvertTimer();
  updateFloodAdvertTimer();

  board.setAdcMultiplier(_prefs.adc_multiplier);

#if ENV_INCLUDE_GPS == 1
  applyGpsPrefs();
#endif
}

void MyMesh::applyTempRadioParams(float freq, float bw, uint8_t sf, uint8_t cr, int timeout_mins) {
  set_radio_at = futureMillis(2000); // give CLI reply some time to be sent back, before applying temp radio params
  pending_freq = freq;
  pending_bw = bw;
  pending_sf = sf;
  pending_cr = cr;

  revert_radio_at = futureMillis(2000 + timeout_mins * 60 * 1000); // schedule when to revert radio params
}

bool MyMesh::formatFileSystem() {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  return InternalFS.format();
#elif defined(RP2040_PLATFORM)
  return LittleFS.format();
#elif defined(ESP32)
  return SPIFFS.format();
#else
#error "need to implement file system erase"
  return false;
#endif
}

void MyMesh::sendSelfAdvertisement(int delay_millis, bool flood) {
  mesh::Packet *pkt = createSelfAdvert();
  if (pkt) {
    if (flood) {
      sendFlood(pkt, delay_millis, _prefs.path_hash_mode + 1);
    } else {
      sendZeroHop(pkt, delay_millis);
    }
  } else {
    MESH_DEBUG_PRINTLN("ERROR: unable to create advertisement packet!");
  }
}

void MyMesh::updateAdvertTimer() {
  if (_prefs.advert_interval > 0) { // schedule local advert timer
    next_local_advert = futureMillis(((uint32_t)_prefs.advert_interval) * 2 * 60 * 1000);
  } else {
    next_local_advert = 0; // stop the timer
  }
}

void MyMesh::updateFloodAdvertTimer() {
  if (_prefs.flood_advert_interval > 0) { // schedule flood advert timer
    next_flood_advert = futureMillis(((uint32_t)_prefs.flood_advert_interval) * 60 * 60 * 1000);
  } else {
    next_flood_advert = 0; // stop the timer
  }
}

void MyMesh::dumpLogFile() {
#if defined(RP2040_PLATFORM)
  File f = _fs->open(PACKET_LOG_FILE, "r");
#else
  File f = _fs->open(PACKET_LOG_FILE);
#endif
  if (f) {
    while (f.available()) {
      int c = f.read();
      if (c < 0) break;
      Serial.print((char)c);
    }
    f.close();
  }
}

void MyMesh::setTxPower(int8_t power_dbm) {
  radio_set_tx_power(power_dbm);
}

void MyMesh::formatNeighborsReply(char *reply) {
  char *dp = reply;

#if MAX_NEIGHBOURS
  // create copy of neighbours list, skipping empty entries so we can sort it separately from main list
  int16_t neighbours_count = 0;
  NeighbourInfo* sorted_neighbours[MAX_NEIGHBOURS];
  for (int i = 0; i < MAX_NEIGHBOURS; i++) {
    auto neighbour = &neighbours[i];
    if (neighbour->heard_timestamp > 0) {
      sorted_neighbours[neighbours_count] = neighbour;
      neighbours_count++;
    }
  }

  // sort neighbours newest to oldest
  std::sort(sorted_neighbours, sorted_neighbours + neighbours_count, [](const NeighbourInfo* a, const NeighbourInfo* b) {
    return a->heard_timestamp > b->heard_timestamp; // desc
  });

  for (int i = 0; i < neighbours_count && dp - reply < 134; i++) {
    NeighbourInfo *neighbour = sorted_neighbours[i];

    // add new line if not first item
    if (i > 0) *dp++ = '\n';

    char hex[10];
    // get 4 bytes of neighbour id as hex
    mesh::Utils::toHex(hex, neighbour->id.pub_key, 4);

    // add next neighbour
    uint32_t secs_ago = getRTCClock()->getCurrentTime() - neighbour->heard_timestamp;
    sprintf(dp, "%s:%d:%d", hex, secs_ago, neighbour->snr);
    while (*dp)
      dp++; // find end of string
  }
#endif
  if (dp == reply) { // no neighbours, need empty response
    strcpy(dp, "-none-");
    dp += 6;
  }
  *dp = 0; // null terminator
}

void MyMesh::removeNeighbor(const uint8_t *pubkey, int key_len) {
#if MAX_NEIGHBOURS
  for (int i = 0; i < MAX_NEIGHBOURS; i++) {
    NeighbourInfo *neighbour = &neighbours[i];
    if (memcmp(neighbour->id.pub_key, pubkey, key_len) == 0) {
      neighbours[i] = NeighbourInfo(); // clear neighbour entry
    }
  }
#endif
}

void MyMesh::formatStatsReply(char *reply) {
  StatsFormatHelper::formatCoreStats(reply, board, *_ms, _err_flags, _mgr);
}

void MyMesh::formatRadioStatsReply(char *reply) {
  StatsFormatHelper::formatRadioStats(reply, _radio, radio_driver, getTotalAirTime(), getReceiveAirTime());
}

void MyMesh::formatPacketStatsReply(char *reply) {
  StatsFormatHelper::formatPacketStats(reply, radio_driver, getNumSentFlood(), getNumSentDirect(), 
                                       getNumRecvFlood(), getNumRecvDirect());
}

void MyMesh::saveIdentity(const mesh::LocalIdentity &new_id) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  IdentityStore store(*_fs, "");
#elif defined(ESP32)
  IdentityStore store(*_fs, "/identity");
#elif defined(RP2040_PLATFORM)
  IdentityStore store(*_fs, "/identity");
#else
#error "need to define saveIdentity()"
#endif
  store.save("_main", new_id);
}

void MyMesh::clearStats() {
  radio_driver.resetStats();
  resetStats();
  ((SimpleMeshTables *)getTables())->resetStats();
}

void MyMesh::handleCommand(uint32_t sender_timestamp, char *command, char *reply) {
  if (region_load_active) {
    if (StrHelper::isBlank(command)) {  // empty/blank line, signal to terminate 'load' operation
      region_map = temp_map;  // copy over the temp instance as new current map
      region_load_active = false;

      sprintf(reply, "OK - loaded %d regions", region_map.getCount());
    } else {
      char *np = command;
      while (*np == ' ') np++;   // skip indent
      int indent = np - command;

      char *ep = np;
      while (RegionMap::is_name_char(*ep)) ep++;
      if (*ep) { *ep++ = 0; }  // set null terminator for end of name

      while (*ep && *ep != 'F') ep++;  // look for (optional) flags

      if (indent > 0 && indent < 8 && strlen(np) > 0) {
        auto parent = load_stack[indent - 1];
        if (parent) {
          auto old = region_map.findByName(np);
          auto nw = temp_map.putRegion(np, parent->id, old ? old->id : 0);  // carry-over the current ID (if name already exists)
          if (nw) {
            nw->flags = old ? old->flags : (*ep == 'F' ? 0 : REGION_DENY_FLOOD);   // carry-over flags from curr

            load_stack[indent] = nw;  // keep pointers to parent regions, to resolve parent_id's
          }
        }
      }
      reply[0] = 0;
    }
    return;
  }

  while (*command == ' ') command++; // skip leading spaces

  if (strlen(command) > 4 && command[2] == '|') { // optional prefix (for companion radio CLI)
    memcpy(reply, command, 3);                    // reflect the prefix back
    reply += 3;
    command += 3;
  }

  // handle ACL related commands
  if (memcmp(command, "setperm ", 8) == 0) {   // format:  setperm {pubkey-hex} {permissions-int8}
    char* hex = &command[8];
    char* sp = strchr(hex, ' ');   // look for separator char
    if (sp == NULL) {
      strcpy(reply, "Err - bad params");
    } else {
      *sp++ = 0;   // replace space with null terminator

      uint8_t pubkey[PUB_KEY_SIZE];
      int hex_len = min(sp - hex, PUB_KEY_SIZE*2);
      if (mesh::Utils::fromHex(pubkey, hex_len / 2, hex)) {
        uint8_t perms = atoi(sp);
        if (acl.applyPermissions(self_id, pubkey, hex_len / 2, perms)) {
          dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);   // trigger acl.save()
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Err - invalid params");
        }
      } else {
        strcpy(reply, "Err - bad pubkey");
      }
    }
  } else if (sender_timestamp == 0 && strcmp(command, "get acl") == 0) {
    Serial.println("ACL:");
    for (int i = 0; i < acl.getNumClients(); i++) {
      auto c = acl.getClientByIdx(i);
      if (c->permissions == 0) continue;  // skip deleted (or guest) entries

      Serial.printf("%02X ", c->permissions);
      mesh::Utils::printHex(Serial, c->id.pub_key, PUB_KEY_SIZE);
      Serial.printf("\n");
    }
    reply[0] = 0;
  } else if (memcmp(command, "region", 6) == 0) {
    reply[0] = 0;

    const char* parts[4];
    int n = mesh::Utils::parseTextParts(command, parts, 4, ' ');
    if (n == 1) {
      region_map.exportTo(reply, 160);
    } else if (n >= 2 && strcmp(parts[1], "load") == 0) {
      temp_map.resetFrom(region_map);   // rebuild regions in a temp instance
      memset(load_stack, 0, sizeof(load_stack));
      load_stack[0] = &temp_map.getWildcard();
      region_load_active = true;
    } else if (n >= 2 && strcmp(parts[1], "save") == 0) {
      _prefs.discovery_mod_timestamp = rtc_clock.getCurrentTime();   // this node is now 'modified' (for discovery info)
      savePrefs();
      bool success = region_map.save(_fs);
      strcpy(reply, success ? "OK" : "Err - save failed");
    } else if (n >= 3 && strcmp(parts[1], "allowf") == 0) {
      auto region = region_map.findByNamePrefix(parts[2]);
      if (region) {
        region->flags &= ~REGION_DENY_FLOOD;
        strcpy(reply, "OK");
      } else {
        strcpy(reply, "Err - unknown region");
      }
    } else if (n >= 3 && strcmp(parts[1], "denyf") == 0) {
      auto region = region_map.findByNamePrefix(parts[2]);
      if (region) {
        region->flags |= REGION_DENY_FLOOD;
        strcpy(reply, "OK");
      } else {
        strcpy(reply, "Err - unknown region");
      }
    } else if (n >= 3 && strcmp(parts[1], "get") == 0) {
      auto region = region_map.findByNamePrefix(parts[2]);
      if (region) {
        auto parent = region_map.findById(region->parent);
        if (parent && parent->id != 0) {
          sprintf(reply, " %s (%s) %s", region->name, parent->name, (region->flags & REGION_DENY_FLOOD) ? "" : "F");
        } else {
          sprintf(reply, " %s %s", region->name, (region->flags & REGION_DENY_FLOOD) ? "" : "F");
        }
      } else {
        strcpy(reply, "Err - unknown region");
      }
    } else if (n >= 3 && strcmp(parts[1], "home") == 0) {
      auto home = region_map.findByNamePrefix(parts[2]);
      if (home) {
        region_map.setHomeRegion(home);
        sprintf(reply, " home is now %s", home->name);
      } else {
        strcpy(reply, "Err - unknown region");
      }
    } else if (n == 2 && strcmp(parts[1], "home") == 0) {
      auto home = region_map.getHomeRegion();
      sprintf(reply, " home is %s", home ? home->name : "*");
    } else if (n >= 3 && strcmp(parts[1], "put") == 0) {
      auto parent = n >= 4 ? region_map.findByNamePrefix(parts[3]) : &region_map.getWildcard();
      if (parent == NULL) {
        strcpy(reply, "Err - unknown parent");
      } else {
        auto region = region_map.putRegion(parts[2], parent->id);
        if (region == NULL) {
          strcpy(reply, "Err - unable to put");
        } else {
          strcpy(reply, "OK");
        }
      }
    } else if (n >= 3 && strcmp(parts[1], "remove") == 0) {
      auto region = region_map.findByName(parts[2]);
      if (region) {
        if (region_map.removeRegion(*region)) {
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Err - not empty");
        }
      } else {
        strcpy(reply, "Err - not found");
      }
    } else if (n >= 3 && strcmp(parts[1], "list") == 0) {
      uint8_t mask = 0;
      bool invert = false;
      
      if (strcmp(parts[2], "allowed") == 0) {
        mask = REGION_DENY_FLOOD;
        invert = false;  // list regions that DON'T have DENY flag
      } else if (strcmp(parts[2], "denied") == 0) {
        mask = REGION_DENY_FLOOD;
        invert = true;   // list regions that DO have DENY flag
      } else {
        strcpy(reply, "Err - use 'allowed' or 'denied'");
        return;
      }
      
      int len = region_map.exportNamesTo(reply, 160, mask, invert);
      if (len == 0) {
        strcpy(reply, "-none-");
      }
    } else {
      strcpy(reply, "Err - ??");
    }
  } else if (memcmp(command, "discover.neighbors", 18) == 0) {
    const char* sub = command + 18;
    while (*sub == ' ') sub++;
    if (*sub != 0) {
      strcpy(reply, "Err - discover.neighbors has no options");
    } else {
      sendNodeDiscoverReq();
      strcpy(reply, "OK - Discover sent");
    }
#if defined(REPEATER_TCP_COMPANION) && defined(ESP32)
  } else if (strncmp(command, "set wifi.ssid ", 14) == 0) {
    if (wifiConfigSetSsid(&command[14])) {
      strcpy(reply, "OK - set wifi.pwd then wifi.apply");
    } else {
      strcpy(reply, "Err - invalid SSID");
    }
  } else if (strncmp(command, "set wifi.pwd ", 13) == 0) {
    if (wifiConfigSetPwd(&command[13])) {
      strcpy(reply, "OK - run wifi.apply");
    } else {
      strcpy(reply, "Err - password too long");
    }
  } else if (strcmp(command, "wifi.apply") == 0 || strcmp(command, "set wifi.apply") == 0) {
    if (!wifiConfigGetRadioEnabled()) {
      strcpy(reply, "Err - wifi radio off; set wifi.radio 1 first");
    } else if (!wifiConfigHasRuntime()) {
      strcpy(reply, "Err - no runtime credentials");
    } else {
      wifiConfigApply();
      strcpy(reply, "OK - reconnecting");
    }
  } else if (strcmp(command, "wifi.clear") == 0 || strcmp(command, "set wifi.clear") == 0) {
    wifiConfigClear();
    strcpy(reply, "OK - cleared");
  } else if (strcmp(command, "get wifi.ssid") == 0) {
    char ssid[WIFI_CONFIG_SSID_MAX];
    wifiConfigGetSsid(ssid, sizeof(ssid));
    snprintf(reply, 160, "%s", ssid[0] ? ssid : "(none)");
  } else if (strncmp(command, "set wifi.radio ", 17) == 0) {
    int v = atoi(&command[17]);
    wifiConfigSetRadioEnabled(v != 0);
#if defined(REPEATER_TCP_COMPANION)
    if (v != 0) repeater_on_wifi_radio_toggled();
#endif
    strcpy(reply, v ? "OK - wifi radio on" : "OK - wifi radio off");
  } else if (strcmp(command, "get wifi.status") == 0 || strcmp(command, "wifi.status") == 0) {
    char ssid[WIFI_CONFIG_SSID_MAX];
    wifiConfigGetSsid(ssid, sizeof(ssid));
    int re = wifiConfigGetRadioEnabled() ? 1 : 0;
    if (WiFi.status() == WL_CONNECTED) {
      IPAddress ip = WiFi.localIP();
      snprintf(reply, 160, "runtime=%d radio_enabled=%d ssid=%s ip=%d.%d.%d.%d", wifiConfigHasRuntime() ? 1 : 0,
               re, ssid[0] ? ssid : "(none)", ip[0], ip[1], ip[2], ip[3]);
    } else {
      snprintf(reply, 160, "runtime=%d radio_enabled=%d ssid=%s disconnected", wifiConfigHasRuntime() ? 1 : 0,
               re, ssid[0] ? ssid : "(none)");
    }
#endif
  } else {
    _cli.handleCommand(sender_timestamp, command, reply);  // common CLI commands
  }
}

void MyMesh::loop() {
#ifdef WITH_BRIDGE
  bridge.loop();
#endif

  mesh::Mesh::loop();

  if (next_flood_advert && millisHasNowPassed(next_flood_advert)) {
    mesh::Packet *pkt = createSelfAdvert();
    if (pkt) sendFlood(pkt);

    updateFloodAdvertTimer(); // schedule next flood advert
    updateAdvertTimer();      // also schedule local advert (so they don't overlap)
  } else if (next_local_advert && millisHasNowPassed(next_local_advert)) {
    mesh::Packet *pkt = createSelfAdvert();
    if (pkt) sendZeroHop(pkt);

    updateAdvertTimer(); // schedule next local advert
  }

  if (set_radio_at && millisHasNowPassed(set_radio_at)) { // apply pending (temporary) radio params
    set_radio_at = 0;                                     // clear timer
    radio_set_params(pending_freq, pending_bw, pending_sf, pending_cr);
    MESH_DEBUG_PRINTLN("Temp radio params");
  }

  if (revert_radio_at && millisHasNowPassed(revert_radio_at)) { // revert radio params to orig
    revert_radio_at = 0;                                        // clear timer
    radio_set_params(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
    MESH_DEBUG_PRINTLN("Radio params restored");
  }

  // is pending dirty contacts write needed?
  if (dirty_contacts_expiry && millisHasNowPassed(dirty_contacts_expiry)) {
    acl.save(_fs);
    dirty_contacts_expiry = 0;
  }

  // update uptime
  uint32_t now = millis();
  uptime_millis += now - last_millis;
  last_millis = now;
}

// To check if there is pending work
bool MyMesh::hasPendingWork() const {
#if defined(WITH_BRIDGE)
  if (bridge.isRunning()) return true;  // bridge needs WiFi radio, can't sleep
#endif
  return _mgr->getOutboundCount(0xFFFFFFFF) > 0;
}

#if defined(REPEATER_TCP_COMPANION) && defined(ESP32)

#include <repeater_companion_proto.h>
#include <helpers/BaseSerialInterface.h>
#include <helpers/TxtDataHelpers.h>

#ifndef MAX_CLIENT_ID_LEN
  #define MAX_CLIENT_ID_LEN 31
#endif

namespace {

struct FreqRange {
  uint32_t lower_freq, upper_freq;
};

bool isValidClientRepeatFreq(uint32_t f) {
  static const FreqRange repeat_freq_ranges[] = {
      {433000, 433000},
      {869000, 869000},
      {918000, 918000},
  };
  for (size_t k = 0; k < sizeof(repeat_freq_ranges) / sizeof(repeat_freq_ranges[0]); k++) {
    const FreqRange *r = &repeat_freq_ranges[k];
    if (f >= r->lower_freq && f <= r->upper_freq) return true;
  }
  return false;
}

static const uint8_t kMeshcomodPubPrefix[6] = {0x4D, 0x45, 0x53, 0x48, 0x43, 0x4D};

static bool isMeshcomodRecipientTcp(const uint8_t *p6) {
  return p6 && memcmp(p6, kMeshcomodPubPrefix, 6) == 0;
}

#define MESHCOMOD_WIFI_SCAN_MAX_TCP 12
static char s_meshcomod_scan_ssids_tcp[MESHCOMOD_WIFI_SCAN_MAX_TCP][WIFI_CONFIG_SSID_MAX];
static int s_meshcomod_scan_count_tcp = 0;
static uint32_t s_meshcomod_last_reply_ts_tcp = 0;

static char *trimWsInPlaceTcp(char *s) {
  if (!s) return s;
  while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
  int n = (int)strlen(s);
  while (n > 0) {
    char c = s[n - 1];
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      s[n - 1] = '\0';
      n--;
    } else {
      break;
    }
  }
  return s;
}

static char *unquoteInPlaceTcp(char *s) {
  s = trimWsInPlaceTcp(s);
  if (!s) return s;
  int n = (int)strlen(s);
  if (n >= 2) {
    char q = s[0];
    if ((q == '"' || q == '\'') && s[n - 1] == q) {
      s[n - 1] = '\0';
      s++;
    }
  }
  return s;
}

static const char kRepeaterMeshcomodHelp[] =
    "help\n"
    "status\n"
    "wifi scan\n"
    "wifi use <n>\n"
    "wifi set ssid \"...\"\n"
    "wifi set pwd \"...\"\n"
    "wifi status\n"
    "wifi on\n"
    "wifi off\n"
    "wifi apply\n"
    "wifi clear\n";

static void repeaterEmitRaw(void (*emit)(void *, const uint8_t *, size_t), void *ctx, const uint8_t *p,
                            size_t n) {
  if (emit && n > 0) emit(ctx, p, n);
}

static bool repeaterCliReplyIsPlainInteger(const char *s, size_t len) {
  if (!s || len == 0) return false;
  size_t i = 0;
  if (s[0] == '-') {
    i = 1;
    if (i >= len) return false;
  }
  for (; i < len; i++) {
    if (s[i] < '0' || s[i] > '9') return false;
  }
  return true;
}

static void repeaterEmitBinaryCliResponse(void (*emit)(void *, const uint8_t *, size_t), void *ctx,
                                          uint32_t tag, const char *reply) {
  if (!emit) return;
  if (!reply) reply = "";
  size_t trimmed_end = strlen(reply);
  while (trimmed_end > 0 && (reply[trimmed_end - 1] == '\r' || reply[trimmed_end - 1] == '\n')) trimmed_end--;
  const char *body = reply;
  size_t body_len = trimmed_end;
  if (trimmed_end >= 2 && reply[0] == '>' && reply[1] == ' ') {
    const char *tail = reply + 2;
    size_t tail_len = trimmed_end - 2;
    if (repeaterCliReplyIsPlainInteger(tail, tail_len)) {
      body = tail;
      body_len = tail_len;
    }
  }
  if (body_len > MAX_FRAME_SIZE - 6) body_len = MAX_FRAME_SIZE - 6;
  uint8_t out[MAX_FRAME_SIZE];
  int j = 0;
  out[j++] = PUSH_CODE_BINARY_RESPONSE;
  out[j++] = 0;
  memcpy(&out[j], &tag, 4);
  j += 4;
  memcpy(&out[j], body, body_len);
  j += (int)body_len;
  repeaterEmitRaw(emit, ctx, out, (size_t)j);
  uint8_t tickle[1] = {PUSH_CODE_MSG_WAITING};
  repeaterEmitRaw(emit, ctx, tickle, 1);
}

static void repeaterEmitMeshcomodText(mesh::RTCClock *rtc, void (*emit)(void *, const uint8_t *, size_t),
                                      void *ctx, const char *text) {
  if (!text || !emit) return;
  int total_len = (int)strlen(text);
  if (total_len <= 0) return;

  const int header_len = 16;
  const int max_text_per_frame = MAX_FRAME_SIZE - header_len;
  if (max_text_per_frame <= 0) return;

  uint8_t out_frame[MAX_FRAME_SIZE];
  int pos = 0;
  while (pos < total_len) {
    int remaining = total_len - pos;
    int take = remaining < max_text_per_frame ? remaining : max_text_per_frame;
    if (remaining > max_text_per_frame) {
      int split = -1;
      for (int k = take - 1; k >= 0; k--) {
        if (text[pos + k] == '\n') {
          split = k + 1;
          break;
        }
      }
      if (split > 0) take = split;
    }

    int j = 0;
    out_frame[j++] = RESP_CODE_CONTACT_MSG_RECV_V3;
    out_frame[j++] = 0;
    out_frame[j++] = 0;
    out_frame[j++] = 0;
    memcpy(&out_frame[j], kMeshcomodPubPrefix, 6);
    j += 6;
    out_frame[j++] = 0xFF;
    out_frame[j++] = TXT_TYPE_PLAIN;
    uint32_t ts = rtc->getCurrentTimeUnique();
    if (ts <= s_meshcomod_last_reply_ts_tcp) ts = s_meshcomod_last_reply_ts_tcp + 1;
    s_meshcomod_last_reply_ts_tcp = ts;
    memcpy(&out_frame[j], &ts, 4);
    j += 4;
    memcpy(&out_frame[j], &text[pos], (size_t)take);
    j += take;
    repeaterEmitRaw(emit, ctx, out_frame, (size_t)j);
    uint8_t tickle[1] = {PUSH_CODE_MSG_WAITING};
    repeaterEmitRaw(emit, ctx, tickle, 1);
    pos += take;
  }
}

static bool handleRepeaterMeshcomodLine(MyMesh *mesh, const char *text, int text_len,
                                        void (*emit)(void *, const uint8_t *, size_t), void *ctx) {
  if (!mesh || !emit) return false;
  char buf[160];
  if (text_len < 0) text_len = 0;
  if ((size_t)text_len >= sizeof(buf)) text_len = (int)sizeof(buf) - 1;
  memcpy(buf, text, (size_t)text_len);
  buf[text_len] = '\0';

  const char *p = buf;
  while (*p == ' ' || *p == '\t') p++;

  mesh::RTCClock *rtc = mesh->getRTCClock();

  if (strncasecmp(p, "help", 4) == 0 && (p[4] == '\0' || p[4] == ' ' || p[4] == '\t')) {
    repeaterEmitMeshcomodText(rtc, emit, ctx, kRepeaterMeshcomodHelp);
    return true;
  }

  if (strncasecmp(p, "status", 6) == 0 && (p[6] == '\0' || p[6] == ' ' || p[6] == '\t')) {
    char line[200];
    if (WiFi.status() == WL_CONNECTED) {
      IPAddress ip = WiFi.localIP();
      snprintf(line, sizeof(line), "repeater status:\ntcp: on\nwifi: connected\nip: %d.%d.%d.%d", ip[0],
               ip[1], ip[2], ip[3]);
    } else {
      snprintf(line, sizeof(line), "repeater status:\ntcp: on\nwifi: disconnected");
    }
    repeaterEmitMeshcomodText(rtc, emit, ctx, line);
    return true;
  }

  if (strncasecmp(p, "wifi", 4) != 0 || (p[4] != '\0' && p[4] != ' ' && p[4] != '\t')) {
    repeaterEmitMeshcomodText(rtc, emit, ctx, kRepeaterMeshcomodHelp);
    return true;
  }
  p += 4;
  while (*p == ' ' || *p == '\t') p++;

  if (strncasecmp(p, "on", 2) == 0 && (p[2] == '\0' || p[2] == ' ' || p[2] == '\t')) {
    wifiConfigSetRadioEnabled(true);
    repeater_on_wifi_radio_toggled();
    repeaterEmitMeshcomodText(rtc, emit, ctx, "OK wifi radio on");
    return true;
  }
  if (strncasecmp(p, "off", 3) == 0 && (p[3] == '\0' || p[3] == ' ' || p[3] == '\t')) {
    wifiConfigSetRadioEnabled(false);
    repeaterEmitMeshcomodText(rtc, emit, ctx, "OK wifi radio off (TCP/WS drop until on or USB)");
    return true;
  }

  if (strncasecmp(p, "set", 3) == 0 && (p[3] == '\0' || p[3] == ' ' || p[3] == '\t')) {
    p += 3;
    while (*p == ' ' || *p == '\t') p++;
    if (strncasecmp(p, "ssid", 4) == 0 && (p[4] == '\0' || p[4] == ' ' || p[4] == '\t')) {
      p += 4;
      while (*p == ' ' || *p == '\t') p++;
      char *ssid = unquoteInPlaceTcp((char *)p);
      if (wifiConfigSetSsid(ssid)) {
        char ok[168];
        snprintf(ok, sizeof(ok),
                 "OK ssid=\"%s\"\nnext: wifi set pwd \"<password>\" (or \"\" for open)\nthen: wifi apply",
                 ssid);
        repeaterEmitMeshcomodText(rtc, emit, ctx, ok);
      } else {
        repeaterEmitMeshcomodText(rtc, emit, ctx, "error: ssid too long or invalid");
      }
      return true;
    }
    if (strncasecmp(p, "pwd", 3) == 0 && (p[3] == '\0' || p[3] == ' ' || p[3] == '\t')) {
      p += 3;
      while (*p == ' ' || *p == '\t') p++;
      char *pwd = unquoteInPlaceTcp((char *)p);
      if (wifiConfigSetPwd(pwd)) {
        repeaterEmitMeshcomodText(rtc, emit, ctx, "OK\npwd set\nnext: wifi apply");
      } else {
        repeaterEmitMeshcomodText(rtc, emit, ctx, "error: password too long");
      }
      return true;
    }
    repeaterEmitMeshcomodText(rtc, emit, ctx, "error: usage wifi set ssid|pwd \"<value>\"");
    return true;
  }

  if (strncasecmp(p, "scan", 4) == 0 && (p[4] == '\0' || p[4] == ' ' || p[4] == '\t')) {
    if (!wifiConfigGetRadioEnabled()) {
      repeaterEmitMeshcomodText(rtc, emit, ctx, "error: wifi radio off — send wifi on first");
      return true;
    }
    wifi_mode_t mode = WiFi.getMode();
    if ((mode & WIFI_MODE_STA) == 0) {
      WiFi.mode(WIFI_STA);
      delay(180);
    } else {
      delay(40);
    }
    WiFi.scanDelete();
    s_meshcomod_scan_count_tcp = 0;
    int found = WiFi.scanNetworks(false, true, false, 300, 0);
    if (found <= 0 && WiFi.status() != WL_CONNECTED) {
      WiFi.disconnect(false, false);
      delay(120);
      found = WiFi.scanNetworks(false, true, false, 300, 0);
    }
    if (found <= 0) {
      repeaterEmitMeshcomodText(rtc, emit, ctx, "scan: no networks (2.4GHz only)");
      WiFi.scanDelete();
      return true;
    }
    String scanMsg = "scan results:";
    for (int idx = 0; idx < found && s_meshcomod_scan_count_tcp < MESHCOMOD_WIFI_SCAN_MAX_TCP; idx++) {
      String ssid = WiFi.SSID(idx);
      if (ssid.length() <= 0) continue;
      bool dup = false;
      for (int k = 0; k < s_meshcomod_scan_count_tcp; k++) {
        if (strcmp(s_meshcomod_scan_ssids_tcp[k], ssid.c_str()) == 0) {
          dup = true;
          break;
        }
      }
      if (dup) continue;
      StrHelper::strzcpy(s_meshcomod_scan_ssids_tcp[s_meshcomod_scan_count_tcp], ssid.c_str(),
                         WIFI_CONFIG_SSID_MAX);
      int ch = WiFi.channel(idx);
      const char *band = (ch >= 1 && ch <= 14) ? "2.4GHz" : "5GHz";
      char line[96];
      snprintf(line, sizeof(line), "\n%d) %s [%s]", s_meshcomod_scan_count_tcp + 1,
               s_meshcomod_scan_ssids_tcp[s_meshcomod_scan_count_tcp], band);
      scanMsg += line;
      s_meshcomod_scan_count_tcp++;
    }
    if (s_meshcomod_scan_count_tcp == 0) {
      String curr = WiFi.SSID();
      if (curr.length() > 0) {
        StrHelper::strzcpy(s_meshcomod_scan_ssids_tcp[0], curr.c_str(), WIFI_CONFIG_SSID_MAX);
        s_meshcomod_scan_count_tcp = 1;
        int ch = WiFi.channel();
        const char *band = (ch >= 1 && ch <= 14) ? "2.4GHz" : "5GHz";
        char line[96];
        snprintf(line, sizeof(line), "\n1) %s [%s] (connected)", s_meshcomod_scan_ssids_tcp[0], band);
        scanMsg += line;
      }
    }
    if (s_meshcomod_scan_count_tcp == 0) {
      repeaterEmitMeshcomodText(rtc, emit, ctx, "scan: no usable SSIDs");
      repeaterEmitMeshcomodText(rtc, emit, ctx, "tip: try wifi status or move closer to AP");
    } else {
      scanMsg += "\nselect SSID: wifi use <n>";
      scanMsg += "\nthen set password: wifi set pwd \"<password>\" and wifi apply";
      repeaterEmitMeshcomodText(rtc, emit, ctx, scanMsg.c_str());
    }
    WiFi.scanDelete();
    return true;
  }

  if (strncasecmp(p, "use", 3) == 0 && (p[3] == '\0' || p[3] == ' ' || p[3] == '\t')) {
    p += 3;
    while (*p == ' ' || *p == '\t') p++;
    int n = atoi(p);
    if (s_meshcomod_scan_count_tcp <= 0) {
      repeaterEmitMeshcomodText(rtc, emit, ctx, "error: no scan results; run wifi scan");
      return true;
    }
    if (n < 1 || n > s_meshcomod_scan_count_tcp) {
      repeaterEmitMeshcomodText(rtc, emit, ctx, "error: invalid index");
      return true;
    }
    if (!wifiConfigSetSsid(s_meshcomod_scan_ssids_tcp[n - 1])) {
      repeaterEmitMeshcomodText(rtc, emit, ctx, "error: ssid too long or invalid");
      return true;
    }
    char line[192];
    snprintf(line, sizeof(line), "OK ssid=\"%s\"\nnext: wifi set pwd \"<password>\" (or \"\" for open)\nthen: wifi apply",
             s_meshcomod_scan_ssids_tcp[n - 1]);
    repeaterEmitMeshcomodText(rtc, emit, ctx, line);
    return true;
  }

  if (strncasecmp(p, "status", 6) == 0 && (p[6] == '\0' || p[6] == ' ' || p[6] == '\t')) {
    char ssid[WIFI_CONFIG_SSID_MAX];
    wifiConfigGetSsid(ssid, sizeof(ssid));
    bool has_runtime = wifiConfigHasRuntime();
    int re = wifiConfigGetRadioEnabled() ? 1 : 0;
    char reply[140];
    if (!has_runtime || ssid[0] == '\0') {
      snprintf(reply, sizeof(reply), "radio_enabled=%d ssid=(none) runtime=0", re);
    } else {
      int connected = (WiFi.status() == WL_CONNECTED) ? 1 : 0;
      if (connected) {
        IPAddress ip = WiFi.localIP();
        snprintf(reply, sizeof(reply), "radio_enabled=%d ssid=%s connected=1 ip=%d.%d.%d.%d", re, ssid,
                 ip[0], ip[1], ip[2], ip[3]);
      } else {
        snprintf(reply, sizeof(reply), "radio_enabled=%d ssid=%s connected=0", re, ssid);
      }
    }
    repeaterEmitMeshcomodText(rtc, emit, ctx, reply);
    return true;
  }

  if (strncasecmp(p, "clear", 5) == 0 && (p[5] == '\0' || p[5] == ' ' || p[5] == '\t')) {
    wifiConfigClear();
    repeaterEmitMeshcomodText(rtc, emit, ctx, "OK");
    return true;
  }

  if (strncasecmp(p, "apply", 5) == 0 && (p[5] == '\0' || p[5] == ' ' || p[5] == '\t')) {
    if (!wifiConfigGetRadioEnabled()) {
      repeaterEmitMeshcomodText(rtc, emit, ctx, "error: wifi radio off — send wifi on first");
      return true;
    }
    if (!wifiConfigHasRuntime()) {
      repeaterEmitMeshcomodText(rtc, emit, ctx, "No runtime credentials; set ssid/pwd first");
      return true;
    }
    wifiConfigApply();
    repeaterEmitMeshcomodText(rtc, emit, ctx, "OK reconnecting");
    return true;
  }

  repeaterEmitMeshcomodText(rtc, emit, ctx, kRepeaterMeshcomodHelp);
  return true;
}

}  // namespace

void MyMesh::enterCLIRescue() {
  Serial.println();
  Serial.println("========= USB serial CLI =========");
  Serial.println("WiFi (saved to NVS): set wifi.ssid, set wifi.pwd, set wifi.apply");
}

size_t MyMesh::handleRepeaterTcpCompanionCommand(const uint8_t *cmd, size_t cmd_len, uint8_t *out,
                                                 size_t out_cap,
                                                 void (*emit_extra)(void *, const uint8_t *, size_t),
                                                 void *emit_ctx) {
  if (!cmd || out_cap < 8) return 0;

  const uint8_t *c = cmd;
  size_t len = cmd_len;

  auto reply_err = [&](uint8_t err_code) -> size_t {
    if (out_cap < 2) return 0;
    out[0] = RESP_CODE_ERR;
    out[1] = err_code;
    return 2;
  };

  auto reply_ok = [&]() -> size_t {
    if (out_cap < 1) return 0;
    out[0] = RESP_CODE_OK;
    return 1;
  };

  auto reply_disabled = [&]() -> size_t {
    if (out_cap < 1) return 0;
    out[0] = RESP_CODE_DISABLED;
    return 1;
  };

  if (len < 1) return reply_err(ERR_CODE_UNSUPPORTED_CMD);

  if (c[0] == CMD_DEVICE_QUERY && len >= 2) {
    size_t i = 0;
    if (out_cap < 82) return 0;
    (void)c[1];  // app protocol version (companion stores per client)
    out[i++] = RESP_CODE_DEVICE_INFO;
    out[i++] = REPEATER_COMPANION_FIRMWARE_VER_CODE;
    out[i++] = 0;  // MAX_CONTACTS / 2 — repeater has no contact store
    out[i++] = 0;  // MAX_GROUP_CHANNELS
    memset(&out[i], 0, 4);  // ble_pin (N/A)
    i += 4;
    memset(&out[i], 0, 12);
    StrHelper::strncpy((char *)&out[i], FIRMWARE_BUILD_DATE, 12);
    i += 12;
    StrHelper::strzcpy((char *)&out[i], board.getManufacturerName(), 40);
    i += 40;
    StrHelper::strzcpy((char *)&out[i], FIRMWARE_VERSION, 20);
    i += 20;
    out[i++] = 0;  // client_repeat (not persisted on repeater prefs)
    out[i++] = _prefs.path_hash_mode;
    return i;
  }

  if (c[0] == CMD_APP_START && len >= 8) {
    (void)len;
    size_t i = 0;
    if (out_cap < 5 + PUB_KEY_SIZE + 4 + 4 + 1 + 1 + 1 + 4 + 4 + 1 + 1 + sizeof(_prefs.node_name))
      return 0;

    out[i++] = RESP_CODE_SELF_INFO;
    out[i++] = ADV_TYPE_REPEATER;
    out[i++] = (uint8_t)_prefs.tx_power_dbm;
    out[i++] = MAX_LORA_TX_POWER;
    memcpy(&out[i], self_id.pub_key, PUB_KEY_SIZE);
    i += PUB_KEY_SIZE;

    int32_t lat = (int32_t)(sensors.node_lat * 1000000.0);
    int32_t lon = (int32_t)(sensors.node_lon * 1000000.0);
    memcpy(&out[i], &lat, 4);
    i += 4;
    memcpy(&out[i], &lon, 4);
    i += 4;
    out[i++] = _prefs.multi_acks;
    out[i++] = _prefs.advert_loc_policy;
    out[i++] = 0;  // telemetry modes (not used on repeater wire subset)
    out[i++] = 0;  // manual_add_contacts

    uint32_t freq = (uint32_t)(_prefs.freq * 1000.0f);
    memcpy(&out[i], &freq, 4);
    i += 4;
    uint32_t bw = (uint32_t)(_prefs.bw * 1000.0f);
    memcpy(&out[i], &bw, 4);
    i += 4;
    out[i++] = _prefs.sf;
    out[i++] = _prefs.cr;

    size_t nlen = strlen(_prefs.node_name);
    if (i + nlen > out_cap) return 0;
    memcpy(&out[i], _prefs.node_name, nlen);
    i += nlen;
    return i;
  }

  if (c[0] == CMD_GET_CONTACTS) {
    if (out_cap < 5) return 0;
    out[0] = RESP_CODE_CONTACTS_START;
    uint32_t count = 0;
    memcpy(&out[1], &count, 4);
    return 5;
  }

  if (c[0] == CMD_SYNC_NEXT_MESSAGE) {
    if (out_cap < 1) return 0;
    out[0] = RESP_CODE_NO_MORE_MESSAGES;
    return 1;
  }

  if (c[0] == CMD_SYNC_SINCE && len >= 5) {
    if (out_cap < 1) return 0;
    out[0] = RESP_CODE_SYNC_SINCE_DONE;
    return 1;
  }

  if (c[0] == CMD_GET_DEVICE_TIME) {
    if (out_cap < 5) return 0;
    out[0] = RESP_CODE_CURR_TIME;
    uint32_t now = getRTCClock()->getCurrentTime();
    memcpy(&out[1], &now, 4);
    return 5;
  }

  if (c[0] == CMD_SET_DEVICE_TIME && len >= 5) {
    uint32_t secs;
    memcpy(&secs, &c[1], 4);
    uint32_t curr = getRTCClock()->getCurrentTime();
    if (secs >= curr) {
      getRTCClock()->setCurrentTime(secs);
      return reply_ok();
    }
    return reply_err(ERR_CODE_ILLEGAL_ARG);
  }

  if (c[0] == CMD_SET_ADVERT_NAME && len >= 2) {
    int nlen = (int)len - 1;
    if (nlen > (int)sizeof(_prefs.node_name) - 1) nlen = sizeof(_prefs.node_name) - 1;
    memcpy(_prefs.node_name, &c[1], (size_t)nlen);
    _prefs.node_name[nlen] = 0;
    savePrefs();
    return reply_ok();
  }

  if (c[0] == CMD_SET_ADVERT_LATLON && len >= 9) {
    int32_t lat, lon, alt = 0;
    memcpy(&lat, &c[1], 4);
    memcpy(&lon, &c[5], 4);
    if (len >= 13) memcpy(&alt, &c[9], 4);
    (void)alt;
    if (lat <= 90 * 1000000 && lat >= -90 * 1000000 && lon <= 180 * 1000000 && lon >= -180 * 1000000) {
      sensors.node_lat = ((double)lat) / 1000000.0;
      sensors.node_lon = ((double)lon) / 1000000.0;
      _prefs.node_lat = sensors.node_lat;
      _prefs.node_lon = sensors.node_lon;
      savePrefs();
      return reply_ok();
    }
    return reply_err(ERR_CODE_ILLEGAL_ARG);
  }

  if (c[0] == CMD_SEND_SELF_ADVERT) {
    mesh::Packet *pkt = createSelfAdvert();
    if (!pkt) return reply_err(ERR_CODE_TABLE_FULL);
    if (len >= 2 && c[1] == 1) {
      sendFlood(pkt, 0, (uint8_t)(_prefs.path_hash_mode + 1));
    } else {
      sendZeroHop(pkt);
    }
    return reply_ok();
  }

  if (c[0] == CMD_SET_RADIO_PARAMS) {
    if (len < 1 + 4 + 4 + 1 + 1) return reply_err(ERR_CODE_ILLEGAL_ARG);
    size_t j = 1;
    uint32_t freq_khz, bw_khz;
    memcpy(&freq_khz, &c[j], 4);
    j += 4;
    memcpy(&bw_khz, &c[j], 4);
    j += 4;
    uint8_t sf = c[j++];
    uint8_t cr = c[j++];
    uint8_t repeat = 0;
    if (len > j) repeat = c[j++];

    if (repeat && !isValidClientRepeatFreq(freq_khz)) return reply_err(ERR_CODE_ILLEGAL_ARG);
    if (freq_khz >= 300000 && freq_khz <= 2500000 && sf >= 5 && sf <= 12 && cr >= 5 && cr <= 8 &&
        bw_khz >= 7000 && bw_khz <= 500000) {
      _prefs.sf = sf;
      _prefs.cr = cr;
      _prefs.freq = (float)freq_khz / 1000.0f;
      _prefs.bw = (float)bw_khz / 1000.0f;
      savePrefs();
      radio_set_params(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
      return reply_ok();
    }
    return reply_err(ERR_CODE_ILLEGAL_ARG);
  }

  if (c[0] == CMD_SET_RADIO_TX_POWER) {
    if (len < 2) return reply_err(ERR_CODE_ILLEGAL_ARG);
    int8_t power = (int8_t)c[1];
    if (power < -9 || power > MAX_LORA_TX_POWER) return reply_err(ERR_CODE_ILLEGAL_ARG);
    _prefs.tx_power_dbm = power;
    savePrefs();
    radio_set_tx_power(_prefs.tx_power_dbm);
    return reply_ok();
  }

  if (c[0] == CMD_SET_TUNING_PARAMS && len >= 1 + 4 + 4) {
    uint32_t rx, af;
    memcpy(&rx, &c[1], 4);
    memcpy(&af, &c[5], 4);
    _prefs.rx_delay_base = ((float)rx) / 1000.0f;
    _prefs.airtime_factor = ((float)af) / 1000.0f;
    savePrefs();
    return reply_ok();
  }

  if (c[0] == CMD_GET_TUNING_PARAMS) {
    if (out_cap < 1 + 4 + 4) return 0;
    uint32_t rx = (uint32_t)(_prefs.rx_delay_base * 1000.0f);
    uint32_t af = (uint32_t)(_prefs.airtime_factor * 1000.0f);
    size_t i = 0;
    out[i++] = RESP_CODE_TUNING_PARAMS;
    memcpy(&out[i], &rx, 4);
    i += 4;
    memcpy(&out[i], &af, 4);
    i += 4;
    return i;
  }

  if (c[0] == CMD_SET_OTHER_PARAMS) {
    if (len >= 4) _prefs.advert_loc_policy = c[3];
    if (len >= 5) _prefs.multi_acks = c[4];
    savePrefs();
    return reply_ok();
  }

  if (c[0] == CMD_SET_PATH_HASH_MODE && len >= 3 && c[1] == 0) {
    if (c[2] >= 3) return reply_err(ERR_CODE_ILLEGAL_ARG);
    _prefs.path_hash_mode = c[2];
    savePrefs();
    return reply_ok();
  }

  if (c[0] == CMD_REBOOT && len >= 7 && memcmp(&c[1], "reboot", 6) == 0) {
    if (dirty_contacts_expiry) {
      acl.save(_fs);
      dirty_contacts_expiry = 0;
    }
    board.reboot();
    return reply_ok();
  }

  if (c[0] == CMD_GET_BATT_AND_STORAGE) {
    if (out_cap < 11) return 0;
    size_t i = 0;
    out[i++] = RESP_CODE_BATT_AND_STORAGE;
    uint16_t battery_millivolts = board.getBattMilliVolts();
    uint32_t used_kb = SPIFFS.usedBytes() / 1024;
    uint32_t total_kb = SPIFFS.totalBytes() / 1024;
    memcpy(&out[i], &battery_millivolts, 2);
    i += 2;
    memcpy(&out[i], &used_kb, 4);
    i += 4;
    memcpy(&out[i], &total_kb, 4);
    i += 4;
    return i;
  }

  if (c[0] == CMD_GET_STATS && len >= 2) {
    uint8_t stats_type = c[1];
    if (stats_type == STATS_TYPE_CORE) {
      if (out_cap < 11) return 0;
      size_t i = 0;
      out[i++] = RESP_CODE_STATS;
      out[i++] = STATS_TYPE_CORE;
      uint16_t battery_mv = board.getBattMilliVolts();
      uint32_t uptime_secs = _ms->getMillis() / 1000;
      uint8_t queue_len = (uint8_t)_mgr->getOutboundCount(0xFFFFFFFF);
      memcpy(&out[i], &battery_mv, 2);
      i += 2;
      memcpy(&out[i], &uptime_secs, 4);
      i += 4;
      memcpy(&out[i], &_err_flags, 2);
      i += 2;
      out[i++] = queue_len;
      return i;
    }
    if (stats_type == STATS_TYPE_RADIO) {
      if (out_cap < 14) return 0;
      size_t i = 0;
      out[i++] = RESP_CODE_STATS;
      out[i++] = STATS_TYPE_RADIO;
      int16_t noise_floor = (int16_t)_radio->getNoiseFloor();
      int8_t last_rssi = (int8_t)radio_driver.getLastRSSI();
      int8_t last_snr = (int8_t)(radio_driver.getLastSNR() * 4.0f);
      uint32_t tx_air_secs = getTotalAirTime() / 1000;
      uint32_t rx_air_secs = getReceiveAirTime() / 1000;
      memcpy(&out[i], &noise_floor, 2);
      i += 2;
      out[i++] = (uint8_t)last_rssi;
      out[i++] = (uint8_t)last_snr;
      memcpy(&out[i], &tx_air_secs, 4);
      i += 4;
      memcpy(&out[i], &rx_air_secs, 4);
      i += 4;
      return i;
    }
    if (stats_type == STATS_TYPE_PACKETS) {
      if (out_cap < 34) return 0;
      size_t i = 0;
      out[i++] = RESP_CODE_STATS;
      out[i++] = STATS_TYPE_PACKETS;
      uint32_t recv = radio_driver.getPacketsRecv();
      uint32_t sent = radio_driver.getPacketsSent();
      uint32_t n_sent_flood = getNumSentFlood();
      uint32_t n_sent_direct = getNumSentDirect();
      uint32_t n_recv_flood = getNumRecvFlood();
      uint32_t n_recv_direct = getNumRecvDirect();
      uint32_t n_recv_errors = radio_driver.getPacketsRecvErrors();
      memcpy(&out[i], &recv, 4);
      i += 4;
      memcpy(&out[i], &sent, 4);
      i += 4;
      memcpy(&out[i], &n_sent_flood, 4);
      i += 4;
      memcpy(&out[i], &n_sent_direct, 4);
      i += 4;
      memcpy(&out[i], &n_recv_flood, 4);
      i += 4;
      memcpy(&out[i], &n_recv_direct, 4);
      i += 4;
      memcpy(&out[i], &n_recv_errors, 4);
      i += 4;
      return i;
    }
    return reply_err(ERR_CODE_ILLEGAL_ARG);
  }

  if (c[0] == CMD_EXPORT_PRIVATE_KEY) {
#if ENABLE_PRIVATE_KEY_EXPORT
    if (out_cap < 65) return 0;
    out[0] = RESP_CODE_PRIVATE_KEY;
    self_id.writeTo(&out[1], 64);
    return 65;
#else
    return reply_disabled();
#endif
  }

  if (c[0] == CMD_IMPORT_PRIVATE_KEY && len >= 65) {
#if ENABLE_PRIVATE_KEY_IMPORT
    if (!mesh::LocalIdentity::validatePrivateKey(&c[1])) return reply_err(ERR_CODE_ILLEGAL_ARG);
    mesh::LocalIdentity identity;
    identity.readFrom(&c[1], 64);
    IdentityStore store(*_fs, "/identity");
    if (store.save("_main", identity)) {
      self_id = identity;
      acl.load(_fs, self_id);
      return reply_ok();
    }
    return reply_err(ERR_CODE_FILE_IO_ERROR);
#else
    return reply_disabled();
#endif
  }

  if (c[0] == CMD_SEND_TXT_MSG && len >= 14) {
    if (!emit_extra) return reply_err(ERR_CODE_BAD_STATE);
    int ii = 1;
    uint8_t txt_type = c[ii++];
    uint8_t attempt = c[ii++];
    (void)attempt;
    uint32_t msg_timestamp;
    memcpy(&msg_timestamp, &c[ii], 4);
    ii += 4;
    const uint8_t *pub_key_prefix = &c[ii];
    ii += 6;
    if (!isMeshcomodRecipientTcp(pub_key_prefix)) {
      return reply_err(ERR_CODE_UNSUPPORTED_CMD);
    }
    if (txt_type != TXT_TYPE_PLAIN && txt_type != TXT_TYPE_CLI_DATA) {
      return reply_err(ERR_CODE_UNSUPPORTED_CMD);
    }
    if (len >= MAX_FRAME_SIZE) return reply_err(ERR_CODE_ILLEGAL_ARG);
    uint8_t cmd_mut[MAX_FRAME_SIZE + 1];
    memcpy(cmd_mut, c, len);
    cmd_mut[len] = '\0';
    char *text = (char *)&cmd_mut[ii];
    int tlen = (int)len - ii;
    uint32_t expected_ack =
        msg_timestamp ? msg_timestamp : getRTCClock()->getCurrentTimeUnique();
    uint32_t est_timeout = 1;
    uint8_t sent_fr[10];
    sent_fr[0] = RESP_CODE_SENT;
    sent_fr[1] = 0;
    memcpy(&sent_fr[2], &expected_ack, 4);
    memcpy(&sent_fr[6], &est_timeout, 4);
    emit_extra(emit_ctx, sent_fr, sizeof(sent_fr));

    if (txt_type == TXT_TYPE_CLI_DATA) {
      // Same CLI as USB serial; response only via push 0x8C (no CONTACT_MSG_RECV_V3, no send-confirmed ack).
      char tcp_cli_reply[320];
      tcp_cli_reply[0] = '\0';
      if (tlen > 0) {
        text[tlen] = '\0';
        this->handleCommand(msg_timestamp, text, tcp_cli_reply);
      }
      repeaterEmitBinaryCliResponse(emit_extra, emit_ctx, expected_ack, tcp_cli_reply);
      return 0;
    }

    uint8_t confirmed[9];
    confirmed[0] = PUSH_CODE_SEND_CONFIRMED;
    uint32_t trip_time = 0;
    memcpy(&confirmed[1], &expected_ack, 4);
    memcpy(&confirmed[5], &trip_time, 4);
    emit_extra(emit_ctx, confirmed, sizeof(confirmed));
    if (tlen > 0) {
      text[tlen] = '\0';
      handleRepeaterMeshcomodLine(this, text, tlen, emit_extra, emit_ctx);
    } else {
      repeaterEmitMeshcomodText(getRTCClock(), emit_extra, emit_ctx, "usage: wifi help");
    }
    return 0;
  }

  if (c[0] == CMD_FACTORY_RESET) {
    return reply_err(ERR_CODE_UNSUPPORTED_CMD);
  }

  return reply_err(ERR_CODE_UNSUPPORTED_CMD);
}

#endif  // REPEATER_TCP_COMPANION && ESP32
