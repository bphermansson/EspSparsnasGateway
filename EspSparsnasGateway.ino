/**
  * EspSparsnasGateway
  *
  * https://github.com/bphermansson/EspSparsnasGateway
  *
  * Based on code from user Sommarlov @ EF: http://elektronikforumet.com/forum/viewtopic.php?f=2&t=85006&start=255#p1357610
  * Which in turn is based on Strigeus work: https://github.com/strigeus/sparsnas_decoder
  */

/**
 * MQTT Config
 */
#define MQTT_USERNAME "emonpi"
#define MQTT_PASSWORD "emonpimqtt2016"
#define MQTT_KEEPALIVE 20
#define MQTT_SOCKET_TIMEOUT 30
#define MQTT_MAX_PACKET_SIZE 265
#define MQTT_HOST "0.0.0.0"

/**
 * Wifi Config
 */
#define WIFI_SSID "your_ssid"
#define WIFI_PASSWORD "*********"

/*
 * Sparsnas Config
 */
#define PULSES_PER_KWH 1000
#define SENSOR_ID 643654

#define DEBUG 1

// You dont have to change anything below

#define INTERRUPT_PIN 5

#define RF69_MODE_SLEEP 0   // XTAL OFF
#define RF69_MODE_STANDBY 1 // XTAL ON
#define RF69_MODE_SYNTH 2   // PLL ON
#define RF69_MODE_RX 3      // RX MODE
#define RF69_MODE_TX 4      // TX MODE

const char *mqtt_status_topic = "EspSparsnasGateway/values";
const char *mqtt_debug_topic = "EspSparsnasGateway/debug";

const char *mqtt_sub_topic_freq = "EspSparsnasGateway/settings/frequency";
const char *mqtt_sub_topic_senderid = "EspSparsnasGateway/settings/senderid";
const char *mqtt_sub_topic_clear = "EspSparsnasGateway/settings/clear";
const char *mqtt_sub_topic_reset = "EspSparsnasGateway/settings/reset";

#define APPNAME "EspSparsnasGateway"

#include <RFM69registers.h>
#include <Arduino.h>
#include <SPI.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <EEPROM.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>

ADC_MODE(ADC_VCC);

WiFiClient espClient;
PubSubClient client(espClient);

uint32_t FXOSC = 32000000;
uint32_t TwoPowerToNinteen = 524288;                   // 2^19
float RF69_FSTEP = (1.0f * FXOSC) / TwoPowerToNinteen; // p13 in datasheet
uint32_t FREQUENCY = 868000000;                        // default frequency
uint16_t BITRATE = FXOSC / 40000;                      // 40kBps
uint16_t FREQUENCYDEVIATION = 10000 / RF69_FSTEP;      // 10kHz
uint16_t SYNCVALUE = 0xd201;
uint8_t RSSITHRESHOLD = 0xE4;                          // must be set to dBm = (-Sensitivity / 2), default is 0xE4 = 228 so -114dBm
uint8_t PAYLOADLENGTH = 20;


static volatile uint8_t DATA[21];
static volatile uint8_t TEMPDATA[21];
static volatile uint8_t DATALEN;
static volatile uint8_t _mode;
static volatile bool inInterrupt = false; // Fake Mutex
uint8_t enc_key[5];
uint16_t rssi = 0;

unsigned long lastRecievedData = millis();
unsigned long lastClientLoop = millis();

uint32_t getFrequency()
{
  return RF69_FSTEP * (((uint32_t)readReg(REG_FRFMSB) << 16) + ((uint16_t)readReg(REG_FRFMID) << 8) + readReg(REG_FRFLSB));
}

void avoidDeadlocks()
{
  uint8_t val = readReg(REG_PACKETCONFIG2);
  writeReg(REG_PACKETCONFIG2, (val & 0xFB) | RF_PACKET2_RXRESTART);
}

