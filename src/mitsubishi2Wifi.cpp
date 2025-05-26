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


#include "mitsubishi2Wifi.h"
#include "util.h"

#include "FS.h"               // SPIFFS for store config
#ifdef ESP32
#include <WiFi.h>             // WIFI for ESP32
#include <WiFiUdp.h>
#include <ESPmDNS.h>          // mDNS for ESP32
#include <WebServer.h>        // webServer for ESP32
#include <HTTPClient.h>
WebServer server(80);         //ESP32 web
#else
#include <ESP8266WiFi.h>      // WIFI for ESP8266
#include <WiFiClient.h>
#include <ESP8266mDNS.h>      // mDNS for ESP8266
#include <ESP8266WebServer.h> // webServer for ESP8266
#include <ESP8266HTTPClient.h> // webClient for ESP8266
ESP8266WebServer server(80);  // ESP8266 web
#endif

//Because SPIFFS is obsolette
#if 0
    #include "SPIFFS.h"           // ESP32 SPIFFS for store config
#else
    #include <LittleFS.h>
    #define SPIFFS LittleFS
#endif

#include <ArduinoJson.h>      // json to process MQTT: ArduinoJson 6.11.4
#include <DNSServer.h>        // DNS for captive portal
#include <math.h>             // for rounding to Fahrenheit values
#include <map>
#include <cmath>              // For roundf function

#include <ArduinoOTA.h>   // for OTA
//#include <Ticker.h>     // for LED status (Using a Wemos D1-Mini)
//void tick(); // led blink tick

#include "config.h"             // config file
#include "html/html_common.h"        // common code HTML (like header, footer)
#include "html/javascript_common.h"  // common code javascript (like refresh page)
#include "html/html_init.h"          // code html for initial config
#include "html/html_menu.h"          // code html for menu
#include "html/html_pages.h"         // code html for pages
#include "html/html_metrics.h"       // prometheus metrics

// wifi, http and heatpump client instances
WiFiClient espClient;
HTTPClient http;

//Captive portal variables, only used for config page
const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 1, 1);
IPAddress netMsk(255, 255, 255, 0);
DNSServer dnsServer;

boolean captive = false;
boolean wifi_config = false;
boolean remoteTempActive = false;

//HVAC
HeatPump hp;
unsigned long lastTempSend;
unsigned long lastHpSync;
unsigned int hpConnectionRetries;
unsigned int hpConnectionTotalRetries;
unsigned long lastRemoteTemp;

//Local state
StaticJsonDocument<JSON_OBJECT_SIZE(12)> rootInfo;

//Used to send json
bool SendJson(const JsonVariant);

//Web OTA
int uploaderror = 0;

// To be optimised
String LogString;

const char compile_date[] = __DATE__ " " __TIME__;

void setup() {
  // Start serial for debug before HVAC connect to serial
  Serial.begin(115200);

  write_log(F("Starting Application"));

  // Mount SPIFFS filesystem
  if (SPIFFS.begin())
  {
    write_log(F("Mounted file system mounted"));
  }
  else
  {
    write_log(F("Failed to mount FS -> formating"));
    SPIFFS.format();
    if (SPIFFS.begin())
      write_log(F("Mounted file system after formating"));
  }

  //set led pin as output
  pinMode(blueLedPin, OUTPUT);
  //Blink every second
  //ticker.attach(1, tick);

  //Define defaut hostname
  hostname = hostnamePrefix;
  hostname += getId();

  setWIFIDefaults();
  wifi_config_exists = loadWifi();

//Workaround for not working device in Access point
#if defined(WIFIPASSWORD) && defined(WIFISSID)
    write_log(F("Force SSID and Password."));
    saveWifi(WIFISSID,WIFIPASSWORD,"ForcedHVAC","");
    wifi_config_exists = loadWifi();
#endif

  if (!wifi_config_exists)
  {
    write_log(F("Can't load Wifi settings"));
  }
  if (!loadOthers())
  {
    write_log(F("Can't load Others settings"));
  }
  if (!loadUnit())
  {
    write_log(F("Can't load Unit settings"));
  }
  if (!loadServerSettings())
  {
    write_log(F("Can't load server settings"));
  }

#ifdef ESP32
  WiFi.setHostname(hostname.c_str());
#else
  WiFi.hostname(hostname.c_str());
#endif

  if (initWifi())
  {
    //Reset the log file
    if (SPIFFS.exists(console_file)) {
      SPIFFS.remove(console_file);
    }

    //Web interface
    if (login_password.length() > 0)
    {
      server.on("/login", handleLogin);
      //here the list of headers to be recorded, use for authentication
      const char * headerkeys[] = {"User-Agent", "Cookie"} ;
      size_t headerkeyssize = sizeof(headerkeys) / sizeof(char*);
      //ask server to track these headers
      server.collectHeaders(headerkeys, headerkeyssize);
    }
    server.on("/", handleRoot);
    server.on("/control", handleControl);
    server.on("/setup", handleSetup);
    server.on("/server", handleServer);
    server.on("/wifi", handleWifi);
    server.on("/unit", handleUnit);
    server.on("/status", handleStatus);
    server.on("/others", handleOthers);
    server.on("/metrics", handleMetrics);
    server.on("/upgrade", handleUpgrade);
    server.on("/logs", handleLogs);
    server.on("/json", handleJson);
    server.on("/upload", HTTP_POST, handleUploadDone, handleUploadLoop);
    server.onNotFound(handleNotFound);

    server.begin();

    lastHpSync = 0;
    hpConnectionRetries = 0;
    hpConnectionTotalRetries = 0;

    write_log(F("Connection to HVAC. Stop serial log."));
    write_log(F("\n\n\n"));
    delay(1000);

    // Used for Auto Update
    hp.setSettingsChangedCallback(hpSettingsChanged); // Called when Settings are changed
    hp.setStatusChangedCallback(hpStatusChanged); // Called when Status is changed
    hp.setPacketCallback(hpPacketDebug); // Called to output debug

    // Allow Remote/Panel
    hp.enableExternalUpdate();
    // Enable auto update
    hp.enableAutoUpdate();

    // Connection
#if defined(ESP32)
#ifdef RX_PIN
    hp.connect(&Serial, RX_PIN, TX_PIN);
#else
    hp.connect(&Serial);
#endif
    //esp_log_level_set("*", ESP_LOG_NONE); // disable all logs because we use UART0 connect to HP
#else
    hp.connect(&Serial);
#endif
    // Get default values.
    hpSettingsChanged();
    hpStatusChanged(hp.getStatus());

  }
  else
  {
    server.on("/", handleInitSetup);
    server.on("/save", handleSaveWifi);
    server.on("/reboot", handleReboot);
    server.onNotFound(handleNotFound);
    server.begin();
    captive = true;
  }

  initOTA();

}

void tick()
{
  // toggle state
  int state = digitalRead(blueLedPin); // get the current state of GPIO2 pin
  digitalWrite(blueLedPin, !state);    // set pin to the opposite state
}

bool SendJson(const JsonVariant j) {
  String s; // LEAK ?
  serializeJson(j, s);

  http.setTimeout(2000);
  http.begin(espClient, server_url.c_str());
  http.addHeader("Content-Type", "application/json");
  //http.addHeader("Accept-Encoding", "identity");
  int httpResponseCode = http.POST(s);

  //http.beginRequest();
  //http.post("/");
  //http.sendHeader("Content-Type", "application/json");
  //http.sendHeader("Content-Length", s.length());
  //http.beginBody();
  //http.print(s);
  //http.endRequest();

  //Wait to be sure data are send, because end() force a clear
  delay(500);

  http.end();

  return true;
}

