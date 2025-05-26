/*
  mitsubishi2Wifi Copyright (c) 2024 Smanar

  Based on mitsubishi2mqtt Copyright (c) 2022 gysmo38, dzungpv, shampeon, endeavour,
  jascdk, chrdavis, alekslyse. All rights reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.
  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.
  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/


const PROGMEM char* m2wifi_version = "2024.0.0";

//Define global variables for files
#ifdef ESP32
const PROGMEM char* wifi_conf = "/wifi.json";
const PROGMEM char* server_conf = "/server.json";
const PROGMEM char* unit_conf = "/unit.json";
const PROGMEM char* console_file = "/console.log";
const PROGMEM char* others_conf = "/others.json";
// pinouts
const PROGMEM  uint8_t blueLedPin = 2;            // The ESP32 has an internal blue LED at D2 (GPIO 02)
#else
const PROGMEM char* wifi_conf = "wifi.json";
const PROGMEM char* server_conf = "mqtt.json";
const PROGMEM char* unit_conf = "unit.json";
const PROGMEM char* console_file = "console.log";
const PROGMEM char* others_conf = "others.json";
// pinouts
const PROGMEM  uint8_t blueLedPin = LED_BUILTIN; // Onboard LED = digital pin 2 "D4" (blue LED on WEMOS D1-Mini)
#endif

//const PROGMEM  uint8_t redLedPin = 0;

// Define global variables for network
const PROGMEM char* hostnamePrefix = "HVAC_";
const PROGMEM uint32_t WIFI_RETRY_INTERVAL_MS = 300000;
unsigned long wifi_timeout;
bool wifi_config_exists;
String hostname = "";
String ap_ssid;
String ap_pwd;
String ota_pwd;

// Define global variables for server
String server_url;

//login
String login_username = "admin";
String login_password;

// debug mode logs, when true, will send all debug messages to topic heatpump_debug_logs_topic
// this can also be set by sending "on" to heatpump_debug_set_topic
bool _debugModeLogs = false;
// debug mode packets, when true, will send all packets received from the heatpump to topic heatpump_debug_packets_topic
// this can also be set by sending "on" to heatpump_debug_set_topic
bool _debugModePckts = false;

// Customization
uint8_t min_temp                    = 16; // Minimum temperature, in your selected unit, check value from heatpump remote control
uint8_t max_temp                    = 31; // Maximum temperature, in your selected unit, check value from heatpump remote control
String temp_step                   = "1"; // Temperature setting step, check value from heatpump remote control

// sketch settings
const PROGMEM uint32_t PREVENT_UPDATE_INTERVAL_MS = 3000;  // interval to prevent application setting change after send settings to HP
const PROGMEM uint32_t SEND_ROOM_TEMP_INTERVAL_MS = 300000; // 5 mn, anything less than 45 seconds may cause bouncing
const PROGMEM uint32_t CHECK_REMOTE_TEMP_INTERVAL_MS = 300000; //5 minutes
const PROGMEM uint32_t MQTT_RETRY_INTERVAL_MS = 1000; // 1 second
const PROGMEM uint32_t HP_RETRY_INTERVAL_MS = 1000; // 1 second
const PROGMEM uint32_t HP_MAX_RETRIES = 10; // Double the interval between retries up to this many times, then keep retrying forever at that maximum interval.
// Default values give a final retry interval of 1000ms * 2^10, which is 1024 seconds, about 17 minutes. 

// temp settings
bool useFahrenheit = false;
// support heat mode settings, some model do not support heat mode
bool supportHeatMode = true;
