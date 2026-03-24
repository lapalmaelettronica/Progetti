/* PER ESP32 S3
1. Modifiche Prioritarie (Configurazione)
USB CDC On Boot: Impostare "Enabled". Senza questo, non vedrai mai l'output del Serial Monitor tramite la USB-C.
Upload Mode: Selezionare "Hardware CDC and JTAG".
Port: /dev/cu.usbmodem....

2. Se esp32 s3 nuovo
Il LED lampeggia. Per farlo apparire nell'elenco delle porte di Arduino IDE e poterlo programmare, segui questa sequenza esatta:
  -Mentre la scheda è collegata, tieni premuto il tasto BOOT (GPIO 0).
  -Premi e rilascia il tasto RESET.
  -Rilascia il tasto BOOT.
Guarda il LED: Se ha smesso di lampeggiare ed è fisso o spento, il chip è in attesa.
Vai ora nel menu Tools -> Port. Dovrebbe essere apparsa una nuova porta. Selezionala.
*/

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RF24.h>
#include <math.h> // Serve per la funzione sin() del LED

// ==========================================
// DEFINIZIONE PIN
// ==========================================
#define PIN_ON_OFF  39
#define PIN_PTT     38
#define PIN_SX      35
#define PIN_OK      37
#define PIN_DX      36
#define PIN_JOY_X   1
#define PIN_JOY_Y   2
#define PIN_BATT    4

#define NRF_CE      9
#define NRF_CSN     10
#define I2C_SDA     8
#define I2C_SCL     18

// PIN LED RGB (Catodo Comune)
#define PIN_LED_R   40
#define PIN_LED_G   41
#define PIN_LED_B   42

// ==========================================
// CONFIGURAZIONE DISPLAY OLED
// ==========================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ==========================================
// CONFIGURAZIONE RADIO NRF24L01
// ==========================================
RF24 radio(NRF_CE, NRF_CSN);
const byte addressRemote[6] = "RMT01"; // Indirizzo Telecomando
const byte addressRobot[6]  = "ROB01"; // Indirizzo Robot

// ==========================================
// VARIABILI DI STATO E STRUTTURE DATI
// ==========================================
bool robotPower = false;
bool isShuttingDown = false; // Traccia la fase di spegnimento per il LED

int currentScreen = 0;
const int TOTAL_SCREENS = 4; // 0:Welcome, 1:Guida, 2:Audio, 3:Batteria

int driveMode = 0; // 0 = Manuale, 1 = IA
int audioMode = 0; // 0 = Mic Robot, 1 = Mic Telecomando (PTT)

// Struttura dati per il Joystick
struct JoyData {
  uint8_t cmdType = 1; // 1 = Dati Joystick
  int16_t x;
  int16_t y;
};
JoyData joyPacket;

// Struttura dati per i Comandi di Sistema
struct SysCommand {
  uint8_t cmdType = 0; // 0 = Comando di sistema
  uint8_t action;      // 1=ON, 2=SHUTDOWN_REQ, 3=FORCE_OFF
};
SysCommand sysPacket;

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  
  // Inizializzazione Pin Tasti (l'ESP32-S3 ha pull-up interni su tutti i pin)
  pinMode(PIN_ON_OFF, INPUT_PULLUP);
  pinMode(PIN_PTT, INPUT_PULLUP);
  pinMode(PIN_SX, INPUT_PULLUP);
  pinMode(PIN_OK, INPUT_PULLUP);
  pinMode(PIN_DX, INPUT_PULLUP);

  // Inizializzazione Pin LED RGB
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);

  // Inizializzazione I2C e Display
  Wire.begin(I2C_SDA, I2C_SCL);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED fallito"));
    // Lampeggio rosso di errore critico se manca il display
    while(true) {
      setLEDColor(255, 0, 0); delay(200);
      setLEDColor(0, 0, 0); delay(200);
    }
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 20);
  display.println("  TELECOMANDO ROBOT");
  display.println("     Avvio in corso...");
  display.display();
  delay(1500);

  // Inizializzazione Radio NRF24
  if (!radio.begin()) {
    Serial.println(F("NRF24 fallito"));
  } else {
    radio.openWritingPipe(addressRobot);
    radio.openReadingPipe(1, addressRemote);
    radio.setPALevel(RF24_PA_LOW); 
    radio.stopListening();
  }
}

