/**The MIT License (MIT)
Copyright (c) 2017 by Daniel Eichhorn
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
See more at https://blog.squix.org
*/


/*****************************
 * Important: see settings.h to configure your settings!!!
 * ***************************/
#include "settings.h"

#include <Arduino.h>
#include <SPI.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPUpdateServer.h>

ESP8266HTTPUpdateServer httpUpdater;

#ifdef HAVE_TOUCHPAD
  #include <XPT2046_Touchscreen.h>
  #include "TouchControllerWS.h"

//  ADC_MODE(ADC_VCC);

  #define CFG_POWER  0b10100111
  #define CFG_TEMP0  0b10000111
  #define CFG_TEMP1  0b11110111
  #define CFG_AUX    0b11100111
  #define CFG_IRQ    0b11010010
  #define CFG_LIRQ   0b11010000

  void XPT2046_EnableIrq(uint8_t q) {
    SPI.beginTransaction(SPISettings(2500000, MSBFIRST, SPI_MODE0));
    digitalWrite(TOUCH_CS, 0);
    const uint8_t buf[4] = { q, 0x00, 0x00, 0x00 };
    SPI.writeBytes((uint8_t *) &buf[0], 3);
    digitalWrite(TOUCH_CS, 1);
    SPI.endTransaction();
  }

  uint32_t XPT2046_ReadRaw(uint8_t c) {
    uint32_t p = 0;
    uint8_t i = 0;  
    digitalWrite(TFT_CS, HIGH);    
    SPI.beginTransaction(SPISettings(2500000, MSBFIRST, SPI_MODE0));
    digitalWrite(TOUCH_CS, 0);
    SPI.transfer16(c) >> 3;
    for(; i < 10; i++) {
      delay(2);
      p += SPI.transfer16(c) >> 3;
    }
    p /= i;

    XPT2046_EnableIrq(CFG_IRQ);
    digitalWrite(TOUCH_CS, 1);
    SPI.endTransaction();
    return p;
  }

int readNTC() {
  float average = (4096 * 1.0 * serialResistance / XPT2046_ReadRaw(CFG_AUX)) - serialResistance;
//  Serial.print("NTC R=");  
//  Serial.println(average);  
  float steinhart;
  steinhart = average * 1.0 / nominalResistance;     // (R/Ro)
  steinhart = log(steinhart);                  // ln(R/Ro)
  steinhart /= bCoefficient;                   // 1/B * ln(R/Ro)
  steinhart += 1.0 / (TEMPERATURENOMINAL + 273.15); // + (1/To)
  steinhart = 1.0 / steinhart;                 // Invert
  steinhart -= 273.15;                         // convert to C
  return (int)(steinhart * 10); 
}  
    
#endif 

/***
 * Install the following libraries through Arduino Library Manager
 * - Mini Grafx by Daniel Eichhorn
 * - ESP8266 WeatherStation by Daniel Eichhorn
 * - Json Streaming Parser by Daniel Eichhorn
 * - simpleDSTadjust by neptune2
 ***/

#include <JsonListener.h>
#include <OpenWeatherMapCurrent.h>
#include <OpenWeatherMapForecast.h>
#include <SunMoonCalc.h>
#include <MiniGrafx.h>
#include <Carousel.h>
#include <ILI9341_SPI.h>

/*  
 *  if (hwSPI) spi_begin();   
    if (!(_rst>0)) writecommand(ILI9341_SWRESET);
    writecommand(0xEF);
    */


#include "ArialRounded.h"
#include "moonphases.h"
#include "weathericons.h"
#include "configportal.h"

#define MINI_BLACK 0
#define MINI_WHITE 1
#define MINI_YELLOW 2
#define MINI_BLUE 3

#define MAX_FORECASTS 12

// defines the colors usable in the paletted 16 color frame buffer
uint16_t palette[] = {ILI9341_BLACK, // 0
                      ILI9341_WHITE, // 1
                      ILI9341_YELLOW, // 2
                      0x7E3C
                      }; //3

int SCREEN_WIDTH = 240;
int SCREEN_HEIGHT = 320;
// Limited to 4 colors due to memory constraints
int BITS_PER_PIXEL = 2; // 2^2 =  4 colors

#ifndef BATT
  ADC_MODE(ADC_VCC);
#endif

ILI9341_SPI tft = ILI9341_SPI(TFT_CS, TFT_DC);
MiniGrafx gfx = MiniGrafx(&tft, BITS_PER_PIXEL, palette);
Carousel carousel(&gfx, 0, 0, 240, 100);

#ifdef HAVE_TOUCHPAD
  XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);
  TouchControllerWS touchController(&ts);

  void calibrationCallback(int16_t x, int16_t y);
  CalibrationCallback calibration = &calibrationCallback;

  void touchCalibration() {
    Serial.println("Touchpad calibration .....");
    touchController.startCalibration(&calibration);
    while (!touchController.isCalibrationFinished()) {
      gfx.fillBuffer(0);
      gfx.setColor(MINI_YELLOW);
      gfx.setTextAlignment(TEXT_ALIGN_CENTER);
      gfx.drawString(120, 160, "Please calibrate\ntouch screen by\ntouch point");
      touchController.continueCalibration();
      gfx.commit();
      yield();
    }
    touchController.saveCalibration();
  }

