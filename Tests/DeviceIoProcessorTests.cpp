#include "Effects/DeviceIoProcessors.h"
#include "Engine/SignalGraph.h"
#include "UI/SelectorSwitch.h"

#include <juce_core/juce_core.h>

namespace openguitarmultifx
{

namespace
{
    void setParam (EffectProcessor& p, const juce::String& id, float value)
    {
        for (auto* param : p.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (param); f != nullptr && f->paramID == id)
                *f = value;
    }

    void fill (juce::AudioBuffer<float>& b, int ch, float v)
    {
        juce::FloatVectorOperations::fill (b.getWritePointer (ch), v, b.getNumSamples());
    }

    constexpr int block = 64;
    constexpr int settleBlocks = 20;   // > the 10 ms ramps at 48 kHz
}

class DeviceIoProcessorTests : public juce::UnitTest
{
public:
    DeviceIoProcessorTests() : juce::UnitTest ("DeviceIoProcessors", "Effects") {}

    void runTest() override
    {
        beginTest ("choice names");
        {
            expectEquals (DeviceIoChoices::outputName (0), juce::String ("Out 1/2"));
            expectEquals (DeviceIoChoices::outputName (4), juce::String ("Out 3 (L)"));
            expectEquals (DeviceIoChoices::outputName (5), juce::String ("Out 4 (R)"));
            expectEquals (DeviceIoChoices::inputName (2), juce::String ("In 3"));
            expectEquals (DeviceIoChoices::inputName (DeviceIoChoices::numInputChannels + 1), juce::String ("In 3/4"));
        }

        beginTest ("Output Send copies the lane to Out 3/4 and the chain goes on to Out 1/2");
        {
            OutputSendProcessor send;
            setParam (send, "outsend_dest", 3.0f); // Out 3/4
            const auto [dev, mask] = runSendGraph (send);
            expectWithinAbsoluteError (dev.getSample (0, block - 1), 0.4f, 1.0e-5f);
            expectWithinAbsoluteError (dev.getSample (1, block - 1), 0.2f, 1.0e-5f);
            expectWithinAbsoluteError (dev.getSample (2, block - 1), 0.4f, 1.0e-5f);
            expectWithinAbsoluteError (dev.getSample (3, block - 1), 0.2f, 1.0e-5f);
            expectEquals ((int) mask, 0xf);
        }

        beginTest ("Output Send to a single channel folds to mono; Mute Thru silences the rest of the chain");
        {
            OutputSendProcessor send;
            setParam (send, "outsend_dest", 5.0f); // Out 4 (R)
            setParam (send, "outsend_mute_thru", 1.0f);
            const auto [dev, mask] = runSendGraph (send);
            expectWithinAbsoluteError (dev.getSample (0, block - 1), 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (dev.getSample (2, block - 1), 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (dev.getSample (3, block - 1), 0.3f, 1.0e-5f);
            expectEquals ((int) mask, 0x3 | 0x8); // the row's own Out 1/2 stays routed, plus channel 4
        }

        beginTest ("Output Send level, bypass fades it out and drops its channels");
        {
            OutputSendProcessor send;
            setParam (send, "outsend_dest", 3.0f);
            setParam (send, "outsend_level", -6.0f);
            SignalGraph graph;
            juce::AudioBuffer<float> dev (4, block);
            prepareSendGraph (graph, send);
            uint32_t mask = 0;
            for (int i = 0; i < settleBlocks; ++i)
                mask = processSendGraph (graph, dev);
            expectWithinAbsoluteError (dev.getSample (2, block - 1), 0.4f * juce::Decibels::decibelsToGain (-6.0f), 1.0e-4f);

            send.setBypassed (true);
            float firstBypassedBlockPeak = 0.0f;
            for (int i = 0; i < settleBlocks; ++i)
            {
                mask = processSendGraph (graph, dev);
                if (i == 0)
                    firstBypassedBlockPeak = dev.getSample (2, 0);
            }
            expect (firstBypassedBlockPeak > 0.0f, "the send fades out instead of stopping dead");
            expectEquals (dev.getSample (2, block - 1), 0.0f);
            expectEquals ((int) mask, 0x3);
        }

        beginTest ("Input Source brings another physical input into the row (Mix: chain only / both / input only)");
        {
            for (auto [mixValue, expected] : { std::pair { 0.0f, 0.25f }, { 0.5f, 0.75f }, { 1.0f, 0.5f } })
            {
                InputSourceProcessor source;
                setParam (source, "insource_src", 2.0f); // In 3
                setParam (source, "insource_mix", mixValue);

                SignalGraph graph;
                graph.setLaneProcessors (0, { &source });
                graph.setLaneReadsDeviceInput (0, true);
                graph.setLaneDeviceInputChannel (0, 0);
                graph.setLaneWritesDeviceOutput (0, true);
                graph.setLaneOutputRoute (0, {});
                graph.prepare (48000.0, block, 2);

                juce::AudioBuffer<float> inputs (4, block);
                inputs.clear();
                fill (inputs, 0, 0.25f);
                fill (inputs, 2, 0.5f);
                juce::AudioBuffer<float> dev (2, block);
                for (int i = 0; i < settleBlocks; ++i)
                {
                    dev.clear();
                    graph.process (dev, inputs.getArrayOfReadPointers(), inputs.getNumChannels(), 1.0f);
                }
                expectWithinAbsoluteError (dev.getSample (0, block - 1), expected, 1.0e-5f);
                expectWithinAbsoluteError (dev.getSample (1, block - 1), expected, 1.0e-5f);
            }
        }

        beginTest ("Input Source reads a stereo pair L -> L, R -> R, scaled by the app's input gain");
        {
            InputSourceProcessor source;
            setParam (source, "insource_src", (float) DeviceIoChoices::numInputChannels + 1.0f); // In 3/4
            setParam (source, "insource_mix", 1.0f);
            SignalGraph graph;
            graph.setLaneProcessors (0, { &source });
            graph.setLaneReadsDeviceInput (0, true);
            graph.setLaneWritesDeviceOutput (0, true);
            graph.prepare (48000.0, block, 2);

            juce::AudioBuffer<float> inputs (4, block);
            inputs.clear();
            fill (inputs, 2, 0.5f);
            fill (inputs, 3, -0.25f);
            juce::AudioBuffer<float> dev (2, block);
            for (int i = 0; i < settleBlocks; ++i)
            {
                dev.clear();
                graph.process (dev, inputs.getArrayOfReadPointers(), inputs.getNumChannels(), 2.0f);
            }
            expectWithinAbsoluteError (dev.getSample (0, block - 1), 1.0f, 1.0e-5f);
            expectWithinAbsoluteError (dev.getSample (1, block - 1), -0.5f, 1.0e-5f);
        }

        beginTest ("without a graph (offline render) both blocks pass the chain through and touch nothing else");
        {
            OutputSendProcessor send;
            InputSourceProcessor source;
            send.prepareIfNeeded (48000.0, block, 2);
            source.prepareIfNeeded (48000.0, block, 2);
            juce::AudioBuffer<float> buf (2, block);
            fill (buf, 0, 0.3f);
            fill (buf, 1, 0.3f);
            send.processWithBypass (buf);
            source.processWithBypass (buf);
            expectWithinAbsoluteError (buf.getSample (0, block - 1), 0.3f, 1.0e-6f);
        }

        beginTest ("Crossover: the sent band and the chain's band sum back to the input level (LR4 all-pass)");
        {
            for (float hz : { 60.0f, 200.0f, 1000.0f, 4000.0f })
            {
                CrossoverProcessor xover;
                setParam (xover, "xover_freq", 200.0f);
                setParam (xover, "xover_band", 1.0f);   // Highs out, Lows stay in the chain
                setParam (xover, "xover_dest", 3.0f);   // Out 3/4
                const auto peaks = runCrossoverSine (xover, hz);
                expectWithinAbsoluteError (peaks.sum, 0.5f, 0.02f, "sum flat at " + juce::String (hz) + " Hz");
            }
        }

        beginTest ("Crossover: lows go out, highs stay in the chain; 24 dB/oct away from the split");
        {
            CrossoverProcessor xover;
            setParam (xover, "xover_freq", 1000.0f);
            setParam (xover, "xover_band", 0.0f);   // Lows out
            setParam (xover, "xover_dest", 3.0f);
            const auto low = runCrossoverSine (xover, 100.0f);
            expectWithinAbsoluteError (low.sent, 0.5f, 0.02f);
            expect (low.chain < 0.5f * 0.012f, "100 Hz is ~-40 dB in the high band: " + juce::String (low.chain));

            CrossoverProcessor xover2;
            setParam (xover2, "xover_freq", 1000.0f);
            setParam (xover2, "xover_dest", 3.0f);
            const auto atSplit = runCrossoverSine (xover2, 1000.0f);
            expectWithinAbsoluteError (atSplit.sent, 0.25f, 0.01f, "LR4 is -6 dB per band at the split");
            expectWithinAbsoluteError (atSplit.chain, 0.25f, 0.01f);
        }

        beginTest ("Crossover: Chain = Full range leaves the chain untouched; offline it still splits the chain");
        {
            CrossoverProcessor xover;
            setParam (xover, "xover_freq", 1000.0f);
            setParam (xover, "xover_chain", 1.0f);
            const auto full = runCrossoverSine (xover, 100.0f);
            expectWithinAbsoluteError (full.chain, 0.5f, 1.0e-3f);
            expectWithinAbsoluteError (full.sent, 0.5f, 0.02f);

            CrossoverProcessor offline;
            setParam (offline, "xover_freq", 1000.0f);   // sends Lows: the chain keeps the highs
            offline.prepareIfNeeded (48000.0, block, 2);
            juce::AudioBuffer<float> buf (2, block);
            float peak = 0.0f;
            for (int b = 0; b < 200; ++b)
            {
                buf.clear();
                for (int i = 0; i < block; ++i)
                    buf.setSample (0, i, 0.5f), buf.setSample (1, i, 0.5f);   // DC: all lows
                offline.processWithBypass (buf);
                if (b > 150)
                    peak = juce::jmax (peak, buf.getMagnitude (0, 0, block));
            }
            expect (peak < 1.0e-3f, "DC is removed from the chain's high band: " + juce::String (peak));
        }

        beginTest ("SelectorSwitch past 8 options scrolls in a capped strip; a drag scrolls, a tap selects");
        {
            SelectorSwitch strip;
            juce::StringArray names;
            for (int i = 0; i < DeviceIoChoices::numInputChoices; ++i)
                names.add (DeviceIoChoices::inputName (i));
            strip.setOptions (names);
            expect (strip.isScrolling());
            expect (strip.getPreferredWidth() <= SelectorSwitch::maxScrollingWidth);

            SelectorSwitch few;
            few.setOptions ({ "A", "B", "C" });
            expect (! few.isScrolling());

            InputSourceProcessor source;
            for (auto* param : source.getParameters()->getParameters (true))
                if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (param); f != nullptr && f->paramID == "insource_src")
                    expect (source.prefersScrollingSelector (*f) && ! source.prefersSteppedKnob (*f));
        }

        beginTest ("a graph with a device I/O block is never pipelined (its input pointers last one callback)");
        {
            OutputSendProcessor send;
            InputSourceProcessor source;
            SignalGraph graph;
            graph.setLaneProcessors (0, { &source, &send });
            graph.setLaneReadsDeviceInput (0, true);
            graph.setLaneWritesDeviceOutput (0, true);
            graph.setPipelineEnabled (true);
            graph.prepare (48000.0, block, 2);
            expect (! graph.isPipelineActive());
        }
    }

private:
    static void prepareSendGraph (SignalGraph& graph, OutputSendProcessor& send)
    {
        graph.setLaneProcessors (0, { &send });
        graph.setLaneReadsDeviceInput (0, true);
        graph.setLaneWritesDeviceOutput (0, true);
        graph.setLaneOutputRoute (0, {}); // Out 1/2
        graph.prepare (48000.0, block, 2);
    }

    static uint32_t processSendGraph (SignalGraph& graph, juce::AudioBuffer<float>& dev)
    {
        dev.clear();
        fill (dev, 0, 0.4f);
        fill (dev, 1, 0.2f);
        graph.process (dev);
        return graph.getRoutedChannelMask();
    }

    struct CrossoverPeaks { float sent = 0.0f, chain = 0.0f, sum = 0.0f; };

    /** A 0.5-peak sine through a Crossover on a routed lane (Out 1/2 = chain, Out 3/4 = send); peaks after settling. */
    static CrossoverPeaks runCrossoverSine (CrossoverProcessor& xover, float hz)
    {
        SignalGraph graph;
        graph.setLaneProcessors (0, { &xover });
        graph.setLaneReadsDeviceInput (0, true);
        graph.setLaneWritesDeviceOutput (0, true);
        graph.setLaneOutputRoute (0, {});
        graph.prepare (48000.0, block, 2);

        CrossoverPeaks peaks;
        juce::AudioBuffer<float> dev (4, block);
        const int total = 48000 / block;   // 1 s
        double phase = 0.0;
        for (int b = 0; b < total; ++b)
        {
            dev.clear();
            for (int i = 0; i < block; ++i)
            {
                const float v = 0.5f * (float) std::sin (phase);
                phase += juce::MathConstants<double>::twoPi * hz / 48000.0;
                dev.setSample (0, i, v);
                dev.setSample (1, i, v);
            }
            graph.process (dev);
            if (b < total / 2)
                continue;
            for (int i = 0; i < block; ++i)
            {
                peaks.chain = juce::jmax (peaks.chain, std::abs (dev.getSample (0, i)));
                peaks.sent = juce::jmax (peaks.sent, std::abs (dev.getSample (2, i)));
                peaks.sum = juce::jmax (peaks.sum, std::abs (dev.getSample (0, i) + dev.getSample (2, i)));
            }
        }
        return peaks;
    }

    static std::pair<juce::AudioBuffer<float>, uint32_t> runSendGraph (OutputSendProcessor& send)
    {
        SignalGraph graph;
        prepareSendGraph (graph, send);
        juce::AudioBuffer<float> dev (4, block);
        uint32_t mask = 0;
        for (int i = 0; i < settleBlocks; ++i)
            mask = processSendGraph (graph, dev);
        return { dev, mask };
    }
};

static DeviceIoProcessorTests deviceIoProcessorTests;

} // namespace openguitarmultifx
