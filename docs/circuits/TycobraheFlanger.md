# Pedalflanger (Tycobrahe-Style, key `TycobraheStyleFlanger`)

Processor: `Source/Effects/Modulation/Flanger/BbdFlangerProcessor.{h,cpp}` (shared BBD flanger engine; this model's values are in its `prepare()` case).

Source: Dirk Hendrik's redraw "Tycobrahe Pedalflanger" (2007, `Flangers/Tycobrahe Pedalflanger.pdf`).

The one model whose delay window comes straight from the drawing: TP1 is annotated **51–509 kHz** (heel → toe) on the 4046
VCO, so with the MN3001 (512 stages) the delay runs **5.0–0.5 ms** — locked by a test. The treadle (Pedal) is Manual;
Mode switches to the auto LFO (Spread = its width, Rate). **Intensity** feeds the wet back through a **germanium diode pair**
(D1/D2, `diodes::germanium` shunt clipper) and C2 10n / R3 330K. Filters are minimal on this design (taken as one pole in,
one out — the drawing's R28/C12 network after the BBD).

Like the Mistress, the output gets two ~17 kHz poles so the BBD's ultrasonic clock images do not alias into the audio band in the 48 kHz model.
