# References

Checked on 12 October 2026.

## Gree Versati III GRS-CQ10Pd/NhH-M

- Manual overview for the model: https://www.manualslib.com/products/Gree-Versati-Iii-Grs-Cq10pd-Nhh-M-11779741.html
- Service manual of the range, technical data for the `GRS-CQ10Pd/NhH-M` variant: https://www.scribd.com/document/675203532/versati-iii-nhh-split-service-manual
  - ch. 1.5.1: A7/W35 and A7/W45 points (about 9.5 kW and COP 4.6 / 3.6 for the 10 kW variant; varies by revision and conditions),
  - ch. 4.5.7: minimum water volume for defrosting **5 l/kW**, i.e. **50 l for 10 kW**,
  - ch. 3.2.3 and 3.2.4: `WOT-Heat`, `ΔT-Heat` and restrictions on changing service settings.
- Czech documentation and the `Weather depend` function: https://www.notice-facile.com/cs/manual/1725706/gree%2Bversati-iii-split
- Gree installation manual hosted by a manufacturer's representative: https://gree.gr/wp-content/uploads/2025/07/600005064924-Installation-manual-for-the-rest.pdf
- Gree Versati III commissioning parameters (WOT-Heat, ΔT-Heat ranges and defaults): https://www.manualslib.com/manual/1561334/Gree-Versati-Iii-Series.html?page=54
- Gree Versati control panel manual (ΔT-Heat 2–10 °C, default 10 °C): https://www.nhbioservice.dk/wp-content/uploads/2024/09/Panel-manual-CopMax-High-Power-engelsk.pdf
- Gree Versati II unit control logic (compressor modulation by leaving water temperature, shutdown at minimum frequency, 3-minute restart delay): https://man.kievclimate.com/man/Gree/Versati/Versati%20II/Versati%202016%202-f/4.Unit%20Control.pdf

> [!NOTE]
> The available documents cover **several generations and revisions** (labelled H2/H5). Before changing any service parameter, check the rating plate and the service manual for your specific unit. The minimum compressor modulation under specific conditions is **not reliably documented** in these sources.

## Cycling and efficiency

- EA Technology for the UK government – The Effects of Cycling on Heat Pump Performance (runs < 6 min reduce COP, ~8 min close to catalogue COP): https://assets.publishing.service.gov.uk/government/uploads/system/uploads/attachment_data/file/65695/7389-effects-cycling-heat-pump-performance.pdf
- Brudermueller et al. 2023 – cycling of 503 heat pumps in the field: https://www.earth.org.uk/bibliography/brudermueller2023cycling.html
- Start-up and shutdown transients: https://iifiir.org/en/fridoc/theoretical-and-experimental-investigation-of-startup-and-shutdown-135447 and https://rehva.eu/rehva-journal/chapter/improved-characterization-of-water-to-water-heat-pumps-part-load-performance
- Bitzer – cycling rate and minimum running times (≥ 5 min): https://bitzer.de/shared_media/html/st-420/en-GB/327407627327409419.html
- Flow temperature and efficiency (1.5–3 % per K): https://www.cibsejournal.com/cibsejournal-magazine/202308/50/page.html, https://edm.linemetrics.com/en/massnahmen/geb-heiz-010, https://www.idm-energie.at/en/heat-pump-efficiency/flow-temperature/
- Practical experience with mild-weather cycling: https://renewableheatinghub.co.uk/forums/renewable-heating-air-source-heap-pumps-ashps/performance-of-heat-pumps-in-mild-weather, https://community.openenergymonitor.org/t/heat-pump-demand-cycling-vs-compressor-load-cycling/29277, https://homey.app/en-ae/wiki/common-heat-pump-mistakes-and-how-to-prevent-them/

## Electronics

- LaskaKit DS18B20 waterproof sensor, 3 m (LA131090) – wires red VDD / black GND / yellow DQ, supply 3.0–5.5 V, range −55 to +125 °C: https://www.laskakit.cz/dallas-digitalni-vodotesne-cidlo-teploty-ds18b20-3m/
- LaskaKit microSD module LA161078, 3.3 V supply and SPI: https://www.laskakit.cz/laskakit-microsd-card-modul/
- LaskaKit MAX6675 + type K thermocouple 800 °C M6 0.5 m (LA131099): https://www.laskakit.cz/prevodnik-max6675-termoclanek-typu-k-800c-m6-0-5m/
- MAX6675 thermocouple module notes (probe end and braided shield must not be grounded): https://protosupplies.com/product/max6675-thermocouple-temperature-module/
- MAX6675 datasheet (Analog Devices) – 0 to 1024 °C range, SPI, T− must be connected to ground: https://www.analog.com/media/en/technical-documentation/data-sheets/MAX6675.pdf
- DS18B20 datasheet (Analog Devices / Maxim) – 1-Wire, pull-up, power supply: https://www.analog.com/media/en/technical-documentation/data-sheets/ds18b20.pdf
- ESP32-DevKitC – power supply and pin layout: https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32/esp32-devkitc/user_guide.html
- Arduino-ESP32 documentation: https://docs.espressif.com/projects/arduino-esp32/en/latest/
- Boards Manager URL: https://espressif.github.io/arduino-esp32/package_esp32_index.json
