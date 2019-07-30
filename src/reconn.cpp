#include <Arduino.h>
#include <MQTT.h> // MQTT by Joel Gaehwiler
extern MQTTClient mClient;
#include <settings.h>

void reconnect() {
  #ifdef DEBUG
    Serial.print("Reconnect");
  #endif
  // Loop until we're reconnected
  while (!mClient.connected()) {
    #ifdef DEBUG
      Serial.print("Attempting MQTT connection...");
    #endif
    if (mClient.connect(APPNAME, MQTT_USERNAME, MQTT_PASSWORD)) {
      #ifdef DEBUG
        String buf = "Connected to Mqtt broker as " + String(APPNAME);
        Serial.println(buf);
      #endif

    } else {
      Serial.print("Mqtt connection failed,");
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}
