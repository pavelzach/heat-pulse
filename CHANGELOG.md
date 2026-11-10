# Changelog

## Unreleased

### SHT3x room sensor

- **SHT30/31/35 supported besides the SHT4x,** e.g. the [DFRobot SHT31 Weather-proof probe SEN0385](https://wiki.dfrobot.com/SHT31_Temperature_Humidity_Sensor_Weatherproof_SKU_SEN0385) (housing, 1 m cable). Same wiring (SDA = GPIO33, SCL = GPIO22). The type is **detected automatically** (SHT4x serial number / SHT3x status register, both checked by CRC) and again whenever the sensor stops answering, so a sensor can be swapped while running.
- **Setting renamed:** `CELLAR_SHT4X` is now `CELLAR_SHT` (SHT4x or SHT3x). `/api/status` → `cellar.sensor` is `SHT4x`, `SHT3x`, or `SHT` while no sensor has been found yet.
- The heater pulse in nearly condensing air also works with the SHT3x (its heater is switched on for about 10 s, then switched off by the next sample).
- **I²C bus recovery:** a sensor interrupted in the middle of a read (the ESP32 reset while the sensor stays powered) can hold SDA low and block the bus. Before looking for the sensor, the firmware now clocks it free and sends a STOP. A measurement result left over from before a reset is read out before detection, so an SHT3x is no longer mistaken for an SHT4x. After a failed read the sensor is detected again.
- **New serial command `i2c`:** shows whether SDA and SCL have a pull-up and which I²C addresses answer, also with the two wires swapped.

### Room sensor outlier filter

- **Single glitched readings no longer distort the daily min/max:** the room temperature and humidity shown on the web page, in the 24 h chart, in `/api/status` and counted in the daily summary (`CELLAR.CSV`, `/api/cellar`) are now the **median of the last 3 readings**, separately for temperature and humidity. For example, one 10 s reading of 19.6 °C and another of 17.0 °C in a room steady at 18.0–18.2 °C would otherwise become that day's max and min; with the filter both are dropped. A real change passes 10 s later.
- After a start or a sensor outage the first two readings only fill the filter (shown, not counted in the day).
- **The daily CSV files keep the raw readings** in `cellar_C` / `cellar_RH`, so outliers stay visible and the filtering can be redone from the data. The Serial Monitor prints both the filtered and the raw value. The CSV format is unchanged. Days closed with firmware 2.3 keep their unfiltered values.

### Web page

- **Chart tooltips:** hovering over a chart (or tapping it on a phone) shows a vertical line and the time and values of the nearest point – in the 24 h temperature chart, the 24 h room chart and both daily trend charts (there with the daily average, min, max and number of readings). Handy for reading when a run started.

## 2.3 – 2026-10-15

### Early start detection

- **Start detected from the temperature rise:** in the cold phase the pipe temperature is compared with the lowest reading of the last 5 minutes; two readings in a row at least 2 °C above it mark a rise (web: *Starting (pipe rising)*, API `estimate`: `RISING`), typically 20–30 s after the compressor starts.
- The start is still counted only after the hot threshold confirms it, but it is **dated to the beginning of the rise**. On the tested Gree Versati III this moves the start time about 2 minutes earlier, so run and cold phase lengths are more accurate.
- A rise that falls back or does not reach the hot threshold within 20 minutes is discarded. Limits: `RISE_START_C`, `RISE_WINDOW_SAMPLES`, `RISE_CONFIRM_SAMPLES`, `RISE_VERIFY_MS`. The CSV format is unchanged.

### Room temperature and humidity (SHT40 / AM2301)

- **New optional sensor,** chosen with `CELLAR_SENSOR`:
  - `CELLAR_SHT4X` (default): Sensirion SHT40/41/45 on I²C, SDA = GPIO33, SCL = GPIO22, address 0x44 or 0x45 found automatically, e.g. the LaskaKit SHT40 module LA131037. The measurement is started with the other sensors and read 800 ms later, so the loop never waits. In nearly condensing air (≥ 90 % RH) the sensor gets a 1 s heater pulse at most once an hour; the following minute is not measured.
  - `CELLAR_AM2301`: AOSONG AM2301 (DHT21) on GPIO33 with a 4.7 kΩ pull-up, read by the firmware itself. Not Dallas 1-Wire, so it does not share the DS18B20 bus. Only up to about 80 % RH.
  - `CELLAR_NONE`: no sensor.
  No new library is needed (the SHT4x uses the core's `Wire`).
- **Web page:** new section *Room around the heat pump* with temperature, relative humidity, dew point and absolute humidity, a 24 h chart and a **daily trend** (daily min–max band and average line for temperature, relative and absolute humidity; 30 days / 90 days / 1 year). Meant for comparing, e.g., a cellar before and after insulation.
- **Daily summary `/CELLAR.CSV`:** one row per day with min/avg/max temperature and relative humidity, average absolute humidity and the number of readings. The day in progress is saved to NVS every 10 minutes, so a restart does not cut the day short; a day that was in progress while the logger was off is closed at the next start. The file is loaded at boot for the web chart (up to 400 days) and is never deleted by the circular archive.
- **Daily CSV files:** three new columns at the end of each row: `cellar_C`, `cellar_RH`, `cellar_valid`. On the day of the update, the existing file keeps its old header.
- **API:** `/api/status` has a new `cellar` object (`sensor`, `t`, `rh`, `dew`, `ah`, `errors`, `last_error`; `null` when disabled), `/api/history` points have two more values `[unix_s, gas_C, water_C, cellar_C, cellar_RH]`, new endpoints `GET /api/cellar` (daily summaries) and `GET /cellar.csv`.
- A single failed reading is bridged by the previous value (and not counted in the daily summary); after three failures in a row the value is reported as an error. Failed readings and their cause are shown on the web page and the Serial Monitor.

## 2.2 – 2026-10-14

### Data storage

- **RAM buffer, write every 10 minutes:** measuring stays at 10 s and the web page updates immediately, but rows are collected in a RAM buffer (1080 rows = 3 h) and written to microSD every 10 minutes by a separate FreeRTOS task. Each row keeps its original time stamp and goes into the daily file of the day it was measured (midnight, DST). Phase events (`EVENTS.CSV`) use the same mechanism. 60× fewer file operations – longer card life.
- **CSV format unchanged:** file names, columns, header, number formatting and CRLF line endings are byte-for-byte the same as in 2.1.
- **Verified writes:** the result of open, write (byte count), flush, `fsync` and close is checked, and the resulting file size is verified. Rows are removed from RAM only after a verified write.
- **Retry without duplicates:** a failed write keeps the rows in RAM, remounts the card and retries every 60 s; a partial earlier write is first cut off (`truncate`), so no row is written twice. A file that does not end with a line break gets one before new rows.
- **Card removal:** detected within 60 s by a read-only check; measuring and the web page keep running, the card is remounted automatically and the waiting rows are written. When the RAM buffer overflows (more than 3 h without a card), the oldest rows are dropped and reported as an error.
- **Circular archive:** below 10 % free space the oldest daily files older than 365 days are deleted until 15 % is free (`SD_MIN_FREE_PERCENT`, `SD_TARGET_FREE_PERCENT`, `SD_RETENTION_DAYS`). Newer files are never deleted automatically – writing is paused with an *insufficient space* error instead. Only `/LOGS/YYYYMMDD.CSV` files are ever deleted, their age is taken from the file name, and only when the clock has been confirmed by NTP. Every deletion and the space actually freed are logged on the Serial Monitor.
- **Storage section on the web page:** capacity, used/free space, usage, state (OK / WARNING / CLEANUP / ERROR), archive state, number of daily files, oldest/newest day, last successful write, records waiting in RAM, button to write the buffer now. `/api/status` has a new `storage` object and `heap_free`/`heap_min`; new endpoint `POST /api/flush`.
- **Serial diagnostics and test commands:** `status`, `flush`, `failwrite <n>`, `simfree <percent|off>`, `settime <unix_s>`.

### Other

- A clock set by NTP can no longer be overridden by browser time during a Wi-Fi outage.
- CSV downloads and the file list share a mutex with the writer task (no remount or deletion during a download); if the card is busy, the server answers 503 instead of blocking measuring.
- New documents: [data storage](docs/storage.md) and [testing](docs/testing.md) (also in Czech).

## 2.1 – 2026-10-12

### Fixes

- **Build:** the firmware did not compile in the Arduino IDE at all (`'Stats' does not name a type`) – the automatically generated function prototypes were inserted before the struct definition.
- **microSD:** the card is remounted after being removed and reinserted (previously `SD.begin()` reported success without actually mounting, and logging never resumed).
- **Phase estimate:** a single invalid MAX6675 reading (e.g. below 0 °C during defrosting) no longer discards the cycle in progress; the estimate is reset only after 5 minutes without valid data.
- **Statistics:** a phase that was already in progress at ESP32 boot or after a threshold change is no longer included in the averages with a wrong (truncated) length. The average cold phase only includes phases of known length.
- **Long uptime:** start and cycle timestamps are 64-bit – after 49.7 days of uptime (`millis()` overflow) old entries are no longer counted as recent. The same applies to `uptime_ms` in the CSV and `uptime_s` in the API.
- **Wi-Fi:** if the ESP32 fails to connect at boot (e.g. the router boots more slowly after a power cut), it retries the home network every 5 minutes instead of staying in access point mode forever.
- **Time:** a browser with an inaccurate clock no longer overrides the NTP-synchronised time.
- **MAX6675:** a missing or disconnected module is reported as an error instead of a false 0 °C (pull-up on SO, D15 bit check).
- **DS18B20:** a sensor connected after boot is detected; a reading of 85.00 °C (the sensor's power-on default) is treated as an error.

### Other

- Project renamed from GreeLogger to **HeatPulse** (independent of the heat pump brand); the fallback access point is called `HeatPulse`.
- Threshold settings are stored in a new namespace `heatpulse` – after flashing version 2.1 the thresholds return to their defaults (45/35/28 °C) and need to be entered again.
- Repository restructured for public use: firmware in `firmware/HeatPulse/`, documentation in `docs/` (English) and `docs/cs/` (Czech).
- Wi-Fi credentials moved to `secrets.h` (kept out of git), template in `secrets.example.h`.
- Added wiring for the LaskaKit LA131090 probe and the required 4.7 kΩ resistor.
- Added a detailed explanation of the phase thresholds and how to tune them.
- **Run length rating:** each estimated run is rated poor (< 6 min), acceptable (6–10 min), good (10–20 min) or ideal (≥ 20 min); the web page shows the rating of the last and average run and the counts per class for the last 24 h. In the API, `short_10m` was replaced by `runs_24h` (counts per class).
- Sensor mounting: step-by-step guide to checking and electrically isolating a grounded-junction thermocouple without losing heat transfer; corrected the explanation of MAX6675 T− grounding.
- New document on heat pump cycling and flow temperature (start-up losses, run length, efficiency, A/B testing) with sources; Gree Versati III notes on the weather compensation curve.
- Firmware fully in English: web interface, serial output, server messages and code comments.
- **API change:** `estimate` in `/api/status` now returns the codes `HOT`, `COLD`, `UNKNOWN`, `SENSOR_ERROR` (the same as the CSV) instead of Czech text labels.
- The chart shows the time of the first and last point; the CSV list is sorted newest first; firmware version shown in the API and on the page.
- Automatic firmware build with GitHub Actions.

## 2.0

- First version: MAX6675 + DS18B20 + microSD, web page, thermal phase estimate, daily CSV files.
