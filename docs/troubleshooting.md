# Troubleshooting

| Symptom | What to check |
|---|---|
| Sketch won't upload | data (not charge-only) micro-USB cable, COM port, **ESP32 Dev Module** board selected, hold the BOOT button, USB–UART driver (CP210x / CH340) |
| `OneWire.h` / `DallasTemperature.h` error | install the libraries in the Library Manager |
| "secrets.h not found" warning | copy `secrets.example.h` to `secrets.h` and fill in your Wi-Fi ([installation](installation.md#2-download-and-configure)) |
| microSD error | FAT32 format, 3.3 V supply, short wires, CS = GPIO21, SPI = GPIO18/19/23, good USB adapter |
| Storage shows **ERROR – card missing** | card seated properly, FAT32, wiring as above; the firmware remounts every 60 s and writes the data waiting in RAM (up to 3 h). Details on the Serial Monitor (`SD ERROR: …`) |
| Storage shows **ERROR – insufficient space** | less than 10 % free and no daily file older than 365 days; copy the CSV files to a PC and delete old ones (or other files) from the card, or use a larger card. Writing resumes automatically ([circular archive](storage.md#circular-archive)) |
| Archive shows **waiting for NTP time** | old files are deleted only with time confirmed by NTP – give the logger internet access |
| Today's CSV is missing the last minutes | rows are written every 10 min; press **Write buffer to microSD now** before downloading |
| MAX6675 reports an error | thermocouple polarity on `T+`/`T−`, CS = GPIO27, SCK = GPIO26, SO = GPIO25, grounded probe sheath, temperature below 0 °C |
| DS18B20 doesn't measure | **4.7 kΩ resistor between GPIO32 and 3V3** (not in series!), wire colours (red 3V3, black GND, yellow GPIO32), cable, interference. The Serial Monitor prints the number of sensors found at boot |
| Room section: **Sensor not responding** (`NO_RESPONSE`), SHT40 / SHT31 | SDA on **GPIO33**, SCL on **GPIO22** (not swapped), VCC on 3V3, GND; `CELLAR_SENSOR = CELLAR_SHT`; with a probe on a cable 4.7 kΩ from SDA and SCL to 3V3. The Serial Monitor prints `SHT4x found at I2C address 0x44` (or `SHT3x`, or 0x45) once the sensor answers. Type `i2c` in the Serial Monitor: it shows whether both wires have a pull-up and which addresses answer, also with SDA and SCL swapped (`answering: 0x44` only in the swapped line = green and yellow wires swapped) |
| SHT40 shows a too high humidity for days | after a long time in nearly condensing air every humidity sensor drifts up; the firmware's heater pulses limit this. LaskaKit describes a longer "refresh" (heater about 90 min) for a sensor that reads wrong |
| Room section: **Sensor not responding** (`NO_RESPONSE`), AM2301 | AM2301 on **GPIO33** (not on the DS18B20 wire – it is not a 1-Wire sensor), **4.7 kΩ between GPIO33 and 3V3**, red wire on 3V3, black on GND; `CELLAR_SENSOR = CELLAR_AM2301` |
| Room section: *Failed readings* keep growing (`CHECKSUM`, `TIMEOUT`) | AM2301: cable too long or next to mains cables, missing external pull-up; at 3.3 V keep the cable short (about 1 m). SHT40: wires longer than about 1 m or next to mains cables. Single failures are harmless – they are bridged by the previous value |
| Room daily trend is empty | the first summary is created with the first reading that has a valid clock; a full day appears after midnight. The current day is shown as "today so far" |
| Room humidity looks too high / too low | sensor near a heat source, in the electronics enclosure, or recently wet (it recovers in hours); tolerance ±3 % RH. Compare with another hygrometer placed next to it |
| DS18B20 shows an error after hot-plugging | the firmware searches for the sensor again on every measurement; wait 10 s |
| Web page won't open | 2.4 GHz Wi-Fi, IP address in the Serial Monitor (115200 baud), client isolation / guest network, fallback access point `HeatPulse` at `http://192.168.4.1` |
| First CSV rows have no time | the clock is not synchronised yet – open the web page from a phone/PC with the correct time, or provide internet access (NTP) |
| Chart is empty after a restart | the chart history is kept in RAM only; data on the microSD card is preserved |
| Many "starts" during defrosting | false transitions from temperature; compare with the actual `Compressor` status on the controller and adjust the [thresholds](usage.md#how-to-tune-the-thresholds) if needed |
| No starts detected although the compressor runs | the pipe does not reach the hot threshold – lower it according to [tuning the thresholds](usage.md#how-to-tune-the-thresholds) |
