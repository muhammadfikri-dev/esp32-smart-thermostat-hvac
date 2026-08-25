#include <Wire.h>
#include <Adafruit_BME280.h>

#define PIN_COOL 27
#define PIN_HEAT 14

Adafruit_BME280 bme;
float targetTemp = 24.0;

void setup() {
  Serial.begin(115200);
  pinMode(PIN_COOL, OUTPUT);
  pinMode(PIN_HEAT, OUTPUT);
  digitalWrite(PIN_COOL, HIGH);
  digitalWrite(PIN_HEAT, HIGH);

  Wire.begin(21, 22);
  bme.begin(0x76, &Wire);
}

void loop() {
  float currentTemp = bme.readTemperature();
  Serial.printf("Current: %.1f C | Target: %.1f C\n", currentTemp, targetTemp);

  if (currentTemp > (targetTemp + 0.5)) {
    digitalWrite(PIN_COOL, LOW);  // AC ON
    digitalWrite(PIN_HEAT, HIGH);
  } else if (currentTemp < (targetTemp - 0.5)) {
    digitalWrite(PIN_COOL, HIGH);
    digitalWrite(PIN_HEAT, LOW);  // Heat ON
  } else {
    digitalWrite(PIN_COOL, HIGH);
    digitalWrite(PIN_HEAT, HIGH);
  }
  delay(3000);
}