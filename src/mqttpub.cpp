#include <Arduino.h>
#include <MQTT.h> // MQTT by Joel Gaehwiler
#include <ArduinoJson.h>
extern MQTTClient mClient;

bool mqttpub(String topic, String subject, String mess, int size) {
  const size_t capacity = JSON_OBJECT_SIZE(size);
  DynamicJsonDocument status(capacity);
  String mqttMess;
  status[subject] = mess;
  serializeJson(status, mqttMess);
  return mClient.publish(topic, mqttMess);
}
