#include "Arduino.h"
#include "WiFi.h"
#include "Audio_nopsram.h"
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <ArduinoJson.h>

#define I2S_DOUT      25
#define I2S_BCLK      33
#define I2S_LRC       32

#define RXD2          16
#define TXD2          5

#define I2C_SDA       21
#define I2C_SCL       22

#define PCA_OE        27   // lascia come nel tuo firmware attuale se è davvero così sul PCB

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40);

#define SERVOMIN  150
#define SERVOMAX  600

String ssid = "Vodafone-A64688680";
String password = "tu3s8utv5ub5tx7w";

Audio audio;
unsigned long lastHeartbeat = 0;


// --------------------------------------------------
// UTILITY DEBUG
// --------------------------------------------------

void scanI2C() {
    Serial.println("Scanning I2C...");

    uint8_t found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        uint8_t err = Wire.endTransmission();

        if (err == 0) {
            Serial.print("Trovato device a 0x");
            if (addr < 16) Serial.print("0");
            Serial.println(addr, HEX);
            found++;
        }
    }

    if (found == 0) {
        Serial.println("Nessun device I2C trovato.");
    }

    Serial.println("Scan finito.");
}

void sendAck(int id, bool ok, const char* error = nullptr) {
    StaticJsonDocument<128> doc;
    doc["id"] = id;
    doc["type"] = "ack";
    doc["ok"] = ok;

    if (error != nullptr) {
        doc["error"] = error;
    }

    serializeJson(doc, Serial2);
    Serial2.println();

    Serial.print("ACK inviato -> id=");
    Serial.print(id);
    Serial.print(" ok=");
    Serial.println(ok ? "true" : "false");
}

void sendHeartbeat() {
    StaticJsonDocument<128> doc;
    doc["type"] = "heartbeat";
    doc["uptime_ms"] = millis();

    serializeJson(doc, Serial2);
    Serial2.println();

    Serial.println("HEARTBEAT sent");
}

void sendLedState(const char* color, int brightness) {
    StaticJsonDocument<192> doc;
    doc["type"] = "state";
    doc["device"] = "led";

    JsonObject value = doc["value"].to<JsonObject>();
    value["color"] = color;
    value["brightness"] = brightness;

    serializeJson(doc, Serial2);
    Serial2.println();

    Serial.print("LED state inviato -> ");
    Serial.print(color);
    Serial.print(" / ");
    Serial.println(brightness);
}

void sendServoState(int channel, int deg) {
    StaticJsonDocument<192> doc;
    doc["type"] = "state";
    doc["device"] = "servo";

    JsonObject value = doc["value"].to<JsonObject>();
    value["channel"] = channel;
    value["deg"] = deg;

    serializeJson(doc, Serial2);
    Serial2.println();

    Serial.print("SERVO state inviato -> ch=");
    Serial.print(channel);
    Serial.print(" deg=");
    Serial.println(deg);
}


// --------------------------------------------------
// HARDWARE
// --------------------------------------------------

void setEyesColor(int r, int g, int b) {
    int redVal   = 4095 - map(r, 0, 255, 0, 4095);
    int greenVal = 4095 - map(g, 0, 255, 0, 4095);
    int blueVal  = 4095 - map(b, 0, 255, 0, 4095);

    // Occhio sinistro
    pwm.setPWM(3, 0, redVal);    // R sx
    pwm.setPWM(4, 0, greenVal);  // G sx
    pwm.setPWM(5, 0, blueVal);   // B sx

    // Occhio destro
    pwm.setPWM(9, 0, redVal);    // R dx
    pwm.setPWM(10, 0, greenVal); // G dx
    pwm.setPWM(11, 0, blueVal);  // B dx

    // Spegne canali non usati per gli occhi
    pwm.setPWM(0, 0, 4095);
    pwm.setPWM(1, 0, 4095);
    pwm.setPWM(2, 0, 4095);
    pwm.setPWM(6, 0, 4095);
    pwm.setPWM(7, 0, 4095);
    pwm.setPWM(8, 0, 4095);
}

void moveServo(int ch, int angle) {
    int pulse = map(angle, 0, 180, SERVOMIN, SERVOMAX);
    pwm.setPWM(ch, 0, pulse);

    Serial.print("moveServo ch=");
    Serial.print(ch);
    Serial.print(" angle=");
    Serial.print(angle);
    Serial.print(" pulse=");
    Serial.println(pulse);
}

void setEyesByColorName(const String& color, int brightness) {
    int v = map(brightness, 0, 100, 0, 255);

    if (color == "red") {
        setEyesColor(v, 0, 0);
    } else if (color == "green") {
        setEyesColor(0, v, 0);
    } else if (color == "blue") {
        setEyesColor(0, 0, v);
    } else if (color == "yellow") {
        setEyesColor(v, v, 0);
    } else if (color == "white") {
        setEyesColor(v, v, v);
    } else if (color == "off") {
        setEyesColor(0, 0, 0);
    } else {
        setEyesColor(0, 0, 0);
    }

    Serial.print("setEyesByColorName -> ");
    Serial.print(color);
    Serial.print(" / ");
    Serial.println(brightness);
}


// --------------------------------------------------
// JSON COMMAND HANDLER
// --------------------------------------------------

