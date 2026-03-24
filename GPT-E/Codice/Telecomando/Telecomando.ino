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
#include <math.h>

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

#define NRF_CE      14 
#define NRF_CSN     10 
#define I2C_SDA     8  
#define I2C_SCL     9  

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
const byte addressRemote[6] = "RMT01";
const byte addressRobot[6]  = "ROB01";

// ==========================================
// VARIABILI DI STATO E LOGICA MENU
// ==========================================
bool robotPower = false;
bool isShuttingDown = false;

// 0: Standby, 1: Config Guida, 2: Config Audio, 3: Cruscotto Operativo
int currentScreen = 0; 

int driveMode = 0; // 0 = Manuale, 1 = IA
int audioMode = 0; // 0 = Mic Robot, 1 = PTT Telecomando

struct JoyData {
  uint8_t cmdType = 1;
  int16_t x;
  int16_t y;
};
JoyData joyPacket;

struct SysCommand {
  uint8_t cmdType = 0;
  uint8_t action;      
};
SysCommand sysPacket;

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  
  pinMode(PIN_ON_OFF, INPUT_PULLUP);
  pinMode(PIN_PTT, INPUT_PULLUP);
  pinMode(PIN_SX, INPUT_PULLUP);
  pinMode(PIN_OK, INPUT_PULLUP);
  pinMode(PIN_DX, INPUT_PULLUP);

  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);

  Wire.begin(I2C_SDA, I2C_SCL);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED fallito"));
    while(true) { setLEDColor(255, 0, 0); delay(200); setLEDColor(0, 0, 0); delay(200); }
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 20);
  display.println("   SISTEMA AVVIATO");
  display.display();
  delay(1500);

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
  
  if (!isShuttingDown) {
    handleMenuButtons();
    updateDisplay();
  }
  
  // Invia comandi joystick solo se siamo nel cruscotto (finito setup) e in manuale
  if (robotPower && currentScreen == 3 && driveMode == 0 && !isShuttingDown) {
    sendJoystickData();
  }

  if (digitalRead(PIN_PTT) == LOW && robotPower) {
    Serial.println("PTT Premuto");
  }

  updateRGBLED();
  delay(20); 
}

// ==========================================
// GESTIONE PULSANTI E LOGICA
// ==========================================
void handleOnOffButton() {
  if (digitalRead(PIN_ON_OFF) == LOW) {
    delay(50); 
    if (digitalRead(PIN_ON_OFF) == LOW) {
      if (!robotPower) {
        // Accensione: Forza l'ingresso nel Setup Wizard
        sysPacket.action = 1; 
        radio.stopListening();
        radio.write(&sysPacket, sizeof(sysPacket));
        robotPower = true;
        currentScreen = 1; // Inizia dalla configurazione guida
      } else {
        safeShutdownSequence();
      }
      while(digitalRead(PIN_ON_OFF) == LOW) { updateRGBLED(); delay(10); } 
    }
  }
}

void handleMenuButtons() {
  // --- TASTO SX (Seleziona opzione 0) ---
  if (digitalRead(PIN_SX) == LOW) {
    if (currentScreen == 1) driveMode = 0; // Manuale
    if (currentScreen == 2) audioMode = 0; // Mic Robot
    while(digitalRead(PIN_SX) == LOW) { updateRGBLED(); delay(10); }
  }
  
  // --- TASTO DX (Seleziona opzione 1) ---
  if (digitalRead(PIN_DX) == LOW) {
    if (currentScreen == 1) driveMode = 1; // Autonoma
    if (currentScreen == 2) audioMode = 1; // PTT Locale
    while(digitalRead(PIN_DX) == LOW) { updateRGBLED(); delay(10); }
  }
  
  // --- TASTO OK (Conferma e vai avanti) ---
  if (digitalRead(PIN_OK) == LOW) {
    if (currentScreen == 0) {
      // Da standby, se preme OK senza accendere il robot
      // fa finta di niente, o potresti forzare l'accensione.
    } 
    else if (currentScreen == 1) {
      currentScreen = 2; // Passa ad Audio
    } 
    else if (currentScreen == 2) {
      currentScreen = 3; // Passa al Cruscotto
    } 
    else if (currentScreen == 3) {
      currentScreen = 1; // Ritorna al setup se vuole ricominciare
    }
    while(digitalRead(PIN_OK) == LOW) { updateRGBLED(); delay(10); }
  }
}

