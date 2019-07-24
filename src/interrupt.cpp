#include <Arduino.h>
#include <ArduinoJson.h>
#include "RFM69functions.h"
#include <RFM69registers.h>

static volatile bool inInterrupt = false; // Fake Mutex
static volatile uint8_t _mode;

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
      Serial.print(TEMPDATA[5+i]);
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
      if (data4 == 1) {
           watt = (3600000.0f / float(PULSES_PER_KWH) * 1024.0f) / float(power);
      } else if (data4 == 0) { // special mode for low power usage
           watt = power * 0.24f / float(PULSES_PER_KWH);
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
        #endif
      }
      else {
        root["seq"] = seq;
        root["watt"] = float(watt);
        root["total"] = float(pulse) / float(PULSES_PER_KWH);
        root["battery"] = battery;
        root["rssi"] = String(srssi);
        root["power"] = String(power);
        root["pulse"] = String(pulse);
      }
      root.printTo((char*)msg, root.measureLength() + 1);
      client.publish(mqtt_status_topic, msg);
    }

    unselect();
    setMode(RF69_MODE_RX);
  }
Serial.println("Int done");
  inInterrupt = false;
}
