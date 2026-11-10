# Poznámky ke Gree Versati III

HeatPulse funguje s jakýmkoli tepelným čerpadlem vzduch–voda. Vyvíjel a testoval se na Gree Versati III Split, a proto tato kapitola shrnuje, co je pro tento model specifické: ekvitermní křivku, jak regulace řídí kompresor, a údaje výrobce. Čísla platí jen pro tento model. Obecné poznatky o cyklování a teplotě otopné vody jsou v dokumentu [cyklování tepelného čerpadla](cyklovani.md).

| | |
|---|---|
| Testováno na | Gree Versati III Split **GRS-CQ10Pd/NhH-M** (10 kW), 400 V 3N~, 50 Hz, chladivo R32, invertor |
| Otopná soustava | radiátory s akumulační nádrží |
| Regulace | ekvitermní (`Weather depend`) |

## Ekvitermní křivka

Křivka je přímka mezi dvěma nastavenými body: teplotou vody při +20 °C venku (`NT-vody`) a při −20 °C venku (`VT-vody`). Předpokládá se lineární interpolace, což je obvyklé chování; mimo rozsah platí krajní hodnoty:

teplota vody = voda při +20 °C + (20 °C − venkovní teplota) × sklon, kde sklon = (voda při −20 °C − voda při +20 °C) / 40

Příklad pro křivku **25 °C při +20 °C a 40 °C při −20 °C** (sklon 0,375):

| Venku | +20 °C | +15 °C | +10 °C | +9 °C | +5 °C | 0 °C | −5 °C | −10 °C | −15 °C | −20 °C |
|---|---|---|---|---|---|---|---|---|---|---|
| Voda | 25,0 °C | 26,9 °C | 28,8 °C | **29,1 °C** | 30,6 °C | 32,5 °C | 34,4 °C | 36,3 °C | 38,1 °C | 40,0 °C |

Sklon 0,375 K na 1 K znamená, že každý stupeň chladněji venku zvýší teplotu vody o 0,375 °C. Změna bodu pro +20 °C posune vodu nejvíc v mírném počasí a směrem k −20 °C se její vliv zmenšuje na nulu; změna bodu pro −20 °C naopak působí hlavně v mrazu.

