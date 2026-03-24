#include <driver/i2s.h>
#include <driver/adc.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ==========================================
// 🛠️ CONFIGURAZIONE HARDWARE (Ultima versione funzionante)
// ==========================================

// --- AUDIO ---
#define I2S_MIC_SD  39  
#define I2S_MIC_WS  26  
#define I2S_MIC_SCK 15  

// --- RASPBERRY ---
#define RASPI_TX 17 
#define RASPI_RX 16 

// --- PULSANTI ---
#define BTN_SX 18  
#define BTN_DX 19  
#define BTN_OK 13  // Tasto OK su Pin 13

// --- LED RGB (Blu su D2) ---
#define RGB_PIN_R 27  
#define RGB_PIN_G 14  
#define RGB_PIN_B 2   

// --- BATTERIA ---
#define PIN_VSENSE  35  
#define BAT_MAX_ADC 2600 
#define BAT_MIN_ADC 1850 

// --- OLED ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

bool displayPresente = false;

// --- AUDIO SETTINGS ---
#define SAMPLE_RATE 16000 
#define VOLUME_BOOST 10     // Volume ottimizzato per non distorcere
#define MIC_THRESHOLD 2000 
#define TAIL_TIME 1500 
#define BUFFER_LEN 512
int16_t sBuffer[BUFFER_LEN * 2]; 

// --- LINGUE ---
const char* langNames[] = {
  "ITALIANO", "ENGLISH", "ESPANOL", "FRANCAIS", "DEUTSCH", 
  "POLSKI",   "PORTUGUES","ARABIC",   "CHINESE"
};
const char* langCodes[] = {
  "it", "en", "es", "fr", "de", "pl", "pt", "ar", "zh-CN"
};
int langCount = 9;

int idxSrc = 0; 
int idxTgt = 1; 

// --- STATI ---
enum State { MENU_SRC, MENU_TGT, LISTENING };
State currentState = MENU_SRC;

// Variabili Logica
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 250; 
unsigned long okPressTime = 0;
bool isOkPressed = false;
bool longPressDone = false;

unsigned long lastLoudSoundTime = 0;   
bool hasSpokenRecently = false;     
bool waitingForTranslation = false; 
unsigned long waitingStartTime = 0; 
unsigned long lastBatteryCheck = 0;

// ==========================================
// 🎨 GESTIONE RGB
// ==========================================
void setRGB(bool r, bool g, bool b) {
  digitalWrite(RGB_PIN_R, r ? HIGH : LOW);
  digitalWrite(RGB_PIN_G, g ? HIGH : LOW);
  digitalWrite(RGB_PIN_B, b ? HIGH : LOW);
}

// ==========================================
// 🔋 GESTIONE BATTERIA
// ==========================================
void aggiornaBatteria() {
  if (millis() - lastBatteryCheck < 5000) return;
  lastBatteryCheck = millis();

  long sum = 0;
  for(int i=0; i<5; i++) { 
    sum += adc1_get_raw(ADC1_CHANNEL_7); 
    delay(2); 
  }
  int adcVal = sum / 5;
  int perc = map(adcVal, BAT_MIN_ADC, BAT_MAX_ADC, 0, 100);
  perc = constrain(perc, 0, 100);

  if (perc > 60) setRGB(0, 1, 0); // Verde
  else if (perc > 20) setRGB(1, 1, 0); // Giallo
  else setRGB(1, 0, 0); // Rosso
}

// ==========================================
// 🖥️ GRAFICA SICURA
// ==========================================
void safeDisplayUpdate() {
  if (displayPresente) display.display();
}

void drawListeningScreen(int volume, int mode) {
  if (!displayPresente) { 
    if(mode == 0) aggiornaBatteria(); 
    return; 
  } 

  display.clearDisplay();
  display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(langCodes[idxSrc]); display.print(" -> "); display.print(langCodes[idxTgt]);

  display.setCursor(0, 20); display.setTextSize(2);
  
  if (mode == 2) { // PENSO
     display.print("PENSO..."); 
     if ((millis()/200)%2==0) setRGB(0,0,1); else setRGB(0,0,0);
  } else if (mode == 1) { // ASCOLTO
     display.print("ASCOLTO"); 
     int bar = map(volume, 0, 10000, 0, 128);
     display.fillRect(0, 50, min(bar, 128), 8, SSD1306_WHITE);
     setRGB(0, 1, 0); 
  } else { // STANDBY
     display.print("PARLA..."); 
     display.setTextSize(1); display.setCursor(0, 50); display.print("Pronto.");
     aggiornaBatteria(); 
  }
  safeDisplayUpdate();
}

// ==========================================
// 🚀 SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  Serial2.begin(921600, SERIAL_8N1, RASPI_RX, RASPI_TX);
  
  pinMode(BTN_SX, INPUT_PULLUP); 
  pinMode(BTN_DX, INPUT_PULLUP);
  pinMode(BTN_OK, INPUT_PULLUP); 

  pinMode(RGB_PIN_R, OUTPUT); 
  pinMode(RGB_PIN_G, OUTPUT); 
  pinMode(RGB_PIN_B, OUTPUT); 
  
  setRGB(1,0,0); delay(200); setRGB(0,1,0); delay(200); setRGB(0,0,1); delay(200); setRGB(0,0,0);

  // ADC Hardware Fix
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_7, ADC_ATTEN_DB_11);

  Wire.begin(21, 22);
  if(display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) displayPresente = true;
  else if(display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) displayPresente = true;
  else displayPresente = false;
  
  if(displayPresente) {
    display.clearDisplay(); 
    display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
    display.setCursor(10,10); display.println("AVVIO...");
    safeDisplayUpdate();
  }
  
  // I2S (MONO)
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, // MONO
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false
  };
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_MIC_SCK, .ws_io_num = I2S_MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE, .data_in_num = I2S_MIC_SD
  };
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  i2s_start(I2S_NUM_0);
  
  aggiornaBatteria();
}

