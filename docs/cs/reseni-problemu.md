# Řešení problémů

| Projev | Co zkontrolovat |
|---|---|
| Sketch nejde nahrát | datový (ne jen nabíjecí) micro-USB kabel, COM port, deska **ESP32 Dev Module**, podržení tlačítka BOOT, ovladač USB–UART (CP210x / CH340) |
| Chyba `OneWire.h` / `DallasTemperature.h` | doinstalujte knihovny v Library Manageru |
| Varování „secrets.h not found“ | zkopírujte `secrets.example.h` jako `secrets.h` a vyplňte Wi-Fi ([instalace](instalace.md#2-stažení-a-nastavení)) |
| microSD hlásí chybu | formát FAT32, napájení 3,3 V, krátké vodiče, CS = GPIO21, SPI = GPIO18/19/23, kvalitní USB adaptér |
| Storage ukazuje **ERROR – card missing** (karta chybí) | karta správně zasunutá, FAT32, zapojení viz výše; firmware ji každých 60 s znovu připojuje a data čekající v RAM (až 3 h) pak zapíše. Podrobnosti na Serial Monitoru (`SD ERROR: …`) |
| Storage ukazuje **ERROR – insufficient space** (nedostatek místa) | volno pod 10 % a žádný denní soubor starší než 365 dní; zkopírujte CSV do PC a smažte z karty staré soubory (nebo jiné soubory), případně použijte větší kartu. Zápis se obnoví sám ([kruhový archiv](uloziste.md#kruhový-archiv)) |
| Archiv ukazuje **waiting for NTP time** | staré soubory se mažou jen s časem potvrzeným z NTP – zajistěte loggeru přístup k internetu |
| V dnešním CSV chybí poslední minuty | řádky se zapisují každých 10 min; před stažením stiskněte **Write buffer to microSD now** |
| MAX6675 vrací chybu | polarita termočlánku na `T+`/`T−`, CS = GPIO27, SCK = GPIO26, SO = GPIO25, uzemněný plášť sondy, teplota pod 0 °C |
| DS18B20 neměří | **rezistor 4,7 kΩ mezi GPIO32 a 3V3** (ne v sérii!), barvy vodičů (červený 3V3, černý GND, žlutý GPIO32), kabel, rušení. Serial Monitor po startu vypíše počet nalezených čidel |
| Sekce místnosti: **Sensor not responding** (`NO_RESPONSE`), SHT40 / SHT31 | SDA na **GPIO33**, SCL na **GPIO22** (ne prohozené), VCC na 3V3, GND; `CELLAR_SENSOR = CELLAR_SHT`; u sondy na kabelu 4,7 kΩ z SDA a SCL na 3V3. Jakmile čidlo odpoví, Serial Monitor vypíše `SHT4x found at I2C address 0x44` (nebo `SHT3x`, nebo 0x45). Napište do Serial Monitoru `i2c`: ukáže, jestli mají oba vodiče pull-up a které adresy odpovídají, i s prohozenými SDA a SCL (`answering: 0x44` jen v prohozeném řádku = zelený a žlutý vodič prohozené) |
| SHT40 ukazuje několik dní příliš vysokou vlhkost | po dlouhém pobytu ve vzduchu blízko rosného bodu se každé čidlo vlhkosti posune nahoru; ohřevy z firmwaru to omezují. LaskaKit popisuje delší „refresh“ (ohřev asi 90 min) pro čidlo, které měří špatně |
| Sekce místnosti: **Sensor not responding** (`NO_RESPONSE`), AM2301 | AM2301 na **GPIO33** (ne na vodiči DS18B20 – není to čidlo 1-Wire), **4,7 kΩ mezi GPIO33 a 3V3**, červený vodič na 3V3, černý na GND; `CELLAR_SENSOR = CELLAR_AM2301` |
| Sekce místnosti: *Failed readings* stále rostou (`CHECKSUM`, `TIMEOUT`) | AM2301: příliš dlouhý kabel nebo vedený u silových kabelů, chybí vnější pull-up; při 3,3 V držte kabel krátký (asi 1 m). SHT40: vodiče delší než asi 1 m nebo vedené u silových kabelů. Jednotlivé chyby nevadí – překlene je předchozí hodnota |
| Denní vývoj místnosti je prázdný | první souhrn vznikne s prvním čtením se synchronizovanými hodinami; celý den se objeví po půlnoci. Rozpracovaný den se zobrazuje jako „today so far“ |
| Vlhkost místnosti se zdá příliš vysoká / nízká | čidlo u zdroje tepla, v krabičce s elektronikou nebo nedávno orosené (vzpamatuje se za několik hodin); tolerance ±3 % RV. Porovnejte s jiným vlhkoměrem položeným vedle |
| DS18B20 ukazuje chybu po připojení za chodu | firmware čidlo hledá znovu při každém měření; vyčkejte 10 s |
| Web nejde otevřít | 2,4GHz Wi-Fi, IP adresa v Serial Monitoru (115200 baud), izolace klientů / hostovská síť, záložní vlastní Wi-Fi `HeatPulse` na `http://192.168.4.1` |
| První řádky CSV bez času | hodiny ještě nejsou synchronizované – otevřete web z telefonu/PC se správným časem nebo zajistěte přístup k internetu (NTP) |
| Graf je po restartu prázdný | historie grafu je jen v RAM; data na microSD zůstávají |
| Mnoho „startů“ při odmrazování | falešné přechody z teploty; porovnejte se skutečným stavem `Compressor` na ovladači a případně upravte [prahy](pouzivani.md#jak-prahy-nastavit) |
| Starty se nezaznamenají, i když kompresor běží | trubka nedosáhne prahu teplé fáze – snižte ho podle [nastavení prahů](pouzivani.md#jak-prahy-nastavit) |
