#include <FS.h>
#include <WiFi.h>
#include <Update.h>
#include <EEPROM.h>
#include <SPIFFS.h>
#include <esp_bt.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <Adafruit_DS3502.h>
#include <Adafruit_ADS1X15.h>
#include <sensirion_arch_config.h>
#include <sensirion_voc_algorithm.h>
#include <SparkFun_SGP40_Arduino_Library.h>

// Firmware Version
#define FIRMWARE_VERSION 1.0.0
#define STRINGIFY(x) #x
#define TOSTRING(x)  STRINGIFY(x)

WebServer server(80);
File fsUploadFile;
Adafruit_DS3502 ds3502;
Adafruit_ADS1115 ads;
SGP40 sgp40;

// Pin Definition
const uint8_t SCLPin = 6;        // GPIO 6
const uint8_t SDAPin = 5;        // GPIO 5

// EEPROM Address
const int ADDR_HELIUM_POLARITY = 0;
const int ADDR_WIPER_VALUE = 4;
const int ADDR_OXYGEN_CAL_PERCENTAGE = 8;
const int ADDR_HELIUM_CAL_PERCENTAGE = 12;
const int ADDR_OXYGEN_CAL_VOLTAGE = 16;
const int ADDR_PURE_OXYGEN_VOLTAGE = 24;
const int ADDR_HELIUM_CAL_VOLTAGE = 32;

// Calibration
const uint8_t defaultwiperValue = 64;           // Potentiometer wiper position
const uint8_t defaultOxygenCalPercentage = 99;  // Oxygen calibration percentage
const uint8_t defaultHeliumCalPercentage = 99;  // Helium calibration percentage
const float defaultOxygenCalVoltage = 10.0;     // Oxygen voltage in air
const float defaultPureOxygenVoltage = 0.0;     // Oxygen voltage in oxygen
const float defaultHeliumCalVoltage = 550.0;    // Helium voltage in helium
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
uint16_t end = 0;
float den = 0.0;

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

