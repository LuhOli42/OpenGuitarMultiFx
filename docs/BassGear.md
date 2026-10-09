# Bass Gear Survey — amplifiers & pedals worth modelling

*Internet research pass, 2026-10-09. Every amp and pedal in the repo so far is
guitar-voiced; this is the candidate list for a bass-focused expansion, ordered
by how much real-world ground each model covers and how well it fits the
project's existing circuit-modelling approach (direct nodal analysis +
`chowdsp_wdf`, one `docs/circuits/*.md` writeup per model, `-Style` names in
`EffectRegistry`). NAM + TONE3000 already cover *captures* of anything — this
list is about physically-modelled processors, same bar as the guitar gear.*

## Amplifiers

### Tier 1 — the reference tones

| Candidate | Why it matters | Circuit notes |
|---|---|---|
| **Ampeg SVT (SVT-CL/VR, 1969+)** | THE bass amp. 300 W all-tube (six 6550s), the default rock/metal bass sound on stages for 50+ years. Bill Wyman to Robert Trujillo. Every bass sim (Line 6 Helix, Neural DSP, Amplitube) leads with it. | 14 tubes total; 12AX7 gain stage + Baxandall-ish "Ultra Hi/Lo" voicing switches + mid-shift rocker, long-tail-pair PI, 6550 sextet. Schematics widely published. Big model — the repo's biggest amp so far is the Dual Rectifier; comparable scale. Tone stack is passive and unusual (SVT stack ≠ Fender/Marshall). |
| **Ampeg B-15 Portaflex (1960s)** | The *studio* bass amp — Motown (Jamerson), Stax (Duck Dunn), Chuck Rainey. 25 W, 6SL7 octal preamp, 2x 6L6GC, sealed 1x15 double-baffle cab. Small, tractable circuit (5-6 tubes) with a huge legacy. Probably the single best effort-to-icon ratio on this list. | 2-channel (Volume/Treble/Bass per channel), octal-tube preamp (different feel from 12AX7s), cathode-bias feel, portaflex cab is half the sound — pairs naturally with a dedicated 1x15 cab IR/model. |
| **Acoustic 360/361 (1967)** | First loud bass amp; solid-state 200 W into a rear-firing folded-horn 18" cab. John Paul Jones (Zep live), Larry Graham. Covers the "clean solid-state headroom + huge low end" archetype nothing else here does. | Solid-state preamp — models as op-amp/transistor nodal rather than tube WDF; cheap on CPU vs. the SVT. The folded-horn 361 cab is exotic; a faithful cab model may matter more than the head. |
| **Trace Elliot GP12 SMX / AH250 (1980s)** | The 80s British bass sound — Mark King/Level 42, John Entwistle's later rigs. Dual-band compressor, 12-band graphic EQ, bi-amp outputs. Covers slap/the whole "hi-fi clean" era that no Fender/Marshall-style amp covers. | Solid-state + a real dual-band comp + a 12-band graphic EQ — largely a *DSP* model (envelope follower + filter bank, both already in the codebase via `EnvelopeFollower`/`GE7StyleEqualizer`), less a nonlinear-circuit one. Fast to build, very distinctive. |
| **Mesa/Boogie Bass 400+ (or D-180)** | The other big all-tube rack bass head — Paul McCartney's 90s rig, plus a dozen modern players. 500 W, twelve 6L6/5881s, graphic EQ on a tube core. Covers "modern tube power + EQ sculpt" vs. SVT's "vintage grind". | Preamp is Fender-ish (Mesa lineage) + 7-band GEQ — the project's JCM800/5150-class machinery transfers directly. |

### Tier 2 — strong niche coverage

| Candidate | Niche |
|---|---|
| **Orange AD200B Mk3** | Doom/stoner bass standard (only 3 knobs — Gain/Bass/Treble/Master). Tiny circuit, huge vibe. |
| **Gallien-Krueger 800RB** | The 80s bi-amp rack head (300W low + 100W high, crossover, "boost" voicing). Flea, Duff McKagan-era GNR. Bi-amping maps onto the project's multi-row `SignalGraph` — a genuinely different topology, not just another tone stack. |
| **Aguilar DB 751** | The modern boutique tube standard (session players, pop/R&B). Hybrid tube preamp + Class D power — preamp model + clean power stage. |
| **SWR SM-900 / Bass 350** | 90s hi-fi "California" sound — aural enhancer knob, solid-state hi-fi voicing. Marcus Miller-adjacent session tone. |
| **Ampeg V4B** | The 100 W "little SVT" — same DNA, simpler; cheap incremental model once SVT machinery exists. |
| **Fender Bassman (piggyback era, '61-'65 blonde / '64+ BF)** | NOTE: the repo's `BassmanStyleAmplifier` is the *tweed* 5F6-A (the guitar legend). The actual *bass*-era Bassman is the later piggyback head — different amp (presence control, diode rectifier, deeper voicing). Worth a distinct model under a distinct registry name, not a variant. |
| **Markbass Little Mark / Darkglass heads** | Modern Class-D era — mostly clean/neutral power; low modelling value vs. the above (their *character* lives in the preamp pedals below). |

## Pedals

### Tier 1 — the essentials

| Candidate | Why | Implementation notes |
|---|---|---|
| **Tech 21 SansAmp Bass Driver DI** | Arguably the single most-used bass pedal ever made — DI + tube-amp emulation + drive, on every pro board since 1994. SVT-in-a-box. | Analog tube-amp emulator: swept mid "Character" voicing + soft clip + speaker simulation. Circuit is proprietary but its topology (FET-based, tone-shaping + speaker-sim filters) is well documented; model as nodal + filter chain. This is the #1 pick overall. |
| **Darkglass Microtubes B7K Ultra** | The modern counterweight to the SansAmp — aggressive, articulate drive on a 4-band EQ with Blend, Growl, Attack switches. The current-generation standard (esp. metal/modern prog). | Op-amp drive into active EQ — same family as the repo's existing dual-op-amp clippers (Guv'nor/BB-style blocks) plus a parametric-mid stage. |
| **Boss OC-2 Octaver** | THE bass octave. Sub-octave synthy square + octave-2 — the "Pino/Steely Dan" sub sound and the funk octave-up-synth attack. | IMPORTANT: different beast from the repo's `OctaverProcessor` (a digital two-grain shifter). The OC-2 is *analog*: envelope-tracked gated octave + flip-flop octave divider — no pitch shifting. Model: tracking detector + square sub-oscillator + octave-divider, a new DSP family. |
| **MXR M82 Bass Envelope Filter (Mu-Tron III lineage)** | Funk bass's defining effect — Bootsy's Mu-Tron is the ancestor. Auto-wah state-variable filter swept by picking dynamics; Dry/FX blend preserves lows. | Envelope follower (exists: `EnvelopeFollower`) driving a 2-pole state-variable bandpass/lowpass — a new but small processor. Mu-Tron III vs. M82 could be one class, two models (like `RatStyleDistortionProcessor`'s model enum). |
| **EHX Bass Big Muff Pi** | The bass-voiced Muff — dry-blend + bass-boost switch, Cliff Burton's (and a thousand others') fuzz. | Straight variant of existing `BigMuffStyleFuzzProcessor` (already has a `Model` enum: usV3, sovtek…): add bass voicing + dry blend + bass-boost switch. Cheapest high-value pedal on the list. |

