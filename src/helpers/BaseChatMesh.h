#pragma once

#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>
#include <helpers/AdvertDataHelpers.h>
#include <helpers/TxtDataHelpers.h>

#define MAX_TEXT_LEN    (10*CIPHER_BLOCK_SIZE)  // must be LESS than (MAX_PACKET_PAYLOAD - 4 - CIPHER_MAC_SIZE - 1)

#include "ContactInfo.h"

#define MAX_SEARCH_RESULTS   8

#define MSG_SEND_FAILED       0
#define MSG_SEND_SENT_FLOOD   1
#define MSG_SEND_SENT_DIRECT  2

#define REQ_TYPE_GET_STATUS      0x01   // same as _GET_STATS
#define REQ_TYPE_KEEP_ALIVE      0x02

#define RESP_SERVER_LOGIN_OK      0   // response to ANON_REQ

class ContactVisitor {
public:
  virtual void onContactVisit(const ContactInfo& contact) = 0;
};

class BaseChatMesh;

#define MAX_ANON_CONTACTS  8

class ContactsIterator {
  int next_idx;
public:
  // Default-constructible: MyMesh keeps one as a member and assigns it from
  // startContactsIterator() before use. Default to the first REAL contact so a
  // never-assigned iterator still skips the reserved anon slots.
  ContactsIterator() { next_idx = MAX_ANON_CONTACTS; }
  ContactsIterator(int start) { next_idx = start; }
  bool hasNext(const BaseChatMesh* mesh, ContactInfo& dest);
};

#ifndef MAX_CONTACTS
  #define MAX_CONTACTS  32
#endif

#ifndef MAX_CONNECTIONS
  #define MAX_CONNECTIONS  16
#endif

struct ConnectionInfo {
  mesh::Identity server_id;
  unsigned long next_ping;
  uint32_t last_activity;
  uint32_t keep_alive_millis;
  uint32_t expected_ack;
};

struct TxtTxDebugInfo {
  uint32_t uniq_ts = 0;
  uint8_t uniq_attempt = 0;
  uint8_t nonce = 0;
  uint32_t packet_hash4 = 0;
};

#include "ChannelDetails.h"

/**
 *  \brief  abstract Mesh class for common 'chat' client
 */
class BaseChatMesh : public mesh::Mesh {

  friend class ContactsIterator;

  ContactInfo* contacts = nullptr;   // MAX_CONTACTS+MAX_ANON_CONTACTS slots, lazily allocated in PSRAM (see allocateContactSlot)
  int num_contacts;
  int sort_array[MAX_CONTACTS+MAX_ANON_CONTACTS];
  int matching_peer_indexes[MAX_SEARCH_RESULTS];
  unsigned long txt_send_timeout;
#ifdef MAX_GROUP_CHANNELS
  ChannelDetails channels[MAX_GROUP_CHANNELS];
  int num_channels;  // only for addChannel()
#endif
  mesh::Packet* _pendingLoopback;
  uint8_t temp_buf[MAX_TRANS_UNIT];
  ConnectionInfo connections[MAX_CONNECTIONS];
  uint32_t last_txt_tx_hash4;
  bool has_last_txt_tx_hash4;
  uint8_t txt_nonce_counter;
  /** Monotonic per-device send counter folded into plain TXT payload (after C-string + nonce) for unique ciphertext. */
  uint32_t txt_send_seq;
  /** Last timestamp used for plain TXT `uniq_ts` (replaces function-local static for reboot/RNG seeding). */
  uint32_t _txt_last_ts;

  mesh::Packet* composeMsgPacket(const ContactInfo& recipient, uint32_t timestamp, uint8_t attempt, const char *text, uint32_t& expected_ack, uint8_t* out_nonce = nullptr);
  void sendAckTo(const ContactInfo& dest, const uint8_t* ack_hash, uint8_t ack_len=4);

protected:
  BaseChatMesh(mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng, mesh::RTCClock& rtc, mesh::PacketManager& mgr, mesh::MeshTables& tables)
      : mesh::Mesh(radio, ms, rng, rtc, mgr, tables)
  {
    resetContacts();

  #ifdef MAX_GROUP_CHANNELS
    memset(channels, 0, sizeof(channels));
    num_channels = 0;
  #endif
    txt_send_timeout = 0;
    _pendingLoopback = NULL;
    memset(connections, 0, sizeof(connections));
    last_txt_tx_hash4 = 0;
    has_last_txt_tx_hash4 = false;
    txt_nonce_counter = 1;
    txt_send_seq = 0;
    _txt_last_ts = 0;
  }

