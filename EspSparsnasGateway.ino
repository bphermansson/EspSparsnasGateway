 /*
  * https://github.com/bphermansson/EspSparsnasGateway
  * 
  * Based on code from user Sommarlov @ EF: http://elektronikforumet.com/forum/viewtopic.php?f=2&t=85006&start=255#p1357610
  * Which in turn is based on Strigeus work: https://github.com/strigeus/sparsnas_decoder
  * 
 */

// Settings for the Mqtt broker:
#define MQTT_USERNAME "emonpi"     
#define MQTT_PASSWORD "emonpimqtt2016"  
const char* mqtt_server = "192.168.1.79";

// Wifi settings
const char* ssid = "NETGEAR83";
const char* password = "..........";

// Set this to the value of your energy meter
#define PULSES_PER_KWH 1000
// The code from the Sparnas tranmitter. Under the battery lid there's a sticker with digits like '400 643 654'.
// Set SENSOR_ID to the last six digits, ie '643654'.
// You can also set this later via Mqtt settings message, see docs.
#define SENSOR_ID 643654 

#define DEBUG 1

// You dont have to change anything below

char* mqtt_status_topic = "EspSparsnasGateway/values";
char* mqtt_debug_topic = "EspSparsnasGateway/debug";
char* mqtt_error_topic = "EspSparsnasGateway/error";

#define appname "EspSparsnasGateway"

const char compile_date[] = __DATE__ " " __TIME__;

// Sometimes you need to change how files are included:
// If the code doesnt compile, try to comment the row below and uncomment the next:
#include <RFM69registers.h>
//#include "RFM69registers.h"

#include <Arduino.h>
#include <SPI.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <EEPROM.h>

// Make it possible to read Vcc from code
ADC_MODE(ADC_VCC);

// OTA
#include <ArduinoOTA.h>

// Json
#include <ArduinoJson.h>

// Mqtt
#include <PubSubClient.h>
WiFiClient espClient;
PubSubClient client(espClient);

#define RF69_MODE_SLEEP 0      // XTAL OFF
#define RF69_MODE_STANDBY 1    // XTAL ON
#define RF69_MODE_SYNTH 2      // PLL ON
#define RF69_MODE_RX 3    // RX MODE
#define RF69_MODE_TX 4    // TX MODE

uint32_t FXOSC = 32000000;
uint32_t TwoPowerToNinteen = 524288; // 2^19
float RF69_FSTEP = (1.0 * FXOSC) / TwoPowerToNinteen; // p13 in datasheet
uint32_t FREQUENCY = 868000000;
uint16_t BITRATE = FXOSC / 40000; // 40kBps
uint16_t FREQUENCYDEVIATION = 10000 / RF69_FSTEP; // 10kHz
uint16_t SYNCVALUE = 0xd201;
uint8_t RSSITHRESHOLD = 0xE4; // must be set to dBm = (-Sensitivity / 2), default is 0xE4 = 228 so -114dBm
uint8_t PAYLOADLENGTH = 20;

#define _interruptNum 5

static volatile uint8_t DATA[21];
static volatile uint8_t TEMPDATA[21];
static volatile uint8_t DATALEN;
//static volatile uint16_t RSSI; // Most accurate RSSI during reception (closest to the reception)
static volatile uint8_t _mode;
static volatile bool inInterrupt = false; // Fake Mutex
uint8_t enc_key[5];
uint16_t rssi = 0;

