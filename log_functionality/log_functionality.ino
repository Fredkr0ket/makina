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
#include <Fonts/FreeSans9pt7b.h>  // Include one of the GFX fonts

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

// oled variables
#define SCREEN_WIDTH 128  // OLED width
#define SCREEN_HEIGHT 64  // OLED height
#define OLED_RESET    -1  // Reset pin (-1 for shared reset)
#define SCREEN_ADDRESS 0x3C  // I2C address for 128x64 OLEDs
#define SDA_PIN 21  // GPIO4
#define SCL_PIN 25 // GPIO2

// voltage variables
#define SENSITIVITY 504  // Voltage sensor sensitivity
#define ADC_PIN 39  // ADC input pin

ZMPT101B voltageSensor(ADC_PIN, 50.0);  // Sensor instance
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);  // OLED instance

// strobeLight pinout
#define strobeLight 33

// log variables
#define sdChipselect 22 
#define logInterval 300000UL
#define thirtySeconds 30000UL // Test time
unsigned long lastLogTime = 0;

// Wifi parameters
const char* ssid = "test";
const char* password = "kaaskaas123";

// NTP time settings
const char* ntpServer = "nl.pool.ntp.org";

// initialize EmonLib
EnergyMonitor emon1;   

// initialize MAX6675
const uint8_t MAX6675_CS = 5;
MAX6675 thermocouple(MAX6675_CS, &SPI);

void setup() {
  // Initialize serial, WiFi and SPI connection
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  SPI.begin();
  Wire.begin(SDA_PIN, SCL_PIN);
  delay(2000);

  // Set voltage sensitivity
  voltageSensor.setSensitivity(SENSITIVITY);

  // Init OLED display
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
      Serial.println("SSD1306 allocation failed");
      for (;;); // Halt if display fails
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.display();

  // Initialize strobelight output pin
  pinMode(strobeLight, OUTPUT);

  // calibrate EmonLib
  emon1.current(32, 30);

  // ---- SD CARD MODULE ---- //
  if (SD.begin(sdChipselect)) {
    Serial.println("SD card initialized.");
  } else {
    Serial.println("Card failed, or not present.");
    return;
  }

  // initialize thermocouple 
  thermocouple.begin();
  uint8_t status = thermocouple.read();
  if(status == STATUS_OK) {
    Serial.println("Thermocouple initialized");
  } else {
    Serial.println("Error while trying to initialized Thermocouple");
  }
  
  // ESP32 restart reason
  //  Serial.println("Reset reason: " + String(esp_reset_reason()));
  
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
  // ---- DUMMY DATA ---- //
  // int voltage = 220;
  // int temperature = 5;
  // --------------------- //

  // ---- SENSOR MESUREMENTS ---//
  float voltage = voltageSensor.getRmsVoltage();
  double current = emon1.calcIrms(1480);
  thermocouple.read();
  delay(50);
  float temperature = thermocouple.getCelsius();
  // --------------------- //

  Serial.println("temp: " + String(temperature));
  Serial.println("current: " + String(current/4));
  Serial.println("voltage: " + String(voltage));

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
  // delay(1000);


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
  // if (
  //   voltage >= config["critical_threshold"]["over_voltage"] ||
  //   voltage <= config["critical_threshold"]["under_voltage"] ||
  //   current >= config["critical_threshold"]["current"] ||
  //   temperature >= config["critical_threshold"]["temperature"]
  // ) {
  //   logData("/critical_logs.csv", voltage, current, temperature);
  //   digitalWrite(strobeLight, HIGH);
  //   delay(5000);
  // } else {
  //   digitalWrite(strobeLight, LOW);
  // }
  // --------------------- //

  // ---- PERIODICAL LOG ---- //
  unsigned long currentTime = millis();
  if (currentTime - lastLogTime >= thirtySeconds) {
    lastLogTime = currentTime;
    Serial.println("5 minutes passed");
    logData("/periodical_logs.csv", 220, 2, 50);
  }
  // --------------------- //
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


void logData(const char* logPath, const int voltage, const int current, const int temperature) {
  // Initialize logFile object
  File logFile = SD.open(logPath, FILE_APPEND);

  // Get currentTime
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