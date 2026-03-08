#include "BeetonAudio.h"

BeetonAudio::BeetonAudio(){}

bool BeetonAudio::begin(const BeetonAudioConfig& cfg){
  _cfg = cfg;
  return _initI2S();
}

bool BeetonAudio::_initI2S(){
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.auto_clear = true;

  // Big DMA buffering avoids starvation/beeping
  chan_cfg.dma_desc_num  = 12;
  chan_cfg.dma_frame_num = 512;

  esp_err_t err = i2s_new_channel(&chan_cfg, &_txChan, nullptr);
  if (err != ESP_OK) {
    Serial.printf("i2s_new_channel failed: %d\n", (int)err);
    return false;
  }

  i2s_pdm_tx_config_t pdm_cfg = {
    .clk_cfg  = I2S_PDM_TX_CLK_DEFAULT_CONFIG(_cfg.sampleRate),
    .slot_cfg = I2S_PDM_TX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .clk  = _cfg.pinClk,
      .dout = _cfg.pinData,
      .invert_flags = { .clk_inv = false },
    },
  };

  err = i2s_channel_init_pdm_tx_mode(_txChan, &pdm_cfg);
  if (err != ESP_OK) {
    Serial.printf("i2s_channel_init_pdm_tx_mode failed: %d\n", (int)err);
    return false;
  }

  err = i2s_channel_enable(_txChan);
  if (err != ESP_OK) {
    Serial.printf("i2s_channel_enable failed: %d\n", (int)err);
    return false;
  }

  return true;
}

void BeetonAudio::setMasterVolume(float volume){
  if(volume < 0.0f){
    volume = 0.0f;
  }
  if(volume > 1.0f){
    volume = 1.0f;
  }
  _cfg.masterVolume = volume;
}

bool BeetonAudio::isPlaying() const{
  return _playing;
}

void BeetonAudio::_closeFile(){
  if(_file){
    _file.close();
  }
  _playing = false;
  _loop = false;
}

void BeetonAudio::stop(){
  _closeFile();
}

bool BeetonAudio::_rewindFile(){
  if(!_file){
    return false;
  }
  return _file.seek(_wav.dataStart);
}

bool BeetonAudio::play(const char* path, bool loop){
  _closeFile();
  _file = SD.open(path, FILE_READ);
  if(!_file){
    Serial.printf("BeetonAudio: failed to open %s\n",path);
    return false;
  }

  if(!_parseWavHeader(_file,_wav)){
    Serial.printf("BeetonAudio: invalid/unsupported WAV: %s\n",path);
    _closeFile();
    return false;
  }

  if(_wav.audioFormat != 1){
    Serial.println("BeetonAudio: only PCM WAV supported");
    _closeFile();
    return false;
  }
  if(_wav.bitsPerSample != 16){
    Serial.println("BeetonAudio: only 16-bit WAV supported");
    _closeFile();
    return false;
  }
  if(!(_wav.numChannels == 1 || _wav.numChannels ==2)){
    Serial.println("BeetonAudio: only mono or stereo WAV supported");
    _closeFile();
    return false;
  }
  if(!_file.seek(_wav.dataStart)){
    Serial.println("BeetonAudio: failed to seek WAV data");
    _closeFile();
    return false;
  }

  _loop = loop;
  _playing = true;
  
  Serial.printf("BeetonAudio: playing %s (%lu Hz, %u ch, %u bit)\n",
                path,
                (unsigned long)_wav.sampleRate,
                _wav.numChannels,
                _wav.bitsPerSample);

  return true;
}

void BeetonAudio::update(){
  if(!_playing){ //not playing
    return;
  }
  if(!_refillBuffer()){ //buffer empty
    if(_loop){ //if looping
      if(!_rewindFile()){ //but unable to restart
        _closeFile();
        return;
      }
      if(!_refillBuffer()){ //restarts, but cannot refill buffer
        _closeFile();
        return;
      }
    }
    else{ //not looping
      _closeFile();
      return;
    }
  }
  size_t bytesToWrite = sizeof(_outBuffer);
  size_t bytesWritten = 0;
  
  esp_err_t err = i2s_channel_write(_txChan, _outBuffer, bytesToWrite, &bytesWritten, portMAX_DELAY);

  if (err != ESP_OK || bytesWritten != bytesToWrite) {
    Serial.printf("BeetonAudio: write failed err=%d bw=%u/%u\n",
                  (int)err,
                  (unsigned)bytesWritten,
                  (unsigned)bytesToWrite);
    _closeFile();
  }
}

