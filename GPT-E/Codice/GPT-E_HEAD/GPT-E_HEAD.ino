#include <WiFi.h>
#include <WiFiClient.h>
#include "AudioFileSource.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"

const char* ssid = "Vodafone-A64688680";
const char* password = "tu3s8utv5ub5tx7w";
WiFiServer server(8080);

AudioGeneratorMP3 *mp3 = nullptr;
AudioOutputI2S *out = nullptr;

class AudioFileSourceTCP : public AudioFileSource {
private:
  WiFiClient* _client;
  static const int PREBUFFER_SIZE = 4096; // Aspetta 4KB prima di iniziare

public:
  AudioFileSourceTCP(WiFiClient* client) { _client = client; }
  
  virtual uint32_t read(void *data, uint32_t len) override {
    if (!_client || !_client->connected()) return 0;
    
    // Se non ci sono dati, aspetta un istante
    while (_client->connected() && _client->available() < len) {
      delay(1); 
    }
    
    return _client->read((uint8_t*)data, len);
  }

  virtual bool isOpen() override { return _client && _client->connected(); }
  virtual bool close() override { return true; }
  virtual bool seek(int32_t pos, int dir) override { return false; }
  virtual uint32_t getSize() override { return 0; }
  virtual uint32_t getPos() override { return 0; }
};

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\n[OK] WiFi Connesso");

  out = new AudioOutputI2S();
  out->SetPinout(33, 32, 25); 
  out->SetOutputModeMono(true); 
  out->SetGain(0.4); 

  server.begin();
}

void loop() {
  WiFiClient client = server.available();
  if (client) {
    Serial.println("[NET] Ricezione stream...");
    
    // Aspettiamo un piccolo buffer iniziale prima di dare in pasto al decoder
    while (client.connected() && client.available() < 2048) {
        delay(10);
    }

    AudioFileSourceTCP *file = new AudioFileSourceTCP(&client);
    mp3 = new AudioGeneratorMP3();
    
    if (mp3->begin(file, out)) {
      while (client.connected() || mp3->isRunning()) {
        if (mp3->isRunning()) {
          if (!mp3->loop()) mp3->stop();
        } else {
          delay(1);
        }
      }
    }
    
    delete mp3; mp3 = nullptr;
    delete file; file = nullptr;
    client.stop();
    Serial.println("[OK] Sessione terminata");
  }
}