#include "MultiTypePitchProcessor.h"
#include "MultiTypeIcons.h"
#include <IconData.h>

namespace openguitarmultifx
{
using namespace mt;

namespace
{
    constexpr double kMaxGrainSeconds = 0.12, kMaxVoiceDelaySeconds = 0.5;
    constexpr int kControlEvery = 16;
    constexpr int kMajor[7] { 0, 2, 4, 5, 7, 9, 11 };
    constexpr int kMinor[7] { 0, 2, 3, 5, 7, 8, 10 };
    using M = MultiTypePitchProcessor::Mode;
}

// Presets for the common pitch-block families; the values are this engine's own.
const std::vector<MultiTypePitchProcessor::TypeSpec>& MultiTypePitchProcessor::types()
{
    static const std::vector<TypeSpec> t = {
        //  name                 mode        n  shift              detune                   delay ms          level dB           pan                 steps         mix grain lfoR lfoD glide whammy
        { "Detune x2",        M::fixed,    2, { 0, 0, 0, 0 },    { -9, 9, 0, 0 },        { 0, 0, 0, 0 },   { 0, 0, 0, 0 },    { -100, 100, 0, 0 }, { 0, 0, 0, 0 },   50, 50, 0.4f, 6,  5,  0 },
        { "Detune x4",        M::fixed,    4, { 0, 0, 0, 0 },    { -16, -6, 6, 16 },     { 0, 0, 0, 0 },   { -3, -3, -3, -3 }, { -100, -35, 35, 100 }, { 0, 0, 0, 0 }, 55, 50, 0.3f, 8,  5,  0 },
        { "Interval x2",     M::fixed,    2, { 4, 7, 0, 0 },    { 0, 0, 0, 0 },        { 0, 0, 0, 0 },   { -3, -3, 0, 0 },  { -60, 60, 0, 0 },  { 0, 0, 0, 0 },   50, 55, 0, 0,    20, 0 },
        { "Interval x4",     M::fixed,    4, { 3, 5, 7, 12 },   { 0, 0, 0, 0 },        { 0, 0, 0, 0 },   { -4, -4, -4, -6 }, { -80, -30, 30, 80 }, { 0, 0, 0, 0 },  50, 55, 0, 0,    20, 0 },
        { "Scale Harmony x2",      M::diatonic, 2, { 0, 0, 0, 0 },    { 0, 0, 0, 0 },        { 0, 0, 0, 0 },   { -3, -3, 0, 0 },  { -60, 60, 0, 0 },  { 2, 4, 0, 0 },   50, 55, 0, 0,    60, 0 },
        { "Scale Harmony x4",      M::diatonic, 4, { 0, 0, 0, 0 },    { 0, 0, 0, 0 },        { 0, 0, 0, 0 },   { -4, -4, -4, -6 }, { -80, -30, 30, 80 }, { 2, 4, 6, 7 },  50, 55, 0, 0,    60, 0 },
        { "Whammy Pedal",     M::whammy,   1, { 0, 0, 0, 0 },    { 0, 0, 0, 0 },        { 0, 0, 0, 0 },   { 0, 0, 0, 0 },    { 0, 0, 0, 0 },     { 0, 0, 0, 0 },   100, 40, 0, 0,   10, 0 },
        { "Whammy Pedal x2",    M::whammy,   2, { 0, 0, 0, 0 },    { 0, 0, 0, 0 },        { 0, 0, 0, 0 },   { 0, -6, 0, 0 },   { -50, 50, 0, 0 },  { 0, 0, 0, 0 },   100, 40, 0, 0,   10, 4 },
        { "Sub Octave",     M::fixed,    2, { -12, -24, 0, 0 }, { 0, 0, 0, 0 },       { 0, 0, 0, 0 },   { 0, -6, 0, 0 },   { 0, 0, 0, 0 },     { 0, 0, 0, 0 },   50, 70, 0, 0,    10, 0 },
        { "Capo",       M::fixed,    1, { 2, 0, 0, 0 },    { 0, 0, 0, 0 },        { 0, 0, 0, 0 },   { 0, 0, 0, 0 },    { 0, 0, 0, 0 },    { 0, 0, 0, 0 },   100, 40, 0, 0,   10, 0 },
        { "Detune Echo",  M::fixed,    2, { 0, 0, 0, 0 },    { -12, 12, 0, 0 },     { 90, 180, 0, 0 }, { 0, -2, 0, 0 },   { -100, 100, 0, 0 }, { 0, 0, 0, 0 },   45, 50, 0.4f, 6, 5,  0 },
        { "Free Shifter",     M::fixed,    4, { 0, 0, 0, 0 },    { 0, 0, 0, 0 },        { 0, 0, 0, 0 },   { 0, -99, -99, -99 }, { 0, 0, 0, 0 },   { 0, 0, 0, 0 },   100, 50, 0, 0,   10, 0 },
    };
    return t;
}

MultiTypePitchProcessor::MultiTypePitchProcessor() : MultiTypePitchProcessor (-1) {}

// A fixed-type block shows only the controls that mean something for that type (no Type/Mode/Voices selectors, no
// Whammy pedal on a detuner, no Key/Scale unless it is diatonic); the generic block (-1) shows everything.
MultiTypePitchProcessor::MultiTypePitchProcessor (int fixedTypeIndex)
{
    const bool fixed = fixedTypeIndex >= 0 && fixedTypeIndex < (int) types().size();
    const TypeSpec* spec = fixed ? &types()[(size_t) fixedTypeIndex] : nullptr;
    juce::StringArray typeNames;
    for (const auto& t : types()) typeNames.add (t.name);
    parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("multiTypePitch", fixed ? juce::String (spec->name) : juce::String ("Multi-Type Pitch"), "|");

    // Every parameter always exists (the engine reads them all); `show` decides whether the panel gets it.
    auto add = [this] (juce::AudioProcessorParameterGroup& g, std::unique_ptr<juce::AudioParameterFloat> p, bool show = true)
    {
        auto* r = p.get();
        if (show) g.addChild (std::move (p));
        else hiddenParams.push_back (std::move (p));
        return r;
    };
    const bool isFixedMode = ! fixed || spec->mode == Mode::fixed;
    const bool isWhammy = ! fixed || spec->mode == Mode::whammy;
    const bool isDiatonic = ! fixed || spec->mode == Mode::diatonic;

    type = add (*parameters, choiceParam ("type", "Type", typeNames, 0), ! fixed);
    mode = add (*parameters, choiceParam ("mode", "Mode", { "FIXED", "WHAMMY", "DIATONIC" }, 0), ! fixed);
    voices = add (*parameters, floatParam ("voices", "Voices", 1, 4, 2, 1.0f, {}, 0), ! fixed);
    mix = add (*parameters, floatParam ("mix", "Mix", 0, 100, 50, 1.0f, "%", 0));
    level = add (*parameters, floatParam ("level", "Level", -30, 12, 0, 1.0f, "dB"));

    for (int v = 0; v < kVoices; ++v)
    {
        const auto n = juce::String (v + 1);
        const bool on = ! fixed || v < spec->voices;
        const auto lab = (fixed && spec->voices == 1) ? juce::String() : " " + n;
        auto* grp = parameters.get(); // one page: every control is a direct child
        shift[v] = add (*grp, floatParam ("shift" + n, "Shift" + lab, -24, 24, 0, 1.0f, "st", 0), on && isFixedMode);
        detune[v] = add (*grp, floatParam ("detune" + n, "Detune" + lab, -100, 100, 0, 1.0f, "ct", 0), on && (isFixedMode || isDiatonic) && ! isWhammy);
        step[v] = add (*grp, floatParam ("step" + n, "Harmony" + lab, -7, 7, 0, 1.0f, {}, 0), on && isDiatonic && fixed);
        delay[v] = add (*grp, floatParam ("delay" + n, "Delay" + lab, 0, 500, 0, 0.5f, "ms", 0), on && (isFixedMode || isDiatonic));
        voiceLevel[v] = add (*grp, floatParam ("level" + n, "Level" + lab, -99, 6, 0, 1.0f, "dB"), on && (! fixed || spec->voices > 1));
        pan[v] = add (*grp, floatParam ("pan" + n, "Pan" + lab, -100, 100, 0, 1.0f, "%", 0), on && (! fixed || spec->voices > 1));
    }

    auto* cfg = parameters.get(); // one page: every control is a direct child
    const bool lfoUsed = ! fixed || (spec->mode != Mode::whammy && spec->lfoDepth > 0.0f);
    grain = add (*cfg, floatParam ("grain", "Grain Size", 20, (float) kMaxGrainSeconds * 1000.0f, 50, 1.0f, "ms", 0));
    glide = add (*cfg, floatParam ("glide", "Glide", 1, 1000, 10, 0.4f, "ms", 0), ! fixed || spec->mode != Mode::fixed);
    lfoRate = add (*cfg, floatParam ("lfoRate", "LFO Rate", 0.05f, 10, 0.4f, 0.5f, "Hz", 2), lfoUsed);
    lfoDepth = add (*cfg, floatParam ("lfoDepth", "LFO Depth", 0, 50, 0, 1.0f, "ct", 0), lfoUsed);
    lowCut = add (*cfg, floatParam ("lowCut", "Low Cut", 20, 2000, 20, 0.4f, "Hz", 0));
    highCut = add (*cfg, floatParam ("highCut", "High Cut", 500, 20000, 20000, 0.4f, "Hz", 0));
    whammyPos = add (*cfg, floatParam ("whammyPos", "Whammy Pedal", 0, 100, 0, 1.0f, "%", 0), isWhammy);
    whammyRange = add (*cfg, choiceParam ("whammyRange", "Whammy Range", whammyRangeNames(), 0), isWhammy);
    key = add (*cfg, choiceParam ("key", "Key", { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0), isDiatonic);
    scale = add (*cfg, choiceParam ("scale", "Scale", { "MAJOR", "MINOR" }, 0), isDiatonic);

    watcher.onTypeChanged = [this] (int t) { applyType (t); };
    watcher.attach (type);
    applyType (fixed ? fixedTypeIndex : 0);
    if (fixed)
    {
        fixedName = spec->name;
        mt::setParam (type, float (fixedTypeIndex));
        watcher.syncWithoutApplying();
    }
}

void MultiTypePitchProcessor::applyType (int index)
{
    const auto& t = types()[(size_t) juce::jlimit (0, (int) types().size() - 1, index)];
    setParam (mode, (float) (int) t.mode);
    setParam (voices, (float) t.voices);
    for (int v = 0; v < kVoices; ++v)
    {
        setParam (shift[v], t.shift[v]); setParam (detune[v], t.detune[v]); setParam (delay[v], t.delayMs[v]);
        setParam (voiceLevel[v], t.levelDb[v]); setParam (pan[v], t.pan[v]); setParam (step[v], (float) t.steps[v]);
    }
    setParam (mix, t.mix); setParam (grain, t.grainMs); setParam (lfoRate, t.lfoRate); setParam (lfoDepth, t.lfoDepth);
    setParam (glide, t.glideMs); setParam (whammyRange, (float) t.whammyRange);
    setParam (lowCut, 20); setParam (highCut, 20000); setParam (level, 0); setParam (whammyPos, 0);
}

void MultiTypePitchProcessor::setState (const juce::XmlElement& state)
{
    EffectProcessor::setState (state);
    watcher.syncWithoutApplying();
}

// --- detector -----------------------------------------------------------------------------------------------------

void MultiTypePitchProcessor::Detector::prepare (double fs)
{
    fsd = fs / dec;
    coeff = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * 1000.0f / (float) fs);
    reset();
}

void MultiTypePitchProcessor::Detector::reset()
{
    lp = 0; phase = 0; w = 0; count = 0; hz = 0;
    ring.fill (0.0f);
}

bool MultiTypePitchProcessor::Detector::push (float x)
{
    lp += coeff * (x - lp);
    if (++phase < dec) return false;
    phase = 0;
    ring[(size_t) w] = lp;
    w = (w + 1) % ringN;
    if (++count < hop) return false;
    count = 0;
    analyse();
    return true;
}

void MultiTypePitchProcessor::Detector::analyse()
{
    const int tauMin = juce::jmax (2, (int) (fsd / 700.0));
    const int tauMax = juce::jmin (ringN - win - 2, (int) (fsd / 40.0));
    auto at = [this] (int i) { return ring[(size_t) (((i % ringN) + ringN) % ringN)]; };
    const int start = w - win; // the newest window

    double energy = 0.0;
    for (int j = 0; j < win; ++j) energy += (double) at (start + j) * at (start + j);
    if (energy / win < 2.5e-5) { hz = 0.0f; return; } // ~ -46 dBFS rms: nothing to track

    double running = 0.0;
    cmnd[0] = 1.0f;
    int best = -1;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        double d = 0.0;
        for (int j = 0; j < win; ++j)
        {
            const double diff = (double) at (start + j) - at (start + j - tau);
            d += diff * diff;
        }
        running += d;
        cmnd[(size_t) tau] = running > 0.0 ? (float) (d * tau / running) : 1.0f;
        if (tau >= tauMin && best < 0 && tau > 1 && cmnd[(size_t) tau] < 0.15f && cmnd[(size_t) tau] <= cmnd[(size_t) tau - 1])
            best = tau;
        else if (best >= 0 && tau == best + 1)
        {
            // walk down to the local minimum
            if (cmnd[(size_t) tau] < cmnd[(size_t) best]) best = tau;
            else break;
        }
        else if (best >= 0 && tau > best + 1 && cmnd[(size_t) tau] < cmnd[(size_t) best]) best = tau;
        else if (best >= 0) break;
    }
    if (best < 0) return; // keep the previous estimate
    float t = (float) best;
    if (best > 1 && best < tauMax)
    {
        const float a = cmnd[(size_t) best - 1], b = cmnd[(size_t) best], c = cmnd[(size_t) best + 1];
        const float den = a - 2.0f * b + c;
        if (std::abs (den) > 1e-9f) t += 0.5f * (a - c) / den;
    }
    hz = (float) (fsd / t);
}

