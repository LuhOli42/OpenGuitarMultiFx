# Effects

## Purpose
Owns: every stage of the signal chain — `EffectProcessor` (the base contract) and each concrete pedal/NAM/cab subclass (`GateProcessor`, `CompressorProcessor`, `OverdriveProcessor`, `NAMProcessor`, `IRLoaderProcessor`), plus `EffectRegistry` (factory + display-name mapping) at `Source/EffectRegistry.{h,cpp}`.
Does not own: chain ordering/execution (`Source/Engine/SignalGraph`), who owns processor instances or the UI around them (`Source/UI/MainComponent`), or realtime-swap machinery itself (`Source/Engine/DeferredReclaimer.h` — Effects only *uses* it).

## Code Map

### Find It Fast
| Looking for... | Go to |
|----------------|-------|
| The contract every processor implements | `EffectProcessor.h` |
| Factory (name → processor), display-name ↔ registry-key mapping | `Source/EffectRegistry.{h,cpp}` |
| NAM inference wrapper (3 roles: Amp/Amp+Cab/Pedal, same class) | `NAMProcessor.{h,cpp}` |
| Cab/Reverb convolution (2 roles, same class) | `IRLoaderProcessor.{h,cpp}` |
| Shared attack/release detector (used by Gate + Compressor) | `EnvelopeFollower.h` |
| Effect icon glyphs, category colours, which glyph each processor uses | `docs/icons/AGENT-icon-notes.md` (read before adding any new effect's icon) |

### Key Relationships
- `Effects/` → `Engine/DeferredReclaimer.h`: NAM models and IRs use the same atomic-swap pattern as `SignalGraph` itself.
- `SignalGraph`/`AudioEngine` never know concrete `Effects/` types — everything goes through `EffectRegistry`. Don't add special-casing for a specific effect type in `SignalGraph` (see root AGENTS.md).

## Public API
| Export | Used By | Change Impact |
|--------|---------|---------------|
| `EffectProcessor` (virtual contract) | Every concrete processor, `SignalGraph`, UI | Every subclass must implement the full contract — no partial exceptions (see root AGENTS.md code conventions) |
| `EffectRegistry::registerType/create/displayNameForKey/keyForDisplayName` | `MainComponent` (Add-effect menu, preset save/load) | `displayNameForKey`/`keyForDisplayName` are built once by *probing* a real instance's `getName()` at `registerType()` time — this is the single source of truth for the Add-effect menu AND preset serialization, so they can't drift apart. Don't hardcode display names elsewhere. |
| `EffectProcessor::wantsModelFile()`/`loadModelFile()` | Generic UI "Load model..." action | False/no-op default; true only for NAM/IR processors — lets UI code avoid `dynamic_cast` |

## Entry Points
| Task | Start Here |
|------|------------|
| Add a new effect processor | See "Adding a new effect processor" under Patterns below |
| Understand the base contract | `EffectProcessor.h` |
| Wire a processor into TONE3000 download routing | `../Tone3000/GearRouting.h` (see `Source/Tone3000/AGENTS.md`) |

## Decisions
| Decision | Why | Rejected |
|----------|-----|----------|
| One `NAMProcessor` class fills 3 chain roles (Neural Amp / Neural Amp + Cab / Neural Pedal) | Inference engine doesn't care which — only the loaded `.nam` file differs; split is purely so TONE3000 search stays scoped to one gear at a time | 3 separate processor classes |
| One `IRLoaderProcessor` class fills 2 roles (Cab / Reverb) | Same convolution operation underneath; only typical IR length and chain position differ | 2 separate processor classes |
| `nam_core` built as a CMake `OBJECT` library, never `STATIC` | NAM registers each architecture (LSTM, WaveNet...) into `get_dsp()`'s factory via static-initializer side effects with no directly-referenced symbol — a real `.a` archive silently dropped those translation units at link time (confirmed: `get_dsp()` threw "No config parser registered for architecture: LSTM") | `STATIC` library |
| Eigen pinned to an exact commit (not the `3.4.1` tag) | The tagged release is missing `Eigen::placeholders::lastN`, which `NAM/lstm.h` needs; pinned commit is the exact one NAM's own submodule vendors (verified via GitHub API) | The obvious tagged release |
| `NAMProcessor`/`IRLoaderProcessor` override `getState()`/`setState()` | Base class only serializes float params; presets need to restore *which* model/IR is loaded, not just gain (fixed in commit `921e0e9` — previously a preset couldn't actually restore the loaded sound) | Relying on the base class default |
| `drawIcon()` glyphs match the user's reference sheet exactly, re-checked per icon rather than assumed | Two earlier passes guessed shapes from memory/description and got them wrong in both directions (chip-vs-amp-head for NAM roles, distinct-vs-shared for the same 3 roles) before a 2026-09-11 pass read the actual image directly and got user approval on a full 68-glyph prototype first — see `docs/icons/AGENT-icon-notes.md` | Inventing a consistent-looking icon language without the reference |
| `drawIcon()` renders an embedded `Assets/Icons/*.svg` via `juce::Drawable` (see `IconKit.h`), not hand-written `juce::Path` calls | Even after the glyphs above were approved, hand-transcribing them into `Graphics`/`Path` calls introduced its own drift per icon — stroke width hardcoded in pixels instead of scaled to the icon's real on-screen size, `drawLine()`'s flat caps vs. the approved rounded ones, wrong inset/pin-length ratios on the NAM chip. Embedding the literal approved SVG removes that whole class of bug — same reasoning as embedding Sora instead of hoping a system font looks similar | Continuing to fine-tune hardcoded `Path` coordinates by eye |
| Phase 2 (`DelayProcessor`, `ReverbProcessor`) ships one representative processor per category, not all 9 Delay/10 Reverb sheet variants | Matches how Phase 1 shipped Gate/Compressor/Overdrive rather than every Dynamics/Drive variant on the sheet; building all 19 DSP algorithms at once with no real-world testing each would be exactly the "half-finished implementation" the project avoids | Building every named variant from the icon sheet immediately |
| `ReverbProcessor::getName()` returns `"Ambient"`, not `"Reverb"` | `IRLoaderProcessor`'s `"Reverb"` chain role (IR convolution) already owns that display name; `EffectRegistry`'s display-name↔key lookup (which presets rely on) needs every registered type's name to be unique, so the new algorithmic reverb takes the sheet's "Ambient" glyph/name instead | Naming it "Reverb" and silently overwriting the IR-based one's registry entry |
| CPU pass 2026-09-13, targeting genuine per-sample savings for the eventual ARM target, not a rewrite: (1) every ring-buffer wraparound of the form `(x + 1) % length` across every delay/comb/allpass/grain-shifter processor replaced with `if (++x >= length) x = 0;` (integer division is markedly more expensive than an almost-always-correctly-predicted branch on Cortex-A-class cores); (2) `MultiTapDelayProcessor`/`DualDelayProcessor` hoist their read-position/fraction math (channel-independent) out of the per-channel loop instead of recomputing it per channel; (3) `AnalogDelayProcessor`/`TapeDelayProcessor`'s feedback-saturation `std::tanh(driveConstant)` denominator (invariant for the process's whole life) computed once via a function-local `static const` instead of every sample; (4) `NAMProcessor`/`GateProcessor`/`CompressorProcessor`/`TremoloProcessor`/`PlateReverbProcessor`/`GatedReverbProcessor`/`HoldProcessor` cache `getWritePointer()`/`getReadPointer()` once per channel per block instead of going through bounds-checked `getSample()`/`setSample()` per element; `NAMProcessor`'s output-gain multiply also collapsed from once-per-channel to once, then `copyFrom()` per channel; (5) `EnvelopeFollower::setAttackTime()`/`setReleaseTime()` skip recomputing their `std::exp()` coefficient when the caller passes the same value as last time (Gate/CompressorProcessor call both unconditionally every block) | Explicit user request ("tenta optimizar... pra gastar bem pouco processador") ahead of the Phase 6 ARM port; survey (2 independent read-only passes) found these as the concrete, measurable per-sample waste, as opposed to already-correct patterns (parameter/coefficient hoisting was fine almost everywhere) | The bigger architectural move the same survey flagged — consolidating the ~10 duplicated fractional-delay-line/comb/grain-shifter struct definitions into one shared class — deferred to the user's separately-requested, not-yet-started "revisit all effects" pass, since it's a maintainability win more than a CPU one and touches every delay/reverb/pitch file at once; also deferred: fast `tan()`/`pow()` approximations for LFO-modulated coefficients (findings 6/7), which need audible/measured accuracy validation before swapping in, not a mechanical change like the rest of this pass |

## Contracts
- Every effect class implements the *full* `EffectProcessor` contract — no partial exceptions (see root AGENTS.md).
- `process()` never allocates, blocks, or does I/O — model/IR loading always happens in `prepare()`/`loadModel()`/`loadImpulseResponse()` on the control thread, then handed to the audio thread via `DeferredReclaimer`.
- `nam::DSP::process()` output saturates to exactly `1.0f` in float32 at extreme drive — a bound test must use `<=`, not `<` (real bug, commit `dc090fe`).
- `juce::AudioProcessorParameterGroup`'s constructor is variadic-template (each `unique_ptr<Parameter>` as a separate argument), not a vector-taking constructor.
- `format` is the load-bearing TONE3000 field, not `gear`: `"nam"` → `NAMProcessor` only, `"ir"` → `IRLoaderProcessor` only. Mixing these up is a routing bug (see `Source/Tone3000/AGENTS.md`), not a matter of taste.

## Patterns

### Adding a new effect processor
1. Subclass `EffectProcessor`, implement the full contract (`prepare`/`process`/`reset`/`getParameters`/`getName`; override `getState`/`setState` only if state is more than float params).
2. Register it in `EffectRegistry::registerBuiltInEffects` (`Source/EffectRegistry.cpp`) — this is the one place concrete types are known.
3. Add its glyph as an `Assets/Icons/*.svg` matching the shared reference sheet, embed it via `CMakeLists.txt`'s `OpenGuitarMultiFx_Icons` target, draw it through `IconKit.h` in `drawIcon()`, set `getAccentColour()`, and add a row to `docs/icons/AGENT-icon-notes.md` — don't invent an ad-hoc glyph or hand-write `juce::Path` calls (see `docs/icons/AGENT-icon-notes.md`'s "To add a new icon" and root AGENTS.md's UI/UX Design Philosophy).
4. If it needs a loaded file (model/IR), override `wantsModelFile()`→true and `loadModelFile()`, and add a route in `Source/Tone3000/GearRouting.h` if it should be reachable via TONE3000 search.

## Pitfalls
- `EffectBlockComponent::paint()` used to hand `drawIcon()` a *stretched* (non-square) rectangle because width/height were reduced by different factors on a non-square block — every icon lacking a defensive `jmin(width,height)` came out visibly squashed until fixed in commit `1dbdc21`. New `drawIcon()` overrides can rely on the caller now passing a proper centred square, but don't assume every historical caller does.
- "Neural Amp" and "Neural Amp + Cab" process *identically* (same `.nam` file → `nam::get_dsp()`) — they look like they should be different code paths but are the exact same one; the split exists only for TONE3000 search scoping.
- Renaming a chain-role display name (e.g. the "NAM Amp"/"Neural Drive" → "Neural Amp"/"Neural Pedal" rename in commit `8d4316a`) requires updating every place that pattern-matches on the old name in lockstep — `GearRouting.h` and `MainComponent` both string-match on `getName()`; these are not meant to drift independently.

## Boundaries

### Always
- Implement the full `EffectProcessor` contract on every new subclass.
- Route new NAM/IR file types through `GearRouting.h` if TONE3000-downloadable.

### Never
- Add effect-type-specific special cases to `SignalGraph` — it must stay ignorant of concrete types.
- Call `loadModel()`/`loadImpulseResponse()` from the audio thread — control thread only, both mutate in place.
