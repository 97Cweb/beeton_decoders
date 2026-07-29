#include <Beeton.h>

LightThread lightThread;
Beeton beeton;

bool packetSent = false;

void successReceive(uint16_t thing, uint8_t id, uint8_t action, uint16_t seq){
  beeton.goDormant();
}

void failReceive(uint16_t thing, uint8_t id, uint8_t action, uint16_t seq){
  beeton.send(true,0xFFFF,0xFF,0x00);
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    lightThread.begin();
    beeton.begin(lightThread);
    beeton.onAckSuccess(successReceive);
}

void loop() {
    beeton.update();
    delay(10);  
    if(!packetSent){
      if(beeton.send(true,0xFFFF,0xFF,0x00)){
        packetSent = true;
      }
    }
}