void enableRadioTx()
{
  setMode(RF69_MODE_RX);
}

void receiveBegin()
{
  DATALEN = 0;
  if (readReg(REG_IRQFLAGS2) & RF_IRQFLAGS2_PAYLOADREADY)
  {
    avoidDeadlocks();
  }
  enableRadioTx();
}

uint8_t readReg(uint8_t addr)
{
  selectRadio();
  SPI.transfer(addr & 0x7F);
  uint8_t regval = SPI.transfer(0);
  deselectRadio();
  return regval;
}

void writeReg(uint8_t addr, uint8_t value)
{
  selectRadio();
  SPI.transfer(addr | 0x80);
  SPI.transfer(value);
  deselectRadio();
}

/**
 * select the RFM69 transceiver (save SPI settings, set CS low)
 */
void selectRadio()
{
  digitalWrite(SS, LOW);
}

/**
 * unselect the RFM69 transceiver (set CS high, restore SPI settings)
 */
void deselectRadio()
{
  digitalWrite(SS, HIGH);
}

/**
 * get the received signal strength indicator (RSSI)
 */
uint16_t readRSSI()
{
  return -readReg(REG_RSSIVALUE);
}

void enableInterrupts()
{
  setMode(RF69_MODE_STANDBY);
}

/**
 * checks if a packet was received
 *
 * puts transceiver in receive (ie RX or listen) mode
 */
bool receiveDone()
{
  if (_mode == RF69_MODE_RX && DATALEN > 0)
  {
    enableInterrupts();
    return true;
  }
  if (_mode == RF69_MODE_RX)
  {
    return false;
  }

  receiveBegin();
  return false;
}

uint16_t crc16(volatile uint8_t *data, size_t n)
{
#ifdef DEBUG
  Serial.println("In crc16");
#endif
  uint16_t crcReg = 0xffff;
  size_t i, j;
  for (j = 0; j < n; j++)
  {
    uint8_t crcData = data[j];
    for (i = 0; i < 8; i++)
    {
      if (((crcReg & 0x8000) >> 8) ^ (crcData & 0x80))
      {
        crcReg = (crcReg << 1) ^ 0x8005;
      }
      else
      {
        crcReg = (crcReg << 1);
      }
      crcData <<= 1;
    }
  }
  return crcReg;
}

/**
 * When a Mqtt message has arrived
 */
