#include "MultiTypeModulationProcessors.h"
#include "../IconKit.h"
#include <IconData.h>

namespace openguitarmultifx
{
using namespace mt;

namespace
{
    const juce::StringArray phaseReverseNames { "NONE", "LEFT", "RIGHT", "BOTH" };
    auto adder() { return [] (juce::AudioProcessorParameterGroup& g, std::unique_ptr<juce::AudioParameterFloat> p) { auto* r = p.get(); g.addChild (std::move (p)); return r; }; }
    template <typename Spec> juce::StringArray namesOf (const std::vector<Spec>& v) { juce::StringArray a; for (const auto& s : v) a.add (s.name); return a; }
    double quantizeLfo (double v, double steps) { return steps < 1.5 ? v : std::round (v * steps) / steps; }
}

// ===================================================================================== Chorus

const std::vector<MultiTypeChorusProcessor::TypeSpec>& MultiTypeChorusProcessor::types()
{
    static const std::vector<TypeSpec> t = {
        //  name                voices rate  depth delay mix  hiCut  drive width phase l2Rate l2Dep lfo dim mono
        { "Digital Mono",        2, 0.6f, 40, 6,   50, 20000, 0,  0,   0,   0.1f, 0,  0, 0, true },
        { "Digital Stereo",      2, 0.6f, 40, 6,   50, 20000, 0,  30,  90,  0.1f, 0,  0, 0, false },
        { "Analog Mono",         2, 0.7f, 45, 7,   50, 6000,  2,  0,   0,   0.1f, 0,  1, 0, true },
        { "Analog Stereo",       2, 0.7f, 45, 7,   50, 6000,  2,  30,  90,  0.1f, 0,  1, 0, false },
        { "Japan CE-2-Style",    1, 0.8f, 55, 6,   50, 5000,  3,  0,   0,   0.1f, 0,  1, 0, true },
        { "Warm Stereo",         2, 0.5f, 50, 9,   50, 4500,  2,  40,  120, 0.1f, 0,  0, 0, false },
        { "80's Style",          2, 0.9f, 60, 5,   50, 12000, 0,  60,  180, 0.1f, 0,  1, 0, false },
        { "Triangle Chorus",     2, 1.2f, 35, 6,   50, 9000,  0,  30,  90,  0.1f, 0,  1, 0, false },
        { "8-Voice Stereo",      8, 0.4f, 35, 9,   50, 12000, 0,  50,  90,  0.15f, 10, 0, 0, false },
        { "Vintage Tape",        1, 0.3f, 25, 4,   50, 8000,  1,  20,  90,  6.0f, 15, 0, 0, false },
        { "Dimension 1",         1, 0.25f, 30, 7,  50, 10000, 0,  0,   180, 0.1f, 0,  1, 1, false },
        { "Dimension 2",         1, 0.40f, 35, 7,  50, 10000, 0,  0,   180, 0.1f, 0,  1, 2, false },
        { "Dimension 3",         1, 0.50f, 45, 7,  50, 10000, 0,  0,   180, 0.1f, 0,  1, 3, false },
        { "4-Voice Analog",      4, 0.5f, 45, 8,   50, 6000,  2,  40,  90,  0.1f, 0,  0, 0, false },
        { "8-Voice Analog",      8, 0.45f, 40, 9,  50, 6000,  2,  50,  90,  0.1f, 0,  0, 0, false },
        { "Stereo Tri-Chorus",   3, 0.6f, 40, 7,   50, 10000, 0,  40,  120, 0.1f, 0,  0, 0, false },
        { "Dual Chorus",         2, 0.8f, 45, 6,   50, 12000, 0,  70,  180, 0.25f, 5, 0, 0, false },
    };
    return t;
}

MultiTypeChorusProcessor::MultiTypeChorusProcessor (int fixedTypeIndex)
    : MultiTypeChorusProcessor()
{
    if (fixedTypeIndex >= 0 && fixedTypeIndex < (int) types().size())
    {
        fixedName = types()[(size_t) fixedTypeIndex].name;
        mt::setParam (type, float (fixedTypeIndex));
        applyType (fixedTypeIndex);
        watcher.syncWithoutApplying();
    }
}

MultiTypeChorusProcessor::MultiTypeChorusProcessor()
{
    parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("multiTypeChorus", "Multi-Type Chorus", "|");
    auto add = adder();
    type = add (*parameters, choiceParam ("type", "Type", namesOf (types()), 1));
    voices = add (*parameters, floatParam ("voices", "Number of Voices", 1, 8, 2, 1.0f, {}, 0));
    voices->range.interval = 1.0f;
    rate = add (*parameters, floatParam ("rate", "Rate", 0.05f, 10, 0.6f, 0.5f, "Hz", 2));
    depth = add (*parameters, floatParam ("depth", "Depth", 0, 100, 40, 1.0f, "%", 0));
    delayTime = add (*parameters, floatParam ("delay", "Delay Time", 0.1f, 50, 6, 0.5f, "ms", 1));
    mix = add (*parameters, floatParam ("mix", "Mix", 0, 100, 50, 1.0f, "%", 0));
    level = add (*parameters, floatParam ("level", "Level", -30, 12, 0, 1.0f, "dB"));
    auto* ex = parameters.get(); // one page: every control is a direct child
    lowCut = add (*ex, floatParam ("lowCut", "Low Cut", 10, 2000, 20, 0.4f, "Hz", 0));
    highCut = add (*ex, floatParam ("highCut", "High Cut", 1000, 20000, 20000, 0.4f, "Hz", 0));
    lfoPhase = add (*ex, floatParam ("lfoPhase", "LFO Phase", 0, 180, 90, 1.0f, "deg", 0));
    lfoType = add (*ex, choiceParam ("lfoType", "LFO Type", lfoShapeNames(), 0));
    autoDepth = add (*ex, choiceParam ("autoDepth", "Auto Depth", { "OFF", "ON" }, 1));
    phaseReverse = add (*ex, choiceParam ("phaseReverse", "Phase Reverse", phaseReverseNames, 0));
    drive = add (*ex, floatParam ("drive", "Drive", 0, 10, 0));
    width = add (*ex, floatParam ("width", "Width", 0, 100, 30, 1.0f, "%", 0));
    lfo2Rate = add (*ex, floatParam ("lfo2Rate", "LFO2 Rate", 0.05f, 10, 0.1f, 0.5f, "Hz", 2));
    lfo2Depth = add (*ex, floatParam ("lfo2Depth", "LFO2 Depth", 0, 100, 0, 1.0f, "%", 0));
    spread = add (*ex, floatParam ("spread", "Stereo Spread", -100, 200, 100, 1.0f, "%", 0));
    dimension = add (*ex, choiceParam ("dimension", "Dimension Mode", { "OFF", "LOW", "MED", "HIGH" }, 0));
    watcher.onTypeChanged = [this] (int t) { applyType (t); };
    watcher.attach (type);
    applyType (1);
}

void MultiTypeChorusProcessor::applyType (int index)
{
    const auto& t = types()[(size_t) juce::jlimit (0, (int) types().size() - 1, index)];
    setParam (voices, (float) t.voices); setParam (rate, t.rate); setParam (depth, t.depth); setParam (delayTime, t.delayMs);
    setParam (mix, t.mix); setParam (highCut, t.highCut); setParam (drive, t.drive); setParam (width, t.width);
    setParam (lfoPhase, t.phase); setParam (lfo2Rate, t.lfo2Rate); setParam (lfo2Depth, t.lfo2Depth);
    setParam (lfoType, (float) t.lfoType); setParam (dimension, (float) t.dimension); setParam (lowCut, 20); setParam (phaseReverse, 0);
    setParam (spread, t.mono ? 0.0f : 100.0f); setParam (level, 0);
}

void MultiTypeChorusProcessor::setState (const juce::XmlElement& s) { EffectProcessor::setState (s); watcher.syncWithoutApplying(); }

void MultiTypeChorusProcessor::prepare (double sr, int, int)
{
    watcher.poll();
    fs = sr;
    for (auto& l : line) l.prepare ((int) (0.1 * fs));
    mixLevel.prepare (fs);
    reset();
}

void MultiTypeChorusProcessor::reset() { for (auto& l : line) l.reset(); for (int c = 0; c < 2; ++c) { lc[c].reset(); hc[c].reset(); } }

void MultiTypeChorusProcessor::process (juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    mixLevel.setTargets (mix->get(), level->get());
    const int nv = juce::jlimit (1, 8, juce::roundToInt (voices->get()));
    const double r = rate->get();
    const double depthMs = depth->get() / 100.0 * (asIndex (autoDepth) == 1 ? juce::jlimit (0.5, 10.0, 2.5 / r) : 10.0);
    const double minMs = delayTime->get();
    const double w = width->get() / 100.0;
    const double ph = lfoPhase->get() / 360.0;
    const auto shape = (LfoShape) asIndex (lfoType);
    const double driveG = 0.3 + drive->get() * 0.5;
    const int dim = asIndex (dimension), pr = asIndex (phaseReverse);
    const double l2d = lfo2Depth->get() / 100.0;
    const double sp = spread->get() / 100.0;
    lfo2.set (lfo2Rate->get(), fs);
    for (int c = 0; c < 2; ++c) { lc[c].set (lowCut->get(), fs); hc[c].set (highCut->get(), fs); }

    for (int i = 0; i < n; ++i)
    {
        const double inL = L[i], inR = R != nullptr ? R[i] : L[i];
        const double x = 0.5 * (inL + inR);
        line[0].push ((float) (drive->get() > 0.01f ? std::tanh (x * driveG) / driveG : x));
        lfo.set (r * (1.0 + 0.5 * l2d * lfo2.next (LfoShape::sine)), fs);
        lfo2.advance();
        double wet[2] = { 0, 0 };
        for (int c = 0; c < 2; ++c)
        {
            for (int v = 0; v < nv; ++v)
            {
                const double off = v / double (nv) + (c == 1 ? (dim > 0 ? 0.5 : ph) : 0.0);
                double dMs = minMs + depthMs * 0.5 * (1.0 + lfo.next (shape, off));
                if (c == 1) dMs = dMs - w * (dMs - 1.0);
                wet[c] += line[0].read (dMs * 1e-3 * fs);
            }
            wet[c] /= std::sqrt ((double) nv);
            wet[c] = hc[c].lp (lc[c].hp (wet[c]));
        }
        lfo.advance();
        if (dim > 0)
        {
            // Dimension-style matrix: two antiphase chorus channels, each side gets one minus a share of the other.
            const double k = dim == 1 ? 0.0 : (dim == 2 ? 0.35 : 0.6);
            const double a = wet[0], b = wet[1];
            wet[0] = a - k * b;
            wet[1] = b - k * a;
        }
        if (pr == 1 || pr == 3) wet[0] = -wet[0];
        if (pr == 2 || pr == 3) wet[1] = -wet[1];
        const double mid = 0.5 * (wet[0] + wet[1]), side = 0.5 * (wet[0] - wet[1]) * sp;
        const double m = mixLevel.mix.getNextValue(), g = mixLevel.level.getNextValue();
        if (R != nullptr)
        {
            L[i] = (float) ((inL * (1.0 - m) + (mid + side) * m) * g);
            R[i] = (float) ((inR * (1.0 - m) + (mid - side) * m) * g);
        }
        else L[i] = (float) ((inL * (1.0 - m) + mid * m) * g);
    }
}

void MultiTypeChorusProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::chorus_svg, IconData::chorus_svgSize);
    icon::drawSvg (g, b, svg.get());
}

