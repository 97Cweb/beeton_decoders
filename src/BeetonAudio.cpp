#include "BeetonAudio.h"
#include "HardwareSerial.h"
#include <cstddef>
#include <cstdint>

BeetonAudio::BeetonAudio() {}

bool BeetonAudio::begin(const BeetonAudioConfig &cfg) {
  _cfg = cfg;
  return _initI2S();
}

bool BeetonAudio::_initI2S() {
  if(_txChan != nullptr) {
    Serial.println("BeetonAudio: audio output is already initialized");
    return true;
  }
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.auto_clear = true;

  // Big DMA buffering avoids starvation/beeping
  chan_cfg.dma_desc_num = 12;
  chan_cfg.dma_frame_num = 512;

  esp_err_t err = i2s_new_channel(&chan_cfg, &_txChan, nullptr);
  if(err != ESP_OK) {
    Serial.printf("i2s_new_channel failed: %d\n", (int)err);
    return false;
  }

  i2s_pdm_tx_config_t pdm_cfg = {
      .clk_cfg = I2S_PDM_TX_CLK_DEFAULT_CONFIG(_cfg.sampleRate),
      .slot_cfg = I2S_PDM_TX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg =
          {
              .clk = _cfg.pinClk,
              .dout = _cfg.pinData,
              .invert_flags = {.clk_inv = false},
          },
  };

  err = i2s_channel_init_pdm_tx_mode(_txChan, &pdm_cfg);
  if(err != ESP_OK) {
    Serial.printf("i2s_channel_init_pdm_tx_mode failed: %d\n", (int)err);
    i2s_del_channel(_txChan);
    _txChan = nullptr;
    return false;
  }

  err = i2s_channel_enable(_txChan);
  if(err != ESP_OK) {
    Serial.printf("i2s_channel_enable failed: %d\n", (int)err);
    i2s_del_channel(_txChan);
    _txChan = nullptr;
    return false;
  }

  return true;
}

void BeetonAudio::setMasterVolume(float volume) {
  if(volume < 0.0f) {
    volume = 0.0f;
  }
  if(volume > 1.0f) {
    volume = 1.0f;
  }
  _cfg.masterVolume = volume;
}

bool BeetonAudio::isPlaying() const { return _playing; }

void BeetonAudio::_closeFile() {
  if(_file) {
    _file.close();
  }
  _playing = false;
  _loop = false;
  _dataBytesRemaining = 0;
}

void BeetonAudio::stop() { _closeFile(); }

bool BeetonAudio::_rewindFile() {
  if(!_file || !_file.seek(_wav.dataStart)) {
    return false;
  }
  _dataBytesRemaining = _wav.dataSize;
  return true;
}

bool BeetonAudio::play(const char *path, bool loop) {
  _closeFile();
  if(_txChan == nullptr) {
    Serial.println("BeetonAudio: audio output is not initialized");
    return false;
  }

  if(path == nullptr || path[0] == '\0') {
    Serial.println("BeetonAudio: invalid audio path");
    return false;
  }
  _file = SD.open(path, FILE_READ);
  if(!_file) {
    Serial.printf("BeetonAudio: failed to open %s\n", path);
    return false;
  }

  if(!_parseWavHeader(_file, _wav)) {
    Serial.printf("BeetonAudio: invalid/unsupported WAV: %s\n", path);
    _closeFile();
    return false;
  }

  if(_wav.audioFormat != 1) {
    Serial.println("BeetonAudio: only PCM WAV supported");
    _closeFile();
    return false;
  }
  if(_wav.sampleRate != _cfg.sampleRate) {
    Serial.printf("BeetonAudio: unsupported sample rate %lu Hz; expected %lu Hz\n",
                  (unsigned long)_wav.sampleRate, (unsigned long)_cfg.sampleRate);
    _closeFile();
    return false;
  }
  if(_wav.bitsPerSample != 16) {
    Serial.println("BeetonAudio: only 16-bit WAV supported");
    _closeFile();
    return false;
  }
  if(!(_wav.numChannels == 1 || _wav.numChannels == 2)) {
    Serial.println("BeetonAudio: only mono or stereo WAV supported");
    _closeFile();
    return false;
  }

  const uint16_t expectedBlockAlign = _wav.numChannels * (_wav.bitsPerSample / 8);
  const uint64_t expectedByteRate = (uint64_t)_wav.sampleRate * expectedBlockAlign;

  if(_wav.blockAlign != expectedBlockAlign || _wav.byteRate != expectedByteRate) {
    Serial.println("BeetonAudio: inconsistent wav format header");
    _closeFile();
    return false;
  }
  if(_wav.dataSize == 0 || _wav.blockAlign == 0 || (_wav.dataSize % _wav.blockAlign) != 0) {
    Serial.println("BeetonAudio: invalid WAV data size");
    _closeFile();
    return false;
  }

  const uint64_t dataEnd = (uint64_t)_wav.dataStart + _wav.dataSize;
  if(dataEnd > _file.size()) {
    Serial.println("BeetonAudio: truncated WAV data chunk");
    _closeFile();
    return false;
  }
  if(!_file.seek(_wav.dataStart)) {
    Serial.println("BeetonAudio: failed to seek WAV data");
    _closeFile();
    return false;
  }

  _dataBytesRemaining = _wav.dataSize;

  _loop = loop;
  _playing = true;

  Serial.printf("BeetonAudio: playing %s (%lu Hz, %u ch, %u bit)\n", path,
                (unsigned long)_wav.sampleRate, _wav.numChannels, _wav.bitsPerSample);

  return true;
}