void callback(char *topic, byte *payload, unsigned int length)
{
  StaticJsonBuffer<150> jsonBuffer;
  JsonObject &root = jsonBuffer.createObject();
  char msg[150];

  // Extract topic
#ifdef DEBUG
  Serial.print("Message arrived: ");
  Serial.println(topic);
#endif

  // Extract payload
  String stringPayload = "";
  for (int i = 0; i < length; i++)
  {
    stringPayload += (char)payload[i];
  }
#ifdef DEBUG
  Serial.print("Payload: ");
  Serial.println(stringPayload);
#endif

  int addr = 0;

  if (strcmp(topic, "EspSparsnasGateway/settings/frequency") == 0)
  {

#ifdef DEBUG
    Serial.println("Frequency change");
#endif
    int charLength = stringPayload.length();
    if (charLength != 6)
    {
#ifdef DEBUG
      Serial.print("Set frequency to: ");
      Serial.println(stringPayload);
#endif
      EEPROM.write(addr, 1);
      addr = 2;

      for (int i = addr; i < stringPayload.length() + 2; ++i)
      {
        EEPROM.write(i, stringPayload[i - 2]);
#ifdef DEBUG
        Serial.print("Wrote: ");
        Serial.println(stringPayload[i - 2]);
#endif
      }
      EEPROM.commit();

#ifdef DEBUG
      root["status"] = "Frequency changed";
      root.printTo((char *)msg, root.measureLength() + 1);
      client.publish(mqtt_debug_topic, msg);
#endif

      delay(10);
      ESP.restart();
    }
    else
    {
#ifdef DEBUG
      Serial.println("Error in frequency length, use 6 digits like '868.23'");
#endif
    }
  }
  if (strcmp(topic, "EspSparsnasGateway/settings/senderid") == 0)
  {
#ifdef DEBUG
    Serial.println("Senderid change");
#endif
    addr++;
    EEPROM.write(addr, 1); // Indicate stored settings by writing 1 to Eeprom address 1
    byte offs = 12;
    addr = addr + offs;
#ifdef DEBUG
    Serial.println(addr);
#endif
    int charLength = stringPayload.length();
    for (int i = addr; i < charLength + offs; ++i)
    {
      EEPROM.write(i, stringPayload[i - offs]);
#ifdef DEBUG
      Serial.print("Wrote: ");
      Serial.println(stringPayload[i - offs]);
#endif
    }
    EEPROM.commit();

#ifdef DEBUG
    root["status"] = "Sender id changed";
    root["NewSenderId"] = stringPayload;
    root.printTo((char *)msg, root.measureLength() + 1);
    client.publish(mqtt_debug_topic, msg);
#endif

    delay(10);
    ESP.restart();
  }
  if (strcmp(topic, "EspSparsnasGateway/settings/clear") == 0)
  {
    Serial.println("Clear settings");
    for (int i = 0; i < 512; i++)
    {
      EEPROM.write(i, 0);
    }
    EEPROM.commit();

#ifdef DEBUG
    root["status"] = "Settings cleared";
    root.printTo((char *)msg, root.measureLength() + 1);
    client.publish(mqtt_debug_topic, msg);
#endif

    delay(10);
    ESP.restart();
  }
  if (strcmp(topic, "EspSparsnasGateway/settings/reset") == 0)
  {
#ifdef DEBUG
    Serial.println("Reset");
    root["status"] = "Reset";
    root.printTo((char *)msg, root.measureLength() + 1);
    client.publish(mqtt_debug_topic, msg);
#endif
    ESP.restart();
  }
}


void setup()
{
  StaticJsonBuffer<150> jsonBuffer;
  JsonObject &root = jsonBuffer.createObject();
  char msg[150];

#ifdef DEBUG
  Serial.begin(115200);
  Serial.println("Welcome to EspSparsnasGateway");
  Serial.print("Compiled at:");
  Serial.println(__DATE__ " " __TIME__);
  Serial.println("Debug on");
  Serial.print("Vcc=");
  Serial.println(ESP.getVcc());
#endif

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
#ifdef DEBUG
    Serial.println("WiFi connection Failed! Rebooting...");
#endif
    delay(5000);
    ESP.restart();
  }
  WiFi.hostname(APPNAME);

  client.setServer(MQTT_HOST, 1883);
  client.setCallback(callback);
  reconnect();

  EEPROM.begin(512);
  bool storedValues;
  // Read stored values
  String freq, sendid;
  uint32_t ifreq;
  uint32_t isendid;

  // Read stored config status. Write this bit when storing settings
  char savedSettingFreq = char(EEPROM.read(0));
  char savedSettingSenderid = char(EEPROM.read(1));

#ifdef DEBUG
  Serial.println("Stored data: ");
  if (savedSettingFreq == 1)
  {
    Serial.println("Found a stored frequency");
  }
  if (savedSettingSenderid == 1)
  {
    Serial.println("Found a stored senderid");
  }
