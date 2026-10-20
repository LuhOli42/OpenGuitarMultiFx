# Tape and drum echoes (Space Echo, Echoplex EP-3, Echorec)

Processor: `Source/Effects/Delay/TapeEchoProcessor.{h,cpp}` — keys `SpaceEchoStyleDelay`, `EchoplexStyleDelay`, `EchorecStyleDelay`.

A behavioural model of a magnetic medium moving past heads, not a parts-level circuit (no schematic used):
- each playback head's delay = its distance from the record head / the medium's speed; the speed wanders (wow ~0.7 Hz,
  flutter ~9 Hz — far less on the Echorec's drum), so repeats are detuned the way a transport detunes them;
- recording saturates (an algebraic magnetic-saturation curve, ~1.5 V knee) and has the record amp's coupling high-pass;
- playback loses treble (head-gap loss, 6 kHz / 5 kHz for the drum);
- repeats go back through the record amp: at full Intensity / Repeats / Swell the loop gain passes 1 and it runs away
  into self-oscillation, bounded by the saturation.

| Unit | Heads | Controls |
|---|---|---|
| Roland RE-201 Space Echo | three, at 1 : 2 : 3; Repeat Rate = motor speed (head 1 ≈ 65–185 ms) | Mode 1–12 (1–4 echo only, 5–11 echo + spring, 12 spring only), Repeat Rate, Intensity, Echo Volume, Reverb Volume, Bass, Treble. The spring is the project's own `SpringReverbProcessor`, fed the input. |
| Maestro Echoplex EP-3 | one; the record head slides (Echo Delay 60–750 ms) | Echo Delay, Repeats, Echo Volume, Record Volume. Everything first goes through the **EP-3 record preamp — the same `EPStyleBoosterProcessor` circuit model** as the EP Booster. |
| Binson Echorec 2 | four, equally spaced on the drum (75/150/225/300 ms) | Selector 1–12 (head combinations), Swell, Echo Volume, Bass, Treble |

Head delays and the RE-201's mode table are the commonly published ones, not measured. Candidate for a parts-level
upgrade if service schematics are sourced (Notion).
