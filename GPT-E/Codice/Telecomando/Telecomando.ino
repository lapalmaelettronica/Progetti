#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RF24.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <math.h>

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
// PACCHETTI RADIO
// ==========================================
#define CMD_POWER     10
#define CMD_JOYSTICK  20

struct RadioPacket {
  uint8_t type;
  uint8_t value;
};

struct JoyData {
  uint8_t cmdType = CMD_JOYSTICK;
  int16_t x;
  int16_t y;
};

JoyData joyPacket;

// ==========================================
// VARIABILI DI STATO
// ==========================================
bool robotPower = false;
bool isShuttingDown = false;

int currentScreen = 0;
int driveMode = 0;
int audioMode = 0;

// ==========================================
// PROTOTIPI
// ==========================================
void setLEDColor(int r, int g, int b);
void updateRGBLED();
void safeShutdownSequence();
void waitForPiBoot();
void handleOnOffButton();
void handleMenuButtons();
void updateDisplay();
void sendJoystickData();
void sendPowerCommand(bool state);

// ==========================================
// INVIO COMANDO POWER RADIO
// ==========================================
void sendPowerCommand(bool state) {
  RadioPacket powerCmd;
  powerCmd.type = CMD_POWER;
  powerCmd.value = state ? 1 : 0;

  radio.stopListening();

  int repetitions = state ? 5 : 10;

  for (int i = 0; i < repetitions; i++) {
    bool ok = radio.write(&powerCmd, sizeof(powerCmd));

    Serial.print("Invio POWER ");
    Serial.print(state ? "ON" : "OFF");
    Serial.print(" tentativo ");
    Serial.print(i + 1);
    Serial.print(" -> ");
    Serial.println(ok ? "OK" : "FAIL");

    delay(state ? 20 : 50);
  }
}

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

  setLEDColor(0, 0, 0);

  Wire.begin(I2C_SDA, I2C_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    while (true) {
      setLEDColor(255, 0, 0);
      delay(200);
      setLEDColor(0, 0, 0);
      delay(200);
    }
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  // --- CONNESSIONE WI-FI ---
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

  if (WiFi.status() == WL_CONNECTED) {
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
  display.clearDisplay();
  display.setCursor(0, 15);
  display.println("Init NRF24...");
  display.display();

  if (!radio.begin()) {
    Serial.println("NRF24 Fallito");

    display.clearDisplay();
    display.setCursor(0, 20);
    display.println("NRF24 FALLITO!");
    display.display();

    setLEDColor(255, 0, 0);
    delay(1500);
  } else {
    Serial.println("NRF24 OK");

    radio.openWritingPipe(addressRobot);
    radio.setPALevel(RF24_PA_LOW);
    radio.setDataRate(RF24_1MBPS);
    radio.setChannel(76);
    radio.stopListening();

    display.clearDisplay();
    display.setCursor(0, 20);
    display.println("NRF24 OK");
    display.display();

    delay(800);
  }

  currentScreen = 0;
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
// SCHERMATA DI CARICAMENTO
// ==========================================
void waitForPiBoot() {
  unsigned long startTime = millis();
  bool piReady = false;

  unsigned long timeout = 60000;

  while (millis() - startTime < timeout) {
    unsigned long elapsed = millis() - startTime;

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);

    display.setCursor(10, 10);
    display.println("Avvio GPT-E in corso");

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

    if ((elapsed / 250) % 2 == 0) {
      setLEDColor(255, 100, 0);
    } else {
      setLEDColor(0, 0, 0);
    }

    if (WiFi.status() == WL_CONNECTED) {
      WiFiClient client;

      if (client.connect(pi_ip, 5000)) {
        client.stop();
        piReady = true;
        break;
      }
    } else {
      if (elapsed > 25000) {
        piReady = true;
        break;
      }
    }

    delay(500);
  }

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
  display.setTextSize(1);
  display.setTextColor(WHITE);

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

  // Attesa per permettere al Raspberry di spegnersi
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

  // Comando radio OFF alla scheda Power Management
  sendPowerCommand(false);

  delay(2000);

  robotPower = false;
  isShuttingDown = false;
  currentScreen = 0;
}

// ==========================================
// GESTIONE PULSANTE ON/OFF
// ==========================================
void handleOnOffButton() {
  if (digitalRead(PIN_ON_OFF) == LOW) {
    delay(50);

    if (digitalRead(PIN_ON_OFF) == LOW) {
      if (!robotPower) {
        // ACCENSIONE FISICA
        sendPowerCommand(true);

        robotPower = true;

        // Attesa boot Raspberry
        waitForPiBoot();

        currentScreen = 1;
      } else {
        safeShutdownSequence();
      }

      while (digitalRead(PIN_ON_OFF) == LOW) {
        updateRGBLED();
        delay(10);
      }
    }
  }
}

// ==========================================
// INVIO JOYSTICK
// ==========================================
void sendJoystickData() {
  joyPacket.x = analogRead(PIN_JOY_X);
  joyPacket.y = analogRead(PIN_JOY_Y);

  radio.stopListening();

  bool ok = radio.write(&joyPacket, sizeof(joyPacket));

  Serial.print("Joystick -> ");
  Serial.println(ok ? "OK" : "FAIL");
}

// ==========================================
// GESTIONE MENU
// ==========================================
void handleMenuButtons() {
  if (digitalRead(PIN_SX) == LOW) {
    if (currentScreen == 1) driveMode = 0;
    if (currentScreen == 2) audioMode = 0;

    while (digitalRead(PIN_SX) == LOW) {
      delay(10);
    }
  }

  if (digitalRead(PIN_DX) == LOW) {
    if (currentScreen == 1) driveMode = 1;
    if (currentScreen == 2) audioMode = 1;

    while (digitalRead(PIN_DX) == LOW) {
      delay(10);
    }
  }

  if (digitalRead(PIN_OK) == LOW) {
    if (currentScreen == 1) {
      currentScreen = 2;
    } else if (currentScreen == 2) {
      currentScreen = 3;
    } else if (currentScreen == 3) {
      currentScreen = 1;
    }

    while (digitalRead(PIN_OK) == LOW) {
      delay(10);
    }
  }
}

// ==========================================
// DISEGNO OPZIONI MENU
// ==========================================
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

// ==========================================
// DISPLAY
// ==========================================
void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  display.setCursor(0, 0);
  display.print(robotPower ? " ROBOT: ON" : " ROBOT: OFF");

  float voltage = analogRead(PIN_BATT) * (3.3 / 4095.0) * 2.0;

  display.setCursor(95, 0);
  display.print(voltage, 1);
  display.print("V");

  display.drawLine(0, 10, 128, 10, WHITE);

  switch (currentScreen) {
    case 0:
      display.setCursor(20, 30);
      display.println("Premi [ON/OFF]");
      display.setCursor(20, 45);
      display.println("per iniziare");
      break;

    case 1:
      display.setCursor(15, 15);
      display.println("1/2: SCELTA GUIDA");

      drawOption(5, 30, 118, "GUIDA MANUALE", driveMode == 0);
      drawOption(5, 48, 118, "IA AUTONOMA", driveMode == 1);
      break;

    case 2:
      display.setCursor(15, 15);
      display.println("2/2: SCELTA AUDIO");

      drawOption(5, 30, 118, "MIC SUL ROBOT", audioMode == 0);
      drawOption(5, 48, 118, "PTT TELECOMANDO", audioMode == 1);
      break;

    case 3:
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

// ==========================================
// LED RGB TELECOMANDO
// ==========================================
void setLEDColor(int r, int g, int b) {
  analogWrite(PIN_LED_R, r);
  analogWrite(PIN_LED_G, g);
  analogWrite(PIN_LED_B, b);
}

void updateRGBLED() {
  unsigned long currentMillis = millis();

  if (isShuttingDown) {
    if ((currentMillis / 150) % 2 == 0) {
      setLEDColor(255, 80, 0);
    } else {
      setLEDColor(0, 0, 0);
    }
  } else if (!robotPower) {
    int breath = (sin(currentMillis / 500.0) + 1) * 127.5;
    setLEDColor(0, 0, breath);
  } else {
    if (currentScreen > 0) {
      if (currentScreen < 3) {
        setLEDColor(0, 150, 255);
      } else {
        if (driveMode == 1) {
          setLEDColor(150, 0, 255);
        } else {
          setLEDColor(0, 200, 0);
        }
      }
    }
  }
}