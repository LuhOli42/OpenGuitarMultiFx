#include "Engine/MasterEq.h"
#include "Engine/SignalGraph.h"
#include "Engine/Spillover.h"

#include <juce_core/juce_core.h>

#include <tuple>

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

        beginTest ("route level, balance and polarity");
        {
            juce::AudioBuffer<float> src (2, 4);
            juce::FloatVectorOperations::fill (src.getWritePointer (0), 1.0f, 4);
            juce::FloatVectorOperations::fill (src.getWritePointer (1), 1.0f, 4);
            juce::AudioBuffer<float> dev (2, 4);

            dev.clear();
            OutputRoute r { 0, C::stereo };
            r.gain = 0.5f;
            r.pan = 0.5f;            // right: L drops to half, R stays (balance never boosts)
            r.invert = true;
            r.addInto (dev, src, 4);
            expectWithinAbsoluteError (dev.getSample (0, 0), -0.25f, 1.0e-6f);
            expectWithinAbsoluteError (dev.getSample (1, 0), -0.5f, 1.0e-6f);

            dev.clear();
            OutputRoute mono { 0, C::left };
            mono.gain = 2.0f;
            mono.addInto (dev, src, 4);
            expectWithinAbsoluteError (dev.getSample (0, 0), 2.0f, 1.0e-6f);
            expect (mono.sameDestination ({ 0, C::left }) && ! mono.sameDestination ({ 0, C::right }));
        }

        beginTest ("row link channel maps, level and polarity");
        {
            juce::AudioBuffer<float> src (2, 4);
            juce::FloatVectorOperations::fill (src.getWritePointer (0), 1.0f, 4);
            juce::FloatVectorOperations::fill (src.getWritePointer (1), 0.2f, 4);
            juce::AudioBuffer<float> dst (2, 4);
            using M = LaneLink::ChannelMap;
            const auto run = [&] (LaneLink link) { dst.clear(); link.addInto (dst, src, 4); return std::pair { dst.getSample (0, 1), dst.getSample (1, 1) }; };

            auto [l, r] = run ({ 1.0f, false, M::stereo });
            expectWithinAbsoluteError (l, 1.0f, 1.0e-6f); expectWithinAbsoluteError (r, 0.2f, 1.0e-6f);
            std::tie (l, r) = run ({ 1.0f, false, M::monoSum });
            expectWithinAbsoluteError (l, 0.6f, 1.0e-6f); expectWithinAbsoluteError (r, 0.6f, 1.0e-6f);
            std::tie (l, r) = run ({ 1.0f, false, M::leftOnly });
            expectWithinAbsoluteError (l, 1.0f, 1.0e-6f); expectWithinAbsoluteError (r, 1.0f, 1.0e-6f);
            std::tie (l, r) = run ({ 1.0f, false, M::rightOnly });
            expectWithinAbsoluteError (l, 0.2f, 1.0e-6f); expectWithinAbsoluteError (r, 0.2f, 1.0e-6f);
            std::tie (l, r) = run ({ 0.5f, true, M::swapLR });
            expectWithinAbsoluteError (l, -0.1f, 1.0e-6f); expectWithinAbsoluteError (r, -0.5f, 1.0e-6f);
        }

        beginTest ("a weighted row link feeds the target at its level; an inverted copy cancels the plain one");
        {
            RouteGain a (1.0f), b (1.0f), c (1.0f);
            SignalGraph graph;
            graph.setLaneProcessors (0, { &a });
            graph.setLaneProcessors (1, { &b });
            graph.setLaneProcessors (2, { &c });
            graph.setLaneReadsDeviceInput (0, true);
            graph.addLaneConnection (0, 1, { 0.25f, false, LaneLink::ChannelMap::stereo });
            graph.addLaneConnection (0, 2);
            graph.addLaneConnection (1, 2, { 4.0f, true, LaneLink::ChannelMap::stereo }); // 0.25 * 4 inverted = -1
            graph.setLaneWritesDeviceOutput (1, true);
            graph.setLaneWritesDeviceOutput (2, true);
            graph.setLaneOutputRoute (1, { 0, C::left });
            graph.setLaneOutputRoute (2, { 0, C::right });
            graph.prepare (48000.0, 16, 2);
            juce::AudioBuffer<float> dev (2, 16);
            fill (dev, 0.8f);
            graph.process (dev);
            expectWithinAbsoluteError (dev.getSample (0, 3), 0.2f, 1.0e-6f);
            expectWithinAbsoluteError (dev.getSample (1, 3), 0.0f, 1.0e-6f);
        }

        beginTest ("merge L/R: mono to the target's L only / R only");
        {
            juce::AudioBuffer<float> src (2, 4);
            juce::FloatVectorOperations::fill (src.getWritePointer (0), 1.0f, 4);
            juce::FloatVectorOperations::fill (src.getWritePointer (1), 0.2f, 4);
            juce::AudioBuffer<float> dst (2, 4);
            dst.clear();
            LaneLink { 1.0f, false, LaneLink::ChannelMap::toLeft }.addInto (dst, src, 4);
            LaneLink { 1.0f, false, LaneLink::ChannelMap::toRight }.addInto (dst, src, 4);
            LaneLink { 1.0f, false, LaneLink::ChannelMap::toRight }.addInto (dst, src, 4);
            expectWithinAbsoluteError (dst.getSample (0, 0), 0.6f, 1.0e-6f);
            expectWithinAbsoluteError (dst.getSample (1, 0), 1.2f, 1.0e-6f);
        }

        beginTest ("crossover: a low-pass route and a high-pass route at the same frequency sum back flat");
        {
            RouteGain a (1.0f);
            SignalGraph graph;
            graph.setLaneProcessors (0, { &a });
            graph.setLaneReadsDeviceInput (0, true);
            graph.setLaneWritesDeviceOutput (0, true);
            OutputRoute low { 0, C::left };
            low.filter = { RouteFilter::Type::lowPass, 500.0f };
            OutputRoute high { 0, C::right };
            high.filter = { RouteFilter::Type::highPass, 500.0f };
            graph.addLaneOutputRoute (0, low);
            graph.addLaneOutputRoute (0, high);
            constexpr int block = 256;
            graph.prepare (48000.0, block, 1);

            for (const double freq : { 60.0, 500.0, 6000.0 })
            {
                graph.prepare (48000.0, block, 1); // clears the filter state between tones
                double inPow = 0.0, lowPow = 0.0, highPow = 0.0, sumPow = 0.0;
                juce::AudioBuffer<float> dev (2, block);
                for (int blk = 0; blk < 200; ++blk)
                {
                    for (int i = 0; i < block; ++i)
                    {
                        const float x = (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * (blk * block + i) / 48000.0);
                        dev.setSample (0, i, x);
                        dev.setSample (1, i, x);
                    }
                    juce::AudioBuffer<float> in;
                    in.makeCopyOf (dev);
                    graph.process (dev);
                    if (blk < 100)
                        continue; // let the filters settle
                    for (int i = 0; i < block; ++i)
                    {
                        const double l = dev.getSample (0, i), h = dev.getSample (1, i), x = in.getSample (0, i);
                        inPow += x * x; lowPow += l * l; highPow += h * h; sumPow += (l + h) * (l + h);
                    }
                }
                // LR4: the bands are 6 dB down each at the crossover and in phase, so they sum to the input's level.
                expectWithinAbsoluteError (sumPow / inPow, 1.0, 0.02);
                if (freq < 100.0)
                    expect (highPow / inPow < 0.001 && lowPow / inPow > 0.95);
                if (freq > 5000.0)
                    expect (lowPow / inPow < 0.001 && highPow / inPow > 0.95);
            }
        }

        beginTest ("crossover on a row link: the target lane gets only its band");
        {
            RouteGain a (1.0f), b (1.0f);
            SignalGraph graph;
            graph.setLaneProcessors (0, { &a });
            graph.setLaneProcessors (1, { &b });
            graph.setLaneReadsDeviceInput (0, true);
            LaneLink link;
            link.filter = { RouteFilter::Type::highPass, 2000.0f };
            graph.addLaneConnection (0, 1, link);
            graph.setLaneWritesDeviceOutput (1, true);
            graph.setLaneOutputRoute (1, { 0, C::stereo });
            constexpr int block = 128;
            graph.prepare (48000.0, block, 2);
            double outPow = 0.0;
            juce::AudioBuffer<float> dev (2, block);
            for (int blk = 0; blk < 100; ++blk)
            {
                for (int i = 0; i < block; ++i)
                    for (int ch = 0; ch < 2; ++ch)
                        dev.setSample (ch, i, (float) std::sin (2.0 * juce::MathConstants<double>::pi * 80.0 * (blk * block + i) / 48000.0));
                graph.process (dev);
                if (blk >= 50)
                    for (int i = 0; i < block; ++i)
                        outPow += dev.getSample (0, i) * dev.getSample (0, i);
            }
            expect (outPow / (50.0 * block * 0.5) < 1.0e-4); // 80 Hz is ~ -116 dB through a 2 kHz LR4 high pass
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