#endif

  if (savedSettingFreq == 1)
  {
    for (int i = 2; i < 8; ++i)
    {
      char curC = char(EEPROM.read(i));
      freq += curC;
    }

#ifdef DEBUG
    Serial.print("Stored frequency: ");
    Serial.println(freq);
#endif
    // Adjust frequency from '868.00' to 868000000
    freq.trim();
    String lft = freq.substring(0, 3);
    String rgt = freq.substring(4, 6);
    String tot = lft + rgt;

    ifreq = tot.toInt();

    ifreq = ifreq * 10000;
#ifdef DEBUG
    Serial.print("Calculated frequency: ");
    Serial.println(ifreq);
#endif
  }
  else
  {
#ifdef DEBUG
    Serial.println("There is no stored frequency, using default value");
    root["status"] = "There is no stored frequency, using default value";
    root.printTo((char *)msg, root.measureLength() + 1);
    client.publish(mqtt_debug_topic, msg);
#endif

    ifreq = FREQUENCY;
  }
  if (savedSettingSenderid == 1)
  {
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
  else
  {
#ifdef DEBUG
    Serial.println("There is no stored senderid, using default value");
    root["status"] = "There is no stored senderid, using default value";
    root.printTo((char *)msg, root.measureLength() + 1);
    client.publish(mqtt_debug_topic, msg);
#endif
    isendid = SENSOR_ID;
  }

#ifdef DEBUG
  Serial.print("Senderid: ");
  Serial.println(isendid);
  Serial.print("Frequency: ");
  Serial.println(ifreq);
  Serial.print("RF69_FSTEP: ");
  Serial.println(RF69_FSTEP);
#endif

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
    if (error == OTA_AUTH_ERROR)
      Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR)
      Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR)
      Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR)
      Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR)
      Serial.println("End Failed");
  });
  ArduinoOTA.begin();
#ifdef DEBUG
  Serial.print("Over The Air programming enabled, port: ");
  Serial.println(APPNAME);
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
#endif

  IPAddress ip = WiFi.localIP();
  char buf[60];
  sprintf(buf, "%s @ IP:%d.%d.%d.%d SSID: %s", APPNAME, WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3], WIFI_SSID);
  Serial.println(buf);
  root["status"] = buf;
  root.printTo((char *)msg, root.measureLength() + 1);
  client.publish(mqtt_debug_topic, msg);

  const uint32_t sensor_id_sub = isendid - 0x5D38E8CB;

  enc_key[0] = (uint8_t)(sensor_id_sub >> 24);
  enc_key[1] = (uint8_t)(sensor_id_sub);
  enc_key[2] = (uint8_t)(sensor_id_sub >> 8);
  enc_key[3] = 0x47;
  enc_key[4] = (uint8_t)(sensor_id_sub >> 16);

  if (!initialize(ifreq))
  {
#ifdef DEBUG
    char mess[] = "Unable to initialize the radio. Exiting.";
    Serial.println(mess);

    StaticJsonBuffer<150> jsonBuffer;
    JsonObject &root = jsonBuffer.createObject();
    char msg[150];
    root["status"] = mess;
    root.printTo((char *)msg, root.measureLength() + 1);
    client.publish(mqtt_debug_topic, msg);
#endif

    while (1)
    {
      yield();
    }
  }
#ifdef DEBUG
  Serial.println("Radio initialized.");
  String temp = "Listening on " + String(getFrequency()) + "hz. Done in setup.";
  Serial.println(temp);

  root["status"] = temp;
  root.printTo((char *)msg, root.measureLength() + 1);
  client.publish(mqtt_debug_topic, msg);
#endif
}

