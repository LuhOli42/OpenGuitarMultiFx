#pragma once

#include "../Effects/EffectProcessor.h"
#include "OutputRoute.h"

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

    /** Pins this lane's device input to one specific physical input channel
        (per-row inputs, e.g. guitar on input 1 and bass on input 3). -1 (the
        default) means "the shared buffer handed to process()". Only used
        when process() is given the per-channel device inputs. */
    void setLaneDeviceInputChannel (int lane, int deviceChannel);

    /** Whether this lane's processed output is mixed into the buffer
        process() hands back to the device. */
    void setLaneWritesDeviceOutput (int lane, bool writesOutput);

    /** Where this lane's output lands on the physical outputs (OutputRoute.h).
        Setting a route on ANY lane switches the graph to routed output: when a
        lane writes the device output, process() clears every device channel
        and each writing lane adds itself along its own route (a writing lane
        with no route uses Out 1/2 stereo), so two rows can play out of
        different pairs -- or single channels -- at once. A graph with no
        routes keeps the legacy mix-and-spread to every channel, which
        AudioEngine then narrows to its one selected pair. Control thread,
        before prepare(). */
    void setLaneOutputRoute (int lane, OutputRoute route);

    /** One more physical destination for this lane (an output split: the same
        row on Out 1/2 AND Out 3/4, say). Up to maxOutputRoutesPerLane; an exact
        duplicate is ignored. Same routed-mode rules as setLaneOutputRoute(). */
    void addLaneOutputRoute (int lane, OutputRoute route);
    static constexpr int maxOutputRoutesPerLane = 8;

    /** Device channels process() writes when the graph is routed and some
        lane writes the output; 0 otherwise (passthrough or legacy spread --
        AudioEngine's selected pair decides then). Valid after prepare(), plus
        the channels the DeviceIo blocks (EffectProcessor::usesDeviceIo) sent
        to in the last process() call -- read it after process(). */
    uint32_t getRoutedChannelMask() const noexcept { return routedChannelMask | sendChannelMask; }

    /** sourceLane's processed output is summed into targetLane's input.
        Call once per edge: calling it for the same source with different
        targets is a split, calling it for the same target from different
        sources is a merge -- both are just "add another edge", not a
        special case. Requires sourceLane < targetLane (see class doc). */
    void addLaneConnection (int sourceLane, int targetLane, LaneLink link = {});

    /** The level/polarity/channel map of the sourceLane -> targetLane edge
        (a default LaneLink when the edge was added without one). */
    const LaneLink& getLaneLink (int sourceLane, int targetLane) const { return links[(size_t) sourceLane][(size_t) targetLane]; }

    void prepare (double sampleRate, int maxBlockSize, int numChannels);

    /** PERFORMANCE MODE (control thread, before prepare()). When the graph is one serial lane (the common live rig:
        input -> chain -> output, no splits), its chain is cut in two: the first part runs on the audio thread, the
        second on a worker pinned to the other fast core, one block behind. Each part then has a whole block of time
        instead of the chain sharing one, roughly doubling what a chain can cost before it drops out.

        The price is `pipelineLatencyBlocks` extra blocks of latency (2 x 2.7 ms at 128 samples) and silence for that
        long when a graph starts (a chain edit). Where the cut sits is rebalanced from measured per-processor times,
        only at moments the worker is idle. Any other graph shape runs exactly as before (isPipelineActive() false). */
    void setPipelineEnabled (bool enabled) { pipelineRequested = enabled; }
    bool isPipelineActive() const noexcept { return pipeLane >= 0 && ! pipeSendMode; }

    /** PARALLEL SEND (control thread, before prepare()), for the same single-lane graph: the trailing delays/reverbs
        (processors with trails and the Wet-over-dry law, see EffectProcessor::isWetOverDryEffect) are moved to the worker
        as a send. The dry signal never waits: the audio thread plays it at once and adds the wet part the worker computed
        pipelineLatencyBlocks blocks ago -- an echo or a reverb tail that starts 5 ms later is not audible, and the cost of
        the effects leaves the audio thread. Ignored when setPipelineEnabled() is on (the whole chain already is split). */
    void setSendEnabled (bool enabled) { sendRequested = enabled; }
    bool isSendActive() const noexcept { return pipeLane >= 0 && pipeSendMode; }
    static constexpr int pipelineLatencyBlocks = 2;
    /** Blocks the worker had not finished in time since the last call (each repeats the previous output). */
    int takePipelineLateBlocks() noexcept { return pipeLateBlocks.exchange (0, std::memory_order_relaxed); }
    /** The current cut: how many processors the audio thread runs itself (diagnostics/tests). */
    int getPipelineSplit() const noexcept { return pipeSplit; }

    /** `buffer` holds the live device input on entry. If at least one lane
        writes to the device output, `buffer` is overwritten with the sum
        of those lanes' output; if NONE do (nothing configured at all, or
        every configured lane only feeds other lanes), `buffer` is left
        exactly as it was on entry -- the same "empty graph is a total
        passthrough" contract AudioEngine has always relied on. */
    void process (juce::AudioBuffer<float>& buffer);

    /** Same, but also given every physical input channel so lanes pinned
        with setLaneDeviceInputChannel() read their own channel (times
        `inputGain`). The pointers must stay valid for the call. */
    void process (juce::AudioBuffer<float>& buffer, const float* const* deviceInputs, int numDeviceInputs, float inputGain);

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
        int deviceInputChannel = -1;   // -1 = shared buffer
        bool writesDeviceOutput = false;
        std::array<OutputRoute, maxOutputRoutesPerLane> outputRoutes {};
        int numOutputRoutes = 0;
        juce::AudioBuffer<float> buffer;
    };

    /** Writes the lanes' output into the device buffer: routed (per-lane
        OutputRoute) or the legacy mix spread to every channel. `pipelined`
        is the delayed output of pipeLane when the pipeline/send path runs. */
    void writeDeviceOutput (juce::AudioBuffer<float>& buffer, int numSamples,
                            const juce::AudioBuffer<float>* pipelined) noexcept;

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
            : juce::Thread ("SigGraphLane" + juce::String (index)), graph (owningGraph), slot (index) {}

        void run() override;

    private:
        SignalGraph& graph;
        int slot; // which audio core this worker owns (worker 0 -> slot 1, because slot 0 is the audio callback)
    };

    void processImpl (juce::AudioBuffer<float>& buffer);
    void processPipelined (juce::AudioBuffer<float>& buffer);
    void processSend (juce::AudioBuffer<float>& buffer);
    void rebalancePipeline();
    void stopPipelineWorker();
    void runLane (int laneIndex, int numSamples);
    void loadLaneInput (int laneIndex, int numSamples); // clears the lane buffer and sums in its input and source lanes
    void runStage (const Stage&, int numSamples);
    void collectJobs (int numJobs, int numSamples);
    void reconcileWorkers (int wanted, double sampleRate, int maxBlockSize);
    void stopWorkers();

    std::array<Lane, numLanes> lanes;
    std::vector<std::pair<int, int>> connections;
    std::array<std::array<LaneLink, numLanes>, numLanes> links {};   // [source][target]

    // Crossover state (phase 4), coefficients set in prepare(). A filtered
    // route runs on a copy of its source: per TARGET lane for links (lanes of
    // one stage load their inputs on different threads), one shared scratch
    // for the output routes (written on the audio thread only).
    std::array<std::array<RouteFilterState, numLanes>, numLanes> linkFilterState {};
    std::array<std::array<RouteFilterState, maxOutputRoutesPerLane>, numLanes> outputFilterState {};
    std::array<juce::AudioBuffer<float>, numLanes> linkScratch;
    juce::AudioBuffer<float> outputScratch;
    void addOutputRoute (juce::AudioBuffer<float>& device, const juce::AudioBuffer<float>& src,
                         int lane, int routeIndex, int numSamples) noexcept;
    juce::AudioBuffer<float> deviceInputSnapshot;
    const float* const* perChannelInputs = nullptr;   // set by process(), read by lanes in the same call
    int numPerChannelInputs = 0;
    float perChannelGain = 1.0f;
    int preparedNumChannels = 0;

    // Everything below is prepared on the control thread, read on the
    // audio thread and the worker threads.
    std::array<Stage, numLanes> stages;   // at most one stage per level, and levels < numLanes
    int numStages = 0;
    std::array<std::array<int, numLanes - 1>, numLanes> laneSources {};
    std::array<int, numLanes> laneSourceCounts {};
    bool anyLaneWritesOutput = false;
    bool routedOutput = false;           // some lane has an OutputRoute (prepare())
    uint32_t routedChannelMask = 0;      // see getRoutedChannelMask()
    bool legacySerialOrder = false;   // shared processor instance across lanes -> old index-order pass

    // Device I/O blocks (EffectProcessor::usesDeviceIo): one DeviceIo + device-wide send bus per lane (lanes of a
    // stage run on different threads), sized in prepare() only when such a block exists. A graph with one is always
    // routed and never pipelined (see usesDeviceIo).
    bool anyDeviceIo = false;
    std::array<bool, numLanes> laneHasDeviceIo {};
    std::array<EffectProcessor::DeviceIo, numLanes> laneIo {};
    std::array<juce::AudioBuffer<float>, numLanes> laneSends;
    uint32_t sendChannelMask = 0;     // audio thread: channels the blocks sent to in the last process()
    void addDeviceSends (juce::AudioBuffer<float>& device, int numSamples) noexcept;
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

    // ---- performance mode (see setPipelineEnabled) ----
    static constexpr int pipeDepth = 3; // blocks in flight: 2 being worked on + 1 free to fill
    struct PipeSlot
    {
        enum State : int { free = 0, ready = 1, done = 2 };
        juce::AudioBuffer<float> buffer;
        int numSamples = 0;
        int split = 0;                    // processors [0, split) ran on the audio thread, [split, end) are the worker's
        int end = 1 << 30;                // clamped to the chain length
        float dryGain = 1.0f;             // send mode: share of the input the worker's chain passes through untouched
        std::atomic<int> state { free };
        juce::AudioBuffer<float> dry;     // send mode: the dry signal the worker's input was copied from
    };

    class PipeWorker final : public juce::Thread
    {
    public:
        explicit PipeWorker (SignalGraph& owningGraph) : juce::Thread ("SigGraphPipe"), graph (owningGraph) {}
        void run() override;

    private:
        SignalGraph& graph;
    };

    bool pipelineRequested = false;
    bool sendRequested = false;
    bool pipeSendMode = false;            // the worker computes a delayed wet-only send instead of the chain's second half
    int pipeLane = -1;                    // the one pipelined lane, -1 = off
    std::array<PipeSlot, pipeDepth> pipeSlots;
    std::unique_ptr<PipeWorker> pipeWorker;
    std::atomic<uint32_t> pipeEpoch { 0 };       // futex the worker sleeps on; bumped after a slot turns ready
    std::atomic<bool> pipeShouldStop { false };
    std::atomic<int> pipeLateBlocks { 0 };
    bool pipeSendHead = false;            // send mode: the worker runs the LEADING synths instead of the trailing delays/reverbs
    int pipeSplit = 1;                    // audio thread only (the worker reads the split stored in each slot)
    int pipeWriteSlot = 0, pipeReadSlot = 0, pipeInFlight = 0;   // audio thread only
    int pipeBlocksSinceRebalance = 0;
    std::vector<std::atomic<float>> pipeProcessorCost;           // smoothed microseconds per processor, measured where it runs
    juce::AudioBuffer<float> pipeLastOutput;                     // repeated when the worker is late
};

} // namespace openguitarmultifx
