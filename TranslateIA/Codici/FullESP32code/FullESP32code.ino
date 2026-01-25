#include <driver/i2s.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// --- CONFIGURAZIONE PIN ---
#define I2S_MIC_SD  39  
#define I2S_MIC_WS  26  
#define I2S_MIC_SCK 15  
#define RASPI_TX 17 
#define RASPI_RX 16 
#define BTN_SX 18  
#define BTN_DX 19  
#define LED_BAT_VERDE 4
#define RGB_PIN_R 27
#define RGB_PIN_G 14
#define RGB_PIN_B 13

// --- OLED ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- AUDIO ---
#define SAMPLE_RATE 16000 
#define VOLUME_BOOST 15 
#define MIC_THRESHOLD 2000 
#define TAIL_TIME 1500 
#define BUFFER_LEN 512
int16_t sBuffer[BUFFER_LEN * 2]; 

// --- LINGUE ---
// Modificare qui per aggiungere lingue al menu
const char* langNames[] = {"ITALIANO", "ENGLISH", "ESPANOL", "FRANCAIS", "DEUTSCH"};
const char* langCodes[] = {"it",       "en",      "es",      "fr",       "de"};
int langCount = 5;

int idxSrc = 0; // Indice Lingua Sorgente
int idxTgt = 1; // Indice Lingua Target

// --- STATI E VARIABILI ---
enum State { MENU_SRC, MENU_TGT, LISTENING };
State currentState = MENU_SRC;

unsigned long lastButtonPress = 0;     
unsigned long lastLoudSoundTime = 0;   
unsigned long lastDebounceTime = 0;
const int SELECTION_DELAY = 4000; 
const unsigned long debounceDelay = 250; 

// Logica LED
bool hasSpokenRecently = false;     
bool waitingForTranslation = false; 
unsigned long waitingStartTime = 0; 

void setRGB(int r, int g, int b) {
  ledcWrite(RGB_PIN_R, r); ledcWrite(RGB_PIN_G, g); ledcWrite(RGB_PIN_B, b);
}

// Funzione Grafica Display
void drawListeningScreen(int volume, int mode) {
  display.clearDisplay();
  display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  // Header "IT -> EN"
  display.print(langCodes[idxSrc]); display.print(" -> "); display.print(langCodes[idxTgt]);

  display.setCursor(0, 20); display.setTextSize(2);
  
  if (mode == 2) { // Elaborazione (Giallo)
     display.print("PENSO..."); 
     display.fillRect(0, 50, 60, 6, SSD1306_WHITE);
  } else if (mode == 1) { // Ascolto (Verde)
     display.print("ASCOLTO"); 
     int barLen = map(volume, 0, 10000, 0, 128);
     if (barLen > 128) barLen = 128;
     display.fillRect(0, 50, barLen, 8, SSD1306_WHITE);
  } else { // Standby
     display.print("PARLA..."); 
     display.setTextSize(1); display.setCursor(0, 50); display.print("Pronto.");
  }
  display.display();
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(921600, SERIAL_8N1, RASPI_RX, RASPI_TX);
  
  pinMode(BTN_SX, INPUT_PULLUP); pinMode(BTN_DX, INPUT_PULLUP);
  pinMode(LED_BAT_VERDE, OUTPUT); digitalWrite(LED_BAT_VERDE, HIGH);
  
  ledcAttach(RGB_PIN_R, 5000, 8); ledcAttach(RGB_PIN_G, 5000, 8); ledcAttach(RGB_PIN_B, 5000, 8);
  setRGB(255, 255, 0); 

  Wire.begin(21, 22);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) for(;;);
  display.clearDisplay(); display.display();
  
  // Configurazione I2S
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4, .dma_buf_len = 512,
    .use_apll = false, .tx_desc_auto_clear = false
  };
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_MIC_SCK, .ws_io_num = I2S_MIC_WS,
    .data_out_num = -1, .data_in_num = I2S_MIC_SD
  };
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  i2s_start(I2S_NUM_0);
  lastButtonPress = millis();
}

