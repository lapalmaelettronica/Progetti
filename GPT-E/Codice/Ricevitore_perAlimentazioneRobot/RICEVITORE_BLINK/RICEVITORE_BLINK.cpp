#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

#define CE_PIN 7
#define CSN_PIN 8
#define ENABLE 10


RF24 radio(CE_PIN, CSN_PIN);
const byte address[6] = "00001";
char message[4];


void setup() {
  Serial.begin(115200);
  pinMode(ENABLE, OUTPUT);
  radio.begin();
  radio.openReadingPipe(0, address);
  radio.setPALevel(RF24_PA_MIN);
  radio.startListening();
}
int stato;
void loop() {

  if (radio.available()) {
    radio.read(&stato, sizeof(stato));
    if (stato == 1) {
      digitalWrite(ENABLE, HIGH);  //robot acceso
      Serial.println("ON");
    } else {
      digitalWrite(ENABLE, LOW);  //robot spento
      Serial.println("OFF");
    }
  }
}
