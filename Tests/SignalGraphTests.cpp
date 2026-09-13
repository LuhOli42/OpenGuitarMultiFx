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
    }
};

static SignalGraphTests signalGraphTests;

} // namespace openguitarmultifx
