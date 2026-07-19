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
See more at http://blog.squix.ch

Config portal adapted for OpenWeatherMap (was Wunderground).
*/

#include <ESP8266WebServer.h>
#include <MiniGrafx.h>
#include <FS.h>

const char HTTP_PAGE_HEAD[] PROGMEM            = "<!DOCTYPE html><html lang=\"en\"><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/><title>{v}</title>";
const char HTTP_STYLE[] PROGMEM           = "<style>.c{text-align: center;} div,input, select{padding:5px;font-size:1em;} input, select{width:95%;} body{text-align: center;font-family:verdana;} button{border:0;border-radius:0.3rem;background-color:#1fa3ec;color:#fff;line-height:2.4rem;font-size:1.2rem;width:100%;}</style>";
const char HTTP_PAGE_HEAD_END[] PROGMEM        = "</head><body><div style='text-align:left;display:inline-block;min-width:260px;'>";
const char HTTP_FORM_START[] PROGMEM      = "<form method='post' action='save'><br/>";
const char HTTP_FORM_PARAM[] PROGMEM      = "<label for='{i}'>{p}</label><br/><input id='{i}' name='{n}' maxlength={l}  value='{v}' {c}><br/><br/>";
const char HTTP_FORM_END[] PROGMEM        = "<br/><button type='submit'>save</button></form><br/><form action=\"/reset\" method=\"post\"><button>Restart ESP</button></form>";
const char HTTP_SAVED[] PROGMEM           = "<div>Settings Saved<br />Restart the device to apply.</div>";
const char HTTP_END[] PROGMEM             = "</div></body></html>";
const char HTTP_OPTION_ITEM[] PROGMEM     = "<option value=\"{v}\" {s}>{n}</option>";

ESP8266WebServer server (80);

#define configFileName "/weatherstation.conf"

String getFormField(String id, String placeholder, String length, String value, String customHTML) {
    String pitem = FPSTR(HTTP_FORM_PARAM);
    pitem.replace("{i}", id);
    pitem.replace("{n}", id);
    pitem.replace("{p}", placeholder);
    pitem.replace("{l}", length);
    pitem.replace("{v}", value);
    pitem.replace("{c}", customHTML);
    return pitem;
}

String getSelectField(String id, String label, String selectedValue,
                      String v1, String n1, String v2, String n2) {
  String page = "<label for=\"" + id + "\">" + label + "</label>";
  page += "<select id=\"" + id + "\" name=\"" + id + "\">";
  String o1 = FPSTR(HTTP_OPTION_ITEM); o1.replace("{v}", v1); o1.replace("{n}", n1); o1.replace("{s}", selectedValue == v1 ? "selected" : "");
  String o2 = FPSTR(HTTP_OPTION_ITEM); o2.replace("{v}", v2); o2.replace("{n}", n2); o2.replace("{s}", selectedValue == v2 ? "selected" : "");
  page += o1 + o2 + "</select><br/><br/>";
  return page;
}

boolean saveConfig() {
  File f = SPIFFS.open(configFileName, "w+");
  if (!f) {
    Serial.println("Failed to open config file");
    return false;
  }
  f.print("WIFI_SSID=");        f.println(WIFI_SSID);
  f.print("WIFI_PASS=");        f.println(WIFI_PASS);
  f.print("OWM_API_KEY=");      f.println(OPEN_WEATHER_MAP_API_KEY);
  f.print("OWM_LOCATION_ID=");  f.println(OPEN_WEATHER_MAP_LOCATION_ID);
  f.print("CITY_NAME=");        f.println(DISPLAYED_CITY_NAME);
  f.print("IS_METRIC=");        f.println(IS_METRIC ? "true" : "false");
  f.print("IS_STYLE_12HR=");    f.println(IS_STYLE_12HR ? "true" : "false");
  f.close();
  Serial.println("Saved values");
  return true;
}

