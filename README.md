# EspSparsnasGateway

This is a MQTT Gateway for Ikeas energy monitor Sparsnas. The monitor
sends encoded data by radio to a control panel. This device collects the data
and sends it to MQTT-enabled receivers in json-format.

The data is also printed to the serial port. If the reception is bad, the received data can be bad.
This gives a CRC-error, the data is in this case not sent via MQTT but printed via the serial port.

The data sent via MQTT is in json format and looks like this:

```json
{
    "error": "",
    "seq": 28767,
    "timestamp": 1592040611,
    "watt": 1920,
    "total": 15016,
    "battery": 100,
    "rssi": -123,
    "power": 80,
    "pulse": 150160049
}
```

The device uses two Mqtt topics to publish, `EspSparsnasGateway/<sensor_id>/state` and `EspSparsnasGateway/debugV2`.

## Dependencies

This requires the following packages:

- ArduinoJson (5.x)
- PubSubClient (2.7)
- SPIFlash_LowPowerLabs (101.1)
- RFM69_LowPowerLabs (1.2.0)

Packages can be installed using the Arduino libs, see the [docs](https://www.arduino.cc/en/guide/libraries) for more info

## Using
Load the project files in Atom/VS Code with PlatformIO. Then copy the file `include/settings.example.h` to `include/settings.h`. Adjust the values in `settings.h` to fit your environment and save it. Upload to your hardware and enjoy :)

