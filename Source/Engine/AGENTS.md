# Engine

## Purpose
Owns: the realtime boundary itself — `AudioEngine` (device I/O callback), `SignalGraph` (ordered chain execution), `ParameterManager` (control→audio parameter delivery), `DeferredReclaimer` (the atomic-swap primitive everything else in the project builds on), `PitchDetector` (YIN pitch estimation for the tuner, audio→control telemetry).
Does not own: what the chain contains (see `Source/Effects`) or who builds/owns processor instances (see `Source/UI/MainComponent`) — `SignalGraph` only holds non-owning pointers.

## Code Map

### Find It Fast
| Looking for... | Go to |
|----------------|-------|
| Audio device callback, block loop | `AudioEngine.cpp::audioDeviceIOCallbackWithContext` |
| Input/output channel selection | `AudioEngine.h` — `setInputChannel`/`setOutputChannelPair`, both plain atomics, no graph rebuild |
| The atomic-swap pattern used everywhere (graphs, NAM models, IRs) | `DeferredReclaimer.h` |
| Control→audio parameter updates | `ParameterManager.h` (SPSC lock-free queue) |
| Chain ordering/execution, real split/merge (parallel lanes summed) | `SignalGraph.h` — 4 lanes matching `MainComponent::numRows`, lanes always run 0→3 (see its class doc for why that's always a valid order, no topo-sort needed) |
| Pitch detection (tuner) | `PitchDetector.h/.cpp` — YIN algorithm, fed raw input in `AudioEngine`'s callback |
| Level metering (IN/OUT footer meters) | `AudioEngine.cpp::audioDeviceIOCallbackWithContext` — peak-per-block via `FloatVectorOperations::findMinAndMax`, published as `std::atomic<float>` |

### Key Relationships
- `AudioEngine` owns one `DeferredReclaimer<SignalGraph>`; `SignalGraph` owns non-owning `EffectProcessor*` pointers into whatever the UI's `chain` (`std::vector<std::unique_ptr<EffectProcessor>>`) keeps alive.
- `Effects/` → `Engine/`: effect processors depend on `DeferredReclaimer.h` for their own internal model/IR swaps (NAM models, IRs) — same primitive, different payload.
- Never the reverse: `Engine/` must not depend on concrete `Effects/` types.

## Public API
| Export | Used By | Change Impact |
|--------|---------|---------------|
| `AudioEngine::setSignalGraph()` | `MainComponent::rebuildSignalGraph()` | The only way to change what's playing; control thread only |
| `AudioEngine::getAvailable{Input,Output}...Names()` | UI I/O selectors | Must stay live-queried, never cached (see Contracts) |
| `DeferredReclaimer<T>` | `Effects/NAMProcessor`, `Effects/IRLoaderProcessor`, `AudioEngine` | Template — any realtime-boundary swap should reuse this, not reinvent it |
| `ParameterManager::push()`/`drain()` | Not yet wired into a processor (infra exists since Phase 0, consumers are Phase 2+) | |
| `AudioEngine::getInputLevel()`/`getOutputLevel()`/`getDetectedFrequencyHz()` | `MainComponent::timerCallback()` → `FooterBar::setLevels()`/`setTuning()` | Plain atomics, same pattern as `getCurrentCpuUsage()` — see Decisions below for why not the SPSC queue |

## Entry Points
| Task | Start Here |
|------|------------|
| Add a new realtime-swappable resource | `DeferredReclaimer.h`, see Patterns below |
| Change audio device I/O / channel routing | `AudioEngine.{h,cpp}` |
| Wire a new control→audio parameter path | `ParameterManager.h` |

## Decisions
| Decision | Why | Rejected |
|----------|-----|----------|
| `DeferredReclaimer<T>` (atomic pointer swap + deferred delete) over `std::atomic<std::shared_ptr<T>>` | Common stdlib impls of the latter aren't actually lock-free (fall back to internal mutex) | `shared_ptr`-based swap |
| Old object freed later by a control-thread sweep (~500ms margin), never inline in `exchange()` | Audio thread may still be mid-`process()` holding the old pointer; deleting there is use-after-free | Deleting synchronously in `publish()` |
| `SignalGraph` doesn't own processors | Lets the UI add/remove one block without resetting every other block's params/loaded model | `SignalGraph` owning `unique_ptr<EffectProcessor>` |
| Output routing = real hardware channel pairs ("Out 1/2", "Out 3/4"), queried live from the device | An abstract stereo/L/R concept doesn't scale past 2 outputs (this project targets a 6-channel interface) | Fixed stereo/mono/left/right enum |
| Level meters and detected pitch bridged via plain `std::atomic<float>` (one per value), not `ParameterManager`'s SPSC queue | Each is a single continuously-overwritten "freshest value" (like `lastCpuUsage`, already precedent), not a stream of discrete events the queue's `Change{id,value}` shape is built for; reusing the queue here would mean draining a backlog of stale readings instead of just reading the latest one | A new `ParameterManager` instance per telemetry value |
| `PitchDetector` uses YIN (cumulative mean normalized difference), not plain autocorrelation | Plain autocorrelation on a guitar's harmonically-rich signal readily locks onto an overtone instead of the fundamental (octave error) — YIN's normalized difference function is the standard fix | Raw autocorrelation peak-picking |
| YIN's full O(window × maxLag) analysis only runs once per ~1/15s of new audio, not every callback | A tuner doesn't need to update faster than ~15Hz, and re-running the full analysis on every (often 128-256 sample) callback would be needless CPU burn for no perceptible benefit | Running the analysis every audioDeviceIOCallbackWithContext() call |
| `SignalGraph` rewritten from one flat `vector<EffectProcessor*>` to 4 fixed lanes with explicit lane-to-lane connections, summed at merge points | The old model flattened every row's routing (including splits) into ONE serial chain -- an explicitly-labelled approximation (see the class doc comment it used to have) that made a split sound like its branches running in series, and had no way to express a merge (two rows feeding one) at all. `MainComponent`'s row UI already modelled splits/merges correctly in its own data (`RowRouting::toRows`); the engine just couldn't execute what the UI could already describe | Keeping the flatten-to-serial approximation and just documenting the gap further |
| Lanes always process in fixed index order 0→3, no topological sort | `MainComponent`'s row-output menu already only allows a row to feed a LATER row (the "forward-only" rule, see `Source/UI/AGENTS.md`) — every connection this engine will ever see already satisfies `source < target` by construction, so ascending index order is *always* a valid topological order. A generic topo-sort would be solving a problem that can't actually occur here | A generic graph topo-sort with cycle detection (dead code for a DAG that's already guaranteed acyclic upstream) |
| A lane's buffer is only written back into the device buffer if AT LEAST ONE lane has `writesDeviceOutput` set; otherwise `process()` leaves the incoming buffer completely untouched | Preserves the pre-existing "empty graph is a total passthrough" contract `AudioEngine` has always relied on (see its own `audioDeviceIOCallbackWithContext` comment) -- unconditionally clearing-then-rebuilding the buffer every call would silence the signal the moment nothing is configured yet, e.g. right after the app starts with an empty chain | Unconditionally overwriting the buffer from the lane mix (breaks passthrough on an empty/partially-configured graph) |

## Contracts
- The audio thread never allocates (`new`/`malloc`) outside `prepare()`, never takes a blocking mutex, never does I/O, never calls anything non-deterministic-latency (exceptions, dynamic RTTI, string allocation), never waits on the control thread. Violating this inside `AudioEngine::process()` or any `EffectProcessor::process()` is an architecture bug (see root AGENTS.md).
- `DeferredReclaimer::currentRaw()` is the *only* access point the audio thread is allowed to call — a single atomic load, no allocation.
- `getAvailableInputChannelNames()`/`getAvailableOutputPairNames()` must re-query the currently-open device every call, never return a cached/hardcoded list — PipeWire's generic ALSA passthrough device reported a placeholder 128/128 channel count that made I/O selectors look "infinite" until this was fixed live (commit `783588c`); actual routing was never affected.
- `sweep()` must be called periodically from the *control* thread only (a `juce::Timer` at 10–20Hz is the existing pattern) with a safety margin generously larger than one audio block.
- `PitchDetector::pushSamples()` never allocates -- every buffer (`ringBuffer`, `analysisBuffer`, `diffBuffer`, `cmndBuffer`) is sized once in `prepare()` (control thread, called from `audioDeviceAboutToStart()`) and only ever resized there, never from the audio thread.
- `SignalGraph::process()` never allocates -- every lane's `juce::AudioBuffer` and the shared `deviceInputSnapshot` are `setSize()`'d once in `prepare()` (control thread), and `addLaneConnection()`/`setLaneProcessors()` must be called before `prepare()`, never from `process()`'s call path.

## Patterns

### Adding a new realtime-boundary swap (new kind of hot-swappable state)
1. Wrap the payload type in `DeferredReclaimer<T>`.
2. Build/prepare the new `T` entirely on the control thread.
3. `publish()` it — audio thread picks it up via `currentRaw()` on its next block, no lock.
4. Add a `juce::Timer` (or reuse an existing one) calling `sweep()` at 10–20Hz from the control thread.

## Pitfalls
- `ParameterManager`'s queue silently *drops the newest change* when full instead of blocking or growing — this is intentional (stalling the audio thread is worse than losing one update), not a bug to "fix" by growing the buffer.
- `juce::AudioDeviceManager::setAudioDeviceSetup()` does **not** revert to the previously-open device when it fails — confirmed live (2026-09-13): `getCurrentAudioDevice()` came back `nullptr` after a failed switch, not the device that was open before the call, despite it being tempting to assume JUCE "just leaves it alone" on failure. `AudioEngine::start()`'s USB-interface-switch attempt used to assume the latter and silently left `AudioEngine` with no open device at all whenever the switch failed (which it reliably does on this dev machine — PipeWire holds the real interface open exclusively, see the UR44 setup notes) — every input-channel-dependent feature (I/O selectors, the tuner, the level meters) went quietly dark with no visible symptom beyond an easily-missed log line. Fixed by capturing the pre-switch `AudioDeviceSetup` and explicitly restoring it with a second `setAudioDeviceSetup()` call on failure, rather than trusting JUCE to have already done so.
- A `STATIC` (archive) library that registers types via static-initializer side effects (no directly-referenced symbol) gets those translation units silently dropped at link time by the linker's archive pruning. This bit `nam_core` (see `Effects/AGENTS.md`) — the general lesson lives here because it's a linking/build concern, not specific to NAM: if you add another self-registering factory anywhere in this project, make sure its target is `OBJECT`, not `STATIC`.
- `AudioEngine` actively tries to switch off the OS's generic/placeholder audio device at startup, in favor of a real interface — don't assume `deviceManager`'s initial state reflects the real hardware.

## Boundaries

### Never
- Add a blocking call (mutex, I/O, allocation, exception, dynamic RTTI) inside `AudioEngine::process()`'s call path.
- Delete a `DeferredReclaimer`-managed object anywhere but `sweep()`, from the control thread.
- Cache `getAvailableInputChannelNames()`/`getAvailableOutputPairNames()` results — always re-query.
