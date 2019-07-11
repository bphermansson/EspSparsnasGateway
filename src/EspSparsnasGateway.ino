 /*
  * https://github.com/bphermansson/EspSparsnasGateway
  *
  * Based on code from user Sommarlov @ EF: http://elektronikforumet.com/forum/viewtopic.php?f=2&t=85006&start=255#p1357610
  * Which in turn is based on Strigeus work: https://github.com/strigeus/sparsnas_decoder
  *
 */

 // Sometimes you need to change how files are included:
 // If the code doesnt compile, try to comment the row below and uncomment the next:
 //#include <RFM69registers.h>
 //#include "RFM69registers.h"

 #include <Arduino.h>
 #include <SPI.h>
 #include <ESP8266WiFi.h>
 #include <ESP8266mDNS.h>
 #include <WiFiUdp.h>
 #include <EEPROM.h>
 #include <ArduinoJson.h>
 #include <MQTT.h> // MQTT by Joel Gaehwiler

 #include "EspSparsnasGateway.h"
 #include "settings.h"
 #include "mqttPublish.h"
 #include "connect.h"
 #include "ota.h"

// Make it possible to read Vcc from code
ADC_MODE(ADC_VCC);
MQTTClient client;
WiFiClient espClient;

unsigned long lastRecievedData = millis();

// Variables for Mqtt
const size_t capacity = JSON_ARRAY_SIZE(2) + JSON_OBJECT_SIZE(3);
DynamicJsonDocument values(capacity);

// ----------------------------------------------------

void setup() {
  Serial.begin(SERIAL_SPEED);
  Serial.println(F("Welcome to EspSparsnasGateway"));
  Serial.print (F("Compiled at:"));
  Serial.println(compile_date);
  #ifdef DEBUG
     Serial.println(F("Debug on"));
     Serial.print (F("Vcc="));
     Serial.println(ESP.getVcc());
  #endif

// Wi-Fi
  Serial.println("Connect to Wi-Fi...");
  connectWifi();
  mess = "Connected to Wi-Fi";
  Serial.println(mess);
  values["mess"] = String(mess);
  serializeJson(values, output);
  client.publish(MQTT_PUB_TOPIC, output);

// Mqtt
  if (!client.connected()) {
    Serial.println("Connect to Mqtt broker...");
    client.begin(MQTT_SERVER, espClient);
    while (!client.connect(APPNAME, MQTT_USERNAME, MQTT_PASSWORD)) {
      Serial.print(".");
      delay(1000);
    }
  mess = "Connected to MQTT broker!";
  Serial.println(mess);
  values["mess"] = String(mess);
  serializeJson(values, output);
  client.publish(MQTT_PUB_TOPIC, output);
  }

  IPAddress ip = WiFi.localIP();
  char buf[60];
  sprintf(buf, "%s @ IP:%d.%d.%d.%d SSID: %s", APPNAME, WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3], WIFI_SSID );
  Serial.println(buf);
  values["mess"] = String(buf);
  serializeJson(values, output);
  client.publish(MQTT_PUB_TOPIC, output);

  String freq, sendid;
  uint32_t ifreq;
  ifreq = FREQUENCY;

  Serial.print ("Frequency: ");
  Serial.println(ifreq);
  Serial.print ("RF69_FSTEP: ");
  Serial.println(RF69_FSTEP);


  const uint32_t sensor_id_sub = SENSOR_ID - 0x5D38E8CB;
  enc_key[0] = (uint8_t)(sensor_id_sub >> 24);
  enc_key[1] = (uint8_t)(sensor_id_sub);
  enc_key[2] = (uint8_t)(sensor_id_sub >> 8);
  enc_key[3] = 0x47;
  enc_key[4] = (uint8_t)(sensor_id_sub >> 16);
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
  if (!initialize(FREQUENCY
  )) {
    char mess[ ] = "Unable to initialize the radio. Exiting.";
    Serial.println(mess);
    mPublish(client, mqtt_pub_topic, mess);

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
/*
    root["status"] = temp;
    root.printTo((char*)msg, root.measureLength() + 1);
    client.publish(mqtt_debug_topic, msg);
*/
  #endif
}







unsigned long lastClientLoop = millis();

void loop() {
  aotaHandle();
  // Web firmware updateaotaHandle()
  //httpServer.handleClient();

  // Mqtt
  client.loop();
  if (!client.connected()) {
    reconnect();
  }
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
    if (client.connect(APPNAME, MQTT_USERNAME, MQTT_PASSWORD)) {
      client.subscribe(mqtt_sub_topic_freq);
      client.subscribe(mqtt_sub_topic_senderid);
      client.subscribe(mqtt_sub_topic_clear);
      client.subscribe(mqtt_sub_topic_reset);
      #ifdef DEBUG
        String temp = "Connected to Mqtt broker as " + String(APPNAME);
        Serial.println(temp);

    /*
        StaticJsonBuffer<150> jsonBuffer;
        JsonObject& root = jsonBuffer.createObject();
        char msg[150];
        root["status"] = temp;
        root.printTo((char*)msg, root.measureLength() + 1);
        client.publish(mqtt_debug_topic, msg);
        */
      #endif

    } else {
      Serial.print("Mqtt connection failed, rc=");
    //  Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}