// HTML Main Page
const char *htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Trimix Analyser</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <meta name="apple-mobile-web-app-capable" content="yes">
  <meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
  <meta name="apple-mobile-web-app-title" content="Trimix Analyser">
  <link rel="apple-touch-icon" href="/icon.png">
  <style>
    body { font-family: Arial, sans-serif; margin: 0; padding: 0; background-color: #f4f4f4; text-align: center; }
    h1 { font-size: 28px; margin: 0; padding: 6px; background-color: #43B02A; color: #F6EB14; }
    .small { font-size: 14px; color: #333; }
    .system-row { display: flex; justify-content: space-between; padding: 2px 12px; }
    .big-gas { font-size: 70px; font-weight: bold; margin-top: 4px; }
    .voltage { font-size: 18px; margin-top: -2px; color: #444; }
    .section-title { font-size: 18px; margin-top: 20px; }
    .data-block { font-size: 16px; margin-top: 0px; }
    .cal-block { font-size: 14px; margin-top: 0px; line-height: 1.4; }
    button { padding: 8px 16px; margin-top: 4px; font-size: 16px; }
  </style>
</head>

<body>
  <h1>Trimix Analyser</h1>

  <!-- System Data -->
  <div class="system-row small">
    <div><span id="time">0:00</span></div>
    <div><span id="count">0</span> SPS</div>
  </div>

  <!-- Gas Quality -->
  <div class="system-row small">
    <div>VOC: <span id="voc">0</span></div>
    <div><span id="sgpRawCorr">0</span> (32767-1)</div>
  </div>

  <!-- O2 / He Percentages -->
  <div class="big-gas">
    <span id="oxygen">0</span> / <span id="helium">0</span>
  </div>
  <div class="voltage">
    O2: <span id="avgOxygenVoltage">0.0</span> mV |
    He: <span id="correctedHeliumVoltage">0.0</span> mV
  </div>

  <!-- Gas Caluclation -->
  <h2 class="section-title">Gas Caluclation</h2>
  <div class="data-block">
    MOD (ppO2 1.4): <span id="mod14">0</span> m<br>
    MOD (ppO2 1.6): <span id="mod16">0</span> m<br>
    END (@ 1.4 MOD): <span id="end">0</span> m<br>
    Density (@ 1.4 MOD): <span id="density">0</span> g/L
  </div>

  <!-- Calibration Data -->
  <h2 class="section-title">Calibration Data</h2>
  <div class="cal-block">
    Low O2 Cal Voltage: <span id="oxygencalVoltage">0.0</span> mV<br>
    O2 Cal Percentage: <span id="oxygenCalPercentageText">0</span>%<br>
    High O2 Cal Voltage: <span id="pureoxygenVoltage">0.0</span> mV<br>
    He Cal Percent: <span id="heliumCalPercentage">0</span>%<br>
    He Cal Voltage: <span id="heliumcalVoltage">0.0</span> mV<br>
    He Zero Position: <span id="bestWiperValue">0</span>
  </div>

  <!-- Calibration Buttons -->
  <div style="margin-top: 10px; display: flex; justify-content: center; gap: 40px; flex-wrap: wrap;">
    <button onclick="calibrate('airOxygen')">Cal. Low O2</button>
    <button onclick="calibrate('pureOxygen')">Cal. High O2</button>
    <button onclick="calibrate('airHelium')">Cal. Zero He</button>
    <button onclick="calibrate('pureHelium')">Cal. High He</button>
  </div>

  <!-- Settings Button -->
  <div style="margin-top: 40px; text-align: center;">
    <button style="font-size: 18px; padding: 4px 10px;" onclick="window.location.href='/settings'">
      Setting
    </button>
  </div>

  <!-- Refresh Button -->
  <div style="margin-top: 30px; text-align: center;">
    <button style="font-size: 18px; padding: 4px 10px;" onclick="location.reload();">
      Refresh
    </button>
  </div>


  <script>
    function calibrate(type) {
      let url = "";
      if (type === "airOxygen") url = "/calibrate_oxygen_air";
      else if (type === "pureOxygen") url = "/calibrate_oxygen_pure";
      else if (type === "airHelium")  url = "/calibrate_helium_zero";
      else if (type === "pureHelium") url = "/calibrate_helium_pure";
      else {
        alert("Unknown calibration type");
        return;
      }
      fetch(url)
        .then(response => response.text())
        .then(msg => {
          alert(msg.replace(/<[^>]*>?/gm, ""));
        })
        .catch(error => {
          console.error("Calibration error:", error);
          alert("Calibration failed");
        });
    }

    function resetCalibration() {
      if (confirm("Reset calibration to default?")) {
        fetch("/reset_calibration")
          .then(() => {
            alert("Rebooting...");
          })
      }
    }
    window.onload = function() {
      setInterval(() => {
        fetch("/data")
          .then(response => response.json())
          .then(data => {
            document.getElementById("time").textContent = data.time;
            document.getElementById("count").textContent = data.count;
            document.getElementById("oxygen").textContent = data.oxygen;
            document.getElementById("avgOxygenVoltage").textContent = data.avgOxygenVoltage;
            document.getElementById("helium").textContent = data.helium;
            document.getElementById("correctedHeliumVoltage").textContent = data.correctedHeliumVoltage;
            document.getElementById("mod14").textContent = data.mod14;
            document.getElementById("mod16").textContent = data.mod16;
            document.getElementById("end").textContent = data.end;
            document.getElementById("density").textContent = data.density;
            document.getElementById("voc").textContent = data.voc;
            document.getElementById("sgpRawCorr").textContent = data.sgpRawCorr;
            document.getElementById("oxygencalVoltage").textContent = data.oxygencalVoltage;
            document.getElementById("oxygenCalPercentageText").textContent = data.OxygenCalPercentage;
            document.getElementById("pureoxygenVoltage").textContent = data.pureoxygenVoltage;
            document.getElementById("heliumCalPercentage").textContent = data.HeliumCalPercentage;
            document.getElementById("heliumcalVoltage").textContent = data.heliumcalVoltage;
            document.getElementById("bestWiperValue").textContent = data.bestWiperValue;
          })
          .catch(error => {
            console.error("Error fetching /data:", error);
          });
      }, 500);
    };
  </script>
</body>
</html>
)rawliteral";

// HTML Setting Page
const char *settingsPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Settings</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <style>
    body { font-family: Arial, sans-serif; text-align: center; background-color: #f4f4f4; margin:0; padding:0; }
    .container { padding:20px; }
    h1 { margin:0; padding:20px; background:#333; color:#fff; }
    button { padding:10px 20px; margin:30px; font-size:20px; }
  </style>
</head>
<body>
  <h1>Setting</h1>
  <div class="container">
    <!-- Oxygen Calibrate High % -->
    <div>
      <button onclick="window.location.href='/oxygen_percentage'">
        Oxygen Calibrate High % 
      </button>
    </div>
    <!-- Helium Calibrate High % -->
    <div>
      <button onclick="window.location.href='/helium_percentage'">
        Helium Calibrate High % 
      </button>
    </div>
    <!-- Helium Polarity -->
    <div>
      <button onclick="window.location.href='/helium_polarity'">
        Helium Polarity
      </button>
    </div>
    <!-- Reset Calibration -->
    <div>
      <button onclick="if(confirm('Reset calibration to default?')) window.location.href='/reset_calibration';">
        Reset Calibration
      </button>
    </div>
    <!-- Firmware Update -->
    <div>
      <button onclick="window.location.href='/firmware'">
        Firmware Update
      </button>
    </div>
    <!-- Return to Main Page -->
    <div style="margin-top: 50px; text-align: center;">
      <button style="font-size: 18px; padding: 6px 14px;" onclick="window.location.href='/'">
        Return
      </button>
    </div>
  </div>
</body>
</html>
)rawliteral";

// Oxygen Percentage Setting Page
const char *oxygenPercentagePage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Oxygen Calibration Percentage</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <style>
    body { font-family: Arial, sans-serif; text-align: center; margin: 0; padding: 0; background-color: #f4f4f4; }
    .container { padding: 20px; }
    h1 { margin: 0; padding: 20px; background-color: #333; color: white; }
    .group { margin-top: 20px; padding: 10px; border: 1px solid #ddd; border-radius: 5px; background-color: #fff; }
    .info { font-size: 16px; margin: 10px 0; }
    label { display: inline-block; width: 200px; text-align: right; margin-right: 10px; white-space: nowrap; }
    input { padding: 5px; width: 40px; }
    button { padding: 10px 20px; margin-top: 20px; font-size: 16px; }
  </style>
</head>
<body>
  <h1>Oxygen Calibration Percentage</h1>
  <div class="container">
    <div class="group">
      <h2>Oxygen Calibration Percentage</h2>
      <form action="/save_oxygen" method="GET">
        <div class="info">
          <label for="OxygenCalPercentage">O2 Calibration Percentage:</label>
          <input type="number" id="OxygenCalPercentage" name="OxygenCalPercentage" min="21" max="99" value="99">
        </div>
        <div class="info">
          <button type="submit">Save</button>
        </div>
      </form>
    </div>
  </div>
</body>
</html>
)rawliteral";

// Helium Percentage Setting Page
const char *heliumPercentagePage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Helium Calibration Percentage</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <style>
    body { font-family: Arial, sans-serif; text-align: center; margin: 0; padding: 0; background-color: #f4f4f4; }
    .container { padding: 20px; }
    h1 { margin: 0; padding: 20px; background-color: #333; color: white; }
    .group { margin-top: 20px; padding: 10px; border: 1px solid #ddd; border-radius: 5px; background-color: #fff; }
    .info { font-size: 16px; margin: 10px 0; }
    label { display: inline-block; width: 200px; text-align: right; margin-right: 10px; white-space: nowrap; }
    input { padding: 5px; width: 40px; }
    button { padding: 10px 20px; margin-top: 20px; font-size: 16px; }
  </style>
</head>
<body>
  <h1>Helium Calibration Percentage</h1>
  <div class="container">
    <div class="group">
      <h2>Helium Calibration Percentage</h2>
      <form action="/save_helium" method="GET">
        <div class="info">
          <label for="HeliumCalPercentage">He Calibration Percentage:</label>
          <input type="number" id="HeliumCalPercentage" name="HeliumCalPercentage" min="30" max="99" value="99">
        </div>
        <div class="info">
          <button type="submit">Save</button>
        </div>
      </form>
    </div>
  </div>
</body>
</html>
)rawliteral";

// Helium Polarity Setting Page
const char *heliumPolarityPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Helium Polarity</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <style>
    body { font-family: Arial, sans-serif; text-align: center; margin: 0; padding: 0; background-color: #f4f4f4; }
    .container { padding: 20px; }
    h1 { margin: 0; padding: 20px; background-color: #333; color: white; }
    .group { margin-top: 20px; padding: 10px; border: 1px solid #ddd; border-radius: 5px; background-color: #fff; }
    .info { font-size: 16px; margin: 10px 0; }
    label { display: inline-block; width: 200px; text-align: right; margin-right: 10px; white-space: nowrap; }
    input { padding: 5px; width: 40px; }
    button { padding: 10px 20px; margin-top: 20px; font-size: 16px; }
  </style>
</head>
<body>
  <h1>Helium Polarity</h1>
  <div class="container">
    <div class="group">
      <h2>Helium Polarity</h2>
      <form action="/save_polarity" method="GET">
        <div class="info">
          <label for="HeliumPolarity">He Polarity:</label>
          <input type="number" id="HeliumPolarity" name="HeliumPolarity" min="0" max="1" value="0">
        </div>
        <div class="info">
          <button type="submit">Save</button>
        </div>
      </form>
    </div>
  </div>
</body>
</html>
)rawliteral";

// HTML Firmware Update Page
const char *firmwarePage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Firmware OTA</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <style>
    .progress-bar { width: 100%; background-color: #ddd; border-radius: 5px; overflow: hidden; margin-top: 10px; }
    .progress { height: 20px; width: 0%; background-color: #4caf50; transition: width 0.4s; }
  </style>
</head>
<body>
  <h1>Firmware Update</h1>
  <p style="font-size:16px; margin-bottom:12px;">
    Current version: )rawliteral" TOSTRING(FIRMWARE_VERSION) R"rawliteral(
  </p>
  <form id="uploadForm" method="POST" action="/update" enctype="multipart/form-data" onsubmit="return checkFile();">
    <input type="file" name="firmware" id="fileInput" accept=".bin" required>
    <div class="progress-bar"><div class="progress" id="progress"></div></div>
    <input type="submit" value="Upload">
    <p id="uploadStatus"></p>
  </form>
  <p><a href="/">Back</a></p>

  <script>
    document.getElementById("fileInput").addEventListener("change", function() {
      let fileName = this.files[0]?.name || "No file selected";
      document.getElementById("uploadStatus").textContent = "Selected: " + fileName;
    });
    function checkFile() {
      let fileInput = document.getElementById("fileInput");
      let file = fileInput.files[0];
      if (!file) {
        alert("Select a bin file.");
        return false;
      }
      if (file.name.split('.').pop().toLowerCase() !== "bin") {
        alert("Invalid file type.");
        return false;
      }
      if (file.size > 1800000) {
        alert("File size exceeded 1.8 MB.");
        return false;
      }
      uploadFirmware(file);
      return false;
    }
    function uploadFirmware(file) {
      let xhr = new XMLHttpRequest();
      let formData = new FormData();
      formData.append("firmware", file);

      xhr.upload.onprogress = function(event) {
        let percent = Math.round((event.loaded / event.total) * 100);
        document.getElementById("progress").style.width = percent + "%";
        document.getElementById("uploadStatus").textContent = "Uploading... " + percent + "%";
      };
      xhr.onload = function() {
        if (xhr.status === 200) {
          document.getElementById("uploadStatus").textContent = "Upload complete. Device rebooting...";
        } else if (xhr.status === 413) {
          document.getElementById("uploadStatus").textContent = "File too large! Upload failed.";
        } else {
          document.getElementById("uploadStatus").textContent = "Upload failed!";
        }
      };
      xhr.onerror = function() {
        document.getElementById("uploadStatus").textContent = "Upload error!";
      };
      xhr.open("POST", "/update", true);
      xhr.send(formData);
    }
  </script> 
</body>
</html>
)rawliteral";

// HTML App Icon Upload Page (no link in UI, must manual input URL)
const char *uploadPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Upload Icon</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
</head>
<body>
  <h1>Upload Icon</h1>
  <form id="iconForm" method="POST" action="/upload" enctype="multipart/form-data" onsubmit="return checkIcon();">
    <label for="iconInput">Choose a PNG (max 20KB):</label><br>
    <input type="file" name="upload" id="iconInput" accept=".png" required><br>
    <div id="iconStatus" class="info"></div>
    <input type="submit" value="Upload">
  </form>
  <p><a href="/">Return</a></p>

  <script>
    const iconInput = document.getElementById('iconInput');
    const iconStatus = document.getElementById('iconStatus');
    iconInput.addEventListener('change', () => {
      const file = iconInput.files[0];
      if (file) {
        const sizeKB = (file.size / 1024).toFixed(1);
        iconStatus.textContent = `Selected: ${file.name} (${sizeKB} KB)`;
      } else {
        iconStatus.textContent = '';
      }
    });
    function checkIcon() {
      const file = iconInput.files[0];
      if (!file) {
        alert('Please select a file.');
        return false;
      }
      const ext = file.name.split('.').pop().toLowerCase();
      if (!file.name.toLowerCase().endsWith('.png')) {
        alert('Invalid file type.');
        return false;
      }
      if (file.size > 20 * 1024) {
        alert('File size over 20 kB.');
        return false;
      }
      return true;
    }
  </script>
</body>
</html>
)rawliteral";

// Send data to client
void handleData() {
  String json = "{";
  json += "\"time\":\"" + formatTime() + "\",";
  json += "\"count\":\"" + String(avgSampleCount) + "\",";
  json += "\"oxygen\":\"" + String(getOxygenPercentage(), 1) + "\",";
  json += "\"helium\":\"" + String(heliumPercentage, 1) + "\",";
  json += "\"avgOxygenVoltage\":\"" + String(avgOxygenVoltage, 2) + "\",";
  json += "\"correctedHeliumVoltage\":\"" + String(correctedHeliumVoltage, 2) + "\",";
  json += "\"mod14\":\"" + String(mod14) + "\",";
  json += "\"mod16\":\"" + String(mod16) + "\",";
  json += "\"end\":\"" + String(end) + "\",";
  json += "\"density\":\"" + String(den, 1) + "\",";
  json += "\"voc\":\"" + String(voc) + "\",";
  json += "\"sgpRawCorr\":\"" + String(sgpRawCorr) + "\",";
  json += "\"oxygencalVoltage\":\"" + String(oxygencalVoltage, 2) + "\",";
  json += "\"OxygenCalPercentage\":\"" + String(OxygenCalPercentage) + "\",";
  json += "\"pureoxygenVoltage\":\"" + String(pureoxygenVoltage, 2) + "\",";
  json += "\"HeliumCalPercentage\":\"" + String(HeliumCalPercentage) + "\",";
  json += "\"heliumcalVoltage\":\"" + String(heliumcalVoltage, 2) + "\",";
  json += "\"bestWiperValue\":\"" + String(bestWiperValue) + "\"";
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
  sgp40.begin();                  // SGP40 start, default 0X59
  ds3502.begin();                 // DS3502 start, default 0X28
  ads.begin(0x48);                // ADS1115 start, default 0X48

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

    // Calculate END
    end = (mod14 + 10.0) * (1 - heliumPercentage / 100.0) - 10.0;  // END at MOD 1.4
    if (end < 0) {
      end = 0;  // Minimum END allowed 0 m
    }

    // Calculate Density
    den = (oxygenPercentage * 0.1756 - heliumPercentage * 1.0582 + 123.46) * (mod14 + 10) / 1000;  // Density at MOD 1.4
    if (den > 99.9) {
      den = 99.9;  // Maximum density allowed 99.9 g/L
    }

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
  }
}