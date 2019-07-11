#include <Arduino.h>
#include <ArduinoJson.h>
#include <MQTT.h>
extern String output;

void mPublish(MQTTClient client, String mqtt_pub_topic, String mess){
  const size_t capacity = JSON_ARRAY_SIZE(2) + JSON_OBJECT_SIZE(3);
  DynamicJsonDocument values(capacity);
  values["mess"] = String(mess);
  serializeJson(values, output);
  client.publish(mqtt_pub_topic, output);
}