#endif 

OpenWeatherMapCurrentData currentWeather;
OpenWeatherMapForecastData forecasts[MAX_FORECASTS];
SunMoonCalc::Moon moonData;

const String WDAY_NAMES[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

String getTime(time_t *timestamp);
String owmToCxandyIcon(String owmIcon);
char determineMoonIcon();

// Setup simpleDSTadjust Library rules
simpleDSTadjust dstAdjusted(StartRule, EndRule);

void drawWifiQuality();
void updateData();
void drawProgress(uint8_t percentage, String text);
void drawTime(bool saver = false);
void drawCurrentWeather();
void drawForecast();
void drawForecastDetail(uint16_t x, uint16_t y, uint8_t dayIndex);
void drawAstronomy();
void drawCurrentWeatherDetail();
void drawLabelValue(uint8_t line, String label, String value);
void drawForecastTable(uint8_t start);
void drawAbout();
void drawSeparator(uint16_t y);
const char* getMeteoconIconFromProgmem(String iconText);
const char* getMiniMeteoconIconFromProgmem(String iconText);
void drawForecast1(MiniGrafx *display, CarouselState* state, int16_t x, int16_t y);
void drawForecast2(MiniGrafx *display, CarouselState* state, int16_t x, int16_t y);
FrameCallback frames[] = { drawForecast1, drawForecast2 };
int frameCount = 2;

// how many different screens do we have?
int screenCount = 5;
long lastDownloadUpdate = millis();

String moonAgeImage = "";
uint16_t screen = 0;
long timerPress;
long timerTouch;
bool canBtnPress;
bool btnClick;

void systemRestart() {
    Serial.flush();
    delay(500);
    Serial.swap();
    delay(100);
    ESP.restart();
    while (1) {
        delay(1);
    }; 
}

int getBtnState() {
    pinMode(BTN_1, INPUT_PULLUP);
    delay(1);
    int btnState = digitalRead(BTN_1);
    pinMode(BTN_1, OUTPUT);
    if (btnState == LOW){
      if(canBtnPress){
        timerPress = millis();
        canBtnPress = false;
      }else {
        if ((!btnClick) && ((millis() - timerPress)>3000)) {     // long press to pen init  
          return 2;
        }    
      }
    }else if(!canBtnPress){
      canBtnPress = true;
      btnClick = false;
      if ((millis() - timerPress)<800) {    
        return 1;
      }
    }   
    return 0;
}

bool firstConnect = true;

void restoreConfig() {
    int btnState = getBtnState();
    if (firstConnect && (btnState==1)) {
      showConfigMessage("Resume to default setting ?\n \n \nYes.(Long press Flash button)\n \nNo.(Short press Flash button)");
      while (true) {
        btnState = getBtnState();
        delay(200);
        if (btnState==1) break;
        if (btnState==2) {
          SPIFFS.remove(configFileName);
          showConfigMessage("\n \n \nSettings are restored. \n \nsystem goto restarting .....");
          delay(2000);
          systemRestart();             
        } 
        if ((millis() - timerPress)> 120 * 1000) break;
      }
    }
    if (btnState==2) {
      startConfig();
    }
}

boolean connectWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  //Manual Wifi
  WiFi.mode(WIFI_STA);
  Serial.print("[");
  Serial.print(WIFI_SSID.c_str());
  Serial.print("]");
  Serial.print("[");
  Serial.print(WIFI_PASS.c_str());
  Serial.print("]");
  
  WiFi.begin(WIFI_SSID.c_str(),WIFI_PASS.c_str());
  int i = 0; 
  while (WiFi.status() != WL_CONNECTED) {
    delay(50);
    if (i > 200) {
      Serial.println("Could not connect to WiFi");
      if (firstConnect) {
        firstConnect = false;
        startConfig();
      }
      return false;
    }
    if (!(i % 10)) drawProgress(i % 80 + 10,"Connecting to WiFi");    
    Serial.print(".");
    restoreConfig();
    i++;   
  }
  return true;
}

void setup() {
  Serial.begin(115200);
  WiFi.disconnect();

  // The LED pin needs to set HIGH
  // Use this pin to save energy
  // Turn on the background LED
  pinMode(TFT_LED, OUTPUT);

  #ifdef TFT_LED_LOW
    digitalWrite(TFT_LED, LOW);
  #else
    digitalWrite(TFT_LED, HIGH);    // HIGH to Turn on;
  #endif  
  delay(100);
  
  #define ILI9341_SWRESET 0x01
  tft.writecommand(ILI9341_SWRESET);  
  gfx.init();
  gfx.fillBuffer(MINI_BLACK);
  gfx.commit();

  // load config if it exists. Otherwise use defaults.
  boolean mounted = SPIFFS.begin();
  if (!mounted) {
    SPIFFS.format();
    SPIFFS.begin();
  }
  // SPIFFS.remove(configFileName);
  loadConfig();  

#ifdef HAVE_TOUCHPAD 
  ts.begin();
  //SPIFFS.remove("/calibration.txt");configFileName
  boolean isCalibrationAvailable = touchController.loadCalibration();
  if (!isCalibrationAvailable) {
    touchCalibration();
  } 
#endif  

  carousel.setFrames(frames, frameCount);
  carousel.disableAllIndicators();

  server.on ( "/", handleRoot );
  server.on ( "/save", handleSave);
  server.on ( "/reset", []() {
//     ESP.restart();
      systemRestart();       
  } );
  server.onNotFound ( handleNotFound );

  httpUpdater.setup(&server);
  server.begin();

  timerPress = millis();
  canBtnPress = true;

  // update the weather information
  updateData();
}

