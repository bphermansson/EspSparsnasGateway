#include <Arduino.h>
#include "time.h"
//#include "settings.h"

void setup_NTP() {
  configTime(GMTOFFSET, DAYLIGHTOFFSET, NTPSERVERNAME);
  Serial.println("\nWaiting for time");
  while (!time(nullptr))
  {
    Serial.print(".");
    delay(1000);
  }
}
