# OpenGuitarMultiFx

*codename: cheapCortex*

A digital guitar processor for live use — realtime audio engine in C++/JUCE, Neural Amp Modeling, and TONE3000 integration. Validated first on an x86 PC, then ported to dedicated hardware.

> Reference document. Web version with diagrams: `OpenGuitarMultiFx` (artifact published 2026-09-06).
> Rev. 0.2 — 2026-09-07

**Development strategy:** all of Phases 0–5 run on a **regular x86 Linux PC**, with native JUCE over ALSA/PipeWire. The final hardware (Radxa Cubie A7S + touchscreen) only enters at Phase 6, once the Audio Engine has already been validated without depending on it.

---

## 0. Summary and classification

| Component | Exists? | ARM / Linux | Realtime | License | Commercial use | Classification |
|---|---|---|---|---|---|---|
| **NeuralAmpModelerCore** (`sdatkinson/NeuralAmpModelerCore`) | Yes, active | Yes — proven on RPi4/5 | Yes, own engine (Eigen), no NPU dependency | MIT | Free | 🟢 EXISTS TODAY |
| **NeuralAudio** (`mikeoliphant/NeuralAudio`) | Yes, active | Yes — dedicated SIMD for RPi4 (128-bit) and RPi5 (256-bit) | Yes | MIT | Free | 🟢 EXISTS TODAY |
| **RTNeural** | Yes, active | Yes, indirect — in production via AIDA-X on the MOD Dwarf | Yes | BSD-3 (+ inherits the backend's license, Eigen/xsimd) | Free | 🟢 EXISTS TODAY |
| **AIDA-X** | Yes, no commits in ~1.8 years | Yes — real production (MOD Dwarf) | Yes | GPL-3.0 | Strong copyleft — do not link into closed-source product | 🟡 REFERENCE, NOT A DEPENDENCY |
| **GuitarML / NeuralPi** | Yes, abandoned (1.5–4 years without commits) | Yes — RPi4 + Elk Audio OS | Yes | GPL-3.0 | Copyleft + unmaintained | 🟠 CASE STUDY ONLY |
| **TONE3000 API** | Yes, official and documented (OAuth2+PKCE) | N/A — cloud service | N/A — outside the audio path | Own terms per tone (T3K / CC / CC0) | **Not confirmed** for commercial embedded hardware — requires prior contact | 🟡 VIABLE, CONTRACTUAL PENDING |
| **NPU (RKNN / eIQ / VIP9000) for NAM** | NPU exists; NAM acceleration, no | — | No evidence — missing causal/dilated Conv1D in the runtimes | — | — | 🔴 DO NOT USE |
| **Elk Audio OS** | Yes, active | Yes — official only on RPi4 | Yes, sub-ms (Sushi/RASPA/TWINE) | Mixed: AGPL-3.0 / GPL-3.0 / GPL-2.0 / MIT | Likely requires a paid commercial license for a closed product | 🟡 VIABLE, LICENSING DECISION |
| **Radxa Cubie A7S** (target hardware) | Yes, real board (launched Feb/2026) | ARM64 yes; kernel is vendor BSP, not mainline | PREEMPT_RT not confirmed on this SoC | Hardware | — | 🟠 BRING-UP BET |
| **Mainline PREEMPT_RT** (kernel ≥6.12) | Yes, since Sep/2024 | Yes — x86 / ARM64 / RISC-V | Yes, but requires per-SoC validation | GPL-2.0 | Free | 🟢 MATURE ON PC · NOT CONFIRMED ON THE A7S |

**Direct reading:** the inference engine (NAM Core / NeuralAudio / RTNeural) is a solved problem — MIT/BSD, active, already runs on ARM Cortex-A72/A76 without an NPU. What still needs a project decision: (1) the TONE3000 commercial agreement for a hardware product, (2) Elk Audio OS vs. building our own ALSA+JACK2 stack, (3) how much the Cubie A7S bring-up delays Phase 6. None of these block Phase 0–5 work.

---

## A. Complete architecture

Two execution lines with a single rule: **the audio thread never waits on the control thread.** Anything that touches disk, network, or UI only mutates its own copies, and hands off to the audio thread through an atomic pointer swap or a lock-free queue.

```
CONTROL / NETWORK THREAD               │  AUDIO THREAD (REALTIME)
                                        │
TOUCHSCREEN UI                         │  GUITAR
      │                                │      │
      ▼                                │      ▼
PRESET MANAGER ─────preloaded graph────┼──►  INPUT (ADC)
      │                                │      │
      ▼                                │      ▼
TONE3000 MANAGER                       │  AUDIO ENGINE (block loop, sample-accurate)
      │                                │      │
      ▼                                │      ▼
MODEL REPOSITORY ────atomic swap───────┼──►  SIGNAL GRAPH
 (download, validation, cache)         │  (Gate→Comp→Drive→NAM→Cab→EQ→Delay→Reverb,
                                        │   order fully reconfigurable)
      ▲                                │      │
      └──── CPU/clipping/meters ───────┼──── lock-free queue (SPSC)
            (lock-free queue)          │      ▼
                                        │  OUTPUT (DAC)
                                        │      │
                                        │      ▼
                                        │  AMPLIFIER / PA
```

The bridge between the two sides is always one of three primitives: **atomic pointer swap** (models/graphs), **lock-free SPSC queue** (parameters and telemetry), or **double buffering** (whole presets). No other form of communication crosses this line.

### Split / merge

The Signal Graph is drawn as a single box because the internal order is free, but the same engine supports branching:

```
Serial:                           Split / Merge:
INPUT                              INPUT
  │                                  │
  ▼                               split
GATE + COMPRESSOR                  ├──────────────┐
  │                                 ▼              ▼
  ▼                              AMP A          AMP B
DRIVE                               │              │
  │                                 ▼              ▼
  ▼                              CAB A          CAB B
NAM AMP                             │              │
  │                                 └──────┬───────┘
  ▼                                        ▼
CAB / IR                                 merge
  │                                        │
  ▼                                        ▼
EQ + DELAY + REVERB                    OUTPUT
  │
  ▼
OUTPUT
```

---

## B. Hardware

### Phase 0–5 · Dev bench
- Platform: x86-64 PC, Linux (any distro with ALSA/PipeWire)
- Audio: class-2 USB interface (e.g. Focusrite Scarlett Solo) for real guitar input
- Toolchain: native JUCE + CMake — the same source code as the ARM target
- Marginal cost: ≈ $0 (existing machine) + audio interface if needed

### Phase 6+ · Target hardware
- SBC: **Radxa Cubie A7S** — Allwinner A733 (2×A76 + 6×A55), 3-TOPS NPU **not usable for NAM**
- Audio: **no onboard codec confirmed** — requires an external I2S HAT/board with instrument-level input
- Display: USB-C with DisplayPort Alt-Mode (4Kp60) + GPIO header with an LCD function; touch via a USB-HID controller
- Kernel: vendor BSP (5.15 / 6.6) — mainline and PREEMPT_RT still in community bring-up

### Documented fallback
If audio/RT bring-up on the Cubie A7S doesn't mature in time for Phase 6, the path with real embedded-product evidence (NeuralPi, the NAM author's own blog) is **Raspberry Pi 4/5** — mature kernel, large community, A72/A76 CPU proven sufficient for multiple NAM models via RTNeural without an NPU. **Orange Pi 5 Plus (RK3588)** is the best-CPU-per-dollar alternative, at the cost of a still-incomplete mainline kernel in 2026. Neither choice needs to be made now — the software architecture (Sections C/D) does not reference specific hardware at any layer.

