# Roadmap

Ideas for future versions, in the recommended order. Contributions are welcome.

## V3 – return water (low cost)

Add a second DS18B20 on the return water before the indoor unit to get the flow/return temperature difference. The code needs to change from `getTempCByIndex(0)` to addressing individual sensors by their ROM addresses. **Without a flow measurement, the temperature difference alone does not give you the heat output.**

## V3+ – hydraulics

A third and fourth DS18B20 on the buffer tank inlet and outlet to reveal hydraulic short-circuiting, mixing, stratification and thermal lag. Interpreting the data correctly requires knowing how the tank is piped (2, 3 or 4 connections) and how the circulation pumps operate.

## V4 – more accurate start counting

Verified reading of the compressor status and frequency, only from an authorised, preferably **read-only** export (e.g. Modbus from a higher-level controller), or a separate measurement of the electrical power input. Do not connect a second Modbus master to an existing RS485 bus.

## V4+ – analytics

- tagging domestic hot water heating and defrosting,
- logging setting changes (e.g. 28 °C → 30 °C),
- outdoor temperature,
- a Python CSV analyser, backup of daily files,
- possibly OTA updates (on a secured LAN only),
- localised web interface (language switch).
