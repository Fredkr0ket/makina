#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include "time.h"
#include <ArduinoJson.h>

/*
CONFIG FILE EXAMPLE
{
    "critical_threshold": {
        "voltage": 260,
        "current": 10,
        "temperature": 100
    },
    "warning_threshold": {
        "voltage": 240,
        "current": 5,
        "temperature": 80
    }
}
*/

#define SD_CS 5  // Chip Select pin
#define LOG_INTERVAL 300000UL
#define THIRTY_SECONDS 30000UL // Test time
unsigned long lastLogTime = 0;

// Wifi parameters
const char* ssid = "test";
const char* password = "kaaskaas123";

// NTP time settings
const char* ntpServer = "nl.pool.ntp.org";

// Dummy data
// const int voltage = 220;
// const int current = 16;
// const int temperature = 69;
// const char* status = "critical" 

void setup() {
  // Initialize serial and WiFi connection
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  delay(2000);
  

  // ESP32 restart reason
  // Serial.println("Reset reason: " + String(esp_reset_reason()));

  // ---- SD CARD MODULE ---- //
  // Initializing SD card
  if (SD.begin(SD_CS)) {
    Serial.println("SD card initialized.");
  } else {
    Serial.println("Card failed, or not present.");
    return;
  }
  
  // Check if periodical_log.yaml and critical_log.yaml exists
  createFileIfNotExists("/periodical_logs.csv");
  createFileIfNotExists("/critical_logs.csv");
  createFileIfNotExists("/config.json");
  // --------------------- //

  // Check if wifi is connected
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Wifi connected");
  }

}

void loop() { 
  unsigned long currentTime = millis(); // Get current time
;
  if (currentTime - lastLogTime >= THIRTY_SECONDS) {
    lastLogTime = currentTime;
    Serial.println("5 minutes passed");
    logPeriodicalData();
  }
}

void createFileIfNotExists(const char* path) {
  if (!SD.exists(path)) {
    File file = SD.open(path, FILE_WRITE); // Create file
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

void logPeriodicalData() {
  // dummy data
  const int voltage = 220;
  const int current = 2;
  const int temperature = 50;
  // Initialize logFile object
  const char* logPath = "/periodical_logs.csv";
  File logFile = SD.open(logPath, FILE_APPEND);

  // Get currentTime
  String currentTime = getCurrentTimeString();
  Serial.println("Current time: " + currentTime);

  if (logFile) {
    String status = getLogStatus(voltage, current, temperature);
    String logData = String(currentTime) + ";" + status + ";" + String(voltage) + ";" + String(current) + ";" + String(temperature) + "\n";
    Serial.println("Log data: " + logData);
    logFile.print(logData); // Append data
    logFile.close();
  } else {
    Serial.printf("Failed to open logfile: %s\n", logPath);
  }
}

void logCriticalData() {
  // Initialize logFile object
  const char* logPath = "/critical_logs.csv";
  File logFile = SD.open(logPath, FILE_APPEND);

  const int voltage = 260;
  const int current = 2;
  const int temperature = 50;

  // Get currentTime
  String currentTime = getCurrentTimeString();
  Serial.println("Current time: " + currentTime);

  if (logFile) {
    String logData = String(currentTime) + ";" + "CRITICAL;" + String(voltage) + ";" + String(current) + ";" + String(temperature) + "\n";
    Serial.println("Log data: " + logData);
    logFile.print(logData); // Append data
    logFile.close();
  } else {
    Serial.printf("Failed to open logfile: %s\n", logPath);
  }
}

String getCurrentTimeString() {
  configTzTime("CET-1CEST,M3.5.0/2,M10.5.0/3", ntpServer); // Get time

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Time not available";
  }
  
  char buffer[30];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo); // Format time
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
    return "test";
  }

  if (voltage >= config["critical_threshold"]["voltage"] || current >= config["critical_threshold"]["current"] || temperature >= config["critical_threshold"]["temperature"]) {
    return "CRITICAL";
  } else if (voltage >= config["warning_threshold"]["voltage"] || current >= config["warning_threshold"]["current"] || temperature >= config["warning_threshold"]["temperature"]) {
    return "WARNING";
  } else {
    return "NORMAL";
  }
}
