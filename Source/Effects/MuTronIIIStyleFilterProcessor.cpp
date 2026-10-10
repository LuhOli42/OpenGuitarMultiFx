#include "MuTronIIIStyleFilterProcessor.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    // The Mu-Tron III's front panel: Gain sets the input amp's amplification and therefore also the
    // detector's drive (the one knob does both on the real unit); Peak is the positive-feedback path
    // that sets the resonance. The real gain pot covers roughly -8 dB to +32 dB of input gain.
    double inputGain (float knob) noexcept { return 0.25 * std::pow (160.0, (double) knob); }

    // State-variable filter constants (the Neutron-class workalike's documented values):
    //   integrator caps C = 1.8 nF (Hi) with a further 2.2 nF switched in for Lo;
    //   the fixed sweep resistor 470K and the optocoupler's LDR (18K lit / ~10M dark) in parallel
    //   with it give the up-sweep law Reff = 470K || Rldr.
    //   fc spans ~90 Hz .. 2.3 kHz in Lo, ~200 Hz .. 5 kHz in Hi.
    constexpr double cLo = 4.0e-9, cHi = 1.8e-9;
    constexpr double rSweep = 470.0e3;
    constexpr double rLdrLit = 18.0e3, rLdrDark = 10.0e6;

    // Envelope detector: C9 4.7 uF charged through a small forward resistance (~1 ms attack),
    // discharging through the much bigger bleeder (~150 ms release) -- the classic Mu-Tron feel.
    // The CdS cell itself is slower still and asymmetric (a second one-pole, ~15 ms / ~250 ms).
    constexpr double envAttackSec = 1.0e-3, envReleaseSec = 150.0e-3;
    constexpr double ldrAttackSec = 15.0e-3, ldrReleaseSec = 250.0e-3;

    // The optocoupler only starts conducting once the LED forward voltage is reached; below that
    // the LDR sits at its dark resistance (filter parked at the bottom of the sweep).
    constexpr double detectorThreshold = 0.03;   // volts of rectified envelope
    constexpr double detectorSpan = 0.45;        // envelope volts to fully light the LED

    // Op-amp saturation: the summer rails before anything else; the real +/-9 V op-amps clip a
    // couple of volts short of the rails.
    constexpr double railV = 6.0;
    inline double softRail (double v) noexcept { return railV * std::tanh (v / railV); }
}

MuTronIIIStyleFilterProcessor::MuTronIIIStyleFilterProcessor()
{
    auto makeParam = [] (const char* id, const char* name)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    };
    auto makeStepped = [] (const char* id, const char* name, float max, float def,
                           std::function<juce::String (float)> label)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            id, name, juce::NormalisableRange<float> (0.0f, max, 1.0f), def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                [label] (float v, int) { return label (v); }));
    };

    auto gainParam = makeParam ("mutron_gain", "Gain");
    auto peakParam = makeParam ("mutron_peak", "Peak");
    auto modeParam = makeStepped ("mutron_mode", "Mode", 2.0f, 0.0f,
                                  [] (float v) { return v < 0.5f ? juce::String ("LP")
                                                      : v < 1.5f ? juce::String ("BP") : juce::String ("HP"); });
    auto rangeParam = makeStepped ("mutron_range", "Range", 1.0f, 1.0f,
                                   [] (float v) { return v < 0.5f ? juce::String ("Lo") : juce::String ("Hi"); });
    auto driveParam = makeStepped ("mutron_drive", "Drive", 1.0f, 0.0f,
                                   [] (float v) { return v < 0.5f ? juce::String ("Up") : juce::String ("Down"); });

    gain = gainParam.get();
    peak = peakParam.get();
    mode = modeParam.get();
    range = rangeParam.get();
    drive = driveParam.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "mutron", "Mu-Tron III-Style Filter", "|", std::move (gainParam));
    group->addChild (std::move (peakParam));
    group->addChild (std::move (modeParam));
    group->addChild (std::move (rangeParam));
    group->addChild (std::move (driveParam));
    parameters = std::move (group);
}

void MuTronIIIStyleFilterProcessor::prepare (double newSampleRate, int, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;

    smoothedGain.reset (newSampleRate, 0.02);
    smoothedGain.setCurrentAndTargetValue ((float) inputGain (gain->get()));

    for (auto& ch : channels)
    {
        ch = Channel {};
        // Park the sweep readout where the idle optocoupler leaves it: Up idles dark
        // (bottom of the sweep), Down idles lit (top).
        const double v = drive->get() >= 0.5f ? 1.0 : 0.0;
        const double rLdr = rLdrLit + (rLdrDark - rLdrLit) * (1.0 - v);
        ch.debugReff = rSweep * rLdr / (rSweep + rLdr);
    }
}

void MuTronIIIStyleFilterProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), (int) channels.size());
    const int numSamples = buffer.getNumSamples();

    smoothedGain.setTargetValue ((float) inputGain (gain->get()));

    const double ts = 1.0 / sampleRate;
    const double kEnvAtk = 1.0 - std::exp (-ts / envAttackSec);
    const double kEnvRel = 1.0 - std::exp (-ts / envReleaseSec);
    const double kLdrAtk = 1.0 - std::exp (-ts / ldrAttackSec);
    const double kLdrRel = 1.0 - std::exp (-ts / ldrReleaseSec);

    const double cInt = range->get() < 0.5f ? cLo : cHi;
    const bool sweepDown = drive->get() >= 0.5f;
    const int modeSel = (int) (mode->get() + 0.5f);

    // Peak injects the LP output back into the summer in phase -- positive feedback that eats the
    // damping: d spans ~1.1 (Q ~ 0.9, almost no ring) to ~0.13 (Q ~ 7.7, the Mu-Tron's quack).
    const double damping = 1.1 + (0.13 - 1.1) * (double) peak->get();

    for (int i = 0; i < numSamples; ++i)
    {
        const double gIn = (double) smoothedGain.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& s = channels[(size_t) ch];
            auto* data = buffer.getWritePointer (ch);
            const double x = (double) data[i];

            // ---- Input amp (inverting): v1 = -g * x ----
            const double v1 = -gIn * x;

            // ---- Detector: precision rectifier -> attack/release RC (C9) -> the slow CdS cell ----
            const double rect = std::abs (v1);
            s.env += (rect - s.env) * (rect > s.env ? kEnvAtk : kEnvRel);
            s.ldr += (s.env - s.ldr) * (s.env > s.ldr ? kLdrAtk : kLdrRel);
            s.debugEnv = s.ldr;

            // Optocoupler drive 0..1, then the effective sweep resistance. In Up mode the LDR sits in
            // parallel with the 470K sweep resistor (light -> Reff falls -> fc rises); the Drive
            // switch's Down position re-routes the optocoupler so the same light-vs-resistance law
            // sweeps the other way -- modelled as the mirrored control law.
            double v = (s.ldr - detectorThreshold) / detectorSpan;
            v = juce::jlimit (0.0, 1.0, v);
            if (sweepDown)
                v = 1.0 - v;
            const double rLdr = rLdrLit + (rLdrDark - rLdrLit) * (1.0 - v);
            const double rEff = rSweep * rLdr / (rSweep + rLdr);
            s.debugReff = rEff;

            // ---- SVF: summer + two trapezoidal integrators, solved in closed form.
            //   hp' = -(vin + lp') + d*bp'   (inverting summer; the BP damping path enters
            //   positively here -- with inverting integrators that is what makes it damped)
            //   bp' = bp - a (hp' + hp);  lp' = lp - a (bp' + bp),  a = Ts / (2 R C)
            // Back-substituting bp'/lp' gives hp' directly -- no iteration. ----
            const double a = ts / (2.0 * rEff * cInt);
            const double a2 = a * a;
            const double hpN = softRail ((-v1 - s.lp + (2.0 * a + damping) * s.bp
                                          - (a2 + damping * a) * s.hp) / (1.0 + a2 + damping * a));
            // The BP/LP op-amps rail just like the summer -- without this the integrator states
            // keep winding up while the summer is saturated and the filter self-oscillates.
            const double bpN = softRail (s.bp - a * (hpN + s.hp));
            const double lpN = softRail (s.lp - a * (bpN + s.bp));

            s.hp = hpN;
            s.bp = bpN;
            s.lp = lpN;

            const double y = modeSel == 0 ? lpN : (modeSel == 1 ? bpN : hpN);
            data[i] = (float) y;
        }
    }
}

void MuTronIIIStyleFilterProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // No envelope-filter glyph on the sheet: the phaser's resonant-filter glyph is the closest
    // category mate, same reasoning the GE-7 model documents for reusing a glyph.
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::phaser_svg, IconData::phaser_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
