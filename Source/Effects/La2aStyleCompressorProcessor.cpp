#include "La2aStyleCompressorProcessor.h"
#include "CompressorCommon.h"
#include "IconKit.h"
#include "TubeModels.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    // The stage: 12AX7, plate load 100K, B+ 250 V, grid bias -1.5 V. The grid swing per unit of sample (full scale) is set so the
    // stage distorts 0.35% at +10 dBm = -8 dBFS (peak 0.4): second harmonic ~1.6% x (drive/0.5 V) at x = 0.4, see the tests.
    constexpr double plateSupply = 250.0, plateLoad = 100.0e3, gridBias = -1.5, gridPerUnit = 0.109;

    double solvePlate (const KorenTriode& t, double vgk)
    {
        double vp = plateSupply * 0.75;
        for (int i = 0; i < 60; ++i)
        {
            const auto pt = t.evaluate (vgk, vp);
            const double f = vp - (plateSupply - pt.ip * plateLoad);
            const double df = 1.0 + pt.dip_dvpk * plateLoad;
            const double step = f / df;
            vp = juce::jlimit (1.0, plateSupply, vp - step);
            if (std::abs (step) < 1.0e-9)
                break;
        }
        return vp;
    }
}

La2aStyleCompressorProcessor::La2aStyleCompressorProcessor()
{
    auto pr = std::make_unique<juce::AudioParameterFloat> ("la2a_peak", "Peak Reduction", juce::NormalisableRange<float> (0.0f, 100.0f, 0.5f), 40.0f);
    auto g = std::make_unique<juce::AudioParameterFloat> ("la2a_gain", "Gain", juce::NormalisableRange<float> (0.0f, 100.0f, 0.5f), 50.0f);
    auto m = std::make_unique<juce::AudioParameterFloat> ("la2a_mode", "Mode", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v < 0.5f ? "Compress" : "Limit"); }));
    peakReduction = pr.get(); gain = g.get(); mode = m.get();
    parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("la2a", "LA-2A-Style Compressor", "|", std::move (pr), std::move (g), std::move (m));
    buildStage();
}

void La2aStyleCompressorProcessor::buildStage()
{
    KorenTriode triode;
    const double q = solvePlate (triode, gridBias);
    const double slope = (solvePlate (triode, gridBias - 1.0e-3) - solvePlate (triode, gridBias + 1.0e-3)) / 2.0e-3; // volts of plate per volt of grid (positive)
    for (int i = 0; i <= stageSize; ++i)
    {
        const double x = stageRange * (2.0 * i / stageSize - 1.0);
        const double vp = solvePlate (triode, gridBias + gridPerUnit * x);
        // inverted plate swing, so the stage does not invert the signal; unit slope at x = 0
        stageTable[(size_t) i] = (float) ((q - vp) / (slope * gridPerUnit));
    }
}

double La2aStyleCompressorProcessor::stage (double x) const noexcept
{
    const double pos = (juce::jlimit (-stageRange, stageRange, x) / stageRange * 0.5 + 0.5) * stageSize;
    const int i = juce::jmin (stageSize - 1, (int) pos);
    const double f = pos - i;
    return (double) stageTable[(size_t) i] * (1.0 - f) + (double) stageTable[(size_t) i + 1] * f;
}

void La2aStyleCompressorProcessor::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;
    reset();
}

void La2aStyleCompressorProcessor::reset()
{
    envelope = 0.0;
    grFast = grSlow = grDb = memory = 0.0;
}

void La2aStyleCompressorProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const double sideGain = peakReduction->get() <= 0.0f ? 0.0 : std::pow (10.0, (peakReduction->get() / 100.0 - 1.0) * 2.0); // 1 at 100, 0.1 at 50, 0.01 at 0+
    const double lawExponent = mode->get() >= 0.5f ? 14.0 : 3.0;                                                           // (n + 1):1 at depth in the loop; ~2.7:1 and ~9:1 measured between -8 and 0 dBFS
    const double makeup = dyn::fromDb (((double) gain->get() - 50.0) * 0.6);

    const double envAttack = dyn::smoothingCoefficient (0.0005, sampleRate), envRelease = dyn::smoothingCoefficient (0.005, sampleRate);
    const double aFast = dyn::smoothingCoefficient (attackFastSeconds, sampleRate);
    const double aSlow = dyn::smoothingCoefficient (attackSlowSeconds, sampleRate);
    const double rFast = dyn::smoothingCoefficient (releaseFastSeconds, sampleRate);
    const double rSlow = dyn::smoothingCoefficient (releaseSlowMin + releaseSlowSpan * memory, sampleRate);
    const double memUp = dyn::smoothingCoefficient (4.0, sampleRate), memDown = dyn::smoothingCoefficient (12.0, sampleRate);

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    for (int i = 0; i < numSamples; ++i)
    {
        const double atten = dyn::fromDb (-grDb);
        double peak = 0.0;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const double y = stage ((double) buffer.getSample (ch, i) * atten);
            peak = std::max (peak, std::abs (y));
            buffer.setSample (ch, i, (float) (y * makeup));
        }

        envelope += (peak - envelope) * (peak > envelope ? envAttack : envRelease);

        // The EL panel is lit by the rectified output: Rs/Rc of the divider rises as a power of that light.
        const double ratio = std::min (std::pow (envelope * sideGain / referencePeak, lawExponent), std::pow (10.0, maxReductionDb / 20.0) - 1.0);
        const double target = dyn::toDb (1.0 + ratio);

        grFast += (target - grFast) * (target > grFast ? aFast : rFast);
        grSlow += (target - grSlow) * (target > grSlow ? aSlow : rSlow);
        grDb = std::min (maxReductionDb, fastFraction * grFast + (1.0 - fastFraction) * grSlow);

        const double exposure = std::min (1.0, grDb / 20.0);
        memory += (exposure - memory) * (exposure > memory ? memUp : memDown);
    }
}

void La2aStyleCompressorProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::pulse_svg, IconData::pulse_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
