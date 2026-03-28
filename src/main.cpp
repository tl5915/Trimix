#include <Arduino.h>
// OTA Libraries
#include <FS.h>
#include <SPIFFS.h>
#include <Update.h>
// UI Libraries
#include <WiFi.h>
#include <esp_bt.h>
#include <esp_wifi.h>
#include <WebServer.h>
// Peripheral Libraries
#include <EEPROM.h>
#include <Adafruit_DS3502.h>
#include <Adafruit_ADS1X15.h>
#include <sensirion_arch_config.h>
#include <sensirion_voc_algorithm.h>
#include <SparkFun_SGP40_Arduino_Library.h>
// HTML pages
#include "htmlPage.h"
#include "settingsPage.h"
#include "oxygenPercentagePage.h"
#include "heliumPercentagePage.h"
#include "heliumPolarityPage.h"
#include "firmwarePage.h"
#include "uploadPage.h"

// Firmware Version
#define FIRMWARE_VERSION 1.1
#define STRINGIFY(x) #x
#define TOSTRING(x)  STRINGIFY(x)

// Global Objects
WebServer server(80);
File fsUploadFile;
Adafruit_DS3502 ds3502;
Adafruit_ADS1115 ads;
SGP40 sgp40;

// Pin Definition
const uint8_t SCLPin = 6;  // GPIO 6
const uint8_t SDAPin = 5;  // GPIO 5

// EEPROM Address
const int ADDR_HELIUM_POLARITY = 0;
const int ADDR_WIPER_VALUE = 4;
const int ADDR_OXYGEN_CAL_PERCENTAGE = 8;
const int ADDR_HELIUM_CAL_PERCENTAGE = 12;
const int ADDR_OXYGEN_CAL_VOLTAGE = 16;
const int ADDR_PURE_OXYGEN_VOLTAGE = 24;
const int ADDR_HELIUM_CAL_VOLTAGE = 32;

// Calibration
const uint8_t defaultwiperValue = 58;           // Potentiometer wiper position
const uint8_t defaultOxygenCalPercentage = 99;  // Oxygen calibration percentage
const uint8_t defaultHeliumCalPercentage = 99;  // Helium calibration percentage
const float defaultOxygenCalVoltage = 12.0;     // Oxygen voltage in air
const float defaultPureOxygenVoltage = 0.0;     // Oxygen voltage in oxygen
const float defaultHeliumCalVoltage = 620.0;    // Helium voltage in helium
bool heliumPolarity = false;
bool isTwoPointCalibrated = false;
uint8_t wiperValue = defaultwiperValue;
uint8_t bestWiperValue = wiperValue;
uint8_t OxygenCalPercentage = defaultOxygenCalPercentage; 
uint8_t HeliumCalPercentage = defaultHeliumCalPercentage;
float oxygencalVoltage = defaultOxygenCalVoltage;
float pureoxygenVoltage = defaultPureOxygenVoltage;
float heliumcalVoltage = defaultHeliumCalVoltage;

// Sampling
const uint8_t calibrationSampleCount = 20;  // Average 20 samples for calibration
const uint8_t samplingRateHz = 50;          // Sampling rate 50 Hz
const uint8_t displayRateHz = 2;            // Display refresh rate 2 Hz
unsigned long lastSampleTime = 0;
unsigned long lastDisplayUpdate = 0;
uint16_t voc = 0;
uint16_t sgpRaw = 0;
uint16_t sgpRawCorr = 0;
uint16_t sampleCount = 0;
uint16_t avgSampleCount = 0;
float oxygenVoltage = 0.0;
float oxygenSum = 0.0;
float avgOxygenVoltage = 0.0;
float oxygenPercentage = 0.0;
float heliumVoltage = 0.0;
float heliumSum = 0.0;
float avgHeliumVoltage = 0.0;
float correctedHeliumVoltage = 0.0;
float heliumPercentage = 0.0;
uint16_t mod14 = 0;
uint16_t mod16 = 0;
uint16_t mod_cc = 0;
uint16_t end = 0;
uint16_t end_cc = 0;
float den = 0.0;
float den_cc = 0.0;

// WiFi Settings
const char *ssid = "Trimix_Analyser";  // WiFi SSID
const char *password = "12345678";     // WiFi password