// ===================================================================================== Flanger

const std::vector<MultiTypeFlangerProcessor::TypeSpec>& MultiTypeFlangerProcessor::types()
{
    static const std::vector<TypeSpec> t = {
        //  name                    rate   depth fb    manual minMs maxMs mix drive hiCut phase tz lfo vco exp  quant
        { "Digital Mono",          0.3f,  70,  40,   50,   0.5f, 6,    50, 0,  20000, 0,   0, 1, 1, 1.0f, 0 },
        { "Digital Stereo",        0.3f,  70,  40,   50,   0.5f, 6,    50, 0,  20000, 90,  0, 1, 1, 1.0f, 0 },
        { "Analog Mono",           0.25f, 75,  50,   50,   0.4f, 7,    50, 3,  7000,  0,   0, 1, 1, 1.3f, 0 },
        { "Analog Stereo",         0.25f, 75,  50,   50,   0.4f, 7,    50, 3,  7000,  90,  0, 1, 1, 1.3f, 0 },
        { "MXF-117-Style",         0.2f,  80,  60,   40,   0.35f, 7,   50, 2,  8000,  0,   0, 1, 1, 1.4f, 0 },
        { "'Cuda Flange",          0.12f, 85,  70,   50,   0.4f, 8,    50, 3,  7000,  0,   0, 1, 1, 1.5f, 0 },
        { "Electric Mystery",      0.15f, 70,  20,   50,   0.4f, 8,    50, 1,  12000, 0,   0, 1, 1, 1.2f, 0 },
        { "BBF-2-Style",           0.3f,  70,  55,   50,   1.0f, 13,   50, 3,  6000,  0,   0, 1, 1, 1.3f, 0 },
        { "Stereo Jet",            0.08f, 90,  75,   50,   0.4f, 10,   50, 2,  9000,  180, 0, 1, 1, 1.4f, 0 },
        { "Thru-Zero",             0.15f, 90,  0,    50,   0.0f, 4,    50, 0,  20000, 0,   1, 1, 0, 1.0f, 0 },
        { "Manual Thru-Zero",      0.15f, 0,   0,    30,   0.0f, 4,    50, 0,  20000, 0,   2, 1, 0, 1.0f, 0 },
        { "Zero Flanger",          0.2f,  80,  -30,  50,   0.0f, 5,    50, 0,  20000, 0,   1, 1, 0, 1.0f, 0 },
        { "Step Flanger",          1.0f,  80,  60,   50,   0.5f, 8,    50, 0,  12000, 0,   0, 4, 0, 1.0f, 8 },
        { "Vowel Flanger",         0.4f,  70,  -75,  50,   1.0f, 6,    50, 0,  12000, 0,   0, 1, 1, 1.2f, 0 },
        { "Trippy Flanger",        2.5f,  60,  65,   50,   0.5f, 9,    50, 0,  12000, 180, 0, 0, 1, 1.0f, 0 },
        { "80s Rack Flanger",      0.35f, 70,  55,   50,   0.5f, 6,    50, 0,  14000, 90,  0, 1, 0, 1.0f, 0 },
        { "Pop Flanger",           0.5f,  60,  35,   50,   0.5f, 5,    50, 0,  14000, 90,  0, 0, 0, 1.0f, 0 },
    };
    return t;
}