long lastDrew = 0;
bool btnLongClick;

float temperature = 0.0;

#ifdef LM75
  #include <Wire.h>
  float lm75() {
    unsigned int data[2];

    Wire.begin(SDA_PIN,SCL_PIN);
    Wire.setClock(700000);
  
    // Start I2C Transmission
    Wire.beginTransmission(Addr);
    // Select temperature data register
    Wire.write(0x00);
    // Stop I2C Transmission
    Wire.endTransmission();
  
    // Request 2 bytes of data
    Wire.requestFrom(Addr,2);

    // Read 2 bytes of data
    // temp msb, temp lsb
    if(Wire.available()==2)
    {
      data[0] = Wire.read();
      data[1] = Wire.read();
    } 
    //  Serial.println(data[0]);  
    //  Serial.println(data[1]);
    
    // Convert the data to 9-bits
    int temp = (data[0] * 256 + (data[1] & 0x80)) / 128;
    if (temp > 255){
      temp -= 512;
    }
    float cTemp = temp * 0.5;
    float fTemp = cTemp * 1.8 + 32;
  
    // Output data to serial monitor
    //  Serial.print("Temperature in Celsius:  ");
    //  Serial.print(cTemp);
    //  Serial.println(" C");
    //  Serial.print("Temperature in Fahrenheit:  ");
    //  Serial.print(fTemp);
    //  Serial.println(" F");  
    if (IS_METRIC) 
      return cTemp; 
    else
      return fTemp;    
  }
#endif

void showConfigMessage(String s) {
  gfx.fillBuffer(0);  
  gfx.setColor(1);  
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.setFont(ArialMT_Plain_16);
  gfx.drawString(240 / 2, 40,s);
  gfx.commit();  
}

void startConfig() {
  String s = "";
  if (WiFi.status() == WL_CONNECTED) {
      Serial.println ( "Open browser at http://" + WiFi.localIP().toString() );
      s = "AZSMZ TFT Setup Mode\nConnected to:\n" + WiFi.SSID() + "\nOpen browser at\nhttp://" + WiFi.localIP().toString();     
  } else {
      WiFi.mode(WIFI_AP);
      WiFi.softAP((ESP.getChipId()+CONFIG_SSID).c_str());
      IPAddress myIP = WiFi.softAPIP();  
      Serial.println(myIP);   
      s = "\nAZSMZ TFT Setup Mode\nConnect WiFi to:\n" + String(ESP.getChipId()) + CONFIG_SSID + "\nOpen browser at\nhttp://" + myIP.toString();
  }

  s += "\n \nPls short press again\n to exit setup.";
#ifdef HAVE_TOUCHPAD 
  s += "\n \nPls long press again\n to setup TOUCHPAD.";
#endif
  showConfigMessage(s);
  Serial.println ( "HTTP server started" );
  btnClick = true;
  btnLongClick = false;
  while(true) {
    server.handleClient();
    yield();

    int btnState =  getBtnState();
    if (btnState == 2) {
          #ifdef HAVE_TOUCHPAD           
            touchCalibration(); 
            showConfigMessage(s);          
          #endif 
    }
    if (btnState == 1) break;
    if ((millis() - timerPress)> 300 * 1000) break;
  }
  pinMode(BTN_1, OUTPUT);   
  updateData();       
}

float power;
#define VREF 2.5
#define MPW 10

void getPower() {
      static int i;
      static float p = 0;
      //XPT2046_setCFG(CFG_POWER);
      if (!power) power = XPT2046_ReadRaw(CFG_POWER) * VREF * 4 / 4096;        
      if (!i) {
        power = p/MPW;
        p = 0;
        i = MPW;
      } else {
        i--;
        int v = XPT2046_ReadRaw(CFG_POWER);   
        p += v * VREF * 4 / 4096;
      }
}

