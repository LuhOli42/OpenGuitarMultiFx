# DD-2-Style Delay (Boss DD-2, key `DD2StyleDelay`)

Processor: `Source/Effects/Delay/BbdDelayProcessor.{h,cpp}` (shared engine; this model is a `case` in `prepare()`). Delay time: the clock is an RC oscillator whose period is proportional to the Delay pot's resistance, so the delay is linear in that resistance (through the pot's taper) between the published minimum and maximum.

Source: Boss DD-2/3 circuit diagram, which annotates its own blocks. The first compact digital delay (1983): pre-emphasis,
a **7 kHz 6th-order** anti-alias (Q3–Q6), NE570 compressor, a **12-bit** converter (R-2R array, successive approximation),
three 4164 DRAMs, a VCO of 1.28–6.18 MHz, then the NE570 expander and a 7 kHz 4th-order reconstruction (Q9–Q10). The drawing
gives the mode ranges: **S 12.5–50 ms, M 50–200 ms, L 200–800 ms, Hold**. All three use the same converter rate range
(~20.5–82 kHz) over 1/16, 1/4 and all of the memory — so the DD-2 is modelled exactly as what it is: a queue of samples at
a variable rate with sample-and-hold, i.e. the BucketBrigadeDelay with its samples rounded to 12 bits, one queue per mode.
Hold stops writing and the memory recirculates.
