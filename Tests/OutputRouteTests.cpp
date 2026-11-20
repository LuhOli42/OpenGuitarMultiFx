#include "Engine/MasterEq.h"
#include "Engine/SignalGraph.h"
#include "Engine/Spillover.h"

#include <juce_core/juce_core.h>

namespace openguitarmultifx
{

namespace
{
    class RouteGain : public EffectProcessor
    {
    public:
        explicit RouteGain (float g, bool trails = false) : gain (g), tails (trails) {}
        void prepare (double, int, int) override {}
        void process (juce::AudioBuffer<float>& buffer) override { buffer.applyGain (gain); }
        void reset() override {}
        bool hasTrails() const override { return tails; }
        juce::AudioProcessorParameterGroup* getParameters() override { return nullptr; }
        std::unique_ptr<juce::XmlElement> getState() const override { return nullptr; }
        void setState (const juce::XmlElement&) override {}
        const char* getName() const override { return "RouteGain"; }
        float gain;
        bool tails;
    };

    /** A tail that rings: emits a constant regardless of input (a stand-in for a reverb fed silence). */
    class ConstantTail : public RouteGain
    {
    public:
        ConstantTail() : RouteGain (1.0f, true) {}
        void process (juce::AudioBuffer<float>& buffer) override
        {
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), 0.25f, buffer.getNumSamples());
        }
    };

    void fill (juce::AudioBuffer<float>& b, float v)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            juce::FloatVectorOperations::fill (b.getWritePointer (ch), v, b.getNumSamples());
    }
}

class OutputRouteTests : public juce::UnitTest
{
public:
    OutputRouteTests() : juce::UnitTest ("OutputRoute", "Engine") {}

