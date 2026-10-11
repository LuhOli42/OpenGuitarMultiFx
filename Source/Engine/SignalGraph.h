#pragma once

#include "../Effects/EffectProcessor.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <utility>
#include <vector>

namespace openguitarmultifx
{

/**
    Four parallel lanes (matching MainComponent::numRows), each a serial
    chain of EffectProcessor instances, connected by explicit lane-to-lane
    edges -- a lane's processed output can feed several other lanes at
    once (a split) and a lane can receive from several other lanes at
    once, summed (a merge), replacing the old "flatten every branch into
    one serial chain" approximation (see MainComponent::rebuildSignalGraph()'s
    former comment). SignalGraph does NOT own the processors, same as
    before -- see the ownership note that used to live here, still true.

    addLaneConnection() REQUIRES sourceLane < targetLane. This isn't a
    general-purpose graph: the UI only ever lets a row feed a LATER row
    (MainComponent's "forward-only" rule, see Source/UI/AGENTS.md), which
    means processing lanes 0,1,2,3 in that fixed order is *always* a valid
    topological order -- no cycle detection or generic topo-sort needed,
    and no cycles are constructible through this API in the first place.

    Built entirely on the control thread (the setX/addX calls + prepare())
    and only then published into AudioEngine via DeferredReclaimer -- same
    "immutable once published" contract as before.

    ROW-PARALLEL PROCESSING (Phase 6, for the A7S's 8 cores): prepare()
    groups lanes into stages by dependency level -- a lane's level is
    1 + the max level of its sources, so two lanes in the same stage can
    never have an edge between them and are safe to run concurrently.
    A stage with 2+ lanes bearing processors is dispatched to a small
    pool of worker threads created here in prepare() (control thread),
    one worker per extra lane; the audio thread always runs one lane
    itself. Workers sleep on a futex (std::atomic::wait) between blocks
    and are woken by an epoch bump + notify -- no allocation, no locks
    with unbounded wait, no condition_variable anywhere in process().
    At each stage's end the audio thread takes over any job no worker
    claimed in time and runs it inline (never drop audio); a job a
    worker DID claim is waited on via futex, bounded by that lane's own
    processing time -- the same work the audio thread would have done
    serially. Set OGMFX_DISABLE_ROW_PARALLEL to fall back to the old
    pure-serial path. Stages that stay serial (every single-processor
    stage, and every stage when no workers exist) cost only the old
    per-lane loop plus a couple of branches.

    Two ordering/parity guarantees on top of that: (1) if the calling
    thread is realtime (juce::Thread::isRealtime), dispatch only engages
    when every worker got realtime scheduling too -- an RT audio
    callback never futex-waits on a non-RT worker, which is the one
    configuration where a preempted worker could out-wait a block
    deadline; non-RT callers (tests, RenderFx, a desktop without
    rtprio) still parallelize, since waiting on a non-RT worker is no
    worse than doing the work inline. (2) A processor instance shared
    between lanes in DIFFERENT stages would see its calls reordered
    vs. the old index-order pass, so any cross-lane sharing forces the
    whole graph back to the legacy serial order -- output stays
    bit-identical to the pre-parallel schedule.
*/
class SignalGraph
{
public:
    static constexpr int numLanes = 4;

    SignalGraph() = default;
    ~SignalGraph();
    SignalGraph (const SignalGraph&) = delete; // workers hold a raw pointer back to this graph
    SignalGraph& operator= (const SignalGraph&) = delete;

    /** One lane's serial sub-chain, in processing order. Replaces
        whatever was set for this lane before. Control thread, before
        prepare(). */
    void setLaneProcessors (int lane, std::vector<EffectProcessor*> processorsForLane);

    /** Whether this lane's input starts from the live device input
        (in addition to whatever addLaneConnection() feeds it, if
        anything -- both can be true at once, they just sum). */
    void setLaneReadsDeviceInput (int lane, bool readsInput);

    /** Whether this lane's processed output is mixed into the buffer
        process() hands back to the device. */
    void setLaneWritesDeviceOutput (int lane, bool writesOutput);

    /** sourceLane's processed output is summed into targetLane's input.
        Call once per edge: calling it for the same source with different
        targets is a split, calling it for the same target from different
        sources is a merge -- both are just "add another edge", not a
        special case. Requires sourceLane < targetLane (see class doc). */
    void addLaneConnection (int sourceLane, int targetLane);

    void prepare (double sampleRate, int maxBlockSize, int numChannels);

