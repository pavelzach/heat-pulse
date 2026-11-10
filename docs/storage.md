# Data storage: RAM buffer, microSD and circular archive

Since firmware 2.2 the logger no longer writes to the microSD card after every measurement. This page explains how the data get from the sensors to the card, what happens when something goes wrong and which limits you should know about.

## Overview

```
 every 10 s                         every 10 min (sdWriter task)
 ┌─────────────┐   row    ┌──────────────────────┐  append + verify  ┌────────────────────┐
 │ measurement │ ───────► │ RAM buffer           │ ────────────────► │ /LOGS/YYYYMMDD.CSV │
 │ (loop)      │          │ 1080 rows = 3 h      │                   │ /EVENTS.CSV        │
 └─────┬───────┘          │ 128 phase events     │ ◄──────────────── └────────────────────┘
       │                  └──────────────────────┘  rows removed only
       ▼                                            after a verified write
 web page: values, chart and statistics update immediately
```

- **Measuring is unchanged:** every 10 s, MAX6675 + DS18B20. Current values, the chart and the cycle statistics are updated immediately, as before.
- Each measurement is stored as one record in a **RAM buffer** together with its original time stamp (Unix time and uptime), temperatures, validity flags, phase estimate and target temperature.
- Every **10 minutes** a separate FreeRTOS task (`sdWriter`) writes all buffered rows to the card in one append per daily file.
- Phase events (`THERMAL_HOT`, `THERMAL_COLD`) use the same mechanism with their own buffer and are written to `/EVENTS.CSV` in the same cycle.
- The CSV format is **byte-for-byte the same** as in 2.1: same file names, columns, header, number formatting and line endings (CRLF). Existing scripts and spreadsheets keep working. (Firmware 2.3 adds three room-sensor columns at the end of each row, see [usage](usage.md#daily-files-logsyyyymmddcsv).)

### Why

Writing every 10 s means 8 640 file open/append/close cycles per day, each of which rewrites the FAT and the directory entry. Writing every 10 minutes reduces this 60× (144 per day), which saves the card and makes logging more robust: a slow or temporarily unavailable card no longer affects measuring at all.

## Writing in detail

The writer task runs on the same CPU core and with the same priority as the measurement loop. FreeRTOS time-slices between them, so even a slow card operation (mounting, a directory scan, a busy card) never stops measuring or the web server.

For each daily file the task:

1. takes up to 64 consecutive rows **of the same calendar day** (each row's day is computed from its own time stamp – rows from 23:58 and 00:02 go to two different files),
2. formats them into a static text buffer,
3. remembers the current file size (for a possible rollback, see below),
4. opens the file, writes the header if the file is new, writes the rows,
5. checks that `fwrite` wrote all bytes, then `fflush`, `fsync` and `fclose` – each return value is checked,
6. verifies that the file now has **exactly** the old size + the number of bytes written,
7. only then removes these rows from the RAM buffer.

All card operations (writer task, CSV downloads, file list) are serialised with a mutex, so the card cannot be remounted or a file deleted while it is being downloaded.

### Failed write and retry without duplicates

If any step fails, the rows **stay in RAM** and are not marked as saved. The firmware logs the failing step on the Serial Monitor (with `errno`), marks the card as unavailable, remounts it after 60 s and retries the write.

A failed write may have left part of the rows in the file. Before the next write the firmware therefore **cuts the file back to the size it had before the failed attempt** (`truncate`), and only then appends the rows again. The same row therefore never appears twice and there is no half line in the middle of the file. As a safety measure the rollback is skipped if the file grew by more than the failed write could have written, or if a card with a different capacity is now inserted – then the bytes are not ours.

If a file does not end with a line break (e.g. after a power cut in the middle of a write by an older firmware), the next write first starts a new line, so the new rows are never glued to a broken one.

### Midnight, daylight saving time and the clock

- Each row carries the time when it was **measured**, not when it was written. File name and `local_time` are derived from that time, so rows end up in the correct daily file even if they are written minutes later and even across a DST change.
- Rows measured before the clock was synchronised go to `NOSYNC.CSV` with empty time columns – as before.
- Once NTP has set the clock, the ESP32 keeps the time running through an NTP or Wi-Fi outage. Browser time (used in access point mode) can no longer override a time that was set by NTP.

## Circular archive

The writer task checks the free space on the card before every write (every 10 minutes, every minute while there is a problem).

| Situation | What the firmware does |
|---|---|
| free space ≥ 15 % | nothing, state **OK** |
| 10 % ≤ free < 15 % | keeps writing, state **WARNING** |
| free < 10 % | **cleanup** (state **CLEANUP**): deletes the **oldest** daily files first, one at a time, until at least **15 %** is free – but only files **older than 365 days** |
| still < 10 % and no file older than 365 days | **nothing newer is deleted**; state **ERROR – insufficient space**, writing is **paused**, data wait in RAM |

Rules that are never broken:

- Only files in `/LOGS` whose name is exactly a valid date `YYYYMMDD.CSV` (e.g. `20251013.CSV`) can be deleted. `NOSYNC.CSV`, `/EVENTS.CSV`, `/CELLAR.CSV`, other files, folders and anything outside `/LOGS` are never touched.
- The age of a file is taken from its **name**, not from the FAT time stamp (which is wrong for files written before the clock was set).
- Nothing is deleted unless the clock has been **confirmed by NTP** since boot. With a manually set clock or browser time the archive only reports the low space and pauses writing. A wrong clock therefore cannot cause wrong deletions.
- Today's file, files that still have rows waiting in RAM and a file with an unfinished write are protected.
- After each deletion the free space is measured again; the Serial Monitor shows the file name, its size, the space actually freed and the new free percentage (`ARCHIVE: deleted …`).

Writing resumes automatically as soon as the free space is back above 10 % (delete files from the card on a PC, or insert a larger card).

With about 0.8 MB of data per day (≈ 300 MB per year), a 4 GB card holds more than ten years of data – the cleanup is mainly a safety net for small cards or cards with other data on them.

### Settings

At the top of `HeatPulse.ino`:

```cpp
constexpr uint8_t SD_MIN_FREE_PERCENT = 10;     // start cleanup below this free space
constexpr uint8_t SD_TARGET_FREE_PERCENT = 15;  // cleanup goal / warning threshold
constexpr uint16_t SD_RETENTION_DAYS = 365;     // never delete files younger than this automatically
constexpr bool SD_PRUNE_EXPIRED_ALWAYS = false; // true = delete files older than the retention even with enough space
constexpr uint32_t SD_FLUSH_MS = 600000;        // write interval (10 min)
constexpr size_t LOG_BUFFER_RECORDS = 1080;     // RAM buffer size (3 h of rows)
constexpr size_t EVENT_BUFFER_RECORDS = 128;    // RAM buffer for phase events
```

With `SD_PRUNE_EXPIRED_ALWAYS = false` (default) old files are kept as long as there is space, so the archive can hold more than a year. Set it to `true` if you want the card to hold at most `SD_RETENTION_DAYS` days.

## Behaviour on failures and limits

### Power cut

The rows that are only in RAM are lost: **at most about the last 10 minutes** (60 rows) plus the events from that period. Data already written to the card are not affected – every write ends with `fsync` and `fclose`. If you plan to unplug the logger, press **Write buffer to microSD now** on the web page first. The same button is useful before downloading today's CSV, because the file on the card lags behind by up to 10 minutes.

### Card removed or not responding

- Measuring, the web page, the chart and the statistics keep running.
- The card is checked every 60 s with a read-only sector read; a failed write is detected immediately. State **ERROR – card missing or not responding**.
- The data wait in RAM; the firmware tries to mount the card every 60 s. When the card is back (the same one or another FAT32 card), it is mounted automatically and the waiting rows are written at once – into the correct daily files.
- The RAM buffer holds **3 hours**. If the card is unavailable for longer, the **oldest** unsaved rows are dropped so that the newest ones survive. This is reported as an error on the Serial Monitor (`ERROR: RAM buffer full …`) and on the web page (count of lost records since start).
- Do not remove the card during a write (the web page shows the time of the last write; a write takes well under a second every 10 minutes). Even then, the retry with rollback prevents duplicates.

### Card full

See [circular archive](#circular-archive). While writing is paused, the data wait in RAM for up to 3 hours, then the oldest unsaved rows start to be dropped (reported as an error).

### Memory

The RAM buffers (≈ 39 KB) are allocated once at boot and never freed; the writer uses static buffers and no `String` objects, so long-term operation does not fragment the heap. Free heap, minimum free heap and the largest free block are shown after every write on the Serial Monitor; `/api/status` also returns `heap_free` and `heap_min`.

### Downloads

Downloading a CSV holds the card for the duration of the transfer. The writer task waits (the data stay in RAM). If the card is busy with a write, the web server answers `503` after 1.5 s instead of blocking measuring; just try again. Downloading a large file can briefly delay a measurement, as before.

## Web page – Storage section

| Item | Meaning |
|---|---|
| state badge | **OK**, **WARNING** (< 15 % free), **CLEANUP** (deleting old files), **ERROR** (card missing, write failed, insufficient space or lost records) with an explanation |
| Capacity / Used / Free / Usage | FAT32 volume of the card |
| Daily CSV files | number of `YYYYMMDD.CSV` files (+ `NOSYNC` if that file exists) |
| Oldest / newest day | dates of the oldest and newest daily file (from the file names) |
| Last successful write | time and age of the last verified write |
| Unsaved records in RAM | rows (and events) waiting for the next write, buffer capacity and the time to the next write |
| Circular archive | what the archive does now: active, waiting for NTP time, cleanup, insufficient space |

The section is refreshed every 10 s together with the other values, without reloading the page.

## Serial Monitor diagnostics

At 115200 baud the firmware reports every write (`SD: saved 60 rows + 1 events …`), every failure with the failing step, remounts, rollbacks, deletions and buffer overflows. You can type commands (send with *Newline*):

| Command | Meaning |
|---|---|
| `status` | storage state as JSON, heap and the writer task's free stack |
| `flush` | write the RAM buffer now |
| `failwrite <n>` | **test:** the next *n* writes are reported as failed after the rows were written – tests retry and rollback |
| `simfree <percent>` / `simfree off` | **test:** pretend the card has this much free space – tests the archive. **Caution:** files older than 365 days are really deleted |
| `settime <unix_s>` | **test:** set the clock (e.g. shortly before midnight); disables archive deletion until the next NTP sync |

See [testing](testing.md) for complete test procedures.
