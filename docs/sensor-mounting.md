# Sensor mounting and safety

> [!CAUTION]
> The logger is **not electrically connected** to the heat pump. Do not connect anything to the terminals of the heat pump or its controller, do not work inside the 400 V switchboard, do not touch electrical conductors and do not open the refrigerant circuit. Refrigerant piping can be very hot and under high pressure.

## Type K thermocouple on the refrigerant pipe

The thermocouple measures the surface of a **safely accessible gas (hot) refrigerant pipe**. On a split unit, only do this if it is clear which pipe gets hot during heating.

- Choose a straight section of pipe. Do not remove covers or handle the refrigerant line.
- **Never drill, solder, bend or otherwise modify** the pipe.
- **An M6 threaded thermocouple must not be screwed into the pipe!** Use a suitable clamp-on mount (pipe clip, heat-resistant cable tie).
- Restore the pipe insulation after attaching the probe. The measured surface temperature may lag slightly behind the actual refrigerant temperature.
- Only the probe tip goes on the pipe – **never put the MAX6675 converter or the ESP32 on the hot pipe**.

### Electrical isolation of the thermocouple

**Why it matters:** the MAX6675 connects the thermocouple's T− lead to the electronics ground (the datasheet requires this for open-thermocouple detection). Many cheap metal probes – including M6 threaded ones – have a **grounded junction**: the thermocouple wires are welded to the metal body. The copper refrigerant pipe is connected to the protective earth of the heat pump. If such a probe touches the pipe, the electronics ground is connected to protective earth a second time through the thermocouple. The result is a ground loop that can add measurement error or noise, and the logger electronics (and over USB possibly a laptop) become tied to the heat pump's earth.

**LaskaKit LA131099 (M6 thread, 0.5 m, metal braid):** the shop does not say whether the junction is insulated, and the metal braid over the cable is usually connected to the M6 body. A supplier of the same MAX6675 kit warns that neither the probe end nor the braided shield may be grounded, otherwise the readings are wrong. Treat **both the M6 body and the braid** as possibly connected to the junction until you have measured them.

**Step 1 – measure the probe** (before mounting, probe disconnected from the MAX6675): set a multimeter to resistance and measure between the metal body of the probe and each thermocouple wire, and between the metal braid and each wire.

| Reading | Meaning | What to do |
|---|---|---|
| open circuit (OL / several MΩ) | ungrounded (insulated) junction | no extra isolation needed – mount it directly with thermal paste |
| a few Ω or less | grounded junction | isolate it from the pipe (step 2), or better buy an ungrounded probe |

**Step 2 – isolate without losing heat transfer** (only for a grounded junction):

- **Best option:** replace it with an **ungrounded / insulated-junction** type K probe, ideally a pipe-surface (clamp-on) type. It is cheap and avoids the problem completely.
- **Kapton tape:** wrap the part of the probe that touches the pipe with **one or two layers of Kapton (polyimide) tape**. One layer is only about 0.05 mm thick and withstands several kV and well over 200 °C. Put a little **non-conductive** silicone thermal paste between the tape and the pipe. The thermal resistance of such a thin layer is tiny – the reading may react a few seconds later, which does not matter with a 10 s sampling interval and phases lasting minutes.
- **Alternative:** a thin (0.2–0.5 mm) electrically insulating **silicone thermal pad**, as used under power transistors.
- Do not rely on thermal paste alone as insulation – under pressure it gets squeezed out and metal touches metal.
- **M6 probe on a round pipe:** lay the probe **along** the pipe so the tip (where the junction is) touches the copper, wrap it together with the first 10–15 cm of the braid in Kapton, and fasten it with two heat-resistant cable ties. Do not use bare aluminium tape over a non-isolated probe – it conducts and would bridge the isolation.
- **Watch the fastening:** a metal hose clamp that touches both the pipe and the bare probe body bridges the isolation. Wrap the probe completely in Kapton where the clamp sits, or use heat-resistant cable ties (PA66 heat-stabilised, or silicone self-fusing tape).
- **Insulate the outside:** cover the probe and about 10–20 cm of pipe with pipe insulation (e.g. closed-cell foam). Heat lost to the room around the probe affects accuracy far more than a thin insulating layer between the probe and the pipe.

**Step 3 – check after mounting:** with the thermocouple disconnected from the MAX6675, measure between the pipe (bare copper, e.g. at a fitting) and a thermocouple wire. It must read open circuit.

