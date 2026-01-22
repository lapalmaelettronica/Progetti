#include "BluetoothA2DPSource.h"
#include <driver/i2s.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ================= CONFIGURAZIONE PIN (HARDWARE) =================

// --- 🎧 BLUETOOTH ---
char *BT_HEADPHONES_NAME = "LeMieCuffie"; // <--- ⚠️ SCRIVI QUI IL NOME DELLE CUFFIE

// --- 🎤 MICROFONO (INMP441) ---
// Configurazione basata sul tuo schema
#define I2S_MIC_SD  39  // Pin VN (Input Only)
#define I2S_MIC_WS  26  // Pin D26
#define I2S_MIC_SCK 15  // Pin D15

// --- 📡 COMUNICAZIONE RASPBERRY (UART2) ---
// Usa questi per collegare il Pi Zero, NON usare TX0/RX0
#define RASPI_TX 17 // Collega a RXD del Raspberry (GPIO 15)
#define RASPI_RX 16 // Collega a TXD del Raspberry (GPIO 14)

// --- 🖥️ DISPLAY OLED (I2C) ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_SDA 21
#define OLED_SCL 22
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- 🕹️ PULSANTI ---
#define BTN_SX 18  // Scorrimento sinistra
#define BTN_DX 19  // Scorrimento destra

// --- 🔋 BATTERIA (Semaforo) ---
#define LED_BAT_ROSSO  23
#define LED_BAT_GIALLO 5
#define LED_BAT_VERDE  4
#define BAT_SENSE_PIN  35 // Pin Vsense (lettura analogica)

// --- 🌈 LED RGB (Stato Sistema) ---
#define RGB_PIN_R 27
#define RGB_PIN_G 14
#define RGB_PIN_B 13

// ================= VARIABILI GLOBALI =================

// Stati del sistema
enum TranslatorState {
  STATE_MENU,      // Utente sceglie la lingua
  STATE_LISTENING, // Ascolto Mic attivo (invia a BT e Raspi)
  STATE_THINKING,  // Attesa traduzione dal Raspi
  STATE_SPEAKING   // Riproduzione audio tradotto
};

TranslatorState currentState = STATE_MENU;

// Lista Lingue
const char* languages[] = {"Italiano", "English", "Espanol", "Français", "Polska", "Deutsch"};
int langCount = 6;
int currentLangIndex = 0;

// Timer selezione
unsigned long lastButtonPress = 0;
bool langSelected = false;
const int SELECTION_DELAY = 4000; // 4 secondi per confermare

// Audio Bluetooth
BluetoothA2DPSource a2dp_source;
#define VOLUME_BOOST 20 // Moltiplicatore volume software (regola se distorce)

// ================= FUNZIONI AUSILIARIE =================

// Gestione Colore RGB (Compatibile ESP32 Core v3.0+)
void setRGB(int r, int g, int b) {
  // Scrittura diretta PWM sui pin
  ledcWrite(RGB_PIN_R, r);
  ledcWrite(RGB_PIN_G, g);
  ledcWrite(RGB_PIN_B, b);
}

// Lettura Batteria e LED Semaforo
void updateBatteryStatus() {
  int raw = analogRead(BAT_SENSE_PIN);
  // Conversione grezza: 4095 = 3.3V al pin. 
  // Moltiplichiamo x2 assumendo un partitore di tensione 100k/100k per leggere la LiPo (4.2V max)
  float voltage = (raw / 4095.0) * 3.3 * 2.0; 

  // Reset LED
  digitalWrite(LED_BAT_ROSSO, LOW);
  digitalWrite(LED_BAT_GIALLO, LOW);
  digitalWrite(LED_BAT_VERDE, LOW);

  // Soglie (modificabili in base alla tua batteria)
  if (voltage > 3.7) {
    digitalWrite(LED_BAT_VERDE, HIGH);
  } else if (voltage > 3.4) {
    digitalWrite(LED_BAT_GIALLO, HIGH);
  } else {
    digitalWrite(LED_BAT_ROSSO, HIGH); // Batteria scarica
  }
}

// Disegno Interfaccia Grafica
void drawUI() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  
  // -- Barra Superiore --
  display.setTextSize(1);
  display.setCursor(0,0);
  if (a2dp_source.is_connected()) display.print("BT:OK");
  else display.print("BT:--");
  
  display.setCursor(80,0);
  switch(currentState) {
    case STATE_MENU: display.print(" MENU"); break;
    case STATE_LISTENING: display.print(" MIC ON"); break;
    case STATE_THINKING: display.print(" ELAB.."); break;
    case STATE_SPEAKING: display.print(" PARLA"); break;
  }

  // -- Area Centrale (Lingua o Stato) --
  display.setTextSize(2);
  
  if (currentState == STATE_MENU) {
    // Centra il testo della lingua
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(languages[currentLangIndex], 0, 0, &x1, &y1, &w, &h);
    display.setCursor((SCREEN_WIDTH - w) / 2, 25);
    display.print(languages[currentLangIndex]);

    // Barra di progresso selezione
    if (!langSelected) {
      long elapsed = millis() - lastButtonPress;
      if (elapsed < SELECTION_DELAY) {
        int barWidth = map(elapsed, 0, SELECTION_DELAY, 0, SCREEN_WIDTH);
        display.fillRect(0, 55, barWidth, 4, SSD1306_WHITE);
      }
    } else {
       display.setTextSize(1);
       display.setCursor(20, 55);
       display.print("PREMI PER MENU");
    }
  } 
  else if (currentState == STATE_LISTENING) {
    display.setCursor(20, 30);
    display.print("Ascolto...");
  }

  display.display();
}