// ==========================================
// LOOP PRINCIPALE
// ==========================================
void loop() {
  handleOnOffButton();
  
  // Il menu è navigabile solo se non stiamo spegnendo il robot
  if (!isShuttingDown) {
    handleMenuButtons();
    updateDisplay();
  }
  
  // Invia dati joystick solo se acceso, in schermata guida e modalità manuale
  if (robotPower && currentScreen == 1 && driveMode == 0 && !isShuttingDown) {
    sendJoystickData();
  }

  // Gestione PTT (Segnaposto per streaming audio WiFi)
  if (digitalRead(PIN_PTT) == LOW && robotPower) {
    // Logica ESP-NOW / WiFi I2S andrà qui
    Serial.println("PTT Premuto - In attesa di logica Audio");
  }

  updateRGBLED();
  delay(20); // Piccolo delay per stabilità ciclo (50Hz)
}

// ==========================================
// FUNZIONI DI GESTIONE LOGICA
// ==========================================

void handleOnOffButton() {
  if (digitalRead(PIN_ON_OFF) == LOW) {
    delay(50); // Anti-rimbalzo
    if (digitalRead(PIN_ON_OFF) == LOW) {
      
      if (!robotPower) {
        // --- ACCENSIONE ---
        sysPacket.action = 1; // ON
        radio.stopListening();
        radio.write(&sysPacket, sizeof(sysPacket));
        robotPower = true;
        currentScreen = 1; // Salta alla guida
      } else {
        // --- SPEGNIMENTO SICURO ---
        safeShutdownSequence();
      }
      
      // Aspetta che il tasto venga rilasciato per evitare doppi click
      while(digitalRead(PIN_ON_OFF) == LOW) { 
        updateRGBLED(); // Continua ad aggiornare il LED anche se tieni premuto
        delay(10); 
      } 
    }
  }
}

void safeShutdownSequence() {
  isShuttingDown = true; // Attiva il lampeggio arancione sul LED
  
  display.clearDisplay();
  display.setCursor(0, 10);
  display.println("Spegnimento Pi 5...");
  display.display();

  // 1. Invia richiesta di spegnimento
  sysPacket.action = 2; // SHUTDOWN_REQ
  radio.stopListening();
  radio.write(&sysPacket, sizeof(sysPacket));

  // 2. Attesa del Feedback dal Pi (ACK)
  unsigned long startTime = millis();
  bool feedbackReceived = false;
  radio.startListening();

  // Timeout di 20 secondi
  while (millis() - startTime < 20000) { 
    updateRGBLED(); // Mantiene vivo il lampeggio arancione!
    
    if (radio.available()) {
      uint8_t response;
      radio.read(&response, sizeof(response));
      if (response == 0xFF) { // Codice "Spento" dal Pi
        feedbackReceived = true;
        break;
      }
    }
    delay(10);
  }

  display.clearDisplay();
  display.setCursor(0, 20);
  if (feedbackReceived) {
    display.println("PI 5 SPENTO SICURO.");
  } else {
    display.println("TIMEOUT RASPI!");
    display.println("Forzatura OFF...");
  }
  display.display();
  delay(2000);

  // 3. Taglio dell'alimentazione generale
  sysPacket.action = 3; // FORCE_OFF
  radio.stopListening();
  radio.write(&sysPacket, sizeof(sysPacket));
  
  robotPower = false;
  isShuttingDown = false;
  currentScreen = 0; // Torna al benvenuto
}

