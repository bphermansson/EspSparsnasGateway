uint16_t readRSSI();
uint16_t crc16(volatile uint8_t *data, size_t n);
bool receiveDone();
void writeReg(uint8_t addr, uint8_t value);
uint8_t readReg(uint8_t addr);
void setMode(uint8_t newMode);
uint32_t getFrequency();
bool initialize(uint32_t frequency);
