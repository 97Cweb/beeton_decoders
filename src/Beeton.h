#ifndef BEETON_PROTOCOL_H
#define BEETON_PROTOCOL_H

#include <Arduino.h>
#include <LightThread.h>
#include <functional>
#include <map>
#include <vector>
#include <functional>

namespace BEETON {
static constexpr uint8_t BEETON_FLAG_ACK      = 0x01;
static constexpr uint8_t BEETON_FLAG_RELIABLE = 0x02;
}

enum BeetonLogLevel { BEETON_LOG_DEBUG, BEETON_LOG_INFO, BEETON_LOG_WARN, BEETON_LOG_ERROR };

struct BeetonThing {
    uint16_t thing;
    uint8_t id;
};

class Beeton {
  public:
    void begin(LightThread &lt);
    void update();

    // Simple send API
    bool send(bool reliable, uint16_t thing, uint8_t id, uint8_t action);
    bool send(bool reliable, uint16_t thing, uint8_t id, uint8_t action, uint8_t payloadByte);
    bool send(bool reliable, uint16_t thing, uint8_t id, uint8_t action,
              const std::vector<uint8_t> &payload);

    // Message receive handler
    using MessageCallback = std::function<void(uint16_t thing, uint8_t id, uint8_t action,
                                      const std::vector<uint8_t> &payload)>;
    void onMessage(MessageCallback cb){ messageCallback = std::move(cb);}
                       
    // === Reliability Callbacks ===
    using AckSuccessCallback = std::function<void(uint16_t thing, uint8_t id, uint8_t action, uint16_t seq)>;
    using AckFailCallback    = std::function<void(uint16_t thing, uint8_t id, uint8_t action, uint16_t seq)>;

    void onAckSuccess(AckSuccessCallback cb) { ackSuccessCb = std::move(cb); }
    void onAckFail(AckFailCallback cb)       { ackFailCb = std::move(cb); }
    
    
    
    String getThingName(uint16_t thing);
    String getActionName(String thingName, uint8_t actionId);
    uint16_t getThingId(const String &name);
    uint8_t getActionId(const String &thingName, const String &actionName);

  private:
    LightThread *lightThread = nullptr;
    std::map<uint32_t, String> thingIdToIp; // thing<<8 | id → IP
    std::vector<BeetonThing> localThings;
    std::map<String, uint16_t> nameToThing;
    std::map<uint16_t, String> thingToName;
    std::map<String, std::map<String, uint8_t>> actionNameToId;
    std::map<String, std::map<uint8_t, String>> actionIdToName;
    bool usbConnected = false;

    void loadMappings(const char *thingsPath = "/beeton/all_things.csv",
                      const char *actionsPath = "/beeton/all_actions.csv",
                      const char *definePath = "/beeton/define_this.csv");
    void ensureFileExists(const char *path);
    void loadThings(const char *path);
    void loadActions(const char *path);
    void loadDefines(const char *path);
    
    // --- Constants ---
    static constexpr uint8_t BEETON_FLAG_ACK      = 0x01;
    static constexpr uint8_t BEETON_FLAG_RELIABLE = 0x02;
  
    // --- Reliability state ---
    struct Pending {
        String destIp;
        String originIp;
        uint16_t thing;
        uint8_t id, action;
        std::vector<uint8_t> payload;
        uint16_t seq;
        uint32_t nextDueMs;
        uint16_t timeoutMs;
        uint8_t  retriesLeft;
    };

    struct SeqKey {
        String origin;
        uint16_t seq;
    };
    
    std::map<uint16_t, Pending> pending;
    std::vector<std::pair<SeqKey, uint32_t>> seen;
    
    // State exposed to parser/handler
    uint8_t  lastFlags = 0;
    uint16_t lastSeq   = 0;
    uint16_t nextSeq   = 1;
    
    AckSuccessCallback ackSuccessCb;
    AckFailCallback    ackFailCb;
    MessageCallback    messageCallback;
    
    void defineThings(const std::vector<BeetonThing> &list);

    void sendAllKnownThingsToUsb();
    void sendFileOverUsb(String filename);
    void sendUsb(const char *fmt, ...);
    void sendCommandFromUsb(String sendCommand);
    void updateUsb();

    std::vector<uint8_t> buildPacket(uint8_t flags, uint16_t seq, uint16_t thing, uint8_t id, uint8_t action,
                                         const std::vector<uint8_t> &payload);
    bool parsePacket(const std::vector<uint8_t> &raw, uint8_t &version, String &originIp, uint8_t &flags, uint16_t &seq, 
                        uint16_t &thing, uint8_t &id, uint8_t &action, std::vector<uint8_t> &payload);
    // Internal message hook (used by UDP recv)
    void handlePacket(const std::vector<uint8_t> &raw,
                               uint8_t version, const String& originIp, uint8_t flags, uint16_t seq,
                               uint16_t thing, uint8_t id, uint8_t action,
                               const std::vector<uint8_t>& payload);

    void logBeeton(BeetonLogLevel level, const char *fmt, ...);
    std::vector<String> splitCsv(const String &input);
    String formatPayload(const std::vector<uint8_t> &payload);
    
    // --- IPv6 origin helpers ---
    std::vector<uint8_t> parseIpv6(const String &ip);
    String               formatIpv6(const std::vector<uint8_t> &bytes);
    
    
    // --- Internal helpers ---
    uint16_t allocSeq();
    // === Internal tick for reliability retries ===
    void pumpReliable();
    bool wasSeenAndMark(const String& origin, uint16_t seq, uint32_t nowMs);

};

#endif // BEETON_PROTOCOL_H
