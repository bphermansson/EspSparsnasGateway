#include <Arduino.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "settings.h"

extern PubSubClient mClient;
const String sensor_id = String(SENSOR_ID);
const String availability_topic = APPNAME "/" + sensor_id + "/availability";
const String state_topic = APPNAME "/" + sensor_id + "/state";
const String discovery_topic = "home/sensor/" APPNAME "_" + sensor_id;

bool send_discovery_message(const char* measurement, const char* value_template) {
  DynamicJsonDocument device(JSON_OBJECT_SIZE(12));
  DynamicJsonDocument config(JSON_OBJECT_SIZE(30));

  device["sw_version"] = __TIME__ " " __DATE__;
  device["name"] = String(APPNAME " ") + sensor_id;
  device["identifiers"] = String(APPNAME "") + sensor_id;
  device["model"] = APPNAME;
  device["manufacturer"] = "IKEA";

  config["device"] = device;
  config["device_class"] = "power";
  config["unit_of_measurement"] = measurement;
  config["name"] = String("ESP Sparsnäs ") + measurement;
  config["unique_id"] = (APPNAME "_") + sensor_id + "_" + measurement;
  config["state_topic"] = state_topic;
  config["json_attributes_topic"] = state_topic;
  config["availability_topic"] = availability_topic.c_str();
  config["value_template"] = value_template;

  String ret;
  serializeJson(config, ret);
  //Serial.print("Discovery message ");
  //Serial.print(strlen(ret.c_str()));
  //Serial.println(ret.c_str());

  mClient.beginPublish(
    (String(discovery_topic) + "/" + measurement + "/config").c_str(),
    ret.length(),
    true
  );
  for (uint i=0; i<ret.length(); i+=64) {
    mClient.print(ret.substring(i, i+64));
  }
  mClient.endPublish();
  return true;
}

void publish_mqtt(String topic, String message) {
  Serial.print("Will publish:");
  Serial.println(message);
  mClient.beginPublish(
    (topic).c_str(),
    message.length(),
    false
  );
  for (uint i=0; i<message.length(); i+=64) {
    mClient.print(message.substring(i, i+64));
  }
  mClient.endPublish();
}

void reconnect() {
  #ifdef DEBUG
    Serial.print("Reconnect");
  #endif
  // Loop until we're reconnected
  while (!mClient.connected()) {
    #ifdef DEBUG
      Serial.print("Attempting MQTT connection...");
    #endif
    if (mClient.connect(APPNAME, MQTT_USERNAME, MQTT_PASSWORD, availability_topic.c_str(), 0, true, "offline")) {
      #ifdef DEBUG
        String buf = "Connected to Mqtt broker as " + String(APPNAME);
        Serial.println(buf);
      #endif

      send_discovery_message("W", "{{ value_json.watt | round(1) }}");
      send_discovery_message("kWh", "{{ value_json.total | round(1) }}");

      mClient.publish(availability_topic.c_str(), "online", true);

      return;
    } else {
      Serial.print("Mqtt connection failed,");
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}