void handleMenuButtons() {
  // Scorri a Destra
  if (digitalRead(PIN_DX) == LOW) {
    currentScreen = (currentScreen + 1) % TOTAL_SCREENS;
    while(digitalRead(PIN_DX) == LOW) { updateRGBLED(); delay(10); }
  }
  // Scorri a Sinistra
  if (digitalRead(PIN_SX) == LOW) {
    currentScreen = (currentScreen - 1 + TOTAL_SCREENS) % TOTAL_SCREENS;
    while(digitalRead(PIN_SX) == LOW) { updateRGBLED(); delay(10); }
  }
  // Tasto OK (Cambia impostazione)
  if (digitalRead(PIN_OK) == LOW) {
    if (currentScreen == 1) {
      driveMode = !driveMode;
    } else if (currentScreen == 2) {
      audioMode = !audioMode;
    }
    while(digitalRead(PIN_OK) == LOW) { updateRGBLED(); delay(10); }
  }
}

void sendJoystickData() {
  joyPacket.x = analogRead(PIN_JOY_X);
  joyPacket.y = analogRead(PIN_JOY_Y);
  
  radio.stopListening();
  radio.write(&joyPacket, sizeof(joyPacket));
}

// ==========================================
// FUNZIONI GRAFICHE E VISIVE (Display / LED)
// ==========================================

void updateDisplay() {
  display.clearDisplay();
  display.setCursor(0, 0);

  // Intestazione
  if(robotPower) {
    display.println("Stato: CONNESSO (ON)");
  } else {
    display.println("Stato: STANDBY");
  }
  display.drawLine(0, 10, 128, 10, WHITE);

  // Corpo della schermata
  display.setCursor(0, 20);
  switch (currentScreen) {
    case 0: // Welcome
      display.println("   --- PRONTO ---");
      display.println(" Premi ON per avviare");
      break;
      
    case 1: // Guida
      display.println("MODALITA' GUIDA:");
      display.setCursor(0, 40);
      display.setTextSize(2);
      if (driveMode == 0) display.println(" MANUALE");
      else display.println(" AUTONOMA");
      display.setTextSize(1);
      break;
      
    case 2: // Audio
      display.println("MODALITA' AUDIO:");
      display.setCursor(0, 40);
      if (audioMode == 0) display.println("> MIC ROBOT");
      else display.println("> PTT TELECOMANDO");
      break;
      
    case 3: // Batteria
      // Regola questo calcolo in base al tuo partitore di tensione!
      float rawADC = analogRead(PIN_BATT);
      float voltage = rawADC * (3.3 / 4095.0) * 2.0; // Moltiplicatore d'esempio
      
      display.println("STATO BATTERIA:");
      display.setCursor(0, 40);
      display.setTextSize(2);
      display.print(voltage); display.println(" V");
      display.setTextSize(1);
      break;
  }
  display.display();
}

// Imposta i canali PWM del LED
void setLEDColor(int r, int g, int b) {
  analogWrite(PIN_LED_R, r);
  analogWrite(PIN_LED_G, g);
  analogWrite(PIN_LED_B, b);
}

void updateRGBLED() {
  unsigned long currentMillis = millis();

  if (isShuttingDown) {
    // FASE SPEGNIMENTO: Lampeggio Arancione Veloce
    if ((currentMillis / 150) % 2 == 0) {
      setLEDColor(255, 80, 0); // Arancione
    } else {
      setLEDColor(0, 0, 0);
    }
  } 
  else if (!robotPower) {
    // STANDBY: Effetto "Respiro" Blu scuro
    int breath = (sin(currentMillis / 500.0) + 1) * 127.5; 
    setLEDColor(0, 0, breath); 
  } 
  else {
    // ROBOT ACCESO
    if (currentScreen == 1 && driveMode == 1) {
      // IA Autonoma: Viola Fisso
      setLEDColor(150, 0, 255);
    } else {
      // Manuale / Generale: Verde Fisso
      setLEDColor(0, 200, 0);
    }
  }
}