bool initialize(uint32_t frequency)
{
  frequency = frequency / RF69_FSTEP;
#ifdef DEBUG
  Serial.print("In initialize, frequency = ");
  Serial.println(frequency * RF69_FSTEP);
  Serial.print("RF69_FSTEP: ");
  Serial.println(RF69_FSTEP);
  Serial.print("Adjusted freq: ");
  Serial.println(frequency);
#endif

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
      /* 0x25 */ {REG_DIOMAPPING1, RF_DIOMAPPING1_DIO0_01},                         // PayloadReady
      /* 0x26 */ {REG_DIOMAPPING2, RF_DIOMAPPING2_CLKOUT_OFF},                      // DIO5 ClkOut disable for power saving
      /* 0x28 */ {REG_IRQFLAGS2, RF_IRQFLAGS2_FIFOOVERRUN},                         // writing to this bit ensures that the FIFO & status flags are reset
      /* 0x29 */ {REG_RSSITHRESH, RSSITHRESHOLD},
      /* 0x2B */ {REG_RXTIMEOUT2, (uint8_t)0x00}, // RegRxTimeout2 (0x2B) interrupt is generated TimeoutRssiThresh *16*T bit after Rssi interrupt if PayloadReady interrupt doesn’t occur.
      /* 0x2D */ {REG_PREAMBLELSB, 3},            // default 3 preamble bytes 0xAAAAAA
      /* 0x2E */ {REG_SYNCCONFIG, RF_SYNC_ON | RF_SYNC_FIFOFILL_AUTO | RF_SYNC_SIZE_2 | RF_SYNC_TOL_0},
      /* 0x2F */ {REG_SYNCVALUE1, (uint8_t)(SYNCVALUE >> 8)},
      /* 0x30 */ {REG_SYNCVALUE2, (uint8_t)(SYNCVALUE)},
      /* 0x37 */ {REG_PACKETCONFIG1, RF_PACKET1_FORMAT_FIXED | RF_PACKET1_DCFREE_OFF | RF_PACKET1_CRC_OFF | RF_PACKET1_CRCAUTOCLEAR_ON | RF_PACKET1_ADRSFILTERING_OFF},
      /* 0x38 */ {REG_PAYLOADLENGTH, PAYLOADLENGTH},
      /* 0x3C */ {REG_FIFOTHRESH, RF_FIFOTHRESH_TXSTART_FIFONOTEMPTY | RF_FIFOTHRESH_VALUE},                              // TX on FIFO not empty
      /* 0x3D */ {REG_PACKETCONFIG2, RF_PACKET2_RXRESTARTDELAY_2BITS | RF_PACKET2_AUTORXRESTART_ON | RF_PACKET2_AES_OFF}, // RXRESTARTDELAY must match transmitter PA ramp-down time (bitrate dependent)
      /* 0x6F */ {REG_TESTDAGC, RF_DAGC_IMPROVED_LOWBETA0},                                                               // run DAGC continuously in RX mode for Fading Margin Improvement, recommended default for AfcLowBetaOn=0
      {255, 0}};

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
  do
  {
    writeReg(REG_SYNCVALUE1, 0xAA);
    yield();
  } while (readReg(REG_SYNCVALUE1) != 0xaa && millis() - start < timeout);
  if (readReg(REG_SYNCVALUE1) != 0xaa)
  {
#ifdef DEBUG
    Serial.println("ERROR: Failed setting syncvalue1 1st time");
#endif
    return false;
  }
  start = millis();
  do
  {
    writeReg(REG_SYNCVALUE1, 0x55);
    yield();
  } while (readReg(REG_SYNCVALUE1) != 0x55 && millis() - start < timeout);

  if (readReg(REG_SYNCVALUE1) != 0x55)
  {
#ifdef DEBUG
    Serial.println("ERROR: Failed setting syncvalue1 2nd time");
#endif
    return false;
  }
  for (uint8_t i = 0; CONFIG[i][0] != 255; i++)
  {
    writeReg(CONFIG[i][0], CONFIG[i][1]);
    yield();
  }

  setMode(RF69_MODE_STANDBY);
  start = millis();
  while (((readReg(REG_IRQFLAGS1) & RF_IRQFLAGS1_MODEREADY) == 0x00) && millis() - start < timeout)
  {
    delay(1);
  }

  if (millis() - start >= timeout)
  {
#ifdef DEBUG
    char mess[] = "Failed on waiting for ModeReady()";
    Serial.println(mess);

    StaticJsonBuffer<150> jsonBuffer;
    JsonObject &root = jsonBuffer.createObject();
    char msg[150];
    root["status"] = mess;
    root.printTo((char *)msg, root.measureLength() + 1);
    client.publish(mqtt_debug_topic, msg);
#endif
    return false;
  }
  attachInterrupt(INTERRUPT_PIN, interruptHandler, RISING);

