WIP project, it's a firmware for ESP32 microcontrollers supporting UART communication via the CN105 Mitsubishi connector to control their HVAC unit using Wifi but without MQTT (like others projects), only using REST request and JSON.   
It use SwiCago libraries: https://github.com/SwiCago/HeatPump    
This component version is based on https://github.com/gysmo38/mitsubishi2MQTT   

## Description   

You still can use the embedded webserver for configuration and communication, but can use json to send command, using the endpoint /json.   
```
curl http://127.0.0.1:81/json -X POST -d '{"power": "on"}'
```
And the device send json to a server when a change happen
```
{
   "temperature":17,
   "fan":"QUIET",
   "vane":"4",
   "widevane":"|",
   "mode":"HEAT",
   "power":"OFF",
   "roomTemperature":17,
   "compressorFrequency":0,
   "action":false
}
```


## Hardware

On my side I m using ESP32 Wemos D1 Mini, from here https://fr.aliexpress.com/item/32858054775.html direclty powered by the unity, this device can support 3.3V and 5V.   

## Configuration

Take a look in the platformio.ini, you can found some informations, like other GPIO pin selected.   
The code is configured by defaut for the ESP32 Wemos D1 Mini, with wire on GPIO 16 and 17.   
