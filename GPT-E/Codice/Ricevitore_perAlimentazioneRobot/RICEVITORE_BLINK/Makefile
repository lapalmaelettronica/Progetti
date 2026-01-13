# Definire il nome del programma
TARGET = RICEVITORE_BLINK

# MCU da usare (ATmega8)
MCU = atmega8

# Cartelle per i file di Arduino e MiniCore
ARDUINO_DIR = /Applications/Arduino.app/Contents/Java/hardware/arduino/avr
ARDUINO_CORE = $(ARDUINO_DIR)/cores/arduino
ARDUINO_VARIANT = $(ARDUINO_DIR)/variants/standard
MINICORE_DIR = /Users/lorenzopalmato/Downloads/Hardware/MiniCore-master/avr/cores/MCUdude_corefiles
ARDUINO_LIBS = /Users/lorenzopalmato/Documents/Arduino/libraries
RF24_LIB = /Users/lorenzopalmato/Documents/Arduino/libraries/RF24

# Compilazione
CXX = avr-g++
CXXFLAGS = -mmcu=$(MCU) -Os -DF_CPU=16000000UL \
  -I$(MINICORE_DIR) -I$(ARDUINO_CORE) -I$(ARDUINO_VARIANT) \
  -I$(ARDUINO_LIBS)/SPI-master \
  -I$(RF24_LIB) \
  -I$(RF24_LIB)/utility \
  -I$(ARDUINO_DIR)/libraries
LDFLAGS = -mmcu=$(MCU)

# File sorgente
SRC = RICEVITORE_BLINK.cpp

# File di output
OBJ = $(SRC:.cpp=.o)
HEX = $(TARGET).hex

# Regole di compilazione
all: $(HEX)

$(HEX): $(OBJ)
	avr-g++ $(OBJ) -o $(TARGET).elf $(LDFLAGS)
	avr-objcopy -O ihex -R .eeprom $(TARGET).elf $(HEX)

%.o: %.cpp
	avr-g++ $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET).elf $(HEX)