void safeShutdownSequence() {
  isShuttingDown = true; 
  display.clearDisplay();
  display.setCursor(0, 20);
  display.println(" Spegnimento Pi 5...");
  display.display();

  sysPacket.action = 2; 
  radio.stopListening();
  radio.write(&sysPacket, sizeof(sysPacket));

  unsigned long startTime = millis();
  bool feedbackReceived = false;
  radio.startListening();

  while (millis() - startTime < 20000) { 
    updateRGBLED(); 
    if (radio.available()) {
      uint8_t response;
      radio.read(&response, sizeof(response));
      if (response == 0xFF) { 
        feedbackReceived = true;
        break;
      }
    }
    delay(10);
  }

  display.clearDisplay();
  display.setCursor(0, 20);
  if (feedbackReceived) display.println("  PI 5 SPENTO SICURO");
  else display.println("  TIMEOUT! Forza OFF");
  display.display();
  delay(2000);

  sysPacket.action = 3; 
  radio.stopListening();
  radio.write(&sysPacket, sizeof(sysPacket));
  
  robotPower = false;
  isShuttingDown = false;
  currentScreen = 0; 
}

void sendJoystickData() {
  joyPacket.x = analogRead(PIN_JOY_X);
  joyPacket.y = analogRead(PIN_JOY_Y);
  radio.stopListening();
  radio.write(&joyPacket, sizeof(joyPacket));
}

// ==========================================
// FUNZIONI GRAFICHE E UI (RE-DESIGN)
// ==========================================

// Funzione helper per disegnare le opzioni selezionabili
void drawOption(int x, int y, int width, const char* text, bool isSelected) {
  if (isSelected) {
    display.fillRoundRect(x, y, width, 14, 3, WHITE); // Sfondo bianco arrotondato
    display.setTextColor(BLACK, WHITE);               // Testo nero
  } else {
    display.drawRoundRect(x, y, width, 14, 3, WHITE); // Solo contorno bianco
    display.setTextColor(WHITE, BLACK);               // Testo bianco
  }
  
  // Centra il testo nel box in modo approssimativo
  int textWidth = strlen(text) * 6;
  int textX = x + (width - textWidth) / 2;
  display.setCursor(textX, y + 3);
  display.print(text);
  
  display.setTextColor(WHITE, BLACK); // Resetta colore base
}

void updateDisplay() {
  display.clearDisplay();

  // --- TOP BAR FISSA ---
  display.setCursor(0, 0);
  if(robotPower) {
    display.print(" ROBOT: ON");
  } else {
    display.print(" ROBOT: OFF");
  }
  
  // Lettura e stampa batteria in alto a destra
  float voltage = analogRead(PIN_BATT) * (3.3 / 4095.0) * 2.0; 
  display.setCursor(95, 0);
  display.print(voltage, 1); display.print("V");
  
  display.drawLine(0, 10, 128, 10, WHITE);

  // --- CORPO CENTRALE ---
  switch (currentScreen) {
    
    case 0: // STANDBY
      display.setCursor(20, 30);
      display.println("Premi [ON/OFF]");
      display.setCursor(20, 45);
      display.println("per iniziare");
      break;

    case 1: // CONFIGURAZIONE 1: GUIDA
      display.setCursor(15, 15);
      display.println("1/2: SCELTA GUIDA");
      
      // Disegna i due bottoni (X, Y, Larghezza, Testo, Selezionato)
      drawOption(5, 30, 118, "GUIDA MANUALE", (driveMode == 0));
      drawOption(5, 48, 118, "IA AUTONOMA", (driveMode == 1));
      break;

    case 2: // CONFIGURAZIONE 2: AUDIO
      display.setCursor(15, 15);
      display.println("2/2: SCELTA AUDIO");
      
      drawOption(5, 30, 118, "MIC SUL ROBOT", (audioMode == 0));
      drawOption(5, 48, 118, "PTT TELECOMANDO", (audioMode == 1));
      break;

    case 3: // CRUSCOTTO OPERATIVO (DASHBOARD)
      display.setCursor(15, 15);
      display.println("--- CRUSCOTTO ---");
      
      display.setCursor(5, 30);
      display.print("Mod: ");
      display.println(driveMode == 0 ? "MANUALE" : "AUTONOMA");
      
      display.setCursor(5, 42);
      display.print("Aud: ");
      display.println(audioMode == 0 ? "MIC ROBOT" : "PTT LOCALE");
      
      display.setCursor(25, 55);
      display.print("[OK] x Impostazioni");
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
    if ((currentMillis / 150) % 2 == 0) setLEDColor(255, 80, 0); 
    else setLEDColor(0, 0, 0);
  } 
  else if (!robotPower) {
    int breath = (sin(currentMillis / 500.0) + 1) * 127.5; 
    setLEDColor(0, 0, breath); 
  } 
  else {
    if (currentScreen == 1 || currentScreen == 2) {
      // Fase di setup: Azzurro fisso
      setLEDColor(0, 150, 255);
    }
    else if (currentScreen == 3 && driveMode == 1) {
      // IA Autonoma Operativa: Viola Fisso
      setLEDColor(150, 0, 255);
    } else {
      // Manuale Operativo: Verde Fisso
      setLEDColor(0, 200, 0);
    }
  }
}