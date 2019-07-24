
// Set this to the value of your energy meter
#define PULSES_PER_KWH 1000
// The code from the Sparnas tranmitter. Under the battery lid there's a sticker with digits like '400 643 654'.
// Set SENSOR_ID to the last six digits, ie '643654'.
#define SENSOR_ID 643654
//uint32_t FREQUENCY 868000000;
#define FREQUENCY 868000000;

// Settings for the Mqtt broker:
#define MQTT_USERNAME "emonpi"
#define MQTT_PASSWORD "emonpimqtt2016"
const char* mqtt_server = "192.168.1.190";

// Wifi settings
const char* ssid = "NETGEAR83";
const char* password = "";

#define appname "EspSparsnasGateway"

const char* mqtt_status_topic = "EspSparsnasGateway/values";
const char* mqtt_debug_topic = "EspSparsnasGateway/debug";
const char* mqtt_sub_topic_freq = "EspSparsnasGateway/settings/frequency";
const char* mqtt_sub_topic_senderid = "EspSparsnasGateway/settings/senderid";
const char* mqtt_sub_topic_clear = "EspSparsnasGateway/settings/clear";
const char* mqtt_sub_topic_reset = "EspSparsnasGateway/settings/reset";