void loop() {

#ifdef HAVE_TOUCHPAD
  if (touchController.isTouched(500)) {
    TS_Point p = touchController.getPoint();
    if (screen==screenCount) screen=0;
    else {
      if (p.y < 80) {
        IS_STYLE_12HR = !IS_STYLE_12HR;
      } else {
          screen = (screen + 1) % screenCount;
      }
    }
    timerTouch = millis();            
  }
#endif  

#ifdef BTN_1
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(TOUCH_CS, HIGH);
  digitalWrite(BTN_1, 0);

  int btnState = getBtnState();
  if (btnState==1) {
    if (screen==screenCount) screen=0;
    else {
      screen = (screen + 1) % screenCount;
    }
  }
  if (btnState==2) {
    btnLongClick = true;
  } 
#endif  

  #ifdef HAVE_TOUCHPAD
//    power = XPT2046_ReadRaw(CFG_POWER) * 2.5 * 4 / 4096;
    getPower();  
    #ifdef NTC
      temperature = readNTC()/10.0;
    #endif 
    if (btnLongClick) {
      touchCalibration();   // long-press = recalibrate touch screen
      btnLongClick = false;
    }

  #else
    power = analogRead(A0) * 49 / 10240.0;
    if (btnLongClick) {
      startConfig();          
      btnLongClick = false;
    }
    
  #endif

//  if ((screen<screenCount) && ((millis() - timerPress)> 30 * 1000)) screen = 0;  // after 30 secs return screen 0
  if ((screen<screenCount) && ((millis() - timerPress)> 30 * 1000) && ((millis() - timerTouch)> 30 * 1000)) screen = 0;  // after 30 secs return screen 0
  
  gfx.fillBuffer(MINI_BLACK);
  if (screen == 0) {
    drawTime();
    drawWifiQuality();
    #ifdef BATT
      drawBattery();
    #endif     
    int remainingTimeBudget = carousel.update();
    if (remainingTimeBudget > 0) {
      // You can do some work here
      // Don't do stuff if you are below your
      // time budget.
      delay(remainingTimeBudget);         
    }
    drawCurrentWeather();
    drawAstronomy();
  } else if (screen == 1) {
    drawCurrentWeatherDetail();
  } else if (screen == 2) {
    drawForecastTable(0);
  } else if (screen == 3) {
    drawForecastTable(6);
  } else if (screen == 4) {
    drawAbout();
  } else if (screen == screenCount) {
    drawTime(true);
  }
  gfx.commit();


  if (SLEEP_INTERVAL_SECS && (millis() - timerPress >= SLEEP_INTERVAL_SECS * 1000) && (millis() - timerTouch >= SLEEP_INTERVAL_SECS * 1000)){ // after 2 minutes go to sleep
      drawProgress(25,"Going to Sleep!");
      delay(1000);
      drawProgress(50,"Going to Sleep!");
      delay(1000);
      drawProgress(75,"Going to Sleep!");
      delay(1000);    
      drawProgress(100,"Going to Sleep!");
      // go to deepsleep for xx minutes or 0 = permanently
      #ifdef HAVE_TOUCHPAD      
        XPT2046_EnableIrq(CFG_LIRQ);
      #endif        
      ESP.deepSleep(0,  WAKE_RF_DEFAULT);                       // 0 delay = permanently to sleep
  }

  if (SAVER_INTERVAL_SECS && (millis() - timerPress >= SAVER_INTERVAL_SECS * 1000)&&(millis() - timerTouch >= SAVER_INTERVAL_SECS * 1000)){ // after SAVER_INTERVAL_SECS go to saver
      screen = screenCount;
  }

  
  // Check if we should update weather information
  if (millis() - lastDownloadUpdate > 1000 * UPDATE_INTERVAL_SECS) {
      updateData();
      lastDownloadUpdate = millis();
  } 
}

// Update the internet based information and update screen
void updateData() {
  gfx.fillBuffer(MINI_BLACK);
  gfx.setFont(ArialRoundedMTBold_14);

  if (!connectWifi()) return;
 
  drawProgress(10, "Updating time...");
  configTime(UTC_OFFSET * 3600, 0, NTP_SERVERS);

  // DNS warm-up + settle: the first TCP/DNS attempt right after a fresh WiFi
  // connect often fails ("failed to connect to host"). Resolve the API host
  // (with retries) so the connection is ready before the weather fetch.
  Serial.printf("NET ip=%s gw=%s dns0=%s dns1=%s\n",
                WiFi.localIP().toString().c_str(),
                WiFi.gatewayIP().toString().c_str(),
                WiFi.dnsIP(0).toString().c_str(),
                WiFi.dnsIP(1).toString().c_str());
  IPAddress owmIp;
  for (int r = 0; r < 8; r++) {
    if (WiFi.hostByName("api.openweathermap.org", owmIp) && owmIp != INADDR_NONE && (uint32_t)owmIp != 0) break;
    Serial.println("Waiting for DNS...");
    drawProgress(30, "Resolving server...");
    delay(1000);
  }
  Serial.printf("api.openweathermap.org -> %s\n", owmIp.toString().c_str());

  drawProgress(50, "Updating conditions...");
  OpenWeatherMapCurrent *currentWeatherClient = new OpenWeatherMapCurrent();
  currentWeatherClient->setMetric(IS_METRIC);
  currentWeatherClient->setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
  currentWeatherClient->updateCurrentById(&currentWeather, OPEN_WEATHER_MAP_API_KEY, OPEN_WEATHER_MAP_LOCATION_ID);
  delete currentWeatherClient;
  currentWeatherClient = nullptr;

  drawProgress(70, "Updating forecasts...");
  OpenWeatherMapForecast *forecastClient = new OpenWeatherMapForecast();
  forecastClient->setMetric(IS_METRIC);
  forecastClient->setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
  uint8_t allowedHours[] = {12, 0};
  forecastClient->setAllowedHours(allowedHours, sizeof(allowedHours));
  forecastClient->updateForecastsById(forecasts, OPEN_WEATHER_MAP_API_KEY, OPEN_WEATHER_MAP_LOCATION_ID, MAX_FORECASTS);
  delete forecastClient;
  forecastClient = nullptr;

  drawProgress(80, "Updating astronomy...");
  // SunMoonCalc needs the current epoch + lat/lon (degrees). Moon phase will be
  // wrong until the clock is synced; sunrise/sunset come from OWM as absolute UTC.
  time_t now = time(nullptr);
  SunMoonCalc *smCalc = new SunMoonCalc(now, currentWeather.lat, currentWeather.lon);
  moonData = smCalc->calculateSunAndMoonData().moon;
  delete smCalc;
  smCalc = nullptr;

//  WiFi.mode(WIFI_OFF);
  delay(1000);
}

