/*
  * https://github.com/bphermansson/EspSparsnasGateway
  *
  * Based on code from user Sommarlov @ EF: http://elektronikforumet.com/forum/viewtopic.php?f=2&t=85006&start=255#p1357610
  * Which in turn is based on Strigeus work: https://github.com/strigeus/sparsnas_decoder
  *
 */
#include <Arduino.h>
#include <SPI.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#define ARDUINOJSON_USE_LONG_LONG 1 // https://arduinojson.org/v6/api/config/use_long_long/
#include <ArduinoJson.h>
#include <Ticker.h>
/*------------------------------*/
#include <RFM69registers.h>
#include <RFM69functions.h>
#include <reconn.h>
#include <mqttpub.h>
#include <ntp.h>
#include <settings.h>

WiFiClient wClient;
PubSubClient mClient(wClient);

static const char* mqtt_status_topic = MQTT_STATUS_TOPIC;
static const char* mqtt_debug_topic = MQTT_DEBUG_TOPIC;

// Make it possible to read Vcc from code
ADC_MODE(ADC_VCC);

Ticker blinkerRed;
Ticker blinkerGreen;

unsigned long lastRecievedData = millis();
String mqttMess;
String freq, sendid;
uint32_t ifreq;
uint32_t isendid;
const char compile_date[] = __DATE__ " " __TIME__;

void changeStateLED_RED();
void changeStateLED_GREEN();

/* ----------------------------------------------------*/

void setup() {
  ifreq = FREQUENCY;
  isendid = SENSOR_ID;
  Serial.begin(SERIALSPEED);
  Serial.println("Welcome to " + String(APPNAME));

  pinMode (LED_RED, OUTPUT);
  pinMode (LED_GREEN, OUTPUT);
  pinMode (LED_BLUE, OUTPUT);

  // Test leds
  analogWrite(LED_RED, LED_RED_BRIGHTNESS);
  delay(500);
  analogWrite(LED_GREEN, LED_GREEN_BRIGHTNESS);
  delay(500);
  analogWrite(LED_BLUE, LED_BLUE_BRIGHTNESS);
  delay(1500);

  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_BLUE, HIGH);
  delay(500);

  blinkerGreen.attach(0.5, changeStateLED_GREEN);

  #ifdef DEBUG
     Serial.println(F("Debug on"));
     Serial.print (F("Vcc="));
     Serial.println(ESP.getVcc());
     Serial.println (F("Set up WiFi..."));
  #endif

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFISSID, WIFIPASSWORD);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {

    Serial.println(F("Connecting to " WIFISSID "..."));
    Serial.println(F("Connection failed, check your settings!"));
    Serial.println(F("Rebooting..."));

    blinkerRed.attach(0.5, changeStateLED_RED);
    delay(5000);
    ESP.restart();
  }

  WiFi.hostname(APPNAME);

  // Setup Mqtt connection
  mClient.setServer(MQTT_SERVER, MQTT_PORT);
  reconnect();

  mqttMess = "Welcome to EspSparsnasGateway, compiled at " + String(compile_date);
  mqttMess = mqttMess + ".\nMqtt topics: " + mqtt_status_topic + ", " + mqtt_debug_topic + "\nIP: " + WiFi.localIP()[0] + "." + WiFi.localIP()[1] + "." + WiFi.localIP()[2] + "." + WiFi.localIP()[3];
  #ifdef DEBUG
    Serial.println(mqttMess);
    Serial.println(mqtt_status_topic);
  #endif
  mqttpub(String(mqtt_debug_topic), "Device", mqttMess, mqttMess.length());

  // Hostname defaults to esp8266-[ChipID], change this
  ArduinoOTA.setHostname(APPNAME);

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

  setup_NTP();

  mqttMess = "Over The Air programming enabled, port: " + String(APPNAME);
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
    blinkerRed.attach(0.3, changeStateLED_RED);
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

// All ok
  blinkerGreen.detach();
  analogWrite(LED_GREEN, LED_GREEN_BRIGHTNESS);
}

void loop() {
  ArduinoOTA.handle();
  if (!mClient.connected()) {
    reconnect();
  }
  mClient.loop();
  //delay(10);
  // Note! This routine is necessary, don't remove it!
  if (receiveDone()) {
  }
}
void changeStateLED_RED()
{
  digitalWrite(LED_RED, !(digitalRead(LED_RED)));  //Invert Current State of LED
}
void changeStateLED_GREEN()
{
  digitalWrite(LED_GREEN, !(digitalRead(LED_GREEN)));  //Invert Current State of LED
}
