#include "util.h"


// Time device running without crash or reboot
String getUpTime()
{
  char uptimeBuffer[64];
#ifdef ESP32
  int64_t microSecondsSinceBoot = esp_timer_get_time();
  int64_t secondsSinceBoot = microSecondsSinceBoot / 1000000;
#else
  int32_t milliSecondsSinceBoot = millis(); // 2^32-1 only about 49 day before roll over
  int32_t secondsSinceBoot = milliSecondsSinceBoot / 1000;
#endif
  int seconds = (secondsSinceBoot % 60);
  int minutes = (secondsSinceBoot % 3600) / 60;
  int hours = (secondsSinceBoot % 86400) / 3600;
  int days = (secondsSinceBoot % (86400 * 30)) / 86400;
  sprintf(uptimeBuffer, "%02i:%02i:%02i:%02i", days, hours, minutes, seconds);
  return String(uptimeBuffer);
}