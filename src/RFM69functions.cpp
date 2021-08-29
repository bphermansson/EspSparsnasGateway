#include "settings.h"
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <mqttpub.h>
#include <SPI.h>
#define ARDUINOJSON_USE_LONG_LONG 1 // https://arduinojson.org/v6/api/config/use_long_long/
#include <ArduinoJson.h>
#include <RFM69registers.h>

#define RF69_MODE_SLEEP 0      // XTAL OFF
#define RF69_MODE_STANDBY 1    // XTAL ON
#define RF69_MODE_SYNTH 2      // PLL ON
#define RF69_MODE_RX 3    // RX MODE
#define RF69_MODE_TX 4    // TX MODE
#define _interruptNum 5

static volatile uint8_t DATA[21];
static volatile uint8_t TEMPDATA[21];
static volatile uint8_t DATALEN;

static const char* mqtt_status_topic = MQTT_STATUS_TOPIC;
static const char* mqtt_debug_topic = MQTT_DEBUG_TOPIC;

static String mqttMess;
const String sensor_id = String(SENSOR_ID);
const String state_topic = APPNAME "/" + sensor_id + "/state"; 

extern PubSubClient mClient;

//#define _interruptNum 5
static volatile bool inInterrupt = false; // Fake Mutex

uint32_t FXOSC = 32000000;
uint32_t TwoPowerToNinteen = 524288; // 2^19
float RF69_FSTEP = (1.0 * FXOSC) / TwoPowerToNinteen; // p13 in datasheet
uint16_t BITRATE = FXOSC / 40000; // 40kBps
uint16_t FREQUENCYDEVIATION = 10000 / RF69_FSTEP; // 10kHz
uint16_t SYNCVALUE = 0xd201;
uint8_t RSSITHRESHOLD = 0xE4; // must be set to dBm = (-Sensitivity / 2), default is 0xE4 = 228 so -114dBm
uint8_t PAYLOADLENGTH = 20;

static volatile uint8_t _mode;
uint8_t enc_key[5];

extern timeval tv;
extern timespec tp;
extern time_t now;
extern "C" int clock_gettime(clockid_t unused, struct timespec *tp);

extern void publish_mqtt(String, String);

int16_t readRSSI();

void  ICACHE_RAM_ATTR interruptHandler();

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

