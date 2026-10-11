#include "Engine/SignalGraph.h"

#include <juce_core/juce_core.h>

namespace openguitarmultifx
{

namespace
{
    /** Minimal test-only processor: applies a fixed gain and counts calls. */
    class TestGainProcessor : public EffectProcessor
    {
    public:
        explicit TestGainProcessor (float gainToApply) : gain (gainToApply) {}

        void prepare (double, int, int) override { ++prepareCalls; }

        void process (juce::AudioBuffer<float>& buffer) override
        {
            ++processCalls;
            buffer.applyGain (gain);
        }

        void reset() override { ++resetCalls; }

        juce::AudioProcessorParameterGroup* getParameters() override { return nullptr; }
        std::unique_ptr<juce::XmlElement> getState() const override { return nullptr; }
        void setState (const juce::XmlElement&) override {}
        const char* getName() const override { return "TestGain"; }

        float gain;
        int prepareCalls = 0, processCalls = 0, resetCalls = 0;
    };

    /** Gain processor that burns a fixed amount of CPU per block, so a
        worker thread reliably gets to its job before the audio thread
        could ever finish the other lane and claim it back. */
    class SlowGainProcessor : public TestGainProcessor
    {
    public:
        SlowGainProcessor (float gainToApply, double busyMs) : TestGainProcessor (gainToApply), ms (busyMs) {}

        void process (juce::AudioBuffer<float>& buffer) override
        {
            const auto deadline = juce::Time::getMillisecondCounterHiRes() + ms;
            volatile float sink = 0.0f; // NOLINT -- deliberate busy work, this is a test double
            while (juce::Time::getMillisecondCounterHiRes() < deadline)
                sink += 1.0e-7f;
            (void) sink;
            TestGainProcessor::process (buffer);
        }

        double ms;
    };

    /** Records the first sample of every block it sees, so a test can tell
        in which order the same instance ran on different lanes. */
    class EntrySamplerProcessor : public EffectProcessor
    {
    public:
        void prepare (double, int, int) override {}

        void process (juce::AudioBuffer<float>& buffer) override
        {
            if (buffer.getNumSamples() > 0)
                seen.push_back (buffer.getSample (0, 0));
        }

        void reset() override {}

        juce::AudioProcessorParameterGroup* getParameters() override { return nullptr; }
        std::unique_ptr<juce::XmlElement> getState() const override { return nullptr; }
        void setState (const juce::XmlElement&) override {}
        const char* getName() const override { return "EntrySampler"; }

        std::vector<float> seen;
    };
}

class SignalGraphTests : public juce::UnitTest
{
public:
    SignalGraphTests() : juce::UnitTest ("SignalGraph", "Engine") {}

