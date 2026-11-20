#include "CpuAffinity.h"
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
    stopPipelineWorker();
}

void SignalGraph::setLaneProcessors (int lane, std::vector<EffectProcessor*> processorsForLane)
{
    lanes[(size_t) lane].processors = std::move (processorsForLane);
}

void SignalGraph::setLaneReadsDeviceInput (int lane, bool readsInput)
{
    lanes[(size_t) lane].readsDeviceInput = readsInput;
}

void SignalGraph::setLaneDeviceInputChannel (int lane, int deviceChannel)
{
    lanes[(size_t) lane].deviceInputChannel = deviceChannel;
}

void SignalGraph::setLaneWritesDeviceOutput (int lane, bool writesOutput)
{
    lanes[(size_t) lane].writesDeviceOutput = writesOutput;
}

void SignalGraph::setLaneOutputRoute (int lane, OutputRoute route)
{
    auto& l = lanes[(size_t) lane];
    l.hasOutputRoute = true;
    l.outputRoute = route;
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

    routedOutput = false;
    for (const auto& lane : lanes)
        routedOutput = routedOutput || lane.hasOutputRoute;
    routedChannelMask = 0;
    if (routedOutput)
        for (const auto& lane : lanes)
            if (lane.writesDeviceOutput)
                routedChannelMask |= (lane.hasOutputRoute ? lane.outputRoute : OutputRoute {}).channelMask();

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

    // ---- Performance mode: only a lone serial lane (input -> chain -> output, no edges) can be cut in two ----
    stopPipelineWorker();
    pipeLane = -1;
    pipeSendMode = false;
    pipeSendHead = false;
    if ((pipelineRequested || sendRequested) && connections.empty())
    {
        int bearing = -1, count = 0;
        for (int l = 0; l < numLanes; ++l)
            if (! lanes[(size_t) l].processors.empty())
            {
                bearing = l;
                ++count;
            }

        if (count == 1 && lanes[(size_t) bearing].readsDeviceInput && lanes[(size_t) bearing].writesDeviceOutput
            && ! legacySerialOrder)
        {
            const auto& chain = lanes[(size_t) bearing].processors;
            if (pipelineRequested && chain.size() >= 2)
                pipeLane = bearing;
            else if (sendRequested)
            {
                // The trailing delays/reverbs that use the Wet-over-dry law are the send; everything before them stays here.
                size_t headEnd = 0;
                while (headEnd < chain.size() && chain[headEnd]->isParallelSynth())
                    ++headEnd;
                size_t tailStart = chain.size();
                while (tailStart > 0 && chain[tailStart - 1]->canRunAsSendTail())
                    --tailStart;
                if (headEnd > 0)
                {
                    // Synths track the clean guitar, so they lead the chain: they go to the worker, the rest stays here.
                    pipeLane = bearing;
                    pipeSendMode = true;
                    pipeSendHead = true;
                    pipeSplit = (int) headEnd;
                }
                else if (tailStart < chain.size())
                {
                    pipeLane = bearing;
                    pipeSendMode = true;
                    pipeSplit = (int) tailStart;
                }
            }
        }
    }

    if (pipeLane >= 0)
    {
        const auto n = lanes[(size_t) pipeLane].processors.size();
        for (auto& slot : pipeSlots)
        {
            slot.buffer.setSize (numChannels, maxBlockSize, false, true, true);
            slot.state.store (PipeSlot::free, std::memory_order_relaxed);
        }
        pipeLastOutput.setSize (numChannels, maxBlockSize, false, true, true);
        pipeLastOutput.clear();
        pipeProcessorCost = std::vector<std::atomic<float>> (n);
        for (auto& c : pipeProcessorCost)
            c.store (0.0f, std::memory_order_relaxed);
        if (! pipeSendMode)
            pipeSplit = (int) juce::jmax<size_t> (1, n / 2);
        else
            for (auto& slot : pipeSlots)
                slot.dry.setSize (numChannels, maxBlockSize, false, true, true);
        pipeWriteSlot = pipeReadSlot = pipeInFlight = pipeBlocksSinceRebalance = 0;
        pipeShouldStop.store (false, std::memory_order_relaxed);

        pipeWorker = std::make_unique<PipeWorker> (*this);
        if (! pipeWorker->startRealtimeThread (juce::Thread::RealtimeOptions().withPriority (5)
                                                   .withApproximateAudioProcessingTime (maxBlockSize, sampleRate)))
            pipeWorker->startThread (juce::Thread::Priority::high);
    }
}