    void runTest() override
    {
        using C = OutputRoute::Channels;

        beginTest ("route writes stereo to its pair, L/R alone as a mono fold, and nothing past the device");
        {
            juce::AudioBuffer<float> src (2, 8);
            juce::FloatVectorOperations::fill (src.getWritePointer (0), 1.0f, 8);
            juce::FloatVectorOperations::fill (src.getWritePointer (1), 0.5f, 8);

            juce::AudioBuffer<float> dev (4, 8);
            dev.clear();
            OutputRoute { 2, C::stereo }.addInto (dev, src, 8);
            expectEquals (dev.getSample (0, 3), 0.0f);
            expectEquals (dev.getSample (1, 3), 0.0f);
            expectEquals (dev.getSample (2, 3), 1.0f);
            expectEquals (dev.getSample (3, 3), 0.5f);

            dev.clear();
            OutputRoute { 0, C::left }.addInto (dev, src, 8);
            expectWithinAbsoluteError (dev.getSample (0, 0), 0.75f, 1.0e-6f);
            expectEquals (dev.getSample (1, 0), 0.0f);

            dev.clear();
            OutputRoute { 2, C::right }.addInto (dev, src, 8);
            expectWithinAbsoluteError (dev.getSample (3, 0), 0.75f, 1.0e-6f);
            expectEquals (dev.getSample (2, 0), 0.0f);

            juce::AudioBuffer<float> mono (1, 8);
            juce::FloatVectorOperations::fill (mono.getWritePointer (0), 0.4f, 8);
            dev.clear();
            OutputRoute { 0, C::right }.addInto (dev, mono, 8); // a mono lane keeps its level on one channel
            expectWithinAbsoluteError (dev.getSample (1, 0), 0.4f, 1.0e-6f);

            dev.clear();
            OutputRoute { 4, C::stereo }.addInto (dev, src, 8); // stale route from a bigger interface
            expectEquals (dev.getMagnitude (0, 8), 0.0f);

            expectEquals ((int) OutputRoute { 2, C::stereo }.channelMask(), 0b1100);
            expectEquals ((int) OutputRoute { 2, C::left }.channelMask(), 0b0100);
            expectEquals ((int) OutputRoute { 0, C::right }.channelMask(), 0b0010);
        }

        beginTest ("routed graph: two rows play out of different pairs at once, unrouted channels silent");
        {
            RouteGain a (0.5f), b (2.0f);
            SignalGraph graph;
            graph.setLaneProcessors (0, { &a });
            graph.setLaneProcessors (1, { &b });
            for (int lane : { 0, 1 })
            {
                graph.setLaneReadsDeviceInput (lane, true);
                graph.setLaneWritesDeviceOutput (lane, true);
            }
            graph.setLaneOutputRoute (0, { 0, C::stereo });
            graph.setLaneOutputRoute (1, { 2, C::left });
            graph.prepare (48000.0, 32, 2);
            expectEquals ((int) graph.getRoutedChannelMask(), 0b0111);

            juce::AudioBuffer<float> dev (6, 32);
            fill (dev, 0.1f); // device input copied to every channel, as AudioEngine does
            graph.process (dev);
            expectWithinAbsoluteError (dev.getSample (0, 5), 0.05f, 1.0e-6f);
            expectWithinAbsoluteError (dev.getSample (1, 5), 0.05f, 1.0e-6f);
            expectWithinAbsoluteError (dev.getSample (2, 5), 0.2f, 1.0e-6f);
            expectEquals (dev.getSample (3, 5), 0.0f);
            expectEquals (dev.getSample (4, 5), 0.0f);
            expectEquals (dev.getSample (5, 5), 0.0f);
        }

        beginTest ("one row on several outputs at once (an output split)");
        {
            RouteGain a (0.5f);
            SignalGraph graph;
            graph.setLaneProcessors (0, { &a });
            graph.setLaneReadsDeviceInput (0, true);
            graph.setLaneWritesDeviceOutput (0, true);
            graph.addLaneOutputRoute (0, { 0, C::stereo });
            graph.addLaneOutputRoute (0, { 2, C::right });
            graph.addLaneOutputRoute (0, { 2, C::right }); // duplicate ignored
            graph.prepare (48000.0, 16, 2);
            expectEquals ((int) graph.getRoutedChannelMask(), 0b1011);
            juce::AudioBuffer<float> dev (4, 16);
            fill (dev, 0.4f);
            graph.process (dev);
            expectWithinAbsoluteError (dev.getSample (0, 0), 0.2f, 1.0e-6f);
            expectWithinAbsoluteError (dev.getSample (1, 0), 0.2f, 1.0e-6f);
            expectEquals (dev.getSample (2, 0), 0.0f);
            expectWithinAbsoluteError (dev.getSample (3, 0), 0.2f, 1.0e-6f);
        }

        beginTest ("rows routed to the same channel sum (an output merge)");
        {
            RouteGain a (1.0f), b (1.0f);
            SignalGraph graph;
            graph.setLaneProcessors (0, { &a });
            graph.setLaneProcessors (1, { &b });
            for (int lane : { 0, 1 })
            {
                graph.setLaneReadsDeviceInput (lane, true);
                graph.setLaneWritesDeviceOutput (lane, true);
                graph.setLaneOutputRoute (lane, { 0, C::stereo });
            }
            graph.prepare (48000.0, 16, 2);
            juce::AudioBuffer<float> dev (2, 16);
            fill (dev, 0.1f);
            graph.process (dev);
            expectWithinAbsoluteError (dev.getSample (0, 0), 0.2f, 1.0e-6f);
        }

        beginTest ("no routes: legacy spread to every channel, mask 0; nothing writing: passthrough");
        {
            RouteGain a (0.5f);
            SignalGraph legacy;
            legacy.setLaneProcessors (0, { &a });
            legacy.setLaneReadsDeviceInput (0, true);
            legacy.setLaneWritesDeviceOutput (0, true);
            legacy.prepare (48000.0, 16, 2);
            expectEquals ((int) legacy.getRoutedChannelMask(), 0);
            juce::AudioBuffer<float> dev (4, 16);
            fill (dev, 0.2f);
            legacy.process (dev);
            for (int ch = 0; ch < 4; ++ch)
                expectWithinAbsoluteError (dev.getSample (ch, 0), 0.1f, 1.0e-6f);

            RouteGain b (0.5f);
            SignalGraph idle;
            idle.setLaneProcessors (0, { &b });
            idle.setLaneReadsDeviceInput (0, true);
            idle.setLaneOutputRoute (0, { 2, C::stereo }); // routed, but the row is not connected to the device
            idle.prepare (48000.0, 16, 2);
            expectEquals ((int) idle.getRoutedChannelMask(), 0);
            fill (dev, 0.2f);
            idle.process (dev);
            for (int ch = 0; ch < 4; ++ch)
                expectWithinAbsoluteError (dev.getSample (ch, 0), 0.2f, 1.0e-6f);
        }

        beginTest ("performance mode keeps the row's route");
        {
            RouteGain a (0.5f), b (2.0f);
            SignalGraph graph;
            graph.setPipelineEnabled (true);
            graph.setLaneProcessors (0, { &a, &b });
            graph.setLaneReadsDeviceInput (0, true);
            graph.setLaneWritesDeviceOutput (0, true);
            graph.setLaneOutputRoute (0, { 2, C::right });
            graph.prepare (48000.0, 32, 2);
            expect (graph.isPipelineActive());

            juce::AudioBuffer<float> dev (4, 32);
            for (int blk = 0; blk < 12; ++blk)
            {
                fill (dev, 0.3f);
                graph.process (dev);
                juce::Thread::sleep (3);
            }
            expectEquals (dev.getSample (0, 4), 0.0f);
            expectEquals (dev.getSample (1, 4), 0.0f);
            expectEquals (dev.getSample (2, 4), 0.0f);
            expectWithinAbsoluteError (dev.getSample (3, 4), 0.3f, 1.0e-5f);
            expectEquals (graph.takePipelineLateBlocks(), 0);
        }

        beginTest ("a spill tail keeps the outgoing row's route and reports its channels");
        {
            std::array<Spillover::LaneSpec, SignalGraph::numLanes> lanes;
            std::vector<std::unique_ptr<EffectProcessor>> retired;
            retired.push_back (std::make_unique<ConstantTail>());
            lanes[0].processors = { retired.back().get() };
            lanes[0].writesDeviceOutput = true;
            lanes[0].outputRoutes = { { 2, C::stereo } };

            auto unit = Spillover::build (lanes, {}, retired);
            expect (unit != nullptr);
            unit->prepare (48000.0, 32, 2, 4);

            Spillover spill;
            spill.add (std::move (unit));
            juce::AudioBuffer<float> dev (4, 32);
            dev.clear();
            spill.process (dev, 48000.0);
            expectEquals (dev.getSample (0, 0), 0.0f);
            expectEquals (dev.getSample (1, 0), 0.0f);
            expectWithinAbsoluteError (dev.getSample (2, 0), 0.25f, 1.0e-6f);
            expectWithinAbsoluteError (dev.getSample (3, 0), 0.25f, 1.0e-6f);
            expectEquals ((int) spill.getRoutedChannelMask(), 0b1100);
            spill.reset();
            expectEquals ((int) spill.getRoutedChannelMask(), 0);
            spill.sweep();
        }

        beginTest ("master EQ filters only the audible channels of the mask");
        {
            MasterEq eq;
            eq.prepare (48000.0, 2);
            MasterEqBand cut;
            cut.type = MasterEqBand::Type::highCut;
            cut.freqHz = 200.0f;
            eq.setBands ({ cut });

            juce::AudioBuffer<float> dev (4, 256);
            for (int i = 0; i < 256; ++i)
                for (int ch = 0; ch < 4; ++ch)
                    dev.setSample (ch, i, (i % 2 == 0) ? 0.5f : -0.5f); // Nyquist: a 200 Hz high cut kills it
            eq.process (dev, 0b1100);
            expectWithinAbsoluteError (dev.getSample (0, 200), 0.5f, 1.0e-6f); // untouched
            expect (std::abs (dev.getSample (2, 200)) < 0.01f);
            expect (std::abs (dev.getSample (3, 200)) < 0.01f);
        }
    }
};

static OutputRouteTests outputRouteTests;

} // namespace openguitarmultifx