// Read Oxygen Voltage
float getOxygenVoltage() {
  ads.setGain(GAIN_SIXTEEN);
  int16_t oxygenRaw = ads.readADC_Differential_2_3();  // O2 pins 2 & 3
  return fabs(oxygenRaw * 0.0078125);                  // Gain 16, 256 mV
}

// Read Helium Voltage
float getHeliumVoltage() {
  ads.setGain(GAIN_FOUR);
  int16_t heliumRaw = ads.readADC_Differential_0_1();  // He pins 0 & 1
  if (heliumPolarity) {
    return -(heliumRaw * 0.03125);                     // Apply helium polarity setting
  }
  return heliumRaw * 0.03125;                          // Gain 4, 1024 mV
}

// Oxygen Percentage Calculation
float getOxygenPercentage() {
  if (!isTwoPointCalibrated) {
    return (avgOxygenVoltage / oxygencalVoltage) * 20.9;  // One-point calibration
  }
  return 20.9 + ((avgOxygenVoltage - oxygencalVoltage) / (pureoxygenVoltage - oxygencalVoltage)) * (OxygenCalPercentage - 20.9);  // Two-point calibration
}

// 21% Oxygen Calibration
void airOxygenCalibration() {
  oxygenSum = 0.0;
  for (uint8_t i = 0; i < calibrationSampleCount; i++) {
    oxygenVoltage = getOxygenVoltage();
    oxygenSum += oxygenVoltage;
    delay(20);  // 50 Hz sampling rate
  }
  oxygencalVoltage = oxygenSum / calibrationSampleCount;
  EEPROM.put(ADDR_OXYGEN_CAL_VOLTAGE, oxygencalVoltage);
  EEPROM.commit();
}

// 100% Oxygen Calibration
void pureOxygenCalibration() {
  oxygenSum = 0.0;
  for (uint8_t i = 0; i < calibrationSampleCount; i++) {
    oxygenVoltage = getOxygenVoltage();
    oxygenSum += oxygenVoltage;
    delay(20);  // 50 Hz sampling rate
  }
  float newPureVoltage = oxygenSum / calibrationSampleCount;
  if (newPureVoltage > oxygencalVoltage) {
    pureoxygenVoltage = newPureVoltage;
    isTwoPointCalibrated = true;
    EEPROM.put(ADDR_PURE_OXYGEN_VOLTAGE, pureoxygenVoltage);
    EEPROM.commit();
  }
}

// 0% Helium Calibration
void zeroHeliumCalibration() {
  uint8_t lowerLimit = 0;    // Resistor low end
  uint8_t upperLimit = 127;  // Resistor high end
  ds3502.setWiper(lowerLimit);
  delay(50);  // 50 ms delay for voltage stabilisation
  float voltageAtMin = getHeliumVoltage();
  ds3502.setWiper(upperLimit);
  delay(50);  // 50 ms delay for voltage stabilisation
  float voltageAtMax = getHeliumVoltage();
  bool potInverted = voltageAtMax < voltageAtMin;  // Determine polarity of DS3502
  float heliumZeroVoltage = 999.9;
  unsigned long calibrationStartTime = millis();
  while (lowerLimit <= upperLimit) {
    wiperValue = (lowerLimit + upperLimit) / 2;  // Start from mid-point
    ds3502.setWiper(wiperValue);
    delay(50);  // 50 ms delay for voltage stabilisation
    heliumSum = 0.0;
    for (uint8_t i = 0; i < calibrationSampleCount; i++) {
      heliumVoltage = getHeliumVoltage();
      heliumSum += heliumVoltage;
      delay(20);  // 50 Hz sampling rate
    }
    avgHeliumVoltage = heliumSum / calibrationSampleCount;
    float currentHeliumZeroVoltage = avgHeliumVoltage - 0.62;  // Helium correction factor at 21% O2
    if (currentHeliumZeroVoltage > 0 && fabs(currentHeliumZeroVoltage) < fabs(heliumZeroVoltage)) {
      heliumZeroVoltage = currentHeliumZeroVoltage;
      bestWiperValue = wiperValue;  // Find wiper position that gives lowest positive corrected helium voltage
    }
    if (!potInverted) {  // Binary search with inverted DS3502 polarity
      if (avgHeliumVoltage <= 0.62) {
        lowerLimit = wiperValue + 1;
      } else {
        upperLimit = wiperValue - 1;
      }
    } else {             // Binary search with default DS3502 polarity
      if (avgHeliumVoltage <= 0.62) {
        upperLimit = wiperValue - 1;
      } else {
        lowerLimit = wiperValue + 1;
      }
    }
  }
  ds3502.setWiper(bestWiperValue);
  EEPROM.put(ADDR_WIPER_VALUE, bestWiperValue);
  EEPROM.commit();
}

