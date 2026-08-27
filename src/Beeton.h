#ifndef BEETON_PROTOCOL_H
#define BEETON_PROTOCOL_H

#include <Arduino.h>
#include <LightThread.h>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

#include "BeetonRouting.h"
#include "BeetonConfig.h"

enum BeetonLogLevel { BEETON_LOG_DEBUG, BEETON_LOG_INFO, BEETON_LOG_WARN, BEETON_LOG_ERROR };

struct BeetonPacket {
  uint8_t version = 0;
  String originIp;
  uint8_t flags = 0;
  uint16_t seq = 0;
  uint16_t thing = 0;
  uint8_t id = 0;
  uint8_t action = 0;
  std::vector<uint8_t> payload;
};

struct BeetonThing {
  uint16_t thing;
  uint8_t id;
};

class BeetonPayload{
  public:
    bool hasValue() const;
    bool hasBytes() const;

    int64_t getValue(int64_t fallback=0) const;
    const std::vector<uint8_t> &getBytes() const;

  private:
    friend class Beeton;

    enum class Type: uint8_t{
      NONE = 0x00,
      BOOL = 0x01,

      UINT8 = 0x10,
      UINT16 = 0x11,
      UINT32 = 0x12,

      INT8 = 0x20,
      INT16 = 0x21,
      INT32 = 0x22,

      BYTES = 0xF0
    };

    Type type = Type::NONE;
    int64_t value = 0;
    std::vector<uint8_t> bytes;
};

class Beeton {
public:
  void begin(LightThread &lt);
  void update();
  bool isReady();
  bool isNetworkReady() const;
  void setNetworkReady(bool ready = true);
  bool goDormant();

  // Simple send API
  // No payload
  bool send(bool reliable, uint16_t thing, uint8_t id, uint8_t action);

  // Scalar payloads
  bool send(bool reliable, uint16_t thing, uint8_t id, uint8_t action, bool value);

  bool send(bool reliable, uint16_t thing, uint8_t id, uint8_t action, uint8_t value);
  bool send(bool reliable, uint16_t thing, uint8_t id, uint8_t action, uint16_t value);
  bool send(bool reliable, uint16_t thing, uint8_t id, uint8_t action, uint32_t value);

  bool send(bool reliable, uint16_t thing, uint8_t id, uint8_t action, int8_t value);
  bool send(bool reliable, uint16_t thing, uint8_t id, uint8_t action, int16_t value);
  bool send(bool reliable, uint16_t thing, uint8_t id, uint8_t action, int32_t value);

  // Raw byte-array payload
  bool send(bool reliable, uint16_t thing, uint8_t id, uint8_t action,
            const std::vector<uint8_t> &bytes);

  // Message receive handler
  using MessageCallback = std::function<void(uint16_t thing, uint8_t id, uint8_t action,const BeetonPayload &payload)>;
  void onMessage(MessageCallback cb) { messageCallback = std::move(cb); }

  // === Reliability Callbacks ===
  using AckSuccessCallback =
      std::function<void(uint16_t thing, uint8_t id, uint8_t action, uint16_t seq)>;
  using AckFailCallback =
      std::function<void(uint16_t thing, uint8_t id, uint8_t action, uint16_t seq)>;

  void onAckSuccess(AckSuccessCallback cb) { ackSuccessCb = std::move(cb); }
  void onAckFail(AckFailCallback cb) { ackFailCb = std::move(cb); }

  String getThingName(uint16_t thing);
  String getActionName(const String &thingName, uint8_t actionId);
  bool getThingId(const String &name, uint16_t &outThing);
  bool getActionId(const String &thingName, const String &actionName, uint8_t &outAction);
  bool thingExists(uint16_t thing);
  bool actionExists(const String &thingName, uint8_t actionId);

private:
  LightThread *lightThread = nullptr;
  std::vector<BeetonThing> localThings;
  std::map<String, uint16_t> nameToThing;
  std::map<uint16_t, String> thingToName;
  std::map<String, std::map<String, uint8_t>> actionNameToId;
  std::map<String, std::map<uint8_t, String>> actionIdToName;


