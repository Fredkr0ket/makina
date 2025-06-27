// ---- Libraries ---- //
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include "time.h"
#include <ArduinoJson.h>
#include "EmonLib.h"
#include <SPI.h>
#include "MAX6675.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ZMPT101B.h>
#include <Fonts/FreeSans9pt7b.h>  
// --------------------- //

/*
CONFIG FILE EXAMPLE
{
    "critical_threshold": {
        "over_voltage": 260,
        "under_voltage": 190,
        "current": 10,
        "temperature": 100
    },
    "warning_threshold": {
        "over_voltage": 240,
        "under_voltage": 200,
        "current": 5,
        "temperature": 80
    }
}
*/

// ---- OLED VARIABLES
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64 
#define OLED_RESET    -1  
#define SCREEN_ADDRESS 0x3C  
#define SDA_PIN 21  
#define SCL_PIN 25 
// --------------------- //

// ---- TEMPERATURE SENSOR ---- //
#define MAX6675_CS 5
// --------------------- //

// ---- CURRENT SENSOR ---- //
#define CURRENT_SENSITIVITY 30
#define CURRENT_PIN 32
// --------------------- //

// ---- VOLTAGE SENSOR ---- //
#define VOLTAGE_SENSITIVITY 512  
#define VOLTAGE_PIN 39
// --------------------- //

// ---- STROBELIGHT ---- //
#define STROBE_LIGHT 33
// --------------------- //

// ---- LOG VARIABLES ---- //
#define SD_CHIPSELECT 22 
bool sdInitialized = false;
#define LOG_INTERVAL 300000UL
#define THIRTY_SECONDS 30000UL // Test time
unsigned long lastLogTime = 0;
// --------------------- //

// ---- WIFI PARAMETERS ---- //
const char* ssid = "test";
const char* password = "kaaskaas123";
// --------------------- //

// ---- NTP SERVER ---- //
const char* ntpServer = "nl.pool.ntp.org";
// --------------------- //

// ---- INITIALIZE SENSORS & DISPLAY ---- ///
EnergyMonitor emon1; // CURRENT
MAX6675 thermocouple(MAX6675_CS, &SPI); // TEMPERATURE
ZMPT101B voltageSensor(VOLTAGE_PIN, 50.0);  // VOLTAGE
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);  // OLED 
// --------------------- //

int skipReading = 5;

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  SPI.begin();
  Wire.begin(SDA_PIN, SCL_PIN);
  delay(2000);  

  // ---- CALIBRATE SENSORS ---- //
  voltageSensor.setSensitivity(VOLTAGE_SENSITIVITY);
  emon1.current(CURRENT_PIN, CURRENT_SENSITIVITY);
  // --------------------- //

  // ---- INITIALIZE THERMOCOUPLE ---- //
  thermocouple.begin();
  uint8_t status = thermocouple.read();
  if(status == STATUS_OK) {
    Serial.println("Thermocouple initialized");
  } else {
    Serial.println("Error while trying to initialized Thermocouple");
  }
  // --------------------- //

  pinMode(STROBE_LIGHT, OUTPUT);

  // ---- INITIALIZE OLED ---- //
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
      Serial.println("SSD1306 allocation failed");
      for (;;); // Halt if display fails
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.display();
  // --------------------- //

  // ---- INITIALIZE SD CARD MODULE ---- //
  sdInitialized = SD.begin(SD_CHIPSELECT);
  if (sdInitialized) {
    Serial.println("SD card initialized.");
  } else {
    Serial.println("Card failed, or not present.");
    return;
  }
  // --------------------- //
  
  // ---- check if wifi is connected //
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Wifi connected");
  }
  // --------------------- //

  createFileIfNotExists("/periodical_logs.csv");
  createFileIfNotExists("/critical_logs.csv");
  createFileIfNotExists("/config.json");
}