void SignalGraph::runLane (int laneIndex, int numSamples)
{
    auto& lane = lanes[(size_t) laneIndex];
    loadLaneInput (laneIndex, numSamples);

    for (auto* p : lane.processors)
        p->processWithBypass (lane.buffer); // bypass is a crossfade now, not a skip -- see EffectProcessor
}

void SignalGraph::loadLaneInput (int laneIndex, int numSamples)
{
    auto& lane = lanes[(size_t) laneIndex];
    lane.buffer.clear();

    if (lane.readsDeviceInput)
    {
        if (lane.deviceInputChannel >= 0 && perChannelInputs != nullptr && lane.deviceInputChannel < numPerChannelInputs)
        {
            // Own physical input, mono duplicated across the lane's channels (same as the shared path does).
            const float* src = perChannelInputs[lane.deviceInputChannel];
            for (int ch = 0; ch < lane.buffer.getNumChannels(); ++ch)
                juce::FloatVectorOperations::addWithMultiply (lane.buffer.getWritePointer (ch), src, perChannelGain, numSamples);
        }
        else
            addClamped (lane.buffer, deviceInputSnapshot, numSamples);
    }

    for (int s = 0; s < laneSourceCounts[(size_t) laneIndex]; ++s)
        addClamped (lane.buffer, lanes[(size_t) laneSources[(size_t) laneIndex][(size_t) s]].buffer, numSamples);
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
    cpuaffinity::pinThisThreadToAudioSlot (slot + 1); // its own fast core, never stacked on the audio callback's (slot 0) -- CpuAffinity.h

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
    workersAreRealtime = true; // re-evaluated per worker below -- a rebuilt pool starts clean

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
    perChannelInputs = nullptr;
    numPerChannelInputs = 0;
    perChannelGain = 1.0f;
    processImpl (buffer);
}

void SignalGraph::process (juce::AudioBuffer<float>& buffer, const float* const* deviceInputs, int numDeviceInputs, float inputGain)
{
    perChannelInputs = deviceInputs;
    numPerChannelInputs = numDeviceInputs;
    perChannelGain = inputGain;
    processImpl (buffer);
}