boolean loadConfig() {
  File f = SPIFFS.open(configFileName, "r");
  if (!f) {
    Serial.println("No config file, using defaults from settings.h");
    return false;
  }
  while (f.available()) {
    String key = f.readStringUntil('=');
    String value = f.readStringUntil('\r');
    f.read();
    Serial.println(key + " = [" + value + "]");
    if (key == "WIFI_SSID")            WIFI_SSID = value;
    else if (key == "WIFI_PASS")       WIFI_PASS = value;
    else if (key == "OWM_API_KEY")     OPEN_WEATHER_MAP_API_KEY = value;
    else if (key == "OWM_LOCATION_ID") OPEN_WEATHER_MAP_LOCATION_ID = value;
    else if (key == "CITY_NAME")       DISPLAYED_CITY_NAME = value;
    else if (key == "IS_METRIC")       IS_METRIC = (value == "true");
    else if (key == "IS_STYLE_12HR")   IS_STYLE_12HR = (value == "true");
  }
  f.close();
  Serial.println("Loaded config");
  return true;
}

void handleRoot() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Content-Type", "text/html", true);
  server.sendHeader("Cache-Control", "no-cache");
  server.send(200);

  String page = FPSTR(HTTP_PAGE_HEAD);
  page.replace("{v}", "Weather Station Setup");
  page += FPSTR(HTTP_STYLE);
  page += FPSTR(HTTP_PAGE_HEAD_END);
  page += "<h1>Weather Station Setup</h1>";
  page += FPSTR(HTTP_FORM_START);
  page += getFormField("ssid", "WiFi SSID", "32", WIFI_SSID, "");
  page += getFormField("password", "WiFi Password", "64", WIFI_PASS, "");
  server.sendContent(page); page = "";

  String maskedKey = OPEN_WEATHER_MAP_API_KEY.length() > 6
      ? OPEN_WEATHER_MAP_API_KEY.substring(0, 3) + "***" + OPEN_WEATHER_MAP_API_KEY.substring(OPEN_WEATHER_MAP_API_KEY.length() - 3)
      : OPEN_WEATHER_MAP_API_KEY;
  page += getFormField("owmkey", "OpenWeatherMap API Key", "40", maskedKey, "");
  page += getFormField("owmlocation", "OpenWeatherMap Location ID", "20", OPEN_WEATHER_MAP_LOCATION_ID, "");
  page += getFormField("cityname", "Displayed City Name", "40", DISPLAYED_CITY_NAME, "");
  server.sendContent(page); page = "";

  page += getSelectField("units", "Units", IS_METRIC ? "metric" : "imperial",
                         "imperial", "Imperial (F)", "metric", "Metric (C)");
  page += getSelectField("clock", "Clock", IS_STYLE_12HR ? "12" : "24",
                         "12", "12-hour", "24", "24-hour");
  server.sendContent(page); page = "";

  page += FPSTR(HTTP_FORM_END);
  page += FPSTR(HTTP_END);
  server.sendContent(page); page = "";
  server.sendContent("");
}

void handleSave() {
  WIFI_SSID = server.arg("ssid");
  WIFI_PASS = server.arg("password");
  OPEN_WEATHER_MAP_LOCATION_ID = server.arg("owmlocation");
  DISPLAYED_CITY_NAME = server.arg("cityname");
  IS_METRIC = (server.arg("units") == "metric");
  IS_STYLE_12HR = (server.arg("clock") == "12");

  // only overwrite the API key if the user typed a real one (not the masked value)
  String apiKey = server.arg("owmkey");
  if (apiKey.length() > 10 && apiKey.indexOf('*') < 0) OPEN_WEATHER_MAP_API_KEY = apiKey;

  saveConfig();

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200);
  String page = FPSTR(HTTP_PAGE_HEAD);
  page.replace("{v}", "Saved");
  page += FPSTR(HTTP_STYLE);
  page += FPSTR(HTTP_PAGE_HEAD_END);
  page += FPSTR(HTTP_SAVED);
  page += FPSTR(HTTP_FORM_END);
  page += FPSTR(HTTP_END);
  server.sendContent(page);
  server.sendContent("");
}

void handleNotFound() {
  String message = "File Not Found\n\nURI: ";
  message += server.uri();
  server.send(404, "text/plain", message);
}
