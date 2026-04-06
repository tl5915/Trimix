#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <WebServer.h>

namespace WebPages {
inline bool sendHtmlFile(WebServer& server, const char* path) {
  File file = SPIFFS.open(path, FILE_READ);
  if (!file) {
    server.send(404, "text/plain", "Page not found");
    return false;
  }
  server.streamFile(file, "text/html");
  file.close();
  return true;
}

inline bool sendFirmwarePage(WebServer& server, const char* firmwareVersion) {
  File file = SPIFFS.open("/firmware.html", FILE_READ);
  if (!file) {
    server.send(404, "text/plain", "Page not found");
    return false;
  }
  String html = file.readString();
  file.close();
  html.replace("{{FIRMWARE_VERSION}}", firmwareVersion);
  server.send(200, "text/html", html);
  return true;
}
}
