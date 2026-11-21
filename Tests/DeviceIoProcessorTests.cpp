#include "Effects/DeviceIoProcessors.h"
#include "Engine/SignalGraph.h"

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
