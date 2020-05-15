whiskeygrid
===========
How I got by the name, I have no idea. But this project is focused on rigging my
home with sensors and automating all kinds of stuff.

These sketches are all written for ESP8266-based hardware. I personally use a lot of
Wemos D1 mini v2's, but anything else based off of an ESP8266 *should* work, and with
some modification these will probably also run on other platforms.

## ClimateNodes
The ClimateNodes are supposed to provide different kinds of climate and air quality
data in similar formats. They communicate via MQTT, and in case of sensors, they all
have integrated support for influx formatting. This means they can publish metrics
to a specific topic in influx line format, allowing you to pass it directly into
InfluxDB, e.g. using something like Telegraf.

### DHT2MQTT
**Hardware:**
* Wemos D1 mini
* DHT22 temperature & RH sensor

**Output:**
* Temperature [°C]
* Relative Humidity [%]

### ClimateNode-DHT22-MHZ19
**Hardware:**
* Wemos D1 mini
* DHT22 temperature & RH sensor
* MH-Z19B CO2 sensor

**Output:**
* Temperature [°C]
* Relative Humidity [%]
* CO2 concentration [ppm]
* Temperature (MH-Z19B) [°C] (influx only)

## SwitchNodes
Things to switch and control other things.

### WiFi-MQTT-LED-controller
This sketch powers a WiFi and MQTT-connected LED controller. It accepts messages in
the format of rgb(X,Y,Z) where XYZ can be anything from 0 to 255, and 1, 2 or 3 digits.
I'm working on adding CCT support and NTP, and I'm still trying to think of a way to
control all the RGBW LED goodness that I'm going to be wiring my walls and ceiling with.
