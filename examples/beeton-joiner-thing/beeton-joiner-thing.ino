#include <Beeton.h>

LightThread lightThread;
Beeton beeton;

const int IN1 = GPIO_NUM_22;
const int IN2 = GPIO_NUM_23;

const uint32_t frequency = 20000;
const uint8_t resolution = 8;

void setup() {
    Serial.begin(115200);
    delay(1000);

    ledcAttach(IN1,frequency,resolution);
    ledcAttach(IN2,frequency,resolution);

    lightThread.begin();
    beeton.begin(lightThread);

    // Handle only user-defined actions
    beeton.onMessage([](uint16_t thingId, uint8_t id, uint8_t actionId, const std::vector<uint8_t>& payload) {
        String thing = beeton.getThingName(thingId);
        String action = beeton.getActionName(thing, actionId);
        if (thing == "train"){
          if (action == "setspeed" && payload.size() == 1) {
            int speed = payload[0]*2 - 255;
            Serial.println(speed);
            if(speed >=0){
              ledcWrite(IN1, speed);
              ledcWrite(IN2, 0);
            }
            else{
              ledcWrite(IN1,0);
              ledcWrite(IN2, abs(speed));
            }
          } 
          if (action == "coast"){
            Serial.println("coasting");
            ledcWrite(IN1, 0);
            ledcWrite(IN2, 0);

          }
          if(action == "stop"){
            Serial.println("stopping");
            ledcWrite(IN1, 255);
            ledcWrite(IN2, 255);
          }
        } 
    });
}

void loop() {
    beeton.update();
    delay(10);
}
