#include "MultiTypeCompressorProcessor.h"
#include "MultiTypeIcons.h"

namespace openguitarmultifx
{
using namespace mt;

namespace
{
    using M = MultiTypeCompressorProcessor::Mode;
    using Co = MultiTypeCompressorProcessor::Colour;
    const juce::StringArray kneeNames { "HARD", "MED-HARD", "MEDIUM", "MED-SOFT", "SOFT" }; 
    constexpr double kKneeDb[5] = { 0, 3, 6, 9, 12 };
}

// Type list for the common Compressor families; values are this engine's presets.
const std::vector<MultiTypeCompressorProcessor::TypeSpec>& MultiTypeCompressorProcessor::types()
{
    static const std::vector<TypeSpec> t = {
        //  name                    mode          colour      fb     thr  ratio attack release knee det autoAR
        { "Studio FF Compressor",  M::downward,  Co::clean,  false, -20, 4,    5,     80,     2,   0,  false },
        { "Studio FF Sustainer",   M::upward,    Co::clean,  false, -30, 3,    5,     80,     2,   0,  false },
        { "Studio FB Compressor",  M::downward,  Co::clean,  true,  -20, 4,    8,     120,    3,   0,  false },
        { "Studio FB Sustainer",   M::upward,    Co::clean,  true,  -30, 3,    8,     120,    3,   0,  false },
        { "Analog Compressor",     M::downward,  Co::clean,  false, -22, 3,    10,    150,    4,   0,  true },
        { "Analog Sustainer",      M::upward,    Co::clean,  false, -32, 3,    10,    150,    4,   0,  true },
        { "Compander",             M::compander, Co::clean,  false, -24, 3,    3,     80,     2,   1,  false },
        { "Dynamicomp-Style",      M::downward,  Co::clean,  true,  -35, 12,   2,     400,    1,   1,  false },
        { "Dynamics Processor",    M::dynamics,  Co::clean,  false, -24, 2,    5,     100,    2,   2,  false },
        { "JFET Compressor",       M::downward,  Co::jfet,   false, -20, 8,    0.3f,  60,     0,   1,  false },
        { "JFET Sustainer",        M::upward,    Co::jfet,   true,  -30, 6,    1,     200,    1,   1,  false },
        { "Optical Compressor",    M::downward,  Co::optical, true, -22, 4,    10,    300,    4,   0,  true },
        { "Optical Sustainer",     M::upward,    Co::optical, true, -32, 4,    10,    300,    4,   0,  true },
        { "Pedal 1",               M::downward,  Co::clean,  true,  -35, 10,   3,     300,    1,   1,  false },
        { "Pedal 2",               M::downward,  Co::clean,  true,  -32, 6,    8,     500,    3,   0,  true },
        { "Tube Compressor",       M::downward,  Co::tube,   false, -22, 3,    15,    250,    4,   0,  true },
    };
    return t;
}

MultiTypeCompressorProcessor::MultiTypeCompressorProcessor (int fixedTypeIndex)
    : MultiTypeCompressorProcessor()
{
    if (fixedTypeIndex >= 0 && fixedTypeIndex < (int) types().size())
    {
        fixedName = types()[(size_t) fixedTypeIndex].name;
        mt::setParam (type, float (fixedTypeIndex));
        applyType (fixedTypeIndex);
        watcher.syncWithoutApplying();
    }
}

MultiTypeCompressorProcessor::MultiTypeCompressorProcessor()
{
    juce::StringArray names;
    for (const auto& t : types()) names.add (t.name);
    parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("multiTypeComp", "Multi-Type Compressor", "|");
    auto add = [] (juce::AudioProcessorParameterGroup& g, std::unique_ptr<juce::AudioParameterFloat> p) { auto* r = p.get(); g.addChild (std::move (p)); return r; };
    type = add (*parameters, choiceParam ("type", "Type", names, 0));
    threshold = add (*parameters, floatParam ("threshold", "Threshold", -60, 0, -20, 1.0f, "dB"));
    ratio = add (*parameters, floatParam ("ratio", "Ratio / Compression", 1, 20, 4, 0.5f, ":1", 1));
    attack = add (*parameters, floatParam ("attack", "Attack Time", 0.1f, 100, 5, 0.4f, "ms", 1));
    release = add (*parameters, floatParam ("release", "Release Time", 10, 2000, 80, 0.4f, "ms", 0));
    mix = add (*parameters, floatParam ("mix", "Mix", 0, 100, 100, 1.0f, "%", 0));
    level = add (*parameters, floatParam ("level", "Level", -30, 20, 0, 1.0f, "dB"));
    auto* cfg = parameters.get(); // one page: every control is a direct child
    knee = add (*cfg, choiceParam ("knee", "Knee Type", kneeNames, 2));
    detector = add (*cfg, choiceParam ("detector", "Detector Type", { "RMS", "PEAK", "RMS+PEAK" }, 0));
    autoAR = add (*cfg, choiceParam ("autoAR", "Auto Attack/Release", { "OFF", "ON" }, 0));
    lookAhead = add (*cfg, floatParam ("lookAhead", "Look Ahead", 0, 10, 0, 1.0f, "ms", 1));
    autoMakeup = add (*cfg, choiceParam ("autoMakeup", "Auto Makeup", { "OFF", "ON" }, 1));
    lightType = add (*cfg, choiceParam ("light", "Light Type", { "1", "2", "3" }, 0));
    dynamicsAmt = add (*cfg, floatParam ("dynamics", "Dynamics", -1, 1, -0.5f, 1.0f, {}, 2));
    scLowCut = add (*cfg, floatParam ("scLowCut", "SC Low Cut", 10, 1000, 20, 0.4f, "Hz", 0));
    scHighCut = add (*cfg, floatParam ("scHighCut", "SC High Cut", 1000, 20000, 20000, 0.4f, "Hz", 0));
    emphasis = add (*cfg, floatParam ("emphasis", "Emphasis", 0, 12, 0, 1.0f, "dB"));
    emphasisFreq = add (*cfg, floatParam ("emphasisFreq", "Emphasis Freq", 200, 8000, 2000, 0.4f, "Hz", 0));
    inputLevel = add (*cfg, choiceParam ("inputLevel", "Input Level", { "INSTRUMENT", "LINE" }, 0));
    watcher.onTypeChanged = [this] (int t) { applyType (t); };
    watcher.attach (type);
    applyType (0);
}

void MultiTypeCompressorProcessor::applyType (int index)
{
    const auto& t = types()[(size_t) juce::jlimit (0, (int) types().size() - 1, index)];
    setParam (threshold, t.threshold); setParam (ratio, t.ratio); setParam (attack, t.attack); setParam (release, t.release);
    setParam (knee, t.knee); setParam (detector, (float) t.detector); setParam (autoAR, t.autoAR ? 1.0f : 0.0f);
    setParam (mix, 100); setParam (level, 0); setParam (lookAhead, 0); setParam (emphasis, 0);
}

void MultiTypeCompressorProcessor::setState (const juce::XmlElement& s) { EffectProcessor::setState (s); watcher.syncWithoutApplying(); }

void MultiTypeCompressorProcessor::prepare (double sr, int, int)
{
    watcher.poll();
    fs = sr;
    for (auto& l : look) l.prepare ((int) (0.012 * fs));
    mixLevel.prepare (fs);
    reset();
}

void MultiTypeCompressorProcessor::reset()
{
    for (auto& l : look) l.reset();
    scLc.reset(); scHc.reset(); scEmph.reset();
    env = rms = gainDb = slowDb = lastOut = 0;
}

void MultiTypeCompressorProcessor::process (juce::AudioBuffer<float>& buffer)
{
    const auto& spec = types()[(size_t) juce::jlimit (0, (int) types().size() - 1, asIndex (type))];
    const int n = buffer.getNumSamples();
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    mixLevel.setTargets (mix->get(), level->get());
    scLc.set (scLowCut->get(), fs);
    scHc.set (scHighCut->get(), fs);
    scEmph.shelf (emphasisFreq->get(), fs, emphasis->get(), false);
    const double thr = threshold->get(), rat = juce::jmax (1.0f, ratio->get());
    const double kneeW = kKneeDb[asIndex (knee)];
    const int det = asIndex (detector);
    const bool autoTimes = asIndex (autoAR) == 1;
    const double la = lookAhead->get() * 1e-3 * fs;
    const double inScale = asIndex (inputLevel) == 0 ? 4.0 : 1.0; // an instrument-level signal is ~12 dB under line level
    const int light = asIndex (lightType);
    double atkMs = attack->get(), relMs = release->get();
    if (spec.colour == Colour::optical) { atkMs *= (1.0 + light); relMs *= (1.0 + 0.6 * light); }
    const double aA = std::exp (-1.0 / (atkMs * 1e-3 * fs)), aR = std::exp (-1.0 / (relMs * 1e-3 * fs));
    const double rmsCoef = std::exp (-1.0 / (0.01 * fs));
    const double slowCoef = std::exp (-1.0 / (1.5 * fs));
    const double dyn = dynamicsAmt->get();

    // static curve: gain change (dB) for a detector level (dB)
    auto curve = [&] (double x) {
        auto down = [&] (double over, double r) {
            if (kneeW > 0 && std::abs (over) < kneeW / 2) { const double t = over + kneeW / 2; return -(1.0 - 1.0 / r) * t * t / (2 * kneeW); }
            return over > 0 ? -(1.0 - 1.0 / r) * over : 0.0;
        };
        switch (spec.mode)
        {
            case Mode::downward: return down (x - thr, rat);
            case Mode::upward: { const double under = thr - x; return under > 0 ? std::min (30.0, (1.0 - 1.0 / rat) * under) : 0.0; }
            case Mode::compander: { const double c = down (x - thr, rat); const double u = thr - 20.0 - x; return c - (u > 0 ? u * 0.5 : 0.0); }
            case Mode::dynamics:
            default:
            {
                const double over = x - thr;
                if (dyn < 0) return down (over, 1.0 + 9.0 * -dyn);
                return over < 0 ? over * dyn * 2.0 : 0.0; // expansion below the threshold
            }
        }
    };

    double makeup = 0.0;
    if (asIndex (autoMakeup) == 1 && spec.mode == Mode::downward) makeup = -curve (0.0) * 0.5;

    for (int i = 0; i < n; ++i)
    {
        const double inL = L[i], inR = R != nullptr ? R[i] : L[i];
        double sc = spec.feedback ? lastOut : 0.5 * (inL + inR);
        sc = scEmph.process (scHc.lp (scLc.hp (sc))) * inScale;
        const double a = std::abs (sc);
        rms = rms * rmsCoef + a * a * (1.0 - rmsCoef);
        const double peakLevel = a, rmsLevel = std::sqrt (rms) * 1.414;
        const double d = det == 0 ? rmsLevel : (det == 1 ? peakLevel : std::max (rmsLevel, peakLevel));
        const double xDb = juce::Decibels::gainToDecibels (std::max (d, 1e-6));
        const double target = curve (xDb);
        double relCoef = aR;
        if (autoTimes) relCoef = std::pow (aR, 1.0 + std::max (0.0, (peakLevel - rmsLevel) * 4.0)); // faster after transients
        gainDb = target < gainDb ? target + (gainDb - target) * aA : target + (gainDb - target) * relCoef;
        double g = gainDb;
        if (spec.colour == Colour::optical)
        {
            // two-stage release: a slow memory of how hard it has been working holds part of the reduction
            slowDb = std::min (gainDb, slowDb * slowCoef + gainDb * (1.0 - slowCoef));
            g = std::min (gainDb, 0.6 * gainDb + 0.4 * slowDb);
        }
        const double lin = juce::Decibels::decibelsToGain (g + makeup);

        double y[2];
        for (int c = 0; c < 2; ++c)
        {
            const double in = c == 0 ? inL : inR;
            double s = la > 0.5 ? (look[c].push ((float) in), look[c].read (la)) : in;
            s *= lin;
            if (spec.colour == Colour::jfet) s += 0.04 * (-g / 20.0) * s * s;          // FET: some even harmonics as it works
            if (spec.colour == Colour::tube) s = std::tanh (s * 1.5) / 1.5;            // tube make-up stage
            y[c] = s;
        }
        lastOut = 0.5 * (y[0] + y[1]);
        const double m = mixLevel.mix.getNextValue(), gl = mixLevel.level.getNextValue();
        L[i] = (float) ((inL * (1.0 - m) + y[0] * m) * gl);
        if (R != nullptr) R[i] = (float) ((inR * (1.0 - m) + y[1] * m) * gl);
    }
}

void MultiTypeCompressorProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    mt::drawIconByKey (g, b, "pulse");
}

} // namespace openguitarmultifx
