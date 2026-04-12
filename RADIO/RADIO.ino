#include "Arduino.h"
#include "WiFi.h"
#include "Audio_nopsram.h"

// Digital I/O per I2S
#define I2S_DOUT      25
#define I2S_BCLK      33
#define I2S_LRC       32

String ssid =     "Vodafone-A64688680";
String password = "tu3s8utv5ub5tx7w";

Audio audio;

void setup() {
    Serial.begin(115200);
    
    // Mostra la memoria libera all'avvio per debug
    Serial.printf("\n[DEBUG] Memoria Heap libera all'avvio: %d byte\n", ESP.getFreeHeap());

    WiFi.begin(ssid.c_str(), password.c_str());
    Serial.print("Connessione al WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nConnesso al WiFi!");

    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(21); // default 0...21

    // --- SCELTA DELLO STREAM ---
    // OPZIONE 1: Il tuo stream AAC originale (Potrebbe bloccarsi senza PSRAM)
    // audio.connecttohost("http://stream.antennethueringen.de/live/aac-64/stream.antennethueringen.de/");

    // OPZIONE 2: Stream MP3 (CONSIGLIATO per ESP32 senza PSRAM)
    // Se l'opzione 1 fallisce, commentala e de-commenta questa riga qui sotto:
    audio.connecttohost("http://icecast.unitedradio.it/Virgin.mp3");
}

void loop(){
    // La funzione audio.loop() deve girare senza pause
    audio.loop(); 
}

// =================================================================
// CALLBACK DI DEBUG STANDARD DELLA LIBRERIA (Opzionali ma utilissimi)
// =================================================================
void audio_info(const char *info){
    Serial.print("Info: "); Serial.println(info);
}

void audio_showstreaminfo(const char *info){
    Serial.print("Stream Info: "); Serial.println(info);
}

void audio_showstation(const char *info){
    Serial.print("Stazione: "); Serial.println(info);
}

void audio_showstreamtitle(const char *info){
    Serial.print("Titolo Brano: "); Serial.println(info);
}

// Questo è vitale: ti avvisa se c'è un errore grave (es. memoria esaurita)
void audio_error(const char *info){
    Serial.print("ERRORE: "); Serial.println(info);
    Serial.printf("[DEBUG] Memoria Heap rimasta al crash: %d byte\n", ESP.getFreeHeap());
}