# Usage: web interface, thresholds and data

## Web interface

Enter the ESP32's IP address in a browser to see:

- the current refrigerant pipe temperature (type K thermocouple),
- the current approximate heating water outlet temperature (DS18B20),
- the difference "water − target", where the target is a **manually entered** value (nothing is read from the heat pump),
- a chart of both temperatures (1 point/min, up to 1440 points = 24 h since the last ESP32 restart),
- the **room** (e.g. cellar) section with the SHT40 or AM2301 sensor: temperature, relative humidity, dew point, absolute humidity, a 24 h chart and the **daily trend** over up to a year (see [room temperature and humidity](#room-temperature-and-humidity)),
- the **estimated** number of starts in the last 1 h and 24 h, the average length of hot and cold phases and the length of the last hot phase, each run rated as poor / acceptable / good / ideal,
- the **run length rating** for the last 24 h: how many runs were under 6 min, 6–10 min, 10–20 min and 20 min or longer (see [heat pump cycling](heat-pump-cycling.md#how-long-should-a-run-be) for where these limits come from),
- the **Storage** section: card capacity, used and free space, state of the circular archive, number of daily files, oldest and newest day, time of the last successful write and the number of records still waiting in RAM (see [data storage](storage.md)),
- a list of daily CSV files to download, the event file `EVENTS.CSV` and the room daily summary `CELLAR.CSV`,
- the threshold and target water temperature settings (stored in the ESP32 flash memory).

**Reading a point in a chart:** move the mouse over any chart (on a phone, tap it) to see the time and the values of the nearest point – for example, when a run started (the pipe temperature starts to climb) or when the room was aired. In the daily trend, the tooltip shows the day's average, minimum, maximum and number of readings. On a touch screen the tooltip stays until you tap elsewhere.

## Thermal phase estimate

While the compressor runs, hot compressed gas flows through the refrigerant gas pipe and the pipe heats up. When the compressor stops, the pipe cools down towards the ambient temperature. From this, the firmware estimates whether the compressor is running (the "hot phase") or stopped (the "cold phase").

Three values are set on the web page:

| Setting | Default | Meaning |
|---|---|---|
| **Hot phase from** (`on`) | 45 °C | When the pipe temperature stays **at or above this value for at least 30 s**, a hot phase begins (estimate: compressor running) and a start is counted. |
| **Cold phase up to** (`off`) | 35 °C | When the pipe temperature stays **at or below this value for at least 60 s**, a cold phase begins (estimate: compressor stopped) and the length of the completed hot phase is recorded. |
| **Target water temperature** (`target`) | 28 °C | Only a **reference value** – copy the desired outlet water temperature from your heat pump controller here. It is used only for the **Water − target** tile and the `target_water_C` CSV column. It **does not affect** the phase estimate or the heat pump. With weather compensation the real target changes with the outdoor temperature – update the value occasionally. |

The band between the two thresholds (35–45 °C by default) is a **hysteresis**: while the temperature is inside it, the state does not change. This keeps the estimate from flickering when the temperature hovers around a single value. The hot threshold must therefore be at least 2 °C above the cold threshold. The 30 s and 60 s confirmation times filter out short spikes; they are fixed in the code (`HOT_CONFIRM_MS`, `COLD_CONFIRM_MS`).

```
 pipe temperature
   55 °C |        ______________
         |       /              \
   45 °C |------/----------------\------------------  hot threshold (on)
         |     /                  \
   35 °C |----/--------------------\----------------  cold threshold (off)
         |   /                      \_____________
   25 °C |__/
         +----------------------------------------> time
              ^ start (≥ 45 °C for 30 s)  ^ end (≤ 35 °C for 60 s)
              |<------ hot phase -------->|<- cold phase ...
```

### Early start detection

The gas pipe heats up slowly: on the tested Gree Versati III it takes about 2 minutes from the compressor start to a hot threshold of 27 °C. To date the start accurately, the firmware also watches **how fast the pipe temperature rises**:

- In the cold phase, every reading is compared with the **lowest reading of the last 5 minutes**. When two readings in a row are at least **2 °C** above it, a rise is detected. The web page then shows **Starting (pipe rising)** and the API `estimate` is `RISING` – typically 20–30 s after the compressor starts.
- The start is **counted only when the hot threshold confirms it** (as before: at or above the hot threshold for 30 s), but it is **dated to the beginning of the rise** – the last reading before the pipe started to climb. Run lengths, cold phase lengths and starts per hour therefore no longer include the minutes the pipe needs to warm up.
- A rise that falls back to the baseline, or that does not reach the hot threshold within 20 minutes, is discarded and not counted. This covers, for example, a pipe warmed only by the circulating water while the compressor is off.
- Without a detected rise (e.g. a very slow warm-up), the start is dated to the threshold crossing as before.
- The CSV column `thermal_estimate` stays `COLD` until the start is confirmed; the `THERMAL_HOT` event is written at confirmation, its `duration_s` (previous cold phase) is computed from the dated start.

The detection limits are constants at the top of [`HeatPulse.ino`](../firmware/HeatPulse/HeatPulse.ino): `RISE_START_C` (2 °C), `RISE_WINDOW_SAMPLES` (30 readings = 5 min), `RISE_CONFIRM_SAMPLES` (2) and `RISE_VERIFY_MS` (20 min).

> [!TIP]
> If the circulation pump also runs while the compressor is off and the water is warmer than the hot threshold, the pipe can be warmed by the water alone and a false start can be confirmed. Keep the hot threshold above the highest water temperature in that case.

### How to tune the thresholds

The defaults are only a starting guess. The actual gas pipe temperature depends on the heat pump, the measuring point, the insulation and above all the water temperature: at a low outlet temperature (e.g. 28 °C) and low compressor output the pipe may only reach about 40 °C while running, and a 45 °C threshold would miss the start entirely.

1. Let the logger run for at least several cycles and watch the chart together with the compressor status on the heat pump controller (on Gree: `Compressor`).
2. Find out **what temperature the pipe holds while the compressor runs** (the lowest steady value) and **how low it drops when it stops**.
3. Set the hot threshold a few degrees **below** the running temperature and the cold threshold a few degrees **above** the idle temperature. Leave at least 5 °C between them.

   *Example:* 48–55 °C while running, drops to 24 °C when stopped → hot phase from 42 °C, cold phase up to 32 °C.
4. Check that the estimated number of starts matches reality.

With early start detection the hot threshold only **confirms** a start, so it can be set safely below the running temperature without losing accuracy of the start time.

After saving the thresholds the estimate starts over (the current phase is not counted); the counters since ESP32 boot are kept. The thresholds and the target are stored in the ESP32 flash memory and survive a restart.

A phase that was already in progress when the ESP32 booted (or when the thresholds were changed) has no known beginning, so it is not counted as a start and not included in the averages.

> [!IMPORTANT]
> This is an **estimate from temperature**, not the actual compressor state or frequency. Defrosting and domestic hot water heating can cause false transitions. The start count is not a service-validated figure.

## Room temperature and humidity

With an optional room sensor – SHT40 or AM2301 ([wiring](wiring.md#room-temperature-and-humidity-optional)) HeatPulse also logs the room where the heat pump stands – useful, for example, to see what insulating a cellar really changed.

| Tile | Meaning |
|---|---|
| **Temperature**, **Relative humidity** | current values; below them the minimum–maximum of the 24 h chart |
| **Dew point** | surfaces colder than this get wet (condensation on walls, cold water pipes) |
| **Absolute humidity** | grams of water per m³ of air. Unlike relative humidity it does not change with temperature |

The **daily trend** shows a band for each day (daily minimum–maximum) and a line for the daily average – for temperature, relative humidity and absolute humidity; the range can be switched between 30 days, 90 days and 1 year. Days with a gap in between (logger switched off) are not joined by a line.

> [!TIP]
> **How to read the effect of insulating a cellar:** compare the daily trend before and after. When the room gets warmer, its **relative** humidity drops even if the amount of water in the air stays the same. Only a lower **absolute** humidity means the room is really drier (less moisture coming in, or better ventilation). A dew point well below the temperature of the walls means no condensation. Outdoor weather also influences both values – compare periods with similar weather.

How the daily summary is made:

- Every valid 10 s reading is added to today's minimum, maximum and average. The day in progress is saved to the ESP32 flash (NVS) every 10 minutes, so a restart during the day continues the day instead of starting from zero.
- At midnight the day is closed and written as one row to `/CELLAR.CSV`. If the logger was off at midnight, the day is closed when it starts again. The `samples` column shows how complete the day is: 8640 = all readings of a full day.
- Readings without a synchronised clock are not included in any day.
- The web page shows up to 400 days; `CELLAR.CSV` itself keeps everything and is never deleted by the [circular archive](storage.md#circular-archive).
- The sensor is read every 10 s. A single failed reading (e.g. interference on the cable) is bridged by the previous value; after three failures in a row the value is reported as an error and the CSV columns stay empty. Only new readings are counted in the daily summary, not bridged ones.
- **Outlier filter:** a sensor sometimes returns a single reading that is clearly off – e.g. one 10 s value of 19.6 °C in a room that stays at 18.0–18.2 °C. One such reading would become the day's minimum or maximum. The web page, the 24 h chart and the daily summary (`CELLAR.CSV`, `/api/cellar`) therefore use the **median of the last 3 readings**, separately for temperature and humidity: a single outlier is dropped, while a real change (airing the room, the heat pump warming it) passes through only 10 s later. Two outliers in a row would still pass. After a start or a sensor outage the first two readings only fill the filter: they are shown but not counted in the day.
- **The daily CSV files keep the raw readings** (`cellar_C`, `cellar_RH`) – including any outliers, so you can see them and redo or change the filtering yourself. The current value in `/api/status` is filtered; the Serial Monitor prints both (`cellar: 18.1 °C … (raw 19.6 °C …)`). Days closed before the filter was added (firmware 2.3) keep their unfiltered min/max; if needed, recompute them from the daily files.
- **SHT sensor in nearly condensing air:** when the humidity is 90 % RH or more, the firmware gives the sensor a short heater pulse at most once an hour (SHT4x: 110 mW for 1 s; SHT3x: its heater is switched on for about 10 s). It dries the sensing element and limits the slow upward drift that every humidity sensor shows in very humid air. For one minute after the pulse, while the sensor cools down, no reading is taken: the web page and the CSV keep the previous value and these rows are not counted in the daily summary. The Serial Monitor prints `SHT4x: … % RH – heater pulse` (or `SHT3x: …`). Limits: `SHT_HEAT_ABOVE_RH`, `SHT_HEAT_INTERVAL_MS`, `SHT_HEAT_SETTLE_MS`.

The sensor type is chosen, or the sensor disabled, with `CELLAR_SENSOR` (`CELLAR_SHT`, `CELLAR_AM2301`, `CELLAR_NONE`) at the top of [`HeatPulse.ino`](../firmware/HeatPulse/HeatPulse.ino); the web page then hides the section.

## Data on the microSD card (CSV)

Every **10 s** a row is created for `/LOGS/YYYYMMDD.CSV`, so a new file is created each day. Until the clock is synchronised, rows go to `NOSYNC.CSV` with the time since ESP32 boot only. `/EVENTS.CSV` contains the estimated transitions between phases and `/CELLAR.CSV` the daily summaries of the room sensor.

The rows are collected in RAM and written to the card **every 10 minutes**, each with its original time stamp and into the daily file of the day it was measured. The file on the card can therefore be up to 10 minutes behind the web page; the **Write buffer to microSD now** button writes immediately. Details, the circular archive and the limits are described in [data storage](storage.md).

The CSV files use a **semicolon** as separator and a **decimal point**. When importing into a spreadsheet with a different locale, set the decimal separator to a point. The column names are deliberately plain ASCII so the files are easy to process with scripts.

### Daily files `/LOGS/YYYYMMDD.CSV`

| Column | Meaning |
|---|---|
| `local_time` | local time in ISO 8601 with time zone; empty before the clock is synchronised |
| `unix_s` | Unix time; empty before the clock is synchronised |
| `uptime_ms` | milliseconds since the last ESP32 boot (restarts from zero after a reboot) |
| `gas_C` | refrigerant pipe temperature; empty on a MAX6675 error |
| `water_C` | surface temperature of the heating water outlet pipe; empty on a DS18B20 error |
| `gas_valid` / `water_valid` | `1` = valid reading, `0` = sensor error |
| `thermal_estimate` | phase estimate: `HOT`, `COLD`, `UNKNOWN` – **not the actual compressor state** |
| `target_water_C` | manually set target water temperature, not read from the heat pump |
| `cellar_C` | room temperature from the room sensor (°C, 0.1 resolution), **raw** – not passed through the [outlier filter](#room-temperature-and-humidity); empty on a sensor error or when the sensor is disabled |
| `cellar_RH` | room relative humidity (%), raw; empty as above |
| `cellar_valid` | `1` = valid room sensor reading, `0` = sensor error or disabled |

> [!NOTE]
> The three `cellar_*` columns were added in firmware 2.3. On the day of the update, the daily file keeps its old 9-column header and only rows written after the update have 12 columns; from the next day the files have the new header.

### Events `/EVENTS.CSV`

| Column | Meaning |
|---|---|
| `local_time`, `unix_s`, `uptime_ms` | time of the event (as above) |
| `event` | `THERMAL_HOT` = start of a hot phase, `THERMAL_COLD` = start of a cold phase |
| `gas_C`, `water_C` | temperatures when the transition was confirmed |
| `duration_s` | length of the **previous** phase in seconds; empty if its beginning is unknown |

### Room daily summary `/CELLAR.CSV`

One row per day, written after midnight.

| Column | Meaning |
|---|---|
| `date` | local date `YYYY-MM-DD` |
| `t_min_C`, `t_avg_C`, `t_max_C` | minimum, average and maximum room temperature (after the outlier filter) |
| `rh_min_pct`, `rh_avg_pct`, `rh_max_pct` | minimum, average and maximum relative humidity (after the outlier filter) |
| `ah_avg_gm3` | average absolute humidity in g/m³ |
| `samples` | number of valid 10 s readings that day (8640 = complete day) |

### Behaviour on failures

- **Wi-Fi outage:** measuring and writing to the card continue.
- **Power cut / restart:** rows not yet written are lost – **at most about the last 10 minutes**. The chart and counters in RAM are cleared; the files on the card are kept.
- **microSD missing or failing:** measuring and the web interface keep running and the rows wait in RAM (up to 3 hours). The firmware tries to remount the card every 60 s and then writes the waiting rows into the correct daily files, without duplicates. After 3 hours the oldest unsaved rows are dropped and reported as an error.
- **Card full:** daily files older than 365 days are deleted oldest first; nothing newer is deleted automatically – instead writing is paused and the web page shows *insufficient space*. See [circular archive](storage.md#circular-archive).
- Downloading large CSV files may briefly delay measurements, because the ESP32 handles both the web server and measuring in a single loop.

## HTTP API

The web page uses a simple API that you can also call from your own scripts (e.g. Home Assistant, Python):

| Method | Path | Description |
|---|---|---|
| GET | `/api/status` | current temperatures, phase estimate, statistics and settings (JSON) |
| GET | `/api/history` | chart points `[unix_s, gas_C, water_C, cellar_C, cellar_RH]` (JSON); `unix_s` is 0 before the clock is synchronised |
| GET | `/api/cellar` | room daily summaries `{"days": [[date, t_min, t_avg, t_max, rh_min, rh_avg, rh_max, ah_avg, samples], …], "today": […] or null}` (JSON); `today` is the day in progress |
| GET | `/api/files` | list of daily CSV files (JSON) |
| GET | `/download?file=YYYYMMDD.CSV` | download a daily CSV file |
| GET | `/events.csv` | download the event file |
| GET | `/cellar.csv` | download the room daily summary |
| POST | `/api/config` | save thresholds: `on`, `off`, `target` (form-urlencoded) |
| POST | `/api/time` | set the clock: `epoch` (Unix time); ignored once NTP has set the time |
| POST | `/api/flush` | write the RAM buffer to the card now |

Example `/api/status` response:

```json
{
  "version": "2.3",
  "gas": 52.25, "water": 29.31, "delta": 1.31,
  "estimate": "HOT",
  "sd": true, "time": true, "uptime_s": 86400,
  "starts_1h": 1, "starts_24h": 14, "runs_24h": [2, 3, 7, 2],
  "avg_hot_s": 1520, "avg_cold_s": 2380, "last_hot_s": 1610,
  "on": 45.0, "off": 35.0, "target": 28.0,
  "heap_free": 182340, "heap_min": 151208,
  "storage": {
    "state": "OK", "reason": "", "card": true,
    "total_b": 7948206080, "used_b": 512000000,
    "files": 143, "nosync": true, "oldest": "2026-05-24", "newest": "2026-10-14",
    "last_write": 1792000000, "last_write_ago_s": 240, "next_write_s": 360,
    "buffered": 24, "buffered_events": 1, "buffer_cap": 1080,
    "dropped": 0, "write_errors": 0, "deleted": 0,
    "archive": "READY", "ntp": true,
    "min_free_pct": 10, "target_free_pct": 15, "retention_days": 365
  },
  "cellar": {
    "sensor": "SHT4x", "t": 14.8, "rh": 63.2, "dew": 7.9, "ah": 8.0,
    "errors": 2, "last_error": "CHECKSUM"
  }
}
```

`runs_24h` holds the number of completed runs in the last 24 h by length: `[under 6 min, 6–10 min, 10–20 min, 20 min and more]`. Temperatures are `null` when the sensor reading is invalid; the averages and `last_hot_s` are `null` until a completed phase is available. `estimate` is one of `HOT`, `COLD`, `RISING` (cold phase, the pipe is rising – a start is expected and waits for confirmation by the hot threshold), `UNKNOWN` (phase not determined yet) or `SENSOR_ERROR` (no valid thermocouple reading).

`storage.state` is `OK`, `WARNING` (less than 15 % free), `CLEANUP` (the archive is deleting files) or `ERROR`; `storage.reason` gives the cause: `NO_CARD`, `WRITE_FAILED`, `NO_SPACE`, `BUFFER_OVERFLOW`, `CLEANUP`, `LOW_SPACE` or an empty string. `storage.archive` is `READY`, `NO_TIME` (deletion disabled until the clock is confirmed by NTP), `CLEANUP` or `NO_SPACE` (writing paused). `buffered` is the number of rows waiting in RAM, `dropped` the number of records lost since start because the RAM buffer was full, `last_write` the Unix time of the last successful write (`null` if unknown) and `next_write_s` the seconds until the next regular write (`null` when no write is planned). `oldest`/`newest` are taken from the daily file names and are `null` when there is no daily file. The older field `sd` stays for compatibility (`true` = card mounted and responding).

`cellar` is `null` when the room sensor is disabled (`CELLAR_SENSOR = CELLAR_NONE`). `sensor` is `SHT4x`, `SHT3x`, `SHT` (I²C sensor not found yet) or `AM2301`. `t` (°C), `rh` (%), `dew` (dew point, °C) and `ah` (absolute humidity, g/m³) are `null` when there is no valid reading. `errors` counts failed readings since start, `last_error` is the cause of the last one: `NO_RESPONSE` (no answer – wiring, power, pull-up; for an SHT sensor also swapped SDA/SCL), `TIMEOUT` (frame cut off or no data), `CHECKSUM` (corrupted frame – interference, long cable) or `RANGE` (value out of the sensor range).