| Criterion | Radxa Cubie A7S | Raspberry Pi 5 | Orange Pi 5 Plus (RK3588) |
|---|---|---|---|
| CPU | 2×Cortex-A76@2.0GHz + 6×A55@1.8GHz | 4×Cortex-A76@2.4GHz | 4×Cortex-A76@2.4GHz + 4×A55 |
| RAM | LPDDR5, 4–16GB | LPDDR4X, up to 8GB | LPDDR4/4X, 4–32GB |
| Onboard audio codec | 🔴 not confirmed | 🟡 no, but HiFiBerry is a proven path | 🟡 no, community documents I2S |
| Kernel | 🟠 BSP, not mainline | 🟢 mature, mainline | 🟡 mainline in progress |
| PREEMPT_RT | 🔴 not confirmed | 🟡 works, spikes under stress | 🟡 mainline since 6.12, not validated |
| Realtime audio precedent | 🔴 none found | 🟢 NeuralPi, NAM blog, Zynthian | 🔴 none found |
| Approx. price (2026) | $25–44 | $110–175 (DRAM price surge) | $90–129 |

---

## C. Software

```
OpenGuitarMultiFx/
├── Source/
│   ├── Engine/
│   │   ├── AudioEngine.{h,cpp}          # block loop, device I/O, CPU budget
│   │   ├── SignalGraph.{h,cpp}          # serial/split/merge graph, runtime reordering
│   │   └── ParameterManager.{h,cpp}     # automation, smoothing, lock-free param queue
│   ├── Effects/
│   │   ├── EffectProcessor.h            # abstract base class for every processor
│   │   ├── GateProcessor.{h,cpp}
│   │   ├── CompressorProcessor.{h,cpp}
│   │   ├── DistortionProcessor.{h,cpp}
│   │   ├── FuzzProcessor.{h,cpp}
│   │   ├── EQProcessor.{h,cpp}
│   │   ├── NAMProcessor.{h,cpp}         # realtime-safe wrapper over NeuralAmpModelerCore (nam::DSP)
│   │   └── CabIRProcessor.{h,cpp}       # partitioned convolution
│   ├── Models/
│   │   ├── ModelRepository.{h,cpp}      # local library, metadata, checksum
│   │   ├── ModelValidator.{h,cpp}
│   │   └── ModelLoader.{h,cpp}          # loads off the audio thread, hands off via atomic swap
│   ├── Tone3000/
│   │   └── Tone3000Manager.{h,cpp}      # OAuth2+PKCE, search, download — optional/pluggable module
│   ├── Presets/
│   │   └── PresetManager.{h,cpp}        # JSON serialization, double-buffered swap
│   ├── UI/
│   │   └── (Phase 7 — separate thread, reads state via FIFO)
│   └── EffectRegistry.{h,cpp}           # factory: name → std::unique_ptr<EffectProcessor>
└── Tests/                               # isolated CPU/latency benchmarks per processor
```