// 100% Helium Calibration
void performHeliumCalibration() {
  heliumSum = 0.0;
  for (uint8_t i = 0; i < calibrationSampleCount; i++) {
    heliumVoltage = getHeliumVoltage();
    heliumSum += heliumVoltage;
    delay(20);  // 20 ms delay between samples
  }
  float newheliumcalVoltage = (heliumSum / calibrationSampleCount) - (17.0 / (1 + exp(0.105 * (0.3240 * HeliumCalPercentage + 19.455))));  // Calibration factor based on standard gas
  if (newheliumcalVoltage > 50) {
    heliumcalVoltage = newheliumcalVoltage;  // 50 mV threshold for invalid helium calibration
    EEPROM.put(ADDR_HELIUM_CAL_VOLTAGE, heliumcalVoltage);
    EEPROM.commit();
  }
}

// Reset to Default
void resetToDefaultCalibration() {
  EEPROM.put(ADDR_HELIUM_POLARITY, false);
  EEPROM.put(ADDR_WIPER_VALUE, defaultwiperValue);
  EEPROM.put(ADDR_OXYGEN_CAL_PERCENTAGE, defaultOxygenCalPercentage);
  EEPROM.put(ADDR_HELIUM_CAL_PERCENTAGE, defaultHeliumCalPercentage);
  EEPROM.put(ADDR_OXYGEN_CAL_VOLTAGE, defaultOxygenCalVoltage);
  EEPROM.put(ADDR_PURE_OXYGEN_VOLTAGE, defaultPureOxygenVoltage);
  EEPROM.put(ADDR_HELIUM_CAL_VOLTAGE, defaultHeliumCalVoltage);
  EEPROM.commit();
  esp_restart();  // Restart after reset
}

// Oxygen Calibration Percentage
void setOxygenCalibration() {
  if (server.hasArg("OxygenCalPercentage")) {
    OxygenCalPercentage = server.arg("OxygenCalPercentage").toInt();
    EEPROM.put(ADDR_OXYGEN_CAL_PERCENTAGE, OxygenCalPercentage);
    EEPROM.commit();
    String response = "<html><body><h1>Saved!</h1><p>Rebooting</p></body></html>";
    server.send(200, "text/html", response);
    esp_restart();
  } else {
    server.send(400, "text/html", "<html><body><h1>Error:</h1><p>Missing parameter.</p></body></html>");
  }
}

// Helium Calibration Percentage
void setHeliumCalibration() {
  if (server.hasArg("HeliumCalPercentage")) {
    HeliumCalPercentage = server.arg("HeliumCalPercentage").toInt();
    EEPROM.put(ADDR_HELIUM_CAL_PERCENTAGE, HeliumCalPercentage);
    EEPROM.commit();
    String response = "<html><body><h1>Saved!</h1><p>Rebooting</p></body></html>";
    server.send(200, "text/html", response);
    esp_restart();
  } else {
    server.send(400, "text/html", "<html><body><h1>Error:</h1><p>Missing parameter.</p></body></html>");
  }
}

// Helium Sensor Polarity
void setHeliumPolarity() {
  if (server.hasArg("HeliumPolarity")) {
    int polarityInput = server.arg("HeliumPolarity").toInt();
    polarityInput = (polarityInput == 1) ? 1 : 0;
    heliumPolarity = (polarityInput == 1);  // 0 = false (default), 1 = true (reverse)
    EEPROM.put(ADDR_HELIUM_POLARITY, heliumPolarity);
    EEPROM.commit();
    String response = "<html><body><h1>Saved!</h1><p>Rebooting</p></body></html>";
    server.send(200, "text/html", response);
    esp_restart();
  } else {
    server.send(400, "text/html", "<html><body><h1>Error:</h1><p>Missing parameter.</p></body></html>");
  }
}

// Format Time
String formatTime() {
  unsigned long seconds = millis() / 1000;
  unsigned long minutes = seconds / 60;
  if (minutes > 99) {
    minutes = 99;
    seconds = 59;
  } else {
    seconds = seconds % 60;
  }
  String timeString = "";
  timeString += String(minutes) + ":";
  if (seconds < 10) {
    timeString += "0";
  }
  timeString += String(seconds);
  return timeString;
}