void SignalGraph::processImpl (juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();

    deviceInputSnapshot.clear();
    addClamped (deviceInputSnapshot, buffer, numSamples);

    if (pipeLane >= 0)
    {
        processPipelined (buffer);
        return;
    }

    if (legacySerialOrder)
        for (int i = 0; i < numLanes; ++i)
            runLane (i, numSamples); // the pre-parallel flat pass, verbatim
    else
    {
        // Parity rule: an RT audio callback only waits on workers that
        // are themselves realtime -- a non-RT worker could be preempted
        // arbitrarily long past a block deadline. A juce::Thread caller
        // reports its own scheduling; a native (non-JUCE) callback thread
        // reports none, so AudioEngine declares it via
        // setAudioCallerIsRealtime instead. Non-RT callers (test runner,
        // RenderFx, desktops without rtprio) always dispatch.
        const auto* callingThread = juce::Thread::getCurrentThread();
        const bool callerIsRealtime = callingThread != nullptr
                                          ? callingThread->isRealtime()
                                          : audioCallerIsRealtime.load (std::memory_order_relaxed);
        const bool mayDispatch = workersAreRealtime || ! callerIsRealtime;

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

    writeDeviceOutput (buffer, numSamples, nullptr);
}

void SignalGraph::writeDeviceOutput (juce::AudioBuffer<float>& buffer, int numSamples,
                                     const juce::AudioBuffer<float>* pipelined) noexcept
{
    if (! anyLaneWritesOutput)
        return; // leave `buffer` exactly as it came in -- the passthrough contract

    if (routedOutput)
    {
        // Every device channel is this graph's now: unrouted ones go silent,
        // each writing lane adds itself along its own route.
        buffer.clear (0, numSamples);
        for (int i = 0; i < numLanes; ++i)
        {
            const auto& lane = lanes[(size_t) i];
            // The pipeline/send path runs only pipeLane (the other lanes'
            // buffers are stale), exactly like its legacy mix below.
            if (! lane.writesDeviceOutput || (pipelined != nullptr && i != pipeLane))
                continue;
            const auto& src = pipelined != nullptr ? *pipelined : lane.buffer;
            (lane.hasOutputRoute ? lane.outputRoute : OutputRoute {}).addInto (buffer, src, numSamples);
        }
        return;
    }

    // Reuse deviceInputSnapshot as the output-mix scratch buffer -- its
    // job as the input snapshot is already done for this block, and
    // allocating a second scratch buffer here would be pointless.
    deviceInputSnapshot.clear();
    if (pipelined != nullptr)
        addClamped (deviceInputSnapshot, *pipelined, numSamples);
    else
        for (int i = 0; i < numLanes; ++i)
            if (lanes[(size_t) i].writesDeviceOutput)
                addClamped (deviceInputSnapshot, lanes[(size_t) i].buffer, numSamples);

    spreadInto (buffer, deviceInputSnapshot, numSamples);
}

// ---------------------------------------------------------------------------------------------------------------------
// Performance mode (see setPipelineEnabled in the header).
// Block k: the audio thread runs stage A on input k and hands the block to the worker, which runs stage B while the
// audio thread is already on block k+1; callback k+2 plays block k. Two blocks of extra latency, in exchange for each
// stage owning a whole block of time. Nothing on the audio thread ever waits: a late worker means the previous output is
// repeated (counted), exactly what an overrun of the serial chain would have cost.
// ---------------------------------------------------------------------------------------------------------------------
void SignalGraph::stopPipelineWorker()
{
    if (pipeWorker == nullptr)
        return;

    pipeShouldStop.store (true, std::memory_order_relaxed);
    pipeEpoch.fetch_add (1, std::memory_order_release);
    pipeEpoch.notify_all();
    pipeWorker->stopThread (10000);
    pipeWorker.reset();
    pipeShouldStop.store (false, std::memory_order_relaxed);
}

void SignalGraph::PipeWorker::run()
{
    const juce::ScopedNoDenormals noDenormals;
    cpuaffinity::pinThisThreadToAudioSlot (1); // the performance-mode / send worker owns the second fast core

    auto& processors = graph.lanes[(size_t) graph.pipeLane].processors;
    int next = 0;

    while (! threadShouldExit() && ! graph.pipeShouldStop.load (std::memory_order_relaxed))
    {
        const auto epoch = graph.pipeEpoch.load (std::memory_order_acquire);
        auto& slot = graph.pipeSlots[(size_t) next];

        if (slot.state.load (std::memory_order_acquire) != PipeSlot::ready)
        {
            graph.pipeEpoch.wait (epoch); // futex sleep; the audio thread bumps the epoch after marking a slot ready
            continue;
        }

        // A view over the slot's memory, sized to this block (the slot itself is allocated for the largest block).
        juce::AudioBuffer<float> view (slot.buffer.getArrayOfWritePointers(), slot.buffer.getNumChannels(), slot.numSamples);
        for (size_t i = (size_t) slot.split; i < processors.size() && (int) i < slot.end; ++i)
        {
            const auto start = juce::Time::getHighResolutionTicks();
            processors[i]->processWithBypass (view);
            const auto us = (float) (juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start) * 1.0e6);
            auto& cost = graph.pipeProcessorCost[i];
            cost.store (cost.load (std::memory_order_relaxed) * 0.97f + us * 0.03f, std::memory_order_relaxed);
        }

        slot.state.store (PipeSlot::done, std::memory_order_release);
        next = (next + 1) % pipeDepth;
    }
}

