# Notes for the Gree Versati III

HeatPulse works with any air-to-water heat pump. It was developed and tested on a Gree Versati III Split, so this chapter collects what is specific to that model: the weather compensation curve, how the controller drives the compressor, and the manufacturer data. The figures apply to this model only. General findings about cycling and flow temperature are in [heat pump cycling](heat-pump-cycling.md).

| | |
|---|---|
| Tested on | Gree Versati III Split **GRS-CQ10Pd/NhH-M** (10 kW), 400 V 3N~, 50 Hz, R32 refrigerant, inverter |
| Heating system | radiators with a buffer tank |
| Control | weather compensation (`Weather depend`) |

## Weather compensation curve

The curve is a straight line between the two set points: the flow temperature at +20 °C outdoors (`NT-vody`, low water temperature) and at −20 °C outdoors (`VT-vody`, high water temperature). Linear interpolation is assumed, which is the usual behaviour; outside the range the end values apply:

flow temperature = flow at +20 °C + (20 °C − outdoor temperature) × slope, where slope = (flow at −20 °C − flow at +20 °C) / 40

Example for a curve of **25 °C at +20 °C and 40 °C at −20 °C** (slope 0.375):

| Outdoor | +20 °C | +15 °C | +10 °C | +9 °C | +5 °C | 0 °C | −5 °C | −10 °C | −15 °C | −20 °C |
|---|---|---|---|---|---|---|---|---|---|---|
| Flow | 25.0 °C | 26.9 °C | 28.8 °C | **29.1 °C** | 30.6 °C | 32.5 °C | 34.4 °C | 36.3 °C | 38.1 °C | 40.0 °C |

A slope of 0.375 K per K means every degree colder outside raises the flow temperature by 0.375 °C. Changing the +20 °C point moves the flow most in mild weather, and its effect shrinks to zero towards −20 °C; changing the −20 °C point acts mainly in frosty weather.

- At +9 °C outdoors the example curve gives **29 °C**. Running at 33 °C instead would mean shifting the whole curve up by about 4 °C – roughly 6–12 % lower efficiency (see [flow temperature and efficiency](heat-pump-cycling.md#flow-temperature-and-efficiency)), and usually no fewer cycles.
- If the rooms are too cold, **shift the curve by 1–2 °C at a time** rather than jumping by 4 °C, and compare using an [A/B test](heat-pump-cycling.md#how-to-decide-ab-test).
- If the rooms are warm enough only in mild weather but cold in frosty weather (or vice versa), change the **slope** (the −20 °C point) rather than the whole curve.

> [!TIP]
> HeatPulse does not know the outdoor temperature, so its **Target water temperature** is a fixed number. With weather compensation, update it occasionally to the current value from the curve, or read the **Water − target** tile as approximate.

## How the Versati controls the compressor

From the Gree service and control manuals:

- In heating, the compressor frequency follows the difference between the **actual leaving water temperature and the setpoint**: the larger the difference, the higher the frequency. When the compressor is already at its **minimum frequency** and the water is still too warm, the unit **shuts down** – this is where cycling in mild weather comes from.
- After the compressor stops it does **not restart for at least 3 minutes**.
- The commissioning parameter **`ΔT-Heat`** ("leaving water temperature difference for heating") has a range of **2–10 °C** with a default of **10 °C**. The available manuals do not explain unambiguously whether it is the restart hysteresis or the target flow/return difference, so do not change it without your installer.
- The water pump speed is controlled by the difference between entering and leaving water temperature.

## Manufacturer data

The Versati III Split service manual covers this model (links in [references](references.md)). The tables vary by revision and test conditions; without checking the rating plate of the actual unit, treat them as indicative.

- The nominal capacity of the model range is about **10 kW**. The service table for one revision of the three-phase 10 kW variant gives, at **A7/W35**, a heating capacity of **9.5 kW**, power input of **2.07 kW** and a COP of about **4.6**; at **A7/W45**, a heating capacity of **9.5 kW**, power input of **2.64 kW** and a COP of about **3.6**. These are nominal laboratory points, **not the minimum or maximum modulating capacity**.
- The heating outlet temperature can be set from roughly **20 to 55 °C** (normal-temperature version, depending on revision).
- For reliable **defrosting**, the service manual requires a water volume of **at least 5 l/kW**, i.e. **at least 50 l** for a 10 kW unit. A 100 l buffer tank, for example, exceeds this nominally, but it must actually be hydraulically available to the heat pump (depends on valves, pumps, bypass and the way it is piped).
- The ideal heat storage of **100 l of water per 1 K is 0.116 kWh**, so with a usable span of **5 K it is 0.58 kWh**. If the heat pump output exceeded the house demand by, say, **3 kW**, this storage would be charged in about **11.6 minutes**. In reality flow, losses and stratification play a role – this is an illustration only, not a prediction of run time.

## What to aim for

- **Run length:** at least 10 minutes per run ("good" in HeatPulse), runs under 6 minutes ("poor") should be rare. See the [run length rating](heat-pump-cycling.md#how-long-should-a-run-be).
- **Off periods:** half an hour or more between runs in mild weather is fine.
- **Flow temperature:** as low as the weather compensation curve allows while keeping the rooms comfortable.
- If steady heating without domestic hot water and defrosting repeatedly shows more than **3–4 starts per hour** or mostly poor runs, check the TRVs, flow rates and hydraulics before raising the curve.

## Diagnostic procedure over several days

1. **Do not change any settings for the first 24–48 h.** Note down from the controller the `Compressor` status, `T-water out PE`, `T-water in PE`, the outdoor temperature and the times of domestic hot water heating and defrosting. Check that the logger's [thresholds](usage.md#how-to-tune-the-thresholds) match the actual compressor starts and stops.
2. Look at the **run length rating** in HeatPulse. Mostly good/ideal runs with long pauses mean the system is fine even though it cycles.
3. Find out **how the buffer tank is connected** (in series, or as a hydraulic separator / parallel four-pipe tank) and whether it actually charges and discharges. Open the thermostatic radiator valves in the main rooms fully.
4. Compare the flow temperature from the curve with the actual outlet temperature. If the outlet repeatedly overshoots and the compressor stops after a few minutes, the heat pump is at its minimum modulation, or the flow or the active water volume is too low.
5. If the rooms are not warm enough, shift the curve by 1–2 °C. If they are warm and the heat pump runs well, try lowering the curve by 1 °C. Always compare the daily electricity consumption at similar outdoor temperatures ([A/B test](heat-pump-cycling.md#how-to-decide-ab-test)).
6. Do not change the service parameters `ΔT-Heat`, `Heat run time` or the circulation pump speeds blindly – they depend on the hydraulics and the firmware version.
7. To determine the **minimum and maximum compressor output** you need its frequency (status parameters on the controller or an authorised Modbus export), the electrical power input and/or a calibrated flow measurement. Two clamp-on sensors **do not measure heat output or COP**.

## Recognising the capacity limits

- **Minimum modulation / low demand:** even at its lowest frequency the heat pump heats the water above the setpoint and stops; thermostatic valves close or the tank charges too quickly. From the gas pipe temperature alone this cannot be reliably distinguished from the control hysteresis.
- **Maximum output / insufficient capacity:** very long continuous runs, high compressor frequency, outlet water persistently below the curve and gradually falling room temperatures. Restricted flow, defrosting, protection limits and domestic hot water heating must be ruled out.
