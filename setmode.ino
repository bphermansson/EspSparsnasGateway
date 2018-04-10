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
