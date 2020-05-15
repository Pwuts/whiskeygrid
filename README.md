whiskeygrid
===========
How I got by the name, I have no idea. But this project is focused on rigging my
home with sensors and automating all kinds of stuff.

## Arduino sketches
These sketches are all written for ESP8266-based hardware. I personally use a lot of
Wemos D1 mini v2's, but anything else based off of an ESP8266 should work, and with
some modification these will probably also run on other platforms.

### DHT2MQTT
This sketch runs on a Wemos D1 mini with a DHT22 temperature and relative humidity
sensor. It regularly measures temperature and RH and reports to the specified
channels on MQTT.

# WiFi-MQTT-LED-controller
It accepts messages in the format of rgb(X,Y,Z) where XYZ can be anything from 0 to 255, and 1, 2 or 3 digits. Patches welcome! :)
