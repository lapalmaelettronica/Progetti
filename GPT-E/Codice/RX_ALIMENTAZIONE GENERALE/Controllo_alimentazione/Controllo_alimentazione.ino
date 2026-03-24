#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

#define CE_PIN 9        // PB1 → pin 15
#define CSN_PIN 8       // PB0 → pin 14
#define ENABLE_PIN 10   // PB2 → pin 16

RF24 radio(CE_PIN, CSN_PIN);
const byte address[6] = "00001";
bool stato = false;

void setup() {
  pinMode(ENABLE_PIN, OUTPUT);
  digitalWrite(ENABLE_PIN, LOW);

  radio.begin();
  radio.setPALevel(RF24_PA_MIN);
  radio.openReadingPipe(0, address);
  radio.startListening();
}

void loop() {
  if (radio.available()) {
    radio.read(&stato, sizeof(stato));
    digitalWrite(ENABLE_PIN, stato ? HIGH : LOW);
  }
}