bool loadWifi() {

  ap_ssid = "";
  ap_pwd  = "";

/*
  write_log(F("Listing file"));
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
 
  while(file){
 
      write_log("FILE: ");
      write_log(file.name());
 
      file = root.openNextFile();
  }
*/

  if (!SPIFFS.exists(wifi_conf)) {
    write_log(F("Wifi config file not exist!"));
    return false;
  }
  File configFile = SPIFFS.open(wifi_conf, "r");
  if (!configFile) {
    write_log(F("Failed to open wifi config file"));
    return false;
  }
  size_t size = configFile.size();
  if (size > 1024) {
    write_log(F("Wifi config file size is too large"));
    return false;
  }

  // Allocate a buffer to store contents of the file.
  std::unique_ptr<char[]> buf(new char[size]);
  configFile.readBytes(buf.get(), size);
  const size_t capacity = JSON_OBJECT_SIZE(4) + 130;
  DynamicJsonDocument doc(capacity);
  deserializeJson(doc, buf.get());

  //write_log(doc.as<String>());

  hostname = doc["hostname"].as<String>();
  ap_ssid  = doc["ap_ssid"].as<String>();
  ap_pwd   = doc["ap_pwd"].as<String>();
  //prevent ota password is "null" if not exist key
  if (doc.containsKey("ota_pwd")) {
    ota_pwd  = doc["ota_pwd"].as<String>();
  } else {
    ota_pwd = "";
  }

  return true;
}

bool loadServerSettings() {
  if (!SPIFFS.exists(server_conf)) {
    //write_log(F("Server config file not exist!"));
    return false;
  }
  //write_log("Loading Server configuration");
  File configFile = SPIFFS.open(server_conf, "r");
  if (!configFile) {
    //write_log(F("Failed to open Server config file"));
    return false;
  }

  size_t size = configFile.size();
  if (size > 1024) {
    //write_log(F("Config file size is too large"));
    return false;
  }
  std::unique_ptr<char[]> buf(new char[size]);

  configFile.readBytes(buf.get(), size);
  const size_t capacity = JSON_OBJECT_SIZE(6) + 400;
  DynamicJsonDocument doc(capacity);
  deserializeJson(doc, buf.get());

  server_url          = doc["server_url"].as<String>();

  return true;
}

bool loadUnit() {
  if (!SPIFFS.exists(unit_conf)) {
    // write_log(F("Unit config file not exist!"));
    return false;
  }
  File configFile = SPIFFS.open(unit_conf, "r");
  if (!configFile) {
    return false;
  }

  size_t size = configFile.size();
  if (size > 1024) {
    return false;
  }
  std::unique_ptr<char[]> buf(new char[size]);

  configFile.readBytes(buf.get(), size);
  const size_t capacity = JSON_OBJECT_SIZE(3) + 200;
  DynamicJsonDocument doc(capacity);
  deserializeJson(doc, buf.get());
  //unit
  String unit_tempUnit            = doc["unit_tempUnit"].as<String>();
  if (unit_tempUnit == "fah") useFahrenheit = true;
  min_temp              = doc["min_temp"].as<uint8_t>();
  max_temp              = doc["max_temp"].as<uint8_t>();
  temp_step             = doc["temp_step"].as<String>();
  //mode
  String supportMode = doc["support_mode"].as<String>();
  if (supportMode == "nht") supportHeatMode = false;
  //prevent login password is "null" if not exist key
  if (doc.containsKey("login_password")) {
    login_password = doc["login_password"].as<String>();
  } else {
    login_password = "";
  }
  return true;
}

bool loadOthers() {
  if (!SPIFFS.exists(others_conf)) {
    // write_log(F("Others config file not exist!"));
    return false;
  }
  File configFile = SPIFFS.open(others_conf, "r");
  if (!configFile) {
    return false;
  }

  size_t size = configFile.size();
  if (size > 1024) {
    return false;
  }
  std::unique_ptr<char[]> buf(new char[size]);

  configFile.readBytes(buf.get(), size);
  const size_t capacity = JSON_OBJECT_SIZE(4) + 200;
  DynamicJsonDocument doc(capacity);
  deserializeJson(doc, buf.get());
  //unit
  String unit_tempUnit    = doc["unit_tempUnit"].as<String>();
  if (unit_tempUnit == "fah") useFahrenheit = true;

  String debugPckts       = doc["debugPckts"].as<String>();
  String debugLogs        = doc["debugLogs"].as<String>();

  if (strcmp(debugPckts.c_str(), "ON") == 0) {
    _debugModePckts = true;
  }
  if (strcmp(debugLogs.c_str(), "ON") == 0) {
    _debugModeLogs = true;
  }
  return true;
}


void saveServerSettings(String ip, String url, String server_port) {

  const size_t capacity = JSON_OBJECT_SIZE(6) + 400;
  DynamicJsonDocument doc(capacity);

  if (url[0] == '\0') url = "http://192.168.1.1:81/";

  doc["server_url"] = url;

  File configFile = SPIFFS.open(server_conf, "w");
  if (!configFile) {
    write_log(F("Failed to open config file for writing"));
    return;
  }
  serializeJson(doc, configFile);
  configFile.close();

  write_log(F("Settings saved"));
}

void saveUnit(String tempUnit, String supportMode, String loginPassword, String minTemp, String maxTemp, String tempStep) {
  const size_t capacity = JSON_OBJECT_SIZE(6) + 200;
  DynamicJsonDocument doc(capacity);
  // if temp unit is empty, we use default celcius
  if (tempUnit.isEmpty()) tempUnit = "cel";
  doc["unit_tempUnit"]   = tempUnit;
  // if minTemp is empty, we use default 16
  if (minTemp.isEmpty()) minTemp = 16;
  doc["min_temp"]   = minTemp;
  // if maxTemp is empty, we use default 31
  if (maxTemp.isEmpty()) maxTemp = 31;
  doc["max_temp"]   = maxTemp;
  // if tempStep is empty, we use default 1
  if (tempStep.isEmpty()) tempStep = 1;
  doc["temp_step"] = tempStep;
  // if support mode is empty, we use default all mode
  if (supportMode.isEmpty()) supportMode = "all";
  doc["support_mode"]   = supportMode;
  // if login password is empty, we use empty
  if (loginPassword.isEmpty()) loginPassword = "";

  doc["login_password"]   = loginPassword;
  File configFile = SPIFFS.open(unit_conf, "w");
  if (!configFile) {
    // write_log(F("Failed to open config file for writing"));
  }
  serializeJson(doc, configFile);
  configFile.close();
}

void saveWifi(String apSsid, String apPwd, String hostName, String otaPwd) {
  const size_t capacity = JSON_OBJECT_SIZE(4) + 130;
  DynamicJsonDocument doc(capacity);
  doc["ap_ssid"] = apSsid;
  doc["ap_pwd"] = apPwd;
  doc["hostname"] = hostName;
  doc["ota_pwd"] = otaPwd;
  File configFile = SPIFFS.open(wifi_conf, "w");
  if (!configFile) {
    // write_log(F("Failed to open wifi file for writing"));
  }
  serializeJson(doc, configFile);
  delay(10);
  configFile.close();
}

void saveOthers(String haa, String haat, String debugPckts, String debugLogs) {
  const size_t capacity = JSON_OBJECT_SIZE(4) + 130;
  DynamicJsonDocument doc(capacity);
  doc["haa"] = haa;
  doc["haat"] = haat;
  doc["debugPckts"] = debugPckts;
  doc["debugLogs"] = debugLogs;
  File configFile = SPIFFS.open(others_conf, "w");
  if (!configFile) {
    // write_log(F("Failed to open wifi file for writing"));
  }
  serializeJson(doc, configFile);
  delay(10);
  configFile.close();
}

