#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RF24.h>
#include <WiFi.h>
#include <HTTPClient.h> 

// ==========================================
// CONFIGURAZIONE PIN (ESP32-S3)
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

#define PIN_LED_R   40
#define PIN_LED_G   41
#define PIN_LED_B   42

// ==========================================
// SETTAGGI RETE GPT-E
// ==========================================
const char* ssid     = "Vodafone-A64688680";
const char* password = "tu3s8utv5ub5tx7w"; 
const char* pi_ip    = "192.168.1.200";    

// ==========================================
// DISPLAY E RADIO
// ==========================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

RF24 radio(NRF_CE, NRF_CSN);
const byte addressRobot[6] = "ROB01"; 

// ==========================================
// VARIABILI DI STATO
// ==========================================
bool robotPower = false;
bool isShuttingDown = false;
int currentScreen = 0; 
int driveMode = 0; 
int audioMode = 0; 

struct JoyData {
  uint8_t cmdType = 1;
  int16_t x;
  int16_t y;
};
JoyData joyPacket;

// ==========================================
// PROTOTIPI FUNZIONI
// ==========================================
void setLEDColor(int r, int g, int b);
void updateRGBLED();
void safeShutdownSequence();

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
    while(true) { setLEDColor(255, 0, 0); delay(200); setLEDColor(0, 0, 0); delay(200); }
  }
  
  // --- CONNESSIONE WI-FI ---
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 10);
  display.println("Connessione Wi-Fi...");
  display.setCursor(0, 25);
  display.println(ssid);
  display.display();

  WiFi.begin(ssid, password);
  int tentativi = 0;
  while (WiFi.status() != WL_CONNECTED && tentativi < 15) {
    delay(500);
    tentativi++;
    Serial.print(".");
  }

  display.clearDisplay();
  display.setCursor(0, 20);
  if(WiFi.status() == WL_CONNECTED) {
    display.println("   WI-FI CONNESSO!");
    setLEDColor(0, 255, 0); 
  } else {
    display.println("  WIFI NON CONNESSO");
    display.setCursor(0, 35);
    display.println("  (Senza Shutdown SW)");
    setLEDColor(255, 0, 0); 
  }
  display.display();
  delay(1500);

  // --- INIZIALIZZAZIONE RADIO ---
  if (!radio.begin()) {
    Serial.println("NRF24 Fallito");
  } else {
    radio.openWritingPipe(addressRobot);
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
  
  if (robotPower && currentScreen == 3 && driveMode == 0 && !isShuttingDown) {
    sendJoystickData();
  }

  updateRGBLED();
  delay(20); 
}

// ==========================================
// SCHERMATA DI CARICAMENTO INTELLIGENTE
// ==========================================
void waitForPiBoot() {
  unsigned long startTime = millis();
  bool piReady = false;
  int timeout = 60000; // Timeout 40 secondi

  while (millis() - startTime < timeout) {
    int elapsed = millis() - startTime;
    
    // 1. Grafica Barra di Caricamento
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(10, 10);
    display.println("Avvio GPT-E in corso");

    // Calcolo progresso barra
    int barWidth = map(elapsed, 0, timeout, 0, 100);
    display.drawRect(14, 30, 100, 12, WHITE);       
    display.fillRect(14, 30, barWidth, 12, WHITE);  

    display.setCursor(10, 50);
    if (WiFi.status() == WL_CONNECTED) {
       display.println("Verifica sistema...");
    } else {
       display.println("Avvio alla cieca...");
    }
    display.display();

    // 2. Animazione LED (Giallo/Arancio pulsante)
    if ((elapsed / 250) % 2 == 0) setLEDColor(255, 100, 0);
    else setLEDColor(0, 0, 0);

    // 3. Ping al server del Raspberry
    if (WiFi.status() == WL_CONNECTED) {
      WiFiClient client;
      if (client.connect(pi_ip, 5000)) {
         client.stop();
         piReady = true;
         break; // Esce dal ciclo, il Pi è pronto!
      }
    } else {
      // Senza Wi-Fi aspetta 25 secondi e sblocca
      if (elapsed > 25000) {
         piReady = true;
         break;
      }
    }
    delay(500); 
  }

  // 4. Schermata finale
  display.clearDisplay();
  display.setCursor(15, 25);
  if (piReady) {
    display.println(" GPT-E OPERATIVO!");
    setLEDColor(0, 255, 0); 
  } else {
    display.println(" TIMEOUT AVVIO!");
    setLEDColor(255, 0, 0); 
  }
  display.display();
  delay(2000);
}

// ==========================================
// LOGICA DI SPEGNIMENTO SICURO
// ==========================================
void safeShutdownSequence() {
  isShuttingDown = true; 
  
  display.clearDisplay();
  display.setCursor(0, 10);
  display.println("1/2: Shutdown SW...");
  display.display();

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "http://" + String(pi_ip) + ":5000/shutdown"; 
    http.begin(url);
    int httpResponseCode = http.GET();
    
    display.setCursor(0, 25);
    if (httpResponseCode > 0) {
      display.println("Inviato al Pi 5 OK!");
    } else {
      display.println("Errore Rete Pi 5");
    }
    display.display();
    http.end();
  } else {
    display.setCursor(0, 25);
    display.println("No WiFi! Attesa...");
    display.display();
  }

  unsigned long startWait = millis();
  while (millis() - startWait < 15000) { 
    updateRGBLED(); 
    delay(100);
  }

  display.clearDisplay();
  display.setCursor(0, 10);
  display.println("2/2: Power OFF HW");
  display.println("Taglio batteria...");
  display.display();

  bool powerCmd = false; 
  radio.stopListening();
  for(int i=0; i<5; i++) { 
    radio.write(&powerCmd, sizeof(powerCmd));
    delay(10);
  }

  delay(2000);
  robotPower = false;
  isShuttingDown = false;
  currentScreen = 0; 
}

