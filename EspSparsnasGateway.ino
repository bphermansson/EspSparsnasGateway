/*
 * Based on code from user Sommarlov @ EF: http://elektronikforumet.com/forum/viewtopic.php?f=2&t=85006&start=255#p1357610
 * Which in turn is based on Strigeus work: https://github.com/strigeus/sparsnas_decoder
 * 
 */
#define MQTT_USERNAME "emonpi"     
#define MQTT_PASSWORD "emonpimqtt2016"  
#define appname "EspSparsnasGateway"
const char* mqtt_server = "192.168.1.79";
char* mqtt_status_topic = "EspSparsnasGateway/values";

// Wifi settings
const char* ssid = "NETGEAR83";
const char* password = "..........";


#include <RFM69registers.h>
#include <Arduino.h>
#include <SPI.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
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

#define SENSOR_ID 643654 //<- ange din 6 siffriga kod här, ta sista 6 siffrorna av 400 666 111. Koden finns i sändaren under batteriet
#define PULSES_PER_KWH 1000 // <- samma här, står på elmätaren

uint32_t FXOSC = 32000000;
uint32_t TwoPowerToNinteen = 524288; // 2^19
float RF69_FSTEP = (1.0 * FXOSC) / TwoPowerToNinteen; // p13 in datasheet
uint32_t FREQUENCY = 868000000;
uint16_t BITRATE = FXOSC / 40000; // 40kBps
uint16_t FREQUENCYDEVIATION = 10000 / RF69_FSTEP; // 10kHz
uint16_t SYNCVALUE = 0xd201;
uint8_t RSSITHRESHOLD = 210; // must be set to dBm = (-Sensitivity / 2), default is 0xE4 = 228 so -114dBm
uint8_t PAYLOADLENGTH = 20;

#define _interruptNum 5

static volatile uint8_t DATA[21];
static volatile uint8_t TEMPDATA[21];
static volatile uint8_t DATALEN;
static volatile uint16_t RSSI; // Most accurate RSSI during reception (closest to the reception)
static volatile uint8_t _mode;
static volatile bool inInterrupt = false; // Fake Mutex
uint8_t enc_key[5];

unsigned long lastCheckedForUpdate = millis();
unsigned long lastRecievedData = millis();

// ----------------------------------------------------

void setup() {
  Serial.begin(115200);
  Serial.println("Setup");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
  WiFi.hostname(appname);

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

  client.setServer(mqtt_server, 1883);
  //client.setCallback(callback); // What to do when a Mqtt message arrives
  if (!client.connected()) {
      reconnect();
  }

  // Publish some info, first via serial, then Mqtt
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  IPAddress ip = WiFi.localIP();
  char buf[60];
  sprintf(buf, "%s @ IP:%d.%d.%d.%d SSID: %s", appname, WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3], ssid );
  client.publish(mqtt_status_topic, buf);
  
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
    client.publish(mqtt_status_topic, mess);

    while (1) {
      yield();
    }
  }
  String temp = "Listening on " + String(getFrequency()) + "hz. Done in setup.";
  char mess[100];
  temp.toCharArray(mess,100);
  Serial.println(temp);
  client.publish(mqtt_status_topic, mess);
}

bool initialize(uint32_t frequency) {
  frequency = frequency / RF69_FSTEP;

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
  start = millis();
  do {
    writeReg(REG_SYNCVALUE1, 0x55);
    yield();
  } while (readReg(REG_SYNCVALUE1) != 0x55 && millis() - start < timeout);

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
    char mess[ ] = "Failed on waiting for ModeReady()";
    Serial.println(mess);
    client.publish(mqtt_status_topic, mess);
    return false;
  }
  attachInterrupt(_interruptNum, interruptHandler, RISING);

  char mess[ ] = "RFM69 init done";
  Serial.println(mess);
  client.publish(mqtt_status_topic, mess);
  return true;
}

uint32_t getFrequency() {
  return RF69_FSTEP * (((uint32_t)readReg(REG_FRFMSB) << 16) + ((uint16_t)readReg(REG_FRFMID) << 8) + readReg(REG_FRFLSB));
}

void receiveBegin() {
  DATALEN = 0;
  RSSI = 0;
  if (readReg(REG_IRQFLAGS2) & RF_IRQFLAGS2_PAYLOADREADY) {
    uint8_t val = readReg(REG_PACKETCONFIG2);
    // avoid RX deadlocks
    writeReg(REG_PACKETCONFIG2, (val & 0xFB) | RF_PACKET2_RXRESTART);
  }
  setMode(RF69_MODE_RX);
}

// checks if a packet was received and/or puts transceiver in receive (ie RX or listen) mode
bool receiveDone() {
  // noInterrupts(); // re-enabled in unselect() via setMode() or via
  // receiveBegin()

  if (_mode == RF69_MODE_RX && DATALEN > 0) {
    setMode(RF69_MODE_STANDBY); // enables interrupts
    return true;

  } else if (_mode == RF69_MODE_RX) {
    // already in RX no payload yet
    // interrupts(); // explicitly re-enable interrupts
    return false;
  }

  receiveBegin();
  return false;
}