// Enable OTA only when connected as a client.
void initOTA() {
  //write_log("Start OTA Listener");
  ArduinoOTA.setHostname(hostname.c_str());
  if (ota_pwd.length() > 0) {
    ArduinoOTA.setPassword(ota_pwd.c_str());
  }
  ArduinoOTA.onStart([]() {
    //write_log("Start");
  });
  ArduinoOTA.onEnd([]() {
    //write_log("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    //    write_log("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    //    write_log("Error[%u]: ", error);
    // if (error == OTA_AUTH_ERROR) write_log(F("Auth Failed"));
    // else if (error == OTA_BEGIN_ERROR) write_log(F("Begin Failed"));
    // else if (error == OTA_CONNECT_ERROR) write_log(F("Connect Failed"));
    // else if (error == OTA_RECEIVE_ERROR) write_log(F("Receive Failed"));
    // else if (error == OTA_END_ERROR) write_log(F("End Failed"));
  });
  ArduinoOTA.begin();
}

void setWIFIDefaults() {
  ap_ssid = "";
  ap_pwd  = "";
}

bool initWifi() {
  bool connectWifiSuccess = true;

  //If we have connection setting
  if (ap_ssid[0] != '\0')
  {
    connectWifiSuccess = wifi_config = connectWifi();
    if (connectWifiSuccess) {
      return true;
    }
    else
    {
      // reset hostname back to default before starting AP mode for privacy
      hostname = hostnamePrefix;
      hostname += getId();
    }
  }

  // write_log(F("\n\r \n\rStarting in AP mode"));
  WiFi.mode(WIFI_AP);
  wifi_timeout = millis() + WIFI_RETRY_INTERVAL_MS;
#ifdef ESP32
  WiFi.persistent(false); // fix crash esp32 https://github.com/espressif/arduino-esp32/issues/2025
#endif

  if (!connectWifiSuccess and login_password != "")
  {
    // Set AP password when falling back to AP on fail
    WiFi.softAP(hostname.c_str(), login_password.c_str());
  }
  else {
    // First time setup does not require password
    WiFi.softAP(hostname.c_str());
  }

  delay(2000); // VERY IMPORTANT
  WiFi.softAPConfig(apIP, apIP, netMsk);
  delay(2000); // VERY IMPORTANT

  //write_log(F("IP address: "));
  //Serial.println(WiFi.softAPIP());

  //ticker.attach(0.2, tick); // Start LED to flash rapidly to indicate we are ready for setting up the wifi-connection (entered captive portal).
  wifi_config = false;

  write_log(F("Launch the device in AP mode."));

  dnsServer.start(DNS_PORT, "*", apIP);

  return false;
}

// Handler webserver response

void sendWrappedHTML(String content) {
  String headerContent = FPSTR(html_common_header);
  String footerContent = FPSTR(html_common_footer);
  String toSend = headerContent + content + footerContent;
  toSend.replace(F("_UNIT_NAME_"), hostname);
  toSend.replace(F("_VERSION_"), m2wifi_version);
  server.send(200, F("text/html"), toSend);
}

void handleNotFound() {
  if (captive) {
    String initSetupContent = FPSTR(html_init_setup);
    initSetupContent.replace("_UNIT_NAME_", hostname);
    sendWrappedHTML(initSetupContent);
  }
  else {
    server.sendHeader("Location", "/");
    server.sendHeader("Cache-Control", "no-cache");
    server.send(302);
    return;
  }
}

void handleSaveWifi() {
  if (!checkLogin()) return;

  // write_log(F("Saving wifi config"));
  if (server.method() == HTTP_POST) {
    saveWifi(server.arg("ssid"), server.arg("psk"), server.arg("hn"), server.arg("otapwd"));
  }
  String initSavePage =  FPSTR(html_init_save);
  sendWrappedHTML(initSavePage);
  delay(500);
  ESP.restart();
}

void handleReboot() {
  if (!checkLogin()) return;

  String initRebootPage = FPSTR(html_init_reboot);
  sendWrappedHTML(initRebootPage);
  delay(500);
  ESP.restart();
}

void handleRoot() {
  if (!checkLogin()) return;

  if (server.hasArg("REBOOT")) {
    String rebootPage =  FPSTR(html_page_reboot);
    String countDown = FPSTR(count_down_script);
    sendWrappedHTML(rebootPage + countDown);
    delay(500);
#ifdef ESP32
    ESP.restart();
#else
    ESP.reset();
#endif
  }
  else {
    String menuRootPage =  FPSTR(html_menu_root);
    menuRootPage.replace("_SHOW_LOGOUT_", (String)(login_password.length() > 0));
    //not show control button if hp not connected
    menuRootPage.replace("_SHOW_CONTROL_", (String)(hp.isConnected()));
    sendWrappedHTML(menuRootPage);
  }
}

void handleInitSetup() {
  String initSetupPage = FPSTR(html_init_setup);

  sendWrappedHTML(initSetupPage);
}

void handleSetup() {
  if (!checkLogin()) return;

  if (server.hasArg("RESET")) {
    String pageReset = FPSTR(html_page_reset);
    String ssid = hostnamePrefix;
    ssid += getId();
    pageReset.replace("_SSID_",ssid);
    sendWrappedHTML(pageReset);
    SPIFFS.format();
    delay(500);
#ifdef ESP32
    ESP.restart();
#else
    ESP.reset();
#endif
  }
  else {
    String menuSetupPage = FPSTR(html_menu_setup);
    sendWrappedHTML(menuSetupPage);
  }

}

void handleLogs() {

  String menuLogsPage = FPSTR(html_menu_logs);
  menuLogsPage.replace("_LOGS_", LogString);
  sendWrappedHTML(menuLogsPage);

}

void rebootAndSendPage() {
    String saveRebootPage =  FPSTR(html_page_save_reboot);
    String countDown = FPSTR(count_down_script);
    sendWrappedHTML(saveRebootPage + countDown);
    delay(500);
    ESP.restart();
}

String Page;
void handleJson() {
  Page = "{\"return\":\"ok\"}";

  if (server.method() == HTTP_POST) {
    DynamicJsonDocument doc(JSON_OBJECT_SIZE(4) + 200);
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (!err)
    {
      JsonObject obj = doc.as<JsonObject>();

      if (login_password.length() == 0 || obj["pass"].as<String>() == login_password)
      {

        heatpumpSettings settings = hp.getSettings();

        if (obj.containsKey("command"))
        {
          if (obj["command"] == "update")
          {
            hpSettingsChanged();
            return;
          }
          if (obj["command"] == "reboot")
          {
            return;
          }
        }
        if (obj.containsKey("power"))
        {
          if (settings.power != obj["power"])
          {
            hp.setPowerSetting(strcmp (obj["power"], "on") == 0);
          }
        }
        if (obj.containsKey("mode"))
        {
          if (settings.mode != obj["mode"])
          {
           hp.setModeSetting(obj["mode"].as<const char*>());
          }
        }
        if (obj.containsKey("fan"))
        {
          if (settings.fan != obj["fan"])
          {
           hp.setFanSpeed(obj["fan"].as<const char*>());
          }
        }
        if (obj.containsKey("temperature"))
        {
          if (settings.temperature != obj["temperature"])
          {
           hp.setTemperature(obj["temperature"].as<float>());
          }
        }
        if (obj.containsKey("vane"))
        {
          if (settings.vane != obj["temperature"])
          {
           hp.setVaneSetting(obj["vane"].as<const char*>());
          }
        }       
        if (obj.containsKey("widevane"))
        {
          if (settings.vane != obj["widevane"])
          {
           hp.setWideVaneSetting(obj["widevane"].as<const char*>());
          }
        }

      }
      else
      {
        Page = "{\"return\":\"Bad password\"}";
      }


    }
  }

  server.send(200, F("application/json; charset=utf-8"), Page);
}

