#include <WiFi.h>
#include <HTTPClient.h>
#include <FS.h>
#include <SPIFFS.h>
#include <UrlEncode.h>
#include <ArduinoJson.h>

#include "AudioGeneratorMP3.h"
#include "AudioFileSourceSPIFFS.h"
#include "AudioOutputI2S.h"

// ================= CONFIGURAZIONI =================
#define I2C_ADDR 0x12
const char* ssid = "SSID";
const char* password = "password";
// --- PIN ---
#define RX_PIN 4
#define TX_PIN 5
#define LED_PIN 2

#define I2S_DOUT 25
#define I2S_BCLK 33
#define I2S_LRC  32

AudioGeneratorMP3 *mp3 = nullptr;
AudioFileSourceSPIFFS *file = nullptr;
AudioOutputI2S *out = nullptr;

// Buffer per accumulare i dati in arrivo
String inputBuffer = "";

void setup() {
  Serial.begin(115200); 
  Serial2.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN); 

  pinMode(LED_PIN, OUTPUT);
  SPIFFS.begin(true);
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  
  out = new AudioOutputI2S();
  out->SetPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  out->SetGain(0.8);
  
  Serial.println("🎧 PRONTO (Modalità JSON)");
}

void playTTS(String text) {
  if (WiFi.status() != WL_CONNECTED) return;
  if (mp3 && mp3->isRunning()) mp3->stop();

  HTTPClient http;
  String url = "https://translate.google.com/translate_tts?ie=UTF-8&tl=it&client=tw-ob&q=" + urlEncode(text);
  
  Serial.println("🗣️ Scarico audio: " + text);
  
  http.begin(url);
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    File f = SPIFFS.open("/tts.mp3", "w");
    http.writeToStream(&f);
    f.close();
File fCheck = SPIFFS.open("/tts.mp3", "r");
Serial.printf("📏 Dimensione file MP3: %d bytes\n", fCheck.size());
fCheck.close();
    
    if (mp3) delete mp3; 
    if (file) delete file;

    file = new AudioFileSourceSPIFFS("/tts.mp3");
    mp3 = new AudioGeneratorMP3();
    mp3->begin(file, out);
  } else {
    Serial.printf("❌ Errore HTTP: %d\n", httpCode);
  }
  http.end();
}

void processLine(String line) {
  // Pulisce la linea da spazi o caratteri sporchi
  line.trim();
  if (line.length() == 0) return;

  Serial.println("📩 Ricevuto JSON: " + line);

  JsonDocument doc; // ArduinoJson v7
  DeserializationError error = deserializeJson(doc, line);

  if (error) {
    Serial.print("❌ JSON non valido: ");
    Serial.println(error.c_str());
    return;
  }

  const char* cmd = doc["cmd"];
  
  if (strcmp(cmd, "tts") == 0) {
    playTTS(String((const char*)doc["val"]));
  }
  else if (strcmp(cmd, "led") == 0) {
    const char* val = doc["val"];
    bool state = (strcmp(val, "on") == 0);
    digitalWrite(LED_PIN, state ? HIGH : LOW);
    Serial.println(state ? "💡 LED ON" : "💡 LED OFF");
  }
}

void loop() {
  // Legge dalla seriale carattere per carattere
  while (Serial2.available()) {
    char c = Serial2.read();
    
    // Se troviamo il carattere "a capo", significa che il comando è finito
    if (c == '\n') {
      processLine(inputBuffer); // Elabora quello che ha accumulato
      inputBuffer = "";         // Svuota il buffer per il prossimo comando
    } else {
      inputBuffer += c;         // Accumula carattere
    }
  }

  // Audio Loop
  if (mp3 && mp3->isRunning()) {
    if (!mp3->loop()) mp3->stop();
  }
}