    /** `buffer` holds the live device input on entry. If at least one lane
        writes to the device output, `buffer` is overwritten with the sum
        of those lanes' output; if NONE do (nothing configured at all, or
        every configured lane only feeds other lanes), `buffer` is left
        exactly as it was on entry -- the same "empty graph is a total
        passthrough" contract AudioEngine has always relied on. */
    void process (juce::AudioBuffer<float>& buffer);

    void reset();

    /** Lanes a worker thread actually ran (the rest ran inline on the
        audio thread). Diagnostics only -- used by the bench/tests to
        prove the parallel path engaged. */
    int getJobsRunByWorkers() const { return jobsRunByWorkers.load (std::memory_order_relaxed); }

    /** Called by whoever owns the audio callback (AudioEngine) once it
        knows the callback's scheduling. process() can identify a
        juce::Thread caller itself, but a native backend thread reports
        no juce::Thread at all -- this flag is how an RT callback on a
        native thread still gets the RT-worker-only gate. Defaults
        false: non-RT callers (tests, RenderFx, desktops without
        rtprio) always parallelize. */
    void setAudioCallerIsRealtime (bool isRealtime) { audioCallerIsRealtime.store (isRealtime, std::memory_order_relaxed); }

private:
    struct Lane
    {
        std::vector<EffectProcessor*> processors;
        bool readsDeviceInput = false;
        bool writesDeviceOutput = false;
        juce::AudioBuffer<float> buffer;
    };

    /** One dispatchable unit of lane work. `word` packs the job state in
        the high bits and the lane index in the low byte so a claimant
        always reads a coherent (lane, state) pair out of ONE atomic --
        a lane index living in a separate field would have no
        happens-before edge to a worker that grabs the job while a later
        stage is being written (see the dispatch path in process()). */
    struct LaneJob
    {
        enum State : uint32_t { idle = 0, pending, claimed, done };
        static constexpr uint32_t laneMask = 0xff;

        static uint32_t pack (State s, int lane) { return (uint32_t) (s << 8) | (uint32_t) lane; }
        static State state (uint32_t w) { return (State) (w >> 8); }
        static int lane (uint32_t w) { return (int) (w & laneMask); }

        std::atomic<uint32_t> word { pack (idle, 0) };
    };

    /** Lanes that can run concurrently: every lane in a stage has all of
        its sources in earlier stages (levels, see the class doc).
        `dispatchable` is computed in prepare(): the stage is worth
        splitting across cores (2+ processor-bearing lanes). A processor
        instance shared between lanes anywhere in the graph forces the
        legacy serial order instead (see legacySerialOrder). */
    struct Stage
    {
        std::array<int, numLanes> laneIndices {};
        int count = 0;
        bool dispatchable = false;
    };

    class LaneWorker final : public juce::Thread
    {
    public:
        LaneWorker (SignalGraph& owningGraph, int index)
            // Fits Linux's 15-char comm limit -- a longer name makes
            // pthread_setname_np fail silently and the workers show up
            // as the process name in ps/top instead.
            : juce::Thread ("SigGraphLane" + juce::String (index)), graph (owningGraph) {}

        void run() override;

    private:
        SignalGraph& graph;
    };

    void runLane (int laneIndex, int numSamples);
    void runStage (const Stage&, int numSamples);
    void collectJobs (int numJobs, int numSamples);
    void reconcileWorkers (int wanted, double sampleRate, int maxBlockSize);
    void stopWorkers();

    std::array<Lane, numLanes> lanes;
    std::vector<std::pair<int, int>> connections;
    juce::AudioBuffer<float> deviceInputSnapshot;
    int preparedNumChannels = 0;

    // Everything below is prepared on the control thread, read on the
    // audio thread and the worker threads.
    std::array<Stage, numLanes> stages;   // at most one stage per level, and levels < numLanes
    int numStages = 0;
    std::array<std::array<int, numLanes - 1>, numLanes> laneSources {};
    std::array<int, numLanes> laneSourceCounts {};
    bool anyLaneWritesOutput = false;
    bool legacySerialOrder = false;   // shared processor instance across lanes -> old index-order pass
    bool workersAreRealtime = true;   // every LaneWorker got RT scheduling in prepare()
    std::atomic<bool> audioCallerIsRealtime { false }; // set via setAudioCallerIsRealtime

    // Dispatch state touched by the audio thread + workers every block.
    // The epoch counter doubles as the futex workers sleep on.
    std::array<LaneJob, numLanes - 1> jobs;
    std::atomic<uint64_t> jobEpoch { 0 };
    int jobNumSamples = 0;                // written before each epoch bump, read only by claimants
    std::atomic<bool> workersShouldStop { false };
    std::atomic<int> jobsRunByWorkers { 0 };
    std::vector<std::unique_ptr<LaneWorker>> workers;
};

} // namespace openguitarmultifx
