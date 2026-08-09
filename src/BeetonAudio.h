/*
 * TODO: Audio playback correctness
 *
 * Hardware testing is required before considering BeetonAudio complete.
 *
 * 1. Sample-rate handling
 *    The I2S output runs at BeetonAudioConfig::sampleRate, but play()
 *    currently accepts WAV files with any sample rate.
 *
 *    Minimum fix:
 *      Reject files where _wav.sampleRate != _cfg.sampleRate.
 *
 *    Possible future fix:
 *      Resample WAV input to the configured output rate.
 *
 * 2. Respect the WAV data-chunk boundary
 *    Playback currently reads until File::read() fails. Track how many
 *    bytes remain in the WAV data chunk so trailing RIFF chunks are not
 *    interpreted as audio.
 *
 * 3. Handle the final partial buffer
 *    _refillBuffer() currently discards the final samples unless it can
 *    fill all FRAMES_PER_CHUNK frames.
 *
 *    It should return the number of frames produced. update() should write
 *    only those frames, then stop or loop after they have been written.
 *
 * 4. Validate WAV header consistency
 *    Validate blockAlign and byteRate against channels, bit depth, and
 *    sample rate. Account for RIFF chunk padding when chunkSize is odd.
 *
 * 5. Hardware verification
 *    Test mono, stereo, looping, stopping, volume limits, truncated files,
 *    short clips, and clips not divisible by FRAMES_PER_CHUNK.
 */
#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SD.h>

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
  };

  BeetonAudioConfig _cfg;
  i2s_chan_handle_t _txChan = nullptr;

  File _file;
  WavInfo _wav;
  bool _playing = false;
  bool _loop = false;

  int16_t _outBuffer[FRAMES_PER_CHUNK * 2];

  bool _initI2S();

  bool _parseWavHeader(File &file, WavInfo &info);
  bool _refillBuffer();
  void _closeFile();
  bool _rewindFile();
};
