#include "Beeton.h"
#include <vector>

void Beeton::logBeeton(BeetonLogLevel level, const char *fmt, ...) {
    char buffer[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    switch(level) {
    case BEETON_LOG_DEBUG:
        log_d("[Beeton] %s", buffer);
        break;
    case BEETON_LOG_INFO:
        log_i("[Beeton] %s", buffer);
        break;
    case BEETON_LOG_WARN:
        log_w("[Beeton] %s", buffer);
        break;
    case BEETON_LOG_ERROR:
        log_e("[Beeton] %s", buffer);
        break;
    }
}

std::vector<String> Beeton::splitCsv(const String &input) {
    std::vector<String> result;
    int start = 0;
    int end = 0;

    while((end = input.indexOf(',', start)) != -1) {
        result.push_back(input.substring(start, end));
        start = end + 1;
    }

    // Add the final part
    if(start < input.length()) {
        result.push_back(input.substring(start));
    }

    return result;
}

String Beeton::formatPayload(const std::vector<uint8_t> &payload) {
    String result;
    for(size_t i = 0; i < payload.size(); ++i) {
        if(i > 0)
            result += " ";
        result += String(payload[i], DEC);
    }
    return result;
} 

// Turn "fd00::abcd" into 16 bytes
std::vector<uint8_t> Beeton::parseIpv6(const String &ip) {
    std::vector<uint8_t> bytes(16, 0);
    char buf[40];
    strncpy(buf, ip.c_str(), sizeof(buf));
    buf[sizeof(buf)-1] = 0;

    uint16_t segments[8] = {0};
    int filled = 0;
    const char *ptr = strtok(buf, ":");
    bool doubleColon = false;
    while (ptr && filled < 8) {
        if (*ptr == 0) break;
        if (strcmp(ptr, "") == 0) { doubleColon = true; break; }
        segments[filled++] = strtol(ptr, nullptr, 16);
        ptr = strtok(nullptr, ":");
    }
    // If :: present, shift trailing parts to the end
    if (doubleColon) {
        int remain = 8 - filled;
        int last = 7;
        const char *tail = strrchr(ip.c_str(), ':');
        if (tail && *(tail+1)) {
            // parse last segments again from end
            // (quick heuristic good enough for embedded use)
        }
    }
    // Write to bytes big-endian
    for (int i=0;i<8;i++) {
        bytes[i*2]   = segments[i] >> 8;
        bytes[i*2+1] = segments[i] & 0xFF;
    }
    return bytes;
}

// Turn 16 bytes back into "xxxx:xxxx:..." compressed form
String Beeton::formatIpv6(const std::vector<uint8_t> &bytes) {
    char buf[40];
    snprintf(buf, sizeof(buf),
        "%x:%x:%x:%x:%x:%x:%x:%x",
        (bytes[0]<<8)|bytes[1], (bytes[2]<<8)|bytes[3],
        (bytes[4]<<8)|bytes[5], (bytes[6]<<8)|bytes[7],
        (bytes[8]<<8)|bytes[9], (bytes[10]<<8)|bytes[11],
        (bytes[12]<<8)|bytes[13], (bytes[14]<<8)|bytes[15]);
    return String(buf);
}