void handleOthers() {
  if (!checkLogin()) return;

  if (server.method() == HTTP_POST) {
    saveOthers(server.arg("HAA"), server.arg("haat"), server.arg("DebugPckts"),server.arg("DebugLogs"));
    rebootAndSendPage();
  }
  else {
    String othersPage =  FPSTR(html_page_others);

    if (_debugModePckts) {
      othersPage.replace("_DEBUG_PCKTS_ON_", "selected");
    }
    else {
      othersPage.replace("_DEBUG_PCKTS_OFF_", "selected");
    }
    if (_debugModeLogs) {
      othersPage.replace("_DEBUG_LOGS_ON_", "selected");
    }
    else {
      othersPage.replace("_DEBUG_LOGS_OFF_", "selected");
    }
    sendWrappedHTML(othersPage);
  }
}

void handleServer() {

  if (!checkLogin()) return;

  if (server.method() == HTTP_POST)
  {
    saveServerSettings(server.arg("ip"), server.arg("url"), server.arg("port"));
    rebootAndSendPage();
  }
  else {
    String ServerPage =  FPSTR(html_page_server);
    ServerPage.replace(F("_SERVER_URL_"), server_url);

    sendWrappedHTML(ServerPage);
  }
}

void handleUnit() {
  if (!checkLogin()) return;

  if (server.method() == HTTP_POST) {
    saveUnit(server.arg("tu"), server.arg("md"), server.arg("lpw"), (String)convertLocalUnitToCelsius(server.arg("min_temp").toFloat(), useFahrenheit), (String)convertLocalUnitToCelsius(server.arg("max_temp").toFloat(), useFahrenheit), server.arg("temp_step"));
    rebootAndSendPage();
  }
  else {
    String unitPage =  FPSTR(html_page_unit);
    unitPage.replace(F("_MIN_TEMP_"), String(convertCelsiusToLocalUnit(min_temp, useFahrenheit)));
    unitPage.replace(F("_MAX_TEMP_"), String(convertCelsiusToLocalUnit(max_temp, useFahrenheit)));
    unitPage.replace(F("_TEMP_STEP_"), String(temp_step));
    //temp
    if (useFahrenheit) unitPage.replace(F("_TU_FAH_"), F("selected"));
    else unitPage.replace(F("_TU_CEL_"), F("selected"));
    //mode
    if (supportHeatMode) unitPage.replace(F("_MD_ALL_"), F("selected"));
    else unitPage.replace(F("_MD_NONHEAT_"), F("selected"));
    unitPage.replace(F("_LOGIN_PASSWORD_"), login_password);
    sendWrappedHTML(unitPage);
  }
}

void handleWifi() {
  if (!checkLogin()) return;

  if (server.method() == HTTP_POST) {
    saveWifi(server.arg("ssid"), server.arg("psk"), server.arg("hn"), server.arg("otapwd"));
    rebootAndSendPage();
#ifdef ESP32
    ESP.restart();
#else
    ESP.reset();
#endif
  }
  else {
    String wifiPage =  FPSTR(html_page_wifi);
    String str_ap_ssid = ap_ssid;
    String str_ap_pwd  = ap_pwd;
    String str_ota_pwd = ota_pwd;
    str_ap_ssid.replace("'", F("&apos;"));
    str_ap_pwd.replace("'", F("&apos;"));
    str_ota_pwd.replace("'", F("&apos;"));
    wifiPage.replace(F("_SSID_"), str_ap_ssid);
    wifiPage.replace(F("_PSK_"), str_ap_pwd);
    wifiPage.replace(F("_OTA_PWD_"), str_ota_pwd);
    sendWrappedHTML(wifiPage);
  }

}

void handleStatus() {
  if (!checkLogin()) return;

  String statusPage =  FPSTR(html_page_status);

  //if (server.hasArg("mrconn")) mqttConnect();

  String connected = F("<span style='color:#47c266'><b>");
  connected += FPSTR("CONNECTED");
  connected += F("</b><span>");

  String disconnected = F("<span style='color:#d43535'><b>");
  disconnected += FPSTR("DISCONNECTED");
  disconnected += F("</b></span>");

  if ((Serial) and hp.isConnected()) statusPage.replace(F("_HVAC_STATUS_"), connected);
  else  statusPage.replace(F("_HVAC_STATUS_"), disconnected);

  statusPage.replace(F("_HVAC_RETRIES_"), String(hpConnectionTotalRetries));

  statusPage.replace(F("_WIFI_STATUS_"), String(WiFi.RSSI()));

  // get free heap and percent
  // Better than esp_get_free_heap_size() ?
#ifdef ESP32
  uint32_t freeHeapBytes = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
  uint32_t totalHeapBytes = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
#else
  uint32_t freeHeapBytes = ESP.getFreeHeap();
  uint32_t totalHeapBytes = 64000;
#endif
  float percentageHeapFree = freeHeapBytes * 100.0f / (float)totalHeapBytes;
  String heap(freeHeapBytes);
  heap += " (";
  heap += String(percentageHeapFree);
  heap += "% )";
  statusPage.replace(F("_FREE_HEAP_"), heap);
  statusPage.replace(F("_COMPIL_DATE_"), compile_date);
  statusPage.replace(F("_BOOT_TIME_"), "<font color='orange'><b>" + getUpTime() + "</b></font>");

  sendWrappedHTML(statusPage);
}



