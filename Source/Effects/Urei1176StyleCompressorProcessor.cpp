#include "Urei1176StyleCompressorProcessor.h"
#include "CompressorCommon.h"
#include "IconKit.h"

#include <IconData.h>

#include <functional>

namespace openguitarmultifx
{

namespace
{
    constexpr double seriesOhms = 27.0e3;          // R6, the L section's series element
    constexpr double fetBeta = 1.5e-3;             // A/V^2: Idss / Vp^2 of a typical selected FET (Idss 6 mA, Vp -2 V)
    constexpr double fetControlMax = 3.0;          // V of bias the side-chain can remove: Rds down to ~110 ohm, about 48 dB of reduction
    constexpr double preampGain = 4.0;             // the signal amplifiers between the FET and the output control (12 dB here; the rest is the Input control's range)
    constexpr double dbfsPerDbu = -18.0;           // 0 dB re 0.775 V

    juce::AudioParameterFloatAttributes selectorText (std::function<juce::String (int)> name)
    {
        return juce::AudioParameterFloatAttributes().withStringFromValueFunction ([name] (float v, int) { return name (juce::roundToInt (v)); });
    }
}

const Urei1176StyleCompressorProcessor::RatioSpec& Urei1176StyleCompressorProcessor::specFor (int ratioIndex) noexcept
{
    // sensitivities tuned so the static curve's slope 5-15 dB above the threshold is the button's ratio (Urei1176StyleCompressorProcessorTests)
    static const RatioSpec specs[] = {
        { 0.66, -30.0, 0.002, 1.0 },  // 4:1
        { 2.21, -26.0, 0.002, 1.0 },  // 8:1
        { 3.85, -25.0, 0.002, 1.0 },  // 12:1
        { 7.17, -24.0, 0.002, 1.0 },  // 20:1
        { 30.0, -28.0, 0.025, 0.7 },  // all buttons in (estimates)
    };
    return specs[juce::jlimit (0, 4, ratioIndex)];
}

Urei1176StyleCompressorProcessor::Urei1176StyleCompressorProcessor()
{
    // Input = the unit's input attenuator (-20 .. +20 dB of drive into the FET stage). Default fully CCW (-20 dB): at
    // 0 dB a guitar-level DI sits ~22 dB over the 4:1 limiting onset, so the pedal idled in ~15 dB of limiting with
    // no make-up -- ~-13 dB out. Output defaults fully CW (+20 dB make-up) to restore the level; compression engages
    // as Input is raised.
    auto in = std::make_unique<juce::AudioParameterFloat> ("u1176_input", "Input", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f);
    auto out = std::make_unique<juce::AudioParameterFloat> ("u1176_output", "Output", juce::NormalisableRange<float> (-20.0f, 20.0f, 0.1f), 20.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; }));
    auto atk = std::make_unique<juce::AudioParameterFloat> ("u1176_attack", "Attack", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            const double us = 800.0 * std::pow (20.0 / 800.0, (double) v); // 800 us (CCW) .. 20 us (fully CW)
            return juce::String (juce::roundToInt (us)) + " us";
        }));
    auto rel = std::make_unique<juce::AudioParameterFloat> ("u1176_release", "Release", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            const double ms = 1100.0 * std::pow (50.0 / 1100.0, (double) v); // 1.1 s (CCW) .. 50 ms (fully CW)
            return ms >= 1000.0 ? juce::String (ms / 1000.0, 2) + " s" : juce::String (juce::roundToInt (ms)) + " ms";
        }));
    auto rat = std::make_unique<juce::AudioParameterFloat> ("u1176_ratio", "Ratio", juce::NormalisableRange<float> (0.0f, 4.0f, 1.0f), 0.0f,
        selectorText ([] (int i) { const char* n[] = { "4:1", "8:1", "12:1", "20:1", "All" }; return juce::String (n[juce::jlimit (0, 4, i)]); }));

    input = in.get(); output = out.get(); attack = atk.get(); release = rel.get(); ratio = rat.get();
    parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("u1176", "1176-Style Compressor", "|", std::move (in), std::move (out), std::move (atk),
                                                                         std::move (rel), std::move (rat));
}

void Urei1176StyleCompressorProcessor::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;
    reset();
}

void Urei1176StyleCompressorProcessor::reset()
{
    detector = 0.0;
    grDb = 0.0;
}

void Urei1176StyleCompressorProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const auto& spec = specFor (juce::roundToInt (ratio->get()));
    const double inputDb = (double) (input->get() - 0.5f) * 40.0;                       // the Input control: -20 .. +20 dB
    const double inGain = dyn::fromDb (inputDb);
    const double outGain = dyn::fromDb ((double) output->get() - dyn::toDb (preampGain));

    // The threshold, in volts at the preamplifier output: with Input at maximum (+20 dB) the input level at minimum limiting is spec.thresholdDb.
    const double thresholdVolts = dyn::fromDb (spec.thresholdDb + dbfsPerDbu + 20.0) * preampGain;

    const double attackSeconds = 800.0e-6 * std::pow (20.0 / 800.0, (double) attack->get()) * spec.timeScale;
    const double releaseSeconds = 1.1 * std::pow (50.0 / 1100.0, (double) release->get()) * spec.timeScale;
    const double aA = dyn::smoothingCoefficient (attackSeconds, sampleRate), aR = dyn::smoothingCoefficient (releaseSeconds, sampleRate);

    const double k = 2.0 * fetBeta * seriesOhms;
    const double a = spec.linearityError * fetBeta * seriesOhms;

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    for (int i = 0; i < numSamples; ++i)
    {
        const double u = std::min (fetControlMax, spec.sensitivity * detector); // the FET's gate overdrive (V above pinch-off)
        const double b = 1.0 + k * u;

        double sidePeak = 0.0;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const double vin = (double) buffer.getSample (ch, i) * inGain;
            // The L section: (vin - v) / R6 = beta v (2 u + (2c - 1) v), solved for v; then the preamplifier and the Output control.
            const double v = 2.0 * vin / (b + std::sqrt (std::max (1.0e-12, b * b + 4.0 * a * vin)));
            const double y = v * preampGain;
            sidePeak = std::max (sidePeak, std::abs (y));
            buffer.setSample (ch, i, (float) (y * outGain));
        }

        // The side-chain (from the preamplifier output): full-wave rectifier biased to the threshold, filtered by C27
        const double drive = std::max (0.0, sidePeak - thresholdVolts);
        detector += (drive - detector) * (drive > detector ? aA : aR);
        grDb = dyn::toDb (b);
    }
}

void Urei1176StyleCompressorProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::pulse_svg, IconData::pulse_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
