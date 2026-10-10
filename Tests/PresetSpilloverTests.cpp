#include "Engine/Spillover.h"
#include "Engine/SignalGraph.h"

#include <atomic>
#include <cstdlib>
#include <new>

// Scoped allocation counter for the realtime-safety test: armed ONLY
// around Spillover::process() calls, so it reports exactly what that call
// allocated. The runner is serial -- nothing else allocates meanwhile.
namespace
{
    std::atomic<bool> countingAllocs { false };
    std::atomic<int> allocCount { 0 };
}

void* operator new (std::size_t size)
{
    if (countingAllocs.load (std::memory_order_relaxed))
        allocCount.fetch_add (1, std::memory_order_relaxed);
    if (auto* p = std::malloc (size))
        return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t size) { return operator new (size); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace openguitarmultifx
{

namespace
{
    /** Dry + one feedback echo, with trails -- a minimal delay pedal
        (same shape as BypassCrossfadeTests' EchoProcessor). */
    class EchoProcessor : public EffectProcessor
    {
    public:
        EchoProcessor (int delay = 2400, double latency = 0.0)
            : delaySamples (delay), latencySeconds (latency)
        {
        }

        void prepare (double, int, int) override { line.assign ((size_t) delaySamples, 0.0f); }
        void process (juce::AudioBuffer<float>& buffer) override
        {
            ++processCalls;
            auto* data = buffer.getWritePointer (0);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const float echo = line[(size_t) pos];
                line[(size_t) pos] = data[i] + 0.5f * echo;
                data[i] += echo;
                pos = (pos + 1) % delaySamples;
            }
        }
        void reset() override {}
        juce::AudioProcessorParameterGroup* getParameters() override { return nullptr; }
        const char* getName() const override { return "Echo"; }
        bool hasTrails() const override { return true; }
        double trailLatencySeconds() const override { return latencySeconds; }

        const int delaySamples;
        int processCalls = 0;

    private:
        const double latencySeconds;
        std::vector<float> line;
        int pos = 0;
    };

    /** Fixed gain, no trails -- a plain non-trail block. */
    class GainProcessor : public EffectProcessor
    {
    public:
        void prepare (double, int, int) override {}
        void process (juce::AudioBuffer<float>& buffer) override { ++processCalls; buffer.applyGain (0.5f); }
        void reset() override {}
        juce::AudioProcessorParameterGroup* getParameters() override { return nullptr; }
        const char* getName() const override { return "Gain"; }

        int processCalls = 0;
    };

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 128;

    /** Charges a tail into `p` exactly the way the live chain would: one
        impulse through a prepared SignalGraph on the live input. */
    void chargeTail (EffectProcessor* p)
    {
        SignalGraph live;
        live.setLaneProcessors (0, { p });
        live.setLaneReadsDeviceInput (0, true);
        live.setLaneWritesDeviceOutput (0, true);
        live.prepare (sampleRate, blockSize, 1);

        juce::AudioBuffer<float> impulse (1, blockSize);
        impulse.clear();
        impulse.setSample (0, 0, 1.0f);
        live.process (impulse);
    }

    using Lanes = std::array<Spillover::LaneSpec, SignalGraph::numLanes>;

    /** One unit wrapping `p` (which must already hold a tail) sitting in
        lane 0 wired straight to the device output. */
    std::unique_ptr<Spillover::Unit> makeUnit (std::unique_ptr<EffectProcessor>& p)
    {
        Lanes lanes;
        lanes[0].processors.push_back (p.get());
        lanes[0].writesDeviceOutput = true;

        std::vector<std::unique_ptr<EffectProcessor>> retired;
        retired.push_back (std::move (p));

        auto unit = Spillover::build (lanes, {}, retired);
        unit->prepare (sampleRate, blockSize, 1);
        return unit;
    }

    /** Runs `numBlocks` audio blocks through the spill, summing its output
        on top of a constant "new preset" signal, appending the result. */
    void runSpill (Spillover& spill, int numBlocks, std::vector<float>& out)
    {
        juce::AudioBuffer<float> buffer (1, blockSize);
        for (int b = 0; b < numBlocks; ++b)
        {
            buffer.clear();
            spill.process (buffer, sampleRate);
            for (int i = 0; i < blockSize; ++i)
                out.push_back (buffer.getSample (0, i));
        }
    }
}

class PresetSpilloverTests : public juce::UnitTest
{
public:
    PresetSpilloverTests() : juce::UnitTest ("PresetSpillover", "Engine") {}

    void runTest() override
    {
        beginTest ("a preset switch keeps the delay tail ringing, then the spill retires");
        {
            std::unique_ptr<EffectProcessor> echo = std::make_unique<EchoProcessor>();
            auto* echoRaw = static_cast<EchoProcessor*> (echo.get());
            echo->prepareIfNeeded (sampleRate, blockSize, 1);
            chargeTail (echoRaw);

            Spillover spill;
            spill.add (makeUnit (echo));

            std::vector<float> out;
            runSpill (spill, (int) (3.0 * sampleRate) / blockSize, out);

            // The echoes the impulse left behind keep coming on schedule --
            // the tail resumes mid-line, so the first echo lands
            // delaySamples - blockSize samples into the spill, then every
            // delaySamples after that, halving each time.
            expectWithinAbsoluteError (out[(size_t) echoRaw->delaySamples - blockSize], 1.0f, 1.0e-4f);
            expectWithinAbsoluteError (out[(size_t) echoRaw->delaySamples * 2 - blockSize], 0.5f, 1.0e-4f);

            // Then it went silent, the unit retired, and the processor
            // stopped being called (a dead spill costs nothing).
            const int callsWhenDone = echoRaw->processCalls;
            runSpill (spill, 8, out);
            expectEquals (echoRaw->processCalls, callsWhenDone);

            spill.sweep();
            expectEquals (spill.numUnitsForTesting(), 0);
        }

        beginTest ("a delay longer than trailsSilenceSeconds keeps its pending repeat (trailLatencySeconds)");
        {
            // 1.2 s echo: the output sits quiet for a whole second mid-tail
            // -- longer than the plain 0.5 s window -- but the repeat is
            // still pending inside the delay line and must be played.
            constexpr int longDelay = (int) (1.2 * sampleRate); // 57600
            std::unique_ptr<EffectProcessor> echo = std::make_unique<EchoProcessor> (longDelay, 1.2);
            auto* echoRaw = static_cast<EchoProcessor*> (echo.get());
            echo->prepareIfNeeded (sampleRate, blockSize, 1);
            chargeTail (echoRaw);

            Spillover spill;
            auto unit = makeUnit (echo);
            auto* unitRaw = unit.get();
            spill.add (std::move (unit));

            // 0.7 s in: older code would already have retired this unit.
            std::vector<float> out;
            runSpill (spill, (int) (0.7 * sampleRate) / blockSize, out);
            expect (! unitRaw->finished);
            expect (! unitRaw->done.load());

            // The repeat lands delaySamples - blockSize into the spill
            // (the charged block already consumed 128 positions).
            runSpill (spill, (int) (0.7 * sampleRate) / blockSize, out);
            expectWithinAbsoluteError (out[(size_t) longDelay - blockSize], 1.0f, 1.0e-4f);
        }

        beginTest ("serial delays' combined latency keeps a repeat pending in the last one");
        {
            // Two 1.0 s echoes in one lane's chain: the impulse's first
            // repeat leaves echo1 at ~1 s, enters echo2, and only reaches
            // the output at ~2 s -- past 0.5 + max(1.0) but inside
            // 0.5 + 1.0 + 1.0.
            constexpr int oneSec = (int) sampleRate; // 48000
            std::unique_ptr<EffectProcessor> echo1 = std::make_unique<EchoProcessor> (oneSec, 1.0);
            std::unique_ptr<EffectProcessor> echo2 = std::make_unique<EchoProcessor> (oneSec, 1.0);
            auto* echo1Raw = static_cast<EchoProcessor*> (echo1.get());
            echo1->prepareIfNeeded (sampleRate, blockSize, 1);
            echo2->prepareIfNeeded (sampleRate, blockSize, 1);
            chargeTail (echo1Raw);

            Lanes lanes;
            lanes[0].processors = { echo1Raw, echo2.get() };
            lanes[0].writesDeviceOutput = true;

            std::vector<std::unique_ptr<EffectProcessor>> retired;
            retired.push_back (std::move (echo1));
            retired.push_back (std::move (echo2));

            auto unit = Spillover::build (lanes, {}, retired);
            unit->prepare (sampleRate, blockSize, 1);
            auto* unitRaw = unit.get();

            Spillover spill;
            spill.add (std::move (unit));

            std::vector<float> out;
            runSpill (spill, (int) (1.6 * sampleRate) / blockSize, out);
            expect (! unitRaw->finished); // a max-only window would have retired it

            runSpill (spill, (int) (0.8 * sampleRate) / blockSize, out);
            // Impulse spent one block in the live graph before the switch,
            // so echo2 releases the stored repeat 2*oneSec - blockSize into
            // the spill -- the same block where echo1's own 0.5-feedback
            // second repeat fires. Output = echo1 passthrough (0.5) +
            // echo2's echo (1.0) = 1.5. The point: it isn't silence.
            expectWithinAbsoluteError (out[(size_t) oneSec * 2 - blockSize], 1.5f, 1.0e-4f);
        }

        beginTest ("a tail relays through an emptied lane that writes device output");
        {
            // Outgoing preset: lane0 echo -> lane1 amp (non-trail) -> device.
            // The amp is cut at the switch, but lane1 kept its output flag:
            // the echo's tail must still reach the device through it.
            std::unique_ptr<EffectProcessor> echo = std::make_unique<EchoProcessor>();
            std::unique_ptr<EffectProcessor> gain = std::make_unique<GainProcessor>();
            auto* echoRaw = static_cast<EchoProcessor*> (echo.get());
            echo->prepareIfNeeded (sampleRate, blockSize, 1);
            chargeTail (echoRaw);

            Lanes lanes;
            lanes[0].processors = { echoRaw };
            lanes[1].processors = { gain.get() }; // dropped: no trails
            lanes[1].writesDeviceOutput = true;

            std::vector<std::unique_ptr<EffectProcessor>> retired;
            retired.push_back (std::move (echo));
            retired.push_back (std::move (gain));

            auto unit = Spillover::build (lanes, { { 0, 1 } }, retired);
            unit->prepare (sampleRate, blockSize, 1);

            Spillover spill;
            spill.add (std::move (unit));

            std::vector<float> out;
            runSpill (spill, (int) (0.3 * sampleRate) / blockSize, out);
            expectWithinAbsoluteError (out[(size_t) echoRaw->delaySamples - blockSize], 1.0f, 1.0e-4f);
        }

        beginTest ("the new preset's output is added to the tail, not fed through the old chain");
        {
            std::unique_ptr<EffectProcessor> echo = std::make_unique<EchoProcessor>();
            echo->prepareIfNeeded (sampleRate, blockSize, 1);
            chargeTail (echo.get());

            Spillover spill;
            spill.add (makeUnit (echo));

            // Simulate the new chain's output already sitting in the
            // buffer: the tail must sum on top of it, not process it.
            juce::AudioBuffer<float> buffer (1, blockSize);
            buffer.clear();
            for (int i = 0; i < blockSize; ++i)
                buffer.setSample (0, i, 0.25f);
            spill.process (buffer, sampleRate);

            expectWithinAbsoluteError (buffer.getSample (0, 0), 0.25f, 1.0e-6f);
            const float tail = buffer.getSample (0, 0) - 0.25f;
            expectWithinAbsoluteError (tail, 0.0f, 1.0e-6f); // first echo lands delaySamples later
        }

        beginTest ("a non-trail block is left out of the spill and cut at the switch");
        {
            std::unique_ptr<EffectProcessor> gain = std::make_unique<GainProcessor>();
            std::unique_ptr<EffectProcessor> echo = std::make_unique<EchoProcessor>();
            auto* gainRaw = gain.get();
            auto* echoRaw = echo.get();
            echo->prepareIfNeeded (sampleRate, blockSize, 1);
            chargeTail (echoRaw);

            Lanes lanes;
            lanes[0].processors = { gainRaw, echoRaw };
            lanes[0].writesDeviceOutput = true;

            std::vector<std::unique_ptr<EffectProcessor>> retired;
            retired.push_back (std::move (gain));
            retired.push_back (std::move (echo));

            auto unit = Spillover::build (lanes, {}, retired);

            // The trails block moved into the unit; the non-trail one
            // stayed in the retired pool (back to the ~600 ms graveyard).
            expect (unit != nullptr);
            expectEquals ((int) unit->owners.size(), 1);
            expect (unit->owners[0].get() == echoRaw);
            expect (retired[0] != nullptr);
            expect (retired[0].get() == gainRaw);
            expect (retired[1] == nullptr);

            // And an outgoing chain with no trails at all spills nothing.
            Lanes noTrails;
            std::unique_ptr<EffectProcessor> gain2 = std::make_unique<GainProcessor>();
            noTrails[1].processors = { gain2.get() };
            std::vector<std::unique_ptr<EffectProcessor>> retired2;
            retired2.push_back (std::move (gain2));
            expect (Spillover::build (noTrails, {}, retired2) == nullptr);
        }

        beginTest ("a tail still flows through a now-empty lane into a downstream trails block");
        {
            std::unique_ptr<EffectProcessor> echo = std::make_unique<EchoProcessor>();
            std::unique_ptr<EffectProcessor> echo2 = std::make_unique<EchoProcessor>();
            auto* echo2Raw = echo2.get();
            echo->prepareIfNeeded (sampleRate, blockSize, 1);
            chargeTail (echo.get());

            // Outgoing preset: lane0 echo -> lane1 (a non-trail block) ->
            // lane2 echo2. Only the echoes have trails.
            Lanes lanes;
            lanes[0].processors = { echo.get() };
            lanes[2].processors = { echo2Raw };
            lanes[2].writesDeviceOutput = true;

            std::vector<std::unique_ptr<EffectProcessor>> retired;
            retired.push_back (std::move (echo));
            retired.push_back (std::move (echo2));

            auto unit = Spillover::build (lanes, { { 0, 1 }, { 1, 2 } }, retired);
            unit->prepare (sampleRate, blockSize, 1);

            Spillover spill;
            spill.add (std::move (unit));

            std::vector<float> out;
            runSpill (spill, (int) (0.3 * sampleRate) / blockSize, out);

            // echo's tail reached echo2 through the empty middle lane:
            // echo2 kept processing and passed it through (its own echo
            // line was empty, so the first repeat comes out unchanged).
            expectGreaterThan (static_cast<EchoProcessor*> (echo2Raw)->processCalls, 0);
            expectWithinAbsoluteError (out[(size_t) static_cast<EchoProcessor*> (echo2Raw)->delaySamples - blockSize], 1.0f, 1.0e-4f);
        }

        beginTest ("only maxActiveUnits spills run at once -- the oldest is retired");
        {
            std::unique_ptr<EffectProcessor> e1 = std::make_unique<EchoProcessor>();
            std::unique_ptr<EffectProcessor> e2 = std::make_unique<EchoProcessor>();
            std::unique_ptr<EffectProcessor> e3 = std::make_unique<EchoProcessor>();
            for (auto* e : { e1.get(), e2.get(), e3.get() })
            {
                e->prepareIfNeeded (sampleRate, blockSize, 1);
                chargeTail (e);
            }

            Spillover spill;
            auto u1 = makeUnit (e1);
            auto* u1Raw = u1.get();
            spill.add (std::move (u1));
            std::vector<float> out;
            runSpill (spill, 1, out); // u1 picked up

            auto u2 = makeUnit (e2);
            auto* u2Raw = u2.get();
            spill.add (std::move (u2));
            runSpill (spill, 1, out); // u2 picked up, both active

            auto u3 = makeUnit (e3);
            auto* u3Raw = u3.get();
            spill.add (std::move (u3));
            runSpill (spill, 1, out); // u3 picked up: u1 was the oldest

            expect (u1Raw->done.load());
            expect (! u2Raw->done.load());
            expect (! u3Raw->done.load());

            spill.sweep();
            expectEquals (spill.numUnitsForTesting(), 2);
        }

        beginTest ("a spill that never gets picked up is displaced by the next one");
        {
            std::unique_ptr<EffectProcessor> e1 = std::make_unique<EchoProcessor>();
            std::unique_ptr<EffectProcessor> e2 = std::make_unique<EchoProcessor>();
            for (auto* e : { e1.get(), e2.get() })
            {
                e->prepareIfNeeded (sampleRate, blockSize, 1);
                chargeTail (e);
            }

            Spillover spill;
            auto u1 = makeUnit (e1);
            auto u2 = makeUnit (e2);
            auto* u1Raw = u1.get();
            auto* u2Raw = u2.get();
            spill.add (std::move (u1));
            spill.add (std::move (u2)); // u1 never heard: newest pending wins

            expect (u1Raw->done.load());
            expect (! u2Raw->done.load());
        }

        beginTest ("Spillover::process never allocates on the audio thread");
        {
            std::unique_ptr<EffectProcessor> echo = std::make_unique<EchoProcessor>();
            echo->prepareIfNeeded (sampleRate, blockSize, 1);
            chargeTail (echo.get());

            Spillover spill;
            spill.add (makeUnit (echo));

            // Sanity: the counter really is armed. A literal `new char`/
            // `delete` pair can be optimised away entirely (new-expression
            // elision), so force an allocation that must reach operator
            // new: a vector's heap block.
            countingAllocs.store (true);
            allocCount.store (0);
            { std::vector<char> sink (37, 1); expectEquals ((int) sink.size(), 37); }
            expectEquals (allocCount.load(), 1);

            allocCount.store (0);
            juce::AudioBuffer<float> buffer (1, blockSize);
            for (int b = 0; b < 64; ++b)
            {
                buffer.clear();
                spill.process (buffer, sampleRate); // includes the pending pick-up
            }
            expectEquals (allocCount.load(), 0);
            countingAllocs.store (false);
        }
    }
};

static PresetSpilloverTests presetSpilloverTests;

} // namespace openguitarmultifx
