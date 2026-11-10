# Montáž čidel a bezpečnost

> [!CAUTION]
> Logger se k tepelnému čerpadlu **nepřipojuje elektricky**. Nic nezapojujte na svorky tepelného čerpadla ani jeho řídicí jednotky, nezasahujte do rozvaděče 400 V, nedotýkejte se elektrických vodičů a neotevírejte chladivový okruh. Chladivové potrubí může být velmi horké a pod vysokým tlakem.

## Termočlánek K na chladivovém potrubí

Termočlánek měří povrch **bezpečně přístupné plynové (teplé) chladivové trubky**. U splitové jednotky jen tehdy, když je jasné, která trubka je při topení teplá.

- Vyberte rovný úsek trubky. Nesundávejte kryty a s chladivovým vedením nemanipulujte.
- Trubku **nikdy nevrtejte, nepájejte, neohýbejte ani jinak neupravujte**.
- **Termočlánek se závitem M6 se do potrubí nešroubuje!** Použijte vhodné příložné uchycení (objímka, stahovací páska odolná teplotě).
- Po přiložení sondy obnovte izolaci potrubí. Měřená povrchová teplota může za skutečnou teplotou chladiva mírně zaostávat.
- Na trubce je jen hrot sondy – **převodník MAX6675 ani ESP32 na horkou trubku nedávejte**.

### Elektrická izolace termočlánku

**Proč na tom záleží:** MAX6675 spojuje vodič T− termočlánku se zemí elektroniky (datasheet to vyžaduje kvůli detekci odpojeného termočlánku). Mnoho levných kovových sond – včetně těch se závitem M6 – má **uzemněný měřicí spoj**: vodiče termočlánku jsou přivařené ke kovovému tělu. Měděné chladivové potrubí je spojené s ochranným uzemněním tepelného čerpadla. Když se taková sonda dotkne potrubí, propojí se zem elektroniky s ochranným uzemněním podruhé – přes termočlánek. Vznikne zemní smyčka, která může přidat chybu měření nebo šum, a elektronika loggeru (a přes USB třeba i notebook) se spojí se zemí tepelného čerpadla.

**LaskaKit LA131099 (závit M6, 0,5 m, kovový oplet):** obchod neuvádí, zda je měřicí spoj izolovaný, a kovový oplet kabelu bývá spojený s tělem M6. Dodavatel stejné sady s MAX6675 upozorňuje, že konec sondy ani kovový oplet nesmí být uzemněné, jinak budou údaje chybné. Dokud je nezměříte, berte **tělo M6 i oplet** jako možná spojené s měřicím spojem.

**Krok 1 – změřte sondu** (před montáží, sonda odpojená od MAX6675): multimetrem na rozsahu odporu změřte mezi kovovým tělem sondy a každým vodičem termočlánku a mezi kovovým opletem a každým vodičem.

| Naměřeno | Význam | Co dělat |
|---|---|---|
| nekonečno (OL / jednotky MΩ a víc) | neuzemněný (izolovaný) spoj | izolace navíc není potřeba – sondu přiložte přímo s teplovodivou pastou |
| jednotky Ω a méně | uzemněný spoj | odizolujte ji od potrubí (krok 2), nebo radši kupte neuzemněnou sondu |

**Krok 2 – izolace bez ztráty přenosu tepla** (jen u uzemněného spoje):

- **Nejlepší řešení:** vyměnit sondu za **neuzemněný (izolovaný) termočlánek K**, ideálně příložný na potrubí. Je levný a problém úplně odpadne.
- **Kaptonová páska:** část sondy, která se dotýká trubky, oviňte **jednou až dvěma vrstvami kaptonové (polyimidové) pásky**. Jedna vrstva má jen asi 0,05 mm, vydrží několik kV a teploty vysoko přes 200 °C. Mezi pásku a trubku dejte trochu **nevodivé** silikonové teplovodivé pasty. Tepelný odpor tak tenké vrstvy je zanedbatelný – čidlo zareaguje nanejvýš o pár sekund později, což při měření po 10 s a fázích trvajících minuty nevadí.
- **Alternativa:** tenká (0,2–0,5 mm) elektricky izolační **silikonová teplovodivá podložka**, jaká se dává pod výkonové tranzistory.
- Samotnou teplovodivou pastu jako izolaci neberte – pod tlakem se vytlačí a kov se dotkne kovu.
- **Sonda M6 na kulaté trubce:** položte sondu **podél** trubky tak, aby se hrot (kde je měřicí spoj) dotýkal mědi, oviňte ji kaptonem spolu s prvními 10–15 cm opletu a upevněte dvěma tepelně odolnými stahovacími páskami. Holou hliníkovou pásku přes neizolovanou sondu nedávejte – je vodivá a izolaci by přemostila.
- **Pozor na upevnění:** kovová objímka, která se dotýká trubky i holého těla sondy, izolaci přemostí. V místě objímky sondu celou oviňte kaptonem, nebo použijte tepelně odolné stahovací pásky (tepelně stabilizovaný PA66) či samovulkanizační silikonovou pásku.
- **Zaizolujte zvenku:** sondu a asi 10–20 cm trubky obalte izolací potrubí (např. pěnovou s uzavřenými póry). Teplo unikající do místnosti kolem sondy ovlivní přesnost mnohem víc než tenká izolační vrstva mezi sondou a trubkou.

