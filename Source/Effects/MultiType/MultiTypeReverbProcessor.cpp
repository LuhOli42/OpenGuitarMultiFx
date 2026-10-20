#include "MultiTypeReverbProcessor.h"
#include "MultiTypeIcons.h"
#include <IconData.h>

namespace openguitarmultifx
{
using namespace mt;

namespace
{
    // Mutually prime-ish base lengths (ms) for the 8 late lines and the 12 early taps.
    constexpr double kLineMs[8] = { 29.7, 37.1, 41.1, 43.7, 47.9, 53.3, 59.1, 67.7 };
    constexpr double kTapMs[12] = { 4.3, 7.9, 11.3, 13.7, 17.9, 21.1, 26.3, 31.7, 37.9, 43.1, 51.3, 61.9 };
    constexpr double kInDiffMs[4] = { 4.77, 3.59, 12.73, 9.31 };
}

// Type list follows the common reverb families (rooms, chambers, halls, plates, springs...);
// the values are this engine's own presets.
const std::vector<MultiTypeReverbProcessor::TypeSpec>& MultiTypeReverbProcessor::types()
{
    static const std::vector<TypeSpec> t = {
        //  name             spring time  size pre  early late hf   lf   xover dens eDif inDif wall modD modR lowC highC pMix sh1 sh2 pFb springs
        { "Small Room",      false, 0.5f, 25, 2,   70,  60,  0.5f, 1.0f, 300, 80, 70, 60, 60, 10, 0.6f, 80, 9000, 0, 0, 0, 0, 0 },
        { "Medium Room",     false, 0.8f, 40, 5,   65,  70,  0.5f, 1.0f, 300, 80, 70, 65, 60, 12, 0.6f, 80, 8500, 0, 0, 0, 0, 0 },
        { "Large Room",      false, 1.3f, 55, 8,   60,  75,  0.55f, 1.1f, 300, 85, 70, 70, 65, 15, 0.5f, 80, 8000, 0, 0, 0, 0, 0 },
        { "Studio",          false, 0.9f, 45, 4,   55,  70,  0.7f, 0.9f, 400, 85, 60, 60, 60, 10, 0.6f, 100, 11000, 0, 0, 0, 0, 0 },
        { "Small Chamber",   false, 1.2f, 40, 6,   50,  80,  0.6f, 1.0f, 350, 90, 75, 75, 70, 15, 0.5f, 100, 9000, 0, 0, 0, 0, 0 },
        { "Large Chamber",   false, 2.0f, 60, 10,  45,  85,  0.6f, 1.1f, 350, 90, 75, 75, 70, 18, 0.5f, 100, 8500, 0, 0, 0, 0, 0 },
        { "Small Hall",      false, 1.6f, 60, 12,  45,  85,  0.5f, 1.2f, 250, 90, 70, 80, 75, 20, 0.4f, 80, 8000, 0, 0, 0, 0, 0 },
        { "Medium Hall",     false, 2.2f, 75, 18,  40,  90,  0.5f, 1.2f, 250, 90, 70, 80, 75, 22, 0.4f, 80, 7500, 0, 0, 0, 0, 0 },
        { "Large Hall",      false, 3.0f, 85, 25,  40,  90,  0.45f, 1.3f, 250, 95, 70, 85, 80, 25, 0.35f, 80, 7000, 0, 0, 0, 0, 0 },
        { "Concert Hall",    false, 2.8f, 90, 30,  55,  85,  0.5f, 1.2f, 250, 95, 75, 85, 80, 25, 0.35f, 80, 7500, 0, 0, 0, 0, 0 },
        { "Cathedral",       false, 6.0f, 100, 40, 35,  95,  0.4f, 1.4f, 200, 95, 80, 90, 85, 30, 0.3f, 60, 6000, 0, 0, 0, 0, 0 },
        { "Small Plate",     false, 1.2f, 30, 0,   0,   100, 0.8f, 0.8f, 500, 100, 0, 85, 90, 15, 0.8f, 150, 12000, 0, 0, 0, 0, 0 },
        { "Large Plate",     false, 2.5f, 50, 10,  0,   100, 0.75f, 0.8f, 500, 100, 0, 85, 90, 18, 0.7f, 150, 11000, 0, 0, 0, 0, 0 },
        { "Vocal Plate",     false, 1.8f, 40, 20,  0,   100, 0.7f, 0.7f, 600, 100, 0, 80, 90, 15, 0.7f, 200, 10000, 0, 0, 0, 0, 0 },
        { "Ambience",        false, 0.4f, 30, 0,   90,  35,  0.5f, 1.0f, 300, 70, 85, 50, 50, 5, 0.5f, 100, 10000, 0, 0, 0, 0, 0 },
        { "Shimmer Verb",    false, 4.0f, 85, 20,  30,  90,  0.6f, 1.0f, 250, 95, 70, 85, 80, 25, 0.4f, 150, 9000, 40, 12, 24, 40, 0 },
        { "Modulated Hall",  false, 2.5f, 80, 15,  35,  90,  0.5f, 1.2f, 250, 90, 70, 80, 75, 70, 0.9f, 80, 7500, 0, 0, 0, 0, 0 },
        { "Small Spring",    true,  1.5f, 30, 0,   0,   100, 0.6f, 1.0f, 300, 0, 0, 0, 0, 0, 0.5f, 150, 5000, 0, 0, 0, 0, 2 },
        { "Medium Spring",   true,  2.2f, 50, 0,   0,   100, 0.6f, 1.0f, 300, 0, 0, 0, 0, 0, 0.5f, 120, 4500, 0, 0, 0, 0, 3 },
        { "Large Spring",    true,  3.0f, 75, 0,   0,   100, 0.6f, 1.0f, 300, 0, 0, 0, 0, 0, 0.5f, 100, 4000, 0, 0, 0, 0, 3 },
    };
    return t;
}

MultiTypeReverbProcessor::MultiTypeReverbProcessor (int fixedTypeIndex)
    : MultiTypeReverbProcessor()
{
    if (fixedTypeIndex >= 0 && fixedTypeIndex < (int) types().size())
    {
        fixedName = types()[(size_t) fixedTypeIndex].name;
        mt::setParam (type, float (fixedTypeIndex));
        applyType (fixedTypeIndex);
        watcher.syncWithoutApplying();
    }
}

MultiTypeReverbProcessor::MultiTypeReverbProcessor()
{
    juce::StringArray typeNames;
    for (const auto& t : types()) typeNames.add (t.name);
    parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("multiTypeReverb", "Multi-Type Reverb", "|");
    auto add = [] (juce::AudioProcessorParameterGroup& g, std::unique_ptr<juce::AudioParameterFloat> p) { auto* r = p.get(); g.addChild (std::move (p)); return r; };

    type = add (*parameters, choiceParam ("type", "Type", typeNames, 7));
    time = add (*parameters, floatParam ("time", "Time", 0.1f, 30, 2.2f, 0.4f, "s", 2));
    size = add (*parameters, floatParam ("size", "Size", 0, 100, 75, 1.0f, "%", 0));
    predelay = add (*parameters, floatParam ("predelay", "Pre-Delay", 0, 250, 18, 0.6f, "ms", 0));
    mix = add (*parameters, floatParam ("mix", "Mix", 0, 100, 25, 1.0f, "%", 0));
    level = add (*parameters, floatParam ("level", "Level", -30, 12, 0, 1.0f, "dB"));

    auto* adv = parameters.get(); // one page: every control is a direct child
    earlyLevel = add (*adv, floatParam ("earlyLevel", "Early Level", 0, 100, 40, 1.0f, "%", 0));
    lateLevel = add (*adv, floatParam ("lateLevel", "Late Level", 0, 100, 90, 1.0f, "%", 0));
    hfTime = add (*adv, floatParam ("hfTime", "HF Decay Time", 0.1f, 1.0f, 0.5f, 1.0f, "x", 2));
    lfTime = add (*adv, floatParam ("lfTime", "LF Decay Time", 0.5f, 2.0f, 1.2f, 1.0f, "x", 2));
    crossover = add (*adv, floatParam ("crossover", "LF Crossover", 50, 2000, 250, 0.4f, "Hz", 0));
    echoDensity = add (*adv, floatParam ("density", "Echo Density", 0, 100, 90, 1.0f, "%", 0));
    earlyDiffusion = add (*adv, floatParam ("earlyDiff", "Early Diffusion", 0, 100, 70, 1.0f, "%", 0));
    earlyDiffTime = add (*adv, floatParam ("earlyDiffTime", "Early Diffusion Time", 0, 100, 50, 1.0f, "%", 0));
    earlyDecay = add (*adv, floatParam ("earlyDecay", "Early Decay", 0, 100, 50, 1.0f, "%", 0));
    lateInputMix = add (*adv, floatParam ("lateInputMix", "Late Input Mix", 0, 100, 30, 1.0f, "%", 0));
    wallDiffusion = add (*adv, floatParam ("wallDiff", "Wall Diffusion", 0, 100, 75, 1.0f, "%", 0));
    inputDiffusion = add (*adv, floatParam ("inputDiff", "Input Diffusion", 0, 100, 80, 1.0f, "%", 0));
    inputDiffTime = add (*adv, floatParam ("inputDiffTime", "Input Diffusion Time", 0, 100, 50, 1.0f, "%", 0));
    micSpacing = add (*adv, floatParam ("micSpacing", "Mic Spacing", 0, 100, 50, 1.0f, "%", 0));
    stackHold = add (*adv, choiceParam ("stackHold", "Stack/Hold", { "OFF", "STACK", "HOLD" }, 0));
    predelayFeedback = add (*adv, floatParam ("pdFeedback", "Pre-Delay Feedback", 0, 90, 0, 1.0f, "%", 0));
    predelayMix = add (*adv, floatParam ("pdMix", "Pre-Delay Mix", 0, 100, 0, 1.0f, "%", 0));
    spread = add (*adv, floatParam ("spread", "Stereo Spread", -100, 200, 100, 1.0f, "%", 0));
    inputGain = add (*adv, floatParam ("inputGain", "Input Gain", 0, 100, 100, 1.0f, "%", 0));

    auto* mod = parameters.get(); // one page: every control is a direct child
    modDepth = add (*mod, floatParam ("modDepth", "Mod Depth", 0, 100, 20, 1.0f, "%", 0));
    modRate = add (*mod, floatParam ("modRate", "Mod Rate", 0.05f, 5, 0.4f, 0.5f, "Hz", 2));
    lfoPhase = add (*mod, floatParam ("lfoPhase", "LFO Phase", 0, 180, 90, 1.0f, "deg", 0));
    pitchMix = add (*mod, floatParam ("pitchMix", "Pitch Mix", 0, 100, 0, 1.0f, "%", 0));
    shift1 = add (*mod, floatParam ("shift1", "Voice 1 Shift", -24, 24, 12, 1.0f, "st", 0));
    shift2 = add (*mod, floatParam ("shift2", "Voice 2 Shift", -24, 24, 24, 1.0f, "st", 0));
    voiceBalance = add (*mod, floatParam ("voiceBalance", "Voice Balance", -100, 100, 0, 1.0f, "%", 0));
    spliceTime = add (*mod, floatParam ("splice", "Splice Time", 10, 200, 60, 0.6f, "ms", 0));
    pitchFeedback = add (*mod, floatParam ("pitchFb", "Pitch Feedback", 0, 90, 0, 1.0f, "%", 0));
    duckAtten = add (*mod, floatParam ("duckAtten", "Ducker Atten", 0, 40, 0, 1.0f, "dB"));
    duckThreshold = add (*mod, floatParam ("duckThreshold", "Ducker Threshold", -60, 0, -30, 1.0f, "dB"));
    duckRelease = add (*mod, floatParam ("duckRelease", "Ducker Release", 10, 1000, 300, 0.5f, "ms", 0));
    numSprings = add (*mod, floatParam ("springs", "Number of Springs", 1, 6, 3, 1.0f, {}, 0));
    springTone = add (*mod, floatParam ("springTone", "Spring Tone", 0, 10, 5));
    springDrive = add (*mod, floatParam ("springDrive", "Spring Drive", 0, 10, 2));
    boing = add (*mod, floatParam ("boing", "Boiiinnng!", 0, 10, 5));

    auto* eq = parameters.get(); // one page: every control is a direct child
    lowCut = add (*eq, floatParam ("lowCut", "Low Cut", 20, 1000, 80, 0.4f, "Hz", 0));
    highCut = add (*eq, floatParam ("highCut", "High Cut", 1000, 20000, 7500, 0.4f, "Hz", 0));
    lowMidFreq = add (*eq, floatParam ("lowMidFreq", "Low Mid Freq", 20, 2000, 300, 0.4f, "Hz", 0));
    lowMidGain = add (*eq, floatParam ("lowMidGain", "Low Mid Gain", -12, 12, 0, 1.0f, "dB"));
    highMidFreq = add (*eq, floatParam ("highMidFreq", "High Mid Freq", 100, 10000, 2500, 0.4f, "Hz", 0));
    highMidGain = add (*eq, floatParam ("highMidGain", "High Mid Gain", -12, 12, 0, 1.0f, "dB"));

    watcher.onTypeChanged = [this] (int t) { applyType (t); };
    watcher.attach (type);
    applyType (7);
}

void MultiTypeReverbProcessor::applyType (int index)
{
    const auto& t = types()[(size_t) juce::jlimit (0, (int) types().size() - 1, index)];
    setParam (time, t.time); setParam (size, t.size); setParam (predelay, t.predelay);
    setParam (earlyLevel, t.early); setParam (lateLevel, t.late); setParam (hfTime, t.hfMult); setParam (lfTime, t.lfMult);
    setParam (crossover, t.crossover); setParam (echoDensity, t.density); setParam (earlyDiffusion, t.earlyDiff);
    setParam (inputDiffusion, t.inputDiff); setParam (wallDiffusion, t.wallDiff); setParam (modDepth, t.modDepth);
    setParam (modRate, t.modRate); setParam (lowCut, t.lowCut); setParam (highCut, t.highCut); setParam (pitchMix, t.pitchMix);
    setParam (shift1, t.shift1); setParam (shift2, t.shift2); setParam (pitchFeedback, t.pitchFb);
    if (t.spring) setParam (numSprings, t.springs);
    setParam (lowMidGain, 0); setParam (highMidGain, 0); setParam (stackHold, 0); setParam (predelayFeedback, 0);
    setParam (predelayMix, 0); setParam (mix, t.spring ? 30.0f : 25.0f); setParam (level, 0);
}

void MultiTypeReverbProcessor::setState (const juce::XmlElement& state)
{
    EffectProcessor::setState (state);
    watcher.syncWithoutApplying();
}

void MultiTypeReverbProcessor::prepare (double sampleRateHz, int, int)
{
    watcher.poll();
    fs = sampleRateHz;
    preLine.prepare ((int) (0.3 * fs));
    erLine.prepare ((int) (0.2 * fs));
    for (auto& l : lines) l.prepare ((int) (0.25 * fs));
    for (auto& d : inDiff) d.prepare ((int) (0.04 * fs));
    for (auto& c : erDiff) for (auto& d : c) d.prepare ((int) (0.03 * fs));
    for (auto& w : wall) w.prepare ((int) (0.02 * fs));
    for (auto& s : springLine) s.prepare ((int) (0.12 * fs));
    for (auto& p : pitchLine) p.prepare ((int) (0.25 * fs));
    for (int i = 0; i < kLines; ++i) lfo[i].phase = i / double (kLines);
    mixLevel.prepare (fs);
    reset();
}

void MultiTypeReverbProcessor::reset()
{
    preLine.reset(); erLine.reset();
    for (auto& l : lines) l.reset();
    for (auto& d : inDiff) d.reset();
    for (auto& c : erDiff) for (auto& d : c) d.reset();
    for (auto& w : wall) w.reset();
    for (auto& s : springLine) s.reset();
    for (auto& p : pitchLine) p.reset();
    for (auto& s : lowSplit) s.reset();
    for (auto& s : highSplit) s.reset();
    for (auto& r : springState) for (auto& v : r) v = 0;
    for (auto& r : springIn) for (auto& v : r) v = 0;
    for (auto& v : springFb) v = 0;
    for (int c = 0; c < 2; ++c) { eqLow[c].reset(); eqHigh[c].reset(); eqP1[c].reset(); eqP2[c].reset(); }
    duckEnv = lateOut = 0;
}

void MultiTypeReverbProcessor::updateTank()
{
    const double sz = 0.3 + 2.2 * size->get() / 100.0;
    const double t60 = juce::jmax (0.05f, time->get());
    for (int i = 0; i < kLines; ++i)
    {
        lineLen[i] = kLineMs[i] * sz * 1e-3 * fs;
        const double secs = lineLen[i] / fs;
        auto g = [&] (double mult) { return std::pow (10.0, -3.0 * secs / (t60 * mult)); };
        gLow[i] = g (lfTime->get());
        gMid[i] = g (1.0);
        gHigh[i] = g (hfTime->get());
        lowSplit[i].set (crossover->get(), fs);
        highSplit[i].set (3500.0 / std::sqrt (sz), fs);
        wall[i].setLength ((int) ((2.0 + 7.0 * ((i * 7) % 8) / 8.0) * 1e-3 * fs * (0.5 + 0.5 * sz)));
        wall[i].g = 0.7 * wallDiffusion->get() / 100.0;
        lfo[i].set (modRate->get() * (0.85 + 0.05 * i), fs);
    }
    const double spacing = micSpacing->get() / 100.0;
    const double eTime = (0.4 + 1.2 * earlyDiffTime->get() / 100.0) * sz;
    const double eDecay = 0.5 + 3.0 * earlyDecay->get() / 100.0;
    for (int c = 0; c < 2; ++c)
        for (int k = 0; k < kTaps; ++k)
        {
            const double ms = kTapMs[k] * eTime * (c == 0 ? 1.0 : 1.0 + 0.15 * spacing * ((k % 3) - 1));
            tapTime[c][k] = ms * 1e-3 * fs;
            tapGain[c][k] = std::exp (-eDecay * k / double (kTaps)) * ((k + c) % 2 == 0 ? 1.0 : -1.0) / std::sqrt ((double) kTaps);
        }
    for (int k = 0; k < 4; ++k)
    {
        inDiff[k].setLength ((int) (kInDiffMs[k] * (0.3 + 1.4 * inputDiffTime->get() / 100.0) * 1e-3 * fs));
        inDiff[k].g = 0.75 * inputDiffusion->get() / 100.0;
    }
    for (int c = 0; c < 2; ++c)
    {
        erDiff[c][0].setLength ((int) ((3.1 + c * 0.7) * 1e-3 * fs * sz)); erDiff[c][0].g = 0.7 * earlyDiffusion->get() / 100.0;
        erDiff[c][1].setLength ((int) ((5.3 + c * 0.9) * 1e-3 * fs * sz)); erDiff[c][1].g = 0.7 * earlyDiffusion->get() / 100.0;
        eqLow[c].highpass (lowCut->get(), fs);
        eqHigh[c].lowpass (highCut->get(), fs);
        eqP1[c].peak (lowMidFreq->get(), fs, lowMidGain->get(), 0.7);
        eqP2[c].peak (highMidFreq->get(), fs, highMidGain->get(), 0.7);
    }
    const double springSize = 0.025 + 0.045 * size->get() / 100.0;
    for (int s = 0; s < kSprings; ++s)
    {
        springTone_[s].set (1500.0 + 650.0 * springTone->get(), fs);
        const double len = springSize * (1.0 + 0.13 * s);
        springFb[s] = std::pow (10.0, -3.0 * len / t60);
    }
}

// Two-tap rotating-delay pitch shifter (sin^2 windows, the taps half a splice apart).
float MultiTypeReverbProcessor::pitchShift (int voice, float x, double ratio)
{
    auto& line = pitchLine[voice];
    line.push (x);
    const double splice = spliceTime->get() * 1e-3 * fs;
    double& p = pitchPhase[voice];
    p += (1.0 - ratio) / splice;
    p -= std::floor (p);
    double y = 0;
    for (int g = 0; g < 2; ++g)
    {
        double q = p + 0.5 * g;
        q -= std::floor (q);
        y += std::sin (kPi * q) * std::sin (kPi * q) * line.read (4.0 + q * splice);
    }
    return (float) y;
}

void MultiTypeReverbProcessor::process (juce::AudioBuffer<float>& buffer)
{
    updateTank();
    const auto& spec = types()[(size_t) juce::jlimit (0, (int) types().size() - 1, asIndex (type))];
    const int n = buffer.getNumSamples();
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    mixLevel.setTargets (mix->get(), level->get());
    const double inG = inputGain->get() / 100.0;
    const double pd = juce::jmax (1.0, predelay->get() * 1e-3 * fs), pdFb = predelayFeedback->get() / 100.0, pdMix = predelayMix->get() / 100.0;
    const double eL = earlyLevel->get() / 100.0, lL = lateLevel->get() / 100.0, lateIn = lateInputMix->get() / 100.0;
    const double dens = echoDensity->get() / 100.0;
    const double modD = modDepth->get() / 100.0 * 0.0015 * fs;
    const double phOff = lfoPhase->get() / 360.0;
    const int hold = asIndex (stackHold);
    const double pMix = pitchMix->get() / 100.0, pFb = pitchFeedback->get() / 100.0;
    const double r1 = std::pow (2.0, shift1->get() / 12.0), r2 = std::pow (2.0, shift2->get() / 12.0);
    const double bal = voiceBalance->get() / 100.0;
    const double sp = spread->get() / 100.0;
    const double duckThr = juce::Decibels::decibelsToGain ((double) duckThreshold->get());
    const double duckDepth = juce::Decibels::decibelsToGain (-(double) duckAtten->get());
    const double duckRel = std::exp (-1.0 / (duckRelease->get() * 1e-3 * fs));
    const int springs = juce::roundToInt (numSprings->get());
    const double sDrive = 0.3 + springDrive->get() * 0.5;
    const double disp = 0.35 + 0.05 * boing->get();
    const double springLen = (0.025 + 0.045 * size->get() / 100.0) * fs;

    for (int i = 0; i < n; ++i)
    {
        const double inL = L[i], inR = R != nullptr ? R[i] : L[i];
        double x = 0.5 * (inL + inR) * inG;
        const double pk = std::max (std::abs (inL), std::abs (inR));
        duckEnv = pk > duckEnv ? pk : duckEnv * duckRel;
        if (hold == 2) x = 0.0;

        // pre-delay with its own feedback; its taps can also be sent around the reverb
        const double pdOut = preLine.read (pd);
        preLine.push ((float) (x + pdFb * pdOut));
        double wetL = 0, wetR = 0;

        if (spec.spring)
        {
            // Spring tank: drive, then per spring a delay loop with a dispersive allpass chain (the "chirp").
            const double drive = std::tanh (pdOut * sDrive) / sDrive;
            for (int s = 0; s < springs; ++s)
            {
                double v = drive + springFb[s] * springLine[s].read (springLen * (1.0 + 0.13 * s));
                for (int k = 0; k < kDispersion; ++k)
                {
                    const double y = -disp * v + springIn[s][k] + disp * springState[s][k];
                    springIn[s][k] = v;
                    springState[s][k] = y;
                    v = y;
                }
                v = springTone_[s].lp (v);
                springLine[s].push ((float) v);
                (s % 2 == 0 ? wetL : wetR) += v * (s == 0 ? 0.8 : 0.6);
            }
            if (springs == 1) wetR = wetL;
            wetL *= lL; wetR *= lL;
        }
        else
        {
            // input diffusion
            double d = pdOut;
            if (pMix > 0.0)
            {
                const double pin = d + pFb * lateOut;
                const double sh = (1.0 - bal) * 0.5 * pitchShift (0, (float) pin, r1) + (1.0 + bal) * 0.5 * pitchShift (1, (float) pin, r2);
                d += pMix * sh;
            }
            for (auto& ap : inDiff) d = ap.process ((float) d);

            // early reflections (stereo taps), diffused
            erLine.push ((float) d);
            double er[2] = { 0, 0 };
            for (int c = 0; c < 2; ++c)
            {
                for (int k = 0; k < kTaps; ++k) er[c] += tapGain[c][k] * erLine.read (tapTime[c][k]);
                er[c] = erDiff[c][1].process (erDiff[c][0].process ((float) er[c]));
            }

            // late tail: 8 lines, Householder mix, 3-band decay per line, wall diffusion, modulation
            double outs[kLines];
            double sum = 0;
            for (int k = 0; k < kLines; ++k)
            {
                const double m = lfo[k].next (LfoShape::sine, k % 2 == 0 ? 0.0 : phOff);
                lfo[k].advance();
                outs[k] = lines[k].read (lineLen[k] + modD * (1.0 + m));
                sum += outs[k];
            }
            const double feed = d * (1.0 - lateIn * 0.5) + lateIn * 0.5 * (er[0] + er[1]);
            double lateL = 0, lateR = 0;
            for (int k = 0; k < kLines; ++k)
            {
                double v = outs[k] - (2.0 / kLines) * sum;
                const double low = lowSplit[k].lp (v);
                const double high = highSplit[k].hp (v - low);
                const double midPart = v - low - high;
                if (hold == 0) v = low * gLow[k] + midPart * gMid[k] + high * gHigh[k];
                else v = v * 0.9999;
                v = (1.0 - dens) * v + dens * wall[k].process ((float) v);
                lines[k].push ((float) (v + feed * (k % 2 == 0 ? 0.5 : -0.5)));
                (k % 2 == 0 ? lateL : lateR) += outs[k];
            }
            lateOut = 0.5 * (lateL + lateR) * 0.25;
            wetL = eL * er[0] + lL * lateL * 0.35;
            wetR = eL * er[1] + lL * lateR * 0.35;
        }

        wetL += pdMix * pdOut;
        wetR += pdMix * pdOut;
        for (int c = 0; c < 2; ++c)
        {
            double& w = c == 0 ? wetL : wetR;
            w = eqP2[c].process (eqP1[c].process (eqHigh[c].process (eqLow[c].process (w))));
        }
        const double duck = duckEnv > duckThr ? duckDepth : 1.0;
        const double mid = 0.5 * (wetL + wetR), side = 0.5 * (wetL - wetR) * sp;
        wetL = (mid + side) * duck;
        wetR = (mid - side) * duck;

        const double m = mixLevel.mix.getNextValue(), g = mixLevel.level.getNextValue();
        if (R != nullptr)
        {
            L[i] = (float) ((inL * (1.0 - m) + wetL * m) * g);
            R[i] = (float) ((inR * (1.0 - m) + wetR * m) * g);
        }
        else
            L[i] = (float) ((inL * (1.0 - m) + 0.5 * (wetL + wetR) * m) * g);
    }
}

void MultiTypeReverbProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    const auto& t = types()[(size_t) juce::jlimit (0, (int) types().size() - 1, asIndex (type))];
    mt::drawIconByKey (g, b, mt::reverbIconKey (t.name, t.spring));
}

} // namespace openguitarmultifx
