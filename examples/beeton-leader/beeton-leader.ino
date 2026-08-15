
#include "Beeton.h"

Beeton beeton;
LightThread lightThread;

void setup() {
    Serial.begin(115200);
    lightThread.begin();
    beeton.begin(lightThread);

    beeton.onMessage([](uint16_t thing, uint8_t id, uint8_t action, const BeetonPayload &payload) {
        Serial.printf("update from %04X:%d received, action %02X occurred\n",thing, id, action);    });
}

void loop() {
    beeton.update();
}
