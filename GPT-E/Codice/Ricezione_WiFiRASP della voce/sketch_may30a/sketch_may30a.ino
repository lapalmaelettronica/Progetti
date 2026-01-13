#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

#define LED_ROSSO 2
#define LED_VERDE 3
#define LED_BLU   4
#define LED_GIALLO 5
#define LED_BIANCO 6

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

void setup() {
  Serial.begin(115200);

  pinMode(LED_ROSSO, OUTPUT);
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_BLU, OUTPUT);
  pinMode(LED_GIALLO, OUTPUT);
  pinMode(LED_BIANCO, OUTPUT);

  pwm.begin();
  pwm.setPWMFreq(50);
}

void accendiLED(int pin) {
  digitalWrite(LED_ROSSO, LOW);
  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_BLU, LOW);
  digitalWrite(LED_GIALLO, LOW);
  digitalWrite(LED_BIANCO, LOW);
  digitalWrite(pin, HIGH);
}

void loop() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd == "led_rosso") accendiLED(LED_ROSSO);
    else if (cmd == "led_verde") accendiLED(LED_VERDE);
    else if (cmd == "led_blu") accendiLED(LED_BLU);
    else if (cmd == "led_giallo") accendiLED(LED_GIALLO);
    else if (cmd == "led_bianco") accendiLED(LED_BIANCO);

    else if (cmd.startsWith("servo")) {
      // Esempio: servo_0_150 --> imposta il servo 0 a 150°
      int sep1 = cmd.indexOf('_');
      int sep2 = cmd.indexOf('_', sep1 + 1);
      int num = cmd.substring(sep1 + 1, sep2).toInt();
      int ang = cmd.substring(sep2 + 1).toInt();
      int pulse = map(ang, 0, 180, 150, 600);
      pwm.setPWM(num, 0, pulse);
    }
  }
}