> [!NOTE]
> Stainless steel probes on copper in a dry, insulated indoor location do not cause noticeable galvanic corrosion. Keep the joint dry and insulated anyway.

## DS18B20 on the heating water outlet

Place the DS18B20 on the **metal heating water outlet pipe of the indoor (hydraulic) unit**, ideally **before the buffer tank** if accessible. After the tank, mixing and delay distort the reading.

- **Isolation is usually not needed:** the DS18B20 is a digital sensor potted in epoxy inside the stainless tube, and the tube is normally not connected to the sensor. Check it with a multimeter between the probe tube and the black (GND) wire – it should read open circuit. If it does, mount the probe directly on the copper.
- Clean the copper, press the probe against the surface with a little thermal paste and secure it with tape or a clamp.
- Wrap it with **thermal insulation** about 10–20 cm around the sensor.
- The measured surface temperature is only an approximation of the water temperature. To calibrate, compare it with the outlet water temperature on the heat pump controller (on Gree: `T-water out PE`) at a steady flow. The values may differ – the sensors are in different places.
- The LA131090 probe has a range of −55 to +125 °C, which is plenty for heating water.
- The firmware reads the **first sensor found**. Do not add a second DS18B20 to the same bus until the code can distinguish sensors by their ROM addresses (see the [roadmap](roadmap.md)).

## Functional diagram

```
Outdoor unit --(refrigerant, Cu gas pipe)--> Indoor unit
                         |                       |
                 [K thermocouple]                | heating water outlet
                                                 +--[DS18B20]--> [buffer tank] --> radiators
                                                    (before tank)

                 MAX6675 -----+
                 DS18B20 -----+--> ESP32 --- Wi-Fi --> web / CSV
                 microSD -----+
```

The diagram is **functional, not hydraulic**: the buffer tank may be connected in series, in parallel, as a four-pipe tank or as a hydraulic separator, and the piping differs accordingly.

## Sensor quirks

- **MAX6675:** range 0 to +1024 °C, resolution 0.25 °C. **It cannot measure below 0 °C** – during defrosting, when the pipe cools down, it may report an error or 0 °C. That does not mean the probe is faulty. The firmware tolerates short dropouts (up to 5 minutes) without resetting the phase estimate.
- **SHT40:** accuracy ±0.2 °C and ±1.8 % RH, works up to 100 % RH. In nearly condensing air the firmware heats it briefly now and then (see [usage](usage.md#room-temperature-and-humidity)).
- **AM2301:** accuracy about ±0.5 °C and ±3 % RH (±5 % at the extremes). After the sensor has been exposed to very humid air or condensation for a long time, it may read high for a while. Keep it away from dripping water and chemical vapours (solvents, paints).
- **DS18B20:** three-wire 3.3 V supply and a 4.7 kΩ pull-up (see [wiring](wiring.md#ds18b20--laskakit-la131090-waterproof-probe-3-m-cable)). A reading of exactly 85.00 °C (the sensor's power-on default) is treated as an invalid reading by the firmware.

## Room sensor in the room

The room sensor (SHT40 or AM2301) measures the air of the room, so it should hang **in free air**, not touch anything warm or cold:

- about 1–1.5 m above the floor, with the vent openings (or the SHT40 chip) facing down or sideways (dust, dripping water),
- a bare SHT40 module belongs in a small **ventilated** box (holes or a grille, not sealed) that keeps dripping water and dust off it; a sealed box would hold its own air and react slowly,
- **away from the heat pump's hot parts and pipes**, the indoor unit, the buffer tank and other heat sources, and out of direct sunlight,
- **not inside the electronics enclosure** and not right next to the ESP32 or the USB adapter – they warm up and would make the room look warmer and drier,
- a few centimetres away from the wall. For comparing before and after insulation, **keep the sensor in the same place** for the whole time.

If you want to know whether walls or pipes are at risk of condensation, compare the **dew point** on the web page with the surface temperature (e.g. measured with an IR thermometer): a surface colder than the dew point gets wet.

## Where to put the electronics

The ESP32, microSD module, MAX6675, cables and USB power supply belong **away from hot pipes and damp areas**, ideally in a plastic enclosure. The MAX6675's 1024 °C range does not mean the electronics can withstand the heat of the pipe.
