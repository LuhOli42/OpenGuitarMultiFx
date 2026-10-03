# Dual Rectifier-Style Amplifier

A component-level model of the **Mesa/Boogie Dual Rectifier** RED channel, built from the factory
Mesa Boogie schematic (2-channel, dated 6-93 GEO. M., obtained from el34world.com). The Dual Rectifier
is one of the defining high-gain amplifiers of the 1990s–2000s metal and hard rock era, known for its
massive low-end, saturated gain, and chunky rhythm tone.

Display name: **Dual Rectifier-Style Amplifier** (trademark-safe convention).

## Architecture

**RED channel only** is modelled, as the amp's defining voice. The ORANGE channel, LDR switching
circuitry, tube/diode rectifier select switch, and FX loop are not implemented. Solid-state
rectification only is modelled.

### Preamp (four cascaded 12AX7 gain stages + cathode follower)

1. **V1A** (220K plate from E rail, 68K grid stopper, 1M grid leak, 1.8K/22µF bypassed cathode)
   - The input stage. Guitar signal enters through a 68K grid stopper.

2. **22nF coupling** → **470K grid leak** → **RED Gain pot** (1MA audio taper, split rGainTop/rGainBot)
   - The RED channel's main drive control.

3. **V2A** (220K plate from D rail, 39K series from Gain wiper, 470K grid leak, 1.8K/22µF bypassed cathode)
   - Second gain stage. Full cathode bypass for maximum gain.

4. **V2B** (100K plate from D rail, 470K series from 22nF coupling, 220K grid leak, 27K unbypassed cathode)
   - Compression/clipping shaping stage. The 27K unbypassed cathode (estimated from 384V plate /
     ~6V cathode → Ip ≈ 22µA) creates a low-gain, soft-clipping stage similar to the Mark IIC+'s V2b.

5. **V3A** (220K plate from C rail, 39K grid stopper, 220K grid leak, 1.8K/22µF bypassed cathode)
   - Final gain stage before the follower/tone stack.

6. **V3D cathode follower** (via `addFollower`)
   - Buffer between the preamp and tone stack. DC offset measured in a two-pass build.

### Tone stack

Fender-derived TMB with Dual Rectifier-specific values:
- 47K slope resistor (R273)
- 680pF treble cap (C4 — larger than the SLO-100's 470pF or the Mark IIC+'s 250pF, giving a
  warmer treble response)
- 250K treble pot, 22K series resistor (R254)
- 1M bass pot, .02µF bass and mid caps (C23, C26)
- 25K mid pot
- **1M Master** (MSTR) — a post-tone-stack master volume control (rheostat), modelled as the
  real amp's own RED channel master

### Phase inverter

12AX7 long-tailed-pair:
- V5B: 82K plate, V5A: 90K plate (C rail)
- 1K combined tail resistor
- 75pF compensation cap between plates
- .047µF coupling caps to the power tubes

### Power amp

4 × 6L6GC beam tetrodes as two push-pull pairs (fixed bias at -51V):
- .047µF coupling caps, 1.5K grid stoppers (3K per pair), 220K grid leak resistors
- 1K/2U screen resistors per tube

### Output transformer and global feedback

- XFMR #562L05, 4 and 8-16 ohm taps
- NFB: R276=47K from OT to PI, R353=10K; scaled to 198K for the 16 ohm model tap
- Presence: 25K pot shunting the feedback path, with C52=0.1µF and 1.5nF stray

### Power supply

460V nominal. Solid-state rectifier, 60Ω series resistance.
Choke-filtered chain: sA (plates, 220µF, 150K bleeder) → CHOKE → sB (screens, 220µF) →
2.7K (R502) → sC (PI/30µF) → 22K (R252) → sD (V2/V3/30µF) → 15K (R261) → sE (V1A/30µF).

## DC operating point (verified)

| Node | Voltage |
|------|---------|
| Plate rail (sA) | 460.0 V |
| Screen rail (sB) | 458.8 V |
| PI rail (sC) | 444.8 V |
| V2/V3 rail (sD) | 337.7 V |
| V1A rail (sE) | 327.2 V |
| V1A plate | 171.8 V |
| V2A plate | 177.1 V |
| V2B plate | 323.7 V (near supply — very low current due to 27K unbypassed cathode) |
| V3A plate | 230.6 V |
| Follower out | 231.5 V |
| PI tail | 3.4 V |
| Bias node | -47.6 V |

## Open issues

Same kg1 instability pattern as the SLO-100, Mark IIC+, and JCM800: the full reference model's absolute
gain is collapsed by the 40x kg1 stability fix on both PI triode and power pentode. The reduced-order
power stage keeps the Twin Reverb's own fitted 6L6GC constants (same tube type, same push-pull topology)
rather than calibrating against this unreliable reference.

## Performance

- Full model plucked note: 0 failures, 0 recoveries (but near-silent output due to kg1 collapse, peak 0.016)
- Reduced-order: ~10% avg CPU (4 preamp gain stages — slightly lighter than the 5-stage SLO-100/Mark IIC+)
- Unity trim: -23.06 dB
