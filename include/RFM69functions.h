#define RF69_MODE_SLEEP 0      // XTAL OFF
#define RF69_MODE_STANDBY 1    // XTAL ON
#define RF69_MODE_SYNTH 2      // PLL ON
#define RF69_MODE_RX 3    // RX MODE
#define RF69_MODE_TX 4    // TX MODE
static volatile uint8_t DATA[21];
static volatile uint8_t TEMPDATA[21];
static volatile uint8_t DATALEN;

uint16_t crc16(volatile uint8_t *data, size_t n);
bool receiveDone();

void writeReg(uint8_t addr, uint8_t value);
uint8_t readReg(uint8_t addr);
uint32_t getFrequency();
