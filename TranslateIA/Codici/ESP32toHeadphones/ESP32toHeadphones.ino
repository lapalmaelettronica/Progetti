#include "BluetoothA2DPSource.h"
#include <driver/i2s.h>

// --- 🎧 NOME CUFFIE ---
// Metti qui il nome ESATTO delle tue cuffie
char *BT_HEADPHONES_NAME = "LeMieCuffie"; 

// --- 🎤 PIN MICROFONO (Basati sul tuo Schematico) ---
#define I2S_MIC_SD  39  // Pin 13 (VN) sullo schema
#define I2S_MIC_WS  26  // Pin 7 (D26) con etichetta WS
#define I2S_MIC_SCK 15  // Pin 28 (D15) con etichetta SCK

// --- CONFIGURAZIONE ---
#define SAMPLE_RATE 44100
BluetoothA2DPSource a2dp_source;

// --- IMPOSTAZIONI VOLUME ---
#define VOLUME_BOOST 20  // Moltiplica il volume x20 (Prova 10, 20 o 30)

int32_t get_data_frames(Frame *frame, int32_t frame_count) {
    size_t bytes_read;
    
    // 1. Leggi i dati dal microfono (Raw)
    i2s_read(I2S_NUM_0, (void*)frame, frame_count * 4, &bytes_read, portMAX_DELAY);
    
    // 2. AMPLIFICAZIONE SOFTWARE
    // Trasformiamo il buffer di byte in un array di numeri a 16 bit (il formato audio)
    int16_t *samples = (int16_t*)frame;
    
    // Ogni frame ha 2 canali (Stereo), quindi i campioni totali sono frame_count * 2
    for (int i = 0; i < frame_count * 2; i++) {
        
        // Moltiplichiamo il valore grezzo per il Boost
        int32_t val = samples[i] * VOLUME_BOOST;

        // 3. LIMITATORE (CLIPPING)
        // Se il numero diventa troppo grande, tagliamo per evitare distorsioni orribili
        if (val > 32767) val = 32767;
        else if (val < -32768) val = -32768;

        // Rimettiamo il valore amplificato nel buffer
        samples[i] = (int16_t)val;
    }
    
    return frame_count;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("--- AVVIO TEST SCHEMATICO ---");
  Serial.printf("SD(VN)=%d, WS=%d, SCK=%d\n", I2S_MIC_SD, I2S_MIC_WS, I2S_MIC_SCK);

  // 1. Configurazione I2S
  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = SAMPLE_RATE,
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

  // Installa driver
  esp_err_t err = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  if (err != ESP_OK) Serial.println("❌ Errore Driver I2S");
  
  err = i2s_set_pin(I2S_NUM_0, &pin_config);
  if (err != ESP_OK) Serial.println("❌ Errore Pin I2S");

  i2s_start(I2S_NUM_0);

  // 2. Avvio Bluetooth
  Serial.printf("Cerco cuffie: %s\n", BT_HEADPHONES_NAME);
  a2dp_source.set_volume(100); 
  a2dp_source.set_auto_reconnect(false);
  a2dp_source.start(BT_HEADPHONES_NAME, get_data_frames);  
}

void loop() {
  if (a2dp_source.is_connected()) {
     delay(2000);
  } else {
     Serial.println("In attesa di connessione...");
     delay(1000);
  }
}