uint16_t readRSSI();

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
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println(F("Connection Failed! Rebooting..."));
    delay(5000);
    ESP.restart();
  }
  WiFi.hostname(appname);

  // Setup Mqtt connection
  client.setServer(mqtt_server, 1883);
  if (!client.connected()) {
      reconnect();
  }

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

  // Publish some info, first via serial, then Mqtt
  Serial.println(F("Ready"));
  Serial.print(F("IP address: "));
  Serial.println(WiFi.localIP());

  IPAddress ip = WiFi.localIP();
  char buf[60];
  sprintf(buf, "%s @ IP:%d.%d.%d.%d SSID: %s", appname, WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3], ssid );
  Serial.println(buf);
  root["status"] = buf;
  root.printTo((char*)msg, root.measureLength() + 1);
  client.publish(mqtt_debug_topic, msg);
  
  // Calc encryption key, used for bytes 5-17  
  const uint32_t sensor_id_sub = SENSOR_ID - 0x5D38E8CB;
  enc_key[0] = (uint8_t)(sensor_id_sub >> 24);
  enc_key[1] = (uint8_t)(sensor_id_sub);
  enc_key[2] = (uint8_t)(sensor_id_sub >> 8);
  enc_key[3] = 0x47;
  enc_key[4] = (uint8_t)(sensor_id_sub >> 16);

  if (!initialize(FREQUENCY)) {
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

void interruptHandler() {
  
  if (inInterrupt) {
    //Serial.println("Already in interruptHandler.");
    return;
  }
  inInterrupt = true;

  if (_mode == RF69_MODE_RX && (readReg(REG_IRQFLAGS2) & RF_IRQFLAGS2_PAYLOADREADY)) {

    // Read Rssi
    char crssi[25];
    int16_t srssi;
    srssi = readRSSI();
    dtostrf(srssi, 16, 0, crssi);
    
    setMode(RF69_MODE_STANDBY);
    DATALEN = 0;
    select();

    // Init reading
    SPI.transfer(REG_FIFO & 0x7F);

    // Read 20 bytes
    for (uint8_t i = 0; i < 20; i++) {
      TEMPDATA[i] = SPI.transfer(0);
    }

    // CRC is done BEFORE decrypting message
    uint16_t crc = crc16(TEMPDATA, 18);
    uint16_t packet_crc = TEMPDATA[18] << 8 | TEMPDATA[19];

    #ifdef DEBUG
       Serial.println(F("Got rf data"));
    #endif

    // Decrypt message
    for (size_t i = 0; i < 13; i++) {
      TEMPDATA[5 + i] = TEMPDATA[5 + i] ^ enc_key[i % 5];
    }

    uint32_t rcv_sensor_id = TEMPDATA[5] << 24 | TEMPDATA[6] << 16 | TEMPDATA[7] << 8 | TEMPDATA[8];

    String output;

    // Bug fix from https://github.com/strigeus/sparsnas_decoder/pull/7/files
    // if (data_[0] != 0x11 || data_[1] != (SENSOR_ID & 0xFF) || data_[3] != 0x07 || rcv_sensor_id != SENSOR_ID) { 
    // if (TEMPDATA[0] != 0x11 || TEMPDATA[1] != (SENSOR_ID & 0xFF) || TEMPDATA[3] != 0x07 || TEMPDATA[4] != 0x0E || rcv_sensor_id != SENSOR_ID) {
    if (TEMPDATA[0] != 0x11 || TEMPDATA[1] != (SENSOR_ID & 0xFF) || TEMPDATA[3] != 0x07 || rcv_sensor_id != SENSOR_ID) {

      /*
      output = "Bad package: ";
      for (int i = 0; i < 18; i++) {
        output += codeLibrary.ToHex(TEMPDATA[i]) + " ";
      }
      Serial.println(output);
      */

    } else {
      /*
        0: uint8_t length;        // Always 0x11
        1: uint8_t sender_id_lo;  // Lowest byte of sender ID
        2: uint8_t unknown;       // Not sure
        3: uint8_t major_version; // Always 0x07 - the major version number of the sender.
        4: uint8_t minor_version; // Always 0x0E - the minor version number of the sender.
        5: uint32_t sender_id;    // ID of sender
        9: uint16_t time;         // Time in units of 15 seconds.
        11:uint16_t effect;       // Current effect usage
        13:uint32_t pulses;       // Total number of pulses
        17:uint8_t battery;       // Battery level, 0-100.
      */

      /*
      output = "";
      for (uint8_t i = 0; i < 20; i++) {
        output += codeLibrary.ToHex(TEMPDATA[i]) + " ";
      }
      Serial.println(output);
      */

      // Ref: https://github.com/strigeus/sparsnas_decoder
      int seq = (TEMPDATA[9] << 8 | TEMPDATA[10]);    // Time in units of 15 seconds.
      uint power = (TEMPDATA[11] << 8 | TEMPDATA[12]); // Current effect usage
      int pulse = (TEMPDATA[13] << 24 | TEMPDATA[14] << 16 | TEMPDATA[15] << 8 | TEMPDATA[16]); // Total number of pulses
      int battery = TEMPDATA[17]; // Battery level, 0-100.

      // This is how to convert the 'effect' field into Watt:
      // float watt =  (float)((3600000 / PULSES_PER_KWH) * 1024) / (effect);  ( 11:uint16_t effect;) This equals "power" in this code.      
      
      // Bug fix from https://github.com/strigeus/sparsnas_decoder/pull/7/files
      // float watt =  (float)((3600000 / PULSES_PER_KWH) * 1024) / (power);
      float watt = power * 24;
      int data4 = TEMPDATA[4]^0x0f;
      //  Note that data_[4] cycles between 0-3 when you first put in the batterys in t$
      if(data4 == 1){
           watt = (float)((3600000 / PULSES_PER_KWH) * 1024) / (power);
      } else if (data4 == 0) { // special mode for low power usage
           watt = power * 0.24 / PULSES_PER_KWH;
      }
      /* m += sprintf(m, "%5d: %7.1f W. %d.%.3d kWh. Batt %d%%. FreqErr: %.2f", seq, watt, pulse/PULSES_PER_KWH, pulse%PULSES_PER_KWH, battery, freq);
      'So in the example 10 % 3, 10 divided by 3 is 3 with remainder 1, so the answer is 1.'
      */
      #ifdef DEBUG
        // Print menory usage in debug mode
        int heap = ESP.getFreeHeap(); 
        Serial.print(F("Memory usage: ")); 
        Serial.println (heap);
      #endif
        
      // Prepare for output
      output = "Seq " + String(seq) + ": ";
      output += String(watt) + " W, total: ";
      output += String(pulse / PULSES_PER_KWH) + " kWh, battery ";
      output += String(battery) + "%, rssi ";
      output += String(srssi) + "dBm. Power(raw): ";
      output += String(power) + " ";
      output += (crc == packet_crc ? "" : "CRC ERR");
      String err = (crc == packet_crc ? "" : "CRC ERR");

      float vcc = ESP.getVcc();
      output += "Vcc: " + String(vcc) + "mV";     
      
      Serial.println(output);

      StaticJsonBuffer<150> jsonBuffer;
      JsonObject& root = jsonBuffer.createObject();
      char msg[150];
        
      if (err=="CRC ERR") {
        Serial.println(err);
        #ifdef DEBUG
          root["error"] = "CRC Error";
          root.printTo((char*)msg, root.measureLength() + 1);
          client.publish(mqtt_error_topic, msg);
        #endif
      }
      else {
        root["seq"] = seq;
        root["watt"] = float(watt);
        root["total"] = float(pulse / PULSES_PER_KWH);
        root["battery"] = battery;
        root["rssi"] = String(srssi);
        root["power"] = String(power);
        root["pulse"] = String(pulse);
        root.printTo((char*)msg, root.measureLength() + 1);
        client.publish(mqtt_status_topic, msg);
      }
    }
    unselect();
    setMode(RF69_MODE_RX);
  }
  inInterrupt = false;
}

void loop() {
  ArduinoOTA.handle();
  // Web firmware update
  //httpServer.handleClient();

  // Mqtt
  if (!client.connected()) {
    reconnect();
  }
  
  client.loop();
  /*
  if (receiveDone()) {
    // We never gets here!
    lastRecievedData = millis();
    // Send data to Mqtt server
    Serial.println(F("We got data to send."));
    client.publish(mqtt_debug_topic, "We got data to send.");
    // Wait a bit
    delay(500);
  }
  */
}

