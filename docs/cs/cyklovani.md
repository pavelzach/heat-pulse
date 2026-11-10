# Cyklování tepelného čerpadla a teplota otopné vody

Co říkají publikované studie a praktické zkušenosti o startech kompresoru, délce běhů a teplotě otopné vody – a jak pomocí HeatPulse najít nejlepší nastavení pro váš dům. Zdroje jsou uvedené na konci a v [technických zdrojích](zdroje.md#cyklování-a-účinnost).

> [!NOTE]
> Jde o shrnutí obecných studií a dokumentace výrobců, ne o radu pro vaši konkrétní instalaci. Nastavení mimo uživatelské menu by měl měnit instalatér.

## Co se děje při startu kompresoru

- **Teplo vzniká téměř okamžitě**, ale ne s plnou účinností. V laboratorní studii dosáhl tepelný výkon maxima 42–60 s po startu, zatímco elektrický příkon byl naplno už do 20 s. Jiné tepelné čerpadlo mělo 90 % ustáleného výkonu 3 minuty po startu.
- **První minuty jsou méně účinné**, protože se nejdřív musí ohřát chladivo, výměníky a voda v potrubí. Část tohoto tepla se po zastavení kompresoru ztratí. Každý start tak stojí pevné množství energie, které krátký běh nevynahradí.
- Rozšířené tvrzení, že „prvních 7 minut elektřina nevyrábí teplo“, je přehnané, ale vystihuje podstatu: **běh trvající pár minut je hlavně režie startu**.
- Kompresor potřebuje i minimální dobu chodu kvůli mazání: výrobce kompresorů Bitzer doporučuje **nejméně 5 minut** na jeden běh a varuje před opakovanými kratšími cykly.

## Jak dlouhý má být běh

Test financovaný britskou vládou (EA Technology) zjistil, že **běhy kratší než 6 minut výrazně snižují COP**, u tepelných čerpadel vzduch–voda mnohem víc než u země–voda. Při běhu **kolem 8 minut** se vzduchové čerpadlo přiblížilo katalogovému COP a delší běhy přinesly další zlepšení. Zpráva doporučuje navrhovat soustavy na minimální dobu běhu zhruba 6 minut za všech podmínek. (Testované jednotky měly jednorychlostní kompresory; invertorové umí modulovat, ale režie startu zůstává.)

HeatPulse hodnotí každý odhadnutý běh (teplou fázi) podle těchto hranic:

| Délka běhu | Hodnocení | Význam |
|---|---|---|
| pod 6 min | **poor** (špatný) | převažují ztráty startu, opotřebení kompresoru – prověřit |
| 6–10 min | **acceptable** (přijatelný) | blízko minimální doporučené délky běhu |
| 10–20 min | **good** (dobrý) | kompresor má čas se ustálit a modulovat |
| 20 min a déle | **ideal** (ideální) | ztráty startu jsou zanedbatelné |

**Počet startů za hodinu je méně důležitý než délka běhu.** Publikované „maximum startů za hodinu“ pro tepelná čerpadla vzduch–voda ve zdrojích není. Orientačně: 1–2 starty za hodinu s přestávkami půl hodiny a víc jsou v pořádku, pokud každý běh trvá aspoň 10 minut. Zkušení uživatelé označují za „dobré“ cyklování desítky minut mezi běhy a za „špatné“ běhy po pár minutách. Pokud čerpadlo i po všech opatřeních níže cykluje častěji než zhruba jednou za hodinu, může být na dům předimenzované.

## Teplota otopné vody a účinnost

Každý stupeň nižší teploty otopné vody zlepší účinnost tepelného čerpadla zhruba o **1,5–3 %** (běžně se uvádí asi 2,5 %), protože kompresor překonává menší teplotní rozdíl. Zvýšení teploty vody o 4 °C tak stojí zhruba **6–12 %** účinnosti po celou dobu chodu.

Nižší teplota pomáhá jen do chvíle, kdy radiátory ještě udrží teplo v místnostech.

## Sníží vyšší teplota vody cyklování?

Většinou **ne**, a stojí účinnost:

- Dům potřebuje stejné množství tepla bez ohledu na teplotu vody. Pokud je tato potřeba pod minimálním výkonem čerpadla (běžné v mírném počasí), čerpadlo musí cyklovat tak jako tak.
- Teplejší radiátory přetopí místnosti, termostatické hlavice se přivřou a odběr ještě klesne.
- Délku běhu určuje hlavně **objem vody** v soustavě (radiátory, potrubí, akumulační nádrž) a **hystereze** regulace – ne nastavená teplota vody.

Vyšší teplota může pomoct v jediné situaci: když radiátory při současné teplotě nestačí odebrat ani minimální výkon čerpadla, voda během pár minut přestřelí a kompresor vypne po velmi krátkých bězích. To se v HeatPulse projeví mnoha **poor** běhy, ne běhy přes 10 minut s dlouhými přestávkami.

## Co proti krátkému cyklování opravdu pomáhá

1. **Naplno otevřené termostatické hlavice** v hlavních obytných místnostech (tzv. referenční zóna). Přivřené hlavice jsou nejčastější příčinou krátkého cyklování; v testech EA Technology výrazně pomohlo už to, že byly otevřené dva radiátory místo jednoho.
2. **Akumulační nádrž, která je opravdu v okruhu tepelného čerpadla** a přidává objem vody (sériové nebo čtyřtrubkové zapojení podle vaší instalace).
3. **Ekvitermní regulace** s křivkou nastavenou tak, aby voda byla jen tak teplá, jak je potřeba – místo pevné a vyšší teploty.
4. **Hystereze / rozdíl pro opětovný start** v regulaci – širší pásmo dává delší běhy. Měňte jen tehdy, když přesně víte, co parametr u vašeho modelu dělá.
5. **Větší otopná tělesa nebo víc otevřených radiátorů** umožní čerpadlu běžet déle při nízké teplotě vody.

## Jak se rozhodnout: A/B test

Ani COP, ani samotný počet startů neřekne, které nastavení je lepší. Rozhoduje **spotřeba elektřiny za den při stejném pohodlí**:

1. Nastavení A (např. současná ekvitermní křivka) nechte 2–3 dny, nastavení B (např. křivka posunutá o +2 °C) dalších 2–3 dny.
2. Porovnávejte dny s **podobnou venkovní teplotou** a stejným pohodlím (teplota v místnostech, spotřeba teplé vody).
3. Odečítejte **elektroměr** tepelného čerpadla (nebo celého domu každý den ve stejnou dobu) a zapište denní spotřebu.
4. V HeatPulse porovnejte **hodnocení délky běhů** a počet startů.
5. Ponechte nastavení s nižší spotřebou při stejném pohodlí.

## Omezení odhadu HeatPulse

HeatPulse měří teplou fázi chladivové trubky, ne samotný kompresor. Běh začíná, když trubka vydrží nad prahem teplé fáze, a končí, když vydrží pod prahem chladné fáze. Odhadnutá délka se proto může od skutečné lišit o minutu či dvě podle [nastavení prahů](pouzivani.md#jak-prahy-nastavit). Odmrazování se může projevit jako krátké běhy navíc.

## Zdroje

- EA Technology pro britskou vládu (DECC): [The Effects of Cycling on Heat Pump Performance](https://assets.publishing.service.gov.uk/government/uploads/system/uploads/attachment_data/file/65695/7389-effects-cycling-heat-pump-performance.pdf)
- Brudermueller a kol. 2023, cyklování 503 tepelných čerpadel v provozu (včetně přehledu ztrát cyklováním a doby rozběhu): [Earth Notes](https://www.earth.org.uk/bibliography/brudermueller2023cycling.html)
- Rozběh a doběh tepelných čerpadel: [IIR](https://iifiir.org/en/fridoc/theoretical-and-experimental-investigation-of-startup-and-shutdown-135447), [REHVA Journal](https://rehva.eu/rehva-journal/chapter/improved-characterization-of-water-to-water-heat-pumps-part-load-performance)
- Bitzer: [četnost cyklů a minimální doba chodu](https://bitzer.de/shared_media/html/st-420/en-GB/327407627327409419.html)
- Teplota otopné vody a účinnost: [CIBSE Journal](https://www.cibsejournal.com/cibsejournal-magazine/202308/50/page.html), [Linemetrics](https://edm.linemetrics.com/en/massnahmen/geb-heiz-010), [iDM](https://www.idm-energie.at/en/heat-pump-efficiency/flow-temperature/)
- Praktické zkušenosti: [Renewable Heating Hub – provoz v mírném počasí](https://renewableheatinghub.co.uk/forums/renewable-heating-air-source-heap-pumps-ashps/performance-of-heat-pumps-in-mild-weather), [OpenEnergyMonitor – cyklování podle odběru vs. zatížení](https://community.openenergymonitor.org/t/heat-pump-demand-cycling-vs-compressor-load-cycling/29277), [Homey – časté chyby u tepelných čerpadel](https://homey.app/en-ae/wiki/common-heat-pump-mistakes-and-how-to-prevent-them/)
