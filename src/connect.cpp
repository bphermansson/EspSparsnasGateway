#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "settings.h"

void connectWifi() {
  Serial.begin(SERIAL_SPEED);
  WiFi.persistent(false);  // Do not write Wifi settings to flash
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }
  WiFi.hostname(APPNAME);
}
