/**The MIT License (MIT)
Copyright (c) 2015 by Daniel Eichhorn
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
See more at http://blog.squix.ch

Converted from Wunderground to OpenWeatherMap while keeping cxandy's
config portal + touch calibration + AZSMZ hardware support.
*/

#include <simpleDSTadjust.h>

// Config mode SSID (an ESP chip id is prepended to make it unique)
const String CONFIG_SSID = "-WeatherStation";

// Setup - these are defaults; the config portal (SPIFFS) overrides them.
// Leave as placeholders: on first boot with no saved config the device can't
// join WiFi and automatically opens the setup portal (AP "<chipid>-WeatherStation").
String WIFI_SSID = "yourssid";
String WIFI_PASS = "yourpassw0rd";

int UPDATE_INTERVAL_SECS = 10 * 60; // Update every 10 minutes
int SAVER_INTERVAL_SECS = 0;    // Going to screen saver after idle times, set 0 for dont screen saver.
int SLEEP_INTERVAL_SECS = 0;    // Going to Sleep after idle times, set 0 for dont sleep.

#define SQUIX         10
#define AZSMZ_1_1     11
#define AZSMZ_1_6     16    // AZSMZ TFT ver 1.6 (have touchpad or no touchpad)

//#define BOARD SQUIX
//#define BOARD AZSMZ_1_1
#define BOARD AZSMZ_1_6

#if BOARD == SQUIX
  // Pins for the ILI9341
  #define TFT_DC D2
  #define TFT_CS D1
  #define TFT_LED D8

  #define HAVE_TOUCHPAD
  #define TOUCH_CS D3
  #define TOUCH_IRQ  D4

#elif BOARD == AZSMZ_1_1
  #define TFT_DC 5
  #define TFT_CS 4
  #define TFT_LED 16
  #define TFT_LED_LOW       // set LOW to Turn on;

  #define BTN_1 0

  #define LM75
  #define SDA_PIN 0
  #define SCL_PIN 2
  // LM75A Address
  #define Addr 0x48
  #define BATT

#elif BOARD == AZSMZ_1_6
  #define TFT_DC 0
  #define TFT_CS 2
  #define TFT_LED 16
  #define TFT_LED_LOW       // set LOW to Turn on;

  #define HAVE_TOUCHPAD
  #define TOUCH_CS 5
  #define TOUCH_IRQ 4

  #define BTN_1 0
  #define BATT
  #define NTC
  #define nominalResistance 10   // NTC 10K
  #define bCoefficient 3950      // B 3950
  #define TEMPERATURENOMINAL 25
  #define serialResistance 10

#endif

// OpenWeatherMap Settings
// Sign up here to get an API key: https://docs.thingpulse.com/how-tos/openweathermap-key/
String OPEN_WEATHER_MAP_API_KEY = "";   // set via the config portal, or paste your key here for a personal build

// Go to https://openweathermap.org/find?q= and search for a location. The number
// at the end of the resulting URL (e.g. .../city/5577592) is the location id.
String OPEN_WEATHER_MAP_LOCATION_ID = "5577592";   // Greeley, CO
String DISPLAYED_CITY_NAME = "Greeley";

// Arabic -> ar, Bulgarian -> bg, Catalan -> ca, Czech -> cz, German -> de, Greek -> el,
// English -> en, Persian (Farsi) -> fa, Finnish -> fi, French -> fr, ... Spanish -> es
String OPEN_WEATHER_MAP_LANGUAGE = "en";

// -7 = US Mountain Standard Time. simpleDSTadjust applies the DST rules below on top.
#define UTC_OFFSET -7
struct dstRule StartRule = {"MDT", Second, Sun, Mar, 2, 3600}; // Mountain Daylight Time = UTC/GMT -6 hours
struct dstRule EndRule = {"MST", First, Sun, Nov, 2, 0};       // Mountain Standard Time = UTC/GMT -7 hours

// values in metric or imperial system?
bool IS_METRIC = false;

// Change for 12 Hour/ 24 hour style clock
bool IS_STYLE_12HR = true;

// change for different ntp (time servers)
#define NTP_SERVERS "us.pool.ntp.org", "time.nist.gov", "pool.ntp.org"

/***************************
 * End Settings
 **************************/