void BeetonAudio::update() {
  if(!_playing) { // not playing
    return;
  }
  size_t frames_produced = _refillBuffer();

  if(frames_produced == 0) { // buffer empty
    if(!_loop) {             // if not looping
      _closeFile();
      return;
    }
    if(!_rewindFile()) {
      Serial.println("BeetonAudio: failed to rewind wav");
      _closeFile();
      return;
    }
    frames_produced = _refillBuffer();

    if(frames_produced == 0) {
      Serial.println("BeetonAudio: failed to refill looped wav");
      _closeFile();
      return;
    }
  }

  size_t bytesToWrite = frames_produced * 2 * sizeof(int16_t);
  size_t bytesWritten = 0;

  esp_err_t err = i2s_channel_write(_txChan, _outBuffer, bytesToWrite, &bytesWritten, 0);

  if(err != ESP_OK || bytesWritten != bytesToWrite) {
    Serial.printf("BeetonAudio: write failed err=%d bw=%u/%u\n", (int)err, (unsigned)bytesWritten,
                  (unsigned)bytesToWrite);
    _closeFile();
  }
}

size_t BeetonAudio::_refillBuffer() {
  if(!_file || _dataBytesRemaining == 0) {
    return 0;
  }

  const size_t bytesPerInputFrame = _wav.blockAlign;

  const size_t framesToRead =
      min(FRAMES_PER_CHUNK, (size_t)(_dataBytesRemaining / bytesPerInputFrame));

  size_t frames_produced = 0;

  for(size_t i = 0; i < framesToRead; i++) {
    uint8_t raw[4] = {0, 0, 0, 0};

    int bytesRead = _file.read(raw, bytesPerInputFrame);
    if(bytesRead != (int)bytesPerInputFrame) {
      Serial.println("BeetonAudio: read failed during wav playback");
      _dataBytesRemaining = 0;
      break;
    }

    int16_t left = 0;
    int16_t right = 0;

    if(_wav.numChannels == 1) {
      int16_t mono = (int16_t)(raw[0] | (raw[1] << 8));
      float scaled = mono * _cfg.masterVolume;
      if(scaled > 32767.0f) {
        scaled = 32767.0f;
      }
      if(scaled < -32768.0f) {
        scaled = -32768.0f;
      }
      int16_t out = (int16_t)scaled;

      left = out;
      right = out;
    } else {
      int16_t inL = (int16_t)(raw[0] | (raw[1] << 8));
      int16_t inR = (int16_t)(raw[2] | (raw[3] << 8));

      float scaledL = inL * _cfg.masterVolume;
      float scaledR = inR * _cfg.masterVolume;

      if(scaledL > 32767.0f) {
        scaledL = 32767.0f;
      }
      if(scaledL < -32768.0f) {
        scaledL = -32768.0f;
      }
      if(scaledR > 32767.0f) {
        scaledR = 32767.0f;
      }
      if(scaledR < -32768.0f) {
        scaledR = -32768.0f;
      }

      left = (int16_t)scaledL;
      right = (int16_t)scaledR;
    }

    _outBuffer[2 * i + 0] = left;
    _outBuffer[2 * i + 1] = right;

    frames_produced++;
    _dataBytesRemaining -= bytesPerInputFrame;
  }

  return frames_produced;
}

static bool readExact(File &file, void *dst, size_t len) {
  return file.read((uint8_t *)dst, len) == (int)len;
}
static bool seekForward(File &file, uint64_t byteCount) {
  const uint64_t destination = (uint64_t)file.position() + byteCount;

  if(destination > file.size()) {
    return false;
  }

  return file.seek((uint32_t)destination);
}
bool BeetonAudio::_parseWavHeader(File &file, WavInfo &info) {
  info = WavInfo();
  char riff[4];
  uint32_t riffSize;
  char wave[4];

  if(!readExact(file, riff, 4))
    return false;
  if(!readExact(file, &riffSize, 4))
    return false;
  if(!readExact(file, wave, 4))
    return false;

  if(memcmp(riff, "RIFF", 4) != 0)
    return false;
  if(memcmp(wave, "WAVE", 4) != 0)
    return false;

  const uint64_t riffEnd = (uint64_t)riffSize + 8;

  if(riffEnd < 12 || riffEnd > file.size()) {
    return false;
  }

  bool foundFmt = false;
  bool foundData = false;

  while(file.available()) {
    char chunkId[4];
    uint32_t chunkSize = 0;

    if(!readExact(file, chunkId, 4))
      return false;
    if(!readExact(file, &chunkSize, 4))
      return false;

    if(memcmp(chunkId, "fmt ", 4) == 0) {
      if(chunkSize < 16)
        return false;

      if(!readExact(file, &info.audioFormat, 2))
        return false;
      if(!readExact(file, &info.numChannels, 2))
        return false;
      if(!readExact(file, &info.sampleRate, 4))
        return false;

      if(!readExact(file, &info.byteRate, 4))
        return false;
      if(!readExact(file, &info.blockAlign, 2))
        return false;
      if(!readExact(file, &info.bitsPerSample, 2))
        return false;

      const uint64_t remainingFmtBytes = (uint64_t)(chunkSize - 16) + (chunkSize & 1U);

      if(!seekForward(file, remainingFmtBytes)) {
        return false;
      }

      foundFmt = true;
    } else if(memcmp(chunkId, "data", 4) == 0) {
      info.dataStart = file.position();
      info.dataSize = chunkSize;

      const uint64_t paddedChunkSize = (uint64_t)chunkSize + (chunkSize & 1U);

      if(!seekForward(file, paddedChunkSize)) {
        return false;
      }
      foundData = true;
    } else {
      const uint64_t paddedChunkSize = (uint64_t)chunkSize + (chunkSize & 1U);

      if(!seekForward(file, paddedChunkSize)) {
        return false;
      }
    }

    if(foundFmt && foundData) {
      break;
    }
  }

  return foundFmt && foundData;
}