- Při +9 °C venku dává ukázková křivka **29 °C**. Provoz na 33 °C by znamenal posunout celou křivku nahoru zhruba o 4 °C – asi o 6–12 % nižší účinnost (viz [teplota otopné vody a účinnost](cyklovani.md#teplota-otopné-vody-a-účinnost)) a obvykle ne méně cyklů.
- Pokud je v místnostech chladno, **posouvejte křivku po 1–2 °C**, ne skokem o 4 °C, a porovnávejte [A/B testem](cyklovani.md#jak-se-rozhodnout-ab-test).
- Pokud je teplo jen v mírném počasí a v mrazu chladno (nebo naopak), měňte **sklon** (bod pro −20 °C), ne celou křivku.

> [!TIP]
> HeatPulse nezná venkovní teplotu, takže jeho **Target water temperature** je pevné číslo. Při ekvitermní regulaci ho občas přepište na aktuální hodnotu z křivky, nebo berte dlaždici **Water − target** jako orientační.

## Jak Versati řídí kompresor

Podle servisních a řídicích manuálů Gree:

- Při topení se frekvence kompresoru řídí rozdílem mezi **skutečnou teplotou výstupní vody a požadovanou hodnotou**: čím větší rozdíl, tím vyšší frekvence. Když je kompresor už na **minimální frekvenci** a voda je pořád moc teplá, jednotka **vypne** – odtud pochází cyklování v mírném počasí.
- Po zastavení se kompresor **nejméně 3 minuty znovu nespustí**.
- Servisní parametr **`ΔT-Heat`** („leaving water temperature difference for heating“) má rozsah **2–10 °C** a výchozí hodnotu **10 °C**. Z dostupných manuálů nelze jednoznačně určit, zda jde o hysterezi pro opětovný start, nebo o cílový rozdíl přívodu a zpátečky – bez instalatéra ho neměňte.
- Otáčky oběhového čerpadla se řídí rozdílem teploty vstupní a výstupní vody.

## Údaje z dokumentace výrobce

Servisní manuál řady Versati III Split tento model zahrnuje (odkazy v [zdrojích](zdroje.md)). Tabulky se liší podle revize a zkušebních podmínek; bez ověření štítku konkrétní jednotky je berte orientačně.

- Nominální výkon modelové řady je kolem **10 kW**. V servisní tabulce jedné revize třífázové 10kW varianty je při **A7/W35** topný výkon **9,5 kW**, příkon **2,07 kW** a COP přibližně **4,6**; při **A7/W45** topný výkon **9,5 kW**, příkon **2,64 kW** a COP přibližně **3,6**. Jde o laboratorní nominální body, **ne o minimální či maximální regulovatelný výkon**.
- Rozsah nastavení výstupní teploty při topení je zhruba **20 až 55 °C** (u provedení normal-temp, podle revize).
- Pro spolehlivé **odmrazování** požaduje servisní manuál objem vody **nejméně 5 l/kW**, u 10kW jednotky tedy **alespoň 50 l**. Například nádrž 100 l tento objem nominálně převyšuje, musí ale být pro tepelné čerpadlo skutečně hydraulicky dostupná (záleží na ventilech, čerpadlech, obtoku a způsobu zapojení).
- Ideální tepelná zásoba **100 l vody na 1 K je 0,116 kWh**, při využitelném rozpětí **5 K tedy 0,58 kWh**. Pokud by přebytek výkonu nad odběrem domu byl například **3 kW**, nabila by se tato zásoba za přibližně **11,6 minuty**. Ve skutečnosti hraje roli proudění, ztráty a stratifikace – jde jen o ilustraci, ne o předpověď délky běhu.

## Kam mířit

- **Délka běhu:** aspoň 10 minut na běh („good“ v HeatPulse), běhy pod 6 minut („poor“) by měly být výjimkou. Viz [hodnocení délky běhů](cyklovani.md#jak-dlouhý-má-být-běh).
- **Přestávky:** v mírném počasí jsou půlhodinové a delší přestávky mezi běhy v pořádku.
- **Teplota vody:** tak nízká, jak ekvitermní křivka dovolí při zachování pohodlí.
- Pokud se při ustáleném topení bez TUV a odmrazování opakovaně objevuje víc než **3–4 starty za hodinu** nebo převážně špatné běhy, zkontrolujte termostatické hlavice, průtoky a hydrauliku dřív, než křivku zvednete.

## Diagnostický postup na několik dní

1. **Prvních 24–48 h neměňte nastavení.** Zapisujte z ovladače stav `Compressor`, `T-water out PE`, `T-water in PE`, venkovní teplotu a okamžiky ohřevu TUV a odmrazování. Ověřte, že [prahy loggeru](pouzivani.md#jak-prahy-nastavit) odpovídají skutečným zapnutím a vypnutím kompresoru.
2. Podívejte se na **hodnocení délky běhů** v HeatPulse. Převážně dobré/ideální běhy s dlouhými přestávkami znamenají, že je soustava v pořádku, i když cykluje.
3. Zjistěte, **jak je zapojená akumulační nádrž** (sériově, nebo jako hydraulický oddělovač / paralelně čtyřtrubkově) a zda se skutečně nabíjí a vybíjí. Termostatické hlavice v hlavních místnostech otevřete naplno.
4. Porovnejte teplotu vody podle křivky se skutečnou teplotou výstupu. Pokud výstup opakovaně přestřelí a kompresor vypne po pár minutách, je čerpadlo na minimu modulace, nebo je příliš malý průtok či aktivní objem vody.
5. Pokud v místnostech není dost teplo, posuňte křivku o 1–2 °C. Pokud je teplo a čerpadlo běží dobře, zkuste křivku o 1 °C snížit. Vždy porovnávejte denní spotřebu elektřiny při podobné venkovní teplotě ([A/B test](cyklovani.md#jak-se-rozhodnout-ab-test)).
6. Servisní parametry `ΔT-Heat`, `Heat run time` ani otáčky oběhových čerpadel neměňte naslepo – závisí na hydraulice a verzi firmwaru.
7. K určení **minimálního a maximálního výkonu kompresoru** potřebujete jeho frekvenci (stavové parametry na ovladači nebo oprávněný export přes Modbus), elektrický příkon a/nebo kalibrované měření průtoku. Dvě příložná čidla **neměří tepelný výkon ani COP**.

## Jak poznat hranici výkonu

- **Minimum modulace / malý odběr:** čerpadlo i na nejnižší frekvenci ohřeje vodu nad požadovanou hodnotu a vypne; termostatické hlavice se zavírají nebo se nádrž nabije příliš rychle. Ze samotné teploty plynové trubky to nelze spolehlivě odlišit od regulační hystereze.
- **Maximum výkonu / nedostatek výkonu:** velmi dlouhý nepřetržitý chod, vysoká frekvence kompresoru, výstupní voda dlouhodobě pod křivkou a postupně klesající teplota v místnostech. Je třeba vyloučit omezený průtok, odmrazování, ochranné limity a ohřev TUV.
