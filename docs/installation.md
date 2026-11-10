# Firmware installation

## 1. Arduino IDE and ESP32 support

1. Install [Arduino IDE 2.x](https://www.arduino.cc/en/software).
2. In **File → Preferences → Additional Boards Manager URLs** add:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. In the **Boards Manager** install `esp32 by Espressif Systems` (version 3.x recommended).
4. In the **Library Manager** install:
   - **OneWire** (Paul Stoffregen et al.)
   - **DallasTemperature** (Miles Burton et al.)

The MAX6675 and the microSD card need no additional libraries (microSD uses the SD library bundled with the ESP32 core).

## 2. Download and configure

1. Get the repository (`git clone`, or **Code → Download ZIP** and extract it).
2. Open `firmware/HeatPulse/HeatPulse.ino` in the Arduino IDE. The folder and sketch names must match, so do not rename the file.
3. In `firmware/HeatPulse/`, copy `secrets.example.h` to **`secrets.h`** and fill in:
   - `WIFI_SSID`, `WIFI_PASSWORD` – your home Wi-Fi (**2.4 GHz**),
   - `AP_PASSWORD` – password of the fallback access point; **always change it** (at least 8 characters).

   `secrets.h` is listed in `.gitignore`, so your passwords never end up in git. If `secrets.h` is missing, the firmware builds with the example values and the compiler prints a warning.
4. Optionally change the time zone `TZ_INFO` at the top of `HeatPulse.ino` (the default is Central European Time with daylight saving).

## 3. Upload to the ESP32

1. Connect the ESP32 with a **data** micro-USB cable (some cables are charge-only).
2. Select **Tools → Board → ESP32 Dev Module** and the serial port.
3. Click **Upload**. If uploading fails, lower **Upload Speed** to 115200 and hold the **BOOT** button while `Connecting...` is shown.
4. Open the **Serial Monitor** at **115200 baud** – it prints the sensor and card status and the web interface IP address.

After uploading, the board can be powered from an ordinary 5 V USB adapter; no computer is needed.

## Wi-Fi

- The ESP32 supports **2.4 GHz** Wi-Fi only. On a dual-band router use the 2.4 GHz network and make sure your phone/computer is on the same LAN (beware of guest networks and client isolation).
- If the ESP32 cannot join your home Wi-Fi within 12 s at boot, it starts its **own access point** `HeatPulse` with the web interface at `http://192.168.4.1`. It then retries the home network every 5 minutes (only while nobody is connected to the access point) – typically after a power cut, when the router boots more slowly than the ESP32.
- On the home network the clock is synchronised via NTP. In access point mode there is usually no internet, so the clock is set from your browser as soon as you open the web page.

> [!WARNING]
> The web interface has no login, and the threshold settings can be changed by anyone on the local network. **Do not expose the logger to the internet** (no port forwarding).
