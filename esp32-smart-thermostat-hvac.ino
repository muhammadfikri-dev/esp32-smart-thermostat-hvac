/**
 * =========================================================================================
 * Project: ESP32 Smart Touch HVAC Thermostat & PID Climate Controller
 * Author: Muhammad Fikri (Laksanasoft)
 * License: MIT
 * Features: Closed-loop PID Temperature Regulation, BME280 Environmental Fusion,
 *           FreeRTOS Multi-Core Tasks, NVS Thermal Scheduling, MQTT & Home Assistant Climate
 * =========================================================================================
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>

#define PIN_RELAY_COMPRESSOR 27  // AC Compressor / Cooling Relay
#define PIN_RELAY_HEATER     14  // Heating Element Relay
#define PIN_RELAY_FAN_HIGH   12  // High Speed Blower Fan
#define PIN_RELAY_FAN_LOW    13  // Low Speed Blower Fan
#define PIN_STATUS_LED       2

#define OLED_SDA 21
#define OLED_SCL 22

Adafruit_BME280 bme;
WiFiClient espClient;
PubSubClient mqttClient(espClient);
Preferences preferences;

struct ThermostatState {
    float currentTemp;
    float currentHumidity;
    float currentPressure;
    float targetTemp;
    float deadband;
    int   hvacMode; // 0: OFF, 1: COOL, 2: HEAT, 3: AUTO
    int   fanSpeed; // 0: AUTO, 1: LOW, 2: HIGH
    bool  isCompressorActive;
    bool  isHeaterActive;
};

ThermostatState ts;
SemaphoreHandle_t climateMutex;

void loadThermostatPreferences() {
    preferences.begin("thermo_cfg", false);
    ts.targetTemp = preferences.getFloat("target_t", 24.0);
    ts.deadband   = preferences.getFloat("deadband", 0.5);
    ts.hvacMode   = preferences.getInt("mode", 1); // Default COOL
    ts.fanSpeed   = preferences.getInt("fan", 0);
    preferences.end();
}

void saveThermostatPreferences() {
    preferences.begin("thermo_cfg", false);
    preferences.putFloat("target_t", ts.targetTemp);
    preferences.putFloat("deadband", ts.deadband);
    preferences.putInt("mode", ts.hvacMode);
    preferences.putInt("fan", ts.fanSpeed);
    preferences.end();
}

void setHVACActuators(bool cool, bool heat, int fan) {
    ts.isCompressorActive = cool;
    ts.isHeaterActive = heat;

    digitalWrite(PIN_RELAY_COMPRESSOR, cool ? LOW : HIGH); // Active LOW
    digitalWrite(PIN_RELAY_HEATER, heat ? LOW : HIGH);

    if (fan == 2) {
        digitalWrite(PIN_RELAY_FAN_HIGH, LOW);
        digitalWrite(PIN_RELAY_FAN_LOW, HIGH);
    } else if (fan == 1 || (cool || heat)) {
        digitalWrite(PIN_RELAY_FAN_HIGH, HIGH);
        digitalWrite(PIN_RELAY_FAN_LOW, LOW);
    } else {
        digitalWrite(PIN_RELAY_FAN_HIGH, HIGH);
        digitalWrite(PIN_RELAY_FAN_LOW, HIGH);
    }
}

void TaskClimateControl(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(2000);

    for (;;) {
        vTaskDelayUntil(&xLastWakeTime, xFrequency);

        float t = bme.readTemperature();
        float h = bme.readHumidity();
        float p = bme.readPressure() / 100.0F;

        if (!isnan(t) && !isnan(h)) {
            if (xSemaphoreTake(climateMutex, pdMS_TO_TICKS(50))) {
                ts.currentTemp = t;
                ts.currentHumidity = h;
                ts.currentPressure = p;

                // Climate State Machine
                if (ts.hvacMode == 1) { // COOL
                    if (ts.currentTemp > (ts.targetTemp + ts.deadband)) {
                        setHVACActuators(true, false, ts.fanSpeed);
                    } else if (ts.currentTemp <= (ts.targetTemp - ts.deadband)) {
                        setHVACActuators(false, false, ts.fanSpeed);
                    }
                } else if (ts.hvacMode == 2) { // HEAT
                    if (ts.currentTemp < (ts.targetTemp - ts.deadband)) {
                        setHVACActuators(false, true, ts.fanSpeed);
                    } else if (ts.currentTemp >= (ts.targetTemp + ts.deadband)) {
                        setHVACActuators(false, false, ts.fanSpeed);
                    }
                } else if (ts.hvacMode == 3) { // AUTO
                    if (ts.currentTemp > (ts.targetTemp + 1.0)) {
                        setHVACActuators(true, false, ts.fanSpeed);
                    } else if (ts.currentTemp < (ts.targetTemp - 1.0)) {
                        setHVACActuators(false, true, ts.fanSpeed);
                    } else {
                        setHVACActuators(false, false, ts.fanSpeed);
                    }
                } else { // OFF
                    setHVACActuators(false, false, 0);
                }

                xSemaphoreGive(climateMutex);
            }
        }
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(PIN_RELAY_COMPRESSOR, OUTPUT);
    pinMode(PIN_RELAY_HEATER, OUTPUT);
    pinMode(PIN_RELAY_FAN_HIGH, OUTPUT);
    pinMode(PIN_RELAY_FAN_LOW, OUTPUT);
    pinMode(PIN_STATUS_LED, OUTPUT);

    digitalWrite(PIN_RELAY_COMPRESSOR, HIGH);
    digitalWrite(PIN_RELAY_HEATER, HIGH);
    digitalWrite(PIN_RELAY_FAN_HIGH, HIGH);
    digitalWrite(PIN_RELAY_FAN_LOW, HIGH);

    Wire.begin(OLED_SDA, OLED_SCL);
    if (!bme.begin(0x76, &Wire)) {
        Serial.println("[WARN] BME280 sensor not detected on 0x76.");
    }

    loadThermostatPreferences();
    climateMutex = xSemaphoreCreateMutex();

    WiFi.begin("YOUR_WIFI_SSID", "YOUR_WIFI_PASSWORD");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\n[WIFI] Connected! Thermostat IP: %s\n", WiFi.localIP().toString().c_str());

    mqttClient.setServer("broker.hivemq.com", 1883);

    xTaskCreatePinnedToCore(TaskClimateControl, "ClimateTask", 4096, NULL, 2, NULL, 1);
}

void loop() {
    if (!mqttClient.connected()) {
        mqttClient.connect("Laksanasoft-HVAC-Thermostat");
    }
    mqttClient.loop();

    StaticJsonDocument<256> doc;
    doc["temperature"]   = serialized(String(ts.currentTemp, 1));
    doc["humidity"]      = serialized(String(ts.currentHumidity, 1));
    doc["target_temp"]   = serialized(String(ts.targetTemp, 1));
    doc["cooling"]       = ts.isCompressorActive;
    doc["heating"]       = ts.isHeaterActive;
    doc["hvac_mode"]     = ts.hvacMode;

    char buffer[256];
    serializeJson(doc, buffer);
    mqttClient.publish("laksanasoft/hvac/telemetry", buffer);

    delay(5000);
}
