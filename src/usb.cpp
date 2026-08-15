#include "Beeton.h"

#include <FS.h>
#include <SD.h>
#include <cstddef>
#include <cstdint>
#include <sys/types.h>

void Beeton::sendAllKnownThingsToUsb() {
  if(!lightThread) {
    return;
  }
  sendUsb("BEGIN_THINGS");
  for(const auto &entry : routingGetKnownDestinations()) {
    uint32_t key = entry.first;
    const String &ip = entry.second;

    uint16_t thing = keyToThing(key);
    uint8_t id = keyToId(key);

    sendUsb("THING %04X:%d", thing, id);
  }
  sendUsb("END_THINGS");
}

void Beeton::sendFileOverUsb(String filename) {
  File f = SD.open("/beeton/" + filename);
  if(!f) {
    sendUsb("ERROR: File %s not found", filename.c_str());
    return;
  }

  sendUsb("BEGIN_FILE,%s", filename.c_str());
  while(f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if(line.length() > 0) {
      sendUsb("%s", line.c_str());
    }
  }
  f.close();
  sendUsb("END_FILE,%s", filename.c_str());
}

void Beeton::sendUsb(const char *fmt, ...) {
  char buffer[BEETON_LOG_BUFFER_SIZE];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);

  Serial.print("[USB] ");
  Serial.println(buffer); // for now just output directly
}

void Beeton::sendRemoteSerialPacket(const BeetonPacket &packet) {
  sendUsb("REMOTE,%s,%s", packet.originIp.c_str(), formatPayload(packet.payload).c_str());
}

void Beeton::sendCommandFromUsb(String sendCommand) {
  std::vector<String> parts = splitCsv(sendCommand);
  if(parts.size() < 4) {
    sendUsb("ERROR: Usage SEND,reliable,thing,id,action,payload[0],payload[1]...");
    return;
  }

  uint32_t parsedReliable;
  uint32_t parsedThing;
  uint32_t parsedId;
  uint32_t parsedAction;

  if(!parseUnsignedField(parts[0], 1, parsedReliable)) {
    sendUsb("ERROR: Invalid reliable value '%s'; expected 0 or 1", parts[0].c_str());
    return;
  }
  if(!parseUnsignedField(parts[1], UINT16_MAX, parsedThing)) {
    sendUsb("ERROR: Invalid thing value '%s'; expected 0..65535", parts[1].c_str());
    return;
  }
  if(!parseUnsignedField(parts[2], UINT8_MAX, parsedId)) {
    sendUsb("ERROR: Invalid Id value '%s'; expected 0..255", parts[2].c_str());
    return;
  }
  if(!parseUnsignedField(parts[3], UINT8_MAX, parsedAction)) {
    sendUsb("ERROR: Invalid action value '%s'; expected 0..255", parts[3].c_str());
    return;
  }

  std::vector<uint8_t> payload;
  for(size_t i = 4; i < parts.size(); ++i) {
    uint32_t parsedByte;
    if(!parseUnsignedField(parts[i], UINT8_MAX, parsedByte)) {
      sendUsb("ERROR: Invalid payload[%zu] value '%s'; expected 0..255", i - 4, parts[i].c_str());
      return;
    }
    payload.push_back(static_cast<uint8_t>(parsedByte));
  }

  send(parsedReliable == 1, static_cast<uint16_t>(parsedThing), static_cast<uint8_t>(parsedId),
       static_cast<uint8_t>(parsedAction), payload);
}

void Beeton::updateUsb() {
  static String input = "";

  while(Serial.available()) {
    char c = Serial.read();

    if(c == '\n' || c == '\r') {
      if(input.length() > 0) {
        handleUsbLine(input);
        input = "";
      }
    } else {
      input += c;
    }
  }
}

void Beeton::handleUsbLine(String input) {
  input.trim();

  if(input.length() == 0) {
    return;
  }

  if(input.equalsIgnoreCase("GETTHINGS")) {
    sendAllKnownThingsToUsb();
    return;
  }

  if(input.startsWith("GETFILE,")) {
    String filename = input.substring(8);
    filename.trim();
    sendFileOverUsb(filename);
    return;
  }

  if(input.startsWith("SEND,")) {
    String sendCommand = input.substring(5);
    sendCommandFromUsb(sendCommand);
    return;
  }

  if(input.equalsIgnoreCase("PACKETTEST")) {
    std::vector<uint8_t> dummy = {1, 2, 3};
    auto raw = buildPacket(0, 0, 0x1234, 1, 42, dummy);

    BeetonPacket packet;

    if(parsePacket(raw, packet)) {
      sendUsb("origin=%s flags=%u seq=%04X thing=%04X id=%u action=%u len=%zu",
              packet.originIp.c_str(), packet.flags, packet.seq, packet.thing, packet.id,
              packet.action, packet.payload.size());
    } else {
      sendUsb("PACKETTEST parse failed");
    }

    return;
  }

  sendUsb("ECHO: %s", input.c_str());
}
