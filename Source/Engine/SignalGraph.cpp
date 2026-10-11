#include "SignalGraph.h"

#include <cstdlib>

namespace openguitarmultifx
{

namespace
{
    /** Adds up to `dst`'s own channel count from `src`, whichever is
        fewer -- lane buffers and the device buffer handed to process()
        don't always have the same channel count (a lane is always
        `preparedNumChannels` wide; the device buffer can have more
        channels on a multi-output interface), and this is the one place
        that mismatch needs handling. */
    void addClamped (juce::AudioBuffer<float>& dst, const juce::AudioBuffer<float>& src, int numSamples) noexcept
    {
        const int numChannels = juce::jmin (dst.getNumChannels(), src.getNumChannels());
        for (int ch = 0; ch < numChannels; ++ch)
            dst.addFrom (ch, 0, src, ch, 0, numSamples);
    }

    /** Same idea, but for writing a (possibly narrower) mix INTO a
        (possibly wider) device buffer -- every device channel gets SOME
        content rather than the extras being left silent, matching the
        pre-existing behaviour of duplicating a mono input across every
        output channel. */
    void spreadInto (juce::AudioBuffer<float>& dst, const juce::AudioBuffer<float>& src, int numSamples) noexcept
    {
        if (src.getNumChannels() == 0)
            return;

        for (int ch = 0; ch < dst.getNumChannels(); ++ch)
            dst.copyFrom (ch, 0, src, ch % src.getNumChannels(), 0, numSamples);
    }
}

SignalGraph::~SignalGraph()
{
    stopWorkers();
}

void SignalGraph::setLaneProcessors (int lane, std::vector<EffectProcessor*> processorsForLane)
{
    lanes[(size_t) lane].processors = std::move (processorsForLane);
}

void SignalGraph::setLaneReadsDeviceInput (int lane, bool readsInput)
{
    lanes[(size_t) lane].readsDeviceInput = readsInput;
}

void SignalGraph::setLaneWritesDeviceOutput (int lane, bool writesOutput)
{
    lanes[(size_t) lane].writesDeviceOutput = writesOutput;
}

void SignalGraph::addLaneConnection (int sourceLane, int targetLane)
{
    jassert (sourceLane < targetLane); // see the class doc comment -- forward-only by construction
    connections.emplace_back (sourceLane, targetLane);
}

void SignalGraph::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    preparedNumChannels = numChannels;
    deviceInputSnapshot.setSize (numChannels, maxBlockSize, false, true, true);

    for (auto& lane : lanes)
    {
        lane.buffer.setSize (numChannels, maxBlockSize, false, true, true);
        for (auto* p : lane.processors)
            p->prepareIfNeeded (sampleRate, maxBlockSize, numChannels);
    }

    // ---- Parallel schedule (see the class doc) ----
    // Per-lane incoming source lanes, and whether each lane does anything
    // at all this block. A lane with no processors, no input, no output
    // and no edges is invisible to the signal path -- it isn't scheduled.
    std::array<bool, numLanes> participates {};

    for (auto& count : laneSourceCounts)
        count = 0;

    for (auto& connection : connections)
    {
        auto& count = laneSourceCounts[(size_t) connection.second];
        if (count < numLanes - 1) // a lane can have at most numLanes-1 distinct sources; ignore dupes
            laneSources[(size_t) connection.second][(size_t) count++] = connection.first;
    }

    std::array<bool, numLanes> isEndpoint {};
    for (auto& connection : connections)
    {
        isEndpoint[(size_t) connection.first] = true;
        isEndpoint[(size_t) connection.second] = true;
    }

    anyLaneWritesOutput = false;
    for (int i = 0; i < numLanes; ++i)
    {
        const auto& lane = lanes[(size_t) i];
        anyLaneWritesOutput = anyLaneWritesOutput || lane.writesDeviceOutput;
        participates[(size_t) i] = ! lane.processors.empty()
                                   || lane.readsDeviceInput
                                   || lane.writesDeviceOutput
                                   || isEndpoint[(size_t) i];
    }

    // Dependency level: 0 for a lane fed only by the device input,
    // 1 + max(source levels) otherwise. Iterating lanes ascending works
    // because every source is a strictly-lower index (forward-only).
    std::array<int, numLanes> level {};
    for (int i = 0; i < numLanes; ++i)
        for (int s = 0; s < laneSourceCounts[(size_t) i]; ++s)
            level[(size_t) i] = juce::jmax (level[(size_t) i], level[(size_t) laneSources[(size_t) i][(size_t) s]] + 1);

    numStages = 0;
    int widestDispatchableStage = 0;

