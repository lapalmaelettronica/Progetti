#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

// ==========================================
// MAPPATURA PIN CORRETTA (MiniCore ATmega8)
// ==========================================
#define ENABLE_PIN 8    // PB0
#define CE_PIN     9    // PB1
#define CSN_PIN    10   // PB2
#define BUZZER     7    // PD7 (Cicalino)

RF24 radio(CE_PIN, CSN_PIN);

// L'indirizzo deve coincidere con quello del telecomando ESP32-S3
const byte address[6] = "ROB01"; 

// --- FEEDBACK ACUSTICO ---
void beep(int duration, int repetitions) {
  for(int i = 0; i < repetitions; i++) {
    digitalWrite(BUZZER, HIGH);
    delay(duration);
    digitalWrite(BUZZER, LOW);
    if(i < repetitions - 1) delay(duration);
  }
}

void setup() {
  pinMode(ENABLE_PIN, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  
  // Sicurezza all'avvio: tieni spento il MOSFET
  digitalWrite(ENABLE_PIN, LOW);

  // Inizializzazione Radio
  radio.begin();
  radio.setPALevel(RF24_PA_MIN);
  radio.openReadingPipe(0, address);
  radio.startListening();

  // Bip di sistema pronto (se lo senti, l'ATmega8 è vivo e ha superato il setup)
  beep(100, 1);
}

void loop() {
  if (radio.available()) {
    bool stato = false;
    radio.read(&stato, sizeof(stato));
    
    // Logica: se riceve 'true' accende, se riceve 'false' spegne
    if (stato == true) {
      digitalWrite(ENABLE_PIN, HIGH);
      beep(150, 2); // Doppio bip = Robot Acceso
    } else {
      digitalWrite(ENABLE_PIN, LOW);
      beep(600, 1); // Bip lungo = Robot Spento
    }
  }
}