void handleJsonCommand(const String& cmd) {
    Serial.print("handleJsonCommand: ");
    Serial.println(cmd);

    StaticJsonDocument<384> doc;
    DeserializationError err = deserializeJson(doc, cmd);

    if (err) {
        Serial.print("JSON parse error: ");
        Serial.println(err.c_str());
        return;
    }

    const char* type = doc["type"];
    int id = doc["id"] | 0;

    if (!type || String(type) != "cmd") {
        sendAck(id, false, "invalid_type");
        return;
    }

    const char* target = doc["target"];
    const char* action = doc["action"];

    if (!target || !action) {
        sendAck(id, false, "missing_target_or_action");
        return;
    }

    String targetStr = String(target);
    String actionStr = String(action);

    if (targetStr == "led" && actionStr == "set") {
        const char* color = doc["args"]["color"] | "off";
        int brightness = doc["args"]["brightness"] | 0;

        if (brightness < 0 || brightness > 100) {
            sendAck(id, false, "invalid_brightness");
            return;
        }

        setEyesByColorName(String(color), brightness);
        sendAck(id, true);
        sendLedState(color, brightness);
        return;
    }

    if (targetStr == "servo" && actionStr == "set_angle") {
        int channel = doc["args"]["channel"] | -1;
        int deg = doc["args"]["deg"] | -1;

        if (channel < 0 || channel > 15) {
            sendAck(id, false, "invalid_channel");
            return;
        }

        if (deg < 0 || deg > 180) {
            sendAck(id, false, "invalid_angle");
            return;
        }

        moveServo(channel, deg);
        sendAck(id, true);
        sendServoState(channel, deg);
        return;
    }

    if (targetStr == "audio" && actionStr == "play") {
        String url = "http://192.168.1.200:5001/stream.mp3?t=" + String(millis());
        Serial.print("AUDIO URL: ");
        Serial.println(url);

        audio.connecttohost(url.c_str());
        sendAck(id, true);
        return;
    }

    sendAck(id, false, "unsupported_command");
}


// --------------------------------------------------
// LEGACY COMMAND HANDLER
// --------------------------------------------------

void handleLegacyCommand(const String& cmd) {
    Serial.print("handleLegacyCommand: ");
    Serial.println(cmd);

    if (cmd.startsWith("PLAY")) {
        String url = "http://192.168.1.200:5001/stream.mp3?t=" + String(millis());
        Serial.print("LEGACY AUDIO URL: ");
        Serial.println(url);
        audio.connecttohost(url.c_str());
        return;
    }

    if (cmd.startsWith("EYES:")) {
        int c1 = cmd.indexOf(',');
        int c2 = cmd.lastIndexOf(',');
        int r = cmd.substring(5, c1).toInt();
        int g = cmd.substring(c1 + 1, c2).toInt();
        int b = cmd.substring(c2 + 1).toInt();
        setEyesColor(r, g, b);
        return;
    }

    if (cmd.startsWith("MOVE:")) {
        int comma = cmd.indexOf(',');
        int ch = cmd.substring(5, comma).toInt();
        int ang = cmd.substring(comma + 1).toInt();
        moveServo(ch, ang);
        return;
    }
}


// --------------------------------------------------
// SETUP
// --------------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println("BOOT: inizio setup");

    Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);
    Serial.println("BOOT: Serial2 ok");

    Wire.begin(I2C_SDA, I2C_SCL);
    Serial.println("BOOT: Wire ok");

    scanI2C();

    pinMode(PCA_OE, OUTPUT);
    digitalWrite(PCA_OE, HIGH);
    Serial.println("BOOT: OE HIGH");

    pwm.begin();
    pwm.setPWMFreq(60);
    Serial.println("BOOT: pwm.begin ok");

    for (int i = 0; i < 12; i++) {
        pwm.setPWM(i, 0, 4095);
    }
    for (int i = 12; i < 16; i++) {
        pwm.setPWM(i, 0, 0);
    }
    Serial.println("BOOT: pwm init done");

    digitalWrite(PCA_OE, LOW);
    Serial.println("BOOT: OE LOW");

    WiFi.begin(ssid.c_str(), password.c_str());
    Serial.println("BOOT: WiFi begin");

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println();
    Serial.print("BOOT: WiFi connected, IP=");
    Serial.println(WiFi.localIP());

    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(21);
    Serial.println("BOOT: audio ok");

    setEyesColor(255, 0, 0); delay(500);
    setEyesColor(0, 255, 0); delay(500);
    setEyesColor(0, 0, 255); delay(500);
    setEyesColor(0, 0, 0);

    Serial.println("BOOT: setup finito");
}
// --------------------------------------------------
// LOOP
// --------------------------------------------------

void loop() {
    audio.loop();

    if (millis() - lastHeartbeat >= 1000) {
        sendHeartbeat();
        lastHeartbeat = millis();
    }

    if (Serial2.available() > 0) {
        String cmd = Serial2.readStringUntil('\n');
        cmd.trim();

        Serial.print("Ricevuto da Serial2: ");
        Serial.println(cmd);

        if (cmd.startsWith("{")) {
            handleJsonCommand(cmd);
        } else {
            handleLegacyCommand(cmd);
        }
    }
}