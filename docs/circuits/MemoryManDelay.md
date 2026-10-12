# Deluxe Memory Man-Style Delay (EHX, key `MemoryManStyleDelay`)

Processor: `Source/Effects/Delay/BbdDelayProcessor.{h,cpp}` (shared engine; this model is a `case` in `prepare()`). Delay time: the clock is an RC oscillator whose period is proportional to the Delay pot's resistance, so the delay is linear in that resistance (through the pot's taper) between the published minimum and maximum.

Source: EHX Deluxe Memory Man schematic (`EH Deluxe Memoryman Dlay.pdf`). Two MN3005 in series (**8192 stages**), 4047
clock (240p / 1n, Delay 100K + 5.6K → a 18.9:1 resistance ratio, matching the published **30–550 ms** almost exactly),
NE570 compander. The **input preamp** (4558: 100K in, 22K + Level 1M feedback, up to ×10 on 15 V) overdrives — the DMM's
famous grit — and feeds both dry and delay. **Chorus-Vibrato** = an LFO on the clock (depth knob; the switch picks the
slow chorus rate ~0.45 Hz or the vibrato ~4.5 Hz). Blend = dry ↔ delay pot, Feedback up to 1.08 (it oscillates).
Filters approximate (the redraw's 4558 Sallen-Key values partly illegible): two 2nd-order stages in and out, ~1.8-3 kHz -- the DMM's known dark repeats.