### Base class contract

```cpp
// EffectProcessor.h — every stage in the chain implements this contract
class EffectProcessor {
public:
    virtual ~EffectProcessor() = default;
    virtual void prepare(double sampleRate, int maxBlockSize, int numChannels) = 0;
    virtual void process(juce::AudioBuffer<float>& buffer) = 0;   // never allocates, never blocks
    virtual void reset() = 0;

    void setBypassed(bool shouldBypass) noexcept { bypassed.store(shouldBypass); }
    bool isBypassed() const noexcept { return bypassed.load(); }

    virtual juce::AudioProcessorParameterGroup* getParameters() = 0;
    virtual std::unique_ptr<juce::XmlElement> getState() const = 0;
    virtual void setState(const juce::XmlElement&) = 0;

private:
    std::atomic<bool> bypassed { false };
};
```

---

## D. Audio Engine (realtime)

**The audio thread never:**
- calls `malloc`/`new`/`free` outside of `prepare()`
- takes a blocking mutex (only lock-free structures or `std::atomic` are allowed)
- performs network I/O, disk I/O, or file logging
- calls anything with non-deterministic latency (exceptions, dynamic RTTI, string allocation)
- waits on any response from the control thread

| Block size | @ 48 kHz | @ 96 kHz | Recommended use |
|---|---|---|---|
| 32 samples | 0.67 ms | 0.33 ms | final live target, after profiling |
| 64 samples | 1.33 ms | 0.67 ms | standard production target |
| **128 samples** | **2.67 ms** | 1.33 ms | **Phase 1 starting point, on PC** |
| 256 samples | 5.33 ms | 2.67 ms | debug / profiling only, never in production |

Round-trip latency target (input → output, including the driver): **< 10 ms** — generally accepted as the threshold before the response to a picked string starts to feel off to the player. Default sample rate: **48 kHz**; 96 kHz is reserved for once the CPU budget allows it without sacrificing the number of simultaneous NAM instances.

---

