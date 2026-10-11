# Test plan — row-parallel SignalGraph worker lifecycle (PR: devin/1791702470-row-parallel-signalgraph)

## What changed
`SignalGraph::prepare()` now spawns a pool of `LaneWorker` threads (`reconcileWorkers`, SignalGraph.cpp:284)
whenever a dependency-level "stage" contains 2+ processor-bearing lanes. `stopWorkers` runs in
`~SignalGraph` (SignalGraph.cpp:38-41), which fires inside the DeferredReclaimer sweep on the
message-thread timer. Every chain edit / routing change / preset load calls
`MainComponent::rebuildSignalGraph()` → `AudioEngine::setSignalGraph` → `newGraph->prepare()`
(spawn) and retires the old graph (stop ~500 ms later). Risk: hang/crash/deadlock from repeated
thread spawn/stop on the message thread, esp. `stopThread(10000)` per worker inside a timer callback.

## Environment trick (setup, already done)
Box has NO audio device (`/dev/snd` absent). Without one, `AudioEngine::start()` fails →
`sampleRate` stays 0 → `setSignalGraph` skips `prepare()` → workers NEVER spawn and the changed
code would be untested. Fixed by writing `~/.asoundrc` with `pcm.!default { type null }` — ALSA's
userspace null PCM opens cleanly (verified: `snd_pcm_open("default")` rc=0 both directions), so
JUCE opens "default", the audio callback actually runs, and the whole dispatch path
(runStage/collectJobs/LaneWorker::run) executes for real.

Worker presence is shell-verifiable while the app runs:
`for t in /proc/<PID>/task/*/comm; do cat $t; done | grep -c SignalGraphLane`
(expected: 0 for serial/empty topologies, N-1 workers for an N-lane dispatchable stage).

A crafted preset `~/.config/OpenGuitarMultiFx/presets/ParallelRows.xml` creates 3 level-0 lanes
(rows 0,1,2 all `inputChannel=0`, all `toDevice=1`) → widest stage 3 → expect 2 workers on load.

## UI references (from code, verified)
- Add effect: hover a grid cell in the target row → "+" appears under cursor → click →
  `AddEffectBrowserDialog` overlay (category rail + tiles + search). MainComponent.cpp:186-194.
- Row input tile: left-gutter "+" per row → `showRowInputMenu` (physical-input list or "Default"
  fallback item). MainComponent.cpp:479-566.
- Row output tile: right-gutter "+" per row → `showRowOutputMenu`, items: "Not connected",
  "Output <pair name>", "Line 2..4" toggles (forward-only). MainComponent.cpp:568-647.
- Preset browser: `presetBadge` (top-left, 88px) → `showPresetsPanel` → `PresetBrowserDialog`.
- Window is JUCE-drawn (no native titlebar, wmctrl shows "N/A") — activate/maximize by window ID.

## Tests

### T1 — Launch
Launch `build/OpenGuitarMultiFx_artefacts/Release/OpenGuitarMultiFx` on :0.
PASS: window appears, title does NOT read "Audio device failed to open"
(MainComponent.cpp:239-240 sets that title iff device open failed); log file
openguitarmultifx.log contains "AudioEngine: using" line; `pgrep -af OpenGuitarMultiFx` alive;
0 SignalGraphLane threads (empty graph: only row 0 participates, 1 lane → not dispatchable).
FAIL: crash, "failed to open" title, or any SignalGraphLane threads already.

### T2 — One effect on row 0 only (serial must spawn ZERO workers)
Hover a row-0 cell, click "+", pick a light effect (e.g. GE-7 equalizer or a noise gate).
PASS: block tile appears in row 0; still 0 SignalGraphLane threads (single lane = serial,
matches commit claim "serial topologies spawn zero workers"); UI responsive.

### T3 — Second row as parallel lane → workers spawn
a) Add an effect to a row-1 cell (e.g. Parametric EQ).
b) Row-1 input "+" (left gutter) → pick the physical-input item ("Default"/first input name).
c) Row-1 output "+" (right gutter) → pick "Output <first pair name>".
PASS: row 1 input tile shows "IN", output tile shows "TO <name>" (refreshRowEndpoints);
exactly 1 SignalGraphLane thread in /proc; UI responsive. Rows 0+1 both at level 0 →
dispatchable stage of 2 → 1 worker expected.
FAIL: app freezes, crashes, wrong thread count, or endpoint tiles don't update.

### T4 — Adversarial rapid rebuilds (worker churn)
Rapidly (~1s spacing or faster):
a) Row-0 output "+" → toggle "Line 2" ON  (row0→row1 edge: row1 level→1; stage 0 drops to 1 lane
   → new graph wants 0 workers; graph WITH a worker is retired ~500ms later)
b) Row-0 output "+" → toggle "Line 2" OFF (back to parallel → respawn 1 worker)
c) Add another effect to row 0 (rebuild while workers live)
d) Select the new block → parameter drawer → Remove it (onRemoveRequested → rebuild)
e) Repeat a/b once more quickly.
PASS after each: UI accepts the next click without visible stall (screenshots show updated
endpoint text/block grid); process alive; thread count matches topology (1 while parallel,
0 after serialize); no zombie SignalGraphLane accumulation (old workers must die).
FAIL: UI frozen >2s, crash, spin, or stale SignalGraphLane threads persisting >2s after
topology change.

### T5 — Multi-row preset load
PresetBadge (top-left) → PresetBrowserDialog → tap "ParallelRows" (crafted: 3 parallel lanes).
If a "save current?" ChoiceCard appears (chain is dirty), choose the discard option.
PASS: 3 block tiles appear on rows 0–2; rows show IN/TO endpoints; exactly 2 SignalGraphLane
threads; UI responsive. The load tears down the whole old chain (removeEffect loop → many
rebuilds) then rebuilds — heavy worker churn in one operation.
FAIL: crash/freeze, wrong thread count, preset content not reflected.

### T6 — More churn + clean exit
One last round of rapid routing toggles, then close the app via its JUCE-drawn close (X) button.
PASS: `closeButtonPressed`/`systemRequestedQuit` in log; process exits within ~5s
(`pgrep -af OpenGuitarMultiFx` empty, no zombie); no SignalGraphLane threads remain
(`~SignalGraph → stopWorkers` must join workers, not detach/orphan).
FAIL: hang on exit (needs SIGKILL), crash/abort message in log.

## Not verifiable here
Real audio output (null device produces silence; output equivalence already proven by
SIGNAL_GRAPH_BENCH bit-identical claim). UI evidence only proves no crash/hang/deadlock and
correct worker spawn/stop counts.
