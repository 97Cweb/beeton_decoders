/*
 * TODO: Audio hardware verification
 *
 * The WAV reader currently supports:
 *   - Uncompressed PCM WAV files
 *   - 16-bit mono or stereo samples
 *   - Files matching BeetonAudioConfig::sampleRate
 *
 * Unsupported or malformed files are rejected without starting playback.
 *
 * Before considering BeetonAudio hardware-verified:
 *   - Confirm the installed amplifier/DAC model and that it is compatible
 *     with the ESP32-C6 PDM TX output used here.
 *   - Test mono and stereo playback.
 *   - Test looping and manual stop.
 *   - Test short clips and final partial buffers.
 *   - Test truncated and malformed WAV files.
 *   - Test volume limits.
 *
 * Resampling is not implemented. WAV files must match the configured
 * output sample rate.
 */
#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <cstddef>
#include <cstdint>

extern "C" {
#include "driver/i2s_common.h"
#include "driver/i2s_pdm.h"
}

struct BeetonAudioConfig {
  gpio_num_t pinClk = GPIO_NUM_14;
  gpio_num_t pinData = GPIO_NUM_15;
  uint32_t sampleRate = 48000;
  float masterVolume = 1.0f;
};

class BeetonAudio {
public:
  BeetonAudio();

  bool begin(const BeetonAudioConfig &cfg);
  void update();

  bool play(const char *path, bool loop = false);
  void stop();

  void setMasterVolume(float volume);

  bool isPlaying() const;

private:
  static constexpr size_t FRAMES_PER_CHUNK = 256;

  struct WavInfo {
    uint16_t audioFormat = 0;
    uint16_t numChannels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    uint32_t dataStart = 0;
    uint32_t dataSize = 0;
    uint32_t byteRate = 0;
    uint16_t blockAlign = 0;
  };

  BeetonAudioConfig _cfg;
  i2s_chan_handle_t _txChan = nullptr;

  File _file;
  WavInfo _wav;
  bool _playing = false;
  bool _loop = false;

  uint32_t _dataBytesRemaining = 0;

  int16_t _outBuffer[FRAMES_PER_CHUNK * 2];

  bool _initI2S();

  bool _parseWavHeader(File &file, WavInfo &info);
  size_t _refillBuffer();
  void _closeFile();
  bool _rewindFile();
};