## E. Phase 1 in detail

**Pedals:** Noise Gate, Compressor, Boost, Overdrive, Distortion, Fuzz, EQ — each an independent `EffectProcessor` (see Section C), with no state shared between instances.

**NAM — inference engine:** implemented directly on **NeuralAmpModelerCore** (sdatkinson, upstream, MIT) — not NeuralAudio as originally scoped here, because NeuralAudio's CMake requires git submodules at fixed relative paths (not FetchContent-friendly) and its bundled test models are CC BY-NC-ND (unusable in a commercial test suite anyway). `NAMProcessor` wraps `nam::DSP`, with the model loaded on the control thread and handed to the audio thread through the same atomic-swap `DeferredReclaimer` pattern the SignalGraph itself uses. The same class serves both the amp position and a "neural drive" position (`NAMAmp`/`NeuralDrive` in `EffectRegistry`) — only the trained `.nam` file loaded into an instance differs. RTNeural remains an option for lightweight LSTM models later. None of these depend on an NPU — all processing is CPU-bound, with NEON where available.

**Cab / IR:** **partitioned convolution** strategy — long IRs (>100ms) are too expensive with direct convolution, and "pure" FFT convolution introduces block latency incompatible with live use. JUCE already exposes `dsp::Convolution` with uniform partitioning built in — a real, not just theoretical, starting point for Phase 1.

**TONE3000 — model pipeline:**

```
Tone3000Manager  →  ModelRepository  →  ModelValidator  →  ModelLoader  →  EffectRegistry
 (OAuth2+PKCE,        (local library      (checksum,         (parsed off        (atomic swap
  search, download)    + cache)            format)             the audio thread)  into the Signal Graph)
```

Authentication via OAuth 2.0 + PKCE (public `client_id` + server-side secret key), as officially documented by TONE3000. Relevant endpoints: `/tones`, `/models`, `/makes`, `/tags`, with a 100 req/min rate limit. **Pending item to resolve before committing product marketing/architecture to it:** the public terms don't explicitly distinguish a "software app" from an "embedded hardware device" — direct contact with `support@tone3000.com` is a prerequisite, not optional. That's why `Tone3000Manager` is designed as a pluggable, optional module: the product works in full without it (models loaded manually from a `.nam` file).

**Model Repository:**

```
/models
  /nam        # NeuralAmpModeler models (.nam / JSON)
  /aida-x     # reference only, not used at runtime by default (GPL)
  /ir         # cabinet impulse responses
/metadata     # id, source, author, checksum, size, date
/presets      # JSON: chain + order + parameters + model references
/cache        # temporary TONE3000 downloads, never read by the audio thread
```

**Presets:** each preset serializes the `EffectProcessor` chain (type + order), parameters, model/IR references (by checksum, not absolute path), and — where applicable — the TONE3000 tone ID to allow re-downloading if the local cache was cleared. Preset switching uses **double buffering**: the next graph is built and "warmed up" (buffers zeroed, filters in a steady state) as a whole separate copy, and only then swapped in via an atomic pointer — zero clicks, zero silence.

---

## F. Roadmap

| Phase | Deliverable | Hardware |
|---|---|---|
| 0 | Architecture + JUCE/CMake skeleton | PC x86 |
| 1 | Audio Engine + Signal Graph + basic pedals + NAM + Cab/IR + TONE3000 + Presets | PC x86 |
| 2 | Delay + Reverb (algorithmic and convolution) | PC x86 |
| 3 | Modulation (chorus, flanger, phaser, tremolo, vibrato, rotary, ring mod) | PC x86 |
| 4 | Pitch (shifter, octaver, harmonizer, detune) | PC x86 |
| 5 | Looper, tuner, noise reduction, advanced EQ/compression, advanced routing, MIDI, expression | PC x86 |
| 6 | Port to the Radxa Cubie A7S: kernel/RT bring-up, audio codec via I2S HAT, real benchmarks | Cubie A7S |
| 7 | Full touch UI (built on the lock-free queue already present since Phase 1) | Cubie A7S + touchscreen |
| 8 | Footswitches, physical MIDI, expression pedal | Cubie A7S |
| 9 | Custom hardware (PCB, chassis, power supply, EMI/EMC) | Custom PCB |
| 10 | Optimization and live-use validation (soak test, cyclictest, real shows) | Final product |

