/*
  ===============================================================
   ESP32 + ChatGPT + Google TTS + Context Memory JSON
   ---------------------------------------------------------------
   - Memoria cronologia persistente salvata in formato JSON su SPIFFS
   - Lettura automatica all'avvio
   - Conversazione continua con contesto
   - Audio TTS multiparte con Google Translate
  ===============================================================
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

#include "AudioGeneratorMP3.h"
#include "AudioFileSourceSPIFFS.h"
#include "AudioOutputI2S.h"

// 🛜 WiFi
const char* ssid = "ssid";
const char* password = "password";

// 🔑 OpenAI API Key
const char* openai_api_key = "sk-...";

// 🔊 I2S pinout
#define I2S_DOUT 25
#define I2S_BCLK 33
#define I2S_LRC  32

// 🎵 Audio
AudioGeneratorMP3 *mp3 = nullptr;
AudioFileSourceSPIFFS *file = nullptr;
AudioOutputI2S *out = nullptr;

// ===============================================================
// Mini classe ChatGPT integrata
// ===============================================================
class ChatGPT {
public:
  ChatGPT(const char* key) { apiKey = String(key); }
  void setSystem(String sys) { systemPrompt = sys; }

  String simpleChat(String userPrompt, WiFiClientSecure *client, String model = "gpt-3.5-turbo") {
    HTTPClient http;
    http.begin("https://api.openai.com/v1/chat/completions");
    http.addHeader("Authorization", "Bearer " + apiKey);
    http.addHeader("Content-Type", "application/json");

    StaticJsonDocument<1024> doc;
    JsonArray messages = doc.createNestedArray("messages");

    JsonObject sys = messages.createNestedObject();
    sys["role"] = "system";
    sys["content"] = systemPrompt;

    JsonObject user = messages.createNestedObject();
    user["role"] = "user";
    user["content"] = userPrompt;

    doc["model"] = model;
    String body;
    serializeJson(doc, body);

    int code = http.POST(body);
    if (code != 200) {
      String err = "Error: HTTP " + String(code) + " - " + http.getString();
      http.end();
      return err;
    }

    String payload = http.getString();
    http.end();

    StaticJsonDocument<8192> resDoc;
    DeserializationError err = deserializeJson(resDoc, payload);
    if (err) {
      Serial.println("⚠️ Errore JSON parse: " + String(err.c_str()));
      return "Error: JSON parsing";
    }

    JsonArray choices = resDoc["choices"];
    if (!choices || choices.size() == 0) return "Error: risposta vuota";

    String text = choices[0]["message"]["content"].as<String>();
    text.trim();
    if (text.length() == 0) return "Error: testo vuoto";
    return text;
  }

private:
  String apiKey;
  String systemPrompt = "Sei un assistente utile.";
};

// Istanza globale
WiFiClientSecure client;
ChatGPT chatGPT(openai_api_key);

// ⚙️ Stato
String inputText = "";
bool playing = false;

// ===============================================================
// Memoria conversazione JSON
// ===============================================================
#define MAX_HISTORY 5
const char* historyFile = "/history.json";
struct Message { String role; String content; };
Message history[MAX_HISTORY];
int historyCount = 0;

String system_prompt = "Sei un assistente vocale in italiano. Rispondi in modo naturale e conciso.";
String model = "gpt-3.5-turbo";

// ===============================================================
// SETUP
// ===============================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n🔧 Avvio ESP32 ChatGPT Assistant...");

  WiFi.begin(ssid, password);
  Serial.print("Connessione WiFi ");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\n✅ Connesso! IP: " + WiFi.localIP().toString());

  if (!SPIFFS.begin(true)) { Serial.println("❌ Errore SPIFFS"); return; }

  configTime(0, 0, "pool.ntp.org");
  client.setInsecure();

  out = new AudioOutputI2S();
  out->SetPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  out->SetChannels(2);
  out->SetGain(0.9);
  out->SetRate(44100);
  out->begin();

  loadHistoryFromJSON();
  Serial.println("\n🗣️ Digita un messaggio o 'reset' per cancellare la memoria.\n");
}

// ===============================================================
// LOOP principale
// ===============================================================
void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (inputText.length() > 0 && !playing) {
        if (inputText.equalsIgnoreCase("reset")) { resetHistory(); inputText = ""; return; }
        Serial.println("\n==============================");
        Serial.println("🗨️  Input: " + inputText);
        Serial.println("==============================");
        getChatGptResponse(inputText);
        inputText = "";
      }
    } else inputText += c;
  }

  // Gestione player MP3
  if (mp3) {
    if (mp3->isRunning()) {
      if (!mp3->loop()) {
        mp3->stop();
        delete mp3; mp3 = nullptr;
        if (file) { delete file; file = nullptr; }
        SPIFFS.remove("/temp.mp3");
        playing = false;
        Serial.println("Fine parlato, puoi scrivere di nuovo.");
      }
    } else {
      mp3->stop();
      delete mp3; mp3 = nullptr;
      if (file) { delete file; file = nullptr; }
      SPIFFS.remove("/temp.mp3");
      playing = false;
      Serial.println("Fine parlato, puoi scrivere di nuovo.");
    }
  }
  delay(2);
}

// ===============================================================
// JSON gestione memoria
// ===============================================================
void saveHistoryToJSON() {
  StaticJsonDocument<2048> doc;
  JsonArray arr = doc.createNestedArray("history");
  for (int i = 0; i < historyCount; i++) {
    JsonObject m = arr.createNestedObject();
    m["role"] = history[i].role;
    m["content"] = history[i].content;
  }
  File f = SPIFFS.open(historyFile, FILE_WRITE);
  if (!f) { Serial.println("Errore salvataggio JSON!"); return; }
  serializeJsonPretty(doc, f);
  f.close();
  Serial.println("Cronologia salvata.");
}

void loadHistoryFromJSON() {
  if (!SPIFFS.exists(historyFile)) { Serial.println("Nessuna cronologia trovata."); return; }
  File f = SPIFFS.open(historyFile, FILE_READ);
  if (!f) { Serial.println("Errore apertura JSON!"); return; }
  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) { Serial.println("⚠️ Errore parsing JSON."); return; }
  JsonArray arr = doc["history"];
  historyCount = 0;
  for (JsonObject m : arr) {
    if (historyCount >= MAX_HISTORY) break;
    history[historyCount].role = m["role"].as<String>();
    history[historyCount].content = m["content"].as<String>();
    historyCount++;
  }
  Serial.printf("Cronologia caricata: %d messaggi.\n", historyCount);
}

void resetHistory() {
  historyCount = 0;
  SPIFFS.remove(historyFile);
  Serial.println("🧹 Cronologia cancellata!");
}

void addToHistory(String role, String content) {
  if (historyCount < MAX_HISTORY) {
    history[historyCount].role = role;
    history[historyCount].content = content;
    historyCount++;
  } else {
    for (int i = 1; i < MAX_HISTORY; i++) history[i - 1] = history[i];
    history[MAX_HISTORY - 1].role = role;
    history[MAX_HISTORY - 1].content = content;
  }
  saveHistoryToJSON();
}

// ===============================================================
// ChatGPT request + risposta vocale
// ===============================================================
void getChatGptResponse(String prompt) {
  playing = true;
  Serial.println("⏳ Attendi, sto pensando...");
  chatGPT.setSystem(system_prompt);

  String fullPrompt = "";
  for (int i = 0; i < historyCount; i++) fullPrompt += history[i].role + ": " + history[i].content + "\n";
  fullPrompt += "utente: " + prompt;

  Serial.println("🤖 Invio richiesta a ChatGPT...");
  String response = chatGPT.simpleChat(fullPrompt, &client, model);

  if (response.startsWith("Error:")) { Serial.println(" X " + response); playing = false; return; }

  Serial.println("\n Risposta ChatGPT:\n" + response + "\n------------------------------");
  addToHistory("utente", prompt);
  addToHistory("assistente", response);
  playLongText(response);
}

// ===============================================================
// TTS multiparte
// ===============================================================
void playLongText(String text) {
  text.trim();
  const int MAX_TTS_LEN = 200;
  int total = (text.length() + MAX_TTS_LEN - 1) / MAX_TTS_LEN;
  Serial.printf(" %d caratteri in %d parti.\n", text.length(), total);

  for (int i = 0; i < total; i++) {
    int s = i * MAX_TTS_LEN;
    int e = min(s + MAX_TTS_LEN, (int)text.length());
    String part = text.substring(s, e);
    Serial.printf("🔹 Parte %d/%d\n", i + 1, total);
    playTextChunk(part);
  }
  Serial.println("Tutto riprodotto.");
}

// ===============================================================
// 🔧 Funzione URL encoding (risolve errori caratteri speciali)
// ===============================================================
String urlEncode(const String &text) {
  String encoded = "";
  char c;
  char buf[4];
  for (int i = 0; i < text.length(); i++) {
    c = text.charAt(i);
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else {
      snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
      encoded += buf;
    }
  }
  return encoded;
}

// ===============================================================
// 🎤 Scarica e prepara la riproduzione TTS (corretto con encoding)
// ===============================================================
void playTextChunk(String text) {
  text.trim();
  if (text.isEmpty()) return;

  String encodedText = urlEncode(text);
  String url = "https://translate.google.com/translate_tts?ie=UTF-8&q=" + encodedText + "&tl=it&client=tw-ob";

  Serial.println("🌐 Richiesta TTS a: " + url);

  HTTPClient http;
  http.begin(url);
  http.addHeader("User-Agent", "Mozilla/5.0");

  int code = http.GET();
  if (code != 200) {
    Serial.println("Errore HTTP: " + String(code));
    http.end();
    return;
  }

  File f = SPIFFS.open("/temp.mp3", FILE_WRITE);
  if (!f) {
    Serial.println("Errore file MP3!");
    http.end();
    return;
  }

  WiFiClient *stream = http.getStreamPtr();
  uint8_t buf[1024];
  while (http.connected()) {
    int c = stream->readBytes(buf, sizeof(buf));
    if (c > 0) f.write(buf, c);
    else break;
    delay(1);
  }

  f.close();
  http.end();

  delay(150);

  Serial.println("File MP3 salvato, preparo riproduzione...");
  file = new AudioFileSourceSPIFFS("/temp.mp3");
  mp3  = new AudioGeneratorMP3();
  mp3->begin(file, out);
}