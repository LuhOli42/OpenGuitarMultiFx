#include "Engine/OutputMatrix.h"

#include <juce_core/juce_core.h>

namespace openguitarmultifx
{

namespace
{
    class TestGainProcessor : public EffectProcessor
    {
    public:
        explicit TestGainProcessor (float gainToApply) : gain (gainToApply) {}

        void prepare (double, int, int) override {}

        void process (juce::AudioBuffer<float>& buffer) override
        {
            buffer.applyGain (gain);
        }

        void reset() override {}
        juce::AudioProcessorParameterGroup* getParameters() override { return nullptr; }
        std::unique_ptr<juce::XmlElement> getState() const override { return nullptr; }
        void setState (const juce::XmlElement&) override {}
        const char* getName() const override { return "TestGain"; }

        float gain;
    };

    void fillOnes (juce::AudioBuffer<float>& buffer)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int s = 0; s < buffer.getNumSamples(); ++s)
                buffer.setSample (ch, s, 1.0f);
    }

    void expectSample (juce::UnitTest& test, const juce::AudioBuffer<float>& buffer, int channel, float expected)
    {
        test.expectWithinAbsoluteError (buffer.getSample (channel, 0), expected, 1.0e-5f);
        test.expectWithinAbsoluteError (buffer.getSample (channel, buffer.getNumSamples() - 1), expected, 1.0e-5f);
    }
}

class OutputMatrixTests : public juce::UnitTest
{
public:
    OutputMatrixTests() : juce::UnitTest ("OutputMatrix", "Engine") {}

    void runTest() override
    {
        testRoutesLanesToDifferentPairs();
        testSamePairSums();
        testMonoLaneDuplicatesToBothPairChannels();
        testRejectsInvalidRoutes();
    }

private:
    void testRoutesLanesToDifferentPairs()
    {
        beginTest ("routes lanes to different pairs");
        TestGainProcessor half (0.5f), quarter (0.25f);

        SignalGraph graph;
        graph.setLaneReadsDeviceInput (0, true);
        graph.setLaneProcessors (0, { &half });
        graph.setLaneWritesDeviceOutput (0, true);
        graph.setLaneReadsDeviceInput (1, true);
        graph.setLaneProcessors (1, { &quarter });
        graph.setLaneWritesDeviceOutput (1, true);
        graph.prepare (48000.0, 128, 2);

        juce::AudioBuffer<float> device (6, 8);
        fillOnes (device);
        graph.processLanes (device);

        OutputMatrix matrix;
        expect (matrix.addRoute (0, 0));
        expect (matrix.addRoute (1, 4));
        expectEquals (matrix.numRoutes(), 2);
        matrix.render (device, graph);

        expectSample (*this, device, 0, 0.5f);
        expectSample (*this, device, 1, 0.5f);
        expectSample (*this, device, 2, 0.0f);
        expectSample (*this, device, 3, 0.0f);
        expectSample (*this, device, 4, 0.25f);
        expectSample (*this, device, 5, 0.25f);
    }

    void testSamePairSums()
    {
        beginTest ("same pair sums");
        TestGainProcessor half (0.5f), quarter (0.25f);

        SignalGraph graph;
        graph.setLaneReadsDeviceInput (0, true);
        graph.setLaneProcessors (0, { &half });
        graph.setLaneReadsDeviceInput (1, true);
        graph.setLaneProcessors (1, { &quarter });
        graph.prepare (48000.0, 128, 2);

        juce::AudioBuffer<float> device (4, 8);
        fillOnes (device);
        graph.processLanes (device);

        OutputMatrix matrix;
        expect (matrix.addRoute (0, 0));
        expect (matrix.addRoute (1, 0));
        matrix.render (device, graph);

        expectSample (*this, device, 0, 0.75f);
        expectSample (*this, device, 1, 0.75f);
        expectSample (*this, device, 2, 0.0f);
        expectSample (*this, device, 3, 0.0f);
    }

    void testMonoLaneDuplicatesToBothPairChannels()
    {
        beginTest ("mono lane duplicates to both pair channels");
        TestGainProcessor half (0.5f);

        SignalGraph graph;
        graph.setLaneReadsDeviceInput (0, true);
        graph.setLaneProcessors (0, { &half });
        graph.prepare (48000.0, 128, 1);

        juce::AudioBuffer<float> device (2, 8);
        fillOnes (device);
        graph.processLanes (device);

        OutputMatrix matrix;
        expect (matrix.addRoute (0, 0));
        matrix.render (device, graph);

        expectSample (*this, device, 0, 0.5f);
        expectSample (*this, device, 1, 0.5f);
    }

    void testRejectsInvalidRoutes()
    {
        beginTest ("rejects invalid routes");
        OutputMatrix matrix;
        expect (! matrix.addRoute (-1, 0));
        expect (! matrix.addRoute (0, 1));
        expect (! matrix.hasRoutes());
        expect (matrix.addRoute (0, 4));
        expect (matrix.hasRoutes());
        expectEquals (matrix.numRoutes(), 1);
    }
};

static OutputMatrixTests outputMatrixTests;

} // namespace openguitarmultifx