    for (int l = 0; l < numLanes; ++l)
    {
        auto& stage = stages[(size_t) numStages];
        stage.count = 0;
        stage.dispatchable = false;

        for (int i = 0; i < numLanes; ++i)
            if (participates[(size_t) i] && level[(size_t) i] == l)
                stage.laneIndices[(size_t) stage.count++] = i;

        if (stage.count == 0)
            continue;

        // Dispatchable = the stage can actually split work across cores:
        // at least two lanes carrying processors. Shared instances are a
        // whole-graph concern handled by legacySerialOrder below, not a
        // per-stage one -- cross-LEVEL sharing would also reorder calls.
        int processorLanes = 0;
        for (int a = 0; a < stage.count; ++a)
            processorLanes += (lanes[(size_t) stage.laneIndices[(size_t) a]].processors.empty() ? 0 : 1);

        stage.dispatchable = stage.count >= 2 && processorLanes >= 2;
        if (stage.dispatchable)
            widestDispatchableStage = juce::jmax (widestDispatchableStage, stage.count);

        ++numStages;
    }

    // A processor instance shared between lanes must keep the OLD call
    // order: same-stage it would run on two threads at once (processor
    // state isn't reentrant), and cross-stage the level-order schedule
    // can reorder it vs. the old index-order pass. Either way the flat
    // 0->3 pass is the only provably identical schedule, so any
    // cross-lane sharing forces it for the whole graph.
    legacySerialOrder = false;
    for (int i = 0; i < numLanes && ! legacySerialOrder; ++i)
        for (int j = i + 1; j < numLanes && ! legacySerialOrder; ++j)
            for (auto* pa : lanes[(size_t) i].processors)
                for (auto* pb : lanes[(size_t) j].processors)
                    legacySerialOrder = legacySerialOrder || (pa == pb);

    // One worker per extra lane in the widest dispatchable stage -- the
    // audio thread always runs one lane itself. Zero on serial topologies,
    // on legacy-order graphs, and when OGMFX_DISABLE_ROW_PARALLEL is set
    // (kill switch / bench A-B).
    const bool parallelAllowed = std::getenv ("OGMFX_DISABLE_ROW_PARALLEL") == nullptr;
    reconcileWorkers (parallelAllowed && ! legacySerialOrder
                          ? juce::jmax (0, widestDispatchableStage - 1) : 0,
                      sampleRate, maxBlockSize);
}

void SignalGraph::runLane (int laneIndex, int numSamples)
{
    auto& lane = lanes[(size_t) laneIndex];
    lane.buffer.clear();

    if (lane.readsDeviceInput)
        addClamped (lane.buffer, deviceInputSnapshot, numSamples);

    for (int s = 0; s < laneSourceCounts[(size_t) laneIndex]; ++s)
        addClamped (lane.buffer, lanes[(size_t) laneSources[(size_t) laneIndex][(size_t) s]].buffer, numSamples);

    for (auto* p : lane.processors)
        p->processWithBypass (lane.buffer); // bypass is a crossfade now, not a skip -- see EffectProcessor
}

void SignalGraph::collectJobs (int numJobs, int numSamples)
{
    // First pass: run every job no worker claimed in time ourselves --
    // never drop audio. Second pass: futex-wait on the jobs a worker is
    // running, bounded by that lane's own duration (the same work the
    // audio thread would have done serially anyway).
    for (int j = 0; j < numJobs; ++j)
    {
        auto& job = jobs[(size_t) j];
        auto w = job.word.load (std::memory_order_acquire);
        while (LaneJob::state (w) == LaneJob::pending)
        {
            if (job.word.compare_exchange_strong (w, LaneJob::pack (LaneJob::claimed, LaneJob::lane (w)),
                                                  std::memory_order_acq_rel))
            {
                runLane (LaneJob::lane (w), numSamples);
                job.word.store (LaneJob::pack (LaneJob::done, LaneJob::lane (w)), std::memory_order_release);
                break;
            }
        }
    }

    for (int j = 0; j < numJobs; ++j)
    {
        auto& job = jobs[(size_t) j];
        auto w = job.word.load (std::memory_order_acquire);
        while (LaneJob::state (w) != LaneJob::done)
        {
            job.word.wait (w);
            w = job.word.load (std::memory_order_acquire);
        }
    }
}

void SignalGraph::runStage (const Stage& stage, int numSamples)
{
    const int numJobs = juce::jmin ((int) workers.size(), stage.count - 1);

    if (numJobs == 0)
    {
        for (int i = 0; i < stage.count; ++i)
            runLane (stage.laneIndices[(size_t) i], numSamples);
        return;
    }

    // Publish the jobs BEFORE the epoch bump: a worker only scans job
    // words after observing the new epoch, so the release/acquire pair
    // makes both fields (packed lane + state, jobNumSamples) visible.
    // The job-word stores are release (not just relaxed under the epoch's
    // release) because a worker finishing an earlier scan can claim a
    // pending word before the bump is even visible to it -- its acquire
    // CAS must then still see jobNumSamples.
    jobNumSamples = numSamples;
    for (int j = 0; j < numJobs; ++j)
        jobs[(size_t) j].word.store (LaneJob::pack (LaneJob::pending, stage.laneIndices[(size_t) j + 1]),
                                     std::memory_order_release);

    jobEpoch.fetch_add (1, std::memory_order_release);
    jobEpoch.notify_all();

    // The audio thread runs the first lane itself, plus every lane the
    // job slots couldn't cover (pool smaller than the stage -- can't
    // happen with the pool sized to the widest stage, kept as a
    // correctness net anyway).
    runLane (stage.laneIndices[0], numSamples);
    for (int i = numJobs + 1; i < stage.count; ++i)
        runLane (stage.laneIndices[(size_t) i], numSamples);

    collectJobs (numJobs, numSamples);
}

