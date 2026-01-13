#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

#define CE_PIN 9         // PB1
#define CSN_PIN 8        // PB0
#define BUTTON_ONOFF 7   // PD7
#define BUTTON_VOICE 6   // PD6
#define LED_PIN 5        // PD5

const char* ssid = "ssid";
const char* password = "password";

const char* raspberryIP = "192.168.1.33";
const unsigned int raspberryPort = 21041;

RF24 radio(CE_PIN, CSN_PIN);
WiFiUDP udp;
Adafruit_ADS1115 ads;

#define SAMPLE_RATE_HZ 8000
#define MAX_SAMPLES 32000
#define PACKET_SIZE 512

bool stato = false;
bool lastOnOffState = HIGH;
bool recording = false;

uint16_t samples[MAX_SAMPLES];
uint16_t sampleIndex = 0;

const byte address[6] = "00001";

void setup() {
  pinMode(BUTTON_ONOFF, INPUT_PULLUP);
  pinMode(BUTTON_VOICE, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.begin(115200);

  // NRF24L01 radio init
  radio.begin();
  radio.setPALevel(RF24_PA_MIN);
  radio.setRetries(5, 15);
  radio.openWritingPipe(address);
  radio.stopListening();

  // WiFi init
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  udp.begin(raspberryPort);

  Wire.begin();
  ads.begin();
  ads.setGain(GAIN_ONE);
}

void sendAudioDataUDP() {
  uint8_t packetBuffer[PACKET_SIZE];
  uint16_t bytesToSend = sampleIndex * 2;
  uint16_t offset = 0;
  uint8_t seq = 0;

  while (bytesToSend > 0) {
    uint16_t chunkSize = (bytesToSend > (PACKET_SIZE - 1)) ? (PACKET_SIZE - 1) : bytesToSend;
    packetBuffer[0] = seq++;
    memcpy(&packetBuffer[1], ((uint8_t*)samples) + offset, chunkSize);

    udp.beginPacket(raspberryIP, raspberryPort);
    udp.write(packetBuffer, chunkSize + 1);
    udp.endPacket();

    offset += chunkSize;
    bytesToSend -= chunkSize;
    delay(10);
  }
}

void loop() {
  // Gestione ON/OFF via NRF24L01
  bool currentOnOffState = digitalRead(BUTTON_ONOFF);
  if (currentOnOffState == LOW && lastOnOffState == HIGH) {
    stato = !stato;
    radio.write(&stato, sizeof(stato));
    digitalWrite(LED_PIN, stato ? HIGH : LOW);
    delay(200);
  }
  lastOnOffState = currentOnOffState;

  // Gestione VOCE e trasmissione WiFi UDP
  if (digitalRead(BUTTON_VOICE) == LOW) {
    if (!recording) {
      recording = true;
      sampleIndex = 0;
      digitalWrite(LED_PIN, HIGH);
    }
    if (sampleIndex < MAX_SAMPLES) {
      samples[sampleIndex++] = ads.readADC_SingleEnded(0);
      delayMicroseconds(1000000 / SAMPLE_RATE_HZ);
    }
  } else if (recording) {
    recording = false;
    digitalWrite(LED_PIN, LOW);
    sendAudioDataUDP();
  }
}

