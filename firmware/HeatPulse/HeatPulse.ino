/*
 * HeatPulse – standalone heat pump temperature logger
 * Documentation: README.md and the docs/ folder in the repository root.
 *
 * Hardware: ESP32-DevKitC-32E (ESP-WROOM-32E), MAX6675 + type K thermocouple,
 *           DS18B20 (e.g. LaskaKit LA131090), LaskaKit microSD LA161078,
 *           optional room (cellar) temperature/humidity sensor: Sensirion SHT4x or SHT3x (e.g. LaskaKit SHT40
 *           LA131037, recommended) or AOSONG AM2301 (DHT21).
 * Software: Arduino-ESP32 core 3.x; OneWire and DallasTemperature libraries (Library Manager).
 *
 * The logger NEVER controls the heat pump. The compressor state is ONLY an estimate from temperature.
 *
 * MAX6675       -> ESP32       microSD (LA161078) -> ESP32
 * VCC           -> 3V3         VCC                 -> 3V3
 * GND           -> GND         GND                 -> GND
 * SCK           -> GPIO26      CLK / SCK           -> GPIO18
 * CS            -> GPIO27      DO  / MISO          -> GPIO19
 * SO            -> GPIO25      DI  / MOSI          -> GPIO23
 *                              CS                  -> GPIO21
 *
 * DS18B20 (three-wire connection, LaskaKit LA131090):
 *   red (VDD) -> 3V3, black (GND) -> GND, yellow (DQ) -> GPIO32
 *   4.7 kΩ resistor between GPIO32 and 3V3 (pull-up, NOT in series with the data wire).
 *
 * Room temperature and humidity (optional, CELLAR_SENSOR), one of:
 *   SHT4x / SHT3x (I2C): VCC -> 3V3, GND -> GND, SDA -> GPIO33, SCL -> GPIO22
 *                  (DFRobot SEN0385 probe: red VCC, black GND, green SDA, yellow SCL)
 *                  (the LaskaKit module has its own pull-ups; a 4.7 kΩ left on GPIO33 does no harm)
 *   AM2301/DHT21:  red (VDD) -> 3V3, black (GND) -> GND, yellow (DATA) -> GPIO33
 *                  4.7 kΩ resistor between GPIO33 and 3V3. Its single-wire protocol is NOT Dallas 1-Wire:
 *                  the AM2301 needs its own GPIO and must not be connected to the DS18B20 bus.
 *
 * Never apply a 5 V signal to a GPIO. Board power: 5 V over micro-USB from an ordinary adapter.
 *
 * Data storage (see docs/storage.md):
 *   - loop() measures every 10 s and puts each row into a RAM ring buffer (it never touches the card);
 *   - a separate FreeRTOS task ("sdWriter") writes the buffer to microSD every 10 minutes,
 *     checks every step (open, write, flush, fsync, close, resulting file size) and removes the rows
 *     from RAM only after they are verifiably on the card;
 *   - the same task remounts a removed card, keeps the circular archive (deletes the oldest daily files
 *     only when free space is low and only when they are older than SD_RETENTION_DAYS) and publishes
 *     the storage state for the web page.
 *   A power cut loses at most the rows that are still in RAM (up to ~10 minutes).
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include <esp_sntp.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

// ===================== SETTINGS =====================
// Wi-Fi credentials live in secrets.h (template: secrets.example.h).
#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "secrets.h not found – using the example values from secrets.example.h"
#include "secrets.example.h"
#endif

// Time zone (POSIX TZ): Central European Time with daylight saving.
const char *TZ_INFO = "CET-1CEST,M3.5.0/2,M10.5.0/3";
const char *NTP_SERVER_1 = "pool.ntp.org";
const char *NTP_SERVER_2 = "time.google.com";
// Room (cellar) temperature and humidity sensor:
//   CELLAR_SHT    – Sensirion SHT4x (SHT40/41/45) or SHT3x (SHT30/31/35) on I2C (SDA = GPIO33, SCL = GPIO22),
//                   the type is detected automatically; recommended, also for very humid rooms (both have
//                   a built-in heater, see SHT_HEAT_*)
//   CELLAR_AM2301 – AOSONG AM2301 / DHT21 on GPIO33 (single-wire); only up to about 80 % RH
//   CELLAR_NONE   – no sensor: the web page hides the section, the CSV columns stay empty
enum : uint8_t { CELLAR_NONE = 0, CELLAR_AM2301 = 1, CELLAR_SHT = 2 };
constexpr uint8_t CELLAR_SENSOR = CELLAR_SHT;
constexpr bool CELLAR_ENABLED = CELLAR_SENSOR != CELLAR_NONE;
// ====================================================

// ===================== microSD STORAGE =====================
// Circular archive: when free space drops below SD_MIN_FREE_PERCENT, the oldest daily CSV files
// are deleted until SD_TARGET_FREE_PERCENT is free again – but ONLY files older than
// SD_RETENTION_DAYS. If that is not enough, writing is paused ("insufficient space") and nothing
// newer is ever deleted automatically.
constexpr uint8_t SD_MIN_FREE_PERCENT = 10;
constexpr uint8_t SD_TARGET_FREE_PERCENT = 15;
constexpr uint16_t SD_RETENTION_DAYS = 365;
// true = also delete files older than SD_RETENTION_DAYS during routine maintenance, even when there
// is enough space (the card then holds about one year). false = keep them until space is needed.
constexpr bool SD_PRUNE_EXPIRED_ALWAYS = false;
constexpr uint32_t SD_FLUSH_MS = 600000;            // write the RAM buffer to the card every 10 min
constexpr size_t LOG_BUFFER_RECORDS = 1080;         // RAM buffer: 3 h of 10 s rows (48 B each)
constexpr size_t EVENT_BUFFER_RECORDS = 128;        // RAM buffer for phase events
constexpr size_t CELLAR_BUFFER_RECORDS = 16;        // RAM buffer for finished cellar days (CELLAR.CSV)
// ===========================================================

const char *FW_VERSION = "2.3";

constexpr int PIN_TC_CLK = 26;
constexpr int PIN_TC_CS = 27;
constexpr int PIN_TC_DO = 25;
constexpr int PIN_SD_CLK = 18;
constexpr int PIN_SD_MISO = 19;
constexpr int PIN_SD_MOSI = 23;
constexpr int PIN_SD_CS = 21;
constexpr int PIN_ONEWIRE = 32;
constexpr int PIN_CELLAR = 33;                      // SHT SDA, or AM2301 data (own protocol, not 1-Wire)
constexpr int PIN_CELLAR_SCL = 22;                  // SHT SCL

constexpr uint32_t SAMPLE_MS = 10000;               // one CSV row every 10 s
constexpr uint32_t GRAPH_MS = 60000;                // web chart: 1 point/min, up to 24 h
constexpr uint32_t DS_CONVERSION_MS = 800;          // 12-bit DS18B20 (~750 ms), non-blocking read
constexpr uint64_t HOT_CONFIRM_MS = 30000;
constexpr uint64_t COLD_CONFIRM_MS = 60000;
constexpr uint64_t SENSOR_LOSS_RESET_MS = 300000;   // reset the estimate only after 5 min without a valid reading
// Early start detection: a compressor start shows as a quick rise of the pipe temperature, minutes before
// it reaches the hot threshold. In the cold phase each reading is compared with the lowest reading of the
// last RISE_WINDOW_SAMPLES; RISE_CONFIRM_SAMPLES readings in a row at least RISE_START_C above it mark a
// rise. The start is still confirmed by the hot threshold (so a pipe warmed only by the circulating water is
// not counted), but its time is taken from the beginning of the rise.
constexpr float RISE_START_C = 2.0f;
constexpr float RISE_NOISE_C = 0.5f;                // MAX6675 readings move in 0.25 °C steps
constexpr size_t RISE_WINDOW_SAMPLES = 30;          // baseline = minimum of the last 5 min
constexpr uint8_t RISE_CONFIRM_SAMPLES = 2;
constexpr uint64_t RISE_VERIFY_MS = 1200000;        // the hot threshold must follow within 20 min
constexpr uint32_t SD_RETRY_MS = 60000;             // remount / retry a failed write every minute
constexpr uint32_t SD_PROBE_MS = 60000;             // check that the card still answers (read only)
constexpr uint32_t SD_WEB_WAIT_MS = 1500;           // web handlers wait at most this long for the card
constexpr uint32_t SD_TASK_WAIT_MS = 30000;         // writer task waits for a running download
constexpr size_t WRITE_CHUNK_RECORDS = 64;          // rows formatted and appended per file operation
constexpr size_t ARCHIVE_CANDIDATES = 16;           // oldest files collected per directory scan
constexpr uint32_t WIFI_CONNECT_MS = 12000;
constexpr uint32_t WIFI_RETRY_MS = 300000;          // in AP mode, retry the home Wi-Fi every 5 min
// Run length rating of an estimated compressor run (hot phase):
// < 6 min poor, 6–10 min acceptable, 10–20 min good, >= 20 min ideal (see docs/heat-pump-cycling.md).
// The web page uses the same limits (RUN in the script).
constexpr uint32_t RUN_ACCEPTABLE_S = 360;
constexpr uint32_t RUN_GOOD_S = 600;
constexpr uint32_t RUN_IDEAL_S = 1200;
constexpr uint64_t CELLAR_WARMUP_MS = 2500;         // the AM2301 must not be read in the first 2 s after power-up
// SHT4x / SHT3x at very high humidity: a short heater pulse dries the sensing element and limits the slow upward
// drift ("creep") that every humidity sensor shows in nearly condensing air. The readings of the next
// minute, while the sensor cools down, are skipped (the previous value is kept, not counted in the day).
constexpr float SHT_HEAT_ABOVE_RH = 90.0f;
constexpr uint64_t SHT_HEAT_INTERVAL_MS = 3600000;  // at most one pulse per hour (SHT4x 110 mW 1 s, SHT3x ~10 s)
constexpr uint64_t SHT_HEAT_SETTLE_MS = 60000;
constexpr uint64_t CELLAR_HOLD_MS = 25000;          // up to 2 failed readings in a row keep the previous value
constexpr uint8_t CELLAR_SAVE_SAMPLES = 60;         // day in progress saved to NVS every 60 samples (10 min)
constexpr size_t CELLAR_DAYS = 400;                 // daily summaries kept in RAM for the web page
constexpr size_t GRAPH_POINTS = 1440;
constexpr size_t MAX_STARTS = 512;
constexpr size_t MAX_CYCLES = 512;

WebServer web(80);
Preferences prefs;
OneWire ow(PIN_ONEWIRE);
DallasTemperature dallas(&ow);

const char *SD_MOUNT = "/sd";                       // VFS mount point of the card (stdio paths)
const char *LOG_HEADER = "local_time;unix_s;uptime_ms;gas_C;water_C;gas_valid;water_valid;thermal_estimate;target_water_C;cellar_C;cellar_RH;cellar_valid\r\n";
const char *EVENT_HEADER = "local_time;unix_s;uptime_ms;event;gas_C;water_C;duration_s\r\n";
const char *CELLAR_HEADER = "date;t_min_C;t_avg_C;t_max_C;rh_min_pct;rh_avg_pct;rh_max_pct;ah_avg_gm3;samples\r\n";

// sdOK = card mounted and answering. Written only by the SD writer task (and setup()).
volatile bool sdOK = false;
volatile bool ntpSynced = false;                    // time confirmed by NTP since boot (not by the browser)
volatile bool flushRequested = false;               // "write now" from the web page or Serial
volatile uint8_t testFailWrites = 0;                // Serial test command: simulate N failed writes
volatile int16_t testFreePercent = -1;              // Serial test command: simulated free space, -1 = off
bool dsPresent = false;
bool samplePending = false;
bool gasValid = false;
bool waterValid = false;
float gasC = NAN;
float waterC = NAN;
float configuredTargetC = 28.0f;
bool cellarValid = false;
float cellarT = NAN;                                // filtered (median of 3): web page, chart, daily summary
float cellarRH = NAN;
float cellarRawT = NAN;                             // last raw reading: the 10 s CSV
float cellarRawRH = NAN;
uint64_t cellarOkUpMs = 0;                          // uptime of the last good room sensor reading, 0 = none
uint32_t cellarErrors = 0;                          // failed room sensor readings since boot
uint8_t cellarLastError = 0;                        // CS_* code of the last failed reading
bool cellarFresh = false;                           // a new filtered value this sample (counted in the day)
uint32_t lastSampleBeginMs = 0;
uint32_t sampleStartedMs = 0;
uint64_t sampleStartedUpMs = 0;
uint32_t lastGraphMs = 0;

bool apFallback = false;
bool wifiAttempt = false;
uint32_t lastWifiRetryMs = 0;

// Temperature-based estimate only, not the actual compressor state.
// Times are in ms since ESP32 boot (64-bit, so they do not overflow after 49 days like millis()).
// A value of 0 in hotFromMs/coldFromMs means "phase start unknown" (it was already running at boot).
float hotThresholdC = 45.0f;
float coldThresholdC = 35.0f;
bool estimatedKnown = false;
bool estimatedHot = false;
uint64_t hotCandidateMs = 0;
uint64_t coldCandidateMs = 0;
uint64_t hotFromMs = 0;
uint64_t coldFromMs = 0;
uint64_t gasInvalidFromMs = 0;
uint32_t lastHotS = 0;
uint32_t lastColdS = 0;
float riseWindow[RISE_WINDOW_SAMPLES];              // recent valid pipe readings (baseline for the rise)
size_t riseWrite = 0;
size_t riseCount = 0;
uint64_t aboveNoiseFromMs = 0;                      // first reading of the current climb above the baseline
uint8_t riseHits = 0;
uint64_t riseFromMs = 0;                            // detected rise: beginning of the climb, 0 = none
float riseBaseC = 0;

// Cellar values in tenths (°C, %RH); NO_TENTHS = no valid reading.
constexpr int16_t NO_TENTHS = INT16_MIN;
struct GraphPoint { uint32_t stamp; float gas; float water; int16_t cellarT; int16_t cellarRH; };
GraphPoint history[GRAPH_POINTS];
size_t graphWrite = 0;
size_t graphCount = 0;

// coldSeconds = 0 means the length of the preceding cold phase is unknown.
struct Cycle { uint64_t endedMs; uint32_t hotSeconds; uint32_t coldSeconds; };
Cycle cycles[MAX_CYCLES];
size_t cycleWrite = 0;
size_t cycleCount = 0;
uint64_t starts[MAX_STARTS];
size_t startWrite = 0;
size_t startCount = 0;

// Structs must be defined before the first function (Arduino inserts prototypes there).
struct Stats {
  uint32_t starts1h = 0;
  uint32_t starts24h = 0;
  uint32_t completed24h = 0;
  uint32_t coldKnown24h = 0;
  uint32_t runs[4] = {0, 0, 0, 0};    // run counts per rating class (poor, acceptable, good, ideal)
  uint64_t totalHotS = 0;
  uint64_t totalColdS = 0;
};

// ---------- RAM buffer for the card ----------
// One 10 s row exactly as it will appear in the daily CSV. The wall-clock time is captured at
// measurement time, so a row written minutes later still gets its original time stamp and lands in
// the daily file of the day it was measured (midnight, DST).
enum : uint8_t { EST_UNKNOWN = 0, EST_COLD = 1, EST_HOT = 2 };
enum : uint8_t { REC_GAS_VALID = 1, REC_WATER_VALID = 2, REC_CELLAR_VALID = 4 };
struct LogRecord {
  uint64_t uptimeMs;
  uint32_t seq;          // sequence number, used to remove exactly the rows that were written
  uint32_t unixS;        // 0 = clock not synchronised -> NOSYNC.CSV, empty time columns
  float gas, water, target;
  float cellarT, cellarRH;
  uint8_t flags;         // REC_* bits
  uint8_t estimate;      // EST_*
};
enum : uint8_t { EVENT_THERMAL_HOT = 0, EVENT_THERMAL_COLD = 1 };
struct EventRecord {
  uint64_t uptimeMs;
  uint32_t seq;
  uint32_t unixS;
  float gas, water;
  int32_t durationS;     // < 0 = unknown
  uint8_t type;          // EVENT_*
  uint8_t flags;         // REC_* bits
};
// One finished day of the cellar sensor (a row of CELLAR.CSV). Values in tenths: °C, %RH, g/m³.
struct CellarDay {
  uint32_t seq;
  uint32_t ymd;
  int16_t tMin, tAvg, tMax, hMin, hAvg, hMax, ahAvg;
  uint16_t samples;      // valid 10 s readings that day (8640 = complete day)
};
// The cellar day in progress. Saved to NVS every 10 minutes, so a restart during the day does not
// lose the morning: after the restart the day continues where it was saved.
constexpr uint16_t CELLAR_ACC_VERSION = 1;
struct CellarAccum {
  uint16_t version = CELLAR_ACC_VERSION;
  uint32_t ymd = 0;      // local day, 0 = none
  uint32_t n = 0;        // valid readings
  float tMin = 0, tMax = 0, hMin = 0, hMax = 0;
  double tSum = 0, hSum = 0, ahSum = 0;
};

// Fixed-size ring buffer. The storage is allocated ONCE in setup() and never freed, so it cannot
// fragment the heap (it does not fit into the static DRAM segment of the ESP32).
// All methods except allocate() must be called inside portENTER_CRITICAL(&bufMux).
// When full, the OLDEST record is dropped so that the newest data survive a long card outage.
template <typename T> struct RecordRing {
  T *items = nullptr;
  size_t cap = 0;
  size_t head = 0;
  size_t count = 0;
  uint32_t nextSeq = 1;
  uint32_t dropped = 0;  // records lost because the buffer was full (since boot)
  size_t allocate(size_t wanted) {
    // If RAM is short, take a smaller buffer rather than none.
    for (size_t n = wanted; n >= 16 && !items; n /= 2) {
      items = (T *)calloc(n, sizeof(T));
      if (items) cap = n;
    }
    return cap;
  }
  bool push(T &r) {
    r.seq = nextSeq++;
    if (!cap) { ++dropped; return true; }
    bool lost = false;
    if (count == cap) { head = (head + 1) % cap; --count; ++dropped; lost = true; }
    items[(head + count) % cap] = r;
    ++count;
    return lost;
  }
  const T &at(size_t i) const { return items[(head + i) % cap]; }
  // Removes every record up to and including lastSeq (records are ordered by seq).
  void commit(uint32_t lastSeq) {
    while (count && items[head].seq <= lastSeq) { head = (head + 1) % cap; --count; }
  }
};

// Storage state shared between the writer task and the web server (guarded by bufMux).
struct StorageInfo {
  uint64_t totalB = 0;
  uint64_t usedB = 0;
  uint64_t lastWriteUpMs = 0;   // uptime of the last successful write, 0 = none yet
  uint64_t lastFlushUpMs = 0;   // start of the current 10-minute period
  uint32_t lastWriteUnix = 0;   // wall-clock time of the last successful write, 0 = unknown
  uint32_t dailyFiles = 0;      // YYYYMMDD.CSV files in /LOGS
  uint32_t oldestYmd = 0;       // oldest / newest daily file as YYYYMMDD, 0 = none
  uint32_t newestYmd = 0;
  uint32_t deletedFiles = 0;    // deleted by the archive since boot
  uint32_t writeErrors = 0;     // failed write attempts since boot
  bool nosyncFile = false;      // NOSYNC.CSV exists
  bool spaceKnown = false;
  bool writeFailed = false;     // the last write attempt failed (cleared by the next success)
  bool noSpace = false;         // writing paused: not enough space and nothing old enough to delete
  bool cleanup = false;         // the archive is deleting files right now
  bool overflow = false;        // rows were lost since the last successful write
};

// Holds the card mutex for the lifetime of the object. ok == false when the card stayed busy.
struct SdLock {
  bool ok;
  explicit SdLock(uint32_t waitMs);
  ~SdLock();
};

// Print target that formats CSV text into a fixed buffer. Using Print keeps the number formatting
// byte-for-byte identical to the previous firmware, which printed directly into the File.
class TextBuffer : public Print {
 public:
  TextBuffer(char *buf, size_t cap) : buf_(buf), cap_(cap) {}
  size_t write(uint8_t c) override {
    if (len_ + 1 >= cap_) { overflow_ = true; return 0; }
    buf_[len_++] = (char)c;
    buf_[len_] = 0;
    return 1;
  }
  size_t write(const uint8_t *b, size_t n) override {
    size_t i = 0;
    while (i < n && write(b[i])) ++i;
    return i;
  }
  size_t length() const { return len_; }
  bool overflow() const { return overflow_; }
  void cutTo(size_t n) { len_ = n; buf_[len_] = 0; overflow_ = false; }
 private:
  char *buf_;
  size_t cap_;
  size_t len_ = 0;
  bool overflow_ = false;
};

// Remembers an append that did not finish cleanly, so a retry first cuts the file back to its
// previous size and the same rows are never written twice (or with a half line in between).
struct PendingAppend {
  bool active = false;
  char path[40] = {0};
  long sizeBefore = 0;
  long maxBytes = 0;          // the failed append wrote at most this many bytes
  uint64_t volumeBytes = 0;   // card capacity at that time (a different card is never truncated)
};

struct LogScan {
  uint32_t dailyFiles = 0;
  bool nosync = false;
  uint32_t oldestYmd = 0;
  uint32_t newestYmd = 0;
  size_t nCand = 0;
  uint32_t cand[ARCHIVE_CANDIDATES];   // oldest deletable files (YYYYMMDD), ascending
};

portMUX_TYPE bufMux = portMUX_INITIALIZER_UNLOCKED;
RecordRing<LogRecord> logRing;
RecordRing<EventRecord> eventRing;
RecordRing<CellarDay> cellarRing;      // finished days waiting for CELLAR.CSV
CellarDay *cellarDays = nullptr;       // daily summaries for the web page, sorted by day (guarded by bufMux)
size_t cellarDayCap = 0;
size_t cellarDayCount = 0;
CellarAccum cellarAcc;                 // day in progress (loop task only)
bool cellarAccRestored = false;
uint8_t cellarSinceSave = 0;
portMUX_TYPE amMux = portMUX_INITIALIZER_UNLOCKED;
StorageInfo storage;
SemaphoreHandle_t sdMutex = nullptr;   // serialises every card access (writer task, downloads, file list)
TaskHandle_t sdTaskHandle = nullptr;
PendingAppend pendingAppend;           // used only by the writer task
LogRecord chunkRows[WRITE_CHUNK_RECORDS];
EventRecord chunkEvents[WRITE_CHUNK_RECORDS];
CellarDay chunkCellar[CELLAR_BUFFER_RECORDS];
char writeText[WRITE_CHUNK_RECORDS * 128];
uint8_t probeSector[512];

SdLock::SdLock(uint32_t waitMs) { ok = sdMutex && xSemaphoreTake(sdMutex, pdMS_TO_TICKS(waitMs)) == pdTRUE; }
SdLock::~SdLock() { if (ok) xSemaphoreGive(sdMutex); }

int runClass(uint32_t seconds) {
  if (seconds < RUN_ACCEPTABLE_S) return 0;
  if (seconds < RUN_GOOD_S) return 1;
  if (seconds < RUN_IDEAL_S) return 2;
  return 3;
}

uint64_t uptimeMs() { return (uint64_t)(esp_timer_get_time() / 1000LL); }

bool timeIsKnown() { return time(nullptr) >= 1700000000; }
uint32_t graphTime() { return timeIsKnown() ? (uint32_t)time(nullptr) : 0; }

// Local calendar date of a Unix time as YYYYMMDD (0 = time unknown -> NOSYNC.CSV).
uint32_t localYmd(uint32_t unixS) {
  if (!unixS) return 0;
  time_t t = (time_t)unixS;
  struct tm lt;
  localtime_r(&t, &lt);
  return (uint32_t)((lt.tm_year + 1900) * 10000 + (lt.tm_mon + 1) * 100 + lt.tm_mday);
}
// Days since 1970-01-01 of a calendar date (H. Hinnant's algorithm, no time zone involved).
int32_t daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const int yoe = y - era * 400;
  const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}
int32_t ymdToDays(uint32_t ymd) { return daysFromCivil((int)(ymd / 10000), (int)(ymd / 100 % 100), (int)(ymd % 100)); }
// Strict parser for daily file names "YYYYMMDD.CSV" with a real calendar date (2000–2099).
// Anything else (NOSYNC.CSV, other files) is never treated as a dated archive file.
uint32_t parseDailyName(const char *n) {
  if (strlen(n) != 12 || strcmp(n + 8, ".CSV") != 0) return 0;
  uint32_t v = 0;
  for (int i = 0; i < 8; ++i) {
    if (n[i] < '0' || n[i] > '9') return 0;
    v = v * 10 + (uint32_t)(n[i] - '0');
  }
  int y = (int)(v / 10000), m = (int)(v / 100 % 100), d = (int)(v % 100);
  static const uint8_t dim[12] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (y < 2000 || y > 2099 || m < 1 || m > 12 || d < 1 || d > dim[m - 1]) return 0;
  if (m == 2 && d == 29 && !(y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))) return 0;
  return v;
}
void dailyPath(uint32_t ymd, char *out, size_t n) {
  if (ymd) snprintf(out, n, "%s/LOGS/%08lu.CSV", SD_MOUNT, (unsigned long)ymd);
  else snprintf(out, n, "%s/LOGS/NOSYNC.CSV", SD_MOUNT);
}
void onNtpSync(struct timeval *tv) { (void)tv; ntpSynced = true; }

// MAX6675: software SPI, data MSB first.
// D15 is always 0, D2 = thermocouple open, D1 = device ID (always 0).
// With the pull-up on SO, a missing module reads all ones -> error instead of a false 0 °C.
// The MAX6675 only measures 0..1024 °C (it cannot be used below zero).
float readMAX6675() {
  uint16_t word = 0;
  digitalWrite(PIN_TC_CLK, LOW);
  digitalWrite(PIN_TC_CS, LOW);
  delayMicroseconds(5);
  for (int b = 0; b < 16; ++b) {
    digitalWrite(PIN_TC_CLK, LOW);
    delayMicroseconds(5);
    word = (uint16_t)((word << 1) | (digitalRead(PIN_TC_DO) ? 1 : 0));
    digitalWrite(PIN_TC_CLK, HIGH);
    delayMicroseconds(5);
  }
  digitalWrite(PIN_TC_CLK, LOW);
  digitalWrite(PIN_TC_CS, HIGH);
  if (word & 0x8006u) return NAN;
  float temp = (word >> 3) * 0.25f;
  return (temp >= 0.0f && temp < 1024.0f) ? temp : NAN;
}

// Result codes of the room sensor (both types).
enum : uint8_t { CS_OK = 0, CS_NO_RESPONSE, CS_TIMEOUT, CS_CHECKSUM, CS_RANGE };
const char *const CELLAR_ERROR_NAMES[] = {"OK", "NO_RESPONSE", "TIMEOUT", "CHECKSUM", "RANGE"};

// How long (µs) the data line stays at `level`, or -1 when it does not change within timeoutUs.
int32_t amPulse(int level, uint32_t timeoutUs) {
  uint32_t t0 = micros();
  while (gpio_get_level((gpio_num_t)PIN_CELLAR) == level)
    if ((uint32_t)(micros() - t0) > timeoutUs) return -1;
  return (int32_t)(micros() - t0);
}

// AM2301 (DHT21): own single-wire protocol on an open-drain pin – NOT Dallas 1-Wire.
// Frame: 16 bit RH x10, 16 bit temperature x10 (MSB = sign), 8 bit checksum. A bit is ~50 µs low
// followed by 26–28 µs high (0) or 70 µs high (1). The ~5 ms frame is read with interrupts off on
// this core only; Wi-Fi runs on the other core. The sensor returns the measurement it took after
// the previous reading, so the value is up to one sample interval (10 s) old.
uint8_t readAM2301(float &t, float &rh) {
  uint8_t d[5] = {0, 0, 0, 0, 0};
  uint8_t res = CS_OK;
  // Start signal: the host holds the line low for ~1.1 ms (datasheet 0.8–20 ms), then releases it.
  gpio_set_level((gpio_num_t)PIN_CELLAR, 0);
  delayMicroseconds(1100);
  portENTER_CRITICAL(&amMux);
  gpio_set_level((gpio_num_t)PIN_CELLAR, 1);   // release: the pull-up takes the line high
  // Line rises, the sensor pulls it low after 20–200 µs and answers with 80 µs low + 80 µs high.
  if (amPulse(0, 60) < 0 || amPulse(1, 250) < 0 || amPulse(0, 120) < 0 || amPulse(1, 120) < 0) res = CS_NO_RESPONSE;
  for (int i = 0; i < 40 && res == CS_OK; ++i) {
    int32_t high = amPulse(0, 100) < 0 ? -1 : amPulse(1, 120);
    if (high < 0) res = CS_TIMEOUT;
    else d[i / 8] = (uint8_t)((d[i / 8] << 1) | (high > 48 ? 1 : 0));
  }
  portEXIT_CRITICAL(&amMux);
  if (res != CS_OK) return res;
  // An all-zero frame has a "valid" checksum, but it is a line problem, not 0 % at 0 °C.
  if ((uint8_t)(d[0] + d[1] + d[2] + d[3]) != d[4] || !(d[0] | d[1] | d[2] | d[3])) return CS_CHECKSUM;
  rh = ((d[0] << 8) | d[1]) * 0.1f;
  t = (((d[2] & 0x7F) << 8) | d[3]) * 0.1f;
  if (d[2] & 0x80) t = -t;
  if (rh > 100.0f || t < -40.0f || t > 80.0f) return CS_RANGE;
  return CS_OK;
}

// Sensirion SHT4x (SHT40/41/45) or SHT3x (SHT30/31/35, e.g. DFRobot SEN0385) on I2C, address 0x44 or 0x45.
// Both families answer at the same addresses but use different commands: the SHT4x has 1-byte commands,
// the SHT3x 2-byte ones. The type is detected once (SHT4x serial number 0x89, SHT3x status register 0xF32D,
// each checked by CRC) and again after the sensor stops answering, so it can be swapped while running.
// The measurement is started in beginSample() and read in finishSample() ~800 ms later (high repeatability
// takes 8.3 ms / 15 ms), so the loop never waits. Frame of both: 16 bit T + CRC-8, 16 bit RH + CRC-8.
enum : uint8_t { SHT_NONE = 0, SHT_4X, SHT_3X };
const uint8_t SHT4X_MEASURE_HIGH[] = {0xFD};
const uint8_t SHT4X_SERIAL[] = {0x89};
const uint8_t SHT4X_HEAT_110MW_1S[] = {0x2F};
const uint8_t SHT3X_MEASURE_HIGH[] = {0x24, 0x00};  // single shot, high repeatability, no clock stretching
const uint8_t SHT3X_STATUS[] = {0xF3, 0x2D};
const uint8_t SHT3X_HEATER_ON[] = {0x30, 0x6D};
const uint8_t SHT3X_HEATER_OFF[] = {0x30, 0x66};
uint8_t shtAddr = 0;                                // 0 = not found yet
uint8_t shtType = SHT_NONE;
bool shtHeaterOn = false;                           // SHT3x heater switched on, to be switched off

uint8_t shtCrc(const uint8_t *d) {
  uint8_t c = 0xFF;
  for (int i = 0; i < 2; ++i) {
    c ^= d[i];
    for (int b = 0; b < 8; ++b) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x31) : (uint8_t)(c << 1);
  }
  return c;
}

bool shtWrite(uint8_t addr, const uint8_t *cmd, size_t n) {
  Wire.beginTransmission(addr);
  Wire.write(cmd, n);
  return Wire.endTransmission() == 0;
}

// Reads n bytes (words of 2 data bytes + CRC) and checks every CRC. Returns a CS_* code.
uint8_t shtReadWords(uint8_t addr, uint8_t *d, size_t n) {
  if (Wire.requestFrom(addr, (uint8_t)n) != n) return CS_TIMEOUT;
  for (size_t i = 0; i < n; ++i) d[i] = (uint8_t)Wire.read();
  for (size_t i = 0; i < n; i += 3)
    if (shtCrc(d + i) != d[i + 2]) return CS_CHECKSUM;
  return CS_OK;
}

// Finds the sensor at 0x44 / 0x45 and tells the SHT4x from the SHT3x.
bool shtDetect() {
  uint8_t d[6];
  for (uint8_t a : {0x44, 0x45}) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() != 0) continue;
    // A result still waiting from before a reset would answer the serial-number read below with a
    // valid CRC and pass an SHT3x off as an SHT4x. Read it out first; with nothing waiting it is a NACK.
    if (Wire.requestFrom(a, (uint8_t)6) > 0) while (Wire.available()) Wire.read();
    if (shtWrite(a, SHT4X_SERIAL, 1)) {
      delay(2);
      if (shtReadWords(a, d, 6) == CS_OK) { shtType = SHT_4X; shtAddr = a; break; }
    }
    // An SHT3x ignores the incomplete 1-byte command above and answers its own 2-byte status read.
    if (shtWrite(a, SHT3X_STATUS, 2)) {
      delay(2);
      if (shtReadWords(a, d, 3) == CS_OK) { shtType = SHT_3X; shtAddr = a; break; }
    }
  }
  if (!shtAddr) return false;
  if (shtType == SHT_3X) shtWrite(shtAddr, SHT3X_HEATER_OFF, 2);   // may have been left on before a reset
  shtHeaterOn = false;
  Serial.printf("%s found at I2C address 0x%02X\n", shtType == SHT_4X ? "SHT4x" : "SHT3x", shtAddr);
  return true;
}

// A sensor interrupted in the middle of a read (e.g. the ESP32 was reset while the sensor stayed powered)
// can hold SDA low and block the bus. Clock out the rest of its byte (up to 9 SCL pulses), then send a STOP.
void i2cRecover() {
  if (gpio_get_level((gpio_num_t)PIN_CELLAR)) return;   // SDA high: the bus is free
  Wire.end();
  pinMode(PIN_CELLAR, INPUT_PULLUP);
  digitalWrite(PIN_CELLAR_SCL, HIGH);
  pinMode(PIN_CELLAR_SCL, OUTPUT_OPEN_DRAIN | PULLUP);
  for (int i = 0; i < 9 && !digitalRead(PIN_CELLAR); ++i) {
    digitalWrite(PIN_CELLAR_SCL, LOW); delayMicroseconds(5);
    digitalWrite(PIN_CELLAR_SCL, HIGH); delayMicroseconds(5);
  }
  digitalWrite(PIN_CELLAR, LOW);                        // STOP: SDA goes high while SCL is high
  pinMode(PIN_CELLAR, OUTPUT_OPEN_DRAIN | PULLUP);
  delayMicroseconds(5);
  digitalWrite(PIN_CELLAR, HIGH); delayMicroseconds(5);
  Serial.printf("I2C: SDA was held low – bus %s\n", digitalRead(PIN_CELLAR) ? "released" : "still blocked");
  Wire.begin(PIN_CELLAR, PIN_CELLAR_SCL, 100000);
  Wire.setTimeOut(20);
}

// Starts a measurement; finds the sensor first if needed (it may be connected after boot).
bool shtStartMeasure() {
  if (!shtAddr) i2cRecover();
  if (!shtAddr && !shtDetect()) return false;
  bool ok = shtType == SHT_4X ? shtWrite(shtAddr, SHT4X_MEASURE_HIGH, 1) : shtWrite(shtAddr, SHT3X_MEASURE_HIGH, 2);
  if (!ok) { shtAddr = 0; shtType = SHT_NONE; }   // detect again next time
  return ok;
}

uint8_t readSHT(float &t, float &rh) {
  uint8_t d[6];
  uint8_t res = shtReadWords(shtAddr, d, 6);
  if (res != CS_OK) return res;
  float rawT = (float)((d[0] << 8) | d[1]) / 65535.0f, rawRH = (float)((d[3] << 8) | d[4]) / 65535.0f;
  t = -45.0f + 175.0f * rawT;
  rh = shtType == SHT_4X ? -6.0f + 125.0f * rawRH : 100.0f * rawRH;
  rh = constrain(rh, 0.0f, 100.0f);   // the SHT4x formula can give slightly below 0 / above 100
  if (t < -40.0f || t > 125.0f) return CS_RANGE;
  return CS_OK;
}

// Heater pulse. SHT4x: 110 mW for 1 s, it switches off by itself. SHT3x: the heater stays on until it is
// switched off, which startCellar() does at the next sample (~10 s later).
bool shtHeat() {
  if (shtType == SHT_4X) return shtWrite(shtAddr, SHT4X_HEAT_110MW_1S, 1);
  shtHeaterOn = shtWrite(shtAddr, SHT3X_HEATER_ON, 2);
  return shtHeaterOn;
}

// Dew point (Magnus formula over water) and absolute humidity in g/m³.
float dewPointC(float t, float rh) {
  if (!(rh >= 1.0f)) return NAN;
  float g = logf(rh / 100.0f) + 17.62f * t / (243.12f + t);
  return 243.12f * g / (17.62f - g);
}
float absHumidity(float t, float rh) { return 6.112f * expf(17.67f * t / (t + 243.5f)) * rh * 2.1674f / (273.15f + t); }

int16_t tenths(double v) { return (int16_t)lround(v * 10.0); }

bool mountSD() {
  // Without SD.end(), SD.begin() would report "mounted" after the card was removed
  // and reinserted, and the card would never be mounted again.
  SD.end();
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
  SPI.begin(PIN_SD_CLK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  if (!SD.begin(PIN_SD_CS, SPI, 4000000, SD_MOUNT, 5, false)) return false;
  if (SD.cardType() == CARD_NONE) return false;
  if (!SD.exists("/LOGS") && !SD.mkdir("/LOGS")) return false;
  return true;
}

// ---------------- Measurement side (loop task): only RAM, never the card ----------------

void reportDropped(const char *what, uint32_t dropped) {
  // Rate-limited: the first loss and then every 60th (= every 10 min of rows).
  if (dropped == 1 || dropped % 60 == 0)
    Serial.printf("ERROR: RAM buffer full – oldest unsaved %s lost (%lu in total). microSD unavailable for too long.\n",
                  what, (unsigned long)dropped);
}

void bufferSample(uint64_t sampleUpMs) {
  LogRecord r;
  r.uptimeMs = sampleUpMs;
  r.unixS = timeIsKnown() ? (uint32_t)time(nullptr) : 0;
  r.gas = gasC; r.water = waterC; r.target = configuredTargetC;
  r.cellarT = cellarRawT; r.cellarRH = cellarRawRH;   // raw: the filter can be redone from the CSV
  r.flags = (gasValid ? REC_GAS_VALID : 0) | (waterValid ? REC_WATER_VALID : 0) | (cellarValid ? REC_CELLAR_VALID : 0);
  r.estimate = !estimatedKnown ? EST_UNKNOWN : estimatedHot ? EST_HOT : EST_COLD;
  portENTER_CRITICAL(&bufMux);
  bool lost = logRing.push(r);
  if (lost) storage.overflow = true;
  uint32_t dropped = logRing.dropped;
  portEXIT_CRITICAL(&bufMux);
  if (lost) reportDropped("rows", dropped);
}

// durationS < 0: the length of the previous phase is unknown (the column is left empty).
void logEvent(const char *name, long durationS) {
  EventRecord e;
  e.uptimeMs = uptimeMs();
  e.unixS = timeIsKnown() ? (uint32_t)time(nullptr) : 0;
  e.gas = gasC; e.water = waterC;
  e.durationS = (int32_t)durationS;
  e.type = strcmp(name, "THERMAL_HOT") == 0 ? EVENT_THERMAL_HOT : EVENT_THERMAL_COLD;
  e.flags = (gasValid ? REC_GAS_VALID : 0) | (waterValid ? REC_WATER_VALID : 0);
  portENTER_CRITICAL(&bufMux);
  bool lost = eventRing.push(e);
  if (lost) storage.overflow = true;
  uint32_t dropped = eventRing.dropped;
  portEXIT_CRITICAL(&bufMux);
  if (lost) reportDropped("events", dropped);
}

// ---------------- Cellar sensor: readings and daily summaries ----------------

uint64_t shtLastHeatUpMs = 0;
uint64_t shtSettleUntilUpMs = 0;                    // readings skipped until then (sensor cooling down)
bool shtStarted = false;                            // a measurement was started in beginSample()

// Outlier filter: median of the last 3 good readings, separately for temperature and humidity. A single
// glitched reading (e.g. one 10 s value of 19.6 °C, another of 17.0 °C, with 18.0–18.2 °C on both
// sides) never reaches the web page, the chart or the daily min/max; a real change passes one
// reading (10 s) later. The window starts empty after boot and after an outage, and until it holds
// 3 readings the raw value is shown but not counted in the day. The CSV keeps the raw readings.
float cellarWinT[3], cellarWinRH[3];
uint8_t cellarWinN = 0, cellarWinPos = 0;

float median3(const float *v) { return max(min(v[0], v[1]), min(max(v[0], v[1]), v[2])); }

void cellarResult(uint8_t res, float t, float rh, uint64_t upMs) {
  if (res == CS_OK) {
    cellarRawT = t; cellarRawRH = rh; cellarValid = true; cellarOkUpMs = upMs;
    cellarWinT[cellarWinPos] = t; cellarWinRH[cellarWinPos] = rh;
    cellarWinPos = (cellarWinPos + 1) % 3;
    if (cellarWinN < 3) ++cellarWinN;
    cellarFresh = cellarWinN == 3;
    cellarT = cellarFresh ? median3(cellarWinT) : t;
    cellarRH = cellarFresh ? median3(cellarWinRH) : rh;
    return;
  }
  // Single failed readings happen (interference on the cable); they are bridged by the previous value.
  ++cellarErrors;
  cellarLastError = res;
  if (!cellarOkUpMs || upMs - cellarOkUpMs > CELLAR_HOLD_MS) {
    cellarValid = false;
    cellarT = cellarRH = cellarRawT = cellarRawRH = NAN;
    cellarWinN = 0;
  }
}

// beginSample(): the AM2301 is read completely here (~6 ms), the SHT only starts its measurement.
void startCellar(uint64_t upMs) {
  cellarFresh = false;
  shtStarted = false;
  if (!CELLAR_ENABLED || upMs < CELLAR_WARMUP_MS) return;
  if (CELLAR_SENSOR == CELLAR_AM2301) {
    float t = NAN, rh = NAN;
    uint8_t res = readAM2301(t, rh);
    cellarResult(res, t, rh, upMs);
  } else {
    if (shtHeaterOn && shtAddr && shtWrite(shtAddr, SHT3X_HEATER_OFF, 2)) shtHeaterOn = false;
    if (upMs < shtSettleUntilUpMs) return;
    shtStarted = shtStartMeasure();
    if (!shtStarted) cellarResult(CS_NO_RESPONSE, NAN, NAN, upMs);
  }
}

// finishSample(): reads the SHT result and, in nearly condensing air, gives it a heater pulse now and
// then. Skipped readings keep the previous value but are not counted in the daily summary.
void finishCellar(uint64_t upMs) {
  if (CELLAR_SENSOR != CELLAR_SHT || !shtStarted) return;
  float t = NAN, rh = NAN;
  uint8_t res = readSHT(t, rh);
  if (res == CS_TIMEOUT || res == CS_CHECKSUM) { shtAddr = 0; shtType = SHT_NONE; }   // detect again next time
  cellarResult(res, t, rh, upMs);
  if (res == CS_OK && rh >= SHT_HEAT_ABOVE_RH && (!shtLastHeatUpMs || upMs - shtLastHeatUpMs >= SHT_HEAT_INTERVAL_MS) &&
      shtHeat()) {
    shtLastHeatUpMs = upMs;
    shtSettleUntilUpMs = upMs + SHT_HEAT_SETTLE_MS;
    Serial.printf("%s: %.1f %% RH – heater pulse (%s), readings paused for %lu s\n", shtType == SHT_4X ? "SHT4x" : "SHT3x", rh,
                  shtType == SHT_4X ? "110 mW, 1 s" : "~10 s", (unsigned long)(SHT_HEAT_SETTLE_MS / 1000));
  }
}

// Inserts a day into the sorted RAM history, or replaces the same day. Caller holds bufMux.
void putCellarDay(const CellarDay &d) {
  if (!cellarDayCap) return;
  size_t i = cellarDayCount;
  while (i > 0 && cellarDays[i - 1].ymd > d.ymd) --i;
  if (i > 0 && cellarDays[i - 1].ymd == d.ymd) { cellarDays[i - 1] = d; return; }
  if (cellarDayCount == cellarDayCap) {
    if (i == 0) return;   // older than everything kept
    memmove(&cellarDays[0], &cellarDays[1], (i - 1) * sizeof(CellarDay));   // drop the oldest day
    cellarDays[i - 1] = d;
    return;
  }
  memmove(&cellarDays[i + 1], &cellarDays[i], (cellarDayCount - i) * sizeof(CellarDay));
  cellarDays[i] = d;
  ++cellarDayCount;
}

CellarDay summarizeCellar(const CellarAccum &a) {
  CellarDay d;
  d.seq = 0;
  d.ymd = a.ymd;
  d.tMin = tenths(a.tMin); d.tAvg = tenths(a.tSum / a.n); d.tMax = tenths(a.tMax);
  d.hMin = tenths(a.hMin); d.hAvg = tenths(a.hSum / a.n); d.hMax = tenths(a.hMax);
  d.ahAvg = tenths(a.ahSum / a.n);
  d.samples = (uint16_t)min<uint32_t>(a.n, 65535);
  return d;
}

// Closes a day: into the web history and into the buffer that the writer task appends to CELLAR.CSV.
void finishCellarDay(const CellarAccum &a) {
  if (!a.n) return;
  CellarDay d = summarizeCellar(a);
  portENTER_CRITICAL(&bufMux);
  bool lost = cellarRing.push(d);
  if (lost) storage.overflow = true;
  putCellarDay(d);
  uint32_t dropped = cellarRing.dropped;
  portEXIT_CRITICAL(&bufMux);
  if (lost) reportDropped("cellar days", dropped);
  Serial.printf("Cellar: day %08lu closed – %.1f / %.1f / %.1f °C, %.1f / %.1f / %.1f %% RH (min/avg/max), %u readings\n",
                (unsigned long)d.ymd, d.tMin / 10.0f, d.tAvg / 10.0f, d.tMax / 10.0f, d.hMin / 10.0f, d.hAvg / 10.0f,
                d.hMax / 10.0f, (unsigned)d.samples);
}

void saveCellarAccum() {
  prefs.putBytes("cellarAcc", &cellarAcc, sizeof(cellarAcc));
  cellarSinceSave = 0;
}

// Adds the current reading to the day in progress (only with a known clock; NOSYNC rows are skipped).
void accumulateCellar() {
  if (!CELLAR_ENABLED || !timeIsKnown()) return;
  uint32_t today = localYmd((uint32_t)time(nullptr));
  if (!cellarAccRestored) {
    cellarAccRestored = true;
    CellarAccum saved;
    if (prefs.getBytes("cellarAcc", &saved, sizeof(saved)) == sizeof(saved) && saved.version == CELLAR_ACC_VERSION) {
      if (saved.ymd == today) cellarAcc = saved;
      else if (saved.ymd && saved.ymd < today) finishCellarDay(saved);   // the logger was off at midnight
    }
  }
  if (cellarAcc.ymd != today) {
    // A new day. (A clock set back over midnight discards the day in progress instead.)
    if (cellarAcc.ymd && cellarAcc.ymd < today) finishCellarDay(cellarAcc);
    cellarAcc = CellarAccum();
    cellarAcc.ymd = today;
    saveCellarAccum();   // right away, so a restart can never close the same day twice
  }
  if (cellarValid && cellarFresh) {   // only new readings; a bridged or held value is not counted again
    CellarAccum &a = cellarAcc;
    if (!a.n) { a.tMin = a.tMax = cellarT; a.hMin = a.hMax = cellarRH; }
    a.tMin = min(a.tMin, cellarT); a.tMax = max(a.tMax, cellarT);
    a.hMin = min(a.hMin, cellarRH); a.hMax = max(a.hMax, cellarRH);
    a.tSum += cellarT; a.hSum += cellarRH; a.ahSum += absHumidity(cellarT, cellarRH);
    ++a.n;
  }
  if (++cellarSinceSave >= CELLAR_SAVE_SAMPLES) saveCellarAccum();
}

// ---------------- CSV formatting (same text as firmware 2.1) ----------------

void printTimeColumns(Print &p, uint32_t unixS, uint64_t upMs) {
  if (unixS) {
    time_t t = (time_t)unixS;
    struct tm lt;
    localtime_r(&t, &lt);
    char iso[40];
    strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%S%z", &lt);
    p.print(iso);
  }
  p.print(';');
  if (unixS) p.print(unixS);
  p.print(';'); p.print(upMs); p.print(';');
}
void printRow(Print &p, const LogRecord &r) {
  bool gv = r.flags & REC_GAS_VALID, wv = r.flags & REC_WATER_VALID;
  printTimeColumns(p, r.unixS, r.uptimeMs);
  if (gv) p.print(r.gas, 2);
  p.print(';');
  if (wv) p.print(r.water, 2);
  p.print(';'); p.print(gv ? 1 : 0);
  p.print(';'); p.print(wv ? 1 : 0);
  p.print(';');
  p.print(r.estimate == EST_UNKNOWN ? "UNKNOWN" : r.estimate == EST_HOT ? "HOT" : "COLD");
  p.print(';'); p.print(r.target, 1);
  bool cv = r.flags & REC_CELLAR_VALID;
  p.print(';');
  if (cv) p.print(r.cellarT, 1);
  p.print(';');
  if (cv) p.print(r.cellarRH, 1);
  p.print(';'); p.println(cv ? 1 : 0);
}
void printCellarDay(Print &p, const CellarDay &d) {
  char date[16];
  snprintf(date, sizeof(date), "%04lu-%02lu-%02lu", (unsigned long)(d.ymd / 10000), (unsigned long)(d.ymd / 100 % 100),
           (unsigned long)(d.ymd % 100));
  p.print(date);
  const int16_t v[] = {d.tMin, d.tAvg, d.tMax, d.hMin, d.hAvg, d.hMax, d.ahAvg};
  for (int16_t x : v) { p.print(';'); p.print(x / 10.0f, 1); }
  p.print(';'); p.println(d.samples);
}
void printEvent(Print &p, const EventRecord &e) {
  printTimeColumns(p, e.unixS, e.uptimeMs);
  p.print(e.type == EVENT_THERMAL_HOT ? "THERMAL_HOT" : "THERMAL_COLD"); p.print(';');
  if (e.flags & REC_GAS_VALID) p.print(e.gas, 2);
  p.print(';');
  if (e.flags & REC_WATER_VALID) p.print(e.water, 2);
  p.print(';');
  if (e.durationS >= 0) p.print((long)e.durationS);
  p.println();
}

// ---------------- SD writer task: all card writes, remounting and the archive ----------------

void setStorageFlags(bool writeFailed) {
  portENTER_CRITICAL(&bufMux);
  storage.writeFailed = writeFailed;
  if (writeFailed) ++storage.writeErrors;
  portEXIT_CRITICAL(&bufMux);
}

void sdFail(const char *step, const char *path) {
  Serial.printf("SD ERROR: %s failed for %s (errno %d: %s)\n", step, path, errno, strerror(errno));
}

// Undo the visible part of an append that failed earlier (see PendingAppend). Card lock held.
bool rollbackPendingAppend() {
  if (!pendingAppend.active) return true;
  struct stat st;
  if (stat(pendingAppend.path, &st) != 0) {
    if (errno == ENOENT) { pendingAppend.active = false; return true; }
    sdFail("stat (rollback)", pendingAppend.path);
    return false;
  }
  long extra = (long)st.st_size - pendingAppend.sizeBefore;
  uint64_t volume = SD.totalBytes();
  if (!volume) { sdFail("capacity (rollback)", pendingAppend.path); return false; }
  if (extra > pendingAppend.maxBytes || (pendingAppend.volumeBytes && volume != pendingAppend.volumeBytes)) {
    // The file grew by more than our failed write, or another card is inserted: these bytes are
    // not ours – leave the file alone.
    Serial.printf("SD: %s changed since the failed write (other card?) – no rollback\n", pendingAppend.path);
    pendingAppend.active = false;
    return true;
  }
  if (extra > 0) {
    if (truncate(pendingAppend.path, pendingAppend.sizeBefore) != 0) { sdFail("truncate (rollback)", pendingAppend.path); return false; }
    if (stat(pendingAppend.path, &st) != 0 || st.st_size != pendingAppend.sizeBefore) { sdFail("verify (rollback)", pendingAppend.path); return false; }
    Serial.printf("SD: removed an incomplete earlier write from %s (back to %ld bytes) – no duplicate rows\n",
                  pendingAppend.path, pendingAppend.sizeBefore);
  }
  pendingAppend.active = false;
  return true;
}

// Appends text to a CSV file and verifies every step. Card lock held.
// Returns true only when the data are on the card and the file has exactly the expected size.
bool appendVerified(const char *path, const char *header, const char *text, size_t len) {
  if (!rollbackPendingAppend()) return false;
  struct stat st;
  long before = 0;
  if (stat(path, &st) == 0) before = st.st_size;
  else if (errno != ENOENT) { sdFail("stat", path); return false; }
  pendingAppend.active = true;
  strlcpy(pendingAppend.path, path, sizeof(pendingAppend.path));
  pendingAppend.sizeBefore = before;
  pendingAppend.maxBytes = (long)(strlen(header) + 2 + len);
  pendingAppend.volumeBytes = SD.totalBytes();

  FILE *f = fopen(path, "a+");
  if (!f) { sdFail("open", path); return false; }
  size_t expected = 0, written = 0;
  if (before == 0) {
    size_t h = strlen(header);
    written += fwrite(header, 1, h, f); expected += h;
  } else {
    // A file cut off by an old power failure may end in the middle of a line: start a new line.
    int last = (fseek(f, -1, SEEK_END) == 0) ? fgetc(f) : EOF;
    if (fseek(f, 0, SEEK_END) != 0) { sdFail("seek", path); fclose(f); return false; }
    if (last != '\n') {
      Serial.printf("SD: %s did not end with a line break (interrupted write?) – starting a new line\n", path);
      written += fwrite("\r\n", 1, 2, f); expected += 2;
    }
  }
  written += fwrite(text, 1, len, f); expected += len;
  if (testFailWrites) {
    // Test hook: the rows ARE written, but the operation is reported as failed -> the retry must
    // cut them away again (rollback) and must not create duplicates.
    testFailWrites = testFailWrites - 1;
    fclose(f);
    errno = EIO;
    sdFail("write (SIMULATED by test command)", path);
    return false;
  }
  if (written != expected) { sdFail("write", path); fclose(f); return false; }
  if (fflush(f) != 0) { sdFail("flush", path); fclose(f); return false; }
  if (fsync(fileno(f)) != 0) { sdFail("fsync", path); fclose(f); return false; }
  if (fclose(f) != 0) { sdFail("close", path); return false; }
  if (stat(path, &st) != 0 || st.st_size != (long)(before + (long)expected)) {
    Serial.printf("SD ERROR: %s has %ld bytes, expected %ld\n", path, (long)st.st_size, (long)(before + (long)expected));
    return false;
  }
  pendingAppend.active = false;
  return true;
}

// Writes everything that is in the RAM buffers now. Rows are removed from RAM chunk by chunk,
// each time only after the chunk was verifiably written. Returns false on the first failure.
bool flushBuffers(bool &busy) {
  busy = false;
  uint32_t untilRow, untilEvent, untilCellar;
  portENTER_CRITICAL(&bufMux);
  untilRow = logRing.nextSeq - 1;
  untilEvent = eventRing.nextSeq - 1;
  untilCellar = cellarRing.nextSeq - 1;
  portEXIT_CRITICAL(&bufMux);
  uint32_t t0 = millis();
  size_t rows = 0, events = 0, days = 0, bytes = 0, files = 0;
  char path[40];

  for (;;) {   // daily rows
    size_t n = 0;
    portENTER_CRITICAL(&bufMux);
    while (n < logRing.count && n < WRITE_CHUNK_RECORDS && logRing.at(n).seq <= untilRow) { chunkRows[n] = logRing.at(n); ++n; }
    portEXIT_CRITICAL(&bufMux);
    if (!n) break;
    // One chunk = one daily file: stop at midnight (or when the clock became known).
    uint32_t ymd = localYmd(chunkRows[0].unixS);
    TextBuffer tb(writeText, sizeof(writeText));
    size_t k = 0;
    for (; k < n && localYmd(chunkRows[k].unixS) == ymd; ++k) {
      size_t mark = tb.length();
      printRow(tb, chunkRows[k]);
      if (tb.overflow()) { tb.cutTo(mark); break; }
    }
    if (!k) { Serial.println("SD ERROR: row does not fit into the write buffer"); return false; }
    dailyPath(ymd, path, sizeof(path));
    {
      SdLock lock(SD_TASK_WAIT_MS);
      if (!lock.ok) { busy = true; return false; }
      if (!appendVerified(path, LOG_HEADER, writeText, tb.length())) return false;
    }
    portENTER_CRITICAL(&bufMux);
    logRing.commit(chunkRows[k - 1].seq);
    portEXIT_CRITICAL(&bufMux);
    rows += k; bytes += tb.length(); ++files;
    vTaskDelay(1);
  }

  for (;;) {   // phase events
    size_t n = 0;
    portENTER_CRITICAL(&bufMux);
    while (n < eventRing.count && n < WRITE_CHUNK_RECORDS && eventRing.at(n).seq <= untilEvent) { chunkEvents[n] = eventRing.at(n); ++n; }
    portEXIT_CRITICAL(&bufMux);
    if (!n) break;
    TextBuffer tb(writeText, sizeof(writeText));
    size_t k = 0;
    for (; k < n; ++k) {
      size_t mark = tb.length();
      printEvent(tb, chunkEvents[k]);
      if (tb.overflow()) { tb.cutTo(mark); break; }
    }
    snprintf(path, sizeof(path), "%s/EVENTS.CSV", SD_MOUNT);
    {
      SdLock lock(SD_TASK_WAIT_MS);
      if (!lock.ok) { busy = true; return false; }
      if (!appendVerified(path, EVENT_HEADER, writeText, tb.length())) return false;
    }
    portENTER_CRITICAL(&bufMux);
    eventRing.commit(chunkEvents[k - 1].seq);
    portEXIT_CRITICAL(&bufMux);
    events += k; bytes += tb.length(); ++files;
  }

  {   // finished cellar days (one row per day, at most a few rows)
    size_t n = 0;
    portENTER_CRITICAL(&bufMux);
    while (n < cellarRing.count && n < CELLAR_BUFFER_RECORDS && cellarRing.at(n).seq <= untilCellar) { chunkCellar[n] = cellarRing.at(n); ++n; }
    portEXIT_CRITICAL(&bufMux);
    if (n) {
      TextBuffer tb(writeText, sizeof(writeText));
      for (size_t k = 0; k < n; ++k) printCellarDay(tb, chunkCellar[k]);
      snprintf(path, sizeof(path), "%s/CELLAR.CSV", SD_MOUNT);
      {
        SdLock lock(SD_TASK_WAIT_MS);
        if (!lock.ok) { busy = true; return false; }
        if (!appendVerified(path, CELLAR_HEADER, writeText, tb.length())) return false;
      }
      portENTER_CRITICAL(&bufMux);
      cellarRing.commit(chunkCellar[n - 1].seq);
      portEXIT_CRITICAL(&bufMux);
      days += n; bytes += tb.length(); ++files;
    }
  }

  portENTER_CRITICAL(&bufMux);
  storage.lastWriteUpMs = uptimeMs();
  storage.lastWriteUnix = timeIsKnown() ? (uint32_t)time(nullptr) : 0;
  storage.writeFailed = false;
  storage.overflow = false;
  portEXIT_CRITICAL(&bufMux);
  if (rows || events || days)
    Serial.printf("SD: saved %u rows + %u events + %u cellar days (%u appends, %u B) in %lu ms | heap free %u, largest block %u\n",
                  (unsigned)rows, (unsigned)events, (unsigned)days, (unsigned)files, (unsigned)bytes, (unsigned long)(millis() - t0),
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
  return true;
}

// Reads total/used bytes of the FAT volume. Card lock held.
void updateSpace() {
  uint64_t total = SD.totalBytes();
  uint64_t used = total ? SD.usedBytes() : 0;
  portENTER_CRITICAL(&bufMux);
  storage.totalB = total;
  storage.usedB = used;
  storage.spaceKnown = total > 0;
  portEXIT_CRITICAL(&bufMux);
}
// Free space in tenths of a percent; -1 when unknown. Honours the Serial test override.
int freePermille() {
  if (testFreePercent >= 0) return testFreePercent * 10;
  portENTER_CRITICAL(&bufMux);
  uint64_t total = storage.totalB, used = storage.usedB;
  bool known = storage.spaceKnown;
  portEXIT_CRITICAL(&bufMux);
  if (!known || !total) return -1;
  return (int)((total - used) * 1000ULL / total);
}

// Scans /LOGS: statistics for the web page and the oldest files that may be deleted
// (dated strictly before deleteBeforeYmd; 0 = collect nothing). Card lock held.
bool scanLogs(LogScan &s, uint32_t deleteBeforeYmd) {
  char dirPath[16];
  snprintf(dirPath, sizeof(dirPath), "%s/LOGS", SD_MOUNT);
  DIR *dir = opendir(dirPath);
  if (!dir) { sdFail("opendir", dirPath); return false; }
  struct dirent *de;
  uint32_t seen = 0;
  while ((de = readdir(dir)) != nullptr) {
    if (de->d_type == DT_DIR) continue;
    if (strcmp(de->d_name, "NOSYNC.CSV") == 0) { s.nosync = true; continue; }
    uint32_t ymd = parseDailyName(de->d_name);
    if (!ymd) continue;   // unknown files are ignored and never deleted
    ++s.dailyFiles;
    if (!s.oldestYmd || ymd < s.oldestYmd) s.oldestYmd = ymd;
    if (ymd > s.newestYmd) s.newestYmd = ymd;
    if (ymd < deleteBeforeYmd) {
      // keep the ARCHIVE_CANDIDATES oldest, sorted ascending (insertion sort, fixed array)
      size_t pos = s.nCand;
      while (pos > 0 && s.cand[pos - 1] > ymd) --pos;
      if (pos < ARCHIVE_CANDIDATES) {
        size_t last = s.nCand < ARCHIVE_CANDIDATES ? s.nCand : ARCHIVE_CANDIDATES - 1;
        for (size_t i = last; i > pos; --i) s.cand[i] = s.cand[i - 1];
        s.cand[pos] = ymd;
        if (s.nCand < ARCHIVE_CANDIDATES) ++s.nCand;
      }
    }
    if (++seen % 32 == 0) vTaskDelay(1);
  }
  closedir(dir);
  return true;
}

// Oldest day that must never be deleted: today minus SD_RETENTION_DAYS, and nothing that still has
// rows waiting in RAM or an unfinished append. Returns 0 (= delete nothing) without NTP time.
uint32_t deletionLimitYmd() {
  if (!ntpSynced || !timeIsKnown()) return 0;
  uint32_t today = localYmd((uint32_t)time(nullptr));
  int32_t limitDays = ymdToDays(today) - (int32_t)SD_RETENTION_DAYS;   // files with day < limit are older than 365 days
  uint32_t oldestBuffered = 0;
  portENTER_CRITICAL(&bufMux);
  for (size_t i = 0; i < logRing.count; ++i) {
    uint32_t u = logRing.at(i).unixS;
    if (u && (!oldestBuffered || u < oldestBuffered)) oldestBuffered = u;
  }
  portEXIT_CRITICAL(&bufMux);
  if (oldestBuffered) {
    int32_t d = ymdToDays(localYmd(oldestBuffered));
    if (d < limitDays) limitDays = d;
  }
  // back from days to YYYYMMDD: walk with localYmd() at noon UTC of that day (no TZ ambiguity)
  return localYmd((uint32_t)((int64_t)limitDays * 86400 + 43200));
}

// Archive maintenance: measures free space, refreshes the file statistics and, if needed, deletes
// the oldest daily files. Never touches anything but /LOGS/YYYYMMDD.CSV older than the retention.
void archiveMaintenance() {
  LogScan scan;
  uint32_t limitYmd = deletionLimitYmd();
  {
    SdLock lock(SD_TASK_WAIT_MS);
    if (!lock.ok) return;
    updateSpace();
    if (!scanLogs(scan, limitYmd)) return;
  }
  int fp = freePermille();
  bool lowSpace = fp >= 0 && fp < SD_MIN_FREE_PERCENT * 10;
  portENTER_CRITICAL(&bufMux);
  bool alreadyPaused = storage.noSpace;   // avoid repeating the same messages every minute
  portEXIT_CRITICAL(&bufMux);
  if (lowSpace || (SD_PRUNE_EXPIRED_ALWAYS && scan.nCand)) {
    if (lowSpace && (!alreadyPaused || scan.nCand))
      Serial.printf("SD: free space %.1f %% < %u %% – archive cleanup (%u deletable file(s) found)\n", fp / 10.0f,
                    SD_MIN_FREE_PERCENT, (unsigned)scan.nCand);
    if (lowSpace && !limitYmd && !alreadyPaused) Serial.println("SD: clock not confirmed by NTP – file age unknown, nothing is deleted");
    portENTER_CRITICAL(&bufMux);
    storage.cleanup = true;
    portEXIT_CRITICAL(&bufMux);
    for (int round = 0; round < 32 && scan.nCand; ++round) {
      bool stop = false;
      for (size_t i = 0; i < scan.nCand; ++i) {
        fp = freePermille();
        if (!SD_PRUNE_EXPIRED_ALWAYS && fp >= SD_TARGET_FREE_PERCENT * 10) { stop = true; break; }
        char path[40];
        dailyPath(scan.cand[i], path, sizeof(path));
        if (pendingAppend.active && strcmp(path, pendingAppend.path) == 0) continue;
        SdLock lock(SD_TASK_WAIT_MS);
        if (!lock.ok) { stop = true; break; }
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        uint64_t usedBefore = SD.usedBytes();
        if (unlink(path) != 0) { sdFail("delete", path); stop = true; break; }
        updateSpace();
        uint64_t usedAfter = SD.usedBytes();
        uint64_t freed = usedBefore > usedAfter ? usedBefore - usedAfter : 0;
        portENTER_CRITICAL(&bufMux);
        ++storage.deletedFiles;
        portEXIT_CRITICAL(&bufMux);
        Serial.printf("ARCHIVE: deleted %s (%ld B, older than %u days), freed %llu B, free now %.1f %%\n",
                      path, (long)st.st_size, SD_RETENTION_DAYS, (unsigned long long)freed, freePermille() / 10.0f);
        if (st.st_size > 0 && freed == 0) Serial.println("ARCHIVE WARNING: free space did not increase after deleting the file");
        vTaskDelay(1);
      }
      if (stop || scan.nCand < ARCHIVE_CANDIDATES) break;
      scan = LogScan();   // more old files may exist: scan again
      SdLock lock(SD_TASK_WAIT_MS);
      if (!lock.ok || !scanLogs(scan, limitYmd)) break;
    }
    portENTER_CRITICAL(&bufMux);
    storage.cleanup = false;
    portEXIT_CRITICAL(&bufMux);
    scan = LogScan();   // final statistics
    SdLock lock(SD_TASK_WAIT_MS);
    if (lock.ok) updateSpace();
    if (!lock.ok || !scanLogs(scan, 0)) return;
  }
  fp = freePermille();
  bool noSpace = fp >= 0 && fp < SD_MIN_FREE_PERCENT * 10;
  portENTER_CRITICAL(&bufMux);
  bool wasNoSpace = storage.noSpace;
  storage.noSpace = noSpace;
  storage.dailyFiles = scan.dailyFiles;
  storage.nosyncFile = scan.nosync;
  storage.oldestYmd = scan.oldestYmd;
  storage.newestYmd = scan.newestYmd;
  portEXIT_CRITICAL(&bufMux);
  if (noSpace && !wasNoSpace)
    Serial.printf("SD ERROR: insufficient space (%.1f %% free) and no file older than %u days to delete – writing PAUSED, data stay in RAM\n",
                  fp / 10.0f, SD_RETENTION_DAYS);
  if (!noSpace && wasNoSpace) Serial.println("SD: enough free space again – writing resumed");
}

// Reads the daily cellar summaries from CELLAR.CSV into the web history (once, after the card is
// mounted). Lines that do not parse (header, a line cut off by a power failure) are skipped.
// Card lock held.
bool loadCellarSummary() {
  char path[24];
  snprintf(path, sizeof(path), "%s/CELLAR.CSV", SD_MOUNT);
  FILE *f = fopen(path, "r");
  if (!f) {
    if (errno == ENOENT) return true;   // no finished day yet
    sdFail("open", path);
    return false;
  }
  char line[112];
  unsigned loaded = 0;
  while (fgets(line, sizeof(line), f)) {
    unsigned y, m, dd, samples;
    float v[7];
    if (sscanf(line, "%4u-%2u-%2u;%f;%f;%f;%f;%f;%f;%f;%u", &y, &m, &dd, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6],
               &samples) != 11 || y < 2000 || y > 2099 || m < 1 || m > 12 || dd < 1 || dd > 31)
      continue;
    CellarDay d;
    d.seq = 0;
    d.ymd = y * 10000 + m * 100 + dd;
    d.tMin = tenths(v[0]); d.tAvg = tenths(v[1]); d.tMax = tenths(v[2]);
    d.hMin = tenths(v[3]); d.hAvg = tenths(v[4]); d.hMax = tenths(v[5]);
    d.ahAvg = tenths(v[6]);
    d.samples = (uint16_t)min(samples, 65535u);
    portENTER_CRITICAL(&bufMux);
    putCellarDay(d);
    portEXIT_CRITICAL(&bufMux);
    if (++loaded % 32 == 0) vTaskDelay(1);
  }
  bool ok = !ferror(f);
  fclose(f);
  if (!ok) { sdFail("read", path); return false; }
  Serial.printf("Cellar: %u daily summaries loaded from CELLAR.CSV\n", loaded);
  return true;
}

void setCardState(bool ok) {
  sdOK = ok;
  if (!ok) {
    portENTER_CRITICAL(&bufMux);
    storage.spaceKnown = false;
    portEXIT_CRITICAL(&bufMux);
  }
}

// The writer task runs on the same core and priority as loop(), so FreeRTOS time slicing keeps the
// measurement and the web server running even while a slow card operation is in progress.
void sdWriterTask(void *) {
  uint32_t lastMountMs = millis(), lastProbeMs = millis(), lastAttemptMs = millis();
  uint32_t lastFlushOkMs = millis();
  bool retry = false;
  bool maintenanceDue = sdOK;
  bool ntpLogged = false;
  bool cellarLoaded = !CELLAR_ENABLED;
  uint32_t lastCellarLoadMs = millis() - SD_RETRY_MS;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));   // wake every second or on "write now"
    uint32_t now = millis();
    if (ntpSynced && !ntpLogged) { ntpLogged = true; Serial.println("Time synchronised by NTP – archive deletion enabled"); }

    if (!sdOK) {
      if ((uint32_t)(now - lastMountMs) < SD_RETRY_MS) continue;
      lastMountMs = now;
      bool ok;
      { SdLock lock(SD_TASK_WAIT_MS); if (!lock.ok) continue; ok = mountSD(); }
      setCardState(ok);
      Serial.printf("SD remount: %s\n", ok ? "OK" : "ERROR (card missing or not responding)");
      if (!ok) continue;
      maintenanceDue = true;
      lastProbeMs = now;
    } else if ((uint32_t)(now - lastProbeMs) >= SD_PROBE_MS) {
      // Read-only presence check (sector 0), detects a removed card between writes.
      lastProbeMs = now;
      bool ok = true;
      { SdLock lock(SD_TASK_WAIT_MS); if (lock.ok) ok = SD.readRAW(probeSector, 0); }
      if (!ok) {
        Serial.println("SD ERROR: card does not respond (removed?) – data stay in RAM, remount every 60 s");
        setCardState(false);
        lastMountMs = now;
        continue;
      }
    }
    if (!cellarLoaded && (uint32_t)(now - lastCellarLoadMs) >= SD_RETRY_MS) {
      lastCellarLoadMs = now;
      SdLock lock(SD_TASK_WAIT_MS);
      if (lock.ok) cellarLoaded = loadCellarSummary();
    }

    portENTER_CRITICAL(&bufMux);
    size_t buffered = logRing.count + eventRing.count + cellarRing.count;
    portEXIT_CRITICAL(&bufMux);
    bool flushNow = buffered && (flushRequested || (uint32_t)(now - lastFlushOkMs) >= SD_FLUSH_MS ||
                                 (retry && (uint32_t)(now - lastAttemptMs) >= SD_RETRY_MS));
    if (!buffered) flushRequested = false;
    if (maintenanceDue || flushNow) { archiveMaintenance(); maintenanceDue = false; }
    if (!flushNow) continue;

    flushRequested = false;
    lastAttemptMs = now;
    portENTER_CRITICAL(&bufMux);
    bool noSpace = storage.noSpace;
    portEXIT_CRITICAL(&bufMux);
    if (noSpace) { retry = true; continue; }   // paused: check the space again in a minute
    bool busy = false;
    if (flushBuffers(busy)) {
      lastFlushOkMs = millis();
      retry = false;
      portENTER_CRITICAL(&bufMux);
      storage.lastFlushUpMs = uptimeMs();
      portEXIT_CRITICAL(&bufMux);
    } else if (busy) {
      retry = true;   // a long download held the card; nothing failed
    } else {
      // The rows stay in RAM. Remount the card and retry in SD_RETRY_MS; an unfinished append is
      // rolled back first (PendingAppend), so the retry cannot duplicate rows.
      retry = true;
      setStorageFlags(true);
      setCardState(false);
      lastMountMs = now;
      Serial.printf("SD: write failed – %u records kept in RAM, retry in %lu s\n", (unsigned)buffered,
                    (unsigned long)(SD_RETRY_MS / 1000));
    }
  }
}

void pushStart(uint64_t ms) {
  starts[startWrite] = ms;
  startWrite = (startWrite + 1) % MAX_STARTS;
  if (startCount < MAX_STARTS) ++startCount;
}
void pushCycle(uint64_t ms, uint32_t hotS, uint32_t coldS) {
  cycles[cycleWrite] = {ms, hotS, coldS};
  cycleWrite = (cycleWrite + 1) % MAX_CYCLES;
  if (cycleCount < MAX_CYCLES) ++cycleCount;
}

void clearRise() {
  riseFromMs = aboveNoiseFromMs = 0;
  riseHits = 0;
}

void resetEstimator() {
  estimatedKnown = false;
  hotCandidateMs = coldCandidateMs = 0;
  hotFromMs = coldFromMs = 0;
  gasInvalidFromMs = 0;
  clearRise();
}

// Cold phase only; called before the current reading is added to riseWindow.
void detectRise(uint64_t ms) {
  if (riseCount < 3) return;
  if (riseFromMs) {
    // A rise that falls back, or that the hot threshold does not follow in time, was not a start.
    if (gasC <= riseBaseC + RISE_NOISE_C || ms - riseFromMs > RISE_VERIFY_MS) {
      Serial.printf("Estimate: rise from %.2f °C not confirmed by the hot threshold – no start\n", riseBaseC);
      clearRise();
    }
    return;
  }
  float base = riseWindow[0];
  for (size_t i = 1; i < riseCount; ++i) base = min(base, riseWindow[i]);
  if (gasC <= base + RISE_NOISE_C) { aboveNoiseFromMs = 0; riseHits = 0; return; }
  // The climb begins at the latest reading that was not rising (a flat step just above the baseline,
  // e.g. after the pipe cooled down, does not move the start back in time).
  float prev = riseWindow[(riseWrite + RISE_WINDOW_SAMPLES - 1) % RISE_WINDOW_SAMPLES];
  if (!aboveNoiseFromMs || (!riseHits && gasC <= prev)) aboveNoiseFromMs = ms;
  riseHits = gasC >= base + RISE_START_C ? riseHits + 1 : 0;
  if (riseHits >= RISE_CONFIRM_SAMPLES) {
    riseFromMs = aboveNoiseFromMs;
    riseBaseC = base;
    Serial.printf("Estimate: pipe rising from %.2f to %.2f °C – start expected, waiting for the hot threshold\n", base, gasC);
  }
}

void pushRiseWindow(float v) {
  riseWindow[riseWrite] = v;
  riseWrite = (riseWrite + 1) % RISE_WINDOW_SAMPLES;
  if (riseCount < RISE_WINDOW_SAMPLES) ++riseCount;
}

void processEstimate(uint64_t ms) {
  if (!gasValid) {
    // A short reading dropout (e.g. MAX6675 during defrosting) does not reset the estimate.
    hotCandidateMs = coldCandidateMs = 0;
    if (!gasInvalidFromMs) gasInvalidFromMs = ms;
    else if (ms - gasInvalidFromMs >= SENSOR_LOSS_RESET_MS) resetEstimator();
    return;
  }
  gasInvalidFromMs = 0;
  updateEstimate(ms);
  pushRiseWindow(gasC);
}

void updateEstimate(uint64_t ms) {
  if (!estimatedKnown) {
    // The phase was already in progress and its start is unknown -> not counted as a start or in averages.
    hotCandidateMs = coldCandidateMs = 0;
    clearRise();
    if (gasC >= hotThresholdC) {
      estimatedHot = true; estimatedKnown = true;
      hotFromMs = 0;
    } else if (gasC <= coldThresholdC) {
      estimatedHot = false; estimatedKnown = true;
      coldFromMs = 0;
    }
    return;
  }
  if (!estimatedHot) {
    coldCandidateMs = 0;
    detectRise(ms);
    if (gasC >= hotThresholdC) {
      if (!hotCandidateMs) hotCandidateMs = ms;
      if (ms - hotCandidateMs >= HOT_CONFIRM_MS) {
        estimatedHot = true;
        // The start is dated to the beginning of the rise when one was seen, otherwise to the threshold.
        hotFromMs = riseFromMs && riseFromMs > coldFromMs ? riseFromMs : hotCandidateMs;
        clearRise();
        lastColdS = coldFromMs ? (uint32_t)((hotFromMs - coldFromMs) / 1000U) : 0;
        hotCandidateMs = 0;
        pushStart(hotFromMs);
        logEvent("THERMAL_HOT", coldFromMs ? (long)lastColdS : -1);
      }
    } else hotCandidateMs = 0;
  } else {
    hotCandidateMs = 0;
    if (gasC <= coldThresholdC) {
      if (!coldCandidateMs) coldCandidateMs = ms;
      if (ms - coldCandidateMs >= COLD_CONFIRM_MS) {
        estimatedHot = false;
        coldFromMs = coldCandidateMs;
        coldCandidateMs = 0;
        clearRise();
        if (hotFromMs) {
          lastHotS = (uint32_t)((coldFromMs - hotFromMs) / 1000U);
          pushCycle(coldFromMs, lastHotS, lastColdS);
          logEvent("THERMAL_COLD", (long)lastHotS);
        } else {
          logEvent("THERMAL_COLD", -1);
        }
      }
    } else coldCandidateMs = 0;
  }
}

void pushGraph(uint32_t ms) {
  if (graphCount == 0 || (uint32_t)(ms - lastGraphMs) >= GRAPH_MS) {
    history[graphWrite] = {graphTime(), gasValid ? gasC : NAN, waterValid ? waterC : NAN,
                           cellarValid ? tenths(cellarT) : NO_TENTHS, cellarValid ? tenths(cellarRH) : NO_TENTHS};
    graphWrite = (graphWrite + 1) % GRAPH_POINTS;
    if (graphCount < GRAPH_POINTS) ++graphCount;
    lastGraphMs = ms;
  }
}

// If no DS18B20 was found at boot (or it was connected later), search the bus again.
void ensureDS18B20() {
  if (dallas.getDeviceCount() > 0) return;
  dallas.begin();
  dallas.setResolution(12);
  dallas.setWaitForConversion(false);
}

// Called from loop(); the DS18B20 conversion runs in the background (12 bit ~750 ms).
void beginSample(uint32_t ms) {
  gasC = readMAX6675();
  gasValid = isfinite(gasC) && gasC >= 0.0f && gasC < 1024.0f;
  ensureDS18B20();
  dsPresent = dallas.getDeviceCount() > 0;
  if (dsPresent) dallas.requestTemperatures();
  sampleStartedMs = ms;
  sampleStartedUpMs = uptimeMs();
  startCellar(sampleStartedUpMs);   // AM2301 ~6 ms / SHT starts measuring, while the DS18B20 converts
  samplePending = true;
}
void finishSample(uint32_t ms) {
  waterC = dsPresent ? dallas.getTempCByIndex(0) : NAN;
  // 85.00 °C is the DS18B20 power-on value without a completed conversion – an error for heating water.
  waterValid = isfinite(waterC) && waterC > -55.0f && waterC <= 125.0f &&
               waterC != DEVICE_DISCONNECTED_C && waterC != 85.0f;
  processEstimate(uptimeMs());
  finishCellar(sampleStartedUpMs);
  bufferSample(sampleStartedUpMs);   // RAM only; the writer task saves it within 10 minutes
  accumulateCellar();
  pushGraph(ms);
  portENTER_CRITICAL(&bufMux);
  size_t buffered = logRing.count;
  portEXIT_CRITICAL(&bufMux);
  Serial.printf("Gas: %s%.2f °C | water: %s%.2f °C | SD: %s | buffer: %u | estimate: %s\n",
                gasValid ? "" : "ERR ", gasValid ? gasC : 0.0f,
                waterValid ? "" : "ERR ", waterValid ? waterC : 0.0f,
                sdOK ? "OK" : "NONE", (unsigned)buffered, !estimatedKnown ? "?" : estimatedHot ? "HOT" : riseFromMs ? "COLD, RISING" : "COLD");
  if (CELLAR_ENABLED) {
    if (cellarValid) Serial.printf("  cellar: %.1f °C, %.1f %% RH (raw %.1f °C, %.1f %% RH; failed readings since boot: %lu)\n",
                                   cellarT, cellarRH, cellarRawT, cellarRawRH, (unsigned long)cellarErrors);
    else if (cellarLastError == CS_OK) Serial.println("  cellar: waiting for the first reading");
    else if (CELLAR_SENSOR == CELLAR_SHT)
      Serial.printf("  cellar: ERR %s – check the SHT sensor: SDA on GPIO%d, SCL on GPIO%d, 3V3, GND\n", CELLAR_ERROR_NAMES[cellarLastError],
                    PIN_CELLAR, PIN_CELLAR_SCL);
    else Serial.printf("  cellar: ERR %s – check the AM2301 on GPIO%d and its pull-up\n", CELLAR_ERROR_NAMES[cellarLastError], PIN_CELLAR);
  }
  samplePending = false;
}

Stats getStats() {
  Stats s;
  uint64_t nowMs = uptimeMs();
  for (size_t i = 0; i < startCount; ++i) {
    uint64_t age = nowMs - starts[i];
    if (age < 3600000ULL) s.starts1h++;
    if (age < 86400000ULL) s.starts24h++;
  }
  for (size_t i = 0; i < cycleCount; ++i) {
    const Cycle &c = cycles[i];
    if (nowMs - c.endedMs < 86400000ULL) {
      ++s.completed24h;
      s.totalHotS += c.hotSeconds;
      ++s.runs[runClass(c.hotSeconds)];
      if (c.coldSeconds) {
        ++s.coldKnown24h;
        s.totalColdS += c.coldSeconds;
      }
    }
  }
  return s;
}

// The whole page (HTML/CSS/JS) is stored on the ESP32; no cloud or CDN needed.
const char PAGE[] PROGMEM = R"PAGE(
<!doctype html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>HeatPulse</title><style>
body{font:15px system-ui,Arial,sans-serif;background:#f3f6f9;color:#213245;margin:0;padding:15px} main{max-width:1020px;margin:auto}
h1{font-size:25px;margin:8px 0}.card{background:#fff;border:1px solid #dce5ec;border-radius:12px;padding:16px;margin:12px 0}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:12px}.tile{background:#f4f7fa;border-radius:10px;padding:12px}
.val{font-size:27px;font-weight:750;margin-top:4px}small,.muted{color:#647486}.legend{margin:8px 0}
.dot{display:inline-block;width:11px;height:11px;border-radius:50%;margin-right:6px}button,a.btn{border:0;border-radius:8px;background:#146eb5;color:white;padding:9px 13px;display:inline-block;text-decoration:none;cursor:pointer}
input{width:83px;padding:7px;border:1px solid #b8c7d2;border-radius:6px}label{display:inline-block;margin:8px 14px 8px 0}
canvas{width:100%;height:330px;display:block}.r0{background:#fdecea}.r1{background:#fff4e0}.r2{background:#e9f6ee}.r3{background:#d9f0e2}
.q0{color:#c0392b}.q1{color:#a8650a}.q2{color:#1e8449}.q3{color:#145a32}a{color:#146eb5}#msg{color:#13713a}.file{padding:6px 0;border-top:1px solid #eff2f4}
.sm{font-size:18px}.badge{display:inline-block;padding:2px 10px;border-radius:9px;font-weight:700;margin-left:8px}
.s-OK{background:#e9f6ee;color:#1e8449}.s-WARNING{background:#fff4e0;color:#a8650a}.s-CLEANUP{background:#e6f0fa;color:#146eb5}.s-ERROR{background:#fdecea;color:#c0392b}
.bar{height:10px;background:#e8eef3;border-radius:5px;overflow:hidden;margin:10px 0}.bar div{height:100%;width:0;background:#146eb5}
canvas.ch{height:230px;margin-top:6px}select{padding:6px;border:1px solid #b8c7d2;border-radius:6px;margin-left:8px}
.tip{position:absolute;display:none;pointer-events:none;background:#fff;border:1px solid #b8c7d2;border-radius:8px;padding:6px 9px;font-size:13px;line-height:1.5;box-shadow:0 2px 8px #0002;white-space:nowrap;z-index:5}
.guide{position:absolute;display:none;pointer-events:none;width:1px;background:#8a9aa9;z-index:4}
</style></head><body><main><h1>HeatPulse – heat pump temperature logger</h1>
<div class="card grid">
<div class="tile"><small>Refrigerant pipe (type K)</small><div id="gas" class="val">--</div></div>
<div class="tile"><small>Outlet water (DS18B20)</small><div id="water" class="val">--</div></div>
<div class="tile"><small>Water − target</small><div id="delta" class="val">--</div></div>
<div class="tile"><small>Thermal phase estimate</small><div id="estimate" class="val">--</div></div>
</div>
<div class="card"><b>History up to 24 h (1 point/min)</b><div class="legend"><span class="dot" style="background:#dc703b"></span>Refrigerant pipe &nbsp; <span class="dot" style="background:#167db4"></span>Heating water</div><canvas id="plot"></canvas><small id="graphInfo">Loading…</small></div>
<div class="card" id="cellarCard" hidden><b>Room around the heat pump (<span id="cSensor">--</span>)</b>
<div class="grid" style="margin-top:10px">
<div class="tile"><small>Temperature</small><div id="cT" class="val">--</div><small id="cTr"></small></div>
<div class="tile"><small>Relative humidity</small><div id="cH" class="val">--</div><small id="cHr"></small></div>
<div class="tile"><small>Dew point</small><div id="cD" class="val">--</div><small>surfaces colder than this get wet</small></div>
<div class="tile"><small>Absolute humidity</small><div id="cA" class="val">--</div><small>water in the air, independent of temperature</small></div>
</div><p class="muted" id="cErr"></p>
<b>History up to 24 h</b><div class="legend"><span class="dot" style="background:#8e44ad"></span>Temperature (left axis) &nbsp; <span class="dot" style="background:#16a085"></span>Relative humidity (right axis)</div>
<canvas id="cPlot" class="ch"></canvas><small id="cInfo"></small>
<p><b>Daily trend</b><select id="cRange"><option value="30">30 days</option><option value="90">90 days</option><option value="366" selected>1 year</option></select></p>
<div class="legend">Band = daily minimum–maximum, line = daily average.</div>
<div class="legend"><span class="dot" style="background:#8e44ad"></span>Temperature °C</div><canvas id="cDayT" class="ch"></canvas>
<div class="legend"><span class="dot" style="background:#16a085"></span>Relative humidity % (left axis) &nbsp; <span class="dot" style="background:#c0781a"></span>Absolute humidity g/m³ (right axis)</div><canvas id="cDayH" class="ch"></canvas>
<small id="cDayInfo">Loading…</small>
<p><a class="btn" href="/cellar.csv">Download daily summary</a></p>
<small>When the room gets warmer, relative humidity drops even with the same amount of water in the air – compare absolute humidity to see whether the room is really drier. Daily summaries are stored on the card in CELLAR.CSV; the 10 s values are in the daily CSV files.</small></div>
<div class="card grid">
<div class="tile"><small>Estimated starts / 1 h</small><div class="val" id="oneh">--</div></div>
<div class="tile"><small>Estimated starts / 24 h</small><div class="val" id="day">--</div></div>
<div class="tile"><small>Average hot phase / 24 h</small><div class="val" id="avg">--</div><small id="avgq"></small></div>
<div class="tile"><small>Average cold phase / 24 h</small><div class="val" id="avgcold">--</div></div>
<div class="tile"><small>Last hot phase</small><div class="val" id="last">--</div><small id="lastq"></small></div>
</div>
<div class="card"><b>Run length rating (last 24 h)</b><p class="muted">Number of estimated compressor runs (hot phases) by length. Runs under 6 minutes waste energy on every start and wear the compressor.</p>
<div class="grid">
<div class="tile r0"><small>Under 6 min · poor</small><div class="val" id="r0">--</div></div>
<div class="tile r1"><small>6–10 min · acceptable</small><div class="val" id="r1">--</div></div>
<div class="tile r2"><small>10–20 min · good</small><div class="val" id="r2">--</div></div>
<div class="tile r3"><small>20 min and more · ideal</small><div class="val" id="r3">--</div></div>
</div></div>
<div class="card"><b>Storage (microSD)</b><span id="stState" class="badge">--</span><p class="muted" id="stReason"></p>
<div class="bar"><div id="stBar"></div></div>
<div class="grid">
<div class="tile"><small>Capacity</small><div class="val sm" id="stTotal">--</div></div>
<div class="tile"><small>Used</small><div class="val sm" id="stUsed">--</div></div>
<div class="tile"><small>Free</small><div class="val sm" id="stFree">--</div></div>
<div class="tile"><small>Usage</small><div class="val sm" id="stPct">--</div></div>
<div class="tile"><small>Daily CSV files</small><div class="val sm" id="stFiles">--</div></div>
<div class="tile"><small>Oldest / newest day</small><div class="val sm" id="stRange">--</div></div>
<div class="tile"><small>Last successful write</small><div class="val sm" id="stLast">--</div></div>
<div class="tile"><small>Unsaved records in RAM</small><div class="val sm" id="stBuf">--</div><small id="stNext"></small></div>
</div>
<p><b>Circular archive:</b> <span id="stArch">--</span></p>
<p><button type="button" id="flushBtn">Write buffer to microSD now</button> <span id="flushMsg" class="muted"></span></p>
<small>Measurements are kept in RAM and written to the card every 10 minutes. A power cut loses at most the last ~10 minutes; press the button above before unplugging the logger or before downloading today's CSV.</small></div>
<div class="card"><b>CSV files on microSD</b><p class="muted" id="health">Checking storage…</p>
<div id="files">Loading list…</div><p><a class="btn" href="/events.csv">Download events</a></p>
<small>Daily files are stored in /LOGS. Without a synchronised clock, NOSYNC.CSV is used. Downloading a large CSV may briefly pause measuring.</small></div>
<div class="card"><b>Estimation settings (logger only – they do not affect the heat pump)</b><p class="muted">Temperature thresholds are indicative. This is not the actual compressor state; the estimate may be wrong during defrosting.
A start is noticed early by a quick rise of the pipe temperature (at least 2 °C) and dated to the beginning of that rise; it is counted once the hot threshold confirms it. Set the hot threshold a few degrees below the pipe temperature while the compressor runs, the cold threshold a few degrees above the pipe temperature at rest.</p>
<form id="config">
<label>Hot phase from <input type="number" name="on" step="0.5" min="2" max="200"> °C</label>
<label>Cold phase up to <input type="number" name="off" step="0.5" min="0" max="198"> °C</label>
<label>Target water temperature (manual) <input type="number" name="target" step="0.5" min="20" max="60"> °C</label>
<button>Save</button></form><p id="msg"></p></div>
<p class="muted">Measuring every 10 seconds. The chart only contains data since the last ESP32 restart; the CSV files on the card survive a restart. The start count is based solely on the gas pipe temperature – it does not measure compressor frequency or output. <span id="ver"></span></p>
</main><script>
let configured=false,lastPoints=[];
const $=s=>document.querySelector(s);
const PHASE={HOT:'Hot phase',COLD:'Cold phase',RISING:'Starting (pipe rising)',UNKNOWN:'Unknown',SENSOR_ERROR:'Sensor error'};
const pretty=x=>x===null||!Number.isFinite(x)?'--':x.toFixed(1)+' °C';
const mins=s=>s===null?'--':(s/60).toFixed(1)+' min';
const RUN=[[360,'poor'],[600,'acceptable'],[1200,'good'],[Infinity,'ideal']]; // same limits as RUN_*_S in the firmware
function rate(sel,s){const e=$(sel);if(s===null){e.textContent='';e.className='';return;}const i=RUN.findIndex(r=>s<r[0]);e.textContent=RUN[i][1];e.className='q'+i;}
const stamp=t=>t>0?new Date(t*1000).toLocaleString(undefined,{day:'numeric',month:'numeric',hour:'2-digit',minute:'2-digit'}):null;
async function setTime(){try{await fetch('/api/time',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'epoch='+Math.floor(Date.now()/1000)});}catch(e){}}
async function status(){try{const r=await fetch('/api/status',{cache:'no-store'});const x=await r.json();
$('#gas').textContent=pretty(x.gas);$('#water').textContent=pretty(x.water);$('#delta').textContent=pretty(x.delta);
$('#estimate').textContent=PHASE[x.estimate]||x.estimate;$('#oneh').textContent=x.starts_1h;$('#day').textContent=x.starts_24h;
$('#avg').textContent=mins(x.avg_hot_s);$('#avgcold').textContent=mins(x.avg_cold_s);
$('#last').textContent=mins(x.last_hot_s);rate('#avgq',x.avg_hot_s);rate('#lastq',x.last_hot_s);
(x.runs_24h||[]).forEach((n,i)=>{$('#r'+i).textContent=n;});
$('#health').textContent='SD: '+(x.sd?'OK':'UNAVAILABLE')+' · clock: '+(x.time?'synchronised':'not synchronised')+' · uptime: '+(x.uptime_s/3600).toFixed(1)+' h';
$('#ver').textContent='Firmware '+x.version+'.';storage(x.storage);cellar(x.cellar);
if(!configured){const f=$('#config').elements;f.on.value=x.on;f.off.value=x.off;f.target.value=x.target;configured=true;}
}catch(e){$('#health').textContent='No connection to the ESP32';}}
const REASON={NO_CARD:'microSD card missing or not responding. Measuring continues, the data wait in RAM and writing resumes automatically when the card works again.',
WRITE_FAILED:'The last write failed. The data stay in RAM and the write is retried every minute.',
NO_SPACE:'Insufficient space – writing is paused. Delete old files or replace the card; the data wait in RAM meanwhile.',
BUFFER_OVERFLOW:'The RAM buffer was full – the oldest unsaved records were lost.',CLEANUP:'Deleting the oldest daily files…',LOW_SPACE:'Less than {t} % free space.'};
const size=b=>b>=1e9?(b/1e9).toFixed(2)+' GB':(b/1e6).toFixed(1)+' MB';
const ago=s=>s<90?s+' s ago':s<5400?Math.round(s/60)+' min ago':(s/3600).toFixed(1)+' h ago';
function storage(s){if(!s)return;const b=$('#stState');b.textContent=s.state;b.className='badge s-'+s.state;
$('#stReason').textContent=(REASON[s.reason]||'Card OK, writes succeed.').replace('{t}',s.target_free_pct)+(s.dropped?' Records lost since start: '+s.dropped+'.':'')+(s.write_errors?' Failed write attempts since start: '+s.write_errors+'.':'');
const k=s.total_b>0,pct=k?100*s.used_b/s.total_b:0;
$('#stTotal').textContent=k?(s.total_b/1e9).toFixed(2)+' GB':'--';$('#stUsed').textContent=k?size(s.used_b):'--';
$('#stFree').textContent=k?size(s.total_b-s.used_b):'--';$('#stPct').textContent=k?pct.toFixed(1)+' %':'--';$('#stBar').style.width=pct+'%';
$('#stFiles').textContent=s.files+(s.nosync?' + NOSYNC':'');$('#stRange').textContent=s.oldest?s.oldest+' – '+s.newest:'--';
$('#stLast').textContent=s.last_write_ago_s===null?'not yet':(s.last_write?stamp(s.last_write)+' · ':'')+ago(s.last_write_ago_s);
$('#stBuf').textContent=s.buffered+' / '+s.buffer_cap+(s.buffered_events?' + '+s.buffered_events+' events':'');
$('#stNext').textContent=s.next_write_s===null?'':'next write in about '+Math.max(1,Math.ceil(s.next_write_s/60))+' min';
const A={READY:'active – when free space drops below '+s.min_free_pct+' %, daily files older than '+s.retention_days+' days are deleted (oldest first) until '+s.target_free_pct+' % is free. Newer files are never deleted automatically.',
NO_TIME:'waiting for NTP time – no file is deleted until the clock is confirmed by NTP.',CLEANUP:'deleting the oldest files…',
NO_SPACE:'insufficient space – no file older than '+s.retention_days+' days is left to delete'+(s.ntp?'':' (or the clock is not confirmed by NTP)')+'. Writing is paused until space is freed.'};
$('#stArch').textContent=(A[s.archive]||s.archive)+(s.deleted?' Files deleted since start: '+s.deleted+'.':'');}
$('#flushBtn').addEventListener('click',async()=>{const m=$('#flushMsg');m.textContent='…';try{const r=await fetch('/api/flush',{method:'POST'});m.textContent=await r.text();}catch(e){m.textContent='No connection';}setTimeout(status,3000);});
function draw(data){const canvas=$('#plot'),dpr=window.devicePixelRatio||1;
const w=Math.max(360,canvas.clientWidth),h=330;canvas.width=Math.round(w*dpr);canvas.height=Math.round(h*dpr);
const c=canvas.getContext('2d');c.scale(dpr,dpr);c.clearRect(0,0,w,h);
const L=43,R=10,T=12,B=30,W=w-L-R,H=h-T-B;
let min=Infinity,max=-Infinity;data.forEach(p=>{for(const i of [1,2])if(Number.isFinite(p[i])){min=Math.min(min,p[i]);max=Math.max(max,p[i]);}});
canvas.tip=null;if(!Number.isFinite(min)){$('#graphInfo').textContent='No valid data yet';return;}
min=Math.floor((min-2)/5)*5;max=Math.ceil((max+2)/5)*5;if(max<=min)max=min+5;
c.strokeStyle='#e0e8ed';c.fillStyle='#667889';c.font='12px system-ui';
for(let k=0;k<=5;k++){let y=T+H*k/5;c.beginPath();c.moveTo(L,y);c.lineTo(w-R,y);c.stroke();c.fillText((max-(max-min)*k/5).toFixed(0),4,y+3);}
function line(col,colId){c.strokeStyle=col;c.lineWidth=2;c.beginPath();let active=false;
data.forEach((p,i)=>{let yv=p[colId];if(!Number.isFinite(yv)){active=false;return;}
const x=L+(W*i)/Math.max(1,data.length-1),y=T+(max-yv)*H/(max-min);
if(!active){c.moveTo(x,y);active=true}else c.lineTo(x,y)});c.stroke();}
line('#dc703b',1);line('#167db4',2);
const left=stamp(data[0][0])||'Older',right=stamp(data[data.length-1][0])||'Newer';
c.fillStyle='#667889';c.textAlign='left';c.fillText(left,L,h-8);c.textAlign='right';c.fillText(right,w-R,h-8);c.textAlign='left';
canvas.tip={T,H,px:data.map((p,i)=>L+(W*i)/Math.max(1,data.length-1)),
text:i=>'<b>'+(stamp(data[i][0])||'time unknown')+'</b>'+tipRow('#dc703b','Refrigerant pipe '+pretty(data[i][1]))+tipRow('#167db4','Heating water '+pretty(data[i][2]))};
$('#graphInfo').textContent=data.length+' points · axis range '+min+' to '+max+' °C · hover over or tap the chart to see a point';}
async function graph(){try{let r=await fetch('/api/history',{cache:'no-store'});let x=await r.json();lastPoints=x.points||[];draw(lastPoints);drawCellar(lastPoints);}catch(e){}}
// ---- Room (cellar) sensor ----
// axis settings: pad = space above/below the data, span = smallest axis range
const CT='#8e44ad',CH='#16a085',CA='#c0781a',AXT={span:2,pad:0.3,col:CT},AXH={span:10,pad:1,col:CH},AXA={span:2,pad:0.3,col:CA};
const nice=x=>{const p=10**Math.floor(Math.log10(x)),f=x/p;return(f<=1?1:f<=2?2:f<=2.5?2.5:f<=5?5:10)*p;}; // 1, 2, 2.5, 5 x 10^n
let cDays=[],cToday=false;
const unit=(v,u)=>v===null||!Number.isFinite(v)?'--':v.toFixed(1)+u;
const span=(a,u)=>{a=a.filter(Number.isFinite);return a.length?'24 h: '+Math.min(...a).toFixed(1)+' – '+Math.max(...a).toFixed(1)+u:'';};
const dayText=d=>new Date(d+'T12:00:00').toLocaleDateString();
function cellar(k){$('#cellarCard').hidden=!k;if(!k)return;
$('#cT').textContent=pretty(k.t);$('#cH').textContent=unit(k.rh,' %');$('#cD').textContent=pretty(k.dew);$('#cA').textContent=unit(k.ah,' g/m³');
$('#cSensor').textContent=k.sensor;
$('#cErr').textContent=k.t===null?'Sensor not responding'+(k.last_error?' ('+k.last_error+')':'')+' – check the '+k.sensor+' wiring: '+
(k.sensor.startsWith('SHT')?'SDA on GPIO33, SCL on GPIO22, VCC on 3V3, GND.':'data on GPIO33, 4.7 kΩ pull-up to 3V3, power from 3V3.'):
k.errors?'Failed readings since start: '+k.errors+' (a single failed reading is bridged by the previous value).':'';}
// Chart with up to two y axes: xs = x values, ser = [{y,lo,hi,col,ax}] (lo/hi = band), ax[i] = axis settings or null,
// gap = largest x step that is still joined by a line (0 = always join), lbl = texts under the left/right end,
// tx = tooltip text of point i.
function plot(cv,xs,ser,ax,gap,lbl,tx){cv.tip=null;const dpr=window.devicePixelRatio||1,w=Math.max(300,cv.clientWidth),h=cv.clientHeight||230;
cv.width=Math.round(w*dpr);cv.height=Math.round(h*dpr);const c=cv.getContext('2d');c.scale(dpr,dpr);c.clearRect(0,0,w,h);
const L=43,R=ax[1]?43:10,T=12,B=30,W=w-L-R,H=h-T-B;
const rg=ax.map((a,i)=>{if(!a)return null;let lo=Infinity,hi=-Infinity;
ser.filter(s=>s.ax===i).forEach(s=>[s.y,s.lo,s.hi].forEach(v=>v&&v.forEach(y=>{if(Number.isFinite(y)){lo=Math.min(lo,y);hi=Math.max(hi,y);}})));
if(!Number.isFinite(lo))return null;lo-=a.pad;hi+=a.pad;let per=nice(Math.max(hi-lo,a.span)/5);
for(;;){const l=Math.floor(lo/per)*per;if(l+5*per>=hi-1e-9)return[l,l+5*per];per=nice(per*1.01);}});
if(!xs.length||!rg[0]&&!rg[1])return false;
const x0=xs[0],x1=xs[xs.length-1],X=x=>L+(x1>x0?(x-x0)*W/(x1-x0):W/2),Y=(i,v)=>T+(rg[i][1]-v)*H/(rg[i][1]-rg[i][0]);
c.strokeStyle='#e0e8ed';c.lineWidth=1;c.font='12px system-ui';
for(let k=0;k<=5;k++){const y=T+H*k/5;c.beginPath();c.moveTo(L,y);c.lineTo(w-R,y);c.stroke();
rg.forEach((r,i)=>{if(!r)return;const v=r[1]-(r[1]-r[0])*k/5;c.fillStyle=ax[i].col;c.textAlign=i?'right':'left';c.fillText(+v.toFixed(2)+'',i?w-4:4,y+3);});}
// runs of consecutive points where all given values exist (a missing value or a gap in x splits the line)
const runs=(s,keys)=>{const out=[];let cur=[];xs.forEach((x,i)=>{const ok=keys.every(k=>Number.isFinite(s[k][i]));
if(!ok||cur.length&&gap&&x-xs[cur[cur.length-1]]>gap){if(cur.length)out.push(cur);cur=[];}if(ok)cur.push(i);});if(cur.length)out.push(cur);return out;};
ser.forEach(s=>{const a=s.ax;if(!rg[a])return;
if(s.lo){c.fillStyle=s.col+'38';runs(s,['lo','hi']).forEach(r=>{if(r.length===1){const x=X(xs[r[0]]);c.fillRect(x-3,Y(a,s.hi[r[0]]),6,Math.max(1,Y(a,s.lo[r[0]])-Y(a,s.hi[r[0]])));return;}
c.beginPath();r.forEach((i,j)=>c[j?'lineTo':'moveTo'](X(xs[i]),Y(a,s.hi[i])));for(let j=r.length-1;j>=0;j--)c.lineTo(X(xs[r[j]]),Y(a,s.lo[r[j]]));c.closePath();c.fill();});}
c.strokeStyle=c.fillStyle=s.col;c.lineWidth=2;runs(s,['y']).forEach(r=>{if(r.length===1){c.beginPath();c.arc(X(xs[r[0]]),Y(a,s.y[r[0]]),3,0,7);c.fill();return;}
c.beginPath();r.forEach((i,j)=>c[j?'lineTo':'moveTo'](X(xs[i]),Y(a,s.y[i])));c.stroke();});});
c.fillStyle='#667889';c.textAlign='left';c.fillText(lbl[0],L,h-8);c.textAlign='right';c.fillText(lbl[1],w-R,h-8);c.textAlign='left';
cv.tip={T,H,px:xs.map(X),text:tx};return true;}
function drawCellar(data){if($('#cellarCard').hidden||!data.length)return;const col=i=>data.map(p=>p[i]);
const ok=plot($('#cPlot'),data.map((p,i)=>i),[{y:col(3),col:CT,ax:0},{y:col(4),col:CH,ax:1}],[AXT,AXH],0,[stamp(data[0][0])||'Older',stamp(data[data.length-1][0])||'Newer'],
i=>'<b>'+(stamp(data[i][0])||'time unknown')+'</b>'+tipRow(CT,'Temperature '+pretty(data[i][3]))+tipRow(CH,'Relative humidity '+unit(data[i][4],' %')));
$('#cTr').textContent=span(col(3),' °C');$('#cHr').textContent=span(col(4),' %');$('#cInfo').textContent=ok?'':'No valid data yet';}
async function cellarDays(){if($('#cellarCard').hidden)return;try{const r=await fetch('/api/cellar',{cache:'no-store'});const x=await r.json();
cDays=x.days||[];cToday=!!x.today;if(x.today){cDays=cDays.filter(d=>d[0]!==x.today[0]);cDays.push(x.today);}drawDays();}catch(e){}}
function drawDays(){if($('#cellarCard').hidden)return;const info=$('#cDayInfo');
if(!cDays.length){info.textContent='No daily data yet – the summary starts with the first reading that has a valid clock.';return;}
const dn=d=>Date.parse(d[0]+'T00:00:00Z')/864e5,last=dn(cDays[cDays.length-1]),d=cDays.filter(r=>dn(r)>last-+$('#cRange').value);
const xs=d.map(dn),col=i=>d.map(r=>r[i]),lbl=[dayText(d[0][0]),dayText(d[d.length-1][0])];
const head=r=>'<b>'+dayText(r[0])+'</b> · '+r[8]+' readings',mm=(r,a,u)=>unit(r[a+1],u)+' (min '+unit(r[a],'')+', max '+unit(r[a+2],'')+')';
plot($('#cDayT'),xs,[{y:col(2),lo:col(1),hi:col(3),col:CT,ax:0}],[AXT,null],1.5,lbl,i=>head(d[i])+tipRow(CT,'Temperature '+mm(d[i],1,' °C')));
plot($('#cDayH'),xs,[{y:col(5),lo:col(4),hi:col(6),col:CH,ax:0},{y:col(7),col:CA,ax:1}],[AXH,AXA],1.5,lbl,
i=>head(d[i])+tipRow(CH,'Relative humidity '+mm(d[i],4,' %'))+tipRow(CA,'Absolute humidity '+unit(d[i][7],' g/m³')));
const part=d.filter(r=>r[8]<8000&&!(cToday&&r===cDays[cDays.length-1])).length;
info.textContent=d.length+(d.length===1?' day':' days')+(cToday?' · the last day is today so far':'')+(part?' · '+part+' incomplete day(s) (logger off or sensor errors)':'');}
$('#cRange').addEventListener('change',drawDays);
// Tooltip: time and values of the point nearest to the mouse (or a tap). A drawn chart keeps cv.tip = {T,H,px,text}.
const tip=document.createElement('div'),guide=document.createElement('div');tip.className='tip';guide.className='guide';document.body.append(tip,guide);
const hideTip=()=>{tip.style.display=guide.style.display='none';};
function tipRow(col,t){return'<br><span class="dot" style="background:'+col+'"></span>'+t;}
function hover(cv){const show=e=>{const t=cv.tip;if(!t||!t.px.length){hideTip();return;}const r=cv.getBoundingClientRect(),x=e.clientX-r.left;
let i=0;t.px.forEach((p,j)=>{if(Math.abs(p-x)<Math.abs(t.px[i]-x))i=j;});
const gx=r.left+scrollX+t.px[i],top=r.top+scrollY+t.T;
guide.style.left=gx+'px';guide.style.top=top+'px';guide.style.height=t.H+'px';guide.style.display='block';
tip.innerHTML=t.text(i);tip.style.display='block';const tw=tip.offsetWidth;
tip.style.left=(gx+12+tw>scrollX+document.documentElement.clientWidth-4?Math.max(scrollX+4,gx-12-tw):gx+12)+'px';tip.style.top=top+'px';};
cv.addEventListener('pointermove',show);cv.addEventListener('pointerdown',show);
cv.addEventListener('pointerleave',e=>{if(e.pointerType==='mouse')hideTip();});}   // on a touch screen the tooltip stays until a tap elsewhere
['#plot','#cPlot','#cDayT','#cDayH'].forEach(s=>hover($(s)));
document.addEventListener('pointerdown',e=>{if(e.target.tagName!=='CANVAS')hideTip();});
async function files(){try{let r=await fetch('/api/files',{cache:'no-store'});if(!r.ok)throw 0;let list=await r.json();let area=$('#files');area.replaceChildren();
if(!list.length){area.textContent='No CSV files yet';return;}
list.sort((a,b)=>(a==='NOSYNC.CSV')-(b==='NOSYNC.CSV')||b.localeCompare(a));
list.forEach(f=>{let row=document.createElement('div');row.className='file';let a=document.createElement('a');a.textContent=f;a.href='/download?file='+encodeURIComponent(f);row.append(a);area.append(row);});
}catch(e){$('#files').textContent='File list unavailable';}}
$('#config').addEventListener('submit',async ev=>{ev.preventDefault();const x=new URLSearchParams(new FormData(ev.target));const r=await fetch('/api/config',{method:'POST',body:x});$('#msg').textContent=await r.text();await status();});
(async()=>{await setTime();await status();await graph();await cellarDays();await files()})();
setInterval(status,10000);setInterval(graph,60000);setInterval(files,120000);setInterval(cellarDays,600000);
window.addEventListener('resize',()=>{hideTip();draw(lastPoints);drawCellar(lastPoints);drawDays();});
</script></body></html>
)PAGE";

String ymdText(uint32_t ymd) {
  if (!ymd) return String("null");
  char buf[16];
  snprintf(buf, sizeof(buf), "\"%04lu-%02lu-%02lu\"", (unsigned long)(ymd / 10000), (unsigned long)(ymd / 100 % 100), (unsigned long)(ymd % 100));
  return String(buf);
}

// Storage part of /api/status (a consistent snapshot of the writer task's state).
String storageJSON() {
  StorageInfo si;
  size_t rows, events;
  uint32_t dropped;
  portENTER_CRITICAL(&bufMux);
  si = storage;
  rows = logRing.count;
  events = eventRing.count + cellarRing.count;
  dropped = logRing.dropped + eventRing.dropped;
  portEXIT_CRITICAL(&bufMux);
  bool card = sdOK;
  uint64_t nowMs = uptimeMs();
  int fp = (si.spaceKnown && si.totalB) ? (int)((si.totalB - si.usedB) * 1000ULL / si.totalB) : -1;
  if (testFreePercent >= 0) fp = testFreePercent * 10;
  // Precedence: the most serious problem is reported.
  const char *state = "OK", *reason = "";
  if (si.noSpace) { state = "ERROR"; reason = "NO_SPACE"; }
  else if (si.writeFailed) { state = "ERROR"; reason = "WRITE_FAILED"; }
  else if (!card) { state = "ERROR"; reason = "NO_CARD"; }
  else if (si.overflow) { state = "ERROR"; reason = "BUFFER_OVERFLOW"; }
  else if (si.cleanup) { state = "CLEANUP"; reason = "CLEANUP"; }
  else if (fp >= 0 && fp < SD_TARGET_FREE_PERCENT * 10) { state = "WARNING"; reason = "LOW_SPACE"; }
  const char *archive = si.cleanup ? "CLEANUP" : si.noSpace ? "NO_SPACE" : !ntpSynced ? "NO_TIME" : "READY";
  uint64_t flushBase = si.lastFlushUpMs;
  long nextS = -1;
  if (card && !si.noSpace && (rows || events)) {
    uint64_t due = flushBase + SD_FLUSH_MS;
    nextS = due > nowMs ? (long)((due - nowMs) / 1000ULL) : 0;
  }
  String o;
  o.reserve(600);
  o += "{\"state\":\""; o += state;
  o += "\",\"reason\":\""; o += reason;
  o += "\",\"card\":"; o += card ? "true" : "false";
  o += ",\"total_b\":"; o += String((unsigned long long)(si.spaceKnown ? si.totalB : 0));
  o += ",\"used_b\":"; o += String((unsigned long long)(si.spaceKnown ? si.usedB : 0));
  o += ",\"files\":"; o += String(si.dailyFiles);
  o += ",\"nosync\":"; o += si.nosyncFile ? "true" : "false";
  o += ",\"oldest\":"; o += ymdText(si.oldestYmd);
  o += ",\"newest\":"; o += ymdText(si.newestYmd);
  o += ",\"last_write\":"; o += si.lastWriteUnix ? String(si.lastWriteUnix) : String("null");
  o += ",\"last_write_ago_s\":"; o += si.lastWriteUpMs ? String((unsigned long)((nowMs - si.lastWriteUpMs) / 1000ULL)) : String("null");
  o += ",\"next_write_s\":"; o += nextS >= 0 ? String(nextS) : String("null");
  o += ",\"buffered\":"; o += String((unsigned)rows);
  o += ",\"buffered_events\":"; o += String((unsigned)events);
  o += ",\"buffer_cap\":"; o += String((unsigned)logRing.cap);
  o += ",\"dropped\":"; o += String(dropped);
  o += ",\"write_errors\":"; o += String(si.writeErrors);
  o += ",\"deleted\":"; o += String(si.deletedFiles);
  o += ",\"archive\":\""; o += archive;
  o += "\",\"ntp\":"; o += ntpSynced ? "true" : "false";
  o += ",\"min_free_pct\":"; o += String(SD_MIN_FREE_PERCENT);
  o += ",\"target_free_pct\":"; o += String(SD_TARGET_FREE_PERCENT);
  o += ",\"retention_days\":"; o += String(SD_RETENTION_DAYS);
  o += "}";
  return o;
}

void sendJSONStatus() {
  Stats s = getStats();
  String out;
  out.reserve(1200);
  out += "{";
  out += "\"version\":\"" + String(FW_VERSION) + "\"";
  out += ",\"gas\":" + String(gasValid ? String(gasC, 2) : String("null"));
  out += ",\"water\":" + String(waterValid ? String(waterC, 2) : String("null"));
  out += ",\"delta\":" + String(waterValid ? String(waterC - configuredTargetC, 2) : String("null"));
  out += ",\"estimate\":\"";
  out += !gasValid ? "SENSOR_ERROR" : !estimatedKnown ? "UNKNOWN" : estimatedHot ? "HOT" : riseFromMs ? "RISING" : "COLD";
  out += "\"";
  out += ",\"sd\":" + String(sdOK ? "true" : "false");
  out += ",\"time\":" + String(timeIsKnown() ? "true" : "false");
  out += ",\"uptime_s\":" + String((unsigned long)(uptimeMs() / 1000ULL));
  out += ",\"starts_1h\":" + String(s.starts1h);
  out += ",\"starts_24h\":" + String(s.starts24h);
  out += ",\"runs_24h\":[" + String(s.runs[0]) + "," + String(s.runs[1]) + "," + String(s.runs[2]) + "," + String(s.runs[3]) + "]";
  out += ",\"avg_hot_s\":" + String(s.completed24h ? String((unsigned long)(s.totalHotS / s.completed24h)) : String("null"));
  out += ",\"avg_cold_s\":" + String(s.coldKnown24h ? String((unsigned long)(s.totalColdS / s.coldKnown24h)) : String("null"));
  out += ",\"last_hot_s\":" + String(lastHotS ? String(lastHotS) : String("null"));
  out += ",\"on\":" + String(hotThresholdC, 1);
  out += ",\"off\":" + String(coldThresholdC, 1);
  out += ",\"target\":" + String(configuredTargetC, 1);
  out += ",\"heap_free\":" + String((unsigned)ESP.getFreeHeap());
  out += ",\"heap_min\":" + String((unsigned)ESP.getMinFreeHeap());
  out += ",\"storage\":" + storageJSON();
  out += ",\"cellar\":" + cellarJSON();
  out += "}";
  web.send(200, "application/json; charset=utf-8", out);
}

String tenthsJSON(int16_t v) { return v == NO_TENTHS ? String("null") : String(v / 10.0f, 1); }

// Cellar part of /api/status; null when the sensor is disabled.
String cellarJSON() {
  if (!CELLAR_ENABLED) return String("null");
  auto num = [](bool ok, float v, int digits) { return ok && isfinite(v) ? String(v, digits) : String("null"); };
  String o = "{\"sensor\":\"";
  o += CELLAR_SENSOR == CELLAR_AM2301 ? "AM2301" : shtType == SHT_4X ? "SHT4x" : shtType == SHT_3X ? "SHT3x" : "SHT";
  o += "\",\"t\":" + num(cellarValid, cellarT, 1);
  o += ",\"rh\":" + num(cellarValid, cellarRH, 1);
  o += ",\"dew\":" + num(cellarValid, dewPointC(cellarT, cellarRH), 1);
  o += ",\"ah\":" + num(cellarValid, absHumidity(cellarT, cellarRH), 1);
  o += ",\"errors\":" + String(cellarErrors);
  o += ",\"last_error\":";
  o += cellarErrors ? "\"" + String(CELLAR_ERROR_NAMES[cellarLastError]) + "\"" : String("null");
  o += "}";
  return o;
}

void cellarDayJSON(String &out, const CellarDay &d) {
  out += '['; out += ymdText(d.ymd);
  const int16_t v[] = {d.tMin, d.tAvg, d.tMax, d.hMin, d.hAvg, d.hMax, d.ahAvg};
  for (int16_t x : v) { out += ','; out += tenthsJSON(x); }
  out += ','; out += String(d.samples); out += ']';
}

// Daily cellar summaries for the long-term chart:
// {"days":[[date,t_min,t_avg,t_max,rh_min,rh_avg,rh_max,ah_avg,samples],...],"today":[...]|null}
void sendCellarDays() {
  web.setContentLength(CONTENT_LENGTH_UNKNOWN);
  web.send(200, "application/json", "");
  web.sendContent("{\"days\":[");
  String part;
  part.reserve(1400);
  for (size_t i = 0;; ++i) {
    CellarDay d;
    portENTER_CRITICAL(&bufMux);
    bool more = i < cellarDayCount;
    if (more) d = cellarDays[i];
    portEXIT_CRITICAL(&bufMux);
    if (!more) break;
    if (i) part += ',';
    cellarDayJSON(part, d);
    if (part.length() > 1200) { web.sendContent(part); part = ""; }
  }
  part += "],\"today\":";
  if (CELLAR_ENABLED && cellarAcc.ymd && cellarAcc.n) cellarDayJSON(part, summarizeCellar(cellarAcc));
  else part += "null";
  part += '}';
  web.sendContent(part);
  web.sendContent("");
}

void sendGraph() {
  web.setContentLength(CONTENT_LENGTH_UNKNOWN);
  web.send(200, "application/json", "");
  web.sendContent("{\"points\":[");
  String part;
  part.reserve(1400);
  size_t first = (graphWrite + GRAPH_POINTS - graphCount) % GRAPH_POINTS;
  for (size_t k = 0; k < graphCount; k++) {
    const GraphPoint &p = history[(first + k) % GRAPH_POINTS];
    if (k) part += ',';
    part += '['; part += String(p.stamp); part += ',';
    part += isfinite(p.gas) ? String(p.gas, 2) : String("null"); part += ',';
    part += isfinite(p.water) ? String(p.water, 2) : String("null"); part += ',';
    part += tenthsJSON(p.cellarT); part += ','; part += tenthsJSON(p.cellarRH); part += ']';
    if (part.length() > 1200) { web.sendContent(part); part = ""; }
  }
  if (part.length()) web.sendContent(part);
  web.sendContent("]}");
  web.sendContent("");
}

// File name: exactly YYYYMMDD.CSV or NOSYNC.CSV, no path.
bool validFileName(const String &n) {
  if (n == "NOSYNC.CSV") return true;
  if (n.length() != 12 || !n.endsWith(".CSV")) return false;
  for (int i = 0; i < 8; i++) if (!isDigit(n.charAt(i))) return false;
  return true;
}
// Card access from the web server holds the card mutex, so the writer task can neither remount the
// card nor delete the file while it is being read. If the writer is busy, answer 503 instead of
// stalling the measurement loop.
void downloadPath(const String &p, const char *name) {
  if (!sdOK) { web.send(404, "text/plain; charset=utf-8", "CSV not found"); return; }
  SdLock lock(SD_WEB_WAIT_MS);
  if (!lock.ok) { web.send(503, "text/plain; charset=utf-8", "microSD busy, try again in a moment"); return; }
  if (!sdOK || !SD.exists(p)) { web.send(404, "text/plain; charset=utf-8", "CSV not found"); return; }
  File file = SD.open(p, FILE_READ);
  if (!file) { web.send(500, "text/plain; charset=utf-8", "Cannot read the SD card"); return; }
  web.sendHeader("Content-Disposition", String("attachment; filename=\"") + name + "\"");
  web.streamFile(file, "text/csv; charset=utf-8");
  file.close();
}
void sendFileList() {
  if (!sdOK) { web.send(200, "application/json", "[]"); return; }
  SdLock lock(SD_WEB_WAIT_MS);
  if (!lock.ok) { web.send(503, "application/json", "[]"); return; }
  File dir = SD.open("/LOGS");
  if (!dir || !dir.isDirectory()) { web.send(200, "application/json", "[]"); return; }
  String json;
  json.reserve(16 * (storage.dailyFiles + 2));
  json = "["; bool first = true;
  File file = dir.openNextFile();
  while (file) {
    String name = String(file.name());
    int slash = name.lastIndexOf('/'); if (slash >= 0) name = name.substring(slash + 1);
    if (!file.isDirectory() && validFileName(name)) {
      if (!first) json += ',';
      json += '"'; json += name; json += '"'; first = false;
    }
    file.close(); file = dir.openNextFile();
  }
  dir.close(); json += ']';
  web.send(200, "application/json", json);
}
// "Write now": wakes the writer task (e.g. before unplugging the logger or downloading today's CSV).
void requestFlush() {
  portENTER_CRITICAL(&bufMux);
  size_t n = logRing.count + eventRing.count + cellarRing.count;
  bool noSpace = storage.noSpace;
  portEXIT_CRITICAL(&bufMux);
  if (!sdOK) { web.send(503, "text/plain; charset=utf-8", "microSD not available – the data stay in RAM"); return; }
  if (noSpace) { web.send(503, "text/plain; charset=utf-8", "Writing paused – insufficient space on the card"); return; }
  if (!n) { web.send(200, "text/plain; charset=utf-8", "Nothing to write – everything is on the card"); return; }
  flushRequested = true;
  if (sdTaskHandle) xTaskNotifyGive(sdTaskHandle);
  web.send(200, "text/plain; charset=utf-8", String("Writing ") + String((unsigned)n) + " records…");
}

void saveConfig() {
  if (!web.hasArg("on") || !web.hasArg("off") || !web.hasArg("target")) {
    web.send(400, "text/plain; charset=utf-8", "Incomplete form"); return;
  }
  float hi = web.arg("on").toFloat(); float lo = web.arg("off").toFloat(); float target = web.arg("target").toFloat();
  if (!isfinite(hi) || !isfinite(lo) || !isfinite(target) || hi < 2 || hi > 200 || lo < 0 || lo > 198 ||
      hi < lo + 2 || target < 20 || target > 60) {
    web.send(400, "text/plain; charset=utf-8", "Invalid values (the hot threshold must be at least 2 °C above the cold threshold)"); return;
  }
  hotThresholdC = hi; coldThresholdC = lo; configuredTargetC = target;
  prefs.putFloat("on", hi); prefs.putFloat("off", lo); prefs.putFloat("target", target);
  resetEstimator();
  web.send(200, "text/plain; charset=utf-8", "Saved. Phase estimation starts over.");
}
// Browser time is used only when the ESP32 has no NTP time (typically in access point mode).
void timeFromBrowser() {
  if (!web.hasArg("epoch")) { web.send(400, "text/plain; charset=utf-8", "Missing epoch"); return; }
  // Once NTP has set the clock, it keeps running correctly through an NTP/Wi-Fi outage; a browser
  // clock must not override it (and only NTP time allows the archive to delete old files).
  if (timeIsKnown() && (WiFi.status() == WL_CONNECTED || ntpSynced)) { web.send(200, "text/plain; charset=utf-8", "Time is set by NTP"); return; }
  long long epoch = atoll(web.arg("epoch").c_str());
  if (epoch < 1700000000LL || epoch > 4102444800LL) { web.send(400, "text/plain; charset=utf-8", "Invalid time"); return; }
  struct timeval tv; tv.tv_sec = (time_t)epoch; tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
  web.send(200, "text/plain; charset=utf-8", "OK");
}

void startNtp() { configTzTime(TZ_INFO, NTP_SERVER_1, NTP_SERVER_2); }

void connectWifi() {
  WiFi.mode(WIFI_STA); WiFi.setSleep(false); WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t from = millis();
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED && (uint32_t)(millis() - from) < WIFI_CONNECT_MS) { delay(300); Serial.print('.'); }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nWeb: http://%s/\n", WiFi.localIP().toString().c_str());
    startNtp();
    return;
  }
  // Fallback access point. The home network keeps being retried in the background –
  // after a power cut the router usually boots more slowly than the ESP32.
  WiFi.disconnect(); WiFi.mode(WIFI_AP_STA);
  if (WiFi.softAP(AP_SSID, AP_PASSWORD))
    Serial.printf("\nAccess point %s, web http://%s/\n", AP_SSID, WiFi.softAPIP().toString().c_str());
  else Serial.println("\nERROR: access point failed to start (the password must have at least 8 characters)");
  apFallback = true;
  lastWifiRetryMs = millis();
}

void maintainWifi(uint32_t now) {
  if (!apFallback) return;
  if (WiFi.status() == WL_CONNECTED) {
    apFallback = false; wifiAttempt = false;
    Serial.printf("Connected to home Wi-Fi, web: http://%s/\n", WiFi.localIP().toString().c_str());
    startNtp();
    WiFi.softAPdisconnect(true);
    return;
  }
  if (wifiAttempt && (uint32_t)(now - lastWifiRetryMs) >= WIFI_CONNECT_MS) {
    WiFi.disconnect(); // attempt failed; stop retries that would disturb the access point
    wifiAttempt = false;
  }
  // Only try when nobody is connected to the access point (a channel switch would drop the client).
  if (!wifiAttempt && (uint32_t)(now - lastWifiRetryMs) >= WIFI_RETRY_MS && WiFi.softAPgetStationNum() == 0) {
    lastWifiRetryMs = now;
    wifiAttempt = true;
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

// ---------------- Serial console (diagnostics and test hooks) ----------------
void printStorageStatus() {
  String js = storageJSON();
  Serial.printf("Storage: %s\n", js.c_str());
  Serial.printf("Heap: free %u, minimum %u, largest block %u | writer task stack left %u B\n",
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(), (unsigned)ESP.getMaxAllocHeap(),
                sdTaskHandle ? (unsigned)uxTaskGetStackHighWaterMark(sdTaskHandle) : 0u);
}
// Room sensor bus check: external pull-ups (a weak internal pull-down loses against 4.7 kΩ) and
// devices answering, also with SDA and SCL swapped. The sensor is detected again afterwards.
void i2cScan(int sda, int scl) {
  Wire.end();
  pinMode(sda, INPUT_PULLDOWN); pinMode(scl, INPUT_PULLDOWN);
  delay(5);
  int hSda = digitalRead(sda), hScl = digitalRead(scl);
  Wire.begin(sda, scl, 100000);
  Wire.setTimeOut(20);
  Serial.printf("SDA=GPIO%d SCL=GPIO%d | pull-up SDA: %s, SCL: %s | answering:", sda, scl,
                hSda ? "yes" : "NO", hScl ? "yes" : "NO");
  int found = 0;
  for (uint8_t a = 0x08; a < 0x78; ++a) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { Serial.printf(" 0x%02X", a); ++found; }
  }
  Serial.println(found ? "" : " none");
}
void i2cDiag() {
  if (CELLAR_SENSOR != CELLAR_SHT) { Serial.println("No I2C room sensor configured"); return; }
  i2cScan(PIN_CELLAR_SCL, PIN_CELLAR);   // swapped wires
  i2cScan(PIN_CELLAR, PIN_CELLAR_SCL);   // as wired by the documentation, left active
  shtAddr = 0; shtType = SHT_NONE; shtStarted = false;
}

void handleCommand(char *cmd) {
  char *arg = strchr(cmd, ' ');
  if (arg) *arg++ = 0;
  if (!strcmp(cmd, "status")) { printStorageStatus(); }
  else if (!strcmp(cmd, "i2c")) { i2cDiag(); }
  else if (!strcmp(cmd, "flush")) { flushRequested = true; if (sdTaskHandle) xTaskNotifyGive(sdTaskHandle); Serial.println("Write requested"); }
  else if (!strcmp(cmd, "failwrite") && arg) {
    testFailWrites = (uint8_t)constrain(atoi(arg), 0, 20);
    Serial.printf("TEST: the next %u write(s) will be reported as failed (rows are written, then rolled back)\n", (unsigned)testFailWrites);
  } else if (!strcmp(cmd, "simfree") && arg) {
    testFreePercent = !strcmp(arg, "off") ? -1 : (int16_t)constrain(atoi(arg), 0, 100);
    if (testFreePercent < 0) Serial.println("TEST: real free space");
    else Serial.printf("TEST: free space simulated as %d %% – WARNING: the archive really deletes files older than %u days\n",
                       (int)testFreePercent, SD_RETENTION_DAYS);
    flushRequested = true; if (sdTaskHandle) xTaskNotifyGive(sdTaskHandle);
  } else if (!strcmp(cmd, "settime") && arg) {
    long long epoch = atoll(arg);
    if (epoch < 1700000000LL || epoch > 4102444800LL) { Serial.println("Invalid Unix time"); return; }
    struct timeval tv; tv.tv_sec = (time_t)epoch; tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    ntpSynced = false;   // manual time never allows archive deletion
    Serial.printf("TEST: clock set to %lld (archive deletion disabled until the next NTP sync)\n", epoch);
  } else {
    Serial.println("Commands: status | i2c | flush | failwrite <n> | simfree <percent|off> | settime <unix_s>");
  }
}
void serialConsole() {
  static char line[48];
  static size_t n = 0;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r' || c == '\n') { line[n] = 0; if (n) handleCommand(line); n = 0; }
    else if (n < sizeof(line) - 1) line[n++] = c;
  }
}

void setup() {
  Serial.begin(115200); delay(300);
  // Time zone also in access point mode, when the ESP32 has no access to NTP.
  setenv("TZ", TZ_INFO, 1);
  tzset();
  // Only time confirmed by NTP allows the archive to delete old files (see deletionLimitYmd()).
  sntp_set_time_sync_notification_cb(onNtpSync);
  sdMutex = xSemaphoreCreateMutex();
  // RAM buffers first, while the heap is still in one piece.
  if (logRing.allocate(LOG_BUFFER_RECORDS) < LOG_BUFFER_RECORDS || eventRing.allocate(EVENT_BUFFER_RECORDS) < EVENT_BUFFER_RECORDS)
    Serial.printf("WARNING: smaller RAM buffer than configured: %u rows, %u events\n", (unsigned)logRing.cap, (unsigned)eventRing.cap);
  if (CELLAR_ENABLED) {
    cellarRing.allocate(CELLAR_BUFFER_RECORDS);
    cellarDays = (CellarDay *)calloc(CELLAR_DAYS, sizeof(CellarDay));
    cellarDayCap = cellarDays ? CELLAR_DAYS : 0;
    if (CELLAR_SENSOR == CELLAR_SHT) {
      Wire.begin(PIN_CELLAR, PIN_CELLAR_SCL, 100000);
      Wire.setTimeOut(20);
    } else {
      // Open drain with the pull-up: writing 0 pulls the line low, 1 releases it, reading sees the line.
      pinMode(PIN_CELLAR, OUTPUT_OPEN_DRAIN | PULLUP);
      digitalWrite(PIN_CELLAR, HIGH);
    }
  }
  pinMode(PIN_TC_CLK, OUTPUT); pinMode(PIN_TC_CS, OUTPUT); pinMode(PIN_TC_DO, INPUT_PULLUP);
  digitalWrite(PIN_TC_CS, HIGH); digitalWrite(PIN_TC_CLK, LOW);
  pinMode(PIN_SD_CS, OUTPUT); digitalWrite(PIN_SD_CS, HIGH);
  prefs.begin("heatpulse", false);
  hotThresholdC = prefs.getFloat("on", 45.0f);
  coldThresholdC = prefs.getFloat("off", 35.0f);
  configuredTargetC = prefs.getFloat("target", 28.0f);
  if (hotThresholdC < coldThresholdC + 2.0f) { hotThresholdC = 45.0f; coldThresholdC = 35.0f; }
  dallas.begin(); dallas.setResolution(12); dallas.setWaitForConversion(false);
  Serial.printf("DS18B20: %u sensor(s) found\n", (unsigned)dallas.getDeviceCount());
  sdOK = mountSD(); Serial.printf("microSD: %s\n", sdOK ? "OK" : "ERROR");
  Serial.printf("Storage: write every %lu min, RAM buffer %u rows (%lu min), archive %u/%u %% free, retention %u days, heap free %u\n",
                (unsigned long)(SD_FLUSH_MS / 60000), (unsigned)logRing.cap,
                (unsigned long)(logRing.cap * SAMPLE_MS / 60000), SD_MIN_FREE_PERCENT, SD_TARGET_FREE_PERCENT, SD_RETENTION_DAYS,
                (unsigned)ESP.getFreeHeap());
  connectWifi();
  web.on("/", HTTP_GET, []() { web.send_P(200, "text/html; charset=utf-8", PAGE); });
  web.on("/api/status", HTTP_GET, sendJSONStatus);
  web.on("/api/history", HTTP_GET, sendGraph);
  web.on("/api/files", HTTP_GET, sendFileList);
  web.on("/api/config", HTTP_POST, saveConfig);
  web.on("/api/time", HTTP_POST, timeFromBrowser);
  web.on("/api/flush", HTTP_POST, requestFlush);
  web.on("/download", HTTP_GET, []() {
    String n = web.arg("file");
    if (!validFileName(n)) { web.send(400, "text/plain; charset=utf-8", "Invalid file name"); return; }
    downloadPath("/LOGS/" + n, n.c_str());
  });
  web.on("/events.csv", HTTP_GET, []() { downloadPath("/EVENTS.CSV", "EVENTS.CSV"); });
  web.on("/api/cellar", HTTP_GET, sendCellarDays);
  web.on("/cellar.csv", HTTP_GET, []() { downloadPath("/CELLAR.CSV", "CELLAR.CSV"); });
  web.onNotFound([]() { web.send(404, "text/plain; charset=utf-8", "Not found"); });
  web.begin();
  storage.lastFlushUpMs = uptimeMs();
  // Same core and priority as loop(): time slicing shares the CPU during long card operations,
  // and the task watchdog on core 0's idle task is never starved.
  if (xTaskCreatePinnedToCore(sdWriterTask, "sdWriter", 8192, nullptr, 1, &sdTaskHandle, ARDUINO_RUNNING_CORE) != pdPASS)
    Serial.println("ERROR: cannot start the SD writer task – nothing will be saved");
  lastSampleBeginMs = millis() - SAMPLE_MS; // first measurement right away
  Serial.printf("HeatPulse %s ready (type 'help' in the Serial Monitor for storage diagnostics)\n", FW_VERSION);
}

void loop() {
  web.handleClient();
  uint32_t now = millis();
  if (!samplePending && (uint32_t)(now - lastSampleBeginMs) >= SAMPLE_MS) {
    lastSampleBeginMs = now; beginSample(now);
  }
  if (samplePending && (uint32_t)(now - sampleStartedMs) >= DS_CONVERSION_MS) finishSample(now);
  // Card mounting, writing and the archive run in sdWriterTask, never here.
  maintainWifi(now);
  serialConsole();
  delay(2);
}