### Tier 2 — round out the board

| Candidate | Niche |
|---|---|
| **Ibanez TS9B Bass Tube Screamer** | Bass-voiced TS (bass knob + mix). Trivial variant of `TubeScreamerStyleOverdriveProcessor`. |
| **Boss GEB-7 Bass EQ** | Bass-voiced 7-band GEQ (50/120/400/500/800/4.5k/10k). Reband of `GE7StyleEqualizerProcessor`. |
| **Boss ODB-3 Bass OverDrive** | The 90s punk/metal bass drive — two-band EQ + balance blend. Divisive in real life, ubiquitous in covers. |
| **Aguilar Tone Hammer preamp pedal** | The session-player's DI/preamp — AGS (adaptive gain shaping) drive + sweepable mids. Pairs the DB751 amp below-or-above. |
| **MXR M80 Bass D.I.+** | The other standard bass DI — clean DI + a Color/gate + distortion channel. |
| **Ampeg SCR-DI / Scrambler** | SVT preamp in a pedal + the vintage Scrambler octave-fuzz (Jeff Ament's tone). Two-for-one: preamp portion reuses SVT preamp work. |
| **DOD Meatbox Subsynth** | Cult sub-harmonic generator — feeds 15 Hz+ subs to the PA; stoner/doom + live PA trickery. Shares the OC-2's sub-generator DSP work. |
| **EHX Bass Clone** | Bass chorus with crossover (keeps lows dry, choruses the tops) — Krist Novoselic's sound. Novel DSP: split-band modulation, maps to `SignalGraph` multi-row or internal crossover. |
| **Boss SYB-5 / EHX Bass Mono Synth** | Bass synth — gated oscillator + filter modes. Larger effort; only worth it if synth sounds are wanted. |
| **Boss BC-1X / Empress / Cali76 bass compressors** | Multi-band studio-grade comp. LOW priority — the repo's 1176/LA-2A/DBX160/DynaComp/SSL-bus models already cover bass compression duties; only a bass-specific *multi-band* comp adds anything new. |

## Supporting work (not processors)

- **Bass cabinets**: Ampeg SVT-810E 8x10 (the fridge), B-15 1x15 sealed, Acoustic 361 folded-horn 18", Hartke 4.5XL aluminium-cone 4x10. These are `IRLoaderProcessor`/`DynamicCabProcessor` roles — bass cab IRs, or behavioural cab models where IRs can't be sourced.
- **Voicing audit**: several existing processors only need rebands/variants (see Tier 2 pedals). Worth one pass marking which existing models are already bass-safe vs. guitar-high-passed.
- **Input path**: bass needs extended low-end response (down to low B, ~31 Hz) — worth auditing `PickupLoading`/input chain and every amp model's coupling caps for low-frequency droop; most guitar models intentionally roll off below ~80 Hz.

## Suggested build order

1. **SansAmp Bass Driver DI** (`BassDriverStylePreampProcessor`) — highest coverage/effort ratio; the "bass rig in a box".
2. **Ampeg B-15** (`PortaflexStyleAmplifier`) — small tube circuit, studio legend, pairs with a 1x15 cab.
3. **Darkglass B7K-style** (`MicrotubeStylePreampProcessor`) — the modern standard.
4. **OC-2-style analog octaver** — new DSP family, unlocks Meatbox later.
5. **Mu-Tron/M82-style envelope filter** — new DSP family, unlocks funk category.
6. **Ampeg SVT** (`SVTStyleAmplifierProcessor`) — the flagship; biggest effort.
7. Bass-voiced variants of existing models: Bass Big Muff, TS9B, GEB-7 (cheap, high polish).
8. Then Tier-2 amps by genre demand: Trace Elliot (slap), Orange AD200B (doom), GK 800RB (bi-amp topology).

Sources: Ampeg SVT/Portaflex history (ampeg.com/history, Wikipedia, Guitar World, Sweetwater), Reverb's "6 Classic Amps Every Bassist Should Know", Boss/MXR/EHX/DOD product pages, Sweetwater pedal roundups, bass octave pedal comparison literature (OC-2 consensus as the bass standard).