void SignalGraph::rebalancePipeline()
{
    const auto n = pipeProcessorCost.size();
    if (n < 2)
        return;

    std::vector<float> cost (n);
    float total = 0.0f;
    for (size_t i = 0; i < n; ++i)
    {
        cost[i] = pipeProcessorCost[i].load (std::memory_order_relaxed);
        total += cost[i];
    }
    if (total <= 0.0f)
        return;

    auto worst = [&] (size_t split)
    {
        float a = 0.0f;
        for (size_t i = 0; i < split; ++i)
            a += cost[i];
        return std::max (a, total - a);
    };

    size_t best = (size_t) pipeSplit;
    float bestWorst = worst (best);
    for (size_t s = 1; s < n; ++s)
        if (worst (s) < bestWorst * 0.9f) // hysteresis: only move for a real (10%) gain
        {
            best = s;
            bestWorst = worst (s);
        }

    pipeSplit = (int) best;
}

void SignalGraph::processSend (juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    auto& lane = lanes[(size_t) pipeLane];
    auto& processors = lane.processors;
    const size_t workerFirst = pipeSendHead ? 0 : (size_t) pipeSplit;
    const size_t workerEnd = pipeSendHead ? (size_t) pipeSplit : processors.size();

    loadLaneInput (pipeLane, numSamples);

    // Tail send: the dry part of the chain runs here first, with no delay. Head send: the synths see the clean input.
    if (! pipeSendHead)
        for (size_t i = 0; i < workerFirst; ++i)
            processors[i]->processWithBypass (lane.buffer);

    // What the worker's blocks pass through untouched right now; the rest of their output is the wet part that arrives
    // two callbacks late: y = g*x + wet, so wet = y - g*x (with g read when the block was handed over).
    float dryGain = 1.0f;
    for (size_t i = workerFirst; i < workerEnd; ++i)
        dryGain *= processors[i]->isBypassed() ? 1.0f : processors[i]->parallelDryGain(); // bypassed: the dry passes as it is

    const int channels = pipeLastOutput.getNumChannels();
    for (int ch = 0; ch < channels; ++ch)
    {
        pipeLastOutput.copyFrom (ch, 0, lane.buffer, ch, 0, numSamples);
        pipeLastOutput.applyGain (ch, 0, numSamples, dryGain);
    }

    if (pipeInFlight >= pipeDepth - 1)
    {
        auto& oldest = pipeSlots[(size_t) pipeReadSlot];
        if (oldest.state.load (std::memory_order_acquire) == PipeSlot::done)
        {
            if (oldest.numSamples == numSamples)
                for (int ch = 0; ch < channels; ++ch)
                {
                    auto* out = pipeLastOutput.getWritePointer (ch);
                    juce::FloatVectorOperations::add (out, oldest.buffer.getReadPointer (ch), numSamples);
                    juce::FloatVectorOperations::addWithMultiply (out, oldest.dry.getReadPointer (ch), -oldest.dryGain, numSamples);
                }
            oldest.state.store (PipeSlot::free, std::memory_order_release);
            pipeReadSlot = (pipeReadSlot + 1) % pipeDepth;
            --pipeInFlight;
        }
        else
            pipeLateBlocks.fetch_add (1, std::memory_order_relaxed); // no wet this block; the dry part is untouched
    }

    // Hand this block's input to the worker (it runs its range in place on `buffer`; `dry` keeps the original).
    if (pipeInFlight < pipeDepth)
    {
        auto& slot = pipeSlots[(size_t) pipeWriteSlot];
        for (int ch = 0; ch < slot.buffer.getNumChannels(); ++ch)
        {
            slot.buffer.copyFrom (ch, 0, lane.buffer, ch, 0, numSamples);
            slot.dry.copyFrom (ch, 0, lane.buffer, ch, 0, numSamples);
        }
        slot.numSamples = numSamples;
        slot.split = (int) workerFirst;
        slot.end = (int) workerEnd;
        slot.dryGain = dryGain;
        slot.state.store (PipeSlot::ready, std::memory_order_release);
        pipeWriteSlot = (pipeWriteSlot + 1) % pipeDepth;
        ++pipeInFlight;
        pipeEpoch.fetch_add (1, std::memory_order_release);
        pipeEpoch.notify_one();
    }
    else
        pipeLateBlocks.fetch_add (1, std::memory_order_relaxed);

    // Head send: the rest of the chain runs here on dry + the synths' wet part.
    if (pipeSendHead)
    {
        for (int ch = 0; ch < channels; ++ch)
            lane.buffer.copyFrom (ch, 0, pipeLastOutput, ch, 0, numSamples);
        for (size_t i = workerEnd; i < processors.size(); ++i)
            processors[i]->processWithBypass (lane.buffer);
        for (int ch = 0; ch < channels; ++ch)
            pipeLastOutput.copyFrom (ch, 0, lane.buffer, ch, 0, numSamples);
    }

    writeDeviceOutput (buffer, numSamples, &pipeLastOutput);
}