// ==========================================
// 🔄 LOOP
// ==========================================
void loop() {
  
  // TASTO OK
  bool currentOk = (digitalRead(BTN_OK) == LOW);
  if (currentOk && !isOkPressed) { isOkPressed = true; okPressTime = millis(); longPressDone = false; }
  
  if (currentOk && isOkPressed && !longPressDone) {
    if (millis() - okPressTime > 3000) { 
      currentState = MENU_SRC; longPressDone = true;
      if(displayPresente) {
        display.clearDisplay(); display.setTextSize(2); display.setCursor(20,20);
        display.print("RESET!"); safeDisplayUpdate(); 
      }
      setRGB(0,0,1); delay(1000); 
    }
  }
  
  if (!currentOk && isOkPressed) { 
    if (!longPressDone && (millis() - lastDebounceTime > debounceDelay)) {
       if (currentState == MENU_SRC) currentState = MENU_TGT;
       else if (currentState == MENU_TGT) {
          Serial2.print("CONF:"); Serial2.print(langCodes[idxSrc]); 
          Serial2.print(">"); Serial2.println(langCodes[idxTgt]);
          currentState = LISTENING;
          drawListeningScreen(0,0);
       }
       lastDebounceTime = millis();
    }
    isOkPressed = false;
  }

  // FRECCE
  if (currentState != LISTENING && (millis() - lastDebounceTime > debounceDelay)) {
    if (digitalRead(BTN_SX) == LOW) {
      if (currentState == MENU_SRC) { idxSrc--; if(idxSrc<0) idxSrc=langCount-1; }
      else if (currentState == MENU_TGT) { idxTgt--; if(idxTgt<0) idxTgt=langCount-1; }
      lastDebounceTime = millis();
    }
    if (digitalRead(BTN_DX) == LOW) {
      if (currentState == MENU_SRC) { idxSrc++; if(idxSrc>=langCount) idxSrc=0; }
      else if (currentState == MENU_TGT) { idxTgt++; if(idxTgt>=langCount) idxTgt=0; }
      lastDebounceTime = millis();
    }
  }

  // MACCHINA A STATI
  if (currentState == MENU_SRC) {
      if(displayPresente) {
        display.clearDisplay();
        display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
        display.setCursor(0,0); display.print("IO PARLO:");
        display.setTextSize(2); display.setCursor(0, 25); display.print(langNames[idxSrc]);
        display.setTextSize(1); display.setCursor(0, 55); display.print("Tieni OK x Reset");
        safeDisplayUpdate();
      }
      aggiornaBatteria();
  }
  else if (currentState == MENU_TGT) {
      if(displayPresente) {
        display.clearDisplay();
        display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
        display.setCursor(0,0); display.print("TRADUCI IN:");
        display.setTextSize(2); display.setCursor(0, 25); display.print(langNames[idxTgt]);
        display.setTextSize(1); display.setCursor(0, 55); display.print("Premi OK...");
        safeDisplayUpdate();
      }
      aggiornaBatteria();
  }
  else if (currentState == LISTENING) {
     size_t bytes_read = 0;
     i2s_read(I2S_NUM_0, (void*)sBuffer, sizeof(sBuffer), &bytes_read, 0);
     
     int maxVol = 0;
     if (bytes_read > 0) {
       for (int i=0; i < bytes_read/2; i++) {
           sBuffer[i] *= VOLUME_BOOST; 
           int sample = abs(sBuffer[i]);
           if (sample > maxVol) maxVol = sample;
       }
     }
     
     // 🔥 MODIFICA ANTI-RUMORE TASTO 🔥
     // Se il tasto OK è premuto, azzeriamo il volume così ignora il click
     if (isOkPressed) {
        maxVol = 0;
     }
     // --------------------------------

     if (waitingForTranslation) {
        if (millis() - waitingStartTime > 15000) { 
           waitingForTranslation = false; hasSpokenRecently = false;
           drawListeningScreen(0,0);
        } else {
           static unsigned long tUpd = 0;
           if (millis() - tUpd > 200) { drawListeningScreen(0, 2); tUpd = millis(); }
        }
     } else {
        bool sendData = false;
        if (maxVol > MIC_THRESHOLD) {
           lastLoudSoundTime = millis(); sendData = true; hasSpokenRecently = true;
        } else {
           if (millis() - lastLoudSoundTime < TAIL_TIME) sendData = true;
           else if (hasSpokenRecently) {
              sendData = false;
              waitingForTranslation = true; waitingStartTime = millis();
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

     if (Serial2.available()) {
        String msg = Serial2.readStringUntil('\n');
        if (msg.startsWith("TXT:")) {
           waitingForTranslation = false; hasSpokenRecently = false;
           String testo = msg.substring(4);
           
           if(displayPresente) {
             display.clearDisplay();
             display.setCursor(0,0); display.setTextSize(1); display.print(langCodes[idxSrc]); 
             display.print(">"); display.print(langCodes[idxTgt]); display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
             display.setCursor(0, 20); display.setTextSize(1); display.println(testo); 
             safeDisplayUpdate();
           }
           
           setRGB(0,0,1); delay(300); setRGB(0,0,0); delay(100);
           setRGB(0,0,1); delay(300); setRGB(0,0,0); 
           
           delay(4000); 
           drawListeningScreen(0,0);
        }
     }
  }
}
