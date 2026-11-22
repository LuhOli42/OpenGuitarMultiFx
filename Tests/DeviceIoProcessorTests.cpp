#include "Effects/DeviceIoProcessors.h"
#include "Engine/SignalGraph.h"
#include "Engine/OutputRoute.h"
#include "UI/ScrollListSelector.h"

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

        beginTest ("Output Send to a later line: Lows into Line 3, the line keeps the highs; both sum back flat");
        {
            for (float hz : { 60.0f, 120.0f, 1000.0f })
            {
                OutputSendProcessor send;
                setParam (send, "outsend_dest", (float) (DeviceIoChoices::numOutputChoices + 2));   // Line 3
                setParam (send, "outsend_band", 1.0f);         // Lows
                setParam (send, "outsend_freq", 120.0f);
                setParam (send, "outsend_mute_thru", 2.0f);    // Thru: Other band
                const auto peaks = runLineSine (&send, nullptr, hz);
                expectWithinAbsoluteError (peaks.sum, 0.5f, 0.02f, "flat at " + juce::String (hz) + " Hz");
                if (hz == 60.0f)
                    expect (peaks.line > 0.45f && peaks.row < 0.1f, "60 Hz goes to Line 3");
                if (hz == 1000.0f)
                    expect (peaks.row > 0.49f && peaks.line < 0.01f, "1 kHz stays on the row");
            }
        }

        beginTest ("Output Send: Crossover Off sends full range to the line, Thru Full keeps the row untouched");
        {
            OutputSendProcessor send;
            setParam (send, "outsend_dest", (float) (DeviceIoChoices::numOutputChoices + 2));
            const auto peaks = runLineSine (&send, nullptr, 440.0f);
            expectWithinAbsoluteError (peaks.line, 0.5f, 1.0e-3f);
            expectWithinAbsoluteError (peaks.row, 0.5f, 1.0e-3f);
        }

        beginTest ("lines are forward-only: a send to its own or an earlier line is silent");
        {
            OutputSendProcessor send;
            setParam (send, "outsend_dest", (float) (DeviceIoChoices::numOutputChoices + 0));   // Line 1 from Line 1
            const auto peaks = runLineSine (&send, nullptr, 440.0f);
            expectWithinAbsoluteError (peaks.line, 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (peaks.row, 0.5f, 1.0e-3f);
        }

        beginTest ("Input Source on Line 3 reads Line 1's output (highs only with the crossover)");
        {
            InputSourceProcessor source;
            setParam (source, "insource_src", (float) (DeviceIoChoices::numInputChoices + 0));   // Line 1
            setParam (source, "insource_mix", 1.0f);
            setParam (source, "insource_band", 2.0f);   // Highs
            setParam (source, "insource_freq", 120.0f);
            const auto high = runLineSine (nullptr, &source, 2000.0f);
            expectWithinAbsoluteError (high.line, 0.5f, 0.02f);
            InputSourceProcessor source2;
            setParam (source2, "insource_src", (float) (DeviceIoChoices::numInputChoices + 0));
            setParam (source2, "insource_mix", 1.0f);
            setParam (source2, "insource_band", 2.0f);
            setParam (source2, "insource_freq", 1000.0f);
            const auto low = runLineSine (nullptr, &source2, 60.0f);
            expect (low.line < 0.01f, "60 Hz is cut by a 1 kHz high pass: " + juce::String (low.line));

            InputSourceProcessor later;   // Line 1 reading Line 3 (a later line) gets silence
            setParam (later, "insource_src", (float) (DeviceIoChoices::numInputChoices + 2));
            setParam (later, "insource_mix", 1.0f);
            SignalGraph graph;
            graph.setLaneProcessors (0, { &later });
            graph.setLaneReadsDeviceInput (0, true);
            graph.setLaneWritesDeviceOutput (0, true);
            graph.prepare (48000.0, block, 2);
            juce::AudioBuffer<float> dev (2, block);
            for (int i = 0; i < settleBlocks; ++i)
            {
                dev.clear();
                fill (dev, 0, 0.3f);
                fill (dev, 1, 0.3f);
                graph.process (dev);
            }
            expectWithinAbsoluteError (dev.getSample (0, block - 1), 0.0f, 1.0e-6f);
        }

        beginTest ("only the channels the open interface has are offered; lines always are");
        {
            EffectProcessor::DeviceIo::setAvailableChannels (2, 6);   // e.g. 2 in / 6 out
            expect (DeviceIoChoices::isOutputAvailable (0));                                 // Out 1/2
            expect (DeviceIoChoices::isOutputAvailable (6));                                 // Out 5/6
            expect (! DeviceIoChoices::isOutputAvailable (9));                               // Out 7/8
            expect (! DeviceIoChoices::isOutputAvailable (10));                              // Out 7 (L)
            expect (DeviceIoChoices::isOutputAvailable (DeviceIoChoices::numOutputChoices)); // Line 1
            expect (DeviceIoChoices::isInputAvailable (0) && DeviceIoChoices::isInputAvailable (1));
            expect (! DeviceIoChoices::isInputAvailable (2));                                // In 3
            expect (DeviceIoChoices::isInputAvailable (DeviceIoChoices::numInputChannels));      // In 1/2
            expect (! DeviceIoChoices::isInputAvailable (DeviceIoChoices::numInputChannels + 1)); // In 3/4
            expect (DeviceIoChoices::isInputAvailable (DeviceIoChoices::numInputChoices + 3));   // Line 4

            OutputSendProcessor send;
            int shown = 0;
            for (auto* param : send.getParameters()->getParameters (true))
                if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (param); f != nullptr && f->paramID == "outsend_dest")
                {
                    expect (send.prefersScrollingSelector (*f) && ! send.prefersSteppedKnob (*f));
                    for (int step = 0; step < DeviceIoChoices::numOutputDestinations; ++step)
                        shown += send.isSelectorOptionAvailable (*f, step) ? 1 : 0;
                }
            expectEquals (shown, 3 * 3 + DeviceIoChoices::numLines);

            EffectProcessor::DeviceIo::setAvailableChannels (-1, -1);   // no device known: everything listed
            expect (DeviceIoChoices::isOutputAvailable (11) && DeviceIoChoices::isInputAvailable (11));
        }

        beginTest ("ScrollListSelector: a box of 3.5 touch rows that scrolls down; a drag scrolls, a tap selects");
        {
            ScrollListSelector list;
            juce::StringArray names;
            for (int i = 0; i < DeviceIoChoices::numInputSources; ++i)
                names.add (DeviceIoChoices::inputName (i));
            list.setOptions (names);
            expectEquals (list.getPreferredHeight(), (int) std::round (ScrollListSelector::rowHeight * 3.5f) + 2 * ScrollListSelector::padding);
            list.setSize (list.getPreferredWidth(), list.getPreferredHeight());

            int chosen = -1;
            list.onChange = [&chosen] (int i) { chosen = i; };
            list.setSelectedIndex (DeviceIoChoices::numInputSources - 1, juce::dontSendNotification);
            expect (list.getScroll() > 0.0f, "selecting the last row scrolls it into view");
            list.setSelectedIndex (-1, juce::dontSendNotification);
            expectEquals (list.getSelectedIndex(), -1);
            list.setSelectedIndex (0, juce::sendNotificationSync);
            expectEquals (chosen, 0);
            expectEquals (list.getScroll(), 0.0f);
        }

        beginTest ("ScrollListSelector tabs: Stereo lists the pairs, Mono the single channels, Line the lines");
        {
            InputSourceProcessor source;
            juce::AudioParameterFloat* src = nullptr;
            for (auto* param : source.getParameters()->getParameters (true))
                if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (param); f != nullptr && f->paramID == "insource_src")
                    src = f;
            expect (src != nullptr);
            juce::StringArray names;
            std::vector<int> masks;
            for (int i = 0; i < DeviceIoChoices::numInputSources; ++i)
            {
                names.add (DeviceIoChoices::inputName (i));
                masks.push_back (source.getSelectorOptionGroups (*src, i));
            }
            ScrollListSelector list;
            list.setOptions (names);
            list.setGroups (masks);
            list.setSize (list.getPreferredWidth(), list.getPreferredHeight());
            expect (list.hasTabs());

            list.setSelectedIndex (2, juce::dontSendNotification);   // In 3: a mono input -> Mono tab
            expectEquals (list.getActiveGroup(), (int) ScrollListSelector::mono);
            expectEquals (list.getNumVisibleRows(), DeviceIoChoices::numInputChannels);
            list.setActiveGroup (ScrollListSelector::stereo);
            expectEquals (list.getNumVisibleRows(), DeviceIoChoices::numInputChannels / 2);
            list.setSelectedIndex (DeviceIoChoices::numInputChoices, juce::dontSendNotification);   // Line 1 -> Line tab
            expectEquals (list.getActiveGroup(), (int) ScrollListSelector::line);
            expectEquals (list.getNumVisibleRows(), DeviceIoChoices::numLines);
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

    struct LinePeaks { float row = 0.0f, line = 0.0f, sum = 0.0f; };

    /** Line 1 (lane 0) reads the device input -> Out 1/2 ("row"); Line 3 (lane 2) -> Out 3/4 ("line"). `send` sits on
        Line 1, `source` on Line 3. A 0.5-peak sine; peaks over the second half of 1 s. */
    static LinePeaks runLineSine (OutputSendProcessor* send, InputSourceProcessor* source, float hz)
    {
        SignalGraph graph;
        graph.setLaneProcessors (0, send != nullptr ? std::vector<EffectProcessor*> { send } : std::vector<EffectProcessor*> {});
        graph.setLaneReadsDeviceInput (0, true);
        graph.setLaneWritesDeviceOutput (0, true);
        graph.setLaneOutputRoute (0, {});
        graph.setLaneProcessors (2, source != nullptr ? std::vector<EffectProcessor*> { source } : std::vector<EffectProcessor*> {});
        graph.setLaneWritesDeviceOutput (2, true);
        OutputRoute out34;
        out34.pairStart = 2;
        graph.setLaneOutputRoute (2, out34);
        graph.prepare (48000.0, block, 2);

        LinePeaks peaks;
        juce::AudioBuffer<float> dev (4, block);
        const int total = 48000 / block;
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
                peaks.row = juce::jmax (peaks.row, std::abs (dev.getSample (0, i)));
                peaks.line = juce::jmax (peaks.line, std::abs (dev.getSample (2, i)));
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
