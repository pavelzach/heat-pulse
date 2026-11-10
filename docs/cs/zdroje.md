# Technické zdroje

Ověřeno 12. 10. 2026.

## Gree Versati III GRS-CQ10Pd/NhH-M

- Přehled manuálů pro model: https://www.manualslib.com/products/Gree-Versati-Iii-Grs-Cq10pd-Nhh-M-11779741.html
- Servisní manuál řady, technická data varianty `GRS-CQ10Pd/NhH-M`: https://www.scribd.com/document/675203532/versati-iii-nhh-split-service-manual
  - kap. 1.5.1: body A7/W35 a A7/W45 (u 10kW varianty cca 9,5 kW a COP 4,6 / 3,6; liší se podle revize a podmínek),
  - kap. 4.5.7: minimální objem vody pro odmrazování **5 l/kW**, tj. **50 l pro 10 kW**,
  - kap. 3.2.3 a 3.2.4: `WOT-Heat`, `ΔT-Heat` a omezení zásahů do servisního nastavení.
- České znění dokumentace a funkce `Weather depend`: https://www.notice-facile.com/cs/manual/1725706/gree%2Bversati-iii-split
- Instalační manuál Gree na stránkách zastoupení výrobce: https://gree.gr/wp-content/uploads/2025/07/600005064924-Installation-manual-for-the-rest.pdf
- Servisní parametry Gree Versati III (rozsahy a výchozí hodnoty WOT-Heat, ΔT-Heat): https://www.manualslib.com/manual/1561334/Gree-Versati-Iii-Series.html?page=54
- Manuál ovládacího panelu Gree Versati (ΔT-Heat 2–10 °C, výchozí 10 °C): https://www.nhbioservice.dk/wp-content/uploads/2024/09/Panel-manual-CopMax-High-Power-engelsk.pdf
- Řídicí logika jednotky Gree Versati II (modulace kompresoru podle výstupní vody, vypnutí na minimální frekvenci, 3minutová prodleva před startem): https://man.kievclimate.com/man/Gree/Versati/Versati%20II/Versati%202016%202-f/4.Unit%20Control.pdf

> [!NOTE]
> Dostupné dokumenty pokrývají **více generací a revizí** (označení H2/H5). Před jakýmkoli zásahem do servisních parametrů ověřte štítek a servisní manuál konkrétní jednotky. Minimum modulace kompresoru v konkrétních podmínkách **není v uvedených zdrojích spolehlivě doložené**.

## Cyklování a účinnost

- EA Technology pro britskou vládu – The Effects of Cycling on Heat Pump Performance (běhy < 6 min snižují COP, ~8 min se blíží katalogovému COP): https://assets.publishing.service.gov.uk/government/uploads/system/uploads/attachment_data/file/65695/7389-effects-cycling-heat-pump-performance.pdf
- Brudermueller a kol. 2023 – cyklování 503 tepelných čerpadel v provozu: https://www.earth.org.uk/bibliography/brudermueller2023cycling.html
- Rozběh a doběh: https://iifiir.org/en/fridoc/theoretical-and-experimental-investigation-of-startup-and-shutdown-135447 a https://rehva.eu/rehva-journal/chapter/improved-characterization-of-water-to-water-heat-pumps-part-load-performance
- Bitzer – četnost cyklů a minimální doba chodu (≥ 5 min): https://bitzer.de/shared_media/html/st-420/en-GB/327407627327409419.html
- Teplota otopné vody a účinnost (1,5–3 % na 1 K): https://www.cibsejournal.com/cibsejournal-magazine/202308/50/page.html, https://edm.linemetrics.com/en/massnahmen/geb-heiz-010, https://www.idm-energie.at/en/heat-pump-efficiency/flow-temperature/
- Praktické zkušenosti s cyklováním v mírném počasí: https://renewableheatinghub.co.uk/forums/renewable-heating-air-source-heap-pumps-ashps/performance-of-heat-pumps-in-mild-weather, https://community.openenergymonitor.org/t/heat-pump-demand-cycling-vs-compressor-load-cycling/29277, https://homey.app/en-ae/wiki/common-heat-pump-mistakes-and-how-to-prevent-them/

## Elektronika

- LaskaKit DS18B20 vodotěsné čidlo 3 m (LA131090) – vodiče červený VDD / černý GND / žlutý DQ, napájení 3,0–5,5 V, rozsah −55 až +125 °C: https://www.laskakit.cz/dallas-digitalni-vodotesne-cidlo-teploty-ds18b20-3m/
- LaskaKit microSD modul LA161078, napájení a SPI 3,3 V: https://www.laskakit.cz/laskakit-microsd-card-modul/
- LaskaKit převodník MAX6675 + termočlánek K 800 °C M6 0,5 m (LA131099): https://www.laskakit.cz/prevodnik-max6675-termoclanek-typu-k-800c-m6-0-5m/
- Poznámky k modulu MAX6675 (konec sondy ani kovový oplet nesmí být uzemněné): https://protosupplies.com/product/max6675-thermocouple-temperature-module/
- Datasheet MAX6675 (Analog Devices) – rozsah 0 až 1024 °C, SPI, T− musí být spojená se zemí: https://www.analog.com/media/en/technical-documentation/data-sheets/MAX6675.pdf
- Datasheet DS18B20 (Analog Devices / Maxim) – 1-Wire, pull-up, napájení: https://www.analog.com/media/en/technical-documentation/data-sheets/ds18b20.pdf
- ESP32-DevKitC – napájení a rozložení pinů: https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32/esp32-devkitc/user_guide.html
- Dokumentace Arduino-ESP32: https://docs.espressif.com/projects/arduino-esp32/en/latest/
- URL pro Boards Manager: https://espressif.github.io/arduino-esp32/package_esp32_index.json
