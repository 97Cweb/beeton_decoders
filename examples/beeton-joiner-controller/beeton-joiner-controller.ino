#include <Beeton.h>

LightThread lightThread;
Beeton beeton;
int oldKnobPos = 0;


bool oldButtonState = false;
unsigned long pressedTime = 0;
const int LONG_PRESS_TIME = 500;

bool sentStop = false;

// Cached Beeton IDs
uint16_t trainThing = 0;
uint8_t stopAction = 0;
uint8_t coastAction = 0;
uint8_t setSpeedAction = 0;

bool mappingsReady = false;

void setup() {
    Serial.begin(115200);
    delay(1000);

    lightThread.begin();
    beeton.begin(lightThread);

    pinMode(GPIO_NUM_14,INPUT_PULLUP);
    pinMode(GPIO_NUM_0, INPUT);
    
    analogReadResolution(8);

    if(!beeton.getThingId("train", trainThing)) {
        Serial.println("ERROR: thing 'train' not found");
        return;
    }

    if(!beeton.getActionId("train", "stop", stopAction)) {
        Serial.println("ERROR: action 'train.stop' not found");
        return;
    }

    if(!beeton.getActionId("train", "coast", coastAction)) {
        Serial.println("ERROR: action 'train.coast' not found");
        return;
    }

    if(!beeton.getActionId("train", "setspeed", setSpeedAction)) {
        Serial.println("ERROR: action 'train.setspeed' not found");
        return;
    }

    mappingsReady = true;

}

void loop() {
    beeton.update();
    if(!mappingsReady){
      delay(10);
      return;
    }

    if(lightThread.isReady()){
      
      bool buttonState = !digitalRead(GPIO_NUM_14);
      if(oldButtonState == false && buttonState == true){
        pressedTime = millis();
        sentStop = false;
      }
      else if(oldButtonState == true && buttonState == true && !sentStop){
        if(millis()-pressedTime  > LONG_PRESS_TIME){
          Serial.println("stop");
          beeton.send(true,trainThing,1,stopAction);
      
          sentStop = true;
        }
      }
      else if(oldButtonState == true && buttonState == false){
        if(!sentStop){
          Serial.println("coast");
          beeton.send(true,trainThing,1,coastAction);
      
        }      
      }
      oldButtonState = buttonState;

      int newKnobPos =  analogRead(GPIO_NUM_0); 
      newKnobPos = map(newKnobPos,0,206,-128,127);
      if(abs(newKnobPos-oldKnobPos) > 10){
        Serial.printf("knobpos: %d\n",newKnobPos);
        oldKnobPos = newKnobPos;
        beeton.send(false,trainThing,1,setSpeedAction,(int8_t) newKnobPos);
      }
    }
    delay(10);
}