#ifdef DEBUG
  char mess[] = "RFM69 init done";
  Serial.println(mess);

  StaticJsonBuffer<150> jsonBuffer;
  JsonObject &root = jsonBuffer.createObject();
  char msg[150];
  root["status"] = mess;
  root.printTo((char *)msg, root.measureLength() + 1);
  client.publish(mqtt_debug_topic, msg);
#endif
  return true;
}

void interruptHandler()
{
  if (inInterrupt)
  {
#ifdef DEBUG
    Serial.println("Already in interruptHandler.");
#endif
    return;
  }
  inInterrupt = true;

  if (_mode == RF69_MODE_RX && (readReg(REG_IRQFLAGS2) & RF_IRQFLAGS2_PAYLOADREADY))
  {
    char crssi[25];
    int16_t srssi;
    srssi = readRSSI();
    dtostrf(srssi, 16, 0, crssi);

    setMode(RF69_MODE_STANDBY);
    DATALEN = 0;
    selectRadio();

    // Init reading
    SPI.transfer(REG_FIFO & 0x7F);

    for (uint8_t i = 0; i < 20; i++)
    {
      TEMPDATA[i] = SPI.transfer(0);
    }

    // CRC is done BEFORE decrypting message
    uint16_t crc = crc16(TEMPDATA, 18);
    uint16_t packet_crc = TEMPDATA[18] << 8 | TEMPDATA[19];

#ifdef DEBUG
    Serial.println("Got rf data");
#endif

    // Decrypt message
    for (size_t i = 0; i < 13; i++)
    {
      TEMPDATA[5 + i] = TEMPDATA[5 + i] ^ enc_key[i % 5];
    }

    uint32_t rcv_sensor_id = TEMPDATA[5] << 24 | TEMPDATA[6] << 16 | TEMPDATA[7] << 8 | TEMPDATA[8];

    String output;

    if (TEMPDATA[0] != 0x11 || TEMPDATA[1] != (SENSOR_ID & 0xFF) || TEMPDATA[3] != 0x07 || rcv_sensor_id != SENSOR_ID)
    {
#ifdef DEBUG
      // Serial.print("data_0: ");
      // Serial.println(TEMPDATA[0]);

      // Serial.print("data_1: ");
      // Serial.println(TEMPDATA[1]);

      // Serial.print("SENSOR_ID & 0xFF: ");
      // Serial.println(SENSOR_ID & 0xFF);

      // Serial.print("data_3: ");
      // Serial.println(TEMPDATA[3]);

      // Serial.print("rcv_sensor_id: ");
      // Serial.println(rcv_sensor_id);

      // Serial.print("SENSOR_ID: ");
      // Serial.println(SENSOR_ID);
#endif
    }
    else
    {
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

      // Ref: https://github.com/strigeus/sparsnas_decoder
      int seq = (TEMPDATA[9] << 8 | TEMPDATA[10]);                                              // Time in units of 15 seconds.
      uint power = (TEMPDATA[11] << 8 | TEMPDATA[12]);                                          // Current effect usage
      int pulse = (TEMPDATA[13] << 24 | TEMPDATA[14] << 16 | TEMPDATA[15] << 8 | TEMPDATA[16]); // Total number of pulses
      int battery = TEMPDATA[17];                                                               // Battery level, 0-100.

      // Effect to watt conversion
      // float watt =  (float)((3600000 / PULSES_PER_KWH) * 1024) / (effect);  ( 11:uint16_t effect;) This equals "power" in this code.

      float watt = power * 24;
      int data4 = TEMPDATA[4] ^ 0x0f;
      //  Note that data_[4] cycles between 0-3 when you first put in the batterys in t$
      if (data4 == 1)
      {
        watt = (3600000.0f / float(PULSES_PER_KWH) * 1024.0f) / float(power);
      }
      else if (data4 == 0)
      { // special mode for low power usage
        watt = power * 0.24f / float(PULSES_PER_KWH);
      }
      /**
       * m += sprintf(m, "%5d: %7.1f W. %d.%.3d kWh. Batt %d%%. FreqErr: %.2f", seq, watt, pulse/PULSES_PER_KWH, pulse%PULSES_PER_KWH, battery, freq);
       * 'So in the example 10 % 3, 10 divided by 3 is 3 with remainder 1, so the answer is 1.'
       */
#ifdef DEBUG
      int heap = ESP.getFreeHeap();
      Serial.print("Memory usage: ");
      Serial.println(heap);
#endif

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
      JsonObject &root = jsonBuffer.createObject();
      char msg[150];

      if (err == "CRC ERR")
      {
        Serial.println(err);
#ifdef DEBUG
        root["error"] = "CRC Error";
#endif
      }
      else
      {
        root["seq"] = seq;
        root["watt"] = float(watt);
        root["total"] = float(pulse) / float(PULSES_PER_KWH);
        root["battery"] = battery;
        root["rssi"] = String(srssi);
        root["power"] = String(power);
        root["pulse"] = String(pulse);
      }
      root.printTo((char *)msg, root.measureLength() + 1);
      client.publish(mqtt_status_topic, msg);
    }

    deselectRadio();
    enableRadioTx();
  }

  inInterrupt = false;
}

