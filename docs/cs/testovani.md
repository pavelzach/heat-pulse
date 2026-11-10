# Testování ukládání dat

Testovací scénáře pro RAM buffer, zápis na microSD a kruhový archiv (firmware 2.2), plus čidlo místnosti (firmware 2.3, [scénář 7](#7-čidlo-místnosti-sht40-nebo-am2301-firmware-23)). Stačí samotný logger, PC se čtečkou karet a Serial Monitor v Arduino IDE (115200 baud, konec řádku *Newline*). Příkazy pro Serial Monitor jsou popsané v [ukládání dat](uloziste.md#diagnostika-na-serial-monitoru).

> [!TIP]
> Testy lze zkrátit dočasným nastavením `SD_FLUSH_MS = 60000` (1 min) na začátku `HeatPulse.ino`. Nezapomeňte ho pak vrátit na `600000`.

> [!CAUTION]
> Archiv testujte na **náhradní kartě**. Archiv i příkaz `simfree` opravdu mažou soubory starší než 365 dní.

## 1. Běžný provoz

1. Vložte prázdnou kartu FAT32, nahrajte firmware, otevřete Serial Monitor.
2. Očekávání po startu: `microSD: OK`, `Storage: write every 10 min, RAM buffer 1080 rows …`.
3. Každých 10 s: `Gas: … | SD: OK | buffer: N | …` – `N` s každým měřením vzroste o jedna.
4. Otevřete web: hodnoty i graf se aktualizují okamžitě; sekce **Storage** ukazuje `OK`, kapacitu, rostoucí *Unsaved records in RAM* a *next write in about … min*.
5. Po 10 minutách: `SD: saved 60 rows + … events …`; *buffer* klesne na 0–1, *Last successful write* ukazuje čas.
6. Stáhněte dnešní CSV a zkontrolujte: hlavička shodná s 2.1, řádky po 10 s **bez mezer a bez duplicit** (`uptime_ms` roste zhruba o 10 000), časové značky odpovídají času měření (ne času zápisu).
7. Stiskněte **Write buffer to microSD now** – Serial Monitor potvrdí zápis a stažený soubor obsahuje i nejnovější řádky.

**Splněno:** žádné mezery, žádné duplicity, formát beze změny, měření během zápisu pokračuje.

## 2. Volné místo na hranici 10 %

Příprava (v PC, na náhradní kartě, na které už logger vytvořil `/LOGS`):

1. Nakopírujte do `/LOGS` několik zkušebních denních souborů starších než 365 dní, např. `20200101.CSV` … `20200110.CSV` (obsah libovolný, např. kopie skutečného CSV), a jeden mladší, např. kopii pojmenovanou včerejším datem.
2. Zaplňte kartu jedním velkým souborem **v kořeni** (ne v `/LOGS`) tak, aby zůstalo asi 9 % volna. V PowerShellu spuštěném jako správce pro kartu s písmenem E: `fsutil file createnew E:\filler.bin <bajty>` (velikost spočítejte z *Vlastností* jednotky; FAT32 dovoluje nejvýše 4 GB na soubor, případně použijte víc souborů).

Test:

1. Vložte kartu, nechte logger připojit k Wi-Fi s internetem (**pro mazání je nutné NTP**). Serial: `Time synchronised by NTP – archive deletion enabled`.
2. Napište `flush` (nebo počkejte na zápis). Očekávání:
   - `SD: free space 9.x % < 10 % – archive cleanup (… deletable file(s) found)`,
   - pro každý soubor řádek `ARCHIVE: deleted /sd/LOGS/2020…CSV (… B, older than 365 days), freed … B, free now … %`, od nejstaršího,
   - mazání skončí, jakmile je volných 15 %, nebo když už nic staršího než 365 dní nezbývá.
3. Zkušební soubory jsou malé, takže volné místo 10 % nedosáhne: očekává se `SD ERROR: insufficient space … writing PAUSED`, web: **ERROR**, *insufficient space*, archiv *insufficient space*.
4. Kontrola v PC: smazané jsou jen soubory `2020….CSV`; `filler.bin`, `NOSYNC.CSV`, `EVENTS.CSV`, mladší denní soubor i dnešní soubor zůstaly.
5. Smažte v PC `filler.bin` (nebo jeho část) a vraťte kartu. Do zhruba minuty: `SD: enough free space again – writing resumed` a řádky čekající v RAM se zapíšou.

Varianta bez zaplňování karty: `simfree 8` → archiv smaže všechny soubory starší než 365 dní (simulovaná hodnota se nemění) a pak pozastaví zápis; `simfree 12` → stav **WARNING**, zápis pokračuje; `simfree off` → skutečná hodnota.

Varianta bez NTP: spusťte logger v režimu vlastní Wi-Fi (bez domácí sítě) a zadejte `simfree 8`. Očekávání: `clock not confirmed by NTP – file age unknown, nothing is deleted`, archiv *waiting for NTP time* / *insufficient space*, **žádný soubor se nesmaže**.

**Splněno:** mažou se jen soubory v `/LOGS` s názvem `YYYYMMDD.CSV` starší než 365 dní, od nejstaršího; nic mladšího se nikdy nesmaže; zápis se pozastaví a sám obnoví.

## 3. Neúspěšný zápis

### 3a. Simulované selhání (vrácení zápisu, žádné duplicity)

1. Nechte logger běžet několik minut, pak napište `failwrite 1` a `flush`.
2. Očekávání: `SD ERROR: write (SIMULATED by test command) failed …`, `SD: write failed – N records kept in RAM, retry in 60 s`; web: **ERROR – last write failed**. Řádky se ve skutečnosti do souboru zapsaly.
3. Asi po 60 s: `SD remount: OK`, `SD: removed an incomplete earlier write from … (back to … bytes) – no duplicate rows`, pak `SD: saved …`.
4. Stáhněte CSV: každá hodnota `uptime_ms` je v souboru **právě jednou**, bez mezery.

### 3b. Vyjmutí karty za provozu

1. Vytáhněte kartu. Do 60 s: `SD ERROR: card does not respond (removed?)`; web: **ERROR – card missing**; v `/api/status` je `sd: false`.
2. Měření, graf i statistiky běží dál; *Unsaved records in RAM* roste.
3. Počkejte aspoň 15 minut (přes jeden interval zápisu) a kartu vraťte. Do 60 s: `SD remount: OK` a řádky čekající v RAM se hned zapíšou.
4. Stáhněte CSV: řádky z doby výpadku jsou v souboru s původními časy, bez mezer a duplicit.

### 3c. Přetečení bufferu (volitelné, trvá přes 3 hodiny)

Nechte kartu venku déle než 3 hodiny. Očekávání: `ERROR: RAM buffer full – oldest unsaved rows lost (1 in total)…` (pak každý 60.), web: **ERROR – RAM buffer was full**, *Records lost since start* > 0. Po vrácení karty se zapíšou nejnovější 3 hodiny; po úspěšném zápisu se stav vrátí na **OK**, počet ztracených záznamů zůstane vidět do restartu.

## 4. Přechod přes půlnoc

1. Na loggeru **bez internetu** (režim vlastní Wi-Fi) nebo před nastavením času z NTP napište `settime <unixový čas dnešního dne 23:56 místního času>`; hodnotu dá online převodník nebo PowerShell `[DateTimeOffset]::new((Get-Date).Date.AddHours(23).AddMinutes(56)).ToUnixTimeSeconds()`. (Na loggeru s internetem může NTP do hodiny čas opravit – pro tento test to nevadí.)
   V režimu vlastní Wi-Fi během testu neotevírejte ani neobnovujte webovou stránku – stránka posílá loggeru čas prohlížeče a testovací čas by přepsala.
2. Počkejte do 00:06 (nebo po půlnoci napište `flush`).
3. Očekávání na kartě: řádky do 23:59:5x v souboru **starého dne**, řádky od 00:00:0x v novém souboru `YYYYMMDD.CSV` s hlavičkou; nic nechybí, nic není dvakrát; web ukazuje nový den jako *newest day*.
4. Případně ověřte hranici na skutečné noci: ráno stáhněte včerejší a dnešní soubor a zkontrolujte poslední a první řádky.

## 5. Obnovení po výpadku napájení a restartu

1. Poznamenejte si čas, počkejte asi 5 minut po posledním zápisu a odpojte napájení.
2. Znovu připojte. Očekávání po startu: `microSD: OK`, měření se rozběhne, nový zápis po 10 minutách.
3. Stáhněte CSV: řádky do posledního zápisu před výpadkem jsou kompletní; mezera pokrývá nejvýše přibližně 10 minut před výpadkem plus dobu startu. Poslední řádek před mezerou je celý (ne useknutý).
4. Varianta: před odpojením stiskněte **Write buffer to microSD now** – pak se neztratí nic.

## 6. Dlouhodobý provoz

Nechte logger běžet několik dní a občas zkontrolujte příkazem `status`:

- `Heap: free …, minimum …, largest block …` – hodnoty nesmějí trvale klesat,
- `writer task stack left …` – má zůstat nad zhruba 2 000 B,
- `write_errors` a `dropped` v JSON úložiště mají při funkční kartě zůstat 0.

## 7. Čidlo místnosti (SHT40 nebo AM2301, firmware 2.3)

1. **Čtení:** s čidlem SHT obsahuje výpis po startu `SHT4x found at I2C address 0x44` (nebo `SHT3x`, nebo 0x45). Po startu a pak každých 10 s vypíše Serial Monitor `cellar: …°C, … % RH (failed readings since boot: n)`. Dýchněte na čidlo – vlhkost během několika čtení stoupne a během minut se vrátí.
2. **Odpojte datový vodič** asi na minutu: první dvě čtení drží předchozí hodnotu, od třetího web ukáže *Sensor not responding (NO_RESPONSE)*, sloupce `cellar_C`/`cellar_RH` v CSV jsou prázdné a `cellar_valid` je `0`. Po připojení se hodnoty vrátí s dalším čtením.
3. **Rozpracovaný den přežije restart:** poznamenejte si *today so far* v denním vývoji (nebo `today` v `/api/cellar` včetně `samples`), restartujte ESP32 a po nastavení hodin ověřte, že `samples` pokračuje od naposledy uložené hodnoty (nejvýše o 60 čtení = 10 minut méně), ne od nuly.
4. **Uzavření dne:** stejně jako u [přechodu přes půlnoc](#4-přechod-přes-půlnoc) nastavte příkazem `settime` čas 23:56. Po půlnoci Serial Monitor vypíše `Cellar: day YYYYMMDD closed – …`; po dalším zápisu (nebo `flush`) má `/CELLAR.CSV` nový řádek s tímto datem a po restartu je den stále v denním vývoji (načtený z karty: `Cellar: N daily summaries loaded from CELLAR.CSV`). Když se hodiny později vrátí na skutečný čas, může se testovací datum v `CELLAR.CSV` objevit podruhé (web ukáže pozdější řádek) – pokud chcete čistý soubor, testovací řádky na PC smažte.
5. **Logger vypnutý přes půlnoc:** odpojte ho před půlnocí a zapojte druhý den. Po nastavení hodin se předchozí den uzavře (s menším `samples`) a zapíše jednou – datum se v `CELLAR.CSV` nesmí objevit dvakrát.
