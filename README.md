# EspSparsnasGateway with second transmitter id

This is a Mqtt Gateway for Ikeas energy monitor Sparsnas. The monitor
sends encoded data by radio to a control panel. This device collects the data
and sends it to Mqtt-enabled receivers in Json-format.

The data is also printed to the seriaĺ port. If the reception is bad, the received data can be bad.
This gives a CRC-error, the data is in this case not sent via Mqtt but printed via the serial port.

The data sent via Mqtt is in Json format and looks like this:

```
 {"seq":63009,"watt":5120,"total":5985,"battery":100,"rssi":"-136","power":"720","pulse":"5985167"}

```

The device uses two Mqtt topics to publish, EspSparsnasGateway/values and EspSparsnasGateway/debug.

## Using
Load the project files in Atom/VS Code with PlatformIO. Then copy the file "include/settings.example" to "include/settings.h". Adjust the values in settings.h to fit your environment and save it. Upload to your hardware and enjoy :)

## Hardware
The hardware used is a Esp8266-based wifi-enabled Mcu. You can use different devices like a Wemos Mini or a Nodemcu, but take care of the Gpio labels that can differ. The receiver is a RFM69B radio transciever. I use a 868MHz device, but a 900MHz should work as well. To this a simple antenna is connected, I use a straight wire, 86 millimeters long connected to the RFM's Ant-connection. The wire shall be vertical, standing up. You can also add a similar wire to the gnd-connection next to the antenna connection, pointing down, opposite to the first wire.

The connection for the RFM69 is hardcoded. This is standard Spi connections set in the spi-library that can't be changed. See https://learn.sparkfun.com/tutorials/esp8266-thing-hookup-guide/using-the-arduino-addon.

The schematic shows a Nodemcu, but you can use another ESP8266-based device if you want (except the Esp-01). Use these pin mappings:

```
NodeMcu - Esp12

D0	- Gpio5  - Gpio16 - LED red
D1	- Gpio5  - DIO0
D2	- Gpio4  - LED green
31	- Gpio0  - LED blue
D5	- Gpio14 - SCK
D6	- Gpio12 - MISO
D7	- Gpio13 - MOSI
D8	- Gpio15 - EN
``` 

![Wiring diagram](https://github.com/bphermansson/EspSparsnasGateway/raw/master/EspSparsnasGateway_schem_Nodemcu.png)

Note! Adafruit modules requires a connection from RST to GND! (Ref: https://www.mysensors.org/build/connect_radio#wiring-the-rfm69-radio).

### Parts
You can build your own device using these parts: (To see the language specific page make sure to select the language at the top of the page or it will give a 404.)

U1 - Nodemcu V3
https://www.lawicel-shop.se/microkontroller/esp8266-esp32/nodemcu-v3-with-esp-12e-ch340

Part1 - RFM69HCW
https://www.lawicel-shop.se/rfm69hcw-transceiver-bob

C2 - Capacitor 100nF
se: https://www.lawicel-shop.se/elektronik/komponenter/kondensatorer/capacitor-100nf-10-pack

C1 - Capacitor 1000uF
se: https://www.lawicel-shop.se/elektronik/komponenter/kondensatorer/capacitor-1000uf-50v

L1 - Inductor 100uH se: https://www.electrokit.com/drossel-100uh.42127

D1 - LED - red se: https://www.electrokit.com/produkt/led-3mm-rod-diffus-3500mcd/

D2 - LED - green se: https://www.electrokit.com/produkt/led-3mm-gron-diffus-3500mcd/

D3 - LED - blue se: https://www.electrokit.com/produkt/led-3mm-bla-diffus-3500mcd/

R1 -R3 - Resistor 220 ohm se: https://www.electrokit.com/produkt/motstand-metallfilm-0-125w-1-220ohm-220r/


## Hardware hacks to ensure good RF performance.
Also add two capacitors, 330-470uF and 100nF, to Vin in and Gnd for stability.
Keep the wires between the RFM69 module and the NodeMCU as short as possible and DO NOT make them 8 cm long hence that calculates into 1/4 wavelength of 868 MHz.
You will experience interference and very poor performance if the above is not applied and followed.

If you want to learn more about the Rfm69 and get some tips & tricks, look at https://learn.sparkfun.com/tutorials/rfm69hcw-hookup-guide.

## If it doesn't work
As usual, check your connections one more time. If possible, solder the connections. Also make sure to use a good power supply, both the Esp and the Rfm69 want's that.

### Connect to computer
You can use the device with a simple USB power supply and get data via Mqtt. The device also puts out more information via the serial port. You can connect it to a computer and look at the messages with a serial monitor, for example the one in the Arduino IDE or Minicom. The baudrate is 115200.

### Enable debug
Further more information is given by the device if debug is activated:

```
#define DEBUG 1
```

### Change the channel filter width
With some RFM's a software adjustment can be tested if the code doesn't work. Line 213 in RFM69functions.cpp looks like this:

```
/* 0x19 */ {REG_RXBW, RF_RXBW_DCCFREQ_010 | RF_RXBW_MANT_16 | RF_RXBW_EXP_4}, // p26 in datasheet, filters out noise
```

You can try to change this to:

```
/* 0x19 */ {REG_RXBW, RF_RXBW_DCCFREQ_010 | RF_RXBW_MANT_16 | RF_RXBW_EXP_3}, // p26 in datasheet, filters out noise
```

This makes the channel filter wider, 62.5khz instead of 31.3khz.

## Home Assistant integration
The Mqtt data can be used anywhere, here's an example for the Home Automation software Home Assistant.
In Home Assistant the sensors can look like this:

```
- platform: mqtt
  state_topic: "EspSparsnasGateway/valuesV2"
  name: "House power usage"
  unit_of_measurement: "W"
  value_template: '{{ float(value_json.watt) | round(0)  }}'

- platform: mqtt
  state_topic: "EspSparsnasGateway/valuesV2"
  name: "House energy usage"
  unit_of_measurement: "kWh"
  value_template: '{{ float(value_json.total) | round(0)  }}'
    

- platform: mqtt
  state_topic: "EspSparsnasGateway/valuesV2"
  name: "House energy meter batt"
  unit_of_measurement: "%"
  value_template: '{{ float(value_json.battery) }}'
```

We then get these sensors:

```
-sensor.house_energy_meter_batt
-sensor.house_energy_usage
-sensor.house_power_usage
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
