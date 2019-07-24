 /*
  * https://github.com/bphermansson/EspSparsnasGateway
  *
  * Based on code from user Sommarlov @ EF: http://elektronikforumet.com/forum/viewtopic.php?f=2&t=85006&start=255#p1357610
  * Which in turn is based on Strigeus work: https://github.com/strigeus/sparsnas_decoder
  *
 */

#include "settings.h"
#include "interrupt.h"

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

uint32_t FXOSC = 32000000;
uint32_t TwoPowerToNinteen = 524288; // 2^19
float RF69_FSTEP = (1.0 * FXOSC) / TwoPowerToNinteen; // p13 in datasheet
uint16_t BITRATE = FXOSC / 40000; // 40kBps
uint16_t FREQUENCYDEVIATION = 10000 / RF69_FSTEP; // 10kHz
uint16_t SYNCVALUE = 0xd201;
uint8_t RSSITHRESHOLD = 0xE4; // must be set to dBm = (-Sensitivity / 2), default is 0xE4 = 228 so -114dBm
uint8_t PAYLOADLENGTH = 20;

static volatile uint8_t _mode;

#define _interruptNum 5
void  ICACHE_RAM_ATTR interruptHandler();

//static volatile uint16_t RSSI; // Most accurate RSSI during reception (closest to the reception)
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
    Serial.println(F("WiFi connection Failed! Rebooting..."));
    delay(5000);
    ESP.restart();
  }
  WiFi.hostname(appname);

  // Setup Mqtt connection
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback); // What to do when a Mqtt message arrives
  //client.subscribe(mqtt_sub_topic_freq);
  //client.subscribe(mqtt_sub_topic_senderid);
  if (!client.connected()) {
      reconnect();
  }

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
  Serial.println(RF69_FSTEP);

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
  sprintf(buf, "%s @ IP:%d.%d.%d.%d SSID: %s", appname, WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3], ssid );
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

