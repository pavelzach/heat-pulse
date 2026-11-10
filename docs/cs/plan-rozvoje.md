# Plán rozvoje

Návrhy dalších verzí v doporučeném pořadí. Příspěvky jsou vítané.

## V3 – zpátečka (nízké náklady)

Přidat druhé DS18B20 na vratnou vodu před vnitřní jednotkou a získat rozdíl teplot přívod/zpátečka. Kód je potřeba změnit z `getTempCByIndex(0)` na adresování jednotlivých čidel podle ROM adres. **Bez měření průtoku z rozdílu teplot výkon nespočítáte.**

## V3+ – hydraulika

Třetí a čtvrté DS18B20 na vstup a výstup akumulační nádrže, aby šlo odhalit hydraulický zkrat, míchání, stratifikaci a teplotní zpoždění. Pro správnou interpretaci je nutné znát zapojení nádrže (2, 3 nebo 4 hrdla) a provoz oběhových čerpadel.

## V4 – přesnější počítání startů

Ověřené čtení stavu a frekvence kompresoru pouze z oprávněného, nejlépe **jen pro čtení** exportu (např. Modbus z nadřazené regulace), případně oddělené měření elektrického příkonu. Na stávající sběrnici RS485 nepřipojujte druhý Modbus master.

## V4+ – analytika

- označování ohřevu TUV a odmrazování,
- záznam změn nastavení (např. 28 °C → 30 °C),
- venkovní teplota,
- analyzátor CSV v Pythonu, zálohování denních souborů,
- případně OTA aktualizace (jen v zabezpečené LAN),
- přepínání jazyka webového rozhraní (čeština).