MultiTypeFlangerProcessor::MultiTypeFlangerProcessor (int fixedTypeIndex)
    : MultiTypeFlangerProcessor()
{
    if (fixedTypeIndex >= 0 && fixedTypeIndex < (int) types().size())
    {
        fixedName = types()[(size_t) fixedTypeIndex].name;
        mt::setParam (type, float (fixedTypeIndex));
        applyType (fixedTypeIndex);
        watcher.syncWithoutApplying();
    }
}

MultiTypeFlangerProcessor::MultiTypeFlangerProcessor()
{
    parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("multiTypeFlanger", "Multi-Type Flanger", "|");
    auto add = adder();
    type = add (*parameters, choiceParam ("type", "Type", namesOf (types()), 3));
    rate = add (*parameters, floatParam ("rate", "Rate", 0.02f, 10, 0.25f, 0.5f, "Hz", 2));
    depth = add (*parameters, floatParam ("depth", "Depth", 0, 100, 75, 1.0f, "%", 0));
    feedback = add (*parameters, floatParam ("feedback", "Feedback", -100, 100, 50, 1.0f, "%", 0));
    manual = add (*parameters, floatParam ("manual", "Manual", 0, 100, 50, 1.0f, "%", 0));
    mix = add (*parameters, floatParam ("mix", "Mix", 0, 100, 50, 1.0f, "%", 0));
    level = add (*parameters, floatParam ("level", "Level", -30, 12, 0, 1.0f, "dB"));
    auto* ex = parameters.get(); // one page: every control is a direct child
    minTime = add (*ex, floatParam ("minTime", "Minimum Time", 0, 10, 0.4f, 0.6f, "ms", 2));
    maxTime = add (*ex, floatParam ("maxTime", "Maximum Time", 0.5f, 20, 7, 0.6f, "ms", 2));
    thruZero = add (*ex, choiceParam ("thruZero", "Thru-Zero", { "OFF", "AUTO", "MANUAL" }, 0));
    dryDelay = add (*ex, floatParam ("dryDelay", "Dry Delay", 0, 100, 50, 1.0f, "%", 0));
    vcoResponse = add (*ex, choiceParam ("vco", "VCO Response", { "LINEAR", "EXPONENTIAL" }, 1));
    exponent = add (*ex, floatParam ("exponent", "Exponent", 0.25f, 4, 1.0f, 0.5f, {}, 2));
    lfoType = add (*ex, choiceParam ("lfoType", "LFO Type", lfoShapeNames(), 1));
    lfoPhase = add (*ex, floatParam ("lfoPhase", "LFO Phase", 0, 180, 90, 1.0f, "deg", 0));
    lfoHicut = add (*ex, floatParam ("lfoHicut", "LFO Hicut", 1, 100, 100, 0.5f, "Hz", 0));
    lfoQuantize = add (*ex, floatParam ("lfoQuantize", "LFO Quantize", 0, 32, 0, 1.0f, {}, 0));
    autoDepth = add (*ex, choiceParam ("autoDepth", "Auto Depth", { "OFF", "ON" }, 0));
    drive = add (*ex, floatParam ("drive", "Drive", 0, 10, 0));
    bassFocus = add (*ex, floatParam ("bassFocus", "Bass Focus", 0, 10, 0));
    lowCut = add (*ex, floatParam ("lowCut", "Low Cut", 10, 2000, 20, 0.4f, "Hz", 0));
    highCut = add (*ex, floatParam ("highCut", "High Cut", 1000, 20000, 20000, 0.4f, "Hz", 0));
    phaseReverse = add (*ex, choiceParam ("phaseReverse", "Phase Reverse", phaseReverseNames, 0));
    spread = add (*ex, floatParam ("spread", "Stereo Spread", -100, 200, 100, 1.0f, "%", 0));
    watcher.onTypeChanged = [this] (int t) { applyType (t); };
    watcher.attach (type);
    applyType (3);
}

