#include <pgmspace.h>
#pragma once

const char settingsPage[] PROGMEM = R"HTML(
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
)HTML";