// Send data to client
void handleData() {
  String json = "{";
  json += "\"time\":\"" + formatTime() + "\",";
  json += "\"count\":\"" + String(avgSampleCount) + "\",";
  json += "\"voc\":\"" + String(voc) + "\",";
  json += "\"sgpRawCorr\":\"" + String(sgpRawCorr) + "\",";
  json += "\"oxygen\":\"" + String(getOxygenPercentage(), 1) + "\",";
  json += "\"helium\":\"" + String(heliumPercentage, 1) + "\",";
  json += "\"avgOxygenVoltage\":\"" + String(avgOxygenVoltage, 2) + "\",";
  json += "\"correctedHeliumVoltage\":\"" + String(correctedHeliumVoltage, 2) + "\",";
  json += "\"mod14\":\"" + String(mod14) + "\",";
  json += "\"mod16\":\"" + String(mod16) + "\",";
  json += "\"end\":\"" + String(end) + "\",";
  json += "\"density\":\"" + String(den, 1) + "\",";
  json += "\"mod_cc\":\"" + String(mod_cc) + "\",";
  json += "\"end_cc\":\"" + String(end_cc) + "\",";
  json += "\"density_cc\":\"" + String(den_cc, 1) + "\",";
  json += "\"oxygencalVoltage\":\"" + String(oxygencalVoltage, 2) + "\",";
  json += "\"pureoxygenVoltage\":\"" + String(pureoxygenVoltage, 2) + "\",";
  json += "\"heliumcalVoltage\":\"" + String(heliumcalVoltage, 2) + "\",";
  json += "\"bestWiperValue\":\"" + String(bestWiperValue) + "\"";
  json += "\"OxygenCalPercentage\":\"" + String(OxygenCalPercentage) + "\",";
  json += "\"HeliumCalPercentage\":\"" + String(HeliumCalPercentage) + "\",";
  json += "}";
  server.send(200, "application/json", json);
}

// Firmware Update
void handleOTAUpload() {
  HTTPUpload& upload = server.upload();
  switch (upload.status) {
    case UPLOAD_FILE_START:
      Serial.printf("OTA: Begin %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
      }
      break;
    case UPLOAD_FILE_WRITE:
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
      break;
    case UPLOAD_FILE_END:
      if (Update.end(true)) {  // true = reboot when done
        Serial.printf("OTA: Success, %u bytes\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
      break;
    default:
      break;
  }
}

void handleOTAFinish() {
  server.sendHeader("Connection", "close");
  if (Update.hasError()) {
    server.send(200, "text/plain", "Update Failed");
  } else {
    server.send(200, "text/plain", "Update Successful. Rebooting...");
  }
  delay(100);
  ESP.restart();
}

// Get Icon File
void handleUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = "/" + upload.filename;
    fsUploadFile = SPIFFS.open(filename, FILE_WRITE);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (fsUploadFile) fsUploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile) fsUploadFile.close();
  }
}


