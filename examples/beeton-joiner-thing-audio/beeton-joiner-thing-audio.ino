#include <Beeton.h>
#include <BeetonAudio.h>
#include <SD.h>

LightThread lightThread;
Beeton beeton;
BeetonAudio audio;
int engineVoice = -1;


void setup() {
    Serial.begin(115200);
    delay(1000);


    lightThread.begin();
    beeton.begin(lightThread);


    //audio
    SD.begin();
    audio.begin({
      .pinClk = 14,
      .pinData = 15,
      .sampleRate = 48000,
      .maxVoices = 4,
      .masterVolume = 0.7f
    });

    engineVoice = audio.play("/engine_idle.wav", true, 0.4f);
}

void loop() {
    beeton.update();
    audio.update();
    delay(10);
}
