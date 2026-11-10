# Heat pump cycling and flow temperature

What published research and practical experience say about compressor starts, run lengths and flow temperature – and how to use HeatPulse to find the best setting for your house. Sources are listed at the end and in [references](references.md#cycling-and-efficiency).

> [!NOTE]
> This is a summary of general studies and manufacturer documentation, not advice for your specific installation. Settings beyond the user menu should be changed by your installer.

## What happens when the compressor starts

- **Heat is produced almost immediately**, but not at full efficiency. In a laboratory study the heating capacity reached its maximum 42–60 s after start-up, while the electrical input was at full value in under 20 s. Another heat pump reached 90 % of its steady-state output 3 minutes after starting.
- **The first minutes are less efficient** because the refrigerant, the heat exchangers and the water in the pipes have to warm up first. Part of that heat is lost after the compressor stops. Every start therefore costs a fixed amount of energy, which short runs cannot recover.
- The popular claim that "for the first 7 minutes electricity produces no heat" is an exaggeration, but it captures the idea: **a run of a few minutes is mostly start-up overhead**.
- Compressors also need a minimum running time for lubrication: compressor manufacturer Bitzer recommends **at least 5 minutes** per run and warns against repeated shorter cycles.

## How long should a run be

A UK government-funded test (EA Technology) found that **runs shorter than 6 minutes clearly reduce the COP**, much more for air-source than for ground-source heat pumps. Runs of **about 8 minutes** brought the air-source unit close to its catalogue COP, and longer runs gave further benefit. The report recommends designing systems for a minimum run time of about 6 minutes in all conditions. (The tested units had fixed-speed compressors; inverter units can modulate, but the start-up overhead remains.)

HeatPulse rates each estimated run (hot phase) using these limits:

| Run length | Rating | Meaning |
|---|---|---|
| under 6 min | **poor** | start-up losses dominate, compressor wear – investigate |
| 6–10 min | **acceptable** | close to the minimum recommended run time |
| 10–20 min | **good** | the compressor has time to settle and modulate |
| 20 min and more | **ideal** | start-up losses become negligible |

**Starts per hour matter less than run length.** No published "maximum starts per hour" was found for air-to-water heat pumps. As a rule of thumb, 1–2 starts per hour with off periods of half an hour or more are fine as long as each run lasts at least 10 minutes. Experienced users describe "good" cycling as tens of minutes between runs and "bad" cycling as runs of a couple of minutes. If the heat pump still cycles faster than about once an hour after all the measures below, it may simply be oversized for the house.

## Flow temperature and efficiency

Every degree of lower flow temperature improves the heat pump's efficiency by roughly **1.5–3 %** (about 2.5 % is a common figure) because the compressor has less temperature lift to overcome. Raising the flow temperature by 4 °C therefore costs roughly **6–12 %** of efficiency during the whole time the heat pump runs.

Lower flow temperatures only help as long as the radiators can still keep the rooms warm.

## Does a higher flow temperature reduce cycling?

Usually **not**, and it costs efficiency:

- The house needs the same amount of heat regardless of the water temperature. If that demand is below the heat pump's minimum output (typical in mild weather), the heat pump has to cycle anyway.
- Hotter radiators overheat the rooms, thermostatic valves close, and the demand seen by the heat pump falls even further.
- The length of each run is mainly determined by the **water volume** in the system (radiators, pipes, buffer tank) and the **hysteresis** of the controller – not by the flow temperature setpoint.

A higher flow temperature can help in one situation: when the radiators at the current temperature cannot even release the heat pump's minimum output, so the water temperature overshoots within a few minutes and the compressor stops after very short runs. That shows up as many **poor** runs in HeatPulse, not as runs of 10+ minutes with long pauses.

## What actually helps against short cycling

1. **Open thermostatic radiator valves fully** in the main living areas (the "reference zone"). Closed TRVs are the most common cause of short cycling; the EA Technology tests showed a large improvement simply by having two radiators open instead of one.
2. **Make sure the buffer tank is really in the heat pump circuit** and adds water volume (series or four-pipe connection; see your installation).
3. **Use weather compensation** and tune the curve so the water is just warm enough – rather than a fixed, higher flow temperature.
4. **Hysteresis / restart difference** of the controller – a wider band gives longer runs. Change it only if you know exactly what the parameter does on your model.
5. **Larger emitters or more open radiators** let the heat pump run longer at a low flow temperature.

## How to decide: A/B test

Neither the COP figure nor the number of starts alone tells you which setting is better. What counts is **electricity used per day for the same comfort**:

1. Run setting A (e.g. the current weather compensation curve) for 2–3 days, setting B (e.g. the curve shifted by +2 °C) for another 2–3 days.
2. Compare days with **similar outdoor temperatures** and the same comfort (room temperature, domestic hot water use).
3. Read the **electricity meter** of the heat pump (or the whole house at the same time each day) and note the daily consumption.
4. Use HeatPulse to compare the **run length rating** and the number of starts.
5. Keep the setting with the lower consumption at the same comfort.

## Limits of the HeatPulse estimate

HeatPulse measures the hot phase of the refrigerant pipe, not the compressor itself. A run starts when the pipe stays above the hot threshold and ends when it stays below the cold threshold, so the estimated run length can differ from the real one by a minute or two, depending on the [thresholds](usage.md#how-to-tune-the-thresholds). Defrost cycles can appear as extra short runs.

## Sources

- EA Technology for the UK government (DECC): [The Effects of Cycling on Heat Pump Performance](https://assets.publishing.service.gov.uk/government/uploads/system/uploads/attachment_data/file/65695/7389-effects-cycling-heat-pump-performance.pdf)
- Brudermueller et al. 2023, cycling of 503 heat pumps in the field (with a review of cycling losses and start-up times): [Earth Notes bibliography](https://www.earth.org.uk/bibliography/brudermueller2023cycling.html)
- Start-up and shutdown transients of heat pumps: [IIR](https://iifiir.org/en/fridoc/theoretical-and-experimental-investigation-of-startup-and-shutdown-135447), [REHVA Journal](https://rehva.eu/rehva-journal/chapter/improved-characterization-of-water-to-water-heat-pumps-part-load-performance)
- Bitzer: [cycling rate and minimum running times](https://bitzer.de/shared_media/html/st-420/en-GB/327407627327409419.html)
- Flow temperature and efficiency: [CIBSE Journal](https://www.cibsejournal.com/cibsejournal-magazine/202308/50/page.html), [Linemetrics](https://edm.linemetrics.com/en/massnahmen/geb-heiz-010), [iDM](https://www.idm-energie.at/en/heat-pump-efficiency/flow-temperature/)
- Practical experience: [Renewable Heating Hub – performance in mild weather](https://renewableheatinghub.co.uk/forums/renewable-heating-air-source-heap-pumps-ashps/performance-of-heat-pumps-in-mild-weather), [OpenEnergyMonitor – demand vs load cycling](https://community.openenergymonitor.org/t/heat-pump-demand-cycling-vs-compressor-load-cycling/29277), [Homey – common heat pump mistakes](https://homey.app/en-ae/wiki/common-heat-pump-mistakes-and-how-to-prevent-them/)
