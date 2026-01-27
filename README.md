# Tilted

Work in progress hydrometer solution using ESP8266. 

Tilted is very similar to iSpindel, the most popular open-source hydrometer solution, but this project aims to be a simpler, smaller, and more compact version.

The first key difference is in architecture. Instead of connecting the sensor (Tilt) device itself to WiFi, the sensor device instead sends data to a gateway device, which in turns connects to WiFi. This enables two key advantages: First, you can place the gateway close to the sensor itself, which can be advantageous if you have a metal fermentation vessel that hinders the wireless signal. Second, the battery life is greatly improved since the sensor device does not have to worry about anything WiFi related.

Due to the above approach, a conservative estimate of the battery life, assuming most of the uptime is spent in normal mode, would be somewhere around 1 year!

<p align="center">
  <img alt="Gateway" src="docs/images/Gateway.jpg" width="600">
</p>
<p align="center">
  <img alt="Tilted assembled" src="docs/images/Tilted_assembled.jpg" width="600">
</p>
<p align="center">
  <img alt="PET preform" src="docs/images/PET_preform.jpg" width="600">
</p>

## Features
* **Long battery life**, more than a year!
* **Small footprint.**
* **Flexible positioning** due to the sensor device not connecting directly to WiFi.
  * Great for aluminum fermentation vessels.
* **Integrations and settings can be updated even when the sensor is in use.**
  * This is possible because integrations are handled by the gateway device.

## Usage
### setup
The gateway (porter) needs to be configured before use, first time you turn it on it should create a hotspot called "Porter-Setup" you can connect with the password: "beermeplease" when connected it should open a captive portal where you can fill in your wifi, wifi password, polynomial and brewfather url.

once configured the porter will go into listing mode and only connect to your wifi when it has a payload to deliver.

you can put it back into config mode by connecting pin 13 to gnd during boot (press "en" or simple pull the usb cable and reinsert it).

### Calibration mode
Calibration mode can be entered by doing the following:

1. Insert the battery into the sensor device.
2. Within 30 seconds from inserting the battery, place the sensor device with the lid facing down.

This will enable an update interval of 30 seconds for 30 minutes, allowing the user to calibrate the device.

follow the procedure for ispindel calibration and paste the polynomial during setup
the polynomial should look something like this; "0.5013885598189161 + 0.019948730468857152 *tilt"

you can calculate the polynomial here: https://www.ispindel.de/tools/calibration/calibration.htm

## Hardware

Unlike the iSpindel project, Tilted uses a bare ESP-12 module for the sensor device. This has some disadvantages, one of them being that the wiring and initial flashing procedure will be harder since there is no access to USB. The bare module is a hard requirement however since a regular Wemos D1 module is simply too big for the desired footprint. A huge advantage of the bare ESP-12 module is that the battery life of the final product is *greatly* increased.

For the battery, Tilted utilizes the amazing LiFePO4 battery technology. Since LiFePO4 batteries output a nominal voltage of ~3.3V, we can power everything directly from it without using any voltage converters.

### Parts list

For the sensor device itself, the following parts are needed:
* ESP-12F or ESP-12S
  * The S variant might be preferred since it has the required resistors for running built in.
* MPU-6050 Gyro/Accelerometer
* 14500 LiFePO4 battery
  * I personally use Soshine 14500 LiFePO4 batteries, and I highly recommend them.
* 113mm (inner length) x 21mm (inner diameter) PET preform
  * Any type can be used as long as the size is about right. I recommend this: https://www.ebay.com/itm/142622693372.
* 3D printed sled

For the gateway device, you will need these parts:
* TTGO T-Display
  * This is a custom ESP32 board with a TFT display. Can be purchased on Aliexpress.
* 3D printed case

### Wiring the ESP-12

For flashing, the following diagram is sufficient:
![](https://www.allaboutcircuits.com/uploads/articles/20170323-lee-wifieye-circuit-pgm-1.jpg)

For running:

VCC --> EN, MPU VCC, Battery +

GND --> IO15 (pull-down resistor preferred), MPU GND, Battery -

IO16 --> RST

IO4 --> MPU SDA

IO5 --> MPU SCL

## Credits
* [weeSpindel](https://github.com/c-/weeSpindel): I took heavy inspiration from the code, but also the approach as a whole.
* [TTGO T-Display Case](https://www.thingiverse.com/thing:4501444)