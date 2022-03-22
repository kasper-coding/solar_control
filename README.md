# solar_control
ESP8266 based Pool Controler Software

DIY Solar Pool Controller to be used with motorized valve and solar mats.
The controller will measure current pool and solar temperatures. If final pool temperature is not reached and the solar temperature is higher then pool temperature (plus offset temp) the motor valve (respectively the relay) will be activeted and move the water through the solar mats.
After specified refresh time (default 5 minutes) the temperatures are checked again.
offset, refresh time and final temperature values may be changed via MQTT by writing to specific topic.
Current temperatures and status information will also be displayed on a small website. Watch LCD, serial log or your router for the assigned IP address.
Website is running on default port 80 -> http://ip-address 

# Hardware
1x ESP8266 (I used WEMOS D1 mini clone)
1x Single Channel Relais
2x DS18B20 Temperature Sensors
1x LCD Display HD44780 I2C 
Some (Dupont-) Cables

# Housing 
I designed a 3d Printable Housing for the electronics. (URL will follow)

# Installation
1) Change the the settings to match your needs. 
   Especially Wifi and MQTT URL and topics should be adapted.
2) Just take the .ino file and make sure you have all necessary libraries installed and push to your Wemos (e.g. via Arduino IDE).
3) Run the Sketch with one Sensor connected.
4) Watch Serial Console for Sensor detection and address. 
5) Apply sensor address to corresponing sensor variable (either Pool or Solar).
6) Connect second sensor and repeat #4 and #5.
7) Connect your motor valve to the relais (depends on your setup)
8) Enjoy a warm pool :)
