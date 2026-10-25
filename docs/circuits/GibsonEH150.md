# Gibson EH-150 (late pre-war, 6SQ7 / 6N7 / 6L6)

The Gibson EH-150 is the archetypal jazz-box amplifier, the amp Charlie Christian used. It went through several
revisions. The model here is the **late pre-war** version: octal 6SQ7 preamp tubes, a 6N7 phase inverter in place of
the earlier interstage transformer, and 2× 6L6. That is the one generally meant by "the Charlie Christian EH-150".

## Sources

- **Gibson factory drawing, "EH-150 LATER"** (circulated as `Gibson_eh_150_later.pdf`, e.g. on el34world). Every
  value below comes from this drawing unless it is marked *estimated*. The drawing uses the period notation where
  "M" means thousand: "100M" is 100k, "250M" is 250k, "10M" is 10k.
- The "older" and undated drawings of the same model (`Gibson_eh_150_older.pdf`, `Gibson_eh150.pdf`) are the earlier,
  transformer-coupled revisions. They are not modelled.
- Radiomuseum model pages for the tube complement: 3× 6SQ7, 6N7, 2× 6L6, 5U4.

## What the real circuit is

- **Inputs.** There are two instrument jacks. Each goes through a 100k resistor into a shared 500k Volume pot. The
  wiper goes straight to the grid of a 6SQ7.
- **Microphone channel.** The mic jack (2M grid leak) feeds its own 6SQ7 (3k + 20 µF cathode, 100k plate). That goes
  through .01 into a second 500k Volume pot, which drives a second 6SQ7.
- **Shared second stage.** The mic channel's second 6SQ7 and the instrument 6SQ7 are wired in parallel. They share
  one 100k plate load and one 1k + 20 µF cathode. So a guitar gets **one** 6SQ7 gain stage before the phase
  inverter. The two-stage path is the microphone's.
- **Tone.** From the shared plate, the signal reaches the 6N7 grid through a .1 µF capacitor in series with the
  1 MEG Tone rheostat. A .00075 µF (750 pF) capacitor bypasses that series pair. The 6N7 grid leak is 100k.
  - With the rheostat at zero, the coupling is flat.
  - Turning the rheostat in starves the lows into the 100k. The 750 pF keeps the top end passing above roughly
    2–3 kHz.
  - So the single Tone control is a **bass cut that sounds like "treble up"**.
- **6N7 paraphase.**
  - Both halves share one **unbypassed 1k** cathode resistor, and each half has a 100k plate load.
  - Plate A drives the upper 6L6 through .1 µF. That grid's leak is 250k + 12,500 to ground.
  - The 12,500 tap (about 1/21 of the swing) drives half B's grid. Plate B drives the lower 6L6 through .1 µF, with a
    250k leak.
- **2× 6L6 push-pull.** The cathodes are tied together onto **one unbypassed 200 Ω** resistor. The screens sit on the
  same node as the output transformer's centre tap. There is no global negative feedback.
- **Supply.**
  - A 5U4 feeds a 10 µF reservoir.
  - The **speaker field coil (750 Ω)** is the filter choke.
  - After it comes a 10 µF capacitor plus a 20k 20 W bleeder. This node feeds both the plates and the screens.
  - From there, 10k + 10 µF feeds the 6N7, then 100k + 10 µF feeds the 6SQ7s.

## What is modelled

1. **`pre`**
   - Source → 100k → 500k Volume → wiper onto the instrument 6SQ7.
   - A second 6SQ7 stands in for the mic channel's idling tube: its grid is grounded, as when the mic Volume is down.
     It shares the 100k plate and the 1k + 20 µF cathode.
   - Then .1 in series with the Tone rheostat, ∥ 750 pF, into the 100k leak.
   - The 6SQ7 uses Koren's ECC83 curve family with kg1 = 1410. That scales the perveance down to the published 6SQ7
     point (0.9 mA at 250 V / −2 V, rp ≈ 91k).
2. **`power`**
   - The 6N7 paraphase exactly as drawn: 100k plates, shared unbypassed 1k cathode, .1 couplings, 250k + 12,500 /
     250k leaks.
   - The 6N7 grid is driven through 22k. That stands in for the tone network's source impedance, which the
     100k-loaded preamp does not carry across the circuit split.
   - 2× 6L6 on the shared unbypassed 200 Ω, a centre-tapped ~5k primary (*estimated*: it is not on the drawing), the
     8 Ω tap, and the speaker model.
3. **`supply`**
   - The 5U4 is modelled as a source plus 150 Ω (*estimated*), feeding 10 µF, then 750 Ω (field coil), then 10 µF ∥ 20k.
   - The 6N7 and 6SQ7 rails sit below that node through the drawing's 10k and 100k, using measured idle currents and
     the drawing's RC time constants (0.1 s and 1 s).

The transformer voltage is not on the drawing. The open-circuit peak (470 V, *estimated*) was picked to put ~330 V on
the plates, which is typical for a cathode-biased 6L6 pair of the period.

## Controls (page 1)

| Param | Real control |
|---|---|
| Volume | Instrument Volume (500k, audio taper assumed) |
| Tone | The 1 MEG Tone rheostat. Up means more resistance, which cuts more bass and leaves the top through the 750 pF. |

Page 2 (synthetic):

- **Power Drive:** a master ahead of the PI. The real amp has no master.
- **Bias:** sweeps the shared cathode resistor from 300 Ω to 100 Ω, with the drawing's 200 Ω at noon.
- **Tube Feel:** sag.
- **Speaker:** 4/8/16 Ω on the 8 Ω tap.
- **Output.**

## Honest simplifications

- The microphone channel's first stage is not in the netlist, because it does not reach the guitar path while the mic
  Volume is down. Its ~0.4 mA idle current is still counted in the 100k rail drop.
- The field coil is modelled as its 750 Ω resistance. Its inductance only smooths ripple, and ripple is not modelled.
  Field-coil speaker magnetisation versus supply sag is not modelled.
- Not on the drawing, so *estimated*:
  - the transformer voltage
  - the 5U4 and winding resistance
  - the output transformer (~5k plate-to-plate)
  - the pot tapers

## Verification targets

- Supply: the field coil drops roughly 80–100 V between the reservoir and the plate/screen node. That node sits
  around 300–370 V.
- 6SQ7 shared plate: roughly 60–160 V on the low 100k-fed rail.
- 6L6 shared cathode: 10–30 V across the 200 Ω, at 25–65 mA per tube, with matched halves.
- No global feedback, so silence stays stable (AC RMS < 0.01) with no motorboating.
- Turning Tone up raises the 3 kHz / 100 Hz ratio by more than 3×. Turning Bias up gives a hotter idle.

## Reduced-order power stage

The shipped build replaces the 6N7 / 6L6 / output transformer / speaker netlist with the standard behavioural stage:

- a fitted saturator with gain0 40, ymax 0.0554 and knee 2;
- a sag table on the field-coil node (325 V to ~301 V at full drive);
- a 300 Hz shelf at 0.6.

These constants were grid-fitted against the full netlist with `AMP_ONLY=eh150 AmpFullVsReducedConsistency`. The
reduced path tracks the full one within 1.04 dB of level and 3.02 dB of band shape at the quiet and driven chord
levels (bound 2 / 4 dB). The registry trim (−10.58 dB) puts the amp at unity at noon.