void SignalGraph::LaneWorker::run()
{
    // Same FTZ/DAZ environment the audio callback sets for itself --
    // denormal-heavy tails (reverb, filters) are 10-100x slower without
    // it, and this thread runs the same DSP.
    const juce::ScopedNoDenormals noDenormals;

    auto seen = graph.jobEpoch.load (std::memory_order_acquire);

    while (! threadShouldExit() && ! graph.workersShouldStop.load (std::memory_order_relaxed))
    {
        const auto e = graph.jobEpoch.load (std::memory_order_acquire);
        if (e == seen)
        {
            graph.jobEpoch.wait (seen); // futex sleep until the next dispatch or the stop bump
            continue;
        }
        seen = e;

        for (auto& job : graph.jobs)
        {
            auto w = job.word.load (std::memory_order_relaxed);
            while (LaneJob::state (w) == LaneJob::pending)
            {
                if (job.word.compare_exchange_weak (w, LaneJob::pack (LaneJob::claimed, LaneJob::lane (w)),
                                                    std::memory_order_acq_rel))
                {
                    graph.runLane (LaneJob::lane (w), graph.jobNumSamples);
                    graph.jobsRunByWorkers.fetch_add (1, std::memory_order_relaxed);
                    job.word.store (LaneJob::pack (LaneJob::done, LaneJob::lane (w)), std::memory_order_release);
                    job.word.notify_all();
                    break;
                }
            }
        }
    }
}

void SignalGraph::reconcileWorkers (int wanted, double sampleRate, int maxBlockSize)
{
    if ((int) workers.size() == wanted)
        return;

    stopWorkers();
    workers.reserve ((size_t) wanted);

    for (int i = 0; i < wanted; ++i)
    {
        auto worker = std::make_unique<LaneWorker> (*this, i);

        // Realtime scheduling first (same DSP work as the audio thread);
        // a plain high-priority thread when the process lacks rtprio/rtkit
        // -- dispatch then only engages for non-RT callers (see process()).
        if (! worker->startRealtimeThread (juce::Thread::RealtimeOptions()
                                               .withPriority (5)
                                               .withApproximateAudioProcessingTime (maxBlockSize, sampleRate)))
        {
            worker->startThread (juce::Thread::Priority::high);
            workersAreRealtime = false;
        }

        workers.push_back (std::move (worker));
    }
}

void SignalGraph::stopWorkers()
{
    if (workers.empty())
        return;

    workersShouldStop.store (true, std::memory_order_relaxed);
    jobEpoch.fetch_add (1, std::memory_order_release);
    jobEpoch.notify_all();

    for (auto& w : workers)
        w->stopThread (10000);

    workers.clear();
    workersShouldStop.store (false, std::memory_order_relaxed);
}

void SignalGraph::process (juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();

    deviceInputSnapshot.clear();
    addClamped (deviceInputSnapshot, buffer, numSamples);

    if (legacySerialOrder)
        for (int i = 0; i < numLanes; ++i)
            runLane (i, numSamples); // the pre-parallel flat pass, verbatim
    else
    {
        // Parity rule: an RT audio callback only waits on workers that
        // are themselves realtime -- a non-RT worker could be preempted
        // arbitrarily long past a block deadline. Non-RT callers (test
        // runner, RenderFx, desktops without rtprio) always dispatch.
        const auto* callingThread = juce::Thread::getCurrentThread();
        const bool mayDispatch = callingThread == nullptr
                                 || ! callingThread->isRealtime()
                                 || workersAreRealtime;

        // Stages run in level order; lanes inside a stage are independent by
        // construction (see the class doc) and go to the worker pool when the
        // stage is dispatchable -- serial otherwise, the same fixed lane
        // order the pre-parallel version used.
        for (int s = 0; s < numStages; ++s)
        {
            const auto& stage = stages[(size_t) s];
            if (stage.dispatchable && mayDispatch)
            {
                runStage (stage, numSamples);
                continue;
            }

            for (int i = 0; i < stage.count; ++i)
                runLane (stage.laneIndices[(size_t) i], numSamples);
        }
    }

    if (! anyLaneWritesOutput)
        return; // leave `buffer` exactly as it came in -- the passthrough contract

    // Reuse deviceInputSnapshot as the output-mix scratch buffer -- its
    // job as the input snapshot is already done for this block, and
    // allocating a second scratch buffer here would be pointless.
    deviceInputSnapshot.clear();
    for (int i = 0; i < numLanes; ++i)
        if (lanes[(size_t) i].writesDeviceOutput)
            addClamped (deviceInputSnapshot, lanes[(size_t) i].buffer, numSamples);

    spreadInto (buffer, deviceInputSnapshot, numSamples);
}

void SignalGraph::reset()
{
    for (auto& lane : lanes)
        for (auto* p : lane.processors)
            p->reset();
}

} // namespace openguitarmultifx