void MultiTypeFlangerProcessor::applyType (int index)
{
    const auto& t = types()[(size_t) juce::jlimit (0, (int) types().size() - 1, index)];
    setParam (rate, t.rate); setParam (depth, t.depth); setParam (feedback, t.feedback); setParam (manual, t.manual);
    setParam (minTime, t.minMs); setParam (maxTime, t.maxMs); setParam (mix, t.mix); setParam (drive, t.drive);
    setParam (highCut, t.highCut); setParam (lfoPhase, t.phase); setParam (thruZero, (float) t.thruZero);
    setParam (lfoType, (float) t.lfoType); setParam (vcoResponse, (float) t.vco); setParam (exponent, t.exponent);
    setParam (lfoQuantize, t.quantize); setParam (phaseReverse, t.thruZero > 0 ? 3.0f : 0.0f);
    setParam (bassFocus, 0); setParam (lowCut, 20); setParam (dryDelay, 50); setParam (level, 0);
}

void MultiTypeFlangerProcessor::setState (const juce::XmlElement& s) { EffectProcessor::setState (s); watcher.syncWithoutApplying(); }

void MultiTypeFlangerProcessor::prepare (double sr, int, int)
{
    watcher.poll();
    fs = sr;
    for (auto& l : line) l.prepare ((int) (0.05 * fs));
    for (auto& l : dryLine) l.prepare ((int) (0.05 * fs));
    mixLevel.prepare (fs);
    reset();
}

