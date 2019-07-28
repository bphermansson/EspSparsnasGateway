 /*
  * https://github.com/bphermansson/EspSparsnasGateway
  *
  * Based on code from user Sommarlov @ EF: http://elektronikforumet.com/forum/viewtopic.php?f=2&t=85006&start=255#p1357610
  * Which in turn is based on Strigeus work: https://github.com/strigeus/sparsnas_decoder
  *
 */
#include "settings.h"
#include <Arduino.h>
#include <SPI.h>
#include <ESP8266WiFi.h>
#include <MQTT.h> // MQTT by Joel Gaehwiler
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
/*------------------------------*/
#include <RFM69registers.h>
#include <EspSparsnasGateway.h>
#include <RFM69functions.h>
#include <callback.h>
#include <reconn.h>
#include <mqttpub.h>

#define DEBUG 1
#define _interruptNum 5 // = ESP8266 GPIO5

WiFiClient wClient;
MQTTClient mClient;
const char* mqtt_status_topic = "EspSparsnasGateway/valuesV2";
const char* mqtt_debug_topic = "EspSparsnasGateway/debugV2";

// Make it possible to read Vcc from code
ADC_MODE(ADC_VCC);

unsigned long lastRecievedData = millis();
String mqttMess;
String freq, sendid;
uint32_t ifreq;
uint32_t isendid;
const char compile_date[] = __DATE__ " " __TIME__;

/* ----------------------------------------------------*/

void setup() {
  ifreq = FREQUENCY;
  isendid = SENSOR_ID;
  Serial.begin(SERIALSPEED);
  Serial.println("Welcome to " + String(appname));

  #ifdef DEBUG
     Serial.println(F("Debug on"));
     Serial.print (F("Vcc="));
     Serial.println(ESP.getVcc());
     Serial.print (F("Set up WiFi..."));
  #endif

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFISSID, WIFIPASSWORD);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println(F("WiFi connection Failed! Rebooting..."));
    delay(5000);
    ESP.restart();
  }
  WiFi.hostname(appname);

  // Setup Mqtt connection
  mClient.begin(mqtt_server, wClient);
  mClient.onMessage(callback);
  reconnect();

  mqttMess = "Welcome to EspSparsnasGateway, compiled at " + String(compile_date);
  mqttMess = mqttMess + ".\nMqtt topics: " + mqtt_status_topic + ", " + mqtt_debug_topic + "\nIP: " + WiFi.localIP()[0] + "." + WiFi.localIP()[1] + "." + WiFi.localIP()[2] + "." + WiFi.localIP()[3];
  #ifdef DEBUG
    Serial.println(mqttMess);
  #endif
  mqttpub(String(mqtt_debug_topic), "Device", mqttMess, mqttMess.length());

  // Hostname defaults to esp8266-[ChipID], change this
  ArduinoOTA.setHostname(appname);

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();

  mqttMess = "Over The Air programming enabled, port: " + String(appname);
  #ifdef DEBUG
    Serial.println(mqttMess);
  #endif
  mqttpub(String(mqtt_debug_topic), "Device", mqttMess, mqttMess.length());

  mqttMess = "Settings: \nSenderid: " + String(isendid) + "\nFrequency: " + String(ifreq);
    #ifdef DEBUG
    Serial.println(mqttMess);
  #endif
  mqttpub(String(mqtt_debug_topic), "Device", mqttMess, mqttMess.length());

  if (!initialize(ifreq)) {
    mqttMess =  "Unable to initialize the radio. Exiting.";
    #ifdef DEBUG
      Serial.println(mqttMess);
    #endif
    mqttpub(String(mqtt_debug_topic), "Radio", mqttMess, mqttMess.length());
    while (1) {
      yield();
    }
  }
  else {
    mqttMess =  "Radio initialized.\nListening on " + String(getFrequency()) + "hz. Done in setup.";
    #ifdef DEBUG
      Serial.println(mqttMess);
    #endif
    mqttpub(String(mqtt_debug_topic), "Radio", mqttMess, mqttMess.length());
  }
}

void loop() {
  ArduinoOTA.handle();
  mClient.loop();

  // Note! This routine is necessary, don't remove it!
  if (receiveDone()) {
  }

}