  // Allocates the lazily-created contact table, returning false when out of memory.
  bool ensureContacts();
  void bootstrapRTCfromContacts();

  void resetContacts() {
    // `contacts` is lazily allocated in PSRAM (see allocateContactSlot), so it is
    // still null when this runs at construction — upstream's array is static, ours
    // is not, and allocating this early would land in internal DRAM before PSRAM
    // is up. The INVARIANT every reader depends on is `num_contacts > 0` implies
    // `contacts != NULL`: readers are gated by `i < num_contacts` and index
    // straight into the table. So the anon-slot reservation is only claimed once
    // the table actually exists — ensureContacts() seeds it at alloc time.
    // Claiming MAX_ANON_CONTACTS unconditionally here broke that invariant and
    // NULL-deref'd at boot on any device whose contact store loaded nothing
    // (fresh install / erase-flash / SD not mounted yet) via the 1.17 addition
    // bootstrapRTCfromContacts().
    if (contacts) {
      memset(contacts, 0, sizeof(contacts[0])*MAX_ANON_CONTACTS);   // set all to have type = ADV_TYPE_NONE(0)
      num_contacts = MAX_ANON_CONTACTS;  // seed the first contacts for anon requests
    } else {
      // Every reader loops on this count and indexes the table without allocating first.
      // Zero keeps them safe until the table exists, and ensureContacts() restores the floor.
      num_contacts = 0;
    }
  }
  void populateContactFromAdvert(ContactInfo& ci, const mesh::Identity& id, const AdvertDataParser& parser, uint32_t timestamp);
  ContactInfo* allocateContactSlot(bool transient_only=false); // helper to find slot for new contact

  // 'UI' concepts, for sub-classes to implement
  virtual bool isAutoAddEnabled() const { return true; }
  virtual bool shouldAutoAddContactType(uint8_t type) const { return true; }
  virtual void onContactsFull() {};
  virtual bool shouldOverwriteWhenFull() const { return false; }
  virtual uint8_t getAutoAddMaxHops() const { return 0; }  // 0 = no limit, 1 = direct (0 hops), N = up to N-1 hops
  virtual void onContactOverwrite(const uint8_t* pub_key) {};
  virtual void onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len, const uint8_t* path) = 0;
  virtual ContactInfo* processAck(const uint8_t *data) = 0;
  virtual void onContactPathUpdated(const ContactInfo& contact) = 0;
  virtual bool onContactPathRecv(ContactInfo& from, uint8_t* in_path, uint8_t in_path_len, uint8_t* out_path, uint8_t out_path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len);
  virtual void onMessageRecv(const ContactInfo& contact, mesh::Packet* pkt, uint32_t sender_timestamp, const char *text) = 0;
  virtual void onCommandDataRecv(const ContactInfo& contact, mesh::Packet* pkt, uint32_t sender_timestamp, const char *text) = 0;
  virtual void onSignedMessageRecv(const ContactInfo& contact, mesh::Packet* pkt, uint32_t sender_timestamp, const uint8_t *sender_prefix, const char *text) = 0;
  virtual uint32_t calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const = 0;
  virtual uint32_t calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis, uint8_t path_len) const = 0;
  virtual void onSendTimeout() = 0;
  virtual void onChannelMessageRecv(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t timestamp, const char *text) = 0;
  virtual void onChannelDataRecv(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint16_t data_type,
                                 const uint8_t* data, size_t data_len) {}
  virtual uint8_t onContactRequest(const ContactInfo& contact, uint32_t sender_timestamp, const uint8_t* data, uint8_t len, uint8_t* reply) = 0;
  virtual void onContactResponse(const ContactInfo& contact, const uint8_t* data, uint8_t len) = 0;
  virtual void handleReturnPathRetry(const ContactInfo& contact, const uint8_t* path, uint8_t path_len);

  virtual void sendFloodScoped(const ContactInfo& recipient, mesh::Packet* pkt, uint32_t delay_millis=0);
  virtual void sendFloodScoped(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t delay_millis=0);

  // storage concepts, for sub-classes to override/implement
  virtual int  getBlobByKey(const uint8_t key[], int key_len, uint8_t dest_buf[]) { return 0; }  // not implemented
  virtual bool putBlobByKey(const uint8_t key[], int key_len, const uint8_t src_buf[], int len) { return false; }

  // Mesh overrides
  void onAdvertRecv(mesh::Packet* packet, const mesh::Identity& id, uint32_t timestamp, const uint8_t* app_data, size_t app_data_len) override;
  int searchPeersByHash(const uint8_t* hash) override;
  void getPeerSharedSecret(uint8_t* dest_secret, int peer_idx) override;
  void onPeerDataRecv(mesh::Packet* packet, uint8_t type, int sender_idx, const uint8_t* secret, uint8_t* data, size_t len) override;
  bool onPeerPathRecv(mesh::Packet* packet, int sender_idx, const uint8_t* secret, uint8_t* path, uint8_t path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len) override;
  void onAckRecv(mesh::Packet* packet, uint32_t ack_crc) override;
