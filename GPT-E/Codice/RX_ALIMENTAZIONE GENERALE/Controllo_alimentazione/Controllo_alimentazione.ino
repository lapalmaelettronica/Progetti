#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

// ==========================================
// POWER MANAGEMENT GPT-E - ATmega8 + NRF24
// Ricevitore semplice, senza sleep/IRQ
// ==========================================

#define ENABLE_PIN 8    // PB0
#define CE_PIN     9    // PB1
#define CSN_PIN    10   // PB2
#define BUZZER     7    // PD7

RF24 radio(CE_PIN, CSN_PIN);

const byte address[6] = "ROB01";

#define CMD_POWER 10

struct RadioPacket {
  uint8_t type;
  uint8_t value;
};

bool robotPower = false;

void beep(unsigned int duration, uint8_t repetitions) {
  for (uint8_t i = 0; i < repetitions; i++) {
    digitalWrite(BUZZER, HIGH);
    delay(duration);
    digitalWrite(BUZZER, LOW);

    if (i < repetitions - 1) {
      delay(duration);
    }
  }
}

void setPower(bool state) {
  robotPower = state;

  if (state) {
    digitalWrite(ENABLE_PIN, HIGH);   // Q1 ON -> P-MOS ON
    beep(150, 2);                     // doppio bip = ON
  } else {
    digitalWrite(ENABLE_PIN, LOW);    // Q1 OFF -> P-MOS OFF
    beep(600, 1);                     // bip lungo = OFF
  }
}

void setup() {
  pinMode(ENABLE_PIN, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  digitalWrite(ENABLE_PIN, LOW);
  digitalWrite(BUZZER, LOW);

  delay(200);

  if (!radio.begin()) {
    while (true) {
      beep(80, 5);   // errore NRF
      delay(1000);
    }
  }

  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_1MBPS);
  radio.setChannel(76);

  radio.openReadingPipe(0, address);
  radio.startListening();

  beep(100, 1); // sistema pronto
}

void loop() {
  if (radio.available()) {
    RadioPacket packet;

    radio.read(&packet, sizeof(packet));

    if (packet.type == CMD_POWER) {
      if (packet.value == 1 && !robotPower) {
        setPower(true);
      }
      else if (packet.value == 0 && robotPower) {
        setPower(false);
      }
      else {
        // comando valido ma stato già uguale
        beep(40, 1);
      }
    }
  }
}