**Backlog, not tied to a phase** (agreed with the user 2026-09-20):

- **Lane-parallel `SignalGraph` (multi-core).** Lanes (rows) that do not depend on each other run on worker threads and join at the merge; a serial chain cannot be parallelised without a block of latency per stage (1-3 ms each, over the <10 ms budget), so only genuinely parallel lanes benefit. Needs fixed worker threads with a short spin/park wait inside the audio path, i.e. a change to the realtime boundary's allowed primitives -- **Ask First** per `CLAUDE.md`; design it and get approval before coding. Motivation: the netlist distortion pedals cost 2.5-25% of a core each.
- **Fender Bassman 5F6-A amplifier model: DONE 2026-09-21** (`docs/circuits/Bassman5F6A.md`, "Bassman-Style Amplifier"). Costs 14-24% of a core at 1x (3x the pedals' 5% target): the follow-ups are channel-variant preambs, a cheaper Newton for tube blocks, and the lane-parallel `SignalGraph` above.
- **More circuit-modelled pedals (requested by the user 2026-09-21; display names follow "<Original>-Style <Category>"):**
  1. *Op-amp + diode clippers*, which need a shared building block first -- an op-amp macro-model with **finite gain-bandwidth and slew-rate limiting** (the LM308 with its 30 pF compensation IS the RAT's sound; `addFiniteGainOpAmp` + pole + saturating follower from the BD-2 work covers gain/GBW/swing, slew needs a new element): ProCo RAT (original, RAT 2 and Turbo RAT), DOD Overdrive 250, MXR Distortion+, MI Audio Crunch Box, Marshall Guv'nor, Nobels ODR-1, Boss Metal Zone (MT-2; multi-stage + semi-parametric mid, the heavy one), Marshall Blues Breaker (schematic to find).
  2. *Other clipping topologies* (schematics to find and classify first): Fulltone OCD (as far as recalled it clips with MOSFETs -- a square-law device like the JFET model, to confirm), Hermida Zendrive.
  3. *Transistor stages*: Big Muff Pi and the Sovtek Big Muff (four BJT stages, two diode-clipped, passive tone stack; ~HM-2 cost, the versions differ in values/transistors), Colorsound Overdrive, EP Booster (the Echoplex preamp as a clean/colouring boost; a small transistor stage, closest to the existing Rangemaster-style booster).
  4. *Tube*: Chandler Tube Driver (a 12AX7 run at a starved plate voltage; the Koren model must be validated at ~20 V, unlike the amp's).
  **Done so far (2026-09-21, the user asked for batches of ~5 pedals):** MXR Distortion+, DOD 250, Marshall Guv'nor, Marshall Blues Breaker, ProCo RAT (original) -- all on `NodalCircuit::addOpAmpMacro` (with rail clamps against integrator windup; docs in `docs/circuits/`). **Still open:** the slew / input-pair-limited transconductor (RAT's LM308), RAT 2 / Turbo RAT (schematics in the collection), Nobels ODR-1 (full schematic found and partly traced: two 4558s, gyrator Spectrum control, JFET buffers), MI Audio Crunch Box (no readable schematic found yet), Metal Zone, OCD, Zendrive, Big Muffs (schematics in the collection), Colorsound, EP Booster, Chandler Tube Driver. Schematic source that worked: `github.com/yuvadm/guitar-effects-schematics` (raw files, readable by eye with `pdftoppm` for PDFs).
  Each one: find the schematic and cross-check against a second source, per-circuit doc in `docs/circuits/`, gain audit against published figures, equivalence checks against any reduced model (edge/spike/gate tests included), and a listen by the user before a reduced model becomes the default.
- **BD-2 / HM-2 below 5% of a core:** reduced-order models of their gain stages (see `docs/circuits/NodalCircuitSolver.md`, "Where the cycles go").

The UI is designed for touch from Phase 1 onward — not because the screen physically exists yet, but because the Audio Engine already exposes state through a lock-free queue from the very first commit. This avoids rewriting the realtime boundary once Phase 7 arrives.

---

## G. Benchmarks

No official source publishes a reliable CPU/latency number for NAM on ARM — the reports found are anecdotal forum posts. The values below are **project targets to validate empirically**, not literature citations.

| Scenario | CPU target | Xruns in 30 min |
|---|---|---|
| 1 · NAM alone | < 15% | 0 |
| 2 · NAM + IR (cab) | < 25% | 0 |
| 3 · NAM + 1 pedal | < 30% | 0 |
| 4 · Multiple pedals (gate+comp+drive+eq) | < 40% | 0 |
| 5 · Two amps in parallel (split/merge) | < 55% | 0 |
| 6 · Full chain | < 70% | 0 |

Metrics collected per scenario: CPU%, RAM, measured round-trip latency, xrun count, SoC temperature, preset-switch time (target: imperceptible, < 5 ms of silence/click). `cyclictest` runs alongside every soak test on the target hardware, since no source guarantees PREEMPT_RT latency numbers on that SoC.

---

## H. BOM (initial estimate)

Prices in USD, Sep/2026 — subject to the same DRAM volatility that already affected the Raspberry Pi 5 this year.

| Item | Phase | Approx. cost |
|---|---|---|
| Development PC | 0–5 | $0 (existing) |
| Class-2 USB audio interface | 0–5 | $100–200 |
| Radxa Cubie A7S (8GB) | 6 | $30–40 |
| Instrument-level I2S audio codec HAT/board | 6 | $15–35 *(not confirmed — needs electrical validation)* |
| Touchscreen panel + USB-HID controller | 7 | $25–60 |
| Footswitches, encoders, LEDs, enclosure, power supply | 8–9 | $50–150 *(placeholder, to be detailed in Phase 9)* |

---

## I. Risks

| Severity | Risk | Impact | Mitigation |
|---|---|---|---|
| 🔴 High | Cubie A7S has no confirmed onboard audio codec, vendor BSP kernel, RT not validated | Could delay all of Phase 6 | PC-first strategy already isolates this from Phases 0–5; RPi5/RK3588 documented as fallback (Section B) |
| 🔴 High | TONE3000 terms for commercial embedded hardware not publicly confirmed | Could block commercial distribution with the integration enabled | Direct contact with `support@tone3000.com` before committing marketing; `Tone3000Manager` designed as optional |
| 🟡 Medium | Target SoC's NPU is useless for NAM (no causal/dilated Conv1D in RKNN/eIQ/VIP9000) | None — this is a known design constraint, not a future surprise | Architecture already assumes CPU-only from Fig. 1 onward; don't budget NPU into any benchmark |
| 🟡 Medium | No guaranteed PREEMPT_RT latency number on any low-cost ARM SBC | Buffer size may need to increase in production, affecting playing feel | `cyclictest` + extensive soak testing on target hardware before locking in a final buffer size (Section G) |
| 🟡 Medium | Reference code (AIDA-X, GuitarML) is GPL-3.0 — license contamination if copied literally | Would force opening up the product's source if linked into a closed binary | Use only as architectural reference; own implementation on top of NAM Core/RTNeural (MIT/BSD) |
| 🟢 Low | DRAM price volatility (already visible in the Raspberry Pi 5 in 2026) | BOM may vary between the estimate and the actual purchase | Treat Section H as a range, not a locked quote; revisit before Phase 9 |

---

## Key sources

- github.com/sdatkinson/NeuralAmpModelerCore · github.com/mikeoliphant/NeuralAudio · github.com/jatinchowdhury18/RTNeural
- github.com/AidaDSP/AIDA-X · mod.audio/aida-x
- tone3000.com/api · github.com/tone-3000/api
- elk-audio.github.io/elk-docs (licensing and supported hardware)
- docs.radxa.com/en/cubie/a7s
- raspberrypi.com (BCM2711/BCM2712 product briefs) · wiki.friendlyelec.com (RK3588 datasheet)
- docs.kernel.org/core-api/real-time · wiki.mod.audio/wiki/Developers (the MOD Dwarf's ALSA+JACK2 stack)
- docs.pipewire.org · docs.yoctoproject.org

Compiled from directed research against primary sources. Where no reliable source was found, that is stated explicitly in the text — no number, license, or capability was invented.