void MultiTypeFlangerProcessor::reset()
{
    for (auto& l : line) l.reset();
    for (auto& l : dryLine) l.reset();
    for (int c = 0; c < 2; ++c) { lfoSmooth[c].reset(); lc[c].reset(); hc[c].reset(); focus[c].reset(); fb[c] = 0; }
}

void MultiTypeFlangerProcessor::process (juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    mixLevel.setTargets (mix->get(), level->get());
    lfo.set (rate->get(), fs);
    const double dep = depth->get() / 100.0 * (asIndex (autoDepth) == 1 ? juce::jlimit (0.2, 1.0, 0.3 / rate->get()) : 1.0);
    const double man = manual->get() / 100.0;
    const double minMs = minTime->get(), maxMs = juce::jmax (minTime->get() + 0.05f, maxTime->get());
    const int tz = asIndex (thruZero), pr = asIndex (phaseReverse), vco = asIndex (vcoResponse);
    const double ex = exponent->get(), fbk = feedback->get() / 100.0 * 0.97;
    const double ph = lfoPhase->get() / 360.0, quant = lfoQuantize->get();
    const auto shape = (LfoShape) asIndex (lfoType);
    const double driveG = 0.3 + drive->get() * 0.5;
    const double dryMs = (minMs + maxMs) * 0.5 * dryDelay->get() / 50.0;
    for (int c = 0; c < 2; ++c)
    {
        lfoSmooth[c].set (lfoHicut->get(), fs);
        lc[c].set (lowCut->get(), fs);
        hc[c].set (highCut->get(), fs);
        focus[c].set (20.0 + bassFocus->get() * 60.0, fs);
    }

    for (int i = 0; i < n; ++i)
    {
        const double in[2] = { (double) L[i], R != nullptr ? (double) R[i] : (double) L[i] };
        double out[2];
        for (int c = 0; c < 2; ++c)
        {
            double lv = 0.5 + 0.5 * quantizeLfo (lfo.next (shape, c == 1 ? ph : 0.0), quant);
            lv = lfoSmooth[c].lp (lv);
            double pos = man * (1.0 - dep) + dep * lv;
            if (vco == 1) pos = std::pow (juce::jlimit (0.0, 1.0, pos), ex);
            double dMs = tz == 0 ? minMs + (maxMs - minMs) * pos : 2.0 * dryMs * pos; // thru-zero sweeps across the dry delay
            const double wet = line[c].read (std::max (1.0, dMs * 1e-3 * fs));
            double s = in[c] + fbk * fb[c];
            if (drive->get() > 0.01f) s = std::tanh (s * driveG) / driveG;
            line[c].push ((float) s);
            dryLine[c].push ((float) in[c]);
            fb[c] = wet;
            double w = hc[c].lp (lc[c].hp (wet));
            w -= focus[c].lp (w) * (bassFocus->get() / 10.0);
            if (c == 0 && (pr == 1 || pr == 3)) w = -w;
            if (c == 1 && (pr == 2 || pr == 3)) w = -w;
            out[c] = w;
        }
        lfo.advance();
        const double sp = spread->get() / 100.0;
        const double mid = 0.5 * (out[0] + out[1]), side = 0.5 * (out[0] - out[1]) * sp;
        const double m = mixLevel.mix.getNextValue(), g = mixLevel.level.getNextValue();
        for (int c = 0; c < (R != nullptr ? 2 : 1); ++c)
        {
            const double dry = tz == 0 ? in[c] : dryLine[c].read (std::max (1.0, dryMs * 1e-3 * fs));
            const double w = R != nullptr ? (c == 0 ? mid + side : mid - side) : mid;
            (c == 0 ? L : R)[i] = (float) ((dry * (1.0 - m) + w * m) * g);
        }
    }
}

void MultiTypeFlangerProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::flanger_svg, IconData::flanger_svgSize);
    icon::drawSvg (g, b, svg.get());
}

// ===================================================================================== Phaser

const std::vector<MultiTypePhaserProcessor::TypeSpec>& MultiTypePhaserProcessor::types()
{
    static const std::vector<TypeSpec> t = {
        //  name                        order rate  depth fb    manual minHz maxHz mix phase tone vcr fbPt vibe bias
        { "Digital Mono",               4,  0.5f, 80,  30,   50,   200,  4000, 50, 0,   10,  1,  0,   false, 0 },
        { "Digital Stereo",             4,  0.5f, 80,  30,   50,   200,  4000, 50, 90,  10,  1,  0,   false, 0 },
        { "Script 45-Style",            2,  0.6f, 85,  0,    50,   150,  2500, 50, 0,   9,   2,  0,   false, 0 },
        { "Script 90-Style",            4,  0.6f, 85,  0,    50,   150,  2500, 50, 0,   9,   2,  0,   false, 0 },
        { "Block 90-Style",             4,  0.5f, 85,  35,   50,   150,  2500, 50, 0,   9,   2,  0,   false, 0 },
        { "Classic Vibe",               4,  1.2f, 80,  0,    50,   80,   2000, 50, 0,   8,   3,  0,   true,  0 },
        { "FAS Vibe",                   4,  1.6f, 85,  15,   50,   80,   2500, 50, 0,   9,   3,  0,   true,  0 },
        { "Stereo 8-Stage",             8,  0.3f, 80,  40,   50,   200,  5000, 50, 180, 10,  1,  0,   false, 0 },
        { "Small Stone-Style",          4,  0.4f, 80,  55,   50,   250,  4000, 50, 0,   9,   2,  0,   false, 0 },
        { "Bi-Phase-Style",             6,  0.3f, 85,  45,   50,   200,  4000, 50, 180, 10,  1,  0,   false, 0 },
        { "Ultra-Super-Mega Phaser",    12, 0.25f, 90, 70,   50,   100,  6000, 50, 90,  10,  1,  0,   false, 0 },
        { "Mutated Twin-Phaser",        6,  0.6f, 75,  -50,  50,   300,  3000, 50, 90,  9,   2,  3,   false, 0 },
    };
    return t;
}

MultiTypePhaserProcessor::MultiTypePhaserProcessor (int fixedTypeIndex)
    : MultiTypePhaserProcessor()
{
    if (fixedTypeIndex >= 0 && fixedTypeIndex < (int) types().size())
    {
        fixedName = types()[(size_t) fixedTypeIndex].name;
        mt::setParam (type, float (fixedTypeIndex));
        applyType (fixedTypeIndex);
        watcher.syncWithoutApplying();
    }
}