void handleControl()
{
  if (!checkLogin()) return;

  //not connected to hp, redirect to status page
  if (!hp.isConnected()) {
    server.sendHeader("Location", "/status");
    server.sendHeader("Cache-Control", "no-cache");
    server.send(302);
    return;
  }

  //Update settings if request
  heatpumpSettings settings = hp.getSettings();

  if (server.hasArg("CONNECT")) {
    hp.connect(&Serial);
  }
  else {

    if (server.hasArg("POWER")) {
      settings.power = server.arg("POWER").c_str();
      hp.setPowerSetting(server.arg("POWER") == "ON");
    }
    if (server.hasArg("MODE")) {
      settings.mode = server.arg("MODE").c_str();
      hp.setModeSetting(server.arg("MODE").c_str());
    }
    if (server.hasArg("TEMP")) {
      settings.temperature = convertLocalUnitToCelsius(server.arg("TEMP").toFloat(), useFahrenheit);
      hp.setTemperature(server.arg("TEMP").toFloat());
    }
    if (server.hasArg("FAN")) {
      settings.fan = server.arg("FAN").c_str();
      hp.setFanSpeed(server.arg("FAN").c_str());
    }
    if (server.hasArg("VANE")) {
      settings.vane = server.arg("VANE").c_str();
      hp.setVaneSetting(server.arg("VANE").c_str());
    }
    if (server.hasArg("WIDEVANE")) {
      settings.wideVane = server.arg("WIDEVANE").c_str();
      hp.setWideVaneSetting(server.arg("WIDEVANE").c_str());
    }

  }

  String controlPage =  FPSTR(html_page_control);
  String headerContent = FPSTR(html_common_header);
  String footerContent = FPSTR(html_common_footer);
  //write_log("Enter HVAC control");
  headerContent.replace("_UNIT_NAME_", hostname);
  footerContent.replace("_VERSION_", m2wifi_version);
  controlPage.replace("_UNIT_NAME_", hostname);
  controlPage.replace("_RATE_", "60");
  controlPage.replace("_ROOMTEMP_", String(convertCelsiusToLocalUnit(hp.getRoomTemperature(), useFahrenheit)));
  controlPage.replace("_USE_FAHRENHEIT_", (String)useFahrenheit);
  controlPage.replace("_TEMP_SCALE_", getTemperatureScale());
  controlPage.replace("_HEAT_MODE_SUPPORT_", (String)supportHeatMode);
  controlPage.replace(F("_MIN_TEMP_"), String(convertCelsiusToLocalUnit(min_temp, useFahrenheit)));
  controlPage.replace(F("_MAX_TEMP_"), String(convertCelsiusToLocalUnit(max_temp, useFahrenheit)));
  controlPage.replace(F("_TEMP_STEP_"), String(temp_step));

  if (strcmp(settings.power, "ON") == 0) {
    controlPage.replace("_POWER_ON_", "selected");
  }
  else if (strcmp(settings.power, "OFF") == 0) {
    controlPage.replace("_POWER_OFF_", "selected");
  }

  if (strcmp(settings.mode, "HEAT") == 0) {
    controlPage.replace("_MODE_H_", "selected");
  }
  else if (strcmp(settings.mode, "DRY") == 0) {
    controlPage.replace("_MODE_D_", "selected");
  }
  else if (strcmp(settings.mode, "COOL") == 0) {
    controlPage.replace("_MODE_C_", "selected");
  }
  else if (strcmp(settings.mode, "FAN") == 0) {
    controlPage.replace("_MODE_F_", "selected");
  }
  else if (strcmp(settings.mode, "AUTO") == 0) {
    controlPage.replace("_MODE_A_", "selected");
  }

  if (strcmp(settings.fan, "AUTO") == 0) {
    controlPage.replace("_FAN_A_", "selected");
  }
  else if (strcmp(settings.fan, "QUIET") == 0) {
    controlPage.replace("_FAN_Q_", "selected");
  }
  else if (strcmp(settings.fan, "1") == 0) {
    controlPage.replace("_FAN_1_", "selected");
  }
  else if (strcmp(settings.fan, "2") == 0) {
    controlPage.replace("_FAN_2_", "selected");
  }
  else if (strcmp(settings.fan, "3") == 0) {
    controlPage.replace("_FAN_3_", "selected");
  }
  else if (strcmp(settings.fan, "4") == 0) {
    controlPage.replace("_FAN_4_", "selected");
  }

  controlPage.replace("_VANE_V_", settings.vane);
  if (strcmp(settings.vane, "AUTO") == 0) {
    controlPage.replace("_VANE_A_", "selected");
  }
  else if (strcmp(settings.vane, "1") == 0) {
    controlPage.replace("_VANE_1_", "selected");
  }
  else if (strcmp(settings.vane, "2") == 0) {
    controlPage.replace("_VANE_2_", "selected");
  }
  else if (strcmp(settings.vane, "3") == 0) {
    controlPage.replace("_VANE_3_", "selected");
  }
  else if (strcmp(settings.vane, "4") == 0) {
    controlPage.replace("_VANE_4_", "selected");
  }
  else if (strcmp(settings.vane, "5") == 0) {
    controlPage.replace("_VANE_5_", "selected");
  }
  else if (strcmp(settings.vane, "SWING") == 0) {
    controlPage.replace("_VANE_S_", "selected");
  }

  controlPage.replace("_WIDEVANE_V_", settings.wideVane);
  if (strcmp(settings.wideVane, "<<") == 0) {
    controlPage.replace("_WVANE_1_", "selected");
  }
  else if (strcmp(settings.wideVane, "<") == 0) {
    controlPage.replace("_WVANE_2_", "selected");
  }
  else if (strcmp(settings.wideVane, "|") == 0) {
    controlPage.replace("_WVANE_3_", "selected");
  }
  else if (strcmp(settings.wideVane, ">") == 0) {
    controlPage.replace("_WVANE_4_", "selected");
  }
  else if (strcmp(settings.wideVane, ">>") == 0) {
    controlPage.replace("_WVANE_5_", "selected");
  }
  else if (strcmp(settings.wideVane, "<>") == 0) {
    controlPage.replace("_WVANE_6_", "selected");
  }
  else if (strcmp(settings.wideVane, "SWING") == 0) {
    controlPage.replace("_WVANE_S_", "selected");
  }
  controlPage.replace("_TEMP_", String(convertCelsiusToLocalUnit(hp.getTemperature(), useFahrenheit)));

  // We need to send the page content in chunks to overcome
  // a limitation on the maximum size we can send at one
  // time (approx 6k).
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", headerContent);
  server.sendContent(controlPage);
  server.sendContent(footerContent);
  // Signal the end of the content
  server.sendContent("");
  //delay(100);
}

void handleMetrics(){
  String metrics =    FPSTR(html_metrics);

  heatpumpSettings currentSettings = hp.getSettings();
  heatpumpStatus currentStatus = hp.getStatus();

  String hppower = (strcmp( currentSettings.power, "ON") == 0) ? "1" : "0";

  String hpfan = currentSettings.fan;
  if(hpfan == "AUTO") hpfan = "-1";
  if(hpfan == "QUIET") hpfan = "0";

  String hpvane = currentSettings.vane;
  if(hpvane == "AUTO") hpvane = "-1";
  if(hpvane == "SWING") hpvane = "0";

  String hpwidevane = "-2";
  if (strcmp(currentSettings.wideVane, "SWING")) hpwidevane = "0";
  if (strcmp(currentSettings.wideVane, "<<")) hpwidevane =  "1";
  if (strcmp(currentSettings.wideVane, "<")) hpwidevane = "2";
  if (strcmp(currentSettings.wideVane, "|")) hpwidevane = "3";
  if (strcmp(currentSettings.wideVane, ">")) hpwidevane = "4";
  if (strcmp(currentSettings.wideVane, ">>")) hpwidevane = "5";
  if (strcmp(currentSettings.wideVane, "<>")) hpwidevane = "6";

  String hpmode = "-2";
  if (strcmp(currentSettings.mode, "AUTO")) hpmode = "-1";
  if (strcmp(currentSettings.mode, "COOL")) hpmode = "1";
  if (strcmp(currentSettings.mode, "DRY")) hpmode = "2";
  if (strcmp(currentSettings.mode, "HEAT")) hpmode = "3";
  if (strcmp(currentSettings.mode, "FAN")) hpmode = "4";
  if(hppower == "0") hpmode = "0";

  metrics.replace("_UNIT_NAME_", hostname);
  metrics.replace("_VERSION_", m2wifi_version);
  metrics.replace("_POWER_", hppower);
  metrics.replace("_ROOMTEMP_", (String)currentStatus.roomTemperature);
  metrics.replace("_TEMP_", (String)currentSettings.temperature);
  metrics.replace("_FAN_", hpfan);
  metrics.replace("_VANE_", hpvane);
  metrics.replace("_WIDEVANE_", hpwidevane);
  metrics.replace("_MODE_", hpmode);
  metrics.replace("_OPER_", (String)currentStatus.operating);
  metrics.replace("_COMPFREQ_", (String)currentStatus.compressorFrequency);

  server.send(200, F("text/plain"), metrics);

}

