# AGENTS.md — guide for agents working in this repository

This file exists to give context to an AI agent (Claude Code or otherwise) opening this repository without having been part of the earlier decisions. The source of truth for the architecture is [`ARCHITECTURE.md`](./ARCHITECTURE.md) — this file is an operational summary + navigation layer, not a duplicate. If the two disagree, `ARCHITECTURE.md` wins, and this file should be corrected. (`AGENT.md`, singular, is a thin redirect kept only because in-code comments still reference it by that name — this file is the current one.)

## Intent Layer

> TL;DR: A digital guitar processor/pedalboard (C++/JUCE, Neural Amp Modeling, TONE3000 integration), validated on a PC before porting to embedded hardware. Start at Entry Points, check Downlinks for subsystem detail.

**Before modifying code in a subdirectory, read its `AGENTS.md` first** — each one carries contracts, pitfalls, and decisions mined from real bugs that aren't visible from reading the code cold.

### Downlinks

| Area | Node | Description |
|------|------|-------------|
| Realtime engine | `Source/Engine/AGENTS.md` | `AudioEngine`, `SignalGraph`, `ParameterManager`, the `DeferredReclaimer` atomic-swap primitive |
| Effects | `Source/Effects/AGENTS.md` | `EffectProcessor` base contract + every pedal/NAM/cab subclass, `EffectRegistry` |
| UI | `Source/UI/AGENTS.md` | Dev-facing chain builder (largest area, 27 files) — pulled forward from Phase 7 |
| TONE3000 | `Source/Tone3000/AGENTS.md` | OAuth2+PKCE login, tone search/download — optional, pluggable module |
| Presets | `Source/Presets/AGENTS.md` | Preset file I/O + stable numbering |

`Tests/` has no dedicated node (8 files, ~3.7k tokens, no responsibility shift beyond "test the thing above it") — see its one binding convention under Contracts below.

### What this project is

Hardware + software guitar pedalboard for live use, not a demo. **Two-legged strategy:** Phases 0–5 are written and validated on a regular x86 Linux PC (native JUCE over ALSA/PipeWire); only Phase 6 ports the same code to the target hardware (Radxa Cubie A7S, documented fallback to RPi5/Orange Pi5+). **No Phase 0–5 code should assume specific ARM hardware** — that belongs to Phase 6.

### Roadmap status

| Phase | Status |
|---|---|
| 0 — Architecture + JUCE/CMake skeleton | done |
| 1 — Audio Engine + Pedals + NAM + Cab/IR + TONE3000 + Presets | done |
| 2 — Delay + Reverb | done (all 14 non-Looper sheet variants shipped; `Looper` deferred to Phase 5's looper/tuner/MIDI work) |
| 3 — Modulation | done (all 8 sheet variants shipped: `TremoloProcessor`, `ChorusProcessor`, `VibratoProcessor`, `FlangerProcessor`, `PhaserProcessor`, `RotaryProcessor`, `UniVibeProcessor`, `PitchModProcessor`) |
| 4 — Pitch | done (`PitchShiftProcessor`, `OctaverProcessor`, `HarmonizerProcessor` shipped -- covers the sheet's Octaver/Pitch Shift/Harmonizer trio; "detune" from ARCHITECTURE.md's phase description is just PitchShiftProcessor/HarmonizerProcessor at a small semitone value, not a separate class) |
| 5 — Looper, tuner, MIDI, advanced routing | done (`LooperProcessor`; tuner + IN/OUT level meters via `PitchDetector` (YIN) + peak metering in `AudioEngine`, wired to `FooterBar`'s real gauge/bars; tap-tempo/BPM, self-contained in `FooterBar`; MIDI Program Change -> preset switching; MIDI Learn (CC-to-parameter mapping, doubles as expression pedal support); advanced routing -- `SignalGraph` rewritten from one flattened serial chain to real parallel lanes with split AND merge, summed rather than approximated, matching the split/merge diagram `ARCHITECTURE.md` always described, AND presets now actually save/restore the full per-row routing (a real gap found right after: they used to only persist row 0's plain device in/out, silently dropping every split/merge). MIDI Learn bindings still in-memory only, not yet saved into presets) |
| 6 — Port to Radxa Cubie A7S | not started |
| 7 — Full touch UI | not started |
| 8–10 — Footswitches, PCB, final validation | not started |