MultiTypePhaserProcessor::MultiTypePhaserProcessor()
{
    parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("multiTypePhaser", "Multi-Type Phaser", "|");
    auto add = adder();
    type = add (*parameters, choiceParam ("type", "Type", namesOf (types()), 3));
    rate = add (*parameters, floatParam ("rate", "Rate", 0.02f, 10, 0.6f, 0.5f, "Hz", 2));
    depth = add (*parameters, floatParam ("depth", "Depth", 0, 100, 85, 1.0f, "%", 0));
    feedback = add (*parameters, floatParam ("feedback", "Feedback", -100, 100, 0, 1.0f, "%", 0));
    manual = add (*parameters, floatParam ("manual", "Manual", 0, 100, 50, 1.0f, "%", 0));
    tone = add (*parameters, floatParam ("tone", "Tone", 0, 10, 9));
    mix = add (*parameters, floatParam ("mix", "Mix", 0, 100, 50, 1.0f, "%", 0));
    level = add (*parameters, floatParam ("level", "Level", -30, 12, 0, 1.0f, "dB"));
    auto* ex = parameters.get(); // one page: every control is a direct child
    order = add (*ex, floatParam ("order", "Order", 2, 12, 4, 1.0f, {}, 0));
    order->range.interval = 2.0f;
    feedbackPoint = add (*ex, floatParam ("fbPoint", "Feedback Point", 0, 12, 0, 1.0f, {}, 0));
    minFreq = add (*ex, floatParam ("minFreq", "Minimum Frequency", 20, 2000, 150, 0.4f, "Hz", 0));
    maxFreq = add (*ex, floatParam ("maxFreq", "Maximum Frequency", 500, 12000, 2500, 0.4f, "Hz", 0));
    lfoType = add (*ex, choiceParam ("lfoType", "LFO Type", lfoShapeNames(), 1));
    lfoPhase = add (*ex, floatParam ("lfoPhase", "LFO Phase", 0, 180, 0, 1.0f, "deg", 0));
    lfoQuantize = add (*ex, floatParam ("lfoQuantize", "LFO Quantize", 0, 32, 0, 1.0f, {}, 0));
    lfoHicut = add (*ex, floatParam ("lfoHicut", "LFO Hicut", 1, 100, 100, 0.5f, "Hz", 0));
    vcrType = add (*ex, choiceParam ("vcr", "VCR Type", { "LINEAR", "EXPONENTIAL", "JFET", "PHOTOCELL" }, 2));
    vcrBias = add (*ex, floatParam ("vcrBias", "VCR Bias", -1, 1, 0, 1.0f, {}, 2));
    exponent = add (*ex, floatParam ("exponent", "Exponent", 0.25f, 4, 1, 0.5f, {}, 2));
    photoAttack = add (*ex, floatParam ("photoAttack", "Photocell Attack", 1, 50, 5, 0.5f, "ms", 0));
    photoRelease = add (*ex, floatParam ("photoRelease", "Photocell Release", 5, 200, 35, 0.5f, "ms", 0));
    spread = add (*ex, floatParam ("spread", "Stereo Spread", -100, 200, 100, 1.0f, "%", 0));
    watcher.onTypeChanged = [this] (int t) { applyType (t); };
    watcher.attach (type);
    applyType (3);
}

void MultiTypePhaserProcessor::applyType (int index)
{
    const auto& t = types()[(size_t) juce::jlimit (0, (int) types().size() - 1, index)];
    setParam (order, (float) t.order); setParam (rate, t.rate); setParam (depth, t.depth); setParam (feedback, t.feedback);
    setParam (manual, t.manual); setParam (minFreq, t.minHz); setParam (maxFreq, t.maxHz); setParam (mix, t.mix);
    setParam (lfoPhase, t.phase); setParam (tone, t.tone); setParam (vcrType, (float) t.vcr); setParam (feedbackPoint, (float) t.fbPoint);
    setParam (vcrBias, t.bias); setParam (level, 0);
}

void MultiTypePhaserProcessor::setState (const juce::XmlElement& s) { EffectProcessor::setState (s); watcher.syncWithoutApplying(); }

void MultiTypePhaserProcessor::prepare (double sr, int, int) { watcher.poll(); fs = sr; mixLevel.prepare (fs); reset(); }

void MultiTypePhaserProcessor::reset()
{
    for (auto& r : ap) for (auto& v : r) v = 0;
    for (auto& r : apIn) for (auto& v : r) v = 0;
    fbSample[0] = fbSample[1] = photo[0] = photo[1] = 0;
    for (int c = 0; c < 2; ++c) { lfoSmooth[c].reset(); toneLp[c].reset(); }
}