  // --- Routing integration ---
  void routingBegin();
  void routingUpdate();
  void routingOnLightThreadReady();
  void routingOnLightThreadLost();
  void routingHandleAck(uint16_t thing, uint8_t id, uint8_t action, uint16_t seq);

  bool routingHandlePacket(const BeetonPacket &packet);
  bool routingFindDestination(uint16_t thing, uint8_t id, String &outIp);
  bool routingIsLocalDestination(uint16_t thing, uint8_t id);
  void routingHandleAckFailure(uint16_t thing, uint8_t id, uint8_t action, std::uint16_t seq);

  const std::map<uint32_t, String> &routingGetKnownDestinations();

  void loadMappings(const char *thingsPath = "/beeton/all_things.csv",
                    const char *actionsPath = "/beeton/all_actions.csv",
                    const char *definePath = "/beeton/define_this.csv");
  void ensureFileExists(const char *path);
  void loadThings(const char *path);
  void loadActions(const char *path);
  void loadDefines(const char *path);

  bool routingLightThreadWasReady = false;

  bool isSetup = false;
  bool networkReady = false;

  // --- Reliability state ---
  struct Pending {
    String destIp;
    uint16_t thing;
    uint8_t id, action;
    std::vector<uint8_t> payload;
    uint16_t seq;
    uint32_t nextDueMs;
    uint16_t timeoutMs;
    uint8_t retriesLeft;
  };

  struct SeqKey {
    String origin;
    uint16_t seq;
  };

  std::map<uint16_t, Pending> pending;
  std::vector<std::pair<SeqKey, uint32_t>> seen;

  AckSuccessCallback ackSuccessCb;
  AckFailCallback ackFailCb;
  MessageCallback messageCallback;

  void defineThings(const std::vector<BeetonThing> &list);

  bool sendPacket(bool reliable, uint16_t thing, uint8_t id, uint8_t action, const std::vector<uint8_t>&payload,bool requireNetworkReady);
  void sendAllKnownThingsToUsb();
  void sendFileOverUsb(String filename);
  void sendUsb(const char *fmt, ...);
  void sendCommandFromUsb(String sendCommand);
  void updateUsb();
  void handleUsbLine(String input);
  void sendRemoteSerialPacket(const BeetonPacket &packet);

  std::vector<uint8_t> buildPacket(uint8_t flags, uint16_t seq, uint16_t thing, uint8_t id,
                                   uint8_t action, const std::vector<uint8_t> &payload);
  bool parsePacket(const std::vector<uint8_t> &raw, BeetonPacket &packet);
  // Internal message hook (used by UDP recv)
  void handlePacket(const std::vector<uint8_t> &raw, const BeetonPacket &packet);
  bool handleAckPacket(const BeetonPacket &packet);
  bool handleReliablePacket(const BeetonPacket &packet);
  bool handleLeaderControlPacket(const BeetonPacket &packet);
  bool forwardPacketIfLeader(const std::vector<uint8_t> &raw, const BeetonPacket &packet);
  void dispatchLocalPacket(const BeetonPacket &packet);

  void logBeeton(BeetonLogLevel level, const char *fmt, ...);
  std::vector<String> splitCsv(const String &input);
  bool parseUnsignedField(const String &field, uint32_t maximum, uint32_t &result);
  String formatPayload(const std::vector<uint8_t> &payload);

  std::vector<uint8_t> encodeValuePayload(uint8_t type, uint64_t value,
                                          uint8_t width);
  bool decodePayload(const std::vector<uint8_t> &encoded,
                     BeetonPayload &decoded);

  // --- IPv6 origin helpers ---
  std::vector<uint8_t> parseIpv6(const String &ip);
  String formatIpv6(const std::vector<uint8_t> &bytes);

  uint16_t keyToThing(uint32_t key);
  uint8_t keyToId(uint32_t key);

  // --- Internal helpers ---
  uint16_t allocSeq();
  bool isLeaderAddress(const BeetonPacket &packet);
  void appendUint16(std::vector<uint8_t> &out, uint16_t value);
  uint16_t readUint16(const std::vector<uint8_t> &data, size_t offset);
  // === Internal tick for reliability retries ===
  void pumpReliable();
  bool wasSeenAndMark(const String &origin, uint16_t seq, uint32_t nowMs);
};

#endif // BEETON_PROTOCOL_H