void SignalGraph::processPipelined (juce::AudioBuffer<float>& buffer)
{
    if (pipeSendMode)
    {
        processSend (buffer);
        return;
    }

    const int numSamples = buffer.getNumSamples();
    auto& lane = lanes[(size_t) pipeLane];
    auto& processors = lane.processors;

    // The live input must be read before `buffer` is overwritten with this callback's output.
    loadLaneInput (pipeLane, numSamples);

    // 1. Output: the block handed over two callbacks ago, if the worker finished it.
    if (pipeInFlight >= pipeDepth - 1)
    {
        auto& oldest = pipeSlots[(size_t) pipeReadSlot];
        if (oldest.state.load (std::memory_order_acquire) == PipeSlot::done)
        {
            if (oldest.numSamples == numSamples)
                for (int ch = 0; ch < pipeLastOutput.getNumChannels(); ++ch)
                    pipeLastOutput.copyFrom (ch, 0, oldest.buffer, ch, 0, numSamples);
            oldest.state.store (PipeSlot::free, std::memory_order_release);
            pipeReadSlot = (pipeReadSlot + 1) % pipeDepth;
            --pipeInFlight;
        }
        else
            pipeLateBlocks.fetch_add (1, std::memory_order_relaxed); // repeat the previous output
    }

    writeDeviceOutput (buffer, numSamples, &pipeLastOutput);

    // 2. Move the cut if the worker is idle (no processor may be touched by both threads at once).
    if (++pipeBlocksSinceRebalance >= 64)
    {
        bool workerIdle = true;
        for (auto& slot : pipeSlots)
            workerIdle = workerIdle && slot.state.load (std::memory_order_acquire) != PipeSlot::ready;
        if (workerIdle)
        {
            pipeBlocksSinceRebalance = 0;
            rebalancePipeline();
        }
    }

    // 3. Stage A here, then hand the block over.
    const int split = juce::jlimit (1, (int) processors.size() - 1, pipeSplit);
    for (int i = 0; i < split; ++i)
    {
        const auto start = juce::Time::getHighResolutionTicks();
        processors[(size_t) i]->processWithBypass (lane.buffer);
        const auto us = (float) (juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start) * 1.0e6);
        auto& cost = pipeProcessorCost[(size_t) i];
        cost.store (cost.load (std::memory_order_relaxed) * 0.97f + us * 0.03f, std::memory_order_relaxed);
    }

    if (pipeInFlight < pipeDepth)
    {
        auto& slot = pipeSlots[(size_t) pipeWriteSlot];
        for (int ch = 0; ch < slot.buffer.getNumChannels(); ++ch)
            slot.buffer.copyFrom (ch, 0, lane.buffer, ch, 0, numSamples);
        slot.numSamples = numSamples;
        slot.split = split;
        slot.end = 1 << 30;
        slot.state.store (PipeSlot::ready, std::memory_order_release);
        pipeWriteSlot = (pipeWriteSlot + 1) % pipeDepth;
        ++pipeInFlight;
        pipeEpoch.fetch_add (1, std::memory_order_release);
        pipeEpoch.notify_one();
    }
    else
        pipeLateBlocks.fetch_add (1, std::memory_order_relaxed); // every slot busy: this block is dropped
}

void SignalGraph::reset()
{
    for (auto& lane : lanes)
        for (auto* p : lane.processors)
            p->reset();
}

} // namespace openguitarmultifx
