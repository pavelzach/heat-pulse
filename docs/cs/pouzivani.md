# Používání: web, prahy a data

## Webová stránka

Webové rozhraní je v angličtině. Po zadání IP adresy ESP32 do prohlížeče se zobrazí:

- aktuální teplota chladivové trubky (termočlánek K),
- aktuální přibližná teplota výstupní otopné vody (DS18B20),
- rozdíl „voda − cíl“, kde cíl je **ručně zadaná** hodnota (z tepelného čerpadla se nečte),
- graf obou teplot (1 bod/min, nejvýše 1440 bodů = 24 h od posledního restartu ESP32),
- sekce **místnosti** (např. sklepa) s čidlem SHT40 nebo AM2301 (**Room around the heat pump**): teplota, relativní vlhkost, rosný bod, absolutní vlhkost, graf za 24 h a **denní vývoj** až za rok (viz [teplota a vlhkost místnosti](#teplota-a-vlhkost-místnosti)),
- **odhadovaný** počet startů za 1 h a 24 h, průměrná délka teplé a chladné fáze a délka poslední teplé fáze, u běhů s hodnocením poor / acceptable / good / ideal (špatný / přijatelný / dobrý / ideální),
- **hodnocení délky běhů** za posledních 24 h (**Run length rating**): kolik běhů trvalo méně než 6 min, 6–10 min, 10–20 min a 20 min a déle (odkud hranice pocházejí, viz [cyklování tepelného čerpadla](cyklovani.md#jak-dlouhý-má-být-běh)),
- sekce **Storage** (úložiště): kapacita karty, obsazené a volné místo, stav kruhového archivu, počet denních souborů, nejstarší a nejnovější den, čas posledního úspěšného zápisu a počet záznamů čekajících v RAM (viz [ukládání dat](uloziste.md)),
- seznam denních souborů CSV ke stažení, soubor událostí `EVENTS.CSV` a denní souhrn místnosti `CELLAR.CSV`,
- nastavení prahů a cílové teploty vody (ukládá se do flash paměti ESP32).

**Odečtení bodu v grafu:** najeďte myší na kterýkoli graf (na telefonu na něj ťukněte) a zobrazí se čas a hodnoty nejbližšího bodu – třeba kdy začal běh (teplota trubky začne stoupat) nebo kdy se v místnosti větralo. V denním vývoji ukáže bublina denní průměr, minimum, maximum a počet čtení. Na dotykové obrazovce bublina zůstane, dokud neťuknete jinam.

## Odhad teplotních fází

Když kompresor běží, proudí plynovou chladivovou trubkou horký stlačený plyn a trubka se ohřeje. Když stojí, trubka chladne k teplotě okolí. Firmware z toho odhaduje, zda kompresor běží („teplá fáze“) nebo stojí („chladná fáze“).

Na webu se nastavují tři hodnoty:

| Nastavení | Výchozí | Význam |
|---|---|---|
| **Hot phase from** – teplá fáze od (`on`) | 45 °C | Když teplota trubky vydrží **alespoň 30 s na této hodnotě nebo nad ní**, začne teplá fáze (odhad: kompresor běží) a započítá se start. |
| **Cold phase up to** – chladná fáze do (`off`) | 35 °C | Když teplota trubky vydrží **alespoň 60 s na této hodnotě nebo pod ní**, začne chladná fáze (odhad: kompresor stojí) a uloží se délka dokončené teplé fáze. |
| **Target water temperature** – cílová teplota vody (`target`) | 28 °C | Jen **porovnávací hodnota** – opište sem požadovanou teplotu výstupní vody z ovladače tepelného čerpadla. Používá se pouze pro dlaždici **Water − target** (rozdíl voda − cíl) a sloupec `target_water_C` v CSV. Odhad fází ani tepelné čerpadlo **neovlivňuje**. Při ekvitermní regulaci se skutečný cíl mění s venkovní teplotou – hodnotu občas aktualizujte. |

Pásmo mezi oběma prahy (výchozí 35–45 °C) je **hystereze**: když je teplota v něm, stav se nemění. Díky tomu odhad „neposkakuje“, když teplota kolísá kolem jedné hodnoty. Práh teplé fáze proto musí být aspoň o 2 °C vyšší než práh chladné. Doby potvrzení 30 s a 60 s odfiltrují krátké výkyvy; jsou pevně v kódu (`HOT_CONFIRM_MS`, `COLD_CONFIRM_MS`).

```
 teplota trubky
   55 °C |        ______________
         |       /              \
   45 °C |------/----------------\------------------  práh teplé fáze (on)
         |     /                  \
   35 °C |----/--------------------\----------------  práh chladné fáze (off)
         |   /                      \_____________
   25 °C |__/
         +----------------------------------------> čas
              ^ start (≥ 45 °C 30 s)  ^ konec (≤ 35 °C 60 s)
              |<---- teplá fáze ----->|<- chladná fáze ...
```

### Brzká detekce startu

Plynová trubka se ohřívá pomalu: na testovaném Gree Versati III trvá od rozběhu kompresoru asi 2 minuty, než dosáhne prahu teplé fáze 27 °C. Aby byl start přesně datovaný, firmware sleduje i **rychlost nárůstu teploty trubky**:

- V chladné fázi se každé čtení porovnává s **nejnižším čtením za posledních 5 minut**. Když jsou dvě čtení po sobě aspoň o **2 °C** výš, je rozpoznán nárůst. Web pak ukáže **Starting (pipe rising)** (rozbíhá se) a `estimate` v API je `RISING` – obvykle 20–30 s po rozběhu kompresoru.
- Start se **započítá až po potvrzení prahem teplé fáze** (jako dosud: na prahu nebo nad ním aspoň 30 s), ale **datuje se na začátek nárůstu** – na poslední čtení předtím, než trubka začala stoupat. Délky běhů, délky chladných fází a starty za hodinu tak už nezahrnují minuty, kdy se trubka ohřívá.
- Nárůst, který se vrátí na výchozí hodnotu nebo do 20 minut nedosáhne prahu teplé fáze, se zahodí a nezapočítá. To pokrývá například trubku ohřátou jen protékající vodou, když kompresor stojí.
- Bez rozpoznaného nárůstu (např. velmi pomalé ohřátí) se start datuje na překročení prahu jako dřív.
- Sloupec `thermal_estimate` v CSV zůstává `COLD`, dokud start není potvrzený; událost `THERMAL_HOT` se zapíše při potvrzení a její `duration_s` (předchozí chladná fáze) se počítá od datovaného startu.

Meze detekce jsou konstanty na začátku [`HeatPulse.ino`](../../firmware/HeatPulse/HeatPulse.ino): `RISE_START_C` (2 °C), `RISE_WINDOW_SAMPLES` (30 čtení = 5 min), `RISE_CONFIRM_SAMPLES` (2) a `RISE_VERIFY_MS` (20 min).

> [!TIP]
> Pokud oběhové čerpadlo běží i při stojícím kompresoru a voda je teplejší než práh teplé fáze, může trubku ohřát samotná voda a falešný start se potvrdí. Práh teplé fáze pak nastavte nad nejvyšší teplotu vody.

### Jak prahy nastavit

Výchozí hodnoty jsou jen odhad. Skutečná teplota plynové trubky závisí na tepelném čerpadle, místě měření, izolaci a hlavně na teplotě vody: při nízké výstupní teplotě (např. 28 °C) a nízkém výkonu kompresoru může být trubka za chodu jen kolem 40 °C a práh 45 °C by start vůbec nezachytil.

1. Nechte logger běžet aspoň několik cyklů a sledujte graf spolu se stavem kompresoru na ovladači tepelného čerpadla (u Gree položka `Compressor`).
2. Zjistěte, **na jaké teplotě se trubka drží, když kompresor běží** (nejnižší ustálená hodnota), a **kam klesne, když stojí**.
3. Práh teplé fáze nastavte o několik stupňů **pod** teplotu za chodu, práh chladné fáze o několik stupňů **nad** teplotu v klidu. Mezi nimi nechte aspoň 5 °C.

   *Příklad:* za chodu 48–55 °C, v klidu klesne na 24 °C → teplá fáze od 42 °C, chladná fáze do 32 °C.
4. Ověřte, že počet odhadovaných startů odpovídá skutečnosti.

S brzkou detekcí startu práh teplé fáze start jen **potvrzuje**, takže ho lze bezpečně nastavit pod teplotu při běhu, aniž by se zhoršila přesnost času startu.

Po uložení prahů začne odhad znovu (aktuální fáze se nezapočítá); počítadla od startu ESP32 se nemažou. Prahy i cíl se ukládají do flash paměti ESP32 a restart přežijí.

Fáze, která už probíhala při startu ESP32 (nebo po změně prahů), nemá známý začátek, a proto se nezapočítává jako start ani do průměrů.

> [!IMPORTANT]
> Jde o **odhad z teploty**, ne o skutečný stav ani frekvenci kompresoru. Odmrazování a ohřev TUV mohou způsobit falešné přechody. Počet startů není servisně ověřený údaj.

## Teplota a vlhkost místnosti

S volitelným čidlem místnosti – SHT40 nebo AM2301 ([zapojení](zapojeni.md#teplota-a-vlhkost-místnosti-volitelné)) HeatPulse zaznamenává i místnost, kde tepelné čerpadlo stojí – hodí se třeba k tomu, abyste viděli, co doopravdy změnilo zateplení sklepa.

| Dlaždice | Význam |
|---|---|
| **Temperature**, **Relative humidity** | aktuální teplota a relativní vlhkost; pod nimi minimum–maximum z grafu za 24 h |
| **Dew point** (rosný bod) | povrchy chladnější než tato teplota vlhnou (kondenzace na stěnách, studených vodovodních trubkách) |
| **Absolute humidity** (absolutní vlhkost) | gramy vody v m³ vzduchu. Na rozdíl od relativní vlhkosti se nemění s teplotou |

**Denní vývoj** (**Daily trend**) ukazuje pro každý den pás (denní minimum–maximum) a čáru denního průměru – pro teplotu, relativní a absolutní vlhkost; rozsah lze přepnout na 30 dní, 90 dní a 1 rok. Dny s mezerou mezi sebou (logger byl vypnutý) se čarou nespojují.

> [!TIP]
> **Jak číst účinek zateplení sklepa:** porovnejte denní vývoj před a po. Když se místnost oteplí, její **relativní** vlhkost klesne, i když je ve vzduchu stejně vody. Teprve nižší **absolutní** vlhkost znamená, že je místnost opravdu sušší (méně vlhkosti proniká dovnitř nebo se lépe větrá). Rosný bod výrazně pod teplotou stěn znamená, že se na nich nesráží voda. Obě hodnoty ovlivňuje i počasí venku – porovnávejte období s podobným počasím.

Jak vzniká denní souhrn:

- Každé platné čtení (10 s) se započítá do dnešního minima, maxima a průměru. Rozpracovaný den se každých 10 minut ukládá do flash paměti ESP32 (NVS), takže restart během dne den nepřeruší a nezačne se od nuly.
- O půlnoci se den uzavře a zapíše jako jeden řádek do `/CELLAR.CSV`. Pokud byl logger o půlnoci vypnutý, den se uzavře po jeho dalším startu. Sloupec `samples` ukazuje, jak úplný den je: 8640 = všechna čtení celého dne.
- Čtení bez synchronizovaných hodin se do žádného dne nezapočítají.
- Web zobrazí až 400 dní; samotný `CELLAR.CSV` uchovává vše a [kruhový archiv](uloziste.md#kruhový-archiv) ho nikdy nemaže.
- Čidlo se čte každých 10 s. Jedno nepovedené čtení (např. rušení na kabelu) se překlene předchozí hodnotou; po třech chybách za sebou se hodnota hlásí jako chyba a sloupce v CSV zůstanou prázdné. Do denního souhrnu se počítají jen nová čtení, ne překlenutá.
- **Filtr odlehlých hodnot:** čidlo občas vrátí jediné čtení, které zjevně nesedí – např. jednu 10s hodnotu 19,6 °C v místnosti, která se drží na 18,0–18,2 °C. Takové jedno čtení by se stalo denním minimem nebo maximem. Web, graf za 24 h i denní souhrn (`CELLAR.CSV`, `/api/cellar`) proto používají **medián posledních 3 čtení**, zvlášť pro teplotu a vlhkost: jediná odlehlá hodnota vypadne, skutečná změna (vyvětrání, ohřátí od tepelného čerpadla) projde jen o 10 s později. Dvě odlehlá čtení za sebou by prošla. Po startu nebo výpadku čidla první dvě čtení jen naplní filtr: zobrazí se, ale do dne se nezapočítají.
- **Denní soubory CSV ukládají surová čtení** (`cellar_C`, `cellar_RH`) – i s odlehlými hodnotami, takže je uvidíte a filtrování si můžete zopakovat nebo upravit sami. Aktuální hodnota v `/api/status` je filtrovaná; Serial Monitor vypisuje obě (`cellar: 18.1 °C … (raw 19.6 °C …)`). Dny uzavřené před přidáním filtru (firmware 2.3) mají min/max nefiltrované; v případě potřeby je přepočítejte z denních souborů.
- **Čidlo SHT ve vzduchu blízko rosného bodu:** když je vlhkost 90 % RV a víc, firmware čidlu nejvýš jednou za hodinu zapne krátký ohřev (SHT4x: 110 mW na 1 s; SHT3x: jeho ohřev běží asi 10 s). Vysuší tím snímací prvek a omezí pomalý drift nahoru, který má každé čidlo vlhkosti ve velmi vlhkém vzduchu. Minutu po ohřevu, než čidlo vychladne, se neměří: web i CSV drží předchozí hodnotu a tyto řádky se nezapočítají do denního souhrnu. Serial Monitor vypíše `SHT4x: … % RH – heater pulse` (nebo `SHT3x: …`). Meze: `SHT_HEAT_ABOVE_RH`, `SHT_HEAT_INTERVAL_MS`, `SHT_HEAT_SETTLE_MS`.

Typ čidla se volí (nebo čidlo vypíná) konstantou `CELLAR_SENSOR` (`CELLAR_SHT`, `CELLAR_AM2301`, `CELLAR_NONE`) na začátku [`HeatPulse.ino`](../../firmware/HeatPulse/HeatPulse.ino); web pak sekci skryje.

## Data na microSD (CSV)

Každých **10 s** vznikne řádek pro soubor `/LOGS/YYYYMMDD.CSV` – každý den tedy vznikne nový soubor. Dokud nejsou hodiny synchronizované, zapisuje se do `NOSYNC.CSV` jen s časem od startu ESP32. Soubor `/EVENTS.CSV` obsahuje odhadované přechody mezi fázemi a `/CELLAR.CSV` denní souhrny čidla místnosti.

Řádky se shromažďují v RAM a na kartu se zapisují **každých 10 minut**, každý s původní časovou značkou a do denního souboru dne, kdy byl změřen. Soubor na kartě proto může být oproti webu až 10 minut pozadu; tlačítko **Write buffer to microSD now** zapíše data hned. Podrobnosti, kruhový archiv a omezení popisuje [ukládání dat](uloziste.md).

CSV používá **středník** jako oddělovač a **desetinnou tečku**. Při importu do Excelu nastavte tečku jako oddělovač desetinných míst. Názvy sloupců jsou záměrně anglické a bez diakritiky, aby se soubory snadno zpracovávaly skripty.

### Denní soubory `/LOGS/YYYYMMDD.CSV`

| Sloupec | Význam |
|---|---|
| `local_time` | místní čas ve formátu ISO 8601 s časovou zónou; prázdný před synchronizací hodin |
| `unix_s` | unixový čas; prázdný před synchronizací hodin |
| `uptime_ms` | milisekundy od posledního startu ESP32 (po restartu začíná od nuly) |
| `gas_C` | teplota chladivové trubky; prázdná při chybě MAX6675 |
| `water_C` | povrchová teplota výstupní otopné trubky; prázdná při chybě DS18B20 |
| `gas_valid` / `water_valid` | `1` = platné čtení, `0` = chyba čidla |
| `thermal_estimate` | odhad fáze: `HOT` (teplá), `COLD` (chladná), `UNKNOWN` (neurčeno) – **nikoli skutečný stav kompresoru** |
| `target_water_C` | ručně nastavená cílová teplota vody, nečte se z tepelného čerpadla |
| `cellar_C` | teplota místnosti z čidla místnosti (°C, rozlišení 0,1), **surová** – neprochází [filtrem odlehlých hodnot](#teplota-a-vlhkost-místnosti); prázdná při chybě čidla nebo když je čidlo vypnuté |
| `cellar_RH` | relativní vlhkost místnosti (%), surová; prázdná jako výše |
| `cellar_valid` | `1` = platné čtení čidla místnosti, `0` = chyba čidla nebo vypnuto |

> [!NOTE]
> Tři sloupce `cellar_*` přibyly ve firmwaru 2.3. V den aktualizace má denní soubor ještě starou hlavičku s 9 sloupci a jen řádky zapsané po aktualizaci mají 12 sloupců; od dalšího dne mají soubory novou hlavičku.

### Události `/EVENTS.CSV`

| Sloupec | Význam |
|---|---|
| `local_time`, `unix_s`, `uptime_ms` | čas události (stejně jako výše) |
| `event` | `THERMAL_HOT` = začátek teplé fáze, `THERMAL_COLD` = začátek chladné fáze |
| `gas_C`, `water_C` | teploty v okamžiku potvrzení přechodu |
| `duration_s` | délka **předchozí** fáze v sekundách; prázdná, pokud její začátek není znám |

### Denní souhrn místnosti `/CELLAR.CSV`

Jeden řádek za den, zapisuje se po půlnoci.

| Sloupec | Význam |
|---|---|
| `date` | místní datum `YYYY-MM-DD` |
| `t_min_C`, `t_avg_C`, `t_max_C` | minimální, průměrná a maximální teplota místnosti (po filtru odlehlých hodnot) |
| `rh_min_pct`, `rh_avg_pct`, `rh_max_pct` | minimální, průměrná a maximální relativní vlhkost (po filtru odlehlých hodnot) |
| `ah_avg_gm3` | průměrná absolutní vlhkost v g/m³ |
| `samples` | počet platných čtení (10 s) za den (8640 = celý den) |

### Chování při výpadcích

- **Výpadek Wi-Fi:** měření i zápis na kartu pokračují.
- **Výpadek napájení / restart:** ztratí se dosud nezapsané řádky – **nejvýše přibližně posledních 10 minut**. Graf a počítadla v RAM se smažou, soubory na kartě zůstanou.
- **microSD chybí nebo selhává:** měření a web běží dál a řádky čekají v RAM (až 3 hodiny). Firmware se každých 60 s pokusí kartu znovu připojit a pak čekající řádky zapíše do správných denních souborů, bez duplicit. Po 3 hodinách se nejstarší neuložené řádky zahazují a hlásí se to jako chyba.
- **Plná karta:** mažou se denní soubory starší než 365 dní, od nejstaršího; nic novějšího se automaticky nesmaže – místo toho se zápis pozastaví a web ukáže *insufficient space* (nedostatek místa). Viz [kruhový archiv](uloziste.md#kruhový-archiv).
- Stahování velkých CSV může krátce zpozdit měření, protože ESP32 obsluhuje web i měření v jedné smyčce.

## HTTP API

Webová stránka používá jednoduché API, které můžete volat i z vlastních skriptů (např. Home Assistant, Python):

| Metoda | Cesta | Popis |
|---|---|---|
| GET | `/api/status` | aktuální teploty, odhad fáze, statistiky a nastavení (JSON) |
| GET | `/api/history` | body grafu `[unix_s, gas_C, water_C, cellar_C, cellar_RH]` (JSON); `unix_s` je 0 před synchronizací hodin |
| GET | `/api/cellar` | denní souhrny místnosti `{"days": [[datum, t_min, t_avg, t_max, rh_min, rh_avg, rh_max, ah_avg, samples], …], "today": […] nebo null}` (JSON); `today` je rozpracovaný dnešek |
| GET | `/api/files` | seznam denních CSV (JSON) |
| GET | `/download?file=YYYYMMDD.CSV` | stažení denního CSV |
| GET | `/events.csv` | stažení souboru událostí |
| GET | `/cellar.csv` | stažení denního souhrnu místnosti |
| POST | `/api/config` | uložení prahů: `on`, `off`, `target` (form-urlencoded) |
| POST | `/api/time` | nastavení hodin: `epoch` (unixový čas); ignoruje se, jakmile čas nastavilo NTP |
| POST | `/api/flush` | zapsat RAM buffer na kartu hned |

Příklad odpovědi `/api/status`:

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

`runs_24h` obsahuje počet dokončených běhů za posledních 24 h podle délky: `[pod 6 min, 6–10 min, 10–20 min, 20 min a déle]`. Teploty jsou `null`, pokud čtení čidla není platné; průměry a `last_hot_s` jsou `null`, dokud není k dispozici žádná dokončená fáze. `estimate` nabývá hodnot `HOT` (teplá fáze), `COLD` (chladná fáze), `RISING` (chladná fáze, trubka stoupá – očekává se start, který čeká na potvrzení prahem teplé fáze), `UNKNOWN` (zatím neurčeno) nebo `SENSOR_ERROR` (žádné platné čtení termočlánku).

`storage.state` je `OK`, `WARNING` (méně než 15 % volného místa), `CLEANUP` (archiv maže soubory) nebo `ERROR`; `storage.reason` udává příčinu: `NO_CARD`, `WRITE_FAILED`, `NO_SPACE`, `BUFFER_OVERFLOW`, `CLEANUP`, `LOW_SPACE` nebo prázdný řetězec. `storage.archive` je `READY`, `NO_TIME` (mazání vypnuto, dokud hodiny nepotvrdí NTP), `CLEANUP` nebo `NO_SPACE` (zápis pozastaven). `buffered` je počet řádků čekajících v RAM, `dropped` počet záznamů ztracených od startu kvůli plnému RAM bufferu, `last_write` unixový čas posledního úspěšného zápisu (`null`, pokud není znám) a `next_write_s` počet sekund do dalšího pravidelného zápisu (`null`, když žádný zápis není naplánován). `oldest`/`newest` se určují z názvů denních souborů a jsou `null`, pokud žádný denní soubor neexistuje. Starší pole `sd` zůstává kvůli kompatibilitě (`true` = karta připojená a odpovídá).

`cellar` je `null`, když je čidlo místnosti vypnuté (`CELLAR_SENSOR = CELLAR_NONE`). `sensor` je `SHT4x`, `SHT3x`, `SHT` (čidlo I²C zatím nenalezeno) nebo `AM2301`. `t` (°C), `rh` (%), `dew` (rosný bod, °C) a `ah` (absolutní vlhkost, g/m³) jsou `null`, pokud není platné čtení. `errors` počítá nepovedená čtení od startu, `last_error` je příčina posledního z nich: `NO_RESPONSE` (čidlo neodpovídá – zapojení, napájení, pull-up; u čidel SHT i prohozené SDA/SCL), `TIMEOUT` (přerušený rámec nebo žádná data), `CHECKSUM` (poškozený rámec – rušení, dlouhý kabel) nebo `RANGE` (hodnota mimo rozsah čidla).