Update this table when a phase completes — don't let it silently go stale. Full detail, BOM, benchmarks, and risk table: `ARCHITECTURE.md` sections F–I. Don't re-derive risk analysis here — if you find a new risk during implementation, add it to `ARCHITECTURE.md` section I, not this file.

### Entry Points

| Task | Start Here |
|------|------------|
| Build / test / run | `scripts/build.sh` / `scripts/test.sh` / `scripts/run.sh` |
| Add a new effect (pedal/NAM/cab) | `Source/Effects/AGENTS.md` → "Adding a new effect processor" |
| Change chain UI/layout/reordering | `Source/UI/AGENTS.md` |
| Touch the realtime audio path | `Source/Engine/AGENTS.md` — read the Global Invariant below first |
| TONE3000 API integration | `Source/Tone3000/AGENTS.md` |

## Global Invariants

**The realtime boundary is not up for negotiation.** Two threads, one-directional communication through allowed primitives only:
- **Audio thread:** guitar → `AudioEngine` → `SignalGraph` → output. Fixed-size blocks, microsecond budget. Never allocates (`new`/`malloc`) outside `prepare()`, never takes a blocking mutex, never does network/disk/file I/O, never calls anything non-deterministic-latency (exceptions, dynamic RTTI, string allocation), never waits on the control thread.
- **Control/network thread:** UI, `PresetManager`, `Tone3000Manager`, model loading. Can take as long as it needs.
- Bridge is always one of three primitives — never anything else: **atomic pointer swap** (`Source/Engine/DeferredReclaimer.h` — models, graphs), **lock-free SPSC queue** (`ParameterManager` — params, telemetry), **double buffering** (whole presets).
- A blocking call inside `AudioEngine::process()` or any `EffectProcessor::process()` is an architecture bug, not an implementation detail — reject or fix it before moving on.

Every new effect is a new `EffectProcessor` subclass registered in `EffectRegistry` (`Source/EffectRegistry.{h,cpp}`) — `SignalGraph` never gets special-cased per effect type; see `Source/Effects/AGENTS.md`.

## Global Decisions (don't reopen without a new reason)

- **NAM engine:** `NeuralAmpModelerCore` (MIT), not `NeuralAudio` (CMake needs submodule-only paths, test models CC BY-NC-ND) and not AIDA-X/GuitarML (GPL-3.0 — license contamination in a closed-source product; reference only). Detail + the `nam_core` OBJECT-library and Eigen-pin pitfalls: `Source/Effects/AGENTS.md`.
- **NPU:** don't use for NAM — RKNN/eIQ/VIP9000 have no confirmed causal/dilated Conv1D support. All NAM processing is CPU-bound.
- **TONE3000:** optional module, commercial-embedded terms not yet confirmed (`ARCHITECTURE.md` section I) — never couple a core feature to it.
- **Phase 1 default:** 48kHz, 128-sample block (~2.7ms); round-trip target < 10ms.

## UI/UX Design Philosophy (summary — full detail in `Source/UI/AGENTS.md`)

Final target is a ~10" touchscreen operated with a fingertip — this governs sizing from Phase 1 onward, not just the eventual touch UI (Phase 7). `touch::minTapTarget` (48px, `Source/UI/TouchSizing.h`) is a hard floor on every interactive element. This is a standalone touchscreen pedalboard, not a desktop app: no separate OS windows, no native file-open dialogs as a primary flow, settings live in exactly one place. Typeface is Sora, embedded as binary data (no system font lookup — the final build has no font installed at all).

