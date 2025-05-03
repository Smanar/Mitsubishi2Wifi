WIP project, it's a firmware for ESP32 microcontrollers supporting UART communication via the CN105 Mitsubishi connector to control their HVAC unit using Wifi but without MQTT (like others projects), only using REST request and JSON.   
It use SwiCago libraries: https://github.com/SwiCago/HeatPump    
This component version is based on https://github.com/gysmo38/mitsubishi2MQTT   

## Description   

You still can use the embedded webserver for configuration and communication

<p align="center">
  <img src="https://raw.githubusercontent.com/Smanar/Ressources/refs/heads/main/pictures/mitsubishi_CN105.png">
</p>

But can use json to send command, using the endpoint /json.   
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
You can found a french tutorial here https://www.domotique-fibaro.fr/topic/16280-quick-app-pilotage-climatisation-pac-mitsubishi-en-local-avec-esp32/

## Configuration

Take a look in the platformio.ini, you can found some informations, like other GPIO pin selected.   
The code is configured by defaut for the ESP32 Wemos D1 Mini, with wire on GPIO 16 and 17.   

On first launch, you will be able to connect to the device using for exemple your smartphone, using Wifi:   
- Connect to AP called HVAC_XXXX (XXXX last 4 character MAC address).
- Go to url http://192.168.1.1
- Set your Wifi setting, save & reboot.

The device will now connect to your Wifi network.   
