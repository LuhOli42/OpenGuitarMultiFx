#include "EffectRegistry.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

/**
    Every circuit-modelled pedal, all knobs at noon, must be at unity loudness for a guitar-like reference signal
    (see OutputTrimEffect for why). If this fails after a change to a pedal's circuit, the message prints the new
    measured gain: put its negative in the pedal's trim in EffectRegistry.cpp.
*/
class PedalUnityLevelTests : public juce::UnitTest
{
public:
    PedalUnityLevelTests() : juce::UnitTest ("PedalUnityLevel", "Effects") {}

    static constexpr double sr = 48000.0;

    /** RMS gain (dB) for E3 with harmonics 1..10 at 1/k amplitude, scaled to 0.1 RMS (-20 dBFS). */
    static double referenceGainDb (EffectProcessor& p)
    {
        const double twoPi = 2.0 * juce::MathConstants<double>::pi, f0 = 164.81, rmsIn = 0.1;
        double norm = 0.0;
        for (int k = 1; k <= 10; ++k)
            norm += 0.5 / (double) (k * k);
        const double scale = rmsIn / std::sqrt (norm);
        const int warm = (int) (5.0 * sr), len = (int) (1.0 * sr);

        juce::AudioBuffer<float> buf (1, 64);
        double sIn = 0.0, sOut = 0.0;
        for (long long base = 0; base < warm + len; base += 64)
        {
            double x[64];
            for (int i = 0; i < 64; ++i)
            {
                double v = 0.0;
                for (int k = 1; k <= 10; ++k)
                    v += std::sin (twoPi * f0 * k * (double) (base + i) / sr) / (double) k;
                x[i] = scale * v;
                buf.setSample (0, i, (float) x[i]);
            }
            p.process (buf);
            if (base >= warm)
                for (int i = 0; i < 64; ++i)
                {
                    sIn += x[i] * x[i];
                    sOut += (double) buf.getSample (0, i) * buf.getSample (0, i);
                }
        }
        return 10.0 * std::log10 (sOut / sIn);
    }

    void runTest() override
    {
        EffectRegistry registry;
        registerBuiltInEffects (registry);

        beginTest ("every pedal at noon is within 1 dB of unity for the reference signal");
        for (const char* key : { "PositiveGroundBooster", "OD1StyleOverdrive", "TS808StyleOverdrive", "TS9StyleOverdrive",
                                 "TS10StyleOverdrive", "CentaurStyleOverdrive", "BD2StyleOverdrive", "DS1StyleDistortion",
                                 "HM2StyleDistortion", "DistortionPlusStyleDistortion", "DOD250StyleOverdrive", "GuvnorStyleDistortion", "BluesBreakerStyleOverdrive", "RatStyleDistortion", "RAT2StyleDistortion", "TurboRatStyleDistortion", "CrunchBoxStyleDistortion", "ZendriveStyleOverdrive", "OCDStyleOverdrive", "DT1StyleDistortion", "ODR1StyleOverdrive", "OverdriverStyleOverdrive", "EPStyleBooster", "TubeDriverStyleOverdrive", "MetalZoneStyleDistortion", "BigMuffStyleFuzz", "RussianBigMuffStyleFuzz", "SovtekBigMuffStyleFuzz", "FuzzFaceStyleFuzz", "SiliconFuzzFaceStyleFuzz", "ToneBenderStyleFuzz", "SiliconToneBenderStyleFuzz", "BassmanStyleAmplifier", "SuperLeadStyleAmplifier", "TwinReverbStyleAmplifier", "DeluxeReverbStyleAmplifier", "JC120StyleAmplifier", "JTM45StyleAmplifier", "JCM800StyleAmplifier", "AC15StyleAmplifier", "AC30StyleAmplifier", "SLO100StyleAmplifier", "MarkIICPlusStyleAmplifier", "DualRectifierStyleAmplifier", "EVH5150StyleAmplifier", "ENGLPowerballStyleAmplifier", "RockerverbStyleAmplifier", "SVTStyleAmplifier", "TrainwreckExpressStyleAmplifier", "KometConcordeStyleAmplifier", "DividedBy13FTR37StyleAmplifier", "CarrRamblerStyleAmplifier", "DumbleSteelStringStyleAmplifier", "SunnModelTStyleAmplifier", "FryetteDeliveranceD120StyleAmplifier", "BognerUberschallStyleAmplifier", "GE7StyleEqualizer", "DynaCompStyleCompressor", "RossStyleCompressor", "SqueezerStyleCompressor" })
        {
            auto pedal = registry.create (key);
            pedal->prepare (sr, 512, 1);
            const auto pages = pedal->getParameterPages();
            for (auto* f : pages[0]) // page 1 only: an amp's page 2 (bias, speaker...) stays at its defaults
            {
                // A stepped selector (interval >= 1, e.g. an amp's Input: Normal/Jumped/Bright) has no real "noon" -- the
                // exact numeric midpoint lands on an ambiguous rounding boundary between two switch positions, not a
                // physical half-way point a real knob would have. Leave those at their own default instead of forcing 0.5.
                if (f->range.interval >= 1.0f)
                    continue;
                // A modulation-depth param (the Fender amps' vibrato "Intensity") has no neutral "noon" either: at half
                // depth the effect is genuinely ON and its amplitude modulation's RMS drop is correct physics, not a
                // unity calibration error. Leave it at its own default (0 = off).
                if (juce::String (f->paramID).endsWith ("_intensity"))
                    continue;
                *f = juce::jlimit (f->range.start, f->range.end, 0.5f); // "noon", whatever a pedal's own default is
            }
            const double gainDb = referenceGainDb (*pedal);
            logMessage (juce::String (key).paddedRight (' ', 24) + juce::String (gainDb, 2) + " dB");
            expectWithinAbsoluteError (gainDb, 0.0, 1.0);
        }
    }
};

static PedalUnityLevelTests pedalUnityLevelTests;

} // namespace openguitarmultifx
