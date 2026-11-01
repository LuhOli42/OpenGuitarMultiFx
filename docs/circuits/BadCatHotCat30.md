# Bad Cat Hot Cat 30-Style Amplifier

## Sources

- **A Bad Cat Hot Cat 30 schematic**, used for the topology rebuild in PR #111. That rebuild is what the code models;
  this page was written before it and has been updated to match.
- Bad Cat's product pages and tube charts, for the panel:
  - [Bad Cat Hot Cat product family](https://badcatamps.com/)
  - [Bad Cat manuals and tube charts](https://badcatamps.com/manuals-and-tube-charts/)

This page used to describe an earlier estimate: an EF86 front end, a Treble/Middle/Bass stack, and four EL84s. None
of that is in the model any more. There is no shared EF86 and no Middle control, and the power section is the amp's
**EL34 pair**.

## What's modelled

### Gain channel

- **V1a:** 68k / 1M input, 220k plate with 1n5 bright cap across it, 1k cathode.
- **GAIN:** 470k into a node with 1n5 to ground and the GAIN pot as a 250k log shunt leg, then 470k on.
- **V2b:** 220k plate, 1k5 + 100n cathode.
- **EDGE:**
  - 22n into a node with 10n + 470k to ground.
  - Then EDGE as a 1M log series rheostat, with 150k + 360p to ground after it.
- **V2a:** 100k plate, 1k5 + 1 µF cathode.
- **LEVEL:** 10n into the 1M linear LEVEL (470k across its top), then 470k into V3b's grid (270k leak).
- **V3b:** 100k plate, 1k cathode.
- **V3a:** a DC-coupled cathode follower (56k).
- **Tone and Master:** the Treble / Bass stack, then 220k into MASTER.

### Clean channel

- **V1b:** 68k / 1M input, 220k plate with 1n5 bright cap, 1k5 + 22 µF cathode.
- **VOLUME:** 1M log with a 180 pF bright cap.
- It feeds the phase inverter's second grid (10n). This is the interactive two-channel wiring.

### Phase inverter and power

- **Phase inverter:** an elevated-reference long-tailed pair. The cathodes go through 1k2 to a reference node with 47k
  to ground (~69 V cathodes), and the 1M grid leaks return to that node.
- **Couplings and Brilliance:** .033 couplings. BRILLIANCE is 4n7 + 1M log across the two coupled outputs.
- **Grid circuit:** 220k leaks, 10k / 2 W grid stoppers.
- **Output tubes:** 2× EL34 (Koren fit) on a shared 135 Ω + 250 µF cathode, with 367 V screens.
- **Output transformer:** ~4.5k plate-to-plate.
- **Supply:** a sagging rectifier / choke supply.

## Controls

Page 1 matches the real panel:

| Param | Real control |
|---|---|
| Channel | Gain / Clean |
| Gain | Gain channel |
| Edge | Gain channel |
| Bass / Treble | Gain channel |
| Level | Gain channel |
| Clean Volume | Clean channel |
| Brilliance | Shared |
| Master | Shared |

Page 2 (synthetic): Power Drive, Bias, Tube Feel, Speaker, Output.

## Verification (current main)

| Measurement | Voltage |
|---|---:|
| B+ | 380.5 V |
| Preamp plate (V1a) | 109.7 V |
| PI plates | 342.9 / 342.9 V |
| Power plates | 388.1 / 388.1 V |
| Shared EL34 cathode | 32.0 V |

**Tests:**
- Silence peaks at 2 µV, with no solve failures.
- The reduced path tracks the netlist within 0.43 dB in the unit test, and within 1.43 dB level / 1.95 dB band shape
  in AmpFullVsReducedConsistency.
- The registry trim (+14.75 dB) puts it at unity in PedalUnityLevel.

## Reduced path refit (2026-11-01)

Level-only correction (2026-11-01): the reduced stage sat ~3 dB under the netlist; level 34 -> 40. Its curve (tanh,
gain 0.9) was searched on level and distortion at Gain 0.2 / 0.5 / 0.8 and kept: nothing in the search was closer.
Fully driven it squares off ~4 dB sooner than the netlist.

The unity trim moved 14.75 → 13.19 dB to keep the amp at unity. AmpFullVsReducedConsistency bound 1 / 3 (was 3 / 11).
