#include <Arduino.h>
#include <RFM69registers.h>

static volatile uint8_t DATALEN;
#define RF69_MODE_STANDBY 1    // XTAL ON
extern WiFiClient espClient;
extern PubSubClient client(espClient);


uint32_t getFrequency() {
  return RF69_FSTEP * (((uint32_t)readReg(REG_FRFMSB) << 16) + ((uint16_t)readReg(REG_FRFMID) << 8) + readReg(REG_FRFLSB));
}
void receiveBegin() {
  DATALEN = 0;
  //RSSI = 0;
  if (readReg(REG_IRQFLAGS2) & RF_IRQFLAGS2_PAYLOADREADY) {
    uint8_t val = readReg(REG_PACKETCONFIG2);
    // avoid RX deadlocks
    writeReg(REG_PACKETCONFIG2, (val & 0xFB) | RF_PACKET2_RXRESTART);
  }
  setMode(RF69_MODE_RX);
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

// get the received signal strength indicator (RSSI)
//uint16_t readRSSI(bool forceTrigger = false) {  // Settings this to true gives a crash...
uint16_t readRSSI() {
/*  if (forceTrigger) {
    // RSSI trigger not needed if DAGC is in continuous mode
    writeReg(REG_RSSICONFIG, RF_RSSI_START);
          client.publish(mqtt_status_topic, "In rssi read");

    while ((readReg(REG_RSSICONFIG) & RF_RSSI_DONE) == 0x00) {
      // wait for RSSI_Ready
      yield();
    }
  }*/
  rssi = -readReg(REG_RSSIVALUE);
  //Serial.println(rssi);
  //rssi >>= 1;
  //Serial.println(rssi);
  return rssi;
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

uint16_t crc16(volatile uint8_t *data, size_t n) {
  #ifdef DEBUG
    //Serial.println("In crc16");
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
