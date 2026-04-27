#include <Arduino.h>
#include <SD.h>
#include <BeetonAudio.h>

BeetonAudio audio;

void setup() {
  Serial.begin(115200);
  delay(1000);

  if (!SD.begin()) {
    Serial.println("SD init failed");
    while (true) delay(1000);
  }

  BeetonAudioConfig cfg;
  cfg.pinClk = GPIO_NUM_14;
  cfg.pinData = GPIO_NUM_15;
  cfg.sampleRate = 48000;
  cfg.masterVolume = 0.5f;

  if (!audio.begin(cfg)) {
    Serial.println("Audio begin failed");
    while (true) delay(1000);
  }

  if (!audio.play("/test.wav", true)) {
    Serial.println("Playback failed");
  }
}

void loop() {
  audio.update();
}