## Code Conventions

- C++/JUCE, CMake, C++20.
- No comments explaining the obvious — only the non-obvious reason (why a lock-free queue has that size, why an unsafe cast is safe here).
- Every effect class implements the *full* `EffectProcessor` contract — no partial exceptions.
- Isolated per-processor benchmark/unit tests live in `Tests/`, not mixed into functional code (`juce::UnitTest`, run via `Tests/RunTests.cpp`).

## Global Pitfalls

- This dev machine is a 16-core/~7.5GB-RAM box on an immutable distro (Bazzite/Fedora Atomic) — an unbounded `cmake --build --parallel` once hard-crashed it building the NAM engine. `scripts/build.sh` caps jobs at 2 by default (`BUILD_JOBS` env var to override); don't drop that cap casually.
- JUCE dev headers (ALSA, X11, FreeType, curl, webkit2gtk) come from a `distrobox` container (`juce-dev`) on this immutable host, not the host package manager — `scripts/build.sh` auto-detects and uses it if present.
- `nam_core` requires `OBJECT` (never `STATIC`) library linkage and an exact pinned Eigen commit (not the `3.4.1` tag) — see `Source/Effects/AGENTS.md` for why; both are easy to "clean up" incorrectly by someone who didn't hit the original bugs.

## Boundaries

### Never
- Add a blocking/allocating/I/O call to the audio thread's call path.
- Couple a core (non-optional) feature to TONE3000 availability.
- Special-case a concrete effect type inside `SignalGraph`.

### Ask First
- Changing the realtime boundary's three allowed primitives (atomic swap / SPSC queue / double buffer).
- Adding a second top-level settings surface or a native OS dialog window (see `Source/UI/AGENTS.md`).

<!-- code-review-graph MCP tools -->
## MCP Tools: code-review-graph

**This project has a knowledge graph. Start with the code-review-graph
MCP tools to narrow scope, then read the source.** The graph is cheaper than scanning files and
gives you structural context (callers, dependents, test coverage) that file search cannot.

### When to use graph tools FIRST

- **Exploring code**: `semantic_search_nodes_tool` or `query_graph_tool` instead of Grep
- **Understanding impact**: `get_impact_radius_tool` instead of manually tracing imports
- **Code review**: `detect_changes_tool` + `get_review_context_tool` instead of reading entire files
- **Finding relationships**: `query_graph_tool` with callers_of/callees_of/imports_of/tests_for
- **Architecture questions**: `get_architecture_overview_tool` + `list_communities_tool`

### Verify in the source

- Narrow scope with the graph, then read the source. Do not change code from graph output alone.
- For any non-trivial change, read the implementation and the relevant tests before concluding.
- Verify the exact source when touching behavior, database logic, migrations, retries, fallbacks,
  recovery, or compatibility code.
- When the graph and the source disagree, the source wins. The graph may be stale or may not
  model that relationship.
- An empty graph result can mean "not indexed" or "not statically visible", not "does not exist".

### Key Tools

| Tool | Use when |
| ------ | ---------- |
| `detect_changes_tool` | Reviewing code changes — gives risk-scored analysis |
| `get_review_context_tool` | Need source snippets for review — token-efficient |
| `get_impact_radius_tool` | Understanding blast radius of a change |
| `get_affected_flows_tool` | Finding which execution paths are impacted |
| `query_graph_tool` | Tracing callers, callees, imports, tests, dependencies |
| `semantic_search_nodes_tool` | Finding functions/classes by name or keyword |
| `get_architecture_overview_tool` | Understanding high-level codebase structure |
| `refactor_tool` | Planning renames, finding dead code |

### Workflow

1. The graph auto-updates on file changes (via hooks).
2. Use `detect_changes_tool` for code review.
3. Use `get_affected_flows_tool` to understand impact.
4. Use `query_graph_tool` pattern="tests_for" to check coverage.
<!-- /code-review-graph MCP tools -->
