#include "BossOC2StyleOctaverProcessor.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    // Analyser pre-filter: the LP that conditions the input before the comparators (keeps the tracker
    // on the fundamental, ~400 Hz corner like the real analyser chain).
    constexpr double analyserHz = 400.0;
    // Octave low-passes: two cascaded 2nd-order sections each; they set the mellow sub-octave voicing
    // and suppress the chopper's upper products (3f/2, 5f/2).
    constexpr double oct1Hz = 300.0, oct2Hz = 200.0;
    // Input HPF (coupling cap) and the peak detectors' ~8 ms release.
    constexpr double hpHz = 20.0, envReleaseMs = 8.0;
    // Comparator hysteresis fraction of the tracked swing, plus a floor so silence can't chatter.
    constexpr double hystFrac = 0.08, hystFloor = 1.0e-4;
}

BossOC2StyleOctaverProcessor::BossOC2StyleOctaverProcessor()
{
    auto direct = std::make_unique<juce::AudioParameterFloat> (
        "oc2_direct", "Direct", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto oct1 = std::make_unique<juce::AudioParameterFloat> (
        "oc2_oct1", "Octave 1", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto oct2 = std::make_unique<juce::AudioParameterFloat> (
        "oc2_oct2", "Octave 2", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);

    directParam = direct.get();
    oct1Param = oct1.get();
    oct2Param = oct2.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "oc2", "Boss OC-2-Style Octaver", "|", std::move (direct));
    group->addChild (std::move (oct1));
    group->addChild (std::move (oct2));
    parameters = std::move (group);
}

void BossOC2StyleOctaverProcessor::updateLevels()
{
    // Mixer gain: each knob is a linear level; the ~2x rescale keeps noon near unity (trimmed upstream).
    directGain = 2.0 * (double) directParam->get();
    oct1Gain = 2.0 * (double) oct1Param->get();
    oct2Gain = 2.0 * (double) oct2Param->get();
}

void BossOC2StyleOctaverProcessor::prepare (double newSampleRate, int, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;

    for (auto& ch : channels)
    {
        ch = Channel {};
        ch.lpAnalyser.makeLowPass (newSampleRate, analyserHz);
        for (auto& b : ch.lpOct1)  b.makeLowPass (newSampleRate, oct1Hz);
        for (auto& b : ch.lpOct2)  b.makeLowPass (newSampleRate, oct2Hz);
    }

    const double pi2 = 2.0 * 3.14159265358979323846;
    hpCoeff = std::exp (-pi2 * hpHz / newSampleRate);
    envDecay = std::exp (-1.0 / (newSampleRate * envReleaseMs * 1.0e-3));

    updateLevels();
    controlCounter = 0;
    shortcut.reset();
}

void BossOC2StyleOctaverProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    for (int i = 0; i < numSamples; ++i)
    {
        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updateLevels();
        }

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);
            const double x = (double) data[i];

            // input coupling HPF: y = x - lp(x)
            ch.hpZ = hpCoeff * ch.hpZ + (1.0 - hpCoeff) * x;
            const double xHp = x - ch.hpZ;

            // ---- analyser: LP-filtered input into the dual peak detectors ----
            const double xf = ch.lpAnalyser.process (xHp);
            ch.envP = juce::jmax (xf, ch.envP * envDecay);
            ch.envN = juce::jmin (xf, ch.envN * envDecay);
            const double mid = 0.5 * (ch.envP + ch.envN);
            const double hyst = juce::jmax (hystFloor, hystFrac * (ch.envP - ch.envN));

            // hysteresis comparator -> the square at the input fundamental
            if (xf > mid + hyst)      ch.comp = true;
            else if (xf < mid - hyst) ch.comp = false;
            // the two toggle flip-flops: f/2 and f/4 squares
            if (ch.comp && ! ch.prevComp)
            {
                ch.ff1 = ! ch.ff1;
                if (ch.ff1)   // ff1 rising edge clocks ff2
                    ch.ff2 = ! ch.ff2;
            }
            ch.prevComp = ch.comp;

            // ---- octave creators: lift ~ a diode drop above virtual ground, then chop by +-0.5 ----
            const double lift = juce::jmax (ch.envP, -ch.envN);
            const double chopped1 = (xHp + lift) * (ch.ff1 ? 0.5 : -0.5);
            const double oct1 = ch.lpOct1[1].process (ch.lpOct1[0].process (chopped1));
            const double chopped2 = (oct1 + 0.5 * lift) * (ch.ff2 ? 0.5 : -0.5);
            const double oct2 = ch.lpOct2[1].process (ch.lpOct2[0].process (chopped2));

            data[i] = (float) (directGain * xHp + oct1Gain * oct1 + oct2Gain * oct2);
        }
    }

    shortcut.end (buffer);
}

void BossOC2StyleOctaverProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::octaver_svg, IconData::octaver_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