**Krok 3 – kontrola po montáži:** s termočlánkem odpojeným od MAX6675 změřte odpor mezi trubkou (holá měď, např. u šroubení) a vodičem termočlánku. Musí ukazovat nekonečno.

> [!NOTE]
> Nerezová sonda na mědi v suchém, zaizolovaném vnitřním prostředí nezpůsobí znatelnou galvanickou korozi. Spoj přesto udržujte suchý a zaizolovaný.

## DS18B20 na výstupu otopné vody

DS18B20 umístěte na **kovovou trubku výstupu otopné vody z vnitřní (hydraulické) jednotky**, ideálně **před akumulační nádrží**, je-li tam přístup. Za nádrží se projeví míchání a zpoždění.

- **Izolace většinou není potřeba:** DS18B20 je digitální čidlo zalité epoxidem v nerezové trubičce a trubička obvykle není s čidlem vodivě spojená. Ověřte to multimetrem mezi trubičkou sondy a černým vodičem (GND) – musí ukazovat nekonečno. Pak sondu přiložte přímo na měď.
- Očistěte měď, sondu přitiskněte k povrchu s trochou teplovodivé pasty a upevněte páskou nebo objímkou.
- Obalte ji **tepelnou izolací** zhruba 10–20 cm kolem čidla.
- Měřená povrchová teplota je jen přibližná teplota vody. Pro kalibraci ji porovnejte s teplotou výstupní vody na ovladači tepelného čerpadla (u Gree `T-water out PE`) při ustáleném průtoku. Hodnoty nemusí být stejné – čidla jsou na různých místech.
- Sonda LA131090 má rozsah −55 až +125 °C, pro otopnou vodu bohatě stačí.
- Firmware čte **první nalezené čidlo**. Druhé DS18B20 na stejnou sběrnici nepřidávejte, dokud kód neumí čidla rozlišit podle ROM adres (viz [plán rozvoje](plan-rozvoje.md)).

## Funkční schéma

```
Venkovní jednotka --(chladivo, plynová Cu trubka)--> Vnitřní jednotka
                              |                           |
                       [termočlánek K]                    | výstup otopné vody
                                                          +--[DS18B20]--> [AKU nádrž] --> radiátory
                                                             (před AKU)

                       MAX6675 -----+
                       DS18B20 -----+--> ESP32 --- Wi-Fi --> web / CSV
                       microSD -----+
```

Schéma je **funkční, ne hydraulické**: akumulační nádrž může být zapojená sériově, paralelně, čtyřtrubkově nebo jako hydraulický oddělovač a potrubí se podle toho liší.

## Zvláštnosti čidel

- **MAX6675:** rozsah 0 až +1024 °C, rozlišení 0,25 °C. **Pod 0 °C neměří** – při odmrazování, kdy se trubka ochladí, proto může hlásit chybu nebo 0 °C. Neznamená to vadnou sondu. Krátké výpadky čtení (do 5 minut) firmware toleruje a odhad fáze kvůli nim nenuluje.
- **SHT40:** přesnost ±0,2 °C a ±1,8 % RV, měří až do 100 % RV. Ve vzduchu blízko rosného bodu ho firmware občas krátce ohřeje (viz [používání](pouzivani.md#teplota-a-vlhkost-místnosti)).
- **AM2301:** přesnost asi ±0,5 °C a ±3 % RV (±5 % na krajích rozsahu). Když bylo čidlo dlouho ve velmi vlhkém vzduchu nebo orosené, může chvíli ukazovat vyšší vlhkost. Chraňte ho před kapající vodou a výpary chemikálií (rozpouštědla, barvy).
- **DS18B20:** třívodičové napájení 3,3 V a pull-up 4,7 kΩ (viz [zapojení](zapojeni.md#ds18b20--laskakit-la131090-vodotěsná-sonda-kabel-3-m)). Hodnotu přesně 85,00 °C (výchozí hodnota po zapnutí čidla) firmware považuje za chybné čtení.

## Čidlo v místnosti

Čidlo místnosti (SHT40 nebo AM2301) měří vzduch v místnosti, proto má viset **volně ve vzduchu** a nedotýkat se ničeho teplého ani studeného:

- asi 1–1,5 m nad podlahou, větracími otvory (nebo čipem SHT40) dolů nebo do strany (prach, kapající voda),
- holý modul SHT40 patří do malé **větrané** krabičky (otvory nebo mřížka, ne utěsněná), která ho chrání před kapající vodou a prachem; utěsněná krabička by držela vlastní vzduch a reagovala pomalu,
- **mimo horké části a potrubí tepelného čerpadla**, vnitřní jednotku, akumulační nádrž a jiné zdroje tepla a mimo přímé slunce,
- **ne v krabičce s elektronikou** a ne těsně vedle ESP32 nebo USB adaptéru – zahřívají se a místnost by vypadala teplejší a sušší,
- několik centimetrů od zdi. Kvůli porovnání před a po zateplení **nechte čidlo celou dobu na stejném místě**.

Pokud chcete vědět, zda stěnám nebo trubkám hrozí kondenzace, porovnejte **rosný bod** na webu s teplotou povrchu (např. změřenou IR teploměrem): povrch chladnější než rosný bod vlhne.

## Kam s elektronikou

ESP32, modul microSD, MAX6675, kabely i USB zdroj patří **mimo horké potrubí a vlhké prostředí**, ideálně do plastové krabičky. Rozsah MAX6675 do 1024 °C neznamená, že by elektronika vydržela teplo potrubí.
