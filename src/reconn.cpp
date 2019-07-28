#include <Arduino.h>
#include <MQTT.h> // MQTT by Joel Gaehwiler
extern MQTTClient mClient;
#include "settings.h"

void reconnect() {
  // Loop until we're reconnected
  while (!mClient.connected()) {
    #ifdef DEBUG
      Serial.print("Attempting MQTT connection...");
    #endif
    // Attempt to connect
    if (mClient.connect(appname, MQTT_USERNAME, MQTT_PASSWORD)) {
      /*
      mClient.subscribe(mqtt_sub_topic_freq);
      mClient.subscribe(mqtt_sub_topic_senderid);
      mClient.subscribe(mqtt_sub_topic_clear);
      mClient.subscribe(mqtt_sub_topic_reset);
      */
      #ifdef DEBUG
        String buf = "Connected to Mqtt broker as " + String(appname);
        Serial.println(buf);
/*
        const size_t capacity = JSON_OBJECT_SIZE(1);
        DynamicJsonDocument status(capacity);
        status["Mqtt"] = buf;
        serializeJson(status, String mqttMess);
        root.printTo((char*)msg, status.measureLength() + 1);
        mClient.publish(mqtt_status_topic, msg);
*/
      #endif

    } else {
      Serial.print("Mqtt connection failed,");
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}
