String mess;
const char* mqtt_pub_topic = "EspSparsnasGateway/values";
const char* mqtt_status_topic = "EspSparsnasGateway/status";
const char* mqtt_debug_topic = "EspSparsnasGateway/debug";
const char* mqtt_sub_topic_freq = "EspSparsnasGateway/settings/frequency";
const char* mqtt_sub_topic_senderid = "EspSparsnasGateway/settings/senderid";
const char* mqtt_sub_topic_clear = "EspSparsnasGateway/settings/clear";
const char* mqtt_sub_topic_reset = "EspSparsnasGateway/settings/reset";
const char compile_date[] = __DATE__ " " __TIME__;

#define RF69_MODE_SLEEP 0      // XTAL OFF
#define RF69_MODE_STANDBY 1    // XTAL ON
#define RF69_MODE_SYNTH 2      // PLL ON
#define RF69_MODE_RX 3    // RX MODE
#define RF69_MODE_TX 4    // TX MODE

uint32_t FXOSC = 32000000;
uint32_t TwoPowerToNinteen = 524288; // 2^19
float RF69_FSTEP = (1.0 * FXOSC) / TwoPowerToNinteen; // p13 in datasheet
uint32_t FREQUENCY = 868000000;
uint16_t BITRATE = FXOSC / 40000; // 40kBps
uint16_t FREQUENCYDEVIATION = 10000 / RF69_FSTEP; // 10kHz
uint16_t SYNCVALUE = 0xd201;
uint8_t RSSITHRESHOLD = 0xE4; // must be set to dBm = (-Sensitivity / 2), default is 0xE4 = 228 so -114dBm
uint8_t PAYLOADLENGTH = 20;

#define _interruptNum 5
void  ICACHE_RAM_ATTR interruptHandler();

static volatile uint8_t DATA[21];
static volatile uint8_t TEMPDATA[21];
static volatile uint8_t DATALEN;
//static volatile uint16_t RSSI; // Most accurate RSSI during reception (closest to the reception)
static volatile uint8_t _mode;
static volatile bool inInterrupt = false; // Fake Mutex
uint8_t enc_key[5];
uint16_t rssi = 0;
uint16_t readRSSI();
char output[255];
