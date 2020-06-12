#include <Arduino.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

extern PubSubClient mClient;

void mqttpub(String topic, String subject, String mess, int size) {
  const size_t capacity = JSON_OBJECT_SIZE(size);
  DynamicJsonDocument status(capacity);
  String mqttMess;
  status[subject] = mess;
  serializeJson(status, mqttMess);
  mClient.publish((char*) topic.c_str(),(char*) mqttMess.c_str());
}