void setup() {
  // Initialisation
  esp_bt_controller_disable();    // Turn off bluetooth
  esp_wifi_set_max_tx_power(40);  // WiFi AP power 10 dBm
  Wire.begin(SDAPin, SCLPin);     // I2C start
  Wire.setClock(400000);          // I2C clock speed 400 kHz
  EEPROM.begin(64);               // EEPROM start
  SPIFFS.begin(true);             // SPIFFS filesystem start
  sgp40.begin();                  // SGP40 start, default 0x59
  ds3502.begin();                 // DS3502 start, default 0x28
  ads.begin(0x48);                // ADS1115 start, default 0x48

  // Load calibration values
  EEPROM.get(ADDR_HELIUM_POLARITY, heliumPolarity);
  EEPROM.get(ADDR_WIPER_VALUE, bestWiperValue);
  EEPROM.get(ADDR_OXYGEN_CAL_PERCENTAGE, OxygenCalPercentage);
  EEPROM.get(ADDR_HELIUM_CAL_PERCENTAGE, HeliumCalPercentage);
  EEPROM.get(ADDR_OXYGEN_CAL_VOLTAGE, oxygencalVoltage);
  EEPROM.get(ADDR_PURE_OXYGEN_VOLTAGE, pureoxygenVoltage);
  EEPROM.get(ADDR_HELIUM_CAL_VOLTAGE, heliumcalVoltage);

  if (heliumPolarity != true && heliumPolarity != false) {
    heliumPolarity = false;
  }
  if (bestWiperValue < 0 || bestWiperValue > 127) {
    bestWiperValue = defaultwiperValue;
  }
  if (OxygenCalPercentage < 21 || OxygenCalPercentage > 99) {
    OxygenCalPercentage = defaultOxygenCalPercentage;
  }
  if (HeliumCalPercentage < 30 || HeliumCalPercentage > 99) {
    HeliumCalPercentage = defaultHeliumCalPercentage;
  }
  if (isnan(oxygencalVoltage) || oxygencalVoltage <= 0.0) {
    oxygencalVoltage = defaultOxygenCalVoltage;
  }
  if (isnan(pureoxygenVoltage) || pureoxygenVoltage <= 0.0) {
    pureoxygenVoltage = defaultPureOxygenVoltage;
  }
  if (isnan(heliumcalVoltage) || heliumcalVoltage <= 0.0) {
    heliumcalVoltage = defaultHeliumCalVoltage;
  }
  ds3502.setWiper(bestWiperValue);

  if (pureoxygenVoltage > oxygencalVoltage) {
    isTwoPointCalibrated = true;
  } else {
    isTwoPointCalibrated = false;
  }

  // Wifi
  WiFi.softAP(ssid, password, 1);
  esp_wifi_set_max_tx_power(40);

  server.serveStatic("/icon.png", SPIFFS, "/icon.png");
  server.on("/", []() {
    server.send(200, "text/html", htmlPage);
  });
  server.on("/data", handleData);
  server.on("/calibrate_oxygen_air", []() {
    airOxygenCalibration();
    server.send(200, "text/html",
      "<html><body><h1>O2 21% Calibrated</h1></body></html>");
  });
  server.on("/calibrate_oxygen_pure", []() {
    pureOxygenCalibration();
    server.send(200, "text/html",
      "<html><body><h1>O2 High Calibrated</h1></body></html>");
  });
  server.on("/calibrate_helium_zero", []() {
    zeroHeliumCalibration();
    server.send(200, "text/html",
      "<html><body><h1>He Zero Calibrated</h1></body></html>");
  });
  server.on("/calibrate_helium_pure", []() {
    performHeliumCalibration();
    server.send(200, "text/html",
      "<html><body><h1>He High Calibrated</h1></body></html>");
  });
  server.on("/settings", []() {
    server.send(200, "text/html", settingsPage);
  });
  server.on("/oxygen_percentage", []() {
    server.send(200, "text/html", oxygenPercentagePage);
  });
  server.on("/helium_percentage", []() {
    server.send(200, "text/html", heliumPercentagePage);
  });
  server.on("/helium_polarity", []() {
    server.send(200, "text/html", heliumPolarityPage);
  });
  server.on("/save_oxygen", HTTP_GET, setOxygenCalibration);
  server.on("/save_helium", HTTP_GET, setHeliumCalibration);
  server.on("/save_polarity", HTTP_GET, setHeliumPolarity);
  server.on("/reset_calibration", HTTP_GET, resetToDefaultCalibration);
  // Link not exposed, manually input URL: http://192.168.4.1/upload_page
  server.on("/upload_page", HTTP_GET, []() {
    server.send(200, "text/html", uploadPage);
  });
  server.on("/upload", HTTP_POST,
    []() {
      server.send(200, "text/html",
        "<html><body>"
          "<h1>Upload Successful</h1>"
          "<p><a href=\"/settings\">Back to Settings</a></p>"
        "</body></html>"
      );
    },
    handleUpload
  );
  server.on("/firmware", HTTP_GET, []() {
    server.send(200, "text/html", firmwarePage);
  });
  server.on("/update", HTTP_POST, handleOTAFinish, handleOTAUpload);
  server.begin();
}