// --- shifts -------------------------------------------------------------------------------------------------------

float MultiTypePitchProcessor::whammyShift (float p) const
{
    const float x = juce::jlimit (0.0f, 1.0f, p);
    switch (asIndex (whammyRange))
    {
        case 0: return 12.0f * x;
        case 1: return -12.0f * x;
        case 2: return 24.0f * x;
        case 3: return -24.0f * x;
        case 4: return 12.0f * (2.0f * x - 1.0f);
        default: return 24.0f * (2.0f * x - 1.0f);
    }
}

void MultiTypePitchProcessor::updateDiatonicShifts()
{
    const float f = detector.hz;
    detectedHz.store (f, std::memory_order_relaxed);
    if (f < 30.0f) return; // unvoiced: keep the last interval
    const int midi = juce::roundToInt (69.0 + 12.0 * std::log2 ((double) f / 440.0));
    const int pc = (((midi - asIndex (key)) % 12) + 12) % 12;
    const int* sc = asIndex (scale) == 0 ? kMajor : kMinor;
    int d = 0;
    for (int i = 0; i < 7; ++i) if (sc[i] <= pc) d = i; // highest scale tone at or below the played note
    for (int v = 0; v < kVoices; ++v)
    {
        const int t = d + juce::roundToInt (step[v]->get());
        const int oct = (int) std::floor ((double) t / 7.0), idx = ((t % 7) + 7) % 7;
        diatonicShift[v] = (float) (sc[idx] + 12 * oct - sc[d]);
    }
}

