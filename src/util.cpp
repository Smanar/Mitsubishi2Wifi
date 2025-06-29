#include "util.h"

unsigned long times_rolled = 0;
unsigned long last_time_value = 0;

// Time device running without crash or reboot
String getUpTime()
{
  char uptimeBuffer[64];

#ifdef ESP32
  int64_t microSecondsSinceBoot = esp_timer_get_time();
  int64_t secondsSinceBoot = microSecondsSinceBoot / 1000000;
#else
  unsigned long now = millis(); // 2^32-1 only about 49 day before roll over
  if (now < last_time_value)
  {
    times_rolled +=1;
  }
  last_time_value = now;

  int64_t secondsSinceBoot = (0xFFFFFFFF / 1000) * times_rolled + (now / 1000);
#endif

  unsigned int seconds = (secondsSinceBoot % 60);
  unsigned int minutes = (secondsSinceBoot % 3600) / 60;
  unsigned int hours = (secondsSinceBoot % 86400) / 3600;
  unsigned int days = (secondsSinceBoot % (86400 * 30)) / 86400;

  sprintf(uptimeBuffer, "%03i:%02i:%02i:%02i", days, hours, minutes, seconds);

  return String(uptimeBuffer);
}