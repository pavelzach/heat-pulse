# Ukládání dat: RAM buffer, microSD a kruhový archiv

Od firmwaru 2.2 logger nezapisuje na microSD po každém měření. Tato stránka popisuje, jak se data dostanou z čidel na kartu, co se stane při poruše a jaká omezení je dobré znát.

## Přehled

```
 každých 10 s                       každých 10 min (úloha sdWriter)
 ┌─────────────┐  řádek   ┌──────────────────────┐  zápis + ověření  ┌────────────────────┐
 │ měření      │ ───────► │ RAM buffer           │ ────────────────► │ /LOGS/YYYYMMDD.CSV │
 │ (loop)      │          │ 1080 řádků = 3 h     │                   │ /EVENTS.CSV        │
 └─────┬───────┘          │ 128 událostí fází    │ ◄──────────────── └────────────────────┘
       │                  └──────────────────────┘  řádky se uvolní až
       ▼                                            po ověřeném zápisu
 web: hodnoty, graf a statistiky se aktualizují okamžitě
```

- **Měření se nemění:** každých 10 s, MAX6675 + DS18B20. Aktuální hodnoty, graf i statistiky cyklování se aktualizují okamžitě jako dosud.
- Každé měření se uloží jako záznam do **RAM bufferu** i s původní časovou značkou (unixový čas a čas od startu), teplotami, příznaky platnosti, odhadem fáze a cílovou teplotou.
- Každých **10 minut** samostatná úloha FreeRTOS (`sdWriter`) zapíše všechny čekající řádky na kartu – jedním připsáním do každého denního souboru.
- Události fází (`THERMAL_HOT`, `THERMAL_COLD`) používají stejný mechanismus s vlastním bufferem a zapisují se do `/EVENTS.CSV` ve stejném cyklu.
- Formát CSV je **bajt po bajtu stejný** jako ve verzi 2.1: stejné názvy souborů, sloupce, hlavička, formát čísel i konce řádků (CRLF). Stávající skripty a tabulky fungují dál. (Firmware 2.3 přidává na konec každého řádku tři sloupce čidla místnosti, viz [používání](pouzivani.md#denní-soubory-logsyyyymmddcsv).)

### Proč

Zápis každých 10 s znamená 8 640 otevření/zápisů/zavření souboru denně a pokaždé se přepisuje FAT a položka adresáře. Zápis po 10 minutách to snižuje 60× (144× denně), šetří kartu a zvyšuje odolnost: pomalá nebo dočasně nedostupná karta už měření vůbec neovlivní.

## Zápis podrobně

Zapisovací úloha běží na stejném jádře a se stejnou prioritou jako měřicí smyčka. FreeRTOS mezi nimi střídá čas procesoru, takže ani pomalá operace s kartou (připojení, procházení adresáře, zaneprázdněná karta) nezastaví měření ani webový server.

Pro každý denní soubor úloha:

1. vezme až 64 po sobě jdoucích řádků **téhož kalendářního dne** (den se počítá z časové značky každého řádku – řádky z 23:58 a 00:02 skončí ve dvou různých souborech),
2. naformátuje je do statického textového bufferu,
3. zapamatuje si aktuální velikost souboru (pro případné vrácení, viz níže),
4. otevře soubor, u nového souboru zapíše hlavičku, zapíše řádky,
5. ověří, že `fwrite` zapsal všechny bajty, pak volá `fflush`, `fsync` a `fclose` – návratová hodnota každého kroku se kontroluje,
6. ověří, že soubor má nyní **přesně** původní velikost + počet zapsaných bajtů,
7. teprve potom tyto řádky odstraní z RAM bufferu.

Všechny operace s kartou (zapisovací úloha, stahování CSV, seznam souborů) jsou serializované mutexem – kartu tak nelze znovu připojit ani soubor smazat, zatímco se stahuje.

### Neúspěšný zápis a opakování bez duplicit

Pokud kterýkoli krok selže, řádky **zůstanou v RAM** a nejsou označené jako uložené. Firmware vypíše na Serial Monitor, který krok selhal (včetně `errno`), označí kartu jako nedostupnou, po 60 s ji znovu připojí a zápis zopakuje.

Neúspěšný zápis mohl část řádků do souboru přece jen dostat. Před dalším zápisem proto firmware **zkrátí soubor zpět na velikost, kterou měl před neúspěšným pokusem** (`truncate`), a teprve pak řádky připíše znovu. Stejný řádek se tak nikdy neobjeví dvakrát a uprostřed souboru nezůstane půlka řádku. Pro jistotu se zkrácení neprovede, pokud soubor narostl o víc, než mohl neúspěšný zápis zapsat, nebo pokud je teď vložená karta s jinou kapacitou – pak to nejsou naše bajty.

Pokud soubor nekončí koncem řádku (např. po výpadku napájení uprostřed zápisu starší verzí firmwaru), další zápis nejdřív začne nový řádek, aby se nové řádky nepřilepily k poškozenému.

### Půlnoc, letní čas a hodiny

- Každý řádek nese čas, kdy byl **změřen**, ne kdy byl zapsán. Z něj se odvozuje název souboru i `local_time`, takže řádky skončí ve správném denním souboru, i když se zapisují o minuty později, a to i přes změnu letního času.
- Řádky změřené před synchronizací hodin jdou do `NOSYNC.CSV` s prázdnými časovými sloupci – jako dosud.
- Jakmile hodiny nastaví NTP, ESP32 udržuje čas i při výpadku NTP nebo Wi-Fi. Čas z prohlížeče (v režimu vlastní Wi-Fi) už čas nastavený z NTP nepřepíše.

## Kruhový archiv

Zapisovací úloha kontroluje volné místo na kartě před každým zápisem (každých 10 minut, při problému každou minutu).

| Situace | Co firmware udělá |
|---|---|
| volno ≥ 15 % | nic, stav **OK** |
| 10 % ≤ volno < 15 % | zapisuje dál, stav **WARNING** |
| volno < 10 % | **úklid** (stav **CLEANUP**): maže **nejstarší** denní soubory jeden po druhém, dokud není volných aspoň **15 %** – ale jen soubory **starší než 365 dní** |
| stále < 10 % a žádný soubor starší než 365 dní | **nic novějšího se nesmaže**; stav **ERROR – nedostatek místa**, zápis se **pozastaví**, data čekají v RAM |

Pravidla, která se nikdy neporuší:

- Smazat lze jen soubory v `/LOGS`, jejichž název je přesně platné datum `YYYYMMDD.CSV` (např. `20251013.CSV`). `NOSYNC.CSV`, `/EVENTS.CSV`, `/CELLAR.CSV`, jiné soubory, složky a cokoli mimo `/LOGS` se nikdy nemaže.
- Stáří souboru se určuje z jeho **názvu**, ne z časové značky FAT (ta je u souborů zapsaných před nastavením hodin chybná).
- Nic se nemaže, dokud hodiny od startu **nepotvrdilo NTP**. S ručně nastaveným časem nebo časem z prohlížeče archiv jen ohlásí nedostatek místa a pozastaví zápis. Špatně nastavené hodiny tak nemohou způsobit chybné mazání.
- Dnešní soubor, soubory, jejichž řádky ještě čekají v RAM, a soubor s nedokončeným zápisem jsou chráněné.
- Po každém smazání se volné místo změří znovu; Serial Monitor vypíše název souboru, jeho velikost, skutečně uvolněné místo a nové procento volného místa (`ARCHIVE: deleted …`).

Zápis se automaticky obnoví, jakmile je volného místa znovu víc než 10 % (smažte soubory z karty v PC nebo vložte větší kartu).

Při zhruba 0,8 MB dat denně (≈ 300 MB ročně) pojme karta 4 GB přes deset let dat – úklid je hlavně pojistka pro malé karty nebo karty s dalšími daty.

### Nastavení

Na začátku `HeatPulse.ino`:

```cpp
constexpr uint8_t SD_MIN_FREE_PERCENT = 10;     // pod tímto volným místem začne úklid
constexpr uint8_t SD_TARGET_FREE_PERCENT = 15;  // cíl úklidu / hranice varování
constexpr uint16_t SD_RETENTION_DAYS = 365;     // mladší soubory se automaticky nikdy nemažou
constexpr bool SD_PRUNE_EXPIRED_ALWAYS = false; // true = mazat soubory starší než retence i při dostatku místa
constexpr uint32_t SD_FLUSH_MS = 600000;        // interval zápisu (10 min)
constexpr size_t LOG_BUFFER_RECORDS = 1080;     // velikost RAM bufferu (3 h řádků)
constexpr size_t EVENT_BUFFER_RECORDS = 128;    // RAM buffer pro události fází
```

S `SD_PRUNE_EXPIRED_ALWAYS = false` (výchozí) zůstávají staré soubory, dokud je místo, takže archiv může obsahovat i víc než rok. Nastavte `true`, pokud má karta obsahovat nejvýše `SD_RETENTION_DAYS` dní.

## Chování při poruchách a omezení

### Výpadek napájení

Ztratí se řádky, které jsou jen v RAM: **nejvýše přibližně posledních 10 minut** (60 řádků) a události z tohoto období. Data už zapsaná na kartu ohrožena nejsou – každý zápis končí `fsync` a `fclose`. Pokud chcete logger odpojit, stiskněte nejdřív na webu **Write buffer to microSD now** (zapsat buffer na microSD). Stejné tlačítko se hodí před stažením dnešního CSV, protože soubor na kartě je až o 10 minut pozadu.

### Karta vyjmutá nebo neodpovídá

- Měření, web, graf i statistiky běží dál.
- Karta se každých 60 s kontroluje čtením sektoru (bez zápisu); neúspěšný zápis se pozná okamžitě. Stav **ERROR – karta chybí nebo neodpovídá**.
- Data čekají v RAM; firmware se každých 60 s pokouší kartu připojit. Po vrácení karty (stejné nebo jiné karty FAT32) se automaticky připojí a čekající řádky se hned zapíšou – do správných denních souborů.
- RAM buffer pojme **3 hodiny**. Je-li karta nedostupná déle, zahazují se **nejstarší** neuložené řádky, aby přežily ty nejnovější. Hlásí se to jako chyba na Serial Monitoru (`ERROR: RAM buffer full …`) i na webu (počet ztracených záznamů od startu).
- Kartu nevytahujte během zápisu (web ukazuje čas posledního zápisu; zápis trvá každých 10 minut výrazně méně než sekundu). I tehdy ale opakování s vrácením zápisu zabrání duplicitám.

### Plná karta

Viz [kruhový archiv](#kruhový-archiv). Během pozastavení zápisu čekají data v RAM nejvýše 3 hodiny, pak se začnou zahazovat nejstarší neuložené řádky (hlásí se jako chyba).

### Paměť

RAM buffery (≈ 39 KB) se alokují jednou při startu a nikdy se neuvolňují; zapisovací úloha používá statické buffery a žádné objekty `String`, takže dlouhodobý provoz nefragmentuje haldu. Volná halda, minimum volné haldy a největší volný blok se vypisují po každém zápisu na Serial Monitor; `/api/status` vrací i `heap_free` a `heap_min`.

### Stahování

Stahování CSV drží kartu po celou dobu přenosu. Zapisovací úloha počká (data zůstávají v RAM). Pokud karta právě zapisuje, webový server po 1,5 s odpoví `503`, místo aby blokoval měření; stačí to zkusit znovu. Stažení velkého souboru může jako dosud krátce zdržet jedno měření.

## Webová stránka – sekce Storage (Úložiště)

| Položka | Význam |
|---|---|
| štítek stavu | **OK**, **WARNING** (< 15 % volno), **CLEANUP** (maže staré soubory), **ERROR** (karta chybí, zápis selhal, nedostatek místa nebo ztracené záznamy) s vysvětlením |
| Capacity / Used / Free / Usage | kapacita, obsazeno, volno a využití svazku FAT32 na kartě |
| Daily CSV files | počet souborů `YYYYMMDD.CSV` (+ `NOSYNC`, pokud existuje) |
| Oldest / newest day | datum nejstaršího a nejnovějšího denního souboru (z názvů souborů) |
| Last successful write | čas a stáří posledního ověřeného zápisu |
| Unsaved records in RAM | řádky (a události) čekající na zápis, kapacita bufferu a čas do dalšího zápisu |
| Circular archive | co archiv právě dělá: aktivní, čeká na čas z NTP, úklid, nedostatek místa |

Sekce se obnovuje každých 10 s spolu s ostatními hodnotami, bez znovunačtení stránky.

## Diagnostika na Serial Monitoru

Při 115200 baud firmware hlásí každý zápis (`SD: saved 60 rows + 1 events …`), každé selhání i s krokem, který selhal, opětovná připojení, vrácení zápisu, mazání a přetečení bufferu. Lze psát příkazy (odesílat s *Newline*):

| Příkaz | Význam |
|---|---|
| `status` | stav úložiště jako JSON, halda a volný zásobník zapisovací úlohy |
| `flush` | zapsat RAM buffer hned |
| `failwrite <n>` | **test:** dalších *n* zápisů se ohlásí jako neúspěšné, ačkoli řádky už jsou zapsané – testuje opakování a vrácení zápisu |
| `simfree <procenta>` / `simfree off` | **test:** předstírá, že karta má tolik volného místa – testuje archiv. **Pozor:** soubory starší než 365 dní se opravdu smažou |
| `settime <unix_s>` | **test:** nastaví hodiny (např. těsně před půlnoc); zakáže mazání archivu do další synchronizace NTP |

Kompletní testovací postupy jsou v [testování](testovani.md).
