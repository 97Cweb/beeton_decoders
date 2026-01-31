#include <Beeton.h>

LightThread lightThread;
Beeton beeton;
int oldKnobPos = 0;


bool oldButtonState = false;
unsigned long pressedTime = 0;
const int LONG_PRESS_TIME = 500;

bool sentStop = false;

void setup() {
    Serial.begin(115200);
    delay(1000);

    lightThread.begin();
    beeton.begin(lightThread);

    pinMode(GPIO_NUM_14,INPUT_PULLUP);
    pinMode(GPIO_NUM_0, INPUT);
    
    analogReadResolution(8);

}

void loop() {
    beeton.update();
    if(lightThread.isReady()){
      
      bool buttonState = !digitalRead(GPIO_NUM_14);
      if(oldButtonState == false && buttonState == true){
        pressedTime = millis();
        sentStop = false;
      }
      else if(oldButtonState == true && buttonState == true && !sentStop){
        if(millis()-pressedTime  > LONG_PRESS_TIME){
          Serial.println("stop");
          beeton.send(true,beeton.getThingId("train"),1,beeton.getActionId("train","stop"));
      
          sentStop = true;
        }
      }
      else if(oldButtonState == true && buttonState == false){
        if(!sentStop){
          Serial.println("coast");
          beeton.send(true,beeton.getThingId("train"),1,beeton.getActionId("train","coast"));
      
        }      
      }
      oldButtonState = buttonState;

      int newKnobPos =  analogRead(GPIO_NUM_0); 
      newKnobPos = map(newKnobPos,0,206,0,255);
      if(abs(newKnobPos-oldKnobPos) > 10){
        Serial.printf("knobpos: %d\n",newKnobPos);
        oldKnobPos = newKnobPos;
        beeton.send(false,beeton.getThingId("train"),1,beeton.getActionId("train","setspeed"),newKnobPos);
      }
    }
    delay(10);
}