juce::String MultiTypePitchProcessor::getStatusText() const
{
    if (asIndex (mode) != (int) Mode::diatonic) return {};
    const float f = detectedHz.load (std::memory_order_relaxed);
    if (f < 30.0f) return "Detected: --";
    static const char* names[12] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int midi = juce::roundToInt (69.0 + 12.0 * std::log2 ((double) f / 440.0));
    return "Detected: " + juce::String (names[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1) + "  (" + juce::String (f, 1) + " Hz)";
}

// --- audio --------------------------------------------------------------------------------------------------------

void MultiTypePitchProcessor::prepare (double sampleRateHz, int, int)
{
    watcher.poll();
    fs = sampleRateHz;
    line.prepare ((int) ((kMaxGrainSeconds + kMaxVoiceDelaySeconds + 0.1) * fs));
    detector.prepare (fs);
    mixLevel.prepare (fs);
    reset();
}

void MultiTypePitchProcessor::reset()
{
    line.reset();
    detector.reset();
    detectedHz.store (0.0f);
    for (auto& f : lc) f.reset();
    for (auto& f : hc) f.reset();
    for (int v = 0; v < kVoices; ++v) { phi[v] = 0.0; semis[v] = 0.0; ratio[v] = 1.0; diatonicShift[v] = 0.0f; }
    ratioCounter = 0;
}

void MultiTypePitchProcessor::process (juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    const int nc = buffer.getNumChannels();
    float* L = buffer.getWritePointer (0);
    float* R = nc > 1 ? buffer.getWritePointer (1) : nullptr;

    const int nVoices = juce::jlimit (1, kVoices, juce::roundToInt (voices->get()));
    const auto m = (Mode) asIndex (mode);
    mixLevel.setTargets (mix->get(), level->get());
    const double G = grain->get() * 1e-3 * fs;
    const double invG = 1.0 / G;
    const double glideCoef = std::exp (-1.0 / (juce::jmax (1.0f, glide->get()) * 1e-3 * fs * 0.5));
    const double base = 2.0;
    const double lfoCents = lfoDepth->get();
    lfo.set (lfoRate->get(), fs);
    for (int c = 0; c < 2; ++c) { lc[c].set (lowCut->get(), fs); hc[c].set (highCut->get(), fs); }

    double gainL[kVoices], gainR[kVoices], vDelay[kVoices], target[kVoices];
    for (int v = 0; v < nVoices; ++v)
    {
        const double g = voiceLevel[v]->get() <= -98.0f ? 0.0 : juce::Decibels::decibelsToGain ((double) voiceLevel[v]->get());
        const double p = (pan[v]->get() / 100.0 + 1.0) * 0.25 * kPi; // 0 .. pi/2
        gainL[v] = g * std::cos (p);
        gainR[v] = g * std::sin (p);
        vDelay[v] = delay[v]->get() * 1e-3 * fs;
        target[v] = m == Mode::whammy ? (double) whammyShift (whammyPos->get() / 100.0f)
                                      : m == Mode::diatonic ? (double) diatonicShift[v] : (double) shift[v]->get();
    }

    for (int i = 0; i < n; ++i)
    {
        const float inL = L[i], inR = R != nullptr ? R[i] : inL;
        const float mono = 0.5f * (inL + inR);
        line.push (mono);
        if (detector.push (mono) && m == Mode::diatonic)
        {
            updateDiatonicShifts();
            for (int v = 0; v < nVoices; ++v) target[v] = (double) diatonicShift[v];
        }

        if (ratioCounter == 0)
        {
            const double l = lfo.next (LfoShape::sine);
            for (int v = 0; v < nVoices; ++v)
            {
                const double mod = lfoCents * (v & 1 ? -l : l);
                semis[v] = target[v] + (semis[v] - target[v]) * std::pow (glideCoef, kControlEvery);
                ratio[v] = std::exp2 ((semis[v] + (detune[v]->get() + mod) / 100.0) / 12.0);
            }
        }
        ratioCounter = (ratioCounter + 1) % kControlEvery;
        lfo.advance();

        double wetL = 0.0, wetR = 0.0;
        for (int v = 0; v < nVoices; ++v)
        {
            phi[v] += (1.0 - ratio[v]) * invG;
            phi[v] -= std::floor (phi[v]);
            double q = phi[v] + 0.5;
            q -= std::floor (q);
            const double tri = 1.0 - std::abs (2.0 * phi[v] - 1.0);
            const double w1 = tri * tri * (3.0 - 2.0 * tri); // smoothstep(tri); w1(phi) + w1(phi + 0.5) == 1
            const float a = line.read (base + vDelay[v] + phi[v] * G);
            const float b = line.read (base + vDelay[v] + q * G);
            const double y = w1 * a + (1.0 - w1) * b;
            wetL += y * gainL[v];
            wetR += y * gainR[v];
        }
        wetL = hc[0].lp (lc[0].hp (wetL));
        wetR = hc[1].lp (lc[1].hp (wetR));

        const double mx = mixLevel.mix.getNextValue(), g = mixLevel.level.getNextValue();
        L[i] = (float) ((inL * (1.0 - mx) + wetL * mx) * g);
        if (R != nullptr) R[i] = (float) ((inR * (1.0 - mx) + wetR * mx) * g);
        else L[i] = (float) ((inL * (1.0 - mx) + 0.5 * (wetL + wetR) * mx) * g);
    }
}

void MultiTypePitchProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    mt::drawIconByKey (g, b, mt::pitchIconKey (types()[(size_t) juce::jlimit (0, (int) types().size() - 1, asIndex (type))].name));
}

} // namespace openguitarmultifx