//login page, also called for logout
void handleLogin() {
  bool loginSuccess = false;
  String msg;
  String loginPage =  FPSTR(html_page_login);

  if (server.hasArg("USERNAME") || server.hasArg("PASSWORD") || server.hasArg("LOGOUT")) {
    if (server.hasArg("LOGOUT")) {
      //logout
      server.sendHeader("Cache-Control", "no-cache");
      server.sendHeader("Set-Cookie", "M2MSESSIONID=0");
      loginSuccess = false;
    }
    if (server.hasArg("USERNAME") && server.hasArg("PASSWORD")) {
      if (server.arg("USERNAME") == "admin" &&  server.arg("PASSWORD") == login_password) {
        server.sendHeader("Cache-Control", "no-cache");
        server.sendHeader("Set-Cookie", "M2MSESSIONID=1");
        loginSuccess = true;
        msg = F("<span style='color:#47c266;font-weight:bold;'>");
        msg += FPSTR("Login successful, you will be redirected in a few seconds.");
        msg += F("<span>");
        loginPage += F("<script>");
        loginPage += F("setTimeout(function () {");
        loginPage += F("window.location.href= '/';");
        loginPage += F("}, 3000);");
        loginPage += F("</script>");
        //Log in Successful;
      } else {
        msg = F("<span style='color:#d43535;font-weight:bold;'>");
        msg += FPSTR("Wrong username/password! Try again.");
        msg += F("</span>");
        //Log in Failed;
      }
    }
  } else {
    if (is_authenticated() or login_password.length() == 0) {
      server.sendHeader("Location", "/");
      server.sendHeader("Cache-Control", "no-cache");
      //use javascript in the case browser disable redirect
      String redirectPage = F("<html lang=\"en\" class=\"\"><head><meta charset='utf-8'>");
      redirectPage += F("<script>");
      redirectPage += F("setTimeout(function () {");
      redirectPage += F("window.location.href= '/';");
      redirectPage += F("}, 1000);");
      redirectPage += F("</script>");
      redirectPage += F("</body></html>");
      server.send(302, F("text/html"), redirectPage);
      return;
    }
  }
  loginPage.replace(F("_LOGIN_SUCCESS_"), (String) loginSuccess);
  loginPage.replace(F("_LOGIN_MSG_"), msg);
  sendWrappedHTML(loginPage);
}

void handleUpgrade() {
  if (!checkLogin()) return;

  uploaderror = 0;
  String upgradePage = FPSTR(html_page_upgrade);

  sendWrappedHTML(upgradePage);
}

void handleUploadDone() {
  //write_log(PSTR("HTTP: Firmware upload done"));
  bool restartflag = false;
  String uploadDonePage = FPSTR(html_page_upload);
  String content = F("<div style='text-align:center;'><b>Upload ");
  if (uploaderror) {
    content += F("<span style='color:#d43535'>failed</span></b><br/><br/>");
    if (uploaderror == 1) {
      content += FPSTR("No file selected");
    } else if (uploaderror == 2) {
      content += FPSTR("File size is larger than available free space");
    } else if (uploaderror == 3) {
      content += FPSTR("File magic header does not start with 0xE9");
    } else if (uploaderror == 4) {
      content += FPSTR("File flash size is larger than device flash size");
    } else if (uploaderror == 5) {
      content += FPSTR("File upload buffer miscompare");
    } else if (uploaderror == 6) {
      content += FPSTR("Upload failed. Enable logging option 3 for more information");
    } else if (uploaderror == 7) {
      content += FPSTR("Upload aborted");
    } else {
      content += FPSTR("Update error code (see Updater.cpp) ");
      content += String(uploaderror);
    }
    if (Update.hasError()) {
      content += FPSTR("Upload error code ");
      content += String(Update.getError());
    }
  } else {
    content += F("<span style='color:#47c266; font-weight: bold;'>");
    content += FPSTR("Successful");
    content += F("</span><br/><br/>");
    content += FPSTR("Refresh in");
    content += F("<span id='count'>10s</span>...");
    content += FPSTR(count_down_script);
    restartflag = true;
  }
  content += F("</div><br/>");
  uploadDonePage.replace("_UPLOAD_MSG_", content);
  sendWrappedHTML(uploadDonePage);
  if (restartflag) {
    delay(500);
#ifdef ESP32
    ESP.restart();
#else
    ESP.reset();
#endif
  }
}

