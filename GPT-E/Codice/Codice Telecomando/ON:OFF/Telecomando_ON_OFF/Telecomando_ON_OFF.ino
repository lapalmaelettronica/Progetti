#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

#define CE_PIN 9       // PB1 → pin 15
#define CSN_PIN 8      // PB0 → pin 14
#define BUTTON_PIN 7   // PD7 → pin 13
#define LED_PIN 5      // PD5 → pin 11

RF24 radio(CE_PIN, CSN_PIN);
const byte address[6] = "00001";

bool stato = false;
bool lastButtonState = HIGH;

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);  // Pull-up interna
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  radio.begin();
  radio.setPALevel(RF24_PA_MIN);
  radio.setRetries(5, 15);  // Ritentativi per maggiore affidabilità
  radio.openWritingPipe(address);
  radio.stopListening();    // Modalità TX
}

void loop() {
  bool currentButtonState = digitalRead(BUTTON_PIN);

  if (currentButtonState == LOW && lastButtonState == HIGH) {
    stato = !stato;  // Cambia stato
    radio.write(&stato, sizeof(stato));

    digitalWrite(LED_PIN, stato ? HIGH : LOW);
    delay(200);  // debounce
  }

  lastButtonState = currentButtonState;
}

