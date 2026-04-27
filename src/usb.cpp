#include "Beeton.h"

#include <FS.h>
#include <SD.h>
void Beeton::sendAllKnownThingsToUsb() {
    if(!lightThread) {
        return;
    }
    sendUsb("BEGIN_THINGS");
    for(const auto &entry : thingIdToIp) {
        uint32_t key = entry.first;
        const String &ip = entry.second;

        uint16_t thing = keyToThing(key);
        uint8_t id = keyToId(key);
        unsigned long lastSeen = lightThread->getLastEchoTime(ip);

        sendUsb("THING %04X:%d, lastSeen=%lu ms ago\n", thing, id, millis() - lastSeen);
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
    char buffer[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    Serial.print("[USB] ");
    Serial.println(buffer); // for now just output directly
}

void Beeton::sendCommandFromUsb(String sendCommand) {
    std::vector<String> parts = splitCsv(sendCommand);
    if(parts.size() < 5) {
        sendUsb("ERROR: Usage SEND,reliable,thing,id,action,payload[0],payload[1]...");
        return;
    }
    
    bool reliable = parts[0].toInt();
    uint16_t thing = parts[1].toInt();
    uint8_t id = parts[2].toInt();
    uint8_t actionId = parts[3].toInt();
    std::vector<uint8_t> payload;
    for(size_t i = 4; i < parts.size(); ++i) {
        payload.push_back(parts[i].toInt());
    }

    send(reliable, thing, id, actionId, payload);
    
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
            sendUsb("origin=%s flags=%u seq=%04X thing=%04X id=%u action=%u len=%u",
                    packet.originIp.c_str(),
                    packet.flags,
                    packet.seq,
                    packet.thing,
                    packet.id,
                    packet.action,
                    packet.payload.size());
        } else {
            sendUsb("PACKETTEST parse failed");
        }

        return;
    }

    sendUsb("ECHO: %s", input.c_str());
}