    void runTest() override
    {
        beginTest ("a single lane, reading input and writing output, runs its processors in series");
        {
            TestGainProcessor a (0.5f), b (0.5f);
            SignalGraph graph;
            graph.setLaneProcessors (0, { &a, &b });
            graph.setLaneReadsDeviceInput (0, true);
            graph.setLaneWritesDeviceOutput (0, true);
            graph.prepare (48000.0, 128, 1);

            juce::AudioBuffer<float> buffer (1, 4);
            buffer.clear();
            buffer.setSample (0, 0, 1.0f);

            graph.process (buffer);

            expectWithinAbsoluteError (buffer.getSample (0, 0), 0.25f, 1.0e-6f);
        }

        beginTest ("a bypassed processor does not run process()");
        {
            TestGainProcessor p (0.0f);
            p.setBypassed (true);
            SignalGraph graph;
            graph.setLaneProcessors (0, { &p });
            graph.setLaneReadsDeviceInput (0, true);
            graph.setLaneWritesDeviceOutput (0, true);
            graph.prepare (48000.0, 128, 1);

            juce::AudioBuffer<float> buffer (1, 4);
            buffer.clear();
            buffer.setSample (0, 0, 1.0f);

            graph.process (buffer);

            expectEquals (p.processCalls, 0);
            expectWithinAbsoluteError (buffer.getSample (0, 0), 1.0f, 1.0e-6f);
        }

        beginTest ("an empty graph (nothing reads input or writes output) leaves the buffer untouched (passthrough)");
        {
            SignalGraph graph;
            graph.prepare (48000.0, 128, 1);

            juce::AudioBuffer<float> buffer (1, 4);
            buffer.clear();
            buffer.setSample (0, 0, 0.75f);

            graph.process (buffer);

            expectWithinAbsoluteError (buffer.getSample (0, 0), 0.75f, 1.0e-6f);
        }

        beginTest ("a lane that reads input but writes nowhere still leaves the buffer untouched (passthrough)");
        {
            // Regression case for the "only overwrite the buffer if SOMETHING
            // writes to device output" rule -- a lane silently running with
            // nowhere for its output to go must not still clear the buffer.
            TestGainProcessor p (0.5f);
            SignalGraph graph;
            graph.setLaneProcessors (0, { &p });
            graph.setLaneReadsDeviceInput (0, true);
            graph.setLaneWritesDeviceOutput (0, false);
            graph.prepare (48000.0, 128, 1);

            juce::AudioBuffer<float> buffer (1, 4);
            buffer.clear();
            buffer.setSample (0, 0, 0.75f);

            graph.process (buffer);

            expect (p.processCalls > 0); // the lane DID run...
            expectWithinAbsoluteError (buffer.getSample (0, 0), 0.75f, 1.0e-6f); // ...but the buffer is untouched
        }

        beginTest ("a split: one lane's output reaches two downstream lanes identically");
        {
            TestGainProcessor half (0.5f);
            SignalGraph graph;
            graph.setLaneReadsDeviceInput (0, true);
            graph.setLaneProcessors (0, { &half });
            graph.addLaneConnection (0, 1);
            graph.addLaneConnection (0, 2);
            graph.setLaneWritesDeviceOutput (1, true);
            graph.setLaneWritesDeviceOutput (2, true);
            graph.prepare (48000.0, 128, 1);

            juce::AudioBuffer<float> buffer (1, 4);
            buffer.clear();
            buffer.setSample (0, 0, 1.0f);

            graph.process (buffer);

            // Lane 0 halves the input (0.5), lanes 1 and 2 each receive that
            // same 0.5 and pass it through unmodified, both summed into the
            // output -- 0.5 + 0.5 = 1.0.
            expectWithinAbsoluteError (buffer.getSample (0, 0), 1.0f, 1.0e-6f);
        }

        beginTest ("a merge: two lanes' outputs are summed into a downstream lane");
        {
            TestGainProcessor quarterA (0.25f), quarterB (0.25f);
            SignalGraph graph;
            graph.setLaneReadsDeviceInput (0, true);
            graph.setLaneReadsDeviceInput (1, true);
            graph.setLaneProcessors (0, { &quarterA });
            graph.setLaneProcessors (1, { &quarterB });
            graph.addLaneConnection (0, 2);
            graph.addLaneConnection (1, 2);
            graph.setLaneWritesDeviceOutput (2, true);
            graph.prepare (48000.0, 128, 1);

            juce::AudioBuffer<float> buffer (1, 4);
            buffer.clear();
            buffer.setSample (0, 0, 1.0f);

            graph.process (buffer);

            // Lane 0 and lane 1 each independently reduce the SAME input to
            // 0.25; lane 2 receives and sums both -- 0.25 + 0.25 = 0.5, not
            // a serial 1.0 * 0.25 * 0.25 = 0.0625 (which is what the old
            // "flatten every branch into one serial chain" approximation
            // would have produced).
            expectWithinAbsoluteError (buffer.getSample (0, 0), 0.5f, 1.0e-6f);
        }

        beginTest ("reset() resets every lane's processors, not just one");
        {
            TestGainProcessor a (1.0f), b (1.0f);
            SignalGraph graph;
            graph.setLaneProcessors (0, { &a });
            graph.setLaneProcessors (2, { &b });
            graph.prepare (48000.0, 128, 1);

            graph.reset();

            expectEquals (a.resetCalls, 1);
            expectEquals (b.resetCalls, 1);
        }

        beginTest ("two independent lanes run in parallel and produce the serial result");
        {
            // Lane 0 is deliberately slow so the worker wakes and claims
            // lane 1 long before the audio thread could take it back. Several
            // blocks, not one: a worker's own thread startup can outlast a
            // single block under load -- claim-back covers it (that's its
            // job), later blocks are still run by the worker.
            SlowGainProcessor a (0.5f, 20.0);
            TestGainProcessor b (0.25f);
            SignalGraph graph;
            graph.setLaneReadsDeviceInput (0, true);
            graph.setLaneReadsDeviceInput (1, true);
            graph.setLaneProcessors (0, { &a });
            graph.setLaneProcessors (1, { &b });
            graph.setLaneWritesDeviceOutput (0, true);
            graph.setLaneWritesDeviceOutput (1, true);
            graph.prepare (48000.0, 128, 1);

            juce::AudioBuffer<float> buffer (1, 4);
            for (int block = 0; block < 32; ++block)
            {
                buffer.clear();
                buffer.setSample (0, 0, 1.0f);
                graph.process (buffer);

                // 1.0 * 0.5 + 1.0 * 0.25 -- identical to the serial order.
                expectWithinAbsoluteError (buffer.getSample (0, 0), 0.75f, 1.0e-6f);
            }
            expect (graph.getJobsRunByWorkers() > 0, "expected a worker to have run a lane");
        }

        beginTest ("parallel stages: a 2-lane merge feeding a downstream lane matches the serial result");
        {
            SlowGainProcessor quarterA (0.25f, 20.0);
            TestGainProcessor quarterB (0.25f);
            SignalGraph graph;
            graph.setLaneReadsDeviceInput (0, true);
            graph.setLaneReadsDeviceInput (1, true);
            graph.setLaneProcessors (0, { &quarterA });
            graph.setLaneProcessors (1, { &quarterB });
            graph.addLaneConnection (0, 2);
            graph.addLaneConnection (1, 2);
            graph.setLaneWritesDeviceOutput (2, true);
            graph.prepare (48000.0, 128, 1);

            juce::AudioBuffer<float> buffer (1, 4);
            for (int block = 0; block < 32; ++block)
            {
                buffer.clear();
                buffer.setSample (0, 0, 1.0f);
                graph.process (buffer);

                // Same merge math as the serial test above: 0.25 + 0.25 = 0.5.
                expectWithinAbsoluteError (buffer.getSample (0, 0), 0.5f, 1.0e-6f);
            }
            expect (graph.getJobsRunByWorkers() > 0, "expected a worker to have run a lane");
        }

        beginTest ("a processor instance shared across a stage's lanes forces that stage serial");
        {
            TestGainProcessor shared (0.5f);
            SignalGraph graph;
            graph.setLaneReadsDeviceInput (0, true);
            graph.setLaneReadsDeviceInput (1, true);
            graph.setLaneProcessors (0, { &shared });
            graph.setLaneProcessors (1, { &shared });
            graph.setLaneWritesDeviceOutput (0, true);
            graph.setLaneWritesDeviceOutput (1, true);
            graph.prepare (48000.0, 128, 1);

            juce::AudioBuffer<float> buffer (1, 4);
            buffer.clear();
            buffer.setSample (0, 0, 1.0f);

            graph.process (buffer);

            expectEquals (shared.processCalls, 2); // both lanes ran it, serially
            expectEquals (graph.getJobsRunByWorkers(), 0); // never dispatched to a worker
        }

        beginTest ("a processor shared across lanes at different levels keeps the legacy call order");
        {
            // rec sits on lane 1 (level 1, fed by lane 0's gain-1 output)
            // and lane 2 (level 0, after a gain-2). Stage scheduling alone
            // would run lane 2's call first; the shared-instance guard must
            // keep the old index order so rec sees [1.0, 2.0], not [2.0, 1.0].
            TestGainProcessor one (1.0f), two (2.0f);
            EntrySamplerProcessor rec;
            SignalGraph graph;
            graph.setLaneReadsDeviceInput (0, true);
            graph.setLaneProcessors (0, { &one });
            graph.addLaneConnection (0, 1);
            graph.setLaneProcessors (1, { &rec });
            graph.setLaneReadsDeviceInput (2, true);
            graph.setLaneProcessors (2, { &two, &rec });
            graph.setLaneWritesDeviceOutput (1, true);
            graph.setLaneWritesDeviceOutput (2, true);
            graph.prepare (48000.0, 128, 1);

            juce::AudioBuffer<float> buffer (1, 4);
            buffer.clear();
            buffer.setSample (0, 0, 1.0f);

            graph.process (buffer);

            expectEquals ((int) rec.seen.size(), 2);
            if (rec.seen.size() == 2)
            {
                expectWithinAbsoluteError (rec.seen[0], 1.0f, 1.0e-6f); // lane 1's call still first
                expectWithinAbsoluteError (rec.seen[1], 2.0f, 1.0e-6f); // then lane 2's
            }
            expectEquals (graph.getJobsRunByWorkers(), 0); // whole graph forced legacy serial
        }
    }
};

static SignalGraphTests signalGraphTests;

} // namespace openguitarmultifx
