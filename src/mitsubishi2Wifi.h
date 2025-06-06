#include <Arduino.h>
#include <HeatPump.h>

String getId();
float convertCelsiusToLocalUnit(float temperature, bool isFahrenheit);
float convertLocalUnitToCelsius(float temperature, bool isFahrenheit);
String getTemperatureScale();
void write_log(String log);

void setWIFIDefaults();
bool checkLogin();
bool is_authenticated();
void initOTA();

bool loadOthers();
bool loadUnit();
bool loadWifi();
void saveWifi(String apSsid, String apPwd, String hostName, String otaPwd);

bool connectWifi();
bool initWifi();
void handleSaveWifi();

// Web pages
void handleRoot();
void handleSetup();
void handleServer();
void handleWifi();
void handleUnit();
void handleStatus();
void handleOthers();
void handleMetrics();
void handleJson();
void handleLogs();

void handleReboot();
bool loadServerSettings();
void handleLogin();
void handleUpgrade();
void handleUploadDone();


void handleNotFound();
void handleUploadLoop();
void handleControl();

void handleInitSetup() ;

void hpStatusChanged(heatpumpStatus currentStatus);
void hpCheckRemoteTemp();
void hpSettingsChanged();
void hpPacketDebug(byte* packet, unsigned int length, const char* packetDirection);