// Callback Audio: Microfono -> Bluetooth
int32_t get_data_frames(Frame *frame, int32_t frame_count) {
    // Se non siamo in ascolto, possiamo silenziare o continuare a sentire l'ambiente
    // Per ora lasciamo l'audio passante ("Transparency Mode")
    
    size_t bytes_read;
    i2s_read(I2S_NUM_0, (void*)frame, frame_count * 4, &bytes_read, portMAX_DELAY);
    
    // --- BOOST SOFTWARE DEL VOLUME ---
    int16_t *samples = (int16_t*)frame;
    for (int i = 0; i < frame_count * 2; i++) {
        // Applichiamo il gain
        int32_t val = samples[i] * VOLUME_BOOST;
        
        // Clipping (taglio) per evitare overflow e rumori orribili
        if (val > 32767) val = 32767;
        else if (val < -32768) val = -32768;
        
        samples[i] = (int16_t)val;
    }
    
    return frame_count;
}

// ================= SETUP =================
void setup() {
  // 1. Seriale USB (Debug PC)
  Serial.begin(115200);
  Serial.println("--- AVVIO SISTEMA ESP32 ---");

  // 2. Seriale UART2 (Per Raspberry Pi) - Pin 16/17
  Serial2.begin(115200, SERIAL_8N1, RASPI_RX, RASPI_TX);
  Serial.println("UART Raspberry avviata su pin 16/17");

  // 3. Pin Pulsanti
  pinMode(BTN_SX, INPUT_PULLUP);
  pinMode(BTN_DX, INPUT_PULLUP);

  // 4. Pin Batteria
  pinMode(LED_BAT_ROSSO, OUTPUT);
  pinMode(LED_BAT_GIALLO, OUTPUT);
  pinMode(LED_BAT_VERDE, OUTPUT);
  pinMode(BAT_SENSE_PIN, INPUT);

  // 5. Pin RGB (Configurazione PWM v3.0)
  ledcAttach(RGB_PIN_R, 5000, 8); // 5kHz, 8bit resolution
  ledcAttach(RGB_PIN_G, 5000, 8);
  ledcAttach(RGB_PIN_B, 5000, 8);
  setRGB(0, 0, 255); // BLU: Avvio in corso

  // 6. Display OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("❌ Errore OLED!"));
  }
  display.clearDisplay();
  display.display();

  // 7. Configurazione Audio I2S
  Serial.println("Configurazione I2S...");
  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = 44100,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 8,
      .dma_buf_len = 64,
      .use_apll = false,
      .tx_desc_auto_clear = true
  };
  i2s_pin_config_t pin_config = {
      .bck_io_num = I2S_MIC_SCK,
      .ws_io_num = I2S_MIC_WS,
      .data_out_num = -1, 
      .data_in_num = I2S_MIC_SD
  };
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  i2s_start(I2S_NUM_0);

  // 8. Avvio Bluetooth
  Serial.printf("Cerco cuffie: %s\n", BT_HEADPHONES_NAME);
  a2dp_source.set_volume(100);
  a2dp_source.set_auto_reconnect(false); // Metti true se vuoi che si riconnetta da solo
  a2dp_source.start(BT_HEADPHONES_NAME, get_data_frames);

  // Timer reset
  lastButtonPress = millis(); 
}

// ================= LOOP PRINCIPALE =================
void loop() {
  
  // --- 1. GESTIONE PULSANTI (Navigazione) ---
  // Reset selezione se si preme un tasto
  if (digitalRead(BTN_SX) == LOW) {
    delay(200); // Debounce
    currentLangIndex--;
    if (currentLangIndex < 0) currentLangIndex = langCount - 1;
    
    lastButtonPress = millis();
    langSelected = false;
    currentState = STATE_MENU;
  }
  
  if (digitalRead(BTN_DX) == LOW) {
    delay(200);
    currentLangIndex++;
    if (currentLangIndex >= langCount) currentLangIndex = 0;
    
    lastButtonPress = millis();
    langSelected = false;
    currentState = STATE_MENU;
  }

  // --- 2. LOGICA SELEZIONE LINGUA ---
  // Se siamo nel menu e l'utente non tocca nulla per 4 secondi
  if (currentState == STATE_MENU && !langSelected) {
    if (millis() - lastButtonPress > SELECTION_DELAY) {
      langSelected = true;
      Serial.printf("Lingua confermata: %s\n", languages[currentLangIndex]);
      
      // Passa allo stato ASCOLTO
      currentState = STATE_LISTENING;
      
      // Invia comando al Raspberry (quando sarà collegato)
      Serial2.printf("SET_LANG:%s\n", languages[currentLangIndex]);
    }
    // Colore Giallo: In attesa di selezione
    setRGB(255, 255, 0); 
  }

  // --- 3. FEEDBACK VISIVO (LED RGB) ---
  switch (currentState) {
    case STATE_MENU:
      // Giallo gestito sopra
      break;
      
    case STATE_LISTENING:
      setRGB(0, 255, 0); // VERDE: Microfono attivo
      break;
      
    case STATE_THINKING:
      setRGB(0, 0, 255); // BLU: Elaborazione
      break;
      
    case STATE_SPEAKING:
      setRGB(255, 0, 255); // VIOLA: TTS Parla
      break;
  }

  // --- 4. RICEZIONE DATI DAL RASPBERRY (Simulazione) ---
  if (Serial2.available()) {
    String msg = Serial2.readStringUntil('\n');
    Serial.print("Da Pi: "); 
    Serial.println(msg);
    // Qui aggiungeremo la logica per cambiare stato quando il Pi risponde
  }

  // --- 5. AGGIORNAMENTO UI (Display e Batt) ---
  // Aggiorna ogni 100ms per non rallentare l'audio
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 100) {
    updateBatteryStatus();
    drawUI();
    lastUpdate = millis();
  }
}