## Hardware
The hardware used is a ESP8266-based wifi-enabled MCU. You can use different devices like a Wemos D1mini or a NodeMCU, but take care of the GPIO labels that can differ. The receiver is a RFM69HCW radio transciever (the RFMCW also works but note that it have a different [pinout](https://github.com/bphermansson/EspSparsnasGateway/raw/master/refrence_doc/RFM69HC_D1mini.png)). I use a 868MHz device, but a 900MHz should work as well. To this a simple antenna is connected, I use a straight wire, 86 millimeters long connected to the RFM's Ant-connection. The wire shall be vertical, standing up. You can also add a similar wire to the GND-connection next to the antenna connection, pointing down, opposite to the first wire.

The connection for the RFM69 is hardcoded. This is standard SPI connections set in the SPI-library that can't be changed. See https://learn.sparkfun.com/tutorials/esp8266-thing-hookup-guide/using-the-arduino-addon.

The schematic shows a NodeMCU, but you can use another ESP8266-based device if you want (except the Esp-01). Use these pin mappings:

| RFM69  | RFM69**H**CW | RFM69**CW** | D1mini/<br/>NodeMCU | ESP8266/<br/>ESP12/<br/>ESP32 |
|--------|:--:|:-:|:--:|:------:|
| DIO0   | 14 | 9 | D1 | Gpio05 |
| SCK    |  4 | 6 | D5 | Gpio14 |
| MISO   |  2 | 8 | D6 | Gpio12 |
| MOSI   |  3 | 5 | D7 | Gpio13 |
| EN/NSS |  5 | 7 | D8 | Gpio15 |
| ANT    |  9|  1 | - | - |

![Wiring diagram](https://github.com/bphermansson/EspSparsnasGateway/raw/master/refrence_doc/RFM69HCW_NodeMCU.png)

Note! Adafruit modules requires a connection from RST to GND! (Ref: https://www.mysensors.org/build/connect_radio#wiring-the-rfm69-radio).

### LEDs

The code also supports three optional LEDs indicating status connected as follows:

| LED | D1mini/<br/>NodeMCU | Usage |
|:-|:-:|:-|
| RED | D0 | Error (typically CRC errors) |
| GREEN | D3 | Used during boot |
| BLUE | D2 | Indicates successfully recieved package|

It is possible to use the onboard (blue) LED on the D1mini by changing `#define LED_BLUE D2` to `#define LED_BLUE D4`.

### Parts
You can build your own device using these parts: (To see the language specific page make sure to select the language at the top of the page or it will give a 404.)

U1 - NodeMCU V3
https://www.lawicel-shop.se/microkontroller/esp8266-esp32/nodemcu-v3-with-esp-12e-ch340

Part1 - RFM69HCW
https://www.lawicel-shop.se/rfm69hcw-transceiver-bob

#### Optional (but highly recommended)
C2 - Capacitor 100nF
se: https://www.lawicel-shop.se/elektronik/komponenter/kondensatorer/capacitor-100nf-10-pack
en: https://www.lawicel-shop.se/components/komponenter/capacitors/capacitor-100nf-10-pack

C1 - Capacitor 1000uF
se: https://www.lawicel-shop.se/components/komponenter/capacitors/capacitor-1000uf-50v
en: https://www.lawicel-shop.se/elektronik/komponenter/kondensatorer/capacitor-1000uf-50v

Also add two capacitors, 330-470uF and 100nF, to Vin in and Gnd for stability.

### Hardware hacks to ensure good RF performance.
Keep the wires between the RFM69 module and the NodeMCU as short as possible and DO NOT make them 8 cm long hence that calculates into 1/4 wavelength of 868 MHz.
You will experience interference and very poor performance if the above is not applied and followed.

If you want to learn more about the RFM69 and get some tips & tricks, look at https://learn.sparkfun.com/tutorials/rfm69hcw-hookup-guide.

### If it doesn't work
As usual, check your connections one more time. If possible, solder the connections. Also make sure to use a good power supply, both the ESP and the RFM69 want's that.

### Connect to computer
You can use the device with a simple USB power supply and get data via MQTT. The device also puts out more information via the serial port. You can connect it to a computer and look at the messages with a serial monitor, for example the one in the Arduino IDE or Minicom. The baudrate is 115200.

### Enable debug
Further more information is given by the device if debug is activated:

```c++
#define DEBUG 1
```

### Change the channel filter width
With some RFM's a software adjustment can be tested if the code doesn't work. Line 213 in RFM69functions.cpp looks like this:

```c++
/* 0x19 */ {REG_RXBW, RF_RXBW_DCCFREQ_010 | RF_RXBW_MANT_16 | RF_RXBW_EXP_4}, // p26 in datasheet, filters out noise
```

You can try to change this to:

```c++
/* 0x19 */ {REG_RXBW, RF_RXBW_DCCFREQ_010 | RF_RXBW_MANT_16 | RF_RXBW_EXP_3}, // p26 in datasheet, filters out noise
```

This makes the channel filter wider, 62.5khz instead of 31.3khz.

## Home Assistant integration
Sensors for power (Watt) and energy (kWh) will be created automatically if Home Assistant is configured to support [discovery](https://www.home-assistant.io/docs/mqtt/discovery/#discovery).
The MQTT data can however be used anywhere, here's an example for the Home Automation software Home Assistant.
In Home Assistant the sensors can look like this:

```yaml
- platform: mqtt
  state_topic: "EspSparsnasGateway/+/state"
  name: "House power usage"
  unit_of_measurement: "W"
  value_template: '{{ float(value_json.watt) | round(0)  }}'

- platform: mqtt
  state_topic: "EspSparsnasGateway/+/state"
  name: "House energy usage"
  unit_of_measurement: "kWh"
  value_template: '{{ float(value_json.total) | round(0)  }}'

- platform: mqtt
  state_topic: "EspSparsnasGateway/+/state"
  name: "House energy meter batt"
  unit_of_measurement: "%"
  value_template: '{{ float(value_json.battery) }}'
```

Wich results in these sensors:

```yaml
- sensor.house_energy_meter_batt
- sensor.house_energy_usage
- sensor.house_power_usage
```

The result can be seen in SparsnasHass.png.

![alt text](https://github.com/bphermansson/EspSparsnasGateway/blob/master/SparsnasHass.png "Sparsnas in Home Assistant")

## Protocol analysis
For much more information about the hardware, the protocol and how to analyse the transmission, see
Kodarn's Github, https://github.com/kodarn/Sparsnas.

## Thanks!
The code is based on Sommarlovs version of Ludvig Strigeus code:
http://elektronikforumet.com/forum/viewtopic.php?f=2&t=85006&start=255
Strigeus original code for use with a DVB-T Usb dongle:
https://github.com/strigeus/sparsnas_decoder