void setMode(uint8_t newMode)
{
  if (newMode == _mode)
  {
    return;
  }
#ifdef DEBUG
  Serial.print("Set mode: ");
  Serial.println(newMode);
#endif

  uint8_t val = readReg(REG_OPMODE);
  switch (newMode)
  {
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
  while (_mode == RF69_MODE_SLEEP && millis() - start < timeout && (readReg(REG_IRQFLAGS1) & RF_IRQFLAGS1_MODEREADY) == 0x00)
  {
    // wait for ModeReady
    yield();
  }
  if (millis() - start >= timeout)
  {
    //Timeout when waiting for getting out of sleep
  }

  _mode = newMode;
}

void loop()
{
  ArduinoOTA.handle();

  reconnect();
  if (millis() - lastClientLoop >= 5000)
  {
#ifdef DEBUG
    Serial.println("client.loop");
#endif
    client.loop();
    lastClientLoop = millis();
  }

  if (receiveDone())
  {
    lastRecievedData = millis();
#ifdef DEBUG
    Serial.println("We got data to send.");
    client.publish(mqtt_debug_topic, "We got data to send.");
#endif

    delay(500);
  }
}

bool reconnect()
{
  while (!client.connected())
  {
#ifdef DEBUG
    Serial.print("Attempting MQTT connection...");
#endif
    if (client.connect(APPNAME, MQTT_USERNAME, MQTT_PASSWORD))
    {
      client.subscribe(mqtt_sub_topic_freq);
      client.subscribe(mqtt_sub_topic_senderid);
      client.subscribe(mqtt_sub_topic_clear);
      client.subscribe(mqtt_sub_topic_reset);
#ifdef DEBUG
      String temp = "Connected to Mqtt broker as " + String(APPNAME);
      Serial.println(temp);
      StaticJsonBuffer<150> jsonBuffer;
      JsonObject &root = jsonBuffer.createObject();
      char msg[150];
      root["status"] = temp;
      root.printTo((char *)msg, root.measureLength() + 1);
      client.publish(mqtt_debug_topic, msg);
#endif
      return true;
    }
#ifdef DEBUG
    Serial.print("Mqtt connection failed, rc=");
    Serial.print(client.state());
    Serial.println(" try again in 5 seconds");
#endif
    delay(5000);
  }
  return true;
}