void loop() { 
  // ---- CHECK IF SD IS STILL INITIALIZED ---- //
  if (!sdInitialized) {
    Serial.println("Reinitializing SD card...");
    sdInitialized = SD.begin(SD_CHIPSELECT);
    if (sdInitialized) {
      Serial.println("SD card reinitialized");
    } else {
      Serial.println("Failed to reinitialize SD card");
    }
  } 
  // --------------------- //

  // ---- SENSOR MESUREMENTS ---//
  double current = 0.00;
  if (skipReading > 0) {
    skipReading--;
    emon1.calcIrms(1480);
  } else {
    current = emon1.calcIrms(1480);
  }
  thermocouple.read();
  delay(50);
  float temperature = thermocouple.getCelsius();
  float voltage = voltageSensor.getRmsVoltage();

  // --------------------- //

  // ---- DISPLAY DATA IN TERMINAL ---- //
  Serial.println("temp: " + String(temperature));
  Serial.println("current: " + String(current));
  Serial.println("voltage: " + String(voltage));
  // --------------------- //

  // ---- DISPLAY DATA ON OLED ---- //
  display.clearDisplay();
  display.setTextSize(1);
  display.setFont(&FreeSans9pt7b);  // Set custom font
  display.setCursor(0, 16);         // Y-position is baseline
  display.println("Amps: " + String(current) +"A");
  display.setCursor(0, 34);
  display.println("Volt:   " + String(voltage) + "V");
  display.setCursor(0, 52);
  display.println("Temp: " + String(temperature) + "C");
  display.display();
  // --------------------- //

  // ---- READ CONFIG FILE ---- //
  const char* configPath = "/config.json";
  File configFile = SD.open(configPath, FILE_READ);

  if (!configFile) {
    Serial.println("Failed to open config file");
    return;
  }

  StaticJsonDocument<256> config;
  DeserializationError error = deserializeJson(config, configFile);
  configFile.close();

  if (error) {
    Serial.print("deserializeJson failed: ");
    Serial.println(error.c_str());
    return;
  }
  // --------------------- //

  // ---- CRITICAL LOG ---- //
  if (
    voltage >= config["critical_threshold"]["over_voltage"] ||
    voltage <= config["critical_threshold"]["under_voltage"] ||
    current >= config["critical_threshold"]["current"] ||
    temperature >= config["critical_threshold"]["temperature"]
  ) {
    logData("/critical_logs.csv", voltage, current, temperature);
    digitalWrite(STROBE_LIGHT, HIGH);
  } else {
    digitalWrite(STROBE_LIGHT, LOW);
  }
  // --------------------- //

  // ---- PERIODICAL LOG ---- //
  unsigned long currentTime = millis();
  if (currentTime - lastLogTime >= THIRTY_SECONDS) {
    lastLogTime = currentTime;
    Serial.println("5 minutes passed");
    logData("/periodical_logs.csv", voltage, current, temperature);
  }
  // --------------------- //
  delay(2500);
}

void createFileIfNotExists(const char* path) {
  if (!SD.exists(path)) {
    File file = SD.open(path, FILE_WRITE);
    if (file) {
      file.close();
      Serial.printf("Successfully created: %s\n", path);
    } else {
      Serial.printf("Failed to create: %s\n", path);
    }
  } else {
    Serial.printf("Already exists: %s\n", path);
  }
}


void logData(const char* logPath, const int voltage, const int current, const int temperature) {
  File logFile = SD.open(logPath, FILE_APPEND);

  String currentTime = getCurrentTimeString();
  Serial.println("Current time: " + currentTime);

  if (logPath == "/periodical_logs.csv" && logFile) {
    String status = getLogStatus(voltage, current, temperature);
    String logData = String(currentTime) + ";" + status + ";" + String(voltage) + ";" + String(current) + ";" + String(temperature) + "\n";
    Serial.println("Log data: " + logData);
    logFile.print(logData); // Append data
    logFile.close();
  } else if (logPath == "/critical_logs.csv" && logFile) {
    String logData = String(currentTime) + ";" + "CRITICAL;" + String(voltage) + ";" + String(current) + ";" + String(temperature) + "\n";
    Serial.println("Log data: " + logData);
    logFile.print(logData); // Append data
    logFile.close();
  } else {
    Serial.printf("Failed to open logfile: %s\n", logPath);
  }
}

String getCurrentTimeString() {
  configTzTime("CET-1CEST,M3.5.0/2,M10.5.0/3", ntpServer);

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Time not available";
  }
  
  char buffer[30];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buffer);
}

String getLogStatus(int voltage, int current, int temperature) {
  const char* configPath = "/config.json";
  File configFile = SD.open(configPath, FILE_READ);

  StaticJsonDocument<256> config;

  DeserializationError error = deserializeJson(config, configFile);
  if (error) {
    Serial.print("deserializeJson failed: ");
    Serial.println(error.c_str());
    configFile.close();
    return "Error reading config";
  }

  if (voltage >= config["critical_threshold"]["over_voltage"] || voltage <= config["critical_threshold"]["under_voltage"] || current >= config["critical_threshold"]["current"] || temperature >= config["critical_threshold"]["temperature"]) {
    return "CRITICAL";
  } else if (voltage >= config["warning_threshold"]["over_voltage"] || voltage <= config["warning_threshold"]["under_voltage"] || current >= config["warning_threshold"]["current"] || temperature >= config["warning_threshold"]["temperature"]) {
    return "WARNING";
  } else {
    return "NORMAL";
  }
}