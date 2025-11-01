#include "Beeton.h"

#include <FS.h>
#include <SD.h>
void Beeton::sendAllKnownThingsToUsb() {
    if(!lightThread) {
        return;
    }
    sendUsb("BEGIN_THINGS");
    for(const auto &entry : thingIdToIp) {
        uint16_t key = entry.first;
        const String &ip = entry.second;

        uint8_t thing = (key >> 8) & 0xFF;
        uint8_t id = key & 0xFF;
        unsigned long lastSeen = lightThread->getLastEchoTime(ip);

        sendUsb("THING %02X:%d, lastSeen=%lu ms ago\n", thing, id, millis() - lastSeen);
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
    if(parts.size() >= 5) {
        bool reliable = parts[0].toInt();
        uint8_t thingId = parts[1].toInt();
        uint8_t id = parts[2].toInt();
        uint8_t actionId = parts[3].toInt();
        std::vector<uint8_t> payload;
        for(size_t i = 4; i < parts.size(); ++i) {
            payload.push_back(parts[i].toInt());
        }

        if(thingId == 0xFF || actionId == 0xFF) {
            sendUsb("ERROR: Unknown thing/action");
        } else {
            send(reliable, thingId, id, actionId, payload);
        }
    } else {
        sendUsb("Error: Usage SEND,reliable,thing,id,action,payload[0],payload[1]...");
    }
}

void Beeton::updateUsb() {
    static String input = "";

    while(Serial.available()) {
        char c = Serial.read();
        if(c == '\n' || c == '\r') {
            if(input.length() > 0) {
                input.trim(); // Remove any accidental whitespace

                if(input.equalsIgnoreCase("GETTHINGS")) {
                    sendAllKnownThingsToUsb();
                }
                else if(input.startsWith("GETFILE,")) {
                    String filename = input.substring(8);
                    sendFileOverUsb(filename);
                } 
                else if(input.startsWith("SEND,")) {
                    String sendCommand = input.substring(5);
                    sendCommandFromUsb(sendCommand);
                }
                else if (input == "PACKETTEST") {
                    std::vector<uint8_t> dummy = {1,2,3};
                    auto raw = this->buildPacket(0x1234, 1, 42, dummy);
                    String origin; uint16_t t; uint8_t id,a; std::vector<uint8_t> pl;
                    uint8_t version = 1;
                    this->parsePacket(raw, version, origin, t, id, a, pl);
                    sendUsb("origin=%s thing=%04X id=%u action=%u len=%d",
                            origin.c_str(), t, id, a, pl.size());
                }

                else {
                    sendUsb("ECHO: %s", input.c_str());
                }
                input = "";
            }
        } else {
            input += c;
        }
    }
}
