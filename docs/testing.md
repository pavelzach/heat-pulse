# Testing the data storage

Test scenarios for the RAM buffer, writing to microSD and the circular archive (firmware 2.2), plus the room sensor (firmware 2.3, [scenario 7](#7-room-sensor-sht40-or-am2301-firmware-23)). They need only the logger itself, a PC with a card reader and the Arduino IDE Serial Monitor (115200 baud, line ending *Newline*). The Serial commands are described in [data storage](storage.md#serial-monitor-diagnostics).

> [!TIP]
> To shorten the tests you can temporarily set `SD_FLUSH_MS = 60000` (1 min) at the top of `HeatPulse.ino`. Do not forget to set it back to `600000`.

> [!CAUTION]
> Test the archive on a **spare card**. The archive and the `simfree` command really delete files older than 365 days.

## 1. Normal operation

1. Insert an empty FAT32 card, flash the firmware, open the Serial Monitor.
2. Expected at boot: `microSD: OK`, `Storage: write every 10 min, RAM buffer 1080 rows …`.
3. Every 10 s: `Gas: … | SD: OK | buffer: N | …` – `N` grows by one with each measurement.
4. Open the web page: values and chart update immediately; **Storage** shows `OK`, capacity, *Unsaved records in RAM* growing and *next write in about … min*.
5. After 10 minutes: `SD: saved 60 rows + … events …`; *buffer* drops to 0–1, *Last successful write* shows the time.
6. Download today's CSV and check: header identical to 2.1, rows every 10 s **without gaps and without duplicates** (`uptime_ms` increases by about 10 000), time stamps match the measurement times (not the write time).
7. Press **Write buffer to microSD now** – the Serial Monitor confirms the write and the downloaded file contains the latest rows.

**Pass:** no gaps, no duplicates, format unchanged, measuring continues during writes.

## 2. Free space at the 10 % boundary

Preparation (on a PC, on a spare card that the logger has already formatted with `/LOGS`):

1. Copy a few dummy daily files older than 365 days into `/LOGS`, e.g. `20200101.CSV` … `20200110.CSV` (any content, e.g. copies of a real CSV), and one younger file, e.g. a copy named with yesterday's date.
2. Fill the card with one large dummy file **in the root** (not in `/LOGS`) so that about 9 % remains free. In an administrator PowerShell for a card with drive letter E: `fsutil file createnew E:\filler.bin <bytes>` (compute the size from *Properties* of the drive; FAT32 allows at most 4 GB per file, use several files if needed).

Test:

1. Insert the card, let the logger connect to Wi-Fi with internet (**NTP is required for deletion**). Serial: `Time synchronised by NTP – archive deletion enabled`.
2. Type `flush` (or wait for the write). Expected:
   - `SD: free space 9.x % < 10 % – archive cleanup (… deletable file(s) found)`,
   - one `ARCHIVE: deleted /sd/LOGS/2020…CSV (… B, older than 365 days), freed … B, free now … %` line per file, oldest first,
   - deletion stops as soon as 15 % is free, or when there is nothing more older than 365 days.
3. Because the dummy files are small, the space will not reach 10 %: expected `SD ERROR: insufficient space … writing PAUSED`, web: **ERROR**, *insufficient space*, archive *insufficient space*.
4. Check on the PC: only `2020….CSV` files were deleted; `filler.bin`, `NOSYNC.CSV`, `EVENTS.CSV`, the younger daily file and today's file are untouched.
5. Delete `filler.bin` on the PC (or a part of it) and put the card back. Within about a minute: `SD: enough free space again – writing resumed` and the rows waiting in RAM are written.

Variant without filling the card: type `simfree 8` → the archive deletes all files older than 365 days (the simulated value does not change) and then pauses writing; `simfree 12` → state **WARNING**, writing continues; `simfree off` → real value.

Variant without NTP: start the logger in access point mode (no home Wi-Fi), with `simfree 8`. Expected: `clock not confirmed by NTP – file age unknown, nothing is deleted`, archive *waiting for NTP time* / *insufficient space*, **no file is deleted**.

**Pass:** only files in `/LOGS` named `YYYYMMDD.CSV` and older than 365 days are deleted, oldest first; nothing younger is ever deleted; writing pauses and resumes automatically.

## 3. Failed write

### 3a. Simulated failure (rollback and no duplicates)

1. Let the logger run for a few minutes, then type `failwrite 1` and `flush`.
2. Expected: `SD ERROR: write (SIMULATED by test command) failed …`, `SD: write failed – N records kept in RAM, retry in 60 s`; web: **ERROR – last write failed**. The rows were in fact written to the file.
3. After about 60 s: `SD remount: OK`, `SD: removed an incomplete earlier write from … (back to … bytes) – no duplicate rows`, then `SD: saved …`.
4. Download the CSV: every `uptime_ms` value appears **exactly once**, no gap.

### 3b. Card removed during operation

1. Remove the card. Within 60 s: `SD ERROR: card does not respond (removed?)`; web: **ERROR – card missing**; `sd: false` in `/api/status`.
2. Measuring, chart and statistics keep running; *Unsaved records in RAM* grows.
3. Wait at least 15 minutes (over one write interval) and put the card back. Within 60 s: `SD remount: OK` and the rows waiting in RAM are written immediately.
4. Download the CSV: the rows from the outage are present with their original times, without gaps and duplicates.

### 3c. Buffer overflow (optional, takes over 3 hours)

Leave the card out for more than 3 hours. Expected: `ERROR: RAM buffer full – oldest unsaved rows lost (1 in total)…` (then every 60th), web: **ERROR – RAM buffer was full**, *Records lost since start* > 0. After reinserting the card the newest 3 hours are written; the state returns to **OK** after a successful write, the lost-record count stays visible until restart.

## 4. Midnight

1. On a logger **without internet** (access point mode) or before NTP sets the time, type `settime <unix time of today 23:56 local>`; online converters or PowerShell `[DateTimeOffset]::new((Get-Date).Date.AddHours(23).AddMinutes(56)).ToUnixTimeSeconds()` give the value. (On a logger with internet NTP may correct the clock again within an hour – that is fine for this test.)
   Do not open or reload the web page during this test in access point mode – the page sends the browser's time to the logger and would replace the test time.
2. Wait until after 00:06 (or type `flush` after midnight).
3. Expected on the card: rows up to 23:59:5x in the file of the **old day**, rows from 00:00:0x in the new file `YYYYMMDD.CSV` with the header; nothing missing, nothing duplicated; the web shows the new day as the *newest day*.
4. Alternatively, verify the boundary on a real night: download yesterday's and today's files in the morning and check the last and first rows.

## 5. Recovery after a power cut and restart

1. Note the time, wait about 5 minutes after the last write and unplug the power.
2. Plug it back in. Expected at boot: `microSD: OK`, measuring starts, a new write after 10 minutes.
3. Download the CSV: rows up to the last write before the power cut are complete; the gap covers at most about 10 minutes before the cut plus the boot time. The last row before the gap is complete (not cut in half).
4. Variant: press **Write buffer to microSD now** before unplugging – then nothing is lost.

## 6. Long-term operation

Run the logger for several days and check occasionally with `status`:

- `Heap: free …, minimum …, largest block …` – the values must not keep falling,
- `writer task stack left …` – should stay above about 2 000 B,
- `write_errors` and `dropped` in the storage JSON should stay at 0 with a working card.

## 7. Room sensor (SHT40 or AM2301, firmware 2.3)

1. **Readings:** with an SHT sensor, the boot output contains `SHT4x found at I2C address 0x44` (or `SHT3x`, or 0x45). At boot and then every 10 s the Serial Monitor prints `cellar: …°C, … % RH (failed readings since boot: n)`. Breathe on the sensor – the humidity rises within a few readings and returns within minutes.
2. **Disconnect the data wire** for about a minute: the first two readings keep the previous value, from the third one the web shows *Sensor not responding (NO_RESPONSE)*, the CSV columns `cellar_C`/`cellar_RH` are empty and `cellar_valid` is `0`. After reconnecting, values return with the next reading.
3. **Day in progress survives a restart:** note *today so far* in the daily trend (or `today` in `/api/cellar`, including `samples`), restart the ESP32 and after the clock is set check that `samples` continues from the last value saved (at most 60 readings = 10 minutes lower), not from zero.
4. **Closing a day:** as in [midnight](#4-midnight), set the clock to 23:56 with `settime`. After midnight the Serial Monitor prints `Cellar: day YYYYMMDD closed – …`; after the next write (or `flush`) `/CELLAR.CSV` has a new row with that date, and after a restart the day is still in the daily trend (loaded from the card: `Cellar: N daily summaries loaded from CELLAR.CSV`). When the clock later returns to the real time, the test date can appear in `CELLAR.CSV` a second time (the web shows the later row) – delete the test rows on a PC if you want a clean file.
5. **Logger off over midnight:** unplug it before midnight and plug it in the next day. After the clock is set, the previous day is closed (with fewer `samples`) and written once – the date must not appear twice in `CELLAR.CSV`.