// ==========================================
// GESTIONE PULSANTI E INVIO DATI
// ==========================================
void handleOnOffButton() {
  if (digitalRead(PIN_ON_OFF) == LOW) {
    delay(50); 
    if (digitalRead(PIN_ON_OFF) == LOW) {
      if (!robotPower) {
        // 1. ACCENSIONE FISICA 
        bool powerCmd = true;
        radio.stopListening();
        for(int i=0; i<3; i++) { 
          radio.write(&powerCmd, sizeof(powerCmd));
          delay(10);
        }
        
        robotPower = true;
        
        // 2. SCHERMATA DI CARICAMENTO INTELLIGENTE
        waitForPiBoot(); 
        
        // 3. PASSA AL MENU
        currentScreen = 1; 
      } else {
        safeShutdownSequence();
      }
      while(digitalRead(PIN_ON_OFF) == LOW) { updateRGBLED(); delay(10); } 
    }
  }
}

void sendJoystickData() {
  joyPacket.x = analogRead(PIN_JOY_X);
  joyPacket.y = analogRead(PIN_JOY_Y);
  radio.stopListening();
  radio.write(&joyPacket, sizeof(joyPacket));
}

// ==========================================
// FUNZIONI UI E LED
// ==========================================
void handleMenuButtons() {
  if (digitalRead(PIN_SX) == LOW) {
    if (currentScreen == 1) driveMode = 0;
    if (currentScreen == 2) audioMode = 0;
    while(digitalRead(PIN_SX) == LOW) delay(10);
  }
  if (digitalRead(PIN_DX) == LOW) {
    if (currentScreen == 1) driveMode = 1;
    if (currentScreen == 2) audioMode = 1;
    while(digitalRead(PIN_DX) == LOW) delay(10);
  }
  if (digitalRead(PIN_OK) == LOW) {
    if (currentScreen == 1) currentScreen = 2;
    else if (currentScreen == 2) currentScreen = 3;
    else if (currentScreen == 3) currentScreen = 1;
    while(digitalRead(PIN_OK) == LOW) delay(10);
  }
}

void drawOption(int x, int y, int width, const char* text, bool isSelected) {
  if (isSelected) {
    display.fillRoundRect(x, y, width, 14, 3, WHITE);
    display.setTextColor(BLACK, WHITE);
  } else {
    display.drawRoundRect(x, y, width, 14, 3, WHITE);
    display.setTextColor(WHITE, BLACK);
  }
  int textWidth = strlen(text) * 6;
  display.setCursor(x + (width - textWidth) / 2, y + 3);
  display.print(text);
  display.setTextColor(WHITE, BLACK);
}

void updateDisplay() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print(robotPower ? " ROBOT: ON" : " ROBOT: OFF");
  
  float voltage = analogRead(PIN_BATT) * (3.3 / 4095.0) * 2.0; 
  display.setCursor(95, 0);
  display.print(voltage, 1); display.print("V");
  display.drawLine(0, 10, 128, 10, WHITE);

  switch (currentScreen) {
    case 0: 
      display.setCursor(20, 30); display.println("Premi [ON/OFF]");
      display.setCursor(20, 45); display.println("per iniziare");
      break;
    case 1:
      display.setCursor(15, 15); display.println("1/2: SCELTA GUIDA");
      drawOption(5, 30, 118, "GUIDA MANUALE", (driveMode == 0));
      drawOption(5, 48, 118, "IA AUTONOMA", (driveMode == 1));
      break;
    case 2:
      display.setCursor(15, 15); display.println("2/2: SCELTA AUDIO");
      drawOption(5, 30, 118, "MIC SUL ROBOT", (audioMode == 0));
      drawOption(5, 48, 118, "PTT TELECOMANDO", (audioMode == 1));
      break;
    case 3:
      display.setCursor(15, 15); display.println("--- CRUSCOTTO ---");
      display.setCursor(5, 30); display.print("Mod: "); display.println(driveMode == 0 ? "MANUALE" : "AUTONOMA");
      display.setCursor(5, 42); display.print("Aud: "); display.println(audioMode == 0 ? "MIC ROBOT" : "PTT LOCALE");
      display.setCursor(25, 55); display.print("[OK] x Impostazioni");
      break;
  }
  display.display();
}

void setLEDColor(int r, int g, int b) {
  analogWrite(PIN_LED_R, r); analogWrite(PIN_LED_G, g); analogWrite(PIN_LED_B, b);
}

void updateRGBLED() {
  unsigned long currentMillis = millis();
  if (isShuttingDown) {
    if ((currentMillis / 150) % 2 == 0) setLEDColor(255, 80, 0); 
    else setLEDColor(0, 0, 0);
  } else if (!robotPower) {
    int breath = (sin(currentMillis / 500.0) + 1) * 127.5; 
    setLEDColor(0, 0, breath); 
  } else {
    // Se siamo nel caricamento (currentScreen == 0 ma robotPower == true), 
    // l'animazione la gestisce waitForPiBoot(), quindi qui non facciamo niente.
    if (currentScreen > 0) {
      if (currentScreen < 3) setLEDColor(0, 150, 255);
      else setLEDColor(driveMode == 1 ? 150 : 0, driveMode == 0 ? 200 : 0, driveMode == 1 ? 255 : 0);
    }
  }
}