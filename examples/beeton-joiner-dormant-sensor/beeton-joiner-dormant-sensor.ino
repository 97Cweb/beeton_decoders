#include <Beeton.h>

LightThread lightThread;
Beeton beeton;

bool packetSent = false;

void successReceive(uint16_t thing, uint8_t id, uint8_t action, uint16_t seq){
  Serial.println("ack received");
  beeton.goDormant();
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
      Serial.println("waiting to send");
      if(beeton.send(true,0xFFFF,0xFF,0x00)){
        Serial.println("sent");
        packetSent = true;
      }
    }
}
