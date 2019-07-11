// Set this to the value of your energy meter
#define PULSES_PER_KWH 1000
// The code from the Sparnas tranmitter. Under the battery lid there's a sticker with digits like '400 643 654'.
// Set SENSOR_ID to the last six digits, ie '643654'.
// You can also set this later via Mqtt settings message, see docs.
#define SENSOR_ID 643654
#define DEBUG 1
#define APPNAME "EspSparsnasGatewayV2"
#define WIFI_SSID "NETGEAR83"
#define WIFI_PASSWORD ""
#define MQTT_USERNAME "emonpi"
#define MQTT_PASSWORD "emonpimqtt2016"
#define MQTT_SERVER "192.168.1.190"
#define SERIAL_SPEED 115200
#define MQTT_PUB_TOPIC "EspSparsnasGatewayV2"