void handleUploadLoop() {
  if (!checkLogin()) return;

  // Based on ESP8266HTTPUpdateServer.cpp uses ESP8266WebServer Parsing.cpp and Cores Updater.cpp (Update)
  //char log[200];
  if (uploaderror) {
    Update.end();
    return;
  }
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    if (upload.filename.c_str()[0] == 0)
    {
      uploaderror = 1;
      return;
    }

    //save cpu by disconnect/stop network stuff ?

    //snprintf_P(log, sizeof(log), PSTR("Upload: File %s ..."), upload.filename.c_str());
    //write_log(log);
    uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if (!Update.begin(maxSketchSpace)) {         //start with max available size
      //Update.printError(Serial);
      uploaderror = 2;
      return;
    }
  } else if (!uploaderror && (upload.status == UPLOAD_FILE_WRITE)) {
    if (upload.totalSize == 0)
    {
      if (upload.buf[0] != 0xE9) {
        //write_log(PSTR("Upload: File magic header does not start with 0xE9"));
        uploaderror = 3;
        return;
      }
      uint32_t bin_flash_size = ESP.magicFlashChipSize((upload.buf[3] & 0xf0) >> 4);

#ifdef ESP32
      if (bin_flash_size > ESP.getFlashChipSize()) {
#else
      if (bin_flash_size > ESP.getFlashChipRealSize()) {
#endif
        //write_log(PSTR("Upload: File flash size is larger than device flash size"));
        uploaderror = 4;
        return;
      }
      if (ESP.getFlashChipMode() == 3) {
        upload.buf[2] = 3; // DOUT - ESP8285
      } else {
        upload.buf[2] = 2; // DIO - ESP8266
      }
    }
    if (!uploaderror && (Update.write(upload.buf, upload.currentSize) != upload.currentSize)) {
      //Update.printError(Serial);
      uploaderror = 5;
      return;
    }
  } else if (!uploaderror && (upload.status == UPLOAD_FILE_END)) {
    if (Update.end(true)) { // true to set the size to the current progress
      //snprintf_P(log, sizeof(log), PSTR("Upload: Successful %u bytes. Restarting"), upload.totalSize);
      //write_log(log)
    } else {
      //Update.printError(Serial);
      uploaderror = 6;
      return;
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    //write_log(PSTR("Upload: Update was aborted"));
    uploaderror = 7;
    Update.end();
  }
  delay(0);
}

void write_log(String log) {
  //File logFile = SPIFFS.open(console_file, "a");
  //logFile.println(log);
  //logFile.close();
  if (!hp.isConnected())
  {
    Serial.println(log);
  }
}

void hpSettingsChanged() {

  if (millis() - hp.getLastWanted() < PREVENT_UPDATE_INTERVAL_MS) // prevent application setting change after send update interval we wait for 1 seconds before udpate data
  {
    return;
  }

  // send room temp, operating info and all information
  heatpumpSettings currentSettings = hp.getSettings();

  //rootInfo.clear();
  rootInfo["temperature"]     = convertCelsiusToLocalUnit(currentSettings.temperature, useFahrenheit);
  rootInfo["fan"]             = currentSettings.fan;
  rootInfo["vane"]            = currentSettings.vane;
  rootInfo["widevane"]        = currentSettings.wideVane;
  rootInfo["mode"]            = currentSettings.mode;
  rootInfo["power"]           = currentSettings.power;

  SendJson(rootInfo);

  //hpStatusChanged(hp.getStatus());
}

void hpStatusChanged(heatpumpStatus currentStatus) {

  if (millis() - hp.getLastWanted() < PREVENT_UPDATE_INTERVAL_MS) // prevent application setting change after send update interval we wait for 1 seconds before udpate data
  {
    return;
  }

  if (millis() - lastTempSend > SEND_ROOM_TEMP_INTERVAL_MS)
  {
    // only send the temperature every SEND_ROOM_TEMP_INTERVAL_MS (millis rollover tolerant)
    hpCheckRemoteTemp(); // if the remote temperature feed from mqtt is stale, disable it and revert to the internal thermometer.

    // send room temp, operating info and all information
    //heatpumpSettings currentSettings = hp.getSettings();

    if (currentStatus.roomTemperature == 0) return;

    //rootInfo.clear();
    rootInfo["roomTemperature"]     = convertCelsiusToLocalUnit(currentStatus.roomTemperature, useFahrenheit);
    rootInfo["compressorFrequency"] = currentStatus.compressorFrequency;
    rootInfo["action"]              = currentStatus.operating;
/*
    rootInfo["temperature"]         = convertCelsiusToLocalUnit(currentSettings.temperature, useFahrenheit);
    rootInfo["fan"]                 = currentSettings.fan;
    rootInfo["vane"]                = currentSettings.vane;
    rootInfo["widevane"]            = currentSettings.wideVane;
    rootInfo["mode"]                = currentSettings.mode;
    rootInfo["power"]               = currentSettings.power;
*/

    SendJson(rootInfo);

    lastTempSend = millis();
  }
}

void hpCheckRemoteTemp(){
    if (remoteTempActive && (millis() - lastRemoteTemp > CHECK_REMOTE_TEMP_INTERVAL_MS)) {
     //if it's been 5 minutes since last remote_temp message, revert back to HP internal temp sensor
     remoteTempActive = false;
     float temperature = 0;
     hp.setRemoteTemperature(temperature);
     hp.update();
    }
}


void hpPacketDebug(byte* packet, unsigned int length, const char* packetDirection) {
  if (_debugModePckts) {
    String message;
    for (unsigned int idx = 0; idx < length; idx++) {
      if (packet[idx] < 16) {
        message += "0"; // pad single hex digits with a 0
      }
      message += String(packet[idx], HEX) + " ";
    }

    const size_t bufferSize = JSON_OBJECT_SIZE(10);
    StaticJsonDocument<bufferSize> root;

    root[packetDirection] = message;
    SendJson(root);
  }
}


#if 0
void mqttCallback(char* topic, byte* payload, unsigned int length) {

  // Copy payload into message buffer
  char message[length + 1];
  for (unsigned int i = 0; i < length; i++) {
    message[i] = (char)payload[i];
  }
  message[length] = '\0';

  // HA topics
  // Receive power topic
  if (strcmp(topic, ha_mode_set_topic.c_str()) == 0) {
    String modeUpper = message;
    modeUpper.toUpperCase();
    if (modeUpper == "OFF") {
      rootInfo["mode"] = "off";
      rootInfo["action"] = "off";
      hpSendLocalState();
      hp.setPowerSetting("OFF");
    } else {
      if (modeUpper == "HEAT_COOL") {
        rootInfo["mode"] = "heat_cool";
        rootInfo["action"] = "idle";
        modeUpper = "AUTO";
      } else if (modeUpper == "HEAT") {
        rootInfo["mode"] = "heat";
        rootInfo["action"] = "heating";
      } else if (modeUpper == "COOL") {
        rootInfo["mode"] = "cool";
        rootInfo["action"] = "cooling";
      } else if (modeUpper == "DRY") {
        rootInfo["mode"] = "dry";
        rootInfo["action"] = "drying";
      } else if (modeUpper == "FAN_ONLY") {
        rootInfo["mode"] = "fan_only";
        rootInfo["action"] = "fan";
        modeUpper = "FAN";
      } else {
        return;
      }
      hpSendLocalState();
      hp.setPowerSetting("ON");
      hp.setModeSetting(modeUpper.c_str());
    }
  }
  else if (strcmp(topic, ha_temp_set_topic.c_str()) == 0) {
    float temperature = strtof(message, NULL);
    float temperature_c = convertLocalUnitToCelsius(temperature, useFahrenheit);
    if (temperature_c < min_temp || temperature_c > max_temp) {
      temperature_c = 23;
      rootInfo["temperature"] = convertCelsiusToLocalUnit(temperature_c, useFahrenheit);
    } else {
      rootInfo["temperature"] = temperature;
    }
    hpSendLocalState();
    hp.setTemperature(temperature_c);
  }
  else if (strcmp(topic, ha_fan_set_topic.c_str()) == 0) {
    String fanUpper = message;
    fanUpper.toUpperCase();
    String fanSpeed = fanUpper;
    if (fanUpper == "DIFFUSE") {
      fanSpeed = "QUIET";
    } else if (fanUpper == "LOW") {
      fanSpeed = "1";
    } else if (fanUpper == "MIDDLE") {
      fanSpeed = "2";
    } else if (fanUpper == "MEDIUM") {
      fanSpeed = "3";
    } else if (fanUpper == "HIGH") {
      fanSpeed = "4";
    }
    rootInfo["fan"] = (String) message;
    hpSendLocalState();
    hp.setFanSpeed(fanSpeed.c_str());
  }
  else if (strcmp(topic, ha_vane_set_topic.c_str()) == 0) {
    rootInfo["vane"] = (String) message;
    hpSendLocalState();
    hp.setVaneSetting(message);
  }
  else if (strcmp(topic, ha_wideVane_set_topic.c_str()) == 0) {
    rootInfo["widevane"] = (String) message;
    hpSendLocalState();
    hp.setWideVaneSetting(message);
  }
  else if (strcmp(topic, ha_remote_temp_set_topic.c_str()) == 0) {
    float temperature = strtof(message, NULL);
    if (temperature == 0){ //Remote temp disabled by mqtt topic set
      remoteTempActive = false; //clear the remote temp flag
      hp.setRemoteTemperature(0.0);
    }
    else {
      remoteTempActive = true; //Remote temp has been pushed.
      lastRemoteTemp = millis(); //Note time
      hp.setRemoteTemperature(convertLocalUnitToCelsius(temperature, useFahrenheit));
    }
  }
  else if (strcmp(topic, ha_system_set_topic.c_str()) == 0) { // We receive command for board
    if (strcmp(message, "reboot") == 0) { // We receive reboot command
      ESP.restart();
    }
  }
  else if (strcmp(topic, ha_debug_pckts_set_topic.c_str()) == 0) { //if the incoming message is on the heatpump_debug_set_topic topic...
    if (strcmp(message, "on") == 0) {
      _debugModePckts = true;
      mqtt_client.publish(ha_debug_pckts_topic.c_str(), (char*)("Debug packets mode enabled"));
    } else if (strcmp(message, "off") == 0) {
      _debugModePckts = false;
      mqtt_client.publish(ha_debug_pckts_topic.c_str(), (char *)("Debug packets mode disabled"));
    }
  }
  else if (strcmp(topic, ha_debug_logs_set_topic.c_str()) == 0) { //if the incoming message is on the heatpump_debug_set_topic topic...
    if (strcmp(message, "on") == 0) {
      _debugModeLogs = true;
      mqtt_client.publish(ha_debug_logs_topic.c_str(), (char*)("Debug logs mode enabled"));
    } else if (strcmp(message, "off") == 0) {
      _debugModeLogs = false;
      mqtt_client.publish(ha_debug_logs_topic.c_str(), (char *)("Debug logs mode disabled"));
    }
  }
  else if(strcmp(topic, ha_custom_packet.c_str()) == 0) { //send custom packet for advance user
      String custom = message;

      // copy custom packet to char array
      char buffer[(custom.length() + 1)]; // +1 for the NULL at the end
      custom.toCharArray(buffer, (custom.length() + 1));

      byte bytes[20]; // max custom packet bytes is 20
      int byteCount = 0;
      char *nextByte;

      // loop over the byte string, breaking it up by spaces (or at the end of the line - \n)
      nextByte = strtok(buffer, " ");
      while (nextByte != NULL && byteCount < 20) {
        bytes[byteCount] = strtol(nextByte, NULL, 16); // convert from hex string
        nextByte = strtok(NULL, "   ");
        byteCount++;
      }

      // dump the packet so we can see what it is. handy because you can run the code without connecting the ESP to the heatpump, and test sending custom packets
      hpPacketDebug(bytes, byteCount, "customPacket");

      hp.sendCustomPacket(bytes, byteCount);
  }
  else {
    mqtt_client.publish(ha_debug_logs_topic.c_str(), strcat((char *)"heatpump: wrong mqtt topic: ", topic));
  }
}
#endif


bool connectWifi() {
#ifdef ESP32
  WiFi.setHostname(hostname.c_str());
#else
  WiFi.hostname(hostname.c_str());
#endif
  if (WiFi.getMode() != WIFI_STA) {
    WiFi.mode(WIFI_STA);
    delay(100);
  }

#ifdef ESP32
  WiFi.config((uint32_t)0, (uint32_t)0, (uint32_t)0);
#else
  WiFi.config(0, 0, 0);
#endif

  WiFi.begin(ap_ssid.c_str(), ap_pwd.c_str());
  write_log("Connecting to " + ap_ssid);
  wifi_timeout = millis() + 30000;

  while (WiFi.status() != WL_CONNECTED && millis() < wifi_timeout) {
    write_log(".");
    //write_log(WiFi.status());
    // wait 500ms, flashing the blue LED to indicate WiFi connecting...
    digitalWrite(blueLedPin, LOW);
    delay(250);
    digitalWrite(blueLedPin, HIGH);
    delay(250);
  }

  if (WiFi.status() != WL_CONNECTED) {
    write_log(F("Failed to connect to wifi"));
    return false;
  }

  wifi_timeout = millis() + 5000;
  while ((WiFi.localIP().toString() == "0.0.0.0" || WiFi.localIP().toString() == "") && millis() < wifi_timeout) {
    write_log(".");
    delay(500);
  }
  if (WiFi.localIP().toString() == "0.0.0.0" || WiFi.localIP().toString() == "") {
    write_log(F("Failed to get IP address"));
    return false;
  }

  //write_log(F("\nConnected with IP address: "));
  //write_log(WiFi.localIP().toString());

  //ticker.detach(); // Stop blinking the LED because now we are connected:)

  //keep LED off (For Wemos D1-Mini)
  digitalWrite(blueLedPin, HIGH);

  // Auto reconnected
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  return true;
}

// temperature helper these are direct mappings based on the remote
float toFahrenheit(float fromCelsius) {
    // Lookup table for specific mappings
    const std::map<float, int> lookupTable = {
        {16.0, 61}, {16.5, 62}, {17.0, 63}, {17.5, 64}, {18.0, 65},
        {18.5, 66}, {19.0, 67}, {20.0, 68}, {21.0, 69}, {21.5, 70},
        {22.0, 71}, {22.5, 72}, {23.0, 73}, {23.5, 74}, {24.0, 75},
        {24.5, 76}, {25.0, 77}, {25.5, 78}, {26.0, 79}, {26.5, 80},
        {27.0, 81}, {27.5, 82}, {28.0, 83}, {28.5, 84}, {29.0, 85},
        {29.5, 86}, {30.0, 87}, {30.5, 88}
    };

    // Check if the input is in the lookup table
    auto it = lookupTable.find(fromCelsius);
    if (it != lookupTable.end()) {
        return it->second;
    }

    // Default conversion and rounding to nearest integer
    return roundf(fromCelsius * 1.8 + 32.0);
}

// temperature helper these are direct mappings based on the remote
float toCelsius(float fromFahrenheit) {
    // Lookup table for specific mappings
    const std::map<int, float> lookupTable = {
        {61, 16.0}, {62, 16.5}, {63, 17.0}, {64, 17.5}, {65, 18.0},
        {66, 18.5}, {67, 19.0}, {68, 20.0}, {69, 21.0}, {70, 21.5},
        {71, 22.0}, {72, 22.5}, {73, 23.0}, {74, 23.5}, {75, 24.0},
        {76, 24.5}, {77, 25.0}, {78, 25.5}, {79, 26.0}, {80, 26.5},
        {81, 27.0}, {82, 27.5}, {83, 28.0}, {84, 28.5}, {85, 29.0},
        {86, 29.5}, {87, 30.0}, {88, 30.5}
    };

    // Check if the input is in the lookup table
    auto it = lookupTable.find(static_cast<int>(fromFahrenheit));
    if (it != lookupTable.end()) {
        return it->second;
    }

    // Default conversion and rounding to nearest 0.5
    return roundf((fromFahrenheit - 32.0) / 1.8 * 2) / 2.0;
}


float convertCelsiusToLocalUnit(float temperature, bool isFahrenheit) {
  if (isFahrenheit) {
    return toFahrenheit(temperature);
  } else {
    return temperature;
  }
}

float convertLocalUnitToCelsius(float temperature, bool isFahrenheit) {
  if (isFahrenheit) {
    return toCelsius(temperature);
  } else {
    return temperature;
  }
}

String getTemperatureScale() {
  if (useFahrenheit) {
    return "F";
  } else {
    return "C";
  }
}

String getId() {
#ifdef ESP32
  uint64_t macAddress = ESP.getEfuseMac();
  uint32_t chipID = macAddress >> 24;
#else
  uint32_t chipID = ESP.getChipId();
#endif
  return String(chipID, HEX);
}

//Check if header is present and correct
bool is_authenticated() {
  if (server.hasHeader("Cookie")) {
    //Found cookie;
    String cookie = server.header("Cookie");
    if (cookie.indexOf("M2MSESSIONID=1") != -1) {
      //Authentication Successful
      return true;
    }
  }
  //Authentication Failed
  return false;
}

bool checkLogin() {
  if (!is_authenticated() and login_password.length() > 0) {
    server.sendHeader("Location", "/login");
    server.sendHeader("Cache-Control", "no-cache");
    //use javascript in the case browser disable redirect
    String redirectPage = F("<html lang=\"en\" class=\"\"><head><meta charset='utf-8'>");
    redirectPage += F("<script>");
    redirectPage += F("setTimeout(function () {");
    redirectPage += F("window.location.href= '/login';");
    redirectPage += F("}, 1000);");
    redirectPage += F("</script>");
    redirectPage += F("</body></html>");
    server.send(302, F("text/html"), redirectPage);
    return false;
  }
  return true;
}

//long lastdebugtimer;

//Main loop
void loop()
{
  server.handleClient();
  ArduinoOTA.handle();

#if 0
  //debug part
  if (millis() - lastdebugtimer > 5000)
  {
    lastdebugtimer = millis();
    const size_t bufferSize = JSON_OBJECT_SIZE(10);
    StaticJsonDocument<bufferSize> root;
    root["test"] = "ok";
    SendJson(root);
  }
#endif

  //reset board to attempt to connect to wifi again if in ap mode or wifi dropped out and time limit passed
  if (WiFi.getMode() == WIFI_STA and WiFi.status() == WL_CONNECTED)
  {
	  wifi_timeout = millis() + WIFI_RETRY_INTERVAL_MS;
  }
  else if (wifi_config_exists and millis() > wifi_timeout)
  {
	  ESP.restart();
  }

  if (!captive)
  {
    // Sync HVAC UNIT
    if (!hp.isConnected())
    {
      // Use exponential backoff for retries, where each retry is double the length of the previous one.
      unsigned long durationNextSync = (1 << hpConnectionRetries) * HP_RETRY_INTERVAL_MS;
      if (((millis() - lastHpSync > durationNextSync) or lastHpSync == 0))
      {
        lastHpSync = millis();
        // If we've retried more than the max number of tries, keep retrying at that fixed interval, which is several minutes.
        hpConnectionRetries = min(hpConnectionRetries + 1u, HP_MAX_RETRIES);
        hpConnectionTotalRetries++;
        hp.sync();
      }
    }
    else
    {
        hpConnectionRetries = 0;
        hp.sync();
    }

  }
  else
  {
    dnsServer.processNextRequest();
  }
}
