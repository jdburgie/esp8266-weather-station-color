# JOURNAL — esp8266-weather-station-color

Running log (newest on top) + todos. This fork is based on cxandy's
esp8266-weather-station-color, converted from Wunderground to OpenWeatherMap,
running on AZSMZ_1_6 hardware.

## Todos
- [ ] Verify the config **AP portal** end-to-end (auto-opens on WiFi-fail; AP "<chipid>-WeatherStation" → http://192.168.4.1 → save writes /weatherstation.conf).
- [ ] Verify **touch calibration** on FLASH-button long-press during normal operation.
- [ ] Remove the temporary `NET ...` / `Waiting for DNS...` diagnostic Serial prints in updateData() (keep the DNS warm-up loop).
- [ ] 3D printed enclosure (in progress, separate track). Note: wake is a tap anywhere on the touchscreen, so the screen must stay physically accessible.

## 2026-07-18 — Deep sleep + wake-on-touch enabled
- Set `SLEEP_INTERVAL_SECS = 60` (deep sleep after 60 s of no touch/button). Wake-on-touch verified working on the AZSMZ_1_6 board → confirms the XPT2046 **PENIRQ is wired to RST**.
- Mechanism: before `ESP.deepSleep(0, WAKE_RF_DEFAULT)` the firmware sets the touch controller to low-power IRQ mode (`XPT2046_EnableIrq(CFG_LIRQ)`); a tap pulses RST and the ESP cold-boots (reconnects WiFi + refetches weather, ~few seconds — not a resume).
- Idle timer resets on any touch (`timerTouch`) or button press (`timerPress`); sleep only triggers when BOTH have been idle for the full interval, so interacting keeps it awake.
- Power: ~20 µA asleep vs ~70 mA active.

## 2026-07-18 — Working: OpenWeatherMap port complete
- Full station functional on the AZSMZ_1_6 board: display, WiFi, date/time, current weather + forecast.
- Base reset to the cxandy fork; ported **Wunderground -> OpenWeatherMap** (ESP8266 Weather Station lib 2.3.0: OpenWeatherMapCurrent/Forecast + SunMoonCalc).
  - `updateData()` now uses OWM current + forecast (daily via `setAllowedHours({12,0})`) + SunMoonCalc for the moon.
  - Rewrote `drawCurrentWeather`, `drawForecastDetail`, `drawForecastTable`, `drawAstronomy`, `drawCurrentWeatherDetail` to OWM fields.
  - Added `owmToCxandyIcon()` to map OWM icon codes -> cxandy's existing icon bitmaps; `getTime()` and `determineMoonIcon()` helpers.
  - `configportal.h` converted to OWM fields (WiFi, API key, location id, city name, units, 12/24h); saves `/weatherstation.conf`. Renamed the `HTTP_HEAD` PROGMEM constant (collided with ESP8266WebServer's `HTTP_HEAD` enum on core 2.7.4).
  - **Touch calibration** moved to FLASH long-press in `loop()`; config portal auto-opens on WiFi-fail.
- **Root cause of the earlier no-weather/no-clock: DNS, not NTP.** The board's DHCP lease pointed gateway+DNS at 192.168.12.53 (a second router/mesh node) which wasn't resolving, so both OpenWeather and NTP (hostnames) failed. Fixing DNS on .53 made both work. Added a DNS warm-up in `updateData()` for robustness.
- Toolchain: `arduino-cli`, esp8266 core 2.7.4, FQBN `esp8266:esp8266:d1_mini`. Real WiFi/API creds are NOT committed (`settings.h` shipped with placeholders; configure via the portal).

## Hardware notes
- Board: AZSMZ_1_6 (ILI9341 240x320 + XPT2046 touch), ESP8266 4MB, MAC 84:f3:eb:4d:2e:97.
- Pins (settings.h `BOARD AZSMZ_1_6`): TFT_DC=GPIO0, TFT_CS=GPIO2, TFT_LED=GPIO16 (active-LOW), TOUCH_CS=GPIO5, TOUCH_IRQ=GPIO4, FLASH button = GPIO0 (shared with TFT_DC).