void setMode(uint8_t newMode) {

  #ifdef DEBUG
     Serial.println(F("In setMode"));
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

uint32_t getFrequency() {
  return RF69_FSTEP * (((uint32_t)readReg(REG_FRFMSB) << 16) + ((uint16_t)readReg(REG_FRFMID) << 8) + readReg(REG_FRFLSB));
}
void receiveBegin() {
  DATALEN = 0;
  if (readReg(REG_IRQFLAGS2) & RF_IRQFLAGS2_PAYLOADREADY) {
    uint8_t val = readReg(REG_PACKETCONFIG2);
    // avoid RX deadlocks
    writeReg(REG_PACKETCONFIG2, (val & 0xFB) | RF_PACKET2_RXRESTART);
  }
  setMode(RF69_MODE_RX);
}

// get the received signal strength indicator (RSSI)
//uint16_t readRSSI(bool forceTrigger = false) {  // Settings this to true gives a crash...
int16_t readRSSI() {
/*  if (forceTrigger) {
    // RSSI trigger not needed if DAGC is in continuous mode
    writeReg(REG_RSSICONFIG, RF_RSSI_START);
          client.publish(mqtt_status_topic, "In rssi read");

    while ((readReg(REG_RSSICONFIG) & RF_RSSI_DONE) == 0x00) {
      // wait for RSSI_Ready
      yield();
    }
  }*/
  int16_t rssi = -readReg(REG_RSSIVALUE);
  #ifdef DEBUG
    Serial.println("rssi: " + String(rssi) + "dbi");
  #endif
  return rssi;
}

// checks if a packet was received and/or puts transceiver in receive (ie RX or listen) mode
bool receiveDone() {
  // noInterrupts(); // re-enabled in unselect() via setMode() or via
  // receiveBegin()
  #ifdef DEBUG
    //Serial.println("receiveDone");
  #endif

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

uint16_t crc16(volatile uint8_t *data, size_t n) {
  #ifdef DEBUG
    Serial.println("In crc16");
  #endif
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
bool initialize(uint32_t frequency) {
  #ifdef DEBUG
    Serial.print("In initialize, frequency = ");
    Serial.println(frequency);
  #endif
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
    #ifdef DEBUG
      Serial.println("ERROR: Failed setting syncvalue1 1st time");
    #endif
    return false;
  }
  start = millis();
  do {
    writeReg(REG_SYNCVALUE1, 0x55);
    yield();
  } while (readReg(REG_SYNCVALUE1) != 0x55 && millis() - start < timeout);
  if (readReg(REG_SYNCVALUE1) != 0x55) {
    #ifdef DEBUG
      Serial.println("ERROR: Failed setting syncvalue1 2nd time");
    #endif
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
    delay(1);
  }
  if (millis() - start >= timeout) {
    #ifdef DEBUG
      Serial.println("Failed on waiting for ModeReady()");
    #endif
    return false;
  }
  attachInterrupt(_interruptNum, interruptHandler, RISING);

  #ifdef DEBUG
    Serial.println("RFM69 init done");
  #endif
  return true;
}
void  ICACHE_RAM_ATTR interruptHandler() {
  String output;

  if (inInterrupt) {
    #ifdef DEBUG
      Serial.println("Already in interruptHandler.");
    #endif
    return;
  }
  inInterrupt = true;

  digitalWrite(LED_BLUE, HIGH);

  if (_mode == RF69_MODE_RX && (readReg(REG_IRQFLAGS2) & RF_IRQFLAGS2_PAYLOADREADY)) {

    // Read Rssi
    int16_t srssi = readRSSI();

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

    const uint32_t sensor_id_sub = SENSOR_ID - 0x5D38E8CB;
    enc_key[0] = (uint8_t)(sensor_id_sub >> 24);
    enc_key[1] = (uint8_t)(sensor_id_sub);
    enc_key[2] = (uint8_t)(sensor_id_sub >> 8);
    enc_key[3] = 0x47;
    enc_key[4] = (uint8_t)(sensor_id_sub >> 16);

    // Decrypt message
    for (size_t i = 0; i < 13; i++) {
      TEMPDATA[5 + i] = TEMPDATA[5 + i] ^ enc_key[i % 5];
    }

    uint32_t rcv_sensor_id = TEMPDATA[5] << 24 | TEMPDATA[6] << 16 | TEMPDATA[7] << 8 | TEMPDATA[8];

    // Bug fix from https://github.com/strigeus/sparsnas_decoder/pull/7/files
    // if (data_[0] != 0x11 || data_[1] != (SENSOR_ID & 0xFF) || data_[3] != 0x07 || rcv_sensor_id != SENSOR_ID) {
    // if (TEMPDATA[0] != 0x11 || TEMPDATA[1] != (SENSOR_ID & 0xFF) || TEMPDATA[3] != 0x07 || TEMPDATA[4] != 0x0E || rcv_sensor_id != SENSOR_ID) {
    if (TEMPDATA[0] != 0x11 || TEMPDATA[1] != (SENSOR_ID & 0xFF) || TEMPDATA[3] != 0x07 || rcv_sensor_id != SENSOR_ID) {
      #ifdef SHOW_BAD_PACKETS
        output = "";
        Serial.println("Bad packet!");
        for (uint8_t i = 0; i < 20; i++) {
           if (TEMPDATA[i]<0x10) {Serial.print("0");}
           Serial.print(TEMPDATA[i],HEX);
           Serial.print(" ");
        }
        Serial.println(" ");
      #endif
    } else 
    {
      #ifdef DEBUG
        Serial.println("Valid package received!");
      #endif
      analogWrite(LED_BLUE, LED_BLUE_BRIGHTNESS);

      gettimeofday(&tv, nullptr);
      clock_gettime(0, &tp);
      now = time(nullptr);
      // EPOCH+tz+dst
      // Serial.print("Time:");
      //Serial.print(ctime(&now));

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
      int seq = (TEMPDATA[9] << 8 | TEMPDATA[10]);    // Time in units of 15 seconds.
      unsigned int power = (TEMPDATA[11] << 8 | TEMPDATA[12]); // Current effect usage
      int pulse = (TEMPDATA[13] << 24 | TEMPDATA[14] << 16 | TEMPDATA[15] << 8 | TEMPDATA[16]); // Total number of pulses
      int battery = TEMPDATA[17]; // Battery level, 0-100.

      #ifdef DEBUG
        Serial.println("Current effect usage, hex");
        Serial.print(TEMPDATA[11], HEX);
        Serial.print(" ");
        Serial.println(TEMPDATA[12], HEX);
      #endif

      // This is how to convert the 'effect' field into Watt:
      // float watt =  (float)((3600000 / PULSES_PER_KWH) * 1024) / (effect);  ( 11:uint16_t effect;) This equals "power" in this code.

      // Bug fix from https://github.com/strigeus/sparsnas_decoder/pull/7/files
      // float watt =  (float)((3600000 / PULSES_PER_KWH) * 1024) / (power);
      float watt = power * 24;
      int data4 = TEMPDATA[4]^0x0f;
      //  Note that data_[4] cycles between 0-3 when you first put in the batterys in t$
      if (data4 == 1) {
           watt = (3600000.0f / float(PULSES_PER_KWH) * 1024.0f) / float(power);
      } else if (data4 == 0) { // special mode for low power usage
           watt = power * 0.24f / float(PULSES_PER_KWH);
      }
      /* m += sprintf(m, "%5d: %7.1f W. %d.%.3d kWh. Batt %d%%. FreqErr: %.2f", seq, watt, pulse/PULSES_PER_KWH, pulse%PULSES_PER_KWH, battery, freq);
      'So in the example 10 % 3, 10 divided by 3 is 3 with remainder 1, so the answer is 1.'
      */
      #ifdef DEBUG
        // Print amount of free memory.
        int heap = ESP.getFreeHeap();
        Serial.print(F("Available memory: "));
        Serial.println (heap);
      #endif

      // Prepare for output
      output = "Seq " + String(seq) + ": ";
      output += String(now) + ", ";
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

      const size_t capacity = JSON_OBJECT_SIZE(9);
      DynamicJsonDocument status(capacity);
      if (err=="CRC ERR") {
        Serial.println(err);
        status["error"] = "CRC Error";
        analogWrite(LED_RED, LED_RED_BRIGHTNESS);
        mClient.publish((char*) String(mqtt_debug_topic).c_str(), (char*) output.c_str());
        delay(300);
        analogWrite(LED_RED, 0);
      }
      else {
        status["error"] = "";
      }

      status["seq"] = seq;
      status["timestamp"] = now;
      status["watt"] = float(watt);
      status["total"] = float(pulse) / float(PULSES_PER_KWH);
      status["battery"] = battery;
      status["rssi"] = srssi;
      status["power"] = power;
      status["pulse"] = pulse;

      String mqttMess;
      serializeJson(status, mqttMess);

      if (status["error"] == "") {
        publish_mqtt(state_topic, mqttMess);
        mClient.publish((char*) String(mqtt_status_topic).c_str(), (char*) mqttMess.c_str());
      }
      analogWrite(LED_BLUE, 0);
    }
    unselect();
    setMode(RF69_MODE_RX);
  }
  digitalWrite(LED_BLUE, LOW);

  //Serial.println("Int done");
  inInterrupt = false;
}
