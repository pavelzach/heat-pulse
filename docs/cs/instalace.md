# Instalace firmwaru

## 1. Arduino IDE a podpora ESP32

1. Nainstalujte [Arduino IDE 2.x](https://www.arduino.cc/en/software).
2. V **File → Preferences → Additional Boards Manager URLs** přidejte:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. V **Boards Manager** nainstalujte `esp32 by Espressif Systems` (doporučená verze 3.x).
4. V **Library Manager** nainstalujte knihovny:
   - **OneWire** (Paul Stoffregen a spol.)
   - **DallasTemperature** (Miles Burton a spol.)

MAX6675 i microSD obsluhuje firmware bez dalších knihoven (microSD využívá knihovnu SD z jádra ESP32).

## 2. Stažení a nastavení

1. Stáhněte repozitář (`git clone` nebo **Code → Download ZIP** a rozbalte).
2. Otevřete v Arduino IDE soubor `firmware/HeatPulse/HeatPulse.ino`. Název složky a sketche se musí shodovat, proto soubor nepřejmenovávejte.
3. Ve složce `firmware/HeatPulse/` zkopírujte `secrets.example.h` jako **`secrets.h`** a vyplňte:
   - `WIFI_SSID`, `WIFI_PASSWORD` – vaše domácí Wi-Fi (**2,4 GHz**),
   - `AP_PASSWORD` – heslo záložní vlastní Wi-Fi, **vždy změňte** (aspoň 8 znaků).

   Soubor `secrets.h` je v `.gitignore`, takže se vaše hesla nedostanou do gitu. Pokud `secrets.h` chybí, firmware se přeloží s ukázkovými hodnotami a překladač vypíše varování.
4. Volitelně upravte časové pásmo `TZ_INFO` na začátku `HeatPulse.ino` (výchozí je střední Evropa včetně letního času).

## 3. Nahrání do ESP32

1. Připojte ESP32 **datovým** micro-USB kabelem (některé kabely jsou jen nabíjecí).
2. **Tools → Board → ESP32 Dev Module**, vyberte sériový port.
3. Klikněte na **Upload**. Pokud nahrávání selhává, snižte **Upload Speed** na 115200 a při hlášce `Connecting...` podržte tlačítko **BOOT**.
4. Otevřete **Serial Monitor** s rychlostí **115200 baud** – vypíše se stav čidel, karty a IP adresa webu.

Po nahrání můžete desku napájet z běžného 5V USB adaptéru, počítač už není potřeba.

## Wi-Fi

- ESP32 podporuje pouze **2,4 GHz** Wi-Fi. U dvoupásmového routeru použijte 2,4GHz síť a zajistěte, aby telefon/počítač byl ve stejné LAN (pozor na hostovskou síť a izolaci klientů).
- Pokud se ESP32 při startu nepřipojí k domácí Wi-Fi do 12 s, spustí **vlastní Wi-Fi** `HeatPulse` s webem na `http://192.168.4.1`. K domácí síti se pak zkouší znovu připojit každých 5 minut (jen když na vlastní Wi-Fi není nikdo připojený) – typicky po výpadku proudu, kdy router startuje déle než ESP32.
- V domácí síti se čas synchronizuje z NTP. V režimu vlastní Wi-Fi internet obvykle není a hodiny se nastaví z prohlížeče, jakmile otevřete webovou stránku.

> [!WARNING]
> Webové rozhraní nemá přihlašování a nastavení prahů je dostupné komukoli v lokální síti. **Nevystavujte logger do internetu** (žádné přesměrování portů).
