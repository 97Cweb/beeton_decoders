#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SD.h>

extern "C" {
  #include "driver/i2s_pdm.h"
  #include "driver/i2s_common.h"
}

struct BeetonAudioConfig {
  gpio_num_t pinClk = GPIO_NUM_14;
  gpio_num_t pinData = GPIO_NUM_15;
  uint32_t sampleRate = 48000;
  uint8_t maxVoices = 4;
  float masterVolume = 1.0f;
};

class BeetonAudio {
public:
  BeetonAudio();

  bool begin(const BeetonAudioConfig& cfg);
  void update();

  bool play(const char* path, bool loop = false);
  //bool pause(int voiceId);
  //bool resume(int voiceId);
  void stop();
  //void stopAll();

  //bool setVolume(int voiceId, float volume);
  void setMasterVolume(float volume);

  bool isPlaying() const;
  //bool isPaused(int voiceId) const;

private:
  static constexpr size_t FRAMES_PER_CHUNK = 256;
  //static constexpr size_t kMaxVoicesHardLimit = 8;

  /*
  struct Voice {
    bool inUse = false;
    bool playing = false;
    bool paused = false;
    bool loop = false;
    float volume = 1.0f;

    File file;

    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    uint32_t dataStart = 0;
    uint32_t dataLength = 0;
    uint32_t dataRemaining = 0;
  };
  */
  struct WavInfo {
    uint16_t audioFormat = 0;
    uint16_t numChannels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    uint32_t dataStart = 0;
    uint32_t dataSize = 0;
  };

  BeetonAudioConfig _cfg;
  i2s_chan_handle_t _txChan = nullptr;
  //Voice voices_[kMaxVoicesHardLimit];
  //uint8_t _maxVoices = 0;

  File _file;
  WavInfo _wav;
  bool _playing = false;
  bool _loop = false;

  //int32_t _mixBuffer[kFramesPerChunk];
  int16_t _outBuffer[FRAMES_PER_CHUNK * 2];

  bool _initI2S();
  //void _deinitVoices();

  //bool _openWav(Voice& v, const char* path, bool loop, float volume);
  bool _parseWavHeader(File& file, WavInfo& info);
  bool _refillBuffer();
  void _closeFile();
  bool _rewindFile();
  
  //int _findFreeVoice();
  //void _mixVoiceIntoBuffer(Voice& v, size_t frames);
  //void _outputMixedBuffer(size_t frames);
};
