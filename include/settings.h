
#define DEBUG 1

// Set this to the value of your energy meter
#define PULSES_PER_KWH 1000
// The code from the Sparnas tranmitter. Under the battery lid there's a sticker with digits like '400 643 654'.
// Set SENSOR_ID to the last six digits, ie '643654'.
#define SENSOR_ID 643654
//uint32_t FREQUENCY 868000000;
#define FREQUENCY 868000000

// Wifi settings
#define WIFISSID "NETGEAR83"
#define WIFIPASSWORD ""
//const char* password = "";

// Settings for the Mqtt broker:
#define MQTT_USERNAME "emonpi"
#define MQTT_PASSWORD "emonpimqtt2016"
#define mqtt_server "192.168.1.190"

#define appname "EspSparsnasGateway"