// Progress bar helper
void drawProgress(uint8_t percentage, String text) {
  gfx.fillBuffer(MINI_BLACK);
  gfx.drawPalettedBitmapFromPgm(23, 30, SquixLogo);
  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.setColor(MINI_WHITE);
  gfx.drawString(120, 80, "https://blog.squix.org");
  gfx.setColor(MINI_YELLOW);

  gfx.drawString(120, 146, text);
  gfx.setColor(MINI_WHITE);
  gfx.drawRect(10, 168, 240 - 20, 15);
  gfx.setColor(MINI_BLUE);
  gfx.fillRect(12, 170, 216 * percentage / 100, 11);

  gfx.commit();
}

String time_prev;
long timeSaver;

// draws the clock
void drawTime(bool saver) {
  static int x;
  static int y;
  if (!saver) {
    x = 120;
    y = 6;
  }  
  char *dstAbbrev;
  char time_str[11];
  time_t now = dstAdjusted.time(&dstAbbrev);
  struct tm * timeinfo = localtime (&now);

  if (IS_STYLE_12HR) {
    int hour = (timeinfo->tm_hour+11)%12+1;  // take care of noon and midnight
    sprintf(time_str, "%2d:%02d:%02d\n",hour, timeinfo->tm_min, timeinfo->tm_sec);
  } else {
    sprintf(time_str, "%02d:%02d:%02d\n",timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
  }


  if (saver && (!time_prev.equals(time_str) || (millis() - timeSaver > 1000))) {
    x = random(75,130);
    y = random(270);
    time_prev = time_str;
    timeSaver = millis();    
  } 

  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setColor(MINI_WHITE);
  String date = ctime(&now);
  date = date.substring(0,11) + String(1900 + timeinfo->tm_year);
//  gfx.drawString(120, 6, date);
  gfx.drawString(x, y, date);  

  gfx.setFont(ArialRoundedMTBold_36);
//  gfx.drawString(120, 20, time_str);
  gfx.drawString(x, y+14, time_str);


  gfx.setTextAlignment(TEXT_ALIGN_LEFT);
  gfx.setFont(ArialMT_Plain_10);
  gfx.setColor(MINI_BLUE);
  if (IS_STYLE_12HR) {
    sprintf(time_str, "%s\n%s", dstAbbrev, timeinfo->tm_hour>=12?"PM":"AM");
  } else {
    sprintf(time_str, "%s", dstAbbrev);
  }
//  gfx.drawString(195, 27, time_str);  
  gfx.drawString(x + 75, y + 21, time_str);  // Known bug: Cuts off 4th character of timezone abbreviation
}

// draws current weather information
void drawCurrentWeather() {
  gfx.setTransparentColor(MINI_BLACK);
  gfx.drawPalettedBitmapFromPgm(0, 55, getMeteoconIconFromProgmem(owmToCxandyIcon(currentWeather.icon)));

  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setColor(MINI_BLUE);
  gfx.setTextAlignment(TEXT_ALIGN_RIGHT);
  gfx.drawString(220, 65, DISPLAYED_CITY_NAME);

  gfx.setFont(ArialRoundedMTBold_36);
  gfx.setColor(MINI_WHITE);
  gfx.setTextAlignment(TEXT_ALIGN_RIGHT);
  String degreeSign = IS_METRIC ? "°C" : "°F";

  // OpenWeatherMap current temperature (outdoor). The board's onboard NTC
  // thermistor reading is still available in `temperature` for the detail screen.
  gfx.drawString(220, 78, String(currentWeather.temp, 1) + degreeSign);

  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setColor(MINI_YELLOW);
  gfx.setTextAlignment(TEXT_ALIGN_RIGHT);
  gfx.drawString(220, 118, currentWeather.description);
}

void drawForecast1(MiniGrafx *display, CarouselState* state, int16_t x, int16_t y) {
  drawForecastDetail(x + 10, y + 165, 0);
  drawForecastDetail(x + 95, y + 165, 1);
  drawForecastDetail(x + 180, y + 165, 2);
}

void drawForecast2(MiniGrafx *display, CarouselState* state, int16_t x, int16_t y) {
  drawForecastDetail(x + 10, y + 165, 3);
  drawForecastDetail(x + 95, y + 165, 4);
  drawForecastDetail(x + 180, y + 165, 5);
}


// helper for the forecast columns
void drawForecastDetail(uint16_t x, uint16_t y, uint8_t dayIndex) {
  gfx.setColor(MINI_YELLOW);
  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  time_t time = forecasts[dayIndex].observationTime;
  struct tm * timeinfo = localtime(&time);
  gfx.drawString(x + 25, y - 15, WDAY_NAMES[timeinfo->tm_wday] + " " + String(timeinfo->tm_hour) + ":00");

  gfx.setColor(MINI_WHITE);
  gfx.drawString(x + 25, y, String(forecasts[dayIndex].temp, 0) + (IS_METRIC ? "°C" : "°F"));

  gfx.drawPalettedBitmapFromPgm(x, y + 15, getMiniMeteoconIconFromProgmem(owmToCxandyIcon(forecasts[dayIndex].icon)));
  gfx.setColor(MINI_BLUE);
  gfx.drawString(x + 25, y + 60, String(forecasts[dayIndex].rain, 1) + (IS_METRIC ? "mm" : "in"));
}

// draw moonphase and sunrise/set and moonrise/set
void drawAstronomy() {

  gfx.setFont(MoonPhases_Regular_36);
  gfx.setColor(MINI_WHITE);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.drawString(120, 275, String(determineMoonIcon()));

  gfx.setColor(MINI_WHITE);
  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.setColor(MINI_YELLOW);
  gfx.drawString(120, 250, moonData.phase.name);

  gfx.setTextAlignment(TEXT_ALIGN_LEFT);
  gfx.setColor(MINI_YELLOW);
  gfx.drawString(5, 250, "Sun");
  gfx.setColor(MINI_WHITE);
  time_t t = currentWeather.sunrise;
  gfx.drawString(5, 276, "Rise: " + getTime(&t));
  t = currentWeather.sunset;
  gfx.drawString(5, 291, "Set:  " + getTime(&t));

  gfx.setTextAlignment(TEXT_ALIGN_RIGHT);
  gfx.setColor(MINI_YELLOW);
  gfx.drawString(235, 250, "Moon");
  gfx.setColor(MINI_WHITE);
  gfx.drawString(235, 276, "Illum: " + String(moonData.illumination * 100, 0) + "%");
  gfx.drawString(235, 291, "Age: " + String(moonData.age, 1) + "d");

}

void drawCurrentWeatherDetail() {
  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.setColor(MINI_WHITE);
  gfx.drawString(120, 2, "Current Conditions");

  //gfx.setTransparentColor(MINI_BLACK);
  //gfx.drawPalettedBitmapFromPgm(0, 20, getMeteoconIconFromProgmem(conditions.weatherIcon));

  String degreeSign = IS_METRIC ? "°C" : "°F";
  const char* dirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  String windDir = dirs[(int)((currentWeather.windDeg + 22.5) / 45.0) % 8];
  String windUnit = IS_METRIC ? "m/s" : "mph";

  drawLabelValue(0, "Temperature:", String(currentWeather.temp, 1) + degreeSign);
  drawLabelValue(1, "Feels Like:", String(currentWeather.feelsLike, 1) + degreeSign);
  drawLabelValue(2, "Humidity:", String(currentWeather.humidity) + "%");
  drawLabelValue(3, "Pressure:", String(currentWeather.pressure) + "hPa");
  drawLabelValue(4, "Wind Speed:", String(currentWeather.windSpeed, 1) + windUnit);
  drawLabelValue(5, "Wind Dir:", windDir);
  drawLabelValue(6, "Clouds:", String(currentWeather.clouds) + "%");
#ifdef NTC
  drawLabelValue(7, "Indoor Temp:", String((int)temperature) + degreeSign);
#endif

  gfx.setTextAlignment(TEXT_ALIGN_LEFT);
  gfx.setColor(MINI_YELLOW);
  gfx.drawString(15, 185, "Description: ");
  gfx.setColor(MINI_WHITE);
  gfx.drawStringMaxWidth(15, 200, 240 - 2 * 15, currentWeather.description);
}

void drawLabelValue(uint8_t line, String label, String value) {
  const uint8_t labelX = 15;
  const uint8_t valueX = 150;
  gfx.setTextAlignment(TEXT_ALIGN_LEFT);
  gfx.setColor(MINI_YELLOW);
  gfx.drawString(labelX, 30 + line * 15, label);
  gfx.setColor(MINI_WHITE);
  gfx.drawString(valueX, 30 + line * 15, value);
}

// converts the dBm to a range between 0 and 100%
int8_t getWifiQuality() {
  int32_t dbm = WiFi.RSSI();
  if(dbm <= -100) {
      return 0;
  } else if(dbm >= -50) {
      return 100;
  } else {
      return 2 * (dbm + 100);
  }
}

void drawWifiQuality() {
  int8_t quality = getWifiQuality();
  gfx.setColor(MINI_WHITE);
  gfx.setTextAlignment(TEXT_ALIGN_RIGHT);  
  gfx.drawString(228, 9, String(quality) + "%");
  for (int8_t i = 0; i < 4; i++) {
    for (int8_t j = 0; j < 2 * (i + 1); j++) {
      if (quality > i * 25 || j == 0) {
        gfx.setPixel(230 + 2 * i, 18 - j);
      }
    }
  }
}

void drawBattery() {
  uint8_t percentage = 100;
  if (power > 4.15) percentage = 100;
  else if (power < 3.7) percentage = 0;
  else percentage = (power - 3.7) * 100 / (4.15-3.7);
  
  gfx.setColor(MINI_WHITE);
  gfx.setTextAlignment(TEXT_ALIGN_LEFT);  
  gfx.drawString(26, 9, String(percentage) + "%");
  gfx.drawRect(1, 11, 18, 9);
  gfx.drawLine(21,13,21,17);  
  gfx.drawLine(22,13,22,17);  
  gfx.setColor(MINI_BLUE); 
  gfx.fillRect(3, 13, 15 * percentage / 100, 5);
}

void drawForecastTable(uint8_t start) {
  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.setColor(MINI_WHITE);
  gfx.drawString(120, 2, "Forecasts");
  uint16_t y = 0;

  String degreeSign = "°F";
  if (IS_METRIC) {
    degreeSign = "°C";
  }
  for (uint8_t i = start; i < start + 6; i++) {
    gfx.setTextAlignment(TEXT_ALIGN_LEFT);
    y = 30 + (i - start) * 45;
    if (y > 320) {
      break;
    }
    gfx.drawPalettedBitmapFromPgm(0, y, getMiniMeteoconIconFromProgmem(owmToCxandyIcon(forecasts[i].icon)));

    gfx.setColor(MINI_YELLOW);
    gfx.setFont(ArialRoundedMTBold_14);

    time_t time = forecasts[i].observationTime;
    struct tm * timeinfo = localtime(&time);
    gfx.drawString(50, y, WDAY_NAMES[timeinfo->tm_wday] + " " + String(timeinfo->tm_hour) + ":00");
    gfx.setColor(MINI_WHITE);
    gfx.drawString(50, y + 15, forecasts[i].description);
    gfx.setColor(MINI_WHITE);
    gfx.setTextAlignment(TEXT_ALIGN_RIGHT);

    gfx.drawString(235, y, String(forecasts[i].temp, 0) + degreeSign);
    gfx.setColor(MINI_BLUE);
    gfx.drawString(235, y + 15, String(forecasts[i].rain, 1) + (IS_METRIC ? "mm" : "in"));

  }
}

void drawAbout() {
  gfx.fillBuffer(MINI_BLACK);
  gfx.drawPalettedBitmapFromPgm(23, 30, SquixLogo);

  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.setColor(MINI_WHITE);
  gfx.drawString(120, 80, "https://blog.squix.org");

  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  drawLabelValue(7, "Heap Mem:", String(ESP.getFreeHeap() / 1024)+"kb");
  drawLabelValue(8, "Flash Mem:", String(ESP.getFlashChipRealSize() / 1024 / 1024) + "MB");
  drawLabelValue(9, "WiFi Strength:", String(WiFi.RSSI()) + "dB");
  drawLabelValue(10, "Chip ID:", String(ESP.getChipId()));
  
  #ifdef BATT
    drawLabelValue(11, "Battery: ", String(power) +"V");
  #else
    drawLabelValue(11, "VCC: ", String(ESP.getVcc() / 1024.0) +"V");
  #endif     
  
  drawLabelValue(12, "CPU Freq.: ", String(ESP.getCpuFreqMHz()) + "MHz");
  char time_str[15];
  const uint32_t millis_in_day = 1000 * 60 * 60 * 24;
  const uint32_t millis_in_hour = 1000 * 60 * 60;
  const uint32_t millis_in_minute = 1000 * 60;
  uint8_t days = millis() / (millis_in_day);
  uint8_t hours = (millis() - (days * millis_in_day)) / millis_in_hour;
  uint8_t minutes = (millis() - (days * millis_in_day) - (hours * millis_in_hour)) / millis_in_minute;
  sprintf(time_str, "%2dd%2dh%2dm", days, hours, minutes);
  drawLabelValue(13, "Uptime: ", time_str);
  gfx.setTextAlignment(TEXT_ALIGN_LEFT);
  gfx.setColor(MINI_YELLOW);
  gfx.drawString(15, 250, "Last Reset: ");
  gfx.setColor(MINI_WHITE);
  gfx.drawStringMaxWidth(15, 265, 240 - 2 * 15, ESP.getResetInfo());
}

void calibrationCallback(int16_t x, int16_t y) {
  gfx.setColor(1);
  gfx.fillCircle(x, y, 10);
}

// Helper function, should be part of the weather station library and should disappear soon
// Convert an OpenWeatherMap icon code (e.g. "09d") to cxandy's Wunderground-style
// icon name so the existing bitmaps in weathericons.h can be reused.
String owmToCxandyIcon(String owmIcon) {
  bool day = owmIcon.endsWith("d");
  String c = owmIcon.substring(0, 2);
  if (c == "01") return day ? "sunny" : "clear";
  if (c == "02") return day ? "partlysunny" : "partlycloudy";
  if (c == "03") return "partlycloudy";
  if (c == "04") return "cloudy";
  if (c == "09") return "rain";
  if (c == "10") return "rain";
  if (c == "11") return "tstorms";
  if (c == "13") return "snow";
  if (c == "50") return "fog";
  return "unknown";
}

// Format a UTC epoch as local HH:MM (applies the configTime offset).
String getTime(time_t *timestamp) {
  struct tm *timeInfo = localtime(timestamp);
  char buf[9];
  if (IS_STYLE_12HR) {
    int hour = (timeInfo->tm_hour + 11) % 12 + 1;
    sprintf(buf, "%2d:%02d%s", hour, timeInfo->tm_min, timeInfo->tm_hour >= 12 ? "p" : "a");
  } else {
    sprintf(buf, "%02d:%02d", timeInfo->tm_hour, timeInfo->tm_min);
  }
  return String(buf);
}

// Pick a Moon Phases font glyph from the current illumination/phase (northern hemisphere).
char determineMoonIcon() {
  int index = (int) round(moonData.illumination * 14);  // 0..14
  if (index < 0) index = 0;
  if (index > 14) index = 14;
  const char north_waning[] = {64,77,76,75,74,73,72,71,70,69,68,67,66,65,48};
  const char north_waxing[] = {64,78,79,80,81,82,83,84,85,86,87,88,89,90,48};
  return (moonData.phase.index > 4) ? north_waning[index] : north_waxing[index];
}

const char* getMeteoconIconFromProgmem(String iconText) {

  if (iconText == "chanceflurries") return chanceflurries;
  if (iconText == "chancerain") return chancerain;
  if (iconText == "chancesleet") return chancesleet;
  if (iconText == "chancesnow") return chancesnow;
  if (iconText == "chancetstorms") return chancestorms;
  if (iconText == "clear") return clear;
  if (iconText == "cloudy") return cloudy;
  if (iconText == "flurries") return flurries;
  if (iconText == "fog") return fog;
  if (iconText == "hazy") return hazy;
  if (iconText == "mostlycloudy") return mostlycloudy;
  if (iconText == "mostlysunny") return mostlysunny;
  if (iconText == "partlycloudy") return partlycloudy;
  if (iconText == "partlysunny") return partlysunny;
  if (iconText == "sleet") return sleet;
  if (iconText == "rain") return rain;
  if (iconText == "snow") return snow;
  if (iconText == "sunny") return sunny;
  if (iconText == "tstorms") return tstorms;


  return unknown;
}
const char* getMiniMeteoconIconFromProgmem(String iconText) {
  if (iconText == "chanceflurries") return minichanceflurries;
  if (iconText == "chancerain") return minichancerain;
  if (iconText == "chancesleet") return minichancesleet;
  if (iconText == "chancesnow") return minichancesnow;
  if (iconText == "chancetstorms") return minichancestorms;
  if (iconText == "clear") return miniclear;
  if (iconText == "cloudy") return minicloudy;
  if (iconText == "flurries") return miniflurries;
  if (iconText == "fog") return minifog;
  if (iconText == "hazy") return minihazy;
  if (iconText == "mostlycloudy") return minimostlycloudy;
  if (iconText == "mostlysunny") return minimostlysunny;
  if (iconText == "partlycloudy") return minipartlycloudy;
  if (iconText == "partlysunny") return minipartlysunny;
  if (iconText == "sleet") return minisleet;
  if (iconText == "rain") return minirain;
  if (iconText == "snow") return minisnow;
  if (iconText == "sunny") return minisunny;
  if (iconText == "tstorms") return minitstorms;


  return miniunknown;
}
// Helper function, should be part of the weather station library and should disappear soon
const String getShortText(String iconText) {

  if (iconText == "chanceflurries") return "Chance of Flurries";
  if (iconText == "chancerain") return "Chance of Rain";
  if (iconText == "chancesleet") return "Chance of Sleet";
  if (iconText == "chancesnow") return "Chance of Snow";
  if (iconText == "chancetstorms") return "Chance of Storms";
  if (iconText == "clear") return "Clear";
  if (iconText == "cloudy") return "Cloudy";
  if (iconText == "flurries") return "Flurries";
  if (iconText == "fog") return "Fog";
  if (iconText == "hazy") return "Hazy";
  if (iconText == "mostlycloudy") return "Mostly Cloudy";
  if (iconText == "mostlysunny") return "Mostly Sunny";
  if (iconText == "partlycloudy") return "Partly Couldy";
  if (iconText == "partlysunny") return "Partly Sunny";
  if (iconText == "sleet") return "Sleet";
  if (iconText == "rain") return "Rain";
  if (iconText == "snow") return "Snow";
  if (iconText == "sunny") return "Sunny";
  if (iconText == "tstorms") return "Storms";


  return "-";
}