void loop() {
  unsigned long currentTime = millis();

  // HTML update
  server.handleClient();

  // Sensor sampling
  if (currentTime - lastSampleTime >= (1000 / samplingRateHz)) {
    lastSampleTime = currentTime;
    if (sampleCount == 0) {
      oxygenSum = heliumSum = 0.0;
    }
    oxygenVoltage = getOxygenVoltage();
    oxygenSum += oxygenVoltage;
    heliumVoltage = getHeliumVoltage();
    heliumSum += heliumVoltage;
    sampleCount++;
  }

  // Update cycle
  if (currentTime - lastDisplayUpdate >= (1000 / displayRateHz)) {
    lastDisplayUpdate = currentTime;

    // Get Time
    String elapsedTime = formatTime();

    // Get Gas Quality, 0% humidity, 20 degree Celsius
    voc = sgp40.getVOCindex(0, 20);    // VOC index
    if (voc < 1) {
      voc = 1;                         // VOC index minimum 1
    } else if (voc > 500) {
      voc = 500;                       // VOC index maximum 500
    }
    sgp40.measureRaw(&sgpRaw, 0, 20);  // Raw reading
    if (sgpRaw < 20001) {
      sgpRaw = 20001;                  // Raw reading minimum 20001
    } else if (sgpRaw > 52767) {
      sgpRaw = 52767;                  // Raw reading maximum 52767
    }
    sgpRawCorr = sgpRaw - 20000;       // Corrected range 1-32767

    // Calculate average voltages
    avgOxygenVoltage = (sampleCount > 0) ? (oxygenSum / sampleCount) : 0.0;
    if (avgOxygenVoltage > 99.9) {
      avgOxygenVoltage = 99.9;   // Maximum oxygen voltage allowed 99.9 mV
    }
    avgHeliumVoltage = (sampleCount > 0) ? (heliumSum / sampleCount) : 0.0;
    avgSampleCount = sampleCount;
    sampleCount = 0;

    // Calculate oxygen percentage
    oxygenPercentage = getOxygenPercentage();
    if (oxygenPercentage < 0.0) {
      oxygenPercentage = 0.0;  // Minimum oxygen percentage allowed 0%
    }

    // Calculate helium percentage
    correctedHeliumVoltage = avgHeliumVoltage - (17.0 / (1 + exp(-0.105 * (oxygenPercentage - 52.095))));  // Helium correction factor based on oxygen percentage
    if (correctedHeliumVoltage > 999.9) {
      correctedHeliumVoltage = 999.9;   // Maximum helium voltage allowed 999.9 mV
    } else if (correctedHeliumVoltage < -999.0) {
      correctedHeliumVoltage = -999.0;  // Minimum helium voltage allowed -999.0 mV if helium sensor polarity is inverted
    }    
    heliumPercentage = (correctedHeliumVoltage > 0.0) ? (correctedHeliumVoltage / heliumcalVoltage) * HeliumCalPercentage : 0.0;
    if (heliumPercentage < 2.0) {
      heliumPercentage = 0.0;  // Minimum helium percentage allowed 0%, treat <2% as 0%
    }

    // Calculate MOD
    mod14 = (oxygenPercentage > 0) ? (int)((1400.0 / oxygenPercentage) - 10.0) : 0;  // MOD at ppO2 1.4
    if (mod14 > 999) {
      mod14 = 999;  // Maximum MOD allowed 999 m
    }
    mod16 = (oxygenPercentage > 0) ? (int)((1600.0 / oxygenPercentage) - 10.0) : 0;  // MOD at ppO2 1.6
    if (mod16 > 999) {
      mod16 = 999;  // Maximum MOD allowed 999 m
    }
    mod_cc = (oxygenPercentage > 0) ? (int)((1200.0 / oxygenPercentage) - 10.0) : 0;  // MOD at ppO2 1.2
    if (mod_cc > 999) {
      mod_cc = 999;  // Maximum MOD allowed 999 m
    }

    // Calculate END
    end = (mod14 + 10.0) * (1 - heliumPercentage / 100.0) - 10.0;  // END at MOD 1.4
    if (end < 0) {
      end = 0;  // Minimum END allowed 0 m
    }
    end_cc = (mod_cc + 10.0) * (1 - heliumPercentage / 100.0) - 10.0;  // END at MOD 1.2
    if (end_cc < 0) {
      end_cc = 0;  // Minimum END allowed 0 m
    }

    // Calculate Density
    den = (oxygenPercentage * 0.1756 - heliumPercentage * 1.0582 + 123.46) * (mod14 + 10) / 1000;  // Density at MOD 1.4
    if (den > 99.9) {
      den = 99.9;  // Maximum density allowed 99.9 g/L
    }
    den_cc = (oxygenPercentage * 0.1756 - heliumPercentage * 1.0582 + 123.46) * (mod_cc + 10) / 1000;  // Density at MOD 1.2
    if (den_cc > 99.9) {
      den_cc = 99.9;  // Maximum density allowed 99.9 g/L
    }
  }
}