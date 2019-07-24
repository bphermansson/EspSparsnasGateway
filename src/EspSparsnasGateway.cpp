 /*
  * https://github.com/bphermansson/EspSparsnasGateway
  *
  * Based on code from user Sommarlov @ EF: http://elektronikforumet.com/forum/viewtopic.php?f=2&t=85006&start=255#p1357610
  * Which in turn is based on Strigeus work: https://github.com/strigeus/sparsnas_decoder
  *
 */

#include "settings.h"

#define DEBUG 1

// You dont have to change anything below

const char compile_date[] = __DATE__ " " __TIME__;

// Sometimes you need to change how files are included:
// If the code doesnt compile, try to comment the row below and uncomment the next:
#include <RFM69registers.h>
#include <EspSparsnasGateway.h>

#include <Arduino.h>
#include <SPI.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <EEPROM.h>

#include <RFM69functions.h>

void setMode(uint8_t newMode);

// Make it possible to read Vcc from code
ADC_MODE(ADC_VCC);

// OTA
#include <ArduinoOTA.h>
#include <ArduinoJson.h>

// Mqtt
#include <PubSubClient.h>
WiFiClient espClient;
PubSubClient client(espClient);
const char* mqtt_status_topic = "EspSparsnasGateway/values";
const char* mqtt_debug_topic = "EspSparsnasGateway/debug";
const char* mqtt_sub_topic_freq = "EspSparsnasGateway/settings/frequency";
const char* mqtt_sub_topic_senderid = "EspSparsnasGateway/settings/senderid";
const char* mqtt_sub_topic_clear = "EspSparsnasGateway/settings/clear";
const char* mqtt_sub_topic_reset = "EspSparsnasGateway/settings/reset";

#define _interruptNum 5
void  ICACHE_RAM_ATTR interruptHandler();

unsigned long lastRecievedData = millis();

// ----------------------------------------------------

void setup() {
  StaticJsonBuffer<150> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  char msg[150];
  Serial.begin(115200);
  Serial.println(F("Welcome to EspSparsnasGateway"));
  Serial.print (F("Compiled at:"));
  Serial.println(compile_date);
  #ifdef DEBUG
     Serial.println(F("Debug on"));
     Serial.print (F("Vcc="));
     Serial.println(ESP.getVcc());
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
//  client.setServer(mqtt_server, 1883);
//  client.setCallback(callback); // What to do when a Mqtt message arrives
  //client.subscribe(mqtt_sub_topic_freq);
  //client.subscribe(mqtt_sub_topic_senderid);
/*  if (!client.connected()) {
      reconnect();
  }*/

  // Enable Eeprom for permanent storage
  EEPROM.begin(512);
  bool storedValues;
  // Read stored values
  String freq, sendid;
  uint32_t ifreq;
  uint32_t isendid;

  // Read stored config status. Write this bit when storing settings
  char savedSettingFreq = char(EEPROM.read(0));
  char savedSettingSenderid = char(EEPROM.read(1));
/*
  root["savedFrequency"] = savedSettingFreq;
  root["savedSenderid"] = savedSettingSenderid;
  root.printTo((char*)msg, root.measureLength() + 1);
  client.publish(mqtt_debug_topic, msg);
*/
  #ifdef DEBUG
    Serial.println("Stored data: ");
    if (savedSettingFreq == 1) {
      Serial.println("Found a stored frequency");
    }
    if (savedSettingSenderid == 1){
      Serial.println("Found a stored senderid");
    }
  #endif

  if (savedSettingFreq==1) {
     //Serial.println("Frequency stored");
     // Read them
     for (int i = 2; i < 8; ++i)
     {
        char curC = char(EEPROM.read(i));
        freq += curC;
     }

     #ifdef DEBUG
       Serial.print("Stored frequency: ");
       Serial.println(freq);
     #endif
     // Adjust to real value 868000000
     // ifreq is a value like '868.00'
     freq.trim();
     String lft = freq.substring(0,3);
     String rgt = freq.substring(4,6);
     String tot = lft + rgt;
     //Serial.println(lft);
     //Serial.println(rgt);
     //Serial.println(tot);

     ifreq=tot.toInt();
     //Serial.println(tot);

     ifreq=ifreq*10000;
     #ifdef DEBUG
      Serial.print("Calculated frequency: ");
      Serial.println(ifreq);
     #endif
  }
  else {
    #ifdef DEBUG
     Serial.println("There is no stored frequency, using default value");
    #endif

    root["status"] = "There is no stored frequency, using default value";
    root.printTo((char*)msg, root.measureLength() + 1);
    client.publish(mqtt_debug_topic, msg);

    // Use default setting
    ifreq = FREQUENCY;
  }
  if (savedSettingSenderid==1) {
     for (int i = 12; i < 18; ++i)
     {
        char curC = char(EEPROM.read(i));
        sendid += curC;
     }
     #ifdef DEBUG
       Serial.print("Stored sender id: ");
       Serial.println(sendid);
     #endif
      isendid = sendid.toInt();

  }
  else {
    Serial.println("There is no stored senderid, using default value");
    root["status"] = "There is no stored senderid, using default value";
    root.printTo((char*)msg, root.measureLength() + 1);
    client.publish(mqtt_debug_topic, msg);
    isendid = SENSOR_ID;
  }

  Serial.print ("Senderid: ");
  Serial.println(isendid);
  Serial.print ("Frequency: ");
  Serial.println(ifreq);
  Serial.print ("RF69_FSTEP: ");
  //Serial.println(RF69_FSTEP);

  /*
  root["Frequency"] = freq;
  root["Senderid"] = isendid;
  root.printTo((char*)msg, root.measureLength() + 1);
  client.publish(mqtt_debug_topic, msg);
  */
  // Hostname defaults to esp8266-[ChipID]
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
  #ifdef DEBUG
    Serial.print("Over The Air programming enabled, port: ");
    Serial.println(appname);
  #endif
  // Web firmware update
/*
  MDNS.begin(host);
  httpUpdater.setup(&httpServer);
  httpServer.begin();
  MDNS.addService("http", "tcp", 80);
*/

  // Publish some info, first via serial, then Mqtt
  Serial.println(F("Ready"));
  Serial.print(F("IP address: "));
  Serial.println(WiFi.localIP());

  IPAddress ip = WiFi.localIP();
  char buf[60];
  sprintf(buf, "%s @ IP:%d.%d.%d.%d SSID: %s", appname, WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3], WIFISSID );
  Serial.println(buf);
  root["status"] = buf;
  root.printTo((char*)msg, root.measureLength() + 1);
  client.publish(mqtt_debug_topic, msg);

  // Calc encryption key, used for bytes 5-17
  //Serial.println(SENSOR_ID);
  //Serial.println(isendid);

  //const uint32_t sensor_id_sub = SENSOR_ID - 0x5D38E8CB;
  const uint32_t sensor_id_sub = isendid - 0x5D38E8CB;
  //const uint32_t sensor_id_subtest = isendid - 0x5D38E8CB;


