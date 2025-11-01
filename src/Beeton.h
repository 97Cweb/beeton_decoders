#ifndef BEETON_PROTOCOL_H
#define BEETON_PROTOCOL_H

#include <Arduino.h>
#include <LightThread.h>
#include <functional>
#include <map>
#include <vector>

namespace BEETON {
constexpr bool RELIABLE = true;
constexpr bool UNRELIABLE = false;
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
    void onMessage(std::function<void(uint16_t thing, uint8_t id, uint8_t action,
                                      const std::vector<uint8_t> &payload)>
                       cb);

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

    void defineThings(const std::vector<BeetonThing> &list);

    void sendAllKnownThingsToUsb();
    void sendFileOverUsb(String filename);
    void sendUsb(const char *fmt, ...);
    void sendCommandFromUsb(String sendCommand);
    void updateUsb();

    std::function<void(uint16_t, uint8_t, uint8_t, const std::vector<uint8_t> &)> messageCallback;

    std::vector<uint8_t> buildPacket(uint16_t thing, uint8_t id, uint8_t action,
                                     const std::vector<uint8_t> &payload);
    bool parsePacket(const std::vector<uint8_t> &raw, uint8_t &version, String &originIp, uint16_t &thing,
                     uint8_t &id, uint8_t &action, std::vector<uint8_t> &payload);
    void handleInternalMessage(const String &srcIp, bool reliable, uint8_t version, uint16_t thing,
                               uint8_t id, uint8_t action, const std::vector<uint8_t> &payload);

    void logBeeton(BeetonLogLevel level, const char *fmt, ...);
    std::vector<String> splitCsv(const String &input);
    String formatPayload(const std::vector<uint8_t> &payload);
    
    // --- IPv6 origin helpers ---
    std::vector<uint8_t> parseIpv6(const String &ip);
    String               formatIpv6(const std::vector<uint8_t> &bytes);

};

#endif // BEETON_PROTOCOL_H
