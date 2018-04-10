bool initialize(uint32_t frequency) {
  Serial.print("In initialize, frequency = ");
  Serial.println(frequency);
  frequency = frequency / RF69_FSTEP;
  Serial.print("Adjusted freq: ");  // Adjusted freq: 14221312
  Serial.println(frequency);
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
