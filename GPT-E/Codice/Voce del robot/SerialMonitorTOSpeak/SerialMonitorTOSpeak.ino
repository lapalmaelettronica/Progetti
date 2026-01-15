#include <WiFi.h>
#include <HTTPClient.h>
#include <FS.h>
#include <SPIFFS.h>
#include "AudioGeneratorMP3.h"
#include "AudioFileSourceSPIFFS.h"
#include "AudioOutputI2S.h"

// WiFi
const char* ssid = "ssid
";
const char* password = "password";

// I2S pins
#define I2S_DOUT 25
#define I2S_BCLK 33
#define I2S_LRC  32

AudioGeneratorMP3 *mp3 = nullptr;
AudioFileSourceSPIFFS *file = nullptr;
AudioOutputI2S *out = nullptr;

String inputText = "";
bool playing = false;

void setup() {
  Serial.begin(115200);

  // Connetti WiFi
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid,password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected ✅");

  // Monta SPIFFS
  if(!SPIFFS.begin(true)){
    Serial.println("Errore SPIFFS!");
    return;
  }

  // Configura I2S
  out = new AudioOutputI2S();
  out->SetPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  out->begin();

  Serial.println("Scrivi qualcosa e premi INVIO:");
}

void loop() {
  // Lettura input seriale
  if(Serial.available()){
    char c = Serial.read();
    if(c=='\n' || c=='\r'){
      if(inputText.length()>0 && !playing){
        playText(inputText);
        inputText = "";
      }
    } else {
      inputText += c;
    }
  }

  // Loop MP3
  if(mp3 && mp3->isRunning()){
    if(!mp3->loop()){
      mp3->stop(); // Questo chiama già out->stop() internamente
      delete mp3; mp3 = nullptr;
      if(file){ delete file; file = nullptr; }

      // NON chiamare out->stop() o out->flush() qui, causa il crash!
      
      playing = false;
      Serial.println("\n✅ Fine parlato, pronto per nuovo input");

      // Cancella file temporaneo
      SPIFFS.remove("/temp.mp3");
    }
  }
}

void playText(String text){
  text.trim();

  // Tronca al primo doppio spazio
  int pos = text.indexOf("  ");
  if(pos != -1){
    text = text.substring(0,pos);
  }

  Serial.println("Parlo: " + text);

  // URL encode semplice
  text.replace(" ", "%20");
  text.replace("#", "%23");
  text.replace("\"", "%22");

  String url = "https://translate.google.com/translate_tts?ie=UTF-8&q=" + text + "&tl=it&client=tw-ob";
  Serial.println("Scarico MP3 da: " + url);

  // Scarica MP3 e salva su SPIFFS
  HTTPClient http;
  http.begin(url);
  http.addHeader("User-Agent","Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
  int httpCode = http.GET();
  if(httpCode != 200){
    Serial.println("Errore HTTP: " + String(httpCode));
    http.end();
    return;
  }

  File f = SPIFFS.open("/temp.mp3", FILE_WRITE);
  if(!f){
    Serial.println("Errore apertura file SPIFFS!");
    http.end();
    return;
  }

  WiFiClient *stream = http.getStreamPtr();
  int len = http.getSize();
  int bytesRead = 0;
  while(http.connected() && (len > 0 || len == -1)){
    uint8_t buf[512];
    int c = stream->readBytes(buf, sizeof(buf));
    if(c>0){
      f.write(buf,c);
      bytesRead += c;
    } else {
      break;
    }
    // IMPORTANTE: Aggiungi un piccolo delay per evitare
    // il reset da parte del Watchdog Timer (WDT)
    delay(1); 
  }
  f.close();
  http.end();
  Serial.println("Bytes scaricati: " + String(bytesRead));

  // Riproduci
  file = new AudioFileSourceSPIFFS("/temp.mp3");
  mp3 = new AudioGeneratorMP3();
  mp3->begin(file, out);
  playing = true;
}
