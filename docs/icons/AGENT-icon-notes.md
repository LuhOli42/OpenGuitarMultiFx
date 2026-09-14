# Effect icon set — reference and rules

**Mandatory check before writing or reviewing any `drawIcon()`:** confirm
against `docs/icons/reference-sheet.png` (or the latest attachment the user
has shared) which glyph a given effect actually uses — never assume, guess,
or reuse a glyph from a "close enough" effect. If the reference file isn't
in this folder yet, or the sheet doesn't clearly cover the effect you're
implementing, stop and ask the user rather than transcribing from memory —
that's exactly how the Cab glyph below drifted from the real sheet.

The user supplied a reference sheet ("PEDALBOARD UI — Icones de Efeitos —
Padrao Unificado") of the icon set every effect block should eventually use:
one consistent line-art glyph per effect type, white on the block's own
category-coloured outline, no fill colour on the glyph itself. Its own
tagline says it best: **same size, same style, more music.**

The sheet was pasted inline in chat (twice now, 2026-09-10 and again
2026-09-11) and this agent has no tool that can save an inline image to
disk — only what a human explicitly attaches as a file makes it into the
repo as a real asset. **If you have the original PNG, drop it in this
folder (e.g. `docs/icons/reference-sheet.png`)** so future work can check
pixel-accurate shapes instead of the descriptions transcribed below.

The 2026-09-11 viewing was read carefully, category by category, with the
image actually visible (not recalled from an earlier description), and an
SVG-based prototype of the full 68-glyph set was built and approved by the
user before any of it touched `drawIcon()` — see "Glyphs implemented so
far" below for what shipped, and the category table above for the full
name list per category. The 8 glyphs below are the only ones translated
into real JUCE `Graphics`/`Path` calls; the rest have no `EffectProcessor`
yet (Phases 2–5) and are deliberately left undrawn rather than speculatively
coded against a class that doesn't exist.

## Rule for every effect's `drawIcon()`

**Icons are embedded SVG assets (`Assets/Icons/*.svg`) drawn through
`juce::Drawable`, not hand-written `juce::Path`/`Graphics::drawLine()` calls
(changed 2026-09-11).** The first implementation reimplemented each glyph's
geometry by eye from the approved SVG prototype, and drifted from it in
several independent, compounding ways per icon — hardcoded pixel stroke
widths that didn't scale with the icon's actual ~65-70px on-screen size
(reading roughly half the intended weight), `drawLine()`'s flat/butt caps
where the approved prototype used rounded caps+joins everywhere, wrong
inset ratios (NAM chip drawn at a 16% box inset instead of the approved
~27%), and a pin-length formula off by roughly 2x. Each one was individually
minor; together they're exactly why the shipped icons read as noticeably
worse than the HTML prototype the user approved, despite the shapes being
"the same" in a code-review sense. There is no audio-thread concern here —
`drawIcon()` only ever runs from `Component::paint()` on the UI thread, so
"keep it simple to avoid bitmap loading" was never a real constraint, just
an overcautious assumption.

**To add a new icon:**
1. Write an SVG matching the reference sheet: `viewBox="0 0 48 48"`,
   `stroke="#FFFFFF" stroke-width="3" stroke-linecap="round"
   stroke-linejoin="round" fill="none"` on the root (override `fill`/
   `stroke="none"` per-shape only for genuinely filled elements, e.g. a
   speaker-grille dot or a filled note head), save it under `Assets/Icons/`.
2. Validate it parses (`python3 -c "import xml.etree.ElementTree as ET;
   ET.parse('Assets/Icons/yourfile.svg')"`) before wiring it in.
3. Add the filename to `juce_add_binary_data(OpenGuitarMultiFx_Icons ...)`'s
   `SOURCES` in `CMakeLists.txt`, and to `Tests/CMakeLists.txt`'s
   `OpenGuitarMultiFx_Icons` link if the processor's `.cpp` is also compiled
   into `OpenGuitarMultiFx_Tests` (it always is, per the per-processor unit
   test convention).
4. In the processor's `drawIcon()`: `#include "IconKit.h"` and
   `#include <IconData.h>`, then
   `static const std::unique_ptr<juce::Drawable> svg =
   icon::loadSvg (IconData::yourfile_svg, IconData::yourfile_svgSize);
   icon::drawSvg (g, b, svg.get());` — see `Source/Effects/IconKit.h` and
   any existing `drawIcon()` for the exact pattern. `IconData`'s symbol
   names come from the filename with `.` replaced by `_` (JUCE's
   `juce_add_binary_data` convention) — a hyphen or space in the filename
   would need checking against the generated `IconData.h` directly.

The glyph still communicates the effect TYPE, never the category colour or
the block's on/off state — `EffectBlockComponent::paint()` already handles
colour/border/bypass around it (see AGENT.md's UI/UX Design Philosophy).

Don't assume two roles of the same processor class share OR don't share a
glyph — check the sheet each time. This flip-flopped twice already: a
guessed-from-memory pass claimed Neura Amp and Neura Pedal use different
glyphs (amp-head-with-knobs vs. stompbox), then a corrected pass drew both
as the same bare chip, and a direct reading of the real sheet confirmed it
genuinely does reuse one glyph across several related effects on purpose
(Neura Amp = Neura Amp+Cab's chip = Neura Pedal; Compressor = Expander =
IR Loader's pulse trace; Sustainer = Looper's infinity symbol). Match the
sheet's own reuse, don't assume either way.

## Categories and colours (as seen on the reference sheet)

| Category (PT label) | Colour | Effects on the sheet |
|---|---|---|
| Amplificadores | orange/red | Amp, Amp+Cab, Cab, Neura Amp, Neura Amp+Cab, Neura Pedal |
| Dinamica | red | Compressor, Limiter, Noise Gate, Expander, Sustainer, Auto Swell |
| Drive | yellow/orange | Overdrive, Distortion, Fuzz, Boost, EQ Drive |
| Modulacao | purple | Chorus, Flanger, Phaser, Tremolo, Vibrato, Rotary, Uni-Vibe, Pitch Mod |
| Delay | blue | Digital, Analog, Tape, Reverse, Dual, Ping Pong, Multi Tap, Looper, Hold |
| Reverb | cyan | Hall, Plate, Room, Spring, Shimmer, Mod Reverb, Cloud, Ambient, Gated, Reverse |
| Filtro/FX | green | Wah, Auto Wah, Filter, Envelope, Octaver, Pitch Shift, Harmonizer, Ring Mod, Synth, Slicer, Bitcrusher, Volume, Bitfession, Expression, Sequencer |
| Utilitarios | grey | Tuner, IR Loader, MIDI, Send/Return, Splitter, Merger, A/B Switch, Buffer, Utility |

Note: `EffectProcessor::getAccentColour()` per class currently picks its own
specific hue rather than these exact category colours — worth reconciling
once more effect classes exist and the palette can be centralised (e.g. a
`GearRouting`-style category→colour table), rather than guessing a full
palette now for effects that don't exist yet.

## Glyphs implemented so far (mapped from the sheet, SVG assets since 2026-09-11)

| Processor (chain role) | Category | SVG asset | File |
|---|---|---|---|
| GateProcessor | Dinamica → Noise Gate | `Assets/Icons/gate.svg` | `Source/Effects/GateProcessor.cpp` |
| CompressorProcessor | Dinamica → Compressor | `Assets/Icons/pulse.svg` (heartbeat/ECG trace) | `Source/Effects/CompressorProcessor.cpp` |
| OverdriveProcessor | Drive → Overdrive | `Assets/Icons/overdrive.svg` (soft-clipped waveform, ~1.5 cycles) | `Source/Effects/OverdriveProcessor.cpp` |
| NAMProcessor ("Neural Amp" / "Neural Pedal") | Amplificadores → Neura Amp / Neura Pedal | `Assets/Icons/neura_chip.svg` (same glyph for both roles) | `Source/Effects/NAMProcessor.cpp` |
| NAMProcessor ("Neural Amp + Cab") | Amplificadores → Neura Amp + Cab | `Assets/Icons/neura_chip_cab.svg` (chip over a cab box) | `Source/Effects/NAMProcessor.cpp` |
| IRLoaderProcessor ("Cab") | Amplificadores → Cab | `Assets/Icons/cab.svg` (box + 2x2 outline circles) | `Source/Effects/IRLoaderProcessor.cpp` |
| IRLoaderProcessor ("Reverb") | Utilitarios → IR Loader (generic loaded-space convolution, not a named reverb type) | `Assets/Icons/cab.svg` (reused from the Cab role — was `hall.svg` as a stand-in until a real Hall block existed; freed up 2026-09-12 for HallReverbProcessor below, since the sheet's own dedicated grey "IR Loader" utility glyph still isn't confirmed) | `Source/Effects/IRLoaderProcessor.cpp` |
| DelayProcessor | Delay → Digital Delay | `Assets/Icons/digital_delay.svg` (3 filled dots decreasing in size) | `Source/Effects/DelayProcessor.cpp` |
| TapeDelayProcessor | Delay → Tape Delay | `Assets/Icons/tape_delay.svg` (box + 2 reels + baseline) | `Source/Effects/TapeDelayProcessor.cpp` |
| ReverbProcessor ("Ambient") | Reverb → Ambient | `Assets/Icons/ambient.svg` (3 concentric circles) | `Source/Effects/ReverbProcessor.cpp` |
| SpringReverbProcessor | Reverb → Spring | `Assets/Icons/spring.svg` (row of overlapping loops) | `Source/Effects/SpringReverbProcessor.cpp` |
| PingPongDelayProcessor | Delay → Ping Pong | `Assets/Icons/ping_pong.svg` (two rails + a bouncing path) | `Source/Effects/PingPongDelayProcessor.cpp` |
| ReverseDelayProcessor | Delay → Reverse Delay | `Assets/Icons/reverse_delay.svg` (rewind triangle + bar) | `Source/Effects/ReverseDelayProcessor.cpp` |
| HoldProcessor | Delay → Hold | `Assets/Icons/hold.svg` (two bare pause bars) | `Source/Effects/HoldProcessor.cpp` |
| HallReverbProcessor | Reverb → Hall | `Assets/Icons/hall.svg` (tall pointed arch) — now this class's alone, see the IRLoaderProcessor row above | `Source/Effects/HallReverbProcessor.cpp` |
| PlateReverbProcessor | Reverb → Plate | `Assets/Icons/plate.svg` (flat plate + sound arcing off its surface) | `Source/Effects/PlateReverbProcessor.cpp` |
| RoomReverbProcessor | Reverb → Room | `Assets/Icons/room.svg` (nested boxes tightening toward the centre) | `Source/Effects/RoomReverbProcessor.cpp` |
| ShimmerReverbProcessor | Reverb → Shimmer | `Assets/Icons/shimmer.svg` (wash + sparkle above it) | `Source/Effects/ShimmerReverbProcessor.cpp` |
| GatedReverbProcessor | Reverb → Gated | `Assets/Icons/gated.svg` (decaying wave cut off by a hard bar) | `Source/Effects/GatedReverbProcessor.cpp` |
| AnalogDelayProcessor | Delay → Analog Delay | `Assets/Icons/analog_delay.svg` (decaying rounded humps) | `Source/Effects/AnalogDelayProcessor.cpp` |
| DualDelayProcessor | Delay → Dual Delay | `Assets/Icons/dual_delay.svg` (two offset dot-rows) | `Source/Effects/DualDelayProcessor.cpp` |
| MultiTapDelayProcessor | Delay → Multi Tap | `Assets/Icons/multi_tap.svg` (one line, several branching taps) | `Source/Effects/MultiTapDelayProcessor.cpp` |
| TremoloProcessor | Modulacao → Tremolo | `Assets/Icons/tremolo.svg` (wave with shrinking/growing humps) | `Source/Effects/TremoloProcessor.cpp` |
| ChorusProcessor | Modulacao → Chorus | `Assets/Icons/chorus.svg` (two near-identical offset waves) | `Source/Effects/ChorusProcessor.cpp` |
| VibratoProcessor | Modulacao → Vibrato | `Assets/Icons/vibrato.svg` (one wave, wobbling period) | `Source/Effects/VibratoProcessor.cpp` |
| FlangerProcessor | Modulacao → Flanger | `Assets/Icons/flanger.svg` (same wave twice, dotted one trailing behind -- redrawn twice: first as comb teeth, then a diagonal sweep arrow, before landing on this delayed-copy pairing per direct user description) | `Source/Effects/FlangerProcessor.cpp` |
| PhaserProcessor | Modulacao → Phaser | `Assets/Icons/phaser.svg` (solid wave + dotted vertically-inverted mirror -- redrawn from an earlier spiral/wave-through-rings design) | `Source/Effects/PhaserProcessor.cpp` |
| RotaryProcessor | Modulacao → Rotary | `Assets/Icons/rotary.svg` (single spin arrow around a centre point -- redrawn from an earlier 3-blade rotor) | `Source/Effects/RotaryProcessor.cpp` |
| UniVibeProcessor | Modulacao → Uni-Vibe | `Assets/Icons/uni_vibe.svg` (plain circle, glowing centre -- redrawn from an earlier organic blob per direct user request) | `Source/Effects/UniVibeProcessor.cpp` |
| PitchModProcessor | Modulacao → Pitch Mod | `Assets/Icons/pitch_mod.svg` (wave with arrows pointing both up and down) | `Source/Effects/PitchModProcessor.cpp` |
| PitchShiftProcessor | Filtro/FX → Pitch Shift | `Assets/Icons/pitch_shift.svg` (one note moving to a new position via a single arrow) | `Source/Effects/PitchShiftProcessor.cpp` |
| OctaverProcessor | Filtro/FX → Octaver | `Assets/Icons/octaver.svg` (same wave twice, dotted one at double wavelength) | `Source/Effects/OctaverProcessor.cpp` |
| HarmonizerProcessor | Filtro/FX → Harmonizer | `Assets/Icons/harmonizer.svg` (two notes on one stem, sounding together) | `Source/Effects/HarmonizerProcessor.cpp` |
| LooperProcessor | Delay → Looper | `Assets/Icons/looper.svg` (a near-complete circular repeat arrow, no centre dot -- distinct from Rotary's partial arc + dot) | `Source/Effects/LooperProcessor.cpp` |
| PositiveGroundBoosterProcessor ("Rangemaster-Style Booster") | Drive → Boost | `Assets/Icons/boost.svg` (plain up-arrow) | `Source/Effects/PositiveGroundBoosterProcessor.cpp` |
| DS1StyleDistortionProcessor ("DS-1-Style Distortion") | Drive → Distortion | `Assets/Icons/overdrive.svg` reused as a placeholder -- the sheet lists "Distortion" as its own distinct glyph, but it still isn't saved in the repo (see the note below), so this reuses the closest same-category glyph rather than inventing one. Replace with the real "Distortion" glyph once the reference sheet is actually available on disk | `Source/Effects/DS1StyleDistortionProcessor.cpp` |

**Reference sheet STILL not actually saved to the repo (checked 2026-09-14):** confirmed via git history that no image file has EVER been committed here, despite the note above asking for exactly that since this doc's first version -- the sheet has only ever existed as an inline chat paste, in at least three separate sessions now (2026-09-10, 2026-09-11, 2026-09-14), and this agent has no tool that can extract raw bytes from an inline-pasted image (confirmed again this session: searched the whole filesystem, `~/Downloads`, `~/Pictures`, `~/Desktop`, this session's own scratch dir -- nothing). The Boost glyph above was built by looking at the 2026-09-14 paste directly, same as every other glyph before it, but the underlying file is still gone the moment that context clears. **If you're reading this planning a new icon: don't assume `docs/icons/reference-sheet.png` exists -- it doesn't, ask the user to re-paste it, and if they've already saved a copy somewhere on disk (not just pasted into chat), ask for that file's path so it can actually be copied into the repo instead of re-lost again.** | `Source/Effects/AGENTS.md`'s decision log has the same finding.

**Phase 2 note (2026-09-11):** `DelayProcessor`/`TapeDelayProcessor`/
`ReverbProcessor`/`SpringReverbProcessor` don't cover every Delay/Reverb
variant from the sheet at once, same approach as Phase 1's
Gate/Compressor/Overdrive -- each new one adds real DSP variety (Tape
Delay's wow/flutter + feedback saturation is audibly different from
Digital Delay's clean line; Spring's allpass-dispersion-into-a-damped-comb
is a genuinely different algorithm from Ambient's smooth `juce::dsp::Reverb`
wash, not the same reverb with different presets) rather than being added
just to check a box on the sheet.
`ReverbProcessor::getName()` is
`"Ambient"`, not `"Reverb"` — `IRLoaderProcessor`'s existing `"Reverb"`
role (convolution with a real captured IR) already owns that display
name, and `EffectRegistry` needs every registered type's `getName()` to be
unique (presets look processors back up by display name). If a future
effect's natural name collides with an existing one, rename it to
whichever sheet glyph actually fits (like "Ambient" did here) rather than
letting the collision silently overwrite the registry's reverse lookup.

A full 68-glyph SVG prototype (all 8 categories, including Bitfession and
Expression which have no `EffectProcessor` yet) was built and approved by
the user 2026-09-11 before this pass — every future effect class (Delay,
Modulation, most of Filtro/FX, Utilitarios — Phases 2–5) should pull its
glyph from that same approved design rather than inventing a new visual
language. That prototype lived in the session's scratch directory, not the
repo, so re-derive from the reference sheet + the category table above
rather than assuming a file exists — re-confirm with the user before
implementing rather than guessing which prototype shape was final.