void loop() {
  bool btnAction = false;
  
  // --- GESTIONE PULSANTI (Anti-rimbalzo) ---
  if (currentState != LISTENING && (millis() - lastDebounceTime) > debounceDelay) {
    if (digitalRead(BTN_SX) == LOW) {
      if (currentState == MENU_SRC) { idxSrc--; if(idxSrc<0) idxSrc=langCount-1; }
      else if (currentState == MENU_TGT) { idxTgt--; if(idxTgt<0) idxTgt=langCount-1; }
      btnAction = true; lastDebounceTime = millis();
    }
    if (digitalRead(BTN_DX) == LOW) {
      if (currentState == MENU_SRC) { idxSrc++; if(idxSrc>=langCount) idxSrc=0; }
      else if (currentState == MENU_TGT) { idxTgt++; if(idxTgt>=langCount) idxTgt=0; }
      btnAction = true; lastDebounceTime = millis();
    }
  }
  if (btnAction) lastButtonPress = millis();

  // --- LETTURA AUDIO ---
  size_t bytes_read = 0;
  // Lettura non bloccante nei menu, bloccante in ascolto
  if (currentState == LISTENING) i2s_read(I2S_NUM_0, (void*)sBuffer, sizeof(sBuffer), &bytes_read, portMAX_DELAY);
  else i2s_read(I2S_NUM_0, (void*)sBuffer, sizeof(sBuffer), &bytes_read, 0); 
  
  int maxVol = 0;
  if (bytes_read > 0) {
    for (int i=0; i < bytes_read/2; i++) {
        sBuffer[i] = sBuffer[i] * VOLUME_BOOST; 
        int sample = abs(sBuffer[i]);
        if (sample > maxVol) maxVol = sample;
    }
  }

  // --- MACCHINA A STATI ---
  
  // FASE 1: Menu Input (IO PARLO)
  if (currentState == MENU_SRC) {
      long elapsed = millis() - lastButtonPress;
      display.clearDisplay();
      display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
      display.setCursor(0,0); display.print("IO PARLO:");
      
      display.setTextSize(2); display.setCursor(10, 25); display.print(langNames[idxSrc]);

      if ((SELECTION_DELAY - elapsed) > 0) {
         int bar = map(elapsed, 0, SELECTION_DELAY, 0, 128);
         display.fillRect(0, 55, bar, 4, SSD1306_WHITE);
      } else {
         currentState = MENU_TGT; lastButtonPress = millis();
         display.clearDisplay(); display.display(); delay(200);
      }
      display.display();
  }
  // FASE 2: Menu Output (TRADUCI IN)
  else if (currentState == MENU_TGT) {
      long elapsed = millis() - lastButtonPress;
      display.clearDisplay();
      display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
      display.setCursor(0,0); display.print("TRADUCI IN:");
      
      display.setTextSize(2); display.setCursor(10, 25); display.print(langNames[idxTgt]);
      
      // Feedback Mic
      display.fillRect(0, 50, map(maxVol,0,10000,0,50), 2, SSD1306_WHITE); 

      if ((SELECTION_DELAY - elapsed) > 0) {
         int bar = map(elapsed, 0, SELECTION_DELAY, 0, 128);
         display.fillRect(0, 55, bar, 4, SSD1306_WHITE);
      } else {
         // INVIO COMANDO A RASPBERRY
         Serial2.print("CONF:"); Serial2.print(langCodes[idxSrc]); Serial2.print(">"); Serial2.println(langCodes[idxTgt]);
         
         display.clearDisplay(); display.setTextSize(2); display.setCursor(10, 20); display.println("SETUP OK!"); display.display(); delay(1000);
         drawListeningScreen(0,0); currentState = LISTENING;
         setRGB(0,0,255); delay(500); setRGB(0,0,0);
      }
      display.display();
  }
  // FASE 3: Ascolto e Traduzione
  else if (currentState == LISTENING) {
     if (waitingForTranslation) {
        // STATO: ATTESA TRADUZIONE (Giallo)
        setRGB(255, 255, 0); 
        static unsigned long tUpd = 0;
        if (millis() - tUpd > 500) { drawListeningScreen(0, 2); tUpd = millis(); }
        // Timeout 15s
        if (millis() - waitingStartTime > 15000) { 
           waitingForTranslation = false; hasSpokenRecently = false;
           setRGB(255,0,0); delay(500); drawListeningScreen(0,0);
        }
     } else {
        // STATO: RILEVAMENTO VOCALE (VAD)
        bool sendData = false;
        unsigned long tSilent = millis() - lastLoudSoundTime;

        if (maxVol > MIC_THRESHOLD) {
           lastLoudSoundTime = millis(); sendData = true; hasSpokenRecently = true;
           setRGB(0, 255, 0); // VERDE
        } else {
           if (tSilent < TAIL_TIME) { sendData = true; setRGB(0, 255, 0); }
           else {
              sendData = false;
              if (hasSpokenRecently) {
                 waitingForTranslation = true; waitingStartTime = millis();
              } else setRGB(0, 0, 0);
           }
        }

        if (sendData) {
           Serial2.write((uint8_t*)sBuffer, bytes_read);
           static unsigned long tDraw = 0;
           if (millis() - tDraw > 100) { drawListeningScreen(maxVol, 1); tDraw = millis(); }
        } else if (!waitingForTranslation) {
           static unsigned long tDraw = 0;
           if (millis() - tDraw > 500) { drawListeningScreen(0, 0); tDraw = millis(); }
        }
     }

     // STATO: RICEZIONE TESTO
     if (Serial2.available()) {
        String msg = Serial2.readStringUntil('\n');
        if (msg.startsWith("TXT:")) {
           waitingForTranslation = false; hasSpokenRecently = false;
           String testo = msg.substring(4);
           
           display.clearDisplay();
           display.setCursor(0,0); display.setTextSize(1); display.print(langCodes[idxSrc]); 
           display.print(">"); display.print(langCodes[idxTgt]); display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
           
           display.setCursor(0, 20); display.setTextSize(1); display.println(testo); display.display();
           
           setRGB(0,0,255); delay(4000); setRGB(0,0,0); // BLU per 4 secondi
           drawListeningScreen(0,0);
        }
     }
  }
}