void MultiTypePhaserProcessor::process (juce::AudioBuffer<float>& buffer)
{
    const auto& spec = types()[(size_t) juce::jlimit (0, (int) types().size() - 1, asIndex (type))];
    const int n = buffer.getNumSamples();
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    mixLevel.setTargets (mix->get(), level->get());
    lfo.set (rate->get(), fs);
    const int stages = juce::jlimit (2, kMaxStages, juce::roundToInt (order->get()));
    const int fbPt = juce::jlimit (0, stages, juce::roundToInt (feedbackPoint->get()));
    const double dep = depth->get() / 100.0, man = manual->get() / 100.0, fbk = feedback->get() / 100.0 * 0.95;
    const double lo = minFreq->get(), hi = juce::jmax (minFreq->get() + 10.0f, maxFreq->get());
    const int vcr = asIndex (vcrType);
    const double ph = lfoPhase->get() / 360.0, quant = lfoQuantize->get(), bias = vcrBias->get(), ex = exponent->get();
    const auto shape = (LfoShape) asIndex (lfoType);
    const double atk = std::exp (-1.0 / (photoAttack->get() * 1e-3 * fs)), rel = std::exp (-1.0 / (photoRelease->get() * 1e-3 * fs));
    // Uni-Vibe-style unequal stage spacing (its four stages sit on different capacitor values)
    static const double vibeSpread[kMaxStages] = { 1.0, 1.9, 3.4, 6.1, 1.0, 1.9, 3.4, 6.1, 1.0, 1.9, 3.4, 6.1 };
    for (int c = 0; c < 2; ++c) { lfoSmooth[c].set (lfoHicut->get(), fs); toneLp[c].set (1500.0 * std::pow (12.0, tone->get() / 10.0), fs); }

    for (int i = 0; i < n; ++i)
    {
        const double in[2] = { (double) L[i], R != nullptr ? (double) R[i] : (double) L[i] };
        double wet[2];
        for (int c = 0; c < 2; ++c)
        {
            double lv = 0.5 + 0.5 * quantizeLfo (lfo.next (shape, c == 1 ? ph : 0.0), quant);
            lv = lfoSmooth[c].lp (lv);
            double pos = juce::jlimit (0.0, 1.0, man * (1.0 - dep) + dep * lv + 0.25 * bias);
            double f;
            switch (vcr)
            {
                case 0: f = lo + (hi - lo) * pos; break;
                case 1: f = lo * std::pow (hi / lo, std::pow (pos, ex)); break;
                case 2: f = lo * std::pow (hi / lo, pos * pos * (3.0 - 2.0 * pos)); break; // JFET: soft ends
                default:
                {
                    // photocell: the light follows the LFO with asymmetric attack/release, resistance ~ 1/light
                    double& p = photo[c];
                    p = pos > p ? pos + (p - pos) * atk : pos + (p - pos) * rel;
                    f = lo * std::pow (hi / lo, std::pow (p, 0.7));
                    break;
                }
            }
            double x = in[c];
            for (int s = 0; s < stages; ++s)
            {
                if (s == fbPt) x += fbk * fbSample[c];
                const double fs_ = juce::jlimit (20.0, 0.45 * fs, f * (spec.vibe ? vibeSpread[s] : 1.0));
                const double t = std::tan (kPi * fs_ / fs);
                const double a = (t - 1.0) / (t + 1.0);
                const double y = a * x + apIn[c][s] - a * ap[c][s];
                apIn[c][s] = x;
                ap[c][s] = y;
                x = y;
            }
            fbSample[c] = x;
            wet[c] = toneLp[c].lp (x);
        }
        lfo.advance();
        const double sp = spread->get() / 100.0;
        const double mid = 0.5 * (wet[0] + wet[1]), side = 0.5 * (wet[0] - wet[1]) * sp;
        const double m = mixLevel.mix.getNextValue(), g = mixLevel.level.getNextValue();
        if (R != nullptr)
        {
            L[i] = (float) ((in[0] * (1.0 - m) + (mid + side) * m) * g);
            R[i] = (float) ((in[1] * (1.0 - m) + (mid - side) * m) * g);
        }
        else L[i] = (float) ((in[0] * (1.0 - m) + mid * m) * g);
    }
}

void MultiTypePhaserProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::phaser_svg, IconData::phaser_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
