View this project on [CADLAB.io](https://cadlab.io/project/1763). 

# PSU
Software-controlled variable-voltage power supply with current and power monitoring.

Features:
- Common voltages preset through web interface (In Progress)
- TFT LCD to display output voltage and current
- Digital Potentiometer for software-controlled adjustment (TODO)

## Hardware
Display hardware as [WifiWeatherGuy](https://cadlab.io/project/1280/master/files):
- [Wemos D1 Mini](https://wiki.wemos.cc/products:d1:d1_mini)
- PCB-mounted push switch
- 1.8" TFT LCD display
- v3 PCB (with pin headers)

PSU hardware:
- [LM317](https://en.wikipedia.org/wiki/LM317) variable voltage regulator
- [INA219](https://www.adafruit.com/product/904) voltage and current sensor
- [X9C103P](https://www.renesas.com/us/en/products/data-converters/digital-potentiometers/dcp/device/X9C103.html) digital potentiometer
- Old laptop transformer (19v)

## Software
- [Arduino](https://www.arduino.cc/en/main/software) 1.8.x
- [Arduino/ESP8266](https://github.com/esp8266/Arduino) 2.7.1
- [ArduinoJson](https://arduinojson.org) 6.15.1
- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) 2.2.8
- [Adafruit_INA219](https://github.com/adafruit/Adafruit_INA219) 1.0.4
- [Arduino-X9C](https://github.com/philbowles/Arduino-X9C) 0.9.0
- [SimpleTimer](https://github.com/schinken/SimpleTimer)
- [Stator](https://github.com/PTS93/Stator)