bool BeetonAudio::_refillBuffer(){
  if(!_file){
    return false;
  }
  const size_t bytesPerInputFrame = _wav.numChannels * sizeof(int16_t);

  for (size_t i = 0; i < FRAMES_PER_CHUNK; i++) {
    uint8_t raw[4] = {0, 0, 0, 0};

    int bytesRead = _file.read(raw, bytesPerInputFrame);
    if (bytesRead != (int)bytesPerInputFrame) {
      return false;
    }

    int16_t left = 0;
    int16_t right = 0;

    if (_wav.numChannels == 1) {
      int16_t mono = (int16_t)(raw[0] | (raw[1] << 8));
      float scaled = mono * _cfg.masterVolume;
      if (scaled > 32767.0f){
        scaled = 32767.0f;
      }
      if (scaled < -32768.0f){
        scaled = -32768.0f;
      }
      int16_t out = (int16_t)scaled;

      left = out;
      right = out;
    } 
    else {
      int16_t inL = (int16_t)(raw[0] | (raw[1] << 8));
      int16_t inR = (int16_t)(raw[2] | (raw[3] << 8));

      float scaledL = inL * _cfg.masterVolume;
      float scaledR = inR * _cfg.masterVolume;

      if (scaledL > 32767.0f){ 
        scaledL = 32767.0f;
      }
      if (scaledL < -32768.0f){ 
        scaledL = -32768.0f;
      }
      if (scaledR > 32767.0f){ 
        scaledR = 32767.0f;
      }
      if (scaledR < -32768.0f){ 
        scaledR = -32768.0f;
      }

      left = (int16_t)scaledL;
      right = (int16_t)scaledR;
    }

    _outBuffer[2 * i + 0] = left;
    _outBuffer[2 * i + 1] = right;
  }

  return true;
}

static bool readExact(File& file, void* dst, size_t len) {
  return file.read((uint8_t*)dst, len) == (int)len;
}

bool BeetonAudio::_parseWavHeader(File& file, WavInfo& info){
  info = WavInfo();
  char riff[4];
  uint32_t riffSize;
  char wave[4];

  if (!readExact(file, riff, 4)) return false;
  if (!readExact(file, &riffSize, 4)) return false;
  if (!readExact(file, wave, 4)) return false;

  if (memcmp(riff, "RIFF", 4) != 0) return false;
  if (memcmp(wave, "WAVE", 4) != 0) return false;

  bool foundFmt = false;
  bool foundData = false;

  while (file.available()) {
    char chunkId[4];
    uint32_t chunkSize = 0;

    if (!readExact(file, chunkId, 4)) return false;
    if (!readExact(file, &chunkSize, 4)) return false;

    if (memcmp(chunkId, "fmt ", 4) == 0) {
      if (chunkSize < 16) return false;

      if (!readExact(file, &info.audioFormat, 2)) return false;
      if (!readExact(file, &info.numChannels, 2)) return false;
      if (!readExact(file, &info.sampleRate, 4)) return false;

      uint32_t byteRate;
      uint16_t blockAlign;

      if (!readExact(file, &byteRate, 4)) return false;
      if (!readExact(file, &blockAlign, 2)) return false;
      if (!readExact(file, &info.bitsPerSample, 2)) return false;

      if (chunkSize > 16) {
        if (!file.seek(file.position() + (chunkSize - 16))) return false;
      }

      foundFmt = true;
    } else if (memcmp(chunkId, "data", 4) == 0) {
      info.dataStart = file.position();
      info.dataSize = chunkSize;

      if (!file.seek(file.position() + chunkSize)) return false;
      foundData = true;
    } else {
      if (!file.seek(file.position() + chunkSize)) return false;
    }

    if (foundFmt && foundData) {
      break;
    }
  }

  return foundFmt && foundData;
}