bool initialize(uint32_t frequency) {
  Serial.print("In initialize, frequency = ");
  Serial.println(frequency);
  frequency = frequency / RF69_FSTEP;
  Serial.print("Adjusted freq: ");  // Adjusted freq: 14221312
  Serial.println(frequency);
  Serial.print("RF69_FSTEP: ");
  Serial.println(RF69_FSTEP);

  const uint8_t CONFIG[][2] = {
    /* 0x01 */ {REG_OPMODE, RF_OPMODE_SEQUENCER_ON | RF_OPMODE_LISTEN_OFF | RF_OPMODE_STANDBY},
    /* 0x02 */ {REG_DATAMODUL, RF_DATAMODUL_DATAMODE_PACKET | RF_DATAMODUL_MODULATIONTYPE_FSK | RF_DATAMODUL_MODULATIONSHAPING_01},
    /* 0x03 */ {REG_BITRATEMSB, (uint8_t)(BITRATE >> 8)},
    /* 0x04 */ {REG_BITRATELSB, (uint8_t)(BITRATE)},
    /* 0x05 */ {REG_FDEVMSB, (uint8_t)(FREQUENCYDEVIATION >> 8)},
    /* 0x06 */ {REG_FDEVLSB, (uint8_t)(FREQUENCYDEVIATION)},
    /* 0x07 */ {REG_FRFMSB, (uint8_t)(frequency >> 16)},
    /* 0x08 */ {REG_FRFMID, (uint8_t)(frequency >> 8)},
    /* 0x09 */ {REG_FRFLSB, (uint8_t)(frequency)},
    /* 0x19 */ {REG_RXBW, RF_RXBW_DCCFREQ_010 | RF_RXBW_MANT_16 | RF_RXBW_EXP_4}, // p26 in datasheet, filters out noise
    /* 0x25 */ {REG_DIOMAPPING1, RF_DIOMAPPING1_DIO0_01},              // PayloadReady
    /* 0x26 */ {REG_DIOMAPPING2, RF_DIOMAPPING2_CLKOUT_OFF},          // DIO5 ClkOut disable for power saving
    /* 0x28 */ {REG_IRQFLAGS2, RF_IRQFLAGS2_FIFOOVERRUN},              // writing to this bit ensures that the FIFO & status flags are reset
    /* 0x29 */ {REG_RSSITHRESH, RSSITHRESHOLD},
    /* 0x2B */ {REG_RXTIMEOUT2, (uint8_t)0x00}, // RegRxTimeout2 (0x2B) interrupt is generated TimeoutRssiThresh *16*T bit after Rssi interrupt if PayloadReady interrupt doesn’t occur.
    /* 0x2D */ {REG_PREAMBLELSB, 3}, // default 3 preamble bytes 0xAAAAAA
    /* 0x2E */ {REG_SYNCCONFIG, RF_SYNC_ON | RF_SYNC_FIFOFILL_AUTO | RF_SYNC_SIZE_2 | RF_SYNC_TOL_0},
    /* 0x2F */ {REG_SYNCVALUE1, (uint8_t)(SYNCVALUE >> 8)},
    /* 0x30 */ {REG_SYNCVALUE2, (uint8_t)(SYNCVALUE)},
    /* 0x37 */ {REG_PACKETCONFIG1, RF_PACKET1_FORMAT_FIXED | RF_PACKET1_DCFREE_OFF | RF_PACKET1_CRC_OFF | RF_PACKET1_CRCAUTOCLEAR_ON | RF_PACKET1_ADRSFILTERING_OFF},
    /* 0x38 */ {REG_PAYLOADLENGTH, PAYLOADLENGTH},
    /* 0x3C */ {REG_FIFOTHRESH, RF_FIFOTHRESH_TXSTART_FIFONOTEMPTY | RF_FIFOTHRESH_VALUE},               // TX on FIFO not empty
    /* 0x3D */ {REG_PACKETCONFIG2, RF_PACKET2_RXRESTARTDELAY_2BITS | RF_PACKET2_AUTORXRESTART_ON | RF_PACKET2_AES_OFF}, // RXRESTARTDELAY must match transmitter PA ramp-down time (bitrate dependent)
    /* 0x6F */ {REG_TESTDAGC, RF_DAGC_IMPROVED_LOWBETA0}, // run DAGC continuously in RX mode for Fading Margin Improvement, recommended default for AfcLowBetaOn=0
    {255, 0}
  };

  digitalWrite(SS, HIGH);
  pinMode(SS, OUTPUT);
  SPI.begin();
  SPI.setDataMode(SPI_MODE0);
  SPI.setBitOrder(MSBFIRST);
  // decided to slow down from DIV2 after SPI stalling in some instances,
  // especially visible on mega1284p when RFM69 and FLASH chip both present
  SPI.setClockDivider(SPI_CLOCK_DIV4);

  unsigned long start = millis();
  uint8_t timeout = 50;
  do {
    writeReg(REG_SYNCVALUE1, 0xAA);
    yield();
  } while (readReg(REG_SYNCVALUE1) != 0xaa && millis() - start < timeout);
  if (readReg(REG_SYNCVALUE1) != 0xaa) {
    Serial.println("ERROR: Failed setting syncvalue1 1st time");
    return false;
  }
  start = millis();
  do {
    writeReg(REG_SYNCVALUE1, 0x55);
    yield();
  } while (readReg(REG_SYNCVALUE1) != 0x55 && millis() - start < timeout);
  if (readReg(REG_SYNCVALUE1) != 0x55) {
    Serial.println("ERROR: Failed setting syncvalue1 2nd time");
    return false;
  }
  for (uint8_t i = 0; CONFIG[i][0] != 255; i++) {
    writeReg(CONFIG[i][0], CONFIG[i][1]);
    yield();
  }

  setMode(RF69_MODE_STANDBY);
  start = millis();
  while (((readReg(REG_IRQFLAGS1) & RF_IRQFLAGS1_MODEREADY) == 0x00) && millis() - start < timeout) {
    // wait for ModeReady
    //codeLibrary.wait(1);
    delay(1);
  }
  if (millis() - start >= timeout) {
    #ifdef DEBUG
      char mess[ ] = "Failed on waiting for ModeReady()";
      Serial.println(mess);

      StaticJsonBuffer<150> jsonBuffer;
      JsonObject& root = jsonBuffer.createObject();
      char msg[150];
      root["status"] = mess;
      root.printTo((char*)msg, root.measureLength() + 1);
      client.publish(mqtt_debug_topic, msg);

    #endif
    return false;
  }
  attachInterrupt(_interruptNum, interruptHandler, RISING);

  #ifdef DEBUG
    char mess[ ] = "RFM69 init done";
    Serial.println(mess);

    StaticJsonBuffer<150> jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();
    char msg[150];
    root["status"] = mess;
    root.printTo((char*)msg, root.measureLength() + 1);
    client.publish(mqtt_debug_topic, msg);
  #endif
  return true;
}


void setMode(uint8_t newMode) {
  #ifdef DEBUG
     //Serial.println(F("In setMode"));
  #endif

  if (newMode == _mode) {
    return;
  }

  uint8_t val = readReg(REG_OPMODE);
  switch (newMode) {
    case RF69_MODE_TX:
      writeReg(REG_OPMODE, (val & 0xE3) | RF_OPMODE_TRANSMITTER);
      break;
    case RF69_MODE_RX:
      writeReg(REG_OPMODE, (val & 0xE3) | RF_OPMODE_RECEIVER);
      break;
    case RF69_MODE_SYNTH:
      writeReg(REG_OPMODE, (val & 0xE3) | RF_OPMODE_SYNTHESIZER);
      break;
    case RF69_MODE_STANDBY:
      writeReg(REG_OPMODE, (val & 0xE3) | RF_OPMODE_STANDBY);
      break;
    case RF69_MODE_SLEEP:
      writeReg(REG_OPMODE, (val & 0xE3) | RF_OPMODE_SLEEP);
      break;
    default:
      return;
  }

  // we are using packet mode, so this check is not really needed but waiting for mode ready is necessary when
  // going from sleep because the FIFO may not be immediately available from previous mode.
  unsigned long start = millis();
  uint16_t timeout = 500;
  while (_mode == RF69_MODE_SLEEP && millis() - start < timeout && (readReg(REG_IRQFLAGS1) & RF_IRQFLAGS1_MODEREADY) == 0x00) {
    // wait for ModeReady
    yield();
  }
  if (millis() - start >= timeout) {
      //Timeout when waiting for getting out of sleep
  }

  _mode = newMode;
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