/*
  Serial.print("Enc key: ");
  for (int y=0;y<5;y++) {
    Serial.println(enc_key[y]);
  }
*/
  //uint32_t iFreq = freq.toInt();
  /*
  #ifdef DEBUG
    Serial.print("Stored freq: ");
    Serial.println(iFreq);
  #endif
  */
//  ifreq = 868100000; Closed, see ticket 28
  if (!initialize(ifreq)) {
    char mess[ ] = "Unable to initialize the radio. Exiting.";
    Serial.println(mess);

    StaticJsonBuffer<150> jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();
    char msg[150];
    root["status"] = mess;
    root.printTo((char*)msg, root.measureLength() + 1);
    client.publish(mqtt_debug_topic, msg);

    while (1) {
      yield();
    }
  }
  else {
    #ifdef DEBUG
       Serial.println(F("Radio initialized."));
    #endif
  }
  #ifdef DEBUG
    String temp = "Listening on " + String(getFrequency()) + "hz. Done in setup.";
    Serial.println(temp);

    root["status"] = temp;
    root.printTo((char*)msg, root.measureLength() + 1);
    client.publish(mqtt_debug_topic, msg);
  #endif
}

unsigned long lastClientLoop = millis();

void loop() {
  ArduinoOTA.handle();
  // Web firmware update
  //httpServer.handleClient();

  // Mqtt
  /*
  if (!client.connected()) {
    reconnect();
  }
*/

/*
  if (millis() - lastClientLoop >= 2500) {
#ifdef DEBUG
    Serial.println("client.loop");
#endif
    client.loop();
    lastClientLoop = millis();
  }
*/
  /*String temp = String(millis());
  char mess[20];
  temp.toCharArray(mess,20);
  client.publish(mqtt_status_topic, mess);
  */
  if (receiveDone()) {
    // We never gets here!
    lastRecievedData = millis();
    // Send data to Mqtt server
    Serial.println(F("We got data to send."));
    client.publish(mqtt_debug_topic, "We got data to send.");

    // Wait a bit
    delay(500);
  }
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(appname, MQTT_USERNAME, MQTT_PASSWORD)) {
      client.subscribe(mqtt_sub_topic_freq);
      client.subscribe(mqtt_sub_topic_senderid);
      client.subscribe(mqtt_sub_topic_clear);
      client.subscribe(mqtt_sub_topic_reset);
      #ifdef DEBUG
        String temp = "Connected to Mqtt broker as " + String(appname);
        Serial.println(temp);
        StaticJsonBuffer<150> jsonBuffer;
        JsonObject& root = jsonBuffer.createObject();
        char msg[150];
        root["status"] = temp;
        root.printTo((char*)msg, root.measureLength() + 1);
        client.publish(mqtt_debug_topic, msg);
      #endif

    } else {
      Serial.print("Mqtt connection failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}