#ifdef MAX_GROUP_CHANNELS
  int searchChannelsByHash(const uint8_t* hash, mesh::GroupChannel channels[], int max_matches) override;
#endif
  void onGroupDataRecv(mesh::Packet* packet, uint8_t type, const mesh::GroupChannel& channel, uint8_t* data, size_t len) override;

  // Connections
  bool startConnection(const ContactInfo& contact, uint16_t keep_alive_secs);
  void stopConnection(const uint8_t* pub_key);
  bool hasConnectionTo(const uint8_t* pub_key);
  void markConnectionActive(const ContactInfo& contact);
  ContactInfo* checkConnectionsAck(const uint8_t* data);
  void checkConnections();

public:
  mesh::Packet* createSelfAdvert(const char* name);
  mesh::Packet* createSelfAdvert(const char* name, double lat, double lon);
  /** Touch Heltec: seed TXT uniqueness from RNG after `fast_rng.begin()` (call once from main). */
  void initTxtTxUniquenessFromRng();
  int  sendMessage(const ContactInfo& recipient, uint32_t timestamp, uint8_t attempt, const char* text, uint32_t& expected_ack, uint32_t& est_timeout, uint32_t* out_packet_hash4 = nullptr, TxtTxDebugInfo* out_dbg = nullptr);
  int  sendCommandData(const ContactInfo& recipient, uint32_t timestamp, uint8_t attempt, const char* text, uint32_t& est_timeout, uint32_t* out_packet_hash4 = nullptr, TxtTxDebugInfo* out_dbg = nullptr);
  bool sendGroupMessage(uint32_t timestamp, mesh::GroupChannel& channel, const char* sender_name, const char* text, int text_len);
  bool sendGroupData(mesh::GroupChannel& channel, uint8_t* path, uint8_t path_len, uint16_t data_type, const uint8_t* data, int data_len);
  int  sendLogin(const ContactInfo& recipient, const char* password, uint32_t& est_timeout);
  int  sendAnonReq(const ContactInfo& recipient, const uint8_t* data, uint8_t len, uint32_t& tag, uint32_t& est_timeout);
  int  sendRequest(const ContactInfo& recipient, uint8_t req_type, uint32_t& tag, uint32_t& est_timeout);
  int  sendRequest(const ContactInfo& recipient, const uint8_t* req_data, uint8_t data_len, uint32_t& tag, uint32_t& est_timeout);
  bool shareContactZeroHop(const ContactInfo& contact);
  uint8_t exportContact(const ContactInfo& contact, uint8_t dest_buf[]);
  bool importContact(const uint8_t src_buf[], uint8_t len);
  void resetPathTo(ContactInfo& recipient);
  void scanRecentContacts(int last_n, ContactVisitor* visitor);
  ContactInfo* searchContactsByPrefix(const char* name_prefix);
  ContactInfo* lookupContactByPubKey(const uint8_t* pub_key, int prefix_len);
  bool  removeContact(ContactInfo& contact);
  bool  addContact(const ContactInfo& contact);
  int getTotalContactSlots() const { return num_contacts; }
  // Clamped because num_contacts is zeroed when allocation fails, which would otherwise report a negative count.
  int getNumContacts() const { return num_contacts > MAX_ANON_CONTACTS ? num_contacts - MAX_ANON_CONTACTS : 0; }
  bool getLastTxtTxHash4(uint32_t& out_hash4) const {
    if (!has_last_txt_tx_hash4) return false;
    out_hash4 = last_txt_tx_hash4;
    return true;
  }
  bool getContactByIdx(uint32_t idx, ContactInfo& contact);
  ContactsIterator startContactsIterator();
  ChannelDetails* addChannel(const char* name, const char* psk_base64);
  bool getChannel(int idx, ChannelDetails& dest);
  bool setChannel(int idx, const ChannelDetails& src);
  int findChannelIdx(const mesh::GroupChannel& ch);

  void loop();
};