// get the received signal strength indicator (RSSI)
uint16_t readRSSI(bool forceTrigger = false) {
  uint16_t rssi = 0;
  if (forceTrigger) {
    // RSSI trigger not needed if DAGC is in continuous mode
    writeReg(REG_RSSICONFIG, RF_RSSI_START);
    while ((readReg(REG_RSSICONFIG) & RF_RSSI_DONE) == 0x00) {
      // wait for RSSI_Ready
      yield();
    }
  }
  rssi = -readReg(REG_RSSIVALUE);
  rssi >>= 1;
  return rssi;
}

uint8_t readReg(uint8_t addr) {
  select();
  SPI.transfer(addr & 0x7F);
  uint8_t regval = SPI.transfer(0);
  unselect();
  return regval;
}

void writeReg(uint8_t addr, uint8_t value) {
  select();
  SPI.transfer(addr | 0x80);
  SPI.transfer(value);
  unselect();
}

// select the RFM69 transceiver (save SPI settings, set CS low)
void select() {
  // noInterrupts();
  digitalWrite(SS, LOW);
}

// unselect the RFM69 transceiver (set CS high, restore SPI settings)
void unselect() {
  digitalWrite(SS, HIGH);
  // interrupts();
}

void interruptHandler() {
  if (inInterrupt) {
    //Serial.println("Already in interruptHandler.");
    return;
  }
  inInterrupt = true;

  if (_mode == RF69_MODE_RX && (readReg(REG_IRQFLAGS2) & RF_IRQFLAGS2_PAYLOADREADY)) {
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

    // Decrypt message
    for (size_t i = 0; i < 13; i++) {
      TEMPDATA[5 + i] = TEMPDATA[5 + i] ^ enc_key[i % 5];
    }

    uint32_t rcv_sensor_id = TEMPDATA[5] << 24 | TEMPDATA[6] << 16 | TEMPDATA[7] << 8 | TEMPDATA[8];

    String output;

    if (TEMPDATA[0] != 0x11 || TEMPDATA[1] != (SENSOR_ID & 0xFF) || TEMPDATA[3] != 0x07 || TEMPDATA[4] != 0x0E || rcv_sensor_id != SENSOR_ID) {
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
      int seq = (TEMPDATA[9] << 8 | TEMPDATA[10]);
      int effect = (TEMPDATA[11] << 8 | TEMPDATA[12]);
      int pulse = (TEMPDATA[13] << 24 | TEMPDATA[14] << 16 | TEMPDATA[15] << 8 | TEMPDATA[16]);
      int battery = TEMPDATA[17];
      float watt =  (float)((3600000 / PULSES_PER_KWH) * 1024) / (effect);
      output = "Seq " + String(seq) + ": ";
      output += String(watt) + " W, total: ";
      output += String(pulse / 1000) + " kWh, battery ";
      output += String(battery) + "% ";

      output += (crc == packet_crc ? "" : "CRC ERR");
      String err = (crc == packet_crc ? "" : "CRC ERR");
      Serial.println(output);

    if (err=="CRC ERR") {
      Serial.println(err);
    }
    else {
      //For Json output
      StaticJsonBuffer<150> jsonBuffer;
      JsonObject& root = jsonBuffer.createObject();
      char msg[150];
      root["seq"] = seq;
      root["effect"] = effect;
      root["total"] = watt;
      root["battery"] = battery;
      root.printTo((char*)msg, root.measureLength() + 1);
      client.publish(mqtt_status_topic, msg);  // Wants a char
    }
    }

    unselect();
    setMode(RF69_MODE_RX);
  }
  RSSI = readRSSI();

  inInterrupt = false;
}

void setMode(uint8_t newMode) {
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


uint16_t crc16(volatile uint8_t *data, size_t n) {
  uint16_t crcReg = 0xffff;
  size_t i, j;
  for (j = 0; j < n; j++) {
    uint8_t crcData = data[j];
    for (i = 0; i < 8; i++) {
      if (((crcReg & 0x8000) >> 8) ^ (crcData & 0x80))
        crcReg = (crcReg << 1) ^ 0x8005;
      else
        crcReg = (crcReg << 1);
      crcData <<= 1;
    }
  }
  return crcReg;
}

void loop() {
  ArduinoOTA.handle();

  if (receiveDone()) {
    lastRecievedData = millis();
    // Send data to Mqtt server
    Serial.println("We got data to send.");
    // Wait a bit
    //codeLibrary.wait(500);
    delay(500);
  }
  // Mqtt
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    //Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(appname, MQTT_USERNAME, MQTT_PASSWORD)) {
      Serial.println("Connected to Mqtt broker");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}
